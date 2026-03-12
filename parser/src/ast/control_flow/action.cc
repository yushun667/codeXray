#include "action.h"

#ifdef CODEXRAY_HAVE_CLANG
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Analysis/CFG.h>
#include <clang/Index/USRGeneration.h>
#include <clang/Basic/SourceManager.h>
#include <llvm/ADT/SmallString.h>
#include <unordered_set>
#include <string>

namespace codexray {
namespace control_flow {

namespace {

static std::string GenUSR(const clang::Decl* d) {
  llvm::SmallString<128> buf;
  if (clang::index::generateUSRForDecl(d, buf)) return "";
  return buf.str().str();
}

static std::string GetFilePath(const clang::SourceManager& sm, clang::SourceLocation loc) {
  if (loc.isInvalid()) return "";
  clang::FullSourceLoc full(loc, sm);
  if (full.isInvalid() || full.isInSystemHeader()) return "";
  const char* fn = sm.getPresumedLoc(full).getFilename();
  return fn ? std::string(fn) : std::string();
}

static void BuildCFGForFunction(
    clang::FunctionDecl* fd,
    clang::ASTContext& ctx,
    std::vector<CfgNodeRow>* cfg_nodes,
    std::vector<CfgEdgeRow>* cfg_edges,
    std::unordered_set<std::string>& ref_files_set,
    std::vector<std::string>* ref_files) {

  if (!fd->hasBody()) return;
  std::string fn_usr = GenUSR(fd);
  if (fn_usr.empty()) return;

  clang::CFG::BuildOptions opts;
  opts.AddImplicitDtors = false;
  opts.AddEHEdges       = false;  // EH edges 大幅增加复杂度，关闭以提高稳定性
  auto cfg = clang::CFG::buildCFG(fd, fd->getBody(), &ctx, opts);
  if (!cfg) return;

  // 跳过超大 CFG（通常是模板展开或极复杂函数），避免内存/时间爆炸
  if (cfg->getNumBlockIDs() > 10000) return;

  const clang::SourceManager& sm = ctx.getSourceManager();

  for (const clang::CFGBlock* block : *cfg) {
    CfgNodeRow node;
    node.function_usr = fn_usr;
    node.block_id     = block->getBlockID();

    // Extract first statement's source location
    for (const auto& elem : *block) {
      if (auto stmt = elem.getAs<clang::CFGStmt>()) {
        auto loc = stmt->getStmt()->getBeginLoc();
        clang::FullSourceLoc full(loc, sm);
        if (full.isValid() && !full.isInSystemHeader()) {
          node.begin_line = full.getSpellingLineNumber();
          node.begin_col  = full.getSpellingColumnNumber();
          std::string fp = GetFilePath(sm, loc);
          if (!fp.empty()) {
            node.file_path = fp;
            if (ref_files_set.insert(fp).second)
              ref_files->push_back(fp);
          }
          break;
        }
      }
    }
    // Build a short label
    if (block == &cfg->getEntry()) node.label = "entry";
    else if (block == &cfg->getExit()) node.label = "exit";
    else if (!block->empty()) {
      if (auto term = block->getTerminatorStmt()) {
        node.label = term->getStmtClassName();
      }
    }
    cfg_nodes->push_back(node);

    // Edges
    size_t succ_idx = 0;
    for (const clang::CFGBlock::AdjacentBlock& succ : block->succs()) {
      if (!succ.getReachableBlock()) { ++succ_idx; continue; }
      CfgEdgeRow edge;
      edge.function_usr = fn_usr;
      edge.from_block   = block->getBlockID();
      edge.to_block     = succ.getReachableBlock()->getBlockID();
      // Determine edge type: if terminator exists, first succ = true, second = false
      if (block->getTerminatorStmt()) {
        edge.edge_type = (succ_idx == 0) ? "true" : "false";
      } else {
        edge.edge_type = "unconditional";
      }
      cfg_edges->push_back(edge);
      ++succ_idx;
    }
  }
}

class CFGVisitor : public clang::RecursiveASTVisitor<CFGVisitor> {
public:
  CFGVisitor(clang::ASTContext& ctx,
             std::vector<CfgNodeRow>* nodes,
             std::vector<CfgEdgeRow>* edges,
             std::vector<std::string>* ref_files)
      : ctx_(ctx), nodes_(nodes), edges_(edges), ref_files_(ref_files) {}

  bool VisitFunctionDecl(clang::FunctionDecl* fd) {
    if (!fd || !fd->isThisDeclarationADefinition()) return true;
    if (!fd->hasBody()) return true;
    try {
      BuildCFGForFunction(fd, ctx_, nodes_, edges_, ref_files_set_, ref_files_);
    } catch (const std::exception& e) {
      // Non-fatal: skip this function's CFG
    }
    return true;
  }

private:
  clang::ASTContext& ctx_;
  std::vector<CfgNodeRow>* nodes_;
  std::vector<CfgEdgeRow>* edges_;
  std::vector<std::string>* ref_files_;
  std::unordered_set<std::string> ref_files_set_;
};

}  // namespace

void Analyze(clang::ASTContext& ctx,
             std::vector<CfgNodeRow>* cfg_nodes,
             std::vector<CfgEdgeRow>* cfg_edges,
             std::vector<std::string>* referenced_files) {
  CFGVisitor v(ctx, cfg_nodes, cfg_edges, referenced_files);
  v.TraverseDecl(ctx.getTranslationUnitDecl());
}

}  // namespace control_flow
}  // namespace codexray

#else
namespace codexray { namespace control_flow {} }
#endif
