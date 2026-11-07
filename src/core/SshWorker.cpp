#include "SshWorker.h"
#include "JumpBridge.h"
#include "KeyTools.h"
#include "KnownHosts.h"
#include "SecureBytes.h"
#include "AgentClient.h"

#include <QElapsedTimer>
#include <QFile>
#include <QSocketNotifier>
#include <QTcpServer>
#include <QTcpSocket>
#include <QStringList>
#include <QThread>
#include <QTimer>

namespace {

struct KeyHolder
{
    ssh_key key = nullptr;
    ~KeyHolder() { if (key) ssh_key_free(key); }
};

QString methodsToText(int m)
{
    QStringList l;
    if (m & SSH_AUTH_METHOD_PUBLICKEY) l << QStringLiteral("publickey");
    if (m & SSH_AUTH_METHOD_PASSWORD) l << QStringLiteral("password");
    if (m & SSH_AUTH_METHOD_INTERACTIVE) l << QStringLiteral("keyboard-interactive");
    if (m & SSH_AUTH_METHOD_GSSAPI_MIC) l << QStringLiteral("gssapi");
    return l.isEmpty() ? QStringLiteral("none") : l.join(QStringLiteral(", "));
}

} // namespace

SshWorker::SshWorker(QObject *parent) : QObject(parent) { m_clock.start(); }

SshWorker::~SshWorker() { cleanup(); }

void SshWorker::start(const ConnectParams &params)
{
    m_p = params;

    if (m_p.killSwitch && !m_p.proxy.isSet()) {
        fail(tr("Kill switch is on and this host has no proxy. Direct connection blocked."));
        return;
    }

    m_session = ssh_new();
    if (!m_session) {
        fail(tr("Out of memory."));
        return;
    }

    const QByteArray host = m_p.host.toUtf8();
    const QByteArray user = m_p.username.toUtf8();
    const QByteArray kh = m_p.knownHostsPath.toUtf8();
    const unsigned int port = static_cast<unsigned int>(m_p.port);
    const long timeout = m_p.connectTimeoutSeconds;
    const int no = 0;

    ssh_options_set(m_session, SSH_OPTIONS_HOST, host.constData());
    ssh_options_set(m_session, SSH_OPTIONS_PORT, &port);
    if (!user.isEmpty())
        ssh_options_set(m_session, SSH_OPTIONS_USER, user.constData());
    ssh_options_set(m_session, SSH_OPTIONS_KNOWNHOSTS, kh.constData());
    ssh_options_set(m_session, SSH_OPTIONS_TIMEOUT, &timeout);
    // Don't silently pick up ~/.ssh/config: what the user sees in the UI is
    // exactly what is used.
    ssh_options_set(m_session, SSH_OPTIONS_PROCESS_CONFIG, &no);

    if (m_p.agentForward && m_p.execCommand.isEmpty()) {
        m_callbacks.userdata = this;
        m_callbacks.channel_open_request_auth_agent_function = &SshWorker::onAgentChannelRequest;
        ssh_callbacks_init(&m_callbacks);
        ssh_set_callbacks(m_session, &m_callbacks);
    }

    if (m_p.jump.enabled) {
        // Jump host: first hop (optionally through the proxy), then a
        // direct-tcpip channel to the target, fed to libssh as a socket.
        emit status(tr("Connecting to jump host %1…").arg(m_p.jump.label.isEmpty() ? m_p.jump.host : m_p.jump.label));
        m_jump = std::make_unique<JumpBridge>();
        QString err;
        const sw_socket_t fd = m_jump->start(m_p.jump, m_p.proxy, m_p.knownHostsPath, m_p.host, m_p.port,
                                             m_p.connectTimeoutSeconds, &err);
        if (fd == kInvalidSocket) {
            fail(tr("Jump host failed: %1").arg(err));
            return;
        }
        socket_t sock = static_cast<socket_t>(fd);
        ssh_options_set(m_session, SSH_OPTIONS_FD, &sock);
        emit status(tr("Jump host connected, negotiating SSH with %1…").arg(m_p.host));
    } else if (m_p.proxy.isSet()) {
        emit status(tr("Connecting to proxy %1:%2…").arg(m_p.proxy.host).arg(m_p.proxy.port));
        QString err;
        const sw_socket_t fd = proxytunnel::open(m_p.proxy, m_p.host, m_p.port,
                                                 m_p.connectTimeoutSeconds * 1000, &err);
        if (fd == kInvalidSocket) {
            // A failed proxy never falls back to a direct connection.
            while (err.endsWith(QLatin1Char('.')))
                err.chop(1);
            fail(tr("Proxy failed: %1. Direct connection was NOT attempted.").arg(err));
            return;
        }
        socket_t sock = static_cast<socket_t>(fd);
        ssh_options_set(m_session, SSH_OPTIONS_FD, &sock); // libssh owns it now
        emit status(tr("Proxy tunnel up, negotiating SSH…"));
    } else {
        emit status(tr("Connecting to %1:%2…").arg(m_p.host).arg(m_p.port));
    }

    if (ssh_connect(m_session) != SSH_OK) {
        fail(tr("Connection failed: %1").arg(QString::fromUtf8(ssh_get_error(m_session))));
        return;
    }
    verifyHostKey();
}

