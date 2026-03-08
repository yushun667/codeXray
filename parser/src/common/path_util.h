/**
 * 解析引擎公共模块：路径规范化与 USR 辅助
 * 参考：doc/01-解析引擎 解析引擎详细功能与架构设计 §4.16
 */

#ifndef CODEXRAY_PARSER_COMMON_PATH_UTIL_H_
#define CODEXRAY_PARSER_COMMON_PATH_UTIL_H_

#include <string>
#include <vector>

namespace codexray {

/** 规范化路径：统一分隔符、去除多余 slash、解析 . 与 .. */
std::string NormalizePath(const std::string& path);

/** 将相对路径转为基于 base 的绝对路径；若 rel 已是绝对路径则返回规范化后的 rel */
std::string MakeAbsolute(const std::string& base, const std::string& rel);

/** 判断 path 是否以 prefix 开头（规范化后比较） */
bool PathStartsWith(const std::string& path, const std::string& prefix);

/** 将 path 转为相对 project_root 的路径；若不在 root 下则返回原 path */
std::string ToProjectRelative(const std::string& path,
                              const std::string& project_root);

}  // namespace codexray

#endif  // CODEXRAY_PARSER_COMMON_PATH_UTIL_H_
