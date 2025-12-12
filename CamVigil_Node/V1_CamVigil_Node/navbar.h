#ifndef NAVBAR_H
#define NAVBAR_H

#include <QWidget>
#include <QLabel>
#include <QHBoxLayout>

class Navbar : public QWidget {
    Q_OBJECT

public:
    explicit Navbar(QWidget* parent = nullptr);

private:
    QLabel* label;
};

#endif // NAVBAR_H
