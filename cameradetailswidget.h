#ifndef CAMERADETAILSWIDGET_H
#define CAMERADETAILSWIDGET_H

#include <QWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <memory>

#include "cameramanager.h"
#include "group_repository.h"

class CameraDetailsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CameraDetailsWidget(CameraManager* cameraManager,
                                 const QString& dbPath,
                                 QWidget* parent = nullptr);

signals:
    // Emitted when group membership for any camera changes
    void cameraGroupsChanged();

private slots:
    void onCameraSelectionChanged(int comboIndex);
    void onSaveClicked();
    void onAddGroupClicked();
    void onDeleteGroupClicked();
    void onRenameGroupClicked();

private:
    void loadCameraInfo(int cameraIndex);
    void initGroupRepository(const QString& dbPath);
    void loadGroups();
    void refreshGroupsForCurrentCamera();
    int  currentCameraId() const;

    CameraManager* cameraManager;
    QComboBox* cameraCombo;      // Dropdown for cameras
    QLineEdit* nameEdit;         // Input for camera name
    int currentCameraIndex;      // Tracks selected camera index
    QPushButton* saveBtn;

    // Group UI
    std::unique_ptr<GroupRepository> groupRepo;
    QListWidget* groupList = nullptr;
    QLineEdit* groupNameEdit = nullptr;
    QPushButton* addGroupBtn = nullptr;
    QPushButton* deleteGroupBtn = nullptr;
    QPushButton* renameGroupBtn = nullptr;
};

#endif // CAMERADETAILSWIDGET_H
