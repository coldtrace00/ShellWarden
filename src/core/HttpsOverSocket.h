#pragma once
#include <QByteArray>
#include <QList>
#include <QSslCertificate>
#include <QString>

// HTTPS GET over a socket that is already connected to the right place — for
// example a Shadowsocks tunnel (shadowsocks::open) to <host>:443. The TLS
// certificate is checked against the system's trusted CAs and `host`, so the
// one carrying the bytes (the Shadowsocks server) cannot read or change the
// answer. Blocking; meant for a background thread (bgthreads). Waits in
// half-second steps and gives up when bgthreads::stopping().
namespace https {

// Takes over `socket` (closes it). Returns the response body ("" on failure,
// with `error` set). Only HTTP 200 counts. `extraCa` is for tests only.
QByteArray get(long long socket, const QString &host, const QByteArray &path, int timeoutMs,
               QString *error, const QList<QSslCertificate> &extraCa = {});

} // namespace https
