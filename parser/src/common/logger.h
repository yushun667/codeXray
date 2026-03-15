/**
 * 日志 — 多级别（INFO/WARNING/ERROR），输出到 stderr；--verbose 控制 INFO。
 */
#pragma once
#include <string>

namespace codexray {

void LogInit(const std::string& /*log_file*/, bool verbose);

void LogInfo(const std::string& msg);
void LogInfo(const char* fmt, ...);  // printf 风格，单参即 const char* 也可（供 load.cc 等）
void LogWarn(const std::string& msg);
void LogError(const std::string& msg);
void LogError(const char* fmt, ...);  // printf 风格，供 load.cc 等单条格式化日志

}  // namespace codexray
