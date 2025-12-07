#include "playback_segment_index.h"
#include <QDebug>
#include <algorithm>
 static inline qint64 clamp(qint64 v, qint64 lo, qint64 hi){
     // correct clamp: min(max(v, lo), hi)
     if (hi < lo) std::swap(lo, hi);
     return std::min(std::max(v, lo), hi);
 }
static inline double sec(qint64 ns) { return double(ns) / 1e9; }

void PlaybackSegmentIndex::build(const SegmentList& segs, qint64 dayStartNs, qint64 dayEndNs)
{
    list_.clear();
    gaps_.clear();
    starts_.clear();
    t0_ = dayStartNs;
    t1_ = dayEndNs;

    if (t1_ <= t0_) {
        qWarning() << "[SegIndex] invalid day window" << t0_ << t1_;
        return;
    }

    qInfo() << "[SegIndex] build t0=" << t0_ << " t1=" << t1_
                << " in.size=" << segs.size();
        int printed = 0;
        // 1) Normalize and clip to the day window
    QVector<FileSeg> raw; raw.reserve(segs.size());
    for (const auto& s : segs) {
        qint64 a = clamp(s.start_ns, t0_, t1_);
        qint64 b = clamp(s.end_ns,   t0_, t1_);
        if (printed < 8) {
                    qInfo() << "[SegIndex] in start=" << s.start_ns << " end=" << s.end_ns
                            << " -> clipped a=" << a << " b=" << b;
                    ++printed;
                }
        if (b <= a) continue; // drop zero/neg
        FileSeg fs{ s.path, a, b };
        raw.push_back(fs);
    }

    if (raw.isEmpty()) {
        qInfo() << "[SegIndex] no segments within day";
        return;
    }

    // 2) Sort by start time (stable)
    std::stable_sort(raw.begin(), raw.end(),
                     [](const FileSeg& x, const FileSeg& y){ return x.start_ns < y.start_ns; });

    // 3) Build final list and detect *significant* gaps (> gapThrNs_)
    qint64 lastEnd = t0_;
    list_.reserve(raw.size());
    for (const auto& fs : raw) {
        // Gap from lastEnd -> fs.start_ns (if significant)
        if (fs.start_ns > lastEnd && (fs.start_ns - lastEnd) > gapThrNs_) {
            gaps_.push_back({ lastEnd, fs.start_ns });
        }
        // Clamp overlaps to monotonic progression (prefer earlier segment)
        qint64 start = qMax(fs.start_ns, lastEnd); // avoid negative "gaps" on overlaps
        if (fs.end_ns > start) {
            list_.push_back({ fs.path, start, fs.end_ns });
            lastEnd = fs.end_ns;
        }
    }

    // Tail gap (end of last segment -> dayEnd)
    if (!list_.isEmpty()) {
        qint64 tailGapStart = list_.last().end_ns;
        if (t1_ > tailGapStart && (t1_ - tailGapStart) > gapThrNs_) {
            gaps_.push_back({ tailGapStart, t1_ });
        }
    } else {
        // No segments added because of overlaps—treat full day as a gap
        gaps_.push_back({ t0_, t1_ });
    }

    // 4) Precompute starts for binary search
    starts_.reserve(list_.size());
    for (const auto& fs : list_) starts_.push_back(fs.start_ns);

    // Debug summary
    qInfo() << "[SegIndex] built"
            << "segs=" << list_.size()
            << "gaps=" << gaps_.size()
            << "covered_s=" << sec(totalCoveredNs())
            << "span_s="    << sec(totalSpanNs())
            << "thr_s="     << sec(gapThrNs_);
}

qint64 PlaybackSegmentIndex::totalCoveredNs() const {
    qint64 sum = 0;
    for (const auto& s : list_) sum += s.duration_ns();
    return sum;
}

bool PlaybackSegmentIndex::mapWallClock(qint64 wall_ns, int& segIndex, qint64& offsetIntoFile_ns) const
{
    segIndex = -1; offsetIntoFile_ns = 0;
    if (list_.isEmpty()) return false;

    // If before first start, fail fast.
    if (wall_ns < list_.first().start_ns) return false;

    // Find last segment whose start <= wall_ns.
    auto it = std::upper_bound(starts_.begin(), starts_.end(), wall_ns);
    int idx = int(it - starts_.begin()) - 1;
    if (idx < 0) return false;

    const auto& s = list_[idx];
    if (wall_ns >= s.end_ns) return false; // falls into a gap or beyond

    segIndex = idx;
    offsetIntoFile_ns = wall_ns - s.start_ns;
    return true;
}

int PlaybackSegmentIndex::nextSegmentIndexAfter(qint64 wall_ns) const
{
    if (list_.isEmpty()) return -1;
    // First segment with start > wall_ns
    auto it = std::upper_bound(starts_.begin(), starts_.end(), wall_ns);
    int idx = int(it - starts_.begin());
    return (idx >= 0 && idx < list_.size()) ? idx : -1;
}

void PlaybackSegmentIndex::exportForStitching(QVector<QString>& paths,
                                              QVector<qint64>&  wallStarts,
                                              QVector<qint64>&  offsets,
                                              QVector<qint64>&  durations) const
{
    paths.clear(); wallStarts.clear(); offsets.clear(); durations.clear();
    paths.reserve(list_.size());
    wallStarts.reserve(list_.size());
    offsets.reserve(list_.size());
    durations.reserve(list_.size());

    qint64 acc = 0;
    for (const auto& s : list_) {
        paths     << s.path;
        wallStarts<< (s.start_ns - t0_);      // wall ns since dayStart()
        offsets   << acc;                     // virtual (gapless) cumulative
        const qint64 dur = s.duration_ns();
        durations << dur;
        acc      += dur;
    }
}

void PlaybackSegmentIndex::debugDump(const char* tag) const
{
    auto totalSpan = totalSpanNs();
    qInfo().noquote() << QString("[%1] window %2 .. %3 (span %4 s)")
        .arg(tag).arg(t0_).arg(t1_).arg(double(totalSpan)/1e9, 0, 'f', 3);
    for (int i=0;i<list_.size();++i){
        const auto& s = list_[i];
        qInfo().noquote() << QString("  seg[%1]: %2 .. %3  dur=%.3fs  path=%4")
            .arg(i).arg(s.start_ns).arg(s.end_ns).arg(double(s.duration_ns())/1e9).arg(s.path);
    }
    for (int i=0;i<gaps_.size();++i){
        const auto& g = gaps_[i];
        qInfo().noquote() << QString("  GAP[%1]: %2 .. %3  dur=%.3fs")
            .arg(i).arg(g.start_ns).arg(g.end_ns).arg(double(g.duration_ns())/1e9);
    }
}
