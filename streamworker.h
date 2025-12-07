#ifndef STREAMWORKER_H
#define STREAMWORKER_H

#include <QObject>
#include <QImage>
#include <QPixmap>
#include <string>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>

class StreamWorker : public QObject {
    Q_OBJECT
public:
    explicit StreamWorker(const std::string& url, int index, QObject* parent = nullptr);
    ~StreamWorker();

    // Process the stream in a dedicated thread.
    void process();
    void stop();

    bool isCameraConnected() const { return isConnected; }

signals:
    // Emits a new frame as a QPixmap.
    void frameReady(int index, const QPixmap &pixmap);
    void streamError(int index, const std::string &url);
    void finished();

private:
    std::string url;
    int index;
    GstElement* pipeline;
    GstElement* appsink;
    bool running;
    bool isConnected;
};

#endif // STREAMWORKER_H