void SshWorker::verifyHostKey()
{
    KeyHolder srv;
    if (ssh_get_server_publickey(m_session, &srv.key) != SSH_OK) {
        fail(tr("Cannot read the server host key."));
        return;
    }

    unsigned char *hash = nullptr;
    size_t hlen = 0;
    ssh_get_publickey_hash(srv.key, SSH_PUBLICKEY_HASH_SHA256, &hash, &hlen);
    char *fp = ssh_get_fingerprint_hash(SSH_PUBLICKEY_HASH_SHA256, hash, hlen);
    const QString fingerprint = fp ? QString::fromLatin1(fp) : QString();
    if (fp)
        ssh_string_free_char(fp);
    ssh_clean_pubkey_hash(&hash);
    const QString keyType = QString::fromLatin1(ssh_key_type_to_char(ssh_key_type(srv.key)));

    switch (ssh_session_is_known_server(m_session)) {
    case SSH_KNOWN_HOSTS_OK:
        tryAuth();
        return;
    case SSH_KNOWN_HOSTS_CHANGED:
        emit hostKeyCheck(HostChanged, keyType, fingerprint);
        return;
    case SSH_KNOWN_HOSTS_OTHER:
        emit hostKeyCheck(HostOtherType, keyType, fingerprint);
        return;
    case SSH_KNOWN_HOSTS_NOT_FOUND:
    case SSH_KNOWN_HOSTS_UNKNOWN:
        emit hostKeyCheck(HostUnknown, keyType, fingerprint);
        return;
    case SSH_KNOWN_HOSTS_ERROR:
    default:
        fail(tr("Host key check failed: %1").arg(QString::fromUtf8(ssh_get_error(m_session))));
        return;
    }
}

void SshWorker::answerHostKey(bool accept)
{
    if (!m_session || m_done)
        return;
    if (!accept) {
        fail(tr("Host key not accepted. Connection closed."));
        return;
    }
    // Drop stale entries for this host first so the file never holds two
    // conflicting keys for the same host.
    knownhosts::removeHost(m_p.knownHostsPath, m_p.host, m_p.port);
    if (ssh_session_update_known_hosts(m_session) != SSH_OK)
        emit status(tr("Warning: could not save the host key."));
    tryAuth();
}

void SshWorker::requestSecret(SecretKind kind, const QString &prompt, bool echo, bool retry)
{
    m_pendingSecret = kind;
    emit secretRequired(kind, prompt, echo, retry);
}

void SshWorker::tryAuth()
{
    if (m_done)
        return;

    if (m_methods == 0) {
        emit status(tr("Authenticating as %1…").arg(m_p.username));
        const int rc = ssh_userauth_none(m_session, nullptr);
        if (rc == SSH_AUTH_SUCCESS) {
            onAuthenticated();
            return;
        }
        if (rc == SSH_AUTH_ERROR) {
            fail(QString::fromUtf8(ssh_get_error(m_session)));
            return;
        }
        m_methods = ssh_userauth_list(m_session, nullptr);
    }

    // 0. Keys from the SSH agent of this computer.
    if (m_p.useAgent && !m_agentTried && (m_methods & SSH_AUTH_METHOD_PUBLICKEY)) {
        if (!tryAgent())
            return; // finished
    }

    // 1. Public key from the vault.
    if (!m_keyTried && !m_p.privateKey.isEmpty() && (m_methods & SSH_AUTH_METHOD_PUBLICKEY)) {
        if (!tryPublicKey())
            return; // waiting for a passphrase, or finished
    }

    // 2. Password.
    if (m_methods & SSH_AUTH_METHOD_PASSWORD) {
        if (m_p.password.isEmpty()) {
            requestSecret(SecretPassword, m_passwordAttempts ? tr("Access denied. Password for %1@%2")
                                                                     .arg(m_p.username, m_p.host)
                                                             : tr("Password for %1@%2").arg(m_p.username, m_p.host),
                          false, m_passwordAttempts > 0);
            return;
        }
        QByteArray pw = m_p.password.toUtf8();
        secure::ByteGuard g(pw);
        handleAuthResult(ssh_userauth_password(m_session, nullptr, pw.constData()), SSH_AUTH_METHOD_PASSWORD);
        return;
    }

    // 3. Keyboard-interactive (password prompts, OTP, …).
    if (m_methods & SSH_AUTH_METHOD_INTERACTIVE) {
        m_kbdActive = true;
        m_kbdIndex = 0;
        m_kbdUsedPassword = false;
        kbdStep(ssh_userauth_kbdint(m_session, nullptr, nullptr));
        return;
    }

    fail(tr("No usable authentication method. Server offers: %1").arg(methodsToText(m_methods)));
}

