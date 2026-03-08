/**
 * 解析引擎 AST：数据流 FrontendAction
 * 参考：doc/01-解析引擎 解析引擎详细功能与架构设计 §4.8
 */

#ifndef CODEXRAY_PARSER_AST_DATA_FLOW_ACTION_H_
#define CODEXRAY_PARSER_AST_DATA_FLOW_ACTION_H_

#ifdef CODEXRAY_HAVE_CLANG
namespace clang { class ASTContext; }
#endif

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
  std::string var_usr;    // 写入时解析为 var_id
  std::string reader_usr; // 写入时解析为 reader_id (symbol)
  std::string writer_usr; // 写入时解析为 writer_id (symbol)
  int64_t read_file_id = 0;
  int read_line = 0, read_column = 0;
  int64_t write_file_id = 0;
  int write_line = 0, write_column = 0;
};

struct DataFlowOutput {
  std::vector<GlobalVarRecord> global_vars;
  std::vector<DataFlowEdgeRecord> edges;
};

bool RunDataFlowOnTU(const TUEntry& tu, DataFlowOutput* out);

#ifdef CODEXRAY_HAVE_CLANG
void RunDataFlowAnalysis(clang::ASTContext& ctx, DataFlowOutput* out);
#endif

}  // namespace codexray

#endif  // CODEXRAY_PARSER_AST_DATA_FLOW_ACTION_H_
