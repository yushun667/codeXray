/**
 * CodeXray 解析引擎入口
 * 子命令：parse / query / list-runs；退出码见接口约定 §4
 * 隐藏子命令：parse-tu（供 pool.cc fork+exec 调用，不面向用户）
 */

#include "cli/parse_args.h"
#include "common/error_code.h"
#include "common/logger.h"
#include "common/path_util.h"
#include "db/connection.h"
#include "db/reader/reader.h"
#include "db/schema.h"
#include "driver/orchestrator.h"
#include "history/history.h"
#include "query/json_output.h"
#include "ast/combined/action.h"
#include "common/analysis_output.h"
#include "compile_commands/load.h"
#include "incremental/incremental.h"
#include <filesystem>
#include <iostream>
#include <cstdlib>
#include <string>
#include <nlohmann/json.hpp>

namespace {

void PrintUsage(const char* prog) {
  std::cerr
      << "Usage: " << (prog ? prog : "codexray-parser")
      << " <command> [options]\n\n"
      << "Commands:\n"
      << "  parse         Parse project (compile_commands.json)\n"
      << "  query         Query call_graph | class_graph | data_flow | control_flow | symbol_at\n"
      << "  list-runs     List parse history\n\n"
      << "Parse options:\n"
      << "  --project <path>              Project root (required)\n"
      << "  --compile-commands <path>     Default: <project>/compile_commands.json\n"
      << "  --output-db <path>            Output SQLite DB path\n"
      << "  --parallel N  (or --jobs N)   Parallelism (default: max(1, cores-2))\n"
      << "  --lazy                        Lazy parse (priority dirs first)\n"
      << "  --priority-dirs <p1,p2,...>   Priority dirs when --lazy\n"
      << "  --incremental                 Incremental update (changed files only)\n"
      << "  --verbose                     Verbose logs to stderr\n\n"
      << "Query options:\n"
      << "  --db <path>   Database path\n"
      << "  --project <path>  Project root (optional, for lazy parse)\n"
      << "  --type <call_graph|class_graph|data_flow|control_flow|symbol_at>\n"
      << "  --symbol <name_or_usr>  [--file <path>] [--depth N]\n"
      << "  --file <path> --line <n> [--column <n>]  resolve symbol at location\n\n"
      << "List-runs:\n"
      << "  --db <path> [--project <path>] [--limit N]\n\n"
      << "Exit codes: 0 ok, 1 arg error, 2 compile_commands, 3 clang, 4 db, 5 query.\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc <= 1) {
    PrintUsage(argv[0]);
    return static_cast<int>(codexray::ExitCode::kArgError);
  }
  std::string cmd = argv[1];

  // ── parse-tu（隐藏子命令，由 pool.cc fork+exec 调用）──────────────────────
  // 从 stdin 读取 TUEntry JSON，分析后将 CombinedOutput JSON 写入 stdout。
  // 格式：stdin = 单行 JSON (TUEntry)；stdout = {"ok":bool,"output":{...}}
  if (cmd == "parse-tu") {
    codexray::LogInit("", false);
    std::string line;
    if (!std::getline(std::cin, line) || line.empty()) {
      std::cout << "{\"ok\":false,\"error\":\"no input\"}" << std::flush;
      return 1;
    }
    try {
      nlohmann::json jtu = nlohmann::json::parse(line);
      codexray::TUEntry tu = codexray::DeserializeTUEntry(jtu);
      codexray::CombinedOutput out;
      bool ok = codexray::combined::RunAllAnalysesOnTU(tu, out);
      // 在子进程中计算源文件 mtime 和 hash，避免父进程重复读文件
      out.source_file_mtime = codexray::GetFileMtime(tu.source_file);
      out.source_file_hash  = codexray::ComputeFileHash(tu.source_file);
      nlohmann::json result;
      result["ok"] = ok;
      if (ok) {
        result["output"] = codexray::SerializeCombinedOutput(out);
      } else {
        result["error"] = "analysis failed";
      }
      std::cout << result.dump() << std::flush;
      return ok ? 0 : 1;
    } catch (const std::exception& e) {
      nlohmann::json err;
      err["ok"] = false;
      err["error"] = std::string("exception: ") + e.what();
      std::cout << err.dump() << std::flush;
      return 1;
    }
  }

