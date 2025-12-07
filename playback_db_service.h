#pragma once
#include <QObject>
#include <QThread>
#include <QPointer>
#include "db_reader.h"

class PlaybackDbService : public QObject {
    Q_OBJECT
public:
    static PlaybackDbService* instance();                 // process-wide
    DbReader* reader() const { return db_; }              // thread-affine (db thread)

    // Idempotent; safe to call repeatedly, reuses same connection if path unchanged
    void ensureOpened(const QString& dbPath);

signals:
    void log(QString msg);

private:
    explicit PlaybackDbService(QObject* parent=nullptr);
    ~PlaybackDbService();
    Q_DISABLE_COPY(PlaybackDbService)

    QThread*  thread_  = nullptr;
    DbReader* db_      = nullptr;
    QString   currentPath_;
    bool      started_ = false;
};
