#ifndef FILESOURCE_H
#define FILESOURCE_H

#include "VideoSource.h"

#include <QString>
#include <opencv2/videoio.hpp>

// 视频文件视频源（可模拟第二路摄像头）
class FileSource : public VideoSource
{
public:
    explicit FileSource(const QString& filePath, bool loop = true);

    bool open() override;
    bool read(cv::Mat& frame) override;
    void close() override;
    bool isOpen() const override;
    QString name() const override;

private:
    QString m_filePath;
    bool m_loop;
    cv::VideoCapture m_capture;
};

#endif // FILESOURCE_H