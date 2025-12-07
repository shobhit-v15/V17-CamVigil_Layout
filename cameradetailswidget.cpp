#include "cameradetailswidget.h"

#include <QFormLayout>
#include <QLabel>
#include <QDebug>

CameraDetailsWidget::CameraDetailsWidget(CameraManager* cameraManager,
                                         const QString& dbPath,
                                         QWidget* parent)
    : QWidget(parent)
    , cameraManager(cameraManager)
    , groupingWidget(nullptr)
    , nameEdit(nullptr)
    , currentCameraIndex(-1)
    , saveBtn(nullptr)
{
    QFormLayout* formLayout = new QFormLayout(this);
    formLayout->setLabelAlignment(Qt::AlignLeft);
    formLayout->setFormAlignment(Qt::AlignHCenter);

    groupingWidget = new CameraGroupingWidget(cameraManager, dbPath, this);
    formLayout->addRow(groupingWidget);

    connect(groupingWidget, &CameraGroupingWidget::cameraChanged,
            this, &CameraDetailsWidget::handleCameraChanged);
    connect(groupingWidget, &CameraGroupingWidget::editCameraRequested,
            this, &CameraDetailsWidget::focusNameEdit);
    connect(groupingWidget, &CameraGroupingWidget::membershipsChanged,
            this, &CameraDetailsWidget::groupsMembershipsChanged);

    QLabel* nameLabel = new QLabel("Name");
    nameLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: white;");

    nameEdit = new QLineEdit(this);
    nameEdit->setStyleSheet("font-size: 18px; font-weight: bold; color: white; background-color: #4d4d4d;");
    nameEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    saveBtn = new QPushButton("Save", this);
    saveBtn->setStyleSheet("font-size:16px; font-weight:bold; color:white; background-color:#2d6cdf;");
    connect(saveBtn, &QPushButton::clicked,
            this, &CameraDetailsWidget::onSaveClicked);

    formLayout->addRow(nameLabel, nameEdit);
    formLayout->addRow(new QLabel(""), saveBtn);

    setLayout(formLayout);

    // Ensure initial camera info is loaded
    handleCameraChanged(groupingWidget->currentCameraIndex());
}

void CameraDetailsWidget::handleCameraChanged(int cameraIndex)
{
    currentCameraIndex = cameraIndex;
    loadCameraInfo(currentCameraIndex);
}

void CameraDetailsWidget::focusNameEdit()
{
    if (nameEdit) {
        nameEdit->setFocus(Qt::TabFocusReason);
        nameEdit->selectAll();
    }
}

void CameraDetailsWidget::loadCameraInfo(int cameraIndex)
{
    if (!cameraManager || cameraIndex < 0)
        return;

    const auto profiles = cameraManager->getCameraProfiles();
    if (cameraIndex >= static_cast<int>(profiles.size()))
        return;

    nameEdit->setText(QString::fromStdString(profiles[cameraIndex].displayName));
}

void CameraDetailsWidget::onSaveClicked()
{
    if (!cameraManager || currentCameraIndex < 0)
        return;

    const QString newName = nameEdit->text().trimmed();
    if (newName.isEmpty()) {
        qWarning() << "[CameraDetails] empty name";
        return;
    }

    QString err;
    if (cameraManager->renameAndPush(currentCameraIndex, newName, &err)) {
        qInfo() << "[CameraDetails] name updated and pushed";
    } else {
        qWarning() << "[CameraDetails] push failed:" << err;
    }
}
