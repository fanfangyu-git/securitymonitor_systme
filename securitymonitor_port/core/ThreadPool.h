#ifndef THREADPOOL_H
#define THREADPOOL_H

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

    int workerCount() const { return m_workerCount; }

private:
    void workerLoop();

    std::vector<std::thread> m_workers;
    std::deque<std::function<void()>> m_tasks;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stop = false;
    int m_workerCount;
};

#endif // THREADPOOL_H