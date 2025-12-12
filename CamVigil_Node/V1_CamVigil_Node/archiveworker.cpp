#include "archiveworker.h"
#include <QDir>
#include <QDebug>
#include <QThread>
#include <QMutexLocker>
#include <gst/gst.h>

ArchiveWorker::ArchiveWorker(const std::string& url,
                             int camIndex,
                             const QString& archDir,
                             int defaultDur,
                             const QDateTime& mStart)
    : cameraUrl(url),
      cameraIndex(camIndex),
      archiveDir(archDir),
      running(true),
      segmentDurationSec(defaultDur),
      pendingDurationUpdate(false),
      nextSegmentDuration(defaultDur),
      masterStart(mStart),
      pipeline(nullptr)
{
    qDebug() << "[ArchiveWorker] Created for cam" << cameraIndex
             << "with masterStart:" << masterStart.toString("yyyyMMdd_HHmmss");
}

QString ArchiveWorker::generateSegmentPrefix() const {
    QString timestamp = masterStart.toString("yyyyMMdd_HHmmss");
    return QString("archive_cam%1_%2").arg(cameraIndex).arg(timestamp);
}

void ArchiveWorker::createPipeline() {
    gst_init(nullptr, nullptr);
    qint64 maxSizeTimeNs = static_cast<qint64>(segmentDurationSec.load()) * 1000000000LL;

    // 1) Create pipeline and elements
    pipeline = gst_pipeline_new(nullptr);
    GstElement* src    = gst_element_factory_make("rtspsrc",      "source");
    GstElement* depay  = gst_element_factory_make("rtph264depay", "depay");
    GstElement* parse  = gst_element_factory_make("h264parse",    "parse");
    GstElement* split  = gst_element_factory_make("splitmuxsink","split");

    if (!pipeline || !src || !depay || !parse || !split) {
        emit recordingError("Failed to create one or more GStreamer elements");
        return;
    }

    // 2) Configure elements
    g_object_set(src,
                 "location", cameraUrl.c_str(),
                 "latency", 300,
                 nullptr);

    g_object_set(split,
                 "name",            "split",
                 "send-keyframe-requests", TRUE,
                 "max-size-time",     maxSizeTimeNs,
                 "async-finalize",    TRUE,
                 "muxer-factory",    "matroskamux",
                 nullptr);

    // 3) Add to pipeline
    gst_bin_add_many(GST_BIN(pipeline), src, depay, parse, split, nullptr);

    // 4) Link static pads
    if (!gst_element_link(depay, parse) ||
        !gst_element_link(parse, split)) {
        emit recordingError("Failed to link depay → parse → splitmuxsink");
        gst_object_unref(pipeline);
        pipeline = nullptr;
        return;
    }

    // 5) Handle dynamic pad from rtspsrc → depay
    g_signal_connect(src, "pad-added",
                     G_CALLBACK(+[](GstElement* src, GstPad* pad, gpointer user_data){
        GstElement* depay = static_cast<GstElement*>(user_data);
        GstPad* sinkpad = gst_element_get_static_pad(depay, "sink");
        if (gst_pad_is_linked(sinkpad) == FALSE) {
            gst_pad_link(pad, sinkpad);
        }
        gst_object_unref(sinkpad);
    }), depay);

    // 6) Bus watch
    GstBus* bus = gst_element_get_bus(pipeline);
    gst_bus_add_signal_watch(bus);
    g_signal_connect(bus, "message",
                     G_CALLBACK(ArchiveWorker::onBusMessage), this);
    gst_object_unref(bus);

    // 7) Connect the format-location-full signal on our splitmuxsink
    g_signal_connect(split,
                     "format-location-full",
                     G_CALLBACK(ArchiveWorker::formatLocationFullCallback),
                     this);
    qDebug() << "[ArchiveWorker] Connected format-location-full on splitmuxsink for cam"
             << cameraIndex;

    qDebug() << "[ArchiveWorker] Pipeline created successfully for cam"
             << cameraIndex;
}



void ArchiveWorker::cleanupPipeline() {
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline = nullptr;
    }
}

