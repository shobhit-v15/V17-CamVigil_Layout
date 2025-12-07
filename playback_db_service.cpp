#include "playback_db_service.h"
#include <QCoreApplication>
#include <QFileInfo>

PlaybackDbService* PlaybackDbService::instance() {
    static PlaybackDbService* s = new PlaybackDbService(qApp);
    return s;
}

PlaybackDbService::PlaybackDbService(QObject* parent) : QObject(parent) {
    thread_ = new QThread(this);
    db_     = new DbReader;               // lives in db thread
    db_->moveToThread(thread_);

    connect(thread_, &QThread::finished, db_, &QObject::deleteLater);
    connect(db_, &DbReader::error, this, [this](const QString& e){ emit log("[DB] " + e); });

    thread_->start();
    started_ = true;
}

PlaybackDbService::~PlaybackDbService() {
    if (started_) {
        // Don’t removeDatabase under active queries — just close thread at app exit.
        // If you really want a clean PRAGMA close, you can invoke shutdown() here.
        thread_->quit();
        thread_->wait();
    }
}

void PlaybackDbService::ensureOpened(const QString& dbPath) {
    if (!db_) return;
    if (dbPath.isEmpty() || !QFileInfo::exists(dbPath)) return;

    if (currentPath_ == dbPath) return;   // already using this path

    currentPath_ = dbPath;
    // Open on the DB thread, non-blocking
    QMetaObject::invokeMethod(db_, "openAt", Qt::QueuedConnection,
                              Q_ARG(QString, dbPath));
}
