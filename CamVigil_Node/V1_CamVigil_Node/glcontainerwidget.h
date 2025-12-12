// glcontainerwidget.h
#ifndef GLCONTAINERWIDGET_H
#define GLCONTAINERWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QDebug>

class GLContainerWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit GLContainerWidget(QWidget *parent = nullptr) : QOpenGLWidget(parent) {}

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        qDebug() << "[GL] OpenGL initialized";
        qDebug() << "Vendor:" << reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        qDebug() << "Renderer:" << reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        qDebug() << "Version:" << reinterpret_cast<const char*>(glGetString(GL_VERSION));
    }

    void paintGL() override {

        glClearColor(0.0f, 0.05f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
};

#endif // GLCONTAINERWIDGET_H
