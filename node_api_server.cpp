/**
 * LAN testing quickstart:
 *   TOKEN=<api_token>
 *   NODE=192.168.1.50
 *   curl -H "Authorization: Bearer $TOKEN" http://$NODE:8080/api/v1/node/info
 *   curl -H "Authorization: Bearer $TOKEN" http://$NODE:8080/api/v1/cameras
 *   curl -H "Authorization: Bearer $TOKEN" "http://$NODE:8080/api/v1/recordings?camera_id=1&from=2024-05-01T00:00:00Z&to=2024-05-01T23:59:59Z"
 *   curl -H "Authorization: Bearer $TOKEN" -H "Range: bytes=0-1023" http://$NODE:8080/media/segments/12345 -o first-kb.bin
 *   curl -I -H "Authorization: Bearer $TOKEN" http://$NODE:8080/media/segments/12345
 */
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
#include <QFileInfo>
#include <QElapsedTimer>
#include <QDebug>

#include "node_core_service.h"

namespace {

struct ByteRange {
    bool hasRange = false;
    qint64 start = 0;
    qint64 end = -1;
};

ByteRange parseRangeHeader(const QByteArray& header, qint64 totalSize, bool& ok)
{
    ByteRange result;
    ok = true;

    if (header.isEmpty()) {
        ok = true;
        return result;
    }

    const QByteArray trimmed = header.trimmed();
    if (!trimmed.startsWith("bytes=")) {
        ok = false;
        return result;
    }

    const QByteArray spec = trimmed.mid(6); // remove "bytes="
    if (spec.contains(',')) {
        ok = false; // multi-range unsupported
        return result;
    }

    const int dash = spec.indexOf('-');
    if (dash < 0) {
        ok = false;
        return result;
    }

    const QByteArray startStr = spec.left(dash).trimmed();
    const QByteArray endStr = spec.mid(dash + 1).trimmed();

    const bool hasStart = !startStr.isEmpty();
    const bool hasEnd = !endStr.isEmpty();

    qint64 start = 0;
    qint64 end = totalSize > 0 ? (totalSize - 1) : -1;

    bool startOk = true;
    bool endOk = true;

    if (hasStart) {
        start = startStr.toLongLong(&startOk);
    } else if (hasEnd) {
        const qint64 suffix = endStr.toLongLong(&endOk);
        if (endOk && totalSize > 0) {
            start = qMax<qint64>(0, totalSize - suffix);
            end = totalSize - 1;
        }
    }

    if (hasStart && hasEnd) {
        end = endStr.toLongLong(&endOk);
    } else if (hasStart && !hasEnd && totalSize > 0) {
        end = totalSize - 1;
    }

    if (!startOk || !endOk || start < 0 || (end >= 0 && end < start)) {
        ok = false;
        return result;
    }

    if (totalSize > 0) {
        if (start >= totalSize) {
            ok = false;
            return result;
        }
        if (end < 0 || end >= totalSize) {
            end = totalSize - 1;
        }
    }

    result.hasRange = true;
    result.start = start;
    result.end = end;
    return result;
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

    QHostAddress addr;
    if (m_cfg.apiBindHost.isEmpty() || m_cfg.apiBindHost == QStringLiteral("0.0.0.0")) {
        addr = QHostAddress::Any;
    } else if (!addr.setAddress(m_cfg.apiBindHost)) {
        qWarning() << "[NodeApiServer] Invalid api_bind_host" << m_cfg.apiBindHost << "- listening on any IPv4.";
        addr = QHostAddress::Any;
    }

    if (!m_server->listen(addr, m_cfg.apiBindPort)) {
        qWarning() << "[NodeApiServer] listen failed" << m_server->errorString();
        return false;
    }

    qInfo() << "[NodeApiServer] Listening on" << m_server->serverAddress().toString()
            << ":" << m_cfg.apiBindPort;
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

    QElapsedTimer timer;
    timer.start();

    QByteArray raw = sock->readAll();

    HttpRequestContext req;
    req.requestId = nextRequestId();
    req.remoteAddress = sock->peerAddress().toString();
    req.remotePort = sock->peerPort();

    HttpResponsePayload resp;
    QString methodStr = QStringLiteral("UNKNOWN");
    QString pathStr = QStringLiteral("-");

    const bool parsed = parseRequest(raw, req.requestId, req);
    if (parsed) {
        methodStr = QString::fromLatin1(req.method);
        pathStr = req.url.path();
    }

    if (!parsed) {
        resp = jsonError(400, "bad_request", "Malformed HTTP request", req.requestId);
    } else if (!checkAuth(req.headers)) {
        resp = jsonError(401, "unauthorized", "Invalid bearer token", req.requestId);
    } else {
        resp = handleRequest(req);
    }

    QByteArray bytes = serializeResponse(resp, req.requestId);
    sock->write(bytes);
    sock->disconnectFromHost();

    const qint64 durationMs = timer.elapsed();
    qInfo().nospace() << "[NodeApiServer] " << req.remoteAddress << ":" << req.remotePort
                      << " " << methodStr << " " << pathStr
                      << " -> " << resp.status
                      << " (" << req.requestId << ", " << durationMs << " ms)";
}

QString NodeApiServer::nextRequestId()
{
    ++m_requestCounter;
    return QStringLiteral("req-%1").arg(m_requestCounter);
}

bool NodeApiServer::parseRequest(const QByteArray& raw,
                                 const QString& requestId,
                                 HttpRequestContext& outReq) const
{
    Q_UNUSED(requestId);
    const int headerEnd = raw.indexOf("\r\n\r\n");
    const QByteArray headerSection = headerEnd >= 0 ? raw.left(headerEnd) : raw;
    QList<QByteArray> lines = headerSection.split('\n');
    if (lines.isEmpty()) {
        return false;
    }

    const QByteArray requestLine = lines.takeFirst().trimmed();
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 3) {
        return false;
    }
    outReq.method = parts[0].trimmed().toUpper();
    outReq.rawPath = parts[1].trimmed();
    outReq.httpVersion = parts[2].trimmed();
    outReq.url = QUrl(QString::fromUtf8(outReq.rawPath));
    outReq.headers.clear();

