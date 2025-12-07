// settingswindow.h
// ─────────────────
#ifndef SETTINGSWINDOW_H
#define SETTINGSWINDOW_H

#include <QWidget>
#include <QPushButton>
#include "archivemanager.h"
#include "archivewidget.h"
#include "cameramanager.h"
#include "timeeditorwidget.h"
#include "cameradetailswidget.h"

class SettingsWindow : public QWidget {
    Q_OBJECT

public:
    explicit SettingsWindow(ArchiveManager* archiveManager,
                            CameraManager* cameraManager,
                            QWidget *parent = nullptr);

signals:
    void groupsMembershipsChanged();

private slots:
    void closeWindow();

private:
    ArchiveManager* archiveManager;
    CameraManager* cameraManager;
    ArchiveWidget* archiveWidget;
    QPushButton* closeIconButton;
    QWidget* navbar;
    CameraDetailsWidget* cameraDetailsWidget = nullptr;
};

#endif // SETTINGSWINDOW_H
