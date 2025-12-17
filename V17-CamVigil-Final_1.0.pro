QT += core gui
QT += widgets multimedia multimediawidgets opengl
QT += sql
QT += network concurrent
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# Disable deprecated Qt APIs before version 6.0.0
DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000

# Use pkg-config to handle OpenCV and GStreamer includes/libs
CONFIG += link_pkgconfig
PKGCONFIG += opencv4 gstreamer-1.0 gstreamer-video-1.0 gstreamer-app-1.0 glib-2.0 gstreamer-gl-1.0 gstreamer-rtsp-server-1.0

# Added linker flags for libudev (required for hotplug support)
LIBS += -Wl,--no-as-needed -ludev -Wl,--as-needed

LIBS += -lGL

LIBS += -lgstreamer-1.0 -lgstvideo-1.0 -lgstapp-1.0 -lgstbase-1.0 -lgobject-2.0 -lglib-2.0
# Explicit link for RTSP server bits (gstreamer pkg-config may cover this on some distros)
LIBS += -lgstrtspserver-1.0

QT += dbus concurrent

SOURCES += \
    archivemanager.cpp \
    archivewidget.cpp \
    archiveworker.cpp \
    cameradetailswidget.cpp \
    cameragridstate.cpp \
    cameramanager.cpp \
    camerastreams.cpp \
    node_config.cpp \
    node_restreamer.cpp \
    node_core_service.cpp \
    node_api_server.cpp \
    node_services_bootstrap.cpp \
    db_reader.cpp \
    db_writer.cpp \
    fullscreenviewer.cpp \
    hik_osd.cpp \
    hik_time.cpp \
    layoutmanager.cpp \
    main.cpp \
    mainwindow.cpp \
    navbar.cpp \
    operationstatuswidget.cpp \
    playback_controls.cpp \
    playback_db_service.cpp \
    playback_exporter.cpp \
    playback_segment_index.cpp \
    playback_side_controls.cpp \
    playback_stitching_player.cpp \
    playback_timeline_controller.cpp \
    playback_timeline_model.cpp \
    playback_timeline_view.cpp \
    playback_title_bar.cpp \
    playback_trim_panel.cpp \
    playback_video_box.cpp \
    playback_video_player_gst.cpp \
    playbackwindow.cpp \
    settingswindow.cpp \
    storagedetailswidget.cpp \
    storageservice.cpp \
    streammanager.cpp \
    streamworker.cpp \
    subscriptionmanager.cpp \
    timeeditorwidget.cpp \
    toolbar.cpp \
    videoplayerwindow.cpp \
    group_repository.cpp \
    camera_grouping_widget.cpp

HEADERS += \
    archivemanager.h \
    archivewidget.h \
    archiveworker.h \
    cameradetailswidget.h \
    cameragridstate.h \
    cameramanager.h \
    camerastreams.h \
    node_config.h \
    node_restreamer.h \
    node_core_service.h \
    node_api_server.h \
    node_services_bootstrap.h \
    clickablelabel.h \
    db_reader.h \
    db_writer.h \
    fullscreenviewer.h \
    glcontainerwidget.h \
    hik_osd.h \
    hik_time.h \
    layoutmanager.h \
    mainwindow.h \
    navbar.h \
    operationstatuswidget.h \
    playback_controls.h \
    playback_db_service.h \
    playback_exporter.h \
    playback_segment_index.h \
    playback_side_controls.h \
    playback_stitching_player.h \
    playback_timeline_controller.h \
    playback_timeline_model.h \
    playback_timeline_view.h \
    playback_title_bar.h \
    playback_trim_panel.h \
    playback_video_box.h \
    playback_video_player_gst.h \
    playbackwindow.h \
    settingswindow.h \
    storagedetailswidget.h \
    storageservice.h \
    streammanager.h \
    streamworker.h \
    subscriptionmanager.h \
    timeeditorwidget.h \
    toolbar.h \
    videoplayerwindow.h \
    group_repository.h \
    camera_grouping_widget.h

FORMS += \
    mainwindow.ui

# Default rules for deployment
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    spinner.qrc \
    splash.qrc
