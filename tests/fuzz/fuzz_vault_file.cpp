// A vault file from disk (could be damaged or replaced): header parsing and
// the KDF limits must hold before anything expensive happens.
#include "FuzzCommon.h"
#include "core/Vault.h"
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
    fuzzInit();
    static QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/v.swv");
    { QFile f(path); f.open(QIODevice::WriteOnly); f.write(bytes(d, n)); }
    Vault v(path);
    QString err;
    v.unlock(QStringLiteral("fuzz password"), &err);
    // the same file read as a backup (re-encryption after a password change)
    Vault::KeyCache cache;
    QJsonObject data;
    Vault::readFile(path, QStringLiteral("fuzz password"), &cache, &data);
    return 0;
}
