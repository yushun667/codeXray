/**
 * 解析引擎 AST：函数指针可能目标分析
 * 无 Clang 时占位返回空；有 Clang 时基于调用点类型与 TU 内函数类型匹配保守枚举可能 callee。
 * 参考：doc/01-解析引擎 解析引擎详细功能与架构设计 §4.6
 */

#include "ast/function_pointer/analyzer.h"
#include "common/logger.h"
#include <vector>

#ifdef CODEXRAY_HAVE_CLANG
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/Index/USRGeneration.h"
#include "llvm/ADT/SmallString.h"
#include <string>
#endif

namespace codexray {

std::vector<std::string> GetPossibleCallees(const std::string& /* caller_usr */) {
  return {};
}

#ifdef CODEXRAY_HAVE_CLANG

namespace {

using namespace clang;

const FunctionProtoType* GetFunctionType(QualType qt) {
  qt = qt.getCanonicalType();
  if (const PointerType* pt = qt->getAs<PointerType>())
    qt = pt->getPointeeType();
  else if (const ReferenceType* rt = qt->getAs<ReferenceType>())
    qt = rt->getPointeeType();
  else
    return nullptr;
  return qt->getAs<FunctionProtoType>();
}

static void CollectMatchingInContext(DeclContext* dc, ASTContext& ctx,
                                      const FunctionProtoType* target,
                                      std::vector<std::string>* out) {
  if (!dc || !target || !out) return;
  for (Decl* d : dc->decls()) {
    if (auto* fd = dyn_cast<FunctionDecl>(d)) {
      if (!fd->isImplicit() && (fd->hasBody() || fd->isDefined())) {
        const FunctionType* ft = fd->getType()->getAs<FunctionType>();
        const FunctionProtoType* fpt = dyn_cast_or_null<FunctionProtoType>(ft);
        if (!fpt) continue;
        if (!ctx.hasSameType(target->getReturnType(), fpt->getReturnType()))
          continue;
        if (target->getNumParams() != fpt->getNumParams()) continue;
        bool params_match = true;
        for (unsigned i = 0; i < target->getNumParams(); ++i) {
          if (!ctx.hasSameType(target->getParamType(i), fpt->getParamType(i))) {
            params_match = false;
            break;
          }
        }
        if (!params_match) continue;
        llvm::SmallString<256> buf;
        if (index::generateUSRForDecl(fd, buf)) continue;
        out->push_back(std::string(buf.str()));
      }
    }
    if (auto* nd = dyn_cast<NamespaceDecl>(d))
      CollectMatchingInContext(nd, ctx, target, out);
  }
}

void CollectMatchingFunctions(ASTContext& ctx, const FunctionProtoType* target,
                              std::vector<std::string>* out) {
  if (!target || !out) return;
  CollectMatchingInContext(ctx.getTranslationUnitDecl(), ctx, target, out);
}

}  // namespace

std::vector<std::string> GetPossibleCallees(CallExpr* call, ASTContext& ctx,
                                            const std::string& caller_usr) {
  std::vector<std::string> result;
  if (!call) return result;
  const Expr* calleeExpr = call->getCallee()->IgnoreParenImpCasts();
  QualType qt = calleeExpr->getType();
  const FunctionProtoType* fpt = GetFunctionType(qt);
  if (!fpt) return result;
  CollectMatchingFunctions(ctx, fpt, &result);
  if (!result.empty())
    LogInfo("GetPossibleCallees: caller %s -> %zu possible callee(s)",
            caller_usr.c_str(), result.size());
  return result;
}

#endif  // CODEXRAY_HAVE_CLANG

}  // namespace codexray
