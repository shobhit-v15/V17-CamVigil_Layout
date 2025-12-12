#include "playback_exporter.h"
#include "storageservice.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDate>
#include <QTextStream>
#include <QDateTime>
#include <QStorageInfo>

static inline double secFromNs(qint64 ns){ return double(ns)/1e9; }

PlaybackExporter::PlaybackExporter(QObject* p): QObject(p) {}

void PlaybackExporter::setPlaylist(const QVector<PlaybackSegmentIndex::FileSeg>& pl, qint64 dayStartNs){
    playlist_ = pl; dayStartNs_ = dayStartNs;
}
void PlaybackExporter::setSelection(qint64 s, qint64 e){
    selStartNs_ = s; selEndNs_ = e;
}
void PlaybackExporter::setOptions(const ExportOptions& o){ opts_ = o; }

void PlaybackExporter::cancel(){ abort_.store(true); }

// ----------------- Phase 1: Prepare (clip to internal temp) -----------------
void PlaybackExporter::startPrepare(){
    emit started();
    emit log("[Export] prepare start");
    preparedPath_.clear();

    if (selEndNs_ <= selStartNs_) { emit error("Invalid selection"); return; }
    if (playlist_.isEmpty()) { emit error("No playlist"); return; }

    // Build parts plan
    const auto parts = computeParts_();
    if (parts.isEmpty()) { emit error("Selection overlaps no files"); return; }

    // Work temp dir
    QTemporaryDir tmp;
    if (!tmp.isValid()) { emit error("Temp directory creation failed"); return; }
    const QString tempDir = tmp.path();
    emit log(QString("[Export] tmp: %1").arg(tempDir));

    // Prepare inputs
    QStringList inputPaths;
    if (!buildInputs_(parts, tempDir, &inputPaths)) { emit error("Prepare inputs failed"); return; }
    if (abort_.load()) { emit error("Canceled"); return; }

    // Concat list (in temp)
    const QString listPath = QDir(tempDir).filePath("concat_inputs.txt");
    if (!writeConcatList_(inputPaths, listPath)) { emit error("Concat list write failed"); return; }

    // Concat to final temp file (basename derived)
    const QString baseName = uniqueOutBaseName_();
    const QString tmpOut   = QDir(tempDir).filePath(baseName);
    QByteArray err;
    if (!concat_(listPath, tmpOut)) { emit error("Concat failed"); return; }
    if (abort_.load()) { emit error("Canceled"); return; }

    // Persist prepared path by copying to a durable temp under /tmp (so QTemporaryDir cleanup doesn’t remove it)
    const QString durableTmp = QDir::temp().filePath(baseName);
    QFile::remove(durableTmp);
    if (!QFile::copy(tmpOut, durableTmp)) {
        emit error("Failed to persist prepared clip");
        return;
    }

    preparedPath_ = durableTmp;
    emit progress(100.0);
    emit log(QString("[Export] prepared -> %1").arg(preparedPath_));
    emit prepared(preparedPath_);
}

// ----------------- Phase 2: Save (copy to external outDir) -----------------
void PlaybackExporter::saveToExternal(){
    emit started();
    emit log("[Export] save start");

    if (preparedPath_.isEmpty() || !QFile::exists(preparedPath_)) {
        emit error("No prepared clip to save");
        return;
    }
    auto *ss = StorageService::instance();
    if (!ss->hasExternal()) { emit error("No external media detected"); return; }

    // Ensure destination dir
    QString outDir = opts_.outDir;
    if (outDir.isEmpty()) outDir = QDir(ss->externalRoot()).filePath("CamVigilExports");
    if (!QDir().mkpath(outDir)) { emit error("Cannot create output directory on external media"); return; }

    // Free space check against actual file size
    QFileInfo fi(preparedPath_);
    const qint64 size = fi.size();
    const qint64 free = ss->freeBytes();
    const qint64 need = qMax(opts_.minFreeBytes, size);
    emit log(QString("[Export] size=%1 MB, free=%2 MB").arg(size/1024/1024).arg(free/1024/1024));
    if (free < need) { emit error(QString("Not enough free space. Need ≥ %1 MB").arg(need/1024/1024)); return; }

    // Copy with coarse progress (copy is single syscall; simulate progress steps)
    const QString dst = QDir(outDir).filePath(QFileInfo(preparedPath_).fileName());
    QFile::remove(dst);

    // Use manual copy to report progress (chunked)
    QFile in(preparedPath_), out(dst);
    if (!in.open(QIODevice::ReadOnly)) { emit error("Open source failed"); return; }
    if (!out.open(QIODevice::WriteOnly)) { emit error("Open destination failed"); return; }

    const qint64 chunk = 4ll * 1024 * 1024; // 4MB
    qint64 written = 0;
    while (!in.atEnd()) {
        if (abort_.load()) { in.close(); out.close(); out.remove(); emit error("Canceled"); return; }
        QByteArray buf = in.read(chunk);
        if (buf.isEmpty() && in.error() != QFileDevice::NoError) { in.close(); out.close(); out.remove(); emit error("Read error"); return; }
        qint64 w = out.write(buf);
        if (w != buf.size()) { in.close(); out.close(); out.remove(); emit error("Write error"); return; }
        written += w;
        double pct = size ? (double(written) / double(size)) * 100.0 : 100.0;
        emit progress(pct);
    }
    out.flush(); out.close(); in.close();

    emit progress(100.0);
    emit log(QString("[Export] saved -> %1").arg(dst));
    emit saved(dst);
}

