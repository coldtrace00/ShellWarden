#include "AgentClient.h"
#include "BackgroundThreads.h"

#include <QObject>
#include <QtEndian>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#else
#  include <QLocalSocket>
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

#ifdef _WIN32
// ---------------------------------------------------------------------------
// 127.0.0.1 bridge for libssh (it wants a socket, Windows agents are a pipe / a window)
// ---------------------------------------------------------------------------
namespace {
std::once_flag g_bridgeOnce;
int g_bridgePort = 0;

bool recvAll(SOCKET s, char *buf, int len)
{
    while (len > 0) {
        const int n = recv(s, buf, len, 0);
        if (n <= 0) return false;
        buf += n; len -= n;
    }
    return true;
}

void serveClient(SOCKET c)
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
        if (send(c, reply.constData(), reply.size(), 0) != reply.size()) break;
    }
    bgthreads::unwatch(static_cast<long long>(c));
    closesocket(c);
}

void startBridge()
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv == INVALID_SOCKET) return;
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (bind(srv, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0 || listen(srv, 8) != 0) { closesocket(srv); return; }
    int alen = sizeof a;
    getsockname(srv, reinterpret_cast<sockaddr *>(&a), &alen);
    g_bridgePort = ntohs(a.sin_port);
    // on exit bgthreads::shutdown() closes srv and cuts the clients: the threads return
    bgthreads::watch(static_cast<long long>(srv), true);
    const bool started = bgthreads::start([srv] {
        for (;;) {
            SOCKET c = accept(srv, nullptr, nullptr);
            if (c == INVALID_SOCKET || bgthreads::stopping()) {
                if (c != INVALID_SOCKET) closesocket(c);
                break;
            }
            bgthreads::watch(static_cast<long long>(c));
            if (!bgthreads::start([c] { serveClient(c); })) {
                bgthreads::unwatch(static_cast<long long>(c));
                closesocket(c);
            }
        }
    });
    if (!started) {
        bgthreads::unwatch(static_cast<long long>(srv));
        closesocket(srv);
        g_bridgePort = 0;
    }
}
} // namespace

long long bridgeSocket(QString *error)
{
    std::call_once(g_bridgeOnce, startBridge);
    if (!g_bridgePort) {
        if (error) *error = QObject::tr("Cannot start the agent bridge.");
        return -1;
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(quint16(g_bridgePort));
    if (s == INVALID_SOCKET || ::connect(s, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0) {
        if (s != INVALID_SOCKET) closesocket(s);
        if (error) *error = QObject::tr("Cannot reach the agent bridge.");
        return -1;
    }
    return static_cast<long long>(s);
}
#endif

} // namespace agent
