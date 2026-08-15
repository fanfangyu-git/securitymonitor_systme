#include "PreviewChannel.h"

#include <chrono>
#include <thread>

#include <QDateTime>
#include <QDir>

#include <opencv2/imgcodecs.hpp>

#include "core/FaceAnalyzer.h"
#include "core/ImageConvert.h"
#include "core/MotionDetector.h"
#include "core/StorageManager.h"
#include "core/VideoRecorder.h"

PreviewChannel::PreviewChannel(int channelId,
                               std::unique_ptr<VideoSource> source,
                               ThreadPool* pool,
                               const QString& recordDir,
                               bool motionEnabled,
                               FaceAnalyzer* faceAnalyzer,
                               bool faceEnabled,
                               qint64 maxStorageBytes,
                               QObject* parent)
    : QObject(parent)
    , m_channelId(channelId)
    , m_source(std::move(source))
    , m_pool(pool)
    , m_recordDir(recordDir)
    , m_motionEnabled(motionEnabled)
    , m_maxStorageBytes(maxStorageBytes)
    , m_faceAnalyzer(faceAnalyzer)
    , m_faceEnabled(faceEnabled)
{
    m_motion.reset(new MotionDetector());
    m_recorder.reset(new VideoRecorder());
}

PreviewChannel::~PreviewChannel()
{
    stop();
}

bool PreviewChannel::start()
{
    if (m_running) return true;
    if (!m_source) return false;

    if (!m_source->open()) {
        emit channelOffline(m_channelId, m_source->name() + QStringLiteral(" 打开失败"));
        return false;
    }

    m_buffer.reset();
    m_motion->reset();
    m_noMotionFrames = 0;
    m_running = true;
    m_captureThread = std::thread(&PreviewChannel::captureLoop, this);
    m_consumeThread = std::thread(&PreviewChannel::consumeLoop, this);
    emit channelOnline(m_channelId, m_source->name());
    return true;
}

void PreviewChannel::stop()
{
    if (!m_running) {
        if (m_source) m_source->close();
        return;
    }

    m_running = false;
    m_buffer.wakeAll(); // 唤醒阻塞中的消费者

    if (m_captureThread.joinable()) m_captureThread.join();
    if (m_consumeThread.joinable()) m_consumeThread.join();

    // 若有未完成的录像，先收尾保存
    if (m_recorder && m_recorder->isRecording()) {
        QString path;
        qint64 duration = 0;
        m_recorder->stop(&path, &duration);
        emit recordingStopped(m_channelId, path, duration);
    }

    if (m_source) m_source->close();
}

QString PreviewChannel::sourceName() const
{
    return m_source ? m_source->name() : QString();
}

bool PreviewChannel::isRecording() const
{
    return m_recorder && m_recorder->isRecording();
}

void PreviewChannel::enforceStorageQuota()
{
    if (m_maxStorageBytes <= 0) return;  // 未设置上限

    // 节流：距上次检查不足 5 秒则跳过，避免高频 stop 时反复全量扫描
    if (m_lastQuotaCheck.isValid() && m_lastQuotaCheck.elapsed() < 5000) return;
    m_lastQuotaCheck.start();

    StorageManager::enforceQuota(m_recordDir, m_maxStorageBytes, 0.8);
}

