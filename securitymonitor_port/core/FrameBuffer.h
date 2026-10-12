#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>

#include <opencv2/core.hpp>

// 环形帧缓冲：生产者 put，消费者取最新帧。
// 缓冲满时丢弃最旧帧，宁可丢帧也要保证实时性。
class FrameBuffer
{
public:
    explicit FrameBuffer(std::size_t capacity = 30);

    // 生产者：放入一帧；缓冲满则丢弃最旧帧
    void put(std::shared_ptr<cv::Mat> frame);

    // 消费者：阻塞等待新帧，返回最新一帧并丢弃积压帧
    std::shared_ptr<cv::Mat> waitAndPopNewest();

    // 消费者：非阻塞取最新帧，无帧时返回 nullptr
    std::shared_ptr<cv::Mat> tryPopNewest();

    // 唤醒所有等待者并标记停止（用于关停）
    void wakeAll();

    // 重置缓冲，允许重新开始（用于再次启动预览）
    void reset();

    std::size_t size() const;
    std::size_t capacity() const { return m_capacity; }

private:
    std::deque<std::shared_ptr<cv::Mat>> m_buffer;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::size_t m_capacity;
    bool m_stopped = false;
};
//缓冲满了为什么丢**最旧**帧而不是拒绝新帧？`waitAndPopNewest` 为什么要 `clear()`？
#endif // FRAMEBUFFER_H
