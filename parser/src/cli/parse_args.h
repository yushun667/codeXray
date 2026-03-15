/**
 * CLI 参数解析：parse / query / list-runs 子命令。
 * 设计 §3.1 / 接口约定 §2。
 */
#pragma once
#include "driver/orchestrator.h"
#include "query/query_options.h"
#include "query/json_output.h"
#include <string>

namespace codexray {

struct ListRunsOptions {
  std::string db_path;
  std::string project_root;
  int         limit   = 20;
  bool        verbose = false;
};

enum class Subcommand {
  kNone,
  kParse,
  kQuery,
  kListRuns,
};

/**
 * 解析 argc/argv，填充对应选项结构体，返回识别到的子命令。
 * 出错时设置 *error_msg 并返回 kNone。
 */
Subcommand ParseArgs(int argc, char* argv[],
                     ParseOptions* parse_opts,
                     QueryOptions* query_opts,
                     ListRunsOptions* list_opts,
                     std::string* error_msg);

}  // namespace codexray
