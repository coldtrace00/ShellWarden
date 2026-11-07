#include "SafeText.h"
#include "JumpBridge.h"
#include "KeyTools.h"
#include "SecureBytes.h"

#include <QObject>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

namespace {
struct KeyHolder
{
    ssh_key key = nullptr;
    ~KeyHolder() { if (key) ssh_key_free(key); }
};
} // namespace

JumpBridge::~JumpBridge() { stop(); }

sw_socket_t JumpBridge::start(const JumpParams &jump, const ProxyConfig &proxy, const QString &knownHosts,
                              const QString &targetHost, int targetPort, int timeoutSeconds, QString *error)
{
    m_session = ssh_new();
    if (!m_session) {
        *error = QObject::tr("Out of memory.");
        return kInvalidSocket;
    }
    const QByteArray host = jump.host.toUtf8();
    const QByteArray user = jump.username.toUtf8();
    const QByteArray kh = knownHosts.toUtf8();
    const unsigned int port = unsigned(jump.port);
    const long timeout = timeoutSeconds;
    const int no = 0;
    ssh_options_set(m_session, SSH_OPTIONS_HOST, host.constData());
    ssh_options_set(m_session, SSH_OPTIONS_PORT, &port);
    if (!user.isEmpty())
        ssh_options_set(m_session, SSH_OPTIONS_USER, user.constData());
    ssh_options_set(m_session, SSH_OPTIONS_KNOWNHOSTS, kh.constData());
    ssh_options_set(m_session, SSH_OPTIONS_TIMEOUT, &timeout);
    ssh_options_set(m_session, SSH_OPTIONS_PROCESS_CONFIG, &no);

    if (proxy.isSet()) {
        QString perr;
        const sw_socket_t fd = proxytunnel::open(proxy, jump.host, jump.port, timeoutSeconds * 1000, &perr);
        if (fd == kInvalidSocket) {
            *error = QObject::tr("proxy failed (%1). Direct connection was NOT attempted.").arg(perr);
            return kInvalidSocket;
        }
        socket_t s = static_cast<socket_t>(fd);
        ssh_options_set(m_session, SSH_OPTIONS_FD, &s);
    }

    if (ssh_connect(m_session) != SSH_OK) {
        *error = safetext::snippet(ssh_get_error(m_session));
        return kInvalidSocket;
    }
    switch (ssh_session_is_known_server(m_session)) {
    case SSH_KNOWN_HOSTS_OK:
        break;
    case SSH_KNOWN_HOSTS_CHANGED:
    case SSH_KNOWN_HOSTS_OTHER:
        *error = QObject::tr("the jump host's key has CHANGED. Connect to it directly to review the key.");
        return kInvalidSocket;
    default:
        *error = QObject::tr("the jump host's key is not trusted yet. Connect to it directly once and confirm its fingerprint.");
        return kInvalidSocket;
    }
    if (!authenticate(jump, error))
        return kInvalidSocket;

    m_channel = ssh_channel_new(m_session);
    const QByteArray th = targetHost.toUtf8();
    if (!m_channel || ssh_channel_open_forward(m_channel, th.constData(), targetPort, "127.0.0.1", 0) != SSH_OK) {
        *error = QObject::tr("the jump host could not reach %1:%2 (%3)")
                     .arg(targetHost).arg(targetPort).arg(safetext::snippet(ssh_get_error(m_session)));
        return kInvalidSocket;
    }

    sw_socket_t a = kInvalidSocket, b = kInvalidSocket;
    if (!proxytunnel::loopbackPair(&a, &b, error))
        return kInvalidSocket;
    m_local = a;
    m_thread = std::thread([this] { run(); });
    return b;
}

