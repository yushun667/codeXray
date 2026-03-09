/**
 * 解析引擎 DB writer：symbol/call_edge/class/global_var/cfg 写入
 * 参考：doc/01-解析引擎 数据库设计 §2、解析引擎详细功能与架构设计 §4.11
 */

#ifndef CODEXRAY_PARSER_DB_WRITER_WRITER_H_
#define CODEXRAY_PARSER_DB_WRITER_WRITER_H_

#include "ast/class_relation/action.h"
#include "ast/data_flow/action.h"
#include "ast/control_flow/action.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct sqlite3;

namespace codexray {

struct SymbolRecord {
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
  /** 解析时：定义/声明是否位于当前 TU 主文件，供 driver 填 def_file_id/decl_file_id */
  bool def_in_tu_file = false;
  bool decl_in_tu_file = false;
};

struct CallEdgeRecord {
  std::string caller_usr;
  std::string callee_usr;
  int64_t call_site_file_id = 0;
  int call_site_line = 0;
  int call_site_column = 0;
  std::string edge_type;  // "direct" | "via_function_pointer"
};

class DBWriter {
 public:
  explicit DBWriter(sqlite3* db);
  ~DBWriter() = default;

  /** 获取或插入 project，返回 project_id */
  int64_t EnsureProject(const std::string& root_path,
                        const std::string& compile_commands_path);

  /** 获取或插入 file，返回 file_id */
  int64_t EnsureFile(int64_t project_id, const std::string& path);

  /** 批量插入 symbol，返回 usr -> id 映射 */
  std::unordered_map<std::string, int64_t> WriteSymbols(
      int64_t project_id,
      const std::vector<SymbolRecord>& symbols);

  /** 写入调用边（caller/callee 以 USR 标识，内部解析为 id；缺失则插入占位 symbol） */
  bool WriteCallEdges(int64_t project_id,
                      const std::vector<CallEdgeRecord>& edges,
                      const std::unordered_map<std::string, int64_t>& usr_to_id);

  /** 批量插入 class，返回 usr -> class id 映射 */
  std::unordered_map<std::string, int64_t> WriteClasses(
      int64_t project_id,
      const std::vector<ClassRecord>& classes);

  /** 写入类关系边（parent_usr/child_usr 由 class_usr_to_id 解析为 id；缺失则跳过该条） */
  bool WriteClassRelations(int64_t project_id,
                           const std::vector<ClassRelationRecord>& relations,
                           const std::unordered_map<std::string, int64_t>& class_usr_to_id);

  /** 批量插入 global_var，返回 usr -> global_var id 映射 */
  std::unordered_map<std::string, int64_t> WriteGlobalVars(
      int64_t project_id,
      const std::vector<GlobalVarRecord>& global_vars);

  /** 写入数据流边（var_usr/reader_usr/writer_usr 由传入映射解析为 id；缺失则跳过） */
  bool WriteDataFlowEdges(int64_t project_id,
                          const std::vector<DataFlowEdgeRecord>& edges,
                          const std::unordered_map<std::string, int64_t>& var_usr_to_id,
                          const std::unordered_map<std::string, int64_t>& symbol_usr_to_id);

  /** 批量插入 cfg_node，nodes 与 symbol_usr_to_id 解析 symbol_usr→symbol_id；返回与 nodes 同序的 node id 列表 */
  std::vector<int64_t> WriteCfgNodes(int64_t project_id,
                                     const std::vector<CfgNodeRecord>& nodes,
                                     const std::unordered_map<std::string, int64_t>& symbol_usr_to_id);

  /** 写入控制流边，from_node_index/to_node_index 为 node_ids 的下标 */
  bool WriteCfgEdges(int64_t project_id,
                     const std::vector<CfgEdgeRecord>& edges,
                     const std::vector<int64_t>& node_ids);

  /** 记录已解析文件（用于增量与懒解析） */
  bool UpdateParsedFile(int64_t project_id, int64_t file_id, int64_t parse_run_id,
                        int64_t file_mtime, const std::string& content_hash);

  /** 按 file 删除该文件相关的 symbol/call_edge/class/global_var/cfg 等（供 incremental 调用） */
  bool DeleteDataForFile(int64_t project_id, int64_t file_id);

 private:
  sqlite3* db_ = nullptr;
};

}  // namespace codexray

#endif  // CODEXRAY_PARSER_DB_WRITER_WRITER_H_
