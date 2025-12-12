#pragma once

#include <QObject>
#include <QHash>
#include <QString>

#include "node_config.h"

struct NodeCameraRtspInfo {
    int cameraId = -1;
    QString mainRtspUrl;
};

class NodeRestreamer : public QObject {
    Q_OBJECT
public:
    explicit NodeRestreamer(const NodeConfig& cfg, QObject* parent = nullptr);
    ~NodeRestreamer() override;

    void registerCamera(int cameraId, const QString& rtspMainUrl);
    bool start();
    QString proxyUrlForCamera(int cameraId) const;

private:
    NodeConfig m_cfg;
    QHash<int, QString> m_rtspUrls;
    bool m_started = false;

    // TODO: Hold GstRTSPServer*, GstRTSPMountPoints*, and per-camera factories when PoC becomes functional.
};
