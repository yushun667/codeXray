/**
 * 解析引擎 AST：类关系
 * 无 Clang 时占位；有 Clang 时遍历 CXXRecordDecl 收集 class、class_relation。
 * 参考：doc/01-解析引擎 解析引擎详细功能与架构设计 §4.7
 */

#include "ast/class_relation/action.h"
#include "compile_commands/load.h"
#include "common/clang_include_detector.h"
#include "common/logger.h"
#include "common/path_util.h"
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
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include <memory>
#endif

namespace codexray {

#ifndef CODEXRAY_HAVE_CLANG

bool RunClassRelationOnTU(const TUEntry& /* tu */, ClassRelationOutput* /* out */) {
  LogInfo("RunClassRelationOnTU: stub");
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

class ClassRelationVisitor : public RecursiveASTVisitor<ClassRelationVisitor> {
 public:
  ClassRelationVisitor(ASTContext* ctx, ClassRelationOutput* out)
      : ctx_(ctx), out_(out), sm_(&ctx->getSourceManager()) {}

  bool VisitCXXRecordDecl(CXXRecordDecl* D) {
    if (!D || !out_ || D->isImplicit()) return true;
    if (!D->isThisDeclarationADefinition()) return true;
    if (D->getLocation().isInvalid()) return true;

    ClassRecord rec;
    rec.usr = GetUSR(D);
    if (rec.usr.empty()) return true;
    rec.name = D->getNameAsString();
    rec.qualified_name = D->getQualifiedNameAsString();
    if (rec.qualified_name.empty()) rec.qualified_name = rec.name;
    SourceLocation start = D->getBeginLoc(), end = D->getEndLoc();
    rec.def_line = GetLine(*sm_, start);
    rec.def_column = GetColumn(*sm_, start);
    rec.def_line_end = GetLine(*sm_, end);
    rec.def_column_end = GetColumn(*sm_, end);

    out_->classes.push_back(rec);
    std::string child_usr = rec.usr;

    for (const CXXBaseSpecifier& base : D->bases()) {
      const Type* t = base.getType().getTypePtrOrNull();
      if (!t) continue;
      CXXRecordDecl* baseDecl = t->getAsCXXRecordDecl();
      if (!baseDecl) continue;
      std::string parent_usr = GetUSR(baseDecl);
      if (parent_usr.empty()) continue;
      ClassRelationRecord rel;
      rel.parent_usr = parent_usr;
      rel.child_usr = child_usr;
      rel.relation_type = base.isVirtual() ? "inheritance" : "inheritance";
      out_->relations.push_back(rel);
    }
    return true;
  }

 private:
  ASTContext* ctx_;
  ClassRelationOutput* out_;
  SourceManager* sm_;

  std::string GetUSR(const Decl* D) {
    if (!D) return {};
    llvm::SmallString<256> buf;
    if (index::generateUSRForDecl(D, buf)) return {};
    return std::string(buf.str());
  }
};

class ClassRelationConsumer : public ASTConsumer {
 public:
  ClassRelationConsumer(ASTContext* ctx, ClassRelationOutput* out)
      : ctx_(ctx), out_(out) {}
  void HandleTranslationUnit(ASTContext& ctx) override {
    ClassRelationVisitor visitor(ctx_, out_);
    visitor.TraverseDecl(ctx.getTranslationUnitDecl());
  }
 private:
  ASTContext* ctx_;
  ClassRelationOutput* out_;
};

class ClassRelationAction : public ASTFrontendAction {
 public:
  explicit ClassRelationAction(ClassRelationOutput* out) : out_(out) {}
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance& CI,
                                                 StringRef) override {
    return std::make_unique<ClassRelationConsumer>(&CI.getASTContext(), out_);
  }
 private:
  ClassRelationOutput* out_;
};

class ClassRelationActionFactory : public tooling::FrontendActionFactory {
 public:
  explicit ClassRelationActionFactory(ClassRelationOutput* out) : out_(out) {}
  std::unique_ptr<FrontendAction> create() override {
    return std::make_unique<ClassRelationAction>(out_);
  }
 private:
  ClassRelationOutput* out_;
};

}  // namespace

void RunClassRelationAnalysis(clang::ASTContext& ctx, ClassRelationOutput* out) {
  if (!out) return;
  ClassRelationVisitor visitor(&ctx, out);
  visitor.TraverseDecl(ctx.getTranslationUnitDecl());
}

bool RunClassRelationOnTU(const TUEntry& tu, ClassRelationOutput* out) {
  if (!out) return true;
  std::string dir = tu.working_directory.empty() ? "." : NormalizePath(tu.working_directory);
  size_t pos = tu.source_file.find_last_of("/\\");
  if (dir == "." && pos != std::string::npos)
    dir = NormalizePath(tu.source_file.substr(0, pos + 1));
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

  ClassRelationActionFactory factory(out);
  int ret = tool.run(&factory);
  if (ret != 0) {
    LogError("RunClassRelationOnTU: ClangTool returned %d for %s", ret, tu.source_file.c_str());
    return false;
  }
  LogInfo("RunClassRelationOnTU: %zu classes, %zu relations for %s",
          out->classes.size(), out->relations.size(), tu.source_file.c_str());
  return true;
}

#endif  // CODEXRAY_HAVE_CLANG

}  // namespace codexray
