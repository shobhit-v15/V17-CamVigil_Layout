#ifndef OPERATIONSTATUSWIDGET_H
#define OPERATIONSTATUSWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QHBoxLayout>

class OperationStatusWidget : public QWidget {
    Q_OBJECT

public:
    explicit OperationStatusWidget(QWidget *parent = nullptr);
    void updateSubscriptionStatus();

private:
    QLabel* operationLabel;
    QLabel* standaloneIndicator;
    QLabel* connectedIndicator;

    void setStandaloneMode(bool isStandalone);
};

#endif // OPERATIONSTATUSWIDGET_H
