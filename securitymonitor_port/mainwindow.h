#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QElapsedTimer>
#include <QImage>
#include <QMainWindow>

#include <atomic>
#include <memory>
#include <thread>

#include "core/FrameBuffer.h"
#include "core/ThreadPool.h"
#include "core/VideoSource.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(bool testMode = false, QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

signals:
    // 跨线程传递一帧画面（队列连接，在主线程刷新 UI）
    void frameReady(const QImage& image);

private slots:
    void onStartStopClicked();
    void onFrameReady(const QImage& image);

private:
    bool startPreview();
    void stopPreview();
    void captureLoop();
    void consumeLoop();
    static QImage cvMatToQImage(const cv::Mat& mat);

    Ui::MainWindow *ui;

    // 核心组件
    std::unique_ptr<ThreadPool> m_pool;
    std::unique_ptr<FrameBuffer> m_buffer;
    std::unique_ptr<VideoSource> m_source;
    bool m_testMode = false;

    // 线程
    std::thread m_captureThread;
    std::thread m_consumeThread;
    std::atomic<bool> m_running{false};

    // 帧率统计
    QElapsedTimer m_fpsTimer;
    int m_frameCount = 0;
    double m_fps = 0.0;
};

#endif // MAINWINDOW_H