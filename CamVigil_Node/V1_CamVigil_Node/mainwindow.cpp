#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "glcontainerwidget.h"
#include "hik_time.h"
#include "playbackwindow.h"
#include "node_api_server.h"
#include "node_config.h"
#include "node_core_service.h"
#include "node_restreamer.h"
#include "storageservice.h"

#include <QResizeEvent>
#include <QTimer>
#include <QThread>
#include <QFileInfo>
#include <QDebug>
#include <QDir>
#include <numeric>      // std::iota
#include <algorithm>    // std::min

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , gridLayout(new QGridLayout)
    , layoutManager(new LayoutManager(gridLayout))
    , streamManager(new StreamManager(this))
    , archiveManager(nullptr)
    , gridRows(3)
    , gridCols(3)
    , currentFullScreenIndex(-1)
    , cameraManager(nullptr)
    , topNavbar(nullptr)
    , toolbar(nullptr)
    , settingsWindow(nullptr)
    , fullScreenViewer(new FullScreenViewer)
    , timeSyncTimer(nullptr)
    , gridState(9)  // fixed 3x3 => 9 cameras per page
{
    ui->setupUi(this);

    topNavbar = new Navbar(this);
    toolbar   = new Toolbar(this);

    connect(toolbar, &Toolbar::settingsButtonClicked,
            this, &MainWindow::openSettingsWindow);
    connect(toolbar, &Toolbar::playbackButtonClicked,
            this, &MainWindow::openPlaybackWindow);

    // Pagination from toolbar
    connect(toolbar, &Toolbar::nextPageRequested,
            this, &MainWindow::nextPage);
    connect(toolbar, &Toolbar::previousPageRequested,
            this, &MainWindow::previousPage);

    // Group selection from toolbar
    connect(toolbar, &Toolbar::groupChanged,
            this, &MainWindow::onGroupChanged);

    // Layout mode selection from toolbar
    connect(toolbar, &Toolbar::layoutModeChanged,
            this, &MainWindow::onLayoutModeChanged);

    // CameraManager + profiles
    cameraManager = new CameraManager();
    std::vector<CamHWProfile> profiles = cameraManager->getCameraProfiles();
    cameraManager->syncOsdToJsonAllAsync();
    hik::syncAllAsync(profiles);

    // Hourly time sync
    timeSyncTimer = new QTimer(this);
    timeSyncTimer->setTimerType(Qt::VeryCoarseTimer);
    timeSyncTimer->setInterval(60 * 60 * 1000);
    connect(timeSyncTimer, &QTimer::timeout, this, [this]() {
        hik::syncAllAsync(cameraManager->getCameraProfiles());
    });
    timeSyncTimer->start();

    // Create labels for all cameras (global camera index = i)
    const int totalCameras = static_cast<int>(profiles.size());
    labels.reserve(totalCameras);
    for (int i = 0; i < totalCameras; ++i) {
        ClickableLabel* label = new ClickableLabel(i, this);
        label->setAlignment(Qt::AlignCenter);
        label->setScaledContents(true);
        label->setStyleSheet(
            "border:2px solid #333; "
            "border-radius:5px; "
            "margin:5px; "
            "padding:5px; "
            "background:#000;"
        );
        label->showLoading();
        labels.push_back(label);

        // Optional debug properties for easier inspection
        label->setProperty("cameraIndex", i);

        connect(label, &ClickableLabel::clicked,
                this, &MainWindow::showFullScreenFeed);
    }

    // Layout: fixed 3×3 grid
    layoutManager->setGridSize(gridRows, gridCols);

    // Prepare 9 placeholder widgets for empty slots
    initEmptySlots();

    // Build the initial visible order (no grouping yet: 0..N-1)
    rebuildVisibleOrder();
    // Sync grid state to visible order and initialize toolbar + grid
    syncGridStateWithVisibleOrder();
    refreshGrid();

    GLContainerWidget* gridWidget = new GLContainerWidget(this);
    gridWidget->setLayout(gridLayout);

    QVBoxLayout* mainLayout = new QVBoxLayout();
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(topNavbar, 0, Qt::AlignTop);
    mainLayout->addWidget(gridWidget, 1);
    mainLayout->addWidget(toolbar, 0, Qt::AlignBottom);

    QWidget* centralWidget = new QWidget(this);
    centralWidget->setStyleSheet("background-color: #121212;");
    centralWidget->setLayout(mainLayout);
    setCentralWidget(centralWidget);

    setWindowFlags(Qt::Window |
                   Qt::WindowMinimizeButtonHint |
                   Qt::WindowMaximizeButtonHint |
                   Qt::WindowCloseButtonHint);

    // Archive manager
    archiveManager = new ArchiveManager(this);
    archiveManager->startRecording(profiles);

    // === Node API PoC: begin ===
    const QString nodeConfigPath = QDir::currentPath() + "/node_config.json";
    NodeConfigService nodeCfgService(nodeConfigPath);
    const NodeConfig nodeCfg = nodeCfgService.load();

    m_nodeRestreamer = new NodeRestreamer(nodeCfg, this);
    StorageService* storageService = StorageService::instance();
    m_nodeCoreService = new NodeCoreService(nullptr,
                                            nullptr,
                                            archiveManager,
                                            storageService,
                                            m_nodeRestreamer,
                                            nodeCfg,
                                            this);

    auto registerRestreamerCameras = [this]() {
        if (!m_nodeCoreService || !m_nodeRestreamer) {
            return;
        }
        const QVector<NodeCamera> coreCameras = m_nodeCoreService->listCameras();
        if (coreCameras.isEmpty()) {
            qInfo() << "[NodeAPI] Cameras not ready for restreamer registration.";
            return;
        }
        for (const NodeCamera& cam : coreCameras) {
            if (cam.id <= 0 || cam.rtspMain.isEmpty()) {
                continue;
            }
            m_nodeRestreamer->registerCamera(cam.id, cam.rtspMain);
        }
    };
    registerRestreamerCameras();
    QTimer::singleShot(5000, this, [registerRestreamerCameras]() {
        registerRestreamerCameras();
    });

    if (!m_nodeRestreamer->start()) {
        qWarning() << "[NodeAPI] NodeRestreamer start failed. TODO: integrate GstRTSPServer.";
    }

    m_nodeApiThread = new QThread(this);
    NodeApiServer* apiServer = new NodeApiServer(m_nodeCoreService, nodeCfg);
    apiServer->moveToThread(m_nodeApiThread);
    connect(m_nodeApiThread, &QThread::started, apiServer, &NodeApiServer::start);
    connect(m_nodeApiThread, &QThread::finished, apiServer, &QObject::deleteLater);
    connect(this, &QObject::destroyed, m_nodeApiThread, &QThread::quit);
    m_nodeApiThread->start();
    // === Node API PoC: end ===

    // Initialize grouping model after cameras and DB are ready
    initGroupsAfterCamerasLoaded();

    // Start streaming async
    QTimer::singleShot(0, this, &MainWindow::startStreamingAsync);
}

