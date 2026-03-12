#include "writer.h"
#include "../../common/logger.h"
#include "cfg.pb.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace codexray {

// ─── helpers ──────────────────────────────────────────────────────────────────

/// 执行 step 并 reset（用于循环中逐行插入的预编译语句）
static bool StepAndReset(sqlite3_stmt* stmt) {
  int rc = sqlite3_step(stmt);
  sqlite3_reset(stmt);
  return rc == SQLITE_DONE || rc == SQLITE_ROW;
}

// ─── SQL 常量 ────────────────────────────────────────────────────────────────

static const char* kSqlInsertFile =
    "INSERT OR IGNORE INTO file(project_id, path) VALUES(?,?)";
static const char* kSqlSelectFile =
    "SELECT id FROM file WHERE project_id=? AND path=?";

static const char* kSqlUpsertSymbol =
    "INSERT INTO symbol(usr, name, qualified_name, kind,"
    " def_file_id, def_line, def_column, def_line_end, def_col_end,"
    " decl_file_id, decl_line, decl_column, decl_line_end, decl_col_end)"
    " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)"
    " ON CONFLICT(usr) DO UPDATE SET"
    "  name=excluded.name, qualified_name=excluded.qualified_name,"
    "  kind=excluded.kind,"
    "  def_file_id=COALESCE(excluded.def_file_id, def_file_id),"
    "  def_line=COALESCE(NULLIF(excluded.def_line,0), def_line),"
    "  def_column=COALESCE(NULLIF(excluded.def_column,0), def_column),"
    "  def_line_end=COALESCE(NULLIF(excluded.def_line_end,0), def_line_end),"
    "  def_col_end=COALESCE(NULLIF(excluded.def_col_end,0), def_col_end)";
static const char* kSqlSelectSymbol =
    "SELECT id FROM symbol WHERE usr=?";

static const char* kSqlUpsertClass =
    "INSERT INTO class(usr, name, qualified_name, def_file_id, def_line,"
    " def_column, def_line_end, def_col_end)"
    " VALUES(?,?,?,?,?,?,?,?)"
    " ON CONFLICT(usr) DO UPDATE SET name=excluded.name,"
    "  qualified_name=excluded.qualified_name,"
    "  def_file_id=COALESCE(excluded.def_file_id, def_file_id),"
    "  def_line=COALESCE(NULLIF(excluded.def_line,0), def_line),"
    "  def_column=COALESCE(NULLIF(excluded.def_column,0), def_column),"
    "  def_line_end=COALESCE(NULLIF(excluded.def_line_end,0), def_line_end),"
    "  def_col_end=COALESCE(NULLIF(excluded.def_col_end,0), def_col_end)";
static const char* kSqlSelectClass =
    "SELECT id FROM class WHERE usr=?";

static const char* kSqlUpsertGlobalVar =
    "INSERT INTO global_var(usr, name, qualified_name, def_file_id,"
    " def_line, def_column, def_line_end, def_col_end)"
    " VALUES(?,?,?,?,?,?,?,?)"
    " ON CONFLICT(usr) DO UPDATE SET name=excluded.name,"
    "  qualified_name=excluded.qualified_name,"
    "  def_file_id=COALESCE(excluded.def_file_id, def_file_id),"
    "  def_line=COALESCE(NULLIF(excluded.def_line,0), def_line),"
    "  def_column=COALESCE(NULLIF(excluded.def_column,0), def_column),"
    "  def_line_end=COALESCE(NULLIF(excluded.def_line_end,0), def_line_end),"
    "  def_col_end=COALESCE(NULLIF(excluded.def_col_end,0), def_col_end)";
static const char* kSqlSelectGlobalVar =
    "SELECT id FROM global_var WHERE usr=?";

static const char* kSqlInsertCallEdge =
    "INSERT OR IGNORE INTO call_edge(caller_id, callee_id, edge_type,"
    " call_file_id, call_line, call_column) VALUES(?,?,?,?,?,?)";

static const char* kSqlInsertClassRelation =
    "INSERT OR IGNORE INTO class_relation(parent_id, child_id, relation_type)"
    " VALUES(?,?,?)";

