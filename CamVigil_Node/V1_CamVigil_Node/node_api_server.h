#pragma once

#include <QObject>
#include <QTcpServer>
#include <QJsonDocument>

#include "node_config.h"

class NodeCoreService;
class QTcpSocket;

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
    QTcpServer* m_server = nullptr;
    NodeCoreService* m_core = nullptr;
    NodeConfig m_cfg;

    bool checkAuth(const QByteArray& headers) const;
    QByteArray handleRequest(const QByteArray& rawRequest);

    QByteArray makeJsonResponse(int statusCode,
                                const QByteArray& statusText,
                                const QJsonDocument& doc) const;
    QByteArray makeBinaryResponse(int statusCode,
                                  const QByteArray& statusText,
                                  const QByteArray& body,
                                  const QByteArray& contentType) const;
};
