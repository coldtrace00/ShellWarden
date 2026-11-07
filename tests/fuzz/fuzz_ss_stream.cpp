// Bytes from a Shadowsocks server (or whoever answers in its place).
#include "FuzzCommon.h"
#include "core/Shadowsocks.h"
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
    fuzzInit();
    if (n < 1)
        return 0;
    static const struct { const char *m, *k; } ciphers[] = {
        {"chacha20-ietf-poly1305", "pw"}, {"aes-256-gcm", "pw"}, {"aes-128-gcm", "pw"},
        {"2022-blake3-aes-256-gcm", "c3dTZXN0S2V5LTMyLWJ5dGVzLWFlczI1Ni0wMDAwMDA="},
        {"2022-blake3-aes-128-gcm", "c3dUZXN0S2V5LWFlczEyOA=="},
        {"2022-blake3-chacha20-poly1305", "c3dUZXN0S2V5LTMyLWJ5dGVzLWNoYWNoYTIwLTAwMDA="}};
    const auto &c = ciphers[d[0] % 6];
    bool ok = false;
    shadowsocks::decodeServerBytesForFuzzing(QString::fromLatin1(c.m), QString::fromLatin1(c.k), bytes(d + 1, n - 1), &ok);
    return 0;
}
