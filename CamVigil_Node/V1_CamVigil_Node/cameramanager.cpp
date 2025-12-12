#include "cameramanager.h"
#include "camerastreams.h"
#include <QJsonArray>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QtConcurrent>

#include "hik_osd.h"

CameraManager::CameraManager() {
    // cameras.json in current working dir
    configFilePath = QDir::currentPath().toStdString() + "/cameras.json";
    // Populate CameraStreams from JSON if empty
    CameraStreams::loadFromJson();
}

std::vector<CamHWProfile> CameraManager::getCameraProfiles() {
    return CameraStreams::getCameraUrls();
}

std::vector<std::string> CameraManager::getCameraUrls() {
    std::vector<CamHWProfile> profiles = getCameraProfiles();
    std::vector<std::string> urls;
    urls.reserve(profiles.size());
    for (const auto& profile : profiles) urls.push_back(profile.url);
    return urls;
}

void CameraManager::renameCamera(int index, const std::string& newName) {
    CameraStreams::setCameraDisplayName(index, newName);
    saveCameraNames();
}

bool CameraManager::renameAndPush(int index, const QString& newName, QString* err){
    auto profiles = getCameraProfiles();
    if (index < 0 || index >= static_cast<int>(profiles.size())) {
        if (err) *err = "invalid index";
        return false;
    }
    // Push to device first
    if (!hik::setOsdTitle(profiles[index], newName, err)) return false;

    // Persist desired state after device accepts
    CameraStreams::setCameraDisplayName(index, newName.toStdString());
    saveCameraNames();
    return true;
}

void CameraManager::syncOsdToJsonAllAsync(){
    const auto profiles = getCameraProfiles();
    QtConcurrent::run([profiles]{
        for (size_t i = 0; i < profiles.size(); ++i) {
            QString err;
            const QString want = QString::fromStdString(profiles[i].displayName);
            const bool ok = hik::setOsdTitle(profiles[i], want, &err);
            if (!ok) {
                qWarning() << "[OSD] PUT failed for cam" << i << ":" << err;
            } else {
                qInfo() << "[OSD] set name for cam" << i << "->" << want;
            }
        }
    });
}

void CameraManager::saveCameraNames() {
    QJsonObject json;
    QJsonArray camerasArray;

    std::vector<CamHWProfile> profiles = getCameraProfiles();
    for (const auto& profile : profiles) {
        QJsonObject camObj;
        camObj["url"] = QString::fromStdString(profile.url);
        camObj["suburl"] = QString::fromStdString(profile.suburl);
        camObj["name"] = QString::fromStdString(profile.displayName);
        camerasArray.append(camObj);
    }
    json["cameras"] = camerasArray;

    QFile file(QString::fromStdString(configFilePath));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(json).toJson());
        file.close();
    } else {
        qDebug() << "Unable to open file for writing:" << QString::fromStdString(configFilePath);
    }
}

void CameraManager::loadCameraNames() {
    // Optional: not used in this snippet. Keep stub.
}
