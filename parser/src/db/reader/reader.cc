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

}  // namespace codexray
