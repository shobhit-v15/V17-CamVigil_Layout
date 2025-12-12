#include <QDir>
#include <QProcess>
#include "playback_exporter.h"
#include "playbackwindow.h"
#include <QDebug>
#include <QMetaType>
#include <QDateTime>
#include <QCloseEvent>
#include "playback_timeline_view.h"
#include "playback_db_service.h"
#include "playback_video_box.h"
#include "playback_title_bar.h"
#include "playback_side_controls.h"
#include <QThread>
#include <QVBoxLayout>
#include "playback_video_player_gst.h"
#include "playback_stitching_player.h"
#include "storageservice.h"
#include <QMessageBox>
#include <QApplication>
#include <QSet>
#include <algorithm>

PlaybackWindow::PlaybackWindow(QWidget* parent)
    : QWidget(parent)
{
    qInfo() << "[PW] ctor tid=" << tid() << "this=" << this;
    // Register types used via queued invokeMethod / signals
    qRegisterMetaType<quintptr>("quintptr");
    qRegisterMetaType<PlaybackVideoPlayerGst*>("PlaybackVideoPlayerGst*");
    qRegisterMetaType<SegmentMeta>("SegmentMeta");
    qRegisterMetaType<QVector<SegmentMeta>>("QVector<SegmentMeta>");
    setWindowTitle("Playback");
    setAttribute(Qt::WA_DeleteOnClose, true);
    setStyleSheet("background-color:#111; color:white;");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0,0,0,0);
    root->setSpacing(0);
    // Header bar (title + controls + close)
        titleBar = new PlaybackTitleBar(this);
        titleBar->setTitle("Playback");
        controls = new PlaybackControlsWidget(titleBar);
        titleBar->setRightWidget(controls);
        connect(titleBar, &PlaybackTitleBar::closeRequested, this, [this]{ close(); });
    // Body placeholder
    auto* body = new QWidget(this);
    body->setStyleSheet("background:#111;");
    auto* bodyLay = new QHBoxLayout(body);
        bodyLay->setContentsMargins(12,12,12,12);
        bodyLay->setSpacing(12);

        // --- New: Video box (left) + stacked controls (right) ---
        videoBox = new PlaybackVideoBox(body);
        videoBox->setPlaceholder(QStringLiteral("Please select the camera and date"));
        videoBox->setStyleSheet("background:#111;");
        videoBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

           sideControls = new PlaybackSideControls(body);
           sideControls->setFixedWidth(140);
            // (signals ready for wiring later)
            // connect(sideControls, &PlaybackSideControls::playClicked,  ...);
            // connect(sideControls, &PlaybackSideControls::pauseClicked, ...);

        bodyLay->addWidget(videoBox, 1);
        bodyLay->addWidget(sideControls, 0);

        root->addWidget(titleBar);
        root->addWidget(body, 1);
    // bottom 24h timeline view
    timelineView = new PlaybackTimelineView(this);
    timelineView->setStyleSheet("background:#111;");
    root->addWidget(timelineView); // bottom bar
    // --- New: compact Trim/Export panel (always visible, disabled until checkbox ON)
    trimPanel = new PlaybackTrimPanel(this);
    root->addWidget(trimPanel);
    trimPanel->setEnabledPanel(false);
    setLayout(root);
    initPlayer_();
    initStitch_();
    // Ensure queued connections can deliver these types
    qRegisterMetaType<SegmentInfo>("SegmentInfo");
    qRegisterMetaType<CamList>("CamList");
    qRegisterMetaType<SegmentList>("SegmentList");

    connect(controls, &PlaybackControlsWidget::groupChanged,
            this, &PlaybackWindow::onUiGroupChanged);
    connect(controls, &PlaybackControlsWidget::cameraChanged,
            this, &PlaybackWindow::onUiCameraChanged);
    //connect(controls, &PlaybackControlsWidget::dateChanged,
      //      this, &PlaybackWindow::onUiDateChanged);
    // Controller handles Go→query→build
        timelineCtl = new PlaybackTimelineController(this);
        connect(controls, &PlaybackControlsWidget::goPressed,
                timelineCtl, &PlaybackTimelineController::onGo);
        connect(controls, &PlaybackControlsWidget::goPressed, this,
                    [this](const QString& /*cam*/, const QDate& day){ currentDay_ = day; });
        connect(timelineCtl, &PlaybackTimelineController::built, this,
                    [this](const QDate&, const PlaybackTimelineModel& m){
                        timelineView->setModel(&m);
                        controls->setGoIdle();   // explicitly reset button
                    });
        connect(timelineCtl, &PlaybackTimelineController::log, this,
                [](const QString& s){ qInfo().noquote() << s; });
        // Timeline seek → stitching seek (wall clock)
        connect(timelineView, &PlaybackTimelineView::seekRequested, this, [this](qint64 day_offset_ns){
             if (!stitch_) return;
             qint64 t = day_offset_ns;
             if (trim_.enabled) {
                 t = qBound<qint64>(trim_.start_ns, t, trim_.end_ns - 1);
                 // also pin playhead visually if user dragged outside
                 timelineView->setPlayheadNs(t);
             }
             const qint64 wall = dayStartNs_ + t;
             QMetaObject::invokeMethod(stitch_, "seekWall", Qt::QueuedConnection, Q_ARG(qint64, wall));
         });

            // Note: Side controls connections moved to initStitch_() to ensure proper timing
            // ---------- Trim/Export wiring ----------
                // Checkbox toggles trim mode
                connect(trimPanel, &PlaybackTrimPanel::trimModeToggled, this, [this](bool on){
                    trim_.enabled = on;
                    const qint64 dayNs = 24LL*3600LL*1000000000LL;
                    const qint64 ph = timelineView->playheadNs();
                    qint64 s = qBound<qint64>(0, ph, dayNs - 2'000'000'000LL);
                    qint64 e = qMin(s + 60LL*1000000000LL, dayNs - 1);
                    trim_.start_ns = s; trim_.end_ns = e;

                    timelineView->setSelection(trim_.start_ns, trim_.end_ns, on);
                    trimPanel->setEnabledPanel(on);
                    trimPanel->setDayStartNs(dayStartNs_);
                    if (on) trimPanel->setPhaseIdle();
                    trimPanel->setRangeNs(trim_.start_ns, trim_.end_ns);
                    // start playback cursor at the selection start for clarity
                    if (timelineView) timelineView->setPlayheadNs(trim_.start_ns);
                });

                // Edits from panel → selection
                connect(trimPanel, &PlaybackTrimPanel::startEditedNs, this, [this](qint64 s){
                    if (!trim_.enabled) return;
                    s = qBound<qint64>(0, s, trim_.end_ns - 1);
                    trim_.start_ns = s;
                    timelineView->setSelection(trim_.start_ns, trim_.end_ns, true);
                    trimPanel->setDurationLabel(trim_.end_ns - trim_.start_ns);
                });
                connect(trimPanel, &PlaybackTrimPanel::endEditedNs, this, [this](qint64 e){
                    if (!trim_.enabled) return;
                    const qint64 dayNs = 24LL*3600LL*1000000000LL;
                    e = qBound<qint64>(trim_.start_ns + 1, e, dayNs - 1);
                    trim_.end_ns = e;
                    timelineView->setSelection(trim_.start_ns, trim_.end_ns, true);
                    trimPanel->setDurationLabel(trim_.end_ns - trim_.start_ns);
                });

                // Dragging on timeline → panel
                connect(timelineView, &PlaybackTimelineView::selectionChanged, this, [this](qint64 s, qint64 e){
                    if (!trim_.enabled) return;
                    trim_.start_ns = s; trim_.end_ns = e;
                    trimPanel->setRangeNs(s, e);
                });

                // --- Export two-phase wiring (Clip -> Save) ---
                                connect(trimPanel, &PlaybackTrimPanel::clipRequested, this, [this](){
                                    if (!trim_.enabled || !db || selectedCamId <= 0) return;
                                    if (exportThread_) { qWarning() << "[Export] already running"; return; }

                                    // Spawn exporter thread
                                    exportThread_ = new QThread(this);
                                    exporter_ = new PlaybackExporter();
                                    exporter_->moveToThread(exportThread_);
                                    connect(exportThread_, &QThread::finished, exporter_, &QObject::deleteLater);
                                    exportThread_->start();

                                    // Configure for CLIP stage (temp preparation – no external required)
                                    ExportOptions clipOpts;
                                    clipOpts.baseName = currentDay_.isValid()
                                        ? currentDay_.toString("yyyy-MM-dd")
                                        : QDate::currentDate().toString("yyyy-MM-dd");
                                    clipOpts.precise   = false;
                                    clipOpts.copyAudio = true;
                                    exporter_->setPlaylist(segIndex_.playlist(), dayStartNs_);
                                    exporter_->setSelection(trim_.start_ns, trim_.end_ns);
                                    exporter_->setOptions(clipOpts);

                                    trimPanel->setPhaseClipping();   // shows "Clipping %p%", gray bar

                                    // Progress during clipping
                                    connect(exporter_, &PlaybackExporter::progress,
                                            trimPanel, &PlaybackTrimPanel::setProgress,
                                            Qt::QueuedConnection);

                                    // Prepared -> allow save, mark as "Video clipped"
                                    connect(exporter_, &PlaybackExporter::prepared, this, [this](){
                                        trimPanel->setPhaseClipped();
                                    }, Qt::QueuedConnection);

                                    // Any error during clipping
                                    connect(exporter_, &PlaybackExporter::error, this, [this](const QString& e){
                                        qWarning() << "[Export][clip] error:" << e;
                                        trimPanel->setPhaseError(e);
                                        trimPanel->enableSave(false);
                                        // teardown exporter thread
                                        exportThread_->quit();
                                        if (!exportThread_->wait(2000)) { exportThread_->terminate(); exportThread_->wait(); }
                                        exportThread_->deleteLater(); exportThread_=nullptr; exporter_=nullptr;
                                        QMessageBox::warning(this, tr("Export failed"), e);
                                    }, Qt::QueuedConnection);

                                    // Kick the CLIP stage
                                    QMetaObject::invokeMethod(exporter_, "startPrepare", Qt::QueuedConnection);
                                }, Qt::QueuedConnection);

                                connect(trimPanel, &PlaybackTrimPanel::saveRequested, this, [this](){
                                    if (!exporter_ || !exportThread_) return;

                                    // Guard: external storage present & usable
                                    auto *ss = StorageService::instance();
                                    if (!ss->hasExternal()) {
                                        QMessageBox::warning(this, tr("External media required"),
                                                             tr("No external USB storage detected.\nInsert a USB drive to save the clip."));
                                        return;
                                    }
                                    trimPanel->setPhaseSaving();

                                    // Set final output directory on external
                                    ExportOptions saveOpts;
                                    saveOpts.outDir = QDir(ss->externalRoot()).filePath("CamVigilExports");
                                    // hand options to exporter on its thread and start save
                                    QMetaObject::invokeMethod(exporter_, [this, saveOpts](){
                                        exporter_->setOptions(saveOpts);
                                        exporter_->saveToExternal();
                                    }, Qt::QueuedConnection);

                                    // Progress during saving
                                    connect(exporter_, &PlaybackExporter::progress,
                                            trimPanel, &PlaybackTrimPanel::setProgress,
                                            Qt::QueuedConnection);

                                    // Success
                                    connect(exporter_, &PlaybackExporter::saved, this, [this](const QString& outPath){
                                        qInfo() << "[Export] saved:" << outPath;
                                        trimPanel->setPhaseSaved();
                                        // cleanup exporter thread
                                        exportThread_->quit();
                                        if (!exportThread_->wait(2000)) { exportThread_->terminate(); exportThread_->wait(); }
                                        exportThread_->deleteLater(); exportThread_=nullptr; exporter_=nullptr;
                                    }, Qt::QueuedConnection);

                                    // Error while saving
                                    connect(exporter_, &PlaybackExporter::error, this, [this](const QString& e){
                                        qWarning() << "[Export][save] error:" << e;
                                        trimPanel->setPhaseError(e);
                                        QMessageBox::warning(this, tr("Save failed"), e);
                                        // keep exporter thread alive so user can retry Save without reclipping?
                                        // If you prefer to teardown on error, uncomment:
                                        // exportThread_->quit();
                                        // if (!exportThread_->wait(2000)) { exportThread_->terminate(); exportThread_->wait(); }
                                        // exportThread_->deleteLater(); exportThread_=nullptr; exporter_=nullptr;
                                    }, Qt::QueuedConnection);
                                }, Qt::QueuedConnection);
}
void PlaybackWindow::stopPlayer_() {
    if (!playerThread_) return;
    qInfo() << "[PW] stopPlayer_";
    if (player_) {
        // Ensure GStreamer is shut down on its own thread
        QMetaObject::invokeMethod(player_, "teardown",
                                  Qt::BlockingQueuedConnection);
        player_ = nullptr;
    }
    playerThread_->quit();
    if (!playerThread_->wait(3000)) {
        qWarning() << "[PW] player thread didn't quit in time, terminating…";
        playerThread_->terminate();
        playerThread_->wait();
    }
    playerThread_->deleteLater();
    playerThread_ = nullptr;
}

