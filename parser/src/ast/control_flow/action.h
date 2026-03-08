/**
 * 解析引擎 AST：控制流 CFG
 * 参考：doc/01-解析引擎 解析引擎详细功能与架构设计 §4.9
 */

#ifndef CODEXRAY_PARSER_AST_CONTROL_FLOW_ACTION_H_
#define CODEXRAY_PARSER_AST_CONTROL_FLOW_ACTION_H_

#include <cstdint>
#include <string>
#include <vector>

namespace codexray {

struct TUEntry;

struct CfgNodeRecord {
  int64_t symbol_id = 0;
  std::string block_id;
  std::string kind;
  int64_t file_id = 0;
  int line = 0, column = 0;
};

struct CfgEdgeRecord {
  int64_t from_node_id = 0;
  int64_t to_node_id = 0;
  std::string edge_type;
};

struct ControlFlowOutput {
  std::vector<CfgNodeRecord> nodes;
  std::vector<CfgEdgeRecord> edges;
};

bool RunControlFlowOnTU(const TUEntry& tu, ControlFlowOutput* out);

}  // namespace codexray

#endif  // CODEXRAY_PARSER_AST_CONTROL_FLOW_ACTION_H_