bool SshWorker::tryAgent()
{
    m_agentTried = true;
    if (agent::available().isEmpty()) {
        emit status(tr("No SSH agent is running on this computer; skipping it."));
        return true;
    }
#ifdef _WIN32
    QString err;
    const long long fd = agent::bridgeSocket(&err);
    if (fd < 0 || ssh_set_agent_socket(m_session, socket_t(fd)) != SSH_OK) {
        emit status(err.isEmpty() ? tr("This libssh build cannot use an SSH agent.") : err);
        return true;
    }
#endif
    emit status(tr("Trying the keys from the SSH agent…"));
    const int rc = ssh_userauth_agent(m_session, nullptr);
    if (rc == SSH_AUTH_SUCCESS) {
        m_lastMethod = SSH_AUTH_METHOD_PUBLICKEY;
        onAuthenticated();
        return false;
    }
    if (rc == SSH_AUTH_ERROR && !ssh_is_connected(m_session)) {
        fail(QString::fromUtf8(ssh_get_error(m_session)));
        return false;
    }
    if (rc == SSH_AUTH_PARTIAL) {
        m_methods = ssh_userauth_list(m_session, nullptr);
        emit status(tr("Agent key accepted, the server requires another factor."));
        return true;
    }
    emit status(tr("The server accepted none of the agent's keys."));
    m_methods = ssh_userauth_list(m_session, nullptr);
    return true;
}

ssh_channel SshWorker::onAgentChannelRequest(ssh_session session, void *userdata)
{
    auto *self = static_cast<SshWorker *>(userdata);
    ssh_channel ch = ssh_channel_new(session);
    if (ch && self)
        self->m_agentChans.append(AgentChan{ch, {}});
    return ch;
}

void SshWorker::pumpAgentChannels()
{
    char buf[16 * 1024];
    for (int i = m_agentChans.size() - 1; i >= 0; --i) {
        AgentChan &a = m_agentChans[i];
        bool dead = false;
        for (int guard = 0; guard < 8; ++guard) {
            const int n = ssh_channel_read_nonblocking(a.ch, buf, sizeof buf, 0);
            if (n == SSH_ERROR) { dead = true; break; }
            if (n <= 0) break;
            a.in.append(buf, n);
        }
        // answer every complete request (4-byte length + body)
        while (a.in.size() >= 4) {
            const quint32 len = (quint32(uchar(a.in[0])) << 24) | (quint32(uchar(a.in[1])) << 16)
                              | (quint32(uchar(a.in[2])) << 8) | quint32(uchar(a.in[3]));
            if (len == 0 || len > 256 * 1024) { dead = true; break; }
            if (quint32(a.in.size()) < len + 4) break;
            const QByteArray msg = a.in.left(int(len) + 4);
            a.in.remove(0, int(len) + 4);
            QByteArray reply = agent::request(msg, nullptr);
            if (reply.isEmpty())
                reply = QByteArray("\0\0\0\1\5", 5);   // SSH_AGENT_FAILURE
            if (ssh_channel_write(a.ch, reply.constData(), uint32_t(reply.size())) == SSH_ERROR) { dead = true; break; }
        }
        if (dead || ssh_channel_is_eof(a.ch) || ssh_channel_is_closed(a.ch)) {
            if (ssh_channel_is_open(a.ch))
                ssh_channel_close(a.ch);
            ssh_channel_free(a.ch);
            m_agentChans.removeAt(i);
        }
    }
}

