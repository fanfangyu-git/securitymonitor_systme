#ifndef PREVIEWCHANNEL_H
#define PREVIEWCHANNEL_H

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QString>
#include <QVector>

#include <QMutex>
#include <atomic>
#include <memory>
#include <thread>

#include "core/FaceAnalyzer.h"
#include "core/FrameBuffer.h"
#include "core/ThreadPool.h"
#include "core/VideoSource.h"

class MotionDetector;
class VideoRecorder;

// 单个人脸框（主线程叠加绘制用）：坐标相对原始帧
struct FaceBox
{
    int x = 0, y = 0, w = 0, h = 0;
    QString name;   // 命中人员库时为姓名，否则为空
    bool known = false;
    bool pending = false; // true=身份判定中（质量门控/跟踪未建立），绘制中性色，不当作陌生人
};
Q_DECLARE_METATYPE(FaceBox)

// 登记候选：某路某张人脸的当前信息（含稳定身份与平均特征，供点名登记）
struct FaceCandidate
{
    int x = 0, y = 0, w = 0, h = 0;
    QString name;      // 稳定身份（空=陌生人）
    bool known = false;
    double score = 0.0; // 最近一次余弦相似度
    QByteArray feature; // 多帧平均后的 512 维特征（2048 字节）；空=暂无可靠特征（不可登记）
};
Q_DECLARE_METATYPE(FaceCandidate)

// 一路视频源的工作单元：采集线程 + 帧缓冲 + 消费线程。
// 采集线程同时做运动检测：检测到运动自动开始录像，运动停止约0.5秒后结束录像。
// 预览帧在消费线程直接转 QImage 发出（不经过线程池，保证实时）；
// 人脸框在消费线程对当前预览帧同步检测（框与画面严格对齐，不产生追尾延迟）；
// 识别/比对每 3 帧提交一次共享线程池（不阻塞预览），只更新框上的姓名/是否已知，
// 并上报 faceInfo/strangerCapture 事件，结果经 faceBoxes 发给主线程叠加绘制。
class PreviewChannel : public QObject
{
    Q_OBJECT

public:
    explicit PreviewChannel(int channelId,
                            std::unique_ptr<VideoSource> source,
                            ThreadPool* pool,
                            const QString& recordDir,
                            bool motionEnabled,
                            FaceAnalyzer* faceAnalyzer = nullptr,
                            bool faceEnabled = false,
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
    // 人脸识别结果（feature 供登记使用；known=false 即陌生人）
    void faceInfo(int channelId, const QString& name, bool known,
                  double distance, const QByteArray& feature);
    // 最新人脸框（含识别结论：known/姓名），主线程叠加到预览画面
    void faceBoxes(int channelId, const QVector<FaceBox>& boxes);
    // 每轮识别的全量人脸候选（含多帧平均特征，供点名登记；每 3 帧更新一次）
    void faceCandidates(int channelId, const QVector<FaceCandidate>& candidates);
    void strangerCapture(int channelId, const QString& filePath);

private:
    void captureLoop();
    void consumeLoop();
    void enforceStorageQuota();
    // 提交一次人脸分析任务（识别+跟踪+事件+登记候选）到共享线程池
    void submitFaceAnalysis(const std::shared_ptr<cv::Mat>& frame);
    // 线程池内执行一轮人脸分析（analyze -> 逐脸跟踪 -> 三区判定 -> 事件/截图 -> 候选）
    void analyzeAndTrack(const std::shared_ptr<cv::Mat>& frame);

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

    // 人脸识别（共享实例，不持有所有权；由主窗口加载模型与人员库）
    FaceAnalyzer* m_faceAnalyzer = nullptr;
    bool m_faceEnabled = false;
    // 每路独立的人脸框检测器（YuNet 实例）：预览线程逐帧同步检测用，
    // 不参与共享 FaceAnalyzer 的互斥锁，避免与异步识别互相阻塞
    cv::Ptr<cv::FaceDetectorYN> m_boxDetector;
    std::atomic<int> m_faceFrameCounter{0};      // 人脸分析节流计数（每 3 帧一次）
    std::atomic<qint64> m_lastFaceEventMs{0};    // 陌生人/识别事件 5 秒节流
    std::atomic<qint64> m_lastCaptureMs{0};      // 陌生人截图 30 秒节流
    std::atomic<bool> m_immediateTaskPending{false}; // 新脸即时识别任务是否在途（防重复提交）
    std::atomic<qint64> m_immediateCooldownMs{0};   // 即时识别未建成跟踪后的冷却截止(ms)，避免反复触发
    static const int m_captureIntervalMs = 30000; // 陌生人截图最小间隔（毫秒）
    static const qint64 m_immediateCooldown = 500; // 即时识别失败后冷却时长（毫秒）

    // 身份平滑（按人脸跟踪）：单帧可能因运动模糊/角度误判，
    // 每个跟踪目标独立做「连续 m_identityConfirmFrames 次一致才切换」的平滑，
    // 并带三区判定（>=knownThreshold 已知 / <=rejectThreshold 陌生人 / 中间保持现状），
    // 目标连续未出现超过 m_trackMissLimit 轮后移除（身份随人脸消失而清除）
    QMutex m_faceStateMutex;                       // 保护以下身份状态（线程池任务与预览线程访问）
    struct TrackedFace
    {
        cv::Rect lastRect;      // 最近一次出现的人脸框（原图坐标），用于 IoU 关联
        QString stableName;     // 稳定身份（空=陌生人）
        bool stableKnown = false;
        QString candName;       // 候选身份（连续一致才切换）
        bool candKnown = false;
        int candCount = 0;
        int missFrames = 0;     // 连续未匹配轮数（每轮≈3 帧），超限删除
        std::vector<QByteArray> recentFeatures; // 最近多帧特征（登记用平均，抗单帧模糊）
    };
    std::vector<TrackedFace> m_tracks;             // 当前画面中的人脸跟踪列表

    // 三区阈值：相似度 >= 此值判定为已知（与 FaceAnalyzer 默认 0.4 一致）
    static const double m_knownThreshold;
    // 相似度 <= 此值判定为陌生人；两者之间为拒识区（保持上一身份，不漂移）
    static const double m_rejectThreshold;
    static const double m_trackIouMin;             // 人脸关联最小 IoU（0.3）
    static const int m_identityConfirmFrames = 2;  // 身份切换需连续确认次数
    static const int m_trackMissLimit = 10;        // 跟踪目标连续 10 轮(≈1秒)未出现则移除
    static const int m_maxPendingTasks = 6;      // 线程池积压上限：超限跳过本轮人脸分析
    static const int m_featureAvgFrames = 5;     // 登记候选特征取最近 5 帧平均（抗模糊）
};

#endif // PREVIEWCHANNEL_H
