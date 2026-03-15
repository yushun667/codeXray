/**
 * 控制流分析：每个函数构建 CFG，写入 cfg_node/cfg_edge。
 * 设计 §3.11。
 */
#pragma once
#include "common/analysis_output.h"
#include <vector>

#ifdef CODEXRAY_HAVE_CLANG
#include <clang/AST/ASTContext.h>

namespace codexray {
namespace control_flow {

void Analyze(clang::ASTContext& ctx,
             std::vector<CfgNodeRow>* cfg_nodes,
             std::vector<CfgEdgeRow>* cfg_edges,
             std::vector<std::string>* referenced_files);

}  // namespace control_flow
}  // namespace codexray
#endif
