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

bool DatabaseManager::addEvent(const QString& type, const QString& sourceName, const QString& detail)
{
    if (!m_initialized) {
        qWarning() << "数据库未初始化，事件未写入:" << type << detail;
        return false;
    }

    QMutexLocker locker(&m_mutex);
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO event (type, source_name, detail) VALUES (?, ?, ?)");
    query.addBindValue(type);
    query.addBindValue(sourceName);
    query.addBindValue(detail);
    if (!query.exec()) {
        qWarning() << "写入事件失败:" << query.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::addVideoFile(const QString& sourceName, const QString& filePath,
                                   const QString& startTime, const QString& endTime,
                                   const QString& eventType, qint64 sizeBytes)
{
    if (!m_initialized) {
        qWarning() << "数据库未初始化，录像记录未写入:" << filePath;
        return false;
    }

    QMutexLocker locker(&m_mutex);
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO video_file (source_name, file_path, start_time, end_time, event_type, size_bytes) "
                  "VALUES (?, ?, ?, ?, ?, ?)");
    query.addBindValue(sourceName);
    query.addBindValue(filePath);
    query.addBindValue(startTime);
    query.addBindValue(endTime);
    query.addBindValue(eventType);
    query.addBindValue(sizeBytes);
    if (!query.exec()) {
        qWarning() << "写入录像记录失败:" << query.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::deleteVideoByPath(const QString& filePath)
{
    if (!m_initialized) return false;

    QMutexLocker locker(&m_mutex);
    QSqlQuery query(m_db);

    if (!m_db.transaction()) {
        qWarning() << "开启删除事务失败:" << m_db.lastError().text();
    }

    query.prepare("DELETE FROM video_file WHERE file_path = ?");
    query.addBindValue(filePath);
    if (!query.exec()) {
        qWarning() << "删除录像记录失败:" << query.lastError().text();
        m_db.rollback();
        return false;
    }

    // 事件记录中 motion / motion_end 的 detail 列存的是完整录像路径
    query.prepare("DELETE FROM event WHERE detail = ?");
    query.addBindValue(filePath);
    if (!query.exec()) {
        qWarning() << "删除关联事件记录失败:" << query.lastError().text();
        m_db.rollback();
        return false;
    }

    if (!m_db.commit()) {
        qWarning() << "提交删除事务失败:" << m_db.lastError().text();
        m_db.rollback();
        return false;
    }
    return true;
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