/**
 * 解析引擎 DB reader 实现
 */

#include "db/reader/reader.h"
#include "common/logger.h"
#include <sqlite3.h>
#include <set>

namespace codexray {

namespace {

std::string ColText(sqlite3_stmt* stmt, int col) {
  const char* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
  return p ? std::string(p) : "";
}

SymbolRow SymbolFromStmt(sqlite3_stmt* stmt) {
  SymbolRow r;
  r.id = sqlite3_column_int64(stmt, 0);
  r.usr = ColText(stmt, 1);
  r.name = ColText(stmt, 2);
  r.qualified_name = ColText(stmt, 3);
  r.kind = ColText(stmt, 4);
  r.def_file_id = sqlite3_column_int64(stmt, 5);
  r.def_line = sqlite3_column_int(stmt, 6);
  r.def_column = sqlite3_column_int(stmt, 7);
  r.def_line_end = sqlite3_column_int(stmt, 8);
  r.def_column_end = sqlite3_column_int(stmt, 9);
  return r;
}

}  // namespace

SymbolRow QuerySymbolByUsr(sqlite3* db, const std::string& usr) {
  SymbolRow r;
  if (!db) return r;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT id,usr,name,qualified_name,kind,def_file_id,def_line,def_column,def_line_end,def_column_end FROM symbol WHERE usr = ?", -1, &stmt, nullptr) != SQLITE_OK)
    return r;
  sqlite3_bind_text(stmt, 1, usr.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) == SQLITE_ROW)
    r = SymbolFromStmt(stmt);
  sqlite3_finalize(stmt);
  return r;
}

std::vector<SymbolRow> QuerySymbolsByFile(sqlite3* db, int64_t file_id) {
  std::vector<SymbolRow> out;
  if (!db) return out;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT id,usr,name,qualified_name,kind,def_file_id,def_line,def_column,def_line_end,def_column_end FROM symbol WHERE def_file_id = ?", -1, &stmt, nullptr) != SQLITE_OK)
    return out;
  sqlite3_bind_int64(stmt, 1, file_id);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    out.push_back(SymbolFromStmt(stmt));
  }
  sqlite3_finalize(stmt);
  return out;
}

std::vector<CallEdgeRow> QueryCallEdges(sqlite3* db,
                                        const std::string& symbol_usr,
                                        bool by_caller,
                                        int depth) {
  std::vector<CallEdgeRow> out;
  if (!db || depth <= 0) return out;
  SymbolRow sym = QuerySymbolByUsr(db, symbol_usr);
  if (sym.id == 0) return out;
  const char* sql_dir = by_caller
      ? "SELECT e.caller_id,e.callee_id,s1.usr,s2.usr,e.call_site_file_id,e.call_site_line,e.call_site_column,e.edge_type "
        "FROM call_edge e JOIN symbol s1 ON e.caller_id=s1.id JOIN symbol s2 ON e.callee_id=s2.id WHERE e.caller_id = ?"
      : "SELECT e.caller_id,e.callee_id,s1.usr,s2.usr,e.call_site_file_id,e.call_site_line,e.call_site_column,e.edge_type "
        "FROM call_edge e JOIN symbol s1 ON e.caller_id=s1.id JOIN symbol s2 ON e.callee_id=s2.id WHERE e.callee_id = ?";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql_dir, -1, &stmt, nullptr) != SQLITE_OK) return out;
  std::set<int64_t> seen;
  std::vector<int64_t> frontier = {sym.id};
  seen.insert(sym.id);
  for (int d = 0; d < depth && !frontier.empty(); ++d) {
    std::vector<int64_t> next;
    for (int64_t id : frontier) {
      sqlite3_reset(stmt);
      sqlite3_bind_int64(stmt, 1, id);
      while (sqlite3_step(stmt) == SQLITE_ROW) {
        CallEdgeRow row;
        row.caller_id = sqlite3_column_int64(stmt, 0);
        row.callee_id = sqlite3_column_int64(stmt, 1);
        row.caller_usr = ColText(stmt, 2);
        row.callee_usr = ColText(stmt, 3);
        row.call_site_file_id = sqlite3_column_int64(stmt, 4);
        row.call_site_line = sqlite3_column_int(stmt, 5);
        row.call_site_column = sqlite3_column_int(stmt, 6);
        row.edge_type = ColText(stmt, 7);
        out.push_back(row);
        int64_t other = by_caller ? row.callee_id : row.caller_id;
        if (seen.insert(other).second) next.push_back(other);
      }
    }
    frontier = std::move(next);
  }
  sqlite3_finalize(stmt);
  return out;
}

