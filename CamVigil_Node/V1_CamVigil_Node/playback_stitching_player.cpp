#include "playback_stitching_player.h"
#include "playback_video_player_gst.h"
#include <QMetaObject>
#include <QDebug>

PlaybackStitchingPlayer::PlaybackStitchingPlayer(QObject* parent)
    : QObject(parent) {}

void PlaybackStitchingPlayer::attachPlayer(PlaybackVideoPlayerGst* p) {
    player_ = p;
    if (!player_) return;

    // Player → Stitching (queued)
    connect(player_, SIGNAL(eos()),                this, SLOT(onPlayerEos()), Qt::QueuedConnection);
    connect(player_, SIGNAL(errorText(QString)),   this, SIGNAL(errorText(QString)), Qt::QueuedConnection);
    connect(player_, SIGNAL(positionNs(qint64)),   this, SLOT(onPlayerPos(qint64)), Qt::QueuedConnection);

    // Push current rate
    playerSetRate(rate_);
}

void PlaybackStitchingPlayer::setPlaylist(QVector<SegmentMeta> metas, qint64 day_start_ns) {
    qInfo() << "[Stitch] setPlaylist called with" << metas.size() << "segments";
    
    paths_.clear(); wallStarts_.clear(); offsets_.clear(); durations_.clear();
    totalVirt_ = 0; curIdx_ = -1; dayStartNs_ = day_start_ns;
    isPlaying_ = false; // Reset playing state

    paths_.reserve(metas.size());
    wallStarts_.reserve(metas.size());
    offsets_.reserve(metas.size());
    durations_.reserve(metas.size());

    for (const auto& m : metas) {
        paths_     << m.path;
        wallStarts_<< m.wall_start_ns;
        offsets_   << m.offset_ns;
        durations_ << m.duration_ns;
        totalVirt_  = qMax(totalVirt_, m.offset_ns + m.duration_ns);
    }
    
    qInfo() << "[Stitch] Playlist set - segments:" << paths_.size() 
            << "total duration:" << (totalVirt_ / 1e9) << "seconds";
}

void PlaybackStitchingPlayer::play() {
    qInfo() << "[Stitch] play() called - hasPlaylist:" << hasPlaylist() 
            << "isPlaying:" << isPlaying_;
    
    if (!hasPlaylist()) {
        emit errorText("Cannot play: No playlist loaded");
        return;
    }
    
    if (!isPlaying_) {
            // Start at the beginning unless already opened
            if (curIdx_ < 0) playAtVirtual(0);
            else { playerPlay(); emit stateChanged(true); isPlaying_ = true; }
        }
}

void PlaybackStitchingPlayer::pause() {
    qInfo() << "[Stitch] pause() called - isPlaying:" << isPlaying_;
    
    if (!isPlaying_) {
        qInfo() << "[Stitch] Not playing, ignoring pause command";
        return;
    }
    
    playerPause();
    isPlaying_ = false;
    emit stateChanged(false);
}

void PlaybackStitchingPlayer::stop() {
    qInfo() << "[Stitch] stop() called";
    playerStop();
    curIdx_ = -1;
    isPlaying_ = false;
    emit stateChanged(false);
}

void PlaybackStitchingPlayer::setRate(double r) {
    rate_ = (r == 0.0 ? 1.0 : r);
    playerSetRate(rate_);
}

void PlaybackStitchingPlayer::playAtVirtual(qint64 virt_ns) {
    qInfo() << "[Stitch] playAtVirtual called with virt_ns:" << virt_ns;
    
    if (paths_.isEmpty() || !player_) {
        qWarning() << "[Stitch] Cannot play: paths_.isEmpty()" << paths_.isEmpty() 
                   << "player_ is null:" << (player_ == nullptr);
        return;
    }
    
    if (virt_ns < 0) virt_ns = 0;
    if (virt_ns >= totalVirt_) virt_ns = totalVirt_ - 1;

    qint64 inSeg=0;
    int idx=0;
    if (!computeIndexFromVirtual(virt_ns, idx, inSeg)) {
        qWarning() << "[Stitch] Failed to compute index from virtual position:" << virt_ns;
        return;
    }

    qInfo() << "[Stitch] Opening segment" << idx << "at position" << inSeg;
    if (idx != curIdx_) openIndex(idx);
    playerSeek(inSeg);
    playerPlay();
    
    isPlaying_ = true;
    emit stateChanged(true);
}

