#include "DatabaseManager.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>

DatabaseManager& DatabaseManager::instance()
{
    static DatabaseManager manager;
    return manager;
}

bool DatabaseManager::init(const QString& dbPath)
{
    if (m_initialized) return m_db.isOpen();

    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) {
        qWarning() << "打开数据库失败:" << m_db.lastError().text();
        return false;
    }

    if (!createTables()) {
        qWarning() << "创建数据表失败";
        m_db.close();
        return false;
    }

    m_initialized = true;
    qDebug() << "数据库初始化成功:" << dbPath;
    return true;
}

void DatabaseManager::shutdown()
{
    if (!m_initialized) return;
    m_db.close();
    const QString connectionName = m_db.connectionName();
    m_db = QSqlDatabase(); // 先释放句柄再移除连接
    QSqlDatabase::removeDatabase(connectionName);
    m_initialized = false;
}

bool DatabaseManager::isOpen() const
{
    return m_initialized && m_db.isOpen();
}

QSqlDatabase DatabaseManager::database() const
{
    return m_db;
}

DatabaseManager::~DatabaseManager()
{
    // 正常情况下由 shutdown() 清理；这里兜底避免连接残留
    if (m_initialized && m_db.isOpen()) {
        m_db.close();
    }
}

bool DatabaseManager::createTables()
{
    QSqlQuery query(m_db);

    // 人员库
    if (!query.exec("CREATE TABLE IF NOT EXISTS person ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "name TEXT NOT NULL,"
                    "face_feature BLOB,"
                    "remark TEXT,"
                    "created_at TEXT DEFAULT (datetime('now','localtime')))")) {
        qWarning() << "创建 person 表失败:" << query.lastError().text();
        return false;
    }

    // 录像文件
    if (!query.exec("CREATE TABLE IF NOT EXISTS video_file ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "source_name TEXT,"
                    "file_path TEXT NOT NULL,"
                    "start_time TEXT,"
                    "end_time TEXT,"
                    "event_type TEXT DEFAULT 'normal',"
                    "size_bytes INTEGER DEFAULT 0)")) {
        qWarning() << "创建 video_file 表失败:" << query.lastError().text();
        return false;
    }

    // 事件记录
    if (!query.exec("CREATE TABLE IF NOT EXISTS event ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "type TEXT NOT NULL,"
                    "source_name TEXT,"
                    "detail TEXT,"
                    "happened_at TEXT DEFAULT (datetime('now','localtime')))")) {
        qWarning() << "创建 event 表失败:" << query.lastError().text();
        return false;
    }

    return true;
}