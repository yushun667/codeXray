/**
 * 日志 — 多级别（INFO/WARNING/ERROR），输出到 stderr；--verbose 控制 INFO。
 */
#pragma once
#include <string>

namespace codexray {

void LogInit(const std::string& /*log_file*/, bool verbose);

void LogInfo(const std::string& msg);
void LogInfo(const char* msg);  // 重载：便于 GCC 下避免 string 到 const char* 的歧义
void LogWarn(const std::string& msg);
void LogError(const std::string& msg);

}  // namespace codexray
