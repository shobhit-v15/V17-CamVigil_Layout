// TODO: Decide final location for node_config.json (likely alongside cameras.json or camvigil.sqlite).
#include "node_config.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

NodeConfigService::NodeConfigService(const QString& configPath)
    : m_configPath(configPath)
{
}

NodeConfig NodeConfigService::load() const
{
    NodeConfig cfg;
    // Reasonable defaults
    cfg.nodeId = "default-node";
    cfg.buildingId = "default-building";
    cfg.apiBindHost = "0.0.0.0";
    cfg.apiBindPort = 8080;
    cfg.apiToken = "change-me";
    cfg.rtspProxyPort = 8554;
    cfg.advertiseHost.clear();
    cfg.advertiseRtspPort = cfg.rtspProxyPort;
    cfg.lowLatency = false;
    cfg.rtspSourceLatencyMs = 150;
    cfg.rtspForceTcp = true;
    cfg.enableRtpJitterBuffer = false;
    cfg.rtpJitterBufferLatencyMs = 50;

    QFile f(m_configPath);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "[NodeConfigService] Could not open" << m_configPath << "- using defaults.";
        return cfg;
    }

    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        qWarning() << "[NodeConfigService] Invalid JSON in" << m_configPath << "- using defaults.";
        return cfg;
    }
    const auto obj = doc.object();

    cfg.nodeId = obj.value("node_id").toString(cfg.nodeId);
    cfg.buildingId = obj.value("building_id").toString(cfg.buildingId);
    cfg.apiBindHost = obj.value("api_bind_host").toString(cfg.apiBindHost);
    cfg.apiBindPort = static_cast<quint16>(obj.value("api_bind_port").toInt(cfg.apiBindPort));
    cfg.apiToken = obj.value("api_token").toString(cfg.apiToken);
    cfg.rtspProxyPort = static_cast<quint16>(obj.value("rtsp_proxy_port").toInt(cfg.rtspProxyPort));
    cfg.advertiseHost = obj.value("advertise_host").toString(cfg.advertiseHost);
    cfg.advertiseRtspPort = static_cast<quint16>(obj.value("advertise_rtsp_port").toInt(cfg.rtspProxyPort));
    cfg.lowLatency = obj.value("low_latency").toBool(cfg.lowLatency);
    cfg.rtspSourceLatencyMs = obj.value("rtsp_source_latency_ms").toInt(cfg.rtspSourceLatencyMs);
    cfg.rtspForceTcp = obj.value("rtsp_force_tcp").toBool(cfg.rtspForceTcp);
    cfg.enableRtpJitterBuffer = obj.value("rtp_jitter_buffer").toBool(cfg.enableRtpJitterBuffer);
    cfg.rtpJitterBufferLatencyMs = obj.value("rtp_jitter_latency_ms").toInt(cfg.rtpJitterBufferLatencyMs);
    if (cfg.advertiseRtspPort == 0) cfg.advertiseRtspPort = cfg.rtspProxyPort;
    if (cfg.rtspSourceLatencyMs <= 0) cfg.rtspSourceLatencyMs = 50;
    if (cfg.rtpJitterBufferLatencyMs <= 0) cfg.rtpJitterBufferLatencyMs = 25;

    return cfg;
}
