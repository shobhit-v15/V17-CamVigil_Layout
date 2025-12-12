#include "node_api_server.h"

#include "node_core_service.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QTcpSocket>
#include <QUrlQuery>
#include <cstring>

namespace {
QByteArray statusText(int statusCode)
{
    switch (statusCode) {
    case 200: return QByteArrayLiteral("OK");
    case 201: return QByteArrayLiteral("Created");
    case 400: return QByteArrayLiteral("Bad Request");
    case 401: return QByteArrayLiteral("Unauthorized");
    case 404: return QByteArrayLiteral("Not Found");
    case 500: return QByteArrayLiteral("Internal Server Error");
    default: return QByteArrayLiteral("OK");
    }
}

QDateTime parseIsoDateTime(const QString& value)
{
    if (value.isEmpty()) {
        return {};
    }
    QDateTime dt = QDateTime::fromString(value, Qt::ISODate);
#if QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
    if (!dt.isValid()) {
        dt = QDateTime::fromString(value, Qt::ISODateWithMs);
    }
#endif
    if (dt.isValid()) {
        return dt;
    }
    return {};
}
} // namespace

NodeApiServer::NodeApiServer(NodeCoreService* core,
                             const NodeConfig& cfg,
                             QObject* parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
    , m_core(core)
    , m_cfg(cfg)
{
}

bool NodeApiServer::start()
{
    if (!m_server) {
        m_server = new QTcpServer(this);
    }
    QHostAddress addr;
    if (!addr.setAddress(m_cfg.apiBindHost)) {
        addr = QHostAddress::AnyIPv4;
    }

    if (!m_server->listen(addr, m_cfg.apiBindPort)) {
        qWarning() << "[NodeApiServer] listen failed:" << m_server->errorString();
        return false;
    }

    connect(m_server, &QTcpServer::newConnection,
            this, &NodeApiServer::onNewConnection);

    qInfo() << "[NodeApiServer] Listening on" << addr.toString() << ":" << m_cfg.apiBindPort;
    return true;
}

void NodeApiServer::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket* socket = m_server->nextPendingConnection();
        if (!socket) {
            continue;
        }
        connect(socket, &QTcpSocket::readyRead,
                this, &NodeApiServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected,
                socket, &QObject::deleteLater);
    }
}

void NodeApiServer::onReadyRead()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) {
        return;
    }

    const QByteArray raw = socket->readAll();
    const QByteArray response = handleRequest(raw);
    socket->write(response);
    socket->disconnectFromHost();
}

bool NodeApiServer::checkAuth(const QByteArray& headers) const
{
    if (m_cfg.apiToken.isEmpty()) {
        return true;
    }
    const auto lines = headers.split('\n');
    for (QByteArray line : lines) {
        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (!line.left(14).toLower().startsWith("authorization:")) {
            continue;
        }
        const int colonIdx = line.indexOf(':');
        if (colonIdx < 0) {
            continue;
        }
        const QByteArray value = line.mid(colonIdx + 1).trimmed();
        const QList<QByteArray> parts = value.split(' ');
        if (parts.size() < 2) {
            continue;
        }
        const QByteArray scheme = parts.at(0).trimmed();
        const QByteArray token = parts.at(1).trimmed();
        if (scheme.compare("Bearer", Qt::CaseInsensitive) == 0
            && QString::fromUtf8(token) == m_cfg.apiToken) {
            return true;
        }
    }
    return false;
}

