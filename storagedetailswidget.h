#ifndef STORAGEDETAILSWIDGET_H
#define STORAGEDETAILSWIDGET_H

#include <QWidget>
#include <QProgressBar>
#include <QLabel>
#include <QComboBox>
#include <QTimer>
#include <QDateTime>
#include "archivemanager.h"

class StorageDetailsWidget : public QWidget {
    Q_OBJECT
public:
    explicit StorageDetailsWidget(ArchiveManager* archiveManager, QWidget *parent = nullptr);

public slots:
    void updateStorageInfo();

signals:
    void segmentDurationChanged(int seconds);
    void requestCleanup();

private slots:
    void onDurationChanged(int index);

private:
    QLabel* storageDeviceStatusLabel{};
    QProgressBar* storageProgressBar{};
    QLabel* capacityDetailsLabel{};
    QComboBox* durationCombo{};
    ArchiveManager* archiveManager{};
    QTimer* refreshTimer{};
    QDateTime lastCleanupTime;
};

#endif // STORAGEDETAILSWIDGET_H
