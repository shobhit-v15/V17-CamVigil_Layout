#pragma once
#include <QString>

struct CamHWProfile; // from camerastreams.h

namespace hik {

// Read the current channel name from /ISAPI/System/Video/inputs/channels/1
bool getOsdTitle(const CamHWProfile& cam, QString* name, QString* err=nullptr);

// Set the channel name by GET->edit->PUT on /ISAPI/System/Video/inputs/channels/1
bool setOsdTitle(const CamHWProfile& cam, const QString& name, QString* err=nullptr);

} // namespace hik
