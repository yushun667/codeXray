/**
 * 解析引擎 AST：函数指针可能目标分析
 * 参考：doc/01-解析引擎 解析引擎详细功能与架构设计 §4.6
 * 当 CODEXRAY_HAVE_CLANG 时基于 CallExpr/ASTContext 分析；否则占位返回空。
 */

#ifndef CODEXRAY_PARSER_AST_FUNCTION_POINTER_ANALYZER_H_
#define CODEXRAY_PARSER_AST_FUNCTION_POINTER_ANALYZER_H_

#include <string>
#include <vector>

#ifdef CODEXRAY_HAVE_CLANG
namespace clang {
class CallExpr;
class ASTContext;
}  // namespace clang
#endif

namespace codexray {

/**
 * 占位（无 Clang）：仅接受 caller_usr，返回空。
 */
std::vector<std::string> GetPossibleCallees(const std::string& caller_usr);

#ifdef CODEXRAY_HAVE_CLANG
/**
 * 给定调用点为函数指针时，返回所有可能 callee 的 USR。
 * 有 Clang 时由 call_graph 在 VisitCallExpr 中调用。
 */
std::vector<std::string> GetPossibleCallees(clang::CallExpr* call,
                                            clang::ASTContext& ctx,
                                            const std::string& caller_usr);
#endif

}  // namespace codexray

#endif  // CODEXRAY_PARSER_AST_FUNCTION_POINTER_ANALYZER_H_
