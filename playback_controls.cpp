#include "playback_controls.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QCalendarWidget>
#include <QTextCharFormat>
#include <QSignalBlocker>
#include <limits>
#include <QDebug>
PlaybackControlsWidget::PlaybackControlsWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("PlaybackControls");
    setStyleSheet(
        "#PlaybackControls { background: transparent; }"
        "QComboBox { color: white; background:#2a2a2a; padding:6px 10px; border:1px solid #444; }"
        "QDateEdit { color: white; background:#2a2a2a; padding:6px 10px; border:1px solid #444; }"
    );

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(10);

    cameraCombo = new QComboBox(this);
    cameraCombo->setMinimumWidth(200);
    connect(cameraCombo, &QComboBox::currentTextChanged,
            this, &PlaybackControlsWidget::cameraChanged);

    dateEdit = new QDateEdit(QDate::currentDate(), this);
    dateEdit->setCalendarPopup(true);
    dateEdit->setDisplayFormat("dd MMM yyyy");
    connect(dateEdit, &QDateEdit::dateChanged,
            this, &PlaybackControlsWidget::dateChanged);
    // Go button
        goBtn = new QPushButton("Go", this);
        goBtn->setCursor(Qt::PointingHandCursor);
           goBtn->setStyleSheet(
                "QPushButton{color:#fff;background:#2a2a2a;padding:6px 14px;border:1px solid #444;"
                "border-radius:6px;}"
                "QPushButton:hover{background:#333;border-color:#666;}"
                "QPushButton:pressed{background:#1f1f1f;border-color:#888; padding-top:7px; padding-bottom:5px;}"
                "QPushButton:disabled{color:#888;background:#1a1a1a;border-color:#333;}"
            );
            goBtn->setFixedHeight(32);
            goBtn->setMinimumWidth(100);
        connect(goBtn, &QPushButton::clicked, this, [this]{
                setGoBusy();
            // emit action
            const auto cam = selectedCamera();
            const auto dt  = selectedDate();
           qInfo() << "[UI] Go pressed cam=" << cam << " date=" << dt.toString("yyyy-MM-dd");
            emit goPressed(cam, dt);
        });

    // add to layout (right of date):

    layout->addWidget(cameraCombo);
    layout->addWidget(dateEdit);
    layout->addWidget(goBtn);
    setLayout(layout);
    connect(this, &PlaybackControlsWidget::dateChanged,
                this, [this](const QDate&){ setGoIdle(); });
}
void PlaybackControlsWidget::setGoIdle() {
    if (!goBtn) return;
    goBtn->setEnabled(true);
    goBtn->setText("Go");
}
void PlaybackControlsWidget::setGoBusy() {
    if (!goBtn) return;
    goBtn->setEnabled(false);
    goBtn->setText("Building…");
}

void PlaybackControlsWidget::setCameraList(const QStringList& names) {
    cameraCombo->clear();
    cameraCombo->addItems(names.isEmpty() ? QStringList{"All Cameras"} : names);
    if (cameraCombo->count() > 0) cameraCombo->setCurrentIndex(0);
}

QString PlaybackControlsWidget::selectedCamera() const { return cameraCombo->currentText(); }
QDate   PlaybackControlsWidget::selectedDate()  const { return dateEdit->date(); }

// --- NEW helpers ---

void PlaybackControlsWidget::setDateBounds(const QDate& min, const QDate& max) {
    dateEdit->setMinimumDate(min.isValid() ? min : QDate(2000,1,1));
    dateEdit->setMaximumDate(max.isValid() ? max : QDate(2099,12,31));
}

void PlaybackControlsWidget::setDate(const QDate& dt) {
    QSignalBlocker b(dateEdit);            // avoid spurious dateChanged
    dateEdit->setDate(dt.isValid() ? dt : QDate::currentDate());
}

void PlaybackControlsWidget::setCurrentCamera(const QString& name) {
    const int idx = cameraCombo->findText(name, Qt::MatchExactly);
    if (idx >= 0) {
        QSignalBlocker b(cameraCombo);     // avoid spurious cameraChanged
        cameraCombo->setCurrentIndex(idx);
    }
}
void PlaybackControlsWidget::setAvailableDates(const QSet<QDate>& dates) {
    availableDates_ = dates;
    auto* cal = dateEdit->calendarWidget();
    if (!cal) return;

    // Clear formats
    cal->setDateTextFormat(QDate(), QTextCharFormat{});

    // Gray-out default for the whole shown range
    QTextCharFormat gray; gray.setForeground(QBrush(QColor("#666")));
    const QDate min = dateEdit->minimumDate();
    const QDate max = dateEdit->maximumDate();
    for (QDate d = min; d.isValid() && d <= max; d = d.addDays(1)) {
        cal->setDateTextFormat(d, gray);
    }

    // Highlight available
    QTextCharFormat hi;
    hi.setForeground(Qt::white);
    hi.setBackground(QColor("#444"));
    hi.setFontWeight(QFont::Bold);
    for (const QDate& d : availableDates_) {
        if (d.isValid()) cal->setDateTextFormat(d, hi);
    }

    // Enforce selection to available dates
    connect(cal, &QCalendarWidget::clicked, this, [this](const QDate& d){
        if (!availableDates_.contains(d) && !availableDates_.isEmpty()) {
            const QDate n = nearestAvailable(d);
            if (n.isValid()) {
                QSignalBlocker b(dateEdit);
                dateEdit->setDate(n);
            }
        }
    });
    // If current selection is unavailable, snap it
    if (!availableDates_.isEmpty() && !availableDates_.contains(dateEdit->date())) {
        const QDate n = nearestAvailable(dateEdit->date());
        if (n.isValid()) {
            QSignalBlocker b(dateEdit);
            dateEdit->setDate(n);
        }
    }
}

QDate PlaybackControlsWidget::nearestAvailable(const QDate& base) const {
    if (availableDates_.isEmpty()) return QDate();
    int bestDiff = std::numeric_limits<int>::max();
    QDate best;
    for (const QDate& d : availableDates_) {
        const int diff = std::abs(d.daysTo(base));
        if (diff < bestDiff) { bestDiff = diff; best = d; }
    }
    return best;
}
