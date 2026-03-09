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

/** 进度输出回调：done/total 步数，current_file 当前完成的 TU 源文件路径 */
using ProgressOutCallback = std::function<void(size_t done, size_t total, const std::string& current_file)>;

struct ParseOptions {
  std::string project_root;
  std::string compile_commands_path;
  std::string output_db;
  unsigned parallel = 0;
  bool lazy = true;
  std::vector<std::string> priority_dirs;
  bool incremental = false;
  bool verbose = false;
  ProgressOutCallback progress_stdout;
};

/** 解析成功时的摘要，供 stdout 输出（接口约定 §3） */
struct ParseSummary {
  int64_t run_id = 0;
  int files_parsed = 0;
  int files_failed = 0;
  size_t symbols_count = 0;
  std::string mode;
};

/** 执行解析；成功时若 summary_out 非空则填入摘要。返回 0 成功，否则为 ExitCode */
int RunParse(const ParseOptions& opts, ParseSummary* summary_out = nullptr);

/**
 * 按需解析：仅解析指定源文件对应的 TU，同步阻塞至完成（设计 §4.3 懒解析查询时触发）。
 * 用于 query 子命令在 --lazy 且目标文件尚未解析时补解析。
 * 防重入：若已在解析中则直接返回 -1。
 * @return 0 成功，-1 重入拒绝，>0 为 ExitCode
 */
int ParseOnDemandForQuery(const std::string& project_root,
                          const std::string& db_path,
                          const std::string& compile_commands_path,
                          const std::vector<std::string>& file_paths,
                          unsigned parallel,
                          const std::vector<std::string>& priority_dirs);

}  // namespace codexray

#endif  // CODEXRAY_PARSER_DRIVER_ORCHESTRATOR_H_
