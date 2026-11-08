#include "HttpsOverSocket.h"
#include "BackgroundThreads.h"
#include "SafeText.h"

#include <QElapsedTimer>
#include <QObject>
#include <QSslConfiguration>
#include <QSslSocket>

namespace https {

QByteArray get(long long socket, const QString &host, const QByteArray &path, int timeoutMs,
               QString *error, const QList<QSslCertificate> &extraCa)
{
    auto fail = [error](const QString &e) {
        if (error)
            *error = e;
        return QByteArray();
    };
    QSslSocket sock;
    if (!sock.setSocketDescriptor(qintptr(socket)))
        return fail(QObject::tr("TLS: cannot use the tunnel socket."));
    QSslConfiguration conf = QSslConfiguration::defaultConfiguration();
    if (!extraCa.isEmpty()) {
        QList<QSslCertificate> cas = conf.caCertificates();
        cas += extraCa;
        conf.setCaCertificates(cas);
    }
    conf.setPeerVerifyMode(QSslSocket::VerifyPeer);
    conf.setProtocol(QSsl::TlsV1_2OrLater);
    sock.setSslConfiguration(conf);
    sock.setPeerVerifyName(host);
    sock.startClientEncryption();

    QElapsedTimer t;
    t.start();
    while (!sock.isEncrypted()) {
        if (bgthreads::stopping() || t.elapsed() > timeoutMs)
            return fail(QObject::tr("TLS: no answer"));
        if (!sock.waitForEncrypted(500)) {
            if (sock.state() != QAbstractSocket::ConnectedState || !sock.sslHandshakeErrors().isEmpty()) {
                const auto errs = sock.sslHandshakeErrors();
                return fail(errs.isEmpty() ? QObject::tr("TLS: connection closed")
                                           : QObject::tr("TLS: %1 — something on the way intercepts HTTPS")
                                                 .arg(safetext::snippet(errs.first().errorString(), 120)));
            }
        }
    }

    const QByteArray req = "GET " + path + " HTTP/1.1\r\nHost: " + host.toLatin1()
                           + "\r\nUser-Agent: ShellWarden-SSH-Client\r\nAccept: application/json\r\nConnection: close\r\n\r\n";
    sock.write(req);
    QByteArray resp;
    while (resp.size() < 65536) {
        if (bgthreads::stopping() || t.elapsed() > timeoutMs)
            break;
        if (sock.waitForReadyRead(500)) {
            resp += sock.readAll();
            continue;
        }
        if (sock.state() != QAbstractSocket::ConnectedState)
            break;   // the server closed after its answer
    }
    resp += sock.readAll();
    sock.abort();

    const int headerEnd = int(resp.indexOf("\r\n\r\n"));
    if (headerEnd < 0)
        return fail(resp.isEmpty() ? QObject::tr("no answer") : QObject::tr("unexpected answer"));
    const QByteArray status = resp.left(int(resp.indexOf("\r\n")));
    if (!status.startsWith("HTTP/1.") || status.mid(9, 3) != "200")
        return fail(QObject::tr("unexpected answer"));
    QByteArray body = resp.mid(headerEnd + 4);
    if (resp.left(headerEnd).toLower().contains("transfer-encoding: chunked")) {
        // join the chunks (small JSON answers)
        QByteArray joined;
        int pos = 0;
        while (pos < body.size()) {
            const int eol = int(body.indexOf("\r\n", pos));
            if (eol < 0)
                break;
            bool ok = false;
            const int len = body.mid(pos, eol - pos).split(';').first().trimmed().toInt(&ok, 16);
            if (!ok || len <= 0 || eol + 2 + len > body.size())
                break;
            joined += body.mid(eol + 2, len);
            pos = eol + 2 + len + 2;
        }
        body = joined;
    }
    return body;
}

} // namespace https