void ArchiveWorker::run() {
    createPipeline();
    if (!pipeline) {
        qDebug() << "[ArchiveWorker] Pipeline creation failed for cam" << cameraIndex << ". Exiting.";
        return;
    }


    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        emit recordingError("Failed to set GStreamer pipeline to PLAYING");
        cleanupPipeline();
        return;
    }

    qDebug() << "[ArchiveWorker] Pipeline running for cam" << cameraIndex;
    running.store(true);

    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    while (running.load()) {
        if (!g_main_context_iteration(g_main_loop_get_context(loop), FALSE)) {
            QThread::msleep(100); // Fallback if no events
        }
    }
    g_main_loop_unref(loop);
    cleanupPipeline();
    qDebug() << "[ArchiveWorker] Pipeline stopped for cam" << cameraIndex;
}

void ArchiveWorker::stop() {
    running.store(false);
    if (pipeline) {
        gst_element_send_event(pipeline, gst_event_new_eos()); // Ensures the  final segment is written
    }
    qDebug() << "[ArchiveWorker] Stop called for cam" << cameraIndex;
}

void ArchiveWorker::updateSegmentDuration(int seconds) {
    qDebug() << "[ArchiveWorker] Segment duration update scheduled for cam" << cameraIndex << "to" << seconds << "seconds.";
    nextSegmentDuration = seconds;
    pendingDurationUpdate.store(true);
    if (pipeline) {
        GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "split");
        if (sink) {
            qDebug() << "[ArchiveWorker] Emitting split-now for cam" << cameraIndex;
            g_signal_emit_by_name(sink, "split-now", NULL);
            gst_object_unref(sink);
        } else {
            qDebug() << "[ArchiveWorker] Failed to get splitmuxsink for split-now on cam" << cameraIndex;
        }
    }
}

gchar* ArchiveWorker::formatLocationFullCallback(GstElement* splitmux, guint fragment_id, GstSample* sample, gpointer user_data) {
    Q_UNUSED(splitmux);
    Q_UNUSED(fragment_id);
    ArchiveWorker* worker = static_cast<ArchiveWorker*>(user_data);

    QDateTime segmentStartTime;
    if (sample) {
        GstBuffer* buffer = gst_sample_get_buffer(sample);
        if (buffer && GST_BUFFER_PTS_IS_VALID(buffer)) {
            GstClockTime pts = GST_BUFFER_PTS(buffer);
            qint64 ptsMs = pts / 1000000;
            segmentStartTime = worker->masterStart.addMSecs(ptsMs);
            qDebug() << "[ArchiveWorker] PTS for cam" << worker->cameraIndex << ":" << pts << "ns (" << ptsMs << "ms)";
        }
    }

    if (!segmentStartTime.isValid()) {
        segmentStartTime = QDateTime::currentDateTime();
        qDebug() << "[ArchiveWorker] No valid PTS for cam" << worker->cameraIndex << ", using system time";
    }

    if (worker->lastSegmentTimestamp.isValid()) {
        qint64 diff = worker->lastSegmentTimestamp.msecsTo(segmentStartTime);
        qDebug() << "[ArchiveWorker] Time since last segment for cam" << worker->cameraIndex
                 << ":" << diff << "ms (expected:" << worker->segmentDurationSec.load()*1000 << "ms)";
    }
    worker->lastSegmentTimestamp = segmentStartTime;

    QString timestamp = segmentStartTime.toString("yyyyMMdd_HHmmss");
    QString filename = QString("%1/archive_cam%2_%3.mkv")
                           .arg(worker->archiveDir)
                           .arg(worker->cameraIndex)
                           .arg(timestamp);
    qDebug() << "[ArchiveWorker] New segment:" << filename;

    // --- DB notifications: close previous, open new ---
       const qint64 startNs = segmentStartTime.toUTC().toMSecsSinceEpoch() * 1000000LL;
       {
           QMutexLocker lk(&worker->curMutex);
           // finalize previous file if present
           if (!worker->currentFilePath.isEmpty() && worker->currentStartTimeUtc.isValid()) {
               const qint64 endMs = worker->currentStartTimeUtc.msecsTo(segmentStartTime);
               const qint64 endNs =
                   worker->currentStartTimeUtc.toMSecsSinceEpoch()*1000000LL + endMs*1000000LL;
               emit worker->segmentClosed(worker->cameraIndex,
                                          worker->currentFilePath,
                                          endNs, endMs);
           }
           // open new
           worker->currentFilePath = filename;
           worker->currentStartTimeUtc = segmentStartTime.toUTC();
       }
       emit worker->segmentOpened(worker->cameraIndex, filename, startNs);
       // ---------------------------------------------------

    // Apply pending duration update if flagged
    if (worker->pendingDurationUpdate.load()) {
        qint64 maxSizeTimeNs = static_cast<qint64>(worker->nextSegmentDuration) * 1000000000LL;
        GstElement* sink = gst_bin_get_by_name(GST_BIN(worker->pipeline), "split");
        if (sink) {
            g_object_set(sink, "max-size-time", maxSizeTimeNs, NULL);
            worker->segmentDurationSec.store(worker->nextSegmentDuration);
            worker->pendingDurationUpdate.store(false);
            qDebug() << "[ArchiveWorker] Updated segment duration to" << worker->segmentDurationSec.load()
                     << "seconds for cam" << worker->cameraIndex;
            gst_object_unref(sink);
        } else {
            qDebug() << "[ArchiveWorker] Failed to update segment duration: splitmuxsink not found for cam" << worker->cameraIndex;
        }
    }

    {
        QMutexLocker locker(&worker->updateMutex);
        worker->updateCondition.wakeAll();
    }
    return g_strdup(filename.toUtf8().constData());
}


