#ifndef ARCHIVEMANAGER_H
#define ARCHIVEMANAGER_H

#include <QObject>
#include <QTimer>
#include <QThread>
#include <QAtomicInt>
#include <vector>
#include <string>

#include "archiveworker.h"
#include "camerastreams.h" // CamHWProfile

class DbWriter;

// Dynamic, size-based ring buffer config.
// minFreeBytes/targetFreeBytes are computed from total capacity by refreshRetentionWatermarks().
// Override percentages via env:
//   CAMVIGIL_MIN_FREE_PCT    (default 10)
//   CAMVIGIL_TARGET_FREE_PCT (default 12)
struct RetentionCfg {
    qint64 minFreeBytes     = 0;   // computed each refresh
    qint64 targetFreeBytes  = 0;   // computed each refresh
    int    highWaterPct     = 90;  // purge if used% >= this
    int    purgeBatchFiles  = 64;  // delete in batches
    int    perCameraMinDays = 0;   // keep N days per camera (0=off)
};

class ArchiveManager : public QObject {
    Q_OBJECT
public:
    explicit ArchiveManager(QObject* parent = nullptr);
    ~ArchiveManager();

    QString archiveRoot() const { return archiveDir; }
    QString getArchiveDir() const { return archiveDir; }
    QString databasePath() const { return archiveDir + "/camvigil.sqlite"; }

    void startRecording(const std::vector<CamHWProfile>& cameraProfiles);
    void stopRecording();
    void updateSegmentDuration(int seconds);

    static QString defaultStorageRoot();

public slots:
    void cleanupArchive(); // ring-buffer purge

signals:
    void segmentWritten(); // emitted after a segment finalizes

private:
    // timers/workers
    QTimer cleanupTimer;
    std::vector<ArchiveWorker*> workers;
    QString archiveDir;
    int defaultDuration;  // seconds
    std::vector<CamHWProfile> cameraProfiles;

    // DB
    QThread*  dbThread = nullptr;
    DbWriter* db       = nullptr;
    QString   sessionId;

    // retention
    RetentionCfg rcfg_;
    QAtomicInt   purgeRunning_{0}; // 0=idle,1=running

    // helpers
    void refreshRetentionWatermarks();     // compute bytes from % of total
    bool shouldPurge_(qint64& needBytes, qint64& availBytes);
    bool purgeOnce_(qint64& freedBytes);
};

#endif // ARCHIVEMANAGER_H
