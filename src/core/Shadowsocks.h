#pragma once

#include "ProxyTunnel.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

// Shadowsocks client: Shadowsocks 2022 (SIP022: 2022-blake3-*) and the older
// AEAD ciphers (SIP004). Everything between this computer
// and the Shadowsocks server is encrypted and looks like random bytes, so the
// network in between sees neither the target host nor that the traffic is SSH.
//
// open() connects to the server and returns one end of a loopback socket pair;
// a background thread encrypts what is written to it and decrypts the answers.
// To the caller it behaves exactly like the socket of a SOCKS5 tunnel.
namespace shadowsocks {

QStringList methods();                       // supported cipher names, Shadowsocks 2022 first
bool isSupported(const QString &method);
// Shadowsocks 2022 takes a key, not a password: base64 of exactly 16/32 bytes
// (several "iPSK:uPSK" for servers with identity headers). Empty = fine.
QString keyProblem(const QString &method, const QString &password);

// waitForData: wait until the target sends its first bytes (an SSH server sends
// its banner at once); this turns a wrong password into a clear error.
sw_socket_t open(const ProxyConfig &server, const QString &targetHost, int targetPort,
                 int timeoutMs, bool waitForData, QString *error);

// ss://… links (SIP002 and the older base64 form) -> fields for the editor:
// {ok, name, host, port, method, password, error}
struct Link { bool ok = false; QString name, host, method, password, error; int port = 0; };
Link parseLink(const QString &uri);

// The decrypting half of a connection, fed with bytes as if from the server
// (fuzzing: must never crash or read out of bounds, whatever comes in).
QByteArray decodeServerBytesForFuzzing(const QString &method, const QString &password, const QByteArray &bytes, bool *ok);

// building blocks, exposed for the tests
QByteArray masterKey(const QByteArray &password, int keyLen);                 // EVP_BytesToKey (MD5)
QByteArray hkdfSha1(const QByteArray &key, const QByteArray &salt, const QByteArray &info, int len);

} // namespace shadowsocks
