#ifndef SYNTHETICSOURCE_H
#define SYNTHETICSOURCE_H

#include "VideoSource.h"

// 测试视频源：无摄像头时生成移动图案，用于验证整条采集链路
class SyntheticSource : public VideoSource
{
public:
    SyntheticSource() = default;

    bool open() override;
    bool read(cv::Mat& frame) override;
    void close() override;
    bool isOpen() const override;
    QString name() const override;

private:
    int m_frameIndex = 0;
    bool m_open = false;
};

#endif // SYNTHETICSOURCE_H