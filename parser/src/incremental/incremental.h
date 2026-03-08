/**
 * 解析引擎 incremental：变更检测、删除旧数据
 * 参考：doc/01-解析引擎 解析引擎详细功能与架构设计 §4.15、接口约定 2.1 --incremental
 */

#ifndef CODEXRAY_PARSER_INCREMENTAL_INCREMENTAL_H_
#define CODEXRAY_PARSER_INCREMENTAL_INCREMENTAL_H_

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

namespace codexray {

/**
 * 对比 parsed_file 与磁盘：返回相较上次解析已变更的源文件路径列表。
 * 通过 file_mtime 或 content_hash 判断；若文件不存在或无法读取视为变更。
 */
std::vector<std::string> GetChangedFiles(sqlite3* db, int64_t project_id);

/**
 * 按路径列表删除这些文件在 DB 中的旧数据（symbol/call_edge/class/global_var/cfg/parsed_file 等）。
 * 路径为工程内相对或绝对路径，会解析为 file_id 后删除。
 */
bool RemoveDataForFiles(sqlite3* db, int64_t project_id,
                        const std::vector<std::string>& paths);

}  // namespace codexray

#endif  // CODEXRAY_PARSER_INCREMENTAL_INCREMENTAL_H_
