#include "Vault.h"
#include "SecureBytes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QtEndian>

#include <cstring>
#include <sodium.h>

namespace {

constexpr char kMagic[8] = {'S', 'W', 'V', 'A', 'U', 'L', 'T', '1'};
constexpr int kSaltLen = crypto_pwhash_SALTBYTES;                       // 16
constexpr int kNonceLen = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES; // 24
constexpr int kKeyLen = crypto_aead_xchacha20poly1305_ietf_KEYBYTES;    // 32
constexpr int kHeaderLen = 8 + 8 + 8 + kSaltLen + kNonceLen;

static_assert(kSaltLen == 16, "unexpected salt size");

void putU64(QByteArray &out, quint64 v)
{
    char buf[8];
    qToLittleEndian<quint64>(v, buf);
    out.append(buf, 8);
}

quint64 getU64(const char *p)
{
    return qFromLittleEndian<quint64>(p);
}

QJsonObject emptyData()
{
    QJsonObject o;
    o.insert(QStringLiteral("version"), 1);
    o.insert(QStringLiteral("hosts"), QJsonArray());
    o.insert(QStringLiteral("keys"), QJsonArray());
    return o;
}

} // namespace

Vault::Vault(QString path) : m_path(std::move(path)) {}

Vault::~Vault() { lock(); }

bool Vault::exists() const { return QFileInfo::exists(m_path); }

bool Vault::deriveKey(const QString &password, const unsigned char *salt,
                      unsigned long long ops, size_t mem, unsigned char *out,
                      QString *error) const
{
    QByteArray pw = password.toUtf8();
    secure::ByteGuard guard(pw);
    if (crypto_pwhash(out, kKeyLen, pw.constData(), static_cast<unsigned long long>(pw.size()),
                      salt, ops, mem, crypto_pwhash_ALG_ARGON2ID13) != 0) {
        if (error)
            *error = QObject::tr("Not enough memory to derive the vault key.");
        return false;
    }
    return true;
}

bool Vault::create(const QString &password, QString *error)
{
    if (password.size() < 8) {
        if (error)
            *error = QObject::tr("The master password must be at least 8 characters.");
        return false;
    }
    lock();

    m_ops = crypto_pwhash_OPSLIMIT_MODERATE;
    m_mem = crypto_pwhash_MEMLIMIT_MODERATE;
    randombytes_buf(m_salt, sizeof m_salt);

    m_key = static_cast<unsigned char *>(sodium_malloc(kKeyLen));
    if (!m_key || !deriveKey(password, m_salt, m_ops, m_mem, m_key, error)) {
        lock();
        return false;
    }

    m_data = emptyData();
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    if (!writeFile(error)) {
        lock();
        return false;
    }
    return true;
}

bool Vault::unlock(const QString &password, QString *error)
{
    lock();

    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Cannot open vault: %1").arg(f.errorString());
        return false;
    }
    const QByteArray blob = f.readAll();
    f.close();

    if (blob.size() < kHeaderLen + int(crypto_aead_xchacha20poly1305_ietf_ABYTES)
        || std::memcmp(blob.constData(), kMagic, 8) != 0) {
        if (error)
            *error = QObject::tr("The vault file is damaged or has an unknown format.");
        return false;
    }

    const char *p = blob.constData() + 8;
    const quint64 ops = getU64(p);
    const quint64 mem = getU64(p + 8);
    const auto *salt = reinterpret_cast<const unsigned char *>(p + 16);
    const auto *nonce = reinterpret_cast<const unsigned char *>(p + 16 + kSaltLen);

    // Refuse absurd parameters so a tampered file can't make us allocate GBs.
    if (ops < crypto_pwhash_OPSLIMIT_MIN || ops > 64
        || mem < crypto_pwhash_MEMLIMIT_MIN || mem > (quint64(1) << 30)) {
        if (error)
            *error = QObject::tr("The vault file has invalid key-derivation parameters.");
        return false;
    }

    m_key = static_cast<unsigned char *>(sodium_malloc(kKeyLen));
    if (!m_key || !deriveKey(password, salt, ops, size_t(mem), m_key, error)) {
        lock();
        return false;
    }

    const auto *cipher = reinterpret_cast<const unsigned char *>(blob.constData() + kHeaderLen);
    const auto cipherLen = static_cast<unsigned long long>(blob.size() - kHeaderLen);
    QByteArray plain(int(cipherLen - crypto_aead_xchacha20poly1305_ietf_ABYTES), Qt::Uninitialized);
    secure::ByteGuard plainGuard(plain);

    unsigned long long plainLen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            reinterpret_cast<unsigned char *>(plain.data()), &plainLen, nullptr,
            cipher, cipherLen,
            reinterpret_cast<const unsigned char *>(blob.constData()), kHeaderLen,
            nonce, m_key) != 0) {
        if (error)
            *error = QObject::tr("Wrong master password.");
        lock();
        return false;
    }

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(plain.left(int(plainLen)), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QObject::tr("The vault content is corrupted.");
        lock();
        return false;
    }

    m_data = doc.object();
    m_ops = ops;
    m_mem = size_t(mem);
    std::memcpy(m_salt, salt, kSaltLen);
    return true;
}

