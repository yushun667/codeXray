#include "load.h"
#include "../common/path_util.h"
#include "../common/logger.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace codexray {

CompileCommandsResult LoadCompileCommands(const std::string& json_path,
                                          const std::string& project_root,
                                          const std::vector<std::string>& priority_dirs) {
  CompileCommandsResult result;

  std::ifstream f(json_path);
  if (!f.is_open()) {
    result.error = "Cannot open: " + json_path;
    return result;
  }

  nlohmann::json j;
  try {
    f >> j;
  } catch (const std::exception& e) {
    result.error = std::string("JSON parse error: ") + e.what();
    return result;
  }

  if (!j.is_array()) {
    result.error = "compile_commands.json must be a JSON array";
    return result;
  }

  // Build absolute priority dir paths for prefix matching
  std::vector<std::string> abs_priority;
  for (const auto& d : priority_dirs) {
    std::string abs = MakeAbsolute(project_root, d);
    if (!abs.empty() && abs.back() != '/') abs += '/';
    abs_priority.push_back(abs);
  }

  for (const auto& entry : j) {
    if (!entry.is_object()) continue;

    TUEntry tu;
    // directory
    std::string dir;
    if (entry.contains("directory") && entry["directory"].is_string())
      dir = entry["directory"].get<std::string>();
    else
      dir = project_root;
    tu.directory = dir;

    // file
    std::string file;
    if (entry.contains("file") && entry["file"].is_string())
      file = entry["file"].get<std::string>();
    else
      continue;

    // Resolve to absolute path
    tu.source_file = MakeAbsolute(dir, file);

    // arguments: prefer "arguments" array, fall back to "command" string split
    if (entry.contains("arguments") && entry["arguments"].is_array()) {
      for (const auto& a : entry["arguments"])
        if (a.is_string()) tu.arguments.push_back(a.get<std::string>());
    } else if (entry.contains("command") && entry["command"].is_string()) {
      // Simple space split (handles most cases; doesn't handle quoted spaces)
      std::string cmd = entry["command"].get<std::string>();
      std::string tok;
      bool in_sq = false, in_dq = false;
      for (char c : cmd) {
        if (c == '\'' && !in_dq) { in_sq = !in_sq; continue; }
        if (c == '"'  && !in_sq) { in_dq = !in_dq; continue; }
        if (c == ' ' && !in_sq && !in_dq) {
          if (!tok.empty()) { tu.arguments.push_back(tok); tok.clear(); }
        } else {
          tok += c;
        }
      }
      if (!tok.empty()) tu.arguments.push_back(tok);
    }

    // Classify into priority or remainder
    bool is_priority = abs_priority.empty(); // empty priority_dirs → all are priority
    if (!is_priority) {
      std::string sf_slash = tu.source_file;
      if (!sf_slash.empty() && sf_slash.back() != '/') sf_slash += '/';
      // Check if source_file starts with any priority dir
      for (const auto& pd : abs_priority) {
        if (tu.source_file.size() >= pd.size() &&
            tu.source_file.compare(0, pd.size(), pd) == 0) {
          is_priority = true;
          break;
        }
      }
    }

    if (is_priority) result.priority.push_back(std::move(tu));
    else             result.remainder.push_back(std::move(tu));
  }

  LogInfo("CompileCommands: priority=" + std::to_string(result.priority.size()) +
          " remainder=" + std::to_string(result.remainder.size()));
  return result;
}

}  // namespace codexray
