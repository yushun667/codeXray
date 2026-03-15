/**
 * 查询选项：CLI 与 query 层共用。
 */
#pragma once
#ifndef CODEXRAY_QUERY_OPTIONS_H
#define CODEXRAY_QUERY_OPTIONS_H
#include <string>
#include <vector>

namespace codexray {

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

}  // namespace codexray
#endif
