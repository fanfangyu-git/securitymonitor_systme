#include "FrameBuffer.h"

FrameBuffer::FrameBuffer(std::size_t capacity)
    : m_capacity(capacity > 0 ? capacity : 1)
{
}

void FrameBuffer::put(std::shared_ptr<cv::Mat> frame)
{
    if (!frame || frame->empty()) return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopped) return;
        if (m_buffer.size() >= m_capacity) {
            m_buffer.pop_front(); // 丢最旧帧，保证实时性
        }
        m_buffer.push_back(std::move(frame));
    }
    m_cv.notify_one();
}

std::shared_ptr<cv::Mat> FrameBuffer::waitAndPopNewest()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] { return m_stopped || !m_buffer.empty(); });
    if (m_buffer.empty()) return nullptr;
    auto frame = std::move(m_buffer.back());
    m_buffer.clear(); // 只保留最新帧，丢弃积压
    return frame;
}

std::shared_ptr<cv::Mat> FrameBuffer::tryPopNewest()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_buffer.empty()) return nullptr;
    auto frame = std::move(m_buffer.back());
    m_buffer.clear();
    return frame;
}

void FrameBuffer::wakeAll()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopped = true;
    }
    m_cv.notify_all();
}

void FrameBuffer::reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buffer.clear();
    m_stopped = false;
}

std::size_t FrameBuffer::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_buffer.size();
}