#pragma once
#include <QVector>
#include <QString>
#include <QtGlobal>
#include <algorithm>
#include "db_reader.h"   // SegmentInfo / SegmentList

// Index of same-day file segments with gap awareness.
// - Keeps file boundaries (no merging across files).
// - Records "significant" gaps (> gapThresholdNs), tolerates tiny jitter.
// - Fast wall-clock -> (segment, offset) mapping.
// - Exports arrays for the stitching player (paths, wallStarts, offsets, durations).
class PlaybackSegmentIndex final {
public:
    struct FileSeg {
        QString path;
        qint64  start_ns = 0;     // wall-clock ns (UTC epoch)
        qint64  end_ns   = 0;     // exclusive
        qint64  duration_ns() const { return qMax<qint64>(0, end_ns - start_ns); }
    };
    struct Gap {
        qint64 start_ns = 0;      // gap start (exclusive end of previous seg)
        qint64 end_ns   = 0;      // gap end   (start of next seg)
        qint64 duration_ns() const { return qMax<qint64>(0, end_ns - start_ns); }
    };

    void   setGapThresholdNs(qint64 ns) { gapThrNs_ = qMax<qint64>(0, ns); }
    qint64 gapThresholdNs() const       { return gapThrNs_; }

    // Build from DB segments for a single day window [dayStartNs, dayEndNs).
    void build(const SegmentList& segs, qint64 dayStartNs, qint64 dayEndNs);

    bool empty() const { return list_.isEmpty(); }

    const QVector<FileSeg>& playlist() const { return list_; }
    const QVector<Gap>&     gaps()     const { return gaps_; }

    qint64 dayStart() const { return t0_; }
    qint64 dayEnd()   const { return t1_; }
    qint64 firstNs()  const { return list_.isEmpty() ? t0_ : list_.first().start_ns; }
    qint64 lastNs()   const { return list_.isEmpty() ? t0_ : list_.last().end_ns;  }

    qint64 totalCoveredNs() const;
    qint64 totalSpanNs()    const { return qMax<qint64>(0, t1_ - t0_); }

    // If wall_ns falls inside a segment -> true and returns (segIndex, offsetIntoFile_ns).
    bool mapWallClock(qint64 wall_ns, int& segIndex, qint64& offsetIntoFile_ns) const;

    // Next playable segment strictly after wall_ns (or -1 if none).
    int  nextSegmentIndexAfter(qint64 wall_ns) const;

    // Export arrays for stitching player:
    //  - paths:      file paths
    //  - wallStarts: start times since dayStart() (ns)
    //  - offsets:    cumulative "virtual" offsets (gapless) per segment (ns)
    //  - durations:  segment durations (ns)
    void exportForStitching(QVector<QString>& paths,
                            QVector<qint64>&  wallStarts,
                            QVector<qint64>&  offsets,
                            QVector<qint64>&  durations) const;

    // Log a human-readable dump.
    void debugDump(const char* tag = "SegIndex") const;

private:
    QVector<FileSeg>  list_;
    QVector<Gap>      gaps_;
    QVector<qint64>   starts_;     // for binary search
    qint64            t0_ = 0;
    qint64            t1_ = 0;
    qint64            gapThrNs_ = 2000000000LL; // 2s tolerance for tiny splits
};
