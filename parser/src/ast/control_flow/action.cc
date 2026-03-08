/**
 * 解析引擎 AST：控制流（占位，有 Clang 时使用 CFG 接口）
 */

#include "ast/control_flow/action.h"
#include "compile_commands/load.h"
#include "common/logger.h"

namespace codexray {

bool RunControlFlowOnTU(const TUEntry& /* tu */, ControlFlowOutput* /* out */) {
  LogInfo("RunControlFlowOnTU: stub");
  return true;
}

}  // namespace codexray
