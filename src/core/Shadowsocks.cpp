#include "Shadowsocks.h"
#include "SecureBytes.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QMessageAuthenticationCode>
#include <QObject>
#include <QUrl>
#include <QUrlQuery>

#include <openssl/evp.h>
#include <sodium.h>

#include "blake3.h"

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

struct CipherInfo { const char *name; int keyLen; bool sip022; bool chacha; };
constexpr CipherInfo kCiphers[] = {
    // Shadowsocks 2022 (SIP022): a real key instead of a password, replay protection, timestamps
    {"2022-blake3-aes-256-gcm", 32, true, false},
    {"2022-blake3-aes-128-gcm", 16, true, false},
    {"2022-blake3-chacha20-poly1305", 32, true, true},
    // AEAD 2017 (SIP004)
    {"chacha20-ietf-poly1305", 32, false, true},
    {"aes-256-gcm", 32, false, false},
    {"aes-128-gcm", 16, false, false},
};
constexpr int kTagLen = 16;
constexpr int kNonceLen = 12;
constexpr int kMaxPayload = 0x3FFF;     // SIP004: payload length is at most 16383 (we never send more)
constexpr int kMaxPayload2022 = 0xFFFF; // SIP022 allows up to 65535
constexpr int kMaxTimeSkew = 30;        // seconds, SIP022

const CipherInfo *cipherOf(const QString &method)
{
    for (const CipherInfo &c : kCiphers)
        if (method == QLatin1String(c.name))
            return &c;
    return nullptr;
}

int keyLenOf(const QString &method)
{
    const CipherInfo *c = cipherOf(method);
    return c ? c->keyLen : 0;
}

QByteArray blake3DeriveKey(const char *context, const QByteArray &material, int len)
{
    blake3_hasher h;
    blake3_hasher_init_derive_key(&h, context);
    blake3_hasher_update(&h, material.constData(), size_t(material.size()));
    QByteArray out(len, Qt::Uninitialized);
    blake3_hasher_finalize(&h, reinterpret_cast<uint8_t *>(out.data()), size_t(len));
    return out;
}

// One direction of the stream: subkey + little-endian nonce counter.
class Aead
{
public:
    Aead(bool chacha, const QByteArray &subkey) : m_chacha(chacha), m_key(subkey)
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

    // empty result = encryption failed
    QByteArray seal(const char *data, int len)
    {
        QByteArray out(len + kTagLen, Qt::Uninitialized);
        bool ok = false;
        if (m_chacha) {
            unsigned long long outLen = 0;
            ok = crypto_aead_chacha20poly1305_ietf_encrypt(
                     reinterpret_cast<unsigned char *>(out.data()), &outLen, reinterpret_cast<const unsigned char *>(data),
                     size_t(len), nullptr, 0, nullptr, m_nonce, reinterpret_cast<const unsigned char *>(m_key.constData())) == 0;
        } else if (m_ctx) {
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
        } else if (m_ctx) {
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
    void next() { sodium_increment(m_nonce, kNonceLen); }   // little-endian counter, as SIP004/SIP022 want

    bool m_chacha;
    QByteArray m_key;
    unsigned char m_nonce[kNonceLen] = {};
    EVP_CIPHER_CTX *m_ctx = nullptr;
    const EVP_CIPHER *m_cipher = nullptr;
};

// SIP022 identity header: AES-ECB of one 16-byte block
QByteArray aesEncryptBlock(const QByteArray &key, const QByteArray &block)
{
    QByteArray out(32, Qt::Uninitialized);
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int n = 0, f = 0;
    const bool ok = ctx
        && EVP_EncryptInit_ex(ctx, key.size() == 32 ? EVP_aes_256_ecb() : EVP_aes_128_ecb(), nullptr,
                              reinterpret_cast<const unsigned char *>(key.constData()), nullptr) == 1
        && EVP_CIPHER_CTX_set_padding(ctx, 0) == 1
        && EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char *>(out.data()), &n,
                             reinterpret_cast<const unsigned char *>(block.constData()), 16) == 1
        && EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(out.data()) + n, &f) == 1;
    if (ctx)
        EVP_CIPHER_CTX_free(ctx);
    return ok && n + f == 16 ? out.left(16) : QByteArray();
}

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

