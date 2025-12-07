#pragma once

#include <QOpenGLWindow>
#include <QOpenGLFunctions>
#include <QPixmap>
#include <QRect>

class FullScreenViewer : public QOpenGLWindow, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit FullScreenViewer(QWindow *parent = nullptr);
    void setImage(const QPixmap &pixmap);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void keyPressEvent(QKeyEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QPixmap currentPixmap;
    QRect closeButtonRect;
};
