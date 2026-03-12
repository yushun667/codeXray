#include "json_output.h"
#include <sstream>

namespace codexray {

namespace {

static std::string JsonStr(const std::string& s) {
  std::string out;
  out += '"';
  for (char c : s) {
    if      (c == '"')  out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else                out += c;
  }
  out += '"';
  return out;
}

static std::string NodeJson(const QueryNode& n) {
  std::ostringstream o;
  o << "{"
    << "\"id\":" << n.id << ","
    << "\"usr\":" << JsonStr(n.usr) << ","
    << "\"name\":" << JsonStr(n.name) << ","
    << "\"qualified_name\":" << JsonStr(n.qualified_name) << ","
    << "\"kind\":" << JsonStr(n.kind) << ","
    << "\"definition\":{"
      << "\"file\":" << JsonStr(n.def_file) << ","
      << "\"line\":" << n.def_line << ","
      << "\"column\":" << n.def_column
    << "},"
    << "\"definition_range\":{"
      << "\"start_line\":" << n.def_line << ","
      << "\"start_column\":" << n.def_column << ","
      << "\"end_line\":" << n.def_line_end << ","
      << "\"end_column\":" << n.def_col_end
    << "}"
    << "}";
  return o.str();
}

static std::string EdgeJson(const QueryEdge& e) {
  std::ostringstream o;
  o << "{"
    << "\"id\":" << e.id << ","
    << "\"caller\":" << e.from_id << ","
    << "\"callee\":" << e.to_id << ","
    << "\"edge_type\":" << JsonStr(e.edge_type) << ","
    << "\"call_site\":{"
      << "\"file\":" << JsonStr(e.call_file) << ","
      << "\"line\":" << e.call_line << ","
      << "\"column\":" << e.call_column
    << "}"
    << "}";
  return o.str();
}

}  // namespace

std::string QueryCallGraphJson(sqlite3* db, const std::string& symbol,
                                const std::string& file_path, int depth) {
  auto res = QueryCallGraph(db, symbol, file_path, depth);
  std::ostringstream o;
  o << "{\"nodes\":[";
  bool first = true;
  for (const auto& n : res.nodes) {
    if (!first) o << ",";
    first = false;
    o << NodeJson(n);
  }
  o << "],\"edges\":[";
  first = true;
  for (const auto& e : res.edges) {
    if (!first) o << ",";
    first = false;
    o << EdgeJson(e);
  }
  o << "]}";
  return o.str();
}

std::string QueryClassGraphJson(sqlite3* db, const std::string& symbol,
                                 const std::string& file_path) {
  auto res = QueryClassGraph(db, symbol, file_path);
  std::ostringstream o;
  o << "{\"nodes\":[";
  bool first = true;
  for (const auto& n : res.nodes) {
    if (!first) o << ",";
    first = false;
    o << "{"
      << "\"id\":" << n.id << ","
      << "\"usr\":" << JsonStr(n.usr) << ","
      << "\"name\":" << JsonStr(n.name) << ","
      << "\"qualified_name\":" << JsonStr(n.qualified_name) << ","
      << "\"definition\":{"
        << "\"file\":" << JsonStr(n.def_file) << ","
        << "\"line\":" << n.def_line << ","
        << "\"column\":" << n.def_column
      << "},"
      << "\"definition_range\":{"
        << "\"start_line\":" << n.def_line << ","
        << "\"start_column\":" << n.def_column << ","
        << "\"end_line\":" << n.def_line_end << ","
        << "\"end_column\":" << n.def_col_end
      << "}"
      << "}";
  }
  o << "],\"edges\":[";
  first = true;
  for (const auto& e : res.edges) {
    if (!first) o << ",";
    first = false;
    o << "{"
      << "\"parent\":" << e.parent_id << ","
      << "\"child\":" << e.child_id << ","
      << "\"relation_type\":" << JsonStr(e.relation_type)
      << "}";
  }
  o << "]}";
  return o.str();
}

std::string QueryDataFlowJson(sqlite3* db, const std::string& symbol,
                               const std::string& file_path) {
  auto res = QueryDataFlow(db, symbol, file_path);
  std::ostringstream o;
  o << "{"
    << "\"variable\":{"
      << "\"id\":" << res.var.id << ","
      << "\"usr\":" << JsonStr(res.var.usr) << ","
      << "\"name\":" << JsonStr(res.var.name) << ","
      << "\"definition\":{"
        << "\"file\":" << JsonStr(res.var.def_file) << ","
        << "\"line\":" << res.var.def_line << ","
        << "\"column\":" << res.var.def_column
      << "}"
    << "},"
    << "\"edges\":[";
  bool first = true;
  for (const auto& e : res.edges) {
    if (!first) o << ",";
    first = false;
    o << "{"
      << "\"accessor_id\":" << e.accessor_id << ","
      << "\"accessor_name\":" << JsonStr(e.accessor_name) << ","
      << "\"access_type\":" << JsonStr(e.access_type) << ","
      << "\"location\":{"
        << "\"file\":" << JsonStr(e.access_file) << ","
        << "\"line\":" << e.access_line << ","
        << "\"column\":" << e.access_column
      << "}"
      << "}";
  }
  o << "]}";
  return o.str();
}

std::string QueryControlFlowJson(sqlite3* db, const std::string& db_dir,
                                  const std::string& symbol,
                                  const std::string& file_path) {
  auto res = QueryControlFlow(db, db_dir, symbol, file_path);
  std::ostringstream o;
  o << "{\"nodes\":[";
  bool first = true;
  for (const auto& n : res.nodes) {
    if (!first) o << ",";
    first = false;
    o << "{"
      << "\"id\":" << n.id << ","
      << "\"block_id\":" << n.block_id << ","
      << "\"label\":" << JsonStr(n.label) << ","
      << "\"location\":{"
        << "\"file\":" << JsonStr(n.file) << ","
        << "\"begin_line\":" << n.begin_line << ","
        << "\"begin_col\":" << n.begin_col << ","
        << "\"end_line\":" << n.end_line << ","
        << "\"end_col\":" << n.end_col
      << "}"
      << "}";
  }
  o << "],\"edges\":[";
  first = true;
  for (const auto& e : res.edges) {
    if (!first) o << ",";
    first = false;
    o << "{"
      << "\"from\":" << e.from_node_id << ","
      << "\"to\":" << e.to_node_id << ","
      << "\"edge_type\":" << JsonStr(e.edge_type)
      << "}";
  }
  o << "]}";
  return o.str();
}

std::string QuerySymbolAtLocationJson(sqlite3* db, int64_t project_id,
                                       const std::string& file_path,
                                       int line, int column) {
  auto syms = QuerySymbolsAt(db, project_id, file_path, line, column);
  std::ostringstream o;
  o << "{\"symbols\":[";
  bool first = true;
  for (const auto& n : syms) {
    if (!first) o << ",";
    first = false;
    o << NodeJson(n);
  }
  o << "]}";
  return o.str();
}

}  // namespace codexray