void PreviewChannel::captureLoop()
{
    while (m_running) {
        auto frame = std::make_shared<cv::Mat>();
        if (!m_source->read(*frame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            continue;
        }

        // 运动检测 + 事件驱动录像（在采集线程执行，不干扰预览）
        if (m_motionEnabled) {
            const bool motion = m_motion->detect(*frame);
            if (motion) {
                m_noMotionFrames = 0;
                if (!m_recorder->isRecording()) {
                    const QString nameBase = QStringLiteral("ch%1_%2")
                        .arg(m_channelId)
                        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
                    if (m_recorder->start(*frame, m_recordDir, nameBase)) {
                        emit recordingStarted(m_channelId, m_recorder->currentPath());
                    }
                }
                m_recorder->write(*frame);
            } else if (m_recorder->isRecording() && ++m_noMotionFrames >= m_stopDelayFrames) {
                QString path;
                qint64 duration = 0;
                m_recorder->stop(&path, &duration);
                emit recordingStopped(m_channelId, path, duration);
                enforceStorageQuota();  // 每段录像结束后检查存储上限
            }
        }

        m_buffer.put(std::move(frame));
    }
}

void PreviewChannel::consumeLoop()
{
    while (m_running) {
        auto frame = m_buffer.waitAndPopNewest();
        if (!frame) continue;

        // 1) 人脸框：每路用独立 YuNet 实例对当前预览帧同步检测（无锁），
        //    框与画面严格对齐，消除"框追不上人"延迟，也不阻塞/被阻塞异步识别。
        if (m_faceAnalyzer && m_faceEnabled) {
            if (!m_boxDetector) {
                m_boxDetector = m_faceAnalyzer->createDetector();
            }
            std::vector<FaceAnalyzer::Result> dets;
            if (m_boxDetector &&
                m_faceAnalyzer->detectBoxes(*frame, m_boxDetector, dets)) {
                QVector<FaceBox> boxes;
                boxes.reserve(static_cast<int>(dets.size()));
                {
                    QMutexLocker lock(&m_faceStateMutex);
                    for (const auto& d : dets) {
                        FaceBox b;
                        b.x = d.face.x; b.y = d.face.y;
                        b.w = d.face.width; b.h = d.face.height;
                        b.name = m_stableName;
                        b.known = m_stableKnown;
                        boxes.append(b);
                    }
                }
                emit faceBoxes(m_channelId, boxes);
            }
        }

        // 预览与人脸分析解耦：
        // 1) 预览：直接在本线程转 QImage 并发出，不经过线程池 —— 画面永远是最新帧，无积压延迟
        const QImage image = matToQImage(*frame);
        if (!image.isNull()) {
            emit frameReady(m_channelId, image);
        }

        // 2) 人脸识别（异步）：每 3 帧提交一次线程池（积压过多时跳过本轮），
        //    只更新稳定身份/事件/截图，不再发人脸框（框由上面的同步检测负责）
        if (m_faceAnalyzer && m_faceEnabled && (++m_faceFrameCounter % 3) == 0) {
            if (m_pool->pendingCount() >= m_maxPendingTasks) continue;

            PreviewChannel* self = this;
            const int id = m_channelId;
            m_pool->submit([self, id, frame]() {
                // 每次都做完整比对（任务本身已按 3 帧节流），结论必然确定：陌生人或姓名
                auto results = self->m_faceAnalyzer->analyze(*frame, true, false);

                // 身份平滑（主脸）：快速移动/运动模糊时单帧可能把已知人员误判成陌生人，
                // 连续 m_identityConfirmFrames 次一致才切换身份，避免红绿闪烁
                if (!results.empty()) {
                    FaceAnalyzer::Result& primary = results.front();
                    QMutexLocker lock(&self->m_faceStateMutex);
                    const bool sameAsStable =
                        (primary.name == self->m_stableName) &&
                        (primary.known == self->m_stableKnown);
                    if (sameAsStable) {
                        self->m_candidateCount = 0;
                    } else if (primary.name == self->m_candidateName &&
                               primary.known == self->m_candidateKnown) {
                        ++self->m_candidateCount;
                    } else {
                        self->m_candidateName = primary.name;
                        self->m_candidateKnown = primary.known;
                        self->m_candidateCount = 1;
                    }
                    if (self->m_candidateCount >= self->m_identityConfirmFrames) {
                        self->m_stableName = self->m_candidateName;
                        self->m_stableKnown = self->m_candidateKnown;
                        self->m_candidateCount = 0;
                    }
                    // 用稳定身份覆盖主脸判定（画框/事件/截图全部走平滑后结果）
                    primary.name = self->m_stableName;
                    primary.known = self->m_stableKnown;
                }

                if (!results.empty()) {
                    const FaceAnalyzer::Result& r = results.front();
                    // 事件节流：同一路 5 秒最多上报一次，避免事件列表刷屏
                    const qint64 now = QDateTime::currentMSecsSinceEpoch();
                    qint64 last = self->m_lastFaceEventMs.load();
                    if (now - last >= 5000 &&
                        self->m_lastFaceEventMs.compare_exchange_strong(last, now)) {
                        emit self->faceInfo(id, r.name, r.known, r.score, r.feature);
                    }

                    // 陌生人自动截图：同一路 30 秒最多一张，保存分析过的那一帧
                    if (!r.known) {
                        const qint64 nowCap = QDateTime::currentMSecsSinceEpoch();
                        qint64 lastCap = self->m_lastCaptureMs.load();
                        if (nowCap - lastCap >= self->m_captureIntervalMs &&
                            self->m_lastCaptureMs.compare_exchange_strong(lastCap, nowCap)) {
                            const QString capDir = self->m_recordDir + QStringLiteral("/captures");
                            QDir().mkpath(capDir);
                            const QString capPath = capDir + QStringLiteral("/stranger_ch%1_%2.jpg")
                                .arg(id + 1)
                                .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz"));
                            if (cv::imwrite(capPath.toLocal8Bit().constData(), *frame)) {
                                emit self->strangerCapture(id, capPath);
                            }
                        }
                    }
                }
            });
        }
    }
}
