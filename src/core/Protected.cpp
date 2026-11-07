#include "Protected.h"

#include <sodium.h>

namespace protect {

static_assert(crypto_box_PUBLICKEYBYTES == kPublicLen && crypto_box_SECRETKEYBYTES == kSecretLen
                  && crypto_box_SEEDBYTES == 32,
              "unexpected libsodium sizes");

QByteArray seal(const QByteArray &plain, const QByteArray &publicKey)
{
    if (publicKey.size() != kPublicLen)
        return {};
    QByteArray c(plain.size() + int(crypto_box_SEALBYTES), Qt::Uninitialized);
    if (crypto_box_seal(reinterpret_cast<unsigned char *>(c.data()), reinterpret_cast<const unsigned char *>(plain.constData()),
                        quint64(plain.size()), reinterpret_cast<const unsigned char *>(publicKey.constData())) != 0)
        return {};
    return c.toBase64();
}

bool open(const QByteArray &sealedBase64, const QByteArray &publicKey, const unsigned char *secretKey, QByteArray *plain)
{
    const QByteArray c = QByteArray::fromBase64(sealedBase64);
    if (!secretKey || publicKey.size() != kPublicLen || c.size() < int(crypto_box_SEALBYTES))
        return false;
    QByteArray m(c.size() - int(crypto_box_SEALBYTES), Qt::Uninitialized);
    if (crypto_box_seal_open(reinterpret_cast<unsigned char *>(m.data()), reinterpret_cast<const unsigned char *>(c.constData()),
                             quint64(c.size()), reinterpret_cast<const unsigned char *>(publicKey.constData()), secretKey) != 0) {
        sodium_memzero(m.data(), size_t(m.size()));
        return false;
    }
    *plain = m;
    return true;
}

void keypairFromSeed(const unsigned char *seed32, QByteArray *publicKey, unsigned char *secretKey32)
{
    publicKey->resize(kPublicLen);
    crypto_box_seed_keypair(reinterpret_cast<unsigned char *>(publicKey->data()), secretKey32, seed32);
}

} // namespace protect
