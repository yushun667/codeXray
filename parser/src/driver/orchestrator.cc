/**
 * 解析引擎 driver：协调 parse 全流程（compile_commands → scheduler → AST → DB → history）
 */

#include "driver/orchestrator.h"
#include "common/error_code.h"
#include "ast/combined/action.h"
#include "common/logger.h"
#include "common/path_util.h"
#include "compile_commands/load.h"
#include "db/connection.h"
#include "db/schema.h"
#include "db/writer/writer.h"
#include "history/history.h"
#include "incremental/incremental.h"
#include "scheduler/pool.h"
#include <sqlite3.h>
#include <mutex>
#include <set>
#include <unordered_map>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace codexray {

namespace {

std::string NowUtc() {
  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  std::tm tm_buf;
#ifdef _WIN32
  gmtime_s(&tm_buf, &t);
#else
  gmtime_r(&t, &tm_buf);
#endif
  std::ostringstream os;
  os << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
  return os.str();
}

}  // namespace

int RunParse(const ParseOptions& opts) {
  if (opts.project_root.empty()) {
    LogError("RunParse: project_root empty");
    return static_cast<int>(ExitCode::kArgError);
  }
  std::string project_root = NormalizePath(opts.project_root);
  std::string cc_path = opts.compile_commands_path.empty()
      ? (project_root + "/compile_commands.json")
      : opts.compile_commands_path;
  std::string db_path = opts.output_db.empty()
      ? (project_root + "/.codexray/codexray.db")
      : opts.output_db;

  LogInfo("RunParse: project=%s db=%s", project_root.c_str(), db_path.c_str());

  Connection conn;
  if (!conn.Open(db_path)) return static_cast<int>(ExitCode::kDbWriteFailed);
  if (!EnsureSchema(conn.Get())) return static_cast<int>(ExitCode::kDbWriteFailed);

  int64_t project_id = DBWriter(conn.Get()).EnsureProject(project_root, cc_path);
  if (project_id <= 0) {
    LogError("RunParse: EnsureProject failed");
    return static_cast<int>(ExitCode::kDbWriteFailed);
  }

  std::vector<TUEntry> all_tus = LoadCompileCommands(project_root, cc_path);
  if (all_tus.empty()) {
    LogError("RunParse: no TU from compile_commands");
    return static_cast<int>(ExitCode::kCompileCommands);
  }

  std::vector<TUEntry> tus_to_run;
  if (opts.incremental) {
    std::vector<std::string> changed = GetChangedFiles(conn.Get(), project_id);
    RemoveDataForFiles(conn.Get(), project_id, changed);
    if (changed.empty()) {
      sqlite3_stmt* check = nullptr;
      bool has_history = (sqlite3_prepare_v2(conn.Get(), "SELECT 1 FROM parsed_file WHERE project_id = ? LIMIT 1", -1, &check, nullptr) == SQLITE_OK &&
                         sqlite3_bind_int64(check, 1, project_id) == SQLITE_OK &&
                         sqlite3_step(check) == SQLITE_ROW);
      if (check) sqlite3_finalize(check);
      if (!has_history) {
        LogInfo("RunParse: incremental first run, do full parse");
        tus_to_run = all_tus;
      } else {
        LogInfo("RunParse: incremental, no changed files");
        tus_to_run.clear();
      }
    } else {
      std::set<std::string> changed_set(changed.begin(), changed.end());
      for (const TUEntry& tu : all_tus) {
        std::string src_n = NormalizePath(tu.source_file);
        if (changed_set.count(src_n) || changed_set.count(tu.source_file))
          tus_to_run.push_back(tu);
      }
    }
  } else {
    if (opts.lazy && !opts.priority_dirs.empty()) {
      std::vector<TUEntry> priority, rest;
      SplitByPriorityDirs(all_tus, project_root, opts.priority_dirs, &priority, &rest);
      tus_to_run = priority;
    } else {
      tus_to_run = all_tus;
    }
  }

  if (tus_to_run.empty()) {
    LogInfo("RunParse: no TU to run");
    return static_cast<int>(ExitCode::kSuccess);
  }

  std::string mode = opts.incremental ? "incremental" : "full";
  int64_t run_id = InsertParseRun(conn.Get(), project_id, mode);
  if (run_id <= 0) {
    LogError("RunParse: InsertParseRun failed");
    return static_cast<int>(ExitCode::kDbWriteFailed);
  }

  std::mutex collect_mutex;
  std::mutex db_mutex;
  std::unordered_map<std::string, int64_t> file_id_by_path;
  std::unordered_map<std::string, SymbolRecord> symbol_by_usr;
  std::vector<CallEdgeRecord> all_edges;
  std::vector<ClassRecord> all_classes;
  std::vector<ClassRelationRecord> all_relations;
  std::vector<GlobalVarRecord> all_global_vars;
  std::vector<DataFlowEdgeRecord> all_data_flow_edges;
  std::vector<CfgNodeRecord> all_cfg_nodes;
  std::vector<CfgEdgeRecord> all_cfg_edges;
  DBWriter writer(conn.Get());
  unsigned parallel = opts.parallel;
  if (parallel == 0) parallel = 1;

  TUProcessor processor = [&](const TUEntry& tu) {
    int64_t file_id;
    {
      std::lock_guard<std::mutex> lock(db_mutex);
      file_id = writer.EnsureFile(project_id, tu.source_file);
    }
    if (file_id <= 0) return false;
    CallGraphOutput cg_out;
    ClassRelationOutput cr_out;
    DataFlowOutput df_out;
    ControlFlowOutput cf_out;
    if (!RunAllAnalysesOnTU(tu, &cg_out, &cr_out, &df_out, &cf_out)) return false;
    for (auto& s : cg_out.symbols) {
      s.def_file_id = s.def_in_tu_file ? file_id : 0;
      s.decl_file_id = s.decl_in_tu_file ? file_id : 0;
    }
    for (auto& e : cg_out.edges) e.call_site_file_id = file_id;
    for (auto& c : cr_out.classes) {
      c.file_id = file_id;
      c.def_file_id = file_id;
    }
    for (auto& g : df_out.global_vars) {
      g.def_file_id = file_id;
      g.file_id = file_id;
    }
    for (auto& n : cf_out.nodes) n.file_id = file_id;
    std::lock_guard<std::mutex> lock(collect_mutex);
    file_id_by_path[tu.source_file] = file_id;
    for (auto& s : cg_out.symbols) {
      auto it = symbol_by_usr.find(s.usr);
      if (it == symbol_by_usr.end()) {
        symbol_by_usr[s.usr] = std::move(s);
      } else {
        SymbolRecord& e = it->second;
        if (s.def_file_id && !e.def_file_id) {
          e.def_file_id = s.def_file_id;
          e.def_line = s.def_line;
          e.def_column = s.def_column;
          e.def_line_end = s.def_line_end;
          e.def_column_end = s.def_column_end;
        }
        if (s.decl_file_id && !e.decl_file_id) {
          e.decl_file_id = s.decl_file_id;
          e.decl_line = s.decl_line;
          e.decl_column = s.decl_column;
          e.decl_line_end = s.decl_line_end;
          e.decl_column_end = s.decl_column_end;
        }
        if (e.kind == "function" && (s.kind == "method" || s.kind == "constructor" || s.kind == "destructor"))
          e.kind = s.kind;
      }
    }
    for (auto& e : cg_out.edges) all_edges.push_back(e);
    for (auto& c : cr_out.classes) all_classes.push_back(c);
    for (auto& r : cr_out.relations) all_relations.push_back(r);
    for (auto& g : df_out.global_vars) all_global_vars.push_back(g);
    for (auto& e : df_out.edges) all_data_flow_edges.push_back(e);
    size_t cfg_base = all_cfg_nodes.size();
    for (auto& n : cf_out.nodes) all_cfg_nodes.push_back(n);
    for (auto& e : cf_out.edges) {
      CfgEdgeRecord e2 = e;
      e2.from_node_index += static_cast<int>(cfg_base);
      e2.to_node_index += static_cast<int>(cfg_base);
      all_cfg_edges.push_back(e2);
    }
    return true;
  };

  const size_t total_steps = tus_to_run.size() + 1;  // +1 for DB write phase
  ProgressCallback on_progress = [&](size_t done, size_t total) {
    if (opts.progress_stdout) opts.progress_stdout(done, total_steps);
  };

  RunTUPool(tus_to_run, parallel, processor, on_progress);

  std::vector<SymbolRecord> all_symbols;
  all_symbols.reserve(symbol_by_usr.size());
  for (auto& kv : symbol_by_usr) all_symbols.push_back(std::move(kv.second));

  if (!conn.BeginTransaction()) return static_cast<int>(ExitCode::kDbWriteFailed);
  auto usr_to_id = writer.WriteSymbols(project_id, all_symbols);
  if (!writer.WriteCallEdges(project_id, all_edges, usr_to_id)) {
    conn.Rollback();
    return static_cast<int>(ExitCode::kDbWriteFailed);
  }
  auto class_usr_to_id = writer.WriteClasses(project_id, all_classes);
  if (!writer.WriteClassRelations(project_id, all_relations, class_usr_to_id)) {
    conn.Rollback();
    return static_cast<int>(ExitCode::kDbWriteFailed);
  }
  auto var_usr_to_id = writer.WriteGlobalVars(project_id, all_global_vars);
  if (!writer.WriteDataFlowEdges(project_id, all_data_flow_edges, var_usr_to_id, usr_to_id)) {
    conn.Rollback();
    return static_cast<int>(ExitCode::kDbWriteFailed);
  }
  std::vector<int64_t> cfg_node_ids = writer.WriteCfgNodes(project_id, all_cfg_nodes, usr_to_id);
  if (!writer.WriteCfgEdges(project_id, all_cfg_edges, cfg_node_ids)) {
    conn.Rollback();
    return static_cast<int>(ExitCode::kDbWriteFailed);
  }
  for (const TUEntry& tu : tus_to_run) {
    auto it = file_id_by_path.find(tu.source_file);
    if (it != file_id_by_path.end() && it->second > 0)
      writer.UpdateParsedFile(project_id, it->second, run_id, 0, "");
  }
  if (!conn.Commit()) {
    conn.Rollback();
    return static_cast<int>(ExitCode::kDbWriteFailed);
  }

  std::string finished = NowUtc();
  if (!UpdateParseRun(conn.Get(), run_id, finished,
                     static_cast<int>(tus_to_run.size()), "completed", "")) {
    LogError("RunParse: UpdateParseRun failed");
    return static_cast<int>(ExitCode::kDbWriteFailed);
  }
  if (opts.progress_stdout) opts.progress_stdout(total_steps, total_steps);
  LogInfo("RunParse: done run_id=%ld files=%zu", (long)run_id, tus_to_run.size());
  return static_cast<int>(ExitCode::kSuccess);
}

}  // namespace codexray
