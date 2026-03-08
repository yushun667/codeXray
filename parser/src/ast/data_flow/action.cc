/**
 * 解析引擎 AST：数据流（占位，有 Clang 时实现全局变量与读写点）
 */

#include "ast/data_flow/action.h"
#include "compile_commands/load.h"
#include "common/logger.h"

namespace codexray {

bool RunDataFlowOnTU(const TUEntry& /* tu */, DataFlowOutput* /* out */) {
  LogInfo("RunDataFlowOnTU: stub");
  return true;
}

}  // namespace codexray
