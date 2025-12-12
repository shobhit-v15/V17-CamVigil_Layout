#include "camera_grouping_widget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QDebug>
#include <QSet>

namespace {
constexpr int kGroupIdAllFallback = -1;
}

CameraGroupingWidget::CameraGroupingWidget(CameraManager* cameraManager,
                                           const QString& dbPath,
                                           QWidget* parent)
    : QWidget(parent)
    , m_cameraManager(cameraManager)
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(6);

    auto* barLayout = new QHBoxLayout();
    barLayout->setContentsMargins(0, 0, 0, 0);
    barLayout->setSpacing(12);

    auto makeLabel = [](const QString& text) {
        QLabel* lbl = new QLabel(text);
        lbl->setStyleSheet("font-size: 18px; font-weight: bold; color: white;");
        return lbl;
    };

    barLayout->addWidget(makeLabel("Groups:"));

    m_groupCombo = new QComboBox(this);
    m_groupCombo->setStyleSheet("font-size: 18px; font-weight: bold; color: white; background-color: #4d4d4d;");
    m_groupCombo->setMinimumWidth(160);
    m_groupCombo->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    barLayout->addWidget(m_groupCombo);

    m_deleteGroupBtn = new QPushButton(tr("Delete"), this);
    m_deleteGroupBtn->setStyleSheet("font-size:14px; font-weight:bold; color:white; background-color:#b02a37;");
    m_deleteGroupBtn->setVisible(false);
    barLayout->addWidget(m_deleteGroupBtn);

    barLayout->addWidget(makeLabel("Cameras:"));

    m_cameraCombo = new QComboBox(this);
    m_cameraCombo->setStyleSheet("font-size: 18px; font-weight: bold; color: white; background-color: #4d4d4d;");
    m_cameraCombo->setMinimumWidth(200);
    m_cameraCombo->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    barLayout->addWidget(m_cameraCombo);

    m_editButton = new QPushButton(tr("Edit"), this);
    m_editButton->setStyleSheet("font-size:16px; font-weight:bold; color:white; background-color:#2d6cdf;");
    barLayout->addWidget(m_editButton);

    barLayout->addStretch();
    rootLayout->addLayout(barLayout);

    m_panelContainer = new QWidget(this);
    m_panelContainer->setVisible(false);
    m_panelLayout = new QVBoxLayout(m_panelContainer);
    m_panelLayout->setContentsMargins(0, 0, 0, 0);
    m_panelLayout->setSpacing(6);
    rootLayout->addWidget(m_panelContainer);

    setLayout(rootLayout);

    initGroupRepository(dbPath);
    rebuildCameraCache();

    connect(m_groupCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CameraGroupingWidget::handleGroupChanged);
    connect(m_cameraCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CameraGroupingWidget::handleCameraChanged);
    connect(m_editButton, &QPushButton::clicked,
            this, &CameraGroupingWidget::handleEditClicked);
    connect(m_deleteGroupBtn, &QPushButton::clicked,
            this, &CameraGroupingWidget::handleDeleteGroup);

    reloadGroups(m_allGroupId > 0 ? m_allGroupId : kGroupIdAllFallback);
}

void CameraGroupingWidget::initGroupRepository(const QString& dbPath)
{
    if (dbPath.isEmpty()) {
        qWarning() << "[CameraGroupingWidget] Empty DB path; grouping limited to All Cameras view";
        return;
    }
    m_groupRepo = std::make_unique<GroupRepository>(dbPath);
    if (!m_groupRepo->ensureSchemaGroups()) {
        qWarning() << "[CameraGroupingWidget] ensureSchemaGroups() failed; disabling DB-backed grouping";
        m_groupRepo.reset();
    }
}

void CameraGroupingWidget::rebuildCameraCache()
{
    m_allCameraIds.clear();
    m_cameraIdToIndex.clear();
    m_cameraIdToName.clear();

    if (!m_cameraManager) return;

    const auto profiles = m_cameraManager->getCameraProfiles();
    for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
        const auto& p = profiles[i];
        const QString name = QString::fromStdString(p.displayName);
        int camId = i;
        if (m_groupRepo) {
            camId = m_groupRepo->ensureCameraRow(QString::fromStdString(p.url), name);
        }
        if (camId <= 0) {
            qWarning() << "[CameraGroupingWidget] Invalid camera id for index" << i << "skipping";
            continue;
        }
        m_allCameraIds.push_back(camId);
        m_cameraIdToIndex.insert(camId, i);
        m_cameraIdToName.insert(camId, name);
    }
}

