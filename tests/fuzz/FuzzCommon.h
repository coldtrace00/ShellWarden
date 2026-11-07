#pragma once
// Shared setup for the libFuzzer targets: a QCoreApplication and libsodium.
#include <QCoreApplication>
#include <QByteArray>
#include <sodium.h>

static void fuzzInit()
{
    static int argc = 1;
    static char name[] = "fuzz";
    static char *argv[] = {name, nullptr};
    static QCoreApplication *app = nullptr;
    if (!app) {
        app = new QCoreApplication(argc, argv);
        if (sodium_init() < 0)
            __builtin_trap();
    }
}
static QByteArray bytes(const uint8_t *d, size_t n) { return QByteArray(reinterpret_cast<const char *>(d), int(n)); }
