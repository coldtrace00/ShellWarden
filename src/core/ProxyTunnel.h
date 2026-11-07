#pragma once

#include <QString>
#include <cstdint>

// Opens a TCP connection to an SSH server through a SOCKS5 or HTTP CONNECT
// proxy and returns the connected socket, ready to be handed to libssh
// (SSH_OPTIONS_FD). The target host name is sent to the proxy unresolved,
// so DNS lookups for the target never leave this machine (no DNS leak).
//
// Everything here is blocking and meant to run on the session worker thread.

struct ProxyConfig
{
    // Tcp: a local port that already leads to the server (cloudflared access tcp), no handshake
    // Shadowsocks: encrypted proxy (see Shadowsocks.h); password + method
    enum Type { None = 0, Socks5 = 1, Http = 2, Tcp = 3, Shadowsocks = 4 };
    Type type = None;
    QString host;
    int port = 0;
    QString username;
    QString password;
    QString method;     // Shadowsocks cipher, e.g. chacha20-ietf-poly1305

    bool isSet() const { return type != None && !host.isEmpty() && port > 0; }
};

#ifdef _WIN32
using sw_socket_t = std::uintptr_t; // SOCKET
#else
using sw_socket_t = int;
#endif

constexpr sw_socket_t kInvalidSocket = sw_socket_t(~sw_socket_t(0));

namespace proxytunnel {

// Returns kInvalidSocket on failure and fills `error`.
sw_socket_t open(const ProxyConfig &proxy, const QString &targetHost, int targetPort,
                 int timeoutMs, QString *error);

void closeSocket(sw_socket_t s);

// Cheap liveness check for the kill switch: TCP connect to the proxy and, for
// SOCKS5, a full greeting + authentication. No traffic leaves the proxy.
bool probe(const ProxyConfig &proxy, int timeoutMs, QString *error);

// A connected pair of loopback TCP sockets (socketpair() replacement that
// also works on Windows). Used to feed a jump-host channel to libssh.
bool loopbackPair(sw_socket_t *a, sw_socket_t *b, QString *error);

// Direct TCP connection (used only for hosts without proxy).
sw_socket_t connectDirect(const QString &host, int port, int timeoutMs, QString *error);

// Reads what arrives within timeoutMs (at most len bytes). Returns the byte
// count, 0 when nothing came or the peer closed, -1 on error.
int readSome(sw_socket_t s, char *buf, int len, int timeoutMs);
bool writeAll(sw_socket_t s, const QByteArray &data, int timeoutMs);

// Call once per process before using sockets (WSAStartup on Windows).
void initSockets();

} // namespace proxytunnel
