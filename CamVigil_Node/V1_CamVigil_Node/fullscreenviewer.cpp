#include "fullscreenviewer.h"
#include <QPainter>
#include <QDebug>
#include <QKeyEvent>
#include <QMouseEvent>

FullScreenViewer::FullScreenViewer(QWindow *parent)
    : QOpenGLWindow(QOpenGLWindow::NoPartialUpdate, parent),
      closeButtonRect(width() - 50, 10, 40, 30) {}

void FullScreenViewer::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    qDebug() << "[FS-Window] OpenGL initialized:";
    qDebug() << "Vendor:  " << reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    qDebug() << "Renderer:" << reinterpret_cast<const char*>(glGetString(GL_RENDERER));
}

void FullScreenViewer::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
    closeButtonRect = QRect(w - 50, 10, 40, 30);  // reposition close button
}

void FullScreenViewer::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);

    QPainter painter(this);
    if (!currentPixmap.isNull()) {
        painter.drawPixmap(0, 0, width(), height(), currentPixmap);
    }

    // Draw the close button like a video player ✖
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::white);
    painter.setBrush(QColor(80, 80, 80, 180));
    painter.drawRoundedRect(closeButtonRect, 5, 5);

    painter.setFont(QFont("Arial", 14, QFont::Bold));
    painter.setPen(Qt::white);
    painter.drawText(closeButtonRect, Qt::AlignCenter, "✖");
}

void FullScreenViewer::setImage(const QPixmap &pixmap) {
    currentPixmap = pixmap;
    update();
}

void FullScreenViewer::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        close();
    }
}

void FullScreenViewer::mouseDoubleClickEvent(QMouseEvent *event) {
    Q_UNUSED(event);
    close();
}

void FullScreenViewer::mousePressEvent(QMouseEvent *event) {
    if (closeButtonRect.contains(event->pos())) {
        close();
    }
}
