#pragma once
#include <QVector>
#include <QString>

struct TimelineSpan {
    qint64 start_ns{0};
    qint64 end_ns{0};
};

class PlaybackTimelineModel {
public:
    void build(qint64 dayStartNs, qint64 dayEndNs,
               const QVector<TimelineSpan>& rawSegments);
    const QVector<TimelineSpan>& spans() const { return spans_; }
    qreal fractionFor(qint64 t_ns) const; // 0..1 position helper
    qint64 totalCoveredNs() const;
private:
    QVector<TimelineSpan> spans_;
    qint64 t0_{0}, t1_{0};
};
