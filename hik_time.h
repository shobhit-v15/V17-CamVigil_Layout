#pragma once
#include <QString>
#include <vector>

struct CamHWProfile; // forward

namespace hik {

// Convert host TZ to Hik format, e.g., IST -> "CST-5:30:00"
QString tzFromSystem();

// One-shot manual sync: set camera time = host UTC now, keep TZ.
bool syncToHostNow(const QString& ip, const QString& user, const QString& pass,
                   const QString& hikTz, QString* err = nullptr);

// Convenience: extract ip/user/pass from RTSP in CamHWProfile.url and sync.
// Returns false if creds missing or request failed.
bool syncToHostNow(const CamHWProfile& cam, QString* err = nullptr);

// Fire-and-forget for a list of cams. Logs to qDebug.
void syncAllAsync(const std::vector<CamHWProfile>& cams);

} // namespace hik