void PlaybackWindow::stopStitch_() {
    if (!stitchThread_) return;
    qInfo() << "[PW] stopStitch_";
    if (stitch_) {
        // provide a no-op if not implemented yet
        QMetaObject::invokeMethod(stitch_, "stop",
                                  Qt::BlockingQueuedConnection);
        stitch_ = nullptr;
    }
    stitchThread_->quit();
    if (!stitchThread_->wait(3000)) {
        qWarning() << "[PW] stitch thread didn't quit in time, terminating…";
        stitchThread_->terminate();
        stitchThread_->wait();
    }
    stitchThread_->deleteLater();
    stitchThread_ = nullptr;
}

void PlaybackWindow::setCameraList(const QStringList& names) {
    controls->setCameraList(names);
    if (!names.isEmpty()) {
        onUiCameraChanged(names.first());
    } else {
        onUiCameraChanged(QString());
    }
}
QString PlaybackWindow::tid() {
    // QThread::currentThreadId() returns a pointer-like handle; stringify as hex
    const quintptr p = reinterpret_cast<quintptr>(QThread::currentThreadId());
    return QString::number(p, 16);
}
PlaybackWindow::~PlaybackWindow() {
    qInfo() << "[PW] dtor tid=" << tid() << "this=" << this;
    // Ensure background threads are down even if window is destroyed externally
    stopStitch_();
    stopPlayer_();
    if (exportThread_ && exporter_) {
        QMetaObject::invokeMethod(exporter_, "cancel", Qt::QueuedConnection);
        exportThread_->quit();
        if (!exportThread_->wait(2000)) { exportThread_->terminate(); exportThread_->wait(); }
        exportThread_->deleteLater();
        exportThread_ = nullptr;
        exporter_ = nullptr;
    }
}

