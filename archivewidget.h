#ifndef ARCHIVEWIDGET_H
#define ARCHIVEWIDGET_H

#include <QWidget>
#include <QDate>
#include <QString>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QThread>
#include "videoplayerwindow.h"
#include "cameramanager.h"
#include "archivemanager.h"
#include <QMovie>
#include "db_reader.h"
struct VideoMetadata {
    QString filePath;
    QString displayText;
    QDateTime timestamp;
    double duration;
};
class ArchiveWidget : public QWidget {
    Q_OBJECT
public:
    //  accepts both CameraManager and ArchiveManager pointers.
    explicit ArchiveWidget(CameraManager* camManager, ArchiveManager* archiveManager, QWidget *parent = nullptr);
    void refreshBackupDates();

signals:
    void dateSelected(const QDate &date);

private slots:
    void loadVideoFiles(const QDate &date);
    void showThumbnail(QListWidgetItem *item);
    void openVideoPlayer(QListWidgetItem *item);
    void refreshFromDb();  // new: trigger DB fetch
    void onRecentSegments(const QVector<RecentSegment>& segs); // new: populate UI

private:
    QMovie *buttonSpinner;
    QMovie *loadingMovie;
    QString archiveDir;
    QPushButton *refreshButton;
    QListWidget *videoListWidget;
    QLabel *thumbnailLabel;
    QLabel *videoDetailsLabel;
    QString selectedVideoPath;
    double getVideoDurationSeconds(const QString &videoPath);
    QString formatFileName(const QString &rawFileName, const QString &absolutePath);
    QString formatFileName(const QString &rawFileName, double durationSeconds);
    QString getVideoDuration(const QString &videoPath);
    void generateThumbnail(const QString &videoPath);
    static QString humanDurFromMs(qint64 ms); // new: duration formatting
    CameraManager* cameraManager;
    ArchiveManager* archiveManager;  // New pointer for ArchiveManager
    // FS scan no longer used on hot path; keep only if needed elsewhere
    QList<VideoMetadata> extractVideoMetadata(const QString& archiveDirPath);

    // DB members
    QThread*  dbThread_ = nullptr;
    DbReader* dbReader_ = nullptr;
    QString   dbPath_;
};

#endif // ARCHIVEWIDGET_H
