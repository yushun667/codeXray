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

static bool StepAndReset(sqlite3_stmt* stmt) {
  int rc = sqlite3_step(stmt);
  sqlite3_reset(stmt);
  return rc == SQLITE_DONE || rc == SQLITE_ROW;
}

// ─── constructor ─────────────────────────────────────────────────────────────

DbWriter::DbWriter(sqlite3* db, int64_t project_id)
    : db_(db), project_id_(project_id) {}

// ─── SetDbDir ────────────────────────────────────────────────────────────────

void DbWriter::SetDbDir(const std::string& db_dir) {
  db_dir_ = db_dir;
}

// ─── EnsureFile ──────────────────────────────────────────────────────────────

int64_t DbWriter::EnsureFile(const std::string& path) {
  {
    std::shared_lock<std::shared_mutex> lk(file_mu_);
    auto it = file_cache_.find(path);
    if (it != file_cache_.end()) return it->second;
  }
  // INSERT OR IGNORE, then SELECT
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_,
      "INSERT OR IGNORE INTO file(project_id, path) VALUES(?,?)", -1, &stmt, nullptr);
  sqlite3_bind_int64(stmt, 1, project_id_);
  sqlite3_bind_text(stmt, 2, path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  sqlite3_prepare_v2(db_,
      "SELECT id FROM file WHERE project_id=? AND path=?", -1, &stmt, nullptr);
  sqlite3_bind_int64(stmt, 1, project_id_);
  sqlite3_bind_text(stmt, 2, path.c_str(), -1, SQLITE_TRANSIENT);
  int64_t fid = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) fid = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);

  if (fid > 0) {
    std::unique_lock<std::shared_mutex> lk(file_mu_);
    file_cache_[path] = fid;
  }
  return fid;
}

// ─── WriteAll ────────────────────────────────────────────────────────────────

bool DbWriter::WriteAll(const CombinedOutput& out) {
  // Pre-register all referenced files
  for (const auto& p : out.referenced_files) EnsureFile(p);

  if (!WriteSymbols(out.symbols))             return false;
  if (!WriteClasses(out.classes))             return false;
  if (!WriteGlobalVars(out.global_vars))      return false;
  if (!WriteCallEdges(out.call_edges))        return false;
  if (!WriteClassRelations(out.class_relations)) return false;
  if (!WriteClassMembers(out.class_members))  return false;
  if (!WriteDataFlowEdges(out.data_flow_edges)) return false;
  if (!WriteCfg(out.cfg_nodes, out.cfg_edges)) return false;
  return true;
}

// ─── GetOrInsertSymbolId ─────────────────────────────────────────────────────

int64_t DbWriter::GetOrInsertSymbolId(const SymbolRow& row) {
  if (row.usr.empty()) return 0;
  {
    std::shared_lock<std::shared_mutex> lk(usr_sym_mu_);
    auto it = usr_sym_cache_.find(row.usr);
    if (it != usr_sym_cache_.end()) return it->second;
  }
  // UPSERT
  const char* sql =
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
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1,  row.usr.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2,  row.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3,  row.qualified_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4,  row.kind.c_str(), -1, SQLITE_TRANSIENT);
  int64_t def_fid = row.def_file_id > 0 ? row.def_file_id :
                    (!row.def_file_path.empty() ? EnsureFile(row.def_file_path) : 0);
  if (def_fid > 0) sqlite3_bind_int64(stmt, 5, def_fid);
  else sqlite3_bind_null(stmt, 5);
  sqlite3_bind_int(stmt, 6,  row.def_line);
  sqlite3_bind_int(stmt, 7,  row.def_column);
  sqlite3_bind_int(stmt, 8,  row.def_line_end);
  sqlite3_bind_int(stmt, 9,  row.def_col_end);
  int64_t decl_fid = row.decl_file_id > 0 ? row.decl_file_id :
                     (!row.decl_file_path.empty() ? EnsureFile(row.decl_file_path) : 0);
  if (decl_fid > 0) sqlite3_bind_int64(stmt, 10, decl_fid);
  else sqlite3_bind_null(stmt, 10);
  sqlite3_bind_int(stmt, 11, row.decl_line);
  sqlite3_bind_int(stmt, 12, row.decl_column);
  sqlite3_bind_int(stmt, 13, row.decl_line_end);
  sqlite3_bind_int(stmt, 14, row.decl_col_end);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  // SELECT id
  sqlite3_stmt* sel = nullptr;
  sqlite3_prepare_v2(db_, "SELECT id FROM symbol WHERE usr=?", -1, &sel, nullptr);
  sqlite3_bind_text(sel, 1, row.usr.c_str(), -1, SQLITE_TRANSIENT);
  int64_t id = 0;
  if (sqlite3_step(sel) == SQLITE_ROW) id = sqlite3_column_int64(sel, 0);
  sqlite3_finalize(sel);

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

