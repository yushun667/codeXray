/**
 * 解析引擎 DB writer 实现
 */

#include "db/writer/writer.h"
#include "common/logger.h"
#include <sqlite3.h>
#include <cstring>
#include <sstream>
#include <tuple>

namespace codexray {

namespace {

/** SQLite 单语句最大绑定参数默认 999，取 100 行/批可兼容 9 列表（class/data_flow_edge） */
constexpr int kBatchSize = 100;

int64_t LastInsertId(sqlite3* db) {
  return static_cast<int64_t>(sqlite3_last_insert_rowid(db));
}

bool Exec(sqlite3* db, const char* sql) {
  char* err = nullptr;
  int r = sqlite3_exec(db, sql, nullptr, nullptr, &err);
  if (r != SQLITE_OK) {
    LogError("db exec: %s", err ? err : sqlite3_errmsg(db));
    if (err) sqlite3_free(err);
    return false;
  }
  return true;
}

int64_t GetSymbolIdByUsr(sqlite3* db, const std::string& usr) {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT id FROM symbol WHERE usr = ?", -1, &stmt, nullptr) != SQLITE_OK)
    return 0;
  sqlite3_bind_text(stmt, 1, usr.c_str(), static_cast<int>(usr.size()), SQLITE_TRANSIENT);
  int64_t id = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    id = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return id;
}

int64_t InsertPlaceholderSymbol(sqlite3* db, int64_t def_file_id, const std::string& usr,
                                 const std::string& name) {
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "INSERT INTO symbol(usr,name,qualified_name,kind,def_file_id,def_line,def_column)"
                    " VALUES(?,?,?,?,?,0,0)";
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
  sqlite3_bind_text(stmt, 1, usr.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 5, def_file_id);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  return LastInsertId(db);
}

}  // namespace

DBWriter::DBWriter(sqlite3* db) : db_(db) {}

int64_t DBWriter::EnsureProject(const std::string& root_path,
                                 const std::string& compile_commands_path) {
  if (!db_) return 0;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT id FROM project WHERE root_path = ?", -1, &stmt, nullptr) != SQLITE_OK)
    return 0;
  sqlite3_bind_text(stmt, 1, root_path.c_str(), -1, SQLITE_TRANSIENT);
  int64_t id = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    id = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  if (id > 0) return id;
  if (sqlite3_prepare_v2(db_, "INSERT INTO project(root_path, compile_commands_path) VALUES(?,?)", -1, &stmt, nullptr) != SQLITE_OK)
    return 0;
  sqlite3_bind_text(stmt, 1, root_path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, compile_commands_path.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  return LastInsertId(db_);
}

int64_t DBWriter::EnsureFile(int64_t project_id, const std::string& path) {
  if (!db_) return 0;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT id FROM file WHERE project_id = ? AND path = ?", -1, &stmt, nullptr) != SQLITE_OK)
    return 0;
  sqlite3_bind_int64(stmt, 1, project_id);
  sqlite3_bind_text(stmt, 2, path.c_str(), -1, SQLITE_TRANSIENT);
  int64_t id = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    id = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  if (id > 0) return id;
  if (sqlite3_prepare_v2(db_, "INSERT INTO file(project_id, path) VALUES(?,?)", -1, &stmt, nullptr) != SQLITE_OK)
    return 0;
  sqlite3_bind_int64(stmt, 1, project_id);
  sqlite3_bind_text(stmt, 2, path.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  return LastInsertId(db_);
}

std::unordered_map<std::string, int64_t> DBWriter::WriteSymbols(
    int64_t project_id,
    const std::vector<SymbolRecord>& symbols) {
  std::unordered_map<std::string, int64_t> usr_to_id;
  if (!db_ || symbols.empty()) return usr_to_id;
  const char* sql = "INSERT INTO symbol(usr,name,qualified_name,kind,def_file_id,def_line,def_column,def_line_end,def_column_end,decl_file_id,decl_line,decl_column,decl_line_end,decl_column_end)"
                    " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(usr) DO UPDATE SET name=excluded.name,qualified_name=excluded.qualified_name,kind=excluded.kind,"
                    " def_file_id=CASE WHEN excluded.def_file_id!=0 THEN excluded.def_file_id ELSE symbol.def_file_id END,"
                    " def_line=CASE WHEN excluded.def_line!=0 THEN excluded.def_line ELSE symbol.def_line END,"
                    " def_column=CASE WHEN excluded.def_column!=0 THEN excluded.def_column ELSE symbol.def_column END,"
                    " def_line_end=CASE WHEN excluded.def_line_end!=0 THEN excluded.def_line_end ELSE symbol.def_line_end END,"
                    " def_column_end=CASE WHEN excluded.def_column_end!=0 THEN excluded.def_column_end ELSE symbol.def_column_end END,"
                    " decl_file_id=CASE WHEN excluded.decl_file_id!=0 THEN excluded.decl_file_id ELSE COALESCE(symbol.decl_file_id,0) END,"
                    " decl_line=CASE WHEN excluded.decl_line!=0 THEN excluded.decl_line ELSE COALESCE(symbol.decl_line,0) END,"
                    " decl_column=CASE WHEN excluded.decl_column!=0 THEN excluded.decl_column ELSE COALESCE(symbol.decl_column,0) END,"
                    " decl_line_end=CASE WHEN excluded.decl_line_end!=0 THEN excluded.decl_line_end ELSE COALESCE(symbol.decl_line_end,0) END,"
                    " decl_column_end=CASE WHEN excluded.decl_column_end!=0 THEN excluded.decl_column_end ELSE COALESCE(symbol.decl_column_end,0) END";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return usr_to_id;
  sqlite3_stmt* sel = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT id FROM symbol WHERE usr = ?", -1, &sel, nullptr) != SQLITE_OK) {
    sqlite3_finalize(stmt);
    return usr_to_id;
  }
  for (const auto& s : symbols) {
    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, s.usr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, s.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, s.qualified_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, s.kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, s.def_file_id);
    sqlite3_bind_int(stmt, 6, s.def_line);
    sqlite3_bind_int(stmt, 7, s.def_column);
    sqlite3_bind_int(stmt, 8, s.def_line_end);
    sqlite3_bind_int(stmt, 9, s.def_column_end);
    sqlite3_bind_int64(stmt, 10, s.decl_file_id);
    sqlite3_bind_int(stmt, 11, s.decl_line);
    sqlite3_bind_int(stmt, 12, s.decl_column);
    sqlite3_bind_int(stmt, 13, s.decl_line_end);
    sqlite3_bind_int(stmt, 14, s.decl_column_end);
    if (sqlite3_step(stmt) != SQLITE_DONE) continue;
    sqlite3_reset(sel);
    sqlite3_bind_text(sel, 1, s.usr.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(sel) == SQLITE_ROW)
      usr_to_id[s.usr] = sqlite3_column_int64(sel, 0);
  }
  sqlite3_finalize(sel);
  sqlite3_finalize(stmt);
  LogInfo("WriteSymbols: inserted/updated %zu symbols", usr_to_id.size());
  return usr_to_id;
}

bool DBWriter::WriteCallEdges(int64_t project_id,
                              const std::vector<CallEdgeRecord>& edges,
                              const std::unordered_map<std::string, int64_t>& usr_to_id) {
  if (!db_ || edges.empty()) return true;
  std::unordered_map<std::string, int64_t> resolved(usr_to_id);
  /* 先解析所有边为 (caller_id, callee_id, ...)，再批量 INSERT */
  struct ResolvedEdge { int64_t caller_id, callee_id, file_id; int line, col; std::string type; };
  std::vector<ResolvedEdge> resolved_edges;
  resolved_edges.reserve(edges.size());
  for (const auto& e : edges) {
    int64_t caller_id = 0, callee_id = 0;
    auto it = resolved.find(e.caller_usr);
    if (it != resolved.end()) caller_id = it->second;
    else {
      caller_id = GetSymbolIdByUsr(db_, e.caller_usr);
      if (caller_id > 0) resolved[e.caller_usr] = caller_id;
    }
    if (caller_id == 0) continue;
    it = resolved.find(e.callee_usr);
    if (it != resolved.end()) callee_id = it->second;
    else {
      callee_id = GetSymbolIdByUsr(db_, e.callee_usr);
      if (callee_id == 0)
        callee_id = InsertPlaceholderSymbol(db_, e.call_site_file_id, e.callee_usr, e.callee_usr);
      if (callee_id > 0) resolved[e.callee_usr] = callee_id;
    }
    if (callee_id == 0) continue;
    resolved_edges.push_back({caller_id, callee_id, e.call_site_file_id, e.call_site_line, e.call_site_column, e.edge_type});
  }
  if (resolved_edges.empty()) return true;
  /* 批量 INSERT：每批 kBatchSize 行，每行 6 列 */
  std::string sql = "INSERT INTO call_edge(caller_id,callee_id,call_site_file_id,call_site_line,call_site_column,edge_type) VALUES";
  for (int i = 0; i < kBatchSize; ++i) sql += (i ? ",(?,?,?,?,?,?)" : "(?,?,?,?,?,?)");
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
  for (size_t off = 0; off < resolved_edges.size(); off += kBatchSize) {
    size_t n = std::min(static_cast<size_t>(kBatchSize), resolved_edges.size() - off);
    if (n < static_cast<size_t>(kBatchSize)) {
      sqlite3_finalize(stmt);
      sql = "INSERT INTO call_edge(caller_id,callee_id,call_site_file_id,call_site_line,call_site_column,edge_type) VALUES";
      for (size_t i = 0; i < n; ++i) sql += (i ? ",(?,?,?,?,?,?)" : "(?,?,?,?,?,?)");
      if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
    }
    int idx = 1;
    for (size_t i = 0; i < n; ++i) {
      const auto& r = resolved_edges[off + i];
      sqlite3_bind_int64(stmt, idx++, r.caller_id);
      sqlite3_bind_int64(stmt, idx++, r.callee_id);
      sqlite3_bind_int64(stmt, idx++, r.file_id);
      sqlite3_bind_int(stmt, idx++, r.line);
      sqlite3_bind_int(stmt, idx++, r.col);
      sqlite3_bind_text(stmt, idx++, r.type.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      return false;
    }
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
  return true;
}

std::unordered_map<std::string, int64_t> DBWriter::WriteClasses(
    int64_t project_id,
    const std::vector<ClassRecord>& classes) {
  std::unordered_map<std::string, int64_t> usr_to_id;
  if (!db_ || classes.empty()) return usr_to_id;
  std::string sql = "INSERT INTO class(usr,file_id,name,qualified_name,def_file_id,def_line,def_column,def_line_end,def_column_end) VALUES";
  for (int i = 0; i < kBatchSize; ++i) sql += (i ? ",(?,?,?,?,?,?,?,?,?)" : "(?,?,?,?,?,?,?,?,?)");
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return usr_to_id;
  for (size_t off = 0; off < classes.size(); off += kBatchSize) {
    size_t n = std::min(static_cast<size_t>(kBatchSize), classes.size() - off);
    if (n < static_cast<size_t>(kBatchSize)) {
      sqlite3_finalize(stmt);
      sql = "INSERT INTO class(usr,file_id,name,qualified_name,def_file_id,def_line,def_column,def_line_end,def_column_end) VALUES";
      for (size_t i = 0; i < n; ++i) sql += (i ? ",(?,?,?,?,?,?,?,?,?)" : "(?,?,?,?,?,?,?,?,?)");
      if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return usr_to_id;
    }
    int idx = 1;
    for (size_t i = 0; i < n; ++i) {
      const auto& c = classes[off + i];
      sqlite3_bind_text(stmt, idx++, c.usr.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64(stmt, idx++, c.file_id);
      sqlite3_bind_text(stmt, idx++, c.name.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, idx++, c.qualified_name.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64(stmt, idx++, c.def_file_id);
      sqlite3_bind_int(stmt, idx++, c.def_line);
      sqlite3_bind_int(stmt, idx++, c.def_column);
      sqlite3_bind_int(stmt, idx++, c.def_line_end);
      sqlite3_bind_int(stmt, idx++, c.def_column_end);
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      return usr_to_id;
    }
    int64_t last_id = LastInsertId(db_);
    for (size_t i = n; i > 0; --i) usr_to_id[classes[off + i - 1].usr] = last_id - static_cast<int64_t>(i) + 1;
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
  LogInfo("WriteClasses: inserted %zu classes (batch)", usr_to_id.size());
  return usr_to_id;
}

bool DBWriter::WriteClassRelations(int64_t project_id,
                                   const std::vector<ClassRelationRecord>& relations,
                                   const std::unordered_map<std::string, int64_t>& class_usr_to_id) {
  if (!db_ || relations.empty()) return true;
  std::vector<std::tuple<int64_t, int64_t, std::string>> rows;
  rows.reserve(relations.size());
  for (const auto& r : relations) {
    auto pit = class_usr_to_id.find(r.parent_usr);
    auto cit = class_usr_to_id.find(r.child_usr);
    if (pit != class_usr_to_id.end() && cit != class_usr_to_id.end())
      rows.emplace_back(pit->second, cit->second, r.relation_type);
  }
  if (rows.empty()) return true;
  std::string sql = "INSERT INTO class_relation(parent_id,child_id,relation_type) VALUES";
  for (int i = 0; i < kBatchSize; ++i) sql += (i ? ",(?,?,?)" : "(?,?,?)");
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
  for (size_t off = 0; off < rows.size(); off += kBatchSize) {
    size_t n = std::min(static_cast<size_t>(kBatchSize), rows.size() - off);
    if (n < static_cast<size_t>(kBatchSize)) {
      sqlite3_finalize(stmt);
      sql = "INSERT INTO class_relation(parent_id,child_id,relation_type) VALUES";
      for (size_t i = 0; i < n; ++i) sql += (i ? ",(?,?,?)" : "(?,?,?)");
      if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
    }
    int idx = 1;
    for (size_t i = 0; i < n; ++i) {
      const auto& t = rows[off + i];
      sqlite3_bind_int64(stmt, idx++, std::get<0>(t));
      sqlite3_bind_int64(stmt, idx++, std::get<1>(t));
      sqlite3_bind_text(stmt, idx++, std::get<2>(t).c_str(), -1, SQLITE_TRANSIENT);
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) { sqlite3_finalize(stmt); return false; }
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
  return true;
}

std::unordered_map<std::string, int64_t> DBWriter::WriteGlobalVars(
    int64_t project_id,
    const std::vector<GlobalVarRecord>& global_vars) {
  std::unordered_map<std::string, int64_t> usr_to_id;
  if (!db_ || global_vars.empty()) return usr_to_id;
  std::string sql = "INSERT INTO global_var(usr,def_file_id,def_line,def_column,def_line_end,def_column_end,file_id,name) VALUES";
  for (int i = 0; i < kBatchSize; ++i) sql += (i ? ",(?,?,?,?,?,?,?,?)" : "(?,?,?,?,?,?,?,?)");
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return usr_to_id;
  for (size_t off = 0; off < global_vars.size(); off += kBatchSize) {
    size_t n = std::min(static_cast<size_t>(kBatchSize), global_vars.size() - off);
    if (n < static_cast<size_t>(kBatchSize)) {
      sqlite3_finalize(stmt);
      sql = "INSERT INTO global_var(usr,def_file_id,def_line,def_column,def_line_end,def_column_end,file_id,name) VALUES";
      for (size_t i = 0; i < n; ++i) sql += (i ? ",(?,?,?,?,?,?,?,?)" : "(?,?,?,?,?,?,?,?)");
      if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return usr_to_id;
    }
    int idx = 1;
    for (size_t i = 0; i < n; ++i) {
      const auto& g = global_vars[off + i];
      sqlite3_bind_text(stmt, idx++, g.usr.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64(stmt, idx++, g.def_file_id);
      sqlite3_bind_int(stmt, idx++, g.def_line);
      sqlite3_bind_int(stmt, idx++, g.def_column);
      sqlite3_bind_int(stmt, idx++, g.def_line_end);
      sqlite3_bind_int(stmt, idx++, g.def_column_end);
      sqlite3_bind_int64(stmt, idx++, g.file_id);
      sqlite3_bind_text(stmt, idx++, g.name.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      return usr_to_id;
    }
    int64_t last_id = LastInsertId(db_);
    for (size_t i = n; i > 0; --i) usr_to_id[global_vars[off + i - 1].usr] = last_id - static_cast<int64_t>(i) + 1;
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
  LogInfo("WriteGlobalVars: inserted %zu vars (batch)", usr_to_id.size());
  return usr_to_id;
}

bool DBWriter::WriteDataFlowEdges(int64_t project_id,
                                  const std::vector<DataFlowEdgeRecord>& edges,
                                  const std::unordered_map<std::string, int64_t>& var_usr_to_id,
                                  const std::unordered_map<std::string, int64_t>& symbol_usr_to_id) {
  if (!db_ || edges.empty()) return true;
  struct Row { int64_t var_id, reader_id, writer_id, read_file_id, write_file_id; int rl, rc, wl, wc; };
  std::vector<Row> rows;
  rows.reserve(edges.size());
  for (const auto& e : edges) {
    auto vit = var_usr_to_id.find(e.var_usr);
    if (vit == var_usr_to_id.end()) continue;
    int64_t reader_id = 0, writer_id = 0;
    if (!e.reader_usr.empty()) {
      auto it = symbol_usr_to_id.find(e.reader_usr);
      if (it != symbol_usr_to_id.end()) reader_id = it->second;
    }
    if (!e.writer_usr.empty()) {
      auto it = symbol_usr_to_id.find(e.writer_usr);
      if (it != symbol_usr_to_id.end()) writer_id = it->second;
    }
    if (reader_id == 0 && writer_id == 0) continue;
    rows.push_back({vit->second, reader_id, writer_id, e.read_file_id, e.write_file_id, e.read_line, e.read_column, e.write_line, e.write_column});
  }
  if (rows.empty()) return true;
  std::string sql = "INSERT INTO data_flow_edge(var_id,reader_id,writer_id,read_file_id,read_line,read_column,write_file_id,write_line,write_column) VALUES";
  for (int i = 0; i < kBatchSize; ++i) sql += (i ? ",(?,?,?,?,?,?,?,?,?)" : "(?,?,?,?,?,?,?,?,?)");
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
  for (size_t off = 0; off < rows.size(); off += kBatchSize) {
    size_t n = std::min(static_cast<size_t>(kBatchSize), rows.size() - off);
    if (n < static_cast<size_t>(kBatchSize)) {
      sqlite3_finalize(stmt);
      sql = "INSERT INTO data_flow_edge(var_id,reader_id,writer_id,read_file_id,read_line,read_column,write_file_id,write_line,write_column) VALUES";
      for (size_t i = 0; i < n; ++i) sql += (i ? ",(?,?,?,?,?,?,?,?,?)" : "(?,?,?,?,?,?,?,?,?)");
      if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
    }
    int idx = 1;
    for (size_t i = 0; i < n; ++i) {
      const Row& r = rows[off + i];
      sqlite3_bind_int64(stmt, idx++, r.var_id);
      if (r.reader_id) sqlite3_bind_int64(stmt, idx++, r.reader_id); else { sqlite3_bind_null(stmt, idx); idx++; }
      if (r.writer_id) sqlite3_bind_int64(stmt, idx++, r.writer_id); else { sqlite3_bind_null(stmt, idx); idx++; }
      if (r.read_file_id) sqlite3_bind_int64(stmt, idx++, r.read_file_id); else { sqlite3_bind_null(stmt, idx); idx++; }
      sqlite3_bind_int(stmt, idx++, r.rl);
      sqlite3_bind_int(stmt, idx++, r.rc);
      if (r.write_file_id) sqlite3_bind_int64(stmt, idx++, r.write_file_id); else { sqlite3_bind_null(stmt, idx); idx++; }
      sqlite3_bind_int(stmt, idx++, r.wl);
      sqlite3_bind_int(stmt, idx++, r.wc);
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) { sqlite3_finalize(stmt); return false; }
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
  return true;
}

std::vector<int64_t> DBWriter::WriteCfgNodes(int64_t project_id,
                                            const std::vector<CfgNodeRecord>& nodes,
                                            const std::unordered_map<std::string, int64_t>& symbol_usr_to_id) {
  std::vector<int64_t> node_ids(nodes.size(), 0);
  if (!db_ || nodes.empty()) return node_ids;
  /* 能解析到 symbol_id 的节点批量插入，回填 node_ids 与 nodes 同序 */
  std::vector<std::pair<size_t, int64_t>> symbol_id_and_index;
  for (size_t i = 0; i < nodes.size(); ++i) {
    auto it = symbol_usr_to_id.find(nodes[i].symbol_usr);
    if (it != symbol_usr_to_id.end())
      symbol_id_and_index.push_back({i, it->second});
  }
  if (symbol_id_and_index.empty()) return node_ids;
  std::string sql = "INSERT INTO cfg_node(symbol_id,block_id,kind,file_id,line,column) VALUES";
  for (int i = 0; i < kBatchSize; ++i) sql += (i ? ",(?,?,?,?,?,?)" : "(?,?,?,?,?,?)");
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return node_ids;
  for (size_t off = 0; off < symbol_id_and_index.size(); off += kBatchSize) {
    size_t n = std::min(static_cast<size_t>(kBatchSize), symbol_id_and_index.size() - off);
    if (n < static_cast<size_t>(kBatchSize)) {
      sqlite3_finalize(stmt);
      sql = "INSERT INTO cfg_node(symbol_id,block_id,kind,file_id,line,column) VALUES";
      for (size_t i = 0; i < n; ++i) sql += (i ? ",(?,?,?,?,?,?)" : "(?,?,?,?,?,?)");
      if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) break;
    }
    int idx = 1;
    for (size_t i = 0; i < n; ++i) {
      size_t node_idx = symbol_id_and_index[off + i].first;
      int64_t sym_id = symbol_id_and_index[off + i].second;
      const auto& no = nodes[node_idx];
      sqlite3_bind_int64(stmt, idx++, sym_id);
      sqlite3_bind_text(stmt, idx++, no.block_id.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, idx++, no.kind.c_str(), -1, SQLITE_TRANSIENT);
      no.file_id ? sqlite3_bind_int64(stmt, idx++, no.file_id) : sqlite3_bind_null(stmt, idx++);
      sqlite3_bind_int(stmt, idx++, no.line);
      sqlite3_bind_int(stmt, idx++, no.column);
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) break;
    int64_t last_id = LastInsertId(db_);
    for (size_t i = n; i > 0; --i)
      node_ids[symbol_id_and_index[off + i - 1].first] = last_id - static_cast<int64_t>(i) + 1;
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
  LogInfo("WriteCfgNodes: inserted %zu nodes (batch)", symbol_id_and_index.size());
  return node_ids;
}

bool DBWriter::WriteCfgEdges(int64_t project_id,
                             const std::vector<CfgEdgeRecord>& edges,
                             const std::vector<int64_t>& node_ids) {
  if (!db_ || edges.empty() || node_ids.empty()) return true;
  std::vector<std::tuple<int64_t, int64_t, std::string>> rows;
  rows.reserve(edges.size());
  for (const auto& e : edges) {
    if (e.from_node_index < 0 || e.from_node_index >= static_cast<int>(node_ids.size()) ||
        e.to_node_index < 0 || e.to_node_index >= static_cast<int>(node_ids.size()))
      continue;
    int64_t from_id = node_ids[e.from_node_index];
    int64_t to_id = node_ids[e.to_node_index];
    if (from_id != 0 && to_id != 0)
      rows.emplace_back(from_id, to_id, e.edge_type);
  }
  if (rows.empty()) return true;
  std::string sql = "INSERT INTO cfg_edge(from_node_id,to_node_id,edge_type) VALUES";
  for (int i = 0; i < kBatchSize; ++i) sql += (i ? ",(?,?,?)" : "(?,?,?)");
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
  for (size_t off = 0; off < rows.size(); off += kBatchSize) {
    size_t n = std::min(static_cast<size_t>(kBatchSize), rows.size() - off);
    if (n < static_cast<size_t>(kBatchSize)) {
      sqlite3_finalize(stmt);
      sql = "INSERT INTO cfg_edge(from_node_id,to_node_id,edge_type) VALUES";
      for (size_t i = 0; i < n; ++i) sql += (i ? ",(?,?,?)" : "(?,?,?)");
      if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
    }
    int idx = 1;
    for (size_t i = 0; i < n; ++i) {
      const auto& t = rows[off + i];
      sqlite3_bind_int64(stmt, idx++, std::get<0>(t));
      sqlite3_bind_int64(stmt, idx++, std::get<1>(t));
      sqlite3_bind_text(stmt, idx++, std::get<2>(t).c_str(), -1, SQLITE_TRANSIENT);
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) { sqlite3_finalize(stmt); return false; }
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
  return true;
}

bool DBWriter::UpdateParsedFile(int64_t project_id, int64_t file_id, int64_t parse_run_id,
                                int64_t file_mtime, const std::string& content_hash) {
  if (!db_) return false;
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "INSERT INTO parsed_file(project_id,file_id,parse_run_id,file_mtime,content_hash,parsed_at)"
                    " VALUES(?,?,?,?,?,datetime('now'))";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
  sqlite3_bind_int64(stmt, 1, project_id);
  sqlite3_bind_int64(stmt, 2, file_id);
  sqlite3_bind_int64(stmt, 3, parse_run_id);
  sqlite3_bind_int64(stmt, 4, file_mtime);
  sqlite3_bind_text(stmt, 5, content_hash.c_str(), -1, SQLITE_TRANSIENT);
  bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return ok;
}

bool DBWriter::DeleteDataForFile(int64_t project_id, int64_t file_id) {
  if (!db_) return false;
  LogInfo("DeleteDataForFile: project=%ld file=%ld", (long)project_id, (long)file_id);
  auto run_one = [this, file_id](const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, file_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return true;
  };
  run_one("DELETE FROM cfg_edge WHERE from_node_id IN (SELECT id FROM cfg_node WHERE symbol_id IN (SELECT id FROM symbol WHERE def_file_id = ?))");
  run_one("DELETE FROM cfg_edge WHERE to_node_id IN (SELECT id FROM cfg_node WHERE symbol_id IN (SELECT id FROM symbol WHERE def_file_id = ?))");
  run_one("DELETE FROM cfg_node WHERE symbol_id IN (SELECT id FROM symbol WHERE def_file_id = ?)");
  run_one("DELETE FROM data_flow_edge WHERE var_id IN (SELECT id FROM global_var WHERE file_id = ?)");
  run_one("DELETE FROM global_var WHERE file_id = ?");
  run_one("DELETE FROM class_member WHERE class_id IN (SELECT id FROM class WHERE file_id = ?)");
  run_one("DELETE FROM class_relation WHERE parent_id IN (SELECT id FROM class WHERE file_id = ?)");
  run_one("DELETE FROM class_relation WHERE child_id IN (SELECT id FROM class WHERE file_id = ?)");
  run_one("DELETE FROM class WHERE file_id = ?");
  run_one("DELETE FROM call_edge WHERE caller_id IN (SELECT id FROM symbol WHERE def_file_id = ?)");
  run_one("DELETE FROM call_edge WHERE callee_id IN (SELECT id FROM symbol WHERE def_file_id = ?)");
  run_one("DELETE FROM symbol WHERE def_file_id = ?");
  run_one("DELETE FROM parsed_file WHERE file_id = ?");
  return true;
}

}  // namespace codexray
