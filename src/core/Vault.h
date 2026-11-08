#pragma once

#include <QJsonObject>
#include <QString>

// Encrypted local storage for hosts, saved passwords and private keys.
//
// File layout (all integers little-endian):
//   magic      8   "SWVAULT1"
//   opslimit   8   Argon2id iterations
//   memlimit   8   Argon2id memory in bytes
//   salt      16
//   nonce     24
//   ciphertext …   XChaCha20-Poly1305(JSON), header bytes are the AAD
//
// The master password never touches disk. The derived 256-bit key lives in
// sodium_malloc()'d, mlock()'d memory and is wiped on lock().
class Vault
{
public:
    explicit Vault(QString path);
    ~Vault();

    Vault(const Vault &) = delete;
    Vault &operator=(const Vault &) = delete;

    QString path() const { return m_path; }
    bool exists() const;
    bool isUnlocked() const { return m_key != nullptr; }

    bool create(const QString &password, QString *error);
    bool unlock(const QString &password, QString *error);
    bool changePassword(const QString &oldPassword, const QString &newPassword, QString *error);
    void lock();
    // Re-checks the master password of the unlocked vault (constant time).
    bool verifyPassword(const QString &password) const;
    // Argon2id with this vault's own cost settings but another salt: keys that
    // come from the master password without being the vault key (protected
    // hosts). Never cheaper than the vault key, so it is no shortcut for guessing.
    bool deriveWithVaultCost(const QString &password, const QByteArray &salt16, unsigned char *out32,
                             QString *error) const;

    // Keys made from ONE password for several files (one cache per password):
    // backups usually share a salt, so re-encrypting 14 of them costs one or two
    // Argon2 runs, not 14.
    class KeyCache
    {
    public:
        KeyCache() = default;
        ~KeyCache();
        KeyCache(const KeyCache &) = delete;
        KeyCache &operator=(const KeyCache &) = delete;
    private:
        friend class Vault;
        struct Entry { QByteArray salt; quint64 ops = 0; quint64 mem = 0; unsigned char *key = nullptr; };
        QList<Entry> m_entries;
    };
    enum class ReadResult { Ok, WrongPassword, Damaged };
    // Reads another vault file (a backup) with `password`, without touching this one.
    static ReadResult readFile(const QString &path, const QString &password, KeyCache *cache, QJsonObject *data);
    // Writes `data` to `path`, encrypted with this vault's current key (its
    // current master password and cost), atomically and without a .bak.
    // Used to re-encrypt backups after the master password changed.
    bool writeCopy(const QString &path, const QJsonObject &data, QString *error) const;

    QJsonObject &data() { return m_data; }
    const QJsonObject &data() const { return m_data; }
    bool save(QString *error);

private:
    bool deriveKey(const QString &password, const unsigned char *salt,
                   unsigned long long ops, size_t mem, unsigned char *out, QString *error) const;
    bool writeFile(QString *error);
    bool encrypt(const QJsonObject &data, QByteArray *out) const;   // header + ciphertext with m_key

    QString m_path;
    unsigned char *m_key = nullptr;         // crypto_aead_xchacha20poly1305_ietf_KEYBYTES
    unsigned char m_salt[16] = {};
    unsigned long long m_ops = 0;
    size_t m_mem = 0;
    QJsonObject m_data;
};
