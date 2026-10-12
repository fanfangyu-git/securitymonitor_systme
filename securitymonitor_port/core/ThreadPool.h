#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// 简单线程池：任务队列 + 固定数量 worker 线程
class ThreadPool
{
public:
    explicit ThreadPool(int workerCount = 4);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 提交一个任务（任意可调用对象），任务在线程池中异步执行
    void submit(std::function<void()> task);

    // 停止所有 worker 并回收线程（重复调用安全）
    void stop();

    // 等待所有已提交任务执行完毕（不停止线程池，之后仍可继续 submit）
    void drain();

    // 当前排队中的任务数（用于背压：积压过多时上游可丢帧保实时）
    std::size_t pendingCount() const;

    int workerCount() const { return m_workerCount; }

private:
    void workerLoop();

    std::vector<std::thread> m_workers;
    std::deque<std::function<void()>> m_tasks;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stop = false;
    std::atomic<int> m_busy{0}; // 正在执行的任务数（drain 用）
    int m_workerCount;
};
//stop()` 和 `drain()` 区别是什么？`m_busy` 为什么必须是 atomic？
#endif // THREADPOOL_H
