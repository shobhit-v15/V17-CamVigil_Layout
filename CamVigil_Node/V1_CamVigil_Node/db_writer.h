#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVector>
#include <QPair>
class DbWriter : public QObject {
    Q_OBJECT
public:
    explicit DbWriter(QObject* parent=nullptr);
    ~DbWriter();

public slots:
    bool openAt(const QString& dbFile);
    void ensureCamera(const QString& mainUrl, const QString& subUrl, const QString& name);
    void beginSession(const QString& sessionId, const QString& archiveDir, int segmentSec);
    void addSegmentOpened(const QString& sessionId, const QString& cameraUrl,
                          const QString& filePath, qint64 startUtcNs);
    void finalizeSegmentByPath(const QString& filePath, qint64 endUtcNs, qint64 durationMs);
    void markError(const QString& where, const QString& detail);
    QVector<QPair<qint64, QString>> oldestFinalizedUnpinned(int limit, int cameraId = 0, int minDays = 0);
    bool deleteSegmentRow(qint64 segmentId);
    bool markPinned(const QString& filePath, bool pinned);
    void checkpointWal();
private:
    bool ensureSchema();
    bool migrateSchema_();
    bool exec(const QString& sql);
    QSqlDatabase db_;
};
