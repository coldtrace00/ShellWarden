#pragma once

#include <QByteArray>
#include <QString>

// Key generation, import and export built on libssh's PKI API.
//
// Public keys are ALWAYS produced in the one-line OpenSSH format
//   "<type> <base64-blob> <comment>\n"
// which is what authorized_keys expects. RFC 4716 ("---- BEGIN SSH2 PUBLIC
// KEY ----", what PuTTYgen's "Save public key" writes) is available only as
// an explicit, separately named export.
namespace keytools {

enum class KeyType { Ed25519, EcdsaP256, EcdsaP384, EcdsaP521, Rsa3072, Rsa4096 };

struct KeyMaterial
{
    QString type;          // "ssh-ed25519", "ecdsa-sha2-nistp256", "ssh-rsa" …
    QByteArray privateKey; // PEM / OpenSSH text, encrypted when passphrase set
    QString publicLine;    // "<type> <base64> <comment>" (no newline)
    QString fingerprint;   // "SHA256:…"
    bool encrypted = false;
};

enum class ImportResult { Ok, NeedPassphrase, WrongPassphrase, Invalid };

bool generate(KeyType type, const QString &comment, const QString &passphrase,
              KeyMaterial *out, QString *error);

// Accepts OpenSSH ("BEGIN OPENSSH PRIVATE KEY") and PEM private keys.
// The original text is kept as-is so its own encryption is preserved.
ImportResult importPrivateKey(const QByteArray &text, const QString &passphrase,
                              const QString &comment, KeyMaterial *out, QString *error);

// PuTTY .ppk (format 2 and 3, encrypted or not). The key is converted to the
// OpenSSH format and protected with the same passphrase.
ImportResult importPpk(const QByteArray &text, const QString &passphrase,
                       const QString &comment, KeyMaterial *out, QString *error);
bool isPpk(const QByteArray &text);

// True when the private key text is passphrase-protected.
bool isEncrypted(const QByteArray &privateKeyText);

// Checks that `passphrase` opens the key.
bool checkPassphrase(const QByteArray &privateKeyText, const QString &passphrase);

enum class PassphraseChange { Ok, WrongPassphrase, Invalid, Failed };

// Adds, changes or removes the passphrase of a private key: opens it with
// `oldPassphrase` (empty for a key without one) and writes it again in the
// OpenSSH format, protected with `newPassphrase` (bcrypt_pbkdf + aes256-ctr),
// or unprotected when that is empty. The key itself does not change: the
// public key and fingerprint stay the same (checked), so nothing on the
// servers needs updating. `out` gets the new private key text.
PassphraseChange changePassphrase(const QByteArray &privateKeyText, const QString &oldPassphrase,
                                  const QString &newPassphrase, QByteArray *out, QString *error);

// RFC 4716 rendering of an OpenSSH public key line.
QString toRfc4716(const QString &publicLine);

// Writes text with LF line endings, UTF-8, no BOM, ending in exactly one '\n'.
bool writeTextFile(const QString &path, const QByteArray &text, bool privateFile, QString *error);

// SHA256 fingerprint ("SHA256:…") of a base64 public key blob.
QString fingerprintOfBlob(const QByteArray &base64Blob);

} // namespace keytools
