/**
 * 查询 JSON 输出：将 db/reader 结果组装为接口约定 §3 规定的 JSON。
 */
#pragma once
#include "db/reader/reader.h"
#include <sqlite3.h>
#include <string>

namespace codexray {

// Query options (filled by CLI layer)
struct QueryOptions {
  std::string db_path;
  std::string project_root;
  std::string query_type;   // call_graph | class_graph | data_flow | control_flow | symbol_at
  std::string symbol;       // USR or name
  std::string file_path;
  int         line   = 0;
  int         column = 0;
  int         depth  = 2;
  std::string direction = "both";  // both | callers | callees
  bool        lazy   = false;
  bool        verbose = false;
  unsigned    parallel = 0;
  std::vector<std::string> priority_dirs;
};

std::string QueryCallGraphJson(sqlite3* db, const std::string& symbol,
                                const std::string& file_path, int depth,
                                const std::string& direction = "both");

std::string QueryClassGraphJson(sqlite3* db, const std::string& symbol,
                                 const std::string& file_path);

std::string QueryDataFlowJson(sqlite3* db, const std::string& symbol,
                               const std::string& file_path);

std::string QueryControlFlowJson(sqlite3* db, const std::string& db_dir,
                                  const std::string& symbol,
                                  const std::string& file_path);

std::string QuerySymbolAtLocationJson(sqlite3* db, int64_t project_id,
                                       const std::string& file_path,
                                       int line, int column);

}  // namespace codexray
