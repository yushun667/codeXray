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
  o << ",\"kind\":";
  EscapeJson(s.kind.empty() ? "function" : s.kind, o);
  o << ",\"definition\":{\"file\":";
  EscapeJson(s.def_file_path.empty() ? std::to_string(s.def_file_id) : s.def_file_path, o);
  o << ",\"line\":" << s.def_line << ",\"column\":" << s.def_column << "}";
  o << ",\"definition_range\":{\"start_line\":" << s.def_line << ",\"start_column\":" << s.def_column;
  o << ",\"end_line\":" << s.def_line_end << ",\"end_column\":" << s.def_column_end << "}";
  if (s.decl_file_id != 0 || s.decl_line != 0) {
    o << ",\"declaration\":{\"file\":";
    EscapeJson(s.decl_file_path.empty() ? std::to_string(s.decl_file_id) : s.decl_file_path, o);
    o << ",\"line\":" << s.decl_line << ",\"column\":" << s.decl_column << "}";
    o << ",\"declaration_range\":{\"start_line\":" << s.decl_line << ",\"start_column\":" << s.decl_column;
    o << ",\"end_line\":" << s.decl_line_end << ",\"end_column\":" << s.decl_column_end << "}";
  }
  o << "}";
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
    if (sqlite3_prepare_v2(db, "SELECT id,usr,name,qualified_name,kind,def_file_id,def_line,def_column,def_line_end,def_column_end,decl_file_id,decl_line,decl_column,decl_line_end,decl_column_end FROM symbol WHERE name = ? OR qualified_name LIKE ? LIMIT 1", -1, &stmt, nullptr) == SQLITE_OK) {
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
        start.decl_file_id = sqlite3_column_type(stmt, 10) != SQLITE_NULL ? sqlite3_column_int64(stmt, 10) : 0;
        start.decl_line = sqlite3_column_type(stmt, 11) != SQLITE_NULL ? sqlite3_column_int(stmt, 11) : 0;
        start.decl_column = sqlite3_column_type(stmt, 12) != SQLITE_NULL ? sqlite3_column_int(stmt, 12) : 0;
        start.decl_line_end = sqlite3_column_type(stmt, 13) != SQLITE_NULL ? sqlite3_column_int(stmt, 13) : 0;
        start.decl_column_end = sqlite3_column_type(stmt, 14) != SQLITE_NULL ? sqlite3_column_int(stmt, 14) : 0;
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

namespace {

std::string ClassNodeJson(const ClassRow& c) {
  std::ostringstream o;
  o << "{\"id\":\"" << c.usr << "\",\"usr\":";
  EscapeJson(c.usr, o);
  o << ",\"name\":";
  EscapeJson(c.name, o);
  o << ",\"definition\":{\"file\":";
  EscapeJson(c.def_file_path.empty() ? std::to_string(c.def_file_id) : c.def_file_path, o);
  o << ",\"line\":" << c.def_line << ",\"column\":" << c.def_column << "}";
  o << ",\"definition_range\":{\"start_line\":" << c.def_line << ",\"start_column\":" << c.def_column;
  o << ",\"end_line\":" << c.def_line_end << ",\"end_column\":" << c.def_column_end << "}}";
  return o.str();
}

std::string ClassEdgeJson(const ClassRelationRow& e) {
  std::ostringstream o;
  o << "{\"parent\":";
  EscapeJson(e.parent_usr, o);
  o << ",\"child\":";
  EscapeJson(e.child_usr, o);
  o << ",\"relation_type\":";
  EscapeJson(e.relation_type, o);
  o << "}";
  return o.str();
}

}  // namespace

std::string QueryClassGraphJson(sqlite3* db, const std::string& symbol,
                                const std::string& file) {
  if (!db) return "{\"nodes\":[],\"edges\":[]}";
  std::vector<ClassRow> classes;
  int64_t file_id = 0;
  if (!file.empty()) {
    file_id = QueryFileIdByPath(db, 0, file);
    if (file_id > 0) classes = QueryClassesByFile(db, file_id);
  }
  if (classes.empty() && !symbol.empty()) {
    ClassRow c = QueryClassByUsr(db, symbol);
    if (c.id == 0) {
      sqlite3_stmt* stmt = nullptr;
      if (sqlite3_prepare_v2(db, "SELECT id,usr,file_id,name,qualified_name,def_file_id,def_line,def_column,def_line_end,def_column_end FROM class WHERE name = ? OR qualified_name LIKE ? LIMIT 1", -1, &stmt, nullptr) == SQLITE_OK) {
        std::string pat = "%" + symbol + "%";
        sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, pat.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
          c.id = sqlite3_column_int64(stmt, 0);
          c.usr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
          c.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
          c.qualified_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
          c.def_file_id = sqlite3_column_int64(stmt, 5);
          c.def_line = sqlite3_column_int(stmt, 6);
          c.def_column = sqlite3_column_int(stmt, 7);
          c.def_line_end = sqlite3_column_int(stmt, 8);
          c.def_column_end = sqlite3_column_int(stmt, 9);
          c.def_file_path = QueryFilePath(db, c.def_file_id);
        }
        sqlite3_finalize(stmt);
      }
    }
    if (c.id > 0) classes.push_back(c);
  }
  if (classes.empty()) return "{\"nodes\":[],\"edges\":[]}";

  std::vector<int64_t> class_ids;
  for (const auto& c : classes) class_ids.push_back(c.id);
  std::vector<ClassRelationRow> relations = QueryClassRelations(db, class_ids);
  std::set<std::string> usrs;
  for (const auto& c : classes) usrs.insert(c.usr);
  for (const auto& r : relations) { usrs.insert(r.parent_usr); usrs.insert(r.child_usr); }
  std::map<std::string, ClassRow> node_map;
  for (const auto& c : classes) node_map[c.usr] = c;
  for (const std::string& u : usrs) {
    if (node_map.count(u)) continue;
    ClassRow r = QueryClassByUsr(db, u);
    if (r.id > 0) node_map[u] = r;
  }

  std::ostringstream out;
  out << "{\"nodes\":[";
  bool first = true;
  for (const auto& p : node_map) {
    if (!first) out << ",";
    first = false;
    out << ClassNodeJson(p.second);
  }
  out << "],\"edges\":[";
  first = true;
  for (const auto& e : relations) {
    if (!first) out << ",";
    first = false;
    out << ClassEdgeJson(e);
  }
  out << "]}";
  return out.str();
}

