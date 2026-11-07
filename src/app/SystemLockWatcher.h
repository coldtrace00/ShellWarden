#pragma once

#include <QObject>

// Tells when the computer is locked (Win+L, lock screen) or goes to sleep,
// so the vault can be locked at the same moment.
//  - Windows: a hidden window gets WM_WTSSESSION_CHANGE and WM_POWERBROADCAST.
//  - Linux:   systemd-logind over D-Bus (Session.Lock, Manager.PrepareForSleep),
//             when Qt was built with D-Bus.
class SystemLockWatcher : public QObject
{
    Q_OBJECT
public:
    explicit SystemLockWatcher(QObject *parent = nullptr);
    ~SystemLockWatcher() override;

    void emitLocked(const QString &reason) { emit systemLocked(reason); }

signals:
    void systemLocked(const QString &reason);   // "lock" | "sleep"

private slots:
    void onPrepareForSleep(bool sleeping);
    void onSessionLock();

private:
    void *m_hwnd = nullptr;
};