// ----------------- Helpers -----------------
QVector<ClipPart> PlaybackExporter::computeParts_() const {
    QVector<ClipPart> out;
    const qint64 selAbsA = dayStartNs_ + selStartNs_;
    const qint64 selAbsB = dayStartNs_ + selEndNs_;
    for (const auto& fs : playlist_) {
        const qint64 a = std::max(fs.start_ns, selAbsA);
        const qint64 b = std::min(fs.end_ns,   selAbsB);
        if (b > a) {
            const bool whole = (a == fs.start_ns) && (b == fs.end_ns);
            out.push_back(ClipPart{ fs.path, a - fs.start_ns, b - fs.start_ns, whole });
        }
        if (fs.end_ns >= selAbsB) break;
    }
    return out;
}

QString PlaybackExporter::uniqueOutBaseName_() const {
    const QString base = opts_.baseName.isEmpty()
        ? QString("CamVigil_%1.mp4").arg(QDate::currentDate().toString("yyyy-MM-dd"))
        : (opts_.baseName.endsWith(".mp4") ? opts_.baseName : (opts_.baseName + ".mp4"));
    return base;
}

bool PlaybackExporter::runFfmpeg_(const QStringList& args, QByteArray* errOut){
    if (abort_.load()) return false;
    QProcess p;
    p.setProgram(opts_.ffmpegPath);
    p.setArguments(args);
    p.setProcessChannelMode(QProcess::SeparateChannels);
    p.start();
    if (!p.waitForStarted()) return false;

    while (p.state() == QProcess::Running) {
        if (abort_.load()) {
            p.kill();
            p.waitForFinished();
            return false;
        }
        p.waitForReadyRead(50);
    }
    if (errOut) *errOut = p.readAllStandardError();
    return p.exitStatus()==QProcess::NormalExit && p.exitCode()==0;
}

bool PlaybackExporter::buildInputs_(const QVector<ClipPart>& parts,
                                    const QString& tempDir,
                                    QStringList* inputPaths){
    const int N = parts.size();
    inputPaths->reserve(N);
    for (int i=0;i<N;++i) {
        if (abort_.load()) return false;
        const auto& part = parts[i];

        if (part.wholeFile) {
            inputPaths->push_back(QFileInfo(part.path).absoluteFilePath());
            continue;
        }

        const double ss = secFromNs(part.inStartNs);
        const double to = secFromNs(part.inEndNs);
        const QString cut = QDir(tempDir).filePath(QString("part_%1.mkv").arg(i,4,10,QChar('0')));
        inputPaths->push_back(cut);

        QStringList args; args << "-hide_banner" << "-y";
        if (opts_.precise) {
            const double coarse = std::max(0.0, ss - 3.0);
            args << "-ss" << QString::number(coarse, 'f', 3)
                 << "-i"  << part.path
                 << "-ss" << QString::number(ss - coarse, 'f', 6)
                 << "-to" << QString::number(to - coarse, 'f', 6)
                 << "-c:v" << opts_.vcodec
                 << "-preset" << opts_.preset
                 << "-crf" << QString::number(opts_.crf)
                 << "-pix_fmt" << "yuv420p"
                 << "-fflags" << "+genpts"
                 << "-reset_timestamps" << "1";
            if (opts_.copyAudio) args << "-c:a" << "copy";
            else                 args << "-c:a" << "aac" << "-b:a" << "128k";
            args << "-movflags" << "+faststart"
                 << cut;
        } else {
            args << "-ss" << QString::number(ss, 'f', 6)
                 << "-to" << QString::number(to, 'f', 6)
                 << "-i"  << part.path
                 << "-c"  << "copy"
                 << "-avoid_negative_ts" << "make_zero"
                 << cut;
        }

        QByteArray err;
        emit log(QString("[Export] cut %1/%2").arg(i+1).arg(N));
        if (!runFfmpeg_(args, &err)) { emit log(QString::fromUtf8(err)); return false; }
        emit progress( (i+1) * 100.0 / (N + 1) ); // leave space for concat
    }
    return true;
}

bool PlaybackExporter::writeConcatList_(const QStringList& inputPaths, const QString& listPath){
    QFile f(listPath);
    if (!f.open(QIODevice::WriteOnly|QIODevice::Text)) return false;
    QTextStream ts(&f);
    for (const auto& cp : inputPaths) {
        ts << "file '" << QFileInfo(cp).absoluteFilePath().replace('\'',"\\'") << "'\n";
    }
    f.close();
    return true;
}

bool PlaybackExporter::concat_(const QString& listPath, const QString& outPath){
    QStringList args;
    args << "-hide_banner" << "-y"
         << "-f" << "concat" << "-safe" << "0"
         << "-i" << listPath;

    if (opts_.precise) {
        args << "-c:v" << opts_.vcodec
             << "-preset" << opts_.preset
             << "-crf" << QString::number(opts_.crf);
        if (opts_.copyAudio) args << "-c:a" << "copy";
    } else {
        args << "-c" << "copy";
    }
    args << outPath;

    QByteArray err;
    emit log("[Export] concat");
    const bool ok = runFfmpeg_(args, &err);
    if (!ok) emit log(QString::fromUtf8(err));
    else     emit log(QString("[Export] wrote %1").arg(outPath));
    emit progress(100.0);
    return ok;
}

qint64 PlaybackExporter::estimateBytes_(const QVector<ClipPart>& parts) const {
    double durSec = 0.0;
    for (const auto& p : parts) durSec += secFromNs(p.inEndNs - p.inStartNs);
    const double v_bps = opts_.precise ? 6.0e6 : 4.0e6;
    const double a_bps = 128.0e3;
    qint64 bytes = qint64((v_bps + a_bps) * durSec / 8.0);
    if (bytes < 200ll*1024*1024) bytes = 200ll*1024*1024;
    return bytes;
}
