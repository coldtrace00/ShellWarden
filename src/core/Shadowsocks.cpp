#include "Shadowsocks.h"
#include "SecureBytes.h"

#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QObject>
#include <QUrl>
#include <QUrlQuery>

#include <openssl/evp.h>
#include <sodium.h>

#include <chrono>
#include <cstring>
#include <memory>
#include <thread>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
using native_socket = SOCKET;
#  define SS_CLOSE closesocket
#else
#  include <poll.h>
#  include <sys/socket.h>
#  include <unistd.h>
using native_socket = int;
#  define SS_CLOSE ::close
#endif

namespace shadowsocks {

namespace {

using Clock = std::chrono::steady_clock;

struct CipherInfo { const char *name; int keyLen; };
constexpr CipherInfo kCiphers[] = {
    {"chacha20-ietf-poly1305", 32},
    {"aes-256-gcm", 32},
    {"aes-128-gcm", 16},
};
constexpr int kTagLen = 16;
constexpr int kNonceLen = 12;
constexpr int kMaxPayload = 0x3FFF;   // SIP004: payload length is at most 16383

int keyLenOf(const QString &method)
{
    for (const CipherInfo &c : kCiphers)
        if (method == QLatin1String(c.name))
            return c.keyLen;
    return 0;
}

// One direction of the stream: subkey + little-endian nonce counter.
class Aead
{
public:
    Aead(const QString &method, const QByteArray &subkey) : m_chacha(method.startsWith(QLatin1String("chacha"))), m_key(subkey)
    {
        if (!m_chacha) {
            m_ctx = EVP_CIPHER_CTX_new();
            m_cipher = subkey.size() == 32 ? EVP_aes_256_gcm() : EVP_aes_128_gcm();
        }
    }
    ~Aead()
    {
        if (m_ctx)
            EVP_CIPHER_CTX_free(m_ctx);
        secure::wipe(m_key);
    }
    Aead(const Aead &) = delete;
    Aead &operator=(const Aead &) = delete;

    QByteArray seal(const char *data, int len)
    {
        QByteArray out(len + kTagLen, Qt::Uninitialized);
        bool ok = false;
        if (m_chacha) {
            unsigned long long outLen = 0;
            ok = crypto_aead_chacha20poly1305_ietf_encrypt(
                     reinterpret_cast<unsigned char *>(out.data()), &outLen, reinterpret_cast<const unsigned char *>(data),
                     size_t(len), nullptr, 0, nullptr, m_nonce, reinterpret_cast<const unsigned char *>(m_key.constData())) == 0;
        } else {
            int n = 0, f = 0;
            ok = EVP_EncryptInit_ex(m_ctx, m_cipher, nullptr, nullptr, nullptr) == 1
                 && EVP_CIPHER_CTX_ctrl(m_ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceLen, nullptr) == 1
                 && EVP_EncryptInit_ex(m_ctx, nullptr, nullptr, reinterpret_cast<const unsigned char *>(m_key.constData()), m_nonce) == 1
                 && EVP_EncryptUpdate(m_ctx, reinterpret_cast<unsigned char *>(out.data()), &n,
                                      reinterpret_cast<const unsigned char *>(data), len) == 1
                 && EVP_EncryptFinal_ex(m_ctx, reinterpret_cast<unsigned char *>(out.data()) + n, &f) == 1
                 && EVP_CIPHER_CTX_ctrl(m_ctx, EVP_CTRL_GCM_GET_TAG, kTagLen, out.data() + len) == 1;
        }
        next();
        return ok ? out : QByteArray();
    }

    bool open(const char *data, int len, QByteArray *plain)
    {
        if (len < kTagLen)
            return false;
        const int plainLen = len - kTagLen;
        plain->resize(plainLen);
        bool ok = false;
        if (m_chacha) {
            unsigned long long outLen = 0;
            ok = crypto_aead_chacha20poly1305_ietf_decrypt(
                     reinterpret_cast<unsigned char *>(plain->data()), &outLen, nullptr,
                     reinterpret_cast<const unsigned char *>(data), size_t(len), nullptr, 0, m_nonce,
                     reinterpret_cast<const unsigned char *>(m_key.constData())) == 0;
        } else {
            int n = 0, f = 0;
            QByteArray tag(data + plainLen, kTagLen);
            ok = EVP_DecryptInit_ex(m_ctx, m_cipher, nullptr, nullptr, nullptr) == 1
                 && EVP_CIPHER_CTX_ctrl(m_ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceLen, nullptr) == 1
                 && EVP_DecryptInit_ex(m_ctx, nullptr, nullptr, reinterpret_cast<const unsigned char *>(m_key.constData()), m_nonce) == 1
                 && EVP_DecryptUpdate(m_ctx, reinterpret_cast<unsigned char *>(plain->data()), &n,
                                      reinterpret_cast<const unsigned char *>(data), plainLen) == 1
                 && EVP_CIPHER_CTX_ctrl(m_ctx, EVP_CTRL_GCM_SET_TAG, kTagLen, tag.data()) == 1
                 && EVP_DecryptFinal_ex(m_ctx, reinterpret_cast<unsigned char *>(plain->data()) + n, &f) == 1;
        }
        next();
        return ok;
    }

private:
    void next() { sodium_increment(m_nonce, kNonceLen); }   // little-endian counter, as SIP004 wants

