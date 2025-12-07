#pragma once
#include <QObject>
#include <QString>
#include <QStorageInfo>
#include <QTimer>

/**
 * StorageService
 * - Tracks presence of an external removable drive (automount or kernel check).
 * - Exposes mount root, free space, and signals for UI/export gating.
 *
 * Requires:
 *   - DBus access to com.idonial.automount (optional but preferred).
 *   - Snap: add plug `removable-media` and `system-observe` if needed.
 */
class StorageService final : public QObject {
    Q_OBJECT
public:
    static StorageService* instance();

    bool hasExternal() const { return !externalRoot_.isEmpty(); }
    QString externalRoot() const { return externalRoot_; }
    qint64 freeBytes() const;

signals:
    void externalPresentChanged(bool present);
    void aboutToUnmount(const QString& root); // future use

public slots:
    void refresh(); // manual poll fallback

private:
    explicit StorageService(QObject* parent=nullptr);
    void setupDbus_();
    void rescanMounted_();
    static bool isKernelRemovable_(const QStorageInfo& storage);

private slots:
    // DBus hooks
    void onUsbMounted(QString device, QString path);
    void onUsbUnmounted(QString device, QString path);

private:
    QString externalRoot_;
    QTimer  pollTimer_;
};
