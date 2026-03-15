#include "logger.h"
#include <iostream>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <vector>

namespace codexray {

static std::atomic<bool> g_verbose{false};

void LogInit(const std::string& /*log_file*/, bool verbose) {
  g_verbose.store(verbose);
}

void LogInfo(const std::string& msg) {
  if (g_verbose.load()) std::cerr << "[INFO] " << msg << "\n";
}

void LogInfo(const char* fmt, ...) {
  if (!g_verbose.load() || !fmt) return;
  va_list ap;
  va_start(ap, fmt);
  std::vector<char> buf(256);
  int n = vsnprintf(buf.data(), buf.size(), fmt, ap);
  va_end(ap);
  if (n >= 0 && static_cast<size_t>(n) >= buf.size()) {
    buf.resize(static_cast<size_t>(n) + 1);
    va_start(ap, fmt);
    vsnprintf(buf.data(), buf.size(), fmt, ap);
    va_end(ap);
  }
  std::cerr << "[INFO] " << buf.data() << "\n";
}

void LogWarn(const std::string& msg) {
  std::cerr << "[WARN] " << msg << "\n";
}

void LogError(const std::string& msg) {
  std::cerr << "[ERROR] " << msg << "\n";
}

void LogError(const char* fmt, ...) {
  if (!fmt) return;
  va_list ap;
  va_start(ap, fmt);
  std::vector<char> buf(256);
  int n = vsnprintf(buf.data(), buf.size(), fmt, ap);
  va_end(ap);
  if (n >= 0 && static_cast<size_t>(n) >= buf.size()) {
    buf.resize(static_cast<size_t>(n) + 1);
    va_start(ap, fmt);
    vsnprintf(buf.data(), buf.size(), fmt, ap);
    va_end(ap);
  }
  std::cerr << "[ERROR] " << buf.data() << "\n";
}

}  // namespace codexray
