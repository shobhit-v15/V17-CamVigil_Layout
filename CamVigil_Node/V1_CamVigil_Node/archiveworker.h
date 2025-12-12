#ifndef ARCHIVEWORKER_H
#define ARCHIVEWORKER_H

#include <QObject>
#include <QThread>
#include <QString>
#include <atomic>
#include <QDateTime>
#include <QMutex>
#include <QWaitCondition>
#include <string>
#include <gst/gst.h>

class ArchiveWorker : public QThread {
    Q_OBJECT
public:
    ArchiveWorker(const std::string& cameraUrl,
                  int cameraIndex,
                  const QString& archiveDir,
                  int defaultDurationSec,
                  const QDateTime& masterStart);
    void run() override;
    void stop();

public slots:
    void updateSegmentDuration(int seconds);

signals:
    void recordingError(const std::string& error);
    void segmentFinalized();
    void segmentOpened(int camIndex, QString filePath, qint64 startUtcNs);     //meta data to store in db
    void segmentClosed(int camIndex, QString filePath, qint64 endUtcNs, qint64 durationMs);//meta data to store in db

private:
    std::string cameraUrl;
    int cameraIndex;
    QString archiveDir;
    std::atomic<bool> running;
    std::atomic<int> segmentDurationSec;
    std::atomic<bool> pendingDurationUpdate;
    int nextSegmentDuration;
    QDateTime masterStart;
    GstElement *pipeline;

    QMutex updateMutex;
    QWaitCondition updateCondition;

    QDateTime lastSegmentTimestamp;

    void createPipeline();
    void cleanupPipeline();
    QString generateSegmentPrefix() const;

    static gchar* formatLocationFullCallback(GstElement* splitmux, guint fragment_id, GstSample* sample, gpointer user_data);
    static void onBusMessage(GstBus* bus, GstMessage* message, gpointer user_data);
    QString currentFilePath;
    QDateTime currentStartTimeUtc;
    QMutex curMutex;
};

#endif // ARCHIVEWORKER_H
