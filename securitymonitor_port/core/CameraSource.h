#ifndef CAMERASOURCE_H
#define CAMERASOURCE_H

#include "VideoSource.h"

#include <opencv2/videoio.hpp>

// 摄像头视频源
class CameraSource : public VideoSource
{
public:
    explicit CameraSource(int deviceIndex = 0);

    bool open() override;
    bool read(cv::Mat& frame) override;
    void close() override;
    bool isOpen() const override;
    QString name() const override;

private:
    int m_deviceIndex;
    cv::VideoCapture m_capture;
};

#endif // CAMERASOURCE_H