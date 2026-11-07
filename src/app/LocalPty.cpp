#include "LocalPty.h"

#include <QDir>
#include <QMetaObject>

#include <algorithm>
#include <vector>

#ifdef Q_OS_WIN
#  include <windows.h>
#else
#  include <cerrno>
#  include <csignal>
#  include <cstdlib>
#  include <cstring>
#  include <fcntl.h>
#  include <poll.h>
#  include <pty.h>
#  include <sys/ioctl.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

#ifdef Q_OS_WIN
namespace {
// ConPTY is loaded at run time so the program still starts on old Windows.
using CreatePC = HRESULT(WINAPI *)(COORD, HANDLE, HANDLE, DWORD, void **);
using ResizePC = HRESULT(WINAPI *)(void *, COORD);
using ClosePC = void(WINAPI *)(void *);
struct ConPty
{
    CreatePC create = nullptr;
    ResizePC resize = nullptr;
    ClosePC close = nullptr;
    ConPty()
    {
        if (HMODULE k = GetModuleHandleW(L"kernel32.dll")) {
            create = reinterpret_cast<CreatePC>(GetProcAddress(k, "CreatePseudoConsole"));
            resize = reinterpret_cast<ResizePC>(GetProcAddress(k, "ResizePseudoConsole"));
            close = reinterpret_cast<ClosePC>(GetProcAddress(k, "ClosePseudoConsole"));
        }
    }
};
const ConPty &conpty() { static ConPty c; return c; }
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#  define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif
QString quoteArg(const QString &a)
{
    if (!a.isEmpty() && !a.contains(QLatin1Char(' ')) && !a.contains(QLatin1Char('"')))
        return a;
    QString q = a;
    q.replace(QLatin1String("\""), QLatin1String("\\\""));
    return QLatin1Char('"') + q + QLatin1Char('"');
}
} // namespace
#endif

LocalPty::LocalPty(QObject *parent)
    : QObject(parent)
{
}

LocalPty::~LocalPty()
{
    stop();
}

bool LocalPty::supported()
{
#ifdef Q_OS_WIN
    return conpty().create != nullptr;
#else
    return true;
#endif
}

bool LocalPty::start(const QString &program, const QStringList &args, int cols, int rows, QString *error)
{
    stop();
#ifdef Q_OS_WIN
    if (!conpty().create) {
        *error = tr("This Windows version has no pseudo console (Windows 10 1809 or newer is needed).");
        return false;
    }
    HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
    if (!CreatePipe(&inRead, &inWrite, nullptr, 0) || !CreatePipe(&outRead, &outWrite, nullptr, 0)) {
        *error = tr("Cannot create pipes.");
        return false;
    }
    void *hpc = nullptr;
    const COORD size{SHORT(std::max(cols, 2)), SHORT(std::max(rows, 2))};
    if (FAILED(conpty().create(size, inRead, outWrite, 0, &hpc))) {
        CloseHandle(inRead); CloseHandle(inWrite); CloseHandle(outRead); CloseHandle(outWrite);
        *error = tr("Cannot create the pseudo console.");
        return false;
    }
    // the console owns these ends now
    CloseHandle(inRead);
    CloseHandle(outWrite);

    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    QByteArray attrBuf(int(attrSize), 0);
    auto *attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize);
    UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hpc, sizeof(hpc), nullptr, nullptr);

    STARTUPINFOEXW si = {};
    si.StartupInfo.cb = sizeof(si);
    si.lpAttributeList = attrs;
    QString cmd = quoteArg(program);
    for (const QString &a : args)
        cmd += QLatin1Char(' ') + quoteArg(a);
    std::wstring wcmd = cmd.toStdWString();
    const std::wstring wdir = QDir::toNativeSeparators(QDir::homePath()).toStdWString();
    PROCESS_INFORMATION pi = {};
    const BOOL ok = CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT,
                                   nullptr, wdir.c_str(), &si.StartupInfo, &pi);
    DeleteProcThreadAttributeList(attrs);
    if (!ok) {
        *error = tr("Cannot start %1 (error %2).").arg(program).arg(GetLastError());
        conpty().close(hpc);
        CloseHandle(inWrite); CloseHandle(outRead);
        return false;
    }
    CloseHandle(pi.hThread);
    m_hpc = hpc;
    m_inWrite = inWrite;
    m_outRead = outRead;
    m_process = pi.hProcess;
