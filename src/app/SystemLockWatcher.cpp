#include "SystemLockWatcher.h"

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <wtsapi32.h>
#endif
#ifdef SW_HAVE_DBUS
#  include <QDBusConnection>
#endif

#ifdef Q_OS_WIN
namespace {

LRESULT CALLBACK lockWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    auto *self = reinterpret_cast<SystemLockWatcher *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) {
        if (msg == WM_WTSSESSION_CHANGE && wp == WTS_SESSION_LOCK)
            self->emitLocked(QStringLiteral("lock"));
        else if (msg == WM_POWERBROADCAST && wp == PBT_APMSUSPEND)
            self->emitLocked(QStringLiteral("sleep"));
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace
#endif

SystemLockWatcher::SystemLockWatcher(QObject *parent)
    : QObject(parent)
{
#ifdef Q_OS_WIN
    // A top-level window (never shown): message-only windows do not get
    // WM_POWERBROADCAST.
    const wchar_t *cls = L"ShellWardenLockWatcher";
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = lockWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, cls, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
    if (hwnd) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION);
        m_hwnd = hwnd;
    }
#elif defined(SW_HAVE_DBUS)
    QDBusConnection sys = QDBusConnection::systemBus();
    sys.connect(QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1"),
                QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("PrepareForSleep"),
                this, SLOT(onPrepareForSleep(bool)));
    sys.connect(QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1/session/auto"),
                QStringLiteral("org.freedesktop.login1.Session"), QStringLiteral("Lock"),
                this, SLOT(onSessionLock()));
#endif
}

SystemLockWatcher::~SystemLockWatcher()
{
#ifdef Q_OS_WIN
    if (m_hwnd) {
        HWND hwnd = static_cast<HWND>(m_hwnd);
        WTSUnRegisterSessionNotification(hwnd);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        DestroyWindow(hwnd);
    }
#endif
}

void SystemLockWatcher::onPrepareForSleep(bool sleeping)
{
    if (sleeping)
        emit systemLocked(QStringLiteral("sleep"));
}

void SystemLockWatcher::onSessionLock()
{
    emit systemLocked(QStringLiteral("lock"));
}