void CameraGroupingWidget::reloadGroups(int preferredGroupId)
{
    if (!m_groupCombo) return;

    clearPanels();

    int fallbackId = (preferredGroupId != std::numeric_limits<int>::min())
                     ? preferredGroupId : m_currentGroupId;

    m_groupCombo->blockSignals(true);
    m_groupCombo->clear();

    QVector<CameraGroupInfo> groups;
    if (m_groupRepo) {
        groups = m_groupRepo->listGroups();
        for (const auto& g : groups) {
            if (g.name.compare(QStringLiteral("All Cameras"), Qt::CaseInsensitive) == 0) {
                m_allGroupId = g.id;
                break;
            }
        }
    }

    const int allId = effectiveAllGroupId();
    m_groupCombo->addItem(QStringLiteral("All Cameras"), allId);

    for (const auto& g : groups) {
        if (g.id == m_allGroupId) continue;
        m_groupCombo->addItem(g.name, g.id);
    }

    m_groupCombo->addItem(tr("Add group…"), kGroupIdAddSentinel);
    m_groupCombo->blockSignals(false);

    if (!setGroupComboToId(fallbackId)) {
        setGroupComboToId(allId);
    }
}

void CameraGroupingWidget::reloadCameras()
{
    rebuildCameraCache();
    if (isAllGroup(m_currentGroupId)) {
        loadAllCameras();
    } else {
        loadCamerasForGroup(m_currentGroupId);
    }
}

void CameraGroupingWidget::handleGroupChanged(int comboIndex)
{
    if (!m_groupCombo || comboIndex < 0) return;

    const int groupId = m_groupCombo->itemData(comboIndex).toInt();
    if (groupId == kGroupIdAddSentinel) {
        showCreateGroupPanel(m_currentGroupId);
        return;
    }

    m_currentGroupId = groupId;
    m_currentGroupName = m_groupCombo->itemText(comboIndex);

    clearPanels();

    if (isAllGroup(groupId) || !m_groupRepo) {
        m_deleteGroupBtn->setVisible(false);
        loadAllCameras();
    } else {
        m_deleteGroupBtn->setVisible(true);
        loadCamerasForGroup(groupId);
    }

    emit groupChanged(m_currentGroupId, m_currentGroupName);
}

void CameraGroupingWidget::handleCameraChanged(int comboIndex)
{
    if (!m_cameraCombo || comboIndex < 0) {
        m_currentCameraIndex = -1;
        m_editButton->setEnabled(false);
        emit cameraChanged(-1);
        return;
    }

    const int value = m_cameraCombo->itemData(comboIndex).toInt();
    if (!isAllGroup(m_currentGroupId) && value == kCameraAddSentinel) {
        showAssignPanel();
        m_currentCameraIndex = -1;
        m_editButton->setEnabled(false);
        emit cameraChanged(-1);
        return;
    }

    m_currentCameraIndex = value;
    const bool validCam = (m_currentCameraIndex >= 0);
    m_editButton->setEnabled(validCam);
    emit cameraChanged(m_currentCameraIndex);
}

void CameraGroupingWidget::handleEditClicked()
{
    emit editCameraRequested(m_currentCameraIndex);
}

void CameraGroupingWidget::handleDeleteGroup()
{
    if (!m_groupRepo || isAllGroup(m_currentGroupId) || m_currentGroupId <= 0) {
        return;
    }

    const auto answer = QMessageBox::question(
        this,
        tr("Delete group"),
        tr("Delete group \"%1\" and free its cameras?")
            .arg(m_currentGroupName));
    if (answer != QMessageBox::Yes) {
        return;
    }

    if (!m_groupRepo->deleteGroup(m_currentGroupId)) {
        QMessageBox::warning(this, tr("Delete group"),
                             tr("Failed to delete group \"%1\"").arg(m_currentGroupName));
        return;
    }

    emit membershipsChanged();
    reloadGroups(effectiveAllGroupId());
}