#else
    struct winsize ws = {};
    ws.ws_col = static_cast<unsigned short>(std::max(cols, 2));
    ws.ws_row = static_cast<unsigned short>(std::max(rows, 2));
    QByteArrayList argv8;
    argv8 << program.toLocal8Bit();
    for (const QString &a : args)
        argv8 << a.toLocal8Bit();
    const QByteArray home = QDir::homePath().toLocal8Bit();
    int master = -1;
    const pid_t pid = forkpty(&master, nullptr, nullptr, &ws);
    if (pid < 0) {
        *error = tr("Cannot create a terminal: %1").arg(QString::fromLocal8Bit(strerror(errno)));
        return false;
    }
    if (pid == 0) {
        // child: become the shell
        std::vector<char *> argv;
        for (QByteArray &a : argv8)
            argv.push_back(a.data());
        argv.push_back(nullptr);
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        if (chdir(home.constData()) != 0) { /* stay where we are */ }
        execvp(argv[0], argv.data());
        _exit(127);
    }
    m_master = master;
    m_pid = pid;
#endif
    m_running = true;
    m_reader = std::thread([this] { readLoop(); });
    return true;
}

void LocalPty::readLoop()
{
    char buf[16384];
    for (;;) {
#ifdef Q_OS_WIN
        DWORD n = 0;
        if (!ReadFile(static_cast<HANDLE>(m_outRead), buf, sizeof buf, &n, nullptr) || n == 0)
            break;
#else
        // poll with a timeout, so stop() can end this loop without closing the fd under it
        struct pollfd pfd = {m_master, POLLIN, 0};
        const int pr = ::poll(&pfd, 1, 200);
        if (!m_running)
            break;
        if (pr == 0 || (pr < 0 && errno == EINTR))
            continue;
        const ssize_t n = ::read(m_master, buf, sizeof buf);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
#endif
        const QByteArray chunk(buf, int(n));
        QMetaObject::invokeMethod(this, [this, chunk] { emit dataReceived(chunk); }, Qt::QueuedConnection);
    }
    int code = 0;
#ifdef Q_OS_WIN
    if (m_process) {
        WaitForSingleObject(static_cast<HANDLE>(m_process), 2000);
        DWORD c = 0;
        GetExitCodeProcess(static_cast<HANDLE>(m_process), &c);
        code = int(c);
    }
#else
    if (m_pid > 0) {
        if (!m_running)
            kill(m_pid, SIGHUP);   // stopped by us: end the shell
        int st = 0;
        for (int i = 0; i < 50; ++i) {   // up to 1 s, then force
            const pid_t w = waitpid(m_pid, &st, WNOHANG);
            if (w == m_pid) {
                code = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
                break;
            }
            if (w < 0)
                break;
            if (i == 49) { kill(m_pid, SIGKILL); waitpid(m_pid, &st, 0); code = 137; }
            usleep(20000);
        }
        m_pid = -1;
    }
#endif
    if (m_running.exchange(false))
        QMetaObject::invokeMethod(this, [this, code] { emit finished(code); }, Qt::QueuedConnection);
}

void LocalPty::write(const QByteArray &data)
{
    if (!m_running || data.isEmpty())
        return;
#ifdef Q_OS_WIN
    DWORD done = 0;
    WriteFile(static_cast<HANDLE>(m_inWrite), data.constData(), DWORD(data.size()), &done, nullptr);
#else
    qsizetype off = 0;
    while (off < data.size()) {
        const ssize_t n = ::write(m_master, data.constData() + off, size_t(data.size() - off));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        off += n;
    }
#endif
}

void LocalPty::resize(int cols, int rows)
{
    if (!m_running)
        return;
#ifdef Q_OS_WIN
    if (conpty().resize && m_hpc)
        conpty().resize(m_hpc, COORD{SHORT(std::max(cols, 2)), SHORT(std::max(rows, 2))});
#else
    struct winsize ws = {};
    ws.ws_col = static_cast<unsigned short>(std::max(cols, 2));
    ws.ws_row = static_cast<unsigned short>(std::max(rows, 2));
    ioctl(m_master, TIOCSWINSZ, &ws);
#endif
}

void LocalPty::stop()
{
    const bool wasRunning = m_running.exchange(false);
#ifdef Q_OS_WIN
    if (m_hpc) {
        // closing the console ends the shell and makes ReadFile return
        conpty().close(m_hpc);
        m_hpc = nullptr;
    }
    if (m_process && wasRunning)
        TerminateProcess(static_cast<HANDLE>(m_process), 0);
    if (m_reader.joinable())
        m_reader.join();
    for (void **h : {&m_inWrite, &m_outRead, &m_process}) {
        if (*h) { CloseHandle(static_cast<HANDLE>(*h)); *h = nullptr; }
    }
#else
    Q_UNUSED(wasRunning)
    if (m_reader.joinable())
        m_reader.join();      // leaves within 200 ms and reaps the shell
    if (m_master >= 0) {
        ::close(m_master);
        m_master = -1;
    }
#endif
}
