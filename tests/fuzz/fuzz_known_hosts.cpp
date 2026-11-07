// known_hosts (also copied from ~/.ssh).
#include "FuzzCommon.h"
#include "core/KnownHosts.h"
#include <QFile>
#include <QTemporaryDir>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
    fuzzInit();
    static QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/kh");
    { QFile f(path); f.open(QIODevice::WriteOnly); f.write(bytes(d, n)); }
    knownhosts::load(path);
    knownhosts::hashFile(path);
    knownhosts::removeHost(path, QStringLiteral("example.com"), 22);
    return 0;
}