    bool m_chacha;
    QByteArray m_key;
    unsigned char m_nonce[kNonceLen] = {};
    EVP_CIPHER_CTX *m_ctx = nullptr;
    const EVP_CIPHER *m_cipher = nullptr;
};

bool waitReadable(native_socket a, native_socket b, int timeoutMs, bool *aReady, bool *bReady)
{
#ifdef _WIN32
    WSAPOLLFD p[2] = {};
    p[0].fd = a; p[0].events = POLLRDNORM;
    p[1].fd = b; p[1].events = POLLRDNORM;
    const int n = WSAPoll(p, b == INVALID_SOCKET ? 1 : 2, timeoutMs);
#else
    pollfd p[2] = {};
    p[0].fd = a; p[0].events = POLLIN;
    p[1].fd = b; p[1].events = POLLIN;
    const int n = ::poll(p, b < 0 ? 1 : 2, timeoutMs);
#endif
    if (n <= 0) {
        *aReady = *bReady = false;
        return n == 0;   // timeout = still fine, error = stop
    }
    *aReady = p[0].revents != 0;
    *bReady = p[1].revents != 0;
    return true;
}

bool sendAll(native_socket s, const QByteArray &data)
{
    int sent = 0;
    while (sent < data.size()) {
        const int n = int(::send(s, data.constData() + sent, int(data.size() - sent), 0));
        if (n <= 0)
            return false;
        sent += n;
    }
    return true;
}

// The encrypted connection to the server: salts, both directions, chunk framing.
class Session
{
public:
    Session(native_socket server, const QString &method, const QByteArray &key)
        : m_server(server), m_method(method), m_key(key) {}
    ~Session()
    {
        secure::wipe(m_key);
        if (m_server != native_socket(kInvalidSocket))
            SS_CLOSE(m_server);
    }

    // first message: our salt + the target address in the first chunk
    bool start(const QString &host, int port)
    {
        QByteArray salt(m_key.size(), Qt::Uninitialized);
        randombytes_buf(salt.data(), size_t(salt.size()));
        m_enc = std::make_unique<Aead>(m_method, hkdfSha1(m_key, salt, "ss-subkey", m_key.size()));
        QByteArray addr;
        const QByteArray h = QUrl::toAce(host).isEmpty() ? host.toUtf8() : QUrl::toAce(host);
        addr.append(char(0x03));               // ATYP: domain name, resolved by the server (no DNS leak)
        addr.append(char(h.size()));
        addr.append(h);
        addr.append(char((port >> 8) & 0xff));
        addr.append(char(port & 0xff));
        return h.size() > 0 && h.size() < 256 && sendAll(m_server, salt + seal(addr));
    }

    QByteArray seal(const QByteArray &plain)
    {
        QByteArray out;
        for (int off = 0; off < plain.size(); off += kMaxPayload) {
            const int n = std::min<int>(kMaxPayload, int(plain.size()) - off);
            const char len[2] = {char((n >> 8) & 0xff), char(n & 0xff)};
            out += m_enc->seal(len, 2);
            out += m_enc->seal(plain.constData() + off, n);
        }
        return out;
    }
    bool send(const char *data, int len) { return sendAll(m_server, seal(QByteArray::fromRawData(data, len))); }