QByteArray NodeApiServer::handleRequest(const QByteArray& rawRequest)
{
    if (!m_core) {
        QJsonObject obj;
        obj.insert(QStringLiteral("error"), QStringLiteral("NodeCoreService unavailable"));
        return makeJsonResponse(500, statusText(500), QJsonDocument(obj));
    }

    const QByteArray delimiter("\r\n\r\n");
    const int headerEnd = rawRequest.indexOf(delimiter);
    if (headerEnd < 0) {
        QJsonObject obj;
        obj.insert(QStringLiteral("error"), QStringLiteral("Malformed request"));
        return makeJsonResponse(400, statusText(400), QJsonDocument(obj));
    }
    const QByteArray headerBytes = rawRequest.left(headerEnd);

    QList<QByteArray> headerLines = headerBytes.split('\n');
    if (headerLines.isEmpty()) {
        QJsonObject obj;
        obj.insert(QStringLiteral("error"), QStringLiteral("Missing request line"));
        return makeJsonResponse(400, statusText(400), QJsonDocument(obj));
    }

    QByteArray requestLine = headerLines.takeFirst().trimmed();
    const QList<QByteArray> requestParts = requestLine.split(' ');
    if (requestParts.size() < 3) {
        QJsonObject obj;
        obj.insert(QStringLiteral("error"), QStringLiteral("Invalid request line"));
        return makeJsonResponse(400, statusText(400), QJsonDocument(obj));
    }

    const QByteArray method = requestParts.at(0);
    QByteArray pathAndQuery = requestParts.at(1);
    const QByteArray version = requestParts.at(2);
    Q_UNUSED(version);

    if (!checkAuth(headerBytes)) {
        QJsonObject obj;
        obj.insert(QStringLiteral("error"), QStringLiteral("Unauthorized"));
        return makeJsonResponse(401, statusText(401), QJsonDocument(obj));
    }

    QByteArray queryPart;
    int queryIndex = pathAndQuery.indexOf('?');
    if (queryIndex >= 0) {
        queryPart = pathAndQuery.mid(queryIndex + 1);
        pathAndQuery = pathAndQuery.left(queryIndex);
    }
    QUrlQuery query(QString::fromUtf8(queryPart));

    if (method != "GET") {
        QJsonObject obj;
        obj.insert(QStringLiteral("error"), QStringLiteral("Only GET supported"));
        return makeJsonResponse(400, statusText(400), QJsonDocument(obj));
    }

    if (pathAndQuery == "/api/v1/node/info") {
        const NodeInfo info = m_core->getNodeInfo();
        QJsonObject obj;
        obj.insert(QStringLiteral("node_id"), info.nodeId);
        obj.insert(QStringLiteral("building_id"), info.buildingId);
        obj.insert(QStringLiteral("hostname"), info.hostname);
        obj.insert(QStringLiteral("software_version"), info.softwareVersion);
        obj.insert(QStringLiteral("uptime_seconds"), static_cast<qint64>(info.uptimeSeconds));
        obj.insert(QStringLiteral("total_cameras"), info.totalCameras);
        obj.insert(QStringLiteral("recording_cameras"), info.recordingCameras);

        QJsonArray storageArr;
        for (const auto& storage : info.storage) {
            QJsonObject storageObj;
            storageObj.insert(QStringLiteral("mount_point"), storage.mountPoint);
            storageObj.insert(QStringLiteral("total_bytes"), static_cast<qint64>(storage.totalBytes));
            storageObj.insert(QStringLiteral("used_bytes"), static_cast<qint64>(storage.usedBytes));
            storageObj.insert(QStringLiteral("free_percent"), storage.freePercent);
            storageArr.push_back(storageObj);
        }
        obj.insert(QStringLiteral("storage"), storageArr);
        return makeJsonResponse(200, statusText(200), QJsonDocument(obj));
    }

    if (pathAndQuery == "/api/v1/cameras") {
        const QVector<NodeCamera> cameras = m_core->listCameras();
        QJsonArray arr;
        for (const auto& cam : cameras) {
            QJsonObject c;
            c.insert(QStringLiteral("id"), cam.id);
            c.insert(QStringLiteral("name"), cam.name);
            c.insert(QStringLiteral("group_name"), cam.groupName);
            c.insert(QStringLiteral("rtsp_main"), cam.rtspMain);
            c.insert(QStringLiteral("rtsp_sub"), cam.rtspSub);
            c.insert(QStringLiteral("is_recording"), cam.isRecording);
            c.insert(QStringLiteral("live_proxy_rtsp"), cam.liveProxyRtsp);
            arr.push_back(c);
        }
        QJsonObject obj;
        obj.insert(QStringLiteral("cameras"), arr);
        return makeJsonResponse(200, statusText(200), QJsonDocument(obj));
    }

    if (pathAndQuery == "/api/v1/recordings") {
        bool ok = false;
        const int cameraId = query.queryItemValue(QStringLiteral("camera_id")).toInt(&ok);
        if (!ok || cameraId <= 0) {
            QJsonObject obj;
            obj.insert(QStringLiteral("error"), QStringLiteral("camera_id is required"));
            return makeJsonResponse(400, statusText(400), QJsonDocument(obj));
        }
        const QDateTime from = parseIsoDateTime(query.queryItemValue(QStringLiteral("from")));
        const QDateTime to = parseIsoDateTime(query.queryItemValue(QStringLiteral("to")));

        const QVector<NodeSegment> segs = m_core->listSegments(cameraId, from, to);
        QJsonArray arr;
        for (const auto& seg : segs) {
            QJsonObject s;
            s.insert(QStringLiteral("segment_id"), static_cast<qint64>(seg.segmentId));
            s.insert(QStringLiteral("camera_id"), seg.cameraId);
            s.insert(QStringLiteral("start_utc"), seg.start.toString(Qt::ISODate));
            s.insert(QStringLiteral("end_utc"), seg.end.toString(Qt::ISODate));
            s.insert(QStringLiteral("duration_sec"), seg.durationSec);
            s.insert(QStringLiteral("size_bytes"), static_cast<qint64>(seg.sizeBytes));
            s.insert(QStringLiteral("file_path"), seg.filePath);
            arr.push_back(s);
        }
        QJsonObject obj;
        obj.insert(QStringLiteral("segments"), arr);
        return makeJsonResponse(200, statusText(200), QJsonDocument(obj));
    }

    if (pathAndQuery.startsWith("/media/segments/")) {
        const QByteArray idPart = pathAndQuery.mid(strlen("/media/segments/"));
        bool ok = false;
        const qint64 segmentId = idPart.toLongLong(&ok);
        if (!ok || segmentId <= 0) {
            QJsonObject obj;
            obj.insert(QStringLiteral("error"), QStringLiteral("Invalid segment id"));
            return makeJsonResponse(400, statusText(400), QJsonDocument(obj));
        }

        const QString path = m_core->resolveSegmentPath(segmentId);
        if (path.isEmpty()) {
            QJsonObject obj;
            obj.insert(QStringLiteral("error"), QStringLiteral("Segment not found"));
            return makeJsonResponse(404, statusText(404), QJsonDocument(obj));
        }

        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            QJsonObject obj;
            obj.insert(QStringLiteral("error"), QStringLiteral("Unable to open segment"));
            return makeJsonResponse(404, statusText(404), QJsonDocument(obj));
        }

        const QByteArray data = f.readAll();
        // TODO: Implement chunked/streamed responses instead of loading entire file in memory.
        return makeBinaryResponse(200,
                                  statusText(200),
                                  data,
                                  QByteArrayLiteral("video/x-matroska"));
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("error"), QStringLiteral("Not Found"));
    return makeJsonResponse(404, statusText(404), QJsonDocument(obj));
}

