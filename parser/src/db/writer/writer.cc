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