    // feed bytes from the server; returns false when they cannot be authenticated
    bool receive(const char *data, int len, QByteArray *plain)
    {
        m_in.append(data, len);
        if (!m_dec) {
            if (m_in.size() < m_key.size())
                return true;
            m_dec = std::make_unique<Aead>(m_method, hkdfSha1(m_key, m_in.left(m_key.size()), "ss-subkey", m_key.size()));
            m_in.remove(0, m_key.size());
        }
        for (;;) {
            if (m_want < 0) {   // waiting for the length chunk
                if (m_in.size() < 2 + kTagLen)
                    return true;
                QByteArray len;
                if (!m_dec->open(m_in.constData(), 2 + kTagLen, &len))
                    return false;
                m_in.remove(0, 2 + kTagLen);
                m_want = ((uchar(len[0]) << 8) | uchar(len[1])) & kMaxPayload;
            }
            if (m_in.size() < m_want + kTagLen)
                return true;
            QByteArray chunk;
            if (!m_dec->open(m_in.constData(), m_want + kTagLen, &chunk))
                return false;
            m_in.remove(0, m_want + kTagLen);
            m_want = -1;
            plain->append(chunk);
        }
    }

    native_socket server() const { return m_server; }

private:
    native_socket m_server;
    QString m_method;
    QByteArray m_key;
    std::unique_ptr<Aead> m_enc, m_dec;
    QByteArray m_in;
    int m_want = -1;
};

// Copies between the loopback end (libssh) and the encrypted server connection.
void relay(std::shared_ptr<Session> s, native_socket local, QByteArray pending)
{
    if (!pending.isEmpty() && !sendAll(local, pending)) {
        SS_CLOSE(local);
        return;
    }
    char buf[kMaxPayload];
    for (;;) {
        bool fromLocal = false, fromServer = false;
        if (!waitReadable(local, s->server(), 30000, &fromLocal, &fromServer))
            break;
        if (fromLocal) {
            const int n = int(::recv(local, buf, sizeof buf, 0));
            if (n <= 0 || !s->send(buf, n))
                break;
        }
        if (fromServer) {
            const int n = int(::recv(s->server(), buf, sizeof buf, 0));
            if (n <= 0)
                break;
            QByteArray plain;
            if (!s->receive(buf, n, &plain) || (!plain.isEmpty() && !sendAll(local, plain)))
                break;
        }
    }
    SS_CLOSE(local);   // libssh sees the end of the stream
}

} // namespace

QStringList methods()
{
    QStringList out;
    for (const CipherInfo &c : kCiphers)
        out << QString::fromLatin1(c.name);
    return out;
}

bool isSupported(const QString &method) { return keyLenOf(method) > 0; }

QByteArray masterKey(const QByteArray &password, int keyLen)
{
    QByteArray key, prev;
    while (key.size() < keyLen) {
        prev = QCryptographicHash::hash(prev + password, QCryptographicHash::Md5);
        key += prev;
    }
    return key.left(keyLen);
}

QByteArray hkdfSha1(const QByteArray &key, const QByteArray &salt, const QByteArray &info, int len)
{
    const QByteArray prk = QMessageAuthenticationCode::hash(key, salt, QCryptographicHash::Sha1);   // extract
    QByteArray out, t;
    for (char i = 1; out.size() < len; ++i)                                                          // expand
        out += (t = QMessageAuthenticationCode::hash(t + info + QByteArray(1, i), prk, QCryptographicHash::Sha1));
    return out.left(len);
}

sw_socket_t open(const ProxyConfig &server, const QString &targetHost, int targetPort,
                 int timeoutMs, bool waitForData, QString *error)
{
    const int keyLen = keyLenOf(server.method);
    if (!keyLen) {
        if (error)
            *error = QObject::tr("Shadowsocks: the cipher \"%1\" is not supported (use %2).")
                         .arg(server.method, methods().join(QStringLiteral(", ")));
        return kInvalidSocket;
    }
    QString err;
    const sw_socket_t fd = proxytunnel::connectDirect(server.host, server.port, timeoutMs, &err);
    if (fd == kInvalidSocket) {
        if (error)
            *error = QObject::tr("Shadowsocks server %1:%2: %3").arg(server.host).arg(server.port).arg(err);
        return kInvalidSocket;
    }
    QByteArray pw = server.password.toUtf8();
    auto session = std::make_shared<Session>(native_socket(fd), server.method, masterKey(pw, keyLen));
    secure::wipe(pw);
    if (!session->start(targetHost, targetPort)) {
        if (error)
            *error = QObject::tr("Shadowsocks: could not send the request.");
        return kInvalidSocket;
    }

    // An SSH server greets first: getting its banner proves the password and cipher.
    QByteArray first;
    if (waitForData) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        char buf[4096];
        while (first.isEmpty()) {
            const int left = int(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count());
            bool ready = false, unused = false;
            if (left <= 0 || !waitReadable(native_socket(fd), native_socket(kInvalidSocket), left, &ready, &unused) || !ready) {
                if (error)
                    *error = QObject::tr("Shadowsocks: no answer from %1:%2 through the server — wrong password or cipher, or the server cannot reach it.").arg(targetHost).arg(targetPort);
                return kInvalidSocket;
            }
            const int n = int(::recv(native_socket(fd), buf, sizeof buf, 0));
            if (n <= 0) {
                if (error)
                    *error = QObject::tr("Shadowsocks: the server closed the connection — wrong password or cipher, or it cannot reach %1:%2.")
                                 .arg(targetHost).arg(targetPort);
                return kInvalidSocket;
            }
            if (!session->receive(buf, n, &first)) {
                if (error)
                    *error = QObject::tr("Shadowsocks: the answer could not be decrypted — wrong password or cipher.");
                return kInvalidSocket;
            }
        }
    }

