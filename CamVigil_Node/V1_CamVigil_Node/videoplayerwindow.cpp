#include "videoplayerwindow.h"

#include <QFileInfo>
#include <QTime>
#include <QDebug>
#include <QShowEvent>
#include <QResizeEvent>
#include <QEvent>
#include <gst/video/videooverlay.h>

// -------------------- Bus thread --------------------
void GstBusThread::run() {
    if (!pipeline) return;
    GstBus *bus = gst_element_get_bus(pipeline);
    if (!bus) return;

    for (;;) {
        GstMessage *msg = gst_bus_timed_pop(bus, GST_MSECOND * 200);
        if (!msg) {
            if (isInterruptionRequested()) break;
            continue;
        }
        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError *err = nullptr; gchar *dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            emit gstError(QString("GStreamer error: %1 (%2)")
                          .arg(err ? err->message : "unknown")
                          .arg(dbg ? dbg : ""));
            g_clear_error(&err); g_free(dbg);
            break;
        }
        case GST_MESSAGE_EOS:
            emit gstEos();
            break;
        default: break;
        }
        gst_message_unref(msg);
        if (isInterruptionRequested()) break;
    }
    gst_object_unref(bus);
}

// -------------------- helpers --------------------
static inline GstElement* try_make(const char* factory) {
    return gst_element_factory_make(factory, nullptr);
}

static GstElement* find_overlay_in_bin(GstElement *elem) {
    if (!elem) return nullptr;
    if (GST_IS_VIDEO_OVERLAY(elem)) return elem;
    if (GST_IS_BIN(elem)) {
        GList *children = GST_BIN_CHILDREN(GST_BIN(elem));
        for (GList *l = children; l; l = l->next) {
            GstElement *child = GST_ELEMENT(l->data);
            if (!child) continue;
            if (GstElement *ov = find_overlay_in_bin(child)) return ov;
        }
    }
    return nullptr;
}