void PlaybackWindow::closeEvent(QCloseEvent* e) {
    qInfo() << "[PW] closeEvent tid=" << tid();
    controls->setEnabled(false);
    if (timelineCtl) timelineCtl->detach();
    // Tear down worker threads before letting the widget die
    stopStitch_();
    stopPlayer_();
    e->accept();
    if (exportThread_ && exporter_) {
        QMetaObject::invokeMethod(exporter_, "cancel", Qt::QueuedConnection);
        exportThread_->quit();
        if (!exportThread_->wait(2000)) { exportThread_->terminate(); exportThread_->wait(); }
        exportThread_->deleteLater();
        exportThread_ = nullptr;
        exporter_ = nullptr;
    }
}
void PlaybackWindow::openDb(const QString& dbPath) {
    qInfo() << "[PW] openDb(" << dbPath << ") tid=" << tid();
    if (dbPath.isEmpty()) return;
    // Attach to the shared DB service
        auto svc = PlaybackDbService::instance();
        svc->ensureOpened(dbPath);          // idempotent
        DbReader* newDb = svc->reader();

        if (db != newDb) {
           // Rebind signals safely for this window context
            if (db) QObject::disconnect(db, nullptr, this, nullptr);
            db = newDb;
            connect(db, &DbReader::opened, this, [](bool ok, const QString& err){
                if (!ok) qWarning() << "[Playback] DB open failed:" << err;
            });
            connect(db, &DbReader::camerasReady, this,
                    &PlaybackWindow::onCamerasReady, Qt::QueuedConnection);
            connect(db, &DbReader::daysReady,     this,
                    &PlaybackWindow::onDaysReady, Qt::QueuedConnection);
            connect(db, &DbReader::segmentsReady, this,
                    &PlaybackWindow::onSegmentsReady, Qt::QueuedConnection);
            connect(db, &DbReader::error,         this,
                    [](const QString& e){ qWarning() << "[Playback] DB error:" << e; });
        }

        // Attach controller to shared DbReader and set resolver
        timelineCtl->attach(db);
        timelineCtl->setCameraResolver([this](const QString& name){
            int id = nameToId.value(name, -1);
            qInfo() << "[PW] Camera resolver: '" << name << "' -> ID" << id;
            return id;
        });
        initGroupRepository(dbPath);
        controls->setGoIdle();
    // Fetch cameras
    QMetaObject::invokeMethod(db, "listCameras", Qt::QueuedConnection);
}
void PlaybackWindow::initGroupRepository(const QString& dbPath) {
    if (dbPath.isEmpty() || m_groupRepo) {
        return;
    }
    auto repo = std::make_unique<GroupRepository>(dbPath);
    if (!repo->ensureSchemaGroups()) {
        qWarning() << "[Playback] Unable to prepare group schema for" << dbPath;
        return;
    }
    qInfo() << "[Playback] Group repository ready for playback at" << dbPath;
    m_groupRepo = std::move(repo);
}
void PlaybackWindow::buildPlaybackGroupsFromDb() {
    m_groups.clear();
    if (!m_groupRepo) {
        return;
    }

    const QString allName = QStringLiteral("All Cameras");
    QVector<CameraGroupInfo> dbGroups = m_groupRepo->listGroups();
    auto appendAllGroup = [&](int groupId, const QString& name) {
        PlaybackGroup g;
        g.id = groupId;
        g.name = name;
        for (int camId : camIds) {
            const auto it = m_recordingCameraNames.find(camId);
            if (it == m_recordingCameraNames.end()) continue;
            g.cameraIds.push_back(camId);
            g.cameraNames.push_back(it.value());
        }
        m_groups.push_back(std::move(g));
    };

    if (dbGroups.isEmpty()) {
        appendAllGroup(-1, allName);
    } else {
        bool allInserted = false;
        for (const auto& info : dbGroups) {
            if (info.name == allName) {
                appendAllGroup(info.id, info.name);
                allInserted = true;
                break;
            }
        }
        if (!allInserted) {
            appendAllGroup(-1, allName);
        }

        QSet<int> recordingIds;
        for (auto it = m_recordingCameraNames.constBegin();
             it != m_recordingCameraNames.constEnd(); ++it) {
            recordingIds.insert(it.key());
        }
        for (const auto& info : dbGroups) {
            if (info.name == allName) {
                continue;
            }
            PlaybackGroup g;
            g.id = info.id;
            g.name = info.name;
            QVector<int> dbCamIds = m_groupRepo->listCameraIdsForGroup(info.id);
            for (int camId : dbCamIds) {
                if (!recordingIds.contains(camId)) continue;
                const auto it = m_recordingCameraNames.find(camId);
                if (it == m_recordingCameraNames.end()) continue;
                g.cameraIds.push_back(camId);
                g.cameraNames.push_back(it.value());
            }
            m_groups.push_back(std::move(g));
        }
    }
    qInfo() << "[Playback] buildPlaybackGroupsFromDb: groups=" << m_groups.size();
    for (const auto& g : m_groups) {
        qInfo() << "   group" << g.name << "cameras=" << g.cameraIds.size();
    }
}
void PlaybackWindow::applyCurrentPlaybackGroupToCameraCombo() {
    QStringList names;
    nameToId.clear();

    if (m_groupRepo &&
        m_currentGroupIndex >= 0 &&
        m_currentGroupIndex < static_cast<int>(m_groups.size())) {
        const PlaybackGroup& g = m_groups[m_currentGroupIndex];
        qInfo() << "[Playback] applying group" << g.name
                << "index" << m_currentGroupIndex
                << "cameras" << g.cameraNames.size();
        const int count = std::min(g.cameraIds.size(), g.cameraNames.size());
        names.reserve(count);
        for (int i = 0; i < count; ++i) {
            names << g.cameraNames.at(i);
            nameToId.insert(g.cameraNames.at(i), g.cameraIds.at(i));
        }
    } else {
        qInfo() << "[Playback] applying fallback group (all cameras)"
                << "count" << camIds.size();
        names.reserve(camIds.size());
        for (int camId : camIds) {
            const auto it = m_recordingCameraNames.find(camId);
            if (it == m_recordingCameraNames.end()) continue;
            names << it.value();
            nameToId.insert(it.value(), camId);
        }
    }

    setCameraList(names);
}
void PlaybackWindow::onCamerasReady(const CamList& cams) {
    camIds.clear();
    nameToId.clear();
    m_recordingCameraNames.clear();

    qInfo() << "[Playback] onCamerasReady: cams from DbReader =" << cams.size();
    for (const auto& p : cams) {
        qInfo() << "  ID:" << p.first << "Name:" << p.second;
        camIds << p.first;
        m_recordingCameraNames.insert(p.first, p.second);
    }

    if (m_recordingCameraNames.isEmpty() && m_groupRepo) {
        const auto allCams = m_groupRepo->listAllCameras();
        qInfo() << "[Playback] DbReader returned no cameras, fallback to GroupRepository list size"
                << allCams.size();
        for (const auto& row : allCams) {
            QString name = row.name.trimmed();
            if (name.isEmpty()) {
                name = row.mainUrl.trimmed();
            }
            if (name.isEmpty()) {
                name = QStringLiteral("Camera %1").arg(row.id);
            }
            camIds << row.id;
            m_recordingCameraNames.insert(row.id, name);
        }
    }

    qInfo() << "[Playback] m_recordingCameraNames after fallback =" << m_recordingCameraNames.size();

    if (!m_groupRepo) {
        QStringList names;
        names.reserve(camIds.size());
        for (int camId : camIds) {
            const auto it = m_recordingCameraNames.find(camId);
            if (it == m_recordingCameraNames.end()) {
                continue;
            }
            names << it.value();
            nameToId.insert(it.value(), camId);
        }
        controls->setGroupList(QStringList(), -1);
        m_currentGroupIndex = -1;
        setCameraList(names);
        if (!names.isEmpty()) {
            controls->setDate(QDate::currentDate());
        }
        return;
    }

    if (m_recordingCameraNames.isEmpty()) {
        qWarning() << "[Playback] No cameras available even after fallback; leaving combos empty.";
        controls->setGroupList(QStringList(), -1);
        m_currentGroupIndex = -1;
        setCameraList(QStringList());
        return;
    }

    buildPlaybackGroupsFromDb();

    QStringList groupNames;
    groupNames.reserve(static_cast<int>(m_groups.size()));
    for (const auto& g : m_groups) {
        groupNames << g.name;
    }

    if (!m_groups.empty()) {
        if (m_currentGroupIndex < 0 ||
            m_currentGroupIndex >= static_cast<int>(m_groups.size())) {
            m_currentGroupIndex = 0;
        }
    } else {
        m_currentGroupIndex = -1;
    }

    controls->setGroupList(groupNames, m_currentGroupIndex);
    applyCurrentPlaybackGroupToCameraCombo();
    if (!camIds.isEmpty()) {
        controls->setDate(QDate::currentDate());
    }
}
void PlaybackWindow::onUiCameraChanged(const QString& camName) {
    lastCamName_ = camName;
    if (camName.isEmpty()) {
        selectedCamId = -1;
        qInfo() << "[Playback] cameraChanged -> <none>";
        return;
    }
    const int cid = nameToId.value(camName, -1);
    selectedCamId = cid;
    
    qInfo() << "[Playback] cameraChanged ->" << camName << "id" << cid;
    qInfo() << "[Playback] Available cameras in nameToId:";
    for (auto it = nameToId.begin(); it != nameToId.end(); ++it) {
        qInfo() << "  " << it.key() << "->" << it.value();
    }
    
    if (db && cid > 0) {
        QMetaObject::invokeMethod(db, "listDays", Qt::QueuedConnection,
                                  Q_ARG(int, cid));
    } else {
        qWarning() << "[Playback] Cannot list days: db=" << (db != nullptr) << "cid=" << cid;
        qWarning() << "[Playback] This usually means the camera name doesn't match the database.";
        qWarning() << "[Playback] Check that camera names in cameras.json match those in the database.";
    }
}
void PlaybackWindow::onUiGroupChanged(int index) {
    if (!m_groupRepo) {
        m_currentGroupIndex = -1;
        return;
    }
    if (index < 0 || index >= static_cast<int>(m_groups.size())) {
        qWarning() << "[Playback] Invalid group index" << index << "size=" << m_groups.size();
        return;
    }
    if (m_currentGroupIndex == index) {
        return;
    }
    m_currentGroupIndex = index;
    qInfo() << "[Playback] group changed to index" << index
            << (index >= 0 && index < static_cast<int>(m_groups.size())
                ? m_groups[index].name
                : QStringLiteral("<out-of-range>"));
    applyCurrentPlaybackGroupToCameraCombo();
}
void PlaybackWindow::onDaysReady(int cameraId, const QStringList& ymdList) {
    if (cameraId != selectedCamId) return;

    if (ymdList.isEmpty()) {
        controls->setDateBounds(QDate(), QDate());
        qInfo() << "[Playback] No recordings for camera" << cameraId;
        return;
    }

    QDate minD = QDate::fromString(ymdList.first(), "yyyy-MM-dd");
    QDate maxD = minD;
   QSet<QDate> avail;
    for (const auto& ymd : ymdList) {
        const QDate d = QDate::fromString(ymd, "yyyy-MM-dd");
        if (d.isValid()) {
            avail.insert(d);
            if (d < minD) minD = d;
            if (d > maxD) maxD = d;
        }
    }
    controls->setDateBounds(minD, maxD);
    controls->setAvailableDates(avail);
    controls->setDate(maxD);
}
void PlaybackWindow::onUiDateChanged(const QDate&) { /* no-op by design */ }
void PlaybackWindow::onSegmentsReady(int cameraId, const SegmentList& segs) {
    if (cameraId != selectedCamId) return;
    if (!controls) return;
    const QDate day = currentDay_.isValid() ? currentDay_ : QDate::currentDate();
    if (!day.isValid()) return;

    qInfo() << "[PW] segmentsReady count=" << segs.size()
           << "cid=" << cameraId
           << "day=" << (currentDay_.isValid()? currentDay_.toString("yyyy-MM-dd")
                                              : QDate::currentDate().toString("yyyy-MM-dd"));
    qInfo() << "[PW] segmentsReady count=" << segs.size()
                << "cid=" << cameraId
                << "day=" << (currentDay_.isValid()? currentDay_.toString("yyyy-MM-dd")
                                                   : QDate::currentDate().toString("yyyy-MM-dd"));

        if (!segs.isEmpty()) {
            qint64 minStart = segs.first().start_ns, maxStart = segs.first().start_ns;
            qint64 minEnd   = segs.first().end_ns,   maxEnd   = segs.first().end_ns;
            for (const auto& s : segs) {
                minStart = std::min(minStart, s.start_ns);
                maxStart = std::max(maxStart, s.start_ns);
                minEnd   = std::min(minEnd,   s.end_ns);
                maxEnd   = std::max(maxEnd,   s.end_ns);
            }
            qInfo() << "[PW] segs range start_ns=[" << minStart << ".." << maxStart
                    << "] end_ns=[" << minEnd << ".." << maxEnd << "]";
       }

    // Compute day window (local midnight)
    dayStartNs_ = dayStartNs(day);
    dayEndNs_   = dayEndNs(day);

    // Build segment index (detect gaps, normalize)
    segIndex_.build(segs, dayStartNs_, dayEndNs_);
    segIndex_.debugDump("SegIndex");

    // Export to metas for stitching (virtual timeline)
    QVector<QString> paths;
    QVector<qint64>  wallStarts, offsets, durations;
    segIndex_.exportForStitching(paths, wallStarts, offsets, durations);

    QVector<SegmentMeta> metas;
    metas.reserve(paths.size());
    for (int i=0;i<paths.size();++i) {
        metas.push_back({ paths[i],
                          dayStartNs_ + wallStarts[i],
                          offsets[i],
                         durations[i] });
    }
    // Feed stitching engine
    if (stitch_) {
        QMetaObject::invokeMethod(stitch_, "setPlaylist", Qt::QueuedConnection,
                                  Q_ARG(QVector<SegmentMeta>, metas),
                                  Q_ARG(qint64, dayStartNs_));
    }
    qInfo() << "[PW] sideControls=" << sideControls << " enable=" << !metas.isEmpty()
                << " metas=" << metas.size();
        if (sideControls) sideControls->setEnabledControls(!metas.isEmpty());
        // Update trim panel’s notion of the day start and clamp selection to new day
        if (trimPanel) trimPanel->setDayStartNs(dayStartNs_);
                if (trim_.enabled) {
                    const qint64 dayNs = 24LL*3600LL*1000000000LL;
                    trim_.start_ns = qBound<qint64>(0, trim_.start_ns, dayNs - 2'000'000'000LL);
                    trim_.end_ns   = qBound<qint64>(trim_.start_ns + 1, trim_.end_ns, dayNs - 1);
                    if (timelineView) {
                        timelineView->setSelection(trim_.start_ns, trim_.end_ns, true);
                    }
                    if (trimPanel) {
                        trimPanel->setRangeNs(trim_.start_ns, trim_.end_ns);
                    }
                }
}
void PlaybackWindow::runGoFor(const QString& camName, const QDate& day) {
    if (!timelineCtl) return;
    // keep UI in sync
    if (controls) controls->setDate(day);
    currentDay_ = day;
    // directly call the controller’s slot (same as Go)
    QMetaObject::invokeMethod(timelineCtl, "onGo", Qt::QueuedConnection,
                              Q_ARG(QString, camName),
                              Q_ARG(QDate, day));
}

    // helper to compute day window (local midnight)
    static inline qint64 toNs(qint64 s) { return s * 1000000000LL; }
    qint64 PlaybackWindow::dayStartNs(const QDate& d) const {
        const QString s = d.toString("yyyy-MM-dd") + " 00:00:00";
        const auto dt = QDateTime::fromString(s, "yyyy-MM-dd HH:mm:ss").toLocalTime();
        return toNs(dt.toSecsSinceEpoch());
   }


    qint64 PlaybackWindow::dayEndNs(const QDate& d) const {
       return dayStartNs(d.addDays(1));
}

    QString PlaybackWindow::fmtRangeLocal(qint64 ns0, qint64 ns1) const {
        const auto s0 = QDateTime::fromSecsSinceEpoch(ns0/1000000000LL).toLocalTime();
        const auto s1 = QDateTime::fromSecsSinceEpoch(ns1/1000000000LL).toLocalTime();
        return QString("%1 → %2  (%3 s)")
              .arg(s0.toString("HH:mm:ss"))
              .arg(s1.toString("HH:mm:ss"))
              .arg((ns1-ns0)/1000000000LL);
}
    void PlaybackWindow::initPlayer_() {
        // Create a dedicated thread for the player backend
        playerThread_ = new QThread(this);
        player_ = new PlaybackVideoPlayerGst();
        player_->moveToThread(playerThread_);
        connect(playerThread_, &QThread::finished, player_, &QObject::deleteLater);
        playerThread_->start();

        // Bind the sink window handle
        const quintptr wid = videoBox->renderWinId();
        QMetaObject::invokeMethod(player_, "setWindowHandle", Qt::QueuedConnection,
                                  Q_ARG(quintptr, wid));
        // Start timers on the player thread
            QMetaObject::invokeMethod(player_, "startTimers", Qt::QueuedConnection);

       // Optional: basic error log
        connect(player_, &PlaybackVideoPlayerGst::errorText, this,
                [](const QString& e){ qWarning() << "[Player]" << e; });
    }
    void PlaybackWindow::initStitch_() {
        stitchThread_ = new QThread(this);
        stitch_ = new PlaybackStitchingPlayer();
        stitch_->moveToThread(stitchThread_);
        connect(stitchThread_, &QThread::finished, stitch_, &QObject::deleteLater);
        stitchThread_->start();

        // Attach the threaded player
        QMetaObject::invokeMethod(stitch_, "attachPlayer", Qt::QueuedConnection,
                                  Q_ARG(PlaybackVideoPlayerGst*, player_));

        // Stitching → playhead
        connect(stitch_, &PlaybackStitchingPlayer::wallPositionNs, this,
                [this](qint64 wall_offset_ns){
            if (timelineView) {
            timelineView->setPlayheadNs(wall_offset_ns);
            }
                    if (stitch_) updateTrimClamps_();
                }, Qt::QueuedConnection);

        // Side controls → Stitcher (queued across threads, type-safe)
        // Moved here to ensure stitch_ is properly initialized
        connect(sideControls, &PlaybackSideControls::playClicked, this, [this](){
                    qInfo() << "[PW] Play button clicked";
                    if (!stitch_) {
                        qWarning() << "[PW] Stitching player not available";
                        return;
                    }
                    qint64 t = timelineView ? timelineView->playheadNs() : 0;
                    if (trim_.enabled) {
                    // If cursor outside, start from selection start
                    if (t < trim_.start_ns || t >= trim_.end_ns) t = trim_.start_ns;
                    }
                    const qint64 wall = dayStartNs_ + t;
                    QMetaObject::invokeMethod(stitch_, "seekWall", Qt::QueuedConnection, Q_ARG(qint64, wall));
                    QMetaObject::invokeMethod(stitch_, "play", Qt::QueuedConnection);
                });
        connect(sideControls, &PlaybackSideControls::pauseClicked, this,
                [this](){
                    qInfo() << "[PW] Pause button clicked";
                    if (!stitch_) {
                        qWarning() << "[PW] Stitching player not available";
                        return;
                    }
                    QMetaObject::invokeMethod(stitch_, "pause", Qt::QueuedConnection);
                });

        // Optional: see activity in logs
        connect(stitch_, &PlaybackStitchingPlayer::segmentChanged, this,
                [](int i){ qInfo() << "[Stitch] segmentChanged" << i; },
                Qt::QueuedConnection);

        // Handle state changes
        connect(stitch_, &PlaybackStitchingPlayer::stateChanged, this,
                [](bool playing){
                    qInfo() << "[PW] Playback state changed to:" << (playing ? "PLAYING" : "PAUSED");
                    // You could update UI elements here if needed
                }, Qt::QueuedConnection);

        // Optional: log errors
        connect(stitch_, &PlaybackStitchingPlayer::errorText, this,
                [](const QString& e){ qWarning() << "[Stitch]" << e; }, Qt::QueuedConnection);
        // Rewind/Forward ±10s using the timeline playhead (day offset)
        connect(sideControls, &PlaybackSideControls::rewind10Clicked, this, [this](){
            if (!stitch_) return;
            //const qint64 dayNs = 24LL*3600LL*1000000000LL;
            qint64 t = timelineView ? timelineView->playheadNs() - 10LL*1000000000LL : 0;
            t = qMax<qint64>(0, t);
            if (trim_.enabled) t = qMax<qint64>(trim_.start_ns, t);
            const qint64 wall = dayStartNs_ + t;
            QMetaObject::invokeMethod(stitch_, "seekWall", Qt::QueuedConnection, Q_ARG(qint64, wall));
        });
        connect(sideControls, &PlaybackSideControls::forward10Clicked, this, [this](){
            if (!stitch_) return;
            const qint64 dayNs = 24LL*3600LL*1000000000LL;
            qint64 t = timelineView ? timelineView->playheadNs() + 10LL*1000000000LL : 0;
            t = qMin<qint64>(dayNs - 1, t);
            if (trim_.enabled) t = qMin<qint64>(trim_.end_ns - 1, t);
            const qint64 wall = dayStartNs_ + t;
            QMetaObject::invokeMethod(stitch_, "seekWall", Qt::QueuedConnection, Q_ARG(qint64, wall));
        });

        // Speed cycle: 1x → 2x → 4x → 0.5x → (loops)
        connect(sideControls, &PlaybackSideControls::speedCycleClicked, this, [this](){
            if (!stitch_) return;
            static const double rates[] = {1.0, 2.0, 4.0, 0.5};
            static int idx = 0;
            idx = (idx + 1) % int(sizeof(rates)/sizeof(rates[0]));
            const double r = rates[idx];
            QMetaObject::invokeMethod(stitch_, "setRate", Qt::QueuedConnection, Q_ARG(double, r));
            sideControls->setSpeedLabel(QString("Speed %1x").arg(r, 0, 'g', 2));
        });
        connect(sideControls, &PlaybackSideControls::previousDayClicked, this, [this](){
            const QDate base = currentDay_.isValid() ? currentDay_ : QDate::currentDate();
            const QDate prev = base.addDays(-1);
            if (lastCamName_.isEmpty()) {
                qWarning() << "[PW] Prev day clicked but no camera selected yet";
                return;
            }
            runGoFor(lastCamName_, prev);
        });
        connect(sideControls, &PlaybackSideControls::nextDayClicked, this, [this](){
            const QDate base = currentDay_.isValid() ? currentDay_ : QDate::currentDate();
            const QDate next = base.addDays(1);
            if (lastCamName_.isEmpty()) { qWarning() << "[PW] Next day clicked but no camera selected yet"; return; }
            runGoFor(lastCamName_, next);
        });
    }
    // ---------- helpers ----------
    void PlaybackWindow::updateTrimClamps_(){
        if (!trim_.enabled || !timelineView || !stitch_) return;
        const qint64 ph = timelineView->playheadNs();
        if (ph >= trim_.end_ns) {
            QMetaObject::invokeMethod(stitch_, "pause", Qt::QueuedConnection);
            timelineView->setPlayheadNs(trim_.end_ns);
        }
    }


    void PlaybackWindow::applyTextEditsToSelection_(qint64 s, qint64 e){
        if (!trim_.enabled) return;
        trim_.start_ns = s; trim_.end_ns = e;
        if (timelineView) timelineView->setSelection(trim_.start_ns, trim_.end_ns, true);
        if (trimPanel)    trimPanel->setRangeNs(trim_.start_ns, trim_.end_ns);
        if (trimPanel)    trimPanel->setDurationLabel(trim_.end_ns - trim_.start_ns);
    }
