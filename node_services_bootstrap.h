#pragma once

#include <QObject>

#include "node_config.h"

class ArchiveManager;
class StorageService;
class NodeRestreamer;
class NodeCoreService;
class NodeApiServer;
class QThread;

/**
 * NodeServicesBootstrap wires together the runtime services required for the
 * CamVigil Node PoC (config, restreamer, core API and HTTP server) without
 * polluting the UI/MainWindow with threading and lifecycle details.
 */
class NodeServicesBootstrap : public QObject {
    Q_OBJECT
public:
    explicit NodeServicesBootstrap(ArchiveManager* archiveManager,
                                   StorageService* storageService,
                                   QObject* parent = nullptr);
    ~NodeServicesBootstrap() override;

    bool start();

    NodeCoreService* coreService() const { return m_core; }
    NodeRestreamer* restreamer() const { return m_restreamer; }
    NodeConfig config() const { return m_cfg; }

private:
    bool ensureRestreamer();
    void registerAllCameras();
    void startApiServerThread();

    ArchiveManager* m_archiveManager = nullptr;
    StorageService* m_storageService = nullptr;
    NodeConfig m_cfg;
    NodeRestreamer* m_restreamer = nullptr;
    NodeCoreService* m_core = nullptr;
    NodeApiServer* m_apiServer = nullptr;
    QThread* m_apiThread = nullptr;
    bool m_started = false;
};
