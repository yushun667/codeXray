/**
 * 解析引擎调度：线程池实现
 */

#include "scheduler/pool.h"
#include "compile_commands/load.h"
#include "common/logger.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

namespace codexray {

namespace {

unsigned DefaultParallel() {
  unsigned n = static_cast<unsigned>(std::thread::hardware_concurrency());
  return n > 0 ? n -2 : 1;
}

}  // namespace

void RunTUPool(const std::vector<TUEntry>& tus,
               unsigned parallel,
               TUProcessor processor,
               ProgressCallback on_progress) {
  if (tus.empty()) {
    if (on_progress) on_progress(0, 0);
    return;
  }
  if (parallel == 0) parallel = DefaultParallel();
  if (parallel > tus.size()) parallel = static_cast<unsigned>(tus.size());

  if (on_progress) on_progress(0, tus.size());

  std::mutex queue_mutex;
  std::queue<size_t> index_queue;
  for (size_t i = 0; i < tus.size(); ++i) index_queue.push(i);
  std::atomic<size_t> done_count{0};
  std::condition_variable cv;
  std::mutex done_mutex;

  auto worker = [&]() {
    for (;;) {
      size_t idx;
      {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (index_queue.empty()) break;
        idx = index_queue.front();
        index_queue.pop();
      }
      bool ok = true;
      if (processor) ok = processor(tus[idx]);
      if (ok) {
        size_t done = ++done_count;
        if (on_progress) on_progress(done, tus.size());
      }
    }
  };

  std::vector<std::thread> threads;
  threads.reserve(parallel);
  for (unsigned i = 0; i < parallel; ++i)
    threads.emplace_back(worker);
  for (auto& t : threads) t.join();

  if (on_progress) on_progress(tus.size(), tus.size());
  LogInfo("RunTUPool: completed %zu TUs", tus.size());
}

}  // namespace codexray