std::string QueryDataFlowJson(sqlite3* db, const std::string& symbol,
                              const std::string& file) {
  if (!db) return "{\"nodes\":[],\"edges\":[]}";
  std::vector<GlobalVarRow> vars;
  int64_t file_id = 0;
  if (!file.empty()) {
    file_id = QueryFileIdByPath(db, 0, file);
    if (file_id > 0) vars = QueryGlobalVarsByFile(db, file_id);
  }
  if (vars.empty() && !symbol.empty()) {
    GlobalVarRow g = QueryGlobalVarByUsr(db, symbol);
    if (g.id == 0) {
      sqlite3_stmt* stmt = nullptr;
      if (sqlite3_prepare_v2(db, "SELECT id,usr,def_file_id,def_line,def_column,def_line_end,def_column_end,file_id,name FROM global_var WHERE name = ? LIMIT 1", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
          g.id = sqlite3_column_int64(stmt, 0);
          g.usr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
          g.def_file_id = sqlite3_column_int64(stmt, 2);
          g.def_line = sqlite3_column_int(stmt, 3);
          g.def_column = sqlite3_column_int(stmt, 4);
          g.def_line_end = sqlite3_column_int(stmt, 5);
          g.def_column_end = sqlite3_column_int(stmt, 6);
          g.file_id = sqlite3_column_int64(stmt, 7);
          g.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
          g.def_file_path = QueryFilePath(db, g.def_file_id);
        }
        sqlite3_finalize(stmt);
      }
    }
    if (g.id > 0) vars.push_back(g);
  }
  if (vars.empty()) return "{\"nodes\":[],\"edges\":[]}";

  std::set<std::string> node_usrs;
  std::vector<DataFlowEdgeRow> all_edges;
  for (const auto& g : vars) {
    node_usrs.insert("var:" + g.usr);
    for (const auto& e : QueryDataFlowEdgesByVar(db, g.id)) {
      all_edges.push_back(e);
      if (!e.reader_usr.empty()) node_usrs.insert("sym:" + e.reader_usr);
      if (!e.writer_usr.empty()) node_usrs.insert("sym:" + e.writer_usr);
    }
  }
  std::ostringstream out;
  out << "{\"nodes\":[";
  bool first = true;
  for (const auto& g : vars) {
    if (!first) out << ",";
    first = false;
    out << "{\"id\":\"var:" << g.usr << "\",\"usr\":";
    EscapeJson(g.usr, out);
    out << ",\"name\":";
    EscapeJson(g.name, out);
    out << ",\"kind\":\"global_var\",\"definition\":{\"file\":";
    EscapeJson(g.def_file_path, out);
    out << ",\"line\":" << g.def_line << ",\"column\":" << g.def_column << "}";
    out << ",\"definition_range\":{\"start_line\":" << g.def_line << ",\"start_column\":" << g.def_column;
    out << ",\"end_line\":" << g.def_line_end << ",\"end_column\":" << g.def_column_end << "}}";
  }
  for (const std::string& key : node_usrs) {
    if (key.substr(0, 4) != "sym:") continue;
    std::string usr = key.substr(4);
    SymbolRow s = QuerySymbolByUsr(db, usr);
    if (s.id == 0) continue;
    s.def_file_path = QueryFilePath(db, s.def_file_id);
    if (!first) out << ",";
    first = false;
    out << CallGraphNodeJson(s);
  }
  out << "],\"edges\":[";
  first = true;
  for (const auto& e : all_edges) {
    if (!first) out << ",";
    first = false;
    out << "{\"var\":";
    EscapeJson(e.var_usr, out);
    out << ",\"reader\":";
    EscapeJson(e.reader_usr, out);
    out << ",\"writer\":";
    EscapeJson(e.writer_usr, out);
    out << "}";
  }
  out << "]}";
  return out.str();
}

