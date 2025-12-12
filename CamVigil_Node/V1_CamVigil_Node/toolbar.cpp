#include "toolbar.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QSignalBlocker>
#include "subscriptionmanager.h"

Toolbar::Toolbar(QWidget* parent)
    : QWidget(parent)
    , clockLabel(nullptr)
    , statusLabel(nullptr)
    , pageLabel(nullptr)
    , settingsButton(nullptr)
    , playbackButton(nullptr)
    , prevPageButton(nullptr)
    , nextPageButton(nullptr)
    , defaultLayoutButton(nullptr)
    , customLayoutButton(nullptr)
    , groupCombo(nullptr)
    , clockTimer(new QTimer(this))
    , networkManager(new QNetworkAccessManager(this))
    , checkTimer(new QTimer(this))
{
    setStyleSheet(
        "color: white;"
        "font-size: 24px;"
        "font-weight: bold;"
        "padding: 10px;"
    );

    // 3-column grid: [left/status] [center/clock] [right/controls]
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(10, 5, 10, 5);
    grid->setHorizontalSpacing(12);

    grid->setColumnStretch(0, 1);   // left  space
    grid->setColumnStretch(1, 0);   // center (clock)
    grid->setColumnStretch(2, 1);   // right space (buttons)

    QWidget* leftBox = new QWidget(this);
    auto* left = new QHBoxLayout(leftBox);
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(10);

    defaultLayoutButton = new QPushButton("Default", this);
    customLayoutButton = new QPushButton("Custom", this);

    const char* layoutButtonStyle =
        "QPushButton {"
        "   color: white;"
        "   padding: 6px 16px;"
        "   font-weight: 700;"
        "   font-size: 16px;"
        "   border: 1px solid #666;"
        "   border-radius: 4px;"
        "   background-color: #2a2a2a;"
        "}"
        "QPushButton:hover {"
        "   background-color: #3a3a3a;"
        "}"
        "QPushButton:pressed {"
        "   background-color: #1a1a1a;"
        "}";

    const char* layoutButtonSelectedStyle =
        "QPushButton {"
        "   color: white;"
        "   padding: 6px 16px;"
        "   font-weight: 700;"
        "   font-size: 16px;"
        "   border: 2px solid #4a9eff;"
        "   border-radius: 4px;"
        "   background-color: #1e3a5f;"
        "}"
        "QPushButton:hover {"
        "   background-color: #2a4a6f;"
        "}"
        "QPushButton:pressed {"
        "   background-color: #1a2a4f;"
        "}";

    defaultLayoutButton->setStyleSheet(layoutButtonSelectedStyle);
    customLayoutButton->setStyleSheet(layoutButtonStyle);

    connect(defaultLayoutButton, &QPushButton::clicked, this, &Toolbar::onDefaultLayoutClicked);
    connect(customLayoutButton, &QPushButton::clicked, this, &Toolbar::onCustomLayoutClicked);

    left->addWidget(defaultLayoutButton);
    left->addWidget(customLayoutButton);

    statusLabel = new QLabel("Standalone Mode", this);
    statusLabel->setStyleSheet("color: orange; font-size: 18px; padding-left: 15px;");
    left->addWidget(statusLabel);

    left->addStretch();

    grid->addWidget(leftBox, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);

    // Center: Clock label
    clockLabel = new QLabel(this);
    clockLabel->setAlignment(Qt::AlignCenter);
    clockLabel->setStyleSheet("color: white; padding: 5px;");
    grid->addWidget(clockLabel, 0, 1, Qt::AlignCenter);

    // Right: Playback + Settings + Pagination in a single row
    QWidget* rightBox = new QWidget(this);
    auto* right = new QHBoxLayout(rightBox);
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(10);

    // Playback button
    playbackButton = new QPushButton("▶ Playback", this);
    playbackButton->setStyleSheet(
        "QPushButton { color: orange; padding: 6px 13px; font-weight: 900; font-size: 18px; }"
        "QPushButton:hover { background-color: #444444; }"
        "QPushButton:pressed { background-color: #222222; }"
    );
    connect(playbackButton, &QPushButton::clicked, this, &Toolbar::playbackButtonClicked);
    right->addWidget(playbackButton, 0, Qt::AlignRight);

    // Settings button
    settingsButton = new QPushButton("⚙ Settings", this);
    settingsButton->setStyleSheet(
        "QPushButton {"
        "   color: white;"
        "   padding: 6px 13px;"
        "   font-weight: 900;"
        "   font-size: 18px;"
        "}"
        "QPushButton:hover {"
        "   background-color: #444444;"
        "}"
        "QPushButton:pressed {"
        "   background-color: #222222;"
        "}"
    );
    connect(settingsButton, &QPushButton::clicked, this, &Toolbar::settingsButtonClicked);
    right->addWidget(settingsButton, 0, Qt::AlignRight);

    // Group selector (compact, near pagination controls)
    groupCombo = new QComboBox(this);
    groupCombo->setMinimumWidth(80);
    groupCombo->setMaximumWidth(140);
    groupCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    groupCombo->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    groupCombo->setStyleSheet("font-size: 18px; font-weight: 900; color: white; background-color: #4d4d4d;");
    connect(groupCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &Toolbar::groupChanged);

    // Pagination controls: ◀ Page X / Y ▶
    const char* pageButtonStyle =
        "QPushButton {"
        "   color: white;"
        "   padding: 4px 8px;"
        "   font-weight: 700;"
        "   font-size: 14px;"
        "   border: 1px solid #666;"
        "   border-radius: 4px;"
        "}"
        "QPushButton:hover { background-color: #333; }"
        "QPushButton:pressed { background-color: #111; }";

    prevPageButton = new QPushButton("◀", this);
    nextPageButton = new QPushButton("▶", this);
    prevPageButton->setStyleSheet(pageButtonStyle);
    nextPageButton->setStyleSheet(pageButtonStyle);

    connect(prevPageButton, &QPushButton::clicked, this, &Toolbar::onPrevPageClicked);
    connect(nextPageButton, &QPushButton::clicked, this, &Toolbar::onNextPageClicked);

    pageLabel = new QLabel("Page 1 / 1", this);
    pageLabel->setStyleSheet("color: white; font-size: 14px;");

    // Keep playback/settings grouped on the left of this row,
    // then a stretch, then group selector + pagination on the right.
    right->addStretch();
    // Match combo height to buttons so it feels consistent
    const int buttonHeight = settingsButton->sizeHint().height();
    groupCombo->setFixedHeight(buttonHeight);
    right->addWidget(groupCombo);
    right->addWidget(prevPageButton);
    right->addWidget(pageLabel);
    right->addWidget(nextPageButton);

    grid->addWidget(rightBox, 0, 2, Qt::AlignRight | Qt::AlignVCenter);
    setLayout(grid);

    // Clock update
    connect(clockTimer, &QTimer::timeout, this, &Toolbar::updateClock);
    clockTimer->start(1000);
    updateClock();

    // Network connectivity check
    connect(checkTimer, &QTimer::timeout, this, &Toolbar::checkInternetConnection);
    checkTimer->start(5000);
    checkInternetConnection();
}

