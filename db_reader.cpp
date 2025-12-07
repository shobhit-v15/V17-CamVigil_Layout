#include "db_reader.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QFileInfo>
#include <QDateTime>
#include <QtDebug>

DbReader::DbReader(QObject* parent) : QObject(parent) {
    // Ensure queued connections work for custom types
    qRegisterMetaType<RecentSegment>("RecentSegment");
    qRegisterMetaType<QVector<RecentSegment>>("QVector<RecentSegment>");
}

DbReader::~DbReader() {
    // ensure clean teardown even if window is closed externally
    if (db_.isValid()) {
        if (db_.isOpen()) db_.close();
        db_ = QSqlDatabase();
        if (!connName_.isEmpty()) QSqlDatabase::removeDatabase(connName_);
    }
}

void DbReader::shutdown() {
    // Must be invoked on the DB thread (use BlockingQueuedConnection)
    qInfo() << "[DB] shutdown() begin";
    if (db_.isValid()) {
        if (db_.isOpen()) db_.close();
        db_ = QSqlDatabase();
        if (!connName_.isEmpty()) {
            QSqlDatabase::removeDatabase(connName_);
            connName_.clear();
        }
    }
    deleteLater(); // delete object on its own thread
    qInfo() << "[DB] shutdown() end";
}

void DbReader::openAt(const QString& dbPath) {
    if (!db_.isValid()) {
        connName_ = QStringLiteral("playback_ro_%1")
                        .arg(reinterpret_cast<quintptr>(this));
        db_ = QSqlDatabase::addDatabase("QSQLITE", connName_);
    } else if (db_.isOpen()) {
        db_.close();
    }

    db_.setDatabaseName(dbPath);
    db_.setConnectOptions(
        "QSQLITE_OPEN_READONLY=1;"
        "QSQLITE_ENABLE_SHARED_CACHE=1;"
        "QSQLITE_BUSY_TIMEOUT=5000"
    );
    const bool ok = db_.open();
    qInfo() << "[DB] RO open:" << QFileInfo(db_.databaseName()).absoluteFilePath();
    emit opened(ok, ok ? QString() : db_.lastError().text());
}

void DbReader::listCameras() {
    QVector<QPair<int,QString>> out;
    QSqlQuery q(db_);
    q.prepare(R"SQL(
        SELECT c.id, c.name
        FROM cameras c
        WHERE EXISTS (
          SELECT 1 FROM segments s
          WHERE s.status IN (0,1)
            AND (s.camera_id=c.id OR s.camera_url=c.main_url)
        )
        ORDER BY c.name
    )SQL");
    if (!q.exec()) { emit error(q.lastError().text()); return; }
    while (q.next()) out.push_back({ q.value(0).toInt(), q.value(1).toString() });
    emit camerasReady(out);
}

void DbReader::listDays(int cameraId) {
    QStringList days;
    QSqlQuery q(db_);
    q.prepare(R"SQL(
        SELECT DISTINCT strftime('%Y-%m-%d',
                                 datetime(start_utc_ns/1000000000,'unixepoch','localtime'))
        FROM segments
        WHERE status IN (0,1)
          AND (camera_id=:cid OR camera_url=(SELECT main_url FROM cameras WHERE id=:cid))
        ORDER BY 1
    )SQL");
    q.bindValue(":cid", cameraId);
    if (!q.exec()) { emit error(q.lastError().text()); return; }
    while (q.next()) days << q.value(0).toString();
    emit daysReady(cameraId, days);
}