void CameraGroupingWidget::handleCreateGroupOk()
{
    if (!m_groupRepo || !m_newGroupEdit) return;
    const QString name = m_newGroupEdit->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Create group"), tr("Group name cannot be empty."));
        return;
    }

    const QVector<CameraGroupInfo> existing = m_groupRepo->listGroups();
    for (const auto& g : existing) {
        if (g.name.compare(name, Qt::CaseInsensitive) == 0) {
            QMessageBox::warning(this, tr("Create group"),
                                 tr("A group named \"%1\" already exists.").arg(name));
            return;
        }
    }

    const int newId = m_groupRepo->createGroup(name);
    if (newId <= 0) {
        QMessageBox::warning(this, tr("Create group"), tr("Failed to create group \"%1\".").arg(name));
        return;
    }

    hideCreateGroupPanel(false);
    reloadGroups(newId);
    emit membershipsChanged();
}

void CameraGroupingWidget::handleCreateGroupCancel()
{
    hideCreateGroupPanel(true);
}

void CameraGroupingWidget::handleAssignSave()
{
    if (!m_groupRepo || !m_assignList || isAllGroup(m_currentGroupId)) {
        hideAssignPanel(false);
        return;
    }

    const int allId = effectiveAllGroupId();
    if (allId <= 0) {
        QMessageBox::warning(this, tr("Assign cameras"),
                             tr("Unable to determine the \"All Cameras\" group."));
        hideAssignPanel(false);
        return;
    }

    QSet<int> selected;
    for (int i = 0; i < m_assignList->count(); ++i) {
        if (auto* item = m_assignList->item(i)) {
            if (item->checkState() == Qt::Checked) {
                const int camId = item->data(Qt::UserRole).toInt();
                if (camId > 0) selected.insert(camId);
            }
        }
    }

    const QVector<int> previousMembers = m_groupRepo->listCameraIdsForGroup(m_currentGroupId);
    QSet<int> prevSet;
    for (int id : previousMembers) prevSet.insert(id);

    for (int camId : selected) {
        QVector<int> membership;
        membership << allId << m_currentGroupId;
        m_groupRepo->setCameraGroups(camId, membership);
    }

    for (int camId : prevSet) {
        if (!selected.contains(camId)) {
            QVector<int> membership;
            membership << allId;
            m_groupRepo->setCameraGroups(camId, membership);
        }
    }

    hideAssignPanel(false);
    loadCamerasForGroup(m_currentGroupId);
    emit membershipsChanged();
    emit groupChanged(m_currentGroupId, m_currentGroupName);
}

void CameraGroupingWidget::handleAssignCancel()
{
    hideAssignPanel(true);
}

void CameraGroupingWidget::loadAllCameras()
{
    if (!m_cameraCombo || !m_cameraManager) return;

    m_cameraCombo->blockSignals(true);
    m_cameraCombo->clear();

    const auto profiles = m_cameraManager->getCameraProfiles();
    for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
        m_cameraCombo->addItem(QString::fromStdString(profiles[i].displayName), i);
    }

    if (m_cameraCombo->count() == 0) {
        m_cameraCombo->addItem(tr("No cameras available"), -1);
        m_editButton->setEnabled(false);
    }

    m_cameraCombo->blockSignals(false);
    if (m_cameraCombo->count() > 0) {
        m_cameraCombo->setCurrentIndex(0);
    }
    handleCameraChanged(m_cameraCombo->currentIndex());
}

void CameraGroupingWidget::loadCamerasForGroup(int groupId)
{
    if (!m_cameraCombo) return;

    m_cameraCombo->blockSignals(true);
    m_cameraCombo->clear();

    QVector<int> camIds;
    if (m_groupRepo && groupId > 0) {
        camIds = m_groupRepo->listCameraIdsForGroup(groupId);
    }

    for (int camId : camIds) {
        const int camIndex = m_cameraIdToIndex.value(camId, -1);
        if (camIndex < 0) continue;
        const QString name = m_cameraIdToName.value(camId, tr("Camera %1").arg(camId));
        m_cameraCombo->addItem(name, camIndex);
    }

    const int addIndex = m_cameraCombo->count();
    m_cameraCombo->addItem(tr("Add cameras…"), kCameraAddSentinel);

    m_cameraCombo->blockSignals(false);

    if (!camIds.isEmpty()) {
        m_cameraCombo->setCurrentIndex(0);
        m_editButton->setEnabled(true);
    } else {
        m_cameraCombo->setCurrentIndex(addIndex);
        m_editButton->setEnabled(false);
    }

    handleCameraChanged(m_cameraCombo->currentIndex());
}

