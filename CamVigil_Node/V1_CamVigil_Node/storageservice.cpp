#include "storageservice.h"
#include <QStorageInfo>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QDBusConnection>

static StorageService* g_ss = nullptr;

StorageService* StorageService::instance(){
    if (!g_ss) g_ss = new StorageService();
    return g_ss;
}

StorageService::StorageService(QObject* parent)
    : QObject(parent)
{
    setupDbus_();
    // Fallback poll every 5s to catch mounts if DBus not available.
    connect(&pollTimer_, &QTimer::timeout, this, &StorageService::rescanMounted_);
    pollTimer_.start(5000);
    rescanMounted_();
}

qint64 StorageService::freeBytes() const {
    if (externalRoot_.isEmpty()) return 0;
    QStorageInfo si(externalRoot_);
    if (!si.isValid() || !si.isReady()) return 0;
    return si.bytesAvailable();
}

void StorageService::setupDbus_(){
    // Subscribe to com.idonial.automount if present
    bool ok1 = QDBusConnection::systemBus().connect(
        "com.idonial.automount",
        "/com/idonial/automount",
        "com.idonial.automount",
        "Mounted",
        this, SLOT(onUsbMounted(QString,QString))
    );
    bool ok2 = QDBusConnection::systemBus().connect(
        "com.idonial.automount",
        "/com/idonial/automount",
        "com.idonial.automount",
        "Unmounted",
        this, SLOT(onUsbUnmounted(QString,QString))
    );
    qInfo() << "[StorageService] DBus subscriptions Mounted/Unmounted:" << ok1 << ok2;
}

bool StorageService::isKernelRemovable_(const QStorageInfo& storage){
    QString devicePath = QString::fromUtf8(storage.device());
    QString devFileName = QFileInfo(devicePath).fileName();
    devFileName.remove(QRegExp("\\d+$"));
    QFile f(QString("/sys/block/%1/removable").arg(devFileName));
    if (!f.open(QIODevice::ReadOnly)) return false;
    return f.readAll().trimmed() == "1";
}

void StorageService::rescanMounted_(){
    QString found;
    const auto vols = QStorageInfo::mountedVolumes();
    for (const auto& s : vols) {
        if (!s.isValid() || !s.isReady() || s.isReadOnly()) continue;
        const QString root = s.rootPath();
        if ((root.startsWith("/media") || root.startsWith("/run/media")) && isKernelRemovable_(s)) {
            found = root;
            break;
        }
    }
    if (found != externalRoot_) {
        externalRoot_ = found;
        qInfo() << "[StorageService] externalRoot =" << externalRoot_;
        emit externalPresentChanged(!externalRoot_.isEmpty());
    }
}

void StorageService::onUsbMounted(QString, QString path){
    if (externalRoot_ == path) return;
    externalRoot_ = path;
    qInfo() << "[StorageService] DBus mounted ->" << externalRoot_;
    emit externalPresentChanged(true);
}

void StorageService::onUsbUnmounted(QString, QString path){
    if (externalRoot_.isEmpty()) return;
    if (externalRoot_.startsWith(path)) {
        qInfo() << "[StorageService] DBus unmounted ->" << path;
        externalRoot_.clear();
        emit externalPresentChanged(false);
        emit aboutToUnmount(path);
    }
}

void StorageService::refresh(){ rescanMounted_(); }