static const char* kSqlInsertClassMember =
    "INSERT OR IGNORE INTO class_member(class_id, member_name, member_type_str,"
    " member_symbol_id) VALUES(?,?,?,?)";

static const char* kSqlInsertDataFlowEdge =
    "INSERT OR IGNORE INTO data_flow_edge(var_id, accessor_id, access_type,"
    " access_file_id, access_line, access_column) VALUES(?,?,?,?,?,?)";

static const char* kSqlUpsertParsedFile =
    "INSERT INTO parsed_file(project_id, file_id, parse_run_id, file_mtime, file_hash)"
    " VALUES(?,?,?,?,?)"
    " ON CONFLICT(project_id, file_id) DO UPDATE SET"
    "  parse_run_id=excluded.parse_run_id,"
    "  file_mtime=excluded.file_mtime,"
    "  file_hash=excluded.file_hash,"
    "  parsed_at=datetime('now')";

static const char* kSqlUpsertCfgIndex =
    "INSERT OR REPLACE INTO cfg_index(symbol_id, pb_path) VALUES(?,?)";

// ─── constructor / destructor ────────────────────────────────────────────────

DbWriter::DbWriter(sqlite3* db, int64_t project_id)
    : db_(db), project_id_(project_id) {
  PrepareStatements();
}

DbWriter::~DbWriter() {
  FinalizeStmt(stmt_insert_file_);
  FinalizeStmt(stmt_select_file_);
  FinalizeStmt(stmt_upsert_symbol_);
  FinalizeStmt(stmt_select_symbol_);
  FinalizeStmt(stmt_upsert_class_);
  FinalizeStmt(stmt_select_class_);
  FinalizeStmt(stmt_upsert_gvar_);
  FinalizeStmt(stmt_select_gvar_);
  FinalizeStmt(stmt_insert_call_edge_);
  FinalizeStmt(stmt_insert_class_rel_);
  FinalizeStmt(stmt_insert_class_mem_);
  FinalizeStmt(stmt_insert_df_edge_);
  FinalizeStmt(stmt_upsert_parsed_file_);
  FinalizeStmt(stmt_upsert_cfg_index_);
}

void DbWriter::FinalizeStmt(sqlite3_stmt*& stmt) {
  if (stmt) { sqlite3_finalize(stmt); stmt = nullptr; }
}

/// 预编译所有常用 SQL 语句，避免每次调用 GetOrInsert* / Write* 时重复 prepare
void DbWriter::PrepareStatements() {
  sqlite3_prepare_v2(db_, kSqlInsertFile,    -1, &stmt_insert_file_,    nullptr);
  sqlite3_prepare_v2(db_, kSqlSelectFile,    -1, &stmt_select_file_,    nullptr);
  sqlite3_prepare_v2(db_, kSqlUpsertSymbol,  -1, &stmt_upsert_symbol_,  nullptr);
  sqlite3_prepare_v2(db_, kSqlSelectSymbol,  -1, &stmt_select_symbol_,  nullptr);
  sqlite3_prepare_v2(db_, kSqlUpsertClass,   -1, &stmt_upsert_class_,   nullptr);
  sqlite3_prepare_v2(db_, kSqlSelectClass,   -1, &stmt_select_class_,   nullptr);
  sqlite3_prepare_v2(db_, kSqlUpsertGlobalVar, -1, &stmt_upsert_gvar_, nullptr);
  sqlite3_prepare_v2(db_, kSqlSelectGlobalVar, -1, &stmt_select_gvar_, nullptr);
  sqlite3_prepare_v2(db_, kSqlInsertCallEdge,  -1, &stmt_insert_call_edge_,  nullptr);
  sqlite3_prepare_v2(db_, kSqlInsertClassRelation, -1, &stmt_insert_class_rel_, nullptr);
  sqlite3_prepare_v2(db_, kSqlInsertClassMember, -1, &stmt_insert_class_mem_, nullptr);
  sqlite3_prepare_v2(db_, kSqlInsertDataFlowEdge, -1, &stmt_insert_df_edge_, nullptr);
  sqlite3_prepare_v2(db_, kSqlUpsertParsedFile, -1, &stmt_upsert_parsed_file_, nullptr);
  sqlite3_prepare_v2(db_, kSqlUpsertCfgIndex, -1, &stmt_upsert_cfg_index_, nullptr);
}