    sw_socket_t a = kInvalidSocket, b = kInvalidSocket;
    if (!proxytunnel::loopbackPair(&a, &b, &err)) {
        if (error)
            *error = err;
        return kInvalidSocket;
    }
    std::thread(relay, session, native_socket(b), first).detach();
    return a;
}

Link parseLink(const QString &uriIn)
{
    Link l;
    QString uri = uriIn.trimmed();
    if (!uri.startsWith(QLatin1String("ss://"), Qt::CaseInsensitive)) {
        l.error = QObject::tr("Not an ss:// link.");
        return l;
    }
    uri = uri.mid(5);
    const int hash = uri.indexOf(QLatin1Char('#'));
    if (hash >= 0) {
        l.name = QUrl::fromPercentEncoding(uri.mid(hash + 1).toUtf8());
        uri = uri.left(hash);
    }
    const int q = uri.indexOf(QLatin1Char('?'));
    QString query;
    if (q >= 0) {
        query = uri.mid(q + 1);
        uri = uri.left(q);
    }
    if (uri.endsWith(QLatin1Char('/')))
        uri.chop(1);
    auto b64 = [](const QString &s) {
        return QByteArray::fromBase64(s.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    };
    QString userinfo, hostport;
    const int at = uri.lastIndexOf(QLatin1Char('@'));
    if (at >= 0) {                 // SIP002: base64url(method:password)@host:port
        userinfo = QUrl::fromPercentEncoding(uri.left(at).toUtf8());
        if (!userinfo.contains(QLatin1Char(':')))
            userinfo = QString::fromUtf8(b64(userinfo));
        hostport = uri.mid(at + 1);
    } else {                       // old form: base64(method:password@host:port)
        const QString all = QString::fromUtf8(b64(uri));
        const int at2 = all.lastIndexOf(QLatin1Char('@'));
        if (at2 < 0) {
            l.error = QObject::tr("The ss:// link could not be read.");
            return l;
        }
        userinfo = all.left(at2);
        hostport = all.mid(at2 + 1);
    }
    const int colon = userinfo.indexOf(QLatin1Char(':'));
    const int pc = hostport.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0 || pc <= 0) {
        l.error = QObject::tr("The ss:// link could not be read.");
        return l;
    }
    l.method = userinfo.left(colon).toLower();
    l.password = userinfo.mid(colon + 1);
    l.host = hostport.left(pc);
    if (l.host.startsWith(QLatin1Char('[')) && l.host.endsWith(QLatin1Char(']')))
        l.host = l.host.mid(1, l.host.size() - 2);
    l.port = hostport.mid(pc + 1).toInt();
    if (QUrlQuery(query).hasQueryItem(QStringLiteral("plugin"))) {
        l.error = QObject::tr("This link needs a plugin (%1), which ShellWarden does not run.")
                      .arg(QUrlQuery(query).queryItemValue(QStringLiteral("plugin"), QUrl::FullyDecoded).section(QLatin1Char(';'), 0, 0));
        return l;
    }
    if (!isSupported(l.method)) {
        l.error = QObject::tr("The cipher \"%1\" is not supported (use %2).").arg(l.method, methods().join(QStringLiteral(", ")));
        return l;
    }
    l.ok = l.port > 0 && l.port < 65536 && !l.host.isEmpty();
    if (!l.ok)
        l.error = QObject::tr("The ss:// link could not be read.");
    return l;
}

} // namespace shadowsocks
