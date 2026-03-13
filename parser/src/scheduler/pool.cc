/**
 * 线程池调度器实现。
 *
 * 新架构（默认）：Pre-fork Worker Pool
 *   - 启动前 fork+exec N 个长驻 "parse-tu-worker" 子进程
 *   - 每线程独占一个 worker，通过 Protobuf 二进制帧通信
 *   - Worker crash 时自动 fork 替代，保持故障隔离
 *
 * 旧架构（CODEXRAY_LEGACY_FORK=1）：
 *   - 每个 TU fork+exec 一个 "parse-tu" 子进程
 *   - 通过 JSON 通信（nlohmann::json）
 */
#include "pool.h"
#include "ipc_proto.h"
#include "../common/logger.h"
#include "../common/clang_include_detector.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <string>
#include <sstream>
#include <nlohmann/json.hpp>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>
#include <chrono>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace codexray {

/// 返回默认并行度：max(1, cores-2)
unsigned DefaultParallelism() {
  unsigned cores = std::thread::hardware_concurrency();
  if (cores <= 2) return 1;
  return cores - 2;
}

// ─── TUEntry JSON 序列化/反序列化（供 parse-tu debug 子命令使用）──────────────

nlohmann::json SerializeTUEntry(const TUEntry& tu) {
  nlohmann::json j;
  j["source_file"] = tu.source_file;
  j["directory"]   = tu.directory;
  j["arguments"]   = tu.arguments;
  return j;
}

TUEntry DeserializeTUEntry(const nlohmann::json& j) {
  TUEntry tu;
  if (j.contains("source_file") && j["source_file"].is_string())
    tu.source_file = j["source_file"].get<std::string>();
  if (j.contains("directory") && j["directory"].is_string())
    tu.directory = j["directory"].get<std::string>();
  if (j.contains("arguments") && j["arguments"].is_array()) {
    for (const auto& a : j["arguments"])
      if (a.is_string()) tu.arguments.push_back(a.get<std::string>());
  }
  return tu;
}

// ─── CombinedOutput JSON 序列化/反序列化（供 parse-tu debug 子命令使用）──────

nlohmann::json SerializeCombinedOutput(const CombinedOutput& o) {
  using json = nlohmann::json;
  json j;

  auto& syms = j["symbols"] = json::array();
  for (const auto& r : o.symbols) {
    syms.push_back({
      {"usr", r.usr}, {"name", r.name}, {"qualified_name", r.qualified_name},
      {"kind", r.kind}, {"def_file_path", r.def_file_path},
      {"def_file_id", r.def_file_id}, {"def_line", r.def_line},
      {"def_column", r.def_column}, {"def_line_end", r.def_line_end},
      {"def_col_end", r.def_col_end}, {"decl_file_path", r.decl_file_path},
      {"decl_file_id", r.decl_file_id}, {"decl_line", r.decl_line},
      {"decl_column", r.decl_column}, {"decl_line_end", r.decl_line_end},
      {"decl_col_end", r.decl_col_end},
    });
  }

  auto& edges = j["call_edges"] = json::array();
  for (const auto& r : o.call_edges) {
    edges.push_back({
      {"caller_usr", r.caller_usr}, {"callee_usr", r.callee_usr},
      {"edge_type", r.edge_type}, {"call_file_path", r.call_file_path},
      {"call_file_id", r.call_file_id}, {"call_line", r.call_line},
      {"call_column", r.call_column},
    });
  }

  auto& classes = j["classes"] = json::array();
  for (const auto& r : o.classes) {
    classes.push_back({
      {"usr", r.usr}, {"name", r.name}, {"qualified_name", r.qualified_name},
      {"def_file_path", r.def_file_path}, {"def_file_id", r.def_file_id},
      {"def_line", r.def_line}, {"def_column", r.def_column},
      {"def_line_end", r.def_line_end}, {"def_col_end", r.def_col_end},
    });
  }

  auto& crels = j["class_relations"] = json::array();
  for (const auto& r : o.class_relations) {
    crels.push_back({{"parent_usr", r.parent_usr}, {"child_usr", r.child_usr},
                     {"relation_type", r.relation_type}});
  }

  auto& cmems = j["class_members"] = json::array();
  for (const auto& r : o.class_members) {
    cmems.push_back({{"class_usr", r.class_usr}, {"member_usr", r.member_usr},
                     {"member_name", r.member_name}, {"member_type_str", r.member_type_str}});
  }

  auto& gvars = j["global_vars"] = json::array();
  for (const auto& r : o.global_vars) {
    gvars.push_back({
      {"usr", r.usr}, {"name", r.name}, {"qualified_name", r.qualified_name},
      {"def_file_path", r.def_file_path}, {"def_file_id", r.def_file_id},
      {"def_line", r.def_line}, {"def_column", r.def_column},
      {"def_line_end", r.def_line_end}, {"def_col_end", r.def_col_end},
    });
  }

  auto& dfedges = j["data_flow_edges"] = json::array();
  for (const auto& r : o.data_flow_edges) {
    dfedges.push_back({
      {"var_usr", r.var_usr}, {"accessor_usr", r.accessor_usr},
      {"access_type", r.access_type}, {"access_file_path", r.access_file_path},
      {"access_file_id", r.access_file_id}, {"access_line", r.access_line},
      {"access_column", r.access_column},
    });
  }

  auto& cnodes = j["cfg_nodes"] = json::array();
  for (const auto& r : o.cfg_nodes) {
    cnodes.push_back({
      {"function_usr", r.function_usr}, {"block_id", r.block_id},
      {"file_path", r.file_path}, {"file_id", r.file_id},
      {"begin_line", r.begin_line}, {"begin_col", r.begin_col},
      {"end_line", r.end_line}, {"end_col", r.end_col}, {"label", r.label},
    });
  }

  auto& cedges = j["cfg_edges"] = json::array();
  for (const auto& r : o.cfg_edges) {
    cedges.push_back({{"function_usr", r.function_usr}, {"from_block", r.from_block},
                      {"to_block", r.to_block}, {"edge_type", r.edge_type}});
  }

  j["referenced_files"] = o.referenced_files;
  j["source_file_mtime"] = o.source_file_mtime;
  j["source_file_hash"]  = o.source_file_hash;
  return j;
}

