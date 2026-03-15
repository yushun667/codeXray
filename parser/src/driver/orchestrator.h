/**
 * 解析驱动：协调全量/增量/懒解析与按需解析。
 * 设计 §3.2 / §4.1 / §4.2 / §4.3。
 */
#pragma once
#include "common/analysis_output.h"
#include "scheduler/pool.h"
#include <functional>
#include <string>
#include <vector>

namespace codexray {

struct ParseOptions {
  std::string project_root;
  std::string compile_commands_path;  // "" → <project_root>/compile_commands.json
  std::string db_path;                // "" → <project_root>/.codexray/codexray.db
  unsigned    parallel        = 0;    // 0 = auto
  bool        lazy            = false;
  bool        incremental     = false;
  bool        verbose         = false;
  std::vector<std::string> priority_dirs;  // relative to project_root
  std::vector<std::string> extra_include_dirs;

  // Progress callback (filled by CLI layer)
  ProgressCallback progress_stdout;
};

struct ParseSummary {
  int64_t     run_id         = 0;
  std::string mode;           // "full" or "incremental"
  int         files_parsed   = 0;
  int         files_failed   = 0;
  int64_t     symbols_count  = 0;
};

/**
 * 执行一次完整解析（全量或增量）。
 * 返回 0 成功，否则为错误码（ExitCode）。
 */
int RunParse(const ParseOptions& opts, ParseSummary* summary = nullptr);

/**
 * 按需解析（懒解析模式下查询触发）。
 * @param file_paths  待解析的源文件路径列表
 * @return 0 成功
 */
int ParseOnDemandForQuery(const std::string& project_root,
                          const std::string& db_path,
                          const std::string& cc_path,
                          const std::vector<std::string>& file_paths,
                          unsigned parallel,
                          const std::vector<std::string>& priority_dirs);

}  // namespace codexray
