#ifndef FILESOURCE_H
#define FILESOURCE_H

#include "VideoSource.h"

#include <chrono>
#include <QString>
#include <opencv2/videoio.hpp>

// 视频文件视频源（可模拟第二路摄像头）
// 按视频原始帧率节流输出：read() 会以原生速度返回帧，避免预览"快放"
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
    double m_fps = 0.0;                              // 视频原始帧率（<=1 时不节流）
    long long m_frameIndex = 0;                      // 已输出帧数（含循环重置）
    std::chrono::steady_clock::time_point m_playStart; // 本次播放起点（含循环起点）
};

#endif // FILESOURCE_H
