/**
 * 解析引擎 DB writer 实现
 */

#include "db/writer/writer.h"
#include "common/logger.h"
#include <sqlite3.h>
#include <cstring>
#include <sstream>

namespace codexray {

namespace {

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
  const char* sql = "INSERT INTO symbol(usr,name,qualified_name,kind,def_file_id,def_line,def_column,def_line_end,def_column_end)"
                    " VALUES(?,?,?,?,?,?,?,?,?)";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return usr_to_id;
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
    if (sqlite3_step(stmt) == SQLITE_DONE)
      usr_to_id[s.usr] = LastInsertId(db_);
  }
  sqlite3_finalize(stmt);
  LogInfo("WriteSymbols: inserted %zu symbols", usr_to_id.size());
  return usr_to_id;
}

bool DBWriter::WriteCallEdges(int64_t project_id,
                              const std::vector<CallEdgeRecord>& edges,
                              const std::unordered_map<std::string, int64_t>& usr_to_id) {
  if (!db_ || edges.empty()) return true;
  const char* sql = "INSERT INTO call_edge(caller_id,callee_id,call_site_file_id,call_site_line,call_site_column,edge_type)"
                    " VALUES(?,?,?,?,?,?)";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
  for (const auto& e : edges) {
    int64_t caller_id = 0, callee_id = 0;
    auto it = usr_to_id.find(e.caller_usr);
    if (it != usr_to_id.end()) caller_id = it->second;
    else caller_id = GetSymbolIdByUsr(db_, e.caller_usr);
    if (caller_id == 0) continue;
    it = usr_to_id.find(e.callee_usr);
    if (it != usr_to_id.end()) callee_id = it->second;
    else callee_id = GetSymbolIdByUsr(db_, e.callee_usr);
    if (callee_id == 0)
      callee_id = InsertPlaceholderSymbol(db_, e.call_site_file_id, e.callee_usr, e.callee_usr);
    if (callee_id == 0) continue;
    sqlite3_reset(stmt);
    sqlite3_bind_int64(stmt, 1, caller_id);
    sqlite3_bind_int64(stmt, 2, callee_id);
    sqlite3_bind_int64(stmt, 3, e.call_site_file_id);
    sqlite3_bind_int(stmt, 4, e.call_site_line);
    sqlite3_bind_int(stmt, 5, e.call_site_column);
    sqlite3_bind_text(stmt, 6, e.edge_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
  }
  sqlite3_finalize(stmt);
  return true;
}

std::unordered_map<std::string, int64_t> DBWriter::WriteClasses(
    int64_t project_id,
    const std::vector<ClassRecord>& classes) {
  std::unordered_map<std::string, int64_t> usr_to_id;
  if (!db_ || classes.empty()) return usr_to_id;
  const char* sql = "INSERT INTO class(usr,file_id,name,qualified_name,def_file_id,def_line,def_column,def_line_end,def_column_end)"
                    " VALUES(?,?,?,?,?,?,?,?,?)";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return usr_to_id;
  for (const auto& c : classes) {
    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, c.usr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, c.file_id);
    sqlite3_bind_text(stmt, 3, c.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, c.qualified_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, c.def_file_id);
    sqlite3_bind_int(stmt, 6, c.def_line);
    sqlite3_bind_int(stmt, 7, c.def_column);
    sqlite3_bind_int(stmt, 8, c.def_line_end);
    sqlite3_bind_int(stmt, 9, c.def_column_end);
    if (sqlite3_step(stmt) == SQLITE_DONE)
      usr_to_id[c.usr] = LastInsertId(db_);
  }
  sqlite3_finalize(stmt);
  LogInfo("WriteClasses: inserted %zu classes", usr_to_id.size());
  return usr_to_id;
}

bool DBWriter::WriteClassRelations(int64_t project_id,
                                   const std::vector<ClassRelationRecord>& relations,
                                   const std::unordered_map<std::string, int64_t>& class_usr_to_id) {
  if (!db_ || relations.empty()) return true;
  const char* sql = "INSERT INTO class_relation(parent_id,child_id,relation_type) VALUES(?,?,?)";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
  for (const auto& r : relations) {
    auto pit = class_usr_to_id.find(r.parent_usr);
    auto cit = class_usr_to_id.find(r.child_usr);
    if (pit == class_usr_to_id.end() || cit == class_usr_to_id.end()) continue;
    sqlite3_reset(stmt);
    sqlite3_bind_int64(stmt, 1, pit->second);
    sqlite3_bind_int64(stmt, 2, cit->second);
    sqlite3_bind_text(stmt, 3, r.relation_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
  }
  sqlite3_finalize(stmt);
  return true;
}

std::unordered_map<std::string, int64_t> DBWriter::WriteGlobalVars(
    int64_t project_id,
    const std::vector<GlobalVarRecord>& global_vars) {
  std::unordered_map<std::string, int64_t> usr_to_id;
  if (!db_ || global_vars.empty()) return usr_to_id;
  const char* sql = "INSERT INTO global_var(usr,def_file_id,def_line,def_column,def_line_end,def_column_end,file_id,name)"
                    " VALUES(?,?,?,?,?,?,?,?)";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return usr_to_id;
  for (const auto& g : global_vars) {
    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, g.usr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, g.def_file_id);
    sqlite3_bind_int(stmt, 3, g.def_line);
    sqlite3_bind_int(stmt, 4, g.def_column);
    sqlite3_bind_int(stmt, 5, g.def_line_end);
    sqlite3_bind_int(stmt, 6, g.def_column_end);
    sqlite3_bind_int64(stmt, 7, g.file_id);
    sqlite3_bind_text(stmt, 8, g.name.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_DONE)
      usr_to_id[g.usr] = LastInsertId(db_);
  }
  sqlite3_finalize(stmt);
  LogInfo("WriteGlobalVars: inserted %zu vars", usr_to_id.size());
  return usr_to_id;
}

