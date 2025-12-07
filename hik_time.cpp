#include "hik_time.h"
#include "camerastreams.h"    // for CamHWProfile
#include <QDateTime>
#include <QTimeZone>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QAuthenticator>
#include <QEventLoop>
#include <QtConcurrent>
#include <QDebug>

namespace hik {

QString tzFromSystem() {                              //Reads the current local time
    const auto now = QDateTime::currentDateTime();
    int secs = QTimeZone::systemTimeZone().offsetFromUtc(now);
    const QChar sign = (secs >= 0) ? '-' : '+';
    secs = std::abs(secs);
    return QString("CST%1%2:%3:00")
            .arg(sign)
            .arg(secs/3600,2,10,QChar('0'))
            .arg((secs%3600)/60,2,10,QChar('0'));
}

static bool httpPutXml(const QUrl& url, const QByteArray& xml, const QString& user,  // Prepares QNetworkAccessRequest sends a HTTP PUT request
                       const QString& pass, QString* err) {
    QNetworkAccessManager nam;
    QObject::connect(&nam, &QNetworkAccessManager::authenticationRequired,
                     [&](QNetworkReply*, QAuthenticator* a){ a->setUser(user); a->setPassword(pass); });
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml");
    QEventLoop loop;                                                          // The event loop until the reply finishes
    QNetworkReply* r = nam.sendCustomRequest(req, "PUT", xml);
    QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const bool ok = (r->error() == QNetworkReply::NoError);
    if (!ok && err) *err = r->errorString();
    r->deleteLater();
    return ok;
}
static QString offsetStr(const QDateTime& now) {    //appends the timezone to a time string for the camera
    int secs = QTimeZone::systemTimeZone().offsetFromUtc(now);
    QChar sign = (secs >= 0) ? '+' : '-';
    secs = std::abs(secs);
    return QString("%1%2:%3").arg(sign)
                             .arg(secs/3600,2,10,QChar('0'))
                             .arg((secs%3600)/60,2,10,QChar('0'));
}
bool syncToHostNow(const QString& ip, const QString& user, const QString& pass,   // builds the xml payload for each camera
                        const QString& hikTz, QString* err) {
    const QDateTime nowLocal = QDateTime::currentDateTime();
    const QString iso = nowLocal.toString("yyyy-MM-dd'T'HH:mm:ss") + offsetStr(nowLocal);
    const QByteArray xml = QString(
        "<?xml version=\"1.0\"?>"
        "<Time><timeMode>manual</timeMode>"
        "<timeZone>%1</timeZone>"
        "<localTime>%2</localTime></Time>"
    ).arg(hikTz, iso).toUtf8();

    return httpPutXml(QUrl(QString("http://%1/ISAPI/System/time").arg(ip)),
                      xml, user, pass, err);
}

static bool extractFromRtsp(const QString& rtsp, QString& ip, QString& user, QString& pass) {
    const QUrl u(rtsp);
    if (!u.isValid()) return false;
    ip   = u.host();
    user = u.userName(QUrl::FullyDecoded);
    pass = u.password(QUrl::FullyDecoded);
    return !(ip.isEmpty() || user.isEmpty() || pass.isEmpty());
}

bool syncToHostNow(const CamHWProfile& cam, QString* err) {  // extract the ips using the above {extractFromRtsp} funcion the rtsp url from our .json file
    QString ip, user, pass;
    if (!extractFromRtsp(QString::fromStdString(cam.url), ip, user, pass))
        return false;
    return syncToHostNow(ip, user, pass, tzFromSystem(), err);
}

void syncAllAsync(const std::vector<CamHWProfile>& cams) { // calls syncToHostNow function
    for (const auto& c : cams) {
        QtConcurrent::run([c]() {
            QString err;
            const bool ok = syncToHostNow(c, &err);
            qDebug() << "[HikSync]" << QString::fromStdString(c.displayName)
                     << (ok ? "OK" : "FAIL")
                     << (ok ? "" : err);
        });
    }
}

} // namespace hik
