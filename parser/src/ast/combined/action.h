/**
 * 单次 AST 遍历合并所有分析。
 * 设计 §3.6 / §5.1。
 * 使用类型别名避免部分环境（如 Linux+LLVM 头）中 Combined→CallGraph 等宏替换。
 */
#pragma once
#include "common/analysis_output.h"
#include "compile_commands/load.h"
#include <string>

namespace codexray {
namespace combined {

using _CombinedOut = codexray::CombinedOutput;

/**
 * 对一个 TU 执行全部分析（调用链+类关系+数据流+控制流），
 * 结果填入 out。返回 false 表示 Clang 解析/工具初始化失败。
 */
bool RunAllAnalysesOnTU(const TUEntry& tu, _CombinedOut& out);

}  // namespace combined
}  // namespace codexray