bool DBWriter::WriteDataFlowEdges(int64_t project_id,
                                  const std::vector<DataFlowEdgeRecord>& edges,
                                  const std::unordered_map<std::string, int64_t>& var_usr_to_id,
                                  const std::unordered_map<std::string, int64_t>& symbol_usr_to_id) {
  if (!db_ || edges.empty()) return true;
  const char* sql = "INSERT INTO data_flow_edge(var_id,reader_id,writer_id,read_file_id,read_line,read_column,write_file_id,write_line,write_column)"
                    " VALUES(?,?,?,?,?,?,?,?,?)";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
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
    sqlite3_reset(stmt);
    sqlite3_bind_int64(stmt, 1, vit->second);
    reader_id ? sqlite3_bind_int64(stmt, 2, reader_id) : sqlite3_bind_null(stmt, 2);
    writer_id ? sqlite3_bind_int64(stmt, 3, writer_id) : sqlite3_bind_null(stmt, 3);
    e.read_file_id ? sqlite3_bind_int64(stmt, 4, e.read_file_id) : sqlite3_bind_null(stmt, 4);
    sqlite3_bind_int(stmt, 5, e.read_line);
    sqlite3_bind_int(stmt, 6, e.read_column);
    e.write_file_id ? sqlite3_bind_int64(stmt, 7, e.write_file_id) : sqlite3_bind_null(stmt, 7);
    sqlite3_bind_int(stmt, 8, e.write_line);
    sqlite3_bind_int(stmt, 9, e.write_column);
    sqlite3_step(stmt);
  }
  sqlite3_finalize(stmt);
  return true;
}

std::vector<int64_t> DBWriter::WriteCfgNodes(int64_t project_id,
                                            const std::vector<CfgNodeRecord>& nodes,
                                            const std::unordered_map<std::string, int64_t>& symbol_usr_to_id) {
  std::vector<int64_t> node_ids;
  if (!db_ || nodes.empty()) return node_ids;
  node_ids.reserve(nodes.size());
  const char* sql = "INSERT INTO cfg_node(symbol_id,block_id,kind,file_id,line,column) VALUES(?,?,?,?,?,?)";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return node_ids;
  for (const auto& n : nodes) {
    auto it = symbol_usr_to_id.find(n.symbol_usr);
    if (it == symbol_usr_to_id.end()) {
      node_ids.push_back(0);
      continue;
    }
    sqlite3_reset(stmt);
    sqlite3_bind_int64(stmt, 1, it->second);
    sqlite3_bind_text(stmt, 2, n.block_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, n.kind.c_str(), -1, SQLITE_TRANSIENT);
    n.file_id ? sqlite3_bind_int64(stmt, 4, n.file_id) : sqlite3_bind_null(stmt, 4);
    sqlite3_bind_int(stmt, 5, n.line);
    sqlite3_bind_int(stmt, 6, n.column);
    if (sqlite3_step(stmt) == SQLITE_DONE)
      node_ids.push_back(LastInsertId(db_));
    else
      node_ids.push_back(0);
  }
  sqlite3_finalize(stmt);
  LogInfo("WriteCfgNodes: inserted %zu nodes", node_ids.size());
  return node_ids;
}

bool DBWriter::WriteCfgEdges(int64_t project_id,
                             const std::vector<CfgEdgeRecord>& edges,
                             const std::vector<int64_t>& node_ids) {
  if (!db_ || edges.empty() || node_ids.empty()) return true;
  const char* sql = "INSERT INTO cfg_edge(from_node_id,to_node_id,edge_type) VALUES(?,?,?)";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
  for (const auto& e : edges) {
    if (e.from_node_index < 0 || e.from_node_index >= static_cast<int>(node_ids.size()) ||
        e.to_node_index < 0 || e.to_node_index >= static_cast<int>(node_ids.size()))
      continue;
    int64_t from_id = node_ids[e.from_node_index];
    int64_t to_id = node_ids[e.to_node_index];
    if (from_id == 0 || to_id == 0) continue;
    sqlite3_reset(stmt);
    sqlite3_bind_int64(stmt, 1, from_id);
    sqlite3_bind_int64(stmt, 2, to_id);
    sqlite3_bind_text(stmt, 3, e.edge_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
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