bool SshWorker::tryPublicKey()
{
    if (keytools::isEncrypted(m_p.privateKey) && m_p.keyPassphrase.isEmpty()) {
        requestSecret(SecretKeyPassphrase, tr("Passphrase for the private key"));
        return false;
    }

    QByteArray pass = m_p.keyPassphrase.toUtf8();
    secure::ByteGuard g(pass);
    KeyHolder key;
    if (ssh_pki_import_privkey_base64(m_p.privateKey.constData(),
                                      pass.isEmpty() ? nullptr : pass.constData(),
                                      nullptr, nullptr, &key.key) != SSH_OK) {
        if (keytools::isEncrypted(m_p.privateKey)) {
            secure::wipe(m_p.keyPassphrase);
            requestSecret(SecretKeyPassphrase, tr("Wrong passphrase. Passphrase for the private key"), false, true);
            return false;
        }
        emit status(tr("The stored private key could not be read; skipping it."));
        m_keyTried = true;
        return true;
    }

    // The passphrase opened the key. If the user typed it, offer to store it.
    if (m_passphraseFromUser && !m_p.keyPassphrase.isEmpty()) {
        emit secretAccepted(SecretKeyPassphrase, m_p.keyPassphrase);
        m_passphraseFromUser = false;
    }

    m_keyTried = true;
    const int rc = ssh_userauth_publickey(m_session, nullptr, key.key);
    if (rc == SSH_AUTH_SUCCESS) {
        onAuthenticated();
        return false;
    }
    if (rc == SSH_AUTH_ERROR) {
        fail(QString::fromUtf8(ssh_get_error(m_session)));
        return false;
    }
    if (rc == SSH_AUTH_PARTIAL) {
        m_methods = ssh_userauth_list(m_session, nullptr);
        emit status(tr("Key accepted, the server requires another factor."));
        return true;
    }
    emit status(tr("The server rejected the key."));
    m_methods = ssh_userauth_list(m_session, nullptr);
    return true;
}

void SshWorker::kbdStep(int rc)
{
    while (rc == SSH_AUTH_INFO) {
        const int n = ssh_userauth_kbdint_getnprompts(m_session);
        while (m_kbdIndex < n) {
            char echo = 0;
            const char *p = ssh_userauth_kbdint_getprompt(m_session, unsigned(m_kbdIndex), &echo);
            const QString prompt = QString::fromUtf8(p ? p : "").trimmed();
            const bool looksLikePassword = prompt.contains(QLatin1String("assword"), Qt::CaseInsensitive);
            if (looksLikePassword && !m_p.password.isEmpty() && !m_kbdUsedPassword) {
                QByteArray pw = m_p.password.toUtf8();
                secure::ByteGuard g(pw);
                ssh_userauth_kbdint_setanswer(m_session, unsigned(m_kbdIndex), pw.constData());
                m_kbdUsedPassword = true;
                ++m_kbdIndex;
                continue;
            }
            const QString name = QString::fromUtf8(ssh_userauth_kbdint_getname(m_session));
            requestSecret(SecretKeyboardInteractive,
                          name.isEmpty() ? prompt : name + QStringLiteral("\n") + prompt, echo != 0);
            return; // resumed from answerSecret()
        }
        m_kbdIndex = 0;
        rc = ssh_userauth_kbdint(m_session, nullptr, nullptr);
    }
    m_kbdActive = false;
    handleAuthResult(rc, SSH_AUTH_METHOD_INTERACTIVE);
}

void SshWorker::handleAuthResult(int rc, int method)
{
    m_lastMethod = method;
    switch (rc) {
    case SSH_AUTH_SUCCESS:
        if (m_passwordFromUser && !m_p.password.isEmpty())
            emit secretAccepted(SecretPassword, m_p.password);
        onAuthenticated();
        return;
    case SSH_AUTH_PARTIAL:
        m_methods = ssh_userauth_list(m_session, nullptr);
        emit status(tr("Partial success, the server requires another factor."));
        tryAuth();
        return;
    case SSH_AUTH_DENIED:
        if (++m_passwordAttempts >= 3) {
            fail(tr("Access denied."));
            return;
        }
        secure::wipe(m_p.password);
        m_kbdUsedPassword = false;
        m_methods = ssh_userauth_list(m_session, nullptr);
        tryAuth();
        return;
    case SSH_AUTH_AGAIN:
    case SSH_AUTH_ERROR:
    default:
        fail(QString::fromUtf8(ssh_get_error(m_session)));
        return;
    }
}

void SshWorker::answerSecret(const QString &secret, bool cancelled)
{
    if (!m_session || m_done || m_pendingSecret < 0)
        return;
    const int kind = m_pendingSecret;
    m_pendingSecret = -1;

    if (cancelled) {
        fail(tr("Authentication cancelled."));
        return;
    }

    switch (kind) {
    case SecretPassword:
        m_p.password = secret;
        m_passwordFromUser = true;
        tryAuth();
        break;
    case SecretKeyPassphrase:
        m_p.keyPassphrase = secret;
        m_passphraseFromUser = true;
        tryAuth();
        break;
    case SecretKeyboardInteractive: {
        QByteArray a = secret.toUtf8();
        secure::ByteGuard g(a);
        ssh_userauth_kbdint_setanswer(m_session, unsigned(m_kbdIndex), a.constData());
        ++m_kbdIndex;
        kbdStep(SSH_AUTH_INFO);
        break;
    }
    }
}

