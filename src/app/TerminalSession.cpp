#include "TerminalSession.h"
#include <QRegularExpression>
#include "LocalPty.h"
#include "core/ActivityLog.h"
#include "core/SecureBytes.h"

#include <QDateTime>
#include <algorithm>
#include <QDeadlineTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QTcpSocket>
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QFile>
#include <QTimer>
#include <QUrl>
#include <QUuid>

#ifdef SW_HAVE_SERIAL
#  include <QSerialPort>
#endif

namespace {
QString formatBytes(qint64 b)
{
    if (b < 1024) return QStringLiteral("%1 B").arg(b);
    if (b < 1024 * 1024) return QStringLiteral("%1 KB").arg(b / 1024.0, 0, 'f', 1);
    if (b < 1024ll * 1024 * 1024) return QStringLiteral("%1 MB").arg(b / 1048576.0, 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(b / 1073741824.0, 0, 'f', 2);
}
} // namespace

TerminalSession::TerminalSession(QString hostId, QString label, QString endpoint, QString kind, QObject *parent)
    : QObject(parent), m_hostId(std::move(hostId)), m_label(std::move(label)), m_endpoint(std::move(endpoint)),
      m_kind(std::move(kind))
{
    // Keys typed in any view → remote side.
    connect(&m_emu, &TerminalEmulator::output, this, &TerminalSession::write);
}

TerminalSession::~TerminalSession()
{
    setBroadcast(false);
    stopRecording();
    stopWorker();
    stopSerial();
}

template<typename F> void TerminalSession::onWorker(F &&f)
{
    if (!m_worker)
        return;
    SshWorker *w = m_worker;
    QMetaObject::invokeMethod(w, [w, f] { f(w); }, Qt::QueuedConnection);
}

void TerminalSession::setColor(const QString &c)
{
    if (c == m_color)
        return;
    m_color = c;
    emit changed();
}

void TerminalSession::setThemeId(const QString &id)
{
    if (id == m_emu.themeId())
        return;
    m_emu.setTheme(id);
    emit changed();
}

void TerminalSession::setDetached(bool d)
{
    if (d == m_detached)
        return;
    m_detached = d;
    emit detachedChanged();
}

void TerminalSession::markActivity()
{
    m_lastActivity = QDateTime::currentMSecsSinceEpoch();
    if (!m_activity) {
        m_activity = true;
        emit activityChanged();
        QTimer::singleShot(700, this, [this] {
            if (QDateTime::currentMSecsSinceEpoch() - m_lastActivity >= 650) {
                m_activity = false;
                emit activityChanged();
            } else {
                m_activity = false;     // will be re-raised by the next chunk
                emit activityChanged();
            }
        });
    }
}

void TerminalSession::setState(State s, const QString &status)
{
    m_state = s;
    if (!status.isNull())
        m_status = status;
    emit changed();
}

void TerminalSession::setRoute(const QString &text, bool viaProxy, const QString &proxyId, bool killSwitch,
                               const QString &jumpLabel)
{
    m_route = text;
    m_viaProxy = viaProxy;
    m_proxyId = proxyId;
    m_killSwitch = killSwitch;
    m_jumpLabel = jumpLabel;
    emit changed();
}

void TerminalSession::setPreparing(const QString &text)
{
    m_closedWithError = false;
    m_seenIp.clear();
    m_seenPort.clear();
    m_serverIp.clear();
    setState(Preparing, text);
}

void TerminalSession::setExpected(const QString &ip, const QString &location, const QString &country)
{
    m_expectedIp = ip;
    m_expectedLocation = location;
    m_expectedCountry = country.trimmed().toUpper();
    emit changed();
}

void TerminalSession::begin(const ConnectParams &params)
{
    stopWorker();
    if (m_started) {
        m_emu.reset();
        emit sessionRestarted();
    }
    m_started = true;
    m_forwards.clear();
    emit forwardsChanged();

    ConnectParams p = params;
    m_dnsHost = params.host;
    m_dnsProxy = params.proxy;
    m_sshfpState.clear();
    p.cols = m_cols;
    p.rows = m_rows;

    m_thread = new QThread;
    liveThreads().removeAll(nullptr);
    liveThreads().append(m_thread);
    m_thread->setObjectName(QStringLiteral("ssh:") + m_label);
    auto *worker = new SshWorker;
    worker->moveToThread(m_thread);
    m_worker = worker;

    // The thread and worker clean themselves up whenever they finish, even
    // if this tab is already gone.
    connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    connect(worker, &SshWorker::status, this, [this](const QString &t) {
        m_status = t;
        emit changed();
    });
    connect(worker, &SshWorker::hostKeyCheck, this, [this](int st, const QString &type, const QString &fp) {
        m_hkStatus = st;
        m_hkType = type;
        m_hkFingerprint = fp;
        if (st != SshWorker::HostUnknown)
            emit logEvent(ActivityLog::Security, QStringLiteral("session"), tr("Server host key CHANGED"), type + QLatin1Char(' ') + fp);
        lookupSshfp(fp);
        setState(HostKeyPrompt, tr("Verify the server's host key"));
    });
    connect(worker, &SshWorker::secretRequired, this, [this](int kind, const QString &prompt, bool echo, bool retry) {
        m_secretKind = kind;
        m_secretPrompt = prompt;
        m_secretEcho = echo;
        m_secretRetry = retry;
        if (retry)
            emit logEvent(ActivityLog::Warning, QStringLiteral("session"), tr("Authentication rejected, asking again"), QString());
        setState(SecretPrompt);
    });
    connect(worker, &SshWorker::secretAccepted, this, [this](int kind, const QString &secret) {
        if (kind >= 0 && kind < 2 && m_saveRequested[kind]) {
            m_saveRequested[kind] = false;
            emit saveSecretRequested(m_hostId, kind, secret);
        }
    });
    connect(worker, &SshWorker::connected, this, [this] {
        setState(Connected, tr("Connected"));
        m_wasConnected = true;
        if (m_monOn)
            QTimer::singleShot(1500, this, [this] { setMonitorConfig(m_monOn, m_monInterval); });
        if (m_retry) { m_retry = 0; emit reconnectChanged(); }
        emit logEvent(ActivityLog::Success, QStringLiteral("session"), tr("Connected"), m_route);
        // Own theme with "input colour" set to apply automatically (bash prompt).
        QTimer::singleShot(900, this, [this] {
            const TerminalTheme &t = m_emu.theme();
            if (m_state == Connected && t.custom && t.autoInput && t.input.isValid())
            {
                // The trailing printf wipes the echoed command (it may wrap over
                // several lines), so the screen looks untouched.
                const QString base = TerminalEmulator::inputColorCommand(t.input);
                const int cols = std::max(1, m_emu.cols());
                const int col = m_emu.cursor().col;
                const int len = int(base.size()) + 20;
                const int lines = (col + len + cols - 1) / cols;
                write((base + QStringLiteral("; printf '\\e[%1A\\e[J'\r").arg(lines)).toUtf8());
            }
        });
    });
    connect(worker, &SshWorker::clientAddressSeen, this,
            [this](const QString &ip, const QString &port, const QString &serverIp) {
                m_seenIp = ip;
                m_seenPort = port;
                m_serverIp = serverIp;
                emit changed();
                emit logEvent(ActivityLog::Info, QStringLiteral("session"),
                              tr("Server sees the client as %1").arg(ip),
                              tr("port %1 → server %2").arg(port, serverIp));
            });
    connect(worker, &SshWorker::dataReceived, this, [this](const QByteArray &d) {
        incoming(d);
        markActivity();
    });
    connect(worker, &SshWorker::execDone, this, [this](int reqId, int code, const QByteArray &out, const QString &err) {
        if (reqId != 1) return;
        if (code < 0) m_monRunning = 0;   // the monitor channel ended: a later setting change restarts it
        parseMonitor(out, err);
    });
    connect(worker, &SshWorker::finished, this, [this](const QString &reason, bool isError) {
        m_cpuTotal = m_rx = 0;
        m_monRunning = 0;
        m_monitor.clear();
        emit monitorChanged();
        m_closedWithError = isError;
        setState(Closed, reason);
        emit logEvent(isError ? ActivityLog::Error : ActivityLog::Info, QStringLiteral("session"),
                      isError ? tr("Connection failed / lost") : tr("Disconnected"), reason);
        m_forwards.clear();
        emit forwardsChanged();
        if (m_thread)
            m_thread->quit();
        m_thread = nullptr;
        // Lost an established connection (network drop, sleep, server restart):
        // try again with growing pauses. Never after an auth failure or a
        // kill-switch block, and never when the user disconnected.
        if (isError && m_wasConnected && m_autoReconnect && m_retry < kMaxRetries) {
            static const int delays[kMaxRetries] = {3, 5, 10, 20, 30};
            m_reconnectIn = delays[m_retry];
            if (!m_retryTimer) {
                m_retryTimer = new QTimer(this);
                m_retryTimer->setInterval(1000);
                connect(m_retryTimer, &QTimer::timeout, this, [this] {
                    if (--m_reconnectIn > 0) { emit reconnectChanged(); return; }
                    m_retryTimer->stop();
                    m_reconnectIn = -1;
                    ++m_retry;
                    emit reconnectChanged();
                    emit logEvent(ActivityLog::Info, QStringLiteral("session"), tr("Reconnecting (attempt %1 of %2)").arg(m_retry).arg(kMaxRetries), QString());
                    reconnect();
                });
            }
            m_retryTimer->start();
            emit reconnectChanged();
        }
    });

    // SFTP
    connect(worker, &SshWorker::sftpListed, this, &TerminalSession::sftpListed);
    connect(worker, &SshWorker::sftpDone, this, &TerminalSession::sftpDone);
    connect(worker, &SshWorker::sftpFileRead, this, [this](int id, const QString &path, const QByteArray &data, const QString &err) {
        // a NUL byte or invalid UTF-8 = not a text file
        const bool binary = data.contains('\0');
        emit sftpFileRead(id, path, binary ? QString() : QString::fromUtf8(data), binary, err);
    });
    connect(worker, &SshWorker::sftpResumed, this, [this](int id, qint64 offset) {
        for (TransferInfo &t : m_transfers) {
            if (t.id == id) {   // these bytes were already there: the speed counts from now
                t.base = offset;
                t.done = offset;
                t.startedMs = QDateTime::currentMSecsSinceEpoch();
            }
        }
        emit transfersChanged();
    });
    connect(worker, &SshWorker::sftpProgress, this,
            [this](int id, qint64 done, qint64 total, int state, const QString &err) {
                for (TransferInfo &t : m_transfers) {
                    if (t.id != id)
                        continue;
                    const qint64 now = QDateTime::currentMSecsSinceEpoch();
                    const double secs = std::max(0.001, (now - t.startedMs) / 1000.0);
                    t.speed = (done - std::max<qint64>(0, t.base)) / secs;
                    t.done = done;
                    if (total > 0)
                        t.total = total;
                    if (state != t.state && state != SshWorker::TransferRunning) {
                        if (state == SshWorker::TransferDone)
                            emit logEvent(ActivityLog::Success, QStringLiteral("sftp"),
                                          t.upload ? tr("Uploaded %1").arg(t.name) : tr("Downloaded %1").arg(t.name),
                                          formatBytes(t.total) + QStringLiteral(" · ") + (t.upload ? t.remote : t.local));
                        else if (state == SshWorker::TransferFailed)
                            emit logEvent(ActivityLog::Error, QStringLiteral("sftp"),
                                          tr("Transfer failed: %1").arg(t.name), err);
                    }
                    t.state = state;
                    t.error = err;
                    break;
                }
                emit transfersChanged();
            });

    // Tunnels
    connect(worker, &SshWorker::forwardsUpdated, this, [this](const QVariantList &f) {
        m_forwards = f;
        emit forwardsChanged();
    });
    connect(worker, &SshWorker::forwardEvent, this, [this](const QString &, bool ok, const QString &msg) {
        emit logEvent(ok ? ActivityLog::Success : ActivityLog::Error, QStringLiteral("tunnel"),
                      ok ? tr("Tunnel opened") : tr("Tunnel error"), msg);
    });

    m_closedWithError = false;
    setState(Connecting, tr("Connecting…"));
    emit logEvent(ActivityLog::Info, QStringLiteral("session"), tr("Connecting to %1").arg(m_endpoint), m_route);
    m_thread->start();
    QMetaObject::invokeMethod(worker, [worker, p] { worker->start(p); }, Qt::QueuedConnection);
}

void TerminalSession::beginSerial(const QString &port, int baud, int dataBits, const QString &parity,
                                  int stopBits, const QString &flow)
{
#ifdef SW_HAVE_SERIAL
    stopSerial();
    if (m_started) {
        m_emu.reset();
        emit sessionRestarted();
    }
    m_started = true;
    auto *sp = new QSerialPort(this);
    sp->setPortName(port);
    sp->setBaudRate(baud);
    sp->setDataBits(QSerialPort::DataBits(std::clamp(dataBits, 5, 8)));
    sp->setParity(parity == QLatin1String("even") ? QSerialPort::EvenParity
                  : parity == QLatin1String("odd") ? QSerialPort::OddParity
                  : parity == QLatin1String("mark") ? QSerialPort::MarkParity
                  : parity == QLatin1String("space") ? QSerialPort::SpaceParity
                                                     : QSerialPort::NoParity);
    sp->setStopBits(stopBits == 2 ? QSerialPort::TwoStop : QSerialPort::OneStop);
    sp->setFlowControl(flow == QLatin1String("rtscts") ? QSerialPort::HardwareControl
                       : flow == QLatin1String("xonxoff") ? QSerialPort::SoftwareControl
                                                          : QSerialPort::NoFlowControl);
    m_route = tr("Serial · %1 · %2 baud").arg(port).arg(baud);
    if (!sp->open(QIODevice::ReadWrite)) {
        const QString err = sp->errorString();
        delete sp;
        m_closedWithError = true;
        setState(Closed, tr("Cannot open %1: %2").arg(port, err));
        emit logEvent(ActivityLog::Error, QStringLiteral("session"), tr("Serial port error"), port + QStringLiteral(": ") + err);
        return;
    }
    m_serial = sp;
    connect(sp, &QSerialPort::readyRead, this, [this, sp] {
        incoming(sp->readAll());
        markActivity();
    });
    connect(sp, &QSerialPort::errorOccurred, this, [this, sp](QSerialPort::SerialPortError e) {
        if (e == QSerialPort::NoError || e == QSerialPort::TimeoutError)
            return;
        const QString err = sp->errorString();
        stopSerial();
        m_closedWithError = true;
        setState(Closed, tr("Serial port closed: %1").arg(err));
    });
    m_closedWithError = false;
    setState(Connected, tr("Connected"));
    emit logEvent(ActivityLog::Success, QStringLiteral("session"), tr("Serial port opened"), m_route);
#else
    Q_UNUSED(port); Q_UNUSED(baud); Q_UNUSED(dataBits); Q_UNUSED(parity); Q_UNUSED(stopBits); Q_UNUSED(flow);
    m_closedWithError = true;
    setState(Closed, tr("This build has no serial port support."));
#endif
}

void TerminalSession::stopSerial()
{
    if (m_pty) {
        m_pty->disconnect(this);
        m_pty->stop();
        m_pty->deleteLater();
        m_pty = nullptr;
    }
    if (m_serial) {
        m_serial->disconnect(this);
        m_serial->close();
        m_serial->deleteLater();
        m_serial = nullptr;
    }
}

void TerminalSession::stopWorker()
{
    if (m_worker) {
        SshWorker *w = m_worker;
        disconnect(w, nullptr, this, nullptr);
        // stop() then quit, in that order, on the worker's own thread.
        QMetaObject::invokeMethod(w, [w] {
            w->stop();
            QThread::currentThread()->quit();
        }, Qt::QueuedConnection);
    } else if (m_thread) {
        m_thread->quit();
    }
    m_thread = nullptr;
    m_worker = nullptr;
}

QList<QPointer<QThread>> &TerminalSession::liveThreads()
{
    static QList<QPointer<QThread>> threads;
    return threads;
}

void TerminalSession::waitForAllThreads(int msTotal)
{
    QDeadlineTimer deadline(msTotal);
    for (const QPointer<QThread> &t : liveThreads()) {
        if (t && t->isRunning())
            t->wait(deadline);
    }
}

void TerminalSession::abort(const QString &reason)
{
    stopWorker();
    stopSerial();
    m_closedWithError = true;
    setState(Closed, reason);
    emit logEvent(ActivityLog::Error, QStringLiteral("session"), tr("Connection blocked"), reason);
}

void TerminalSession::kill(const QString &reason)
{
    if (m_state == Closed)
        return;
    stopWorker();
    stopSerial();
    m_closedWithError = true;
    setState(Closed, reason);
    m_forwards.clear();
    emit forwardsChanged();
    m_emu.feed(QByteArray("\r\n\x1b[1;31m[") + reason.toUtf8() + "]\x1b[0m\r\n");
    emit logEvent(ActivityLog::Security, QStringLiteral("killswitch"), tr("Session closed by the kill switch"), reason);
}

QString TerminalSession::commandFromLine(const QString &line)
{
    // The prompt ends at the first "$ ", "# ", "% " or "> " (user@host:~$ ls -la);
    // prompts of zsh themes end with "❯", "➜" or "»".
    static const QRegularExpression shell(QStringLiteral("^.*?[$#%>] "));
    static const QRegularExpression fancy(QStringLiteral("^.*?[\u276F\u279C\u00BB]\\s+"));
    QRegularExpressionMatch m = shell.match(line);
    if (!m.hasMatch())
        m = fancy.match(line);
    if (!m.hasMatch())
        return {};                  // no prompt: a password or a program asking something
    const QString rest = line.mid(m.capturedEnd());
    if (rest.trimmed().isEmpty() || rest.startsWith(QLatin1Char(' ')))
        return {};                  // " command" = do not remember (like bash's ignorespace)
    return rest.trimmed();
}

void TerminalSession::noteCommand(const QString &line)
{
    const QString cmd = commandFromLine(line);
    if (cmd.isEmpty() || cmd.size() > 2000)
        return;
    emit commandEntered(cmd);
}

void TerminalSession::write(const QByteArray &data)
{
    if (m_inputDepth > 0 && m_broadcast) {
        for (const auto &o : broadcastGroup())
            if (o && o != this && o->m_state == Connected)
                o->write(data);   // their depth is 0, so this does not echo back
    }
    if (m_state != Connected || data.isEmpty())
        return;
    // Command history: on a plain Enter at a shell prompt remember where the line
    // starts; it is read once the server echoes the Enter (see incoming()), so even
    // over a slow link the line holds everything that was typed, after editing and
    // Tab completion.
    if (data == "\r" && !m_hostId.isEmpty() && !m_emu.altScreen()) {
        m_cmdRow = m_emu.cursorLineStart() - m_emu.scrollbackSize();   // row on the screen
        m_cmdPushed = m_emu.linesPushed();
        m_cmdTyped = m_emu.cursorLineText();   // what was there at the Enter: the fallback
        m_cmdSince.start();
    }
    if (m_pty) {
        m_pty->write(data);
        return;
    }
    if (m_serial) {
        if (m_kind == QLatin1String("telnet")) {
            QByteArray esc = data;
            esc.replace("\xff", "\xff\xff");   // IAC must be doubled
            m_serial->write(esc);
        } else {
            m_serial->write(data);
        }
        return;
    }
    onWorker([data](SshWorker *w) { w->write(data); });
}

void TerminalSession::sendText(const QString &text)
{
    write(text.toUtf8());
}

void TerminalSession::resizeTerminal(int cols, int rows)
{
    m_cols = cols;
    m_rows = rows;
    m_emu.resize(rows, cols);
    // Panels slide in with an animation; tell the server only the final size
    // so the shell gets one SIGWINCH instead of dozens.
    if (!m_resizeTimer) {
        m_resizeTimer = new QTimer(this);
        m_resizeTimer->setSingleShot(true);
        m_resizeTimer->setInterval(120);
        connect(m_resizeTimer, &QTimer::timeout, this, [this] {
            if (m_kind == QLatin1String("telnet")) { telnetSendSize(); return; }
            if (m_pty) { m_pty->resize(m_cols, m_rows); return; }
            const int c = m_cols, r = m_rows;
            onWorker([c, r](SshWorker *w) { w->resize(c, r); });
        });
    }
    m_resizeTimer->start();
}

void TerminalSession::acceptHostKey(bool accept)
{
    if (!m_worker || m_state != HostKeyPrompt)
        return;
    setState(Connecting, accept ? tr("Host key accepted") : tr("Host key rejected"));
    emit logEvent(accept ? ActivityLog::Security : ActivityLog::Warning, QStringLiteral("session"),
                  accept ? (m_hkStatus == 0 ? tr("New host key trusted") : tr("Changed host key accepted"))
                         : tr("Host key rejected"),
                  m_hkType + QLatin1Char(' ') + m_hkFingerprint);
    onWorker([accept](SshWorker *w) { w->answerHostKey(accept); });
}

void TerminalSession::submitSecret(const QString &secret, bool save)
{
    if (!m_worker || m_state != SecretPrompt)
        return;
    if (m_secretKind >= 0 && m_secretKind < 2)
        m_saveRequested[m_secretKind] = save && m_canSaveSecret;
    setState(Connecting, tr("Authenticating…"));
    onWorker([secret](SshWorker *w) { w->answerSecret(secret, false); });
}

void TerminalSession::cancelSecret()
{
    onWorker([](SshWorker *w) { w->answerSecret(QString(), true); });
}

void TerminalSession::cancelReconnect()
{
    if (m_retryTimer)
        m_retryTimer->stop();
    m_reconnectIn = -1;
    m_retry = 0;
    emit reconnectChanged();
}

QList<QPointer<TerminalSession>> &TerminalSession::broadcastGroup()
{
    static QList<QPointer<TerminalSession>> g;
    return g;
}

void TerminalSession::setBroadcast(bool on)
{
    if (on == m_broadcast)
        return;
    m_broadcast = on;
    auto &g = broadcastGroup();
    g.removeAll(nullptr);
    if (on)
        g.append(this);
    else
        g.removeAll(this);
    for (const auto &s : std::as_const(g))
        if (s && s != this)
            emit s->broadcastChanged();
    emit broadcastChanged();
}

int TerminalSession::broadcastCount() const
{
    int n = 0;
    for (const auto &s : broadcastGroup())
        n += s ? 1 : 0;
    return n;
}

void TerminalSession::userInput(const QByteArray &data)
{
    beginUserInput();
    write(data);
    endUserInput();
}

void TerminalSession::disconnectSession()
{
    cancelReconnect();
    m_wasConnected = false;
    if (m_state == Closed)
        return;
    stopWorker();
    stopSerial();
    m_closedWithError = false;
    setState(Closed, tr("Disconnected."));
    m_forwards.clear();
    emit forwardsChanged();
    emit logEvent(ActivityLog::Info, QStringLiteral("session"), tr("Disconnected by user"), QString());
}

void TerminalSession::reconnect()
{
    emit reconnectRequested();
}

// ---------------------------------------------------------------------------
// SFTP
// ---------------------------------------------------------------------------

int TerminalSession::sftpReadFile(const QString &path)
{
    const int id = m_nextReq++;
    if (!m_worker || m_state != Connected) {
        QTimer::singleShot(0, this, [this, id, path] { emit sftpFileRead(id, path, {}, false, tr("Not connected.")); });
        return id;
    }
    onWorker([id, path](SshWorker *w) { w->sftpReadFile(id, path); });
    return id;
}

int TerminalSession::sftpWriteFile(const QString &path, const QString &text)
{
    const int id = m_nextReq++;
    if (!m_worker || m_state != Connected) {
        QTimer::singleShot(0, this, [this, id] { emit sftpDone(id, tr("Not connected.")); });
        return id;
    }
    const QByteArray data = text.toUtf8();
    onWorker([id, path, data](SshWorker *w) { w->sftpWriteFile(id, path, data); });
    return id;
}

int TerminalSession::sftpList(const QString &path)
{
    const int id = m_nextReq++;
    if (!m_worker || m_state != Connected) {
        QTimer::singleShot(0, this, [this, id, path] { emit sftpListed(id, path, {}, tr("Not connected.")); });
        return id;
    }
    onWorker([id, path](SshWorker *w) { w->sftpList(id, path); });
    return id;
}

int TerminalSession::sftpMkdir(const QString &path)
{
    const int id = m_nextReq++;
    onWorker([id, path](SshWorker *w) { w->sftpMkdir(id, path); });
    emit logEvent(ActivityLog::Info, QStringLiteral("sftp"), tr("Created folder"), path);
    return id;
}

int TerminalSession::sftpRemove(const QString &path, bool isDir)
{
    const int id = m_nextReq++;
    onWorker([id, path, isDir](SshWorker *w) { w->sftpRemove(id, path, isDir); });
    emit logEvent(ActivityLog::Warning, QStringLiteral("sftp"), tr("Deleted on server"), path);
    return id;
}

int TerminalSession::sftpRename(const QString &from, const QString &to)
{
    const int id = m_nextReq++;
    onWorker([id, from, to](SshWorker *w) { w->sftpRename(id, from, to); });
    emit logEvent(ActivityLog::Info, QStringLiteral("sftp"), tr("Renamed"), from + QStringLiteral(" → ") + to);
    return id;
}

int TerminalSession::download(const QString &remote, const QUrl &localFolder)
{
    const int id = m_nextReq++;
    const QString name = remote.section(QLatin1Char('/'), -1);
    const QString dir = localFolder.isLocalFile() ? localFolder.toLocalFile() : localFolder.toString();
    QString local = QDir(dir).filePath(name);
    // never overwrite silently
    for (int n = 1; QFileInfo::exists(local); ++n) {
        const QFileInfo fi(QDir(dir).filePath(name));
        local = QDir(dir).filePath(fi.completeBaseName() + QStringLiteral(" (%1)").arg(n)
                                   + (fi.suffix().isEmpty() ? QString() : QLatin1Char('.') + fi.suffix()));
    }
    TransferInfo t{id, false, name, remote, local, 0, 0, 0, QString(), 0, 0};
    t.startedMs = QDateTime::currentMSecsSinceEpoch();
    m_transfers.append(t);
    emit transfersChanged();
    onWorker([id, remote, local](SshWorker *w) { w->sftpDownload(id, remote, local); });
    return id;
}

int TerminalSession::upload(const QUrl &localFile, const QString &remoteFolder)
{
    const int id = m_nextReq++;
    const QString local = localFile.isLocalFile() ? localFile.toLocalFile() : localFile.toString();
    const QString name = QFileInfo(local).fileName();
    const QString remote = (remoteFolder.endsWith(QLatin1Char('/')) ? remoteFolder : remoteFolder + QLatin1Char('/')) + name;
    TransferInfo t{id, true, name, remote, local, 0, 0, 0, QString(), 0, 0};
    t.total = QFileInfo(local).size();
    t.startedMs = QDateTime::currentMSecsSinceEpoch();
    m_transfers.append(t);
    emit transfersChanged();
    onWorker([id, local, remote](SshWorker *w) { w->sftpUpload(id, local, remote); });
    return id;
}

void TerminalSession::cancelTransfer(int id)
{
    onWorker([id](SshWorker *w) { w->sftpCancel(id); });
}

void TerminalSession::clearFinishedTransfers()
{
    m_transfers.erase(std::remove_if(m_transfers.begin(), m_transfers.end(),
                                     [](const TransferInfo &t) { return t.state != SshWorker::TransferRunning; }),
                      m_transfers.end());
    emit transfersChanged();
}

QVariantList TerminalSession::transfers() const
{
    QVariantList out;
    for (const TransferInfo &t : m_transfers) {
        out << QVariantMap{
            {QStringLiteral("id"), t.id},
            {QStringLiteral("upload"), t.upload},
            {QStringLiteral("name"), t.name},
            {QStringLiteral("done"), t.done},
            {QStringLiteral("total"), t.total},
            {QStringLiteral("state"), t.state},
            {QStringLiteral("error"), t.error},
            {QStringLiteral("progress"), t.total > 0 ? double(t.done) / double(t.total) : 0.0},
            {QStringLiteral("sizeText"), formatBytes(t.done) + QStringLiteral(" / ") + formatBytes(t.total)},
            {QStringLiteral("speedText"), formatBytes(qint64(t.speed)) + QStringLiteral("/s")},
            {QStringLiteral("local"), t.local},
            {QStringLiteral("resumed"), t.base > 0},
        };
    }
    return out;
}

// ---------------------------------------------------------------------------
// Tunnels
// ---------------------------------------------------------------------------

void TerminalSession::incoming(const QByteArray &data)
{
    m_emu.feed(data);
    if (m_cmdRow >= 0) {
        // lines scrolled away since the Enter move our line up by the same amount
        const int shift = int(m_emu.linesPushed() - m_cmdPushed);
        const int row = m_cmdRow - shift;                     // < 0: now in the scrollback
        const int abs = m_emu.scrollbackSize() + row;
        if (m_emu.cursor().row + shift > m_cmdRow && abs >= 0) {   // the Enter came back: the line is complete
            bool full = false;
            QString line = m_emu.lineText(abs, &full);
            const int cursorAbs = m_emu.scrollbackSize() + m_emu.cursor().row;
            for (int l = abs + 1; full && l < cursorAbs; ++l)      // a long command wraps
                line += m_emu.lineText(l, &full);
            m_cmdRow = -1;
            noteCommand(line);
        } else if (m_cmdSince.elapsed() > 5000 || abs < 0) {
            m_cmdRow = -1;            // no echo in time (or the line is gone): use what was typed
            noteCommand(m_cmdTyped);
        }
    }
    if (!m_record)
        return;
    // Plain text for the log: drop escape sequences (colours, cursor moves,
    // titles) and carriage returns; keep line feeds and tabs.
    QByteArray in = m_recordTail + data;
    m_recordTail.clear();
    QByteArray out;
    out.reserve(in.size());
    for (qsizetype i = 0; i < in.size(); ++i) {
        const char c = in.at(i);
        if (c == '\x1b') {
            qsizetype j = i + 1;
            if (j >= in.size()) { m_recordTail = in.mid(i); break; }
            if (in.at(j) == '[') {                       // CSI ... final byte
                ++j;
                while (j < in.size() && !(in.at(j) >= 0x40 && in.at(j) <= 0x7e)) ++j;
                if (j >= in.size()) { m_recordTail = in.mid(i); break; }
                i = j;
            } else if (in.at(j) == ']') {                // OSC ... ended by BEL or ESC-backslash
                qsizetype k = j + 1;
                while (k < in.size() && in.at(k) != '\a' && !(in.at(k) == '\x1b' && k + 1 < in.size() && in.at(k + 1) == '\\')) ++k;
                if (k >= in.size()) { m_recordTail = in.mid(i); break; }
                i = in.at(k) == '\a' ? k : k + 1;
            } else {
                i = j;                                   // two-byte sequence
            }
            continue;
        }
        if (c == '\r' || c == '\a' || (static_cast<unsigned char>(c) < 0x20 && c != '\n' && c != '\t' && c != '\b'))
            continue;
        if (c == '\b') { if (!out.isEmpty()) out.chop(1); continue; }
        out.append(c);
    }
    m_record->write(out);
    m_record->flush();
}

QString TerminalSession::startRecording(const QUrl &file)
{
    stopRecording();
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    auto *f = new QFile(path, this);
    if (!f->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        const QString e = f->errorString();
        delete f;
        return tr("Cannot write %1: %2").arg(path, e);
    }
    f->write(QStringLiteral("\n===== %1 — %2 (%3) =====\n")
                 .arg(QDateTime::currentDateTime().toString(Qt::ISODate), m_label, m_endpoint).toUtf8());
    m_record = f;
    m_recordPath = QDir::toNativeSeparators(path);
    emit recordingChanged();
    emit logEvent(ActivityLog::Info, QStringLiteral("session"), tr("Session log started"), m_recordPath);
    return {};
}

void TerminalSession::stopRecording()
{
    if (!m_record)
        return;
    m_record->write(QStringLiteral("\n===== %1 — end =====\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate)).toUtf8());
    m_record->close();
    delete m_record;
    m_record = nullptr;
    m_recordTail.clear();
    emit logEvent(ActivityLog::Info, QStringLiteral("session"), tr("Session log saved"), m_recordPath);
    emit recordingChanged();
}

QString TerminalSession::addForward(const QString &type, const QString &bindHost, int bindPort,
                                    const QString &destHost, int destPort)
{
    ForwardSpec f;
    f.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    f.type = type == QLatin1String("R") ? ForwardSpec::Remote : type == QLatin1String("D") ? ForwardSpec::Dynamic : ForwardSpec::Local;
    f.bindHost = bindHost.trimmed().isEmpty() ? QStringLiteral("127.0.0.1") : bindHost.trimmed();
    f.bindPort = bindPort;
    f.destHost = destHost.trimmed();
    f.destPort = destPort;
    onWorker([f](SshWorker *w) { w->addForward(f); });
    return f.id;
}

void TerminalSession::removeForward(const QString &id)
{
    onWorker([id](SshWorker *w) { w->removeForward(id); });
    emit logEvent(ActivityLog::Info, QStringLiteral("tunnel"), tr("Tunnel closed"), QString());
}

// ---------------------------------------------------------------------------
// server mini-monitor: one short command on its own channel every N seconds
// ---------------------------------------------------------------------------

namespace {
// POSIX sh and fish both understand this; only /proc readers, nothing is written.
const char kMonitorCmd[] =
    "LC_ALL=C; echo \"@L $(cat /proc/loadavg 2>/dev/null)\"; echo \"@C $(head -n1 /proc/stat 2>/dev/null)\"; "
    "grep -E '^(MemTotal|MemAvailable|SwapTotal|SwapFree):' /proc/meminfo 2>/dev/null | sed 's/^/@M /'; "
    "echo \"@D $(df -kP / 2>/dev/null | tail -n1)\"; echo \"@U $(cat /proc/uptime 2>/dev/null)\"; "
    "echo \"@N $(nproc 2>/dev/null)\"; "
    "awk -F'[: ]+' 'NR>2 && $2!=\"lo\" {rx+=$3; tx+=$11} END {print \"@T\", rx+0, tx+0}' /proc/net/dev 2>/dev/null";
}

void TerminalSession::setMonitorConfig(bool on, int intervalSec)
{
    m_monOn = on && m_kind != QLatin1String("serial");
    m_monInterval = std::clamp(intervalSec, 2, 300);
    // one long-running channel on the server prints a sample every interval:
    // no new channel per sample, so the terminal never waits for the monitor
    if (m_monOn && m_state == Connected && m_worker) {
        if (m_monRunning == m_monInterval)
            return;   // already running with this interval
        m_monRunning = m_monInterval;
        const QString cmd = QString::fromLatin1(kMonitorCmd);
        const int iv = m_monInterval;
        onWorker([cmd, iv](SshWorker *w) { w->startMonitor(cmd, iv); });
    } else {
        if (m_worker && m_monRunning) onWorker([](SshWorker *w) { w->stopMonitor(); });
        m_monRunning = 0;
        if (!m_monOn) { m_monitor.clear(); m_cpuTotal = m_rx = 0; }
    }
    emit monitorChanged();
}

void TerminalSession::parseMonitor(const QByteArray &out, const QString &error)
{
    QVariantMap m;
    quint64 memTotal = 0, memAvail = 0, swapTotal = 0, swapFree = 0;
    bool any = false;
    for (const QByteArray &raw : out.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.size() < 3 || line.at(0) != QLatin1Char('@'))
            continue;
        const QString tag = line.mid(1, 1);
        const QStringList f = line.mid(3).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (tag == QLatin1String("L") && !f.isEmpty()) {
            m.insert(QStringLiteral("load1"), f.at(0).toDouble());
            any = true;
        } else if (tag == QLatin1String("C") && f.size() >= 5 && f.at(0) == QLatin1String("cpu")) {
            quint64 total = 0;
            for (int i = 1; i < f.size(); ++i) total += f.at(i).toULongLong();
            const quint64 idle = f.at(4).toULongLong() + (f.size() > 5 ? f.at(5).toULongLong() : 0);   // idle + iowait
            if (m_cpuTotal && total > m_cpuTotal) {
                const double busy = 1.0 - double(idle - m_cpuIdle) / double(total - m_cpuTotal);
                m.insert(QStringLiteral("cpu"), std::clamp(busy * 100.0, 0.0, 100.0));
            }
            m_cpuTotal = total;
            m_cpuIdle = idle;
        } else if (tag == QLatin1String("M") && f.size() >= 2) {
            const quint64 v = f.at(1).toULongLong() * 1024;
            if (f.at(0) == QLatin1String("MemTotal:")) memTotal = v;
            else if (f.at(0) == QLatin1String("MemAvailable:")) memAvail = v;
            else if (f.at(0) == QLatin1String("SwapTotal:")) swapTotal = v;
            else if (f.at(0) == QLatin1String("SwapFree:")) swapFree = v;
        } else if (tag == QLatin1String("D") && f.size() >= 4) {
            m.insert(QStringLiteral("diskTotal"), double(f.at(1).toULongLong()) * 1024);
            m.insert(QStringLiteral("diskUsed"), double(f.at(2).toULongLong()) * 1024);
        } else if (tag == QLatin1String("U") && !f.isEmpty()) {
            m.insert(QStringLiteral("uptime"), qint64(f.at(0).toDouble()));
        } else if (tag == QLatin1String("N") && !f.isEmpty()) {
            m.insert(QStringLiteral("cores"), f.at(0).toInt());
        } else if (tag == QLatin1String("T") && f.size() >= 2) {
            const quint64 rx = f.at(0).toULongLong(), tx = f.at(1).toULongLong();
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (m_rx && now > m_netAt && rx >= m_rx && tx >= m_tx) {
                const double sec = (now - m_netAt) / 1000.0;
                m.insert(QStringLiteral("rxRate"), (rx - m_rx) / sec);
                m.insert(QStringLiteral("txRate"), (tx - m_tx) / sec);
            }
            m_rx = rx; m_tx = tx; m_netAt = now;
        }
    }
    if (memTotal) {
        m.insert(QStringLiteral("memTotal"), double(memTotal));
        m.insert(QStringLiteral("memUsed"), double(memTotal - std::min(memAvail, memTotal)));
    }
    if (swapTotal) {
        m.insert(QStringLiteral("swapTotal"), double(swapTotal));
        m.insert(QStringLiteral("swapUsed"), double(swapTotal - std::min(swapFree, swapTotal)));
    }
    // keep last CPU / network values until the next delta is known
    for (const char *k : {"cpu", "rxRate", "txRate"})
        if (!m.contains(QString::fromLatin1(k)) && m_monitor.contains(QString::fromLatin1(k)))
            m.insert(QString::fromLatin1(k), m_monitor.value(QString::fromLatin1(k)));
    const bool ok = any || memTotal;
    m.insert(QStringLiteral("ok"), ok);
    if (!ok)
        m.insert(QStringLiteral("error"), error.isEmpty() ? tr("This server does not report its load (Linux /proc is needed).") : error);
    m_monitor = m;
    emit monitorChanged();
}

// ---------------------------------------------------------------------------
// Telnet: a TCP socket plus the minimum of RFC 854 option negotiation
// (echo, suppress-go-ahead, terminal type, window size). Nothing is encrypted.
// ---------------------------------------------------------------------------

namespace {
constexpr char IAC = char(255), DONT = char(254), DO_ = char(253), WONT = char(252), WILL = char(251),
               SB = char(250), SE = char(240);
constexpr char OPT_ECHO = 1, OPT_SGA = 3, OPT_TTYPE = 24, OPT_NAWS = 31;
}

void TerminalSession::beginTelnet(const QString &host, int port, const ProxyConfig &proxy)
{
    stopSerial();
    if (m_started) {
        m_emu.reset();
        emit sessionRestarted();
    }
    m_started = true;
    m_tnBuf.clear();
    m_tnNaws = false;
    auto *sock = new QTcpSocket(this);
    if (proxy.isSet()) {
        QNetworkProxy np(proxy.type == ProxyConfig::Http ? QNetworkProxy::HttpProxy : QNetworkProxy::Socks5Proxy,
                         proxy.host, quint16(proxy.port), proxy.username, proxy.password);
        np.setCapabilities(np.capabilities() | QNetworkProxy::HostNameLookupCapability);   // remote DNS (.onion too)
        sock->setProxy(np);
    } else {
        sock->setProxy(QNetworkProxy::NoProxy);
    }
    m_serial = sock;
    setState(Connecting, tr("Connecting to %1:%2…").arg(host).arg(port));
    connect(sock, &QTcpSocket::connected, this, [this] {
        m_closedWithError = false;
        setState(Connected, tr("Connected"));
        emit logEvent(ActivityLog::Warning, QStringLiteral("session"), tr("Telnet connected (not encrypted)"), m_route);
    });
    connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
        telnetIncoming(sock->readAll());
        markActivity();
    });
    connect(sock, &QTcpSocket::errorOccurred, this, [this, sock](QAbstractSocket::SocketError e) {
        const bool wasConnected = m_state == Connected;
        const QString err = sock->errorString();
        stopSerial();
        m_closedWithError = e != QAbstractSocket::RemoteHostClosedError;
        setState(Closed, e == QAbstractSocket::RemoteHostClosedError ? tr("Session ended.")
                                                                     : (wasConnected ? tr("Connection lost: %1") : tr("Cannot connect: %1")).arg(err));
        emit logEvent(m_closedWithError ? ActivityLog::Error : ActivityLog::Info, QStringLiteral("session"),
                      m_closedWithError ? tr("Connection failed / lost") : tr("Disconnected"), err);
    });
    sock->connectToHost(host, quint16(port));
}