std::vector<CallEdgeRow> QueryCallGraphExpand(sqlite3* db,
                                              const std::string& from_usr,
                                              const std::string& direction,
                                              int depth) {
  bool by_caller = (direction == "caller");
  return QueryCallEdges(db, from_usr, by_caller, depth);
}

std::string QueryFilePath(sqlite3* db, int64_t file_id) {
  if (!db) return "";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT path FROM file WHERE id = ?", -1, &stmt, nullptr) != SQLITE_OK)
    return "";
  sqlite3_bind_int64(stmt, 1, file_id);
  std::string path;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    path = ColText(stmt, 0);
  sqlite3_finalize(stmt);
  return path;
}

int64_t QueryProjectIdByRoot(sqlite3* db, const std::string& root_path) {
  if (!db) return 0;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT id FROM project WHERE root_path = ?", -1, &stmt, nullptr) != SQLITE_OK)
    return 0;
  sqlite3_bind_text(stmt, 1, root_path.c_str(), -1, SQLITE_TRANSIENT);
  int64_t id = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) id = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return id;
}

int64_t QueryFileIdByPath(sqlite3* db, int64_t project_id, const std::string& path) {
  if (!db) return 0;
  sqlite3_stmt* stmt = nullptr;
  if (project_id > 0) {
    if (sqlite3_prepare_v2(db, "SELECT id FROM file WHERE project_id = ? AND path = ?", -1, &stmt, nullptr) != SQLITE_OK)
      return 0;
    sqlite3_bind_int64(stmt, 1, project_id);
    sqlite3_bind_text(stmt, 2, path.c_str(), -1, SQLITE_TRANSIENT);
  } else {
    if (sqlite3_prepare_v2(db, "SELECT id FROM file WHERE path = ? LIMIT 1", -1, &stmt, nullptr) != SQLITE_OK)
      return 0;
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
  }
  int64_t id = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) id = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return id;
}

// --- class ---
ClassRow QueryClassByUsr(sqlite3* db, const std::string& usr) {
  ClassRow r;
  if (!db) return r;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT id,usr,file_id,name,qualified_name,def_file_id,def_line,def_column,def_line_end,def_column_end FROM class WHERE usr = ?", -1, &stmt, nullptr) != SQLITE_OK)
    return r;
  sqlite3_bind_text(stmt, 1, usr.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    r.id = sqlite3_column_int64(stmt, 0);
    r.usr = ColText(stmt, 1);
    r.file_id = sqlite3_column_int64(stmt, 2);
    r.name = ColText(stmt, 3);
    r.qualified_name = ColText(stmt, 4);
    r.def_file_id = sqlite3_column_int64(stmt, 5);
    r.def_line = sqlite3_column_int(stmt, 6);
    r.def_column = sqlite3_column_int(stmt, 7);
    r.def_line_end = sqlite3_column_int(stmt, 8);
    r.def_column_end = sqlite3_column_int(stmt, 9);
  }
  sqlite3_finalize(stmt);
  if (r.id > 0) r.def_file_path = QueryFilePath(db, r.def_file_id);
  return r;
}

