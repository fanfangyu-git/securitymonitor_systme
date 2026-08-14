#include "StorageManager.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>

#include "service/DatabaseManager.h"

int StorageManager::enforceQuota(const QString& dir, qint64 maxBytes, double keepRatio)
{
    if (maxBytes <= 0) return 0;       // 未启用上限
    if (!QDir(dir).exists()) return 0;  // 目录不存在，无需清理

    // 全局串行：多路采集线程可能同时触发清理，串行可避免重复删过头
    static QMutex s_mutex;
    QMutexLocker locker(&s_mutex);

    // 1. 收集 .avi，按文件名(时间序)升序：文件名形如 ch0_20260813_214641.avi，字典序即时间序
    const QFileInfoList files = QDir(dir).entryInfoList(
        QStringList() << "*.avi", QDir::Files, QDir::Name);
    if (files.isEmpty()) return 0;

    // 2. 计算总大小
    qint64 total = 0;
    for (const QFileInfo& fi : files) total += fi.size();

    const qint64 target = static_cast<qint64>(maxBytes * keepRatio);
    if (total <= target) return 0;  // 未超阈值，不删

    // 3. 从最旧(列表头)开始删除，直到 <= target
    int removed = 0;
    for (const QFileInfo& fi : files) {
        if (total <= target) break;
        const QString path = fi.absoluteFilePath();
        if (QFile::remove(path)) {
            total -= fi.size();
            ++removed;
            // 同步清理数据库中的录像与事件记录（按完整路径）
            DatabaseManager::instance().deleteVideoByPath(path);
        }
    }

    if (removed > 0) {
        qInfo() << "[StorageManager] 录像超限清理:" << removed << "个文件，目录" << dir;
    }
    return removed;
}