    for (QByteArray line : lines) {
        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const int colon = line.indexOf(':');
        if (colon <= 0) {
            continue;
        }
        QByteArray key = line.left(colon).trimmed().toLower();
        QByteArray value = line.mid(colon + 1).trimmed();
        outReq.headers.insert(key, value);
    }
    return true;
}

bool NodeApiServer::checkAuth(const QMap<QByteArray, QByteArray>& headers) const
{
    const QByteArray auth = headers.value("authorization");
    if (auth.isEmpty()) {
        return false;
    }
    const QByteArray prefix = "Bearer ";
    if (!auth.startsWith(prefix)) {
        return false;
    }
    const QByteArray token = auth.mid(prefix.size()).trimmed();
    return token == m_cfg.apiToken.toUtf8();
}

NodeApiServer::HttpResponsePayload NodeApiServer::handleRequest(const HttpRequestContext& req)
{
    const QString path = req.url.path();
    const QByteArray method = req.method;

    if (method == "GET" && path == "/api/v1/node/info") {
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
        return jsonPayload(200, QByteArray(), obj, req.requestId);
    }

    if (method == "GET" && path == "/api/v1/cameras") {
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
        QJsonObject payload;
        payload["cameras"] = arr;
        return jsonPayload(200, QByteArray(), payload, req.requestId);
    }

    if (method == "GET" && path == "/api/v1/recordings") {
        if (!m_core) {
            return jsonError(500, "core_unavailable", "NodeCoreService unavailable", req.requestId);
        }
        QUrlQuery query(req.url);
        const int cameraId = query.queryItemValue("camera_id").toInt();
        const QDateTime from = QDateTime::fromString(query.queryItemValue("from"), Qt::ISODate);
        const QDateTime to = QDateTime::fromString(query.queryItemValue("to"), Qt::ISODate);

        QVector<NodeSegment> segments = m_core->listSegments(cameraId, from, to);

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
        QJsonObject payload;
        payload["segments"] = arr;
        return jsonPayload(200, QByteArray(), payload, req.requestId);
    }

    if ((method == "GET" || method == "HEAD") && path.startsWith("/media/segments/")) {
        const QString idStr = path.section('/', 3, 3);
        bool ok = false;
        const qint64 segId = idStr.toLongLong(&ok);
        if (!ok || segId <= 0) {
            return jsonError(400, "invalid_segment_id", "Segment id must be numeric", req.requestId);
        }
        const bool isHead = (method == "HEAD");
        return handleMediaRequest(req, segId, isHead);
    }

    if (method == "GET" && path == "/api/v1/health") {
        QJsonObject obj{
            {"http_ok", true},
            {"db_ok", m_core ? m_core->isDatabaseOk() : false},
            {"rtsp_ok", m_core ? m_core->isRtspOk() : false},
            {"cameras_count", m_core ? m_core->cameraCount() : 0},
            {"time_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}
        };
        return jsonPayload(200, QByteArray(), obj, req.requestId);
    }

    if (method == "GET" && path == "/api/v1/version") {
        const QString version = m_core ? m_core->softwareVersion() : QStringLiteral("unknown");
        QJsonObject obj{{"version", version}};
        return jsonPayload(200, QByteArray(), obj, req.requestId);
    }

    return jsonError(404, "not_found", "Endpoint not found", req.requestId);
}

NodeApiServer::HttpResponsePayload NodeApiServer::handleMediaRequest(const HttpRequestContext& req,
                                                                     qint64 segmentId,
                                                                     bool isHead)
{
    if (!m_core) {
        return jsonError(500, "core_unavailable", "NodeCoreService unavailable", req.requestId);
    }
    bool found = false;
    const NodeSegment seg = m_core->segmentById(segmentId, &found);
    if (!found || seg.filePath.isEmpty()) {
        return jsonError(404, "segment_not_found", "Segment not found", req.requestId);
    }

    QFileInfo info(seg.filePath);
    if (!info.exists() || !info.isFile()) {
        return jsonError(404, "segment_missing", "Segment file missing", req.requestId);
    }

    const qint64 totalSize = info.size();
    bool rangeOk = true;
    const QByteArray rangeHeader = req.headers.value("range");
    const ByteRange range = parseRangeHeader(rangeHeader, totalSize, rangeOk);
    if (!rangeOk) {
        return jsonError(416, "range_not_satisfiable", "Invalid Range header", req.requestId);
    }

    HttpResponsePayload resp;
    resp.status = (range.hasRange ? 206 : 200);
    resp.statusText = range.hasRange ? QByteArray("Partial Content") : QByteArray("OK");
    resp.contentType = QByteArray("video/x-matroska");
    resp.headers.append(qMakePair(QByteArray("Accept-Ranges"), QByteArray("bytes")));

    qint64 start = 0;
    qint64 end = totalSize > 0 ? totalSize - 1 : -1;
    if (range.hasRange) {
        start = range.start;
        end = range.end;
        const QByteArray contentRange = QByteArray("bytes ")
                                            + QByteArray::number(start) + "-"
                                            + QByteArray::number(end) + "/"
                                            + QByteArray::number(totalSize);
        resp.headers.append(qMakePair(QByteArray("Content-Range"), contentRange));
    }

    const qint64 length = (end >= start) ? (end - start + 1) : 0;

    if (isHead) {
        resp.explicitContentLength = length;
        return resp;
    }

    QFile f(seg.filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        return jsonError(500, "segment_open_failed", "Failed to open segment", req.requestId);
    }

    if (start > 0 && !f.seek(start)) {
        return jsonError(416, "range_not_satisfiable", "Failed to seek to requested range", req.requestId);
    }

    resp.body = f.read(length);
    resp.explicitContentLength = resp.body.size();
    if (resp.body.size() != length) {
        qWarning() << "[NodeApiServer] Short read while serving segment" << segmentId
                   << "expected" << length << "bytes got" << resp.body.size();
    }

    return resp;
}

namespace {
QByteArray statusTextFor(int status)
{
    switch (status) {
    case 200: return QByteArray("OK");
    case 206: return QByteArray("Partial Content");
    case 400: return QByteArray("Bad Request");
    case 401: return QByteArray("Unauthorized");
    case 404: return QByteArray("Not Found");
    case 416: return QByteArray("Range Not Satisfiable");
    case 500: return QByteArray("Internal Server Error");
    default: return QByteArray("Error");
    }
}
} // namespace

NodeApiServer::HttpResponsePayload NodeApiServer::jsonPayload(int status,
                                                             const QByteArray& statusText,
                                                             const QJsonValue& value,
                                                             const QString& requestId) const
{
    HttpResponsePayload resp;
    resp.status = status;
    resp.statusText = statusText.isEmpty() ? statusTextFor(status) : statusText;
    QJsonObject wrapped;
    if (value.isObject()) {
        wrapped = value.toObject();
    } else {
        wrapped["data"] = value;
    }
    wrapped["request_id"] = requestId;
    resp.body = QJsonDocument(wrapped).toJson(QJsonDocument::Compact);
    resp.contentType = "application/json";
    return resp;
}

NodeApiServer::HttpResponsePayload NodeApiServer::jsonError(int status,
                                                            const QString& code,
                                                            const QString& message,
                                                            const QString& requestId) const
{
    QJsonObject err{
        {"error", QJsonObject{
             {"code", code},
             {"message", message}
         }}
    };
    return jsonPayload(status, statusTextFor(status), err, requestId);
}

QByteArray NodeApiServer::serializeResponse(const HttpResponsePayload& resp,
                                            const QString& requestId) const
{
    QByteArray data;
    data.append("HTTP/1.1 ")
        .append(QByteArray::number(resp.status))
        .append(' ')
        .append(resp.statusText.isEmpty() ? QByteArray("OK") : resp.statusText)
        .append("\r\n");
    const qint64 bodyLen = resp.explicitContentLength >= 0
        ? resp.explicitContentLength
        : resp.body.size();
    data.append("Content-Type: ").append(resp.contentType).append("\r\n");
    data.append("Content-Length: ").append(QByteArray::number(bodyLen)).append("\r\n");
    data.append("Connection: close\r\n");
    data.append("X-Request-Id: ").append(requestId.toUtf8()).append("\r\n");
    for (const auto& header : resp.headers) {
        data.append(header.first).append(": ").append(header.second).append("\r\n");
    }
    data.append("\r\n");
    data.append(resp.body);
    return data;
}
