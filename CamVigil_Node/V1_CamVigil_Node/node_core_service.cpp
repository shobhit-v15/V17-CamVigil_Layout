#include "node_core_service.h"

#include "archivemanager.h"
#include "db_reader.h"
#include "db_writer.h"
#include "node_restreamer.h"
#include "storageservice.h"

#include <QDebug>
#include <QFileInfo>
#include <QHostInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStorageInfo>
#include <QUuid>
#include <QtGlobal>
#include <limits>

namespace {
static constexpr const char* kSoftwareVersion = "camvigil-node-poc";

static QDateTime fromNs(qint64 ns)
{
    if (ns <= 0) {
        return {};
    }
    const qint64 secs = ns / 1000000000LL;
    const qint64 nsecRemainder = ns % 1000000000LL;
    QDateTime dt = QDateTime::fromSecsSinceEpoch(secs, Qt::UTC);
    dt = dt.addMSecs(nsecRemainder / 1000000LL);
    return dt;
}
} // namespace

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
}

NodeInfo NodeCoreService::getNodeInfo() const
{
    NodeInfo info;
    info.nodeId = m_cfg.nodeId;
    info.buildingId = m_cfg.buildingId;
    info.hostname = QHostInfo::localHostName();
    info.softwareVersion = QString::fromLatin1(kSoftwareVersion);

    const auto now = QDateTime::currentDateTimeUtc();
    info.uptimeSeconds = m_startupTime.isValid()
        ? qMax<qint64>(0, m_startupTime.secsTo(now))
        : 0;

    // Archive storage info
    if (m_archiveManager) {
        const QString archiveRoot = m_archiveManager->archiveRoot();
        if (!archiveRoot.isEmpty()) {
            QStorageInfo st(archiveRoot);
            NodeInfo::StorageInfo s;
            s.mountPoint = st.rootPath();
            s.totalBytes = static_cast<quint64>(st.bytesTotal());
            s.usedBytes = s.totalBytes - static_cast<quint64>(st.bytesAvailable());
            if (s.totalBytes > 0) {
                const double freePct = (static_cast<double>(st.bytesAvailable()) / static_cast<double>(s.totalBytes)) * 100.0;
                s.freePercent = freePct;
            }
            info.storage.push_back(s);
        }
    }

    if (m_storageService && m_storageService->hasExternal()) {
        QStorageInfo st(m_storageService->externalRoot());
        NodeInfo::StorageInfo s;
        s.mountPoint = st.rootPath();
        s.totalBytes = static_cast<quint64>(st.bytesTotal());
        s.usedBytes = s.totalBytes - static_cast<quint64>(st.bytesAvailable());
        if (s.totalBytes > 0) {
            const double freePct = (static_cast<double>(st.bytesAvailable()) / static_cast<double>(s.totalBytes)) * 100.0;
            s.freePercent = freePct;
        }
        info.storage.push_back(s);
    }

    withDatabase([&info](QSqlDatabase& db) {
        QSqlQuery q(db);
        if (q.exec(QStringLiteral("SELECT COUNT(*) FROM cameras;")) && q.next()) {
            info.totalCameras = q.value(0).toInt();
        }
        q.finish();
        if (q.exec(QStringLiteral("SELECT COUNT(DISTINCT camera_id) FROM segments WHERE status IN (0,1) AND camera_id IS NOT NULL;"))
            && q.next()) {
            info.recordingCameras = q.value(0).toInt();
        }
        return true;
    });

    return info;
}

QVector<NodeCamera> NodeCoreService::listCameras() const
{
    QVector<NodeCamera> cameras;

    withDatabase([this, &cameras](QSqlDatabase& db) {
        QSqlQuery q(db);
        const QString sql = QStringLiteral(R"SQL(
            SELECT c.id,
                   COALESCE(c.name, c.main_url) AS name,
                   c.main_url,
                   COALESCE(c.sub_url, '') AS sub_url,
                   EXISTS(SELECT 1 FROM segments s WHERE s.camera_id = c.id AND s.status IN (0,1)) AS is_recording
            FROM cameras c
            ORDER BY name
        )SQL");
        if (!q.exec(sql)) {
            qWarning() << "[NodeCoreService] listCameras SQL error:" << q.lastError().text();
            return false;
        }
        while (q.next()) {
            NodeCamera cam;
            cam.id = q.value(0).toInt();
            cam.name = q.value(1).toString();
            cam.groupName = QStringLiteral("TODO"); // TODO: integrate with group_repository/groups.
            cam.rtspMain = q.value(2).toString();
            cam.rtspSub = q.value(3).toString();
            cam.isRecording = q.value(4).toInt() != 0;
            if (m_restreamer) {
                cam.liveProxyRtsp = m_restreamer->proxyUrlForCamera(cam.id);
            }
            cameras.push_back(cam);
        }
        return true;
    });

    return cameras;
}

