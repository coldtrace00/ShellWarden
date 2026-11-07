// Signature files of updates (downloaded from the internet).
#include "FuzzCommon.h"
#include "core/Minisign.h"
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
    fuzzInit();
    QString err, tc;
    minisign::verify(QByteArrayLiteral("data"), bytes(d, n), QStringLiteral("RWSSkHbWDMD2/E/lBsrPfDbBhXxI5Kw9zo4ELaOiJp1LwupraYGhvEfu"), &err, &tc);
    minisign::verify(QByteArrayLiteral("data"), QByteArrayLiteral("x"), QString::fromLatin1(bytes(d, n)), &err, &tc);
    return 0;
}
