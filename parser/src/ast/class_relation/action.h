/**
 * 类关系分析：继承/组合/依赖 + 成员。
 * 设计 §3.9。
 */
#pragma once
#include "common/analysis_output.h"
#include <vector>

#ifdef CODEXRAY_HAVE_CLANG
#include <clang/AST/ASTContext.h>

namespace codexray {
namespace class_relation {

void Analyze(clang::ASTContext& ctx,
             std::vector<ClassRow>* classes,
             std::vector<ClassRelationRow>* relations,
             std::vector<ClassMemberRow>* members,
             std::vector<std::string>* referenced_files);

}  // namespace class_relation
}  // namespace codexray
#endif
