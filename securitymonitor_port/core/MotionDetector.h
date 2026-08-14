#ifndef MOTIONDETECTOR_H
#define MOTIONDETECTOR_H

#include <opencv2/core.hpp>

// 基于帧差法的运动检测：当前帧与上一帧灰度图做差，
// 变化像素数超过 minArea 即认为检测到运动。适合固定机位的监控场景。
class MotionDetector
{
public:
    explicit MotionDetector(int minArea = 1000, int diffThreshold = 25);

    // 返回 true 表示该帧检测到运动
    bool detect(const cv::Mat& frame);

    // 清空上一帧状态（启动/复位时调用）
    void reset();

    int minArea() const { return m_minArea; }
    int diffThreshold() const { return m_diffThreshold; }

private:
    cv::Mat m_prevGray;
    int m_minArea;
    int m_diffThreshold;
};

#endif // MOTIONDETECTOR_H