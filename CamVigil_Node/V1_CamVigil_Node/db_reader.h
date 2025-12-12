#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QVector>
#include <QPair>
#include <QStringList>
#include <QMetaType>

struct SegmentInfo {
    QString path;
    qint64  start_ns;
    qint64  end_ns;
    qint64  duration_ms;
};
Q_DECLARE_METATYPE(SegmentInfo)
using CamList     = QVector<QPair<int, QString>>;
using SegmentList = QVector<SegmentInfo>;
Q_DECLARE_METATYPE(CamList)
Q_DECLARE_METATYPE(SegmentList)
struct RecentSegment {
    QString path;
    QString camera_name;
    qint64  start_ns;
    qint64  end_ns;       // 0 if open-ended
    qint64  duration_ms;  // may be 0 if open-ended
};
Q_DECLARE_METATYPE(RecentSegment)
class DbReader : public QObject {
    Q_OBJECT
public:
    explicit DbReader(QObject* parent=nullptr);
    ~DbReader();

public slots:
    void openAt(const QString& dbPath);                 // read-only connection
    void listCameras();                                 // id + name, only with recordings
    void listDays(int cameraId);                        // distinct YYYY-MM-DD with data
    void listSegments(int cameraId, const QString& ymd);// segments overlapping that day
    void shutdown();
    void listRecentSegments(int limit = 500);
signals:
    void opened(bool ok, QString err);
    void camerasReady(CamList cams);
    void daysReady(int cameraId, QStringList ymdList);
    void segmentsReady(int cameraId, SegmentList segs);
    void error(QString err);
    void recentSegmentsReady(QVector<RecentSegment> segs);
private:
    QSqlDatabase db_;
    QString      connName_; // for QSqlDatabase::removeDatabase
};