std::vector<ClassRow> QueryClassesByFile(sqlite3* db, int64_t file_id) {
  std::vector<ClassRow> out;
  if (!db) return out;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT id,usr,file_id,name,qualified_name,def_file_id,def_line,def_column,def_line_end,def_column_end FROM class WHERE file_id = ?", -1, &stmt, nullptr) != SQLITE_OK)
    return out;
  sqlite3_bind_int64(stmt, 1, file_id);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    ClassRow r;
    r.id = sqlite3_column_int64(stmt, 0);
    r.usr = ColText(stmt, 1);
    r.file_id = sqlite3_column_int64(stmt, 2);
    r.name = ColText(stmt, 3);
    r.qualified_name = ColText(stmt, 4);
    r.def_file_id = sqlite3_column_int64(stmt, 5);
    r.def_line = sqlite3_column_int(stmt, 6);
    r.def_column = sqlite3_column_int(stmt, 7);
    r.def_line_end = sqlite3_column_int(stmt, 8);
    r.def_column_end = sqlite3_column_int(stmt, 9);
    r.def_file_path = QueryFilePath(db, r.def_file_id);
    out.push_back(r);
  }
  sqlite3_finalize(stmt);
  return out;
}

std::vector<ClassRelationRow> QueryClassRelations(sqlite3* db, const std::vector<int64_t>& class_ids) {
  std::vector<ClassRelationRow> out;
  if (!db || class_ids.empty()) return out;
  std::string placeholders;
  for (size_t i = 0; i < class_ids.size(); ++i) placeholders += (i ? ",?" : "?");
  std::string sql = "SELECT e.parent_id,e.child_id,p.usr,c.usr,e.relation_type FROM class_relation e "
                    "JOIN class p ON e.parent_id=p.id JOIN class c ON e.child_id=c.id "
                    "WHERE e.parent_id IN (" + placeholders + ") OR e.child_id IN (" + placeholders + ")";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return out;
  for (size_t i = 0; i < class_ids.size(); ++i) { sqlite3_bind_int64(stmt, static_cast<int>(i) + 1, class_ids[i]); }
  for (size_t i = 0; i < class_ids.size(); ++i) { sqlite3_bind_int64(stmt, static_cast<int>(i + class_ids.size()) + 1, class_ids[i]); }
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    ClassRelationRow row;
    row.parent_id = sqlite3_column_int64(stmt, 0);
    row.child_id = sqlite3_column_int64(stmt, 1);
    row.parent_usr = ColText(stmt, 2);
    row.child_usr = ColText(stmt, 3);
    row.relation_type = ColText(stmt, 4);
    out.push_back(row);
  }
  sqlite3_finalize(stmt);
  return out;
}

// --- global_var / data_flow ---
GlobalVarRow QueryGlobalVarByUsr(sqlite3* db, const std::string& usr) {
  GlobalVarRow r;
  if (!db) return r;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT id,usr,def_file_id,def_line,def_column,def_line_end,def_column_end,file_id,name FROM global_var WHERE usr = ?", -1, &stmt, nullptr) != SQLITE_OK)
    return r;
  sqlite3_bind_text(stmt, 1, usr.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    r.id = sqlite3_column_int64(stmt, 0);
    r.usr = ColText(stmt, 1);
    r.def_file_id = sqlite3_column_int64(stmt, 2);
    r.def_line = sqlite3_column_int(stmt, 3);
    r.def_column = sqlite3_column_int(stmt, 4);
    r.def_line_end = sqlite3_column_int(stmt, 5);
    r.def_column_end = sqlite3_column_int(stmt, 6);
    r.file_id = sqlite3_column_int64(stmt, 7);
    r.name = ColText(stmt, 8);
    r.def_file_path = QueryFilePath(db, r.def_file_id);
  }
  sqlite3_finalize(stmt);
  return r;
}

