#include "SyntheticSource.h"

#include <chrono>
#include <thread>

#include <QDateTime>
#include <opencv2/imgproc.hpp>

bool SyntheticSource::open()
{
    m_open = true;
    m_frameIndex = 0;
    return true;
}

bool SyntheticSource::read(cv::Mat& frame)
{
    if (!m_open) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(33)); // 约 30fps

    frame = cv::Mat(480, 640, CV_8UC3, cv::Scalar(30, 30, 30));

    // 移动方块，方便肉眼确认画面在动
    const int x = (m_frameIndex * 6) % 560;
    cv::rectangle(frame, cv::Rect(x, 200, 80, 80), cv::Scalar(0, 0, 255), 2);

    cv::putText(frame, "TEST PATTERN", cv::Point(20, 40),
                cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2);
    const std::string ts = QDateTime::currentDateTime().toString("HH:mm:ss.zzz").toStdString();
    cv::putText(frame, ts, cv::Point(20, 460),
                cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 0), 1);

    ++m_frameIndex;
    return true;
}

void SyntheticSource::close()
{
    m_open = false;
}

bool SyntheticSource::isOpen() const
{
    return m_open;
}

QString SyntheticSource::name() const
{
    return QStringLiteral("测试视频源");
}