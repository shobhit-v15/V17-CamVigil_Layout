#include "storagedetailswidget.h"
#include <QFormLayout>
#include <QStorageInfo>
#include <QComboBox>
#include <QLabel>
#include <QDebug>
#include <QTimer>
#include <QDateTime>
#include <QFileInfo>

StorageDetailsWidget::StorageDetailsWidget(ArchiveManager* archiveManager, QWidget *parent)
    : QWidget(parent), archiveManager(archiveManager)
{
    QFormLayout* formLayout = new QFormLayout(this);
    formLayout->setLabelAlignment(Qt::AlignLeft);
    formLayout->setFormAlignment(Qt::AlignHCenter);

    storageDeviceStatusLabel = new QLabel(this);
    storageDeviceStatusLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: white;");
    formLayout->addRow(new QLabel("Archive Path"), storageDeviceStatusLabel);

    storageProgressBar = new QProgressBar(this);
    storageProgressBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    storageProgressBar->setMaximum(100);
    formLayout->addRow(new QLabel("Capacity"), storageProgressBar);

    capacityDetailsLabel = new QLabel(this);
    capacityDetailsLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: white;");
    formLayout->addRow(new QLabel(""), capacityDetailsLabel);

    durationCombo = new QComboBox(this);
    durationCombo->addItem("1 min", 60);
    durationCombo->addItem("5 mins", 300);
    durationCombo->addItem("15 mins", 900);
    durationCombo->addItem("30 mins", 1800);
    durationCombo->addItem("60 mins", 3600);
    durationCombo->setCurrentIndex(1);
    durationCombo->setStyleSheet("font-size: 18px; font-weight: bold; color: white; background-color: #4d4d4d;");
    durationCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connect(durationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StorageDetailsWidget::onDurationChanged);
    formLayout->addRow(new QLabel("Recording Duration"), durationCombo);

    setLayout(formLayout);
    updateStorageInfo();

    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &StorageDetailsWidget::updateStorageInfo);
    refreshTimer->start(3000);
}

void StorageDetailsWidget::onDurationChanged(int index) {
    int seconds = durationCombo->itemData(index).toInt();
    emit segmentDurationChanged(seconds);
    if (archiveManager) {
        archiveManager->updateSegmentDuration(seconds);
    }
}

void StorageDetailsWidget::updateStorageInfo() {
    const QString root = archiveManager ? archiveManager->archiveRoot() : QString();
    if (root.isEmpty() || !QFileInfo::exists(root)) {
        storageDeviceStatusLabel->setText("Not initialized");
        capacityDetailsLabel->setText("");
        storageProgressBar->setValue(0);
        storageProgressBar->setFormat("");
        return;
    }

    storageDeviceStatusLabel->setText(root);
    QStorageInfo storage(root);
    const qint64 total = storage.bytesTotal();
    const qint64 avail = storage.bytesAvailable();
    const qint64 used  = (total > 0) ? (total - avail) : 0;

    const qint64 totalGB = total / (1024LL * 1024LL * 1024LL);
    const qint64 availGB = avail / (1024LL * 1024LL * 1024LL);
    const qint64 usedGB  = used  / (1024LL * 1024LL * 1024LL);

    capacityDetailsLabel->setText(QString("%1 GB Used | %2 GB Available | %3 GB Total")
                                  .arg(usedGB).arg(availGB).arg(totalGB));

    int usedPercent = (total > 0) ? int((used * 100) / total) : 0;
    storageProgressBar->setValue(usedPercent);
    storageProgressBar->setFormat(QString("%1% Used").arg(usedPercent));
    storageProgressBar->setStyleSheet(R"(
        QProgressBar {
            border: 1px solid #FFFFFF;
            background-color: #111;
            text-align: center;
            color: #383838;
            font-weight: bold;
        }
        QProgressBar::chunk {
            background-color: #FFFFFF;
        }
    )");

    if (usedPercent >= 95) {
        const QDateTime now = QDateTime::currentDateTime();
        if (!lastCleanupTime.isValid() || lastCleanupTime.msecsTo(now) > 60000) {
            qDebug() << "Storage is" << usedPercent << "% full. Requesting cleanup.";
            emit requestCleanup();
            lastCleanupTime = now;
        }
    }
}
