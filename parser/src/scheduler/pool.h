/**
 * 解析引擎调度：线程池、TU 队列、进度回调
 * 参考：doc/01-解析引擎 解析引擎详细功能与架构设计 §4.4、接口约定 2.1 --parallel
 */

#ifndef CODEXRAY_PARSER_SCHEDULER_POOL_H_
#define CODEXRAY_PARSER_SCHEDULER_POOL_H_

#include <cstddef>
#include <functional>
#include <vector>

namespace codexray {

struct TUEntry;

using ProgressCallback = std::function<void(size_t done, size_t total)>;
/** 处理单个 TU，返回是否成功；由 driver 注入（如运行 Clang FrontendAction） */
using TUProcessor = std::function<bool(const TUEntry&)>;

/**
 * 使用线程池并行处理 TU 列表，阻塞直到全部完成。
 * parallel 为线程数；processor 为单 TU 处理函数（可空则仅计数）；每完成一个 TU 调用 on_progress(done, total)。
 */
void RunTUPool(const std::vector<TUEntry>& tus,
               unsigned parallel,
               TUProcessor processor,
               ProgressCallback on_progress);

}  // namespace codexray

#endif  // CODEXRAY_PARSER_SCHEDULER_POOL_H_
