#include "HistoryDialog.h"

#include <QComboBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPixmap>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QVBoxLayout>

#include <opencv2/videoio.hpp>

#include "core/ImageConvert.h"
#include "core/ThreadPool.h"
#include "PlaybackWindow.h"

HistoryDialog::HistoryDialog(ThreadPool* pool, QWidget* parent)
    : QDialog(parent)
    , m_pool(pool)
{
    setWindowTitle(QStringLiteral("历史检索与回放"));
    // 本对话框在 MainWindow 中以堆对象 + show() 非模态方式使用，
    // WA_DeleteOnClose 由 Qt 在关闭时 deleteLater 回收（不能配栈对象/exec 使用）
    setAttribute(Qt::WA_DeleteOnClose);
    resize(860, 560);

    m_tabs = new QTabWidget(this);

    // ---------- 事件记录页 ----------
    auto* eventPage = new QWidget(this);
    m_eventTypeCombo = new QComboBox(eventPage);
    m_eventTypeCombo->addItem(QStringLiteral("全部事件"), QString());
    const QStringList types = {
        QStringLiteral("online"), QStringLiteral("offline"),
        QStringLiteral("motion"), QStringLiteral("motion_end"),
        QStringLiteral("stranger"), QStringLiteral("stranger_capture"), QStringLiteral("known"),
        QStringLiteral("register"), QStringLiteral("face_error")
    };
    for (const QString& t : types) m_eventTypeCombo->addItem(t, t);

    auto* refreshEventsBtn = new QPushButton(QStringLiteral("刷新"), eventPage);
    connect(refreshEventsBtn, &QPushButton::clicked, this, &HistoryDialog::refreshEvents);
    connect(m_eventTypeCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &HistoryDialog::refreshEvents);

    m_eventTable = new QTableWidget(eventPage);
    m_eventTable->setColumnCount(4);
    m_eventTable->setHorizontalHeaderLabels({ QStringLiteral("时间"), QStringLiteral("类型"),
                                              QStringLiteral("通道"), QStringLiteral("详情") });
    m_eventTable->horizontalHeader()->setStretchLastSection(true);
    m_eventTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_eventTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_eventTable->setAlternatingRowColors(true);
    m_eventTable->setTextElideMode(Qt::ElideNone); // 长文本不省略，靠拖动列宽查看
    m_eventTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    connect(m_eventTable, &QTableWidget::cellDoubleClicked,
            this, &HistoryDialog::onEventDoubleClicked);

    auto* eventBar = new QHBoxLayout;
    eventBar->addWidget(new QLabel(QStringLiteral("事件类型:"), eventPage));
    eventBar->addWidget(m_eventTypeCombo);
    eventBar->addStretch(1);
    eventBar->addWidget(refreshEventsBtn);

    auto* eventLayout = new QVBoxLayout(eventPage);
    eventLayout->addLayout(eventBar);
    eventLayout->addWidget(m_eventTable, 1);
    m_tabs->addTab(eventPage, QStringLiteral("事件记录"));

    // ---------- 录像文件页 ----------
    auto* videoPage = new QWidget(this);
    m_videoTypeCombo = new QComboBox(videoPage);
    m_videoTypeCombo->addItem(QStringLiteral("全部类型"), QString());
    m_videoTypeCombo->addItem(QStringLiteral("motion"), QStringLiteral("motion"));
    m_videoTypeCombo->addItem(QStringLiteral("normal"), QStringLiteral("normal"));

    auto* refreshVideosBtn = new QPushButton(QStringLiteral("刷新"), videoPage);
    connect(refreshVideosBtn, &QPushButton::clicked, this, &HistoryDialog::refreshVideos);
    connect(m_videoTypeCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &HistoryDialog::refreshVideos);

    m_videoTable = new QTableWidget(videoPage);
    m_videoTable->setColumnCount(6);
    m_videoTable->setHorizontalHeaderLabels({ QStringLiteral("缩略图"), QStringLiteral("开始时间"),
                                              QStringLiteral("结束时间"), QStringLiteral("通道"),
                                              QStringLiteral("类型"), QStringLiteral("大小") });
    m_videoTable->horizontalHeader()->setStretchLastSection(true);
    m_videoTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_videoTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_videoTable->setAlternatingRowColors(true);
    m_videoTable->setTextElideMode(Qt::ElideNone); // 长文本不省略，靠拖动列宽查看
    m_videoTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_videoTable->verticalHeader()->setDefaultSectionSize(80);
    connect(m_videoTable, &QTableWidget::cellDoubleClicked,
            this, &HistoryDialog::onVideoDoubleClicked);

    auto* videoBar = new QHBoxLayout;
    videoBar->addWidget(new QLabel(QStringLiteral("录像类型:"), videoPage));
    videoBar->addWidget(m_videoTypeCombo);
    videoBar->addStretch(1);
    videoBar->addWidget(refreshVideosBtn);

    auto* videoLayout = new QVBoxLayout(videoPage);
    videoLayout->addLayout(videoBar);
    videoLayout->addWidget(m_videoTable, 1);
    m_tabs->addTab(videoPage, QStringLiteral("录像文件"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs, 1);
    auto* tip = new QLabel(QStringLiteral("提示：双击带录像的行可打开回放窗口"), this);
    tip->setStyleSheet(QStringLiteral("color: #666666;"));
    layout->addWidget(tip);

    // 缩略图信号（跨线程队列连接）
    connect(this, &HistoryDialog::thumbnailReady,
            this, &HistoryDialog::onThumbnailReady);

    refreshEvents();
    refreshVideos();
}

void HistoryDialog::refreshEvents()
{
    const QString type = m_eventTypeCombo->currentData().toString();
    const auto events = DatabaseManager::instance().queryEvents(type);
    m_eventTable->setRowCount(0);
    m_eventTable->setRowCount(static_cast<int>(events.size()));
    for (int i = 0; i < events.size(); ++i) {
        const auto& e = events.at(i);
        m_eventTable->setItem(i, 0, new QTableWidgetItem(e.happenedAt));
        m_eventTable->setItem(i, 1, new QTableWidgetItem(e.type));
        m_eventTable->setItem(i, 2, new QTableWidgetItem(e.sourceName));
        m_eventTable->setItem(i, 3, new QTableWidgetItem(e.detail));
        for (int col = 0; col < 4; ++col) { // 悬停显示完整内容
            if (QTableWidgetItem* it = m_eventTable->item(i, col)) it->setToolTip(it->text());
        }
    }
}

void HistoryDialog::refreshVideos()
{
    const QString type = m_videoTypeCombo->currentData().toString();
    m_videos = DatabaseManager::instance().queryVideoFiles(type);
    m_videoTable->setRowCount(0);
    m_videoTable->setRowCount(m_videos.size());
    for (int i = 0; i < m_videos.size(); ++i) {
        const auto& v = m_videos.at(i);
        m_videoTable->setItem(i, 1, new QTableWidgetItem(v.startTime));
        m_videoTable->setItem(i, 2, new QTableWidgetItem(v.endTime));
        m_videoTable->setItem(i, 3, new QTableWidgetItem(v.sourceName));
        m_videoTable->setItem(i, 4, new QTableWidgetItem(v.eventType));
        const QString sizeText = v.sizeBytes > 0
            ? QString::number(v.sizeBytes / 1024.0 / 1024.0, 'f', 1) + QStringLiteral(" MB")
            : QStringLiteral("-");
        m_videoTable->setItem(i, 5, new QTableWidgetItem(sizeText));
        m_videoTable->setItem(i, 0, new QTableWidgetItem(QStringLiteral("生成中…")));
        for (int col = 1; col < 6; ++col) { // 悬停显示完整内容
            if (QTableWidgetItem* it = m_videoTable->item(i, col)) it->setToolTip(it->text());
        }

        // 异步生成首帧缩略图（共享线程池，QPointer 防窗口关闭后悬挂）
        const int row = i;
        const QString path = v.filePath;
        QPointer<HistoryDialog> guard(this);
        if (m_pool) {
            m_pool->submit([guard, row, path]() {
                QImage thumb;
                cv::VideoCapture cap(path.toLocal8Bit().constData());
                cv::Mat frame;
                if (cap.isOpened() && cap.read(frame)) thumb = matToQImage(frame);
                if (guard) emit guard->thumbnailReady(row, thumb);
            });
        }
    }
}

void HistoryDialog::onEventDoubleClicked(int row, int column)
{
    Q_UNUSED(column);
    const QString detail = m_eventTable->item(row, 3)
        ? m_eventTable->item(row, 3)->text() : QString();
    if (!detail.endsWith(QStringLiteral(".avi"), Qt::CaseInsensitive) ||
        !QFileInfo::exists(detail)) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("该事件没有关联录像文件。"));
        return;
    }
    openPlayback(detail, QStringLiteral("回放：%1").arg(QFileInfo(detail).fileName()));
}

void HistoryDialog::onVideoDoubleClicked(int row, int column)
{
    Q_UNUSED(column);
    if (row < 0 || row >= m_videos.size()) return;
    const QString path = m_videos.at(row).filePath;
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("录像文件不存在：%1").arg(path));
        return;
    }
    openPlayback(path, QStringLiteral("回放：%1").arg(QFileInfo(path).fileName()));
}

void HistoryDialog::onThumbnailReady(int row, const QImage& image)
{
    if (row < 0 || row >= m_videoTable->rowCount()) return;
    if (image.isNull()) {
        m_videoTable->setItem(row, 0, new QTableWidgetItem(QStringLiteral("无预览")));
        return;
    }
    QPixmap pix = QPixmap::fromImage(image).scaled(
        120, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QTableWidgetItem* item = new QTableWidgetItem;
    item->setIcon(QIcon(pix));
    item->setToolTip(m_videos.value(row).filePath);
    m_videoTable->setItem(row, 0, item);
}

void HistoryDialog::openPlayback(const QString& filePath, const QString& title)
{
    // 无父窗口顶层窗口：关闭历史检索时不影响正在回放的窗口
    auto* win = new PlaybackWindow(filePath, title);
    win->show();
}