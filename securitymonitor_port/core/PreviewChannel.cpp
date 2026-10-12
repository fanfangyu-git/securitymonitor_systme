#include "PreviewChannel.h"

#include <algorithm>
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

namespace {

// 两框交并比（IoU），用于跨帧人脸关联
double rectIou(const cv::Rect& a, const cv::Rect& b)
{
    const cv::Rect inter = a & b;
    if (inter.width <= 0 || inter.height <= 0) return 0.0;
    const double ia = static_cast<double>(inter.area());
    const double ua = static_cast<double>(a.area()) + b.area() - ia;
    return ua > 0.0 ? ia / ua : 0.0;
}

// 多帧 512 维特征平均（每帧已 L2 归一化：加和方向再 L2 归一化，抗单帧模糊/噪声）
QByteArray averageFeatures(const std::vector<QByteArray>& feats)
{
    if (feats.empty()) return QByteArray();
    cv::Mat sum(1, 512, CV_32F, cv::Scalar(0.0));
    int used = 0;
    for (const auto& f : feats) {
        if (f.size() != 512 * static_cast<int>(sizeof(float))) continue;
        const cv::Mat m(1, 512, CV_32F, const_cast<char*>(f.constData()));
        sum += m;
        ++used;
    }
    if (used == 0) return QByteArray();
    cv::normalize(sum, sum, 1.0, 0.0, cv::NORM_L2); // 平均方向再归一化
    return QByteArray(reinterpret_cast<const char*>(sum.data),
                      512 * static_cast<int>(sizeof(float)));
}

} // namespace

const double PreviewChannel::m_knownThreshold  = 0.4;  // 与 FaceAnalyzer 默认阈值一致
const double PreviewChannel::m_rejectThreshold = 0.30; // 明确陌生人下限（拒识区 0.30~0.40）
const double PreviewChannel::m_trackIouMin     = 0.3;

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
                bool hasNewFace = false; // 是否存在尚未建立跟踪的新脸
                int nfx = 0, nfy = 0, nfw = 0, nfh = 0;
                {
                    QMutexLocker lock(&m_faceStateMutex);
                    // 每张脸按 IoU 关联到自己的跟踪目标，取该目标独立的身份；
                    // 新出现/尚未建立跟踪的脸标记 pending（识别中），不再套用主脸身份
                    for (const auto& d : dets) {
                        FaceBox b;
                        b.x = d.face.x; b.y = d.face.y;
                        b.w = d.face.width; b.h = d.face.height;
                        double bestIou = 0.0;
                        const TrackedFace* hit = nullptr;
                        for (const auto& t : m_tracks) {
                            const double v = rectIou(d.face, t.lastRect);
                            if (v > bestIou) { bestIou = v; hit = &t; }
                        }
                        if (hit && bestIou >= m_trackIouMin) {
                            b.name = hit->stableName;
                            b.known = hit->stableKnown;
                        } else {
                            b.pending = true; // 身份判定中，画中性框
                            if (!hasNewFace) {
                                hasNewFace = true;
                                nfx = d.face.x; nfy = d.face.y;
                                nfw = d.face.width; nfh = d.face.height;
                            }
                        }
                        boxes.append(b);
                    }
                }
                emit faceBoxes(m_channelId, boxes);

                // 方案A：新脸即时识别——检测到尚未建立跟踪的新脸时立即提交一轮识别，
                // 不再等 3 帧节流（首识别延迟由 ~150ms 降到 ~50ms）。
                // 防抖：同一新脸任务在途(原子标志)或上次即时识别未建成跟踪(500ms 冷却)
                // 时不重复触发，避免低质量/闪烁检测反复打满识别任务。
                if (hasNewFace && !m_immediateTaskPending.exchange(true)) {
                    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                    if (nowMs < m_immediateCooldownMs.load()) {
                        m_immediateTaskPending.store(false);
                    } else {
                        submitFaceAnalysis(frame);
                    }
                }
            }
        }

        // 预览与人脸分析解耦：
        // 1) 预览：直接在本线程转 QImage 并发出，不经过线程池 —— 画面永远是最新帧，无积压延迟
        const QImage image = matToQImage(*frame);
        if (!image.isNull()) {
            emit frameReady(m_channelId, image);
        }

        // 2) 人脸识别（异步）：每 3 帧提交一次线程池（积压过多时跳过本轮），
        //    只更新跟踪身份/事件/截图，不再发人脸框（框由上面的同步检测负责）
        if (m_faceAnalyzer && m_faceEnabled && (++m_faceFrameCounter % 3) == 0) {
            submitFaceAnalysis(frame);
        }
    }
}

void PreviewChannel::submitFaceAnalysis(const std::shared_ptr<cv::Mat>& frame)
{
    if (!m_faceAnalyzer || !m_faceEnabled || !frame) return;
    if (m_pool->pendingCount() >= m_maxPendingTasks) {
        m_immediateTaskPending.store(false); // 本轮未提交（队列满），允许下次重试
        return;
    }
    PreviewChannel* self = this;
    m_pool->submit([self, frame]() { self->analyzeAndTrack(frame); });
}

