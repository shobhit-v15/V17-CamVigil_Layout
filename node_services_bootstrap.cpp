/**
 * Remote testing quickstart:
 *   TOKEN=<api_token_from_node_config>
 *   NODE=192.168.1.50
 *   curl -H "Authorization: Bearer $TOKEN" http://$NODE:8080/api/v1/node/info
 *   curl -H "Authorization: Bearer $TOKEN" http://$NODE:8080/api/v1/cameras
 *   curl -H "Authorization: Bearer $TOKEN" "http://$NODE:8080/api/v1/recordings?camera_id=1&from=2024-05-01T00:00:00Z&to=2024-05-01T23:59:59Z"
 *   curl -H "Authorization: Bearer $TOKEN" -H "Range: bytes=0-1023" http://$NODE:8080/media/segments/12345 -o first-kb.bin
 *   curl -I -H "Authorization: Bearer $TOKEN" http://$NODE:8080/media/segments/12345
 *   gst-play-1.0 rtsp://$NODE:8554/cam/1
 *   ffplay -rtsp_transport tcp rtsp://$NODE:8554/cam/1
 */
#include "node_services_bootstrap.h"

#include <QDir>
#include <QThread>
#include <QDebug>

#include "node_config.h"
#include "node_restreamer.h"
#include "node_core_service.h"
#include "node_api_server.h"
#include "archivemanager.h"
#include "storageservice.h"

NodeServicesBootstrap::NodeServicesBootstrap(ArchiveManager* archiveManager,
                                             StorageService* storageService,
                                             QObject* parent)
    : QObject(parent)
    , m_archiveManager(archiveManager)
    , m_storageService(storageService)
{
    const QString nodeConfigPath = QDir::currentPath() + "/node_config.json";
    NodeConfigService cfgService(nodeConfigPath);
    m_cfg = cfgService.load();
}

NodeServicesBootstrap::~NodeServicesBootstrap()
{
    if (m_apiThread) {
        m_apiThread->quit();
        m_apiThread->wait();
        m_apiThread = nullptr;
    }
    delete m_restreamer;
    m_restreamer = nullptr;
}

bool NodeServicesBootstrap::start()
{
    if (m_started) {
        return true;
    }
    if (!m_archiveManager) {
        qWarning() << "[NodeServices] ArchiveManager not ready. Node services disabled.";
        return false;
    }
    if (!ensureRestreamer()) {
        return false;
    }

    if (!m_core) {
        m_core = new NodeCoreService(nullptr,
                                     nullptr,
                                     m_archiveManager,
                                     m_storageService,
                                     m_restreamer,
                                     m_cfg,
                                     this);
    }

    registerAllCameras();

    if (!m_restreamer->start()) {
        qWarning() << "[NodeServices] NodeRestreamer failed to start.";
    }

    startApiServerThread();
    m_started = true;
    return true;
}

bool NodeServicesBootstrap::ensureRestreamer()
{
    if (m_restreamer) {
        return true;
    }
    m_restreamer = new NodeRestreamer(m_cfg);
    return m_restreamer != nullptr;
}

void NodeServicesBootstrap::registerAllCameras()
{
    if (!m_core || !m_restreamer) {
        return;
    }
    const QVector<NodeCamera> cameras = m_core->listCameras();
    if (cameras.isEmpty()) {
        qWarning() << "[NodeServices] No cameras found in DB when bootstrapping restreamer.";
    }
    for (const NodeCamera& cam : cameras) {
        if (cam.rtspMain.isEmpty()) {
            continue;
        }
        m_restreamer->registerCamera(cam.id, cam.rtspMain);
    }
}

void NodeServicesBootstrap::startApiServerThread()
{
    if (m_apiThread) {
        return;
    }
    m_apiThread = new QThread(this);
    m_apiServer = new NodeApiServer(m_core, m_cfg);
    m_apiServer->moveToThread(m_apiThread);
    connect(m_apiThread, &QThread::started, m_apiServer, &NodeApiServer::start);
    connect(m_apiThread, &QThread::finished, m_apiServer, &QObject::deleteLater);
    m_apiThread->start();
}