bool Vault::changePassword(const QString &oldPassword, const QString &newPassword, QString *error)
{
    if (newPassword.size() < 8) {
        if (error)
            *error = QObject::tr("The master password must be at least 8 characters.");
        return false;
    }
    // Re-verify the current password by decrypting the file again.
    const QJsonObject current = m_data;
    if (!unlock(oldPassword, error))
        return false;
    m_data = current; // keep unsaved in-memory edits

    unsigned char newSalt[kSaltLen];
    randombytes_buf(newSalt, sizeof newSalt);
    auto *newKey = static_cast<unsigned char *>(sodium_malloc(kKeyLen));
    if (!newKey || !deriveKey(newPassword, newSalt, m_ops, m_mem, newKey, error)) {
        if (newKey)
            sodium_free(newKey);
        return false;
    }
    sodium_free(m_key);
    m_key = newKey;
    std::memcpy(m_salt, newSalt, kSaltLen);
    return writeFile(error);
}

bool Vault::deriveWithVaultCost(const QString &password, const QByteArray &salt16, unsigned char *out32,
                                QString *error) const
{
    if (!m_key || salt16.size() != kSaltLen || kKeyLen != 32) {
        if (error)
            *error = QObject::tr("The vault is locked.");
        return false;
    }
    return deriveKey(password, reinterpret_cast<const unsigned char *>(salt16.constData()), m_ops, m_mem, out32, error);
}

bool Vault::verifyPassword(const QString &password) const
{
    if (!m_key)
        return false;
    auto *k = static_cast<unsigned char *>(sodium_malloc(kKeyLen));
    if (!k)
        return false;
    QString err;
    const bool ok = deriveKey(password, m_salt, m_ops, m_mem, k, &err) && sodium_memcmp(k, m_key, kKeyLen) == 0;
    sodium_free(k);
    return ok;
}

void Vault::lock()
{
    if (m_key) {
        sodium_free(m_key); // sodium_free zeroes the buffer before releasing it
        m_key = nullptr;
    }
    m_data = QJsonObject();
}

bool Vault::save(QString *error)
{
    if (!m_key) {
        if (error)
            *error = QObject::tr("The vault is locked.");
        return false;
    }
    return writeFile(error);
}

bool Vault::writeFile(QString *error)
{
    QByteArray header;
    header.reserve(kHeaderLen);
    header.append(kMagic, 8);
    putU64(header, m_ops);
    putU64(header, quint64(m_mem));
    header.append(reinterpret_cast<const char *>(m_salt), kSaltLen);

    unsigned char nonce[kNonceLen];
    randombytes_buf(nonce, sizeof nonce); // fresh nonce on every save
    header.append(reinterpret_cast<const char *>(nonce), kNonceLen);

    QByteArray plain = QJsonDocument(m_data).toJson(QJsonDocument::Compact);
    secure::ByteGuard plainGuard(plain);

    QByteArray cipher(plain.size() + int(crypto_aead_xchacha20poly1305_ietf_ABYTES), Qt::Uninitialized);
    unsigned long long cipherLen = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(
        reinterpret_cast<unsigned char *>(cipher.data()), &cipherLen,
        reinterpret_cast<const unsigned char *>(plain.constData()),
        static_cast<unsigned long long>(plain.size()),
        reinterpret_cast<const unsigned char *>(header.constData()), header.size(),
        nullptr, nonce, m_key);
    cipher.resize(int(cipherLen));

    // Keep the previous version as a backup, then replace atomically. The backup
    // itself is replaced atomically too: there is never a moment without one.
    if (QFileInfo::exists(m_path)) {
        QFile cur(m_path);
        if (cur.open(QIODevice::ReadOnly)) {
            const QByteArray previous = cur.readAll();
            cur.close();
            QSaveFile bak(m_path + QStringLiteral(".bak"));
            if (bak.open(QIODevice::WriteOnly)) {
                bak.write(previous);
                bak.commit();
            }
        }
    }

    QSaveFile out(m_path);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QObject::tr("Cannot write vault: %1").arg(out.errorString());
        return false;
    }
    out.write(header);
    out.write(cipher);
    if (!out.commit()) {
        if (error)
            *error = QObject::tr("Cannot write vault: %1").arg(out.errorString());
        return false;
    }
    return true;
}
