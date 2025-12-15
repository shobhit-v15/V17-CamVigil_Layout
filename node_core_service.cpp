#include "node_core_service.h"

#include <QHostInfo>
#include <QStorageInfo>
#include <QDebug>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QFileInfo>

#include "archivemanager.h"
#include "storageservice.h"
#include "node_restreamer.h"
#include "db_reader.h"
#include "db_writer.h"

NodeCoreService::NodeCoreService(DbReader* dbReader,
                                 DbWriter* dbWriter,
                                 ArchiveManager* archiveManager,
                                 StorageService* storageService,
                                 NodeRestreamer* restreamer,
                                 const NodeConfig& cfg,
                                 QObject* parent)
    : QObject(parent)
    , m_dbReader(dbReader)
    , m_dbWriter(dbWriter)
    , m_archiveManager(archiveManager)
    , m_storageService(storageService)
    , m_restreamer(restreamer)
    , m_cfg(cfg)
    , m_startupTime(QDateTime::currentDateTimeUtc())
{
    if (m_archiveManager) {
        m_dbPath = m_archiveManager->databasePath();
        if (!m_dbPath.isEmpty()) {
            m_dbConnName = QStringLiteral("node_core_ro_%1")
                               .arg(reinterpret_cast<quintptr>(this));
            m_db = QSqlDatabase::addDatabase("QSQLITE", m_dbConnName);
            m_db.setDatabaseName(m_dbPath);
            m_db.setConnectOptions(
                "QSQLITE_OPEN_READONLY=1;"
                "QSQLITE_ENABLE_SHARED_CACHE=1;"
                "QSQLITE_BUSY_TIMEOUT=2000");
            if (!m_db.open()) {
                qWarning() << "[NodeCoreService] DB open failed for" << m_dbPath
                           << ":" << m_db.lastError().text();
            }
        }
    }
}

NodeCoreService::~NodeCoreService()
{
    if (m_db.isValid()) {
        if (m_db.isOpen()) {
            m_db.close();
        }
        m_db = QSqlDatabase();
    }
    if (!m_dbConnName.isEmpty()) {
        QSqlDatabase::removeDatabase(m_dbConnName);
    }
}

NodeInfo NodeCoreService::getNodeInfo() const
{
    NodeInfo info;
    info.nodeId = m_cfg.nodeId;
    info.buildingId = m_cfg.buildingId;
    info.hostname = QHostInfo::localHostName();
    info.softwareVersion = QStringLiteral("TODO-version"); // TODO: wire to build/version metadata.
    const QDateTime now = QDateTime::currentDateTimeUtc();
    info.uptimeSeconds = m_startupTime.secsTo(now);

    if (m_archiveManager) {
        const QString root = m_archiveManager->archiveRoot();
        QStorageInfo s(root);
        if (s.isValid()) {
            NodeInfo::StorageInfo si;
            si.mountPoint = root;
            si.totalBytes = static_cast<quint64>(s.bytesTotal());
            const quint64 freeBytes = static_cast<quint64>(s.bytesAvailable());
            si.usedBytes = si.totalBytes > freeBytes ? si.totalBytes - freeBytes : 0;
            si.freePercent = si.totalBytes > 0
                                 ? (static_cast<double>(freeBytes) / si.totalBytes) * 100.0
                                 : 0.0;
            info.storage.append(si);
        } else {
            qWarning() << "[NodeCoreService] StorageInfo invalid for" << root;
        }
    } else if (m_storageService) {
        NodeInfo::StorageInfo si;
        si.mountPoint = m_storageService->externalRoot();
        // TODO: gather total/used via StorageService for external disks.
        info.storage.append(si);
    }

    // TODO: Query DbReader for camera counts + recording state.
    info.totalCameras = 0;
    info.recordingCameras = 0;

    if (m_db.isValid() && m_db.isOpen()) {
        QSqlQuery q(m_db);
        if (q.exec(QStringLiteral("SELECT COUNT(*) FROM cameras;")) && q.next()) {
            info.totalCameras = q.value(0).toInt();
        }
        if (q.exec(QStringLiteral("SELECT COUNT(DISTINCT camera_id) FROM segments WHERE status IN (0,1);"))
            && q.next()) {
            info.recordingCameras = q.value(0).toInt();
        }
    }

    return info;
}

