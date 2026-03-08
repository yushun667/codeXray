/**
 * 解析引擎 AST：单次解析合并实现
 * 一个 FrontendAction 内对同一 AST 依次执行四种分析，避免每个 TU 解析 4 次。
 */

#include "ast/combined/action.h"
#include "compile_commands/load.h"
#include "common/clang_include_detector.h"
#include "common/logger.h"
#include "common/path_util.h"
#include <string>
#include <vector>

#ifndef CODEXRAY_HAVE_CLANG

namespace codexray {

bool RunAllAnalysesOnTU(const TUEntry& tu,
                        CallGraphOutput* cg_out,
                        ClassRelationOutput* cr_out,
                        DataFlowOutput* df_out,
                        ControlFlowOutput* cf_out) {
  if (!cg_out) return false;
  if (!RunCallGraphOnTU(tu, cg_out)) return false;
  if (cr_out) RunClassRelationOnTU(tu, cr_out);
  if (df_out) RunDataFlowOnTU(tu, df_out);
  if (cf_out) RunControlFlowOnTU(tu, cf_out);
  return true;
}

}  // namespace codexray

#else  // CODEXRAY_HAVE_CLANG

#include "clang/AST/ASTContext.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <memory>

namespace codexray {

namespace {

class CombinedConsumer : public ::clang::ASTConsumer {
 public:
  CombinedConsumer(::clang::ASTContext* ctx,
                   CallGraphOutput* cg_out,
                   ClassRelationOutput* cr_out,
                   DataFlowOutput* df_out,
                   ControlFlowOutput* cf_out)
      : ctx_(ctx), cg_out_(cg_out), cr_out_(cr_out), df_out_(df_out), cf_out_(cf_out) {}
  void HandleTranslationUnit(::clang::ASTContext& ctx) override {
    if (cg_out_) RunCallGraphAnalysis(ctx, cg_out_);
    if (cr_out_) RunClassRelationAnalysis(ctx, cr_out_);
    if (df_out_) RunDataFlowAnalysis(ctx, df_out_);
    if (cf_out_) RunControlFlowAnalysis(ctx, cf_out_);
  }
 private:
  ::clang::ASTContext* ctx_;
  CallGraphOutput* cg_out_;
  ClassRelationOutput* cr_out_;
  DataFlowOutput* df_out_;
  ControlFlowOutput* cf_out_;
};

class CombinedAction : public ::clang::ASTFrontendAction {
 public:
  CombinedAction(CallGraphOutput* cg_out,
                 ClassRelationOutput* cr_out,
                 DataFlowOutput* df_out,
                 ControlFlowOutput* cf_out)
      : cg_out_(cg_out), cr_out_(cr_out), df_out_(df_out), cf_out_(cf_out) {}
  std::unique_ptr<::clang::ASTConsumer> CreateASTConsumer(::clang::CompilerInstance& CI,
                                                          ::llvm::StringRef) override {
    return std::make_unique<CombinedConsumer>(&CI.getASTContext(), cg_out_, cr_out_, df_out_, cf_out_);
  }
 private:
  CallGraphOutput* cg_out_;
  ClassRelationOutput* cr_out_;
  DataFlowOutput* df_out_;
  ControlFlowOutput* cf_out_;
};

class CombinedActionFactory : public ::clang::tooling::FrontendActionFactory {
 public:
  CombinedActionFactory(CallGraphOutput* cg_out,
                        ClassRelationOutput* cr_out,
                        DataFlowOutput* df_out,
                        ControlFlowOutput* cf_out)
      : cg_out_(cg_out), cr_out_(cr_out), df_out_(df_out), cf_out_(cf_out) {}
  std::unique_ptr<::clang::FrontendAction> create() override {
    return std::make_unique<CombinedAction>(cg_out_, cr_out_, df_out_, cf_out_);
  }
 private:
  CallGraphOutput* cg_out_;
  ClassRelationOutput* cr_out_;
  DataFlowOutput* df_out_;
  ControlFlowOutput* cf_out_;
};

static std::string WorkingDirForTU(const TUEntry& tu) {
  std::string dir = tu.working_directory;
  if (dir.empty()) {
    size_t pos = tu.source_file.find_last_of("/\\");
    dir = (pos != std::string::npos) ? tu.source_file.substr(0, pos + 1) : ".";
  }
  dir = NormalizePath(dir);
  if (dir.empty()) dir = ".";
  return dir;
}

}  // namespace

bool RunAllAnalysesOnTU(const TUEntry& tu,
                        CallGraphOutput* cg_out,
                        ClassRelationOutput* cr_out,
                        DataFlowOutput* df_out,
                        ControlFlowOutput* cf_out) {
  if (!cg_out) {
    LogError("RunAllAnalysesOnTU: cg_out is null");
    return false;
  }
  std::string dir = WorkingDirForTU(tu);
  auto db = std::make_unique<::clang::tooling::FixedCompilationDatabase>(
      dir, ::llvm::ArrayRef<std::string>(tu.compile_args));
  ::clang::tooling::ClangTool tool(*db, ::llvm::ArrayRef<std::string>(tu.source_file));

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
        ::clang::tooling::getInsertArgumentAdjuster(extra, ::clang::tooling::ArgumentInsertPosition::BEGIN));

  CombinedActionFactory factory(cg_out, cr_out, df_out, cf_out);
  int ret = tool.run(&factory);
  if (ret != 0) {
    LogError("RunAllAnalysesOnTU: ClangTool run returned %d for %s", ret, tu.source_file.c_str());
    return false;
  }
  LogInfo("RunAllAnalysesOnTU: %s done (cg %zu symbols, %zu edges)",
          tu.source_file.c_str(), cg_out->symbols.size(), cg_out->edges.size());
  return true;
}

}  // namespace codexray

#endif  // CODEXRAY_HAVE_CLANG