void DeserializeCombinedOutput(const nlohmann::json& j, CombinedOutput& o) {
  auto sv = [](const nlohmann::json& x, const char* k) -> std::string {
    return x.contains(k) && x[k].is_string() ? x[k].get<std::string>() : "";
  };
  auto iv = [](const nlohmann::json& x, const char* k) -> int64_t {
    return x.contains(k) && x[k].is_number() ? x[k].get<int64_t>() : 0;
  };

  if (j.contains("symbols") && j["symbols"].is_array()) {
    for (const auto& x : j["symbols"]) {
      SymbolRow r;
      r.usr = sv(x,"usr"); r.name = sv(x,"name"); r.qualified_name = sv(x,"qualified_name");
      r.kind = sv(x,"kind"); r.def_file_path = sv(x,"def_file_path");
      r.def_file_id = iv(x,"def_file_id"); r.def_line = (int)iv(x,"def_line");
      r.def_column = (int)iv(x,"def_column"); r.def_line_end = (int)iv(x,"def_line_end");
      r.def_col_end = (int)iv(x,"def_col_end"); r.decl_file_path = sv(x,"decl_file_path");
      r.decl_file_id = iv(x,"decl_file_id"); r.decl_line = (int)iv(x,"decl_line");
      r.decl_column = (int)iv(x,"decl_column"); r.decl_line_end = (int)iv(x,"decl_line_end");
      r.decl_col_end = (int)iv(x,"decl_col_end");
      o.symbols.push_back(std::move(r));
    }
  }
  if (j.contains("call_edges") && j["call_edges"].is_array()) {
    for (const auto& x : j["call_edges"]) {
      CallEdgeRow r;
      r.caller_usr = sv(x,"caller_usr"); r.callee_usr = sv(x,"callee_usr");
      r.edge_type = sv(x,"edge_type"); r.call_file_path = sv(x,"call_file_path");
      r.call_file_id = iv(x,"call_file_id"); r.call_line = (int)iv(x,"call_line");
      r.call_column = (int)iv(x,"call_column");
      o.call_edges.push_back(std::move(r));
    }
  }
  if (j.contains("classes") && j["classes"].is_array()) {
    for (const auto& x : j["classes"]) {
      ClassRow r;
      r.usr = sv(x,"usr"); r.name = sv(x,"name"); r.qualified_name = sv(x,"qualified_name");
      r.def_file_path = sv(x,"def_file_path"); r.def_file_id = iv(x,"def_file_id");
      r.def_line = (int)iv(x,"def_line"); r.def_column = (int)iv(x,"def_column");
      r.def_line_end = (int)iv(x,"def_line_end"); r.def_col_end = (int)iv(x,"def_col_end");
      o.classes.push_back(std::move(r));
    }
  }
  if (j.contains("class_relations") && j["class_relations"].is_array()) {
    for (const auto& x : j["class_relations"]) {
      ClassRelationRow r;
      r.parent_usr = sv(x,"parent_usr"); r.child_usr = sv(x,"child_usr");
      r.relation_type = sv(x,"relation_type");
      o.class_relations.push_back(std::move(r));
    }
  }
  if (j.contains("class_members") && j["class_members"].is_array()) {
    for (const auto& x : j["class_members"]) {
      ClassMemberRow r;
      r.class_usr = sv(x,"class_usr"); r.member_usr = sv(x,"member_usr");
      r.member_name = sv(x,"member_name"); r.member_type_str = sv(x,"member_type_str");
      o.class_members.push_back(std::move(r));
    }
  }
  if (j.contains("global_vars") && j["global_vars"].is_array()) {
    for (const auto& x : j["global_vars"]) {
      GlobalVarRow r;
      r.usr = sv(x,"usr"); r.name = sv(x,"name"); r.qualified_name = sv(x,"qualified_name");
      r.def_file_path = sv(x,"def_file_path"); r.def_file_id = iv(x,"def_file_id");
      r.def_line = (int)iv(x,"def_line"); r.def_column = (int)iv(x,"def_column");
      r.def_line_end = (int)iv(x,"def_line_end"); r.def_col_end = (int)iv(x,"def_col_end");
      o.global_vars.push_back(std::move(r));
    }
  }
  if (j.contains("data_flow_edges") && j["data_flow_edges"].is_array()) {
    for (const auto& x : j["data_flow_edges"]) {
      DataFlowEdgeRow r;
      r.var_usr = sv(x,"var_usr"); r.accessor_usr = sv(x,"accessor_usr");
      r.access_type = sv(x,"access_type"); r.access_file_path = sv(x,"access_file_path");
      r.access_file_id = iv(x,"access_file_id"); r.access_line = (int)iv(x,"access_line");
      r.access_column = (int)iv(x,"access_column");
      o.data_flow_edges.push_back(std::move(r));
    }
  }
  if (j.contains("cfg_nodes") && j["cfg_nodes"].is_array()) {
    for (const auto& x : j["cfg_nodes"]) {
      CfgNodeRow r;
      r.function_usr = sv(x,"function_usr"); r.block_id = (int)iv(x,"block_id");
      r.file_path = sv(x,"file_path"); r.file_id = iv(x,"file_id");
      r.begin_line = (int)iv(x,"begin_line"); r.begin_col = (int)iv(x,"begin_col");
      r.end_line = (int)iv(x,"end_line"); r.end_col = (int)iv(x,"end_col");
      r.label = sv(x,"label");
      o.cfg_nodes.push_back(std::move(r));
    }
  }
  if (j.contains("cfg_edges") && j["cfg_edges"].is_array()) {
    for (const auto& x : j["cfg_edges"]) {
      CfgEdgeRow r;
      r.function_usr = sv(x,"function_usr"); r.from_block = (int)iv(x,"from_block");
      r.to_block = (int)iv(x,"to_block"); r.edge_type = sv(x,"edge_type");
      o.cfg_edges.push_back(std::move(r));
    }
  }
  if (j.contains("referenced_files") && j["referenced_files"].is_array()) {
    for (const auto& x : j["referenced_files"])
      if (x.is_string()) o.referenced_files.push_back(x.get<std::string>());
  }
  if (j.contains("source_file_mtime") && j["source_file_mtime"].is_number())
    o.source_file_mtime = j["source_file_mtime"].get<int64_t>();
  if (j.contains("source_file_hash") && j["source_file_hash"].is_string())
    o.source_file_hash = j["source_file_hash"].get<std::string>();
}

