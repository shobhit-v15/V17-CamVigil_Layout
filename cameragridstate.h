#ifndef CAMERAGRIDSTATE_H
#define CAMERAGRIDSTATE_H

// Pure logic helper for paged camera grids.
// It knows only:
//  - visibleCount: number of visible cameras in current filter/group
//  - camerasPerPage: slots per page (fixed 9 for 3x3)
//  - currentPage, totalPages
//
// It does NOT know anything about QWidget, labels, or camera profiles.

class CameraGridState {
public:
    explicit CameraGridState(int camerasPerPage = 9);

    // Set how many cameras are currently visible (after any filtering/grouping).
    void setVisibleCount(int count);

    // Get the current total visible count.
    int visibleCount() const;

    // Page controls (0-based internally).
    void setCurrentPage(int page);
    int  currentPage() const;
    int  totalPages() const;
    int  camerasPerPage() const;

    // Navigation helpers.
    void nextPage();
    void previousPage();

    // Recalculate totalPages and clamp currentPage.
    void recalcPages();

    // Mapping: (page, slot) -> visible camera index (0..visibleCount-1) or -1 if blank.
    int cameraIndexForSlot(int page, int slot) const;

    // Mapping: visible camera index -> page / slot (in that page).
    int pageForCamera(int visibleIndex) const;
    int slotForCamera(int visibleIndex) const;

private:
    int m_visibleCount;
    int m_camerasPerPage;
    int m_currentPage;
    int m_totalPages;
};

#endif // CAMERAGRIDSTATE_H
