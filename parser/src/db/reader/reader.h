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
  int64_t decl_file_id = 0;
  int decl_line = 0;
  int decl_column = 0;
  int decl_line_end = 0;
  int decl_column_end = 0;
  std::string def_file_path;   // 由调用方或二次查询填充
  std::string decl_file_path;  // 由调用方或二次查询填充
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

/** 按 file_id 查该文件下所有 symbol（按 def 位置） */
std::vector<SymbolRow> QuerySymbolsByFile(sqlite3* db, int64_t file_id);

/** 按文件+行号查符号：def 或 decl 位于 (file_id, line) 的 symbol；column_hint 可选，非 0 时同时匹配列 */
std::vector<SymbolRow> QuerySymbolsByFileAndLine(sqlite3* db, int64_t file_id, int line, int column_hint = 0);

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

/** 按 project root_path 查 project_id；不存在返回 0 */
int64_t QueryProjectIdByRoot(sqlite3* db, const std::string& root_path);
/** 按 project_id 与 path 查 file_id；path 可为绝对或与 file 表一致；不存在返回 0 */
int64_t QueryFileIdByPath(sqlite3* db, int64_t project_id, const std::string& path);

// --- class / class_relation ---
struct ClassRow {
  int64_t id = 0;
  std::string usr;
  int64_t file_id = 0;
  std::string name;
  std::string qualified_name;
  int64_t def_file_id = 0;
  int def_line = 0, def_column = 0, def_line_end = 0, def_column_end = 0;
  std::string def_file_path;
};
struct ClassRelationRow {
  int64_t parent_id = 0;
  int64_t child_id = 0;
  std::string parent_usr;
  std::string child_usr;
  std::string relation_type;
};
ClassRow QueryClassByUsr(sqlite3* db, const std::string& usr);
std::vector<ClassRow> QueryClassesByFile(sqlite3* db, int64_t file_id);
/** 查询与给定 class id 列表相关的所有 class_relation（parent 或 child 在 ids 中） */
std::vector<ClassRelationRow> QueryClassRelations(sqlite3* db, const std::vector<int64_t>& class_ids);

// --- global_var / data_flow_edge ---
struct GlobalVarRow {
  int64_t id = 0;
  std::string usr;
  int64_t def_file_id = 0;
  int def_line = 0, def_column = 0, def_line_end = 0, def_column_end = 0;
  int64_t file_id = 0;
  std::string name;
  std::string def_file_path;
};
struct DataFlowEdgeRow {
  int64_t var_id = 0;
  int64_t reader_id = 0;
  int64_t writer_id = 0;
  std::string var_usr;
  std::string reader_usr;
  std::string writer_usr;
};
GlobalVarRow QueryGlobalVarByUsr(sqlite3* db, const std::string& usr);
std::vector<GlobalVarRow> QueryGlobalVarsByFile(sqlite3* db, int64_t file_id);
std::vector<DataFlowEdgeRow> QueryDataFlowEdgesByVar(sqlite3* db, int64_t var_id);

// --- cfg_node / cfg_edge ---
struct CfgNodeRow {
  int64_t id = 0;
  int64_t symbol_id = 0;
  std::string block_id;
  std::string kind;
  int64_t file_id = 0;
  int line = 0, column = 0;
  std::string file_path;
};
struct CfgEdgeRow {
  int64_t from_node_id = 0;
  int64_t to_node_id = 0;
  std::string edge_type;
};
std::vector<CfgNodeRow> QueryCfgNodesBySymbolId(sqlite3* db, int64_t symbol_id);
std::vector<CfgEdgeRow> QueryCfgEdgesByFromNodeIds(sqlite3* db, const std::vector<int64_t>& node_ids);

}  // namespace codexray

#endif  // CODEXRAY_PARSER_DB_READER_READER_H_
