/**
 * 解析引擎 AST：数据流
 * 无 Clang 时占位；有 Clang 时收集文件作用域全局变量及其读写点（引用/赋值的函数 USR）。
 * 参考：doc/01-解析引擎 解析引擎详细功能与架构设计 §4.8
 */

#include "ast/data_flow/action.h"
#include "compile_commands/load.h"
#include "common/logger.h"
#include "common/path_util.h"
#include <string>
#include <unordered_set>
#include <vector>

#ifdef CODEXRAY_HAVE_CLANG
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Index/USRGeneration.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include <memory>
#endif

namespace codexray {

#ifndef CODEXRAY_HAVE_CLANG

bool RunDataFlowOnTU(const TUEntry& /* tu */, DataFlowOutput* /* out */) {
  LogInfo("RunDataFlowOnTU: stub");
  return true;
}

#else  // CODEXRAY_HAVE_CLANG

namespace {

using namespace clang;

static int GetLine(SourceManager& SM, SourceLocation loc) {
  return loc.isValid() ? SM.getSpellingLineNumber(loc) : 0;
}
static int GetColumn(SourceManager& SM, SourceLocation loc) {
  return loc.isValid() ? SM.getSpellingColumnNumber(loc) : 0;
}

class DataFlowVisitor : public RecursiveASTVisitor<DataFlowVisitor> {
 public:
  DataFlowVisitor(ASTContext* ctx, DataFlowOutput* out)
      : ctx_(ctx), out_(out), sm_(&ctx->getSourceManager()) {}

  bool TraverseFunctionDecl(FunctionDecl* D) {
    if (!D || !out_) return RecursiveASTVisitor::TraverseFunctionDecl(D);
    if (!D->isThisDeclarationADefinition()) return RecursiveASTVisitor::TraverseFunctionDecl(D);
    std::string usr = GetUSR(D);
    if (usr.empty()) return RecursiveASTVisitor::TraverseFunctionDecl(D);
    current_function_usr_stack_.push_back(current_function_usr_);
    current_function_usr_ = usr;
    bool ok = RecursiveASTVisitor::TraverseFunctionDecl(D);
    if (!current_function_usr_stack_.empty()) {
      current_function_usr_ = current_function_usr_stack_.back();
      current_function_usr_stack_.pop_back();
    } else {
      current_function_usr_.clear();
    }
    return ok;
  }

  bool VisitVarDecl(VarDecl* D) {
    if (!D || !out_) return true;
    if (!D->hasGlobalStorage() || D->isLocalVarDeclOrParm()) return true;
    if (D->isImplicit() || D->getLocation().isInvalid()) return true;
    GlobalVarRecord rec;
    rec.usr = GetUSR(D);
    if (rec.usr.empty()) return true;
    rec.name = D->getNameAsString();
    SourceLocation start = D->getBeginLoc(), end = D->getEndLoc();
    rec.def_line = GetLine(*sm_, start);
    rec.def_column = GetColumn(*sm_, start);
    rec.def_line_end = GetLine(*sm_, end);
    rec.def_column_end = GetColumn(*sm_, end);
    out_->global_vars.push_back(rec);
    global_usr_set_.insert(rec.usr);
    return true;
  }

  bool VisitDeclRefExpr(DeclRefExpr* E) {
    if (!E || !out_ || current_function_usr_.empty()) return true;
    const VarDecl* vd = dyn_cast<VarDecl>(E->getDecl());
    if (!vd || global_usr_set_.find(GetUSR(vd)) == global_usr_set_.end()) return true;
    DataFlowEdgeRecord edge;
    edge.var_usr = GetUSR(vd);
    edge.reader_usr = current_function_usr_;
    out_->edges.push_back(edge);
    return true;
  }