QByteArray NodeApiServer::makeJsonResponse(int statusCode,
                                           const QByteArray& statusTextValue,
                                           const QJsonDocument& doc) const
{
    const QByteArray body = doc.toJson(QJsonDocument::Compact);
    QByteArray out;
    out.reserve(256 + body.size());
    out.append("HTTP/1.1 ");
    out.append(QByteArray::number(statusCode));
    out.append(' ');
    out.append(statusTextValue);
    out.append("\r\nContent-Type: application/json\r\n");
    out.append("Content-Length: ");
    out.append(QByteArray::number(body.size()));
    out.append("\r\nConnection: close\r\n\r\n");
    out.append(body);
    return out;
}

QByteArray NodeApiServer::makeBinaryResponse(int statusCode,
                                             const QByteArray& statusTextValue,
                                             const QByteArray& body,
                                             const QByteArray& contentType) const
{
    QByteArray out;
    out.reserve(256 + body.size());
    out.append("HTTP/1.1 ");
    out.append(QByteArray::number(statusCode));
    out.append(' ');
    out.append(statusTextValue);
    out.append("\r\nContent-Type: ");
    out.append(contentType);
    out.append("\r\nContent-Length: ");
    out.append(QByteArray::number(body.size()));
    out.append("\r\nConnection: close\r\n\r\n");
    out.append(body);
    return out;
}
