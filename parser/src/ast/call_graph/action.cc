/**
 * 解析引擎 AST：调用链 Action
 * 无 Clang 时占位；有 Clang 时实现 FrontendAction + RecursiveASTVisitor 收集 symbol 与 call_edge。
 * 参考：doc/01-解析引擎 解析引擎详细功能与架构设计 §4.5
 */

#include "ast/call_graph/action.h"
#include "ast/function_pointer/analyzer.h"
#include "compile_commands/load.h"
#include "common/clang_include_detector.h"
#include "common/logger.h"
#include "common/path_util.h"
#include "db/writer/writer.h"
#include <string>
#include <vector>

#ifdef CODEXRAY_HAVE_CLANG
#include "clang/AST/ASTContext.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Index/USRGeneration.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/StringRef.h"
#include <memory>
#endif

namespace codexray {

#ifndef CODEXRAY_HAVE_CLANG

bool RunCallGraphOnTU(const TUEntry& /* tu */, CallGraphOutput* /* out */) {
  LogInfo("RunCallGraphOnTU: stub (no Clang), skipping");
  return true;
}

void* CreateCallGraphAction(CallGraphOutput* /* out */) {
  return nullptr;
}

#else  // CODEXRAY_HAVE_CLANG

namespace {

using namespace clang;

static int GetLine(SourceManager& SM, SourceLocation loc) {
  if (loc.isInvalid()) return 0;
  return SM.getSpellingLineNumber(loc);
}
static int GetColumn(SourceManager& SM, SourceLocation loc) {
  if (loc.isInvalid()) return 0;
  return SM.getSpellingColumnNumber(loc);
}

class CallGraphVisitor : public RecursiveASTVisitor<CallGraphVisitor> {
 public:
  CallGraphVisitor(ASTContext* ctx, CallGraphOutput* out)
      : ctx_(ctx), out_(out), sm_(&ctx->getSourceManager()) {}

  bool TraverseFunctionDecl(FunctionDecl* D) {
    if (!D || !out_) return true;
    if (D->isImplicit() || !D->getLocation().isValid())
      return RecursiveASTVisitor::TraverseFunctionDecl(D);
    if (!D->isThisDeclarationADefinition())
      return RecursiveASTVisitor::TraverseFunctionDecl(D);
    SymbolRecord sym = SymbolFromDecl(D);
    if (sym.usr.empty()) return RecursiveASTVisitor::TraverseFunctionDecl(D);
    function_usr_stack_.push_back(current_function_usr_);
    current_function_usr_ = sym.usr;
    out_->symbols.push_back(std::move(sym));
    bool ok = RecursiveASTVisitor::TraverseFunctionDecl(D);
    if (!function_usr_stack_.empty()) {
      current_function_usr_ = function_usr_stack_.back();
      function_usr_stack_.pop_back();
    } else {
      current_function_usr_.clear();
    }
    return ok;
  }

  bool VisitCallExpr(CallExpr* E) {
    if (!E || !out_ || current_function_usr_.empty()) return true;
    CallEdgeRecord edge;
    edge.caller_usr = current_function_usr_;
    edge.edge_type = "direct";
    SourceLocation loc = E->getBeginLoc();
    edge.call_site_line = GetLine(*sm_, loc);
    edge.call_site_column = GetColumn(*sm_, loc);

    const Expr* calleeExpr = E->getCallee()->IgnoreParenImpCasts();
    const FunctionDecl* callee = nullptr;
    if (const auto* dre = dyn_cast<DeclRefExpr>(calleeExpr)) {
      if (const auto* fd = dyn_cast_or_null<FunctionDecl>(dre->getDecl()))
        callee = fd;
    }
    if (callee) {
      edge.callee_usr = GetUSR(callee);
      if (!edge.callee_usr.empty())
        out_->edges.push_back(std::move(edge));
      return true;
    }
    std::vector<std::string> possible = GetPossibleCallees(E, *ctx_, current_function_usr_);
    edge.edge_type = "via_function_pointer";
    for (const std::string& usr : possible) {
      if (usr.empty()) continue;
      CallEdgeRecord e2 = edge;
      e2.callee_usr = usr;
      out_->edges.push_back(std::move(e2));
    }
    return true;
  }

 private:
  ASTContext* ctx_;
  CallGraphOutput* out_;
  SourceManager* sm_;
  std::string current_function_usr_;
  std::vector<std::string> function_usr_stack_;