// ─── 获取自身可执行文件路径 ──────────────────────────────────────────────────

static std::string GetSelfPath() {
  char buf[4096] = {};
#if defined(__APPLE__)
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) == 0) return buf;
#elif defined(__linux__)
  ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n > 0) { buf[n] = '\0'; return buf; }
#endif
  return {};
}

// ═══════════════════════════════════════════════════════════════════════════════
// 旧架构：fork+exec per TU + JSON IPC（CODEXRAY_LEGACY_FORK=1 时使用）
// ═══════════════════════════════════════════════════════════════════════════════

/// fork+exec 隔离运行单个 TU（旧模式，每 TU 一个进程）
/// 使用 JSON 通信，通过 parse-tu 子命令
static bool RunOneTUForked(
    const TUEntry& tu,
    CombinedOutput& out,
    const std::string& self_path,
    int timeout_secs = 120) {

  int stdin_pipe[2];   // 父→子
  int stdout_pipe[2];  // 子→父

  if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) {
    LogError("pipe() failed for " + tu.source_file);
    return false;
  }

  pid_t pid = fork();
  if (pid < 0) {
    LogError("fork() failed for " + tu.source_file);
    close(stdin_pipe[0]);  close(stdin_pipe[1]);
    close(stdout_pipe[0]); close(stdout_pipe[1]);
    return false;
  }

  if (pid == 0) {
    // ── 子进程 ──────────────────────────────────────────────────────────
    dup2(stdin_pipe[0],  STDIN_FILENO);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    close(stdin_pipe[0]);  close(stdin_pipe[1]);
    close(stdout_pipe[0]); close(stdout_pipe[1]);

    execl(self_path.c_str(), self_path.c_str(), "parse-tu", nullptr);
    _exit(127);
  }

  // ── 父进程 ────────────────────────────────────────────────────────────
  close(stdin_pipe[0]);
  close(stdout_pipe[1]);

  // 写入 TUEntry JSON 到子进程 stdin
  {
    nlohmann::json tu_j = SerializeTUEntry(tu);
    std::string s = tu_j.dump() + "\n";
    size_t written = 0;
    while (written < s.size()) {
      ssize_t n = write(stdin_pipe[1], s.data() + written, s.size() - written);
      if (n <= 0) break;
      written += n;
    }
    close(stdin_pipe[1]);
  }

  // 读取子进程 stdout（非阻塞 + 超时轮询）
  std::string result_buf;
  bool timed_out = false;
  {
    int flags = fcntl(stdout_pipe[0], F_GETFL, 0);
    fcntl(stdout_pipe[0], F_SETFL, flags | O_NONBLOCK);

    const int poll_us = 50000;  // 50ms
    const int max_polls = (timeout_secs * 1000000) / poll_us;
    int polls = 0;
    bool exited = false;
    int child_status = 0;
    char buf[65536];

    while (polls < max_polls) {
      ssize_t n;
      while ((n = read(stdout_pipe[0], buf, sizeof(buf))) > 0)
        result_buf.append(buf, n);

      pid_t r = waitpid(pid, &child_status, WNOHANG);
      if (r == pid) { exited = true; break; }
      if (r < 0)    { exited = true; break; }

      usleep(poll_us);
      ++polls;
    }

    if (!exited) {
      timed_out = true;
      kill(pid, SIGKILL);
      waitpid(pid, &child_status, 0);
    } else {
      ssize_t n;
      while ((n = read(stdout_pipe[0], buf, sizeof(buf))) > 0)
        result_buf.append(buf, n);
    }
  }
  close(stdout_pipe[0]);

  if (timed_out) {
    LogWarn("TU parse timed out (" + std::to_string(timeout_secs) + "s): " + tu.source_file);
    return false;
  }
  if (result_buf.empty()) {
    LogWarn("TU parse produced no output: " + tu.source_file);
    return false;
  }

  try {
    nlohmann::json j = nlohmann::json::parse(result_buf);
    if (!j.contains("ok") || !j["ok"].get<bool>()) {
      std::string err = j.contains("error") ? j["error"].get<std::string>() : "unknown";
      LogWarn("TU parse-tu failed [" + err + "]: " + tu.source_file);
      return false;
    }
    if (j.contains("output"))
      DeserializeCombinedOutput(j["output"], out);
    return true;
  } catch (const std::exception& e) {
    LogError("Deserialize failed for " + tu.source_file + ": " + e.what());
    return false;
  }
}

