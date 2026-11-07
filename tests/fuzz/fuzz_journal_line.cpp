// Lines of the encrypted journal (the file can be edited by anyone with disk access).
#include "FuzzCommon.h"
#include "core/ActivityLog.h"
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
    fuzzInit();
    static QByteArray pk(crypto_box_PUBLICKEYBYTES, 0), sk(crypto_box_SECRETKEYBYTES, 0);
    static bool made = false;
    if (!made) {
        crypto_box_keypair(reinterpret_cast<unsigned char *>(pk.data()), reinterpret_cast<unsigned char *>(sk.data()));
        made = true;
    }
    ActivityLog::openLine(bytes(d, n), pk, reinterpret_cast<const unsigned char *>(sk.constData()));
    return 0;
}
