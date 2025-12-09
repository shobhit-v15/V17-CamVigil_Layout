#ifndef TOOLBAR_H
#define TOOLBAR_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QComboBox>
#include <QStringList>

class Toolbar : public QWidget {
    Q_OBJECT

public:
    explicit Toolbar(QWidget* parent = nullptr);

    // Update "Page X / Y" display (MainWindow calls this)
    void setPageInfo(int currentPage, int totalPages);
    void setGroups(const QStringList& names, int currentIndex);

signals:
    void settingsButtonClicked();
    void playbackButtonClicked();

    // Pagination requests
    void nextPageRequested();
    void previousPageRequested();

    void groupChanged(int index);
    void layoutModeChanged(bool isDefault);

private slots:
    void updateClock();
    void checkInternetConnection();

    void onPrevPageClicked();
    void onNextPageClicked();
    void onDefaultLayoutClicked();
    void onCustomLayoutClicked();

private:
    QLabel* clockLabel;
    QLabel* statusLabel;
    QLabel* pageLabel;

    QPushButton* settingsButton;
    QPushButton* playbackButton;

    QPushButton* prevPageButton;
    QPushButton* nextPageButton;

    QPushButton* defaultLayoutButton;
    QPushButton* customLayoutButton;

    QComboBox* groupCombo;

    QTimer* clockTimer;
    QNetworkAccessManager* networkManager;
    QTimer* checkTimer;
};

#endif // TOOLBAR_H
