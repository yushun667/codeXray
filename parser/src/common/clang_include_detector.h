/**
 * 自动探测 Clang 头文件搜索路径与 resource-dir，供 LibTooling 注入。
 * 通过执行 clang++ -E -x c++ - -v 解析 "#include <...> search starts here" 段，
 * 以及 clang++ -print-resource-dir 获取 builtin 目录，避免手写路径、适配不同环境。
 */

#ifndef CODEXRAY_PARSER_COMMON_CLANG_INCLUDE_DETECTOR_H_
#define CODEXRAY_PARSER_COMMON_CLANG_INCLUDE_DETECTOR_H_

#include <string>
#include <vector>

namespace codexray {

/** 探测结果：系统/标准库头路径 + resource-dir，结果带缓存 */
struct ClangIncludeEnv {
  std::vector<std::string> system_include_paths;
  std::string resource_dir;
};

/**
 * 获取当前环境的 Clang 头文件搜索路径与 resource-dir（首次调用时探测并缓存）。
 * @return 引用进程内缓存，若探测失败则 system_include_paths 可为空、resource_dir 可为空
 */
const ClangIncludeEnv& GetClangIncludeEnv();

/**
 * 获取供 ArgumentsAdjuster 注入的 -resource-dir / -isystem 参数列表（缓存，避免每 TU 构建）。
 * 与 GetClangIncludeEnv() 共用同一缓存，首次调用时若未探测则先探测。
 */
const std::vector<std::string>& GetClangIncludeEnvExtraArgs();

}  // namespace codexray

#endif  // CODEXRAY_PARSER_COMMON_CLANG_INCLUDE_DETECTOR_H_
