/**
 * 解析引擎 AST：单次解析合并
 * 对每个 TU 只调用一次 Clang 前端，在同一 AST 上依次执行 call_graph、class_relation、data_flow、control_flow 分析，降低解析耗时。
 */

#ifndef CODEXRAY_PARSER_AST_COMBINED_ACTION_H_
#define CODEXRAY_PARSER_AST_COMBINED_ACTION_H_

#include "ast/call_graph/action.h"
#include "ast/class_relation/action.h"
#include "ast/control_flow/action.h"
#include "ast/data_flow/action.h"

namespace codexray {

struct TUEntry;

/**
 * 对单个 TU 执行一次 Clang 解析，并填充四种分析结果。
 * 有 Clang 时只跑一次 ClangTool.run；无 Clang 时退化为分别调用四个 RunXxxOnTU（占位）。
 */
bool RunAllAnalysesOnTU(const TUEntry& tu,
                        CallGraphOutput* cg_out,
                        ClassRelationOutput* cr_out,
                        DataFlowOutput* df_out,
                        ControlFlowOutput* cf_out);

}  // namespace codexray

#endif  // CODEXRAY_PARSER_AST_COMBINED_ACTION_H_
