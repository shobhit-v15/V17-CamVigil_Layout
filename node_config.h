#pragma once

#include <QString>

struct NodeConfig {
    QString nodeId;
    QString buildingId;
    QString apiBindHost;
    quint16 apiBindPort;
    QString apiToken;
    quint16 rtspProxyPort;
    QString advertiseHost;
    quint16 advertiseRtspPort;
    bool lowLatency = false;
    int rtspSourceLatencyMs = 150;
    bool rtspForceTcp = true;
    bool enableRtpJitterBuffer = false;
    int rtpJitterBufferLatencyMs = 50;
};

class NodeConfigService {
public:
    explicit NodeConfigService(const QString& configPath);
    NodeConfig load() const;

private:
    QString m_configPath;
};
