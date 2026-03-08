/**
 * 解析引擎 query：四种类型 JSON 输出
 * 参考：doc/01-解析引擎 接口约定 §3
 */

#ifndef CODEXRAY_PARSER_QUERY_JSON_OUTPUT_H_
#define CODEXRAY_PARSER_QUERY_JSON_OUTPUT_H_

#include <string>

struct sqlite3;

namespace codexray {

std::string QueryCallGraphJson(sqlite3* db, const std::string& symbol,
                               const std::string& file, int depth);
std::string QueryClassGraphJson(sqlite3* db, const std::string& symbol,
                                const std::string& file);
std::string QueryDataFlowJson(sqlite3* db, const std::string& symbol,
                              const std::string& file);
std::string QueryControlFlowJson(sqlite3* db, const std::string& symbol,
                                 const std::string& file);

}  // namespace codexray

#endif  // CODEXRAY_PARSER_QUERY_JSON_OUTPUT_H_
