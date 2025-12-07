#include "playback_trim_panel.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QTimeEdit>
#include <QPushButton>
#include <QLabel>
#include <QTime>
#include <QProgressBar>

static inline QTime nsToTime(qint64 ns){
    qint64 s = ns/1000000000LL;
    return QTime::fromMSecsSinceStartOfDay(int((s%86400)*1000));
}

PlaybackTrimPanel::PlaybackTrimPanel(QWidget* p): QWidget(p){
    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(12,4,12,4);
    h->setSpacing(12);

    enableBox_ = new QCheckBox("Enable Trim/Export", this);
    enableBox_->setStyleSheet(
        "QCheckBox { color: white; }"
        "QCheckBox::indicator { background-color: #222; border: 1px solid #aaa; }"
        "QCheckBox::indicator:checked { background-color: white; }"
    );

    startEdit_ = new QTimeEdit(this); startEdit_->setDisplayFormat("HH:mm:ss");
    endEdit_   = new QTimeEdit(this); endEdit_->setDisplayFormat("HH:mm:ss");
    durLab_    = new QLabel("Duration: 00:00:00", this);

    clipBtn_ = new QPushButton("Clip", this);
    saveBtn_ = new QPushButton("Save", this);
    saveBtn_->setEnabled(false);

    prog_ = new QProgressBar(this);
    prog_->setMinimum(0);
    prog_->setMaximum(100);
    prog_->setValue(0);
    prog_->setTextVisible(true);
    prog_->setAlignment(Qt::AlignCenter);
    setPhaseIdle();

    h->addWidget(enableBox_);
    h->addSpacing(8);
    h->addWidget(new QLabel("Start:", this));
    h->addWidget(startEdit_);
    h->addWidget(new QLabel("End:", this));
    h->addWidget(endEdit_);
    h->addWidget(durLab_);
    h->addWidget(clipBtn_);
    h->addWidget(saveBtn_);
    h->addWidget(prog_, 1);

    setEnabledPanel(false);

    connect(enableBox_, &QCheckBox::toggled, this, &PlaybackTrimPanel::trimModeToggled);
    connect(startEdit_, &QTimeEdit::timeChanged, this, [this]{ emit startEditedNs(timeEditToNs(startEdit_)); });
    connect(endEdit_,   &QTimeEdit::timeChanged, this, [this]{ emit endEditedNs(timeEditToNs(endEdit_)); });
    connect(clipBtn_,   &QPushButton::clicked,   this, &PlaybackTrimPanel::clipRequested);
    connect(saveBtn_,   &QPushButton::clicked,   this, &PlaybackTrimPanel::saveRequested);
}

void PlaybackTrimPanel::setEnabledPanel(bool on){
    startEdit_->setEnabled(on);
    endEdit_->setEnabled(on);
    clipBtn_->setEnabled(on);
    prog_->setEnabled(on);
}

void PlaybackTrimPanel::setDayStartNs(qint64 ns){ dayStartNs_=ns; }
void PlaybackTrimPanel::setTimeEdit(QTimeEdit* w, qint64 ns){ w->setTime(nsToTime(ns)); }

qint64 PlaybackTrimPanel::timeEditToNs(const QTimeEdit* w) const{
    const QTime t = w->time();
    return ((t.hour()*3600LL + t.minute()*60LL + t.second())*1000000000LL);
}

void PlaybackTrimPanel::setRangeNs(qint64 start_ns, qint64 end_ns){
    setTimeEdit(startEdit_, start_ns);
    setTimeEdit(endEdit_,   end_ns);
    setDurationLabel(end_ns - start_ns);
}

void PlaybackTrimPanel::setDurationLabel(qint64 dur_ns){
    qint64 s = dur_ns/1000000000LL; int hh=s/3600, mm=(s%3600)/60, ss=s%60;
    durLab_->setText(QString("Duration: %1:%2:%3")
        .arg(hh,2,10,QChar('0')).arg(mm,2,10,QChar('0')).arg(ss,2,10,QChar('0')));
}

// ---------- Phase helpers ----------
void PlaybackTrimPanel::setPhaseIdle(){
    prog_->reset();
    prog_->setFormat("Idle");
    prog_->setStyleSheet("QProgressBar::chunk { background-color: #555; }");
    enableSave(false);
}

void PlaybackTrimPanel::setPhaseClipping(){
    if (prog_->value() != 0) prog_->setValue(0);
    prog_->setFormat("Clipping %p%");
    enableSave(false);
}

void PlaybackTrimPanel::setPhaseClipped(){
    prog_->setValue(100);
    prog_->setFormat("Video clipped");
    enableSave(true);
}

void PlaybackTrimPanel::setPhaseSaving(){
    if (prog_->value() != 0) prog_->setValue(0);
    prog_->setFormat("Saving %p%");
    enableSave(false);
}

void PlaybackTrimPanel::setPhaseSaved(){
    prog_->setValue(100);
    prog_->setFormat("Video saved");
    enableSave(false);
}

void PlaybackTrimPanel::setPhaseError(const QString& msg){
    prog_->setValue(100);
    prog_->setFormat(msg);
    prog_->setStyleSheet("QProgressBar::chunk { background-color: #F44336; }"); // red
    enableSave(false);
}

void PlaybackTrimPanel::resetProgress(){
    setPhaseIdle();
}

void PlaybackTrimPanel::setProgress(double pct){
    int v = int(qBound(0.0, pct, 100.0));
    prog_->setValue(v);
}

void PlaybackTrimPanel::enableSave(bool on){
    saveBtn_->setEnabled(on);
}
