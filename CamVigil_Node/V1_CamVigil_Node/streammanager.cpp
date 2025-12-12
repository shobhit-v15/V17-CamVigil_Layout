#include "streammanager.h"
#include <QDebug>
#include <opencv2/opencv.hpp>

StreamManager::StreamManager(QObject* parent)
    : QObject(parent)
{
}

StreamManager::~StreamManager() {
    stopStreaming();
}

void StreamManager::startStreaming(const std::vector<CamHWProfile>& cameraProfiles, const std::vector<QLabel*>& labels) {
    stopStreaming();
    this->labels = labels;

    for (size_t i = 0; i < cameraProfiles.size(); ++i) {
        int currentIndex = static_cast<int>(i);
        const std::string& subUrl = cameraProfiles[i].suburl;

        // Checks camera connection using OpenCV with the suburl.
        cv::VideoCapture cap(subUrl);
        if (!cap.isOpened()) {
            qDebug() << "Initial connection check failed for camera substream at index:" << currentIndex;
            if (currentIndex < static_cast<int>(labels.size()) && labels[currentIndex]) {
                labels[currentIndex]->setText("❌ Camera Unavailable");
                labels[currentIndex]->setAlignment(Qt::AlignCenter);
                labels[currentIndex]->setStyleSheet("color: red; font-size: 18px; font-weight: bold;");
            }
            continue;
        }
        cap.release();

        // Create a StreamWorker for a valid camera using the suburl.
        StreamWorker* worker = new StreamWorker(subUrl, currentIndex);
        QThread* thread = new QThread();
        worker->moveToThread(thread);

        connect(thread, &QThread::started, worker, &StreamWorker::process);
        connect(worker, &StreamWorker::frameReady, this, [this](int idx, const QPixmap &pixmap){
            emit frameReady(idx, pixmap);
        });
        connect(worker, &StreamWorker::finished, thread, &QThread::quit);
        connect(worker, &StreamWorker::finished, worker, &QObject::deleteLater);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);


        thread->start();
        workers.push_back({subUrl, thread, worker});
    }
}

void StreamManager::stopStreaming() {
    for (auto &info : workers) {
        if (info.worker) {
            info.worker->stop();
        }
    }
    workers.clear();
}

void StreamManager::restartStream(const std::string& url) {
    for (size_t i = 0; i < workers.size(); ++i) {
        if (workers[i].url == url) {
            qDebug() << "Restarting stream for" << QString::fromStdString(url);
            if (workers[i].worker) {
                workers[i].worker->stop();
            }

            StreamWorker* newWorker = new StreamWorker(url, static_cast<int>(i));
            QThread* newThread = new QThread();
            newWorker->moveToThread(newThread);

            connect(newThread, &QThread::started, newWorker, &StreamWorker::process);
            connect(newWorker, &StreamWorker::frameReady, this, [this](int idx, const QPixmap &pixmap){
                emit frameReady(idx, pixmap);
            });
            connect(newWorker, &StreamWorker::finished, newThread, &QThread::quit);
            connect(newWorker, &StreamWorker::finished, newWorker, &QObject::deleteLater);
            connect(newThread, &QThread::finished, newThread, &QObject::deleteLater);

            newThread->start();
            workers[i].worker = newWorker;
            workers[i].thread = newThread;
            break;
        }
    }
}
