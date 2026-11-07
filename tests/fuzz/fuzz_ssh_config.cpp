// ~/.ssh/config import.
#include "FuzzCommon.h"
#include "core/HostImport.h"
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
    fuzzInit();
    hostimport::parseSshConfig(QString::fromUtf8(bytes(d, n)), QStringLiteral("/nonexistent"));
    return 0;
}
