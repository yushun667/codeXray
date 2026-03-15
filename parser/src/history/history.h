/**
 * 历史解析记录：每次 parse 写入 parse_run，提供 list-runs 接口。
 * 设计 §3.1 / §4.4 / §5.7。
 */
#pragma once
#include <sqlite3.h>
#include <cstdint>
#include <string>

namespace codexray {

// 在 DB 中创建 parse_run 记录，返回 run_id（0 表示失败）
int64_t InsertParseRun(sqlite3* db, int64_t project_id, const std::string& mode);

// 更新 parse_run 状态（completed/failed）及文件计数
bool UpdateParseRun(sqlite3* db, int64_t run_id, const std::string& status,
                    int files_parsed, int files_failed,
                    const std::string& error_message = "");

// 返回最近 limit 条解析记录的 JSON 字符串
std::string ListRunsJson(sqlite3* db, int64_t project_id, int limit = 20);

}  // namespace codexray