int64_t DbWriter::GetOrInsertClassId(const ClassRow& row) {
  if (row.usr.empty()) return 0;
  {
    std::shared_lock<std::shared_mutex> lk(usr_cls_mu_);
    auto it = usr_cls_cache_.find(row.usr);
    if (it != usr_cls_cache_.end()) return it->second;
  }
  const char* sql =
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
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, row.usr.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, row.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, row.qualified_name.c_str(), -1, SQLITE_TRANSIENT);
  {
    int64_t fid = row.def_file_id > 0 ? row.def_file_id :
                  (!row.def_file_path.empty() ? EnsureFile(row.def_file_path) : 0);
    if (fid > 0) sqlite3_bind_int64(stmt, 4, fid);
    else sqlite3_bind_null(stmt, 4);
  }
  sqlite3_bind_int(stmt, 5, row.def_line);
  sqlite3_bind_int(stmt, 6, row.def_column);
  sqlite3_bind_int(stmt, 7, row.def_line_end);
  sqlite3_bind_int(stmt, 8, row.def_col_end);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  sqlite3_stmt* sel = nullptr;
  sqlite3_prepare_v2(db_, "SELECT id FROM class WHERE usr=?", -1, &sel, nullptr);
  sqlite3_bind_text(sel, 1, row.usr.c_str(), -1, SQLITE_TRANSIENT);
  int64_t id = 0;
  if (sqlite3_step(sel) == SQLITE_ROW) id = sqlite3_column_int64(sel, 0);
  sqlite3_finalize(sel);

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

int64_t DbWriter::GetOrInsertGlobalVarId(const GlobalVarRow& row) {
  if (row.usr.empty()) return 0;
  {
    std::shared_lock<std::shared_mutex> lk(usr_gv_mu_);
    auto it = usr_gv_cache_.find(row.usr);
    if (it != usr_gv_cache_.end()) return it->second;
  }
  const char* sql =
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
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, row.usr.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, row.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, row.qualified_name.c_str(), -1, SQLITE_TRANSIENT);
  {
    int64_t fid = row.def_file_id > 0 ? row.def_file_id :
                  (!row.def_file_path.empty() ? EnsureFile(row.def_file_path) : 0);
    if (fid > 0) sqlite3_bind_int64(stmt, 4, fid);
    else sqlite3_bind_null(stmt, 4);
  }
  sqlite3_bind_int(stmt, 5, row.def_line);
  sqlite3_bind_int(stmt, 6, row.def_column);
  sqlite3_bind_int(stmt, 7, row.def_line_end);
  sqlite3_bind_int(stmt, 8, row.def_col_end);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  sqlite3_stmt* sel = nullptr;
  sqlite3_prepare_v2(db_, "SELECT id FROM global_var WHERE usr=?", -1, &sel, nullptr);
  sqlite3_bind_text(sel, 1, row.usr.c_str(), -1, SQLITE_TRANSIENT);
  int64_t id = 0;
  if (sqlite3_step(sel) == SQLITE_ROW) id = sqlite3_column_int64(sel, 0);
  sqlite3_finalize(sel);

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

