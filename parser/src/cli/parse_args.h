/**
 * 解析引擎 CLI：参数解析与子命令分发
 * 参考：doc/01-解析引擎 接口约定 2.1–2.3、§4
 */

#ifndef CODEXRAY_PARSER_CLI_PARSE_ARGS_H_
#define CODEXRAY_PARSER_CLI_PARSE_ARGS_H_

#include "driver/orchestrator.h"
#include <string>
#include <vector>

namespace codexray {

struct QueryOptions {
  std::string db_path;
  std::string project_root;
  std::string query_type;   // call_graph | class_graph | data_flow | control_flow
  std::string symbol;
  std::string file_path;    // 与 line 同时给出时按「文件+行号」解析符号
  int line = 0;
  int column = 0;
  int depth = 3;
};

struct ListRunsOptions {
  std::string db_path;
  std::string project_root;
  int limit = 100;
};

enum class Subcommand { kNone, kParse, kQuery, kListRuns };

/** 解析 argc/argv，填充对应 opts，返回子命令；失败返回 kNone 并可选设置 error_msg */
Subcommand ParseArgs(int argc, char* argv[],
                     ParseOptions* parse_opts,
                     QueryOptions* query_opts,
                     ListRunsOptions* list_opts,
                     std::string* error_msg = nullptr);

}  // namespace codexray

#endif  // CODEXRAY_PARSER_CLI_PARSE_ARGS_H_