  if (cmd == "-h" || cmd == "--help") {
    PrintUsage(argv[0]);
    return 0;
  }

  codexray::ParseOptions    parse_opts;
  codexray::QueryOptions    query_opts;
  codexray::ListRunsOptions list_opts;
  std::string error_msg;
  codexray::Subcommand sub = codexray::ParseArgs(
      argc, argv, &parse_opts, &query_opts, &list_opts, &error_msg);

  if (sub == codexray::Subcommand::kNone) {
    if (!error_msg.empty()) std::cerr << error_msg << "\n";
    PrintUsage(argv[0]);
    return static_cast<int>(codexray::ExitCode::kArgError);
  }

  // Init logging
  bool verbose = (sub == codexray::Subcommand::kParse   && parse_opts.verbose)
              || (sub == codexray::Subcommand::kQuery    && query_opts.verbose)
              || (sub == codexray::Subcommand::kListRuns && list_opts.verbose);
  codexray::LogInit("", verbose);

  // ── parse ──────────────────────────────────────────────────────────────────
  if (sub == codexray::Subcommand::kParse) {
    parse_opts.progress_stdout = [](size_t done, size_t total,
                                    const std::string& current_file) {
      size_t pct = total ? (done * 100 / total) : 0;
      std::string escaped;
      for (char c : current_file) {
        if      (c == '\\') escaped += "\\\\";
        else if (c == '"')  escaped += "\\\"";
        else if (c == '\n') escaped += "\\n";
        else if (c == '\r') escaped += "\\r";
        else                escaped += c;
      }
      std::cout << "{\"progress\":" << pct
                << ",\"current_file\":\"" << escaped << "\"}\n"
                << std::flush;
    };

    codexray::ParseSummary summary;
    int rc = codexray::RunParse(parse_opts, &summary);
    if (rc != 0) {
      if (rc == static_cast<int>(codexray::ExitCode::kCompileCommands))
        std::cerr << "No TU from compile_commands.json (missing or empty).\n";
      else if (rc == static_cast<int>(codexray::ExitCode::kDbWriteFailed))
        std::cerr << "Database write failed.\n";
      else
        std::cerr << "Parse failed (rc=" << rc << ").\n";
      return rc;
    }
    std::cout << "{\"status\":\"ok\",\"run_id\":" << summary.run_id
              << ",\"mode\":\"" << summary.mode << "\""
              << ",\"files_parsed\":" << summary.files_parsed
              << ",\"files_failed\":" << summary.files_failed
              << ",\"symbols\":" << summary.symbols_count << "}\n"
              << std::flush;
    return 0;
  }

