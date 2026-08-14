#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QMutex>
#include <QSqlDatabase>
#include <QString>

// 数据库单例：负责连接 SQLite 并确保基础表存在
class DatabaseManager
{
public:
    static DatabaseManager& instance();

    // 初始化数据库（连接 + 建表），成功返回 true
    bool init(const QString& dbPath);

    // 关闭连接并移除，供程序退出前调用
    void shutdown();

    // 写入一条事件记录（type: online/offline/motion/stranger 等）
    bool addEvent(const QString& type, const QString& sourceName, const QString& detail);

    // 写入一条录像文件记录
    bool addVideoFile(const QString& sourceName, const QString& filePath,
                      const QString& startTime, const QString& endTime,
                      const QString& eventType, qint64 sizeBytes);

    // 按路径删除录像记录，并清理关联的 event 记录（事务）
    bool deleteVideoByPath(const QString& filePath);

    bool isOpen() const;
    QSqlDatabase database() const;

private:
    DatabaseManager() = default;
    ~DatabaseManager();
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    bool createTables();

    QSqlDatabase m_db;
    bool m_initialized = false;
    QMutex m_mutex;  // 保护所有数据库访问，允许采集线程跨线程调用
};

#endif // DATABASEMANAGER_H