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
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QTime>
#include <QTimer>

#include "core/CameraSource.h"
#include "core/FileSource.h"
#include "core/StorageManager.h"
#include "core/SyntheticSource.h"
#include "service/DatabaseManager.h"
#include "AdminLoginDialog.h"
#include "HistoryDialog.h"
#include "PlaybackWindow.h"

#include <QDesktopServices>
#include <QListWidgetItem>
#include <QUrl>
#include <QVariantMap>

MainWindow::MainWindow(bool testMode, const QStringList& fileSources,
                       const QString& recordDir, const QString& faceDir,
                       qint64 maxStorageBytes, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_testMode(testMode)
    , m_fileSources(fileSources)
    , m_recordDir(recordDir)
    , m_faceDir(faceDir)
    , m_maxStorageBytes(maxStorageBytes)
{
    ui->setupUi(this);

    updateAdminStatus(); // 管理员状态提示（初始未登录，登记人脸需先验证）

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
    m_pool.reset(new ThreadPool(8)); // 人脸识别并行任务多，6→8 提升多路吞吐

    // 启动前清理：按存储上限删除过旧录像及其数据库记录
    StorageManager::enforceQuota(recordDirectory(), m_maxStorageBytes, 0.8);

    // 存储容量兜底：每 30 分钟检查一次，防止长时间连续录像时上限一直不生效
    m_quotaTimer = new QTimer(this);
    m_quotaTimer->setInterval(30 * 60 * 1000);
    connect(m_quotaTimer, &QTimer::timeout, this, &MainWindow::onQuotaCheckTimeout);
    m_quotaTimer->start();

    // 人脸识别：加载模型 + 人员库（模型目录默认 exe 同级 models/，可用 --face-dir 指定）
    m_faceAnalyzer.reset(new FaceAnalyzer());
    QString modelDir = m_faceDir;
    if (modelDir.isEmpty()) {
        modelDir = QCoreApplication::applicationDirPath() + "/models";
    }
    m_faceReady = m_faceAnalyzer->loadModels(modelDir);
    if (m_faceReady) {
        std::vector<FaceAnalyzer::Person> persons;
        const auto infos = DatabaseManager::instance().loadPersons();
        for (const auto& info : infos) {
            FaceAnalyzer::Person p;
            p.id = info.id;
            p.name = info.name;
            p.feature = FaceAnalyzer::bytesToFeature(info.feature);
            if (!p.feature.empty()) persons.push_back(p);
        }
        m_faceAnalyzer->loadPersons(persons);
        ui->statusLabel->setText(QStringLiteral("人脸识别就绪（%1 人）").arg(persons.size()));
    } else {
        ui->faceCheckBox->setEnabled(false);
        appendEvent(QStringLiteral("人脸模型未找到（%1），人脸识别已禁用").arg(modelDir),
                    QStringLiteral("face_error"),
                    QStringLiteral("系统"), modelDir);
    }

    connect(ui->registerButton, &QPushButton::clicked,
            this, &MainWindow::onRegisterClicked);

    connect(ui->eventList, &QListWidget::itemDoubleClicked,
            this, &MainWindow::onEventItemDoubleClicked);
    connect(ui->historyButton, &QPushButton::clicked,
            this, &MainWindow::onHistoryClicked);
}

QString MainWindow::recordDirectory() const
{
    return m_recordDir.isEmpty()
        ? QCoreApplication::applicationDirPath() + "/records"
        : m_recordDir;
}

void MainWindow::onQuotaCheckTimeout()
{
    // 兜底检查：容量超限时从最旧录像开始清理（内部有互斥锁，多线程调用安全）
    StorageManager::enforceQuota(recordDirectory(), m_maxStorageBytes, 0.8);
}

MainWindow::~MainWindow()
{
    stopPreview();
    // 关键：先回收线程池，确保残留的在途任务（转 QImage / 人脸识别）在
    // m_faceAnalyzer 与通道对象销毁前全部执行完；stop() 会丢弃未开始的任务，
    // 只等当前正在执行的任务收尾，避免 worker 访问已销毁对象导致堆损坏/卡顿
    m_pool->stop();
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
    ui->faceCheckBox->setEnabled(false);
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
    ui->faceCheckBox->setEnabled(m_faceReady);
    ui->statusLabel->setText(QStringLiteral("已停止"));
}

void MainWindow::setupChannels(int count)
{
    clearChannels();

    const bool motionEnabled = ui->motionCheckBox->isChecked();
    const QString recordDir = recordDirectory();

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

        const bool faceEnabled = m_faceReady && ui->faceCheckBox->isChecked();
        auto channel = std::unique_ptr<PreviewChannel>(new PreviewChannel(
            i, std::move(source), m_pool.get(), recordDir, motionEnabled,
            m_faceAnalyzer.get(), faceEnabled, m_maxStorageBytes, this));
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
        connect(channel.get(), &PreviewChannel::faceInfo,
                this, &MainWindow::onFaceInfo, Qt::QueuedConnection);
        connect(channel.get(), &PreviewChannel::faceBoxes,
                this, &MainWindow::onFaceBoxes, Qt::QueuedConnection);
        connect(channel.get(), &PreviewChannel::strangerCapture,
                this, &MainWindow::onStrangerCapture, Qt::QueuedConnection);
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

    m_faceBoxes.resize(count); // 每路一份人脸框缓存
}

void MainWindow::clearChannels()
{
    for (auto& channel : m_channels) {
        channel->stop();
    }
    // 等待线程池把已提交任务执行完再销毁通道，防止任务访问已释放的通道对象
    m_pool->drain();
    m_channels.clear();

    while (QLayoutItem* item = ui->previewGridLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    m_previewLabels.clear();
    m_faceBoxes.clear();
}

void MainWindow::onChannelFrameReady(int channelId, const QImage& image)
{
    if (channelId < 0 || channelId >= m_previewLabels.size()) return;
    if (image.isNull()) return;

    // 按内容区缩放（录像时红框边框不占用画面、不影响布局）
    QSize target = m_previewLabels[channelId]->contentsRect().size();
    if (target.isEmpty()) target = m_previewLabels[channelId]->size();
    QPixmap pix = QPixmap::fromImage(image).scaled(
        target, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // 叠加最近一次人脸框：绿框=已录人员+姓名，红框=stranger，黄框=识别中
    // （坐标按显示缩放比例换算；pending 为身份判定中，不作陌生人处理）
    if (channelId < m_faceBoxes.size() && !m_faceBoxes[channelId].isEmpty()) {
        const double sx = pix.width() / static_cast<double>(image.width());
        const double sy = pix.height() / static_cast<double>(image.height());
        QPainter painter(&pix);
        for (const FaceBox& b : m_faceBoxes[channelId]) {
            const QColor color = b.pending ? QColor(255, 200, 0)
                                 : b.known  ? QColor(0, 255, 0)
                                            : QColor(255, 0, 0);
            painter.setPen(QPen(color, 3));
            painter.drawRect(QRectF(b.x * sx, b.y * sy, b.w * sx, b.h * sy));
            painter.setPen(QPen(color));
            QFont f = painter.font();
            f.setPixelSize(qMax(12, qRound(22 * qMin(sx, sy))));
            f.setBold(true);
            painter.setFont(f);
            const QString label = b.pending ? QStringLiteral("识别中")
                                 : b.known   ? b.name
                                             : QStringLiteral("stranger");
            painter.drawText(QPointF(b.x * sx, qMax(12.0, b.y * sy - 6)), label);
        }
        painter.end();
    }

    m_previewLabels[channelId]->setPixmap(pix);

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
    QListWidgetItem* item = new QListWidgetItem(line);
    item->setToolTip(line); // 长文本省略时鼠标悬停查看完整内容
    item->setData(Qt::UserRole, QVariantMap{
        {QStringLiteral("type"), type},
        {QStringLiteral("detail"), detail}});
    ui->eventList->insertItem(0, item);
    DatabaseManager::instance().addEvent(type, sourceName, detail);
}

void MainWindow::onFaceInfo(int channelId, const QString& name, bool known,
                           double distance, const QByteArray& feature)
{
    if (!feature.isEmpty()) m_lastFaceFeature = feature; // 供"登记人脸"使用

    if (known) {
        appendEvent(QStringLiteral("第 %1 路 识别到: %2（相似度 %3）")
                        .arg(channelId + 1).arg(name)
                        .arg(QString::asprintf("%.3f", distance)),
                    QStringLiteral("known"),
                    QStringLiteral("第 %1 路").arg(channelId + 1),
                    name);
    } else {
        appendEvent(QStringLiteral("第 %1 路 检测到陌生人！").arg(channelId + 1),
                    QStringLiteral("stranger"),
                    QStringLiteral("第 %1 路").arg(channelId + 1),
                    QStringLiteral("stranger"));
    }
}

void MainWindow::onFaceBoxes(int channelId, const QVector<FaceBox>& boxes)
{
    if (channelId < 0) return;
    if (channelId >= m_faceBoxes.size()) m_faceBoxes.resize(channelId + 1);
    m_faceBoxes[channelId] = boxes;
}

void MainWindow::onRegisterClicked()
{
    // 管理员权限：登记人脸需先通过管理员验证（本会话内有效，重启后需重新登录）
    if (!m_adminAuthed) {
        if (!AdminLoginDialog::execLogin(this)) return;
        m_adminAuthed = true;
        updateAdminStatus();
    }

    const QString name = ui->nameEdit->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("请先在上方输入要登记的姓名。"));
        return;
    }
    if (m_lastFaceFeature.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("暂无人脸特征：请开启人脸识别，并让画面中有人脸后再登记。"));
        return;
    }

    int id = -1;
    if (!DatabaseManager::instance().addPerson(name, m_lastFaceFeature, &id)) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("人员登记失败（数据库错误）。"));
        return;
    }

    FaceAnalyzer::Person p;
    p.id = id;
    p.name = name;
    p.feature = FaceAnalyzer::bytesToFeature(m_lastFaceFeature);
    m_faceAnalyzer->addPerson(p);

    appendEvent(QStringLiteral("已登记人脸: %1（编号 %2）").arg(name).arg(id),
                QStringLiteral("register"),
                QStringLiteral("系统"), name);
    ui->nameEdit->clear();
    ui->statusLabel->setText(QStringLiteral("人脸识别就绪（%1 人）").arg(m_faceAnalyzer->personCount()));
}

