/**
 * 解析引擎 driver：Orchestrator，协调 parse 全流程
 * 参考：doc/01-解析引擎 解析引擎详细功能与架构设计 §4.2
 */

#ifndef CODEXRAY_PARSER_DRIVER_ORCHESTRATOR_H_
#define CODEXRAY_PARSER_DRIVER_ORCHESTRATOR_H_

#include <functional>
#include <string>
#include <vector>

namespace codexray {

using ProgressOutCallback = std::function<void(size_t done, size_t total)>;

struct ParseOptions {
  std::string project_root;
  std::string compile_commands_path;
  std::string output_db;
  unsigned parallel = 0;
  bool lazy = true;
  std::vector<std::string> priority_dirs;
  bool incremental = false;
  ProgressOutCallback progress_stdout;
};

bool RunParse(const ParseOptions& opts);

}  // namespace codexray

#endif  // CODEXRAY_PARSER_DRIVER_ORCHESTRATOR_H_
