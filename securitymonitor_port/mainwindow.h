#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QElapsedTimer>
#include <QImage>
#include <QListWidgetItem>
#include <QMainWindow>
#include <QStringList>
#include <QVector>

#include <memory>
#include <vector>

#include "core/FaceAnalyzer.h"
#include "core/PreviewChannel.h"
#include "core/ThreadPool.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QLabel;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(bool testMode = false,
                        const QStringList& fileSources = QStringList(),
                        const QString& recordDir = QString(),
                        const QString& faceDir = QString(),
                        qint64 maxStorageBytes = 0,
                        QWidget *parent = nullptr);
    ~MainWindow() override;

    // --autostart 用：窗口显示后自动启动预览（方便演示/测试）
    void autoStart();

    // --channels N 用：启动前指定路数（1/2/4）
    void setChannelCount(int count);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onStartStopClicked();
    void onChannelFrameReady(int channelId, const QImage& image);
    void onChannelOnline(int channelId, const QString& sourceName);
    void onChannelOffline(int channelId, const QString& reason);
    void onRecordingStarted(int channelId, const QString& filePath);
    void onRecordingStopped(int channelId, const QString& filePath, qint64 durationMs);
    void onQuotaCheckTimeout();
    void onFaceInfo(int channelId, const QString& name, bool known,
                    double distance, const QByteArray& feature);
    void onFaceBoxes(int channelId, const QVector<FaceBox>& boxes);
    void onRegisterClicked();
    void onEventItemDoubleClicked(QListWidgetItem* item);
    void onHistoryClicked();

private:
    bool startPreview();
    void stopPreview();
    void setupChannels(int count);
    void clearChannels();
    void appendEvent(const QString& text, const QString& type,
                     const QString& sourceName, const QString& detail);
    void setLabelRecording(int channelId, bool recording);
    QString recordDirectory() const;

    Ui::MainWindow *ui;

    // 核心组件
    std::unique_ptr<ThreadPool> m_pool;
    std::vector<std::unique_ptr<PreviewChannel>> m_channels;
    QVector<QLabel*> m_previewLabels;

    bool m_testMode = false;
    QStringList m_fileSources;
    QString m_recordDir;
    QString m_faceDir;             // 人脸模型目录（含两个 onnx）
    std::unique_ptr<FaceAnalyzer> m_faceAnalyzer;
    bool m_faceReady = false;      // 模型加载成功后才允许开启人脸识别
    QByteArray m_lastFaceFeature;  // 最近一次检测到的人脸特征（供登记）
    QVector<QVector<FaceBox>> m_faceBoxes; // 每路最近一次人脸框（主线程叠加绘制）
    qint64 m_maxStorageBytes = 0;  // 录像存储上限(字节)，0=不限制
    QTimer* m_quotaTimer = nullptr; // 容量检查兜底定时器（30 分钟）
    bool m_previewRunning = false;
    int m_runningCount = 0;

    // 帧率统计
    QElapsedTimer m_fpsTimer;
    int m_frameCount = 0;
    double m_fps = 0.0;
};

#endif // MAINWINDOW_H