std::vector<GlobalVarRow> QueryGlobalVarsByFile(sqlite3* db, int64_t file_id) {
  std::vector<GlobalVarRow> out;
  if (!db) return out;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT id,usr,def_file_id,def_line,def_column,def_line_end,def_column_end,file_id,name FROM global_var WHERE file_id = ?", -1, &stmt, nullptr) != SQLITE_OK)
    return out;
  sqlite3_bind_int64(stmt, 1, file_id);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    GlobalVarRow r;
    r.id = sqlite3_column_int64(stmt, 0);
    r.usr = ColText(stmt, 1);
    r.def_file_id = sqlite3_column_int64(stmt, 2);
    r.def_line = sqlite3_column_int(stmt, 3);
    r.def_column = sqlite3_column_int(stmt, 4);
    r.def_line_end = sqlite3_column_int(stmt, 5);
    r.def_column_end = sqlite3_column_int(stmt, 6);
    r.file_id = sqlite3_column_int64(stmt, 7);
    r.name = ColText(stmt, 8);
    r.def_file_path = QueryFilePath(db, r.def_file_id);
    out.push_back(r);
  }
  sqlite3_finalize(stmt);
  return out;
}

std::vector<DataFlowEdgeRow> QueryDataFlowEdgesByVar(sqlite3* db, int64_t var_id) {
  std::vector<DataFlowEdgeRow> out;
  if (!db) return out;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT e.var_id,e.reader_id,e.writer_id,g.usr,s1.usr,s2.usr FROM data_flow_edge e "
                             "JOIN global_var g ON e.var_id=g.id LEFT JOIN symbol s1 ON e.reader_id=s1.id LEFT JOIN symbol s2 ON e.writer_id=s2.id WHERE e.var_id = ?", -1, &stmt, nullptr) != SQLITE_OK)
    return out;
  sqlite3_bind_int64(stmt, 1, var_id);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    DataFlowEdgeRow row;
    row.var_id = sqlite3_column_int64(stmt, 0);
    row.reader_id = sqlite3_column_int64(stmt, 1);
    row.writer_id = sqlite3_column_int64(stmt, 2);
    row.var_usr = ColText(stmt, 3);
    row.reader_usr = ColText(stmt, 4);
    row.writer_usr = ColText(stmt, 5);
    out.push_back(row);
  }
  sqlite3_finalize(stmt);
  return out;
}

// --- cfg ---
std::vector<CfgNodeRow> QueryCfgNodesBySymbolId(sqlite3* db, int64_t symbol_id) {
  std::vector<CfgNodeRow> out;
  if (!db) return out;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT id,symbol_id,block_id,kind,file_id,line,column FROM cfg_node WHERE symbol_id = ? ORDER BY id", -1, &stmt, nullptr) != SQLITE_OK)
    return out;
  sqlite3_bind_int64(stmt, 1, symbol_id);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    CfgNodeRow r;
    r.id = sqlite3_column_int64(stmt, 0);
    r.symbol_id = sqlite3_column_int64(stmt, 1);
    r.block_id = ColText(stmt, 2);
    r.kind = ColText(stmt, 3);
    r.file_id = sqlite3_column_int64(stmt, 4);
    r.line = sqlite3_column_int(stmt, 5);
    r.column = sqlite3_column_int(stmt, 6);
    r.file_path = QueryFilePath(db, r.file_id);
    out.push_back(r);
  }
  sqlite3_finalize(stmt);
  return out;
}

std::vector<CfgEdgeRow> QueryCfgEdgesByFromNodeIds(sqlite3* db, const std::vector<int64_t>& node_ids) {
  std::vector<CfgEdgeRow> out;
  if (!db || node_ids.empty()) return out;
  std::string placeholders;
  for (size_t i = 0; i < node_ids.size(); ++i) placeholders += (i ? ",?" : "?");
  std::string sql = "SELECT from_node_id,to_node_id,edge_type FROM cfg_edge WHERE from_node_id IN (" + placeholders + ")";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return out;
  for (size_t i = 0; i < node_ids.size(); ++i) sqlite3_bind_int64(stmt, static_cast<int>(i) + 1, node_ids[i]);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    CfgEdgeRow row;
    row.from_node_id = sqlite3_column_int64(stmt, 0);
    row.to_node_id = sqlite3_column_int64(stmt, 1);
    row.edge_type = ColText(stmt, 2);
    out.push_back(row);
  }
  sqlite3_finalize(stmt);
  return out;
}

}  // namespace codexray
