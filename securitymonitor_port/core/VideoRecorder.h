#ifndef VIDEORECORDER_H
#define VIDEORECORDER_H

#include <QElapsedTimer>
#include <QString>

#include <opencv2/videoio.hpp>

// 录像器：封装 cv::VideoWriter（MJPG/AVI），只负责写文件，
// 何时开始/停止由调用方（PreviewChannel）决定。
class VideoRecorder
{
public:
    VideoRecorder() = default;

    // 用第一帧的尺寸初始化录像，成功返回 true
    bool start(const cv::Mat& firstFrame, const QString& dir,
               const QString& fileNameBase, int fps = 25);

    // 写入一帧（仅录像中有效）
    void write(const cv::Mat& frame);

    // 停止录像，返回文件路径与录制时长(毫秒)
    bool stop(QString* outPath = nullptr, qint64* outDurationMs = nullptr);

    bool isRecording() const { return m_writer.isOpened(); }
    QString currentPath() const { return m_path; }

private:
    cv::VideoWriter m_writer;
    QString m_path;
    QElapsedTimer m_timer;
};

#endif // VIDEORECORDER_H