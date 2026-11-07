// Port forwarding part of SshWorker:
//   Local   (-L) 127.0.0.1:bind → [ssh] → dest:port
//   Remote  (-R) server:bind   → [ssh] → dest:port reachable from this PC
//   Dynamic (-D) local SOCKS4/4a/5 proxy whose connections leave from the server
#include "SshWorker.h"

#include <QHostAddress>
#include <QtEndian>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QVariantMap>

namespace {
QString typeName(int t)
{
    switch (t) {
    case ForwardSpec::Remote: return QStringLiteral("R");
    case ForwardSpec::Dynamic: return QStringLiteral("D");
    default: return QStringLiteral("L");
    }
}
} // namespace

int SshWorker::forwardIndex(const QString &id) const
{
    for (int i = 0; i < m_forwards.size(); ++i) {
        if (m_forwards.at(i).spec.id == id && !m_forwards.at(i).removed)
            return i;
    }
    return -1;
}

void SshWorker::addForward(const ForwardSpec &spec)
{
    if (!m_session || !m_shellOpen)
        return;
    if (forwardIndex(spec.id) >= 0)
        removeForward(spec.id);
    FwdState f;
    f.spec = spec;
    f.state = QStringLiteral("starting");
    m_forwards.append(f);
    startForward(m_forwards.last());
    publishForwards(true);
}

void SshWorker::startForward(FwdState &f)
{
    const int index = int(&f - m_forwards.data());
    if (f.spec.type == ForwardSpec::Remote) {
        int bound = 0;
        const QByteArray bind = f.spec.bindHost.toUtf8();
        const int rc = ssh_channel_listen_forward(m_session, bind.isEmpty() ? nullptr : bind.constData(),
                                                  f.spec.bindPort, &bound);
        if (rc != SSH_OK) {
            f.state = QStringLiteral("error");
            f.error = tr("The server refused remote forwarding: %1").arg(QString::fromUtf8(ssh_get_error(m_session)));
            emit forwardEvent(f.spec.id, false, f.error);
            return;
        }
        f.boundPort = bound > 0 ? bound : f.spec.bindPort;
        f.state = QStringLiteral("active");
        emit forwardEvent(f.spec.id, true, tr("Server port %1 → %2:%3").arg(f.boundPort).arg(f.spec.destHost).arg(f.spec.destPort));
        return;
    }

    f.server = new QTcpServer(this);
    QHostAddress addr;
    if (f.spec.bindHost.isEmpty() || f.spec.bindHost == QLatin1String("localhost"))
        addr = QHostAddress::LocalHost;
    else if (!addr.setAddress(f.spec.bindHost))
        addr = QHostAddress::LocalHost;
    if (!f.server->listen(addr, quint16(f.spec.bindPort))) {
        f.state = QStringLiteral("error");
        f.error = tr("Cannot listen on %1:%2 — %3").arg(addr.toString()).arg(f.spec.bindPort).arg(f.server->errorString());
        delete f.server;
        f.server = nullptr;
        emit forwardEvent(f.spec.id, false, f.error);
        return;
    }
    f.boundPort = f.server->serverPort();
    f.state = QStringLiteral("active");
    const QString id = f.spec.id;
    connect(f.server, &QTcpServer::newConnection, this, [this, id] {
        const int i = forwardIndex(id);
        if (i >= 0)
            onLocalConnection(i);
    });
    Q_UNUSED(index);
    emit forwardEvent(f.spec.id, true,
                      f.spec.type == ForwardSpec::Dynamic
                          ? tr("SOCKS proxy on %1:%2").arg(addr.toString()).arg(f.boundPort)
                          : tr("%1:%2 → %3:%4").arg(addr.toString()).arg(f.boundPort).arg(f.spec.destHost).arg(f.spec.destPort));
}