void PreviewChannel::analyzeAndTrack(const std::shared_ptr<cv::Mat>& frame)
{
    if (!frame) return;
    // 每次都做完整比对（任务本身已按节流提交），结论必然确定：陌生人或姓名
    auto results = m_faceAnalyzer->analyze(*frame, true, false);
    const int id = m_channelId;

    QMutexLocker lock(&m_faceStateMutex);

    // 全部跟踪目标本轮未匹配计数 +1（用于过期清理）
    for (auto& t : m_tracks) ++t.missFrames;

    bool createdTrack = false; // 本轮是否新建了跟踪（即时识别防抖用）

    // 逐脸关联跟踪目标并独立做身份平滑（三区判定 + 连续确认）
    for (auto& r : results) {
        // 质量门控未通过（decided=false）：只更新位置，不改变身份
        if (!r.decided) {
            for (auto& t : m_tracks) {
                if (rectIou(r.face, t.lastRect) >= m_trackIouMin) {
                    t.lastRect = r.face;
                    t.missFrames = 0;
                }
            }
            continue;
        }

        // IoU 关联：命中已有目标则更新，否则新建跟踪
        TrackedFace* track = nullptr;
        double bestIou = 0.0;
        for (auto& t : m_tracks) {
            const double v = rectIou(r.face, t.lastRect);
            if (v > bestIou) { bestIou = v; track = &t; }
        }
        if (!track || bestIou < m_trackIouMin) {
            TrackedFace nf;
            nf.lastRect = r.face;
            // 方案D：新脸首帧判定直接作为稳定身份立即显示（先显示、再确认）——
            // 身份平滑只用于后续变化，避免新脸出现后长时间"识别中"
            nf.stableName = r.name;
            nf.stableKnown = r.known;
            nf.recentFeatures.push_back(r.feature); // 首帧特征入列
            m_tracks.push_back(nf);
            createdTrack = true;
            continue;
        }
        track->lastRect = r.face;
        track->missFrames = 0;

        // 登记特征累积：最近 m_featureAvgFrames 帧特征（平均后抗单帧模糊）
        track->recentFeatures.push_back(r.feature);
        if (track->recentFeatures.size() > static_cast<size_t>(m_featureAvgFrames)) {
            track->recentFeatures.erase(track->recentFeatures.begin());
        }

        // 三区判定：>=knownThreshold 已知；<=rejectThreshold 陌生人；
        // 中间为拒识区——保持该目标当前身份，不向任何方向漂移
        if (r.score >= m_knownThreshold) {
            if (r.name == track->stableName && track->stableKnown) {
                track->candCount = 0;
            } else if (r.name == track->candName && track->candKnown) {
                ++track->candCount;
            } else {
                track->candName = r.name;
                track->candKnown = true;
                track->candCount = 1;
            }
        } else if (r.score <= m_rejectThreshold) {
            if (track->stableName.isEmpty() && !track->stableKnown) {
                track->candCount = 0;
            } else if (track->candName.isEmpty() && !track->candKnown) {
                ++track->candCount;
            } else {
                track->candName.clear();
                track->candKnown = false;
                track->candCount = 1;
            }
        } else {
            // 拒识区：保持现状，消除候选（不因中间值抖动而切换）
            track->candCount = 0;
        }

        // 连续确认后才切换稳定身份（避免红绿闪烁）
        if (track->candCount >= m_identityConfirmFrames) {
            track->stableName = track->candName;
            track->stableKnown = track->candKnown;
            track->candCount = 0;
        }

        // 事件节流：同一路 5 秒最多上报一次，避免事件列表刷屏；
        // 结论使用该目标的稳定身份（与画框一致）
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 last = m_lastFaceEventMs.load();
        if (now - last >= 5000 && m_lastFaceEventMs.compare_exchange_strong(last, now)) {
            emit faceInfo(id, track->stableName, track->stableKnown, r.score, r.feature);
        }

        // 陌生人自动截图：同一路 30 秒最多一张，保存分析过的那一帧
        if (!track->stableKnown) {
            const qint64 nowCap = QDateTime::currentMSecsSinceEpoch();
            qint64 lastCap = m_lastCaptureMs.load();
            if (nowCap - lastCap >= m_captureIntervalMs &&
                m_lastCaptureMs.compare_exchange_strong(lastCap, nowCap)) {
                const QString capDir = m_recordDir + QStringLiteral("/captures");
                QDir().mkpath(capDir);
                const QString capPath = capDir + QStringLiteral("/stranger_ch%1_%2.jpg")
                    .arg(id + 1)
                    .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz"));
                if (cv::imwrite(capPath.toLocal8Bit().constData(), *frame)) {
                    emit strangerCapture(id, capPath);
                }
            }
        }
    }

    // 过期清理：人脸持续未出现，身份随目标移除而清除（下张人脸不继承身份）
    auto& tracks = m_tracks;
    tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
                                [this](const TrackedFace& t) {
                                    return t.missFrames > m_trackMissLimit;
                                }),
                 tracks.end());

    // 构建全量登记候选（锁内拷贝数据），供主窗口点名登记
    QVector<FaceCandidate> cands;
    cands.reserve(static_cast<int>(results.size()));
    for (const auto& r : results) {
        FaceCandidate c;
        c.x = r.face.x; c.y = r.face.y;
        c.w = r.face.width; c.h = r.face.height;
        c.score = r.score;
        const TrackedFace* hit = nullptr;
        double bestIou = 0.0;
        for (const auto& t : m_tracks) {
            const double v = rectIou(r.face, t.lastRect);
            if (v > bestIou) { bestIou = v; hit = &t; }
        }
        if (hit && bestIou >= m_trackIouMin) {
            c.name = hit->stableName;
            c.known = hit->stableKnown;
            c.feature = averageFeatures(hit->recentFeatures);
        }
        cands.append(c);
    }
    lock.unlock();
    emit faceCandidates(id, cands);

    // 即时识别任务收尾：复位在途标志；若本轮未建成任何跟踪（质量差/闪烁检测），
    // 设置冷却，避免同一张低质量脸反复触发即时识别
    m_immediateTaskPending.store(false);
    if (!createdTrack) {
        m_immediateCooldownMs.store(QDateTime::currentMSecsSinceEpoch() + m_immediateCooldown);
    }
}
