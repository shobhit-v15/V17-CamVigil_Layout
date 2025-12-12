#ifndef LAYOUTMANAGER_H
#define LAYOUTMANAGER_H

#include <QGridLayout>
#include <vector>

class LayoutManager {
public:
    explicit LayoutManager(QGridLayout* layout);

    // Fixed grid size for the live view (3x3 in your case).
    void setGridSize(int rows, int cols);

    // Apply a new set of widgets to the grid in row-major order.
    // widgets.size() MUST be == rows * cols.
    void apply(const std::vector<QWidget*>& widgets);

private:
    QGridLayout* gridLayout;
    int gridRows;
    int gridCols;

    void clearLayout();
};

#endif // LAYOUTMANAGER_H
