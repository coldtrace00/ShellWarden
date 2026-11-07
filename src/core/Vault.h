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

    QJsonObject &data() { return m_data; }
    const QJsonObject &data() const { return m_data; }
    bool save(QString *error);

private:
    bool deriveKey(const QString &password, const unsigned char *salt,
                   unsigned long long ops, size_t mem, unsigned char *out, QString *error) const;
    bool writeFile(QString *error);

    QString m_path;
    unsigned char *m_key = nullptr;         // crypto_aead_xchacha20poly1305_ietf_KEYBYTES
    unsigned char m_salt[16] = {};
    unsigned long long m_ops = 0;
    size_t m_mem = 0;
    QJsonObject m_data;
};
