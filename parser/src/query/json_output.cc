/**
 * 解析引擎 query：四种类型 JSON 输出，符合接口约定 §3
 */

#include "query/json_output.h"
#include "db/reader/reader.h"
#include "common/logger.h"
#include <sqlite3.h>
#include <sstream>
#include <set>
#include <map>

namespace codexray {

namespace {

void EscapeJson(const std::string& s, std::ostringstream& out) {
  out << '"';
  for (char c : s) {
    if (c == '\\') out << "\\\\";
    else if (c == '"') out << "\\\"";
    else if (c == '\n') out << "\\n";
    else if (c == '\r') out << "\\r";
    else if (static_cast<unsigned char>(c) < 32) out << "\\u00" << std::hex << (c & 0xff) << std::dec;
    else out << c;
  }
  out << '"';
}

std::string CallGraphNodeJson(const SymbolRow& s) {
  std::ostringstream o;
  o << "{\"id\":\"" << s.usr << "\",\"usr\":";
  EscapeJson(s.usr, o);
  o << ",\"name\":";
  EscapeJson(s.name, o);
  o << ",\"definition\":{\"file\":";
  EscapeJson(s.def_file_path.empty() ? std::to_string(s.def_file_id) : s.def_file_path, o);
  o << ",\"line\":" << s.def_line << ",\"column\":" << s.def_column << "}";
  o << ",\"definition_range\":{\"start_line\":" << s.def_line << ",\"start_column\":" << s.def_column;
  o << ",\"end_line\":" << s.def_line_end << ",\"end_column\":" << s.def_column_end << "}}";
  return o.str();
}

std::string CallGraphEdgeJson(const CallEdgeRow& e) {
  std::ostringstream o;
  o << "{\"caller\":";
  EscapeJson(e.caller_usr, o);
  o << ",\"callee\":";
  EscapeJson(e.callee_usr, o);
  o << ",\"call_site\":{\"file\":";
  EscapeJson(e.call_site_file_path, o);
  o << ",\"line\":" << e.call_site_line << ",\"column\":" << e.call_site_column << "}";
  o << ",\"edge_type\":";
  EscapeJson(e.edge_type, o);
  o << "}";
  return o.str();
}

}  // namespace

std::string QueryCallGraphJson(sqlite3* db, const std::string& symbol,
                               const std::string& file, int depth) {
  if (!db) return "{\"nodes\":[],\"edges\":[]}";
  if (depth <= 0) depth = 3;
  SymbolRow start = QuerySymbolByUsr(db, symbol);
  if (start.id == 0 && !symbol.empty()) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT id,usr,name,qualified_name,kind,def_file_id,def_line,def_column,def_line_end,def_column_end FROM symbol WHERE name = ? OR qualified_name LIKE ? LIMIT 1", -1, &stmt, nullptr) == SQLITE_OK) {
      std::string pat = "%" + symbol + "%";
      sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, 2, pat.c_str(), -1, SQLITE_TRANSIENT);
      if (sqlite3_step(stmt) == SQLITE_ROW) {
        start.id = sqlite3_column_int64(stmt, 0);
        start.usr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        start.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        start.qualified_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        start.kind = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        start.def_file_id = sqlite3_column_int64(stmt, 5);
        start.def_line = sqlite3_column_int(stmt, 6);
        start.def_column = sqlite3_column_int(stmt, 7);
        start.def_line_end = sqlite3_column_int(stmt, 8);
        start.def_column_end = sqlite3_column_int(stmt, 9);
      }
      sqlite3_finalize(stmt);
    }
  }
  if (start.id == 0) return "{\"nodes\":[],\"edges\":[]}";
  start.def_file_path = QueryFilePath(db, start.def_file_id);

  std::vector<CallEdgeRow> edges_caller = QueryCallEdges(db, start.usr, true, depth);
  std::vector<CallEdgeRow> edges_callee = QueryCallEdges(db, start.usr, false, depth);
  std::set<std::string> usrs;
  usrs.insert(start.usr);
  for (const auto& e : edges_caller) { usrs.insert(e.caller_usr); usrs.insert(e.callee_usr); }
  for (const auto& e : edges_callee) { usrs.insert(e.caller_usr); usrs.insert(e.callee_usr); }

  std::map<std::string, SymbolRow> node_map;
  node_map[start.usr] = start;
  for (const std::string& u : usrs) {
    if (node_map.count(u)) continue;
    SymbolRow r = QuerySymbolByUsr(db, u);
    if (r.id > 0) {
      r.def_file_path = QueryFilePath(db, r.def_file_id);
      node_map[u] = r;
    }
  }

  std::ostringstream out;
  out << "{\"nodes\":[";
  bool first = true;
  for (const auto& p : node_map) {
    if (!first) out << ",";
    first = false;
    out << CallGraphNodeJson(p.second);
  }
  out << "],\"edges\":[";
  first = true;
  for (const auto& e : edges_caller) {
    CallEdgeRow e2 = e;
    e2.call_site_file_path = QueryFilePath(db, e.call_site_file_id);
    if (!first) out << ",";
    first = false;
    out << CallGraphEdgeJson(e2);
  }
  for (const auto& e : edges_callee) {
    CallEdgeRow e2 = e;
    e2.call_site_file_path = QueryFilePath(db, e.call_site_file_id);
    if (!first) out << ",";
    first = false;
    out << CallGraphEdgeJson(e2);
  }
  out << "]}";
  return out.str();
}

std::string QueryClassGraphJson(sqlite3* db, const std::string& symbol,
                                const std::string& file) {
  if (!db) return "{\"nodes\":[],\"edges\":[]}";
  (void)symbol;
  (void)file;
  return "{\"nodes\":[],\"edges\":[]}";
}

std::string QueryDataFlowJson(sqlite3* db, const std::string& symbol,
                              const std::string& file) {
  if (!db) return "{\"nodes\":[],\"edges\":[]}";
  (void)symbol;
  (void)file;
  return "{\"nodes\":[],\"edges\":[]}";
}

std::string QueryControlFlowJson(sqlite3* db, const std::string& symbol,
                                 const std::string& file) {
  if (!db) return "{\"nodes\":[],\"edges\":[]}";
  (void)symbol;
  (void)file;
  return "{\"nodes\":[],\"edges\":[]}";
}

}  // namespace codexray
