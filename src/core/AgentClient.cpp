#include "AgentClient.h"
#include "BackgroundThreads.h"
#include "ProxyTunnel.h"

#include <QObject>
#include <QtEndian>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#else
#  include <QLocalSocket>
#  include <poll.h>
#  include <sys/socket.h>
#endif

namespace agent {

namespace {
constexpr char SSH2_AGENTC_REQUEST_IDENTITIES = 11;
constexpr char SSH2_AGENT_IDENTITIES_ANSWER = 12;
constexpr int kMaxMessage = 256 * 1024;

#ifdef _WIN32
// ---- OpenSSH agent: named pipe ----
bool pipeRequest(const QByteArray &msg, QByteArray *reply)
{
    HANDLE h = CreateFileW(L"\\\\.\\pipe\\openssh-ssh-agent", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD n = 0;
    bool ok = WriteFile(h, msg.constData(), DWORD(msg.size()), &n, nullptr) && n == DWORD(msg.size());
    QByteArray out;
    if (ok) {
        char lenBuf[4];
        DWORD got = 0, total = 0;
        while (ok && total < 4) { ok = ReadFile(h, lenBuf + total, 4 - total, &got, nullptr) && got > 0; total += got; }
        const quint32 len = ok ? qFromBigEndian<quint32>(lenBuf) : 0;
        if (ok && len > 0 && len < quint32(kMaxMessage)) {
            out.resize(int(len) + 4);
            std::memcpy(out.data(), lenBuf, 4);
            total = 0;
            while (ok && total < len) { ok = ReadFile(h, out.data() + 4 + total, len - total, &got, nullptr) && got > 0; total += got; }
        } else {
            ok = false;
        }
    }
    CloseHandle(h);
    if (ok) *reply = out;
    return ok;
}

// ---- Pageant: WM_COPYDATA with a shared memory block ----
bool pageantRequest(const QByteArray &msg, QByteArray *reply)
{
    HWND hwnd = FindWindowW(L"Pageant", L"Pageant");
    if (!hwnd || msg.size() > 8192)
        return false;
    char mapName[64];
    std::snprintf(mapName, sizeof mapName, "PageantRequest%08lx", GetCurrentThreadId());
    HANDLE map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 8192, mapName);
    if (!map)
        return false;
    auto *mem = static_cast<char *>(MapViewOfFile(map, FILE_MAP_WRITE, 0, 0, 0));
    bool ok = false;
    if (mem) {
        std::memcpy(mem, msg.constData(), size_t(msg.size()));
        COPYDATASTRUCT cds;
        cds.dwData = 0x804e50ba;   // AGENT_COPYDATA_ID
        cds.cbData = DWORD(std::strlen(mapName) + 1);
        cds.lpData = mapName;
        if (SendMessageW(hwnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds)) != 0) {
            const quint32 len = qFromBigEndian<quint32>(mem);
            if (len > 0 && len + 4 <= 8192) {
                *reply = QByteArray(mem, int(len) + 4);
                ok = true;
            }
        }
        UnmapViewOfFile(mem);
    }
    CloseHandle(map);
    return ok;
}

bool pipeExists()
{
    return WaitNamedPipeW(L"\\\\.\\pipe\\openssh-ssh-agent", 0) || GetLastError() == ERROR_SEM_TIMEOUT;
}
#endif
} // namespace

QString available()
{
#ifdef _WIN32
    if (FindWindowW(L"Pageant", L"Pageant"))
        return QStringLiteral("Pageant");
    if (pipeExists())
        return QStringLiteral("OpenSSH agent");
    return {};
#else
    return qEnvironmentVariableIsEmpty("SSH_AUTH_SOCK") ? QString() : QStringLiteral("ssh-agent");
#endif
}

