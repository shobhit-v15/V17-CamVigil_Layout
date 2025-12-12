#include "playback_timeline_view.h"
#include <QPainter>
#include <QMouseEvent>
#include <QtMath>

PlaybackTimelineView::PlaybackTimelineView(QWidget* parent)
: QWidget(parent)
{
setMouseTracking(true);
setMinimumHeight(56);
setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void PlaybackTimelineView::setModel(const PlaybackTimelineModel* m) {
model_ = m;
update();
}

void PlaybackTimelineView::setPlayheadNs(qint64 ns_from_midnight) {
const qint64 d = dayNs_();
if (ns_from_midnight < 0) ns_from_midnight = 0;
if (ns_from_midnight >= d) ns_from_midnight = d - 1;
playheadNs_ = ns_from_midnight;
update();
}

QRect PlaybackTimelineView::barRect() const {
return rect().adjusted(16, 16, -16, -24);
}
// trim selection
// -------
qint64 PlaybackTimelineView::posToNs_(int x, const QRect& r) const {
const qreal fx = qBound<qreal>(0.0, (x - r.left()) / qreal(r.width()), 1.0);
return qint64(fx * dayNs_());
}

QRect PlaybackTimelineView::handleRectAt_(qint64 ns, const QRect& r) const {
const qreal fx = qBound<qreal>(0.0, ns / (qreal)dayNs_(), 1.0);
const int cx = int(r.left() + fx * r.width());
return QRect(cx - kHandlePx, r.top()+1, 2*kHandlePx, r.height()-2);
}

void PlaybackTimelineView::setSelection(qint64 s, qint64 e, bool enabled){
selEnabled_ = enabled;
selStartNs_ = qMax<qint64>(0, qMin(s, dayNs_()-1));
selEndNs_   = qMax<qint64>(0, qMin(e, dayNs_()-1));
clampSelection_();
update();
}

void PlaybackTimelineView::clampSelection_(){
if (!selEnabled_) return;
if (selEndNs_ <= selStartNs_) selEndNs_ = qMin(selStartNs_ + 1'000'000'000LL, dayNs_()-1);
selStartNs_ = qBound<qint64>(0, selStartNs_, dayNs_()-2);
selEndNs_   = qBound<qint64>(selStartNs_+1, selEndNs_, dayNs_()-1);
}
// --------
void PlaybackTimelineView::paintEvent(QPaintEvent*) {
QPainter p(this);
p.setRenderHint(QPainter::Antialiasing, false);


const QRect r = barRect();

// card + base
p.fillRect(r.adjusted(-4,-6,4,10), QColor(20,20,20));
p.setPen(QColor("#2f2f2f")); p.drawRect(r.adjusted(-4,-6,4,10));
p.fillRect(r, QColor("#242424"));
p.setPen(QColor("#3a3a3a")); p.drawRect(r.adjusted(0,0,-1,-1));

// grid
QFont f = p.font(); f.setPointSizeF(f.pointSizeF()*0.9); p.setFont(f);
p.setPen(QColor(60,60,60));
for (int q=0; q<=24*4; ++q) {
    const qreal fx = q/(24.0*4.0);
    const qreal x = r.left() + r.width()*fx;
    p.drawLine(QPointF(x, r.center().y()-6), QPointF(x, r.center().y()+6));
}
p.setPen(QColor(110,110,110));
for (int h=0; h<=24; ++h) {
    const qreal fx = h/24.0;
    const qreal x = r.left() + r.width()*fx;
    p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    const QString lab = QString("%1").arg(h,2,10,QChar('0'));
    const QRect tr(int(x)-14, r.bottom()+4, 28, 14);
    p.drawText(tr, Qt::AlignHCenter|Qt::AlignTop, lab);
}

if (model_) {
    p.setPen(Qt::NoPen);
    for (const auto& s : model_->spans()) {
        const qreal fx1 = model_->fractionFor(s.start_ns);
        const qreal fx2 = model_->fractionFor(s.end_ns);
        const int x1 = int(r.left() + fx1 * r.width());
        const int x2 = int(r.left() + fx2 * r.width());
        QRect seg(x1, r.top()+4, qMax(2, x2-x1), r.height()-8);
        p.fillRect(seg, QColor("#3ddc84"));
        p.setPen(QColor("#2aa864")); p.drawRect(seg.adjusted(0,0,-1,-1));
        p.setPen(Qt::NoPen);
    }

    const qint64 covered = model_->totalCoveredNs();
    const qreal pct = (covered / (qreal)dayNs_()) * 100.0;
    p.setPen(QColor(180,180,180));
    p.drawText(QRect(r.left(), r.top()-14, r.width(), 12),
               Qt::AlignLeft|Qt::AlignVCenter,
               QString("Coverage: %1%").arg(QString::number(pct, 'f', 1)));
}
// selection shading and handles (only when enabled)
    if (selEnabled_) {
        const qreal fx1 = qBound<qreal>(0.0, selStartNs_ / (qreal)dayNs_(), 1.0);
        const qreal fx2 = qBound<qreal>(0.0, selEndNs_   / (qreal)dayNs_(), 1.0);
        const int x1 = int(r.left() + fx1 * r.width());
        const int x2 = int(r.left() + fx2 * r.width());
        // shaded region
        QRect selRect(qMin(x1,x2), r.top()+1, qMax(2, qAbs(x2-x1)), r.height()-2);
        p.fillRect(selRect, QColor(80,140,220,70));
        p.setPen(QColor(80,140,220)); p.drawRect(selRect.adjusted(0,0,-1,-1));
        // handles
        p.fillRect(handleRectAt_(selStartNs_, r), QColor(200, 220, 255));
        p.fillRect(handleRectAt_(selEndNs_,   r), QColor(200, 220, 255));
    }
// red playhead
const qreal fx = qBound<qreal>(0.0, playheadNs_ / (qreal)dayNs_(), 1.0);
const int x = int(r.left() + fx * r.width());
QPen pen(QColor(220, 50, 47)); pen.setWidth(2);
p.setPen(pen);
p.drawLine(QPoint(x, r.top()), QPoint(x, r.bottom()));


}

void PlaybackTimelineView::mouseMoveEvent(QMouseEvent* e) {
const QRect r = barRect();
if (!r.contains(e->pos())) { setToolTip(QString()); return; }


const qint64 t_ns = posToNs_(e->pos().x(), r);

const qreal fx = qBound<qreal>(0.0, t_ns / (qreal)dayNs_(), 1.0);
const int hh = int(fx * 24.0);
const int mm = int(fmod(fx*24.0, 1.0) * 60.0);
setToolTip(QString("%1:%2").arg(hh,2,10,QChar('0')).arg(mm,2,10,QChar('0')));

emit hoverTimeNs(t_ns);

if (dragging_) {
        if (dragKind_ == DragKind::StartHandle && selEnabled_) {
            selStartNs_ = qMin(t_ns, selEndNs_-1);
            clampSelection_();
            emit selectionChanged(selStartNs_, selEndNs_);
            update();
            return;
        } else if (dragKind_ == DragKind::EndHandle && selEnabled_) {
            selEndNs_ = qMax(t_ns, selStartNs_+1);
            clampSelection_();
            emit selectionChanged(selStartNs_, selEndNs_);
            update();
            return;
        }
    setPlayheadNs(t_ns); // visual
    if (!dragTick_.isValid() || dragTick_.elapsed() >= kDragEmitMs) {
        dragTick_.restart();
        emit seekRequested(t_ns); // throttled live-seek
    }
}


}

void PlaybackTimelineView::mousePressEvent(QMouseEvent* e) {
if (e->button()!=Qt::LeftButton) return;
const QRect r = barRect();
if (!r.contains(e->pos())) return;


const qint64 t = posToNs_(e->pos().x(), r);

    // If selection is enabled, check handle hit first
    if (selEnabled_) {
        const QRect hs = handleRectAt_(selStartNs_, r);
        const QRect he = handleRectAt_(selEndNs_,   r);
        if (hs.contains(e->pos())) {
            dragging_ = true; dragKind_ = DragKind::StartHandle; return;
        }
        if (he.contains(e->pos())) {
            dragging_ = true; dragKind_ = DragKind::EndHandle; return;
        }
    }
dragging_ = true;
dragKind_ = DragKind::Playhead;
dragTick_.restart(); // start throttle window
setPlayheadNs(t);
emit seekRequested(t); // immediate jump on press

}

void PlaybackTimelineView::mouseReleaseEvent(QMouseEvent* e) {
if (e->button()==Qt::LeftButton) { dragging_ = false; dragKind_ = DragKind::None; }
}

void PlaybackTimelineView::leaveEvent(QEvent*) {
setToolTip(QString());
}

