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

    return cfg;
}
