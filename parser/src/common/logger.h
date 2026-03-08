/**
 * 解析引擎公共模块：日志（落盘 + 可选 stderr）
 * 参考：doc/01-解析引擎 解析引擎详细功能与架构设计 §4.16
 */

#ifndef CODEXRAY_PARSER_COMMON_LOGGER_H_
#define CODEXRAY_PARSER_COMMON_LOGGER_H_

#include <cstdarg>
#include <string>

namespace codexray {

/** 初始化日志：log_path 为空则仅 stderr（当 also_stderr 为 true 时） */
void LogInit(const std::string& log_path, bool also_stderr);

/** 写 INFO 级别日志，支持 printf 风格格式 */
void LogInfo(const char* fmt, ...);

/** 写 ERROR 级别日志，支持 printf 风格格式 */
void LogError(const char* fmt, ...);

/** 内部：带级别的格式化写入（供需要 va_list 的调用方使用） */
void LogV(const char* level, const char* fmt, std::va_list ap);

}  // namespace codexray

#endif  // CODEXRAY_PARSER_COMMON_LOGGER_H_
