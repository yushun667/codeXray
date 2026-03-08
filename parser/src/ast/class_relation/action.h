/**
 * 解析引擎 AST：类关系 FrontendAction
 * 参考：doc/01-解析引擎 解析引擎详细功能与架构设计 §4.7
 */

#ifndef CODEXRAY_PARSER_AST_CLASS_RELATION_ACTION_H_
#define CODEXRAY_PARSER_AST_CLASS_RELATION_ACTION_H_

#ifdef CODEXRAY_HAVE_CLANG
namespace clang { class ASTContext; }
#endif

#include <cstdint>
#include <string>
#include <vector>

namespace codexray {

struct TUEntry;

struct ClassRecord {
  std::string usr;
  int64_t file_id = 0;
  std::string name;
  std::string qualified_name;
  int64_t def_file_id = 0;
  int def_line = 0, def_column = 0, def_line_end = 0, def_column_end = 0;
};

struct ClassRelationRecord {
  std::string parent_usr;
  std::string child_usr;
  std::string relation_type;  // inheritance | composition | dependency
};

struct ClassRelationOutput {
  std::vector<ClassRecord> classes;
  std::vector<ClassRelationRecord> relations;
};

bool RunClassRelationOnTU(const TUEntry& tu, ClassRelationOutput* out);

#ifdef CODEXRAY_HAVE_CLANG
void RunClassRelationAnalysis(clang::ASTContext& ctx, ClassRelationOutput* out);
#endif

}  // namespace codexray

#endif  // CODEXRAY_PARSER_AST_CLASS_RELATION_ACTION_H_
