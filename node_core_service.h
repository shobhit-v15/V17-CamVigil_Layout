#pragma once

#include <QObject>
#include <QVector>
#include <QDateTime>
#include <QSqlDatabase>

#include "node_config.h"

class DbReader;
class DbWriter;
class ArchiveManager;
class StorageService;
class NodeRestreamer;

struct NodeInfo {
    QString nodeId;
    QString buildingId;
    QString hostname;
    QString softwareVersion;
    qint64 uptimeSeconds;

    struct StorageInfo {
        QString mountPoint;
        quint64 totalBytes = 0;
        quint64 usedBytes = 0;
        double freePercent = 0.0;
    };

    QVector<StorageInfo> storage;
    int totalCameras = 0;
    int recordingCameras = 0;
};

struct NodeCamera {
    int id = 0;
    QString name;
    QString groupName;
    QString rtspMain;
    QString rtspSub;
    bool isRecording = false;
    QString liveProxyRtsp;
};

struct NodeSegment {
    qint64 segmentId = 0;
    int cameraId = 0;
    QDateTime start;
    QDateTime end;
    qint64 durationSec = 0;
    quint64 sizeBytes = 0;
    QString filePath;
};

class NodeCoreService : public QObject {
    Q_OBJECT
public:
    NodeCoreService(DbReader* dbReader,
                    DbWriter* dbWriter,
                    ArchiveManager* archiveManager,
                    StorageService* storageService,
                    NodeRestreamer* restreamer,
                    const NodeConfig& cfg,
                    QObject* parent = nullptr);
    ~NodeCoreService() override;

    NodeInfo getNodeInfo() const;
    QVector<NodeCamera> listCameras() const;
    QVector<NodeSegment> listSegments(int cameraId,
                                      const QDateTime& from,
                                      const QDateTime& to) const;
    QString resolveSegmentPath(qint64 segmentId) const;

private:
    DbReader* m_dbReader{};
    DbWriter* m_dbWriter{};
    ArchiveManager* m_archiveManager{};
    StorageService* m_storageService{};
    NodeRestreamer* m_restreamer{};
    NodeConfig m_cfg;
    QDateTime m_startupTime;
    QString m_dbConnName;
    QSqlDatabase m_db;
    QString m_dbPath;

    static QDateTime nsToDateTime(qint64 ns);
};
