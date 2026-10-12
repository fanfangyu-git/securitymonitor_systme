#ifndef VIDEOSOURCE_H
#define VIDEOSOURCE_H

#include <QString>
#include <opencv2/core.hpp>

// 视频源抽象基类：摄像头 / 视频文件 / 测试源
class VideoSource
{
public:
    virtual ~VideoSource() = default;

    virtual bool open() = 0;
    virtual bool read(cv::Mat& frame) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual QString name() const;
};
//为什么要抽象基类？4 个纯虚函数分别对应什么生命周期？
#endif // VIDEOSOURCE_H
