#pragma once
#include <QWidget>
#include <QComboBox>
#include <QDateEdit>
#include <QStringList>
#include <QDate>
#include <QSet>
#include <QPushButton>
class PlaybackControlsWidget : public QWidget {
    Q_OBJECT
public:
    explicit PlaybackControlsWidget(QWidget* parent=nullptr);
    void setGoIdle();
    void setGoBusy();
    void setCameraList(const QStringList& names);
    QString selectedCamera() const;
    QDate   selectedDate()  const;

    // NEW: programmatic setters used by PlaybackWindow
    void setDateBounds(const QDate& min, const QDate& max);
    void setDate(const QDate& dt);
    void setCurrentCamera(const QString& name);
    void setAvailableDates(const QSet<QDate>& dates);
    QPushButton* goBtn{nullptr};
signals:
    void cameraChanged(const QString& name);
    void dateChanged(const QDate& date);
    void goPressed(const QString& cameraName, const QDate& date);
private:
    QComboBox* cameraCombo{nullptr};
    QDateEdit* dateEdit{nullptr};
    QSet<QDate> availableDates_;
    QDate nearestAvailable(const QDate& base) const;
};
