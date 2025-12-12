#include "camerastreams.h"

std::vector<CamHWProfile> CameraStreams::cameraUrls = {};
std::mutex CameraStreams::cameraMutex;

std::vector<CamHWProfile> CameraStreams::getCameraUrls() {
    std::lock_guard<std::mutex> lock(cameraMutex);
    // If the vector is empty, try to load from JSON.
    if (cameraUrls.empty()) {
        loadFromJson();
    }
    return cameraUrls;
}

void CameraStreams::addCameraUrl(const std::string& rtspUrl) {
    std::lock_guard<std::mutex> lock(cameraMutex);
    // Here we add a camera with empty suburl and displayName as default.
    cameraUrls.emplace_back(rtspUrl, "", "");
}

void CameraStreams::setCameraDisplayName(int index, const std::string& name) {
    std::lock_guard<std::mutex> lock(cameraMutex);
    if (index >= 0 && index < static_cast<int>(cameraUrls.size())) {
        cameraUrls[index].displayName = name;
    }
}

void CameraStreams::loadFromJson() {
    std::lock_guard<std::mutex> lock(cameraMutex);
    QString configFilePath = QDir::currentPath() + "/cameras.json";
    QFile file(configFilePath);

    if (!file.exists()) {
        qDebug() << "cameras.json not found. Application will proceed with an empty camera list.";
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Failed to open cameras.json for reading.";
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        qDebug() << "Invalid JSON format in cameras.json";
        return;
    }

    QJsonObject json = doc.object();
    if (!json.contains("cameras") || !json["cameras"].isArray()) {
        qDebug() << "No 'cameras' array found in JSON.";
        return;
    }

    QJsonArray camerasArray = json["cameras"].toArray();
    // Create a set of URLs already present to avoid duplicates.
    std::set<std::string> existingUrls;
    for (const auto& profile : cameraUrls) {
        existingUrls.insert(profile.url);
    }

    // Clear previous profiles to reload completely.
    cameraUrls.clear();

    for (const QJsonValue &value : camerasArray) {
        if (!value.isObject())
            continue;
        QJsonObject camObj = value.toObject();
        if (!camObj.contains("url") || !camObj.contains("suburl") || !camObj.contains("name"))
            continue;
        std::string url = camObj["url"].toString().toStdString();
        std::string suburl = camObj["suburl"].toString().toStdString();
        std::string name = camObj["name"].toString().toStdString();
        if (existingUrls.find(url) == existingUrls.end()) {
            cameraUrls.emplace_back(url, suburl, name);
            existingUrls.insert(url);
            qDebug() << "Loaded Camera:" << QString::fromStdString(name)
                     << "->" << QString::fromStdString(url)
                     << "Substream:" << QString::fromStdString(suburl);
        }
    }
}
