#pragma once
#include <QWidget>

class QCheckBox;
class QTimeEdit;
class QLabel;
class QPushButton;
class QProgressBar;

class PlaybackTrimPanel : public QWidget {
    Q_OBJECT
public:
    explicit PlaybackTrimPanel(QWidget* parent=nullptr);

    void setEnabledPanel(bool on);
    void setRangeNs(qint64 start_ns, qint64 end_ns);
    void setDayStartNs(qint64 day_start_ns);
    void setDurationLabel(qint64 dur_ns);

    // Progress + phase helpers
    void setPhaseIdle();
    void setPhaseClipping();
    void setPhaseClipped();
    void setPhaseSaving();
    void setPhaseSaved();
    void setPhaseError(const QString& msg);
    void resetProgress();
    void setProgress(double pct);
    void enableSave(bool on);

signals:
    void trimModeToggled(bool on);
    void startEditedNs(qint64 ns_from_midnight);
    void endEditedNs(qint64 ns_from_midnight);

    void clipRequested();
    void saveRequested();

private:
    qint64 dayStartNs_=0;

    QCheckBox  *enableBox_;
    QTimeEdit  *startEdit_;
    QTimeEdit  *endEdit_;
    QLabel     *durLab_;
    QPushButton* clipBtn_;
    QPushButton* saveBtn_;
    QProgressBar* prog_;

    qint64 timeEditToNs(const QTimeEdit*) const;
    void setTimeEdit(QTimeEdit*, qint64 ns);
};
