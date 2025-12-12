#pragma once
#include <QObject>
#include <QString>
#include <QVector>
#include <QMetaType>
class PlaybackVideoPlayerGst;

// Global metatype (must be declared at global scope)
struct SegmentMeta {
    QString path;
    qint64  wall_start_ns;  // absolute wall time within the day (ns from midnight local)
    qint64  offset_ns;      // virtual (gapless) base offset
    qint64  duration_ns;    // length to play
};
Q_DECLARE_METATYPE(SegmentMeta)

/**
 * Gapless stitching controller that plays a day’s worth of clips
 * as a continuous virtual timeline (gaps skipped).
 *
 * Thread model:
 * - This object is moved to its own QThread by the owner.
 * - It calls the GStreamer player via queued invokeMethod (player is in its own thread).
 */
class PlaybackStitchingPlayer : public QObject {
    Q_OBJECT
public:
    explicit PlaybackStitchingPlayer(QObject* parent=nullptr);
    
    // State queries
    bool isPlaying() const { return isPlaying_; }
    bool hasPlaylist() const { return !paths_.isEmpty(); }
    int currentSegment() const { return curIdx_; }
    
public slots:
    void attachPlayer(PlaybackVideoPlayerGst* player);
    void setPlaylist(QVector<SegmentMeta> metas, qint64 day_start_ns);

    void play();                 // start from beginning (virtual 0)
    void pause();
    void stop();

    void playAtVirtual(qint64 virt_ns);   // start/resume at virtual position
    void seekWall(qint64 wall_ns);        // wall clock seek (ns from midnight absolute)

    void setRate(double r);

signals:
    void errorText(QString);
    void reachedEnd();
    void wallPositionNs(qint64 wall_ns_from_midnight); // emit absolute wall ns since midnight
    void segmentChanged(int idx);
    void stateChanged(bool playing); // NEW: emit when play/pause state changes

private slots:
    // slots to receive player feedback (queued from player thread)
    void onPlayerEos();
    void onPlayerPos(qint64 in_seg_pos_ns);

private:
    // helpers
    void openIndex(int idx);
    bool computeIndexFromWall(qint64 wall_ns, int& idx, qint64& in_seg_ns) const;
    bool computeIndexFromVirtual(qint64 virt_ns, int& idx, qint64& in_seg_ns) const;
    qint64 virtualToWall(qint64 virt_ns) const;

    // invoke helpers (queued to player thread)
    void playerOpen(const QString& path);
    void playerPlay();
    void playerPause();
    void playerStop();
    void playerSeek(qint64 in_seg_ns);
    void playerSetRate(double r);

    PlaybackVideoPlayerGst* player_ = nullptr; // lives in another thread

    QVector<QString> paths_;
    QVector<qint64>  wallStarts_;
    QVector<qint64>  offsets_;    // virtual offset base per segment
    QVector<qint64>  durations_;
    qint64           totalVirt_ = 0;
    qint64           dayStartNs_ = 0;

    int              curIdx_ = -1;
    double           rate_   = 1.0;
    bool             isPlaying_ = false; // NEW: track play/pause state
};
Q_DECLARE_METATYPE(QVector<SegmentMeta>)