void TerminalSession::telnetSendSize()
{
    if (!m_serial || !m_tnNaws)
        return;
    QByteArray b;
    b.append(IAC).append(SB).append(OPT_NAWS);
    for (int v : {m_cols, m_rows}) {
        const char hi = char((v >> 8) & 0xff), lo = char(v & 0xff);
        b.append(hi); if (hi == IAC) b.append(IAC);
        b.append(lo); if (lo == IAC) b.append(IAC);
    }
    b.append(IAC).append(SE);
    m_serial->write(b);
}

void TerminalSession::telnetIncoming(const QByteArray &raw)
{
    QByteArray in = m_tnBuf + raw;
    m_tnBuf.clear();
    QByteArray text, reply;
    int i = 0;
    while (i < in.size()) {
        const char c = in.at(i);
        if (c != IAC) { text.append(c); ++i; continue; }
        if (i + 1 >= in.size()) { m_tnBuf = in.mid(i); break; }
        const char cmd = in.at(i + 1);
        if (cmd == IAC) { text.append(IAC); i += 2; continue; }
        if (cmd == DO_ || cmd == DONT || cmd == WILL || cmd == WONT) {
            if (i + 2 >= in.size()) { m_tnBuf = in.mid(i); break; }
            const char opt = in.at(i + 2);
            if (cmd == DO_) {
                if (opt == OPT_TTYPE || opt == OPT_NAWS || opt == OPT_SGA) {
                    reply.append(IAC).append(WILL).append(opt);
                    if (opt == OPT_NAWS) m_tnNaws = true;
                } else {
                    reply.append(IAC).append(WONT).append(opt);
                }
            } else if (cmd == WILL) {
                reply.append(IAC).append((opt == OPT_ECHO || opt == OPT_SGA) ? DO_ : DONT).append(opt);
            } else if (cmd == DONT && opt == OPT_NAWS) {
                m_tnNaws = false;
            }
            i += 3;
            continue;
        }
        if (cmd == SB) {
            const int end = in.indexOf(QByteArray().append(IAC).append(SE), i + 2);
            if (end < 0) { m_tnBuf = in.mid(i); break; }
            // terminal type request: IAC SB TTYPE SEND(1) IAC SE
            if (end - i >= 4 && in.at(i + 2) == OPT_TTYPE && in.at(i + 3) == char(1))
                reply.append(IAC).append(SB).append(OPT_TTYPE).append(char(0)).append("XTERM-256COLOR").append(IAC).append(SE);
            i = end + 2;
            continue;
        }
        i += 2;   // NOP, GA, etc.
    }
    if (!reply.isEmpty() && m_serial) {
        m_serial->write(reply);
        if (m_tnNaws && reply.contains(QByteArray().append(IAC).append(WILL).append(OPT_NAWS)))
            telnetSendSize();
    }
    if (!text.isEmpty())
        incoming(text);
}

