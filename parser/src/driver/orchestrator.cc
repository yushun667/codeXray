#include "orchestrator.h"
#include "../common/error_code.h"
#include "../common/logger.h"
#include "../common/path_util.h"
#include "../compile_commands/load.h"
#include "../db/connection.h"
#include "../db/schema.h"
#include "../db/writer/writer.h"
#include "../db/reader/reader.h"
#include "../history/history.h"
#include "../incremental/incremental.h"
#include "../scheduler/pool.h"
#include "../ast/combined/action.h"
#include <filesystem>
#include <mutex>
#include <unordered_set>
#include <atomic>
#include <sys/stat.h>

namespace fs = std::filesystem;

namespace codexray {

// ─── helpers ─────────────────────────────────────────────────────────────────

static std::string ResolveDbPath(const ParseOptions& opts) {
  if (!opts.db_path.empty()) return opts.db_path;
  std::string dir = opts.project_root + "/.codexray";
  fs::create_directories(dir);
  return dir + "/codexray.db";
}

static std::string ResolveCCPath(const ParseOptions& opts) {
  if (!opts.compile_commands_path.empty()) return opts.compile_commands_path;
  return opts.project_root + "/compile_commands.json";
}

// ─── RunParse ────────────────────────────────────────────────────────────────

int RunParse(const ParseOptions& opts, ParseSummary* summary) {
  // Resolve paths
  std::string cc_path = ResolveCCPath(opts);
  std::string db_path = ResolveDbPath(opts);
  std::string proj_root = NormalizePath(opts.project_root);

  // Load compile_commands
  auto cc = LoadCompileCommands(cc_path, proj_root, opts.priority_dirs);
  if (!cc.error.empty()) {
    LogError("compile_commands: " + cc.error);
    return static_cast<int>(ExitCode::kCompileCommands);
  }

  // Determine parse mode and TU list
  bool is_incremental = opts.incremental;
  std::string mode = is_incremental ? "incremental" : "full";

  // Open / init DB
  Connection conn;
  if (!conn.Open(db_path)) return static_cast<int>(ExitCode::kDbWriteFailed);
  if (!EnsureSchema(conn.Get())) return static_cast<int>(ExitCode::kDbWriteFailed);

  int64_t project_id = EnsureProjectId(conn.Get(), proj_root, cc_path);

  // Collect TU candidates: lazy mode restricts to priority dirs
  std::vector<TUEntry> candidate_tus;
  if (opts.lazy && !opts.priority_dirs.empty()) {
    candidate_tus = cc.priority;
  } else {
    for (auto& t : cc.priority)  candidate_tus.push_back(t);
    for (auto& t : cc.remainder) candidate_tus.push_back(t);
  }

  std::vector<TUEntry> tus_to_parse;

  if (is_incremental) {
    std::vector<std::string> paths;
    paths.reserve(candidate_tus.size());
    for (const auto& t : candidate_tus) paths.push_back(t.source_file);
    auto changed = GetChangedFiles(conn.Get(), project_id, paths);
    // Build a set for quick lookup
    std::unordered_set<std::string> changed_set;
    for (const auto& cf : changed) changed_set.insert(cf.path);
    for (const auto& tu : candidate_tus) {
      if (changed_set.count(tu.source_file)) tus_to_parse.push_back(tu);
    }
    if (tus_to_parse.empty()) {
      LogInfo("Incremental: no changed files, nothing to parse");
      if (summary) {
        summary->mode        = mode;
        summary->files_parsed = 0;
        summary->files_failed = 0;
        summary->symbols_count = 0;
      }
      return 0;
    }
    // Delete old data for changed files
    DbWriter writer(conn.Get(), project_id);
    writer.SetDbDir(fs::path(db_path).parent_path().string());
    conn.BeginTransaction();
    for (const auto& cf : changed) {
      int64_t fid = cf.file_id;
      if (fid == 0) fid = QueryFileIdByPath(conn.Get(), project_id, cf.path);
      if (fid > 0) writer.DeleteDataForFile(fid);
    }
    conn.Commit();
  } else {
    tus_to_parse = candidate_tus;
  }

  // Create parse_run record
  int64_t run_id = InsertParseRun(conn.Get(), project_id, mode);

  // Schedule parallel analysis with incremental DB writes
  // 使用 on_result 回调在每个 TU 完成后立即写入数据库，
  // 避免将所有 TU 的结果（可能数 GB）累积在内存中。
  SchedulerConfig sched_cfg;
  sched_cfg.parallel = opts.parallel > 0 ? opts.parallel : DefaultParallelism();

  auto run_one = [](const TUEntry& tu, CombinedOutput& out) -> bool {
    return combined::RunAllAnalysesOnTU(tu, out);
  };

  DbWriter writer(conn.Get(), project_id);
  // 设置 db_dir，供 WriteCfg 写 protobuf 文件时确定 cfg/ 子目录位置
  writer.SetDbDir(fs::path(db_path).parent_path().string());
  std::atomic<int> write_errors{0};

  // on_result 在持有 result_mu 的情况下串行调用，可安全写 DB
  auto on_result = [&](const TUEntry& tu, CombinedOutput& out, bool ok) {
    if (!ok) return;
    // 每个 TU 单独事务写入（合并 WriteAll + UpdateParsedFile 为一个事务，
    // 减少 COMMIT 次数），失败不影响其他 TU
    conn.BeginTransaction();
    if (!writer.WriteAll(out)) {
      conn.Rollback();
      write_errors.fetch_add(1);
      LogWarn("DB write failed for TU: " + tu.source_file);
      return;
    }
    // 更新 parsed_file 记录（同一事务内）
    // 使用子进程已计算的 mtime/hash，避免父进程重复读文件
    int64_t fid = writer.EnsureFile(tu.source_file);
    int64_t mtime = out.source_file_mtime > 0
                        ? out.source_file_mtime
                        : GetFileMtime(tu.source_file);
    const std::string& hash = !out.source_file_hash.empty()
                                  ? out.source_file_hash
                                  : (out.source_file_hash = ComputeFileHash(tu.source_file));
    writer.UpdateParsedFile(fid, run_id, mtime, hash);
    conn.Commit();
  };

  auto result = RunScheduled(tus_to_parse, sched_cfg, run_one,
                             opts.progress_stdout, on_result);

  bool write_ok = (write_errors.load() == 0);

  // Count symbols
  int64_t sym_count = 0;
  {
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(conn.Get(), "SELECT COUNT(*) FROM symbol", -1, &s, nullptr);
    if (sqlite3_step(s) == SQLITE_ROW) sym_count = sqlite3_column_int64(s, 0);
    sqlite3_finalize(s);
  }

  int files_parsed = static_cast<int>(tus_to_parse.size()) - result.failed_count;
  UpdateParseRun(conn.Get(), run_id,
                 write_ok ? "completed" : "failed",
                 files_parsed, result.failed_count,
                 write_ok ? "" : "DB write error");

  if (summary) {
    summary->run_id        = run_id;
    summary->mode          = mode;
    summary->files_parsed  = files_parsed;
    summary->files_failed  = result.failed_count;
    summary->symbols_count = sym_count;
  }

  return write_ok ? 0 : static_cast<int>(ExitCode::kDbWriteFailed);
}

// ─── ParseOnDemandForQuery ────────────────────────────────────────────────────

int ParseOnDemandForQuery(const std::string& project_root,
                          const std::string& db_path,
                          const std::string& cc_path,
                          const std::vector<std::string>& file_paths,
                          unsigned parallel,
                          const std::vector<std::string>& priority_dirs) {
  if (file_paths.empty()) return 0;

  // Load compile_commands to find TUEntry for each requested file
  auto cc = LoadCompileCommands(cc_path, project_root, priority_dirs);
  if (!cc.error.empty()) {
    LogError("On-demand parse: " + cc.error);
    return static_cast<int>(ExitCode::kCompileCommands);
  }

  // Build lookup map: source_file → TUEntry
  std::unordered_map<std::string, TUEntry> path_to_tu;
  for (const auto& t : cc.priority)  path_to_tu[t.source_file] = t;
  for (const auto& t : cc.remainder) path_to_tu[t.source_file] = t;

  std::vector<TUEntry> tus;
  for (const auto& fp : file_paths) {
    auto it = path_to_tu.find(fp);
    if (it != path_to_tu.end()) tus.push_back(it->second);
    else LogWarn("On-demand: no compile command for " + fp);
  }
  if (tus.empty()) return 0;

  Connection conn;
  if (!conn.Open(db_path)) return static_cast<int>(ExitCode::kDbWriteFailed);
  if (!EnsureSchema(conn.Get())) return static_cast<int>(ExitCode::kDbWriteFailed);
  int64_t project_id = EnsureProjectId(conn.Get(), NormalizePath(project_root), cc_path);

  int64_t run_id = InsertParseRun(conn.Get(), project_id, "on_demand");

  SchedulerConfig sched_cfg;
  sched_cfg.parallel = parallel > 0 ? parallel : DefaultParallelism();

  auto run_one = [](const TUEntry& tu, CombinedOutput& out) -> bool {
    return combined::RunAllAnalysesOnTU(tu, out);
  };

  DbWriter writer(conn.Get(), project_id);
  writer.SetDbDir(fs::path(db_path).parent_path().string());
  std::atomic<int> write_errors{0};

  auto on_result = [&](const TUEntry& tu, CombinedOutput& out, bool ok) {
    if (!ok) return;
    conn.BeginTransaction();
    if (!writer.WriteAll(out)) {
      conn.Rollback();
      write_errors.fetch_add(1);
      return;
    }
    // 同一事务内更新 parsed_file 记录（优先使用子进程已计算的值）
    int64_t fid = writer.EnsureFile(tu.source_file);
    int64_t mtime = out.source_file_mtime > 0
                        ? out.source_file_mtime
                        : GetFileMtime(tu.source_file);
    const std::string& hash = !out.source_file_hash.empty()
                                  ? out.source_file_hash
                                  : (out.source_file_hash = ComputeFileHash(tu.source_file));
    writer.UpdateParsedFile(fid, run_id, mtime, hash);
    conn.Commit();
  };

  auto result = RunScheduled(tus, sched_cfg, run_one, nullptr, on_result);
  bool write_ok = (write_errors.load() == 0);

  int files_parsed = static_cast<int>(tus.size()) - result.failed_count;
  UpdateParseRun(conn.Get(), run_id,
                 write_ok ? "completed" : "failed",
                 files_parsed, result.failed_count);

  return write_ok ? 0 : static_cast<int>(ExitCode::kDbWriteFailed);
}

}  // namespace codexray
