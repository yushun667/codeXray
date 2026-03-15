#include "action.h"

#ifdef CODEXRAY_HAVE_CLANG
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Index/USRGeneration.h>
#include <clang/Basic/SourceManager.h>
#include <llvm/ADT/SmallString.h>
#include <unordered_map>
#include <unordered_set>

namespace codexray {
namespace data_flow {

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

// First pass: collect global var USRs
class GlobalVarCollector
    : public clang::RecursiveASTVisitor<GlobalVarCollector> {
public:
  GlobalVarCollector(clang::ASTContext& ctx,
                     std::vector<GlobalVarRow>* gvars,
                     std::vector<std::string>* ref_files,
                     std::unordered_map<std::string, std::string>& usr_name)
      : sm_(ctx.getSourceManager()), gvars_(gvars),
        ref_files_(ref_files), usr_name_(usr_name) {}

  bool VisitVarDecl(clang::VarDecl* vd) {
    if (!vd) return true;
    // File-scope global variable
    if (!vd->isFileVarDecl()) return true;
    if (vd->isStaticLocal()) return true;
    // Skip extern declarations without definition in this TU where possible
    if (!vd->isThisDeclarationADefinition() && !vd->hasExternalStorage()) return true;

    std::string usr = GenUSR(vd);
    if (usr.empty() || seen_.count(usr)) return true;
    seen_.insert(usr);

    GlobalVarRow r;
    r.usr            = usr;
    r.name           = vd->getNameAsString();
    r.qualified_name = vd->getQualifiedNameAsString();
    clang::FullSourceLoc begin(vd->getBeginLoc(), sm_);
    clang::FullSourceLoc end(vd->getEndLoc(), sm_);
    r.def_line     = begin.isValid() ? begin.getSpellingLineNumber() : 0;
    r.def_column   = begin.isValid() ? begin.getSpellingColumnNumber() : 0;
    r.def_line_end = end.isValid() ? end.getSpellingLineNumber() : 0;
    r.def_col_end  = end.isValid() ? end.getSpellingColumnNumber() : 0;
    std::string fp = GetFilePath(sm_, vd->getBeginLoc());
    r.def_file_path = fp;
    if (!fp.empty() && ref_files_set_.insert(fp).second)
      ref_files_->push_back(fp);
    gvars_->push_back(r);
    usr_name_[usr] = r.name;
    return true;
  }

private:
  const clang::SourceManager& sm_;
  std::vector<GlobalVarRow>* gvars_;
  std::vector<std::string>* ref_files_;
  std::unordered_map<std::string, std::string>& usr_name_;
  std::unordered_set<std::string> seen_;
  std::unordered_set<std::string> ref_files_set_;
};

// Second pass: track read/write access to known global vars
class DataFlowVisitor
    : public clang::RecursiveASTVisitor<DataFlowVisitor> {
public:
  DataFlowVisitor(const clang::SourceManager& sm,
                  const std::unordered_map<std::string, std::string>& global_usrs,
                  std::vector<DataFlowEdgeRow>* edges,
                  std::vector<std::string>* ref_files)
      : sm_(sm), global_usrs_(global_usrs),
        edges_(edges), ref_files_(ref_files) {}

  bool TraverseFunctionDecl(clang::FunctionDecl* fd) {
    if (!fd || !fd->isThisDeclarationADefinition()) return true;
    current_fn_usr_ = GenUSR(fd);
    bool r = clang::RecursiveASTVisitor<DataFlowVisitor>::TraverseFunctionDecl(fd);
    current_fn_usr_ = "";
    return r;
  }
  bool TraverseCXXMethodDecl(clang::CXXMethodDecl* md) {
    return TraverseFunctionDecl(md);
  }

  // Handle writes: assignment LHS
  bool VisitBinaryOperator(clang::BinaryOperator* bo) {
    if (!bo->isAssignmentOp()) return true;
    RecordAccess(bo->getLHS()->IgnoreParenImpCasts(), "write");
    return true;
  }

  // Handle writes: ++/-- operator
  bool VisitUnaryOperator(clang::UnaryOperator* uo) {
    auto op = uo->getOpcode();
    if (op == clang::UO_PreInc  || op == clang::UO_PostInc ||
        op == clang::UO_PreDec  || op == clang::UO_PostDec) {
      RecordAccess(uo->getSubExpr()->IgnoreParenImpCasts(), "write");
    }
    return true;
  }

  // All DeclRefExprs to global vars are reads (unless we already recorded a write above)
  bool VisitDeclRefExpr(clang::DeclRefExpr* dr) {
    if (current_fn_usr_.empty()) return true;
    auto* vd = clang::dyn_cast<clang::VarDecl>(dr->getDecl());
    if (!vd || !vd->isFileVarDecl()) return true;
    std::string var_usr = GenUSR(vd);
    if (!global_usrs_.count(var_usr)) return true;
    if (written_ptrs_.count(dr)) return true;  // already recorded as write
    EmitEdge(var_usr, dr->getBeginLoc(), "read");
    return true;
  }

private:
  void RecordAccess(clang::Expr* expr, const std::string& access_type) {
    if (current_fn_usr_.empty()) return;
    auto* dr = clang::dyn_cast<clang::DeclRefExpr>(expr);
    if (!dr) return;
    auto* vd = clang::dyn_cast<clang::VarDecl>(dr->getDecl());
    if (!vd || !vd->isFileVarDecl()) return;
    std::string var_usr = GenUSR(vd);
    if (!global_usrs_.count(var_usr)) return;
    written_ptrs_.insert(dr);
    EmitEdge(var_usr, dr->getBeginLoc(), access_type);
  }

  void EmitEdge(const std::string& var_usr, clang::SourceLocation loc,
                const std::string& access_type) {
    DataFlowEdgeRow e;
    e.var_usr      = var_usr;
    e.accessor_usr = current_fn_usr_;
    e.access_type  = access_type;
    clang::FullSourceLoc full(loc, sm_);
    e.access_line   = full.isValid() ? full.getSpellingLineNumber() : 0;
    e.access_column = full.isValid() ? full.getSpellingColumnNumber() : 0;
    std::string fp = GetFilePath(sm_, loc);
    e.access_file_path = fp;
    if (!fp.empty() && ref_files_set_.insert(fp).second)
      ref_files_->push_back(fp);
    edges_->push_back(e);
  }

  const clang::SourceManager& sm_;
  const std::unordered_map<std::string, std::string>& global_usrs_;
  std::vector<DataFlowEdgeRow>* edges_;
  std::vector<std::string>* ref_files_;
  std::string current_fn_usr_;
  std::unordered_set<std::string> ref_files_set_;
  std::unordered_set<const clang::DeclRefExpr*> written_ptrs_;
};

}  // namespace

void Analyze(clang::ASTContext& ctx,
             std::vector<GlobalVarRow>* global_vars,
             std::vector<DataFlowEdgeRow>* edges,
             std::vector<std::string>* referenced_files) {
  std::unordered_map<std::string, std::string> global_usr_map;
  GlobalVarCollector collector(ctx, global_vars, referenced_files, global_usr_map);
  collector.TraverseDecl(ctx.getTranslationUnitDecl());

  DataFlowVisitor visitor(ctx.getSourceManager(), global_usr_map, edges, referenced_files);
  visitor.TraverseDecl(ctx.getTranslationUnitDecl());
}

}  // namespace data_flow
}  // namespace codexray

#else
namespace codexray { namespace data_flow {} }
#endif
