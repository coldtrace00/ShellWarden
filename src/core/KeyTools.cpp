#include "KeyTools.h"
#include "SecureBytes.h"

#include <QFile>
#include <QMap>
#include <QSaveFile>
#include <QStringList>

#include <libssh/libssh.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <sodium.h>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <aclapi.h>
#  include <string>
#endif

namespace keytools {

namespace {

struct KeyHolder
{
    ssh_key key = nullptr;
    ~KeyHolder() { if (key) ssh_key_free(key); }
};

struct CStr
{
    char *p = nullptr;
    ~CStr() { if (p) ssh_string_free_char(p); }
};

QString publicLineOf(ssh_key pub, const QString &comment)
{
    CStr b64;
    if (ssh_pki_export_pubkey_base64(pub, &b64.p) != SSH_OK || !b64.p)
        return {};
    QString line = QString::fromLatin1(ssh_key_type_to_char(ssh_key_type(pub)))
                   + QLatin1Char(' ') + QString::fromLatin1(b64.p);
    const QString c = comment.simplified(); // a comment must stay on one line
    if (!c.isEmpty())
        line += QLatin1Char(' ') + c;
    return line;
}

QString fingerprintOf(ssh_key pub)
{
    unsigned char *hash = nullptr;
    size_t len = 0;
    if (ssh_get_publickey_hash(pub, SSH_PUBLICKEY_HASH_SHA256, &hash, &len) != 0)
        return {};
    char *fp = ssh_get_fingerprint_hash(SSH_PUBLICKEY_HASH_SHA256, hash, len);
    ssh_clean_pubkey_hash(&hash);
    QString out = fp ? QString::fromLatin1(fp) : QString();
    if (fp)
        ssh_string_free_char(fp);
    return out;
}

bool fillFromPrivate(ssh_key priv, const QString &comment, KeyMaterial *out)
{
    KeyHolder pub;
    if (ssh_pki_export_privkey_to_pubkey(priv, &pub.key) != SSH_OK)
        return false;
    out->type = QString::fromLatin1(ssh_key_type_to_char(ssh_key_type(pub.key)));
    out->publicLine = publicLineOf(pub.key, comment);
    out->fingerprint = fingerprintOf(pub.key);
    return !out->publicLine.isEmpty();
}

QByteArray normalizeNewlines(QByteArray text)
{
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    while (text.endsWith('\n'))
        text.chop(1);
    text.append('\n');
    return text;
}

} // namespace

bool generate(KeyType type, const QString &comment, const QString &passphrase,
              KeyMaterial *out, QString *error)
{
    ssh_keytypes_e kt = SSH_KEYTYPE_ED25519;
    int bits = 0;
    switch (type) {
    case KeyType::Ed25519:   kt = SSH_KEYTYPE_ED25519; break;
    case KeyType::EcdsaP256: kt = SSH_KEYTYPE_ECDSA_P256; bits = 256; break;
    case KeyType::EcdsaP384: kt = SSH_KEYTYPE_ECDSA_P384; bits = 384; break;
    case KeyType::EcdsaP521: kt = SSH_KEYTYPE_ECDSA_P521; bits = 521; break;
    case KeyType::Rsa3072:   kt = SSH_KEYTYPE_RSA; bits = 3072; break;
    case KeyType::Rsa4096:   kt = SSH_KEYTYPE_RSA; bits = 4096; break;
    }

    KeyHolder priv;
    if (ssh_pki_generate(kt, bits, &priv.key) != SSH_OK) {
        if (error)
            *error = QObject::tr("Key generation failed.");
        return false;
    }

    QByteArray pass = passphrase.toUtf8();
    secure::ByteGuard passGuard(pass);
    CStr b64;
    int rc;
    // libssh >= 0.10 writes the OpenSSH "openssh-key-v1" container
    // (bcrypt_pbkdf + aes256-ctr when a passphrase is set).
    rc = ssh_pki_export_privkey_base64(priv.key, pass.isEmpty() ? nullptr : pass.constData(),
                                       nullptr, nullptr, &b64.p);
    if (rc != SSH_OK || !b64.p) {
        if (error)
            *error = QObject::tr("Cannot export the private key.");
        return false;
    }

    out->privateKey = normalizeNewlines(QByteArray(b64.p));
    sodium_memzero(b64.p, strlen(b64.p));
    out->encrypted = !pass.isEmpty();
    if (!fillFromPrivate(priv.key, comment, out)) {
        if (error)
            *error = QObject::tr("Cannot derive the public key.");
        return false;
    }
    return true;
}

ImportResult importPrivateKey(const QByteArray &text, const QString &passphrase,
                              const QString &comment, KeyMaterial *out, QString *error)
{
    const QByteArray norm = normalizeNewlines(text.trimmed());
    if (isPpk(norm))
        return importPpk(norm, passphrase, comment, out, error);

    const bool encrypted = isEncrypted(norm);
    if (encrypted && passphrase.isEmpty())
        return ImportResult::NeedPassphrase;

    QByteArray pass = passphrase.toUtf8();
    secure::ByteGuard passGuard(pass);
    KeyHolder priv;
    if (ssh_pki_import_privkey_base64(norm.constData(), pass.isEmpty() ? nullptr : pass.constData(),
                                      nullptr, nullptr, &priv.key) != SSH_OK) {
        if (encrypted)
            return ImportResult::WrongPassphrase;
        if (error)
            *error = QObject::tr("Unsupported or damaged private key.");
        return ImportResult::Invalid;
    }

    out->privateKey = norm;
    out->encrypted = encrypted;
    if (!fillFromPrivate(priv.key, comment, out)) {
        if (error)
            *error = QObject::tr("Cannot derive the public key.");
        return ImportResult::Invalid;
    }
    return ImportResult::Ok;
}

// ---------------------------------------------------------------------------
// PuTTY .ppk
// ---------------------------------------------------------------------------

namespace {

struct Reader
{
    QByteArray data;
    int pos = 0;
    bool ok = true;
    quint32 u32()
    {
        if (pos + 4 > data.size()) { ok = false; return 0; }
        const auto *p = reinterpret_cast<const uchar *>(data.constData() + pos);
        pos += 4;
        return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | p[3];
    }
    QByteArray str()
    {
        const quint32 n = u32();
        if (!ok || pos + int(n) > data.size() || n > (1u << 24)) { ok = false; return {}; }
        QByteArray r = data.mid(pos, int(n));
        pos += int(n);
        return r;
    }
};

void putU32(QByteArray &b, quint32 v)
{
    b.append(char(v >> 24)).append(char(v >> 16)).append(char(v >> 8)).append(char(v));
}
void putStr(QByteArray &b, const QByteArray &s)
{
    putU32(b, quint32(s.size()));
    b.append(s);
}

QByteArray sha1(const QByteArray &d)
{
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_Digest(d.constData(), size_t(d.size()), out, &len, EVP_sha1(), nullptr);
    return QByteArray(reinterpret_cast<char *>(out), int(len));
}

QByteArray hmac(const EVP_MD *md, const QByteArray &key, const QByteArray &data)
{
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(md, key.constData(), int(key.size()), reinterpret_cast<const unsigned char *>(data.constData()),
         size_t(data.size()), out, &len);
    return QByteArray(reinterpret_cast<char *>(out), int(len));
}

bool aes256cbcDecrypt(const QByteArray &key, const QByteArray &iv, QByteArray &data)
{
    if (data.size() % 16 != 0)
        return false;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    bool ok = ctx
              && EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                                    reinterpret_cast<const unsigned char *>(key.constData()),
                                    reinterpret_cast<const unsigned char *>(iv.constData())) == 1
              && EVP_CIPHER_CTX_set_padding(ctx, 0) == 1;
    QByteArray out(data.size() + 16, 0);
    int len1 = 0, len2 = 0;
    ok = ok && EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char *>(out.data()), &len1,
                                 reinterpret_cast<const unsigned char *>(data.constData()), int(data.size())) == 1
         && EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(out.data()) + len1, &len2) == 1;
    if (ctx)
        EVP_CIPHER_CTX_free(ctx);
    if (ok) {
        secure::wipe(data);
        data = out.left(len1 + len2);
    }
    secure::wipe(out);
    return ok;
}

