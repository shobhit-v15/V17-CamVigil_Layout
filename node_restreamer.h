#pragma once

#include <QObject>
#include <QString>
#include <QHash>
#include <QMutex>

#include <condition_variable>
#include <thread>

#include "node_config.h"

typedef struct _GMainContext GMainContext;
typedef struct _GMainLoop GMainLoop;
typedef struct _GstRTSPServer GstRTSPServer;
typedef struct _GstRTSPMountPoints GstRTSPMountPoints;
typedef unsigned int guint;
typedef int gboolean;
typedef void* gpointer;

struct NodeCameraRtspInfo {
    int cameraId;
    QString mainRtspUrl;
};

class NodeRestreamer : public QObject {
    Q_OBJECT
public:
    explicit NodeRestreamer(const NodeConfig& cfg, QObject* parent = nullptr);
    ~NodeRestreamer() override;

    void registerCamera(int cameraId, const QString& rtspMainUrl, bool useH265 = false);
    bool start();
    void stop();

    QString proxyUrlForCamera(int cameraId) const;

private:
    struct CameraEntry {
        QString url;
        bool useH265 = false;
    };

    struct InvokePayload {
        NodeRestreamer* self = nullptr;
        int cameraId = 0;
    };

    NodeConfig m_cfg;
    QHash<int, CameraEntry> m_cameras;
    mutable QMutex m_cameraMutex;

    GMainContext* m_context = nullptr;
    GMainLoop* m_loop = nullptr;
    GstRTSPServer* m_server = nullptr;
    GstRTSPMountPoints* m_mounts = nullptr;
    guint m_serverSourceId = 0;
    std::thread m_loopThread;

    mutable std::mutex m_stateMutex;
    std::condition_variable m_stateCv;
    bool m_running = false;
    bool m_serverReady = false;

    void runLoop();
    void addAllCamerasOnContext();
    void addMountForCamera(int cameraId);
    QString buildPipeline(const CameraEntry& entry) const;
    void teardownAfterLoop();
    static gboolean invokeAddCamera(gpointer data);
    static gboolean invokeStopLoop(gpointer data);
    static QString makeMountPath(int cameraId);
};
