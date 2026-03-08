/**
 * 解析引擎 AST：数据流 FrontendAction
 * 参考：doc/01-解析引擎 解析引擎详细功能与架构设计 §4.8
 */

#ifndef CODEXRAY_PARSER_AST_DATA_FLOW_ACTION_H_
#define CODEXRAY_PARSER_AST_DATA_FLOW_ACTION_H_

#include <cstdint>
#include <string>
#include <vector>

namespace codexray {

struct TUEntry;

struct GlobalVarRecord {
  std::string usr;
  int64_t def_file_id = 0;
  int def_line = 0, def_column = 0, def_line_end = 0, def_column_end = 0;
  int64_t file_id = 0;
  std::string name;
};

struct DataFlowEdgeRecord {
  int64_t var_id = 0;
  int64_t reader_id = 0;
  int64_t writer_id = 0;
};

struct DataFlowOutput {
  std::vector<GlobalVarRecord> global_vars;
  std::vector<DataFlowEdgeRecord> edges;
};

bool RunDataFlowOnTU(const TUEntry& tu, DataFlowOutput* out);

}  // namespace codexray

#endif  // CODEXRAY_PARSER_AST_DATA_FLOW_ACTION_H_