// openssh-key-v1 container, unencrypted, from the SSH wire-format fields.
QByteArray buildOpenSshKey(const QByteArray &pubBlob, const QByteArray &keyFields, const QString &comment)
{
    QByteArray priv;
    quint32 check = randombytes_random();
    putU32(priv, check);
    putU32(priv, check);
    priv.append(keyFields);
    putStr(priv, comment.toUtf8());
    for (char pad = 1; priv.size() % 8 != 0; ++pad)
        priv.append(pad);

    QByteArray blob("openssh-key-v1", 15); // includes the trailing NUL
    blob[14] = '\0';
    putStr(blob, "none");
    putStr(blob, "none");
    putStr(blob, QByteArray());
    putU32(blob, 1);
    putStr(blob, pubBlob);
    putStr(blob, priv);
    secure::wipe(priv);

    QByteArray b64 = blob.toBase64();
    secure::wipe(blob);
    QByteArray text = "-----BEGIN OPENSSH PRIVATE KEY-----\n";
    for (int i = 0; i < b64.size(); i += 70)
        text += b64.mid(i, 70) + '\n';
    text += "-----END OPENSSH PRIVATE KEY-----\n";
    secure::wipe(b64);
    return text;
}

} // namespace

// RSA from PuTTY fields → PKCS#8 PEM (AES-256-CBC when a passphrase is given).
// Used for RSA because libssh (OpenSSL 3 backend) cannot re-export an RSA key
// that was loaded from the OpenSSH container.
#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
QByteArray rsaToPkcs8(const QByteArray &n, const QByteArray &e, const QByteArray &d, const QByteArray &p,
                      const QByteArray &q, const QByteArray &iqmp, const QByteArray &pass)
{
    auto bn = [](const QByteArray &b) {
        return BN_bin2bn(reinterpret_cast<const unsigned char *>(b.constData()), int(b.size()), nullptr);
    };
    BIGNUM *N = bn(n), *E = bn(e), *D = bn(d), *P = bn(p), *Q = bn(q), *IQ = bn(iqmp);
    BIGNUM *DP = BN_new(), *DQ = BN_new(), *P1 = BN_new(), *Q1 = BN_new();
    BN_CTX *ctx = BN_CTX_new();
    BN_sub(P1, P, BN_value_one());
    BN_sub(Q1, Q, BN_value_one());
    BN_mod(DP, D, P1, ctx);
    BN_mod(DQ, D, Q1, ctx);
    BN_CTX_free(ctx);
    BN_clear_free(P1);
    BN_clear_free(Q1);

    RSA *rsa = RSA_new();
    RSA_set0_key(rsa, N, E, D);
    RSA_set0_factors(rsa, P, Q);
    RSA_set0_crt_params(rsa, DP, DQ, IQ);
    EVP_PKEY *pkey = EVP_PKEY_new();
    EVP_PKEY_assign_RSA(pkey, rsa);

    BIO *bio = BIO_new(BIO_s_mem());
    int ok = pass.isEmpty()
                 ? PEM_write_bio_PKCS8PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr)
                 : PEM_write_bio_PKCS8PrivateKey(bio, pkey, EVP_aes_256_cbc(), const_cast<char *>(pass.constData()),
                                                 int(pass.size()), nullptr, nullptr);
    QByteArray out;
    if (ok == 1) {
        char *data = nullptr;
        const long len = BIO_get_mem_data(bio, &data);
        out = QByteArray(data, int(len));
    }
    BIO_free(bio);
    EVP_PKEY_free(pkey);
    return out;
}
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