void SshWorker::removeForward(const QString &id)
{
    const int i = forwardIndex(id);
    if (i < 0)
        return;
    FwdState &f = m_forwards[i];
    f.removed = true;
    if (f.server) {
        f.server->close();
        f.server->deleteLater();
        f.server = nullptr;
    }
    if (f.spec.type == ForwardSpec::Remote && m_session) {
        const QByteArray bind = f.spec.bindHost.toUtf8();
        ssh_channel_cancel_forward(m_session, bind.isEmpty() ? nullptr : bind.constData(), f.boundPort);
    }
    for (FwdConn *c : std::as_const(m_conns)) {
        if (c->fwd == i)
            c->dead = true;
    }
    publishForwards(true);
}

bool SshWorker::openChannelFor(FwdConn *c, const QString &host, int port)
{
    c->ch = ssh_channel_new(m_session);
    const QByteArray h = host.toUtf8();
    const QByteArray origin = c->sock->peerAddress().toString().toUtf8();
    if (!c->ch || ssh_channel_open_forward(c->ch, h.constData(), port, origin.constData(), c->sock->peerPort()) != SSH_OK) {
        if (c->ch) {
            ssh_channel_free(c->ch);
            c->ch = nullptr;
        }
        return false;
    }
    return true;
}

void SshWorker::onLocalConnection(int fwdIndex)
{
    FwdState &f = m_forwards[fwdIndex];
    while (f.server && f.server->hasPendingConnections()) {
        QTcpSocket *sock = f.server->nextPendingConnection();
        auto *c = new FwdConn;
        c->fwd = fwdIndex;
        c->sock = sock;
        sock->setParent(this);
        m_conns.append(c);

        if (f.spec.type == ForwardSpec::Dynamic) {
            c->socksPending = true;
        } else if (!openChannelFor(c, f.spec.destHost, f.spec.destPort)) {
            emit forwardEvent(f.spec.id, false, tr("Cannot open %1:%2 through the server: %3")
                                                    .arg(f.spec.destHost).arg(f.spec.destPort)
                                                    .arg(QString::fromUtf8(ssh_get_error(m_session))));
            c->dead = true;
        }
        connect(sock, &QTcpSocket::readyRead, this, [this, c] {
            if (c->dead)
                return;
            if (c->socksPending) {
                handleSocksData(c);
                return;
            }
            const QByteArray data = c->sock->readAll();
            if (data.isEmpty() || !c->ch)
                return;
            int off = 0;
            while (off < data.size()) {
                const int w = ssh_channel_write(c->ch, data.constData() + off, uint32_t(data.size() - off));
                if (w == SSH_ERROR) {
                    c->dead = true;
                    return;
                }
                off += w;
            }
            FwdState &fs = m_forwards[c->fwd];
            fs.bytesOut += data.size();
            fs.lastActivityMs = m_clock.elapsed();
        });
        connect(sock, &QTcpSocket::disconnected, this, [c] { c->dead = true; });
        if (sock->bytesAvailable())
            QMetaObject::invokeMethod(sock, "readyRead", Qt::QueuedConnection);
    }
}