void MainWindow::initEmptySlots() {
    emptySlots.clear();
    emptySlots.reserve(gridState.camerasPerPage());

    for (int i = 0; i < gridState.camerasPerPage(); ++i) {
        QLabel* placeholder = new QLabel(this);
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setText("Empty");
        placeholder->setProperty("slotIndex", i);
        placeholder->setStyleSheet(
            "border:2px dashed #444;"
            "border-radius:5px;"
            "margin:5px;"
            "padding:5px;"
            "background:#000;"
            "color:#555;"
            "font-size:14px;"
        );
        emptySlots.push_back(placeholder);
    }
}

void MainWindow::rebuildVisibleOrder() {
    // Early hook for future grouping/filtering:
    // Default: visibleOrder is just 0..(labels.size()-1)
    const int n = static_cast<int>(labels.size());
    visibleOrder.resize(n);
    std::iota(visibleOrder.begin(), visibleOrder.end(), 0);

    // Optional: you can sort/reorder visibleOrder here in future based on tags, building, etc.
}

void MainWindow::syncGridStateWithVisibleOrder() {
    // visibleCount = number of visible cameras under current filter/group
    gridState.setVisibleCount(static_cast<int>(visibleOrder.size()));
    updateToolbarPageInfo();
}

void MainWindow::initGroupRepository() {
    if (m_groupRepo) {
        return;
    }
    if (!archiveManager) {
        qWarning() << "[Groups] ArchiveManager not available for GroupRepository";
        return;
    }

    const QString dbPath = archiveManager->databasePath();
    if (dbPath.isEmpty()) {
        qWarning() << "[Groups] No DB path available for GroupRepository";
        return;
    }

    m_groupRepo = std::make_unique<GroupRepository>(dbPath);
    if (!m_groupRepo->ensureSchemaGroups()) {
        qWarning() << "[Groups] ensureSchemaGroups() failed in MainWindow for" << dbPath;
    } else {
        qInfo() << "[Groups] Group schema ready in MainWindow for" << dbPath;
    }
}