void PlaybackStitchingPlayer::seekWall(qint64 wall_ns) {
    if (paths_.isEmpty() || !player_) return;
    qint64 inSeg=0;
    int idx=0;
    if (!computeIndexFromWall(wall_ns, idx, inSeg)) {
        // seek landed in a gap → choose next segment if any
        for (int i=0;i<wallStarts_.size();++i) {
            if (wall_ns < wallStarts_[i]) { idx=i; inSeg=0; goto OPEN; }
        }
        return;
    }
OPEN:
    if (idx != curIdx_) openIndex(idx);
    playerSeek(inSeg);
    playerPlay();
    // reflect actual playing state so Pause works immediately after a drag seek
    if (!isPlaying_) {
    isPlaying_ = true;
    emit stateChanged(true);
    }
}

void PlaybackStitchingPlayer::onPlayerEos() {
    qInfo() << "[Stitch] onPlayerEos - current segment:" << curIdx_ 
            << "total segments:" << paths_.size();
    
    const int next = curIdx_ + 1;
    if (next >= 0 && next < paths_.size()) {
        qInfo() << "[Stitch] Moving to next segment:" << next;
        openIndex(next);
        playerSeek(0);
        playerPlay();
        // Keep isPlaying_ = true since we're continuing to next segment
    } else {
        qInfo() << "[Stitch] Reached end of playlist";
        isPlaying_ = false;
        emit stateChanged(false);
        emit reachedEnd();
    }
}

void PlaybackStitchingPlayer::onPlayerPos(qint64 in_seg_pos_ns) {
    if (curIdx_ < 0 || curIdx_ >= offsets_.size()) return;
    const qint64 virt = offsets_[curIdx_] + in_seg_pos_ns;
    const qint64 wall = virtualToWall(virt); // absolute within day
    emit wallPositionNs(wall - dayStartNs_);
}

void PlaybackStitchingPlayer::openIndex(int idx) {
    curIdx_ = idx;
    emit segmentChanged(curIdx_);
    playerOpen(paths_[curIdx_]);
    playerSetRate(rate_);
}

bool PlaybackStitchingPlayer::computeIndexFromWall(qint64 wall_ns, int& idx, qint64& in_seg_ns) const {
    // Find segment i where wall_ns ∈ [wallStarts[i], wallStarts[i]+dur)
    int lo=0, hi=wallStarts_.size()-1, ans=-1;
    while (lo<=hi) {
        int mid=(lo+hi)/2;
        const qint64 s = wallStarts_[mid];
        const qint64 e = s + durations_[mid];
        if (wall_ns < s) hi=mid-1;
        else if (wall_ns >= e) lo=mid+1;
        else { ans=mid; break; }
    }
    if (ans<0) return false;
    idx = ans;
    in_seg_ns = wall_ns - wallStarts_[ans];
    return true;
}

bool PlaybackStitchingPlayer::computeIndexFromVirtual(qint64 virt_ns, int& idx, qint64& in_seg_ns) const {
    // Find segment i where virt_ns ∈ [offsets[i], offsets[i]+dur)
    int lo=0, hi=offsets_.size()-1, ans=-1;
    while (lo<=hi) {
        int mid=(lo+hi)/2;
        const qint64 s = offsets_[mid];
        const qint64 e = s + durations_[mid];
        if (virt_ns < s) hi=mid-1;
        else if (virt_ns >= e) lo=mid+1;
        else { ans=mid; break; }
    }
    if (ans<0) return false;
    idx = ans;
    in_seg_ns = virt_ns - offsets_[ans];
    return true;
}

qint64 PlaybackStitchingPlayer::virtualToWall(qint64 virt_ns) const {
    int idx=0; qint64 inSeg=0;
    if (!computeIndexFromVirtual(virt_ns, idx, inSeg)) return dayStartNs_;
    return wallStarts_[idx] + inSeg;
}

// ---------- Player invocations (queued) ----------
void PlaybackStitchingPlayer::playerOpen(const QString& path) {
    if (!player_) return;
    QMetaObject::invokeMethod(player_, "open", Qt::QueuedConnection,
                              Q_ARG(QString, path));
}
void PlaybackStitchingPlayer::playerPlay() {
    if (!player_) return;
    QMetaObject::invokeMethod(player_, "play", Qt::QueuedConnection);
}
void PlaybackStitchingPlayer::playerPause() {
    if (!player_) return;
    QMetaObject::invokeMethod(player_, "pause", Qt::QueuedConnection);
}
void PlaybackStitchingPlayer::playerStop() {
    if (!player_) return;
    QMetaObject::invokeMethod(player_, "stop", Qt::QueuedConnection);
}
void PlaybackStitchingPlayer::playerSeek(qint64 in_seg_ns) {
    if (!player_) return;
    QMetaObject::invokeMethod(player_, "seekNs", Qt::QueuedConnection,
                              Q_ARG(qint64, in_seg_ns));
}
void PlaybackStitchingPlayer::playerSetRate(double r) {
    if (!player_) return;
    QMetaObject::invokeMethod(player_, "setRate", Qt::QueuedConnection,
                              Q_ARG(double, r));
}
