#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <chrono>
#include <thread>

#include <QCloseEvent>
#include <QDebug>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>

#include <opencv2/imgproc.hpp>

#include "core/CameraSource.h"
#include "core/SyntheticSource.h"
#include "service/DatabaseManager.h"

MainWindow::MainWindow(bool testMode, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_testMode(testMode)
{
    ui->setupUi(this);

    // 事件列表占位
    ui->eventList->addItem(QStringLiteral("（事件列表占位）"));
    ui->eventList->addItem(QStringLiteral("后续里程碑：运动检测 / 人脸识别 / 录像回放"));

    // 启动/停止按钮
    connect(ui->startStopButton, &QPushButton::clicked,
            this, &MainWindow::onStartStopClicked);

    // 核心组件
    m_pool.reset(new ThreadPool(4));
    m_buffer.reset(new FrameBuffer(30));

    // 跨线程刷新 UI：必须使用队列连接
    connect(this, &MainWindow::frameReady, this, &MainWindow::onFrameReady, Qt::QueuedConnection);
}

MainWindow::~MainWindow()
{
    stopPreview();
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    stopPreview();
    QMainWindow::closeEvent(event);
}

void MainWindow::onStartStopClicked()
{
    if (m_running) {
        stopPreview();
        ui->startStopButton->setText(QStringLiteral("启动预览"));
        ui->previewLabel->setText(QStringLiteral("已停止"));
        ui->statusLabel->setText(QStringLiteral("已停止"));
    } else {
        if (startPreview()) {
            ui->startStopButton->setText(QStringLiteral("停止预览"));
        }
    }
}

bool MainWindow::startPreview()
{
    if (m_running) return true;

    // 选择视频源：测试模式 > 摄像头（后续里程碑加入视频文件/多路）
    if (m_testMode) {
        m_source.reset(new SyntheticSource());
    } else {
        m_source.reset(new CameraSource(0));
    }

    if (!m_source->open()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("无法打开视频源（%1）。\n"
                                            "请检查摄像头是否可用，或用 --test 参数启动测试视频源。")
                                 .arg(m_source->name()));
        m_source.reset();
        return false;
    }

    m_buffer->reset();
    m_running = true;

    m_captureThread = std::thread(&MainWindow::captureLoop, this);
    m_consumeThread = std::thread(&MainWindow::consumeLoop, this);

    ui->previewLabel->setText(QStringLiteral("正在启动…"));
    ui->statusLabel->setText(QStringLiteral("视频源: %1").arg(m_source->name()));
    qDebug() << "预览已启动，视频源:" << m_source->name();
    return true;
}

void MainWindow::stopPreview()
{
    if (!m_running) {
        if (m_source) m_source->close();
        return;
    }

    m_running = false;
    m_buffer->wakeAll(); // 唤醒阻塞中的消费者

    if (m_captureThread.joinable()) m_captureThread.join();
    if (m_consumeThread.joinable()) m_consumeThread.join();

    if (m_source) m_source->close();
    m_source.reset();
    qDebug() << "预览已停止";
}

void MainWindow::captureLoop()
{
    while (m_running) {
        auto frame = std::make_shared<cv::Mat>();
        if (!m_source->read(*frame)) {
            // 采集失败（如摄像头临时断开），短暂休眠避免空转
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            continue;
        }
        m_buffer->put(std::move(frame));
    }
}

void MainWindow::consumeLoop()
{
    while (m_running) {
        auto frame = m_buffer->waitAndPopNewest();
        if (!frame) continue;

        // 把耗时转换任务丢给线程池，不阻塞消费线程
        MainWindow* self = this;
        m_pool->submit([self, frame]() {
            const QImage image = MainWindow::cvMatToQImage(*frame);
            if (!image.isNull()) {
                emit self->frameReady(image);
            }
        });
    }
}

QImage MainWindow::cvMatToQImage(const cv::Mat& mat)
{
    if (mat.empty()) return QImage();

    switch (mat.channels()) {
    case 1:
        return QImage(mat.data, mat.cols, mat.rows,
                      static_cast<int>(mat.step), QImage::Format_Grayscale8).copy();
    case 3: {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        return QImage(rgb.data, rgb.cols, rgb.rows,
                      static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
    }
    case 4: {
        cv::Mat rgba;
        cv::cvtColor(mat, rgba, cv::COLOR_BGRA2RGBA);
        return QImage(rgba.data, rgba.cols, rgba.rows,
                      static_cast<int>(rgba.step), QImage::Format_RGBA8888).copy();
    }
    default:
        return QImage();
    }
}

void MainWindow::onFrameReady(const QImage& image)
{
    if (image.isNull()) return;

    ui->previewLabel->setPixmap(QPixmap::fromImage(image).scaled(
        ui->previewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    // 帧率统计
    ++m_frameCount;
    if (!m_fpsTimer.isValid()) m_fpsTimer.start();
    const qint64 elapsed = m_fpsTimer.elapsed();
    if (elapsed >= 1000) {
        m_fps = m_frameCount * 1000.0 / elapsed;
        m_frameCount = 0;
        m_fpsTimer.restart();
        ui->statusLabel->setText(QStringLiteral("视频源: %1 | 帧率: %2 fps")
                                     .arg(m_source ? m_source->name() : QStringLiteral("-"))
                                     .arg(m_fps, 0, 'f', 1));
    }
}