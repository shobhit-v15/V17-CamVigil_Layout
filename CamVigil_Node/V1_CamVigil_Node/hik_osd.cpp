#include "hik_osd.h"
#include "camerastreams.h"

#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QAuthenticator>
#include <QEventLoop>
#include <QRegularExpression>
#include <QDebug>

namespace {

// Extract host and credentials from RTSP URL
bool extractFromRtsp(const QString& rtsp, QString& ip, QString& user, QString& pass) {
    const QUrl u(rtsp);
    if (!u.isValid()) return false;
    ip   = u.host();
    user = u.userName(QUrl::FullyDecoded);
    pass = u.password(QUrl::FullyDecoded);
    return !(ip.isEmpty() || user.isEmpty() || pass.isEmpty());
}

QByteArray httpGet(const QUrl& url, const QString& user, const QString& pass, QString* err) {
    QNetworkAccessManager nam;
    QObject::connect(&nam, &QNetworkAccessManager::authenticationRequired,
                     [&](QNetworkReply*, QAuthenticator* a){
                         a->setUser(user);
                         a->setPassword(pass);
                     });
    QNetworkRequest req(url);
    QEventLoop loop;
    QNetworkReply* r = nam.get(req);
    QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (r->error() != QNetworkReply::NoError) {
        if (err) *err = r->errorString();
        r->deleteLater();
        return {};
    }
    const QByteArray body = r->readAll();
    r->deleteLater();
    return body;
}

bool httpPut(const QUrl& url, const QByteArray& body, const QString& user, const QString& pass, QString* err) {
    QNetworkAccessManager nam;
    QObject::connect(&nam, &QNetworkAccessManager::authenticationRequired,
                     [&](QNetworkReply*, QAuthenticator* a){
                         a->setUser(user);
                         a->setPassword(pass);
                     });
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml");
    QEventLoop loop;
    QNetworkReply* r = nam.sendCustomRequest(req, "PUT", body);
    QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const bool ok = (r->error() == QNetworkReply::NoError);
    if (!ok && err) *err = r->errorString();
    r->deleteLater();
    return ok;
}

} // namespace

namespace hik {

bool getOsdTitle(const CamHWProfile& cam, QString* name, QString* err) {
    QString ip, user, pass;
    if (!extractFromRtsp(QString::fromStdString(cam.url), ip, user, pass)) {
        if (err) *err = "bad rtsp";
        return false;
    }

    const QUrl url(QString("http://%1/ISAPI/System/Video/inputs/channels/1").arg(ip));
    const QByteArray body = httpGet(url, user, pass, err);
    if (body.isEmpty()) return false;

    // Pull first <name>...</name>
    const QString xml = QString::fromUtf8(body);
    static const QRegularExpression re(R"(<name>(.*?)</name>)", QRegularExpression::DotMatchesEverythingOption);
    const auto m = re.match(xml);
    if (!m.hasMatch()) {
        if (err) *err = "name tag not found";
        return false;
    }
    if (name) *name = m.captured(1).trimmed();
    return true;
}

bool setOsdTitle(const CamHWProfile& cam, const QString& newName, QString* err) {
    QString ip, user, pass;
    if (!extractFromRtsp(QString::fromStdString(cam.url), ip, user, pass)) {
        if (err) *err = "bad rtsp";
        return false;
    }

    // 1) GET current channel XML so we preserve required fields (id, inputPort, videoFormat, etc.)
    const QUrl url(QString("http://%1/ISAPI/System/Video/inputs/channels/1").arg(ip));
    QByteArray body = httpGet(url, user, pass, err);
    if (body.isEmpty()) return false;

    QString xml = QString::fromUtf8(body);

    // 2) Replace <name>...</name> content (first occurrence)
    static const QRegularExpression re(R"(<name>.*?</name>)", QRegularExpression::DotMatchesEverythingOption);
    if (!re.match(xml).hasMatch()) {
        if (err) *err = "name tag not found";
        return false;
    }
    xml.replace(re, QString("<name>%1</name>").arg(newName));

    // 3) PUT back full document
    const bool ok = httpPut(url, xml.toUtf8(), user, pass, err);
    if (!ok) {
        qWarning() << "[OSD] PUT failed:" << (err ? *err : QString());
        return false;
    }
    qInfo() << "[OSD] channel name set to" << newName << "@" << ip;
    return true;
}

} // namespace hik
