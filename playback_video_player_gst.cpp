#include "playback_video_player_gst.h"
#include <QTimer>
#include <QDebug>
#include <gst/video/videooverlay.h>

static inline GstElement* mk(const char* f){ return gst_element_factory_make(f,nullptr); }

// Ensure gst_init runs once per process.
static void ensure_gst_init() {
    static bool inited = false;
    if (!inited) { gst_init(nullptr, nullptr); inited = true; }
}

PlaybackVideoPlayerGst::PlaybackVideoPlayerGst(QObject* parent)
    : QObject(parent)
{
    ensure_gst_init();
    posTimer = nullptr;
    busTimer = nullptr;
    bus = nullptr;
}

PlaybackVideoPlayerGst::~PlaybackVideoPlayerGst() {
    teardown();
    if (posTimer) posTimer->stop();
    if (busTimer) busTimer->stop();
}

void PlaybackVideoPlayerGst::setWindowHandle(quintptr wid) {
    winHandle = wid;
    bindOverlay();
}

bool PlaybackVideoPlayerGst::open(const QString& path) {
    qInfo() << "[Player] Opening file:" << path;

    // First-time pipeline build
    if (!pipeline) {
        pipeline = gst_pipeline_new("playback-player");
        filesrc  = mk("filesrc");

        // Pick a demuxer (fallback to decodebin if unknown)
        const QString p = path.toLower();
        if      (p.endsWith(".mkv") || p.endsWith(".webm")) demux = mk("matroskademux");
        else if (p.endsWith(".mp4") || p.endsWith(".mov") || p.endsWith(".m4v")) demux = mk("qtdemux");
        else demux = mk("decodebin");

        // Buffers around decode to smooth playback during seeks
        // filesrc ! demux ! queue_demux ! h264parse ! avdec_h264 ! queue_post ! videoconvert ! sink
        queue_demux = mk("queue");
        parser      = mk("h264parse");
        decoder     = mk("vaapih264dec");
        queue_post  = mk("queue");
        vconv       = mk("videoconvert");
        videosink   = mk("glimagesink");
        if (!videosink) videosink = mk("ximagesink");
        if (!videosink) videosink = mk("autovideosink");

        if (!pipeline || !filesrc || !demux || !queue_demux || !parser || !decoder || !queue_post || !vconv || !videosink) {
            emit errorText("Failed to create GStreamer elements");
            teardown(); return false;
        }

        // Reasonable queue sizing
        g_object_set(queue_demux, "max-size-buffers", 0, "max-size-bytes", 0, "max-size-time", 2*GST_SECOND, nullptr);
        g_object_set(queue_post,  "max-size-buffers", 0, "max-size-bytes", 0, "max-size-time", 2*GST_SECOND, nullptr);

        if (g_object_class_find_property(G_OBJECT_GET_CLASS(videosink), "force-aspect-ratio"))
            g_object_set(videosink, "force-aspect-ratio", TRUE, nullptr);
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(videosink), "sync"))
            g_object_set(videosink, "sync", TRUE, nullptr);

        gst_bin_add_many(GST_BIN(pipeline),
                         filesrc, demux, queue_demux, parser, decoder, queue_post, vconv, videosink, nullptr);

        // Static links (everything except demux src → queue_demux sink which is dynamic)
        if (!gst_element_link_many(queue_demux, parser, decoder, queue_post, vconv, videosink, nullptr)) {
            emit errorText("Link failed: queue_demux→parser→decoder→queue_post→vconv→sink");
            teardown(); return false;
        }
        if (!gst_element_link(filesrc, demux)) {
            emit errorText("Link failed: filesrc→demux");
            teardown(); return false;
        }

        // Connect demux pad-added once
        g_signal_connect(demux, "pad-added",
            G_CALLBACK(+[](GstElement*, GstPad* pad, gpointer user){
                auto self = static_cast<PlaybackVideoPlayerGst*>(user);
                GstCaps* caps = gst_pad_query_caps(pad, nullptr);
                bool isVideo = false;
                if (caps) {
                    const gchar* n = gst_structure_get_name(gst_caps_get_structure(caps, 0));
                    if (n && g_str_has_prefix(n, "video/")) isVideo = true;
                    gst_caps_unref(caps);
                }
                if (!isVideo) return;
                GstPad* sinkPad = gst_element_get_static_pad(self->queue_demux, "sink");
                if (!gst_pad_is_linked(sinkPad)) gst_pad_link(pad, sinkPad);
                gst_object_unref(sinkPad);
            }), this);

        // Bus polling on the Qt thread (no GLib loop)
        bus = gst_element_get_bus(pipeline);
        if (!busTimer) {
            busTimer = new QTimer(this);
            connect(busTimer, &QTimer::timeout, this, [this]{
                if (!pipeline || !bus) return;
                while (GstMessage* msg = gst_bus_pop_filtered(
                           bus, (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) {
                    switch (GST_MESSAGE_TYPE(msg)) {
                    case GST_MESSAGE_ERROR: {
                        GError* err=nullptr; gchar* dbg=nullptr;
                        gst_message_parse_error(msg, &err, &dbg);
                        emit errorText(QString::fromUtf8(err ? err->message : "GStreamer error"));
                        if (dbg) qWarning("GST DEBUG: %s", dbg);
                        g_clear_error(&err); g_free(dbg);
                        break;
                    }
                    case GST_MESSAGE_EOS:
                        emit eos();
                        break;
                    default: break;
                    }
                    gst_message_unref(msg);
                }
            });
        }
        if (!busTimer->isActive()) busTimer->start(50);

        // Bind overlay once
        bindOverlay();

        // Start timers once
        startTimers();
    }

    // Reuse pipeline: go READY, swap location, preroll to PAUSED
    gst_element_set_state(pipeline, GST_STATE_READY);
    g_object_set(filesrc, "location", path.toUtf8().constData(), nullptr);
    gst_element_set_state(pipeline, GST_STATE_PAUSED);
    if (gst_element_get_state(pipeline, nullptr, nullptr, 3*GST_SECOND) != GST_STATE_CHANGE_SUCCESS) {
        emit errorText("Preroll failed");
        teardown(); return false;
    }

    // Apply current rate at preroll position
    setRate(rate_);

    // Emit duration
    gint64 dur=0;
    if (gst_element_query_duration(pipeline, GST_FORMAT_TIME, &dur))
        emit durationNs(dur);

    return true;
}