  std::string GetUSR(const Decl* D) {
    if (!D) return {};
    llvm::SmallString<256> buf;
    if (index::generateUSRForDecl(D, buf))
      return {};
    return std::string(buf.str());
  }

  SymbolRecord SymbolFromDecl(const FunctionDecl* D) {
    SymbolRecord sym;
    sym.usr = GetUSR(D);
    if (sym.usr.empty()) return sym;
    sym.name = D->getNameAsString();
    if (const DeclContext* dc = D->getEnclosingNamespaceContext())
      if (!dc->isTranslationUnit())
        sym.qualified_name = D->getQualifiedNameAsString();
    if (sym.qualified_name.empty()) sym.qualified_name = sym.name;
    sym.kind = "function";
    SourceLocation start = D->getBeginLoc(), end = D->getEndLoc();
    sym.def_line = GetLine(*sm_, start);
    sym.def_column = GetColumn(*sm_, start);
    sym.def_line_end = GetLine(*sm_, end);
    sym.def_column_end = GetColumn(*sm_, end);
    return sym;
  }
};

class CallGraphConsumer : public ASTConsumer {
 public:
  CallGraphConsumer(ASTContext* ctx, CallGraphOutput* out)
      : ctx_(ctx), out_(out) {}
  void HandleTranslationUnit(ASTContext& ctx) override {
    CallGraphVisitor visitor(ctx_, out_);
    visitor.TraverseDecl(ctx.getTranslationUnitDecl());
  }
 private:
  ASTContext* ctx_;
  CallGraphOutput* out_;
};

class CallGraphAction : public ASTFrontendAction {
 public:
  explicit CallGraphAction(CallGraphOutput* out) : out_(out) {}
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance& CI,
                                                 StringRef) override {
    return std::make_unique<CallGraphConsumer>(&CI.getASTContext(), out_);
  }
 private:
  CallGraphOutput* out_;
};

class CallGraphActionFactory : public tooling::FrontendActionFactory {
 public:
  explicit CallGraphActionFactory(CallGraphOutput* out) : out_(out) {}
  std::unique_ptr<FrontendAction> create() override {
    return std::make_unique<CallGraphAction>(out_);
  }
 private:
  CallGraphOutput* out_;
};

}  // namespace

void RunCallGraphAnalysis(clang::ASTContext& ctx, CallGraphOutput* out) {
  if (!out) return;
  CallGraphVisitor visitor(&ctx, out);
  visitor.TraverseDecl(ctx.getTranslationUnitDecl());
}

bool RunCallGraphOnTU(const TUEntry& tu, CallGraphOutput* out) {
  if (!out) {
    LogError("RunCallGraphOnTU: out is null");
    return false;
  }
  std::string dir = tu.working_directory;
  if (dir.empty()) {
    size_t pos = tu.source_file.find_last_of("/\\");
    dir = (pos != std::string::npos) ? tu.source_file.substr(0, pos + 1) : ".";
  }
  dir = NormalizePath(dir);
  if (dir.empty()) dir = ".";

  auto db = std::make_unique<tooling::FixedCompilationDatabase>(
      dir, llvm::ArrayRef<std::string>(tu.compile_args));
  tooling::ClangTool tool(*db, llvm::ArrayRef<std::string>(tu.source_file));

  ClangIncludeEnv env = GetClangIncludeEnv();
  std::vector<std::string> extra;
  if (!env.resource_dir.empty()) {
    extra.push_back("-resource-dir");
    extra.push_back(env.resource_dir);
  }
  for (const std::string& p : env.system_include_paths) {
    extra.push_back("-isystem");
    extra.push_back(p);
  }
  if (!extra.empty())
    tool.appendArgumentsAdjuster(
        tooling::getInsertArgumentAdjuster(extra, tooling::ArgumentInsertPosition::BEGIN));

  CallGraphActionFactory factory(out);
  int ret = tool.run(&factory);
  if (ret != 0) {
    LogError("RunCallGraphOnTU: ClangTool run returned %d for %s", ret, tu.source_file.c_str());
    return false;
  }
  LogInfo("RunCallGraphOnTU: %zu symbols, %zu edges for %s",
          out->symbols.size(), out->edges.size(), tu.source_file.c_str());
  return true;
}

void* CreateCallGraphAction(CallGraphOutput* out) {
  if (!out) return nullptr;
  return new CallGraphAction(out);
}

#endif  // CODEXRAY_HAVE_CLANG

}  // namespace codexray
