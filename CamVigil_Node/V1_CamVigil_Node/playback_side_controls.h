#pragma once
#include <QWidget>

class QPushButton;

class PlaybackSideControls : public QWidget {
    Q_OBJECT
public:
    explicit PlaybackSideControls(QWidget* parent=nullptr);

    void setEnabledControls(bool on);
    void setSpeedLabel(const QString& s);  // already added earlier

signals:
    void playClicked();
    void pauseClicked();
    void rewind10Clicked();
    void forward10Clicked();
    void speedCycleClicked();
    void previousDayClicked();
    void nextDayClicked();

private:
    QPushButton* play_{nullptr};
    QPushButton* pause_{nullptr};
    QPushButton* rewind10_{nullptr};
    QPushButton* forward10_{nullptr};
    QPushButton* speed_{nullptr};
    QPushButton* prevDay_{nullptr};
    QPushButton* nextDay_{nullptr};
};
