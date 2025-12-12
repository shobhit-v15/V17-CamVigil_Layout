#include "playback_title_bar.h"
#include <QLabel>
#include <QToolButton>
#include <QHBoxLayout>

PlaybackTitleBar::PlaybackTitleBar(QWidget* parent)
    : QWidget(parent)
{
    setStyleSheet("background:#1b1b1b; border-bottom:1px solid #333;");
    lay_ = new QHBoxLayout(this);
    lay_->setContentsMargins(12,8,12,8);
    lay_->setSpacing(8);

    title_ = new QLabel("Playback", this);
    title_->setStyleSheet("font-size:20px; font-weight:800; color:white;");

    closeBtn_ = new QToolButton(this);
    closeBtn_->setText(QString::fromUtf8("✕"));
    closeBtn_->setToolTip("Close");
    closeBtn_->setAutoRaise(true);
        // Match GO button look/metrics
        closeBtn_->setStyleSheet(
            "QToolButton{color:#fff;background:#2a2a2a;padding:6px 14px;border:1px solid #444;"
            "border-radius:6px;}"
            "QToolButton:hover{background:#333;border-color:#666;}"
            "QToolButton:pressed{background:#1f1f1f;border-color:#888; padding-top:7px; padding-bottom:5px;}"
            "QToolButton:disabled{color:#888;background:#1a1a1a;border-color:#333;}"
        );
        // identical control height/width as GO
        closeBtn_->setFixedHeight(32);
        closeBtn_->setMinimumWidth(100);
    connect(closeBtn_, &QToolButton::clicked, this, [this]{ emit closeRequested(); });

    lay_->addWidget(title_, 0, Qt::AlignVCenter|Qt::AlignLeft);
    lay_->addStretch(1);              // placeholder gap; right widget goes before the close button
    lay_->addWidget(closeBtn_, 0, Qt::AlignRight);
}

void PlaybackTitleBar::setTitle(const QString& text) {
    title_->setText(text);
}

void PlaybackTitleBar::setRightWidget(QWidget* w) {
    if (!w) return;
    w->setParent(this);
    // Insert just before the close button (last item)
    lay_->insertWidget(lay_->count()-1, w, /*stretch*/0, Qt::AlignRight|Qt::AlignVCenter);
}
