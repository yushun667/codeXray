/**
 * 解析引擎 AST：调用链 FrontendAction
 * 参考：doc/01-解析引擎 解析引擎详细功能与架构设计 §4.5
 * 当 CODEXRAY_HAVE_CLANG 时实现 Clang FrontendAction；否则占位。
 */

#ifndef CODEXRAY_PARSER_AST_CALL_GRAPH_ACTION_H_
#define CODEXRAY_PARSER_AST_CALL_GRAPH_ACTION_H_

#ifdef CODEXRAY_HAVE_CLANG
namespace clang { class ASTContext; }
#endif

#include "db/writer/writer.h"
#include <memory>
#include <vector>

namespace codexray {

struct TUEntry;

/** 单 TU 解析结果，供 db/writer 写入 */
struct CallGraphOutput {
  std::vector<SymbolRecord> symbols;
  std::vector<CallEdgeRecord> edges;
};

/**
 * 对单个 TU 运行调用图分析，结果写入 out。
 * 无 Clang 时为占位（out 保持空）；有 Clang 时运行 FrontendAction 填充 out。
 */
bool RunCallGraphOnTU(const TUEntry& tu, CallGraphOutput* out);

#ifdef CODEXRAY_HAVE_CLANG
/** 在已构建的 AST 上仅运行调用图分析（供单次解析合并用） */
void RunCallGraphAnalysis(clang::ASTContext& ctx, CallGraphOutput* out);
#endif

/** 创建 FrontendAction（有 Clang 时返回非空；无 Clang 时返回 nullptr，由 RunCallGraphOnTU 占位） */
void* CreateCallGraphAction(CallGraphOutput* out);

}  // namespace codexray

#endif  // CODEXRAY_PARSER_AST_CALL_GRAPH_ACTION_H_
