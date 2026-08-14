#include "MotionDetector.h"

#include <opencv2/imgproc.hpp>

MotionDetector::MotionDetector(int minArea, int diffThreshold)
    : m_minArea(minArea)
    , m_diffThreshold(diffThreshold)
{
}

bool MotionDetector::detect(const cv::Mat& frame)
{
    cv::Mat gray;
    if (frame.channels() == 3) {
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = frame;
    }

    if (m_prevGray.empty()) {
        m_prevGray = gray.clone();
        return false; // 第一帧只做初始化
    }

    cv::Mat diff;
    cv::absdiff(gray, m_prevGray, diff);
    cv::threshold(diff, diff, m_diffThreshold, 255, cv::THRESH_BINARY);

    const int changed = cv::countNonZero(diff);
    m_prevGray = gray.clone();

    return changed > m_minArea;
}

void MotionDetector::reset()
{
    m_prevGray.release();
}