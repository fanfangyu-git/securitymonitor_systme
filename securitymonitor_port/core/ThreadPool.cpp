#include "ThreadPool.h"

ThreadPool::ThreadPool(int workerCount)
    : m_workerCount(workerCount > 0 ? workerCount : 1)
{
    m_workers.reserve(m_workerCount);
    for (int i = 0; i < m_workerCount; ++i) {
        m_workers.emplace_back(&ThreadPool::workerLoop, this);
    }
}

ThreadPool::~ThreadPool()
{
    stop();
}

void ThreadPool::submit(std::function<void()> task)
{
    if (!task) return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stop) return;
        m_tasks.push_back(std::move(task));
    }
    m_cv.notify_one();
}

void ThreadPool::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stop) return;
        // 丢弃尚未开始的任务（它们引用的通道/人脸对象即将销毁，无需再执行）
        m_tasks.clear();
        m_stop = true;
    }
    m_cv.notify_all();
    for (auto& worker : m_workers) {
        if (worker.joinable()) worker.join();
    }
    m_workers.clear();
}

void ThreadPool::drain()//等待那些正在工作的线程完成
{
    std::unique_lock<std::mutex> lock(m_mutex);
    // 丢弃尚未开始的任务（引用即将销毁的通道对象），只等在途任务执行完
    m_tasks.clear();
    m_cv.wait(lock, [this] { return m_busy == 0; });
}

std::size_t ThreadPool::pendingCount() const//待处理的任务
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tasks.size();
}

void ThreadPool::workerLoop()
{
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stop || !m_tasks.empty(); });
            if (m_stop && m_tasks.empty()) return;
            task = std::move(m_tasks.front());
            m_tasks.pop_front();
            ++m_busy;
        }
        task();
        --m_busy;
        m_cv.notify_all(); // 唤醒 drain()：队列已空且无任务在执行
    }
}
