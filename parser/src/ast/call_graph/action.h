/**
 * 调用链分析：收集函数/方法定义 + call_edge。
 * 设计 §3.7。
 * 使用类型别名避免 Clang/LLVM 头中可能存在的 Row→Record 宏影响。
 */
#pragma once
#include "common/analysis_output.h"
#include <vector>

#ifdef CODEXRAY_HAVE_CLANG
// 在包含 Clang 头之前固定类型别名，避免 LLVM 头中 #define Row Record 导致 SymbolRow→SymbolRecord
using _CG_SymbolRow = codexray::SymbolRow;
using _CG_CallEdgeRow = codexray::CallEdgeRow;
#include <clang/AST/ASTContext.h>

namespace codexray {
namespace call_graph {

void Analyze(clang::ASTContext& ctx,
             std::vector<_CG_SymbolRow>* symbols,
             std::vector<_CG_CallEdgeRow>* call_edges,
             std::vector<std::string>* referenced_files);

}  // namespace call_graph
}  // namespace codexray
#endif  // CODEXRAY_HAVE_CLANG
