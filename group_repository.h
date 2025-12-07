#pragma once

#include <QVector>
#include <QString>
#include <QSqlDatabase>

struct CameraGroupInfo {
    int id = -1;
    QString name;
};

struct CameraRowInfo {
    int id = -1;
    QString name;
    QString mainUrl;
};

class GroupRepository
{
public:
    // dbPath: absolute or relative path to the existing camvigil SQLite DB.
    explicit GroupRepository(const QString& dbPath);
    ~GroupRepository();

    // Return true if schema exists / successfully ensured.
    bool ensureSchemaGroups();

    // For later phases – implemented fully but unused in Phase 0.
    QVector<CameraGroupInfo> listGroups();
    int createGroup(const QString& name);
    bool renameGroup(int groupId, const QString& newName);
    bool deleteGroup(int groupId);

    int ensureCameraRow(const QString& mainUrl, const QString& displayName);
    int findCameraIdByMainUrl(const QString& mainUrl);

    QVector<int> listCameraIdsForGroup(int groupId);
    QVector<int> listGroupIdsForCamera(int cameraId);
    bool setCameraGroups(int cameraId, const QVector<int>& groupIds);
    QVector<CameraRowInfo> listAllCameras();

private:
    QString m_dbPath;
    QString m_connectionName;

    bool open();
    void close();
    bool ensureSchemaCameras();         // ensure base cameras table exists if needed
    bool ensureSchemaGroupsInternal();  // create group tables

    // Local connection handle for this repository.
    QSqlDatabase m_db;
};
