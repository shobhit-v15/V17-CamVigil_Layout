#include "cameragridstate.h"
#include <algorithm>
#include <QDebug>

CameraGridState::CameraGridState(int camerasPerPage)
    : m_visibleCount(0)
    , m_camerasPerPage(camerasPerPage > 0 ? camerasPerPage : 1)
    , m_currentPage(0)
    , m_totalPages(1)
{
    recalcPages();
}

void CameraGridState::setVisibleCount(int count) {
    if (count < 0) count = 0;
    m_visibleCount = count;
    recalcPages();
    qInfo() << "[GridState] setVisibleCount =" << m_visibleCount
            << "pages =" << m_totalPages;
}

int CameraGridState::visibleCount() const {
    return m_visibleCount;
}

void CameraGridState::setCurrentPage(int page) {
    if (page < 0) page = 0;
    if (page >= m_totalPages) page = m_totalPages - 1;
    m_currentPage = page;
}

int CameraGridState::currentPage() const {
    return m_currentPage;
}

int CameraGridState::totalPages() const {
    return m_totalPages;
}

int CameraGridState::camerasPerPage() const {
    return m_camerasPerPage;
}

void CameraGridState::nextPage() {
    int before = m_currentPage;
    if (m_currentPage + 1 < m_totalPages) {
        ++m_currentPage;
    }
    qInfo() << "[GridState] nextPage from" << before << "to" << m_currentPage
            << "of" << m_totalPages;
}

void CameraGridState::previousPage() {
    int before = m_currentPage;
    if (m_currentPage > 0) {
        --m_currentPage;
    }
    qInfo() << "[GridState] previousPage from" << before << "to" << m_currentPage
            << "of" << m_totalPages;
}

void CameraGridState::recalcPages() {
    if (m_visibleCount <= 0) {
        m_totalPages = 1;
        m_currentPage = 0;
        return;
    }
    m_totalPages = (m_visibleCount + m_camerasPerPage - 1) / m_camerasPerPage;
    if (m_totalPages < 1) m_totalPages = 1;

    if (m_currentPage >= m_totalPages) {
        m_currentPage = m_totalPages - 1;
    }
    if (m_currentPage < 0) {
        m_currentPage = 0;
    }
}

int CameraGridState::cameraIndexForSlot(int page, int slot) const {
    if (slot < 0 || slot >= m_camerasPerPage) {
        return -1;
    }
    if (page < 0 || page >= m_totalPages) {
        return -1;
    }
    int idx = page * m_camerasPerPage + slot;
    if (idx >= m_visibleCount) {
        return -1;  // empty slot
    }
    return idx;     // visible camera index
}

int CameraGridState::pageForCamera(int visibleIndex) const {
    if (visibleIndex < 0 || visibleIndex >= m_visibleCount) {
        return -1;
    }
    return visibleIndex / m_camerasPerPage;
}

int CameraGridState::slotForCamera(int visibleIndex) const {
    if (visibleIndex < 0 || visibleIndex >= m_visibleCount) {
        return -1;
    }
    return visibleIndex % m_camerasPerPage;
}
