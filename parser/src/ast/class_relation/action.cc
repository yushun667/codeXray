/**
 * 解析引擎 AST：类关系（占位，有 Clang 时实现 CXXRecordDecl 遍历）
 */

#include "ast/class_relation/action.h"
#include "compile_commands/load.h"
#include "common/logger.h"

namespace codexray {

bool RunClassRelationOnTU(const TUEntry& /* tu */, ClassRelationOutput* /* out */) {
  LogInfo("RunClassRelationOnTU: stub");
  return true;
}

}  // namespace codexray
