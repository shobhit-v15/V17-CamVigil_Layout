#include "playback_side_controls.h"
#include <QVBoxLayout>
#include <QPushButton>

PlaybackSideControls::PlaybackSideControls(QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0,0,0,0);
    lay->setSpacing(10);

    const char* btnStyle =
        "QPushButton{color:#fff;background:#2a2a2a;padding:10px 14px;border:1px solid #444;border-radius:8px;}"
        "QPushButton:hover{background:#333;border-color:#666;}"
        "QPushButton:pressed{background:#1f1f1f;border-color:#888; padding-top:11px; padding-bottom:9px;}"
        "QPushButton:disabled{color:#888;background:#1a1a1a;border-color:#333;}";

    play_      = new QPushButton("Play", this);
    pause_     = new QPushButton("Pause", this);
    rewind10_  = new QPushButton("⏪ 10s", this);
    forward10_ = new QPushButton("10s ⏩", this);
    speed_     = new QPushButton("Speed 1x", this);
    prevDay_   = new QPushButton("◀ Prev day", this);
    nextDay_   = new QPushButton("Next day ▶", this);

    for (auto* b : {play_, pause_, rewind10_, forward10_, speed_, prevDay_, nextDay_}) {
        b->setStyleSheet(btnStyle);
        b->setFixedHeight(36);
        b->setMinimumWidth(120);
    }

    // default disabled until playlist exists
    setEnabledControls(false);

    lay->addStretch(1);
    lay->addWidget(play_,      0, Qt::AlignHCenter);
    lay->addWidget(pause_,     0, Qt::AlignHCenter);
    lay->addWidget(rewind10_,  0, Qt::AlignHCenter);
    lay->addWidget(forward10_, 0, Qt::AlignHCenter);
    lay->addWidget(speed_,     0, Qt::AlignHCenter);
    lay->addWidget(prevDay_,   0, Qt::AlignHCenter);
    lay->addWidget(nextDay_,   0, Qt::AlignHCenter);
    lay->addStretch(1);

    connect(play_,      &QPushButton::clicked, this, &PlaybackSideControls::playClicked);
    connect(pause_,     &QPushButton::clicked, this, &PlaybackSideControls::pauseClicked);
    connect(rewind10_,  &QPushButton::clicked, this, &PlaybackSideControls::rewind10Clicked);
    connect(forward10_, &QPushButton::clicked, this, &PlaybackSideControls::forward10Clicked);
    connect(speed_,     &QPushButton::clicked, this, &PlaybackSideControls::speedCycleClicked);
    connect(prevDay_,   &QPushButton::clicked, this, &PlaybackSideControls::previousDayClicked);
    connect(nextDay_,   &QPushButton::clicked, this, &PlaybackSideControls::nextDayClicked);
}

void PlaybackSideControls::setEnabledControls(bool on) {
    for (auto* b : {play_, pause_, rewind10_, forward10_, speed_, prevDay_, nextDay_})
        b->setEnabled(on);
}

void PlaybackSideControls::setSpeedLabel(const QString& s) {
    if (speed_) speed_->setText(s);
}
