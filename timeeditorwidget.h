// timeeditorwidget.h
#ifndef TIMEEDITORWIDGET_H
#define TIMEEDITORWIDGET_H

#include <QWidget>
#include <QDateEdit>
#include <QTimeEdit>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QTimer>

class TimeEditorWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimeEditorWidget(QWidget *parent = nullptr);

signals:
    void timeDateUpdated();

protected:
    void changeEvent(QEvent* e) override;

private slots:
    void updateLiveTime();
    void onApplyClicked();

private:
    void refreshUI();
    // helper: run timedatectl with args, capture stderr
    int runTimedatectl(const QStringList& args, QString* stderrOut = nullptr) const;

    QCheckBox*  formatToggle{nullptr};
    QDateEdit*  dateEdit{nullptr};
    QTimeEdit*  timeEdit{nullptr};
    QComboBox*  timezoneCombo{nullptr};
    QPushButton* applyButton{nullptr};
    QTimer*     liveTimeTimer{nullptr};
};

#endif // TIMEEDITORWIDGET_H
