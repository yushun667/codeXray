/**
 * 线程池调度器：并行执行 TU 分析任务。
 * 设计 §3.4 / §5.6。
 * 默认并行度 = max(1, 系统 CPU 核心数 - 2)（接口约定 §2.1 / 模块功能说明 1.4）。
 *
 * 隔离机制：每个 TU 通过 fork+exec 在独立子进程运行（codexray-parser parse-tu），
 * 子进程 abort/crash 不影响父进程，超时后被 SIGKILL。
 */
#pragma once
#include "compile_commands/load.h"
#include "common/analysis_output.h"
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace codexray {

using ProgressCallback = std::function<void(size_t done, size_t total,
                                             const std::string& current_file)>;

struct SchedulerConfig {
  unsigned parallel = 0;  // 0 = auto (max(1, cores-2))
};

/**
 * 每完成一个 TU 的回调：(tu, output, ok)。
 * 若 ok==false，output 为空；调用方可在回调内立即写库并释放内存，
 * 避免大型项目（如 llvm-project）将所有结果累积在内存中导致 OOM。
 * 回调在调度线程内串行（持有内部锁），不得执行耗时操作。
 */
using ResultCallback = std::function<void(const TUEntry&, CombinedOutput&, bool ok)>;

/**
 * 并行执行 tu_list 中的每个 TU，使用 fork+exec 隔离。
 * 每完成一个 TU：
 *   - 若 on_result 非空，则调用 on_result(tu, output, ok)
 *   - 若 progress 非空，则调用 progress(done, total, file)
 * outputs 字段保留兼容性，当 on_result 非空时不填充（节省内存）。
 * 失败的 TU 记录警告并跳过，不中止其他 TU。
 */
struct RunResult {
  std::vector<CombinedOutput> outputs;  // 仅 on_result==nullptr 时填充
  int failed_count = 0;
};

RunResult RunScheduled(
    const std::vector<TUEntry>& tu_list,
    const SchedulerConfig& cfg,
    std::function<bool(const TUEntry&, CombinedOutput&)> run_one,
    ProgressCallback progress = nullptr,
    ResultCallback on_result = nullptr);

// 返回默认并行度：max(1, cores-2)
unsigned DefaultParallelism();

// CombinedOutput 序列化/反序列化（供 parse-tu 子命令使用）
nlohmann::json SerializeCombinedOutput(const CombinedOutput& o);
void DeserializeCombinedOutput(const nlohmann::json& j, CombinedOutput& o);

// TUEntry 序列化/反序列化（供 parse-tu 子命令使用）
nlohmann::json SerializeTUEntry(const TUEntry& tu);
TUEntry DeserializeTUEntry(const nlohmann::json& j);

}  // namespace codexray
