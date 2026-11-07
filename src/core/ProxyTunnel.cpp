#include "ProxyTunnel.h"
#include "SecureBytes.h"
#include "Shadowsocks.h"

#include <QByteArray>
#include <QHostAddress>
#include <QObject>

#include <chrono>
#include <cstring>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
using native_socket = SOCKET;
#  define SW_CLOSE closesocket
#  define SW_LASTERR WSAGetLastError()
#else
#  include <arpa/inet.h>
#  include <cerrno>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <unistd.h>
using native_socket = int;
#  define SW_CLOSE ::close
#  define SW_LASTERR errno
#endif

namespace proxytunnel {

namespace {

using Clock = std::chrono::steady_clock;

void setNonBlocking(native_socket s, bool on)
{
#ifdef _WIN32
    u_long mode = on ? 1 : 0;
    ioctlsocket(s, FIONBIO, &mode);
#else
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
#endif
}

// Waits until the socket is readable/writable or the deadline passes.
bool waitFor(native_socket s, bool forWrite, Clock::time_point deadline)
{
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    if (left <= 0)
        return false;
#ifdef _WIN32
    WSAPOLLFD p{};
    p.fd = s;
    p.events = forWrite ? POLLWRNORM : POLLRDNORM;
    return WSAPoll(&p, 1, int(left)) > 0;
#else
    pollfd p{};
    p.fd = s;
    p.events = forWrite ? POLLOUT : POLLIN;
    return ::poll(&p, 1, int(left)) > 0;
#endif
}

bool sendAll(native_socket s, const QByteArray &data, Clock::time_point deadline)
{
    int sent = 0;
    while (sent < data.size()) {
        if (!waitFor(s, true, deadline))
            return false;
        const int n = int(::send(s, data.constData() + sent, data.size() - sent, 0));
        if (n <= 0)
            return false;
        sent += n;
    }
    return true;
}

bool recvExact(native_socket s, char *buf, int len, Clock::time_point deadline)
{
    int got = 0;
    while (got < len) {
        if (!waitFor(s, false, deadline))
            return false;
        const int n = int(::recv(s, buf + got, len - got, 0));
        if (n <= 0)
            return false;
        got += n;
    }
    return true;
}

native_socket connectTcp(const QString &host, int port, Clock::time_point deadline, QString *error)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo *res = nullptr;
    const QByteArray h = host.toUtf8();
    const QByteArray p = QByteArray::number(port);
    if (getaddrinfo(h.constData(), p.constData(), &hints, &res) != 0 || !res) {
        if (error)
            *error = QObject::tr("Cannot resolve host %1").arg(host);
        return native_socket(kInvalidSocket);
    }

    native_socket result = native_socket(kInvalidSocket);
    for (addrinfo *ai = res; ai; ai = ai->ai_next) {
        native_socket s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == native_socket(kInvalidSocket))
            continue;
        setNonBlocking(s, true);
        int rc = ::connect(s, ai->ai_addr, int(ai->ai_addrlen));
        bool ok = rc == 0;
        if (!ok) {
#ifdef _WIN32
            const bool pending = WSAGetLastError() == WSAEWOULDBLOCK;
#else
            const bool pending = errno == EINPROGRESS;
#endif
            if (pending && waitFor(s, true, deadline)) {
                int soErr = 0;
                socklen_t len = sizeof soErr;
                getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&soErr), &len);
                ok = soErr == 0;
            }
        }
        if (ok) {
            result = s;
            break;
        }
        SW_CLOSE(s);
    }
    freeaddrinfo(res);

    if (result == native_socket(kInvalidSocket) && error)
        *error = QObject::tr("Cannot connect to %1:%2").arg(host).arg(port);
    return result;
}

