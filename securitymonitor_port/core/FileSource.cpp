#include "FileSource.h"

#include <QFileInfo>

FileSource::FileSource(const QString& filePath, bool loop)
    : m_filePath(filePath)
    , m_loop(loop)
{
}

bool FileSource::open()
{
    if (m_capture.isOpened()) return true;
    m_capture.open(m_filePath.toLocal8Bit().constData());
    return m_capture.isOpened();
}

bool FileSource::read(cv::Mat& frame)
{
    if (!m_capture.isOpened()) return false;
    if (!m_capture.read(frame)) {
        if (!m_loop) return false;
        // 播放到结尾后从头循环
        m_capture.set(cv::CAP_PROP_POS_FRAMES, 0);
        if (!m_capture.read(frame)) return false;
    }
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