#ifndef HISTORYDIALOG_H
#define HISTORYDIALOG_H

#include <QDialog>
#include <QImage>
#include <QVector>

#include "service/DatabaseManager.h"

class QComboBox;
class QPushButton;
class QTableWidget;
class QTabWidget;
class ThreadPool;

// 历史检索对话框：事件记录 + 录像文件两个页签
// 双击可打开回放窗口；录像文件页签用共享线程池异步生成首帧缩略图
class HistoryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HistoryDialog(ThreadPool* pool, QWidget* parent = nullptr);

private slots:
    void refreshEvents();
    void refreshVideos();
    void onEventDoubleClicked(int row, int column);
    void onVideoDoubleClicked(int row, int column);
    void onThumbnailReady(int row, const QImage& image);

signals:
    // 线程池 worker → 主线程（QueuedConnection）
    void thumbnailReady(int row, const QImage& image);

private:
    void openPlayback(const QString& filePath, const QString& title);

    ThreadPool* m_pool = nullptr;   // 共享线程池，仅用于生成缩略图
    QTabWidget* m_tabs = nullptr;
    QComboBox* m_eventTypeCombo = nullptr;
    QComboBox* m_videoTypeCombo = nullptr;
    QTableWidget* m_eventTable = nullptr;
    QTableWidget* m_videoTable = nullptr;
    QVector<DatabaseManager::VideoFileInfo> m_videos; // 表行 → 录像信息
};

#endif // HISTORYDIALOG_H