// PuTTY .ppk (v2/v3) and OpenSSH private keys from a file the user picks.
#include "FuzzCommon.h"
#include "core/KeyTools.h"
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
    fuzzInit();
    keytools::KeyMaterial km;
    QString err;
    keytools::importPrivateKey(bytes(d, n), QStringLiteral("pass"), QStringLiteral("c"), &km, &err);
    keytools::isEncrypted(bytes(d, n));
    return 0;
}