void SshWorker::onAuthenticated()
{
    secure::wipe(m_p.password);
    secure::wipe(m_p.keyPassphrase);
    secure::wipe(m_p.privateKey);
    if (!m_p.execCommand.isEmpty()) {
        // batch mode: one command, then disconnect
        emit connected();
        QByteArray out;
        QString err;
        const int code = execOn(m_p.execCommand, m_p.execTimeoutMs, &out, &err);
        emit execDone(0, code, out, err);
        cleanup();
        m_done = true;
        emit finished(QString(), false);
        return;
    }
    // $SSH_CONNECTION is read on a short exec channel that the server handles BEFORE the
    // shell. Besides the client address this keeps the screen clean: with PAM (Debian,
    // Ubuntu) sshd hands the one-time login banner (motd) to the first session.
    // Both channels are opened in parallel so this costs no extra round trips.
    if (openPipelined()) {
        finishShell();
        return;
    }
    if (!m_done)
        openShell();   // classic order: address first, then the shell
}

static const char kAddrCommand[] = "echo \"$SSH_CONNECTION\"";

bool SshWorker::openPipelined()
{
    ssh_channel addr = ssh_channel_new(m_session);
    m_channel = ssh_channel_new(m_session);
    if (!addr || !m_channel) {
        if (addr)
            ssh_channel_free(addr);
        if (m_channel)
            ssh_channel_free(m_channel);
        m_channel = nullptr;
        readClientAddress();
        return false;
    }
    const QByteArray term = m_p.term.toLatin1();
    const qint64 limit = qint64(std::max(10, m_p.connectTimeoutSeconds)) * 1000;
    QElapsedTimer t;
    t.start();
    ssh_set_blocking(m_session, 0);
    // 1. open both channels: one round trip instead of two
    int a = SSH_AGAIN, s = SSH_AGAIN;
    while ((a == SSH_AGAIN || s == SSH_AGAIN) && t.elapsed() < limit) {
        if (a == SSH_AGAIN)
            a = ssh_channel_open_session(addr);
        if (s == SSH_AGAIN)
            s = ssh_channel_open_session(m_channel);
        if (a == SSH_AGAIN || s == SSH_AGAIN)
            QThread::msleep(1);
    }
    // 2. exec on the first channel (sent first, so sshd sets that session up first and
    //    it takes the login banner), terminal on the shell channel, again in parallel
    int ae = a == SSH_OK ? SSH_AGAIN : SSH_ERROR;
    int sp = s == SSH_OK ? SSH_AGAIN : SSH_ERROR;
    while ((ae == SSH_AGAIN || sp == SSH_AGAIN) && t.elapsed() < limit) {
        if (ae == SSH_AGAIN)
            ae = ssh_channel_request_exec(addr, kAddrCommand);
        if (sp == SSH_AGAIN)
            sp = ssh_channel_request_pty_size(m_channel, term.constData(), m_p.cols, m_p.rows);
        if (ae == SSH_AGAIN || sp == SSH_AGAIN)
            QThread::msleep(1);
    }
    ssh_set_blocking(m_session, 1);

    if (ae == SSH_OK) {
        m_addrCh = addr;
        m_addrTimer.start();
    } else {
        if (ssh_channel_is_open(addr))
            ssh_channel_close(addr);
        ssh_channel_free(addr);
    }
    if (sp == SSH_OK)
        return true;

    // The shell channel failed (for example MaxSessions 1 refuses a second session):
    // finish the address channel first, then open the shell the classic way.
    if (ssh_channel_is_open(m_channel))
        ssh_channel_close(m_channel);
    ssh_channel_free(m_channel);
    m_channel = nullptr;
    if (!ssh_is_connected(m_session)) {
        fail(tr("Cannot open a session channel: %1").arg(QString::fromUtf8(ssh_get_error(m_session))));
        return false;
    }
    if (m_addrCh) {
        readClientAddress(m_addrCh);
        m_addrCh = nullptr;
    }
    return false;
}

void SshWorker::pumpAddress()
{
    if (!m_addrCh)
        return;
    char buf[256];
    bool end = false;
    for (int guard = 0; guard < 8; ++guard) {
        const int n = ssh_channel_read_nonblocking(m_addrCh, buf, sizeof buf, 0);
        if (n > 0)
            m_addrBuf.append(buf, n);
        if (n == SSH_ERROR || ssh_channel_is_eof(m_addrCh)) {
            end = true;
            break;
        }
        if (n <= 0)
            break;
    }
    if (!end && !m_addrBuf.contains('\n') && m_addrBuf.size() < 1024 && m_addrTimer.elapsed() < 8000)
        return;
    const QStringList parts = QString::fromUtf8(m_addrBuf).simplified().split(QLatin1Char(' '));
    if (parts.size() >= 4)
        emit clientAddressSeen(parts.at(0), parts.at(1), parts.at(2));
    m_addrBuf.clear();
    if (ssh_channel_is_open(m_addrCh)) {
        ssh_channel_send_eof(m_addrCh);
        ssh_channel_close(m_addrCh);
    }
    ssh_channel_free(m_addrCh);
    m_addrCh = nullptr;
}

