#pragma once
#include <QObject>
#include <QVector>
#include <QStringList>
#include <QString>
#include <atomic>
#include <QProcess>
#include <QTemporaryDir>
#include "playback_segment_index.h" // for FileSeg

struct ExportOptions {
    QString ffmpegPath = "ffmpeg";
    QString outDir;              // externalRoot()/CamVigilExports for Save
    QString baseName;            // e.g., "CamVigil_YYYY-MM-DD"
    bool precise = false;        // false => -c copy, true => re-encode
    QString vcodec = "libx264";
    QString preset = "veryfast";
    int crf = 18;
    bool copyAudio = true;

    qint64 minFreeBytes = 512ll * 1024 * 1024; // 512 MB guardrail for Save
};

struct ClipPart {
    QString path;
    qint64  inStartNs;
    qint64  inEndNs;
    bool    wholeFile;
};

class PlaybackExporter final : public QObject {
    Q_OBJECT
public:
    explicit PlaybackExporter(QObject* parent=nullptr);

    void setPlaylist(const QVector<PlaybackSegmentIndex::FileSeg>& playlist, qint64 dayStartNs);
    void setSelection(qint64 selStartNs, qint64 selEndNs); // ns from midnight
    void setOptions(const ExportOptions& opts);

public slots:
    // Stage 1: cut/concat to a temp file in an internal fast location
    void startPrepare();

    // Stage 2: copy prepared clip to external outDir (opts_.outDir)
    void saveToExternal();

    void cancel();

signals:
    void progress(double pct);       // 0..100 for current phase
    void log(QString line);
    void prepared(QString tempPath); // Stage 1 done
    void saved(QString outPath);     // Stage 2 done
    void error(QString msg);
    void started();                  // phase start

private:
    QVector<PlaybackSegmentIndex::FileSeg> playlist_;
    qint64 dayStartNs_{0};
    qint64 selStartNs_{0};
    qint64 selEndNs_{0};
    ExportOptions opts_;
    std::atomic_bool abort_{false};

    // Persistent between phases
    QString preparedPath_;

    QVector<ClipPart> computeParts_() const;
    QString uniqueOutBaseName_() const;         // basename without dir
    bool runFfmpeg_(const QStringList& args, QByteArray* errOut);
    bool buildInputs_(const QVector<ClipPart>& parts,
                      const QString& tempDir,
                      QStringList* inputPaths);
    bool writeConcatList_(const QStringList& inputPaths, const QString& listPath);
    bool concat_(const QString& listPath, const QString& outPath);

    qint64 estimateBytes_(const QVector<ClipPart>& parts) const;
};
