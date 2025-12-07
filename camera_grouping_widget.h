#ifndef CAMERA_GROUPING_WIDGET_H
#define CAMERA_GROUPING_WIDGET_H

#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QListWidget>
#include <QLineEdit>
#include <QHash>
#include <memory>
#include <limits>

#include "cameramanager.h"
#include "group_repository.h"

class QVBoxLayout;
class QLabel;

class CameraGroupingWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CameraGroupingWidget(CameraManager* cameraManager,
                                  const QString& dbPath,
                                  QWidget* parent = nullptr);

    int currentCameraIndex() const { return m_currentCameraIndex; }
    int currentGroupId() const { return m_currentGroupId; }

signals:
    void groupChanged(int groupId, const QString& groupName);
    void cameraChanged(int cameraIndex);
    void editCameraRequested(int cameraIndex);
    void membershipsChanged();

public slots:
    void reloadGroups(int preferredGroupId = std::numeric_limits<int>::min());
    void reloadCameras();

private slots:
    void handleGroupChanged(int comboIndex);
    void handleCameraChanged(int comboIndex);
    void handleEditClicked();
    void handleDeleteGroup();
    void handleCreateGroupOk();
    void handleCreateGroupCancel();
    void handleAssignSave();
    void handleAssignCancel();

private:
    void initGroupRepository(const QString& dbPath);
    void rebuildCameraCache();
    void loadAllCameras();
    void loadCamerasForGroup(int groupId);
    void clearPanels();
    void showCreateGroupPanel(int previousGroupId);
    void hideCreateGroupPanel(bool restoreSelection);
    void showAssignPanel();
    void hideAssignPanel(bool refreshSelection);
    void populateAssignList();
    bool setGroupComboToId(int groupId);
    QString groupNameForId(int groupId) const;
    bool isAllGroup(int groupId) const;
    int effectiveAllGroupId() const;

    CameraManager* m_cameraManager;
    std::unique_ptr<GroupRepository> m_groupRepo;

    QComboBox* m_groupCombo = nullptr;
    QComboBox* m_cameraCombo = nullptr;
    QPushButton* m_editButton = nullptr;
    QPushButton* m_deleteGroupBtn = nullptr;

    QWidget* m_panelContainer = nullptr;
    QVBoxLayout* m_panelLayout = nullptr;

    QWidget* m_createGroupPanel = nullptr;
    QLineEdit* m_newGroupEdit = nullptr;
    QPushButton* m_newGroupOkBtn = nullptr;
    QPushButton* m_newGroupCancelBtn = nullptr;
    int m_createPanelPreviousGroupId = -1;

    QWidget* m_assignPanel = nullptr;
    QLabel* m_assignTitle = nullptr;
    QListWidget* m_assignList = nullptr;
    QPushButton* m_assignSaveBtn = nullptr;
    QPushButton* m_assignCancelBtn = nullptr;

    int m_currentGroupId = -1;
    QString m_currentGroupName;
    int m_currentCameraIndex = -1;
    int m_allGroupId = -1;

    QVector<int> m_allCameraIds;
    QHash<int, int> m_cameraIdToIndex;
    QHash<int, QString> m_cameraIdToName;

    static constexpr int kGroupIdAddSentinel = -9999;
    static constexpr int kCameraAddSentinel = -9999;
};

#endif // CAMERA_GROUPING_WIDGET_H