std::string QueryControlFlowJson(sqlite3* db, const std::string& symbol,
                                 const std::string& file) {
  if (!db) return "{\"nodes\":[],\"edges\":[]}";
  SymbolRow sym = QuerySymbolByUsr(db, symbol);
  if (sym.id == 0 && !symbol.empty()) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT id,usr,name,qualified_name,kind,def_file_id,def_line,def_column,def_line_end,def_column_end,decl_file_id,decl_line,decl_column,decl_line_end,decl_column_end FROM symbol WHERE name = ? AND (kind = 'function' OR kind = 'method' OR kind = 'constructor' OR kind = 'destructor') LIMIT 1", -1, &stmt, nullptr) == SQLITE_OK) {
      sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);
      if (sqlite3_step(stmt) == SQLITE_ROW) {
        sym.id = sqlite3_column_int64(stmt, 0);
        sym.usr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        sym.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        sym.qualified_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        sym.kind = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        sym.def_file_id = sqlite3_column_int64(stmt, 5);
        sym.def_line = sqlite3_column_int(stmt, 6);
        sym.def_column = sqlite3_column_int(stmt, 7);
        sym.def_line_end = sqlite3_column_int(stmt, 8);
        sym.def_column_end = sqlite3_column_int(stmt, 9);
        sym.decl_file_id = sqlite3_column_type(stmt, 10) != SQLITE_NULL ? sqlite3_column_int64(stmt, 10) : 0;
        sym.decl_line = sqlite3_column_type(stmt, 11) != SQLITE_NULL ? sqlite3_column_int(stmt, 11) : 0;
        sym.decl_column = sqlite3_column_type(stmt, 12) != SQLITE_NULL ? sqlite3_column_int(stmt, 12) : 0;
        sym.decl_line_end = sqlite3_column_type(stmt, 13) != SQLITE_NULL ? sqlite3_column_int(stmt, 13) : 0;
        sym.decl_column_end = sqlite3_column_type(stmt, 14) != SQLITE_NULL ? sqlite3_column_int(stmt, 14) : 0;
      }
      sqlite3_finalize(stmt);
    }
  }
  if (sym.id == 0) return "{\"nodes\":[],\"edges\":[]}";
  std::vector<CfgNodeRow> nodes = QueryCfgNodesBySymbolId(db, sym.id);
  if (nodes.empty()) return "{\"nodes\":[],\"edges\":[]}";
  std::vector<int64_t> node_ids;
  for (const auto& n : nodes) node_ids.push_back(n.id);
  std::vector<CfgEdgeRow> edges = QueryCfgEdgesByFromNodeIds(db, node_ids);

  std::ostringstream out;
  out << "{\"nodes\":[";
  bool first = true;
  for (const auto& n : nodes) {
    if (!first) out << ",";
    first = false;
    out << "{\"id\":\"cfg_" << n.id << "\",\"block_id\":";
    EscapeJson(n.block_id, out);
    out << ",\"kind\":";
    EscapeJson(n.kind, out);
    out << ",\"file\":";
    EscapeJson(n.file_path, out);
    out << ",\"line\":" << n.line << ",\"column\":" << n.column << "}";
  }
  out << "],\"edges\":[";
  first = true;
  for (const auto& e : edges) {
    if (!first) out << ",";
    first = false;
    out << "{\"from\":\"cfg_" << e.from_node_id << "\",\"to\":\"cfg_" << e.to_node_id << "\",\"edge_type\":";
    EscapeJson(e.edge_type, out);
    out << "}";
  }
  out << "]}";
  return out.str();
}

std::string QuerySymbolAtLocationJson(sqlite3* db, int64_t project_id,
                                      const std::string& file_path, int line, int column) {
  if (!db || line <= 0) return "[]";
  int64_t file_id = QueryFileIdByPath(db, project_id, file_path);
  if (file_id == 0) return "[]";
  std::vector<SymbolRow> symbols = QuerySymbolsByFileAndLine(db, file_id, line, column);
  if (symbols.empty()) return "[]";
  for (auto& s : symbols) {
    if (s.def_file_id) s.def_file_path = QueryFilePath(db, s.def_file_id);
    if (s.decl_file_id) s.decl_file_path = QueryFilePath(db, s.decl_file_id);
  }
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < symbols.size(); ++i) {
    if (i) out << ",";
    out << CallGraphNodeJson(symbols[i]);
  }
  out << "]";
  return out.str();
}

}  // namespace codexray