QVector<NodeSegment> NodeCoreService::listSegments(int cameraId,
                                                   const QDateTime& from,
                                                   const QDateTime& to) const
{
    QVector<NodeSegment> segments;
    if (cameraId <= 0) {
        return segments;
    }

    const qint64 fromNs = from.isValid()
        ? from.toSecsSinceEpoch() * 1000000000LL
        : std::numeric_limits<qint64>::min();
    const qint64 toNs = to.isValid()
        ? to.toSecsSinceEpoch() * 1000000000LL
        : std::numeric_limits<qint64>::max();

    withDatabase([&](QSqlDatabase& db) {
        QSqlQuery q(db);
        const QString sql = QStringLiteral(R"SQL(
            SELECT s.id,
                   s.camera_id,
                   s.start_utc_ns,
                   CASE
                     WHEN s.end_utc_ns IS NOT NULL AND s.end_utc_ns > 0 THEN s.end_utc_ns
                     WHEN COALESCE(s.duration_ms,0) > 0 THEN s.start_utc_ns + s.duration_ms*1000000
                     ELSE s.start_utc_ns
                   END AS eff_end_ns,
                   COALESCE(s.duration_ms, 0),
                   COALESCE(s.size_bytes, 0),
                   s.file_path
            FROM segments s
            WHERE s.status IN (0,1)
              AND (s.camera_id = :cid OR s.camera_url = (SELECT main_url FROM cameras WHERE id = :cid))
              AND s.start_utc_ns < :to_ns
              AND (
                    CASE
                      WHEN s.end_utc_ns IS NOT NULL AND s.end_utc_ns > 0 THEN s.end_utc_ns
                      WHEN COALESCE(s.duration_ms,0) > 0 THEN s.start_utc_ns + s.duration_ms*1000000
                      ELSE s.start_utc_ns
                    END
                  ) > :from_ns
            ORDER BY s.start_utc_ns ASC
        )SQL");
        q.prepare(sql);
        q.bindValue(QStringLiteral(":cid"), cameraId);
        q.bindValue(QStringLiteral(":from_ns"), fromNs);
        q.bindValue(QStringLiteral(":to_ns"), toNs);

        if (!q.exec()) {
            qWarning() << "[NodeCoreService] listSegments SQL error:" << q.lastError().text();
            return false;
        }
        while (q.next()) {
            NodeSegment seg;
            seg.segmentId = q.value(0).toLongLong();
            seg.cameraId = q.value(1).toInt();
            const qint64 startNs = q.value(2).toLongLong();
            const qint64 endNs = q.value(3).toLongLong();
            const qint64 durationMs = q.value(4).toLongLong();
            seg.start = ::fromNs(startNs);
            seg.end = ::fromNs(endNs);
            if (durationMs > 0) {
                seg.durationSec = durationMs / 1000;
            } else if (endNs > startNs) {
                seg.durationSec = (endNs - startNs) / 1000000000LL;
            } else {
                seg.durationSec = 0;
            }
            seg.sizeBytes = static_cast<quint64>(q.value(5).toLongLong());
            seg.filePath = QFileInfo(q.value(6).toString()).absoluteFilePath();
            segments.push_back(seg);
        }
        return true;
    });

    return segments;
}

QString NodeCoreService::resolveSegmentPath(qint64 segmentId) const
{
    if (segmentId <= 0) {
        return {};
    }

    QString foundPath;
    withDatabase([&](QSqlDatabase& db) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT file_path FROM segments WHERE id = :sid LIMIT 1;"));
        q.bindValue(QStringLiteral(":sid"), segmentId);
        if (!q.exec()) {
            qWarning() << "[NodeCoreService] resolveSegmentPath SQL error:" << q.lastError().text();
            return false;
        }
        if (q.next()) {
            foundPath = QFileInfo(q.value(0).toString()).absoluteFilePath();
        }
        return true;
    });

    return foundPath;
}

QString NodeCoreService::databasePath() const
{
    if (m_archiveManager) {
        return m_archiveManager->databasePath();
    }
    return {};
}

bool NodeCoreService::withDatabase(const std::function<bool(QSqlDatabase&)>& fn) const
{
    if (!fn) {
        return false;
    }
    const QString dbPath = databasePath();
    if (dbPath.isEmpty()) {
        qWarning() << "[NodeCoreService] Database path unavailable.";
        return false;
    }
    const QString connName = QStringLiteral("node_core_%1")
                                 .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    db.setDatabaseName(dbPath);
    db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY=1;QSQLITE_ENABLE_SHARED_CACHE=1;QSQLITE_BUSY_TIMEOUT=5000;"));
    if (!db.open()) {
        qWarning() << "[NodeCoreService] Failed to open DB" << dbPath << ":" << db.lastError().text();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connName);
        return false;
    }

    const bool ok = fn(db);

    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connName);
    return ok;
}
