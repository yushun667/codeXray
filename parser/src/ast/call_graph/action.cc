#include "action.h"

#ifdef CODEXRAY_HAVE_CLANG
#include "../function_pointer/analyzer.h"
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Index/USRGeneration.h>
#include <clang/Basic/SourceManager.h>
#include <llvm/ADT/SmallString.h>
#include <unordered_map>
#include <unordered_set>
#include <string>

namespace codexray {
namespace call_graph {

namespace {

static std::string GenUSR(const clang::Decl* d) {
  llvm::SmallString<128> buf;
  if (clang::index::generateUSRForDecl(d, buf)) return "";
  return buf.str().str();
}

static std::string GetFilePath(const clang::SourceManager& sm,
                                clang::SourceLocation loc) {
  if (loc.isInvalid()) return "";
  clang::FullSourceLoc full(loc, sm);
  if (full.isInvalid() || full.isInSystemHeader()) return "";
  const char* fn = sm.getPresumedLoc(full).getFilename();
  return fn ? std::string(fn) : std::string();
}

struct LocInfo {
  std::string file;
  int line = 0, col = 0, end_line = 0, end_col = 0;
};

static LocInfo GetLocInfo(const clang::SourceManager& sm, clang::SourceRange range) {
  LocInfo li;
  clang::FullSourceLoc begin(range.getBegin(), sm);
  if (begin.isInvalid()) return li;
  li.file     = GetFilePath(sm, range.getBegin());
  li.line     = begin.getSpellingLineNumber();
  li.col      = begin.getSpellingColumnNumber();
  clang::FullSourceLoc end(range.getEnd(), sm);
  if (!end.isInvalid()) {
    li.end_line = end.getSpellingLineNumber();
    li.end_col  = end.getSpellingColumnNumber();
  }
  return li;
}

static std::string SymbolKind(const clang::FunctionDecl* fd) {
  if (clang::isa<clang::CXXDestructorDecl>(fd))  return "destructor";
  if (clang::isa<clang::CXXConstructorDecl>(fd)) return "constructor";
  if (clang::isa<clang::CXXMethodDecl>(fd))      return "method";
  return "function";
}

class CallGraphVisitor
    : public clang::RecursiveASTVisitor<CallGraphVisitor> {
public:
  CallGraphVisitor(clang::ASTContext& ctx,
                   std::vector<SymbolRow>* symbols,
                   std::vector<CallEdgeRow>* edges,
                   std::vector<std::string>* ref_files,
                   const std::unordered_map<std::string,
                       std::vector<std::string>>& fp_map)
      : ctx_(ctx), sm_(ctx.getSourceManager()),
        symbols_(symbols), edges_(edges), ref_files_(ref_files),
        fp_map_(fp_map) {}

  // TraverseFunctionDecl handles all CXX subclasses
  bool TraverseFunctionDecl(clang::FunctionDecl* fd) {
    if (!fd) return true;
    current_caller_usr_ = "";
    if (!fd->isThisDeclarationADefinition()) {
      // Record declaration-only
      RecordSymbol(fd);
      return true;
    }
    RecordSymbol(fd);
    current_caller_usr_ = GenUSR(fd);
    bool r = clang::RecursiveASTVisitor<CallGraphVisitor>::TraverseFunctionDecl(fd);
    current_caller_usr_ = "";
    return r;
  }

  bool TraverseCXXMethodDecl(clang::CXXMethodDecl* md) {
    return TraverseFunctionDecl(md);
  }
  bool TraverseCXXConstructorDecl(clang::CXXConstructorDecl* cd) {
    return TraverseFunctionDecl(cd);
  }
  bool TraverseCXXDestructorDecl(clang::CXXDestructorDecl* dd) {
    return TraverseFunctionDecl(dd);
  }

  bool VisitCallExpr(clang::CallExpr* call) {
    if (current_caller_usr_.empty()) return true;
    const clang::SourceManager& sm = sm_;

    auto* callee_decl = call->getDirectCallee();
    if (callee_decl) {
      // Direct or virtual call
      std::string callee_usr = GenUSR(callee_decl);
      if (callee_usr.empty()) return true;

      // Ensure callee symbol is recorded
      RecordSymbol(callee_decl);

      CallEdgeRow e;
      e.caller_usr  = current_caller_usr_;
      e.callee_usr  = callee_usr;
      e.edge_type   = "direct";
      auto loc = call->getBeginLoc();
      e.call_line   = clang::FullSourceLoc(loc, sm).getSpellingLineNumber();
      e.call_column = clang::FullSourceLoc(loc, sm).getSpellingColumnNumber();
      e.call_file_path = GetFilePath(sm, loc);
      RecordRefFile(e.call_file_path);
      edges_->push_back(e);
    } else {
      // 间接调用：普通函数指针 或 成员函数指针（obj.*fp / ptr->*fp）
      auto possible = function_pointer::GetPossibleCallees(call, ctx_, &fp_map_);
      for (const auto& callee_usr : possible) {
        CallEdgeRow e;
        e.caller_usr = current_caller_usr_;
        e.callee_usr = callee_usr;
        e.edge_type  = "via_function_pointer";
        auto loc = call->getBeginLoc();
        e.call_line   = clang::FullSourceLoc(loc, sm).getSpellingLineNumber();
        e.call_column = clang::FullSourceLoc(loc, sm).getSpellingColumnNumber();
        e.call_file_path = GetFilePath(sm, loc);
        RecordRefFile(e.call_file_path);
        edges_->push_back(e);
      }
    }
    return true;
  }

  // CXXMemberCallExpr 是 CallExpr 的子类，通过 VisitCallExpr 已处理大多数情况。
  // 但对于通过成员函数指针的调用（obj.*fp 或 ptr->*fp），Clang 将其表示为
  // CXXMemberCallExpr，其 getDirectCallee() 返回 nullptr。
  // VisitCallExpr 已统一处理 getDirectCallee()==nullptr 的 indirect 路径，
  // 因此这里不需要额外 VisitCXXMemberCallExpr。
  // RecursiveASTVisitor 会对 CXXMemberCallExpr 节点调用 VisitCallExpr。

  bool VisitCXXConstructExpr(clang::CXXConstructExpr* ce) {
    if (current_caller_usr_.empty()) return true;
    auto* ctor = ce->getConstructor();
    if (!ctor) return true;
    std::string callee_usr = GenUSR(ctor);
    if (callee_usr.empty()) return true;
    RecordSymbol(ctor);
    CallEdgeRow e;
    e.caller_usr = current_caller_usr_;
    e.callee_usr = callee_usr;
    e.edge_type  = "direct";
    auto loc = ce->getBeginLoc();
    e.call_line   = clang::FullSourceLoc(loc, sm_).getSpellingLineNumber();
    e.call_column = clang::FullSourceLoc(loc, sm_).getSpellingColumnNumber();
    e.call_file_path = GetFilePath(sm_, loc);
    RecordRefFile(e.call_file_path);
    edges_->push_back(e);
    return true;
  }

private:
  /// 记录函数符号。当声明和定义分离时（头文件声明 + .cpp 定义），
  /// RecursiveASTVisitor 会先访问头文件中的声明，再访问 .cpp 中的定义。
  /// 若仅用 seen_usrs_ 去重，定义的 def_* 信息会被跳过，导致 def_line=0。
  /// 修复：已见过的 USR 若本次是定义，则回填 def_* 字段到已存储的 SymbolRow。
  void RecordSymbol(const clang::FunctionDecl* fd) {
    std::string usr = GenUSR(fd);
    if (usr.empty()) return;

    // 若已记录过此 USR，检查是否需要回填定义位置
    auto it = usr_index_.find(usr);
    if (it != usr_index_.end()) {
      // 已存在：仅当本次是定义且之前未记录定义位置时，回填 def_* 字段
      if (fd->isThisDeclarationADefinition()) {
        SymbolRow& existing = (*symbols_)[it->second];
        if (existing.def_line == 0) {
          auto li = GetLocInfo(sm_, fd->getSourceRange());
          existing.def_file_path = li.file;
          existing.def_line      = li.line;
          existing.def_column    = li.col;
          existing.def_line_end  = li.end_line;
          existing.def_col_end   = li.end_col;
          RecordRefFile(li.file);
        }
      }
      return;
    }

    // 首次见到此 USR，记录完整符号信息
    size_t idx = symbols_->size();
    usr_index_[usr] = idx;

    SymbolRow r;
    r.usr  = usr;
    r.name = fd->getNameAsString();
    r.qualified_name = fd->getQualifiedNameAsString();
    r.kind = SymbolKind(fd);

    // 定义位置
    if (fd->isThisDeclarationADefinition()) {
      auto li = GetLocInfo(sm_, fd->getSourceRange());
      r.def_file_path = li.file;
      r.def_line      = li.line;
      r.def_column    = li.col;
      r.def_line_end  = li.end_line;
      r.def_col_end   = li.end_col;
      RecordRefFile(li.file);
    }
    // 声明位置（遍历其他 redecl 取第一个有效位置）
    for (auto* redecl : fd->redecls()) {
      if (redecl == fd) continue;
      auto li = GetLocInfo(sm_, redecl->getSourceRange());
      if (li.file.empty()) continue;
      r.decl_file_path = li.file;
      r.decl_line      = li.line;
      r.decl_column    = li.col;
      r.decl_line_end  = li.end_line;
      r.decl_col_end   = li.end_col;
      RecordRefFile(li.file);
      break;
    }
    symbols_->push_back(r);
  }

  void RecordRefFile(const std::string& path) {
    if (path.empty()) return;
    if (ref_files_path_set_.insert(path).second)
      ref_files_->push_back(path);
  }

  clang::ASTContext& ctx_;
  const clang::SourceManager& sm_;
  std::vector<SymbolRow>* symbols_;
  std::vector<CallEdgeRow>* edges_;
  std::vector<std::string>* ref_files_;
  const std::unordered_map<std::string, std::vector<std::string>>& fp_map_;
  std::string current_caller_usr_;
  std::unordered_map<std::string, size_t> usr_index_;  // USR → symbols_ 中的索引
  std::unordered_set<std::string> ref_files_path_set_;
};

}  // namespace

void Analyze(clang::ASTContext& ctx,
             std::vector<SymbolRow>* symbols,
             std::vector<CallEdgeRow>* call_edges,
             std::vector<std::string>* referenced_files) {
  // Phase 0: collect function pointer assignments
  std::unordered_map<std::string, std::vector<std::string>> fp_map;
  function_pointer::CollectAssignments(ctx, &fp_map);

  CallGraphVisitor v(ctx, symbols, call_edges, referenced_files, fp_map);
  v.TraverseDecl(ctx.getTranslationUnitDecl());
}

}  // namespace call_graph
}  // namespace codexray

#else
namespace codexray { namespace call_graph {} }
#endif  // CODEXRAY_HAVE_CLANG