void MainWindow::initGroupsAfterCamerasLoaded() {
    initGroupRepository();

    const int cameraCount = static_cast<int>(labels.size());

    if (!m_groupRepo) {
        // Fallback: single in-memory "All Cameras" group
        m_groups.clear();
        CameraGroupRuntime all;
        all.id = -1;
        all.name = tr("All Cameras");
        all.cameraIndexes.reserve(cameraCount);
        for (int i = 0; i < cameraCount; ++i) {
            all.cameraIndexes.push_back(i);
        }
        m_groups.push_back(std::move(all));
        m_currentGroupIndex = 0;

        if (toolbar) {
            QStringList names;
            for (const auto& g : m_groups) {
                names << g.name;
            }
            toolbar->setGroups(names, m_currentGroupIndex);
        }

        applyCurrentGroupToGrid();
        return;
    }

    m_cameraIdToIndex.clear();

    const auto profiles = cameraManager->getCameraProfiles();
    const int profileCount = static_cast<int>(profiles.size());

    for (int i = 0; i < profileCount; ++i) {
        const auto& p = profiles[i];
        const QString mainUrl = QString::fromStdString(p.url);
        const QString displayName = QString::fromStdString(p.displayName);
        const int camId = m_groupRepo->ensureCameraRow(mainUrl, displayName);
        if (camId > 0) {
            m_cameraIdToIndex.insert(camId, i);
        }
    }

    reloadGroupsFromDb();
}

void MainWindow::reloadGroupsFromDb() {
    m_groups.clear();

    if (!m_groupRepo) {
        qWarning() << "[Groups] reloadGroupsFromDb called without repository";
        return;
    }

    QVector<CameraGroupInfo> dbGroups = m_groupRepo->listGroups();

    if (dbGroups.isEmpty()) {
        // No groups yet -> create default "All Cameras"
        const int defaultGroupId = m_groupRepo->createGroup(QStringLiteral("All Cameras"));
        if (defaultGroupId <= 0) {
            qWarning() << "[Groups] Failed to create default All Cameras group";
            return;
        }

        const auto cameraIds = m_cameraIdToIndex.keys();
        for (int camId : cameraIds) {
            QVector<int> singleGroup;
            singleGroup.append(defaultGroupId);
            if (!m_groupRepo->setCameraGroups(camId, singleGroup)) {
                qWarning() << "[Groups] Failed to assign camera" << camId
                           << "to default All Cameras group";
            }
        }

        dbGroups = m_groupRepo->listGroups();
    }

    for (const auto& g : dbGroups) {
        CameraGroupRuntime rt;
        rt.id = g.id;
        rt.name = g.name;

        QVector<int> camIds = m_groupRepo->listCameraIdsForGroup(g.id);
        for (int camId : camIds) {
            const int idx = m_cameraIdToIndex.value(camId, -1);
            if (idx >= 0) {
                rt.cameraIndexes.push_back(idx);
            }
        }
        m_groups.push_back(std::move(rt));
    }

    if (!m_groups.empty()) {
        if (m_currentGroupIndex < 0 ||
            m_currentGroupIndex >= static_cast<int>(m_groups.size())) {
            m_currentGroupIndex = 0;
        }
    } else {
        m_currentGroupIndex = -1;
    }

    if (toolbar) {
        QStringList names;
        for (const auto& g : m_groups) {
            names << g.name;
        }
        toolbar->setGroups(names, m_currentGroupIndex);
    }

    applyCurrentGroupToGrid();
}

