/**
 * DB 读取与查询接口。
 * 设计 §3.14。
 */
#pragma once
#include "common/analysis_output.h"
#include <sqlite3.h>
#include <cstdint>
#include <string>
#include <vector>

namespace codexray {

// ─── 查询结果节点/边（供 query 层 JSON 化）────────────────────────────────────

struct QueryNode {
  int64_t     id;
  std::string usr;
  std::string name;
  std::string qualified_name;
  std::string kind;
  // definition location
  std::string def_file;
  int def_line    = 0;
  int def_column  = 0;
  int def_line_end = 0;
  int def_col_end  = 0;
};

struct QueryEdge {
  int64_t     id;
  int64_t     from_id;
  int64_t     to_id;
  std::string edge_type;
  std::string call_file;
  int call_line   = 0;
  int call_column = 0;
};

struct ClassQueryNode {
  int64_t     id;
  std::string usr;
  std::string name;
  std::string qualified_name;
  std::string def_file;
  int def_line   = 0;
  int def_column = 0;
  int def_line_end = 0;
  int def_col_end  = 0;
};

struct ClassQueryEdge {
  int64_t parent_id;
  int64_t child_id;
  std::string relation_type;
};

struct GVarNode {
  int64_t     id;
  std::string usr;
  std::string name;
  std::string def_file;
  int def_line   = 0;
  int def_column = 0;
};

struct DataFlowEdge {
  int64_t accessor_id;
  std::string accessor_name;
  std::string access_type;
  std::string access_file;
  int access_line   = 0;
  int access_column = 0;
};

struct CfgNodeQ {
  int64_t id;
  int block_id = 0;
  std::string file;
  int begin_line = 0;
  int begin_col  = 0;
  int end_line   = 0;
  int end_col    = 0;
  std::string label;
};

struct CfgEdgeQ {
  int64_t from_node_id;
  int64_t to_node_id;
  std::string edge_type;
};

// ─── project / file helpers ───────────────────────────────────────────────────

// Return project id for given root_path (0 if not found, -1 to upsert/create)
int64_t GetProjectId(sqlite3* db, const std::string& root_path);

// Get or create project record, return id
int64_t EnsureProjectId(sqlite3* db, const std::string& root_path,
                        const std::string& compile_commands_path = "");

// Return file id for given (project_id, path); 0 if not found
int64_t QueryFileIdByPath(sqlite3* db, int64_t project_id, const std::string& path);

// Return file path for given file id; "" if not found
std::string QueryFilePathById(sqlite3* db, int64_t file_id);

// Check whether a file has been parsed in the given project
bool IsFileParsed(sqlite3* db, int64_t project_id, int64_t file_id);

// ─── symbol queries ───────────────────────────────────────────────────────────

// Resolve symbol at file+line+column; returns 0 or 1 results (best match)
std::vector<SymbolRow> QuerySymbolsByFileAndLine(sqlite3* db, int64_t file_id,
                                                  int line, int column);

// Find symbols by name (partial match)
std::vector<QueryNode> QuerySymbolsByName(sqlite3* db, const std::string& name,
                                           int limit = 20);

// ─── call graph ───────────────────────────────────────────────────────────────

struct CallGraphResult {
  std::vector<QueryNode> nodes;
  std::vector<QueryEdge> edges;
};

/// 查询方向
enum class CallDirection { kBoth, kForward, kBackward };

// BFS from symbol (USR or name) up to depth with direction-aware recursion.
// kBoth: root level queries both callers & callees; subsequent levels
//   continue only in the direction each node was discovered from.
// kForward/kBackward: only query callees/callers at every level.
CallGraphResult QueryCallGraph(sqlite3* db, const std::string& symbol_usr_or_name,
                               const std::string& file_path, int depth = 2,
                               CallDirection direction = CallDirection::kBoth);

// ─── class graph ──────────────────────────────────────────────────────────────

struct ClassGraphResult {
  std::vector<ClassQueryNode> nodes;
  std::vector<ClassQueryEdge> edges;
};

ClassGraphResult QueryClassGraph(sqlite3* db, const std::string& symbol_usr_or_name,
                                 const std::string& file_path);

// ─── data flow ────────────────────────────────────────────────────────────────

struct DataFlowResult {
  GVarNode               var;
  std::vector<DataFlowEdge> edges;
};

DataFlowResult QueryDataFlow(sqlite3* db, const std::string& symbol_usr_or_name,
                              const std::string& file_path);

// ─── control flow ─────────────────────────────────────────────────────────────

struct ControlFlowResult {
  std::vector<CfgNodeQ> nodes;
  std::vector<CfgEdgeQ> edges;
};

// db_dir：数据库文件所在目录，用于定位 cfg/ 子目录中的 pb 文件
ControlFlowResult QueryControlFlow(sqlite3* db, const std::string& db_dir,
                                    const std::string& symbol_usr_or_name,
                                    const std::string& file_path);

// ─── symbol_at ────────────────────────────────────────────────────────────────

std::vector<QueryNode> QuerySymbolsAt(sqlite3* db, int64_t project_id,
                                       const std::string& file_path,
                                       int line, int column);

}  // namespace codexray
