#include "FileSource.h"

#include <QFileInfo>

#include <thread>

FileSource::FileSource(const QString& filePath, bool loop)
    : m_filePath(filePath)
    , m_loop(loop)
{
}

bool FileSource::open()
{
    if (m_capture.isOpened()) return true;
    m_capture.open(m_filePath.toLocal8Bit().constData());
    if (!m_capture.isOpened()) return false;
    m_fps = m_capture.get(cv::CAP_PROP_FPS);
    m_frameIndex = 0;
    m_playStart = std::chrono::steady_clock::now();
    return true;
}

bool FileSource::read(cv::Mat& frame)
{
    if (!m_capture.isOpened()) return false;
    if (!m_capture.read(frame)) {
        if (!m_loop) return false;
        // 播放到结尾后从头循环
        m_capture.set(cv::CAP_PROP_POS_FRAMES, 0);
        if (!m_capture.read(frame)) return false;
        m_frameIndex = 0;
        m_playStart = std::chrono::steady_clock::now();
    }

    // 按视频原始帧率节流：文件源按原生速度输出，避免预览"快放"。
    // 第 n 帧应在播放开始后 n/fps 秒出现，提前到达则休眠补足。
    if (m_fps > 1.0) {
        const double expectedSec = m_frameIndex / m_fps;
        const double elapsedSec = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - m_playStart).count();
        const double remainSec = expectedSec - elapsedSec;
        if (remainSec > 0) {
            std::this_thread::sleep_for(std::chrono::duration<double>(remainSec));
        }
    }
    ++m_frameIndex;
    return true;
}

void FileSource::close()
{
    if (m_capture.isOpened()) m_capture.release();
}

bool FileSource::isOpen() const
{
    return m_capture.isOpened();
}

QString FileSource::name() const
{
    return QFileInfo(m_filePath).fileName();
}
