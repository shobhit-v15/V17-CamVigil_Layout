#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtGlobal>
#include <gst/gst.h>

class QTimer;

/**
 * playback_video_player_gst
 * -------------------------
 * Thin GStreamer file player that renders into a native window (winId).
 * - call setWindowHandle(renderWinId) once you have a video host widget
 * - open(path) → preroll (PAUSED)
 * - play(), pause(), stop()
 * - seekNs(t), setRate(r)
 */
class PlaybackVideoPlayerGst : public QObject {
    Q_OBJECT
public:
    explicit PlaybackVideoPlayerGst(QObject* parent=nullptr);
    ~PlaybackVideoPlayerGst();




signals:
    void eos();
    void errorText(QString);
    void positionNs(qint64);
    void durationNs(qint64);

public slots:                         // make invokable across threads
    void setWindowHandle(quintptr wid);
    bool open(const QString& path);
    void play();
    void pause();
    void stop();
    bool seekNs(qint64 t_ns);
    bool setRate(double r);
    void startTimers();               // ensure timer runs on this thread
    void teardown();

private:
    void bindOverlay();
    static gboolean bus_cb(GstBus*, GstMessage*, gpointer);

    GstElement *queue_demux = nullptr;
    GstElement *queue_post  = nullptr;
    QTimer*     posTimer      = nullptr;
    GstElement* pipeline      = nullptr;
    GstElement* filesrc       = nullptr;
    GstElement* demux         = nullptr;
    GstElement* parser        = nullptr;
    GstElement* decoder       = nullptr;
    GstElement* vconv         = nullptr;
    GstElement* videosink     = nullptr;
    quintptr    winHandle     = 0;
    double      rate_         = 1.0;
    QTimer* busTimer = nullptr;
    GstBus* bus = nullptr;
};
Q_DECLARE_METATYPE(PlaybackVideoPlayerGst*)