// ---------------------------------------------------------------------------
// SSHFP: is this host key published in the domain's DNS?
// Asked over DNS-over-HTTPS (dns.google), through the same proxy as the
// session, so it works the same with the kill switch. "AD" = DNSSEC-validated.
// ---------------------------------------------------------------------------

void TerminalSession::lookupSshfp(const QString &fingerprint)
{
    m_sshfpState.clear();
    m_sshfpSecure = false;
    const QString host = m_dnsHost.trimmed().toLower();
    QHostAddress ip;
    if (!m_sshfpOn || host.isEmpty() || ip.setAddress(host) || !host.contains(QLatin1Char('.'))
        || host.endsWith(QLatin1String(".onion")) || host.endsWith(QLatin1String(".local"))
        || !fingerprint.startsWith(QLatin1String("SHA256:")))
        return;   // IP addresses and local names have no SSHFP
    QByteArray b64 = fingerprint.mid(7).toLatin1();
    while (b64.size() % 4) b64.append('=');
    const QString want = QString::fromLatin1(QByteArray::fromBase64(b64).toHex()).toLower();

    m_sshfpState = QStringLiteral("checking");
    emit changed();
    auto *nam = new QNetworkAccessManager(this);
    if (m_dnsProxy.isSet()) {
        QNetworkProxy np(m_dnsProxy.type == ProxyConfig::Http ? QNetworkProxy::HttpProxy : QNetworkProxy::Socks5Proxy,
                         m_dnsProxy.host, quint16(m_dnsProxy.port), m_dnsProxy.username, m_dnsProxy.password);
        np.setCapabilities(np.capabilities() | QNetworkProxy::HostNameLookupCapability);
        nam->setProxy(np);
    } else {
        nam->setProxy(QNetworkProxy::NoProxy);
    }
    QUrl url(QStringLiteral("https://dns.google/resolve"));
    url.setQuery(QStringLiteral("name=%1&type=44&do=1").arg(QString::fromLatin1(QUrl::toAce(host))));
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/dns-json");
    req.setTransferTimeout(6000);
    QNetworkReply *r = nam->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r, nam, want] {
        r->deleteLater();
        nam->deleteLater();
        if (m_state != HostKeyPrompt)
            return;
        if (r->error() != QNetworkReply::NoError) {
            m_sshfpState = QStringLiteral("error");
            emit changed();
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        m_sshfpSecure = o.value(QStringLiteral("AD")).toBool();
        bool any = false, match = false;
        for (const QJsonValue &v : o.value(QStringLiteral("Answer")).toArray()) {
            const QJsonObject a = v.toObject();
            if (a.value(QStringLiteral("type")).toInt() != 44)
                continue;
            // data: "<algorithm> <fp type> <hex>"
            const QStringList f = a.value(QStringLiteral("data")).toString().simplified().split(QLatin1Char(' '));
            if (f.size() < 3)
                continue;
            any = true;
            if (f.at(1) == QLatin1String("2") && f.mid(2).join(QString()).toLower() == want)
                match = true;
        }
        m_sshfpState = match ? QStringLiteral("match") : any ? QStringLiteral("mismatch") : QStringLiteral("none");
        emit logEvent(match ? ActivityLog::Success : any ? ActivityLog::Warning : ActivityLog::Info, QStringLiteral("session"),
                      match ? tr("SSHFP: the host key matches DNS") : any ? tr("SSHFP: the host key does NOT match DNS") : tr("SSHFP: no records in DNS"),
                      m_dnsHost + (m_sshfpSecure ? QStringLiteral(" (DNSSEC)") : QString()));
        emit changed();
    });
}

// ---------------------------------------------------------------------------
// local shell
// ---------------------------------------------------------------------------

void TerminalSession::beginLocal(const QString &program, const QStringList &args)
{
    stopSerial();
    if (m_started) {
        m_emu.reset();
        emit sessionRestarted();
    }
    m_started = true;
    m_localProgram = program;
    m_localArgs = args;
    m_route = tr("This computer · %1").arg(QFileInfo(program).completeBaseName());
    auto *pty = new LocalPty(this);
    QString err;
    if (!pty->start(program, args, m_cols, m_rows, &err)) {
        delete pty;
        m_closedWithError = true;
        setState(Closed, err);
        return;
    }
    m_pty = pty;
    connect(pty, &LocalPty::dataReceived, this, [this](const QByteArray &d) {
        incoming(d);
        markActivity();
    });
    connect(pty, &LocalPty::finished, this, [this](int code) {
        stopSerial();
        m_closedWithError = false;
        setState(Closed, tr("The shell ended (exit code %1).").arg(code));
    });
    m_closedWithError = false;
    setState(Connected, tr("Connected"));
}
