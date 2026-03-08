/**
 * 解析引擎 AST：调用链 Action
 * 无 Clang 时占位；有 Clang 时实现 FrontendAction + RecursiveASTVisitor 收集 symbol 与 call_edge。
 */

#include "ast/call_graph/action.h"
#include "compile_commands/load.h"
#include "common/logger.h"

// 当 LLVM/Clang 可用时在此实现 Clang FrontendAction + Visitor，并在此文件中
// 实现 RunCallGraphOnTU（通过 ClangTool 运行 CreateCallGraphAction 产生的 Action）。
// 占位：不依赖 Clang，RunCallGraphOnTU 直接返回 true，CreateCallGraphAction 返回 nullptr。

namespace codexray {

bool RunCallGraphOnTU(const TUEntry& /* tu */, CallGraphOutput* /* out */) {
  LogInfo("RunCallGraphOnTU: stub (no Clang), skipping");
  return true;
}

void* CreateCallGraphAction(CallGraphOutput* /* out */) {
  return nullptr;
}

}  // namespace codexray
