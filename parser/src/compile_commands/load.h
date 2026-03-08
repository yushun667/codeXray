/**
 * 解析引擎 compile_commands 加载
 * 参考：doc/01-解析引擎 解析引擎详细功能与架构设计 §4.3
 * 格式：https://clang.llvm.org/docs/JSONCompilationDatabase.html
 */

#ifndef CODEXRAY_PARSER_COMPILE_COMMANDS_LOAD_H_
#define CODEXRAY_PARSER_COMPILE_COMMANDS_LOAD_H_

#include <string>
#include <vector>

namespace codexray {

struct TUEntry {
  std::string source_file;   // 绝对路径
  std::vector<std::string> compile_args;
};

/**
 * 加载 compile_commands.json，将路径展开为基于 project_root 的绝对路径。
 * path_to_cc 可为空，表示使用 project_root/compile_commands.json。
 * 失败返回空 vector（并写日志）。
 */
std::vector<TUEntry> LoadCompileCommands(const std::string& project_root,
                                        const std::string& path_to_cc);

/**
 * 按 priority_dirs（相对 project_root 的路径前缀）将 all 划分为优先 TU 与剩余 TU。
 * 若 source_file 的工程相对路径以任一 priority_dir 开头，则归入 priority，否则归入 rest。
 */
void SplitByPriorityDirs(const std::vector<TUEntry>& all,
                        const std::string& project_root,
                        const std::vector<std::string>& priority_dirs,
                        std::vector<TUEntry>* priority,
                        std::vector<TUEntry>* rest);

}  // namespace codexray

#endif  // CODEXRAY_PARSER_COMPILE_COMMANDS_LOAD_H_
