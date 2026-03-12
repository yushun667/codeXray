#include "analyzer.h"

#ifdef CODEXRAY_HAVE_CLANG
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Index/USRGeneration.h>
#include <llvm/ADT/SmallString.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

namespace codexray {
namespace function_pointer {

namespace {

// Generate USR string for a Decl
static std::string GenUSR(const clang::Decl* d) {
  llvm::SmallString<128> buf;
  if (clang::index::generateUSRForDecl(d, buf)) return "";
  return buf.str().str();
}

// Visitor：收集函数指针赋值，建立 VarDecl USR → 被赋值函数 USR 的映射
class AssignmentVisitor
    : public clang::RecursiveASTVisitor<AssignmentVisitor> {
public:
  explicit AssignmentVisitor(
      std::unordered_map<std::string, std::vector<std::string>>* map)
      : map_(map) {}

  bool VisitVarDecl(clang::VarDecl* vd) {
    if (!vd) return true;
    clang::QualType qt = vd->getType().getCanonicalType();
    bool is_fp = qt->isFunctionPointerType();
    bool is_mfp = qt->isMemberFunctionPointerType();
    if (!is_fp && !is_mfp) return true;
    if (auto* init = vd->getInit()) CollectFunctionRef(GenUSR(vd), init);
    return true;
  }

  bool VisitBinaryOperator(clang::BinaryOperator* bo) {
    if (!bo->isAssignmentOp()) return true;
    clang::QualType qt = bo->getLHS()->getType().getCanonicalType();
    if (!qt->isFunctionPointerType() && !qt->isMemberFunctionPointerType()) return true;
    // LHS: try to get VarDecl
    if (auto* dr = clang::dyn_cast<clang::DeclRefExpr>(
            bo->getLHS()->IgnoreParenImpCasts())) {
      if (auto* vd = clang::dyn_cast<clang::VarDecl>(dr->getDecl())) {
        CollectFunctionRef(GenUSR(vd), bo->getRHS());
      }
    }
    return true;
  }

private:
  void CollectFunctionRef(const std::string& var_usr, clang::Expr* expr) {
    if (var_usr.empty()) return;
    expr = expr->IgnoreParenImpCasts();
    // 普通函数引用：DeclRefExpr → FunctionDecl
    if (auto* dr = clang::dyn_cast<clang::DeclRefExpr>(expr)) {
      if (auto* fd = clang::dyn_cast<clang::FunctionDecl>(dr->getDecl())) {
        std::string usr = GenUSR(fd);
        if (!usr.empty()) (*map_)[var_usr].push_back(usr);
      }
    }
    // 成员函数指针：UnaryOperator(&) 包裹的 DeclRefExpr → CXXMethodDecl
    // 例：void (MyClass::*fp)() = &MyClass::foo;
    if (auto* uo = clang::dyn_cast<clang::UnaryOperator>(expr)) {
      if (uo->getOpcode() == clang::UO_AddrOf) {
        auto* inner = uo->getSubExpr()->IgnoreParenImpCasts();
        if (auto* dr = clang::dyn_cast<clang::DeclRefExpr>(inner)) {
          if (auto* md = clang::dyn_cast<clang::CXXMethodDecl>(dr->getDecl())) {
            std::string usr = GenUSR(md);
            if (!usr.empty()) (*map_)[var_usr].push_back(usr);
          }
        }
      }
    }
  }
  std::unordered_map<std::string, std::vector<std::string>>* map_;
};

}  // namespace

void CollectAssignments(
    clang::ASTContext& ctx,
    std::unordered_map<std::string, std::vector<std::string>>* fu_map) {
  AssignmentVisitor v(fu_map);
  v.TraverseDecl(ctx.getTranslationUnitDecl());
}

std::vector<std::string> GetPossibleCallees(
    clang::CallExpr* call,
    clang::ASTContext& ctx,
    const std::unordered_map<std::string, std::vector<std::string>>* fu_map) {
  std::unordered_set<std::string> found;

  // Stage 1: look up callee expr → VarDecl → fu_map
  // 同时处理普通函数指针和成员函数指针（通过 VarDecl 赋值追踪）
  if (fu_map) {
    auto* callee_expr = call->getCallee()->IgnoreParenImpCasts();

    // 普通函数指针：(fp)(args) → DeclRefExpr → VarDecl
    if (auto* dr = clang::dyn_cast<clang::DeclRefExpr>(callee_expr)) {
      if (auto* vd = clang::dyn_cast<clang::VarDecl>(dr->getDecl())) {
        std::string var_usr = GenUSR(vd);
        auto it = fu_map->find(var_usr);
        if (it != fu_map->end()) {
          for (const auto& u : it->second) found.insert(u);
        }
      }
    }

    // 成员函数指针：(obj.*mfp)(args) 或 (ptr->*mfp)(args)
    // Clang 将这类调用表示为 CXXMemberCallExpr，callee 是 BinaryOperator(.* 或 ->*)
    // 外层 CallExpr 的 callee 经过 strip 后可能是 ParenExpr 包裹的 BinaryOperator
    if (found.empty()) {
      auto* raw_callee = call->getCallee()->IgnoreParens();
      if (auto* bo = clang::dyn_cast<clang::BinaryOperator>(raw_callee)) {
        if (bo->getOpcode() == clang::BO_PtrMemD ||
            bo->getOpcode() == clang::BO_PtrMemI) {
          // RHS 是成员函数指针变量
          auto* rhs = bo->getRHS()->IgnoreParenImpCasts();
          if (auto* dr = clang::dyn_cast<clang::DeclRefExpr>(rhs)) {
            if (auto* vd = clang::dyn_cast<clang::VarDecl>(dr->getDecl())) {
              std::string var_usr = GenUSR(vd);
              auto it = fu_map->find(var_usr);
              if (it != fu_map->end()) {
                for (const auto& u : it->second) found.insert(u);
              }
            }
          }
        }
      }
    }
  }

  // 仅依赖 Stage 1 数据流分析结果。若无法追踪到具体赋值，
  // 不产生任何边——宁可漏报也不误报（保守枚举会产生大量虚假边）。

  return std::vector<std::string>(found.begin(), found.end());
}

}  // namespace function_pointer
}  // namespace codexray

#else  // !CODEXRAY_HAVE_CLANG

// Stub when Clang is not available
namespace codexray {
namespace function_pointer {
}
}

#endif  // CODEXRAY_HAVE_CLANG
