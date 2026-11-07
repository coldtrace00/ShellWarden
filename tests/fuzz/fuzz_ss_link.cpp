// ss:// links pasted by the user.
#include "FuzzCommon.h"
#include "core/Shadowsocks.h"
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
    fuzzInit();
    const auto l = shadowsocks::parseLink(QString::fromUtf8(bytes(d, n)));
    if (l.ok)
        shadowsocks::keyProblem(l.method, l.password);
    return 0;
}
