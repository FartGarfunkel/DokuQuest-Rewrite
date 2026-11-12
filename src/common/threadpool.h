// common/threadpool.h — background workers for world generation, meshing, LOD and saving.
#pragma once
#include "common/common.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>

namespace dq {

class ThreadPool {
public:
  explicit ThreadPool(int threads = 0);   // 0 = hardware_concurrency - 1 (at least 1)
  ~ThreadPool();
  // Lower priority value runs first (e.g. distance to the player).
  void submit(std::function<void()> job, float priority = 0);
  size_t pending() const;
  int threads() const { return (int)workers.size(); }
  void clear();  // drop queued (not running) jobs
private:
  struct Job { float pri; u64 seq; std::function<void()> fn; };
  std::vector<std::thread> workers;
  std::vector<Job> heap;
  mutable std::mutex mx;
  std::condition_variable cv;
  std::atomic<bool> stop{false};
  u64 seq = 0;
};

// Simple main-thread queue: background jobs post their results back here.
class MainQueue {
public:
  void post(std::function<void()> fn) { std::lock_guard<std::mutex> lk(mx); q.push_back(std::move(fn)); }
  // run queued callbacks for at most budgetMs milliseconds
  void run(double budgetMs = 1e9);
private:
  std::mutex mx; std::deque<std::function<void()>> q;
};

} // namespace dq
