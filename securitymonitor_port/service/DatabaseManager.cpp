#include "DatabaseManager.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <QCryptographicHash>
#include <QRandomGenerator>

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

    // 确保管理员账号存在（首次运行写入默认 admin/admin123）
    if (!ensureDefaultAdmin()) {
        qWarning() << "初始化默认管理员失败";
    }

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

QVector<DatabaseManager::PersonInfo> DatabaseManager::loadPersons()
{
    QVector<PersonInfo> persons;
    if (!m_initialized) return persons;

    QMutexLocker locker(&m_mutex);
    QSqlQuery query(m_db);
    if (!query.exec("SELECT id, name, face_feature FROM person ORDER BY id")) {
        qWarning() << "读取人员库失败:" << query.lastError().text();
        return persons;
    }
    while (query.next()) {
        PersonInfo p;
        p.id = query.value(0).toInt();
        p.name = query.value(1).toString();
        p.feature = query.value(2).toByteArray();
        persons.push_back(p);
    }
    return persons;
}

bool DatabaseManager::addPerson(const QString& name, const QByteArray& feature, int* outId)
{
    if (!m_initialized) {
        qWarning() << "数据库未初始化，人员登记失败:" << name;
        return false;
    }

    QMutexLocker locker(&m_mutex);
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO person (name, face_feature) VALUES (?, ?)");
    query.addBindValue(name);
    query.addBindValue(feature);
    if (!query.exec()) {
        qWarning() << "人员登记失败:" << query.lastError().text();
        return false;
    }
    if (outId) *outId = query.lastInsertId().toInt();
    return true;
}

