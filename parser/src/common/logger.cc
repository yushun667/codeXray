/**
 * 解析引擎公共模块：日志实现（落盘 + 可选 stderr）
 */

#include "common/logger.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>

namespace codexray {

namespace {

std::mutex g_log_mutex;
std::string g_log_path;
bool g_also_stderr = false;
std::FILE* g_log_file = nullptr;

std::string Now() {
  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) % 1000;
  std::tm tm_buf;
#ifdef _WIN32
  localtime_s(&tm_buf, &t);
#else
  localtime_r(&t, &tm_buf);
#endif
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
                tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                static_cast<long>(ms.count()));
  return buf;
}

void WriteLog(const char* level, const char* fmt, std::va_list ap) {
  std::va_list ap2;
  va_copy(ap2, ap);
  int n = std::vsnprintf(nullptr, 0, fmt, ap2);
  va_end(ap2);
  if (n < 0) return;
  std::string msg(static_cast<size_t>(n) + 1, '\0');
  std::vsnprintf(&msg[0], msg.size(), fmt, ap);
  msg.resize(static_cast<size_t>(n));

  std::string line = Now() + " [" + level + "] " + msg + "\n";

  std::lock_guard<std::mutex> lock(g_log_mutex);
  if (g_log_file) {
    std::fputs(line.c_str(), g_log_file);
    std::fflush(g_log_file);
  }
  if (g_also_stderr) {
    std::fputs(line.c_str(), stderr);
  }
}

}  // namespace

void LogInit(const std::string& log_path, bool also_stderr) {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  if (g_log_file) {
    std::fclose(g_log_file);
    g_log_file = nullptr;
  }
  g_log_path = log_path;
  g_also_stderr = also_stderr;
  if (!log_path.empty()) {
    g_log_file = std::fopen(log_path.c_str(), "a");
  }
}

void LogV(const char* level, const char* fmt, std::va_list ap) {
  WriteLog(level, fmt, ap);
}

void LogInfo(const char* fmt, ...) {
  std::va_list ap;
  va_start(ap, fmt);
  LogV("INFO", fmt, ap);
  va_end(ap);
}

void LogError(const char* fmt, ...) {
  std::va_list ap;
  va_start(ap, fmt);
  LogV("ERROR", fmt, ap);
  va_end(ap);
}

}  // namespace codexray