void putU16(QByteArray &b, int v) { b.append(char((v >> 8) & 0xff)); b.append(char(v & 0xff)); }
void putU64(QByteArray &b, quint64 v) { for (int i = 7; i >= 0; --i) b.append(char((v >> (8 * i)) & 0xff)); }
quint64 getU64(const char *p) { quint64 v = 0; for (int i = 0; i < 8; ++i) v = (v << 8) | uchar(p[i]); return v; }

// The encrypted connection to the server: salts, both directions, chunk framing.
// SIP004 (AEAD 2017) and SIP022 (Shadowsocks 2022).
class Session
{
public:
    // keys: SIP004 — one key from the password; SIP022 — the PSKs (identity keys…, user key)
    Session(native_socket server, const CipherInfo &cipher, const QList<QByteArray> &keys)
        : m_server(server), m_cipher(cipher), m_keys(keys) {}
    ~Session()
    {
        for (QByteArray &k : m_keys)
            secure::wipe(k);
        if (m_server != native_socket(kInvalidSocket))
            SS_CLOSE(m_server);
    }

    const QByteArray &userKey() const { return m_keys.last(); }
    int keyLen() const { return m_cipher.keyLen; }
    QString error() const { return m_error; }

    QByteArray subkey(const QByteArray &salt) const
    {
        return m_cipher.sip022 ? blake3DeriveKey("shadowsocks 2022 session subkey", userKey() + salt, keyLen())
                               : hkdfSha1(userKey(), salt, "ss-subkey", keyLen());
    }

    // first message: our salt + the target address (SIP022: + identity headers, header chunks)
    bool start(const QString &host, int port)
    {
        QByteArray salt(keyLen(), Qt::Uninitialized);
        randombytes_buf(salt.data(), size_t(salt.size()));
        m_enc = std::make_unique<Aead>(m_cipher.chacha, subkey(salt));
        const QByteArray h = QUrl::toAce(host).isEmpty() ? host.toUtf8() : QUrl::toAce(host);
        if (h.isEmpty() || h.size() >= 256)
            return false;
        QByteArray addr;
        addr.append(char(0x03));               // ATYP: domain name, resolved by the server (no DNS leak)
        addr.append(char(h.size()));
        addr.append(h);
        putU16(addr, port);

        if (!m_cipher.sip022) {
            const QByteArray first = seal(addr);
            return !first.isEmpty() && sendAll(m_server, salt + first);
        }

        m_requestSalt = salt;
        QByteArray out = salt;
        // identity headers (several keys, aes only): each says which key comes next
        for (int i = 0; i + 1 < m_keys.size(); ++i) {
            const QByteArray idKey = blake3DeriveKey("shadowsocks 2022 identity subkey", m_keys.at(i) + salt, keyLen());
            QByteArray next(32, Qt::Uninitialized);
            blake3_hasher hh;
            blake3_hasher_init(&hh);
            blake3_hasher_update(&hh, m_keys.at(i + 1).constData(), size_t(m_keys.at(i + 1).size()));
            blake3_hasher_finalize(&hh, reinterpret_cast<uint8_t *>(next.data()), 32);
            const QByteArray eih = aesEncryptBlock(idKey, next.left(16));
            if (eih.isEmpty())
                return false;
            out += eih;
        }
        // variable-length header: address, padding (there is no initial payload: SSH waits for the server)
        QByteArray var = addr;
        const int pad = 1 + int(randombytes_uniform(900));
        putU16(var, pad);
        QByteArray padding(pad, Qt::Uninitialized);
        randombytes_buf(padding.data(), size_t(pad));
        var += padding;
        // fixed-length header: type 0 (client stream), timestamp, length of the variable header
        QByteArray fixed;
        fixed.append(char(0));
        putU64(fixed, quint64(QDateTime::currentSecsSinceEpoch()));
        putU16(fixed, int(var.size()));
        const QByteArray f = m_enc->seal(fixed.constData(), int(fixed.size()));
        const QByteArray v = m_enc->seal(var.constData(), int(var.size()));
        if (f.isEmpty() || v.isEmpty())
            return false;
        out += f;
        out += v;
        return sendAll(m_server, out);
    }

