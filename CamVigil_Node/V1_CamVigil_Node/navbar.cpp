#include "navbar.h"

Navbar::Navbar(QWidget* parent) : QWidget(parent) {
    label = new QLabel("CamVigil", this);
    label->setAlignment(Qt::AlignCenter);

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->addWidget(label);
    layout->setContentsMargins(0, 0, 0, 0);
    setLayout(layout);

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setStyleSheet("background-color: #1E1E1E; color: #FFFFFF; font-size: 24px; "
                  "font-weight: bold; padding: 15px;");
}