bool socks5Handshake(native_socket s, const ProxyConfig &proxy, const QString &host, int port,
                     Clock::time_point deadline, QString *error)
{
    const bool withAuth = !proxy.username.isEmpty();
    QByteArray hello;
    hello.append(char(0x05));
    if (withAuth) {
        hello.append(char(0x02)).append(char(0x00)).append(char(0x02));
    } else {
        hello.append(char(0x01)).append(char(0x00));
    }
    char reply[2];
    if (!sendAll(s, hello, deadline) || !recvExact(s, reply, 2, deadline) || reply[0] != 0x05) {
        if (error)
            *error = QObject::tr("The proxy did not answer as a SOCKS5 server.");
        return false;
    }

    if (quint8(reply[1]) == 0x02) {
        QByteArray user = proxy.username.toUtf8();
        QByteArray pass = proxy.password.toUtf8();
        secure::ByteGuard passGuard(pass);
        if (user.size() > 255 || pass.size() > 255) {
            if (error)
                *error = QObject::tr("Proxy username or password is too long.");
            return false;
        }
        QByteArray auth;
        auth.append(char(0x01));
        auth.append(char(user.size())).append(user);
        auth.append(char(pass.size())).append(pass);
        secure::ByteGuard authGuard(auth);
        char ar[2];
        if (!sendAll(s, auth, deadline) || !recvExact(s, ar, 2, deadline) || ar[1] != 0x00) {
            if (error)
                *error = QObject::tr("The proxy rejected the username or password.");
            return false;
        }
    } else if (quint8(reply[1]) != 0x00) {
        if (error)
            *error = withAuth ? QObject::tr("The proxy offers no supported authentication method.")
                              : QObject::tr("The proxy requires a username and password.");
        return false;
    }

    QByteArray req;
    req.append(char(0x05)).append(char(0x01)).append(char(0x00));
    QHostAddress addr;
    if (addr.setAddress(host) && addr.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 v4 = addr.toIPv4Address();
        req.append(char(0x01));
        req.append(char(v4 >> 24)).append(char(v4 >> 16)).append(char(v4 >> 8)).append(char(v4));
    } else if (addr.setAddress(host) && addr.protocol() == QAbstractSocket::IPv6Protocol) {
        const Q_IPV6ADDR v6 = addr.toIPv6Address();
        req.append(char(0x04));
        req.append(reinterpret_cast<const char *>(v6.c), 16);
    } else {
        const QByteArray name = host.toUtf8();
        if (name.size() > 255) {
            if (error)
                *error = QObject::tr("Host name is too long.");
            return false;
        }
        req.append(char(0x03)).append(char(name.size())).append(name); // remote DNS
    }
    req.append(char((port >> 8) & 0xff)).append(char(port & 0xff));

    char head[4];
    if (!sendAll(s, req, deadline) || !recvExact(s, head, 4, deadline) || head[0] != 0x05) {
        if (error)
            *error = QObject::tr("The proxy closed the connection.");
        return false;
    }
    if (head[1] != 0x00) {
        static const char *reasons[] = {
            "succeeded", "general SOCKS server failure", "connection not allowed by ruleset",
            "network unreachable", "host unreachable", "connection refused",
            "TTL expired", "command not supported", "address type not supported"};
        const int code = quint8(head[1]);
        if (error)
            *error = QObject::tr("Proxy could not reach %1:%2 (%3).")
                         .arg(host).arg(port)
                         .arg(QString::fromLatin1(code < 9 ? reasons[code] : "unknown error"));
        return false;
    }
    // Skip the bound address in the reply.
    int skip = 0;
    switch (head[3]) {
    case 0x01: skip = 4 + 2; break;
    case 0x04: skip = 16 + 2; break;
    case 0x03: {
        char len = 0;
        if (!recvExact(s, &len, 1, deadline))
            return false;
        skip = quint8(len) + 2;
        break;
    }
    default:
        if (error)
            *error = QObject::tr("Malformed SOCKS5 reply.");
        return false;
    }
    QByteArray tail(skip, 0);
    return recvExact(s, tail.data(), skip, deadline);
}

bool httpConnectHandshake(native_socket s, const ProxyConfig &proxy, const QString &host, int port,
                          Clock::time_point deadline, QString *error)
{
    QHostAddress addr;
    const bool isV6 = addr.setAddress(host) && addr.protocol() == QAbstractSocket::IPv6Protocol;
    const QByteArray authority = (isV6 ? "[" + host.toUtf8() + "]" : host.toUtf8())
                                 + ":" + QByteArray::number(port);
    QByteArray req = "CONNECT " + authority + " HTTP/1.1\r\nHost: " + authority + "\r\n";
    QByteArray credentials;
    secure::ByteGuard credGuard(credentials);
    if (!proxy.username.isEmpty()) {
        credentials = (proxy.username + QLatin1Char(':') + proxy.password).toUtf8().toBase64();
        req += "Proxy-Authorization: Basic " + credentials + "\r\n";
    }
    req += "\r\n";
    secure::ByteGuard reqGuard(req);

    if (!sendAll(s, req, deadline)) {
        if (error)
            *error = QObject::tr("Cannot send the request to the HTTP proxy.");
        return false;
    }

    // Read byte by byte up to the end of headers so we never consume SSH data.
    QByteArray resp;
    char c = 0;
    while (!resp.endsWith("\r\n\r\n")) {
        if (resp.size() > 16384 || !recvExact(s, &c, 1, deadline)) {
            if (error)
                *error = QObject::tr("The HTTP proxy did not answer.");
            return false;
        }
        resp.append(c);
    }
    const QByteArray status = resp.left(resp.indexOf("\r\n"));
    const QList<QByteArray> parts = status.split(' ');
    if (parts.size() < 2 || parts.at(1) != "200") {
        if (error) {
            if (parts.size() >= 2 && parts.at(1) == "407")
                *error = QObject::tr("The HTTP proxy requires authentication (407).");
            else
                *error = QObject::tr("The HTTP proxy refused the tunnel: %1").arg(QString::fromLatin1(status));
        }
        return false;
    }
    return true;
}

} // namespace

void initSockets()
{
#ifdef _WIN32
    static bool done = false;
    if (!done) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        done = true;
    }
#endif
}

void closeSocket(sw_socket_t s)
{
    if (s != kInvalidSocket)
        SW_CLOSE(native_socket(s));
}

