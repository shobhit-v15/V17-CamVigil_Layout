#include "timeeditorwidget.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDateTime>
#include <QProcess>
#include <QMessageBox>
#include <QTimer>
#include <QApplication>
#include <QStyleFactory>
#include <QEvent>

TimeEditorWidget::TimeEditorWidget(QWidget *parent)
    : QWidget(parent)
{
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    // Elements
    formatToggle = new QCheckBox(tr("12-hr (AM/PM)"), this);
    formatToggle->setStyleSheet("color:white; background-color:#333; border:1px solid #777; padding:4px;");
    formatToggle->setFixedWidth(180);

    dateEdit = new QDateEdit(QDate::currentDate(), this);
    dateEdit->setDisplayFormat("dd MMMM yyyy");
    dateEdit->setCalendarPopup(true);
    dateEdit->setStyleSheet("color:white; background-color:#333; border:1px solid #777; padding:4px;");
    dateEdit->setFixedWidth(180);

    timezoneCombo = new QComboBox(this);
    timezoneCombo->addItems({"Asia/Kolkata","UTC","America/New_York","Europe/London","Asia/Dubai"});
    timezoneCombo->setStyleSheet("color:white; background-color:#333; border:1px solid #777; padding:4px;");
    timezoneCombo->setFixedWidth(180);

    applyButton = new QPushButton(tr("Set System Time"), this);
    applyButton->setStyleSheet("background-color:#666; color:white; font-weight:bold; padding:6px 12px;");

    connect(applyButton, &QPushButton::clicked, this, &TimeEditorWidget::onApplyClicked);

    // Row 1 layout
    QHBoxLayout *topRow = new QHBoxLayout;
    topRow->addWidget(formatToggle);
    topRow->addSpacing(10);
    topRow->addWidget(dateEdit);
    topRow->addSpacing(10);
    topRow->addWidget(timezoneCombo);
    topRow->addStretch();

    // Row 2 layout
    QHBoxLayout *buttonRow = new QHBoxLayout;
    buttonRow->addStretch();
    buttonRow->addWidget(applyButton);
    buttonRow->addStretch();

    // Main layout
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(topRow);
    mainLayout->addSpacing(12);
    mainLayout->addLayout(buttonRow);
    setLayout(mainLayout);

    setStyleSheet("background-color:black; font-size:14px;");
}

void TimeEditorWidget::updateLiveTime() {}

int TimeEditorWidget::runTimedatectl(const QStringList& args, QString* stderrOut) const
{
    QProcess p;
    p.start("timedatectl", args);
    p.waitForFinished(-1);
    if (stderrOut) *stderrOut = QString::fromLocal8Bit(p.readAllStandardError());
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
}

void TimeEditorWidget::onApplyClicked()
{
    QDateTime selectedDateTime(dateEdit->date(), QTime::currentTime());
    QString timezone = timezoneCombo->currentText();
    QString dateTimeStr = selectedDateTime.toString("yyyy-MM-dd HH:mm:ss");

    QString err;
    int ec = runTimedatectl({"set-ntp","false"}, &err);
    if (ec != 0) {
        QMessageBox::warning(this, "Error", "Failed to disable NTP:\n" + err);
        return;
    }

    ec = runTimedatectl({"set-timezone", timezone}, &err);
    if (ec != 0) {
        QMessageBox::warning(this, "Error", "Failed to set timezone:\n" + err);
        return;
    }

    ec = runTimedatectl({"set-time", dateTimeStr}, &err);
    if (ec != 0) {
        QMessageBox::warning(this, "Error", "Failed to set system time:\n" + err);
        return;
    }

    QMessageBox::information(this, "Success", "System time updated successfully.");
    emit timeDateUpdated();
}

void TimeEditorWidget::changeEvent(QEvent* e)
{
    QWidget::changeEvent(e);
}