void CameraGroupingWidget::clearPanels()
{
    hideCreateGroupPanel(false);
    hideAssignPanel(false);
}

void CameraGroupingWidget::showCreateGroupPanel(int previousGroupId)
{
    if (!m_groupRepo) {
        QMessageBox::information(this, tr("Create group"),
                                 tr("Cannot create groups without a valid database."));
        setGroupComboToId(previousGroupId);
        return;
    }

    clearPanels();

    if (!m_createGroupPanel) {
        m_createGroupPanel = new QWidget(this);
        auto* layout = new QHBoxLayout(m_createGroupPanel);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);

        QLabel* lbl = new QLabel(tr("Create new group:"), m_createGroupPanel);
        lbl->setStyleSheet("font-size: 16px; font-weight: bold; color: white;");
        layout->addWidget(lbl);

        m_newGroupEdit = new QLineEdit(m_createGroupPanel);
        m_newGroupEdit->setPlaceholderText(tr("Group name"));
        m_newGroupEdit->setStyleSheet("font-size: 16px; font-weight: bold; color: white; background-color: #4d4d4d;");
        layout->addWidget(m_newGroupEdit, 1);

        m_newGroupOkBtn = new QPushButton(tr("OK"), m_createGroupPanel);
        m_newGroupOkBtn->setStyleSheet("font-size:14px; font-weight:bold; color:white; background-color:#2d6cdf;");
        layout->addWidget(m_newGroupOkBtn);

        m_newGroupCancelBtn = new QPushButton(tr("Cancel"), m_createGroupPanel);
        m_newGroupCancelBtn->setStyleSheet("font-size:14px; font-weight:bold; color:white; background-color:#6c757d;");
        layout->addWidget(m_newGroupCancelBtn);

        connect(m_newGroupOkBtn, &QPushButton::clicked,
                this, &CameraGroupingWidget::handleCreateGroupOk);
        connect(m_newGroupCancelBtn, &QPushButton::clicked,
                this, &CameraGroupingWidget::handleCreateGroupCancel);
    }

    m_createPanelPreviousGroupId = previousGroupId;
    m_newGroupEdit->clear();
    m_newGroupEdit->setFocus(Qt::TabFocusReason);

    m_panelLayout->addWidget(m_createGroupPanel);
    m_createGroupPanel->show();
    m_panelContainer->setVisible(true);

    m_groupCombo->setEnabled(false);
    m_cameraCombo->setEnabled(false);
    m_deleteGroupBtn->setEnabled(false);
    m_editButton->setEnabled(false);
}

void CameraGroupingWidget::hideCreateGroupPanel(bool restoreSelection)
{
    if (!m_createGroupPanel) return;
    m_panelLayout->removeWidget(m_createGroupPanel);
    m_createGroupPanel->hide();

    m_groupCombo->setEnabled(true);
    m_cameraCombo->setEnabled(true);
    m_deleteGroupBtn->setEnabled(true);
    m_editButton->setEnabled(true);
    m_panelContainer->setVisible(m_assignPanel && m_assignPanel->isVisible());

    if (restoreSelection) {
        setGroupComboToId(m_createPanelPreviousGroupId);
    }
}

