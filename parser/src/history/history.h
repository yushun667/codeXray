/**
 * 解析引擎 history：parse_run 写入、list-runs JSON
 * 参考：doc/01-解析引擎 接口约定 2.3、数据库设计 parse_run
 */

#ifndef CODEXRAY_PARSER_HISTORY_HISTORY_H_
#define CODEXRAY_PARSER_HISTORY_HISTORY_H_

#include <cstdint>
#include <string>

struct sqlite3;

namespace codexray {

/** 插入一条 parse_run（status=running），返回 run_id */
int64_t InsertParseRun(sqlite3* db, int64_t project_id, const std::string& mode);

/** 更新 parse_run：finished_at, files_parsed, files_failed, status[, error_message] */
bool UpdateParseRun(sqlite3* db, int64_t run_id,
                   const std::string& finished_at,
                   int files_parsed,
                   int files_failed,
                   const std::string& status,
                   const std::string& error_message = "");

/** 按 started_at 降序返回 JSON 数组，每项 run_id, started_at, finished_at, mode, files_parsed, status；project_id=0 表示全部 */
std::string ListRunsJson(sqlite3* db, int64_t project_id, int limit);

/** 按 root_path 查 project_id，不存在返回 0 */
int64_t GetProjectId(sqlite3* db, const std::string& root_path);

}  // namespace codexray

#endif  // CODEXRAY_PARSER_HISTORY_HISTORY_H_