void MainWindow::applyCurrentGroupToGrid() {
    visibleOrder.clear();

    const int totalCameras = static_cast<int>(labels.size());

    if (m_currentGroupIndex < 0 ||
        m_currentGroupIndex >= static_cast<int>(m_groups.size())) {
        // Fallback: show all cameras
        visibleOrder.resize(totalCameras);
        std::iota(visibleOrder.begin(), visibleOrder.end(), 0);
    } else {
        const auto& g = m_groups[m_currentGroupIndex];
        visibleOrder.insert(visibleOrder.end(),
                            g.cameraIndexes.begin(),
                            g.cameraIndexes.end());
    }

    gridState.setVisibleCount(static_cast<int>(visibleOrder.size()));
    gridState.setCurrentPage(0);

    updateToolbarPageInfo();
    refreshGrid();
}

void MainWindow::updateToolbarPageInfo() {
    if (!toolbar) return;

    const int page1Based  = gridState.currentPage() + 1;
    const int totalPages  = gridState.totalPages();

    toolbar->setPageInfo(page1Based, totalPages);
}

void MainWindow::onGroupChanged(int index) {
    if (index < 0 || index >= static_cast<int>(m_groups.size())) {
        return;
    }
    m_currentGroupIndex = index;
    applyCurrentGroupToGrid();
}

void MainWindow::onLayoutModeChanged(bool isDefault) {
    if (isDefault) {
        refreshGrid();
    } else {
    }
}

void MainWindow::refreshGrid() {
    // Robustness checks
    Q_ASSERT(static_cast<int>(visibleOrder.size()) <= static_cast<int>(labels.size()));
    Q_ASSERT(static_cast<int>(emptySlots.size()) == gridState.camerasPerPage());

    std::vector<QWidget*> pageWidgets;
    pageWidgets.reserve(gridState.camerasPerPage());

    const int page      = gridState.currentPage();
    const int slotCount = gridState.camerasPerPage();

    qInfo() << "[MainWindow] refreshGrid page" << page
            << "visibleCount" << gridState.visibleCount()
            << "visibleOrder.size" << visibleOrder.size();

    for (int slot = 0; slot < slotCount; ++slot) {
        int visibleIndex = gridState.cameraIndexForSlot(page, slot);

        if (visibleIndex >= 0 && visibleIndex < static_cast<int>(visibleOrder.size())) {
            int globalIndex = visibleOrder[visibleIndex];
            bool validGlobal = (globalIndex >= 0 && globalIndex < static_cast<int>(labels.size()));
            if (!validGlobal) {
                qWarning() << "[MainWindow] INVALID globalIndex" << globalIndex
                           << "for visibleIndex" << visibleIndex;
                QWidget* placeholder = emptySlots[slot];
                placeholder->setProperty("pageIndex", page);
                pageWidgets.push_back(placeholder);
                continue;
            }

            QWidget* w = labels[globalIndex];

            qInfo() << "  slot" << slot
                    << "visibleIndex" << visibleIndex
                    << "globalIndex" << globalIndex
                    << "-> camera label";

            // Optional: debug properties
            w->setProperty("pageIndex", page);
            w->setProperty("slotIndex", slot);
            w->setProperty("visibleIndex", visibleIndex);

            pageWidgets.push_back(w);
        } else {
            qInfo() << "  slot" << slot
                    << "visibleIndex" << visibleIndex
                    << "-> placeholder";

            QWidget* placeholder = emptySlots[slot];
            placeholder->setProperty("pageIndex", page);
            pageWidgets.push_back(placeholder);
        }
    }

    layoutManager->apply(pageWidgets);
}

