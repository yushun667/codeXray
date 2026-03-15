#include "logger.h"
#include <iostream>
#include <atomic>

namespace codexray {

static std::atomic<bool> g_verbose{false};

void LogInit(const std::string& /*log_file*/, bool verbose) {
  g_verbose.store(verbose);
}

void LogInfo(const std::string& msg) {
  if (g_verbose.load()) std::cerr << "[INFO] " << msg << "\n";
}

void LogInfo(const char* msg) {
  if (msg && g_verbose.load()) std::cerr << "[INFO] " << msg << "\n";
}

void LogWarn(const std::string& msg) {
  std::cerr << "[WARN] " << msg << "\n";
}

void LogError(const std::string& msg) {
  std::cerr << "[ERROR] " << msg << "\n";
}

}  // namespace codexray