bool probe(const ProxyConfig &proxy, int timeoutMs, QString *error)
{
    initSockets();
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    native_socket s = connectTcp(proxy.host, proxy.port, deadline, error);
    if (s == native_socket(kInvalidSocket))
        return false;
    bool ok = true;
    if (proxy.type == ProxyConfig::Socks5) {
        const bool withAuth = !proxy.username.isEmpty();
        QByteArray hello;
        hello.append(char(0x05));
        if (withAuth)
            hello.append(char(0x02)).append(char(0x00)).append(char(0x02));
        else
            hello.append(char(0x01)).append(char(0x00));
        char reply[2];
        ok = sendAll(s, hello, deadline) && recvExact(s, reply, 2, deadline) && reply[0] == 0x05;
        if (ok && quint8(reply[1]) == 0x02) {
            QByteArray user = proxy.username.toUtf8();
            QByteArray pass = proxy.password.toUtf8();
            secure::ByteGuard pg(pass);
            QByteArray auth;
            auth.append(char(0x01)).append(char(user.size())).append(user).append(char(pass.size())).append(pass);
            secure::ByteGuard ag(auth);
            char ar[2];
            ok = sendAll(s, auth, deadline) && recvExact(s, ar, 2, deadline) && ar[1] == 0x00;
            if (!ok && error)
                *error = QObject::tr("The proxy rejected the username or password.");
        } else if (ok && quint8(reply[1]) != 0x00) {
            ok = false;
            if (error)
                *error = QObject::tr("The proxy requires a username and password.");
        } else if (!ok && error) {
            *error = QObject::tr("The proxy did not answer as a SOCKS5 server.");
        }
    }
    SW_CLOSE(s);
    return ok;
}

bool loopbackPair(sw_socket_t *a, sw_socket_t *b, QString *error)
{
    initSockets();
    native_socket listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == native_socket(kInvalidSocket)) {
        if (error)
            *error = QObject::tr("Cannot create a local socket.");
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    socklen_t len = sizeof addr;
    native_socket client = native_socket(kInvalidSocket), server = native_socket(kInvalidSocket);
    if (::bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof addr) == 0
        && ::listen(listener, 1) == 0
        && ::getsockname(listener, reinterpret_cast<sockaddr *>(&addr), &len) == 0) {
        client = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (client != native_socket(kInvalidSocket)
            && ::connect(client, reinterpret_cast<sockaddr *>(&addr), sizeof addr) == 0)
            server = ::accept(listener, nullptr, nullptr);
    }
    SW_CLOSE(listener);
    if (server == native_socket(kInvalidSocket)) {
        if (client != native_socket(kInvalidSocket))
            SW_CLOSE(client);
        if (error)
            *error = QObject::tr("Cannot create a local socket.");
        return false;
    }
    int one = 1;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&one), sizeof one);
    setsockopt(server, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&one), sizeof one);
    *a = sw_socket_t(server);
    *b = sw_socket_t(client);
    return true;
}

sw_socket_t connectDirect(const QString &host, int port, int timeoutMs, QString *error)
{
    initSockets();
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    native_socket s = connectTcp(host, port, deadline, error);
    if (s == native_socket(kInvalidSocket))
        return kInvalidSocket;
    setNonBlocking(s, false);
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&one), sizeof one);
    return sw_socket_t(s);
}

bool writeAll(sw_socket_t s, const QByteArray &data, int timeoutMs)
{
    return sendAll(native_socket(s), data, Clock::now() + std::chrono::milliseconds(timeoutMs));
}

int readSome(sw_socket_t s, char *buf, int len, int timeoutMs)
{
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    if (!waitFor(native_socket(s), false, deadline))
        return 0;
    const int n = int(::recv(native_socket(s), buf, len, 0));
    return n < 0 ? -1 : n;
}

sw_socket_t open(const ProxyConfig &proxy, const QString &targetHost, int targetPort,
                 int timeoutMs, QString *error)
{
    initSockets();
    if (proxy.type == ProxyConfig::Shadowsocks)   // SSH servers greet first: wait for that to check the password
        return shadowsocks::open(proxy, targetHost, targetPort, timeoutMs, true, error);
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);

    native_socket s = connectTcp(proxy.host, proxy.port, deadline, error);
    if (s == native_socket(kInvalidSocket))
        return kInvalidSocket;

    const bool ok = proxy.type == ProxyConfig::Tcp ? true
                    : proxy.type == ProxyConfig::Socks5
                        ? socks5Handshake(s, proxy, targetHost, targetPort, deadline, error)
                        : httpConnectHandshake(s, proxy, targetHost, targetPort, deadline, error);
    if (!ok) {
        if (error && error->isEmpty())
            *error = QObject::tr("Proxy handshake timed out.");
        SW_CLOSE(s);
        return kInvalidSocket;
    }

    setNonBlocking(s, false); // libssh expects a regular blocking socket
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&one), sizeof one);
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char *>(&one), sizeof one);
    return sw_socket_t(s);
}

} // namespace proxytunnel
