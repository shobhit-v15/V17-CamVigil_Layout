#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QPointer>
#include <QHash>
#include <vector>
#include <memory>

#include "layoutmanager.h"
#include "streammanager.h"
#include "navbar.h"
#include "toolbar.h"
#include "settingswindow.h"
#include "archivemanager.h"
#include "clickablelabel.h"
#include "fullscreenviewer.h"
#include "cameramanager.h"
#include "cameragridstate.h"
#include "group_repository.h"

class QThread;
class NodeServicesBootstrap;

class PlaybackWindow;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

struct CameraGroupRuntime {
    int id = -1;                      // DB group id
    QString name;                     // group name
    std::vector<int> cameraIndexes;   // indices into main camera/profiles list
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void openSettingsWindow();
    void openPlaybackWindow();
    void showFullScreenFeed(int index);

    void startStreamingAsync();

    // From Toolbar (fixed 3×3 design)
    void nextPage();
    void previousPage();

    void onGroupChanged(int index);
    void onLayoutModeChanged(bool isDefault);

private:
    Ui::MainWindow *ui;
    QGridLayout* gridLayout;
    LayoutManager* layoutManager;
    StreamManager* streamManager;
    ArchiveManager* archiveManager;
    std::vector<ClickableLabel*> labels;   // labels[globalCameraIndex]

    int gridRows;
    int gridCols;
    int currentFullScreenIndex;

    CameraManager* cameraManager;

    Navbar* topNavbar;
    Toolbar* toolbar;
    SettingsWindow* settingsWindow;
    FullScreenViewer* fullScreenViewer;

    QVector<ClickableLabel*> streamDisplayLabels;
    bool streamsStarted = false;

    QTimer* timeSyncTimer = nullptr;
    QPointer<PlaybackWindow> playbackWindow;

    // Fixed 3×3 layout + paging logic
    CameraGridState gridState;             // operates on visible indexes
    std::vector<int> visibleOrder;         // visibleOrder[visibleIndex] = globalCameraIndex

    // 9 reusable placeholder widgets for empty slots
    std::vector<QWidget*> emptySlots;

    // Grouping state (Phase 1)
    std::unique_ptr<GroupRepository> m_groupRepo;
    std::vector<CameraGroupRuntime> m_groups;
    int m_currentGroupIndex = -1;          // index into m_groups
    QHash<int, int> m_cameraIdToIndex;     // camera_id (DB) -> camera index (0..N-1)

    // Helpers
    void initEmptySlots();
    void rebuildVisibleOrder();            // early hook for grouping/filtering (Option C)
    void syncGridStateWithVisibleOrder();
    void updateToolbarPageInfo();
    void refreshGrid();
    void refreshGridDefault();
    void refreshGridCustom();

    void initGroupRepository();
    void initGroupsAfterCamerasLoaded();
    void reloadGroupsFromDb();
    void applyCurrentGroupToGrid();

    bool m_isCustomLayout;
    int m_primaryCameraIndex;

    // Node API PoC
    NodeServicesBootstrap* m_nodeServices = nullptr;
};

#endif // MAINWINDOW_H