void CameraGroupingWidget::showAssignPanel()
{
    if (!m_groupRepo || isAllGroup(m_currentGroupId)) return;

    clearPanels();

    if (!m_assignPanel) {
        m_assignPanel = new QWidget(this);
        auto* layout = new QVBoxLayout(m_assignPanel);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        m_assignTitle = new QLabel(m_assignPanel);
        m_assignTitle->setStyleSheet("font-size: 16px; font-weight: bold; color: white;");
        layout->addWidget(m_assignTitle);

        m_assignList = new QListWidget(m_assignPanel);
        m_assignList->setStyleSheet("font-size: 16px; color: white; background-color: #2b2b2b;");
        m_assignList->setSelectionMode(QAbstractItemView::NoSelection);
        layout->addWidget(m_assignList);

        auto* btnRow = new QHBoxLayout();
        btnRow->setContentsMargins(0, 0, 0, 0);
        btnRow->setSpacing(8);
        btnRow->addStretch();

        m_assignSaveBtn = new QPushButton(tr("Save"), m_assignPanel);
        m_assignSaveBtn->setStyleSheet("font-size:14px; font-weight:bold; color:white; background-color:#2d6cdf;");
        btnRow->addWidget(m_assignSaveBtn);

        m_assignCancelBtn = new QPushButton(tr("Cancel"), m_assignPanel);
        m_assignCancelBtn->setStyleSheet("font-size:14px; font-weight:bold; color:white; background-color:#6c757d;");
        btnRow->addWidget(m_assignCancelBtn);

        layout->addLayout(btnRow);

        connect(m_assignSaveBtn, &QPushButton::clicked,
                this, &CameraGroupingWidget::handleAssignSave);
        connect(m_assignCancelBtn, &QPushButton::clicked,
                this, &CameraGroupingWidget::handleAssignCancel);
    }

    if (m_assignTitle) {
        m_assignTitle->setText(
            tr("Add cameras to \"%1\":").arg(m_currentGroupName));
    }

    populateAssignList();

    m_panelLayout->addWidget(m_assignPanel);
    m_assignPanel->show();
    m_panelContainer->setVisible(true);

    m_groupCombo->setEnabled(false);
    m_cameraCombo->setEnabled(false);
    m_deleteGroupBtn->setEnabled(false);
    m_editButton->setEnabled(false);
}

void CameraGroupingWidget::hideAssignPanel(bool restoreSelection)
{
    if (!m_assignPanel) return;
    m_panelLayout->removeWidget(m_assignPanel);
    m_assignPanel->hide();

    m_groupCombo->setEnabled(true);
    m_cameraCombo->setEnabled(true);
    m_deleteGroupBtn->setEnabled(true);
    m_editButton->setEnabled(true);
    m_panelContainer->setVisible(m_createGroupPanel && m_createGroupPanel->isVisible());

    if (restoreSelection) {
        loadCamerasForGroup(m_currentGroupId);
    }
}

void CameraGroupingWidget::populateAssignList()
{
    if (!m_assignList) return;
    m_assignList->clear();

    const QVector<int> currentMembers = m_groupRepo->listCameraIdsForGroup(m_currentGroupId);
    QSet<int> currentSet;
    for (int id : currentMembers) currentSet.insert(id);

    QSet<int> usedElsewhere;
    const QVector<CameraGroupInfo> groups = m_groupRepo->listGroups();
    for (const auto& g : groups) {
        if (g.id == m_allGroupId || g.id == m_currentGroupId) continue;
        const QVector<int> ids = m_groupRepo->listCameraIdsForGroup(g.id);
        for (int id : ids) usedElsewhere.insert(id);
    }

    for (int camId : m_allCameraIds) {
        if (!currentSet.contains(camId) && usedElsewhere.contains(camId)) {
            continue;
        }
        const QString label = m_cameraIdToName.value(camId, tr("Camera %1").arg(camId));
        auto* item = new QListWidgetItem(label, m_assignList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        item->setCheckState(currentSet.contains(camId) ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, camId);
    }
}

bool CameraGroupingWidget::setGroupComboToId(int groupId)
{
    if (!m_groupCombo) return false;
    const int index = m_groupCombo->findData(groupId);
    if (index < 0) return false;
    if (m_groupCombo->currentIndex() == index) {
        handleGroupChanged(index);
    } else {
        m_groupCombo->setCurrentIndex(index);
    }
    return true;
}

QString CameraGroupingWidget::groupNameForId(int groupId) const
{
    if (!m_groupCombo) return QString();
    for (int i = 0; i < m_groupCombo->count(); ++i) {
        if (m_groupCombo->itemData(i).toInt() == groupId) {
            return m_groupCombo->itemText(i);
        }
    }
    return QString();
}

bool CameraGroupingWidget::isAllGroup(int groupId) const
{
    const int allId = effectiveAllGroupId();
    return groupId == allId || groupId == kGroupIdAllFallback;
}

int CameraGroupingWidget::effectiveAllGroupId() const
{
    return (m_allGroupId > 0) ? m_allGroupId : kGroupIdAllFallback;
}