bool DatabaseManager::updatePerson(int id, const QString& name, const QByteArray& feature)
{
    if (!m_initialized) {
        qWarning() << "数据库未初始化，人员更新失败:" << name;
        return false;
    }

    QMutexLocker locker(&m_mutex);
    QSqlQuery query(m_db);
    query.prepare("UPDATE person SET name = ?, face_feature = ? WHERE id = ?");
    query.addBindValue(name);
    query.addBindValue(feature);
    query.addBindValue(id);
    if (!query.exec()) {
        qWarning() << "人员更新失败:" << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() > 0;
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

QVector<DatabaseManager::EventInfo> DatabaseManager::queryEvents(
    const QString& type, const QString& startTime, const QString& endTime)
{
    QVector<EventInfo> events;
    if (!m_initialized) return events;

    QMutexLocker locker(&m_mutex);
    QString sql = QStringLiteral("SELECT id, type, source_name, detail, happened_at FROM event WHERE 1=1");
    if (!type.isEmpty()) sql += QStringLiteral(" AND type = ?");
    if (!startTime.isEmpty()) sql += QStringLiteral(" AND happened_at >= ?");
    if (!endTime.isEmpty()) sql += QStringLiteral(" AND happened_at <= ?");
    sql += QStringLiteral(" ORDER BY id DESC LIMIT 500");

    QSqlQuery query(m_db);
    query.prepare(sql);
    if (!type.isEmpty()) query.addBindValue(type);
    if (!startTime.isEmpty()) query.addBindValue(startTime);
    if (!endTime.isEmpty()) query.addBindValue(endTime);
    if (!query.exec()) {
        qWarning() << "查询历史事件失败:" << query.lastError().text();
        return events;
    }
    while (query.next()) {
        EventInfo e;
        e.id = query.value(0).toInt();
        e.type = query.value(1).toString();
        e.sourceName = query.value(2).toString();
        e.detail = query.value(3).toString();
        e.happenedAt = query.value(4).toString();
        events.push_back(e);
    }
    return events;
}

QVector<DatabaseManager::VideoFileInfo> DatabaseManager::queryVideoFiles(
    const QString& eventType, const QString& startTime, const QString& endTime)
{
    QVector<VideoFileInfo> files;
    if (!m_initialized) return files;

    QMutexLocker locker(&m_mutex);
    QString sql = QStringLiteral("SELECT id, source_name, file_path, start_time, end_time, event_type, size_bytes FROM video_file WHERE 1=1");
    if (!eventType.isEmpty()) sql += QStringLiteral(" AND event_type = ?");
    if (!startTime.isEmpty()) sql += QStringLiteral(" AND start_time >= ?");
    if (!endTime.isEmpty()) sql += QStringLiteral(" AND start_time <= ?");
    sql += QStringLiteral(" ORDER BY id DESC LIMIT 500");

    QSqlQuery query(m_db);
    query.prepare(sql);
    if (!eventType.isEmpty()) query.addBindValue(eventType);
    if (!startTime.isEmpty()) query.addBindValue(startTime);
    if (!endTime.isEmpty()) query.addBindValue(endTime);
    if (!query.exec()) {
        qWarning() << "查询历史录像失败:" << query.lastError().text();
        return files;
    }
    while (query.next()) {
        VideoFileInfo f;
        f.id = query.value(0).toInt();
        f.sourceName = query.value(1).toString();
        f.filePath = query.value(2).toString();
        f.startTime = query.value(3).toString();
        f.endTime = query.value(4).toString();
        f.eventType = query.value(5).toString();
        f.sizeBytes = query.value(6).toLongLong();
        files.push_back(f);
    }
    return files;
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

    // 管理员账号表（登记人脸权限控制；密码加盐 SHA-256 存储，不存明文）
    if (!query.exec("CREATE TABLE IF NOT EXISTS admin ("
                    "username TEXT PRIMARY KEY,"
                    "password_hash TEXT NOT NULL,"
                    "salt TEXT NOT NULL,"
                    "updated_at TEXT DEFAULT (datetime('now','localtime')))")) {
        qWarning() << "创建 admin 表失败:" << query.lastError().text();
        return false;
    }

    return true;
}
// ---- 管理员（登记人脸权限）----

QString DatabaseManager::hashPassword(const QString& password, const QString& salt)
{
    // 加盐 SHA-256：salt + password 拼接后哈希，十六进制存储
    const QByteArray data = (salt + password).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

bool DatabaseManager::ensureDefaultAdmin()
{
    if (!m_initialized) return false;

    QMutexLocker locker(&m_mutex);
    QSqlQuery query(m_db);
    if (!query.exec("SELECT COUNT(*) FROM admin")) {
        qWarning() << "查询 admin 表失败:" << query.lastError().text();
        return false;
    }
    if (query.next() && query.value(0).toInt() > 0) return true; // 已有账号

    // 首次运行：写入默认管理员 admin / admin123
    const quint64 s1 = QRandomGenerator::global()->generate64();
    const quint64 s2 = QRandomGenerator::global()->generate64();
    const QString salt = QString::number(s1, 16) + QString::number(s2, 16);
    query.prepare("INSERT INTO admin (username, password_hash, salt) VALUES (?, ?, ?)");
    query.addBindValue(defaultAdminUsername());
    query.addBindValue(hashPassword(defaultAdminPassword(), salt));
    query.addBindValue(salt);
    if (!query.exec()) {
        qWarning() << "写入默认管理员失败:" << query.lastError().text();
        return false;
    }
    qInfo() << "已创建默认管理员账号:" << defaultAdminUsername();
    return true;
}

bool DatabaseManager::verifyAdmin(const QString& username, const QString& password)
{
    if (!m_initialized) return false;

    QMutexLocker locker(&m_mutex);
    QSqlQuery query(m_db);
    query.prepare("SELECT password_hash, salt FROM admin WHERE username = ?");
    query.addBindValue(username);
    if (!query.exec()) {
        qWarning() << "查询管理员失败:" << query.lastError().text();
        return false;
    }
    if (!query.next()) return false; // 账号不存在

    const QString stored = query.value(0).toString();
    const QString salt = query.value(1).toString();
    return stored == hashPassword(password, salt);
}

bool DatabaseManager::changeAdminPassword(const QString& username,
                                          const QString& oldPassword,
                                          const QString& newPassword)
{
    if (!m_initialized || newPassword.isEmpty()) return false;
    if (!verifyAdmin(username, oldPassword)) return false;

    QMutexLocker locker(&m_mutex);
    const quint64 s1 = QRandomGenerator::global()->generate64();
    const quint64 s2 = QRandomGenerator::global()->generate64();
    const QString salt = QString::number(s1, 16) + QString::number(s2, 16);
    QSqlQuery query(m_db);
    query.prepare("UPDATE admin SET password_hash = ?, salt = ?, "
                  "updated_at = datetime('now','localtime') WHERE username = ?");
    query.addBindValue(hashPassword(newPassword, salt));
    query.addBindValue(salt);
    query.addBindValue(username);
    if (!query.exec()) {
        qWarning() << "修改管理员密码失败:" << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() > 0;
}