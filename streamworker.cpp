#include "streamworker.h"
#include <QDebug>
#include <QThread>

StreamWorker::StreamWorker(const std::string& url, int index, QObject* parent)
    : QObject(parent),
      url(url),
      index(index),
      pipeline(nullptr),
      appsink(nullptr),
      running(true),
      isConnected(false)
{
    gst_init(nullptr, nullptr);
}

StreamWorker::~StreamWorker() {
    stop();
    if (appsink) {
        gst_object_unref(appsink);
        appsink = nullptr;
    }
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline = nullptr;
    }
}

void StreamWorker::process() {
    QString pipelineDesc = QString(
        "rtspsrc location=\"%1\" latency=200 ! "
        "rtph264depay ! h264parse ! vaapih264dec ! videoconvert ! "
        "videoscale ! video/x-raw,format=RGB,width=640,height=480 ! "
        "appsink name=mysink sync=false"
    ).arg(QString::fromStdString(url));

    GError* error = nullptr;
    pipeline = gst_parse_launch(pipelineDesc.toUtf8().constData(), &error);
    if (!pipeline) {
        qDebug() << "StreamWorker[" << index << "]: Failed to create pipeline:" << (error ? error->message : "Unknown error");
        emit streamError(index, url);
        return;
    }

    appsink = gst_bin_get_by_name(GST_BIN(pipeline), "mysink");
    if (!appsink) {
        qDebug() << "StreamWorker[" << index << "]: Failed to get appsink.";
        emit streamError(index, url);
        gst_object_unref(pipeline);
        pipeline = nullptr;
        return;
    }

    gst_app_sink_set_emit_signals(GST_APP_SINK(appsink), false);
    gst_app_sink_set_drop(GST_APP_SINK(appsink), true);
    gst_app_sink_set_max_buffers(GST_APP_SINK(appsink), 1);

    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        qDebug() << "StreamWorker[" << index << "]: Failed to set pipeline to PLAYING state.";
        emit streamError(index, url);
        gst_object_unref(appsink);
        gst_object_unref(pipeline);
        appsink = nullptr;
        pipeline = nullptr;
        return;
    }

    qDebug() << "StreamWorker[" << index << "] started streaming.";

    isConnected = true;
    int nullSampleCount = 0;
    const int MAX_NULL_SAMPLES = 30;

    while (running) {
        GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(appsink));
        if (!sample) {
            nullSampleCount++;
            QThread::msleep(100);
            if (nullSampleCount >= MAX_NULL_SAMPLES) {
                qDebug() << "StreamWorker[" << index << "] timeout: No frames received after"
                         << MAX_NULL_SAMPLES << "attempts.";
                emit streamError(index, url);
                break;
            }
            continue;
        }

        nullSampleCount = 0;
        GstBuffer* buffer = gst_sample_get_buffer(sample);
        GstCaps* caps = gst_sample_get_caps(sample);
        if (!caps) {
            gst_sample_unref(sample);
            continue;
        }

        GstStructure* s = gst_caps_get_structure(caps, 0);
        int width = 0, height = 0;
        gst_structure_get_int(s, "width", &width);
        gst_structure_get_int(s, "height", &height);

        GstMapInfo map;
        if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            gst_sample_unref(sample);
            continue;
        }

        QImage image((uchar*)map.data, width, height, QImage::Format_RGB888);
        emit frameReady(index, QPixmap::fromImage(image.copy()));

        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);

        QThread::msleep(200);  // Throttle for 5 fps
    }

    // Cleanup
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(appsink);
    gst_object_unref(pipeline);
    appsink = nullptr;
    pipeline = nullptr;

    //emit finished();
}

void StreamWorker::stop() {
    running = false;
    if (pipeline) {
        gst_element_send_event(pipeline, gst_event_new_eos());
    }
}