void SshWorker::readClientAddress(ssh_channel existing)
{
    // $SSH_CONNECTION = "<client ip> <client port> <server ip> <server port>".
    // This is exactly the address sshd writes into its auth log.
    ssh_channel ch = existing ? existing : ssh_channel_new(m_session);
    if (!ch)
        return;
    if (existing || (ssh_channel_open_session(ch) == SSH_OK
                     && ssh_channel_request_exec(ch, kAddrCommand) == SSH_OK)) {
        QByteArray out;
        char buf[256];
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 4000 && out.size() < 1024) {
            const int n = ssh_channel_read_timeout(ch, buf, sizeof buf, 0, 500);
            if (n > 0)
                out.append(buf, n);
            else if (n == SSH_ERROR || ssh_channel_is_eof(ch))
                break;
            if (out.contains('\n'))
                break;
        }
        const QStringList parts = QString::fromUtf8(out).simplified().split(QLatin1Char(' '));
        if (parts.size() >= 4)
            emit clientAddressSeen(parts.at(0), parts.at(1), parts.at(2));
        // Let the command finish on the server: with MaxSessions 1 sshd frees the
        // session only after that, and the shell needs that slot.
        while (t.elapsed() < 4000 && !ssh_channel_is_eof(ch)) {
            if (ssh_channel_read_timeout(ch, buf, sizeof buf, 0, 200) == SSH_ERROR)
                break;
        }
        ssh_channel_send_eof(ch);
    }
    ssh_channel_close(ch);
    ssh_channel_free(ch);
    m_addrSequential = true;
}

void SshWorker::openShell()
{
    m_channel = ssh_channel_new(m_session);
    // right after the address channel a strict server may still count that session
    for (int retry = 0; m_channel && m_addrSequential && retry < 5; ++retry) {
        if (ssh_channel_open_session(m_channel) == SSH_OK)
            break;
        ssh_channel_free(m_channel);
        m_channel = nullptr;
        if (!ssh_is_connected(m_session))
            break;
        QThread::msleep(150);
        m_channel = ssh_channel_new(m_session);
    }
    if (!m_channel || (!ssh_channel_is_open(m_channel) && ssh_channel_open_session(m_channel) != SSH_OK)) {
        fail(tr("Cannot open a session channel: %1").arg(QString::fromUtf8(ssh_get_error(m_session))));
        return;
    }
    const QByteArray term = m_p.term.toLatin1();
    if (ssh_channel_request_pty_size(m_channel, term.constData(), m_p.cols, m_p.rows) != SSH_OK) {
        fail(tr("The server refused a terminal: %1").arg(QString::fromUtf8(ssh_get_error(m_session))));
        return;
    }
    finishShell();
}

void SshWorker::finishShell()
{
    // Best effort; many servers only accept LANG/LC_* via AcceptEnv.
    ssh_channel_request_env(m_channel, "LANG", "en_US.UTF-8");
    if (m_p.agentForward) {
        if (agent::available().isEmpty())
            emit status(tr("Agent forwarding is on, but no SSH agent is running on this computer."));
        else if (ssh_channel_request_auth_agent(m_channel) != SSH_OK)
            emit status(tr("The server refused agent forwarding."));
    }
    if (ssh_channel_request_shell(m_channel) != SSH_OK) {
        fail(tr("The server refused a shell: %1").arg(QString::fromUtf8(ssh_get_error(m_session))));
        return;
    }

    const socket_t fd = ssh_get_fd(m_session);
    m_notifier = new QSocketNotifier(qintptr(fd), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &SshWorker::pump);

    // libssh may buffer data internally (e.g. after a write), so also poll.
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(25);
    connect(m_pollTimer, &QTimer::timeout, this, &SshWorker::pump);
    m_pollTimer->start();

    if (m_p.keepaliveSeconds > 0) {
        m_keepalive = new QTimer(this);
        m_keepalive->setInterval(m_p.keepaliveSeconds * 1000);
        connect(m_keepalive, &QTimer::timeout, this, [this] {
            if (m_session && ssh_send_ignore(m_session, "keepalive") != SSH_OK)
                fail(tr("Connection lost (keepalive failed)."));
        });
        m_keepalive->start();
    }

    m_shellOpen = true;
    emit connected();
    emit status(tr("Connected"));

    // Port forwards configured for this host.
    m_fwdTimer = new QTimer(this);
    m_fwdTimer->setInterval(250);
    connect(m_fwdTimer, &QTimer::timeout, this, [this] { publishForwards(); });
    m_fwdTimer->start();
    for (const ForwardSpec &spec : std::as_const(m_p.forwards))
        addForward(spec);
}

