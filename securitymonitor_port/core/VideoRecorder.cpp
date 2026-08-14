#include "VideoRecorder.h"

#include <QDebug>
#include <QDir>

bool VideoRecorder::start(const cv::Mat& firstFrame, const QString& dir,
                          const QString& fileNameBase, int fps)
{
    if (m_writer.isOpened()) return true;

    if (!QDir().mkpath(dir)) {
        qWarning() << "创建录像目录失败:" << dir;
        return false;
    }

    m_path = dir + "/" + fileNameBase + ".avi";
    m_writer.open(m_path.toLocal8Bit().constData(),
                  cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                  fps,
                  cv::Size(firstFrame.cols, firstFrame.rows));
    if (!m_writer.isOpened()) {
        qWarning() << "打开录像文件失败:" << m_path;
        m_path.clear();
        return false;
    }

    m_timer.start();
    return true;
}

void VideoRecorder::write(const cv::Mat& frame)
{
    if (m_writer.isOpened()) m_writer.write(frame);
}

bool VideoRecorder::stop(QString* outPath, qint64* outDurationMs)
{
    if (!m_writer.isOpened()) return false;

    m_writer.release();
    const qint64 duration = m_timer.elapsed();

    if (outPath) *outPath = m_path;
    if (outDurationMs) *outDurationMs = duration;
    return true;
}