void ArchiveWorker::onBusMessage(GstBus* bus, GstMessage* message, gpointer user_data) {
    Q_UNUSED(bus);
    ArchiveWorker* worker = static_cast<ArchiveWorker*>(user_data);
    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
        GError* err = nullptr;
        gchar* debug_info = nullptr;
        gst_message_parse_error(message, &err, &debug_info);
        qDebug() << "[ArchiveWorker] GST ERROR for cam" << worker->cameraIndex << ":" << err->message;
        emit worker->recordingError(err->message);
        g_error_free(err);
        g_free(debug_info);
        worker->stop();
        break;
    }
    case GST_MESSAGE_EOS:
        // finalize the current open segment on EOS
                {
                    QMutexLocker lk(&worker->curMutex);
                    if (!worker->currentFilePath.isEmpty() && worker->currentStartTimeUtc.isValid()) {
                        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
                        const qint64 durMs = worker->currentStartTimeUtc.msecsTo(nowUtc);
                        const qint64 endNs = nowUtc.toMSecsSinceEpoch()*1000000LL;
                        emit worker->segmentClosed(worker->cameraIndex,
                                                   worker->currentFilePath, endNs, durMs);
                        worker->currentFilePath.clear();
                        worker->currentStartTimeUtc = QDateTime();
                    }
                }
        qDebug() << "[ArchiveWorker] GST EOS received for cam" << worker->cameraIndex;
        emit worker->segmentFinalized();
        break;
    case GST_MESSAGE_WARNING: {
        GError* err = nullptr;
        gchar* debug_info = nullptr;
        gst_message_parse_warning(message, &err, &debug_info);
        qDebug() << "[ArchiveWorker] GST WARNING for cam" << worker->cameraIndex << ":" << err->message;
        g_error_free(err);
        g_free(debug_info);
        break;
    }
    case GST_MESSAGE_STATE_CHANGED: {
        GstState old_state, new_state, pending;
        gst_message_parse_state_changed(message, &old_state, &new_state, &pending);
        if (GST_MESSAGE_SRC(message) == GST_OBJECT(worker->pipeline)) {
            qDebug() << "[ArchiveWorker] STATE CHANGED for cam" << worker->cameraIndex << ":"
                     << gst_element_state_get_name(old_state) << "->" << gst_element_state_get_name(new_state);
        }
        break;
    }
    default:
        break;
    }
}