void SshWorker::pump()
{
    if (!m_channel || m_done)
        return;

    static constexpr int kChunk = 64 * 1024;
    char buf[kChunk];
    QByteArray batch;
    for (int is_stderr = 0; is_stderr <= 1; ++is_stderr) {
        for (int guard = 0; guard < 16; ++guard) {
            const int n = ssh_channel_read_nonblocking(m_channel, buf, kChunk, is_stderr);
            if (n == SSH_ERROR) {
                if (!batch.isEmpty())
                    emit dataReceived(batch);
                fail(tr("Connection lost: %1").arg(QString::fromUtf8(ssh_get_error(m_session))));
                return;
            }
            if (n <= 0)
                break;
            batch.append(buf, n);
        }
    }
    if (!batch.isEmpty())
        emit dataReceived(batch);

    pumpForwards();
    pumpTransfers();
    pumpAgentChannels();
    pumpMonitor();
    pumpAddress();
    if (m_done)
        return;

    if (ssh_channel_is_eof(m_channel) || ssh_channel_is_closed(m_channel)) {
        const int code = ssh_channel_get_exit_status(m_channel);
        cleanup();
        m_done = true;
        emit finished(code >= 0 ? tr("Session ended (exit code %1).").arg(code) : tr("Session ended."), false);
    }
}

void SshWorker::write(const QByteArray &data)
{
    if (!m_channel || m_done || data.isEmpty())
        return;
    int off = 0;
    while (off < data.size()) {
        const int n = ssh_channel_write(m_channel, data.constData() + off, uint32_t(data.size() - off));
        if (n == SSH_ERROR) {
            fail(tr("Write failed: %1").arg(QString::fromUtf8(ssh_get_error(m_session))));
            return;
        }
        off += n;
    }
}

void SshWorker::resize(int cols, int rows)
{
    m_p.cols = cols;
    m_p.rows = rows;
    if (m_channel && !m_done)
        ssh_channel_change_pty_size(m_channel, cols, rows);
}

void SshWorker::stop()
{
    if (m_done)
        return;
    cleanup();
    m_done = true;
    emit finished(tr("Disconnected."), false);
}

void SshWorker::fail(const QString &reason)
{
    if (m_done)
        return;
    cleanup();
    m_done = true;
    emit finished(reason, true);
}

void SshWorker::cleanup()
{
    for (Transfer &t : m_transfers) {
        if (t.remoteFile)
            sftp_close(t.remoteFile);
        if (t.localFile) {
            delete t.localFile;   // an unfinished download keeps its ".part": the next try resumes
        }
    }
    m_transfers.clear();
    for (FwdConn *c : std::as_const(m_conns)) {
        if (c->ch) {
            ssh_channel_close(c->ch);
            ssh_channel_free(c->ch);
        }
        if (c->sock) {
            c->sock->disconnect(this);
            c->sock->abort();
            c->sock->deleteLater();
        }
        delete c;
    }
    m_conns.clear();
    for (FwdState &f : m_forwards) {
        delete f.server;
        f.server = nullptr;
    }
    m_forwards.clear();
    delete m_fwdTimer;
    m_fwdTimer = nullptr;
    if (m_sftp) {
        sftp_free(m_sftp);
        m_sftp = nullptr;
    }
    m_shellOpen = false;

    delete m_notifier;
    m_notifier = nullptr;
    delete m_pollTimer;
    m_pollTimer = nullptr;
    delete m_keepalive;
    m_keepalive = nullptr;

    if (m_addrCh) {
        if (ssh_channel_is_open(m_addrCh))
            ssh_channel_close(m_addrCh);
        ssh_channel_free(m_addrCh);
        m_addrCh = nullptr;
    }
    if (m_monCh) {
        if (ssh_channel_is_open(m_monCh))
            ssh_channel_close(m_monCh);
        ssh_channel_free(m_monCh);
        m_monCh = nullptr;
    }
    for (AgentChan &a : m_agentChans) {
        if (ssh_channel_is_open(a.ch))
            ssh_channel_close(a.ch);
        ssh_channel_free(a.ch);
    }
    m_agentChans.clear();
    if (m_channel) {
        if (ssh_channel_is_open(m_channel)) {
            ssh_channel_send_eof(m_channel);
            ssh_channel_close(m_channel);
        }
        ssh_channel_free(m_channel);
        m_channel = nullptr;
    }
    if (m_session) {
        if (ssh_is_connected(m_session))
            ssh_disconnect(m_session);
        ssh_free(m_session);
        m_session = nullptr;
    }
    m_jump.reset();
    secure::wipe(m_p.password);
    secure::wipe(m_p.keyPassphrase);
    secure::wipe(m_p.privateKey);
}