  // ── query ──────────────────────────────────────────────────────────────────
  if (sub == codexray::Subcommand::kQuery) {
    codexray::Connection conn;
    if (!conn.Open(query_opts.db_path)) {
      std::cerr << "Cannot open DB: " << query_opts.db_path << "\n";
      return static_cast<int>(codexray::ExitCode::kQueryFailed);
    }
    if (!codexray::EnsureSchema(conn.Get())) {
      std::cerr << "Schema init failed.\n";
      return static_cast<int>(codexray::ExitCode::kQueryFailed);
    }

    // Lazy parse: if target file not yet parsed, trigger on-demand parse
    if (query_opts.lazy && !query_opts.project_root.empty() &&
        !query_opts.file_path.empty()) {
      std::string abs_path = codexray::MakeAbsolute(
          query_opts.project_root, query_opts.file_path);
      int64_t project_id = codexray::GetProjectId(
          conn.Get(), codexray::NormalizePath(query_opts.project_root));
      int64_t file_id = codexray::QueryFileIdByPath(conn.Get(), project_id, abs_path);
      if (file_id == 0 || !codexray::IsFileParsed(conn.Get(), project_id, file_id)) {
        // Use stored cc_path from project record; fall back to default
      std::string cc_path;
      {
        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(conn.Get(),
            "SELECT compile_commands_path FROM project WHERE id=?",
            -1, &s, nullptr);
        sqlite3_bind_int64(s, 1, project_id);
        if (sqlite3_step(s) == SQLITE_ROW) {
          const char* p = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
          if (p) cc_path = p;
        }
        sqlite3_finalize(s);
      }
      if (cc_path.empty())
        cc_path = query_opts.project_root + "/compile_commands.json";
        unsigned par = query_opts.parallel > 0 ? query_opts.parallel : 1;
        int pr = codexray::ParseOnDemandForQuery(
            query_opts.project_root, query_opts.db_path, cc_path,
            std::vector<std::string>{abs_path}, par, query_opts.priority_dirs);
        if (pr != 0) {
          std::cerr << "On-demand parse failed.\n";
          return static_cast<int>(codexray::ExitCode::kQueryFailed);
        }
      }
    }

    // Resolve symbol from file+line if --symbol not given
    if (query_opts.symbol.empty() && !query_opts.file_path.empty() &&
        query_opts.line > 0) {
      int64_t project_id = 0;
      if (!query_opts.project_root.empty())
        project_id = codexray::GetProjectId(
            conn.Get(), codexray::NormalizePath(query_opts.project_root));
      int64_t file_id = codexray::QueryFileIdByPath(
          conn.Get(), project_id, query_opts.file_path);
      if (file_id > 0) {
        auto syms = codexray::QuerySymbolsByFileAndLine(
            conn.Get(), file_id, query_opts.line, query_opts.column);
        if (!syms.empty()) query_opts.symbol = syms[0].usr;
      }
    }

    std::string json;
    if (query_opts.query_type == "symbol_at") {
      if (query_opts.file_path.empty() || query_opts.line <= 0) {
        std::cerr << "symbol_at requires --file and --line\n";
        return static_cast<int>(codexray::ExitCode::kQueryFailed);
      }
      int64_t project_id = 0;
      if (!query_opts.project_root.empty())
        project_id = codexray::GetProjectId(
            conn.Get(), codexray::NormalizePath(query_opts.project_root));
      json = codexray::QuerySymbolAtLocationJson(
          conn.Get(), project_id, query_opts.file_path,
          query_opts.line, query_opts.column);
    }
    else if (query_opts.query_type == "call_graph")
      json = codexray::QueryCallGraphJson(
          conn.Get(), query_opts.symbol, query_opts.file_path, query_opts.depth);
    else if (query_opts.query_type == "class_graph")
      json = codexray::QueryClassGraphJson(
          conn.Get(), query_opts.symbol, query_opts.file_path);
    else if (query_opts.query_type == "data_flow")
      json = codexray::QueryDataFlowJson(
          conn.Get(), query_opts.symbol, query_opts.file_path);
    else if (query_opts.query_type == "control_flow") {
      // db_dir：db 文件所在目录，用于定位 cfg/ pb 文件
      std::string db_dir = std::filesystem::path(query_opts.db_path).parent_path().string();
      json = codexray::QueryControlFlowJson(
          conn.Get(), db_dir, query_opts.symbol, query_opts.file_path);
    }
    else {
      std::cerr << "Unknown query type: " << query_opts.query_type << "\n";
      return static_cast<int>(codexray::ExitCode::kQueryFailed);
    }
    std::cout << json << "\n";
    return 0;
  }

  // ── list-runs ──────────────────────────────────────────────────────────────
  if (sub == codexray::Subcommand::kListRuns) {
    codexray::Connection conn;
    if (!conn.Open(list_opts.db_path)) {
      std::cerr << "Cannot open DB: " << list_opts.db_path << "\n";
      return static_cast<int>(codexray::ExitCode::kQueryFailed);
    }
    if (!codexray::EnsureSchema(conn.Get())) {
      std::cerr << "Schema init failed.\n";
      return static_cast<int>(codexray::ExitCode::kQueryFailed);
    }
    int64_t project_id = 0;
    if (!list_opts.project_root.empty())
      project_id = codexray::GetProjectId(
          conn.Get(), codexray::NormalizePath(list_opts.project_root));
    std::string json = codexray::ListRunsJson(conn.Get(), project_id, list_opts.limit);
    std::cout << json << "\n";
    return 0;
  }

  return static_cast<int>(codexray::ExitCode::kArgError);
}