QByteArray request(const QByteArray &message, QString *error)
{
    QByteArray reply;
#ifdef _WIN32
    // Pageant first when it runs (most PuTTY users), then the OpenSSH agent
    if (FindWindowW(L"Pageant", L"Pageant") && pageantRequest(message, &reply))
        return reply;
    if (pipeRequest(message, &reply))
        return reply;
    if (error) *error = QObject::tr("No SSH agent is running (Pageant or the OpenSSH Authentication Agent service).");
    return {};
#else
    const QString path = qEnvironmentVariable("SSH_AUTH_SOCK");
    if (path.isEmpty()) {
        if (error) *error = QObject::tr("No SSH agent: SSH_AUTH_SOCK is not set.");
        return {};
    }
    QLocalSocket s;
    s.connectToServer(path);
    if (!s.waitForConnected(1500)) {
        if (error) *error = QObject::tr("Cannot reach the SSH agent: %1").arg(s.errorString());
        return {};
    }
    s.write(message);
    s.waitForBytesWritten(1500);
    while (reply.size() < 4 && s.waitForReadyRead(3000))
        reply += s.readAll();
    if (reply.size() >= 4) {
        const quint32 len = qFromBigEndian<quint32>(reply.constData());
        while (quint32(reply.size()) < len + 4 && len < quint32(kMaxMessage) && s.waitForReadyRead(5000))
            reply += s.readAll();
        if (quint32(reply.size()) >= len + 4)
            return reply.left(int(len) + 4);
    }
    if (error) *error = QObject::tr("The SSH agent did not answer.");
    return {};
#endif
}

int identityCount(QString *error)
{
    QByteArray msg(5, '\0');
    qToBigEndian<quint32>(1, msg.data());
    msg[4] = SSH2_AGENTC_REQUEST_IDENTITIES;
    const QByteArray r = request(msg, error);
    if (r.size() < 9 || r.at(4) != SSH2_AGENT_IDENTITIES_ANSWER)
        return -1;
    return int(qFromBigEndian<quint32>(r.constData() + 5));
}

// ---------------------------------------------------------------------------
// Socket for libssh (on Windows it can use an agent only through a socket, and
// the Windows agents are a pipe / a window). No port stays open: each call
// makes its own connected pair (proxytunnel::loopbackPair checks that the
// other end is really ours), hands one end to libssh and serves the other.
// Another program or another Windows user cannot get in between: there is
// nothing to connect to.
// ---------------------------------------------------------------------------
namespace {

// Waits in half-second steps so that bgthreads::stopping() ends the thread.
bool recvAll(sw_socket_t s, char *buf, int len)
{
    while (len > 0) {
#ifdef _WIN32
        WSAPOLLFD p = {};
        p.fd = s;
        p.events = POLLRDNORM;
        const int r = WSAPoll(&p, 1, 500);
#else
        pollfd p = {};
        p.fd = int(s);
        p.events = POLLIN;
        const int r = ::poll(&p, 1, 500);
#endif
        if (r < 0 || bgthreads::stopping())
            return false;
        if (r == 0)
            continue;
        const int n = int(::recv(s, buf, len, 0));
        if (n <= 0)
            return false;
        buf += n;
        len -= n;
    }
    return true;
}

void serveClient(sw_socket_t c)
{
    for (;;) {
        char lenBuf[4];
        if (!recvAll(c, lenBuf, 4)) break;
        const quint32 len = qFromBigEndian<quint32>(lenBuf);
        if (len == 0 || len > quint32(kMaxMessage)) break;
        QByteArray msg(int(len) + 4, '\0');
        std::memcpy(msg.data(), lenBuf, 4);
        if (!recvAll(c, msg.data() + 4, int(len))) break;
        QByteArray reply = request(msg, nullptr);
        if (reply.isEmpty()) {   // SSH_AGENT_FAILURE
            reply = QByteArray(5, '\0');
            qToBigEndian<quint32>(1, reply.data());
            reply[4] = 5;
        }
        if (!proxytunnel::writeAll(c, reply, 10000)) break;
    }
    bgthreads::unwatch(static_cast<long long>(c));
    proxytunnel::closeSocket(c);
}

} // namespace

long long bridgeSocket(QString *error)
{
    proxytunnel::initSockets();
    sw_socket_t ours = kInvalidSocket, theirs = kInvalidSocket;
    QString err;
    if (!proxytunnel::loopbackPair(&theirs, &ours, &err)) {
        if (error) *error = QObject::tr("Cannot connect libssh to the SSH agent: %1").arg(err);
        return -1;
    }
    bgthreads::watch(static_cast<long long>(ours));
    if (!bgthreads::start([ours] { serveClient(ours); })) {
        bgthreads::unwatch(static_cast<long long>(ours));
        proxytunnel::closeSocket(ours);
        proxytunnel::closeSocket(theirs);
        if (error) *error = QObject::tr("The program is closing.");
        return -1;
    }
    return static_cast<long long>(theirs);   // libssh owns and closes it
}

} // namespace agent
