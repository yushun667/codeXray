/**
 * 解析引擎 DB 模块：Schema 创建/迁移
 * 与 doc/01-解析引擎/数据库设计.md §2 一致
 */

#ifndef CODEXRAY_PARSER_DB_SCHEMA_H_
#define CODEXRAY_PARSER_DB_SCHEMA_H_

struct sqlite3;

namespace codexray {

/** 创建或迁移所有表与索引，若已存在则跳过 */
bool EnsureSchema(sqlite3* db);

}  // namespace codexray

#endif  // CODEXRAY_PARSER_DB_SCHEMA_H_