bool isPpk(const QByteArray &text)
{
    return text.trimmed().startsWith("PuTTY-User-Key-File-");
}

ImportResult importPpk(const QByteArray &text, const QString &passphrase, const QString &commentOverride,
                       KeyMaterial *out, QString *error)
{
    QMap<QByteArray, QByteArray> h;
    QByteArray pubB64, privB64;
    const QList<QByteArray> lines = normalizeNewlines(text).split('\n');
    int version = 0;
    QByteArray algo;
    for (int i = 0; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        const QByteArray key = line.left(colon).trimmed();
        const QByteArray value = line.mid(colon + 1).trimmed();
        if (key.startsWith("PuTTY-User-Key-File-")) {
            version = key.mid(20).toInt();
            algo = value;
        } else if (key == "Public-Lines" || key == "Private-Lines") {
            const int n = value.toInt();
            QByteArray &dst = key == "Public-Lines" ? pubB64 : privB64;
            for (int j = 0; j < n && i + 1 < lines.size(); ++j)
                dst += lines.at(++i).trimmed();
        } else {
            h.insert(key, value);
        }
    }
    if ((version != 2 && version != 3) || algo.isEmpty() || pubB64.isEmpty() || privB64.isEmpty()) {
        *error = QObject::tr("Unsupported or damaged PuTTY key file.");
        return ImportResult::Invalid;
    }
    const QByteArray enc = h.value("Encryption");
    const QByteArray comment = h.value("Comment");
    const QByteArray pub = QByteArray::fromBase64(pubB64);
    QByteArray priv = QByteArray::fromBase64(privB64);
    secure::ByteGuard privGuard(priv);
    const bool encrypted = enc != "none";
    if (encrypted && enc != "aes256-cbc") {
        *error = QObject::tr("Unsupported PuTTY key encryption: %1").arg(QString::fromLatin1(enc));
        return ImportResult::Invalid;
    }
    if (encrypted && passphrase.isEmpty())
        return ImportResult::NeedPassphrase;

    QByteArray pass = encrypted ? passphrase.toUtf8() : QByteArray();
    secure::ByteGuard passGuard(pass);
    QByteArray macKey;
    secure::ByteGuard macGuard(macKey);

    if (version == 3) {
        if (encrypted) {
            const QByteArray kdf = h.value("Key-Derivation");
            int alg = 0;
            if (kdf == "Argon2id") alg = crypto_pwhash_ALG_ARGON2ID13;
            else if (kdf == "Argon2i") alg = crypto_pwhash_ALG_ARGON2I13;
            else {
                *error = QObject::tr("This PuTTY key uses %1, which is not supported. Re-save it in PuTTYgen with Argon2id.")
                             .arg(QString::fromLatin1(kdf));
                return ImportResult::Invalid;
            }
            const unsigned long long mem = h.value("Argon2-Memory").toULongLong() * 1024ull;
            const unsigned long long passes = h.value("Argon2-Passes").toULongLong();
            const int par = h.value("Argon2-Parallelism").toInt();
            const QByteArray salt = QByteArray::fromHex(h.value("Argon2-Salt"));
            if (par != 1 || salt.size() != crypto_pwhash_SALTBYTES || mem == 0 || passes == 0 || mem > (1ull << 31)) {
                *error = QObject::tr("This PuTTY key uses Argon2 parameters that are not supported (parallelism must be 1).");
                return ImportResult::Invalid;
            }
            QByteArray k(80, 0);
            if (crypto_pwhash(reinterpret_cast<unsigned char *>(k.data()), 80, pass.constData(),
                              (unsigned long long)pass.size(), reinterpret_cast<const unsigned char *>(salt.constData()),
                              passes, size_t(mem), alg) != 0) {
                *error = QObject::tr("Not enough memory to derive the key.");
                return ImportResult::Invalid;
            }
            if (!aes256cbcDecrypt(k.left(32), k.mid(32, 16), priv)) {
                secure::wipe(k);
                return ImportResult::WrongPassphrase;
            }
            macKey = k.mid(48, 32);
            secure::wipe(k);
        }
    } else { // version 2
        if (encrypted) {
            QByteArray seq0(4, 0), seq1(4, 0);
            seq1[3] = 1;
            QByteArray key = sha1(seq0 + pass) + sha1(seq1 + pass);
            key.truncate(32);
            if (!aes256cbcDecrypt(key, QByteArray(16, 0), priv)) {
                secure::wipe(key);
                return ImportResult::WrongPassphrase;
            }
            secure::wipe(key);
        }
        macKey = sha1(QByteArray("putty-private-key-file-mac-key") + pass);
    }

    QByteArray macData;
    putStr(macData, algo);
    putStr(macData, enc);
    putStr(macData, comment);
    putStr(macData, pub);
    putStr(macData, priv);
    const QByteArray mac = hmac(version == 3 ? EVP_sha256() : EVP_sha1(), macKey, macData);
    secure::wipe(macData);
    if (mac.toHex() != h.value("Private-MAC").toLower()) {
        if (encrypted)
            return ImportResult::WrongPassphrase;
        *error = QObject::tr("The PuTTY key file is damaged (MAC mismatch).");
        return ImportResult::Invalid;
    }

    // Wire-format fields for the OpenSSH container.
    Reader pr{pub};
    Reader sr{priv};
    const QByteArray type = pr.str();
    QByteArray fields;
    putStr(fields, type);
    if (type == "ssh-ed25519") {
        const QByteArray pk = pr.str();
        const QByteArray sk = sr.str();
        if (!pr.ok || !sr.ok || pk.size() != 32 || sk.size() != 32) {
            *error = QObject::tr("Unsupported or damaged PuTTY key file.");
            return ImportResult::Invalid;
        }
        putStr(fields, pk);
        QByteArray full = sk + pk;
        putStr(fields, full);
        secure::wipe(full);
    } else if (type == "ssh-rsa") {
        const QByteArray e = pr.str(), n = pr.str();
        QByteArray d = sr.str(), p = sr.str(), q = sr.str(), iqmp = sr.str();
        if (!pr.ok || !sr.ok) {
            *error = QObject::tr("Unsupported or damaged PuTTY key file.");
            return ImportResult::Invalid;
        }
        QByteArray pem = rsaToPkcs8(n, e, d, p, q, iqmp, pass);
        secure::wipe(d); secure::wipe(p); secure::wipe(q); secure::wipe(iqmp);
        if (pem.isEmpty()) {
            *error = QObject::tr("Could not convert the PuTTY key.");
            return ImportResult::Invalid;
        }
        KeyHolder key;
        if (ssh_pki_import_privkey_base64(pem.constData(), pass.isEmpty() ? nullptr : pass.constData(),
                                          nullptr, nullptr, &key.key) != SSH_OK) {
            secure::wipe(pem);
            *error = QObject::tr("Could not convert the PuTTY key.");
            return ImportResult::Invalid;
        }
        out->privateKey = normalizeNewlines(pem);
        secure::wipe(pem);
        out->encrypted = !pass.isEmpty();
        const QString cmt = commentOverride.trimmed().isEmpty() ? QString::fromUtf8(comment) : commentOverride.trimmed();
        if (!fillFromPrivate(key.key, cmt, out)) {
            *error = QObject::tr("Cannot derive the public key.");
            return ImportResult::Invalid;
        }
        return ImportResult::Ok;
    } else if (type.startsWith("ecdsa-sha2-")) {
        const QByteArray curve = pr.str(), point = pr.str();
        const QByteArray d = sr.str();
        if (!pr.ok || !sr.ok) {
            *error = QObject::tr("Unsupported or damaged PuTTY key file.");
            return ImportResult::Invalid;
        }
        putStr(fields, curve); putStr(fields, point); putStr(fields, d);
    } else {
        *error = QObject::tr("Unsupported PuTTY key type: %1").arg(QString::fromLatin1(type));
        return ImportResult::Invalid;
    }

    const QString cmt = commentOverride.trimmed().isEmpty() ? QString::fromUtf8(comment) : commentOverride.trimmed();
    QByteArray openssh = buildOpenSshKey(pub, fields, QString::fromUtf8(comment));
    secure::wipe(fields);
    secure::ByteGuard osshGuard(openssh);

    KeyHolder key;
    if (ssh_pki_import_privkey_base64(openssh.constData(), nullptr, nullptr, nullptr, &key.key) != SSH_OK) {
        *error = QObject::tr("Could not convert the PuTTY key.");
        return ImportResult::Invalid;
    }
    // Same protection as the original file: same passphrase, OpenSSH format.
    CStr b64;
    if (ssh_pki_export_privkey_base64(key.key, pass.isEmpty() ? nullptr : pass.constData(), nullptr, nullptr, &b64.p) != SSH_OK
        || !b64.p) {
        *error = QObject::tr("Cannot export the private key.");
        return ImportResult::Invalid;
    }
    out->privateKey = normalizeNewlines(QByteArray(b64.p));
    sodium_memzero(b64.p, strlen(b64.p));
    out->encrypted = !pass.isEmpty();
    if (!fillFromPrivate(key.key, cmt, out)) {
        *error = QObject::tr("Cannot derive the public key.");
        return ImportResult::Invalid;
    }
    return ImportResult::Ok;
}

