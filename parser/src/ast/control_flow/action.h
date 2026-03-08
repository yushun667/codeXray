/**
 * 解析引擎 AST：控制流 CFG
 * 参考：doc/01-解析引擎 解析引擎详细功能与架构设计 §4.9
 */

#ifndef CODEXRAY_PARSER_AST_CONTROL_FLOW_ACTION_H_
#define CODEXRAY_PARSER_AST_CONTROL_FLOW_ACTION_H_

#ifdef CODEXRAY_HAVE_CLANG
namespace clang { class ASTContext; }
#endif

#include <cstdint>
#include <string>
#include <vector>

namespace codexray {

struct TUEntry;

struct CfgNodeRecord {
  std::string symbol_usr;  // 所属函数 USR，写入时解析为 symbol_id
  std::string block_id;
  std::string kind;
  int64_t file_id = 0;
  int line = 0, column = 0;
};

struct CfgEdgeRecord {
  int from_node_index = 0;  // 在本次输出的 nodes 数组中的下标，写入时解析为 from_node_id
  int to_node_index = 0;
  std::string edge_type;
};

struct ControlFlowOutput {
  std::vector<CfgNodeRecord> nodes;
  std::vector<CfgEdgeRecord> edges;
};

bool RunControlFlowOnTU(const TUEntry& tu, ControlFlowOutput* out);

#ifdef CODEXRAY_HAVE_CLANG
void RunControlFlowAnalysis(clang::ASTContext& ctx, ControlFlowOutput* out);
#endif

}  // namespace codexray

#endif  // CODEXRAY_PARSER_AST_CONTROL_FLOW_ACTION_H_
