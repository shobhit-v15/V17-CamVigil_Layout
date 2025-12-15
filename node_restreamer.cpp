#include "node_restreamer.h"

#include <QDebug>
#include <QUrl>
#include <QMutexLocker>

#include <QtGlobal>
#include <chrono>

#ifdef signals
#undef signals
#endif
#ifdef slots
#undef slots
#endif
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

namespace {

QString sanitizeUrl(const QString& url)
{
    QString safe = url;
    safe.replace('"', QStringLiteral("%22"));
    return safe;
}

QString makeH264Launch(const QString& url)
{
    const QString tpl = QStringLiteral(
        "rtspsrc location=\"%1\" protocols=tcp latency=200 drop-on-latency=true "
        "! rtph264depay ! h264parse config-interval=-1 "
        "! rtph264pay name=pay0 pt=96 config-interval=1");
    return tpl.arg(sanitizeUrl(url));
}

QString makeH265Launch(const QString& url)
{
    const QString tpl = QStringLiteral(
        "rtspsrc location=\"%1\" protocols=tcp latency=200 drop-on-latency=true "
        "! rtph265depay ! h265parse "
        "! rtph265pay name=pay0 pt=96 config-interval=1");
    return tpl.arg(sanitizeUrl(url));
}

} // namespace

QString NodeRestreamer::makeMountPath(int cameraId)
{
    return QString("/cam/%1").arg(cameraId);
}

NodeRestreamer::NodeRestreamer(const NodeConfig& cfg, QObject* parent)
    : QObject(parent)
    , m_cfg(cfg)
{
    gst_init(nullptr, nullptr);
}

NodeRestreamer::~NodeRestreamer()
{
    stop();
}

void NodeRestreamer::registerCamera(int cameraId, const QString& rtspMainUrl, bool useH265)
{
    {
        QMutexLocker locker(&m_cameraMutex);
        CameraEntry entry;
        entry.url = rtspMainUrl;
        entry.useH265 = useH265;
        m_cameras.insert(cameraId, entry);
    }
    qInfo() << "[NodeRestreamer] Registered camera" << cameraId
            << "URL:" << rtspMainUrl
            << "codec:" << (useH265 ? "H265" : "H264");

    std::unique_lock<std::mutex> stateLock(m_stateMutex);
    const bool shouldAttach = m_running && m_context;
    stateLock.unlock();

    if (shouldAttach) {
        auto* payload = new InvokePayload{this, cameraId};
        g_main_context_invoke(m_context, &NodeRestreamer::invokeAddCamera, payload);
    }
}

bool NodeRestreamer::start()
{
    {
        std::unique_lock<std::mutex> lk(m_stateMutex);
        if (m_running) {
            return true;
        }
    }

    m_context = g_main_context_new();
    if (!m_context) {
        qWarning() << "[NodeRestreamer] Failed to create GMainContext.";
        return false;
    }

    m_loop = g_main_loop_new(m_context, FALSE);
    if (!m_loop) {
        qWarning() << "[NodeRestreamer] Failed to create GMainLoop.";
        g_main_context_unref(m_context);
        m_context = nullptr;
        return false;
    }

    {
        std::unique_lock<std::mutex> lk(m_stateMutex);
        m_serverReady = false;
    }

    m_loopThread = std::thread(&NodeRestreamer::runLoop, this);

    std::unique_lock<std::mutex> lk(m_stateMutex);
    const bool ready = m_stateCv.wait_for(
        lk, std::chrono::seconds(5), [this]() { return m_serverReady; });
    if (!ready) {
        qWarning() << "[NodeRestreamer] Timed out waiting for RTSP server to start.";
        lk.unlock();
        stop();
        return false;
    }
    return true;
}

void NodeRestreamer::stop()
{
    {
        std::unique_lock<std::mutex> lk(m_stateMutex);
        if (!m_running) {
            if (m_loopThread.joinable()) {
                m_loopThread.join();
            }
            if (m_loop) {
                g_main_loop_unref(m_loop);
                m_loop = nullptr;
            }
            if (m_context) {
                g_main_context_unref(m_context);
                m_context = nullptr;
            }
            return;
        }
    }

    if (m_context) {
        g_main_context_invoke(m_context, &NodeRestreamer::invokeStopLoop, this);
    }

    if (m_loopThread.joinable()) {
        m_loopThread.join();
    }

    if (m_loop) {
        g_main_loop_unref(m_loop);
        m_loop = nullptr;
    }
    if (m_context) {
        g_main_context_unref(m_context);
        m_context = nullptr;
    }

    std::unique_lock<std::mutex> lk(m_stateMutex);
    m_running = false;
    lk.unlock();
}

