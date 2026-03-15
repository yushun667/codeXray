/**
 * compile_commands.json 加载与 TU 列表生成。
 * 设计 §3.3。
 */
#pragma once
#include <string>
#include <vector>

namespace codexray {

struct TUEntry {
  std::string source_file;  // 绝对路径
  std::string directory;    // 工作目录
  std::vector<std::string> arguments;  // 完整编译命令行（argv 风格）
};

struct CompileCommandsResult {
  std::vector<TUEntry> priority;    // priority_dirs 下的 TU
  std::vector<TUEntry> remainder;   // 其余 TU
  std::string error;
};

/**
 * 加载 compile_commands.json
 * @param json_path  JSON 文件路径
 * @param project_root  工程根目录（用于规范化相对路径）
 * @param priority_dirs  优先目录列表（相对 project_root），为空则所有 TU 归 priority
 */
CompileCommandsResult LoadCompileCommands(const std::string& json_path,
                                          const std::string& project_root,
                                          const std::vector<std::string>& priority_dirs);

}  // namespace codexray
