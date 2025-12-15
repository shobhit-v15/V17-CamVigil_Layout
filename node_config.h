#pragma once

#include <QString>

struct NodeConfig {
    QString nodeId;
    QString buildingId;
    QString apiBindHost;
    quint16 apiBindPort;
    QString apiToken;
    quint16 rtspProxyPort;
};

class NodeConfigService {
public:
    explicit NodeConfigService(const QString& configPath);
    NodeConfig load() const;

private:
    QString m_configPath;
};