void MainWindow::onEventItemDoubleClicked(QListWidgetItem* item)
{
    if (!item) return;
    const QVariantMap data = item->data(Qt::UserRole).toMap();
    const QString detail = data.value(QStringLiteral("detail")).toString();
    const QString type = data.value(QStringLiteral("type")).toString();
    if (detail.endsWith(QStringLiteral(".avi"), Qt::CaseInsensitive) &&
        QFileInfo::exists(detail)) {
        auto* win = new PlaybackWindow(detail,
            QStringLiteral("回放：%1（%2 事件）").arg(QFileInfo(detail).fileName()).arg(type),
            this);
        win->show();
    } else if (detail.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive) &&
               QFileInfo::exists(detail)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(detail)); // 截图直接用系统看图打开
    } else {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("该事件没有关联录像文件。"));
    }
}

void MainWindow::onHistoryClicked()
{
    // 非模态独立窗口：模态 exec 会禁用主程序所有窗口，导致从其中弹出的回放窗口无法交互、关闭后回放落到主窗口后面
    if (m_historyDlg) {
        m_historyDlg->raise();
        m_historyDlg->activateWindow();
        return;
    }
    auto* dlg = new HistoryDialog(m_pool.get(), this);
    m_historyDlg = dlg;
    connect(dlg, &QObject::destroyed, this, [this]() { m_historyDlg = nullptr; });
    dlg->show();
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
void MainWindow::onStrangerCapture(int channelId, const QString& filePath)
{
    appendEvent(QStringLiteral("第 %1 路 陌生人截图: %2")
                    .arg(channelId + 1).arg(QFileInfo(filePath).fileName()),
                QStringLiteral("stranger_capture"),
                QStringLiteral("第 %1 路").arg(channelId + 1),
                filePath);
}

void MainWindow::updateAdminStatus()
{
    if (!ui->adminStatusLabel) return;
    if (m_adminAuthed) {
        ui->adminStatusLabel->setText(QStringLiteral("管理员：已登录"));
        ui->adminStatusLabel->setStyleSheet(QStringLiteral("color: #2e7d32;"));
    } else {
        ui->adminStatusLabel->setText(QStringLiteral("管理员：未登录"));
        ui->adminStatusLabel->setStyleSheet(QStringLiteral("color: #c0504d;"));
    }
}
