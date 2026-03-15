/**
 * 数据流分析：全局变量读写点。
 * 设计 §3.10。
 */
#pragma once
#include "common/analysis_output.h"
#include <vector>

#ifdef CODEXRAY_HAVE_CLANG
#include <clang/AST/ASTContext.h>

namespace codexray {
namespace data_flow {

void Analyze(clang::ASTContext& ctx,
             std::vector<GlobalVarRow>* global_vars,
             std::vector<DataFlowEdgeRow>* edges,
             std::vector<std::string>* referenced_files);

}  // namespace data_flow
}  // namespace codexray
#endif
