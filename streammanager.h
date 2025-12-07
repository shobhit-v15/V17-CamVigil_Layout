#ifndef STREAMMANAGER_H
#define STREAMMANAGER_H

#include <QObject>
#include <QLabel>
#include <QThread>
#include <vector>
#include <string>
#include "streamworker.h"
#include "camerastreams.h"

// Structure that ties each worker to its thread & URL.
struct WorkerInfo {
    std::string url;
    QThread* thread;
    StreamWorker* worker;
};

class StreamManager : public QObject {
    Q_OBJECT
public:
    explicit StreamManager(QObject* parent = nullptr);
    ~StreamManager();

    //  now accepts a vector of CamHWProfile to use the suburl for streaming.
    void startStreaming(const std::vector<CamHWProfile>& cameraProfiles, const std::vector<QLabel*>& labels);
    void stopStreaming();
    void restartStream(const std::string& url);

signals:
    // Forward frameReady signals from individual workers.
    void frameReady(int index, const QPixmap &pixmap);
   // void workerFinished();

private:
    std::vector<WorkerInfo> workers;
    std::vector<QLabel*> labels;
};

#endif // STREAMMANAGER_H
