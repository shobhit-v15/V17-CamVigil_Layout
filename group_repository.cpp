#include "group_repository.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QFileInfo>
#include <QDebug>
#include <QtGlobal>

GroupRepository::GroupRepository(const QString& dbPath)
    : m_dbPath(dbPath)
{
    // Use the pointer value to build a reasonably unique connection name.
    m_connectionName = QStringLiteral("group_repo_%1")
                           .arg(reinterpret_cast<quintptr>(this));
}

GroupRepository::~GroupRepository()
{
    close();
}

bool GroupRepository::open()
{
    if (m_db.isValid() && m_db.isOpen()) {
        return true;
    }

    if (!m_db.isValid()) {
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        m_db.setDatabaseName(m_dbPath);
    }

    if (!m_db.open()) {
        qWarning() << "[GroupRepository] Failed to open DB at"
                   << QFileInfo(m_db.databaseName()).absoluteFilePath()
                   << "error:" << m_db.lastError().text();
        return false;
    }
    return true;
}

void GroupRepository::close()
{
    if (!m_db.isValid()) {
        return;
    }

    if (m_db.isOpen()) {
        m_db.close();
    }

    m_db = QSqlDatabase();
    if (!m_connectionName.isEmpty()) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

bool GroupRepository::ensureSchemaCameras()
{
    if (!open()) {
        return false;
    }

    QSqlQuery q(m_db);
    const QString sql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS cameras ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " name TEXT, main_url TEXT UNIQUE, sub_url TEXT,"
        " created_at INTEGER DEFAULT (strftime('%s','now')) );");
    if (!q.exec(sql)) {
        qWarning() << "[GroupRepository] ensureSchemaCameras error:"
                   << q.lastError().text();
        return false;
    }
    return true;
}

bool GroupRepository::ensureSchemaGroupsInternal()
{
    if (!open()) {
        return false;
    }

    QSqlQuery q(m_db);

    const QString createGroups = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS camera_groups ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " name TEXT UNIQUE NOT NULL,"
        " created_at INTEGER DEFAULT (strftime('%s','now')) );");

    if (!q.exec(createGroups)) {
        qWarning() << "[GroupRepository] ensureSchemaGroupsInternal camera_groups error:"
                   << q.lastError().text();
        return false;
    }

    const QString createMembers = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS camera_group_members ("
        " group_id INTEGER NOT NULL,"
        " camera_id INTEGER NOT NULL,"
        " PRIMARY KEY(group_id, camera_id),"
        " FOREIGN KEY(group_id) REFERENCES camera_groups(id) ON DELETE CASCADE,"
        " FOREIGN KEY(camera_id) REFERENCES cameras(id) ON DELETE CASCADE );");

    if (!q.exec(createMembers)) {
        qWarning() << "[GroupRepository] ensureSchemaGroupsInternal camera_group_members error:"
                   << q.lastError().text();
        return false;
    }

    // Optional helper indexes.
    const QString idxCamera = QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_group_members_camera "
        "ON camera_group_members(camera_id);");
    if (!q.exec(idxCamera)) {
        qWarning() << "[GroupRepository] ensureSchemaGroupsInternal index(camera_id) error:"
                   << q.lastError().text();
        // Non-fatal.
    }

    const QString idxGroup = QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_group_members_group "
        "ON camera_group_members(group_id);");
    if (!q.exec(idxGroup)) {
        qWarning() << "[GroupRepository] ensureSchemaGroupsInternal index(group_id) error:"
                   << q.lastError().text();
        // Non-fatal.
    }

    return true;
}

bool GroupRepository::ensureSchemaGroups()
{
    if (!ensureSchemaCameras()) {
        return false;
    }
    return ensureSchemaGroupsInternal();
}