// Minimal SOCKS server for dynamic forwarding: SOCKS5 (no auth, CONNECT) and
// SOCKS4/4a. Host names are passed to the SSH server unresolved, so DNS
// happens on the server side.
void SshWorker::handleSocksData(FwdConn *c)
{
    c->pending += c->sock->readAll();
    QByteArray &b = c->pending;
    if (b.isEmpty())
        return;
    const quint8 ver = quint8(b[0]);
    QString host;
    int port = 0;
    int consumed = 0;
    bool socks5 = ver == 5;

    if (socks5) {
        if (b.size() < 2)
            return;
        const int nmethods = quint8(b[1]);
        if (b.size() < 2 + nmethods)
            return;
        // stage 1: greeting
        if (!c->sock->property("sw_greeted").toBool()) {
            c->sock->write(QByteArray("\x05\x00", 2));
            c->sock->setProperty("sw_greeted", true);
            b.remove(0, 2 + nmethods);
            if (b.isEmpty())
                return;
        }
        if (b.size() < 5)
            return;
        if (quint8(b[1]) != 1) { // only CONNECT
            c->sock->write(QByteArray("\x05\x07\x00\x01\x00\x00\x00\x00\x00\x00", 10));
            c->dead = true;
            return;
        }
        const quint8 atyp = quint8(b[3]);
        int need = 0;
        if (atyp == 1) need = 4 + 4 + 2;
        else if (atyp == 3) need = 4 + 1 + quint8(b[4]) + 2;
        else if (atyp == 4) need = 4 + 16 + 2;
        else { c->dead = true; return; }
        if (b.size() < need)
            return;
        if (atyp == 1) {
            host = QHostAddress(qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(b.constData() + 4))).toString();
            port = (quint8(b[8]) << 8) | quint8(b[9]);
        } else if (atyp == 3) {
            const int len = quint8(b[4]);
            host = QString::fromUtf8(b.mid(5, len));
            port = (quint8(b[5 + len]) << 8) | quint8(b[6 + len]);
        } else {
            host = QHostAddress(reinterpret_cast<const quint8 *>(b.constData() + 4)).toString();
            port = (quint8(b[20]) << 8) | quint8(b[21]);
        }
        consumed = need;
    } else if (ver == 4) {
        const int nul = b.indexOf('\0', 8);
        if (b.size() < 9 || nul < 0)
            return;
        port = (quint8(b[2]) << 8) | quint8(b[3]);
        const quint32 ip = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(b.constData() + 4));
        consumed = nul + 1;
        if (ip > 0 && ip < 256) { // SOCKS4a: host name follows
            const int nul2 = b.indexOf('\0', consumed);
            if (nul2 < 0)
                return;
            host = QString::fromUtf8(b.mid(consumed, nul2 - consumed));
            consumed = nul2 + 1;
        } else {
            host = QHostAddress(ip).toString();
        }
    } else {
        c->dead = true;
        return;
    }

    b.remove(0, consumed);
    c->socksPending = false;
    const bool ok = openChannelFor(c, host, port);
    if (socks5)
        c->sock->write(ok ? QByteArray("\x05\x00\x00\x01\x00\x00\x00\x00\x00\x00", 10)
                          : QByteArray("\x05\x05\x00\x01\x00\x00\x00\x00\x00\x00", 10));
    else
        c->sock->write(ok ? QByteArray("\x00\x5a\x00\x00\x00\x00\x00\x00", 8)
                          : QByteArray("\x00\x5b\x00\x00\x00\x00\x00\x00", 8));
    if (!ok) {
        c->dead = true;
        return;
    }
    if (!b.isEmpty()) {
        ssh_channel_write(c->ch, b.constData(), uint32_t(b.size()));
        b.clear();
    }
}

void SshWorker::closeConn(FwdConn *c)
{
    if (c->ch) {
        if (ssh_channel_is_open(c->ch)) {
            ssh_channel_send_eof(c->ch);
            ssh_channel_close(c->ch);
        }
        ssh_channel_free(c->ch);
        c->ch = nullptr;
    }
    if (c->sock) {
        c->sock->disconnect(this);
        c->sock->disconnectFromHost();
        c->sock->deleteLater();
        c->sock = nullptr;
    }
}

