#pragma once
#include <QWidget>
#include <QMap>
#include "db_reader.h"
#include "playback_controls.h"
#include "playback_timeline_controller.h"
#include "playback_segment_index.h"
#include "playback_trim_panel.h"

class PlaybackTimelineView;
class PlaybackTimelineModel;
class QCloseEvent;
class PlaybackVideoBox;
class PlaybackTitleBar;
class PlaybackSideControls;
class PlaybackVideoPlayerGst;
class QThread;
class PlaybackStitchingPlayer;
class PlaybackExporter;

class PlaybackWindow : public QWidget {
    Q_OBJECT
public:
    explicit PlaybackWindow(QWidget* parent=nullptr);
    ~PlaybackWindow();

    // Call this with ".../CamVigilArchives/camvigil.sqlite"
    void openDb(const QString& dbPath);
    void setCameraList(const QStringList& names);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    // --- UI ---
    PlaybackTitleBar*       titleBar{nullptr};
    PlaybackControlsWidget* controls{};      // hosted inside titleBar
    PlaybackVideoBox*       videoBox{nullptr};
    PlaybackSideControls*   sideControls{nullptr};
    PlaybackTimelineView*   timelineView{nullptr};
    PlaybackTrimPanel*      trimPanel{nullptr};

    // --- Database ---
    DbReader* db{nullptr};
    QVector<int> camIds;           // index-aligned with names we show
    QMap<QString,int> nameToId;    // name → camera_id
    int selectedCamId = -1;

    // --- Timeline ---
    PlaybackTimelineController* timelineCtl{nullptr};
    qint64 dayStartNs(const QDate&) const;
    qint64 dayEndNs(const QDate&) const;
    QString fmtRangeLocal(qint64 ns0, qint64 ns1) const;
    static QString tid();

    // --- Video player ---
    void initPlayer_();
    void stopPlayer_();
    PlaybackVideoPlayerGst* player_{nullptr};
    QThread*                playerThread_{nullptr};

    // --- Stitching engine ---
    void initStitch_();
    void stopStitch_();
    PlaybackStitchingPlayer* stitch_{nullptr};
    QThread*                 stitchThread_{nullptr};

    // --- Day/segment state ---
    qint64 dayStartNs_{0}, dayEndNs_{0};
    PlaybackSegmentIndex segIndex_;
    QDate currentDay_;
    QString lastCamName_;
    void runGoFor(const QString& camName, const QDate& day);

    // --- Trim/Export UI state ---
    struct TrimRange { bool enabled=false; qint64 start_ns=0; qint64 end_ns=0; };
    TrimRange trim_;
    void updateTrimClamps_();
    void applyTextEditsToSelection_(qint64 s, qint64 e);

    // --- Clip + Save export flow ---
    void startClip_();             // stage 1: run exporter into tmp
    void finalizeSave_();          // stage 2: move tmp clip to final
    void cleanupExportThread_();   // helper

    QThread*          exportThread_{nullptr};
    PlaybackExporter* exporter_{nullptr};
    QString           tmpClipPath_; // holds stage-1 result path

private slots:
    void onCamerasReady(const CamList& cams);
    void onDaysReady(int cameraId, const QStringList& ymdList);
    void onSegmentsReady(int cameraId, const SegmentList& segs);
    void onUiCameraChanged(const QString& camName);
    void onUiDateChanged(const QDate& date);
};
