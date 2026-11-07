#pragma once

#include <QByteArray>

// Secrets of hosts that "ask for the master password to connect".
//
// Opening the vault decrypts everything in it; for these hosts that would make
// the extra question a formality. So their password, and their private key
// with its passphrase, are encrypted once more, with a public key (sealed box:
// X25519 + XSalsa20-Poly1305). Its secret half is never stored: it is derived
// from the master password (Argon2id, the vault's own cost, another salt)
// when a connection asks for it, used, and wiped. While the vault is open these
// secrets stay encrypted in memory; saving a new one needs no password because
// sealing only needs the public key.
namespace protect {

constexpr int kSaltLen = 16;
constexpr int kPublicLen = 32;
constexpr int kSecretLen = 32;

// base64 of the sealed box; empty on error
QByteArray seal(const QByteArray &plain, const QByteArray &publicKey);
// false if the box was not made for this key pair or was changed
bool open(const QByteArray &sealedBase64, const QByteArray &publicKey, const unsigned char *secretKey, QByteArray *plain);
// the key pair for a 32-byte seed (from the master password)
void keypairFromSeed(const unsigned char *seed32, QByteArray *publicKey, unsigned char *secretKey32);

} // namespace protect
