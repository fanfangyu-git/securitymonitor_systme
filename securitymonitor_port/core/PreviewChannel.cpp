#include "PreviewChannel.h"

#include <chrono>
#include <thread>

#include <QDateTime>

#include "core/ImageConvert.h"
#include "core/MotionDetector.h"
#include "core/StorageManager.h"
#include "core/VideoRecorder.h"

PreviewChannel::PreviewChannel(int channelId,
                               std::unique_ptr<VideoSource> source,
                               ThreadPool* pool,
                               const QString& recordDir,
                               bool motionEnabled,
                               qint64 maxStorageBytes,
                               QObject* parent)
    : QObject(parent)
    , m_channelId(channelId)
    , m_source(std::move(source))
    , m_pool(pool)
    , m_recordDir(recordDir)
    , m_motionEnabled(motionEnabled)
    , m_maxStorageBytes(maxStorageBytes)
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

        PreviewChannel* self = this;
        const int id = m_channelId;
        m_pool->submit([self, id, frame]() {
            const QImage image = matToQImage(*frame);
            if (!image.isNull()) {
                emit self->frameReady(id, image);
            }
        });
    }
}