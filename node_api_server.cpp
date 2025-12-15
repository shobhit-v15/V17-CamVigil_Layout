#include "node_api_server.h"

#include <QTcpSocket>
#include <QThread>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QUrlQuery>
#include <QUrl>
#include <QFile>
#include <QDebug>

#include "node_core_service.h"

namespace {
QByteArray httpResponse(int status, const QByteArray& statusText,
                        const QByteArray& body,
                        const QByteArray& contentType = "application/json")
{
    QByteArray resp;
    resp.append("HTTP/1.1 ").append(QByteArray::number(status)).append(' ').append(statusText).append("\r\n");
    resp.append("Content-Type: ").append(contentType).append("\r\n");
    resp.append("Content-Length: ").append(QByteArray::number(body.size())).append("\r\n");
    resp.append("Connection: close\r\n");
    resp.append("\r\n");
    resp.append(body);
    return resp;
}
} // namespace

NodeApiServer::NodeApiServer(NodeCoreService* core,
                             const NodeConfig& cfg,
                             QObject* parent)
    : QObject(parent)
    , m_core(core)
    , m_cfg(cfg)
{
}

bool NodeApiServer::start()
{
    if (m_server) {
        return true;
    }

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection,
            this, &NodeApiServer::onNewConnection);

    const QHostAddress addr(m_cfg.apiBindHost);
    if (!m_server->listen(addr, m_cfg.apiBindPort)) {
        qWarning() << "[NodeApiServer] listen failed" << m_server->errorString();
        return false;
    }

    qInfo() << "[NodeApiServer] Listening on" << addr << ":" << m_cfg.apiBindPort;
    return true;
}

void NodeApiServer::onNewConnection()
{
    if (!m_server) {
        return;
    }

    while (QTcpSocket* sock = m_server->nextPendingConnection()) {
        connect(sock, &QTcpSocket::readyRead, this, &NodeApiServer::onReadyRead);
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }
}

void NodeApiServer::onReadyRead()
{
    QTcpSocket* sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) {
        return;
    }

    QByteArray raw = sock->readAll();
    QByteArray response = handleRequest(raw);
    sock->write(response);
    sock->disconnectFromHost();
}

bool NodeApiServer::checkAuth(const QByteArray& headers) const
{
    const QByteArray prefix = "Authorization: Bearer ";
    const int idx = headers.indexOf(prefix);
    if (idx < 0) {
        return false;
    }
    const int end = headers.indexOf("\r\n", idx);
    QByteArray token = headers.mid(idx + prefix.size(),
                                   (end > idx ? end : headers.size()) - (idx + prefix.size()));
    token = token.trimmed();
    return token == m_cfg.apiToken.toUtf8();
}