void Toolbar::setPageInfo(int currentPage, int totalPages) {
    if (!pageLabel) return;

    if (totalPages < 1) totalPages = 1;
    if (currentPage < 1) currentPage = 1;
    if (currentPage > totalPages) currentPage = totalPages;

    pageLabel->setText(QString("Page %1 / %2").arg(currentPage).arg(totalPages));
}

void Toolbar::setGroups(const QStringList& names, int currentIndex) {
    if (!groupCombo)
        return;

    QSignalBlocker blocker(groupCombo);

    groupCombo->clear();
    groupCombo->addItems(names);

    if (currentIndex >= 0 && currentIndex < names.size()) {
        groupCombo->setCurrentIndex(currentIndex);
    } else if (!names.isEmpty()) {
        groupCombo->setCurrentIndex(0);
    }
}

void Toolbar::updateClock() {
    clockLabel->setText(QDateTime::currentDateTime().toString("dd MMM yyyy  HH:mm:ss AP"));
}

void Toolbar::checkInternetConnection() {
    QNetworkRequest request(QUrl("http://www.google.com"));
    QNetworkReply* reply = networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError &&
            SubscriptionManager::currentSubscriptionStatus()) {
            statusLabel->setText("Connected");
            statusLabel->setStyleSheet("color: green; font-size: 18px; padding-left: 5px;");
        } else {
            statusLabel->setText("Standalone Mode");
            statusLabel->setStyleSheet("color: orange; font-size: 18px; padding-left: 5px;");
        }
        reply->deleteLater();
    });
}

void Toolbar::onPrevPageClicked() {
    emit previousPageRequested();
}

void Toolbar::onNextPageClicked() {
    emit nextPageRequested();
}

void Toolbar::onDefaultLayoutClicked() {
    if (!defaultLayoutButton || !customLayoutButton) return;

    const char* selectedStyle =
        "QPushButton {"
        "   color: white;"
        "   padding: 6px 16px;"
        "   font-weight: 700;"
        "   font-size: 16px;"
        "   border: 2px solid #4a9eff;"
        "   border-radius: 4px;"
        "   background-color: #1e3a5f;"
        "}"
        "QPushButton:hover {"
        "   background-color: #2a4a6f;"
        "}"
        "QPushButton:pressed {"
        "   background-color: #1a2a4f;"
        "}";

    const char* unselectedStyle =
        "QPushButton {"
        "   color: white;"
        "   padding: 6px 16px;"
        "   font-weight: 700;"
        "   font-size: 16px;"
        "   border: 1px solid #666;"
        "   border-radius: 4px;"
        "   background-color: #2a2a2a;"
        "}"
        "QPushButton:hover {"
        "   background-color: #3a3a3a;"
        "}"
        "QPushButton:pressed {"
        "   background-color: #1a1a1a;"
        "}";

    defaultLayoutButton->setStyleSheet(selectedStyle);
    customLayoutButton->setStyleSheet(unselectedStyle);

    emit layoutModeChanged(true);
}

void Toolbar::onCustomLayoutClicked() {
    if (!defaultLayoutButton || !customLayoutButton) return;

    const char* selectedStyle =
        "QPushButton {"
        "   color: white;"
        "   padding: 6px 16px;"
        "   font-weight: 700;"
        "   font-size: 16px;"
        "   border: 2px solid #4a9eff;"
        "   border-radius: 4px;"
        "   background-color: #1e3a5f;"
        "}"
        "QPushButton:hover {"
        "   background-color: #2a4a6f;"
        "}"
        "QPushButton:pressed {"
        "   background-color: #1a2a4f;"
        "}";

    const char* unselectedStyle =
        "QPushButton {"
        "   color: white;"
        "   padding: 6px 16px;"
        "   font-weight: 700;"
        "   font-size: 16px;"
        "   border: 1px solid #666;"
        "   border-radius: 4px;"
        "   background-color: #2a2a2a;"
        "}"
        "QPushButton:hover {"
        "   background-color: #3a3a3a;"
        "}"
        "QPushButton:pressed {"
        "   background-color: #1a1a1a;"
        "}";

    customLayoutButton->setStyleSheet(selectedStyle);
    defaultLayoutButton->setStyleSheet(unselectedStyle);

    emit layoutModeChanged(false);
}
