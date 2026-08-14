#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

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
};

#endif // DATABASEMANAGER_H