void SshWorker::pumpForwards()
{
    if (!m_session || m_forwards.isEmpty())
        return;

    // Incoming connections for remote forwards.
    bool hasRemote = false;
    for (const FwdState &f : std::as_const(m_forwards))
        hasRemote = hasRemote || (f.spec.type == ForwardSpec::Remote && !f.removed && f.state == QLatin1String("active"));
    if (hasRemote) {
        for (int guard = 0; guard < 8; ++guard) {
            int dport = 0;
            char *originator = nullptr;
            int oport = 0;
            ssh_channel ch = ssh_channel_open_forward_port(m_session, 0, &dport, &originator, &oport);
            if (originator)
                ssh_string_free_char(originator);
            if (!ch)
                break;
            int idx = -1;
            for (int i = 0; i < m_forwards.size(); ++i) {
                const FwdState &f = m_forwards.at(i);
                if (f.spec.type == ForwardSpec::Remote && !f.removed && f.boundPort == dport)
                    idx = i;
            }
            if (idx < 0) {
                ssh_channel_close(ch);
                ssh_channel_free(ch);
                continue;
            }
            auto *c = new FwdConn;
            c->fwd = idx;
            c->ch = ch;
            c->sock = new QTcpSocket(this);
            m_conns.append(c);
            connect(c->sock, &QTcpSocket::connected, this, [this, c] {
                if (!c->pending.isEmpty()) {
                    c->sock->write(c->pending);
                    c->pending.clear();
                }
            });
            connect(c->sock, &QTcpSocket::readyRead, this, [this, c] {
                if (c->dead || !c->ch)
                    return;
                const QByteArray data = c->sock->readAll();
                ssh_channel_write(c->ch, data.constData(), uint32_t(data.size()));
                FwdState &fs = m_forwards[c->fwd];
                fs.bytesOut += data.size();
                fs.lastActivityMs = m_clock.elapsed();
            });
            connect(c->sock, &QTcpSocket::disconnected, this, [c] { c->dead = true; });
            connect(c->sock, &QTcpSocket::errorOccurred, this, [c](QAbstractSocket::SocketError) { c->dead = true; });
            const FwdState &f = m_forwards.at(idx);
            c->sock->connectToHost(f.spec.destHost, quint16(f.spec.destPort));
        }
    }

    // Channel → socket
    char buf[32 * 1024];
    for (FwdConn *c : std::as_const(m_conns)) {
        if (c->dead || !c->ch || !c->sock)
            continue;
        for (int guard = 0; guard < 16; ++guard) {
            const int n = ssh_channel_read_nonblocking(c->ch, buf, sizeof buf, 0);
            if (n == SSH_ERROR) {
                c->dead = true;
                break;
            }
            if (n <= 0)
                break;
            if (c->sock->state() == QAbstractSocket::ConnectedState)
                c->sock->write(buf, n);
            else
                c->pending.append(buf, n);
            FwdState &fs = m_forwards[c->fwd];
            fs.bytesIn += n;
            fs.lastActivityMs = m_clock.elapsed();
        }
        if (ssh_channel_is_eof(c->ch) || ssh_channel_is_closed(c->ch))
            c->dead = true;
    }

    // Reap closed connections.
    for (int i = int(m_conns.size()) - 1; i >= 0; --i) {
        FwdConn *c = m_conns.at(i);
        if (!c->dead)
            continue;
        if (c->sock && c->sock->bytesToWrite() > 0 && c->sock->state() == QAbstractSocket::ConnectedState) {
            c->sock->flush();
            continue; // let pending data drain first
        }
        closeConn(c);
        m_conns.removeAt(i);
        delete c;
    }
}

void SshWorker::publishForwards(bool force)
{
    QVariantList out;
    const qint64 now = m_clock.elapsed();
    for (int i = 0; i < m_forwards.size(); ++i) {
        const FwdState &f = m_forwards.at(i);
        if (f.removed)
            continue;
        int active = 0;
        for (const FwdConn *c : std::as_const(m_conns))
            active += (c->fwd == i && !c->dead) ? 1 : 0;
        const QString bind = (f.spec.type == ForwardSpec::Remote ? tr("server") : f.spec.bindHost)
                             + QLatin1Char(':') + QString::number(f.boundPort ? f.boundPort : f.spec.bindPort);
        const QString dest = f.spec.type == ForwardSpec::Dynamic
                                 ? QStringLiteral("SOCKS")
                                 : f.spec.destHost + QLatin1Char(':') + QString::number(f.spec.destPort);
        out << QVariantMap{
            {QStringLiteral("id"), f.spec.id},
            {QStringLiteral("type"), typeName(f.spec.type)},
            {QStringLiteral("bind"), bind},
            {QStringLiteral("dest"), dest},
            {QStringLiteral("state"), f.state},
            {QStringLiteral("error"), f.error},
            {QStringLiteral("active"), active},
            {QStringLiteral("bytesIn"), f.bytesIn},
            {QStringLiteral("bytesOut"), f.bytesOut},
            {QStringLiteral("idleMs"), f.lastActivityMs < 0 ? qint64(-1) : now - f.lastActivityMs},
        };
    }
    if (force || out != m_lastPublished) {
        m_lastPublished = out;
        emit forwardsUpdated(out);
    }
}
