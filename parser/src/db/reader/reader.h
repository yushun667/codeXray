/**
 * 解析引擎 DB reader：按 USR/文件/类型/depth 查询
 * 参考：doc/01-解析引擎 数据库设计 §3、解析引擎详细功能与架构设计 §4.12
 */

#ifndef CODEXRAY_PARSER_DB_READER_READER_H_
#define CODEXRAY_PARSER_DB_READER_READER_H_

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

namespace codexray {

struct SymbolRow {
  int64_t id = 0;
  std::string usr;
  std::string name;
  std::string qualified_name;
  std::string kind;
  int64_t def_file_id = 0;
  int def_line = 0;
  int def_column = 0;
  int def_line_end = 0;
  int def_column_end = 0;
  std::string def_file_path;  // 由调用方或二次查询填充
};

struct CallEdgeRow {
  int64_t caller_id = 0;
  int64_t callee_id = 0;
  std::string caller_usr;
  std::string callee_usr;
  int64_t call_site_file_id = 0;
  int call_site_line = 0;
  int call_site_column = 0;
  std::string edge_type;
  std::string call_site_file_path;
};

/** 按 USR 查单个 symbol，不存在返回空 optional（id=0） */
SymbolRow QuerySymbolByUsr(sqlite3* db, const std::string& usr);

/** 按 file_id 查该文件下所有 symbol */
std::vector<SymbolRow> QuerySymbolsByFile(sqlite3* db, int64_t file_id);

/**
 * 查调用边：by_caller=true 时以 caller_usr 为起点查其发出的边；
 * by_caller=false 时以 callee_usr 为起点查指向它的边。
 * depth 限制展开层数（默认 3）。
 */
std::vector<CallEdgeRow> QueryCallEdges(sqlite3* db,
                                        const std::string& symbol_usr,
                                        bool by_caller,
                                        int depth);

/** 展开调用子图：from_usr 为起点，direction "caller"|"callee"，depth 层数 */
std::vector<CallEdgeRow> QueryCallGraphExpand(sqlite3* db,
                                              const std::string& from_usr,
                                              const std::string& direction,
                                              int depth);

/** 将 file_id 解析为 path（用于填充 SymbolRow.def_file_path 等） */
std::string QueryFilePath(sqlite3* db, int64_t file_id);

}  // namespace codexray

#endif  // CODEXRAY_PARSER_DB_READER_READER_H_
