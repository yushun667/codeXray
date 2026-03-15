/**
 * 函数指针分析：给定 CallExpr，解析所有可能的 callee USR。
 * 设计 §3.8 / §5.3。
 */
#pragma once

#ifdef CODEXRAY_HAVE_CLANG
#include <clang/AST/ASTContext.h>
#include <clang/AST/Expr.h>
#endif

#include <string>
#include <unordered_map>
#include <vector>

namespace codexray {
namespace function_pointer {

#ifdef CODEXRAY_HAVE_CLANG
/**
 * 分析阶段 1：从 TU 的所有赋值/初始化中，收集函数指针变量到函数 USR 的映射。
 * 在单次 AST 遍历中由 call_graph 分析调用，传入 ASTContext。
 *
 * @param ctx     当前 TU 的 ASTContext
 * @param fu_map  输出：VarDecl USR → 可能指向的函数 USR 列表
 */
void CollectAssignments(clang::ASTContext& ctx,
                        std::unordered_map<std::string, std::vector<std::string>>* fu_map);

/**
 * 给定一个间接调用的 CallExpr（被调用者为函数指针），返回所有可能的 callee USR。
 *
 * 策略（分阶段）：
 *  1. 从 fu_map（同 TU 赋值）中查找；
 *  2. 若无结果，使用类型兼容的保守策略（枚举 ASTContext 中同类型函数）。
 *
 * @param call    CallExpr（被调用者不是 FunctionDecl）
 * @param ctx     ASTContext
 * @param fu_map  阶段 1 收集到的映射（可为 nullptr）
 * @return 可能 callee 的 USR 列表（已去重），空表示无法解析
 */
std::vector<std::string> GetPossibleCallees(
    clang::CallExpr* call,
    clang::ASTContext& ctx,
    const std::unordered_map<std::string, std::vector<std::string>>* fu_map);
#endif  // CODEXRAY_HAVE_CLANG

}  // namespace function_pointer
}  // namespace codexray
