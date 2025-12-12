// TODO: Hook up actual GstRTSPServer usage once gstreamer-rtsp-server-1.0 is available at runtime.

#include "node_restreamer.h"

#include <QDebug>

// GStreamer headers (library must be available via pkg-config / linker flags)
#ifdef signals
#undef signals
#endif

#include <gst/gst.h>

#ifdef HAVE_GST_RTSP_SERVER
#include <gst/rtsp-server/rtsp-server.h>
#endif

NodeRestreamer::NodeRestreamer(const NodeConfig& cfg, QObject* parent)
    : QObject(parent)
    , m_cfg(cfg)
{
}

NodeRestreamer::~NodeRestreamer()
{
    // TODO: Shut down RTSP server + clean up factories when implementation is ready.
}

void NodeRestreamer::registerCamera(int cameraId, const QString& rtspMainUrl)
{
    if (cameraId < 0 || rtspMainUrl.isEmpty()) {
        qWarning() << "[NodeRestreamer] Invalid camera registration request" << cameraId << rtspMainUrl;
        return;
    }

    m_rtspUrls.insert(cameraId, rtspMainUrl);

    if (m_started) {
        // TODO: add dynamic mount registration when server is running.
        qInfo() << "[NodeRestreamer] Camera" << cameraId << "registered while server running - TODO attach factory.";
    }
}

bool NodeRestreamer::start()
{
    if (m_started) {
        return true;
    }

    qInfo() << "[NodeRestreamer] Starting RTSP proxy on port" << m_cfg.rtspProxyPort
            << "for" << m_rtspUrls.size() << "cameras";

#ifndef HAVE_GST_RTSP_SERVER
    qWarning() << "[NodeRestreamer] gstreamer-rtsp-server-1.0 not available at build time. Stub mode only.";
    m_started = false;
    return false;
#else
    // TODO: Instantiate GstRTSPServer, create mount points, and attach media factories per camera.
    qWarning() << "[NodeRestreamer] GStreamer RTSP server integration not implemented yet.";
    m_started = false;
    return false;
#endif
}

QString NodeRestreamer::proxyUrlForCamera(int cameraId) const
{
    if (!m_rtspUrls.contains(cameraId)) {
        return {};
    }
    return QStringLiteral("rtsp://%1:%2/cam/%3")
        .arg(m_cfg.apiBindHost.isEmpty() ? QStringLiteral("127.0.0.1") : m_cfg.apiBindHost)
        .arg(m_cfg.rtspProxyPort)
        .arg(cameraId);
}
