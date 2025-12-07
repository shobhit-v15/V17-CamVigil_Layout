#include "cameradetailswidget.h"
#include <QFormLayout>
#include <QLabel>
#include <QDebug>
#include <QHBoxLayout>
#include <QSet>
#include <vector>

CameraDetailsWidget::CameraDetailsWidget(CameraManager* cameraManager,
                                         const QString& dbPath,
                                         QWidget* parent)
    : QWidget(parent),
      cameraManager(cameraManager),
      currentCameraIndex(-1)
{
    // Use a form layout for clear label-field alignment.
    QFormLayout* formLayout = new QFormLayout(this);
    formLayout->setLabelAlignment(Qt::AlignLeft);
    formLayout->setFormAlignment(Qt::AlignHCenter);

    // Cameras row
    QLabel* cameraLabel = new QLabel("Cameras");
    cameraLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: white;");

    cameraCombo = new QComboBox(this);
    cameraCombo->setStyleSheet("font-size: 18px; font-weight: bold; color: white; background-color: #4d4d4d;");
    cameraCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    std::vector<CamHWProfile> profiles = cameraManager->getCameraProfiles();
    for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
        cameraCombo->addItem(QString::fromStdString(profiles[i].displayName), i);
    }
    connect(cameraCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CameraDetailsWidget::onCameraSelectionChanged);

    // Name row
    QLabel* nameLabel = new QLabel("Name");
    nameLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: white;");

    nameEdit = new QLineEdit(this);
    nameEdit->setStyleSheet("font-size: 18px; font-weight: bold; color: white; background-color: #4d4d4d;");
    nameEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    saveBtn = new QPushButton("Save", this);
    saveBtn->setStyleSheet("font-size:16px; font-weight:bold; color:white; background-color:#2d6cdf;");
    connect(saveBtn, &QPushButton::clicked, this, &CameraDetailsWidget::onSaveClicked);

    formLayout->addRow(cameraLabel, cameraCombo);
    formLayout->addRow(nameLabel, nameEdit);
    formLayout->addRow(new QLabel(""), saveBtn);

    // Groups section
    QLabel* groupsLabel = new QLabel("Groups");
    groupsLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: white;");

    groupList = new QListWidget(this);
    groupList->setStyleSheet("font-size: 16px; color: white; background-color: #4d4d4d;");
    groupList->setSelectionMode(QAbstractItemView::SingleSelection);
    groupList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    // Group controls: name edit + add / delete / rename
    QWidget* groupControls = new QWidget(this);
    QHBoxLayout* groupCtrlLayout = new QHBoxLayout(groupControls);
    groupCtrlLayout->setContentsMargins(0, 0, 0, 0);
    groupCtrlLayout->setSpacing(8);

    groupNameEdit = new QLineEdit(groupControls);
    groupNameEdit->setPlaceholderText("Group name");
    groupNameEdit->setStyleSheet("font-size: 16px; font-weight: bold; color: white; background-color: #4d4d4d;");

    addGroupBtn = new QPushButton("Add", groupControls);
    addGroupBtn->setStyleSheet("font-size:14px; font-weight:bold; color:white; background-color:#2d6cdf;");

    deleteGroupBtn = new QPushButton("Delete", groupControls);
    deleteGroupBtn->setStyleSheet("font-size:14px; font-weight:bold; color:white; background-color:#b02a37;");

    renameGroupBtn = new QPushButton("Rename", groupControls);
    renameGroupBtn->setStyleSheet("font-size:14px; font-weight:bold; color:white; background-color:#6c757d;");

    groupCtrlLayout->addWidget(groupNameEdit, 1);
    groupCtrlLayout->addWidget(addGroupBtn, 0);
    groupCtrlLayout->addWidget(renameGroupBtn, 0);
    groupCtrlLayout->addWidget(deleteGroupBtn, 0);

    connect(addGroupBtn, &QPushButton::clicked,
            this, &CameraDetailsWidget::onAddGroupClicked);
    connect(deleteGroupBtn, &QPushButton::clicked,
            this, &CameraDetailsWidget::onDeleteGroupClicked);
    connect(renameGroupBtn, &QPushButton::clicked,
            this, &CameraDetailsWidget::onRenameGroupClicked);

    // Insert groups label + list + controls into the form layout
    formLayout->addRow(groupsLabel, groupList);
    formLayout->addRow(new QLabel(""), groupControls);

    setLayout(formLayout);

    // Initialize group repository and load groups
    initGroupRepository(dbPath);
    loadGroups();

    // Load info for the first camera if available.
    if (!profiles.empty()) {
        cameraCombo->setCurrentIndex(0);
        onCameraSelectionChanged(0);
    }
}

void CameraDetailsWidget::initGroupRepository(const QString& dbPath) {
    if (dbPath.isEmpty()) {
        qWarning() << "[CameraDetails] Empty DB path; group editing disabled";
        return;
    }
    groupRepo = std::make_unique<GroupRepository>(dbPath);
    if (!groupRepo->ensureSchemaGroups()) {
        qWarning() << "[CameraDetails] ensureSchemaGroups() failed; group editing disabled";
        groupRepo.reset();
    }
}

void CameraDetailsWidget::loadGroups() {
    if (!groupRepo || !groupList) {
        return;
    }
    groupList->clear();

    const QVector<CameraGroupInfo> groups = groupRepo->listGroups();
    for (const auto& g : groups) {
        auto* item = new QListWidgetItem(g.name, groupList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        item->setCheckState(Qt::Unchecked);
        item->setData(Qt::UserRole, g.id);
        groupList->addItem(item);
    }
}

void CameraDetailsWidget::loadCameraInfo(int cameraIndex) {
    std::vector<CamHWProfile> profiles = cameraManager->getCameraProfiles();
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(profiles.size()))
        return;
    nameEdit->setText(QString::fromStdString(profiles[cameraIndex].displayName));
}