void MainWindow::nextPage() {
    qInfo() << "[MainWindow] nextPage() called";
    gridState.nextPage();
    updateToolbarPageInfo();
    refreshGrid();
}

void MainWindow::previousPage() {
    qInfo() << "[MainWindow] previousPage() called";
    gridState.previousPage();
    updateToolbarPageInfo();
    refreshGrid();
}


void MainWindow::openSettingsWindow() {
    qInfo() << "[UI] Settings action triggered";

    if (!settingsWindow) {
        settingsWindow = new SettingsWindow(archiveManager, cameraManager, this);
        connect(settingsWindow, &SettingsWindow::groupsMembershipsChanged,
                this, [this]() { reloadGroupsFromDb(); });
    }
    settingsWindow->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    settingsWindow->showFullScreen();
    settingsWindow->raise();
    settingsWindow->activateWindow();
}

void MainWindow::openPlaybackWindow() {
    const QString dbPath = (archiveManager ? archiveManager->archiveRoot() + "/camvigil.sqlite" : QString());
    bool created = false;
    if (!playbackWindow) {
        playbackWindow = new PlaybackWindow(nullptr);
        playbackWindow->setAttribute(Qt::WA_DeleteOnClose, true);
        playbackWindow->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        connect(playbackWindow, &QObject::destroyed, this, [this]{
            qInfo() << "[Main] PlaybackWindow destroyed, clearing pointer";
            playbackWindow = nullptr;
        });
        created = true;
    }

    if (created && !dbPath.isEmpty() && QFileInfo::exists(dbPath)) {
        playbackWindow->openDb(dbPath);
    } else if (created) {
        QStringList camNames;
        const auto profiles = cameraManager->getCameraProfiles();
        camNames.reserve(static_cast<int>(profiles.size()));
        for (const auto& p : profiles) {
            camNames << (p.displayName.empty()
                         ? QString::fromStdString(p.url)
                         : QString::fromStdString(p.displayName));
        }
        playbackWindow->setCameraList(camNames);
    }

    playbackWindow->showFullScreen();
    playbackWindow->raise();
    playbackWindow->activateWindow();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    for (ClickableLabel* label : labels) {
        label->setMinimumSize(1, 1);
    }
}

void MainWindow::showFullScreenFeed(int index) {
    currentFullScreenIndex = index;
    QVariant pixmapVar = labels[index]->property("pixmap");
    QPixmap pixmap = pixmapVar.value<QPixmap>();
    if (!pixmap.isNull()) {
        fullScreenViewer->setImage(pixmap);
        fullScreenViewer->showFullScreen();
        fullScreenViewer->raise();
    }
}

MainWindow::~MainWindow() {
    if (m_nodeApiThread) {
        m_nodeApiThread->quit();
        m_nodeApiThread->wait(2000);
    }
    // NOTE: streamManager here is the member created in the initializer list.
    // The worker used in startStreamingAsync() is owned and deleted via the thread.
    streamManager->stopStreaming();
    if (archiveManager) {
        archiveManager->stopRecording();
        delete archiveManager;
    }
    delete layoutManager;
    delete cameraManager;
    delete ui;
}

void MainWindow::startStreamingAsync() {
    QThread* thread = new QThread;
    // Worker will live in the thread.
    StreamManager* worker = new StreamManager;

    worker->moveToThread(thread);

    auto profiles = cameraManager->getCameraProfiles();
    std::vector<QLabel*> labelPtrs(labels.begin(), labels.end());

    connect(thread, &QThread::started, [worker, profiles, labelPtrs]() {
        worker->startStreaming(profiles, labelPtrs);
    });

    // Forward frame updates to UI
    connect(worker, &StreamManager::frameReady, this, [this](int idx, const QPixmap &pixmap){
        if (idx >= 0 && idx < static_cast<int>(labels.size())) {
            labels[idx]->setPixmap(pixmap);
            if (fullScreenViewer->isVisible() && idx == currentFullScreenIndex) {
                fullScreenViewer->setImage(pixmap);
            }
        }
    });

    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);

    thread->start();
}
