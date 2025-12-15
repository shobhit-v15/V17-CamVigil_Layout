#pragma once

#include <QObject>
#include <QTcpServer>

#include "node_config.h"

class NodeCoreService;

class NodeApiServer : public QObject {
    Q_OBJECT
public:
    NodeApiServer(NodeCoreService* core,
                  const NodeConfig& cfg,
                  QObject* parent = nullptr);

public slots:
    bool start();

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    QTcpServer* m_server{};
    NodeCoreService* m_core{};
    NodeConfig m_cfg;
    bool checkAuth(const QByteArray& headers) const;
    QByteArray handleRequest(const QByteArray& rawRequest);
};
