#pragma once

#include <QObject>
#include <QTcpServer>
#include <QMap>
#include <QUrl>
#include <QJsonValue>
#include <QPair>

#include "node_config.h"

class NodeCoreService;

class NodeApiServer : public QObject {
    Q_OBJECT
public:
    struct HttpRequestContext {
        QByteArray method;
        QByteArray rawPath;
        QUrl url;
        QByteArray httpVersion;
        QMap<QByteArray, QByteArray> headers; // lower-cased keys
        QString requestId;
        QString remoteAddress;
        quint16 remotePort = 0;
    };

    struct HttpResponsePayload {
        int status = 500;
        QByteArray statusText = "Internal Server Error";
        QList<QPair<QByteArray, QByteArray>> headers;
        QByteArray body;
        QByteArray contentType = "application/json";
        qint64 explicitContentLength = -1;
    };

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
    quint64 m_requestCounter = 0;

    QString nextRequestId();
    bool checkAuth(const QMap<QByteArray, QByteArray>& headers) const;
    HttpResponsePayload handleRequest(const HttpRequestContext& req);
    HttpResponsePayload handleMediaRequest(const HttpRequestContext& req,
                                           qint64 segmentId,
                                           bool isHead);
    HttpResponsePayload jsonPayload(int status,
                                    const QByteArray& statusText,
                                    const QJsonValue& value,
                                    const QString& requestId) const;
    HttpResponsePayload jsonError(int status,
                                  const QString& code,
                                  const QString& message,
                                  const QString& requestId) const;
    QByteArray serializeResponse(const HttpResponsePayload& resp,
                                 const QString& requestId) const;
    bool parseRequest(const QByteArray& raw,
                      const QString& requestId,
                      HttpRequestContext& outReq) const;
};
