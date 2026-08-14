#ifndef PREVIEWCHANNEL_H
#define PREVIEWCHANNEL_H

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QString>

#include <atomic>
#include <memory>
#include <thread>

#include "core/FrameBuffer.h"
#include "core/ThreadPool.h"
#include "core/VideoSource.h"

class MotionDetector;
class VideoRecorder;

// 一路视频源的工作单元：采集线程 + 帧缓冲 + 消费线程。
// 采集线程同时做运动检测：检测到运动自动开始录像，运动停止约0.5秒后结束录像。
// 帧经共享线程池转换为 QImage 后通过信号发给界面（队列连接，主线程显示）。
class PreviewChannel : public QObject
{
    Q_OBJECT

public:
    explicit PreviewChannel(int channelId,
                            std::unique_ptr<VideoSource> source,
                            ThreadPool* pool,
                            const QString& recordDir,
                            bool motionEnabled,
                            qint64 maxStorageBytes = 0,
                            QObject* parent = nullptr);
    ~PreviewChannel() override;

    // 启动成功返回 true，失败时发出 channelOffline
    bool start();
    void stop();

    int channelId() const { return m_channelId; }
    QString sourceName() const;
    bool isRecording() const;

signals:
    void frameReady(int channelId, const QImage& image);
    void channelOnline(int channelId, const QString& sourceName);
    void channelOffline(int channelId, const QString& reason);
    void recordingStarted(int channelId, const QString& filePath);
    void recordingStopped(int channelId, const QString& filePath, qint64 durationMs);

private:
    void captureLoop();
    void consumeLoop();
    void enforceStorageQuota();

    int m_channelId;
    std::unique_ptr<VideoSource> m_source;
    ThreadPool* m_pool;   // 共享线程池，不持有所有权
    FrameBuffer m_buffer; // 每路独立帧缓冲
    std::thread m_captureThread;
    std::thread m_consumeThread;
    std::atomic<bool> m_running{false};

    // 运动检测与录像
    std::unique_ptr<MotionDetector> m_motion;
    std::unique_ptr<VideoRecorder> m_recorder;
    QString m_recordDir;
    bool m_motionEnabled = true;
    int m_noMotionFrames = 0;
    static const int m_stopDelayFrames = 15; // 运动停止约 0.5 秒后结束录像

    // 录像存储上限(字节)，<=0 表示不限制；超限时从最旧录像开始删除
    qint64 m_maxStorageBytes = 0;
    QElapsedTimer m_lastQuotaCheck;  // 节流：避免每停止一段都全量扫描
};

#endif // PREVIEWCHANNEL_H