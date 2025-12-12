#include "playback_timeline_model.h"
#include <algorithm>

static inline TimelineSpan clip(const TimelineSpan& s, qint64 a, qint64 b) {
    TimelineSpan c{ std::max(s.start_ns, a), std::min(s.end_ns, b) };
    if (c.end_ns < c.start_ns) c.end_ns = c.start_ns;
    return c;
}

void PlaybackTimelineModel::build(qint64 dayStartNs, qint64 dayEndNs,
                                  const QVector<TimelineSpan>& raw) {
    t0_ = dayStartNs; t1_ = dayEndNs; spans_.clear();

    // 1) Clip-by-day and drop outside
    QVector<TimelineSpan> v; v.reserve(raw.size());
    for (const auto& s : raw) {
        if (s.end_ns <= dayStartNs || s.start_ns >= dayEndNs) continue;
        v.push_back(clip(s, dayStartNs, dayEndNs));
    }
    if (v.isEmpty()) return;

    // 2) Sort by start
    std::sort(v.begin(), v.end(), [](auto& a, auto& b){ return a.start_ns < b.start_ns; });

    // 3) Cap each segment's end to the next segment's start
    for (int i = 0; i < v.size(); ++i) {
        // Repair pathological ends (unknown end -> zero-length already from DB)
        if (v[i].end_ns < v[i].start_ns) v[i].end_ns = v[i].start_ns;

        if (i + 1 < v.size()) {
            const qint64 nextStart = v[i+1].start_ns;
            if (v[i].end_ns > nextStart) {
                v[i].end_ns = std::max(v[i].start_ns, nextStart);
            }
        }
        // re-clip for safety
        v[i] = clip(v[i], dayStartNs, dayEndNs);
    }

    // 4) Merge only true overlaps/touches; gaps will remain
    for (const auto& s : v) {
        if (spans_.isEmpty() || s.start_ns > spans_.last().end_ns) {
            spans_.push_back(s);
        } else {
            spans_.last().end_ns = std::max(spans_.last().end_ns, s.end_ns);
        }
    }
}


qreal PlaybackTimelineModel::fractionFor(qint64 t_ns) const {
    if (t1_ <= t0_) return 0.0;
    if (t_ns <= t0_) return 0.0;
    if (t_ns >= t1_) return 1.0;
    return qreal(t_ns - t0_) / qreal(t1_ - t0_);
}

qint64 PlaybackTimelineModel::totalCoveredNs() const {
    qint64 sum = 0;
    for (const auto& s : spans_) sum += std::max<qint64>(0, s.end_ns - s.start_ns);
    return sum;
}
