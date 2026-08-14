#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QElapsedTimer>
#include <QImage>
#include <QMainWindow>
#include <QStringList>
#include <QVector>

#include <memory>
#include <vector>

#include "core/PreviewChannel.h"
#include "core/ThreadPool.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QLabel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(bool testMode = false,
                        const QStringList& fileSources = QStringList(),
                        const QString& recordDir = QString(),
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

private:
    bool startPreview();
    void stopPreview();
    void setupChannels(int count);
    void clearChannels();
    void appendEvent(const QString& text, const QString& type,
                     const QString& sourceName, const QString& detail);
    void setLabelRecording(int channelId, bool recording);

    Ui::MainWindow *ui;

    // 核心组件
    std::unique_ptr<ThreadPool> m_pool;
    std::vector<std::unique_ptr<PreviewChannel>> m_channels;
    QVector<QLabel*> m_previewLabels;

    bool m_testMode = false;
    QStringList m_fileSources;
    QString m_recordDir;
    qint64 m_maxStorageBytes = 0;  // 录像存储上限(字节)，<=0 不限制
    bool m_previewRunning = false;
    int m_runningCount = 0;

    // 帧率统计
    QElapsedTimer m_fpsTimer;
    int m_frameCount = 0;
    double m_fps = 0.0;
};

#endif // MAINWINDOW_H