bool JumpBridge::authenticate(const JumpParams &jump, QString *error)
{
    int rc = ssh_userauth_none(m_session, nullptr);
    if (rc == SSH_AUTH_SUCCESS)
        return true;
    const int methods = ssh_userauth_list(m_session, nullptr);

    if (!jump.privateKey.isEmpty() && (methods & SSH_AUTH_METHOD_PUBLICKEY)) {
        QByteArray pass = jump.keyPassphrase.toUtf8();
        secure::ByteGuard g(pass);
        KeyHolder key;
        if (ssh_pki_import_privkey_base64(jump.privateKey.constData(), pass.isEmpty() ? nullptr : pass.constData(),
                                          nullptr, nullptr, &key.key) == SSH_OK) {
            rc = ssh_userauth_publickey(m_session, nullptr, key.key);
            if (rc == SSH_AUTH_SUCCESS)
                return true;
        } else if (keytools::isEncrypted(jump.privateKey)) {
            *error = QObject::tr("the jump host's key passphrase is not saved in the vault.");
            return false;
        }
    }
    if (!jump.password.isEmpty()) {
        QByteArray pw = jump.password.toUtf8();
        secure::ByteGuard g(pw);
        if (methods & SSH_AUTH_METHOD_PASSWORD) {
            if (ssh_userauth_password(m_session, nullptr, pw.constData()) == SSH_AUTH_SUCCESS)
                return true;
        }
        if (methods & SSH_AUTH_METHOD_INTERACTIVE) {
            rc = ssh_userauth_kbdint(m_session, nullptr, nullptr);
            while (rc == SSH_AUTH_INFO) {
                const int n = ssh_userauth_kbdint_getnprompts(m_session);
                for (int i = 0; i < n; ++i)
                    ssh_userauth_kbdint_setanswer(m_session, unsigned(i), pw.constData());
                rc = ssh_userauth_kbdint(m_session, nullptr, nullptr);
            }
            if (rc == SSH_AUTH_SUCCESS)
                return true;
        }
    }
    *error = QObject::tr("could not log in to the jump host with the saved credentials (save its password or key passphrase in the vault).");
    return false;
}

void JumpBridge::run()
{
    char buf[32 * 1024];
    const socket_t sshFd = ssh_get_fd(m_session);
    while (!m_stop.load()) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(m_local, &rd);
        FD_SET(sshFd, &rd);
        timeval tv{0, 50 * 1000};
        const int maxFd = int(std::max<sw_socket_t>(m_local, sw_socket_t(sshFd)));
        const int r = ::select(maxFd + 1, &rd, nullptr, nullptr, &tv);
        if (r < 0)
            break;

        if (FD_ISSET(m_local, &rd)) {
            const int n = int(::recv(m_local, buf, sizeof buf, 0));
            if (n <= 0)
                break;
            int off = 0;
            while (off < n) {
                const int w = ssh_channel_write(m_channel, buf + off, uint32_t(n - off));
                if (w == SSH_ERROR)
                    goto out;
                off += w;
            }
        }
        // drain whatever libssh has for us
        for (;;) {
            const int n = ssh_channel_read_nonblocking(m_channel, buf, sizeof buf, 0);
            if (n == SSH_ERROR)
                goto out;
            if (n <= 0)
                break;
            int off = 0;
            while (off < n) {
                const int w = int(::send(m_local, buf + off, n - off, 0));
                if (w <= 0)
                    goto out;
                off += w;
            }
        }
        if (ssh_channel_is_eof(m_channel) || ssh_channel_is_closed(m_channel))
            break;
    }
out:
    proxytunnel::closeSocket(m_local);   // the target session sees EOF
    m_local = kInvalidSocket;
}

void JumpBridge::stop()
{
    m_stop.store(true);
    if (m_thread.joinable())
        m_thread.join();
    if (m_local != kInvalidSocket) {
        proxytunnel::closeSocket(m_local);
        m_local = kInvalidSocket;
    }
    if (m_channel) {
        ssh_channel_close(m_channel);
        ssh_channel_free(m_channel);
        m_channel = nullptr;
    }
    if (m_session) {
        ssh_disconnect(m_session);
        ssh_free(m_session);
        m_session = nullptr;
    }
}
