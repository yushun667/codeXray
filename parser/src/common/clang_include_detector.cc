#include "clang_include_detector.h"
#include "logger.h"
#include <array>
#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <sstream>

namespace codexray {

namespace {

// 执行命令并捕获 stdout+stderr（合并）
std::string RunCommand(const std::string& cmd) {
  std::array<char, 4096> buf{};
  std::string out;
  FILE* fp = popen(cmd.c_str(), "r");
  if (!fp) return out;
  while (fgets(buf.data(), static_cast<int>(buf.size()), fp))
    out += buf.data();
  pclose(fp);
  return out;
}

// 解析 clang++ -E -x c++ - -v 的 stderr，提取 #include <...> search list
std::vector<std::string> ParseSearchPaths(const std::string& output) {
  std::vector<std::string> paths;
  std::istringstream ss(output);
  std::string line;
  bool in_section = false;
  while (std::getline(ss, line)) {
    if (line.find("#include <...> search starts here:") != std::string::npos) {
      in_section = true;
      continue;
    }
    if (in_section && line.find("End of search list.") != std::string::npos) break;
    if (in_section) {
      // strip leading whitespace
      size_t start = line.find_first_not_of(" \t");
      if (start != std::string::npos) {
        std::string p = line.substr(start);
        // strip trailing ' (framework directory)' suffix
        size_t end = p.find(" (");
        if (end != std::string::npos) p = p.substr(0, end);
        if (!p.empty()) paths.push_back(p);
      }
    }
  }
  return paths;
}

ClangIncludeEnv DetectEnv() {
  ClangIncludeEnv env;
  // resource dir
  std::string rd = RunCommand("clang++ -print-resource-dir 2>/dev/null");
  while (!rd.empty() && (rd.back() == '\n' || rd.back() == '\r' || rd.back() == ' '))
    rd.pop_back();
  env.resource_dir = rd;

  // system include paths via verbose preprocessing of empty stdin
  std::string verbose = RunCommand("echo '' | clang++ -E -x c++ - -v 2>&1");
  env.system_includes = ParseSearchPaths(verbose);

  LogInfo("ClangIncludeEnv: resource_dir=" + env.resource_dir +
          " system_includes=" + std::to_string(env.system_includes.size()));
  return env;
}

}  // namespace

/// 将环境变量值（ASCII 0x1F 分隔）反序列化为 ClangIncludeEnv。
/// 格式：resource_dir\x1Fpath1\x1Fpath2\x1F...
static ClangIncludeEnv ParseEnvString(const std::string& s) {
  ClangIncludeEnv env;
  const char kSep = '\x1F';  // ASCII Unit Separator
  size_t pos = 0;
  size_t next = s.find(kSep, pos);
  // 第一段是 resource_dir
  env.resource_dir = (next == std::string::npos) ? s : s.substr(0, next);
  // 后续段是 system_includes
  while (next != std::string::npos) {
    pos = next + 1;
    next = s.find(kSep, pos);
    std::string p = (next == std::string::npos) ? s.substr(pos) : s.substr(pos, next - pos);
    if (!p.empty()) env.system_includes.push_back(p);
  }
  return env;
}

const ClangIncludeEnv& GetClangIncludeEnv() {
  static std::once_flag flag;
  static ClangIncludeEnv env;
  std::call_once(flag, [&] {
    // 优先从环境变量读取（fork+exec 子进程路径，避免重复探测）
    const char* ev = std::getenv("CODEXRAY_CLANG_INCLUDE_ENV");
    if (ev && ev[0]) {
      env = ParseEnvString(std::string(ev));
      LogInfo("ClangIncludeEnv: loaded from env var, resource_dir=" +
              env.resource_dir + " system_includes=" +
              std::to_string(env.system_includes.size()));
    } else {
      env = DetectEnv();
    }
  });
  return env;
}

std::string SerializeClangIncludeEnv(const ClangIncludeEnv& env) {
  const char kSep = '\x1F';  // ASCII Unit Separator，避免路径中常见的 | : 冲突
  std::string s = env.resource_dir;
  for (const auto& p : env.system_includes) {
    s += kSep;
    s += p;
  }
  return s;
}

std::vector<std::string> ClangIncludeArgs() {
  const auto& env = GetClangIncludeEnv();
  std::vector<std::string> args;
  if (!env.resource_dir.empty()) {
    args.push_back("-resource-dir");
    args.push_back(env.resource_dir);
  }
  for (const auto& p : env.system_includes) {
    args.push_back("-isystem");
    args.push_back(p);
  }
  return args;
}

}  // namespace codexray