int CameraDetailsWidget::currentCameraId() const {
    if (!groupRepo || !cameraManager || currentCameraIndex < 0) {
        return -1;
    }
    const auto profiles = cameraManager->getCameraProfiles();
    if (currentCameraIndex >= static_cast<int>(profiles.size())) {
        return -1;
    }
    const auto& p = profiles[currentCameraIndex];
    const QString mainUrl = QString::fromStdString(p.url);
    if (mainUrl.isEmpty()) {
        return -1;
    }
    // Ensure row exists to keep names in sync
    const QString displayName = nameEdit->text().trimmed();
    return groupRepo->ensureCameraRow(mainUrl, displayName);
}

void CameraDetailsWidget::refreshGroupsForCurrentCamera() {
    if (!groupRepo || !groupList) {
        return;
    }
    const int camId = currentCameraId();
    if (camId <= 0) {
        // Clear all checks
        for (int i = 0; i < groupList->count(); ++i) {
            if (auto* item = groupList->item(i)) {
                item->setCheckState(Qt::Unchecked);
            }
        }
        return;
    }

    QVector<int> camGroups = groupRepo->listGroupIdsForCamera(camId);
    QSet<int> camGroupSet;
    for (int gid : camGroups) {
        camGroupSet.insert(gid);
    }

    for (int i = 0; i < groupList->count(); ++i) {
        if (auto* item = groupList->item(i)) {
            const int gid = item->data(Qt::UserRole).toInt();
            item->setCheckState(camGroupSet.contains(gid) ? Qt::Checked : Qt::Unchecked);
        }
    }
}

void CameraDetailsWidget::onCameraSelectionChanged(int comboIndex) {
    int cameraIndex = cameraCombo->itemData(comboIndex).toInt();
    currentCameraIndex = cameraIndex;
    loadCameraInfo(currentCameraIndex);
    refreshGroupsForCurrentCamera();
}

void CameraDetailsWidget::onSaveClicked() {
    if (currentCameraIndex < 0) return;

    const QString newName = nameEdit->text().trimmed();
    if (newName.isEmpty()) {
        qWarning() << "[CameraDetails] empty name";
        return;
    }

    // Update name and push to device/JSON
    QString err;
    if (cameraManager->renameAndPush(currentCameraIndex, newName, &err)) {
        const int comboIndex = cameraCombo->currentIndex();
        if (comboIndex >= 0) cameraCombo->setItemText(comboIndex, newName);
        qInfo() << "[CameraDetails] name updated and pushed";
    } else {
        qWarning() << "[CameraDetails] push failed:" << err;
    }

    // Persist group membership for this camera
    if (groupRepo && groupList) {
        const int camId = currentCameraId();
        if (camId > 0) {
            QVector<int> groupIds;
            for (int i = 0; i < groupList->count(); ++i) {
                if (auto* item = groupList->item(i)) {
                    if (item->checkState() == Qt::Checked) {
                        const int gid = item->data(Qt::UserRole).toInt();
                        if (gid > 0) groupIds.append(gid);
                    }
                }
            }
            if (!groupRepo->setCameraGroups(camId, groupIds)) {
                qWarning() << "[CameraDetails] setCameraGroups failed for camId" << camId;
            } else {
                emit cameraGroupsChanged();
            }
        }
    }
}

void CameraDetailsWidget::onAddGroupClicked() {
    if (!groupRepo || !groupList) return;

    const QString name = groupNameEdit ? groupNameEdit->text().trimmed() : QString();
    if (name.isEmpty()) {
        qWarning() << "[CameraDetails] AddGroup: empty name";
        return;
    }

    const int gid = groupRepo->createGroup(name);
    if (gid <= 0) {
        qWarning() << "[CameraDetails] AddGroup: createGroup failed for" << name;
        return;
    }

    auto* item = new QListWidgetItem(name, groupList);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    item->setCheckState(Qt::Unchecked);
    item->setData(Qt::UserRole, gid);
    groupList->addItem(item);

    qInfo() << "[CameraDetails] Added group" << name << "id" << gid;
}

void CameraDetailsWidget::onDeleteGroupClicked() {
    if (!groupRepo || !groupList) return;

    QListWidgetItem* item = groupList->currentItem();
    if (!item) return;

    const int gid = item->data(Qt::UserRole).toInt();
    if (gid <= 0) return;

    if (!groupRepo->deleteGroup(gid)) {
        qWarning() << "[CameraDetails] DeleteGroup: deleteGroup failed for gid" << gid;
        return;
    }

    delete item;
    qInfo() << "[CameraDetails] Deleted group id" << gid;
}

void CameraDetailsWidget::onRenameGroupClicked() {
    if (!groupRepo || !groupList) return;

    QListWidgetItem* item = groupList->currentItem();
    if (!item) return;

    const int gid = item->data(Qt::UserRole).toInt();
    if (gid <= 0) return;

    const QString newName = groupNameEdit ? groupNameEdit->text().trimmed() : QString();
    if (newName.isEmpty()) {
        qWarning() << "[CameraDetails] RenameGroup: empty name";
        return;
    }

    if (!groupRepo->renameGroup(gid, newName)) {
        qWarning() << "[CameraDetails] RenameGroup: renameGroup failed for gid" << gid;
        return;
    }

    item->setText(newName);
    qInfo() << "[CameraDetails] Renamed group id" << gid << "to" << newName;
}
