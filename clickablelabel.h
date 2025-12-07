#ifndef CLICKABLELABEL_H
#define CLICKABLELABEL_H

#include <QLabel>
#include <QMouseEvent>

class ClickableLabel : public QLabel {
    Q_OBJECT

public:
    // Construct with an index that identifies which camera this label represents.
    explicit ClickableLabel(int index, QWidget *parent = nullptr)
        : QLabel(parent), labelIndex(index) {}
    void showLoading() {
        this->setText("Loading...");
        this->setAlignment(Qt::AlignCenter);
        this->setStyleSheet("color: white; font-size: 18px;");
    }

signals:
    void clicked(int index);

protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            emit clicked(labelIndex);
        }
        QLabel::mousePressEvent(event);
    }


private:
    int labelIndex;
};

#endif // CLICKABLELABEL_H
