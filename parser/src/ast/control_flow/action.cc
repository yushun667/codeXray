/**
 * 解析引擎 AST：控制流 CFG
 * 无 Clang 时占位；有 Clang 时对每个函数构建 CFG，输出基本块节点与边。
 * 参考：doc/01-解析引擎 解析引擎详细功能与架构设计 §4.9
 */

#include "ast/control_flow/action.h"
#include "compile_commands/load.h"
#include "common/logger.h"
#include "common/path_util.h"
#include <string>
#include <vector>

#ifdef CODEXRAY_HAVE_CLANG
#include "clang/Analysis/CFG.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
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

bool RunControlFlowOnTU(const TUEntry& /* tu */, ControlFlowOutput* /* out */) {
  LogInfo("RunControlFlowOnTU: stub");
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

std::string GetUSR(const Decl* D, ASTContext& ctx) {
  if (!D) return {};
  llvm::SmallString<256> buf;
  if (index::generateUSRForDecl(D, buf)) return {};
  return std::string(buf.str());
}

void BuildCFGForFunction(FunctionDecl* FD, ASTContext& ctx, SourceManager& SM,
                         ControlFlowOutput* out) {
  if (!FD->hasBody()) return;
  std::string func_usr = GetUSR(FD, ctx);
  if (func_usr.empty()) return;

  CFG::BuildOptions opts;
  std::unique_ptr<CFG> cfg_ptr = CFG::buildCFG(FD, FD->getBody(), &ctx, opts);
  if (!cfg_ptr) return;
  CFG* cfg = cfg_ptr.get();

  std::vector<int> block_index_by_id;
  block_index_by_id.resize(cfg->getNumBlockIDs(), -1);
  int next_index = static_cast<int>(out->nodes.size());

  for (CFG::const_iterator it = cfg->begin(); it != cfg->end(); ++it) {
    const CFGBlock* block = *it;
    if (!block) continue;
    unsigned block_id = block->getBlockID();
    CfgNodeRecord node;
    node.symbol_usr = func_usr;
    node.block_id = "B" + std::to_string(block_id);
    node.kind = "block";
    if (block->getTerminatorStmt()) {
      SourceLocation loc = block->getTerminatorStmt()->getBeginLoc();
      node.line = GetLine(SM, loc);
      node.column = GetColumn(SM, loc);
    }
    out->nodes.push_back(node);
    if (block_id < block_index_by_id.size())
      block_index_by_id[block_id] = next_index;
    ++next_index;
  }

  for (CFG::const_iterator it = cfg->begin(); it != cfg->end(); ++it) {
    const CFGBlock* block = *it;
    if (!block) continue;
    unsigned from_id = block->getBlockID();
    int from_idx = (from_id < block_index_by_id.size()) ? block_index_by_id[from_id] : -1;
    if (from_idx < 0) continue;
    for (CFGBlock::const_succ_iterator si = block->succ_begin(), se = block->succ_end(); si != se; ++si) {
      const CFGBlock* succ = *si;
      if (!succ) continue;
      unsigned to_id = succ->getBlockID();
      int to_idx = (to_id < block_index_by_id.size()) ? block_index_by_id[to_id] : -1;
      if (to_idx < 0) continue;
      CfgEdgeRecord edge;
      edge.from_node_index = from_idx;
      edge.to_node_index = to_idx;
      edge.edge_type = "control";
      out->edges.push_back(edge);
    }
  }
}

class ControlFlowVisitor : public RecursiveASTVisitor<ControlFlowVisitor> {
 public:
  ControlFlowVisitor(ASTContext* ctx, ControlFlowOutput* out, int64_t /* file_id */)
      : ctx_(ctx), out_(out), sm_(&ctx->getSourceManager()) {}

  bool VisitFunctionDecl(FunctionDecl* D) {
    if (!D || !out_) return true;
    if (!D->isThisDeclarationADefinition() || D->isImplicit()) return true;
    BuildCFGForFunction(D, *ctx_, *sm_, out_);
    return true;
  }

 private:
  ASTContext* ctx_;
  ControlFlowOutput* out_;
  SourceManager* sm_;
};

class ControlFlowConsumer : public ASTConsumer {
 public:
  ControlFlowConsumer(ASTContext* ctx, ControlFlowOutput* out) : ctx_(ctx), out_(out) {}
  void HandleTranslationUnit(ASTContext& ctx) override {
    ControlFlowVisitor visitor(ctx_, out_, 0);
    visitor.TraverseDecl(ctx.getTranslationUnitDecl());
  }
 private:
  ASTContext* ctx_;
  ControlFlowOutput* out_;
};

class ControlFlowAction : public ASTFrontendAction {
 public:
  explicit ControlFlowAction(ControlFlowOutput* out) : out_(out) {}
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance& CI, StringRef) override {
    return std::make_unique<ControlFlowConsumer>(&CI.getASTContext(), out_);
  }
 private:
  ControlFlowOutput* out_;
};

class ControlFlowActionFactory : public tooling::FrontendActionFactory {
 public:
  explicit ControlFlowActionFactory(ControlFlowOutput* out) : out_(out) {}
  std::unique_ptr<FrontendAction> create() override {
    return std::make_unique<ControlFlowAction>(out_);
  }
 private:
  ControlFlowOutput* out_;
};

}  // namespace

bool RunControlFlowOnTU(const TUEntry& tu, ControlFlowOutput* out) {
  if (!out) return true;
  std::string dir = tu.working_directory.empty() ? "." : NormalizePath(tu.working_directory);
  size_t pos = tu.source_file.find_last_of("/\\");
  if (dir == "." && pos != std::string::npos)
    dir = NormalizePath(tu.source_file.substr(0, pos + 1));
  if (dir.empty()) dir = ".";

  auto db = std::make_unique<tooling::FixedCompilationDatabase>(
      dir, llvm::ArrayRef<std::string>(tu.compile_args));
  tooling::ClangTool tool(*db, llvm::ArrayRef<std::string>(tu.source_file));
  ControlFlowActionFactory factory(out);
  int ret = tool.run(&factory);
  if (ret != 0) {
    LogError("RunControlFlowOnTU: ClangTool returned %d for %s", ret, tu.source_file.c_str());
    return false;
  }
  LogInfo("RunControlFlowOnTU: %zu nodes, %zu edges for %s",
          out->nodes.size(), out->edges.size(), tu.source_file.c_str());
  return true;
}

#endif  // CODEXRAY_HAVE_CLANG

}  // namespace codexray
