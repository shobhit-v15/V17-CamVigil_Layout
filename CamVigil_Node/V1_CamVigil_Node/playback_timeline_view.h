#pragma once
#include <QWidget>
#include <QElapsedTimer>
#include "playback_timeline_model.h"

class PlaybackTimelineView : public QWidget {
Q_OBJECT
public:
explicit PlaybackTimelineView(QWidget* parent=nullptr);
void setModel(const PlaybackTimelineModel* m);
void setPlayheadNs(qint64 ns_from_midnight);
qint64 playheadNs() const { return playheadNs_; }
// --- Trim selection API ---
void setSelection(qint64 start_ns, qint64 end_ns, bool enabled);
bool selectionEnabled() const { return selEnabled_; }
qint64 selectionStartNs() const { return selStartNs_; }
qint64 selectionEndNs()   const { return selEndNs_; }

signals:
void hoverTimeNs(qint64 t_ns);
void seekRequested(qint64 t_ns);
void selectionChanged(qint64 start_ns, qint64 end_ns); //trim selection
protected:
void paintEvent(QPaintEvent*) override;
void mouseMoveEvent(QMouseEvent*) override;
void mousePressEvent(QMouseEvent*) override;
void mouseReleaseEvent(QMouseEvent*) override;
void leaveEvent(QEvent*) override;
QSize sizeHint() const override { return {800, 64}; }

private:
const PlaybackTimelineModel* model_{nullptr};
QRect  barRect() const;
qint64 dayNs_() const { return 24LL*3600LL*1000000000LL; }

qint64       playheadNs_ = 0;
bool         dragging_   = false;
QElapsedTimer dragTick_;
static constexpr int kDragEmitMs = 90; // ~11 Hz while dragging

//trim selecion
qint64 posToNs_(int x, const QRect& bar) const;
QRect handleRectAt_(qint64 ns, const QRect& bar) const;
// --- Selection state ---
bool   selEnabled_ = false;
qint64 selStartNs_ = 0;
qint64 selEndNs_   = 0;
enum class DragKind { None, Playhead, StartHandle, EndHandle } dragKind_ = DragKind::None;
static constexpr int kHandlePx = 8; // visual + hit area half-width
void clampSelection_();

};