bool DbWriter::WriteCallEdges(const std::vector<CallEdgeRow>& rows) {
  if (rows.empty()) return true;
  const char* sql =
      "INSERT OR IGNORE INTO call_edge(caller_id, callee_id, edge_type,"
      " call_file_id, call_line, call_column) VALUES(?,?,?,?,?,?)";
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
  for (const auto& r : rows) {
    // Caller / callee might be 0 if USR not in DB — skip
    int64_t caller_id = 0, callee_id = 0;
    {
      std::shared_lock<std::shared_mutex> lk(usr_sym_mu_);
      auto it = usr_sym_cache_.find(r.caller_usr);
      if (it != usr_sym_cache_.end()) caller_id = it->second;
      it = usr_sym_cache_.find(r.callee_usr);
      if (it != usr_sym_cache_.end()) callee_id = it->second;
    }
    if (caller_id == 0 || callee_id == 0) continue;
    sqlite3_bind_int64(stmt, 1, caller_id);
    sqlite3_bind_int64(stmt, 2, callee_id);
    sqlite3_bind_text(stmt, 3, r.edge_type.c_str(), -1, SQLITE_TRANSIENT);
    int64_t cfid = r.call_file_id > 0 ? r.call_file_id :
                   (!r.call_file_path.empty() ? EnsureFile(r.call_file_path) : 0);
    if (cfid > 0) sqlite3_bind_int64(stmt, 4, cfid);
    else sqlite3_bind_null(stmt, 4);
    sqlite3_bind_int(stmt, 5, r.call_line);
    sqlite3_bind_int(stmt, 6, r.call_column);
    sqlite3_step(stmt);
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
  return true;
}

// ─── WriteClassRelations ─────────────────────────────────────────────────────

bool DbWriter::WriteClassRelations(const std::vector<ClassRelationRow>& rows) {
  if (rows.empty()) return true;
  const char* sql =
      "INSERT OR IGNORE INTO class_relation(parent_id, child_id, relation_type)"
      " VALUES(?,?,?)";
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
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
    sqlite3_bind_int64(stmt, 1, pid);
    sqlite3_bind_int64(stmt, 2, cid);
    sqlite3_bind_text(stmt, 3, r.relation_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
  return true;
}

// ─── WriteClassMembers ────────────────────────────────────────────────────────

bool DbWriter::WriteClassMembers(const std::vector<ClassMemberRow>& rows) {
  if (rows.empty()) return true;
  const char* sql =
      "INSERT OR IGNORE INTO class_member(class_id, member_name, member_type_str,"
      " member_symbol_id) VALUES(?,?,?,?)";
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
  for (const auto& r : rows) {
    int64_t cls_id = 0;
    {
      std::shared_lock<std::shared_mutex> lk(usr_cls_mu_);
      auto it = usr_cls_cache_.find(r.class_usr);
      if (it != usr_cls_cache_.end()) cls_id = it->second;
    }
    if (cls_id == 0) continue;
    sqlite3_bind_int64(stmt, 1, cls_id);
    sqlite3_bind_text(stmt, 2, r.member_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, r.member_type_str.c_str(), -1, SQLITE_TRANSIENT);
    int64_t sym_id = 0;
    if (!r.member_usr.empty()) {
      std::shared_lock<std::shared_mutex> lk(usr_sym_mu_);
      auto it = usr_sym_cache_.find(r.member_usr);
      if (it != usr_sym_cache_.end()) sym_id = it->second;
    }
    if (sym_id > 0) sqlite3_bind_int64(stmt, 4, sym_id);
    else sqlite3_bind_null(stmt, 4);
    sqlite3_step(stmt);
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
  return true;
}

// ─── WriteDataFlowEdges ───────────────────────────────────────────────────────

bool DbWriter::WriteDataFlowEdges(const std::vector<DataFlowEdgeRow>& rows) {
  if (rows.empty()) return true;
  const char* sql =
      "INSERT OR IGNORE INTO data_flow_edge(var_id, accessor_id, access_type,"
      " access_file_id, access_line, access_column) VALUES(?,?,?,?,?,?)";
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
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
    sqlite3_bind_int64(stmt, 1, vid);
    sqlite3_bind_int64(stmt, 2, aid);
    sqlite3_bind_text(stmt, 3, r.access_type.c_str(), -1, SQLITE_TRANSIENT);
    int64_t afid = r.access_file_id > 0 ? r.access_file_id :
                   (!r.access_file_path.empty() ? EnsureFile(r.access_file_path) : 0);
    if (afid > 0) sqlite3_bind_int64(stmt, 4, afid);
    else sqlite3_bind_null(stmt, 4);
    sqlite3_bind_int(stmt, 5, r.access_line);
    sqlite3_bind_int(stmt, 6, r.access_column);
    sqlite3_step(stmt);
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
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

  // 准备 cfg_index INSERT/REPLACE 语句
  sqlite3_stmt* idx_stmt = nullptr;
  sqlite3_prepare_v2(db_,
      "INSERT OR REPLACE INTO cfg_index(symbol_id, pb_path) VALUES(?,?)",
      -1, &idx_stmt, nullptr);

  bool all_ok = true;
  for (auto& [usr, node_ptrs] : nodes_by_usr) {
    // 查找对应的 symbol_id
    int64_t sym_id = 0;
    {
      std::shared_lock<std::shared_mutex> lk(usr_sym_mu_);
      auto it = usr_sym_cache_.find(usr);
      if (it != usr_sym_cache_.end()) sym_id = it->second;
    }
    if (sym_id == 0) continue;  // 符号未注册，跳过

    // 计算 pb 文件路径
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
    sqlite3_bind_int64(idx_stmt, 1, sym_id);
    sqlite3_bind_text(idx_stmt, 2, rel_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(idx_stmt);
    sqlite3_reset(idx_stmt);
  }

  sqlite3_finalize(idx_stmt);
  return all_ok;
}

// ─── UpdateParsedFile ─────────────────────────────────────────────────────────

bool DbWriter::UpdateParsedFile(int64_t file_id, int64_t parse_run_id,
                                int64_t mtime, const std::string& hash) {
  const char* sql =
      "INSERT INTO parsed_file(project_id, file_id, parse_run_id, file_mtime, file_hash)"
      " VALUES(?,?,?,?,?)"
      " ON CONFLICT(project_id, file_id) DO UPDATE SET"
      "  parse_run_id=excluded.parse_run_id,"
      "  file_mtime=excluded.file_mtime,"
      "  file_hash=excluded.file_hash,"
      "  parsed_at=datetime('now')";
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
  sqlite3_bind_int64(stmt, 1, project_id_);
  sqlite3_bind_int64(stmt, 2, file_id);
  sqlite3_bind_int64(stmt, 3, parse_run_id);
  sqlite3_bind_int64(stmt, 4, mtime);
  sqlite3_bind_text(stmt, 5, hash.c_str(), -1, SQLITE_TRANSIENT);
  bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return ok;
}

// ─── DeleteDataForFile ────────────────────────────────────────────────────────

bool DbWriter::DeleteDataForFile(int64_t file_id) {
  // Delete symbols defined in this file (cascades to call_edge via symbol IDs)
  // We have to delete manually since we don't use ON DELETE CASCADE everywhere
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
  // 清理 cfg_index 及对应的 pb 文件（CFG 数据存储在 protobuf 文件中）
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
  // call_edge for callers/callees defined in this file
  exec("DELETE FROM call_edge WHERE caller_id IN "
       "(SELECT id FROM symbol WHERE def_file_id=?)", file_id);
  // class_relation / member for classes in this file
  exec("DELETE FROM class_member WHERE class_id IN "
       "(SELECT id FROM class WHERE def_file_id=?)", file_id);
  exec("DELETE FROM class_relation WHERE parent_id IN "
       "(SELECT id FROM class WHERE def_file_id=?)"
       " OR child_id IN (SELECT id FROM class WHERE def_file_id=?)",
       file_id); // note: only 1 bind slot — handle specially
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
  exec("DELETE FROM parsed_file WHERE file_id=? AND project_id=?", file_id);  // simple
  // Invalidate caches (brute-force: clear all — fine for incremental re-parse)
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
