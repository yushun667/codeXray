#include "action.h"

#ifdef CODEXRAY_HAVE_CLANG
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Index/USRGeneration.h>
#include <clang/Basic/SourceManager.h>
#include <llvm/ADT/SmallString.h>
#include <unordered_set>

namespace codexray {
namespace class_relation {

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

class ClassVisitor : public clang::RecursiveASTVisitor<ClassVisitor> {
public:
  ClassVisitor(clang::ASTContext& ctx,
               std::vector<ClassRow>* classes,
               std::vector<ClassRelationRow>* relations,
               std::vector<ClassMemberRow>* members,
               std::vector<std::string>* ref_files)
      : ctx_(ctx), sm_(ctx.getSourceManager()),
        classes_(classes), relations_(relations),
        members_(members), ref_files_(ref_files) {}

  bool VisitCXXRecordDecl(clang::CXXRecordDecl* rd) {
    if (!rd || !rd->isThisDeclarationADefinition()) return true;
    if (rd->isInjectedClassName()) return true;

    std::string usr = GenUSR(rd);
    if (usr.empty() || seen_.count(usr)) return true;
    seen_.insert(usr);

    // Record class
    ClassRow cr;
    cr.usr            = usr;
    cr.name           = rd->getNameAsString();
    cr.qualified_name = rd->getQualifiedNameAsString();
    clang::FullSourceLoc begin(rd->getBeginLoc(), sm_);
    clang::FullSourceLoc end(rd->getEndLoc(), sm_);
    cr.def_line     = begin.isValid() ? begin.getSpellingLineNumber() : 0;
    cr.def_column   = begin.isValid() ? begin.getSpellingColumnNumber() : 0;
    cr.def_line_end = end.isValid() ? end.getSpellingLineNumber() : 0;
    cr.def_col_end  = end.isValid() ? end.getSpellingColumnNumber() : 0;
    std::string fp = GetFilePath(sm_, rd->getBeginLoc());
    cr.def_file_path = fp;
    RegisterFile(fp);
    classes_->push_back(cr);

    // Inheritance relations
    for (const auto& base : rd->bases()) {
      auto* base_rd = base.getType()->getAsCXXRecordDecl();
      if (!base_rd) continue;
      std::string base_usr = GenUSR(base_rd);
      if (base_usr.empty()) continue;
      std::string rel_type = base.isVirtual() ? "virtual_inheritance" : "inheritance";
      ClassRelationRow rr;
      rr.parent_usr    = base_usr;
      rr.child_usr     = usr;
      rr.relation_type = rel_type;
      relations_->push_back(rr);
    }

    // Members (fields + methods declared in this class)
    for (auto* f : rd->fields()) {
      ClassMemberRow mr;
      mr.class_usr        = usr;
      mr.member_name      = f->getNameAsString();
      mr.member_type_str  = f->getType().getAsString();
      mr.member_usr       = GenUSR(f);
      members_->push_back(mr);

      // Composition/dependency: if field type is another user-defined class
      auto* field_rd = f->getType()->getAsCXXRecordDecl();
      if (field_rd && !field_rd->isImplicit()) {
        std::string ft_usr = GenUSR(field_rd);
        if (!ft_usr.empty() && ft_usr != usr) {
          ClassRelationRow dep;
          dep.parent_usr    = ft_usr;
          dep.child_usr     = usr;
          dep.relation_type = "composition";
          relations_->push_back(dep);
        }
      }
    }
    return true;
  }

private:
  void RegisterFile(const std::string& path) {
    if (path.empty()) return;
    if (ref_files_set_.insert(path).second)
      ref_files_->push_back(path);
  }

  clang::ASTContext& ctx_;
  const clang::SourceManager& sm_;
  std::vector<ClassRow>* classes_;
  std::vector<ClassRelationRow>* relations_;
  std::vector<ClassMemberRow>* members_;
  std::vector<std::string>* ref_files_;
  std::unordered_set<std::string> seen_;
  std::unordered_set<std::string> ref_files_set_;
};

}  // namespace

void Analyze(clang::ASTContext& ctx,
             std::vector<ClassRow>* classes,
             std::vector<ClassRelationRow>* relations,
             std::vector<ClassMemberRow>* members,
             std::vector<std::string>* referenced_files) {
  ClassVisitor v(ctx, classes, relations, members, referenced_files);
  v.TraverseDecl(ctx.getTranslationUnitDecl());
}

}  // namespace class_relation
}  // namespace codexray

#else
namespace codexray { namespace class_relation {} }
#endif
