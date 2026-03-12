#include "pool.h"
#include "../common/logger.h"
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
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace codexray {

unsigned DefaultParallelism() {
  unsigned cores = std::thread::hardware_concurrency();
  if (cores <= 2) return 1;
  return cores - 2;
}

// ─── TUEntry 序列化/反序列化 ─────────────────────────────────────────────────

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

// ─── CombinedOutput 序列化/反序列化（JSON，通过管道传输）────────────────────

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

// ─── fork+exec 隔离运行单个 TU ───────────────────────────────────────────────
// 使用 fork+exec 在全新子进程中运行 parse-tu 子命令。
// 全新子进程没有 LLVM 全局状态，不会死锁。
// 子进程通过 stdin 接收 TUEntry JSON，通过 stdout 输出结果 JSON。
// 超时后 SIGKILL 子进程。

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
    // 关闭 stderr 中的多余 fd，保留 2 (stderr) 用于调试
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
      // 读取可用数据
      ssize_t n;
      while ((n = read(stdout_pipe[0], buf, sizeof(buf))) > 0)
        result_buf.append(buf, n);

      // 检查子进程
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
      // 读取剩余输出
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

// ─── RunScheduled ────────────────────────────────────────────────────────────

RunResult RunScheduled(
    const std::vector<TUEntry>& tu_list,
    const SchedulerConfig& cfg,
    std::function<bool(const TUEntry&, CombinedOutput&)> /*run_one*/,
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

  RunResult result;
  // 仅在无 on_result 回调时预分配输出数组（兼容旧接口）
  if (!on_result) result.outputs.resize(total);

  std::mutex q_mu;
  std::queue<size_t> task_queue;
  for (size_t i = 0; i < total; ++i) task_queue.push(i);

  std::atomic<size_t> done_count{0};
  std::atomic<int>    failed{0};
  std::mutex          result_mu;   // 保护 on_result 回调（串行写 DB）
  std::mutex          progress_mu; // 保护 progress 回调

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
        if (!ok) {
          failed.fetch_add(1);
        }
        // 调用结果回调（加锁保证串行，回调内可写 DB）
        if (on_result) {
          std::lock_guard<std::mutex> lk(result_mu);
          on_result(tu, out, ok);
        } else if (ok) {
          // 兼容旧接口：存入 outputs 数组
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
  LogInfo("Scheduler done: " + std::to_string(total - result.failed_count) +
          "/" + std::to_string(total) + " succeeded");
  return result;
}

}  // namespace codexray