    // empty result = encryption failed (never sent half-encrypted)
    QByteArray seal(const QByteArray &plain)
    {
        QByteArray out;
        for (int off = 0; off < plain.size(); off += kMaxPayload) {
            const int n = std::min<int>(kMaxPayload, int(plain.size()) - off);
            const char len[2] = {char((n >> 8) & 0xff), char(n & 0xff)};
            const QByteArray l = m_enc->seal(len, 2), p = m_enc->seal(plain.constData() + off, n);
            if (l.isEmpty() || p.isEmpty())
                return {};
            out += l;
            out += p;
        }
        return out;
    }
    bool send(const char *data, int len)
    {
        const QByteArray c = seal(QByteArray::fromRawData(data, len));
        return !c.isEmpty() && sendAll(m_server, c);
    }

    // feed bytes from the server; returns false when they cannot be authenticated
    bool receive(const char *data, int len, QByteArray *plain)
    {
        m_in.append(data, len);
        if (!m_dec) {
            if (m_in.size() < keyLen())
                return true;
            m_dec = std::make_unique<Aead>(m_cipher.chacha, subkey(m_in.left(keyLen())));
            m_in.remove(0, keyLen());
            m_wantHeader = m_cipher.sip022;
        }
        for (;;) {
            if (m_wantHeader) {   // SIP022 response header: type 1, timestamp, our salt, first chunk length
                const int fixedLen = 1 + 8 + keyLen() + 2;
                if (m_in.size() < fixedLen + kTagLen)
                    return true;
                QByteArray hdr;
                if (!m_dec->open(m_in.constData(), fixedLen + kTagLen, &hdr)) {
                    m_error = QObject::tr("the answer could not be decrypted — wrong key or cipher.");
                    return false;
                }
                m_in.remove(0, fixedLen + kTagLen);
                const qint64 skew = qint64(getU64(hdr.constData() + 1)) - QDateTime::currentSecsSinceEpoch();
                if (uchar(hdr.at(0)) != 1) {
                    m_error = QObject::tr("the server's answer has a wrong type.");
                    return false;
                }
                if (skew > kMaxTimeSkew || skew < -kMaxTimeSkew) {
                    m_error = QObject::tr("the server's clock differs from this computer's by %1 s (at most %2 s are allowed).")
                                  .arg(skew).arg(kMaxTimeSkew);
                    return false;
                }
                if (m_requestSalt.size() != keyLen()
                    || sodium_memcmp(hdr.constData() + 9, m_requestSalt.constData(), size_t(keyLen())) != 0) {
                    m_error = QObject::tr("the server's answer does not belong to this connection.");
                    return false;
                }
                m_want = (uchar(hdr.at(9 + keyLen())) << 8) | uchar(hdr.at(10 + keyLen()));
                m_wantHeader = false;
            }
            if (m_want < 0) {   // waiting for the length chunk
                if (m_in.size() < 2 + kTagLen)
                    return true;
                QByteArray len;
                if (!m_dec->open(m_in.constData(), 2 + kTagLen, &len)) {
                    m_error = QObject::tr("the answer could not be decrypted — wrong password or cipher.");
                    return false;
                }
                m_in.remove(0, 2 + kTagLen);
                m_want = (uchar(len[0]) << 8) | uchar(len[1]);
                if (!m_cipher.sip022)
                    m_want &= kMaxPayload;
            }
            if (m_want > kMaxPayload2022)
                return false;
            if (m_in.size() < m_want + kTagLen)
                return true;
            QByteArray chunk;
            if (!m_dec->open(m_in.constData(), m_want + kTagLen, &chunk)) {
                m_error = QObject::tr("the answer could not be decrypted — wrong password or cipher.");
                return false;
            }
            m_in.remove(0, m_want + kTagLen);
            m_want = -1;
            plain->append(chunk);
        }
    }

