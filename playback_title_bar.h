#pragma once
#include <QWidget>
class QLabel;
class QToolButton;
class QHBoxLayout;

class PlaybackTitleBar : public QWidget {
    Q_OBJECT
public:
    explicit PlaybackTitleBar(QWidget* parent=nullptr);
    void setTitle(const QString& text);
    void setRightWidget(QWidget* w);   // e.g. PlaybackControlsWidget

signals:
    void closeRequested();

private:
    QLabel*      title_{nullptr};
    QToolButton* closeBtn_{nullptr};
    QHBoxLayout* lay_{nullptr};
};