QByteArray NodeApiServer::handleRequest(const QByteArray& rawRequest)
{
    const int headerEnd = rawRequest.indexOf("\r\n\r\n");
    QByteArray headerSection = headerEnd >= 0 ? rawRequest.left(headerEnd) : rawRequest;
    QList<QByteArray> lines = headerSection.split('\n');
    if (lines.isEmpty()) {
        return httpResponse(400, "Bad Request", "{\"error\":\"empty request\"}");
    }

    const QByteArray requestLine = lines.takeFirst().trimmed();
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 3) {
        return httpResponse(400, "Bad Request", "{\"error\":\"invalid request line\"}");
    }
    const QByteArray method = parts[0];
    const QByteArray pathAndQuery = parts[1];

    const int firstLineEnd = headerSection.indexOf("\r\n");
    const QByteArray headers = firstLineEnd >= 0
        ? headerSection.mid(firstLineEnd + 2)
        : QByteArray();
    if (!checkAuth(headers)) {
        return httpResponse(401, "Unauthorized", "{\"error\":\"unauthorized\"}");
    }

    QUrl url(QString::fromUtf8(pathAndQuery));

    if (method == "GET" && url.path() == "/api/v1/node/info") {
        const NodeInfo info = m_core ? m_core->getNodeInfo() : NodeInfo{};
        QJsonObject obj{
            {"node_id", info.nodeId},
            {"building_id", info.buildingId},
            {"hostname", info.hostname},
            {"software_version", info.softwareVersion},
            {"uptime_seconds", info.uptimeSeconds},
            {"total_cameras", info.totalCameras},
            {"recording_cameras", info.recordingCameras}
        };
        QJsonArray storageArr;
        for (const auto& s : info.storage) {
            QJsonObject so;
            so["mount_point"] = s.mountPoint;
            so["total_bytes"] = static_cast<double>(s.totalBytes);
            so["used_bytes"] = static_cast<double>(s.usedBytes);
            so["free_percent"] = s.freePercent;
            storageArr.append(so);
        }
        obj["storage"] = storageArr;
        const QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        return httpResponse(200, "OK", body);
    }

    if (method == "GET" && url.path() == "/api/v1/cameras") {
        QVector<NodeCamera> cameras = m_core ? m_core->listCameras() : QVector<NodeCamera>{};
        QJsonArray arr;
        for (const auto& cam : cameras) {
            QJsonObject c;
            c["id"] = cam.id;
            c["name"] = cam.name;
            c["group"] = cam.groupName;
            c["rtsp_main"] = cam.rtspMain;
            c["rtsp_sub"] = cam.rtspSub;
            c["is_recording"] = cam.isRecording;
            c["live_proxy_rtsp"] = cam.liveProxyRtsp;
            arr.append(c);
        }
        const QByteArray body = QJsonDocument(arr).toJson(QJsonDocument::Compact);
        return httpResponse(200, "OK", body);
    }

    if (method == "GET" && url.path() == "/api/v1/recordings") {
        QUrlQuery query(url);
        const int cameraId = query.queryItemValue("camera_id").toInt();
        const QDateTime from = QDateTime::fromString(query.queryItemValue("from"), Qt::ISODate);
        const QDateTime to = QDateTime::fromString(query.queryItemValue("to"), Qt::ISODate);

        QVector<NodeSegment> segments = m_core
            ? m_core->listSegments(cameraId, from, to)
            : QVector<NodeSegment>{};

        QJsonArray arr;
        for (const auto& seg : segments) {
            QJsonObject s;
            s["segment_id"] = static_cast<double>(seg.segmentId);
            s["camera_id"] = seg.cameraId;
            s["start"] = seg.start.toString(Qt::ISODate);
            s["end"] = seg.end.toString(Qt::ISODate);
            s["duration_sec"] = seg.durationSec;
            s["size_bytes"] = static_cast<double>(seg.sizeBytes);
            s["file_path"] = seg.filePath;
            arr.append(s);
        }
        const QByteArray body = QJsonDocument(arr).toJson(QJsonDocument::Compact);
        return httpResponse(200, "OK", body);
    }

    if (method == "GET" && url.path().startsWith("/media/segments/")) {
        const QString idStr = url.path().section('/', 3, 3);
        bool ok = false;
        const qint64 segId = idStr.toLongLong(&ok);
        if (!ok || segId <= 0) {
            return httpResponse(400, "Bad Request", "{\"error\":\"invalid segment id\"}");
        }
        const QString path = m_core ? m_core->resolveSegmentPath(segId) : QString();
        if (path.isEmpty()) {
            return httpResponse(404, "Not Found", "{\"error\":\"segment not found\"}");
        }

        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            return httpResponse(500, "Internal Server Error",
                                "{\"error\":\"failed to open segment\"}");
        }
        const QByteArray body = f.readAll(); // TODO: stream file instead of buffering entire clip.
        return httpResponse(200, "OK", body, "video/x-matroska");
    }

    return httpResponse(404, "Not Found", "{\"error\":\"not found\"}");
}