// ─── SetDbDir ────────────────────────────────────────────────────────────────

void DbWriter::SetDbDir(const std::string& db_dir) {
  db_dir_ = db_dir;
}

// ─── EnsureFile ──────────────────────────────────────────────────────────────
// 使用预编译的 stmt_insert_file_ 和 stmt_select_file_，避免逐次 prepare/finalize

int64_t DbWriter::EnsureFile(const std::string& path) {
  {
    std::shared_lock<std::shared_mutex> lk(file_mu_);
    auto it = file_cache_.find(path);
    if (it != file_cache_.end()) return it->second;
  }
  // INSERT OR IGNORE
  sqlite3_bind_int64(stmt_insert_file_, 1, project_id_);
  sqlite3_bind_text(stmt_insert_file_, 2, path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(stmt_insert_file_);
  sqlite3_reset(stmt_insert_file_);

  // SELECT id
  sqlite3_bind_int64(stmt_select_file_, 1, project_id_);
  sqlite3_bind_text(stmt_select_file_, 2, path.c_str(), -1, SQLITE_TRANSIENT);
  int64_t fid = 0;
  if (sqlite3_step(stmt_select_file_) == SQLITE_ROW)
    fid = sqlite3_column_int64(stmt_select_file_, 0);
  sqlite3_reset(stmt_select_file_);

  if (fid > 0) {
    std::unique_lock<std::shared_mutex> lk(file_mu_);
    file_cache_[path] = fid;
  }
  return fid;
}

// ─── WriteAll ────────────────────────────────────────────────────────────────

bool DbWriter::WriteAll(const CombinedOutput& out) {
  // 预注册所有引用文件
  for (const auto& p : out.referenced_files) EnsureFile(p);

  if (!WriteSymbols(out.symbols))                   return false;
  if (!WriteClasses(out.classes))                   return false;
  if (!WriteGlobalVars(out.global_vars))            return false;
  if (!WriteCallEdges(out.call_edges))              return false;
  if (!WriteClassRelations(out.class_relations))    return false;
  if (!WriteClassMembers(out.class_members))        return false;
  if (!WriteDataFlowEdges(out.data_flow_edges))     return false;
  if (!WriteCfg(out.cfg_nodes, out.cfg_edges))      return false;
  return true;
}

// ─── GetOrInsertSymbolId ─────────────────────────────────────────────────────
// 使用预编译的 stmt_upsert_symbol_ 和 stmt_select_symbol_

int64_t DbWriter::GetOrInsertSymbolId(const SymbolRow& row) {
  if (row.usr.empty()) return 0;
  {
    std::shared_lock<std::shared_mutex> lk(usr_sym_mu_);
    auto it = usr_sym_cache_.find(row.usr);
    if (it != usr_sym_cache_.end()) return it->second;
  }
  // UPSERT
  sqlite3_bind_text(stmt_upsert_symbol_, 1,  row.usr.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt_upsert_symbol_, 2,  row.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt_upsert_symbol_, 3,  row.qualified_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt_upsert_symbol_, 4,  row.kind.c_str(), -1, SQLITE_TRANSIENT);
  int64_t def_fid = row.def_file_id > 0 ? row.def_file_id :
                    (!row.def_file_path.empty() ? EnsureFile(row.def_file_path) : 0);
  if (def_fid > 0) sqlite3_bind_int64(stmt_upsert_symbol_, 5, def_fid);
  else sqlite3_bind_null(stmt_upsert_symbol_, 5);
  sqlite3_bind_int(stmt_upsert_symbol_, 6,  row.def_line);
  sqlite3_bind_int(stmt_upsert_symbol_, 7,  row.def_column);
  sqlite3_bind_int(stmt_upsert_symbol_, 8,  row.def_line_end);
  sqlite3_bind_int(stmt_upsert_symbol_, 9,  row.def_col_end);
  int64_t decl_fid = row.decl_file_id > 0 ? row.decl_file_id :
                     (!row.decl_file_path.empty() ? EnsureFile(row.decl_file_path) : 0);
  if (decl_fid > 0) sqlite3_bind_int64(stmt_upsert_symbol_, 10, decl_fid);
  else sqlite3_bind_null(stmt_upsert_symbol_, 10);
  sqlite3_bind_int(stmt_upsert_symbol_, 11, row.decl_line);
  sqlite3_bind_int(stmt_upsert_symbol_, 12, row.decl_column);
  sqlite3_bind_int(stmt_upsert_symbol_, 13, row.decl_line_end);
  sqlite3_bind_int(stmt_upsert_symbol_, 14, row.decl_col_end);
  sqlite3_step(stmt_upsert_symbol_);
  sqlite3_reset(stmt_upsert_symbol_);

  // SELECT id
  sqlite3_bind_text(stmt_select_symbol_, 1, row.usr.c_str(), -1, SQLITE_TRANSIENT);
  int64_t id = 0;
  if (sqlite3_step(stmt_select_symbol_) == SQLITE_ROW)
    id = sqlite3_column_int64(stmt_select_symbol_, 0);
  sqlite3_reset(stmt_select_symbol_);

  if (id > 0) {
    std::unique_lock<std::shared_mutex> lk(usr_sym_mu_);
    usr_sym_cache_[row.usr] = id;
  }
  return id;
}

// ─── WriteSymbols ─────────────────────────────────────────────────────────────

bool DbWriter::WriteSymbols(const std::vector<SymbolRow>& rows) {
  for (const auto& r : rows) GetOrInsertSymbolId(r);
  return true;
}

// ─── GetOrInsertClassId ──────────────────────────────────────────────────────
// 使用预编译的 stmt_upsert_class_ 和 stmt_select_class_

int64_t DbWriter::GetOrInsertClassId(const ClassRow& row) {
  if (row.usr.empty()) return 0;
  {
    std::shared_lock<std::shared_mutex> lk(usr_cls_mu_);
    auto it = usr_cls_cache_.find(row.usr);
    if (it != usr_cls_cache_.end()) return it->second;
  }
  // UPSERT
  sqlite3_bind_text(stmt_upsert_class_, 1, row.usr.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt_upsert_class_, 2, row.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt_upsert_class_, 3, row.qualified_name.c_str(), -1, SQLITE_TRANSIENT);
  {
    int64_t fid = row.def_file_id > 0 ? row.def_file_id :
                  (!row.def_file_path.empty() ? EnsureFile(row.def_file_path) : 0);
    if (fid > 0) sqlite3_bind_int64(stmt_upsert_class_, 4, fid);
    else sqlite3_bind_null(stmt_upsert_class_, 4);
  }
  sqlite3_bind_int(stmt_upsert_class_, 5, row.def_line);
  sqlite3_bind_int(stmt_upsert_class_, 6, row.def_column);
  sqlite3_bind_int(stmt_upsert_class_, 7, row.def_line_end);
  sqlite3_bind_int(stmt_upsert_class_, 8, row.def_col_end);
  sqlite3_step(stmt_upsert_class_);
  sqlite3_reset(stmt_upsert_class_);

  // SELECT id
  sqlite3_bind_text(stmt_select_class_, 1, row.usr.c_str(), -1, SQLITE_TRANSIENT);
  int64_t id = 0;
  if (sqlite3_step(stmt_select_class_) == SQLITE_ROW)
    id = sqlite3_column_int64(stmt_select_class_, 0);
  sqlite3_reset(stmt_select_class_);

  if (id > 0) {
    std::unique_lock<std::shared_mutex> lk(usr_cls_mu_);
    usr_cls_cache_[row.usr] = id;
  }
  return id;
}

bool DbWriter::WriteClasses(const std::vector<ClassRow>& rows) {
  for (const auto& r : rows) GetOrInsertClassId(r);
  return true;
}

// ─── GetOrInsertGlobalVarId ───────────────────────────────────────────────────
// 使用预编译的 stmt_upsert_gvar_ 和 stmt_select_gvar_

int64_t DbWriter::GetOrInsertGlobalVarId(const GlobalVarRow& row) {
  if (row.usr.empty()) return 0;
  {
    std::shared_lock<std::shared_mutex> lk(usr_gv_mu_);
    auto it = usr_gv_cache_.find(row.usr);
    if (it != usr_gv_cache_.end()) return it->second;
  }
  // UPSERT
  sqlite3_bind_text(stmt_upsert_gvar_, 1, row.usr.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt_upsert_gvar_, 2, row.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt_upsert_gvar_, 3, row.qualified_name.c_str(), -1, SQLITE_TRANSIENT);
  {
    int64_t fid = row.def_file_id > 0 ? row.def_file_id :
                  (!row.def_file_path.empty() ? EnsureFile(row.def_file_path) : 0);
    if (fid > 0) sqlite3_bind_int64(stmt_upsert_gvar_, 4, fid);
    else sqlite3_bind_null(stmt_upsert_gvar_, 4);
  }
  sqlite3_bind_int(stmt_upsert_gvar_, 5, row.def_line);
  sqlite3_bind_int(stmt_upsert_gvar_, 6, row.def_column);
  sqlite3_bind_int(stmt_upsert_gvar_, 7, row.def_line_end);
  sqlite3_bind_int(stmt_upsert_gvar_, 8, row.def_col_end);
  sqlite3_step(stmt_upsert_gvar_);
  sqlite3_reset(stmt_upsert_gvar_);

  // SELECT id
  sqlite3_bind_text(stmt_select_gvar_, 1, row.usr.c_str(), -1, SQLITE_TRANSIENT);
  int64_t id = 0;
  if (sqlite3_step(stmt_select_gvar_) == SQLITE_ROW)
    id = sqlite3_column_int64(stmt_select_gvar_, 0);
  sqlite3_reset(stmt_select_gvar_);

  if (id > 0) {
    std::unique_lock<std::shared_mutex> lk(usr_gv_mu_);
    usr_gv_cache_[row.usr] = id;
  }
  return id;
}

bool DbWriter::WriteGlobalVars(const std::vector<GlobalVarRow>& rows) {
  for (const auto& r : rows) GetOrInsertGlobalVarId(r);
  return true;
}

// ─── WriteCallEdges ───────────────────────────────────────────────────────────
// 使用预编译的 stmt_insert_call_edge_

bool DbWriter::WriteCallEdges(const std::vector<CallEdgeRow>& rows) {
  if (rows.empty()) return true;
  for (const auto& r : rows) {
    int64_t caller_id = 0, callee_id = 0;
    {
      std::shared_lock<std::shared_mutex> lk(usr_sym_mu_);
      auto it = usr_sym_cache_.find(r.caller_usr);
      if (it != usr_sym_cache_.end()) caller_id = it->second;
      it = usr_sym_cache_.find(r.callee_usr);
      if (it != usr_sym_cache_.end()) callee_id = it->second;
    }
    if (caller_id == 0 || callee_id == 0) continue;
    sqlite3_bind_int64(stmt_insert_call_edge_, 1, caller_id);
    sqlite3_bind_int64(stmt_insert_call_edge_, 2, callee_id);
    sqlite3_bind_text(stmt_insert_call_edge_, 3, r.edge_type.c_str(), -1, SQLITE_TRANSIENT);
    int64_t cfid = r.call_file_id > 0 ? r.call_file_id :
                   (!r.call_file_path.empty() ? EnsureFile(r.call_file_path) : 0);
    if (cfid > 0) sqlite3_bind_int64(stmt_insert_call_edge_, 4, cfid);
    else sqlite3_bind_null(stmt_insert_call_edge_, 4);
    sqlite3_bind_int(stmt_insert_call_edge_, 5, r.call_line);
    sqlite3_bind_int(stmt_insert_call_edge_, 6, r.call_column);
    sqlite3_step(stmt_insert_call_edge_);
    sqlite3_reset(stmt_insert_call_edge_);
  }
  return true;
}

// ─── WriteClassRelations ─────────────────────────────────────────────────────
// 使用预编译的 stmt_insert_class_rel_

bool DbWriter::WriteClassRelations(const std::vector<ClassRelationRow>& rows) {
  if (rows.empty()) return true;
  for (const auto& r : rows) {
    int64_t pid = 0, cid = 0;
    {
      std::shared_lock<std::shared_mutex> lk(usr_cls_mu_);
      auto it = usr_cls_cache_.find(r.parent_usr);
      if (it != usr_cls_cache_.end()) pid = it->second;
      it = usr_cls_cache_.find(r.child_usr);
      if (it != usr_cls_cache_.end()) cid = it->second;
    }
    if (pid == 0 || cid == 0) continue;
    sqlite3_bind_int64(stmt_insert_class_rel_, 1, pid);
    sqlite3_bind_int64(stmt_insert_class_rel_, 2, cid);
    sqlite3_bind_text(stmt_insert_class_rel_, 3, r.relation_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt_insert_class_rel_);
    sqlite3_reset(stmt_insert_class_rel_);
  }
  return true;
}

// ─── WriteClassMembers ────────────────────────────────────────────────────────
// 使用预编译的 stmt_insert_class_mem_

bool DbWriter::WriteClassMembers(const std::vector<ClassMemberRow>& rows) {
  if (rows.empty()) return true;
  for (const auto& r : rows) {
    int64_t cls_id = 0;
    {
      std::shared_lock<std::shared_mutex> lk(usr_cls_mu_);
      auto it = usr_cls_cache_.find(r.class_usr);
      if (it != usr_cls_cache_.end()) cls_id = it->second;
    }
    if (cls_id == 0) continue;
    sqlite3_bind_int64(stmt_insert_class_mem_, 1, cls_id);
    sqlite3_bind_text(stmt_insert_class_mem_, 2, r.member_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt_insert_class_mem_, 3, r.member_type_str.c_str(), -1, SQLITE_TRANSIENT);
    int64_t sym_id = 0;
    if (!r.member_usr.empty()) {
      std::shared_lock<std::shared_mutex> lk(usr_sym_mu_);
      auto it = usr_sym_cache_.find(r.member_usr);
      if (it != usr_sym_cache_.end()) sym_id = it->second;
    }
    if (sym_id > 0) sqlite3_bind_int64(stmt_insert_class_mem_, 4, sym_id);
    else sqlite3_bind_null(stmt_insert_class_mem_, 4);
    sqlite3_step(stmt_insert_class_mem_);
    sqlite3_reset(stmt_insert_class_mem_);
  }
  return true;
}

// ─── WriteDataFlowEdges ───────────────────────────────────────────────────────
// 使用预编译的 stmt_insert_df_edge_

bool DbWriter::WriteDataFlowEdges(const std::vector<DataFlowEdgeRow>& rows) {
  if (rows.empty()) return true;
  for (const auto& r : rows) {
    int64_t vid = 0, aid = 0;
    {
      std::shared_lock<std::shared_mutex> lk(usr_gv_mu_);
      auto it = usr_gv_cache_.find(r.var_usr);
      if (it != usr_gv_cache_.end()) vid = it->second;
    }
    {
      std::shared_lock<std::shared_mutex> lk(usr_sym_mu_);
      auto it = usr_sym_cache_.find(r.accessor_usr);
      if (it != usr_sym_cache_.end()) aid = it->second;
    }
    if (vid == 0 || aid == 0) continue;
    sqlite3_bind_int64(stmt_insert_df_edge_, 1, vid);
    sqlite3_bind_int64(stmt_insert_df_edge_, 2, aid);
    sqlite3_bind_text(stmt_insert_df_edge_, 3, r.access_type.c_str(), -1, SQLITE_TRANSIENT);
    int64_t afid = r.access_file_id > 0 ? r.access_file_id :
                   (!r.access_file_path.empty() ? EnsureFile(r.access_file_path) : 0);
    if (afid > 0) sqlite3_bind_int64(stmt_insert_df_edge_, 4, afid);
    else sqlite3_bind_null(stmt_insert_df_edge_, 4);
    sqlite3_bind_int(stmt_insert_df_edge_, 5, r.access_line);
    sqlite3_bind_int(stmt_insert_df_edge_, 6, r.access_column);
    sqlite3_step(stmt_insert_df_edge_);
    sqlite3_reset(stmt_insert_df_edge_);
  }
  return true;
}

// ─── FNV-1a 64-bit 哈希（无外部依赖，用于计算 pb 文件路径）─────────────────

static std::string Fnv1a64Hex(const std::string& s) {
  uint64_t h = 14695981039346656037ULL;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  char buf[17];
  std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
  return std::string(buf);
}

// ─── WriteCfg ─────────────────────────────────────────────────────────────────
// 将 CFG 数据序列化为 protobuf 文件，并在 cfg_index 表中记录索引。
// 每个函数独立一个 pb 文件，路径为 cfg/<hash[0:2]>/<hash>.pb（相对 db_dir_）。
// 使用预编译的 stmt_upsert_cfg_index_

bool DbWriter::WriteCfg(const std::vector<CfgNodeRow>& nodes,
                         const std::vector<CfgEdgeRow>& edges) {
  if (nodes.empty()) return true;
  if (db_dir_.empty()) {
    LogError("WriteCfg: db_dir_ not set, cannot write pb files");
    return false;
  }

  // 按 function_usr 分组 nodes
  std::unordered_map<std::string, std::vector<const CfgNodeRow*>> nodes_by_usr;
  for (const auto& r : nodes) {
    if (!r.function_usr.empty()) nodes_by_usr[r.function_usr].push_back(&r);
  }

  // 按 function_usr 分组 edges
  std::unordered_map<std::string, std::vector<const CfgEdgeRow*>> edges_by_usr;
  for (const auto& r : edges) {
    if (!r.function_usr.empty()) edges_by_usr[r.function_usr].push_back(&r);
  }

  bool all_ok = true;
  for (auto& [usr, node_ptrs] : nodes_by_usr) {
    int64_t sym_id = 0;
    {
      std::shared_lock<std::shared_mutex> lk(usr_sym_mu_);
      auto it = usr_sym_cache_.find(usr);
      if (it != usr_sym_cache_.end()) sym_id = it->second;
    }
    if (sym_id == 0) continue;

    std::string hex = Fnv1a64Hex(usr);
    std::string rel_path = "cfg/" + hex.substr(0, 2) + "/" + hex + ".pb";
    std::string abs_path = db_dir_ + "/" + rel_path;

    // 构建 FunctionCfg protobuf 消息
    codexray::cfg::FunctionCfg proto;
    proto.set_function_usr(usr);
    for (const auto* n : node_ptrs) {
      auto* pn = proto.add_nodes();
      pn->set_block_id(n->block_id);
      pn->set_file_path(n->file_path);
      pn->set_begin_line(n->begin_line);
      pn->set_begin_col(n->begin_col);
      pn->set_end_line(n->end_line);
      pn->set_end_col(n->end_col);
      pn->set_label(n->label);
    }
    auto eit = edges_by_usr.find(usr);
    if (eit != edges_by_usr.end()) {
      for (const auto* e : eit->second) {
        auto* pe = proto.add_edges();
        pe->set_from_block(e->from_block);
        pe->set_to_block(e->to_block);
        pe->set_edge_type(e->edge_type);
      }
    }

    // 创建目录并写入 pb 文件
    fs::path dir = fs::path(abs_path).parent_path();
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
      LogError("WriteCfg: cannot create dir " + dir.string() + ": " + ec.message());
      all_ok = false;
      continue;
    }
    std::string data;
    if (!proto.SerializeToString(&data)) {
      LogError("WriteCfg: SerializeToString failed for usr=" + usr);
      all_ok = false;
      continue;
    }
    std::ofstream ofs(abs_path, std::ios::binary | std::ios::trunc);
    if (!ofs) {
      LogError("WriteCfg: cannot open " + abs_path + " for writing");
      all_ok = false;
      continue;
    }
    ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
    ofs.close();

    // 更新 cfg_index 表
    sqlite3_bind_int64(stmt_upsert_cfg_index_, 1, sym_id);
    sqlite3_bind_text(stmt_upsert_cfg_index_, 2, rel_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt_upsert_cfg_index_);
    sqlite3_reset(stmt_upsert_cfg_index_);
  }

  return all_ok;
}

