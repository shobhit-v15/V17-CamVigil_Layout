#include "archivemanager.h"

#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QFile>
#include <QDebug>
#include <QStorageInfo>
#include <QUuid>
#include <QThread>
#include <QtGlobal>

#include "db_writer.h"
#include "group_repository.h"

// Resolve storage root. Env override supported.
QString ArchiveManager::defaultStorageRoot() {
    const QString env = qEnvironmentVariable("CAMVIGIL_ARCHIVE_ROOT");
    if (!env.isEmpty()) return env;
    return QDir::homePath() + "/CamVigil_StoragePartition";
}

ArchiveManager::ArchiveManager(QObject *parent)
    : QObject(parent),
      defaultDuration(300)  // 5 min
{
    archiveDir = defaultStorageRoot() + "/CamVigilArchives";
    QDir().mkpath(archiveDir);

    // Initial compute of dynamic watermarks
    refreshRetentionWatermarks();

    // Timer: refresh watermarks then purge check
    connect(&cleanupTimer, &QTimer::timeout, this, [this]{
        refreshRetentionWatermarks();
        cleanupArchive();
    });
    cleanupTimer.start(5 * 60 * 1000);  // every 5 minutes

    qDebug() << "[ArchiveManager] Initialized. archiveDir=" << archiveDir;
}

ArchiveManager::~ArchiveManager()
{
    stopRecording();
    if (dbThread) { dbThread->quit(); dbThread->wait(); dbThread = nullptr; }
    qDebug() << "[ArchiveManager] Destroyed.";
}

void ArchiveManager::startRecording(const std::vector<CamHWProfile> &camProfiles)
{
    cameraProfiles = camProfiles;
    archiveDir = defaultStorageRoot() + "/CamVigilArchives";
    QDir().mkpath(archiveDir);

    const QString dbPath = archiveDir + "/camvigil.sqlite";

    {
        GroupRepository groupRepo(dbPath);
        if (!groupRepo.ensureSchemaGroups()) {
            qWarning() << "[Groups] Failed to ensure camera group schema for" << dbPath;
        } else {
            qInfo() << "[Groups] Camera group schema ready for" << dbPath;
        }
    }

    QStorageInfo si(archiveDir);
    if (si.isValid() && si.bytesAvailable() > 0 && si.bytesAvailable() < 5LL*1024*1024*1024) {
        qWarning() << "[ArchiveManager] Low free space in" << archiveDir
                   << "avail=" << si.bytesAvailable();
    }

    if (!dbThread) {
        dbThread = new QThread(this);
        db = new DbWriter();
        db->moveToThread(dbThread);
        connect(dbThread, &QThread::finished, db, &QObject::deleteLater);
        dbThread->start();
        QMetaObject::invokeMethod(db, "openAt", Qt::BlockingQueuedConnection,
                                  Q_ARG(QString, dbPath));
    }

    for (const auto& p : camProfiles) {
        QMetaObject::invokeMethod(db, "ensureCamera", Qt::QueuedConnection,
            Q_ARG(QString, QString::fromStdString(p.url)),
            Q_ARG(QString, QString::fromStdString(p.suburl)),
            Q_ARG(QString, QString::fromStdString(p.displayName)));
    }

    sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QMetaObject::invokeMethod(db, "beginSession", Qt::QueuedConnection,
        Q_ARG(QString, sessionId), Q_ARG(QString, archiveDir), Q_ARG(int, defaultDuration));

    const QDateTime masterStart = QDateTime::currentDateTime();
    qDebug() << "[ArchiveManager] Master start:" << masterStart.toString("yyyyMMdd_HHmmss");

    for (size_t i = 0; i < camProfiles.size(); ++i) {
        const auto &profile = camProfiles[i];
        auto* worker = new ArchiveWorker(profile.url, static_cast<int>(i),
                                         archiveDir, defaultDuration, masterStart);

        connect(worker, &ArchiveWorker::recordingError, [](const std::string &err){
            qDebug() << "[ArchiveManager] ArchiveWorker error:" << QString::fromStdString(err);
        });

        connect(worker, &ArchiveWorker::segmentOpened, this,
            [this, camProfiles](int camIdx, const QString& path, qint64 startNs){
                const QString camUrl = QString::fromStdString(camProfiles[camIdx].url);
                QMetaObject::invokeMethod(db, "addSegmentOpened", Qt::QueuedConnection,
                    Q_ARG(QString, sessionId), Q_ARG(QString, camUrl),
                    Q_ARG(QString, path), Q_ARG(qint64, startNs));
            });

        connect(worker, &ArchiveWorker::segmentClosed, this,
            [this](int camIdx, const QString& path, qint64 endNs, qint64 durMs){
                Q_UNUSED(camIdx);
                QMetaObject::invokeMethod(db, "finalizeSegmentByPath", Qt::QueuedConnection,
                    Q_ARG(QString, path), Q_ARG(qint64, endNs), Q_ARG(qint64, durMs));
            });

        // Trigger purge after each finalized segment
        connect(worker, &ArchiveWorker::segmentFinalized, this, &ArchiveManager::segmentWritten);
        connect(this, &ArchiveManager::segmentWritten, this, &ArchiveManager::cleanupArchive);

        workers.push_back(worker);
        worker->start();
        qDebug() << "[ArchiveManager] Started ArchiveWorker for cam" << i;
    }

    qDebug() << "[ArchiveManager] Recording at" << archiveDir;

    // Kick an immediate check at startup
    QTimer::singleShot(0, this, [this]{ refreshRetentionWatermarks(); cleanupArchive(); });
}

// -----------------------------------------------

