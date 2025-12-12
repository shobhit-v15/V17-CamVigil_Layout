#include <QApplication>
#include <QSurfaceFormat>
#include <QOpenGLContext>
#include <QDebug>

#include <QSplashScreen>
#include <QPixmap>
#include <QBitmap>
#include <QThread>
#include <QScreen>

#include "mainwindow.h"

int main(int argc, char *argv[])
{
    // OpenGL format setup
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 1);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);

    // loading the pixmap
    QPixmap pix(":/images/splash.png");
    qDebug() << "Splash loaded size:" << pix.size();


    QSplashScreen splash(pix, Qt::WindowStaysOnTopHint | Qt::SplashScreen);
    splash.setMask(pix.mask());

    // centere it on the primary screen
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        QRect scr = screen->geometry();
        QSize sz = splash.size();
        splash.move((scr.width() - sz.width()) / 2,
                    (scr.height() - sz.height()) / 2);
    }

    splash.show();
    splash.raise();
    app.processEvents();


    MainWindow w;
    w.show();

    splash.finish(&w);

    auto ctx = QOpenGLContext::currentContext();
    if (ctx)
        qDebug() << "Using OpenGL:" << ctx->format();
    else
        qDebug() << "No OpenGL context available yet.";

    return app.exec();
}
