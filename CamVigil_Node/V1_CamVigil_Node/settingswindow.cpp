#include "settingswindow.h"
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QGuiApplication>
#include <QScreen>
#include <QDebug>
#include <QFormLayout>
#include "operationstatuswidget.h"
#include "cameradetailswidget.h"
#include "storagedetailswidget.h"
#include "archivewidget.h"
#include "cameramanager.h"

SettingsWindow::SettingsWindow(ArchiveManager* archiveManager,
                               CameraManager* cameraManager,
                               QWidget *parent)
    : QWidget(parent)
    , archiveManager(archiveManager)
    , cameraManager(cameraManager)
{
    setWindowTitle("Settings");
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        setGeometry(screen->geometry());
    } else {
        qWarning() << "[SettingsWindow] No primary screen available; using default geometry";
    }


    this->setStyleSheet(R"(
        QWidget {
            background-color: #000000;
            color: #FFFFFF;
            font-family: "Courier New", monospace;
            font-size: 14px;
        }
        QLabel { padding: 4px; }
        QLineEdit, QComboBox {
            background-color: #111;
            border: 1px solid #FFFFFF;
            color: #FFFFFF;
            padding: 4px;
        }
        QPushButton {
            background-color: #111;
            border: 1px solid #FFFFFF;
            padding: 5px 10px;
            color: #FFFFFF;
        }
        QPushButton:hover { background-color: #222; }
        QListWidget {
            background-color: #111;
            border: 1px solid #FFFFFF;
            color: #00BFFF;
        }
        QProgressBar {
            border: 1px solid #FFFFFF;
            background-color: #111;
            text-align: center;
        }
        QProgressBar::chunk { background-color: #FFFFFF; }
    )");

    // Main vertical layout
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    setLayout(mainLayout);

    // Scrollable area for all settings content
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("border: none; background-color: black;");
    mainLayout->addWidget(scrollArea);

    QWidget* scrollContent = new QWidget();
    QVBoxLayout* scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setSpacing(20);
    scrollContent->setStyleSheet("background-color: black;");
    scrollArea->setWidget(scrollContent);

    // ─── Navbar ─────────────────────────────────────────────────────────────
    navbar = new QWidget(scrollContent);
    navbar->setStyleSheet("background-color: #2C2C2C; padding: 5px;");
    navbar->setFixedHeight(50);
    QHBoxLayout* navbarLayout = new QHBoxLayout(navbar);
    navbarLayout->setContentsMargins(0, 0, 0, 0);
    navbarLayout->setSpacing(0);

    QPushButton* backButton = new QPushButton("← Back", navbar);
    backButton->setStyleSheet("color: white; background: transparent; font-size: 16px;");
    backButton->setCursor(Qt::PointingHandCursor);
    connect(backButton, &QPushButton::clicked, this, &SettingsWindow::close);
    navbarLayout->addWidget(backButton, 0, Qt::AlignLeft);

    QLabel* titleLabel = new QLabel("CamVigil-Settings", navbar);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-weight: bold; font-size: 20px; padding: 5px;");
    navbarLayout->addWidget(titleLabel, 1, Qt::AlignCenter);

    closeIconButton = new QPushButton("✖", navbar);
    closeIconButton->setStyleSheet("background: transparent; border: none; color: white; font-size: 18px; padding: 5px;");
    closeIconButton->setFixedSize(30, 30);
    connect(closeIconButton, &QPushButton::clicked, this, &SettingsWindow::closeWindow);
    navbarLayout->addWidget(closeIconButton, 0, Qt::AlignRight);

    navbar->setLayout(navbarLayout);
    scrollLayout->addWidget(navbar, 0, Qt::AlignTop);

    // ─── Operation Status ───────────────────────────────────────────────────
    OperationStatusWidget* operationWidget = new OperationStatusWidget();
    scrollLayout->addWidget(operationWidget, 0, Qt::AlignLeft);

    // ─── Camera Details ─────────────────────────────────────────────────────
    if (cameraManager) {
        QString dbPath;
        if (archiveManager) {
            dbPath = archiveManager->databasePath();
        }
        cameraDetailsWidget = new CameraDetailsWidget(cameraManager, dbPath, this);
        cameraDetailsWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        scrollLayout->addWidget(cameraDetailsWidget, 0, Qt::AlignLeft);
        connect(cameraDetailsWidget, &CameraDetailsWidget::groupsMembershipsChanged,
                this, &SettingsWindow::groupsMembershipsChanged);
    } else {
        qWarning() << "[SettingsWindow] cameraManager is null; skipping CameraDetailsWidget";
    }

    // ─── Time Settings ───────────────────────────────────────────────────────
    QFormLayout* timeLayout = new QFormLayout();
    timeLayout->setLabelAlignment(Qt::AlignLeft);
    timeLayout->setFormAlignment(Qt::AlignHCenter);

    QLabel* systemTimeLabel = new QLabel("System Time");
    systemTimeLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: white;");

    TimeEditorWidget* timeEditor = new TimeEditorWidget();
    timeEditor->setStyleSheet("background-color: #4d4d4d;");
    timeEditor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    timeLayout->addRow(systemTimeLabel, timeEditor);

    // Wrap in a QWidget to insert into scrollLayout
    QWidget* timeWidgetWrapper = new QWidget(scrollContent);
    timeWidgetWrapper->setLayout(timeLayout);
    scrollLayout->addWidget(timeWidgetWrapper, 0, Qt::AlignLeft);


    // ─── Storage Details ────────────────────────────────────────────────────
    if (archiveManager) {
        StorageDetailsWidget* storageWidget = new StorageDetailsWidget(archiveManager);
        storageWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        scrollLayout->addWidget(storageWidget, 0, Qt::AlignLeft);
        connect(storageWidget, &StorageDetailsWidget::requestCleanup,
                archiveManager, &ArchiveManager::cleanupArchive);
        connect(archiveManager, &ArchiveManager::segmentWritten,
                storageWidget, &StorageDetailsWidget::updateStorageInfo);
    } else {
        qWarning() << "[SettingsWindow] archiveManager is null; skipping StorageDetailsWidget";
    }

    scrollLayout->addStretch();

    // ─── Archive List ───────────────────────────────────────────────────────
    // To avoid crashes originating from the ArchiveWidget/DB reader path
    // in this window, show a simple placeholder and keep full archive
    // browsing in the dedicated Playback window.
    QLabel* archivePlaceholder = new QLabel("Archives are available via the Playback window.", scrollContent);
    archivePlaceholder->setAlignment(Qt::AlignLeft);
    archivePlaceholder->setStyleSheet("font-size: 18px; font-weight: bold; color: white;");
    scrollLayout->addWidget(archivePlaceholder, 0, Qt::AlignLeft);

    scrollLayout->addStretch();
}

void SettingsWindow::closeWindow()
{
    close();
}
