#ifndef CAMERASTREAMS_H
#define CAMERASTREAMS_H

#include <string>
#include <vector>
#include <mutex>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QDebug>
#include <set>

class CamHWProfile {
public:
    std::string url;       // Main URL for archiving (high quality)
    std::string suburl;    // Sub URL for streaming (low quality)
    std::string displayName;


    CamHWProfile(const std::string& rtspUrl, const std::string& subUrl, const std::string& name = "")
        : url(rtspUrl), suburl(subUrl), displayName(name) {}

    // Default constructor.
    CamHWProfile() {}
};

class CameraStreams {
public:
    // Returns the current camera profiles.
    // If the vector is empty, it loads from cameras.json.
    static std::vector<CamHWProfile> getCameraUrls();
    static void addCameraUrl(const std::string& rtspUrl);
    static void setCameraDisplayName(int index, const std::string& name);

    // Loads camera profiles from the JSON file if available,
    // ensuring that duplicate cameras are not added.
    static void loadFromJson();

private:
    static std::vector<CamHWProfile> cameraUrls;
    static std::mutex cameraMutex;
};

#endif // CAMERASTREAMS_H