/// 旧模式 RunScheduled（每 TU fork+exec + JSON）
static RunResult RunScheduledLegacy(
    const std::vector<TUEntry>& tu_list,
    const SchedulerConfig& cfg,
    ProgressCallback progress,
    ResultCallback on_result) {

  unsigned par = cfg.parallel > 0 ? cfg.parallel : DefaultParallelism();
  const size_t total = tu_list.size();
  if (total == 0) return {};

  std::string self_path = GetSelfPath();
  if (self_path.empty()) {
    LogError("Cannot determine self executable path for fork+exec");
    return {};
  }

  // 设置环境变量供子进程继承
  {
    const auto& inc_env = GetClangIncludeEnv();
    std::string serialized = SerializeClangIncludeEnv(inc_env);
    setenv("CODEXRAY_CLANG_INCLUDE_ENV", serialized.c_str(), 1);
  }

  RunResult result;
  if (!on_result) result.outputs.resize(total);

  std::mutex q_mu;
  std::queue<size_t> task_queue;
  for (size_t i = 0; i < total; ++i) task_queue.push(i);

  std::atomic<size_t> done_count{0};
  std::atomic<int>    failed{0};
  std::mutex          result_mu;
  std::mutex          progress_mu;

  std::vector<std::thread> workers;
  workers.reserve(par);
  for (unsigned t = 0; t < par; ++t) {
    workers.emplace_back([&] {
      while (true) {
        size_t idx;
        {
          std::lock_guard<std::mutex> lk(q_mu);
          if (task_queue.empty()) break;
          idx = task_queue.front();
          task_queue.pop();
        }
        const TUEntry& tu = tu_list[idx];
        CombinedOutput out;
        bool ok = RunOneTUForked(tu, out, self_path);
        if (!ok) failed.fetch_add(1);
        if (on_result) {
          std::lock_guard<std::mutex> lk(result_mu);
          on_result(tu, out, ok);
        } else if (ok) {
          result.outputs[idx] = std::move(out);
        }
        size_t d = done_count.fetch_add(1) + 1;
        if (progress) {
          std::lock_guard<std::mutex> lk(progress_mu);
          progress(d, total, tu.source_file);
        }
      }
    });
  }
  for (auto& w : workers) w.join();

  result.failed_count = failed.load();
  LogInfo("Scheduler (legacy) done: " + std::to_string(total - result.failed_count) +
          "/" + std::to_string(total) + " succeeded");
  return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 新架构：Pre-fork Worker Pool + Protobuf IPC
// ═══════════════════════════════════════════════════════════════════════════════

/// Worker 进程句柄
struct WorkerProcess {
  pid_t pid      = -1;   // Worker 进程 PID
  int   write_fd = -1;   // 父→子 stdin 管道（写端）
  int   read_fd  = -1;   // 子→父 stdout 管道（读端）
};

/// fork+exec 一个 parse-tu-worker 子进程
/// @param self_path 当前可执行文件路径
/// @return WorkerProcess，pid=-1 表示失败
static WorkerProcess ForkOneWorker(const std::string& self_path) {
  WorkerProcess w;

  int parent_to_child[2];  // 父写 → 子读 (stdin)
  int child_to_parent[2];  // 子写 → 父读 (stdout)

  if (pipe(parent_to_child) != 0 || pipe(child_to_parent) != 0) {
    LogError("ForkOneWorker: pipe() failed");
    return w;
  }

  // 设置 FD_CLOEXEC：exec 后自动关闭父侧 fd，
  // 防止 worker 进程继承其他 worker 的管道写端，
  // 导致 close(write_fd) 后 EOF 无法传递的 bug
  fcntl(parent_to_child[1], F_SETFD, FD_CLOEXEC);  // 父写端：exec 后关闭
  fcntl(child_to_parent[0], F_SETFD, FD_CLOEXEC);  // 父读端：exec 后关闭

  pid_t pid = fork();
  if (pid < 0) {
    LogError("ForkOneWorker: fork() failed");
    close(parent_to_child[0]); close(parent_to_child[1]);
    close(child_to_parent[0]); close(child_to_parent[1]);
    return w;
  }

  if (pid == 0) {
    // ── 子进程 ──────────────────────────────────────────────────────────
    dup2(parent_to_child[0], STDIN_FILENO);
    dup2(child_to_parent[1], STDOUT_FILENO);
    close(parent_to_child[0]); close(parent_to_child[1]);
    close(child_to_parent[0]); close(child_to_parent[1]);

    execl(self_path.c_str(), self_path.c_str(), "parse-tu-worker", nullptr);
    _exit(127);  // exec 失败
  }

  // ── 父进程 ────────────────────────────────────────────────────────────
  close(parent_to_child[0]);  // 关闭子侧读端
  close(child_to_parent[1]);  // 关闭子侧写端

  w.pid = pid;
  w.write_fd = parent_to_child[1];
  w.read_fd = child_to_parent[0];
  return w;
}

/// 关闭 Worker 子进程
/// @param w Worker 进程句柄
/// @param timeout_secs 等待退出的超时秒数
static void ShutdownWorker(WorkerProcess& w, int timeout_secs = 5) {
  if (w.pid <= 0) return;

  // 关闭写端 → worker 的 ReadFrame 返回 EOF → worker 正常退出
  if (w.write_fd >= 0) {
    close(w.write_fd);
    w.write_fd = -1;
  }

  // 等待子进程退出（带超时）
  int status = 0;
  for (int i = 0; i < timeout_secs * 10; ++i) {
    pid_t r = waitpid(w.pid, &status, WNOHANG);
    if (r == w.pid || r < 0) goto done;
    usleep(100000);  // 100ms
  }
  // 超时 → 强制 kill
  kill(w.pid, SIGKILL);
  waitpid(w.pid, &status, 0);

done:
  if (w.read_fd >= 0) {
    close(w.read_fd);
    w.read_fd = -1;
  }
  w.pid = -1;
}

/// 新模式 RunScheduled（Pre-fork Worker Pool + Protobuf IPC）
static RunResult RunScheduledPreFork(
    const std::vector<TUEntry>& tu_list,
    const SchedulerConfig& cfg,
    ProgressCallback progress,
    ResultCallback on_result) {

  unsigned par = cfg.parallel > 0 ? cfg.parallel : DefaultParallelism();
  const size_t total = tu_list.size();
  if (total == 0) return {};

  std::string self_path = GetSelfPath();
  if (self_path.empty()) {
    LogError("Cannot determine self executable path for pre-fork workers");
    return {};
  }

  // 父进程预先探测 Clang 系统 include 路径，通过环境变量传递给子进程
  {
    const auto& inc_env = GetClangIncludeEnv();
    std::string serialized = SerializeClangIncludeEnv(inc_env);
    setenv("CODEXRAY_CLANG_INCLUDE_ENV", serialized.c_str(), 1);
  }

  // ── 主线程：Pre-fork N 个 worker（单线程上下文，fork+exec 安全）──────
  std::vector<WorkerProcess> worker_procs(par);
  for (unsigned i = 0; i < par; ++i) {
    worker_procs[i] = ForkOneWorker(self_path);
    if (worker_procs[i].pid <= 0) {
      LogError("Failed to fork worker " + std::to_string(i));
      // 清理已 fork 的 workers
      for (unsigned j = 0; j < i; ++j) ShutdownWorker(worker_procs[j]);
      return {};
    }
  }
  LogInfo("Pre-forked " + std::to_string(par) + " workers");

  // ── Profiling 基础设施 ──────────────────────────────────────────────
  auto global_start = std::chrono::steady_clock::now();
  auto ms_since = [&]() -> int64_t {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - global_start).count();
  };
  auto now_us = []() -> uint64_t {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
  };

  // 每线程累计各阶段耗时（微秒）
  struct ThreadStats {
    uint64_t serialize_us = 0;   // SerializeTURequestPb 耗时
    uint64_t write_us     = 0;   // WriteFrame 发送请求耗时
    uint64_t wait_us      = 0;   // ReadFrame 等待响应耗时
    uint64_t deser_us     = 0;   // DeserializeWorkerResponsePb 耗时
    uint64_t enqueue_us   = 0;   // 推入结果队列耗时
    uint64_t total_us     = 0;   // 线程总耗时
    int      tu_count     = 0;   // 处理 TU 数
    uint64_t resp_bytes   = 0;   // 响应帧总字节数
  };
  std::vector<ThreadStats> thread_stats(par);

  // DB 写入线程 profiling（单线程，在 writer_thread 中累计）
  struct WriterStats {
    uint64_t total_us     = 0;   // 写入线程总工作时间
    uint64_t wait_us      = 0;   // 等待队列有数据的时间
    int      tu_count     = 0;   // 处理 TU 数
  };
  WriterStats writer_stats;

  RunResult result;
  if (!on_result) result.outputs.resize(total);

  // 任务队列
  std::mutex q_mu;
  std::queue<size_t> task_queue;
  for (size_t i = 0; i < total; ++i) task_queue.push(i);

  std::atomic<size_t> done_count{0};
  std::atomic<int>    failed{0};
  std::mutex          progress_mu; // 保护 progress 回调

  // ── 异步 DB 写入队列 ────────────────────────────────────────────────
  // 分析线程将结果推入队列后立即继续下一个 TU，不等 DB 写入。
  // 单独的写入线程顺序消费队列，避免 SQLite 并发问题。
  struct ResultItem {
    size_t idx;
    TUEntry tu;         // 拷贝，因为原 tu_list 引用在分析线程中
    CombinedOutput out; // move 语义
    bool ok;
  };
  std::mutex                 rq_mu;
  std::condition_variable    rq_cv;
  std::queue<ResultItem>     result_queue;
  bool                       rq_done = false;  // 分析线程全部完成

  // 启动 DB 写入线程
  std::thread writer_thread;
  if (on_result) {
    writer_thread = std::thread([&] {
      while (true) {
        ResultItem item;
        uint64_t wait_start = now_us();
        {
          std::unique_lock<std::mutex> lk(rq_mu);
          rq_cv.wait(lk, [&] { return !result_queue.empty() || rq_done; });
          if (result_queue.empty() && rq_done) break;
          item = std::move(result_queue.front());
          result_queue.pop();
        }
        uint64_t wait_end = now_us();
        writer_stats.wait_us += (wait_end - wait_start);
        uint64_t work_start = now_us();
        on_result(item.tu, item.out, item.ok);
        writer_stats.total_us += (now_us() - work_start);
        writer_stats.tu_count++;
      }
    });
  }

  const int timeout_secs = 120;

  // ── 启动 N 个线程，每线程独占一个 worker ────────────────────────────
  std::vector<std::thread> threads;
  threads.reserve(par);
  for (unsigned t = 0; t < par; ++t) {
    threads.emplace_back([&, t] {
      WorkerProcess& w = worker_procs[t];
      ThreadStats& stats = thread_stats[t];
      uint64_t thread_start = now_us();

      while (true) {
        // 从队列取任务
        size_t idx;
        {
          std::lock_guard<std::mutex> lk(q_mu);
          if (task_queue.empty()) break;
          idx = task_queue.front();
          task_queue.pop();
        }
        const TUEntry& tu = tu_list[idx];
        CombinedOutput out;
        bool ok = false;
        stats.tu_count++;

        // 计时变量（声明在 goto 之前，避免跳过初始化）
        uint64_t t0 = 0, t1 = 0, t2 = 0, t3 = 0, t4 = 0;

        // 序列化请求
        t0 = now_us();
        std::string req_data = SerializeTURequestPb(tu);
        t1 = now_us();
        stats.serialize_us += (t1 - t0);

        // 发送请求
        if (!WriteFrame(w.write_fd, req_data)) {
          LogWarn("Worker write failed on TU: " + tu.source_file);
          goto crash_recovery;
        }
        t2 = now_us();
        stats.write_us += (t2 - t1);

        // 读取响应
        {
          std::string resp_data;
          bool timed_out = false;
          if (!ReadFrame(w.read_fd, resp_data, timeout_secs, timed_out)) {
            if (timed_out) {
              LogWarn("Worker timed out (" + std::to_string(timeout_secs) +
                      "s) on TU: " + tu.source_file);
              kill(w.pid, SIGKILL);
            } else {
              LogWarn("Worker crashed on TU: " + tu.source_file);
            }
            goto crash_recovery;
          }
          t3 = now_us();
          stats.wait_us += (t3 - t2);
          stats.resp_bytes += resp_data.size();

          // 反序列化响应
          {
            std::string resp_error;
            if (!DeserializeWorkerResponsePb(resp_data, out, ok, resp_error)) {
              LogError("Worker response deserialization failed on TU: " + tu.source_file);
              goto crash_recovery;
            }
            t4 = now_us();
            stats.deser_us += (t4 - t3);
            if (!ok) {
              LogWarn("Worker analysis failed [" + resp_error + "]: " + tu.source_file);
            }
          }
        }

        // 正常路径
        goto handle_result;

      crash_recovery:
        {
          int status = 0;
          waitpid(w.pid, &status, 0);
          if (WIFSIGNALED(status)) {
            LogWarn("Worker killed by signal " +
                    std::to_string(WTERMSIG(status)));
          }
          w = ForkOneWorker(self_path);
          if (w.pid <= 0) {
            LogError("Failed to fork replacement worker, aborting remaining TUs");
            failed.fetch_add(1);
            break;
          }
          ok = false;
        }

      handle_result:
        if (!ok) failed.fetch_add(1);

        if (on_result) {
          // 推入异步写入队列（不等 DB 写入完成）
          uint64_t eq0 = now_us();
          {
            std::lock_guard<std::mutex> lk(rq_mu);
            result_queue.push(ResultItem{idx, tu, std::move(out), ok});
          }
          rq_cv.notify_one();
          stats.enqueue_us += (now_us() - eq0);
        } else if (ok) {
          result.outputs[idx] = std::move(out);
        }

        size_t d = done_count.fetch_add(1) + 1;
        if (progress) {
          std::lock_guard<std::mutex> lk(progress_mu);
          progress(d, total, tu.source_file);
        }
      }
      stats.total_us = now_us() - thread_start;
    });
  }

  // 等待所有分析线程完成
  for (auto& th : threads) th.join();
  int64_t analysis_done_ms = ms_since();

  // 通知 DB 写入线程：所有分析完成
  if (writer_thread.joinable()) {
    {
      std::lock_guard<std::mutex> lk(rq_mu);
      rq_done = true;
    }
    rq_cv.notify_one();
    writer_thread.join();
  }

  // ── 优雅关闭所有 worker ────────────────────────────────────────────
  for (auto& w : worker_procs) ShutdownWorker(w);
  int64_t total_ms = ms_since();

  // ── Profiling 输出 ──────────────────────────────────────────────────
  LogInfo("=== PROFILING SUMMARY (T+" + std::to_string(total_ms) + "ms) ===");
  LogInfo("Analysis done at T+" + std::to_string(analysis_done_ms) + "ms, "
          "workers shutdown at T+" + std::to_string(total_ms) + "ms");
  for (unsigned t = 0; t < par; ++t) {
    auto& s = thread_stats[t];
    LogInfo("Thread " + std::to_string(t) +
            ": TUs=" + std::to_string(s.tu_count) +
            " total=" + std::to_string(s.total_us / 1000) + "ms" +
            " ser=" + std::to_string(s.serialize_us / 1000) + "ms" +
            " write=" + std::to_string(s.write_us / 1000) + "ms" +
            " wait=" + std::to_string(s.wait_us / 1000) + "ms" +
            " deser=" + std::to_string(s.deser_us / 1000) + "ms" +
            " enqueue=" + std::to_string(s.enqueue_us / 1000) + "ms" +
            " resp=" + std::to_string(s.resp_bytes / 1024) + "KB");
  }
  LogInfo("WriterThread: work=" + std::to_string(writer_stats.total_us / 1000) + "ms" +
          " wait=" + std::to_string(writer_stats.wait_us / 1000) + "ms" +
          " TUs=" + std::to_string(writer_stats.tu_count));

  result.failed_count = failed.load();
  LogInfo("Scheduler (pre-fork) done: " + std::to_string(total - result.failed_count) +
          "/" + std::to_string(total) + " succeeded");
  return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 公共入口：根据环境变量选择架构
// ═══════════════════════════════════════════════════════════════════════════════

RunResult RunScheduled(
    const std::vector<TUEntry>& tu_list,
    const SchedulerConfig& cfg,
    std::function<bool(const TUEntry&, CombinedOutput&)> /*run_one*/,
    ProgressCallback progress,
    ResultCallback on_result) {

  // CODEXRAY_LEGACY_FORK=1 → 回退到旧模式（每 TU fork+exec + JSON）
  const char* legacy = std::getenv("CODEXRAY_LEGACY_FORK");
  if (legacy && legacy[0] == '1') {
    LogInfo("Using legacy fork+exec+JSON mode (CODEXRAY_LEGACY_FORK=1)");
    return RunScheduledLegacy(tu_list, cfg, progress, on_result);
  }

  return RunScheduledPreFork(tu_list, cfg, progress, on_result);
}

}  // namespace codexray