void ArchiveManager::stopRecording()
{
    for (auto* worker : workers) { worker->stop(); worker->wait(); delete worker; }
    workers.clear();
    qDebug() << "[ArchiveManager] All ArchiveWorkers stopped.";
}

void ArchiveManager::updateSegmentDuration(int seconds)
{
    qDebug() << "[ArchiveManager] Update segment duration to" << seconds << "s";
    for (auto *worker : workers)
        QMetaObject::invokeMethod(worker, "updateSegmentDuration", Qt::QueuedConnection,
                                  Q_ARG(int, seconds));
}

// ---------- Dynamic watermarks ----------

static double envPct(const char* name, double fallbackPct)
{
    bool ok=false;
    const QString s = qEnvironmentVariable(name);
    if (!s.isEmpty()) {
        const double v = s.toDouble(&ok);
        if (ok) return qBound(0.0, v/100.0, 0.95); // 72 -> 0.72
    }
    return qBound(0.0, fallbackPct/100.0, 0.95);    // << divide fallback too
}


void ArchiveManager::refreshRetentionWatermarks()
{
    QStorageInfo si(archiveDir);
    if (!si.isValid() || si.bytesTotal() <= 0) return;

    const qint64 total = si.bytesTotal();

    // Defaults: start at 10% free, recover to 12% free. Overridable via env.
    const double minPct    = envPct("CAMVIGIL_MIN_FREE_PCT",    70.0);
    const double targetPct = envPct("CAMVIGIL_TARGET_FREE_PCT", 72.0);

    rcfg_.minFreeBytes    = static_cast<qint64>(total * minPct);
    rcfg_.targetFreeBytes = static_cast<qint64>(total * targetPct);
    rcfg_.highWaterPct    = 90; // 90% used is an alternate trigger

    qInfo() << "[Purge] watermarks set:"
            << "total=" << total
            << "minFreeBytes=" << rcfg_.minFreeBytes
            << "targetFreeBytes=" << rcfg_.targetFreeBytes
            << "highWater%=" << rcfg_.highWaterPct;
}

// ---------- Ring-buffer helpers ----------

bool ArchiveManager::shouldPurge_(qint64& needBytes, qint64& availBytes) {
    QStorageInfo si(archiveDir);
    if (!si.isValid()) return false;
    const qint64 total = si.bytesTotal();
    availBytes = si.bytesAvailable();
    if (total <= 0) return false;
    const int usedPct = int((total - availBytes) * 100 / total);

    const bool trigger = (availBytes < rcfg_.minFreeBytes) || (usedPct >= rcfg_.highWaterPct);
    qInfo() << "[Purge] check avail=" << availBytes
            << "total=" << total
            << "used%=" << usedPct
            << "minFree=" << rcfg_.minFreeBytes
            << "targetFree=" << rcfg_.targetFreeBytes
            << "trigger=" << trigger;

    if (trigger) {
        needBytes = qMax<qint64>(rcfg_.targetFreeBytes - availBytes, 0);
        return needBytes > 0;
    }
    return false;
}

bool ArchiveManager::purgeOnce_(qint64& freedBytes) {
    freedBytes = 0;
    QVector<QPair<qint64, QString>> victims;
    const bool okFetch = QMetaObject::invokeMethod(
        db, [&]{ victims = db->oldestFinalizedUnpinned(rcfg_.purgeBatchFiles, 0, rcfg_.perCameraMinDays); },
        Qt::BlockingQueuedConnection);
    if (!okFetch || victims.isEmpty()) return false;

    qInfo() << "[Purge] batch candidates=" << victims.size()
            << "batch_limit=" << rcfg_.purgeBatchFiles;

    for (const auto& v : victims) {
        const qint64 id = v.first;
        const QString path = v.second;

        QFile f(path);
        const qint64 sz = f.exists() ? f.size() : 0;

        bool ok = !f.exists() || f.remove();
        if (!ok) { QThread::msleep(50); ok = !f.exists() || f.remove(); }
        if (!ok) { qWarning() << "[Purge] unlink failed:" << path; continue; }

        bool rowOk = false;
        QMetaObject::invokeMethod(db, [&]{ rowOk = db->deleteSegmentRow(id); },
                                  Qt::BlockingQueuedConnection);
        if (!rowOk) { qWarning() << "[Purge] DB row delete failed id=" << id; continue; }

        freedBytes += sz;

        QStorageInfo si(archiveDir);
        if (si.isValid()) {
            const qint64 freeNow = si.bytesAvailable();
            qInfo() << "[Purge] deleted" << path << "size=" << sz
                    << "free_now=" << freeNow;
            if (freeNow >= rcfg_.targetFreeBytes) break;
        }
    }
    return true;
}

// ---------- Purge entry point ----------

void ArchiveManager::cleanupArchive()
{
    if (archiveDir.isEmpty()) return;
    if (!QDir(archiveDir).exists()) return;
    if (purgeRunning_.fetchAndStoreOrdered(1) == 1) return;

    qint64 need=0, avail=0;
    if (!shouldPurge_(need, avail)) {
        purgeRunning_.storeRelease(0);
        return;
    }

    qint64 totalFreed = 0;
    while (true) {
        qint64 freed = 0;
        const bool any = purgeOnce_(freed);
        totalFreed += freed;

        QStorageInfo si(archiveDir);
        const bool done = !si.isValid()
                       || si.bytesAvailable() >= rcfg_.targetFreeBytes
                       || !any;
        if (done) break;

        QThread::msleep(20);
    }

    QStorageInfo si2(archiveDir);
    qInfo() << "[Purge] exit freed_total=" << totalFreed
            << "free_now=" << (si2.isValid() ? si2.bytesAvailable() : -1);
    purgeRunning_.storeRelease(0);
}