QString NodeRestreamer::proxyUrlForCamera(int cameraId) const
{
    QMutexLocker locker(&m_cameraMutex);
    if (!m_cameras.contains(cameraId)) {
        return {};
    }
    const QString host = m_cfg.apiBindHost.isEmpty() ? QStringLiteral("127.0.0.1")
                                                     : m_cfg.apiBindHost;
    return QString("rtsp://%1:%2%3")
        .arg(host)
        .arg(m_cfg.rtspProxyPort)
        .arg(makeMountPath(cameraId));
}

void NodeRestreamer::runLoop()
{
    g_main_context_push_thread_default(m_context);

    m_server = gst_rtsp_server_new();
    if (!m_server) {
        qWarning() << "[NodeRestreamer] Failed to allocate GstRTSPServer.";
        {
            std::unique_lock<std::mutex> lk(m_stateMutex);
            m_running = false;
            m_serverReady = true;
        }
        m_stateCv.notify_all();
        g_main_context_pop_thread_default(m_context);
        return;
    }
    const QByteArray service = QByteArray::number(m_cfg.rtspProxyPort);
    g_object_set(m_server, "service", service.constData(), nullptr);

    m_mounts = gst_rtsp_server_get_mount_points(m_server);
    if (!m_mounts) {
        qWarning() << "[NodeRestreamer] Failed to obtain mount points.";
    }

    m_serverSourceId = gst_rtsp_server_attach(m_server, m_context);
    if (m_serverSourceId == 0) {
        qWarning() << "[NodeRestreamer] Failed to attach GstRTSPServer to main context.";
        {
            std::unique_lock<std::mutex> lk(m_stateMutex);
            m_running = false;
            m_serverReady = true;
        }
        m_stateCv.notify_all();
        teardownAfterLoop();
        g_main_context_pop_thread_default(m_context);
        return;
    }

    {
        std::unique_lock<std::mutex> lk(m_stateMutex);
        m_running = true;
        m_serverReady = true;
    }
    m_stateCv.notify_all();

    addAllCamerasOnContext();

    qInfo() << "[NodeRestreamer] RTSP server listening on port" << m_cfg.rtspProxyPort;
    g_main_loop_run(m_loop);
    qInfo() << "[NodeRestreamer] RTSP server loop terminated.";

    {
        std::unique_lock<std::mutex> lk(m_stateMutex);
        m_running = false;
    }

    teardownAfterLoop();
    g_main_context_pop_thread_default(m_context);
}

void NodeRestreamer::addAllCamerasOnContext()
{
    QMutexLocker locker(&m_cameraMutex);
    const auto keys = m_cameras.keys();
    locker.unlock();

    for (int cameraId : keys) {
        addMountForCamera(cameraId);
    }
}

void NodeRestreamer::addMountForCamera(int cameraId)
{
    QMutexLocker locker(&m_cameraMutex);
    const CameraEntry entry = m_cameras.value(cameraId);
    locker.unlock();

    if (!m_mounts || entry.url.isEmpty()) {
        return;
    }

    GstRTSPMediaFactory* factory = gst_rtsp_media_factory_new();
    if (!factory) {
        qWarning() << "[NodeRestreamer] Failed to create media factory for camera" << cameraId;
        return;
    }

    gst_rtsp_media_factory_set_shared(factory, TRUE);

    const QString launch = buildPipeline(entry);
    const QByteArray launchBytes = launch.toUtf8();
    gst_rtsp_media_factory_set_launch(factory, launchBytes.constData());

    const QByteArray mountPath = makeMountPath(cameraId).toUtf8();
    gst_rtsp_mount_points_add_factory(m_mounts, mountPath.constData(), factory);
    qInfo() << "[NodeRestreamer] Mounted" << mountPath.constData()
            << "->" << entry.url
            << "pipeline:" << launch;
}

QString NodeRestreamer::buildPipeline(const CameraEntry& entry) const
{
    if (entry.useH265) {
        return makeH265Launch(entry.url);
    }
    return makeH264Launch(entry.url);
}

void NodeRestreamer::teardownAfterLoop()
{
    if (m_serverSourceId != 0) {
        g_source_remove(m_serverSourceId);
        m_serverSourceId = 0;
    }
    if (m_mounts) {
        g_object_unref(m_mounts);
        m_mounts = nullptr;
    }
    if (m_server) {
        g_object_unref(m_server);
        m_server = nullptr;
    }
}

gboolean NodeRestreamer::invokeAddCamera(gpointer data)
{
    auto* payload = static_cast<InvokePayload*>(data);
    if (payload && payload->self) {
        payload->self->addMountForCamera(payload->cameraId);
    }
    delete payload;
    return G_SOURCE_REMOVE;
}

gboolean NodeRestreamer::invokeStopLoop(gpointer data)
{
    auto* self = static_cast<NodeRestreamer*>(data);
    if (self && self->m_loop) {
        g_main_loop_quit(self->m_loop);
    }
    return G_SOURCE_REMOVE;
}
