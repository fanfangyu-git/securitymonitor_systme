#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <cmath>

#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QLabel>
#include <QLayout>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QTime>

#include "core/CameraSource.h"
#include "core/FileSource.h"
#include "core/StorageManager.h"
#include "core/SyntheticSource.h"
#include "service/DatabaseManager.h"

MainWindow::MainWindow(bool testMode, const QStringList& fileSources,
                       const QString& recordDir, qint64 maxStorageBytes, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_testMode(testMode)
    , m_fileSources(fileSources)
    , m_recordDir(recordDir)
    , m_maxStorageBytes(maxStorageBytes)
{
    ui->setupUi(this);

    // 路数下拉框数据：0->1路 1->2路 2->4路
    ui->channelCombo->setItemData(0, 1);
    ui->channelCombo->setItemData(1, 2);
    ui->channelCombo->setItemData(2, 4);

    ui->eventList->addItem(QStringLiteral("（事件列表：第N路上线/掉线/运动录像）"));

    // 预览区优先占据多余空间，事件列表保持固定宽度（避免启动时被挤成窄条）
    ui->splitter->setStretchFactor(0, 1);
    ui->splitter->setStretchFactor(1, 0);

    connect(ui->startStopButton, &QPushButton::clicked,
            this, &MainWindow::onStartStopClicked);

    // 共享线程池：所有路共用
    m_pool.reset(new ThreadPool(4));

    // 启动前清理：按存储上限删除过旧录像及其数据库记录
    const QString dir = m_recordDir.isEmpty()
        ? QCoreApplication::applicationDirPath() + "/records"
        : m_recordDir;
    StorageManager::enforceQuota(dir, m_maxStorageBytes, 0.8);
}

MainWindow::~MainWindow()
{
    stopPreview();
    delete ui;
}

void MainWindow::autoStart()
{
    onStartStopClicked();
}

void MainWindow::setChannelCount(int count)
{
    if (count <= 1) {
        ui->channelCombo->setCurrentIndex(0);
    } else if (count <= 2) {
        ui->channelCombo->setCurrentIndex(1);
    } else {
        ui->channelCombo->setCurrentIndex(2);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    stopPreview();
    QMainWindow::closeEvent(event);
}

void MainWindow::onStartStopClicked()
{
    if (m_previewRunning) {
        stopPreview();
        ui->startStopButton->setText(QStringLiteral("启动预览"));
    } else {
        if (startPreview()) {
            ui->startStopButton->setText(QStringLiteral("停止预览"));
        }
    }
}

bool MainWindow::startPreview()
{
    if (m_previewRunning) return true;

    const int count = ui->channelCombo->currentData().toInt();
    setupChannels(count);
    if (m_channels.empty()) return false;

    int okCount = 0;
    for (auto& channel : m_channels) {
        if (channel->start()) ++okCount;
    }

    if (okCount == 0) {
        stopPreview();
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("所有视频源均无法打开，请检查摄像头或改用 --test 参数。"));
        return false;
    }

    m_previewRunning = true;
    m_runningCount = okCount;
    ui->startStopButton->setText(QStringLiteral("停止预览"));
    ui->channelCombo->setEnabled(false);
    ui->motionCheckBox->setEnabled(false);
    ui->statusLabel->setText(QStringLiteral("运行中: %1 路 | 帧率: - fps").arg(okCount));
    return true;
}

void MainWindow::stopPreview()
{
    for (auto& channel : m_channels) {
        channel->stop();
    }
    m_previewRunning = false;
    m_runningCount = 0;

    for (int i = 0; i < m_previewLabels.size(); ++i) {
        setLabelRecording(i, false);
        m_previewLabels[i]->setText(QStringLiteral("第 %1 路（已停止）").arg(i + 1));
    }

    ui->startStopButton->setText(QStringLiteral("启动预览"));
    ui->channelCombo->setEnabled(true);
    ui->motionCheckBox->setEnabled(true);
    ui->statusLabel->setText(QStringLiteral("已停止"));
}

void MainWindow::setupChannels(int count)
{
    clearChannels();

    const bool motionEnabled = ui->motionCheckBox->isChecked();
    const QString recordDir = m_recordDir.isEmpty()
        ? QCoreApplication::applicationDirPath() + "/records"
        : m_recordDir;

    for (int i = 0; i < count; ++i) {
        // 视频源分配：第1路摄像头，其余优先 --file 指定文件，否则测试源
        std::unique_ptr<VideoSource> source;
        if (i == 0) {
            if (m_testMode) {
                source.reset(new SyntheticSource());
            } else {
                source.reset(new CameraSource(0));
            }
        } else if (i - 1 < m_fileSources.size()) {
            source.reset(new FileSource(m_fileSources.at(i - 1)));
        } else {
            source.reset(new SyntheticSource());
        }

        auto channel = std::unique_ptr<PreviewChannel>(new PreviewChannel(
            i, std::move(source), m_pool.get(), recordDir, motionEnabled,
            m_maxStorageBytes, this));
        connect(channel.get(), &PreviewChannel::frameReady,
                this, &MainWindow::onChannelFrameReady, Qt::QueuedConnection);
        connect(channel.get(), &PreviewChannel::channelOnline,
                this, &MainWindow::onChannelOnline, Qt::QueuedConnection);
        connect(channel.get(), &PreviewChannel::channelOffline,
                this, &MainWindow::onChannelOffline, Qt::QueuedConnection);
        connect(channel.get(), &PreviewChannel::recordingStarted,
                this, &MainWindow::onRecordingStarted, Qt::QueuedConnection);
        connect(channel.get(), &PreviewChannel::recordingStopped,
                this, &MainWindow::onRecordingStopped, Qt::QueuedConnection);
        m_channels.push_back(std::move(channel));

        // 每路一个预览标签（动态创建：路数可变，由数据驱动）
        QLabel* label = new QLabel(ui->previewGridWidget);
        label->setAlignment(Qt::AlignCenter);
        // 关键：忽略 sizeHint（画面/红框边框都不会再撑大 label），预览格始终填满所在格子
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        label->setStyleSheet(QStringLiteral("background-color: #101010; color: #888888; font-size: 14px;"));
        label->setText(QStringLiteral("第 %1 路").arg(i + 1));
        m_previewLabels.append(label);
    }

    // 网格排布：列数 = ceil(sqrt(N))，行数 = ceil(N/列数)
    const int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
    const int rows = (count + cols - 1) / cols;
    for (int i = 0; i < m_previewLabels.size(); ++i) {
        ui->previewGridLayout->addWidget(m_previewLabels[i], i / cols, i % cols);
    }
    for (int c = 0; c < cols; ++c) ui->previewGridLayout->setColumnStretch(c, 1);
    for (int r = 0; r < rows; ++r) ui->previewGridLayout->setRowStretch(r, 1);
}

void MainWindow::clearChannels()
{
    for (auto& channel : m_channels) {
        channel->stop();
    }
    m_channels.clear();

    while (QLayoutItem* item = ui->previewGridLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    m_previewLabels.clear();
}

void MainWindow::onChannelFrameReady(int channelId, const QImage& image)
{
    if (channelId < 0 || channelId >= m_previewLabels.size()) return;
    if (image.isNull()) return;

    // 按内容区缩放（录像时红框边框不占用画面、不影响布局）
    QSize target = m_previewLabels[channelId]->contentsRect().size();
    if (target.isEmpty()) target = m_previewLabels[channelId]->size();
    m_previewLabels[channelId]->setPixmap(QPixmap::fromImage(image).scaled(
        target, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    // 总帧率统计（所有路合计）
    ++m_frameCount;
    if (!m_fpsTimer.isValid()) m_fpsTimer.start();
    const qint64 elapsed = m_fpsTimer.elapsed();
    if (elapsed >= 1000) {
        m_fps = m_frameCount * 1000.0 / elapsed;
        m_frameCount = 0;
        m_fpsTimer.restart();
        ui->statusLabel->setText(QStringLiteral("运行中: %1 路 | 帧率: %2 fps")
                                     .arg(m_runningCount).arg(m_fps, 0, 'f', 1));
    }
}

void MainWindow::onChannelOnline(int channelId, const QString& sourceName)
{
    if (m_runningCount < static_cast<int>(m_channels.size())) ++m_runningCount;
    appendEvent(QStringLiteral("第 %1 路上线: %2").arg(channelId + 1).arg(sourceName),
                QStringLiteral("online"),
                QStringLiteral("第 %1 路").arg(channelId + 1),
                sourceName);
}

void MainWindow::onChannelOffline(int channelId, const QString& reason)
{
    if (m_runningCount > 0) --m_runningCount;
    appendEvent(QStringLiteral("第 %1 路不可用: %2").arg(channelId + 1).arg(reason),
                QStringLiteral("offline"),
                QStringLiteral("第 %1 路").arg(channelId + 1),
                reason);
}

void MainWindow::onRecordingStarted(int channelId, const QString& filePath)
{
    setLabelRecording(channelId, true);
    appendEvent(QStringLiteral("第 %1 路 检测到运动，开始录像: %2")
                    .arg(channelId + 1).arg(QFileInfo(filePath).fileName()),
                QStringLiteral("motion"),
                QStringLiteral("第 %1 路").arg(channelId + 1),
                filePath);
}

void MainWindow::onRecordingStopped(int channelId, const QString& filePath, qint64 durationMs)
{
    setLabelRecording(channelId, false);

    const QString durationText = QString::number(durationMs / 1000.0, 'f', 1) + QStringLiteral(" 秒");
    appendEvent(QStringLiteral("第 %1 路 录像结束: %2（%3）")
                    .arg(channelId + 1).arg(QFileInfo(filePath).fileName()).arg(durationText),
                QStringLiteral("motion_end"),
                QStringLiteral("第 %1 路").arg(channelId + 1),
                filePath);

    // 写入录像文件登记表
    const QDateTime end = QDateTime::currentDateTime();
    const QDateTime start = end.addMSecs(-durationMs);
    DatabaseManager::instance().addVideoFile(
        (channelId >= 0 && channelId < static_cast<int>(m_channels.size()))
            ? m_channels[channelId]->sourceName() : QStringLiteral("-"),
        filePath,
        start.toString("yyyy-MM-dd HH:mm:ss"),
        end.toString("yyyy-MM-dd HH:mm:ss"),
        QStringLiteral("motion"),
        QFileInfo(filePath).size());
}

void MainWindow::appendEvent(const QString& text, const QString& type,
                             const QString& sourceName, const QString& detail)
{
    const QString line = QStringLiteral("[%1] %2")
                             .arg(QTime::currentTime().toString("HH:mm:ss"))
                             .arg(text);
    ui->eventList->insertItem(0, line);
    DatabaseManager::instance().addEvent(type, sourceName, detail);
}

void MainWindow::setLabelRecording(int channelId, bool recording)
{
    if (channelId < 0 || channelId >= m_previewLabels.size()) return;
    if (recording) {
        m_previewLabels[channelId]->setStyleSheet(
            QStringLiteral("background-color: #101010; color: #888888; font-size: 14px;"
                           "border: 3px solid #ff3333;"));
    } else {
        m_previewLabels[channelId]->setStyleSheet(
            QStringLiteral("background-color: #101010; color: #888888; font-size: 14px;"));
    }
}