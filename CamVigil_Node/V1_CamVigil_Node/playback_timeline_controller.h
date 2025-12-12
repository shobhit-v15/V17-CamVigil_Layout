#pragma once
#include <QObject>
#include <QDate>
#include "db_reader.h"
#include "playback_timeline_model.h"

class PlaybackTimelineController : public QObject {
    Q_OBJECT
public:
    explicit PlaybackTimelineController(QObject* parent=nullptr) : QObject(parent) {}
    void attach(DbReader* dbReader);
    void detach();
    void setCameraResolver(const std::function<int(const QString&)>& fn) { resolveCamId_ = fn; }
signals:
    void built(const QDate& day, const PlaybackTimelineModel& model);
    void log(const QString& msg);
public slots:
    void onGo(const QString& camName, const QDate& day);
    void onSegmentsReady(int cameraId, const SegmentList& segs);
private:
    DbReader* db_{nullptr};
    std::function<int(const QString&)> resolveCamId_;
    int pendingCid_{-1};
    QDate pendingDay_;
    PlaybackTimelineModel model_;
    qint64 dayStartNs(const QDate&) const;
    qint64 dayEndNs(const QDate&) const;
};
