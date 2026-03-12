/**
 * DB 写入器：批量写入解析结果；维护 USR→ID 和 path→ID 缓存。
 * 设计 §3.13 / §5.4。
 * CFG 数据以 protobuf 文件存储，不写入 SQLite 行存储。
 *
 * 性能优化：所有常用 SQL 语句在构造时预编译（PrepareStatements），
 * 析构时统一 finalize，避免每次调用重复 prepare_v2 + finalize 的开销。
 */
#pragma once
#include "common/analysis_output.h"
#include <sqlite3.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <shared_mutex>

namespace codexray {

class DbWriter {
public:
  /// @param db     已打开的 SQLite 连接
  /// @param project_id  当前项目 ID
  explicit DbWriter(sqlite3* db, int64_t project_id);

  /// 析构：finalize 所有预编译语句
  ~DbWriter();

  // 禁止拷贝（持有 sqlite3_stmt*）
  DbWriter(const DbWriter&) = delete;
  DbWriter& operator=(const DbWriter&) = delete;

  /// 设置数据库目录（CFG pb 文件存储于 <db_dir>/cfg/）
  void SetDbDir(const std::string& db_dir);

  /// 预注册文件路径，返回 file_id（如已存在直接返回缓存值）
  int64_t EnsureFile(const std::string& path);

  /// 写入全部聚合输出（在单个大事务中执行）
  bool WriteAll(const CombinedOutput& out);

  /// 更新 parsed_file 表（parse 完成后调用）
  bool UpdateParsedFile(int64_t file_id, int64_t parse_run_id,
                        int64_t mtime, const std::string& hash);

  /// 删除某文件的所有旧数据（增量更新前）
  bool DeleteDataForFile(int64_t file_id);

  /// 查 symbol_id by USR（先查缓存，再查 DB）
  int64_t GetOrInsertSymbolId(const SymbolRow& row);

  /// 查 global_var_id by USR
  int64_t GetOrInsertGlobalVarId(const GlobalVarRow& row);

  /// 查 class_id by USR
  int64_t GetOrInsertClassId(const ClassRow& row);

  sqlite3* Db() const { return db_; }
  int64_t  ProjectId() const { return project_id_; }

private:
  bool WriteSymbols(const std::vector<SymbolRow>& rows);
  bool WriteCallEdges(const std::vector<CallEdgeRow>& rows);
  bool WriteClasses(const std::vector<ClassRow>& rows);
  bool WriteClassRelations(const std::vector<ClassRelationRow>& rows);
  bool WriteClassMembers(const std::vector<ClassMemberRow>& rows);
  bool WriteGlobalVars(const std::vector<GlobalVarRow>& rows);
  bool WriteDataFlowEdges(const std::vector<DataFlowEdgeRow>& rows);
  /// 将 cfg_nodes/cfg_edges 序列化为 pb 文件，并更新 cfg_index 表
  bool WriteCfg(const std::vector<CfgNodeRow>& nodes,
                const std::vector<CfgEdgeRow>& edges);

  /// 预编译所有常用 SQL 语句（构造时调用）
  void PrepareStatements();

  /// finalize 并置空单个语句指针
  static void FinalizeStmt(sqlite3_stmt*& stmt);

  sqlite3* db_;
  int64_t  project_id_;
  std::string db_dir_;  // 数据库目录，用于定位 cfg/ 子目录

  // ── USR→ID 缓存 ────────────────────────────────────────────────────────
  mutable std::shared_mutex usr_sym_mu_;
  std::unordered_map<std::string, int64_t> usr_sym_cache_;

  mutable std::shared_mutex usr_gv_mu_;
  std::unordered_map<std::string, int64_t> usr_gv_cache_;

  mutable std::shared_mutex usr_cls_mu_;
  std::unordered_map<std::string, int64_t> usr_cls_cache_;

  mutable std::shared_mutex file_mu_;
  std::unordered_map<std::string, int64_t> file_cache_;

  // ── 预编译 SQL 语句（构造时 prepare，析构时 finalize）─────────────────
  sqlite3_stmt* stmt_insert_file_    = nullptr;  // EnsureFile: INSERT OR IGNORE
  sqlite3_stmt* stmt_select_file_    = nullptr;  // EnsureFile: SELECT id
  sqlite3_stmt* stmt_upsert_symbol_  = nullptr;  // GetOrInsertSymbolId: UPSERT
  sqlite3_stmt* stmt_select_symbol_  = nullptr;  // GetOrInsertSymbolId: SELECT id
  sqlite3_stmt* stmt_upsert_class_   = nullptr;  // GetOrInsertClassId: UPSERT
  sqlite3_stmt* stmt_select_class_   = nullptr;  // GetOrInsertClassId: SELECT id
  sqlite3_stmt* stmt_upsert_gvar_    = nullptr;  // GetOrInsertGlobalVarId: UPSERT
  sqlite3_stmt* stmt_select_gvar_    = nullptr;  // GetOrInsertGlobalVarId: SELECT id
  sqlite3_stmt* stmt_insert_call_edge_  = nullptr;
  sqlite3_stmt* stmt_insert_class_rel_  = nullptr;
  sqlite3_stmt* stmt_insert_class_mem_  = nullptr;
  sqlite3_stmt* stmt_insert_df_edge_    = nullptr;
  sqlite3_stmt* stmt_upsert_parsed_file_ = nullptr;
  sqlite3_stmt* stmt_upsert_cfg_index_   = nullptr;
};

}  // namespace codexray
