#ifndef CAMERAMANAGER_H
#define CAMERAMANAGER_H

#include "camerastreams.h"
#include <vector>
#include <string>
#include <QDir>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QJsonArray>

class CameraManager {
public:
    CameraManager();

    std::vector<CamHWProfile> getCameraProfiles();
    std::vector<std::string> getCameraUrls();

    // Persistence
    void renameCamera(int index, const std::string& newName);
    void saveCameraNames();
    void loadCameraNames();

    // OSD sync
    void syncOsdToJsonAllAsync();
    bool renameAndPush(int index, const QString& newName, QString* err=nullptr);

private:
    std::string configFilePath;
};

#endif // CAMERAMANAGER_H