void SshWorker::exec(int reqId, const QString &command, int timeoutMs)
{
    if (!m_session || m_done || !m_channel) {
        emit execDone(reqId, -1, {}, tr("Not connected."));
        return;
    }
    QByteArray out;
    QString err;
    const int code = execOn(command, timeoutMs, &out, &err);
    emit execDone(reqId, code, out, err);
}

void SshWorker::startMonitor(const QString &command, int intervalSec)
{
    stopMonitor();
    if (!m_session || m_done || !m_channel)
        return;
    // the loop runs on the server; sent as base64 so any login shell (bash, zsh, fish) runs it
    const QByteArray loop = QStringLiteral("while :; do %1; echo @END; sleep %2; done")
                                .arg(command).arg(std::max(1, intervalSec)).toUtf8();
    const QByteArray cmd = "echo " + loop.toBase64() + " | base64 -d | sh";
    ssh_channel ch = ssh_channel_new(m_session);
    if (!ch)
        return;
    if (ssh_channel_open_session(ch) != SSH_OK || ssh_channel_request_exec(ch, cmd.constData()) != SSH_OK) {
        emit execDone(1, -1, {}, QString::fromUtf8(ssh_get_error(m_session)));
        ssh_channel_free(ch);
        return;
    }
    m_monCh = ch;
    m_monBuf.clear();
}

void SshWorker::stopMonitor()
{
    if (!m_monCh)
        return;
    if (ssh_channel_is_open(m_monCh)) {
        ssh_channel_send_eof(m_monCh);
        ssh_channel_close(m_monCh);
    }
    ssh_channel_free(m_monCh);
    m_monCh = nullptr;
    m_monBuf.clear();
}

void SshWorker::pumpMonitor()
{
    if (!m_monCh)
        return;
    char buf[4096];
    for (int guard = 0; guard < 8; ++guard) {
        int n = ssh_channel_read_nonblocking(m_monCh, buf, sizeof buf, 0);
        if (n <= 0)
            n = ssh_channel_read_nonblocking(m_monCh, buf, sizeof buf, 1);   // stderr: shown as the error
        if (n <= 0)
            break;
        m_monBuf.append(buf, n);
    }
    int end;
    while ((end = m_monBuf.indexOf("@END\n")) >= 0) {
        const QByteArray sample = m_monBuf.left(end);
        m_monBuf.remove(0, end + 5);
        emit execDone(1, 0, sample, QString());
    }
    if (m_monBuf.size() > 256 * 1024)
        m_monBuf.clear();
    if (ssh_channel_is_eof(m_monCh) || ssh_channel_is_closed(m_monCh)) {
        // e.g. no sh / base64 / /proc on this server (Windows OpenSSH)
        const QString why = QString::fromUtf8(m_monBuf).trimmed().left(200);
        stopMonitor();
        emit execDone(1, -1, {}, why.isEmpty() ? tr("The monitor stopped on the server.") : why);
    }
}

int SshWorker::execOn(const QString &command, int timeoutMs, QByteArray *outp, QString *errp)
{
    QByteArray &out = *outp;
    QString &err = *errp;
    ssh_channel ch = ssh_channel_new(m_session);
    if (!ch) {
        err = tr("Cannot open a channel.");
        return -1;
    }
    int code = -1;
    const QByteArray cmd = command.toUtf8();
    if (ssh_channel_open_session(ch) == SSH_OK && ssh_channel_request_exec(ch, cmd.constData()) == SSH_OK) {
        char buf[4096];
        QElapsedTimer t;
        t.start();
        bool timedOut = true;
        while (t.elapsed() < timeoutMs && out.size() < 4 * 1024 * 1024) {
            int n = ssh_channel_read_timeout(ch, buf, sizeof buf, 0, 200);
            if (n <= 0 && n != SSH_ERROR)
                n = ssh_channel_read_nonblocking(ch, buf, sizeof buf, 1);   // stderr
            if (n > 0) {
                out.append(buf, n);
                continue;
            }
            if (n == SSH_ERROR) { err = QString::fromUtf8(ssh_get_error(m_session)); timedOut = false; break; }
            if (ssh_channel_is_eof(ch)) { timedOut = false; break; }
        }
        if (timedOut && err.isEmpty())
            err = tr("No answer in %1 s.").arg(timeoutMs / 1000);
        code = ssh_channel_get_exit_status(ch);
        ssh_channel_send_eof(ch);
    } else {
        err = QString::fromUtf8(ssh_get_error(m_session));
    }
    ssh_channel_close(ch);
    ssh_channel_free(ch);
    return code;
}