QVector<NodeCamera> NodeCoreService::listCameras() const
{
    QVector<NodeCamera> list;
    if (!m_db.isValid() || !m_db.isOpen()) {
        qWarning() << "[NodeCoreService] listCameras(): DB not open";
        return list;
    }

    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT id, name, main_url, sub_url FROM cameras ORDER BY id;"))) {
        qWarning() << "[NodeCoreService] listCameras query failed:" << q.lastError().text();
        return list;
    }

    while (q.next()) {
        NodeCamera cam;
        cam.id = q.value(0).toInt();
        cam.name = q.value(1).toString();
        cam.rtspMain = q.value(2).toString();
        cam.rtspSub = q.value(3).toString();
        cam.isRecording = true; // TODO: derive from ArchiveManager worker state.
        if (m_restreamer) {
            cam.liveProxyRtsp = m_restreamer->proxyUrlForCamera(cam.id);
        }
        list.append(cam);
    }

    return list;
}

QVector<NodeSegment> NodeCoreService::listSegments(int cameraId,
                                                   const QDateTime& from,
                                                   const QDateTime& to) const
{
    QVector<NodeSegment> segs;
    if (!m_db.isValid() || !m_db.isOpen()) {
        qWarning() << "[NodeCoreService] listSegments(): DB not open";
        return segs;
    }
    if (cameraId <= 0) {
        qWarning() << "[NodeCoreService] listSegments(): invalid cameraId";
        return segs;
    }

    const QDateTime fromUtc = from.isValid()
        ? from.toUTC()
        : QDateTime::currentDateTimeUtc().addSecs(-3600);
    const QDateTime toUtc = to.isValid()
        ? to.toUTC()
        : QDateTime::currentDateTimeUtc();

    const qint64 fromNs = fromUtc.toSecsSinceEpoch() * 1000000000LL;
    const qint64 toNs = toUtc.toSecsSinceEpoch() * 1000000000LL;

    QSqlQuery q(m_db);
    q.setForwardOnly(true);
    q.prepare(R"SQL(
        SELECT id, camera_id, start_utc_ns,
               CASE
                 WHEN end_utc_ns IS NOT NULL AND end_utc_ns > 0 THEN end_utc_ns
                 WHEN COALESCE(duration_ms,0) > 0 THEN start_utc_ns + duration_ms*1000000
                 ELSE start_utc_ns
               END AS eff_end,
               duration_ms,
               size_bytes,
               file_path
        FROM segments
        WHERE status IN (0,1)
          AND (camera_id = :cid OR camera_url = (SELECT main_url FROM cameras WHERE id=:cid))
          AND start_utc_ns < :to_ns
          AND (
                CASE
                  WHEN end_utc_ns IS NOT NULL AND end_utc_ns > 0 THEN end_utc_ns
                  WHEN COALESCE(duration_ms,0) > 0 THEN start_utc_ns + duration_ms*1000000
                  ELSE start_utc_ns
                END
              ) > :from_ns
        ORDER BY start_utc_ns
    )SQL");
    q.bindValue(":cid", cameraId);
    q.bindValue(":from_ns", fromNs);
    q.bindValue(":to_ns", toNs);

    if (!q.exec()) {
        qWarning() << "[NodeCoreService] listSegments query failed:" << q.lastError().text();
        return segs;
    }

    while (q.next()) {
        NodeSegment seg;
        seg.segmentId = q.value(0).toLongLong();
        seg.cameraId = q.value(1).toInt();
        const qint64 startNs = q.value(2).toLongLong();
        const qint64 endNs = q.value(3).toLongLong();
        seg.start = nsToDateTime(startNs);
        seg.end = nsToDateTime(endNs);
        seg.durationSec = q.value(4).toLongLong() / 1000;
        seg.sizeBytes = static_cast<quint64>(q.value(5).toLongLong());
        seg.filePath = q.value(6).toString();
        segs.append(seg);
    }

    return segs;
}

QString NodeCoreService::resolveSegmentPath(qint64 segmentId) const
{
    if (!m_db.isValid() || !m_db.isOpen() || segmentId <= 0) {
        return QString();
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT file_path FROM segments WHERE id=:id;"));
    q.bindValue(":id", segmentId);
    if (!q.exec()) {
        qWarning() << "[NodeCoreService] resolveSegmentPath query failed:" << q.lastError().text();
        return QString();
    }
    if (q.next()) {
        return QFileInfo(q.value(0).toString()).absoluteFilePath();
    }
    return QString();
}

QDateTime NodeCoreService::nsToDateTime(qint64 ns)
{
    if (ns <= 0) {
        return {};
    }
    const qint64 msecs = ns / 1000000LL;
    return QDateTime::fromMSecsSinceEpoch(msecs, Qt::UTC);
}