  bool VisitBinaryOperator(BinaryOperator* BO) {
    if (!BO || !out_ || current_function_usr_.empty()) return true;
    if (BO->getOpcode() != BO_Assign && !BO->isCompoundAssignmentOp()) return true;
    const Expr* lhs = BO->getLHS()->IgnoreParenImpCasts();
    const auto* dre = dyn_cast<DeclRefExpr>(lhs);
    if (!dre) return true;
    const VarDecl* vd = dyn_cast<VarDecl>(dre->getDecl());
    if (!vd || global_usr_set_.find(GetUSR(vd)) == global_usr_set_.end()) return true;
    DataFlowEdgeRecord edge;
    edge.var_usr = GetUSR(vd);
    edge.writer_usr = current_function_usr_;
    out_->edges.push_back(edge);
    return true;
  }

  bool VisitUnaryOperator(UnaryOperator* UO) {
    if (!UO || !out_ || current_function_usr_.empty()) return true;
    if (!UO->isIncrementDecrementOp()) return true;
    const Expr* op = UO->getSubExpr()->IgnoreParenImpCasts();
    const auto* dre = dyn_cast<DeclRefExpr>(op);
    if (!dre) return true;
    const VarDecl* vd = dyn_cast<VarDecl>(dre->getDecl());
    if (!vd || global_usr_set_.find(GetUSR(vd)) == global_usr_set_.end()) return true;
    DataFlowEdgeRecord edge;
    edge.var_usr = GetUSR(vd);
    edge.writer_usr = current_function_usr_;
    out_->edges.push_back(edge);
    return true;
  }

 private:
  ASTContext* ctx_;
  DataFlowOutput* out_;
  SourceManager* sm_;
  std::string current_function_usr_;
  std::vector<std::string> current_function_usr_stack_;
  std::unordered_set<std::string> global_usr_set_;

  std::string GetUSR(const Decl* D) {
    if (!D) return {};
    llvm::SmallString<256> buf;
    if (index::generateUSRForDecl(D, buf)) return {};
    return std::string(buf.str());
  }
};

class DataFlowConsumer : public ASTConsumer {
 public:
  DataFlowConsumer(ASTContext* ctx, DataFlowOutput* out) : ctx_(ctx), out_(out) {}
  void HandleTranslationUnit(ASTContext& ctx) override {
    DataFlowVisitor visitor(ctx_, out_);
    visitor.TraverseDecl(ctx.getTranslationUnitDecl());
  }
 private:
  ASTContext* ctx_;
  DataFlowOutput* out_;
};

class DataFlowAction : public ASTFrontendAction {
 public:
  explicit DataFlowAction(DataFlowOutput* out) : out_(out) {}
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance& CI, StringRef) override {
    return std::make_unique<DataFlowConsumer>(&CI.getASTContext(), out_);
  }
 private:
  DataFlowOutput* out_;
};

class DataFlowActionFactory : public tooling::FrontendActionFactory {
 public:
  explicit DataFlowActionFactory(DataFlowOutput* out) : out_(out) {}
  std::unique_ptr<FrontendAction> create() override {
    return std::make_unique<DataFlowAction>(out_);
  }
 private:
  DataFlowOutput* out_;
};

}  // namespace

bool RunDataFlowOnTU(const TUEntry& tu, DataFlowOutput* out) {
  if (!out) return true;
  std::string dir = tu.working_directory.empty() ? "." : NormalizePath(tu.working_directory);
  size_t pos = tu.source_file.find_last_of("/\\");
  if (dir == "." && pos != std::string::npos)
    dir = NormalizePath(tu.source_file.substr(0, pos + 1));
  if (dir.empty()) dir = ".";

  auto db = std::make_unique<tooling::FixedCompilationDatabase>(
      dir, llvm::ArrayRef<std::string>(tu.compile_args));
  tooling::ClangTool tool(*db, llvm::ArrayRef<std::string>(tu.source_file));
  DataFlowActionFactory factory(out);
  int ret = tool.run(&factory);
  if (ret != 0) {
    LogError("RunDataFlowOnTU: ClangTool returned %d for %s", ret, tu.source_file.c_str());
    return false;
  }
  LogInfo("RunDataFlowOnTU: %zu global_vars, %zu edges for %s",
          out->global_vars.size(), out->edges.size(), tu.source_file.c_str());
  return true;
}

#endif  // CODEXRAY_HAVE_CLANG

}  // namespace codexray
