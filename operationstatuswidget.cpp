#include "operationstatuswidget.h"
#include "subscriptionmanager.h"

OperationStatusWidget::OperationStatusWidget(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(50);

    // Operation label
    operationLabel = new QLabel("Mode", this);
    operationLabel->setStyleSheet("color: white; font-weight: bold; font-size: 18px;");

    // Standalone indicator
    standaloneIndicator = new QLabel(this);
    standaloneIndicator->setText("◉ Standalone");
    standaloneIndicator->setStyleSheet("color: orange; font-size: 16px;");

    // Connected indicator
    connectedIndicator = new QLabel(this);
    connectedIndicator->setText("○ Connected");
    connectedIndicator->setStyleSheet("color: #4CAF50; font-size: 16px;");

    // Layout setup
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(15, 5, 15, 5);
    layout->setSpacing(15);
    layout->addWidget(operationLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addStretch();
    layout->addWidget(standaloneIndicator, 0, Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(connectedIndicator, 0, Qt::AlignRight | Qt::AlignVCenter);

    setLayout(layout);

    // Update UI initially
    updateSubscriptionStatus();
}

void OperationStatusWidget::updateSubscriptionStatus() {
    bool subscribed = SubscriptionManager::currentSubscriptionStatus();
    setStandaloneMode(!subscribed);
}

void OperationStatusWidget::setStandaloneMode(bool isStandalone) {
    if (isStandalone) {
        standaloneIndicator->setText("◉ Standalone");
        connectedIndicator->setText("○ Connected");
    } else {
        standaloneIndicator->setText("○ Standalone");
        connectedIndicator->setText("◉ Connected");
    }
}