QVector<CameraGroupInfo> GroupRepository::listGroups()
{
    QVector<CameraGroupInfo> out;
    if (!open()) {
        return out;
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, name FROM camera_groups ORDER BY name;"));
    if (!q.exec()) {
        qWarning() << "[GroupRepository] listGroups error:" << q.lastError().text();
        return out;
    }

    while (q.next()) {
        CameraGroupInfo g;
        g.id = q.value(0).toInt();
        g.name = q.value(1).toString();
        out.push_back(g);
    }
    return out;
}

int GroupRepository::createGroup(const QString& name)
{
    if (name.trimmed().isEmpty()) {
        return -1;
    }
    if (!open()) {
        return -1;
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO camera_groups(name) VALUES(?);"));
    q.addBindValue(name.trimmed());
    if (!q.exec()) {
        qWarning() << "[GroupRepository] createGroup error:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toInt();
}

bool GroupRepository::renameGroup(int groupId, const QString& newName)
{
    if (groupId <= 0) {
        return false;
    }
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    if (!open()) {
        return false;
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE camera_groups SET name=? WHERE id=?;"));
    q.addBindValue(trimmed);
    q.addBindValue(groupId);
    if (!q.exec()) {
        qWarning() << "[GroupRepository] renameGroup error:" << q.lastError().text();
        return false;
    }
    return (q.numRowsAffected() > 0);
}

bool GroupRepository::deleteGroup(int groupId)
{
    if (groupId <= 0) {
        return false;
    }
    if (!open()) {
        return false;
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM camera_groups WHERE id=?;"));
    q.addBindValue(groupId);
    if (!q.exec()) {
        qWarning() << "[GroupRepository] deleteGroup error:" << q.lastError().text();
        return false;
    }
    return (q.numRowsAffected() > 0);
}

int GroupRepository::ensureCameraRow(const QString& mainUrl, const QString& displayName)
{
    const QString trimmedUrl = mainUrl.trimmed();
    if (trimmedUrl.isEmpty()) {
        return -1;
    }
    if (!ensureSchemaCameras()) {
        return -1;
    }

    if (!open()) {
        return -1;
    }

    // Try to find existing camera by main_url.
    {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT id FROM cameras WHERE main_url=?;"));
        q.addBindValue(trimmedUrl);
        if (q.exec() && q.next()) {
            int id = q.value(0).toInt();
            if (!displayName.trimmed().isEmpty()) {
                QSqlQuery uq(m_db);
                uq.prepare(QStringLiteral("UPDATE cameras SET name=? WHERE id=?;"));
                uq.addBindValue(displayName.trimmed());
                uq.addBindValue(id);
                if (!uq.exec()) {
                    qWarning() << "[GroupRepository] ensureCameraRow update name error:"
                               << uq.lastError().text();
                }
            }
            return id;
        }
    }

    // Insert new row if not found.
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO cameras(name, main_url, sub_url) VALUES(?,?,NULL);"));
    q.addBindValue(displayName.trimmed());
    q.addBindValue(trimmedUrl);
    if (!q.exec()) {
        qWarning() << "[GroupRepository] ensureCameraRow insert error:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toInt();
}

int GroupRepository::findCameraIdByMainUrl(const QString& mainUrl)
{
    const QString trimmedUrl = mainUrl.trimmed();
    if (trimmedUrl.isEmpty()) {
        return -1;
    }
    if (!open()) {
        return -1;
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM cameras WHERE main_url=?;"));
    q.addBindValue(trimmedUrl);
    if (!q.exec()) {
        qWarning() << "[GroupRepository] findCameraIdByMainUrl error:" << q.lastError().text();
        return -1;
    }
    if (q.next()) {
        return q.value(0).toInt();
    }
    return -1;
}

QVector<int> GroupRepository::listCameraIdsForGroup(int groupId)
{
    QVector<int> out;
    if (groupId <= 0) {
        return out;
    }
    if (!open()) {
        return out;
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT camera_id FROM camera_group_members "
        "WHERE group_id=? ORDER BY camera_id;"));
    q.addBindValue(groupId);
    if (!q.exec()) {
        qWarning() << "[GroupRepository] listCameraIdsForGroup error:" << q.lastError().text();
        return out;
    }

    while (q.next()) {
        out.push_back(q.value(0).toInt());
    }
    return out;
}

QVector<int> GroupRepository::listGroupIdsForCamera(int cameraId)
{
    QVector<int> out;
    if (cameraId <= 0) {
        return out;
    }
    if (!open()) {
        return out;
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT group_id FROM camera_group_members "
        "WHERE camera_id=? ORDER BY group_id;"));
    q.addBindValue(cameraId);
    if (!q.exec()) {
        qWarning() << "[GroupRepository] listGroupIdsForCamera error:" << q.lastError().text();
        return out;
    }

    while (q.next()) {
        out.push_back(q.value(0).toInt());
    }
    return out;
}

bool GroupRepository::setCameraGroups(int cameraId, const QVector<int>& groupIds)
{
    if (cameraId <= 0) {
        return false;
    }
    if (!open()) {
        return false;
    }

    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("BEGIN TRANSACTION;"))) {
        qWarning() << "[GroupRepository] setCameraGroups: begin transaction error:"
                   << q.lastError().text();
        return false;
    }

    // Remove existing memberships for this camera.
    {
        QSqlQuery del(m_db);
        del.prepare(QStringLiteral(
            "DELETE FROM camera_group_members WHERE camera_id=?;"));
        del.addBindValue(cameraId);
        if (!del.exec()) {
            qWarning() << "[GroupRepository] setCameraGroups: delete error:"
                       << del.lastError().text();
            QSqlQuery rb(m_db);
            rb.exec(QStringLiteral("ROLLBACK;"));
            return false;
        }
    }

    // Insert new memberships.
    for (int gid : groupIds) {
        if (gid <= 0) {
            continue;
        }
        QSqlQuery ins(m_db);
        ins.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO camera_group_members(group_id, camera_id) "
            "VALUES(?,?);"));
        ins.addBindValue(gid);
        ins.addBindValue(cameraId);
        if (!ins.exec()) {
            qWarning() << "[GroupRepository] setCameraGroups: insert error:"
                       << ins.lastError().text();
            QSqlQuery rb(m_db);
            rb.exec(QStringLiteral("ROLLBACK;"));
            return false;
        }
    }

    {
        QSqlQuery commit(m_db);
        if (!commit.exec(QStringLiteral("COMMIT;"))) {
            qWarning() << "[GroupRepository] setCameraGroups: commit error:"
                       << commit.lastError().text();
            return false;
        }
    }

    return true;
}
