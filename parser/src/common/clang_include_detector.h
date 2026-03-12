/**
 * Clang 环境探测：自动获取系统头路径与资源目录。
 * 设计 §3.16 / §5.2。
 */
#pragma once
#include <string>
#include <vector>

namespace codexray {

struct ClangIncludeEnv {
  std::string              resource_dir;   // clang++ -print-resource-dir
  std::vector<std::string> system_includes; // -isystem paths
};

// 探测（结果进程内缓存，调多次只执行一次外部命令）。
// 子进程（fork+exec）场景下，若环境变量 CODEXRAY_CLANG_INCLUDE_ENV 已设置，
// 则直接从中恢复而不再运行 clang++ 子进程探测。
const ClangIncludeEnv& GetClangIncludeEnv();

// 将 ClangIncludeEnv 序列化为字符串（用 ASCII 0x1F 分隔），
// 供父进程通过环境变量传递给 fork+exec 的子进程，避免每个子进程重复探测。
std::string SerializeClangIncludeEnv(const ClangIncludeEnv& env);

// 转换为 ClangTool ArgumentsAdjuster 需要的字符串向量
// 格式：[ "-resource-dir", "<dir>", "-isystem", "<p1>", "-isystem", "<p2>", ... ]
std::vector<std::string> ClangIncludeArgs();

}  // namespace codexray