    native_socket server() const { return m_server; }
    void setRequestSalt(const QByteArray &salt) { m_requestSalt = salt; }   // for fuzzing only

private:
    native_socket m_server;
    const CipherInfo &m_cipher;
    QList<QByteArray> m_keys;
    QByteArray m_requestSalt;
    std::unique_ptr<Aead> m_enc, m_dec;
    QByteArray m_in;
    int m_want = -1;
    bool m_wantHeader = false;
    QString m_error;
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
                break;   // libssh closed its end: the session is over, so is this thread
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

QByteArray decodeServerBytesForFuzzing(const QString &method, const QString &password, const QByteArray &bytes, bool *ok)
{
    const CipherInfo *cipher = cipherOf(method);
    *ok = false;
    if (!cipher || !keyProblem(method, password).isEmpty())
        return {};
    QList<QByteArray> keys;
    if (cipher->sip022) {
        for (const QString &k : password.split(QLatin1Char(':')))
            keys << QByteArray::fromBase64(k.toLatin1());
    } else {
        keys << masterKey(password.toUtf8(), cipher->keyLen);
    }
    Session s(native_socket(kInvalidSocket), *cipher, keys);
    s.setRequestSalt(QByteArray(cipher->keyLen, '\x42'));
    QByteArray plain;
    // in pieces of different sizes, as they arrive from a socket
    int off = 0, step = 1;
    *ok = true;
    while (off < bytes.size() && *ok) {
        const int n = std::min<int>(step, int(bytes.size()) - off);
        *ok = s.receive(bytes.constData() + off, n, &plain);
        off += n;
        step = step * 3 % 4093 + 1;
    }
    return plain;
}

QString keyProblem(const QString &method, const QString &password)
{
    const CipherInfo *c = cipherOf(method);
    if (!c)
        return QObject::tr("The cipher \"%1\" is not supported (use %2).").arg(method, methods().join(QStringLiteral(", ")));
    if (password.isEmpty())
        return QObject::tr("Enter the Shadowsocks password.");
    if (!c->sip022)
        return {};
    const QStringList parts = password.split(QLatin1Char(':'));
    if (parts.size() > 1 && c->chacha)
        return QObject::tr("Several keys (key:key) work only with the 2022-blake3-aes ciphers.");
    for (const QString &k : parts) {
        const QByteArray raw = QByteArray::fromBase64(k.trimmed().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (raw.size() != c->keyLen)
            return QObject::tr("For %1 the password is a key: %2 random bytes in base64 (e.g. from \"openssl rand -base64 %2\"), "
                               "as written in the server's config.").arg(method).arg(c->keyLen);
    }
    return {};
}

sw_socket_t open(const ProxyConfig &server, const QString &targetHost, int targetPort,
                 int timeoutMs, bool waitForData, QString *error)
{
    const CipherInfo *cipher = cipherOf(server.method);
    if (const QString problem = keyProblem(server.method, server.password); !problem.isEmpty()) {
        if (error)
            *error = QObject::tr("Shadowsocks: %1").arg(problem);
        return kInvalidSocket;
    }
    QList<QByteArray> keys;
    if (cipher->sip022) {
        for (const QString &k : server.password.split(QLatin1Char(':')))
            keys << QByteArray::fromBase64(k.trimmed().toLatin1());
    } else {
        QByteArray pw = server.password.toUtf8();
        keys << masterKey(pw, cipher->keyLen);
        secure::wipe(pw);
    }
    QString err;
    const sw_socket_t fd = proxytunnel::connectDirect(server.host, server.port, timeoutMs, &err);
    if (fd == kInvalidSocket) {
        if (error)
            *error = QObject::tr("Shadowsocks server %1:%2: %3").arg(server.host).arg(server.port).arg(err);
        return kInvalidSocket;
    }
    auto session = std::make_shared<Session>(native_socket(fd), *cipher, keys);
    for (QByteArray &k : keys)
        secure::wipe(k);
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
                    *error = cipher->sip022
                        ? QObject::tr("Shadowsocks: the server closed the connection — wrong key or cipher, this computer's clock is off by more than 30 s, or it cannot reach %1:%2.")
                              .arg(targetHost).arg(targetPort)
                        : QObject::tr("Shadowsocks: the server closed the connection — wrong password or cipher, or it cannot reach %1:%2.")
                              .arg(targetHost).arg(targetPort);
                return kInvalidSocket;
            }
            if (!session->receive(buf, n, &first)) {
                if (error)
                    *error = QObject::tr("Shadowsocks: %1").arg(session->error().isEmpty()
                                 ? QObject::tr("the answer could not be decrypted — wrong password or cipher.") : session->error());
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