bool isEncrypted(const QByteArray &text)
{
    if (isPpk(text))
        return !text.contains("Encryption: none");
    if (text.contains("BEGIN OPENSSH PRIVATE KEY")) {
        // openssh-key-v1\0 | string ciphername | …
        QByteArray body;
        bool inBody = false;
        for (const QByteArray &line : text.split('\n')) {
            if (line.startsWith("-----BEGIN")) { inBody = true; continue; }
            if (line.startsWith("-----END")) break;
            if (inBody) body += line.trimmed();
        }
        const QByteArray raw = QByteArray::fromBase64(body);
        static const QByteArray magic("openssh-key-v1\0", 15);
        if (!raw.startsWith(magic) || raw.size() < 19)
            return false;
        const auto *p = reinterpret_cast<const unsigned char *>(raw.constData() + 15);
        const quint32 len = (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | p[3];
        if (raw.size() < int(19 + len))
            return false;
        return raw.mid(19, int(len)) != "none";
    }
    // Traditional PEM ("Proc-Type: 4,ENCRYPTED") and PKCS#8 encrypted keys.
    return text.contains("ENCRYPTED");
}

bool checkPassphrase(const QByteArray &text, const QString &passphrase)
{
    QByteArray pass = passphrase.toUtf8();
    secure::ByteGuard passGuard(pass);
    KeyHolder priv;
    return ssh_pki_import_privkey_base64(text.constData(), pass.isEmpty() ? nullptr : pass.constData(),
                                         nullptr, nullptr, &priv.key) == SSH_OK;
}

QString toRfc4716(const QString &publicLine)
{
    const QStringList parts = publicLine.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return {};
    const QString blob = parts.at(1);
    const QString comment = parts.mid(2).join(QLatin1Char(' '));

    QString out = QStringLiteral("---- BEGIN SSH2 PUBLIC KEY ----\n");
    if (!comment.isEmpty()) {
        QString c = QStringLiteral("Comment: \"%1\"").arg(comment);
        // Header lines are limited to 72 bytes; continue with '\'.
        while (c.size() > 72) {
            out += c.left(71) + QLatin1String("\\\n");
            c = c.mid(71);
        }
        out += c + QLatin1Char('\n');
    }
    for (int i = 0; i < blob.size(); i += 70)
        out += blob.mid(i, 70) + QLatin1Char('\n');
    out += QStringLiteral("---- END SSH2 PUBLIC KEY ----\n");
    return out;
}

bool writeTextFile(const QString &path, const QByteArray &text, bool privateFile, QString *error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { // binary mode: no CRLF translation
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(normalizeNewlines(text));
    if (privateFile)
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }

#ifdef Q_OS_WIN
    if (privateFile) {
        // OpenSSH for Windows refuses private keys readable by other users.
        // Give the file a protected DACL with full access for the current user only.
        HANDLE token = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            DWORD len = 0;
            GetTokenInformation(token, TokenUser, nullptr, 0, &len);
            QByteArray buf(int(len), 0);
            if (GetTokenInformation(token, TokenUser, buf.data(), len, &len)) {
                auto *tu = reinterpret_cast<TOKEN_USER *>(buf.data());
                EXPLICIT_ACCESSW ea{};
                ea.grfAccessPermissions = GENERIC_ALL;
                ea.grfAccessMode = SET_ACCESS;
                ea.grfInheritance = NO_INHERITANCE;
                ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
                ea.Trustee.TrusteeType = TRUSTEE_IS_USER;
                ea.Trustee.ptstrName = reinterpret_cast<LPWSTR>(tu->User.Sid);
                PACL acl = nullptr;
                if (SetEntriesInAclW(1, &ea, nullptr, &acl) == ERROR_SUCCESS) {
                    std::wstring wpath = path.toStdWString();
                    SetNamedSecurityInfoW(wpath.data(), SE_FILE_OBJECT,
                                          DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                          nullptr, nullptr, acl, nullptr);
                    LocalFree(acl);
                }
            }
            CloseHandle(token);
        }
    }
#endif
    return true;
}

QString fingerprintOfBlob(const QByteArray &base64Blob)
{
    const QByteArray raw = QByteArray::fromBase64(base64Blob);
    unsigned char digest[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(digest, reinterpret_cast<const unsigned char *>(raw.constData()),
                       static_cast<unsigned long long>(raw.size()));
    QByteArray b64 = QByteArray(reinterpret_cast<const char *>(digest), sizeof digest)
                         .toBase64(QByteArray::OmitTrailingEquals);
    return QStringLiteral("SHA256:") + QString::fromLatin1(b64);
}

} // namespace keytools
