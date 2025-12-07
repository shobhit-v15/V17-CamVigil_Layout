#include "layoutmanager.h"
#include <QLayoutItem>
#include <QWidget>
#include <QDebug>

LayoutManager::LayoutManager(QGridLayout* layout)
    : gridLayout(layout)
    , gridRows(0)
    , gridCols(0)
{
}

void LayoutManager::setGridSize(int rows, int cols) {
    if (!gridLayout) return;

    if (rows <= 0 || cols <= 0) {
        qWarning() << "[LayoutManager] Invalid grid size:" << rows << "x" << cols;
        return;
    }

    gridRows = rows;
    gridCols = cols;

    // Set row/column stretch factors so grid expands evenly.
    for (int r = 0; r < gridRows; ++r) {
        gridLayout->setRowStretch(r, 1);
    }
    for (int c = 0; c < gridCols; ++c) {
        gridLayout->setColumnStretch(c, 1);
    }
}

void LayoutManager::clearLayout() {
    if (!gridLayout) return;

    qInfo() << "[LayoutManager] Clearing layout items";
    QLayoutItem* item = nullptr;
    while ((item = gridLayout->takeAt(0)) != nullptr) {
        if (QWidget* w = item->widget()) {
            // Remove from layout AND hide, so it’s not visible anymore.
            gridLayout->removeWidget(w);
            w->hide();
        }
        delete item; // deletes only QLayoutItem, not the QWidget
    }
}


void LayoutManager::apply(const std::vector<QWidget*>& widgets) {
    if (!gridLayout) return;

    const int expectedCount = gridRows * gridCols;
    if (static_cast<int>(widgets.size()) != expectedCount) {
        qWarning() << "[LayoutManager] apply(): widget count mismatch. Expected"
                   << expectedCount << "got" << widgets.size();
        return;
    }

    clearLayout();

    qInfo() << "[LayoutManager] Applying" << widgets.size()
            << "widgets in" << gridRows << "x" << gridCols << "grid";

    int index = 0;
    for (int r = 0; r < gridRows; ++r) {
        for (int c = 0; c < gridCols; ++c) {
            QWidget* w = widgets[index++];
            if (!w) {
                qWarning() << "[LayoutManager] apply(): null widget at index" << (index - 1);
                continue;
            }
            w->show();                    // make the ones we *do* use visible
            gridLayout->addWidget(w, r, c);
        }
    }
}