void PlaybackVideoPlayerGst::play()  {
    if (pipeline) {
        qInfo() << "[Player] Starting playback";
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
    } else {
        qWarning() << "[Player] Cannot play: pipeline is null";
    }
}
void PlaybackVideoPlayerGst::pause() {
    if (pipeline) {
        qInfo() << "[Player] Pausing playback";
        gst_element_set_state(pipeline, GST_STATE_PAUSED);
    } else {
        qWarning() << "[Player] Cannot pause: pipeline is null";
    }
}
void PlaybackVideoPlayerGst::stop()  {
    if (pipeline) {
        qInfo() << "[Player] Stopping playback";
        gst_element_set_state(pipeline, GST_STATE_NULL);
    } else {
        qWarning() << "[Player] Cannot stop: pipeline is null";
    }
}

bool PlaybackVideoPlayerGst::seekNs(qint64 t_ns) {
    if (!pipeline) return false;
    // Interactive seeks: fast, keyframe-based, flushing
    gboolean ok = gst_element_seek(pipeline, rate_, GST_FORMAT_TIME,
        (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_SNAP_NEAREST),
        GST_SEEK_TYPE_SET, t_ns,
        GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
    // Do NOT auto-play here; the caller controls play/pause
    return ok;
}

bool PlaybackVideoPlayerGst::setRate(double r) {
    if (r == 0.0) r = 1.0;
    rate_ = r;
    if (!pipeline) return true;
    gint64 pos=0; gst_element_query_position(pipeline, GST_FORMAT_TIME, &pos);
    return gst_element_seek(pipeline, rate_, GST_FORMAT_TIME,
        (GstSeekFlags)(GST_SEEK_FLAG_FLUSH /* no TRICKMODE, smoother */),
        GST_SEEK_TYPE_SET, pos,
        GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
}

void PlaybackVideoPlayerGst::bindOverlay() {
    if (videosink && winHandle && GST_IS_VIDEO_OVERLAY(videosink)) {
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(videosink), (guintptr)winHandle);
        // no forced expose on every switch (prevents flashes)
    }
}

void PlaybackVideoPlayerGst::teardown() {
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline = nullptr;
    }
    if (busTimer) busTimer->stop();
    if (bus) { gst_object_unref(bus); bus = nullptr; }
    filesrc = demux = parser = decoder = queue_demux = queue_post = vconv = videosink = nullptr;
    if (posTimer && posTimer->isActive()) posTimer->stop();
    stop();
}

gboolean PlaybackVideoPlayerGst::bus_cb(GstBus*, GstMessage* msg, gpointer s) {
    auto self = static_cast<PlaybackVideoPlayerGst*>(s);
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
        GError* err=nullptr; gchar* dbg=nullptr;
        gst_message_parse_error(msg, &err, &dbg);
        if (self) self->errorText(QString::fromUtf8(err ? err->message : "GStreamer error"));
        if (dbg) qWarning("GST DEBUG: %s", dbg);
        g_clear_error(&err); g_free(dbg);
        break;
    }
    case GST_MESSAGE_EOS:
        if (self) emit self->eos();
        break;
    default: break;
    }
    return TRUE;
}

// Start polling timers on the player’s own thread
void PlaybackVideoPlayerGst::startTimers() {
    if (!posTimer) {
        posTimer = new QTimer(this);
        connect(posTimer, &QTimer::timeout, this, [this]{
            if (!pipeline) return;
            gint64 pos=0, dur=0;
            if (gst_element_query_position(pipeline, GST_FORMAT_TIME, &pos))
                emit positionNs(pos);
            if (gst_element_query_duration(pipeline, GST_FORMAT_TIME, &dur))
                emit durationNs(dur);
        });
    }
    if (!posTimer->isActive())
        posTimer->start(200);
}
