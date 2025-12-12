#ifndef CAMERADETAILSWIDGET_H
#define CAMERADETAILSWIDGET_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>

#include "cameramanager.h"
#include "camera_grouping_widget.h"

class CameraDetailsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CameraDetailsWidget(CameraManager* cameraManager,
                                 const QString& dbPath,
                                 QWidget* parent = nullptr);

signals:
    void groupsMembershipsChanged();

private slots:
    void onSaveClicked();
    void handleCameraChanged(int cameraIndex);
    void focusNameEdit();

private:
    void loadCameraInfo(int cameraIndex);

    CameraManager* cameraManager;
    CameraGroupingWidget* groupingWidget;
    QLineEdit* nameEdit;         // Input for camera name
    int currentCameraIndex;      // Tracks selected camera index
    QPushButton* saveBtn;
};

#endif // CAMERADETAILSWIDGET_H