// -------------------- VideoPlayerWindow --------------------
VideoPlayerWindow::VideoPlayerWindow(const QString& filePath, QWidget *parent)
    : QWidget(parent)
{
    gst_init(nullptr, nullptr);

    // Global minimal grey theme
    setStyleSheet(R"(
      QWidget{ background:#0d0d0d; color:#bbb; font:13px "Inter","Segoe UI","DejaVu Sans"; }
      QLabel{ color:#bbb; }
      QFrame#ControlBar { background:#1e1e1e; border:1px solid #333; border-radius:10px; }
      QPushButton{
        background:#2a2a2a; color:#ddd; border:1px solid #5a5a5a; border-radius:8px;
        padding:8px 14px;
      }
      QPushButton:hover{ background:#3a3a3a; }
      QPushButton:pressed{ background:#222; }
      QPushButton:disabled{ background:#1a1a1a; color:#777; border-color:#3a3a3a; }
      QSlider::groove:horizontal{
        height:6px; background:#1e1e1e; border:1px solid #333; border-radius:4px; margin:0 8px;
      }
      QSlider::sub-page:horizontal{ background:#3a3a3a; border:1px solid #444; border-radius:4px; }
      QSlider::add-page:horizontal{ background:#151515; border:1px solid #222; border-radius:4px; }
      QSlider::handle:horizontal{
        width:14px; height:14px; margin:-5px -7px; border-radius:7px;
        background:#ddd; border:1px solid #666;
      }
      QSlider::handle:horizontal:hover{ background:#fff; }
    )");

    setAttribute(Qt::WA_DeleteOnClose, true);
    resize(960, 600);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12,12,12,12);
    mainLayout->setSpacing(10);

    // Header info
    QFileInfo fi(filePath);
    const QString fileName     = fi.fileName();
    const QString fileDateTime = fi.lastModified().toString("yyyy-MM-dd hh:mm:ss");

    fileInfoLabel = new QLabel(QString("📂 %1    |    📅 %2").arg(fileName, fileDateTime), this);
    fileInfoLabel->setStyleSheet("color:#bbb; font-size:13px; padding:6px; background:#1e1e1e; border:1px solid #333; border-radius:8px;");
    fileInfoLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(fileInfoLabel);

    // Native video area
    videoArea = new QWidget(this);
    videoArea->setStyleSheet("background:#111; border:1px solid #222; border-radius:8px;");
    videoArea->setAttribute(Qt::WA_NativeWindow);
    mainLayout->addWidget(videoArea, /*stretch*/1);

    // Seek slider (full width)
    seekSlider = new QSlider(Qt::Horizontal, this);
    seekSlider->setRange(0, 0);
    seekSlider->setSingleStep(1000);   // 1s
    seekSlider->setPageStep(5000);     // 5s
    seekSlider->setTracking(false);
    connect(seekSlider, &QSlider::sliderPressed,  this, &VideoPlayerWindow::onSeekPressed);
    connect(seekSlider, &QSlider::sliderReleased, this, &VideoPlayerWindow::onSeekReleased);
    // connect(seekSlider, &QSlider::valueChanged, this, &VideoPlayerWindow::onSeekMoved); // enable for live seek
    mainLayout->addWidget(seekSlider);

    // Bottom symmetric control bar
    controlBar = new QFrame(this);
    controlBar->setObjectName("ControlBar");
    auto *barLayout = new QHBoxLayout(controlBar);
    barLayout->setContentsMargins(12,8,12,8);
    barLayout->setSpacing(10);

    // Left: play/pause
    playPauseButton = new QPushButton("⏸ Pause", controlBar);
    playPauseButton->setMinimumWidth(110);
    connect(playPauseButton, &QPushButton::clicked, this, &VideoPlayerWindow::playPauseVideo);
    barLayout->addWidget(playPauseButton, 0, Qt::AlignLeft);

    // Center: time label
    timeLabel = new QLabel("00:00 / 00:00", controlBar);
    timeLabel->setAlignment(Qt::AlignCenter);
    barLayout->addWidget(timeLabel, 1); // stretch to center

    // Right: close
    closeButton = new QPushButton("Close", controlBar);
    connect(closeButton, &QPushButton::clicked, this, &VideoPlayerWindow::close);
    barLayout->addWidget(closeButton, 0, Qt::AlignRight);

    mainLayout->addWidget(controlBar);

    setLayout(mainLayout);

    // Polling timer
    updateTimer = new QTimer(this);
    updateTimer->setTimerType(Qt::CoarseTimer);
    updateTimer->setInterval(200);
    connect(updateTimer, &QTimer::timeout, this, &VideoPlayerWindow::updateElapsedTime);

    // Build and start pipeline
    initPipeline(filePath);

    // Bus thread
    busThread = new GstBusThread(this);
    busThread->setPipeline(pipeline);
    connect(busThread, &GstBusThread::gstError, this, &VideoPlayerWindow::onGstError);
    connect(busThread, &GstBusThread::gstEos,   this, &VideoPlayerWindow::onGstEos);
    busThread->start();
}

void VideoPlayerWindow::initPipeline(const QString &filePath) {
    GstElement *filesrc = try_make("filesrc");
    g_object_set(filesrc, "location", filePath.toUtf8().constData(), NULL);

    GstElement *demux   = try_make("matroskademux");
    GstElement *parser  = try_make("h264parse");

    GstElement *decoder = try_make("vaapih264dec");
    const bool usingVaapi = (decoder != nullptr);
    if (!decoder) decoder = try_make("avdec_h264");

    GstElement *q1 = try_make("queue");
    GstElement *q2 = try_make("queue");
    GstElement *vconv = try_make("videoconvert");
    GstElement *sink  = try_make("ximagesink");

    if (!filesrc || !demux || !parser || !decoder || !vconv || !sink) {
        qWarning() << "Failed to create required GStreamer elements";
        return;
    }

    if (g_object_class_find_property(G_OBJECT_GET_CLASS(sink), "force-aspect-ratio"))
        g_object_set(sink, "force-aspect-ratio", TRUE, NULL);
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(sink), "sync"))
        g_object_set(sink, "sync", TRUE, NULL);

    pipeline = gst_pipeline_new("archive-player");
    gst_bin_add_many(GST_BIN(pipeline), filesrc, demux, parser, NULL);
    if (q1) gst_bin_add(GST_BIN(pipeline), q1);
    gst_bin_add(GST_BIN(pipeline), decoder);
    if (q2) gst_bin_add(GST_BIN(pipeline), q2);
    gst_bin_add_many(GST_BIN(pipeline), vconv, sink, NULL);

    // Static links
    gst_element_link(filesrc, demux);
    if (q1) gst_element_link_many(parser, q1, decoder, NULL);
    else    gst_element_link_many(parser, decoder, NULL);
    if (q2) gst_element_link_many(decoder, q2, vconv, sink, NULL);
    else    gst_element_link_many(decoder, vconv, sink, NULL);

    // Demux dynamic pad (video only)
    g_signal_connect(demux, "pad-added",
        G_CALLBACK(+[] (GstElement*, GstPad *pad, gpointer data){
            auto *parser = static_cast<GstElement*>(data);
            GstCaps *caps = gst_pad_query_caps(pad, nullptr);
            bool isVideo = false;
            if (caps) {
                const gchar *name = gst_structure_get_name(gst_caps_get_structure(caps, 0));
                if (name && g_str_has_prefix(name, "video/")) isVideo = true;
                gst_caps_unref(caps);
            }
            if (!isVideo) return;
            GstPad *sinkPad = gst_element_get_static_pad(parser, "sink");
            if (!gst_pad_is_linked(sinkPad)) gst_pad_link(pad, sinkPad);
            gst_object_unref(sinkPad);
        }), parser);

    installBusSyncHandler();

    // Preroll to get caps
    gst_element_set_state(pipeline, GST_STATE_PAUSED);
    const GstStateChangeReturn preroll =
        gst_element_get_state(pipeline, nullptr, nullptr, GST_SECOND * 3);
    qDebug() << "[Player] Preroll state:" << preroll
             << "Decoder:" << (usingVaapi ? "vaapih264dec" : "avdec_h264");

    videoSink = find_overlay_in_bin(sink);

    bindOverlay();
    applyRenderRect();

    // Play
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    isPlaying = true;
    playPauseButton->setText("⏸ Pause");

    queryDuration();
    updateTimer->start();
}

// -------------------- Bus sync handler --------------------
GstBusSyncReply VideoPlayerWindow::onBusSync(GstBus * /*bus*/, GstMessage *msg, gpointer user_data) {
    if (GST_MESSAGE_TYPE(msg) != GST_MESSAGE_ELEMENT) return GST_BUS_PASS;
    if (!gst_is_video_overlay_prepare_window_handle_message(msg)) return GST_BUS_PASS;

    auto *self = static_cast<VideoPlayerWindow*>(user_data);
    if (!self || !self->videoSink || !self->videoArea) return GST_BUS_PASS;

    (void)self->videoArea->winId(); // ensure handle

    gst_video_overlay_set_window_handle(
        GST_VIDEO_OVERLAY(self->videoSink),
        (guintptr)self->videoArea->winId()
    );
    self->applyRenderRect();

    return GST_BUS_DROP;
}

void VideoPlayerWindow::installBusSyncHandler() {
    if (!pipeline) return;
    GstBus *bus = gst_element_get_bus(pipeline);
    if (bus) {
        gst_bus_set_sync_handler(bus, (GstBusSyncHandler)&VideoPlayerWindow::onBusSync, this, nullptr);
        gst_object_unref(bus);
    }
}

// -------------------- Overlay binding & sizing --------------------
void VideoPlayerWindow::bindOverlay() {
    if (!videoSink || !videoArea) return;
    (void)videoArea->winId();
    if (GST_IS_VIDEO_OVERLAY(videoSink)) {
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(videoSink),
                                            (guintptr)videoArea->winId());
        gst_video_overlay_expose(GST_VIDEO_OVERLAY(videoSink));
    }
}

void VideoPlayerWindow::applyRenderRect() {
    if (!videoSink || !videoArea) return;
    if (!GST_IS_VIDEO_OVERLAY(videoSink)) return;
    const int x = 0, y = 0, w = videoArea->width(), h = videoArea->height();
    gst_video_overlay_set_render_rectangle(GST_VIDEO_OVERLAY(videoSink), x, y, w, h);
    gst_video_overlay_expose(GST_VIDEO_OVERLAY(videoSink));
}

bool VideoPlayerWindow::eventFilter(QObject *obj, QEvent *ev) {
    if (obj == videoArea && (ev->type() == QEvent::Resize || ev->type() == QEvent::Show)) {
        applyRenderRect();
    }
    return QWidget::eventFilter(obj, ev);
}

void VideoPlayerWindow::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    if (videoArea) videoArea->installEventFilter(this);
    bindOverlay();
    applyRenderRect();
}

void VideoPlayerWindow::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    applyRenderRect();
}

// -------------------- Media controls & housekeeping --------------------
void VideoPlayerWindow::queryDuration() {
    if (!pipeline) return;
    gint64 durNs = 0;
    if (gst_element_query_duration(pipeline, GST_FORMAT_TIME, &durNs)) {
        durationMs = durNs / GST_MSECOND;
        const QString total = QTime(0,0).addMSecs(durationMs).toString("mm:ss");
        timeLabel->setText(QString("00:00 / %1").arg(total));
        if (seekSlider) seekSlider->setRange(0, int(durationMs));
    }
}

void VideoPlayerWindow::playPauseVideo() {
    if (!pipeline) return;
    gst_element_set_state(pipeline, isPlaying ? GST_STATE_PAUSED : GST_STATE_PLAYING);
    isPlaying = !isPlaying;
    playPauseButton->setText(isPlaying ? "⏸ Pause" : "▶ Play");
}

bool VideoPlayerWindow::seekToMs(qint64 ms) {
    if (!pipeline) return false;
    ms = clampMs(ms);
    const qint64 ns = ms * GST_MSECOND;
    return gst_element_seek(
        pipeline, 1.0, GST_FORMAT_TIME,
        (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_SNAP_NEAREST),
        GST_SEEK_TYPE_SET, ns,
        GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
}

void VideoPlayerWindow::updateElapsedTime() {
    if (!pipeline) return;
    gint64 posNs = 0, durNs = 0;
    if (gst_element_query_position(pipeline, GST_FORMAT_TIME, &posNs)) {
        const qint64 posMs = posNs / GST_MSECOND;
        if (seekSlider && !draggingSeek) seekSlider->setValue(int(posMs));
        const QString cur = QTime(0,0).addMSecs(posMs).toString("mm:ss");
        const QString tot = QTime(0,0).addMSecs(durationMs).toString("mm:ss");
        timeLabel->setText(QString("%1 / %2").arg(cur, tot));
    }
    // late duration update
    if (gst_element_query_duration(pipeline, GST_FORMAT_TIME, &durNs)) {
        const qint64 dMs = durNs / GST_MSECOND;
        if (dMs > 0 && dMs != durationMs) {
            durationMs = dMs;
            if (seekSlider) seekSlider->setRange(0, int(durationMs));
        }
    }
}

void VideoPlayerWindow::onGstError(QString msg) {
    qWarning() << msg;
}

void VideoPlayerWindow::onGstEos() {
    isPlaying = false;
    playPauseButton->setText("▶ Play");
    if (updateTimer) updateTimer->stop();
}

void VideoPlayerWindow::onSeekPressed() {
    draggingSeek = true;
}

void VideoPlayerWindow::onSeekReleased() {
    draggingSeek = false;
    if (!seekSlider) return;
    seekToMs(seekSlider->value());
}

void VideoPlayerWindow::onSeekMoved(int v) {
    if (!draggingSeek) return;
    seekToMs(v); // enable for live preview while dragging
}

void VideoPlayerWindow::keyPressEvent(QKeyEvent *e) {
    switch (e->key()) {
    case Qt::Key_Space:  playPauseVideo(); return;
    case Qt::Key_Left:   seekToMs((seekSlider ? seekSlider->value() : 0) - 5000); return;
    case Qt::Key_Right:  seekToMs((seekSlider ? seekSlider->value() : 0) + 5000); return;
    case Qt::Key_Escape: close(); return;
    default: break;
    }
    QWidget::keyPressEvent(e);
}

VideoPlayerWindow::~VideoPlayerWindow() {
    if (updateTimer) updateTimer->stop();
    if (busThread) {
        busThread->requestInterruption();
        busThread->wait(300);
    }
    cleanupPipeline();
}

void VideoPlayerWindow::cleanupPipeline() {
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline = nullptr;
        videoSink = nullptr;
    }
}
