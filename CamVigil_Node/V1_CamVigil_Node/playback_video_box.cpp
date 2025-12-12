#include "playback_video_box.h"
#include <QPainter>
#include <QPaintEvent>
#include <QFont>
#include <QBoxLayout>

PlaybackVideoBox::PlaybackVideoBox(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(320, 180);
    setAutoFillBackground(false);
        // Create a native child widget to host the video sink
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(12,12,12,12);
        lay->setSpacing(0);

        renderHost_ = new QWidget(this);
        renderHost_->setAttribute(Qt::WA_NativeWindow, true);
        renderHost_->setAutoFillBackground(true);
        renderHost_->setStyleSheet("background:#000; border:1px solid #2f2f2f; border-radius:6px;");
        renderHost_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        lay->addWidget(renderHost_, 1);
}

void PlaybackVideoBox::setPlaceholder(const QString& text) {
    placeholder_ = text;
    update();
}

void PlaybackVideoBox::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Placeholder text centered over the whole widget area
    QRect inner = rect();
    QFont f = p.font();
    f.setPointSizeF(f.pointSizeF()*1.1);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(180,180,180));
    p.drawText(inner, Qt::AlignCenter, placeholder_);
}
quintptr PlaybackVideoBox::renderWinId() const {
    return renderHost_ ? quintptr(renderHost_->winId()) : 0;
}