void DbReader::listSegments(int cameraId, const QString& ymd) {
    // compute local-day window in UTC epoch nanoseconds
    const QDate d = QDate::fromString(ymd, "yyyy-MM-dd");
    const QDateTime d0(d, QTime(0,0,0), Qt::LocalTime);
    const QDateTime d1 = d0.addDays(1);
    const qint64 start_ns = d0.toSecsSinceEpoch() * 1000000000LL;
    const qint64 end_ns   = d1.toSecsSinceEpoch() * 1000000000LL;

    QVector<SegmentInfo> segs;
    QSqlQuery q(db_);
    q.setForwardOnly(true);

    // NOTE: no "now()" fallback — open-ended rows collapse to start_utc_ns
    q.prepare(R"SQL(
      SELECT path, start_utc_ns, eff_end_ns, duration_ms FROM (
        -- branch 1: rows with camera_id filled (uses idx_segments_camera_time)
        SELECT
          s.file_path AS path,
          s.start_utc_ns,
          CASE
            WHEN s.end_utc_ns IS NOT NULL AND s.end_utc_ns > 0 THEN s.end_utc_ns
            WHEN COALESCE(s.duration_ms,0) > 0 THEN s.start_utc_ns + s.duration_ms*1000000
            ELSE s.start_utc_ns
          END AS eff_end_ns,
          s.duration_ms
        FROM segments s
        WHERE s.status IN (0,1)
          AND s.camera_id = :cid
          AND s.start_utc_ns < :end_ns
          AND (
                CASE
                  WHEN s.end_utc_ns IS NOT NULL AND s.end_utc_ns > 0 THEN s.end_utc_ns
                  WHEN COALESCE(s.duration_ms,0) > 0 THEN s.start_utc_ns + s.duration_ms*1000000
                  ELSE s.start_utc_ns
                END
              ) > :start_ns

        UNION ALL

        -- branch 2: legacy rows matched by URL
        SELECT
          s.file_path AS path,
          s.start_utc_ns,
          CASE
            WHEN s.end_utc_ns IS NOT NULL AND s.end_utc_ns > 0 THEN s.end_utc_ns
            WHEN COALESCE(s.duration_ms,0) > 0 THEN s.start_utc_ns + s.duration_ms*1000000
            ELSE s.start_utc_ns
          END AS eff_end_ns,
          s.duration_ms
        FROM segments s
        WHERE s.status IN (0,1)
          AND s.camera_id IS NULL
          AND s.camera_url = (SELECT main_url FROM cameras WHERE id=:cid)
          AND s.start_utc_ns < :end_ns
          AND (
                CASE
                  WHEN s.end_utc_ns IS NOT NULL AND s.end_utc_ns > 0 THEN s.end_utc_ns
                  WHEN COALESCE(s.duration_ms,0) > 0 THEN s.start_utc_ns + s.duration_ms*1000000
                  ELSE s.start_utc_ns
                END
              ) > :start_ns
      )
      ORDER BY start_utc_ns
    )SQL");

    q.bindValue(":cid", cameraId);
    q.bindValue(":start_ns", start_ns);
    q.bindValue(":end_ns", end_ns);

    qInfo() << "[SQL] listSegments cid=" << cameraId
            << " day=" << ymd
            << " start_ns=" << start_ns << "end_ns=" << end_ns
            << " start_local=" << d0.toString(Qt::ISODate)
            << " end_local="   << d1.toString(Qt::ISODate);

    if (!q.exec()) { emit error(q.lastError().text()); return; }

    while (q.next()) {
        SegmentInfo s;
        s.path        = q.value(0).toString();
        s.start_ns    = q.value(1).toLongLong();
        s.end_ns      = q.value(2).toLongLong();
        s.duration_ms = q.value(3).toLongLong();
        segs.push_back(s);
    }
    emit segmentsReady(cameraId, segs);
}
void DbReader::listRecentSegments(int limit) {
    QVector<RecentSegment> out;
    QSqlQuery q(db_);
    q.setForwardOnly(true);

    // Use end_utc_ns if set, else derive from duration_ms, else fall back to start_utc_ns
    q.prepare(R"SQL(
      SELECT s.file_path,
             COALESCE(c.name, s.camera_url) AS camera_name,
             s.start_utc_ns,
             CASE
               WHEN s.end_utc_ns IS NOT NULL AND s.end_utc_ns > 0 THEN s.end_utc_ns
               WHEN COALESCE(s.duration_ms,0) > 0 THEN s.start_utc_ns + s.duration_ms*1000000
               ELSE s.start_utc_ns
             END AS eff_end_ns,
             COALESCE(s.duration_ms, 0)
      FROM segments s
      LEFT JOIN cameras c ON c.id = s.camera_id
      WHERE s.status IN (0,1)
      ORDER BY s.start_utc_ns DESC
      LIMIT :lim
    )SQL");
    q.bindValue(":lim", limit);

    if (!q.exec()) { emit error(q.lastError().text()); return; }

    while (q.next()) {
        RecentSegment r;
        r.path        = q.value(0).toString();
        r.camera_name = q.value(1).toString();
        r.start_ns    = q.value(2).toLongLong();
        r.end_ns      = q.value(3).toLongLong();
        r.duration_ms = q.value(4).toLongLong();
        out.push_back(r);
    }
    emit recentSegmentsReady(out);
}
