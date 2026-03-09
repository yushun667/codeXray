/**
 * CodeXray 解析引擎入口
 * 子命令：parse / query / list-runs；退出码见接口约定 §4
 */

#include "cli/parse_args.h"
#include "common/error_code.h"
#include "common/path_util.h"
#include "db/connection.h"
#include "db/reader/reader.h"
#include "db/schema.h"
#include "driver/orchestrator.h"
#include "history/history.h"
#include "query/json_output.h"
#include <iostream>
#include <cstdlib>
#include <string>

namespace {

void PrintUsage(const char* prog) {
  std::cerr
      << "Usage: " << (prog ? prog : "codexray-parser")
      << " <command> [options]\n\n"
      << "Commands:\n"
      << "  parse         Parse project (compile_commands.json)\n"
      << "  query         Query call_graph | class_graph | data_flow | control_flow\n"
      << "  list-runs     List parse history\n\n"
      << "Parse options:\n"
      << "  --project <path>              Project root (required)\n"
      << "  --compile-commands <path>    Default: <project>/compile_commands.json\n"
      << "  --output-db <path>            Output SQLite DB path\n"
      << "  --parallel N                  Parallelism (default: max(1, cores-2))\n"
      << "  --lazy                        Lazy parse (priority dirs first)\n"
      << "  --priority-dirs <path1,...>   Priority dirs when --lazy\n"
      << "  --incremental                 Incremental update (changed files only)\n\n"
      << "Query options:\n"
      << "  --db <path>   Database path\n"
      << "  --type <call_graph|class_graph|data_flow|control_flow|symbol_at>\n"
      << "  --symbol <name>  [--file <path>] [--depth N]\n"
      << "  --file <path> --line <n> [--column <n>]  resolve symbol at location (or type=symbol_at)\n\n"
      << "List-runs:\n"
      << "  --db <path> [--project <path>] [--limit N]\n\n"
      << "Exit codes: 0 success, 1 arg error, 2 compile_commands, 3 Clang, 4 DB, 5 query.\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc <= 1) {
    PrintUsage(argv[0]);
    return static_cast<int>(codexray::ExitCode::kArgError);
  }
  std::string cmd = argv[1];
  if (cmd == "-h" || cmd == "--help") {
    PrintUsage(argv[0]);
    return 0;
  }
  if (cmd != "parse" && cmd != "query" && cmd != "list-runs") {
    std::cerr << "Unknown command: " << cmd << "\n";
    PrintUsage(argv[0]);
    return static_cast<int>(codexray::ExitCode::kArgError);
  }

  codexray::ParseOptions parse_opts;
  codexray::QueryOptions query_opts;
  codexray::ListRunsOptions list_opts;
  std::string error_msg;
  codexray::Subcommand sub = codexray::ParseArgs(argc, argv, &parse_opts, &query_opts, &list_opts, &error_msg);

  if (sub == codexray::Subcommand::kNone) {
    if (!error_msg.empty()) std::cerr << error_msg << "\n";
    PrintUsage(argv[0]);
    return static_cast<int>(codexray::ExitCode::kArgError);
  }

  if (sub == codexray::Subcommand::kParse) {
    parse_opts.progress_stdout = [](size_t done, size_t total) {
      size_t pct = total ? (done * 100 / total) : 0;
      std::cout << "{\"percent\":" << pct << ",\"total\":" << total << "}\n" << std::flush;
    };
    int parse_result = codexray::RunParse(parse_opts);
    if (parse_result != 0) {
      if (parse_result == static_cast<int>(codexray::ExitCode::kCompileCommands))
        std::cerr << "No TU from compile_commands.json (missing or empty).\n";
      else
        std::cerr << "Parse failed.\n";
      return parse_result;
    }
    return 0;
  }

  if (sub == codexray::Subcommand::kQuery) {
    codexray::Connection conn;
    if (!conn.Open(query_opts.db_path)) {
      std::cerr << "Cannot open DB: " << query_opts.db_path << "\n";
      return static_cast<int>(codexray::ExitCode::kQueryFailed);
    }
    if (!codexray::EnsureSchema(conn.Get())) {
      std::cerr << "Schema failed.\n";
      return static_cast<int>(codexray::ExitCode::kQueryFailed);
    }
    if (query_opts.symbol.empty() && !query_opts.file_path.empty() && query_opts.line > 0) {
      int64_t project_id = 0;
      if (!query_opts.project_root.empty())
        project_id = codexray::GetProjectId(conn.Get(), codexray::NormalizePath(query_opts.project_root));
      int64_t file_id = codexray::QueryFileIdByPath(conn.Get(), project_id, query_opts.file_path);
      if (file_id > 0) {
        std::vector<codexray::SymbolRow> syms = codexray::QuerySymbolsByFileAndLine(
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
        project_id = codexray::GetProjectId(conn.Get(), codexray::NormalizePath(query_opts.project_root));
      json = codexray::QuerySymbolAtLocationJson(conn.Get(), project_id, query_opts.file_path,
                                                 query_opts.line, query_opts.column);
    } else if (query_opts.query_type == "call_graph")
      json = codexray::QueryCallGraphJson(conn.Get(), query_opts.symbol, query_opts.file_path, query_opts.depth);
    else if (query_opts.query_type == "class_graph")
      json = codexray::QueryClassGraphJson(conn.Get(), query_opts.symbol, query_opts.file_path);
    else if (query_opts.query_type == "data_flow")
      json = codexray::QueryDataFlowJson(conn.Get(), query_opts.symbol, query_opts.file_path);
    else if (query_opts.query_type == "control_flow")
      json = codexray::QueryControlFlowJson(conn.Get(), query_opts.symbol, query_opts.file_path);
    else {
      std::cerr << "Unknown query type: " << query_opts.query_type << "\n";
      return static_cast<int>(codexray::ExitCode::kQueryFailed);
    }
    std::cout << json << "\n";
    return 0;
  }

  if (sub == codexray::Subcommand::kListRuns) {
    codexray::Connection conn;
    if (!conn.Open(list_opts.db_path)) {
      std::cerr << "Cannot open DB: " << list_opts.db_path << "\n";
      return static_cast<int>(codexray::ExitCode::kQueryFailed);
    }
    if (!codexray::EnsureSchema(conn.Get())) {
      std::cerr << "Schema failed.\n";
      return static_cast<int>(codexray::ExitCode::kQueryFailed);
    }
    int64_t project_id = 0;
    if (!list_opts.project_root.empty())
      project_id = codexray::GetProjectId(conn.Get(), codexray::NormalizePath(list_opts.project_root));
    std::string json = codexray::ListRunsJson(conn.Get(), project_id, list_opts.limit);
    std::cout << json << "\n";
    return 0;
  }

  return static_cast<int>(codexray::ExitCode::kArgError);
}
