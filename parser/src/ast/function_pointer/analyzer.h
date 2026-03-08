/**
 * 解析引擎 AST：函数指针可能目标分析
 * 参考：doc/01-解析引擎 解析引擎详细功能与架构设计 §4.6
 * 当 CODEXRAY_HAVE_CLANG 时基于 CallExpr/ASTContext 分析；否则占位返回空。
 */

#ifndef CODEXRAY_PARSER_AST_FUNCTION_POINTER_ANALYZER_H_
#define CODEXRAY_PARSER_AST_FUNCTION_POINTER_ANALYZER_H_

#include <string>
#include <vector>

namespace codexray {

/**
 * 给定调用点为函数指针时，返回所有可能 callee 的 USR。
 * 占位签名（无 Clang）：仅接受 caller_usr，返回空。
 * 完整签名（有 Clang）：GetPossibleCallees(CallExpr* call, ASTContext& ctx, const std::string& caller_usr)。
 */
std::vector<std::string> GetPossibleCallees(const std::string& caller_usr);

}  // namespace codexray

#endif  // CODEXRAY_PARSER_AST_FUNCTION_POINTER_ANALYZER_H_
