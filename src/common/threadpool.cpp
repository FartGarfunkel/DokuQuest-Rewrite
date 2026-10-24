#include "common/threadpool.h"

namespace dq {

static bool jobLess(const auto& a, const auto& b) { return a.pri > b.pri || (a.pri == b.pri && a.seq > b.seq); }

ThreadPool::ThreadPool(int n) {
  if (n <= 0) n = std::max(1, (int)std::thread::hardware_concurrency() - 1);
  for (int i = 0; i < n; i++) workers.emplace_back([this] {
    for (;;) {
      std::function<void()> fn;
      {
        std::unique_lock<std::mutex> lk(mx);
        cv.wait(lk, [&] { return stop || !heap.empty(); });
        if (stop) return;
        std::pop_heap(heap.begin(), heap.end(), [](const Job& a, const Job& b) { return jobLess(a, b); });
        fn = std::move(heap.back().fn); heap.pop_back();
      }
      try { fn(); } catch (std::exception& e) { DQ_ERR("worker job failed: %s", e.what()); }
    }
  });
}
ThreadPool::~ThreadPool() {
  { std::lock_guard<std::mutex> lk(mx); stop = true; heap.clear(); }
  cv.notify_all();
  for (auto& t : workers) t.join();
}
void ThreadPool::submit(std::function<void()> job, float priority) {
  { std::lock_guard<std::mutex> lk(mx); heap.push_back({priority, seq++, std::move(job)});
    std::push_heap(heap.begin(), heap.end(), [](const Job& a, const Job& b) { return jobLess(a, b); }); }
  cv.notify_one();
}
size_t ThreadPool::pending() const { std::lock_guard<std::mutex> lk(mx); return heap.size(); }
void ThreadPool::clear() { std::lock_guard<std::mutex> lk(mx); heap.clear(); }

void MainQueue::run(double budgetMs) {
  double t0 = nowSeconds();
  for (;;) {
    std::function<void()> fn;
    { std::lock_guard<std::mutex> lk(mx); if (q.empty()) return; fn = std::move(q.front()); q.pop_front(); }
    fn();
    if ((nowSeconds() - t0) * 1000.0 > budgetMs) return;
  }
}

} // namespace dq
