#include "playback_timeline_controller.h"
#include <QDateTime>

static inline qint64 toNs(qint64 s){ return s*1000000000LL; }
qint64 PlaybackTimelineController::dayStartNs(const QDate& d) const {
    const auto dt = QDateTime::fromString(d.toString("yyyy-MM-dd")+" 00:00:00","yyyy-MM-dd HH:mm:ss").toLocalTime();
    return toNs(dt.toSecsSinceEpoch());
}
qint64 PlaybackTimelineController::dayEndNs(const QDate& d) const { return dayStartNs(d.addDays(1)); }

void PlaybackTimelineController::attach(DbReader* r){
    if (db_ == r) return;
        if (db_) QObject::disconnect(db_, nullptr, this, nullptr);
        db_ = r;
        if (db_) {
            connect(db_, &DbReader::segmentsReady,
                    this, &PlaybackTimelineController::onSegmentsReady,
                    Qt::QueuedConnection);
            emit log(QString("[Ctl] attached DbReader=%1")
                     .arg(reinterpret_cast<quintptr>(db_), 0, 16));
        }
}
void PlaybackTimelineController::detach(){
    if (!db_) return;
    QObject::disconnect(db_, nullptr, this, nullptr);
    db_ = nullptr;
    emit log("[Ctl] detached DbReader");
}

void PlaybackTimelineController::onGo(const QString& camName, const QDate& day){
    if (!db_) { emit log("[Ctl] onGo ignored: no DbReader"); return; }
    if (!resolveCamId_) { emit log("[Ctl] onGo ignored: no camera resolver"); return; }
    const int cid = resolveCamId_(camName);
    emit log(QString("[Ctl] onGo: camName='%1' resolved to cid=%2").arg(camName).arg(cid));
    if (cid<=0 || !day.isValid()) { 
        emit log(QString("[Ctl] onGo ignored: cid=%1 day.valid=%2").arg(cid).arg(day.isValid()));
        return; 
    }
    pendingCid_ = cid; pendingDay_ = day;
    emit log(QString("[Go] cid=%1 day=%2").arg(cid).arg(day.toString("yyyy-MM-dd")));
    QMetaObject::invokeMethod(db_, "listSegments", Qt::QueuedConnection,
                              Q_ARG(int, cid),
                              Q_ARG(QString, day.toString("yyyy-MM-dd")));
}

void PlaybackTimelineController::onSegmentsReady(int cameraId, const SegmentList& segs){
    if (cameraId != pendingCid_) return;
    QVector<TimelineSpan> raw; raw.reserve(segs.size());
    for (const auto& s: segs) raw.push_back({s.start_ns, s.end_ns});
    model_.build(dayStartNs(pendingDay_), dayEndNs(pendingDay_), raw);
    emit built(pendingDay_, model_);
    emit log(QString("[Timeline] built spans=%1 covered_s=%2")
             .arg(model_.spans().size())
             .arg(model_.totalCoveredNs()/1e9, 0, 'f', 3));
}
