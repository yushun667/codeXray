/**
 * 自动探测 Clang 头文件搜索路径与 resource-dir 的实现。
 * 解析 clang++ -E -x c++ - -v 的 stderr 中 "#include <...> search starts here" 至 "End of search list."。
 */

#include "common/clang_include_detector.h"
#include "common/logger.h"
#include <sstream>
#include <string>
#include <mutex>
#include <cstdio>
#include <cstring>

#if defined(_WIN32) || defined(_WIN64)
#include <io.h>
#define popen _popen
#define pclose _pclose
#else
#include <unistd.h>
#endif

namespace codexray {

namespace {

static std::mutex g_env_mutex;
static bool g_env_cached = false;
static ClangIncludeEnv g_cached_env;

static std::string Trim(const std::string& s) {
  size_t a = 0;
  while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) ++a;
  size_t b = s.size();
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
  return s.substr(a, b - a);
}

/** 从 clang -v 的 stderr 输出中解析 "#include <...> search starts here" 到 "End of search list." 的行 */
static std::vector<std::string> ParseIncludePathsFromStderr(const std::string& output) {
  std::vector<std::string> paths;
  const std::string start_marker = "#include <...> search starts here:";
  const std::string end_marker = "End of search list.";
  size_t start = output.find(start_marker);
  if (start == std::string::npos) return paths;
  start += start_marker.size();
  size_t end = output.find(end_marker, start);
  if (end == std::string::npos) end = output.size();

  std::istringstream iss(output.substr(start, end - start));
  std::string line;
  while (std::getline(iss, line)) {
    std::string path = Trim(line);
    if (!path.empty()) paths.push_back(path);
  }
  return paths;
}

/** 执行 command，将 stdout 与 stderr 合并读取（用于 -v 输出在 stderr 的情况） */
static bool RunCommand(const char* cmd, std::string* out_stdout_stderr) {
  if (!out_stdout_stderr) return false;
  out_stdout_stderr->clear();
  FILE* fp = popen(cmd, "r");
  if (!fp) return false;
  char buf[4096];
  while (fgets(buf, sizeof(buf), fp) != nullptr)
    *out_stdout_stderr += buf;
  int status = pclose(fp);
  return (status >= 0);
}

/** 执行 command，仅读取 stdout（用于 -print-resource-dir） */
static bool RunCommandStdoutOnly(const char* cmd, std::string* out_stdout) {
  if (!out_stdout) return false;
  out_stdout->clear();
  FILE* fp = popen(cmd, "r");
  if (!fp) return false;
  char buf[4096];
  while (fgets(buf, sizeof(buf), fp) != nullptr)
    *out_stdout += buf;
  pclose(fp);
  size_t end = out_stdout->size();
  while (end > 0 && (out_stdout->at(end - 1) == '\n' || out_stdout->at(end - 1) == '\r'))
    --end;
  out_stdout->resize(end);
  return true;
}

static void DetectOnce(ClangIncludeEnv* env) {
  if (!env) return;
  env->system_include_paths.clear();
  env->resource_dir.clear();

  std::string combined;
#if defined(_WIN32) || defined(_WIN64)
  const char* verbose_cmd = "clang++ -E -x c++ - -v 2>&1";
#else
  const char* verbose_cmd = "clang++ -E -x c++ - -v 2>&1 </dev/null";
#endif
  if (!RunCommand(verbose_cmd, &combined)) {
    LogError("GetClangIncludeEnv: run clang++ -v failed");
    return;
  }
  env->system_include_paths = ParseIncludePathsFromStderr(combined);
  LogInfo("GetClangIncludeEnv: detected %zu system include paths",
          env->system_include_paths.size());

  if (!RunCommandStdoutOnly("clang++ -print-resource-dir 2>/dev/null", &env->resource_dir)) {
    env->resource_dir.clear();
  } else {
    env->resource_dir = Trim(env->resource_dir);
    if (!env->resource_dir.empty())
      LogInfo("GetClangIncludeEnv: resource-dir=%s", env->resource_dir.c_str());
  }
}

static std::vector<std::string> g_cached_extra_args;
static bool g_extra_args_cached = false;

static void BuildExtraArgsLocked() {
  g_cached_extra_args.clear();
  if (!g_cached_env.resource_dir.empty()) {
    g_cached_extra_args.push_back("-resource-dir");
    g_cached_extra_args.push_back(g_cached_env.resource_dir);
  }
  for (const std::string& p : g_cached_env.system_include_paths) {
    g_cached_extra_args.push_back("-isystem");
    g_cached_extra_args.push_back(p);
  }
  g_extra_args_cached = true;
}

}  // namespace

const ClangIncludeEnv& GetClangIncludeEnv() {
  std::lock_guard<std::mutex> lock(g_env_mutex);
  if (!g_env_cached) {
    DetectOnce(&g_cached_env);
    g_env_cached = true;
  }
  return g_cached_env;
}

const std::vector<std::string>& GetClangIncludeEnvExtraArgs() {
  std::lock_guard<std::mutex> lock(g_env_mutex);
  if (!g_env_cached) {
    DetectOnce(&g_cached_env);
    g_env_cached = true;
  }
  if (!g_extra_args_cached)
    BuildExtraArgsLocked();
  return g_cached_extra_args;
}

}  // namespace codexray