// ─── UpdateParsedFile ─────────────────────────────────────────────────────────
// 使用预编译的 stmt_upsert_parsed_file_

bool DbWriter::UpdateParsedFile(int64_t file_id, int64_t parse_run_id,
                                int64_t mtime, const std::string& hash) {
  sqlite3_bind_int64(stmt_upsert_parsed_file_, 1, project_id_);
  sqlite3_bind_int64(stmt_upsert_parsed_file_, 2, file_id);
  sqlite3_bind_int64(stmt_upsert_parsed_file_, 3, parse_run_id);
  sqlite3_bind_int64(stmt_upsert_parsed_file_, 4, mtime);
  sqlite3_bind_text(stmt_upsert_parsed_file_, 5, hash.c_str(), -1, SQLITE_TRANSIENT);
  bool ok = (sqlite3_step(stmt_upsert_parsed_file_) == SQLITE_DONE);
  sqlite3_reset(stmt_upsert_parsed_file_);
  return ok;
}

// ─── DeleteDataForFile ────────────────────────────────────────────────────────

bool DbWriter::DeleteDataForFile(int64_t file_id) {
  // 删除操作不频繁，不需要预编译语句，使用 lambda 简化
  auto exec = [&](const char* sql, int64_t fid) {
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, fid);
    sqlite3_step(s);
    sqlite3_finalize(s);
  };
  // data_flow_edge referencing symbols in this file's vars
  exec("DELETE FROM data_flow_edge WHERE var_id IN "
       "(SELECT id FROM global_var WHERE def_file_id=?)", file_id);
  // 清理 cfg_index 及对应的 pb 文件
  if (!db_dir_.empty()) {
    sqlite3_stmt* sel = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT ci.pb_path FROM cfg_index ci"
        " JOIN symbol s ON ci.symbol_id = s.id"
        " WHERE s.def_file_id = ?",
        -1, &sel, nullptr);
    sqlite3_bind_int64(sel, 1, file_id);
    while (sqlite3_step(sel) == SQLITE_ROW) {
      const char* rel = reinterpret_cast<const char*>(sqlite3_column_text(sel, 0));
      if (rel) {
        std::error_code ec;
        fs::remove(fs::path(db_dir_) / rel, ec);
      }
    }
    sqlite3_finalize(sel);
  }
  exec("DELETE FROM cfg_index WHERE symbol_id IN "
       "(SELECT id FROM symbol WHERE def_file_id=?)", file_id);
  exec("DELETE FROM call_edge WHERE caller_id IN "
       "(SELECT id FROM symbol WHERE def_file_id=?)", file_id);
  exec("DELETE FROM class_member WHERE class_id IN "
       "(SELECT id FROM class WHERE def_file_id=?)", file_id);
  // class_relation 需要两个绑定参数
  {
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(db_,
        "DELETE FROM class_relation WHERE parent_id IN "
        "(SELECT id FROM class WHERE def_file_id=?)"
        " OR child_id IN (SELECT id FROM class WHERE def_file_id=?)",
        -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, file_id);
    sqlite3_bind_int64(s, 2, file_id);
    sqlite3_step(s);
    sqlite3_finalize(s);
  }
  exec("DELETE FROM class WHERE def_file_id=?", file_id);
  exec("DELETE FROM global_var WHERE def_file_id=?", file_id);
  exec("DELETE FROM symbol WHERE def_file_id=?", file_id);
  exec("DELETE FROM parsed_file WHERE file_id=? AND project_id=?", file_id);
  // 清空缓存（增量重解析时可能 ID 变化）
  {
    std::unique_lock<std::shared_mutex> lk1(usr_sym_mu_);
    usr_sym_cache_.clear();
  }
  {
    std::unique_lock<std::shared_mutex> lk2(usr_cls_mu_);
    usr_cls_cache_.clear();
  }
  {
    std::unique_lock<std::shared_mutex> lk3(usr_gv_mu_);
    usr_gv_cache_.clear();
  }
  return true;
}

}  // namespace codexray
