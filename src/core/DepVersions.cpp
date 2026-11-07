#include "DepVersions.h"

#include <QObject>
#include <QStringList>
#include <QVersionNumber>

#include <libssh/libssh.h>
#include <openssl/crypto.h>
#include <openssl/opensslv.h>
#include <sodium.h>

#ifndef SHELLWARDEN_ALLOW_OLD_LIBSSH
static_assert(LIBSSH_VERSION_INT >= SSH_VERSION_INT(0, 10, 6), "libssh 0.10.6 or newer is required (Terrapin, CVE-2023-48795)");
#endif
static_assert(OPENSSL_VERSION_MAJOR >= 3, "OpenSSL 3.0 or newer is required");

namespace depversions {

namespace {
QString sodiumVersion() { return QString::fromLatin1(sodium_version_string()); }
QString sslVersion()
{
    return QStringLiteral("%1.%2.%3").arg(OPENSSL_version_major()).arg(OPENSSL_version_minor()).arg(OPENSSL_version_patch());
}
QString sshVersion()
{
    // ssh_version(0) returns the full version string, e.g. "0.11.1/openssl/zlib"
    const char *v = ssh_version(0);
    return v ? QString::fromLatin1(v).section(QLatin1Char('/'), 0, 0) : QString();
}
} // namespace

QString runtimeProblem()
{
    QStringList bad;
#ifndef SHELLWARDEN_ALLOW_OLD_LIBSSH
    if (!ssh_version(SSH_VERSION_INT(0, 10, 6)))
        bad << QObject::tr("libssh %1 (0.10.6 or newer is needed)").arg(sshVersion());
#endif
    if (QVersionNumber::fromString(sodiumVersion()) < QVersionNumber(1, 0, 18))
        bad << QObject::tr("libsodium %1 (1.0.18 or newer is needed)").arg(sodiumVersion());
    if (OPENSSL_version_major() < 3)
        bad << QObject::tr("OpenSSL %1 (3.0 or newer is needed)").arg(sslVersion());
    if (bad.isEmpty())
        return {};
    return QObject::tr("These libraries are too old and have known security problems:\n%1\n\n"
                       "Install the current ShellWarden package, or update them.")
        .arg(bad.join(QLatin1Char('\n')));
}

QString summary()
{
    return QStringLiteral("libssh %1 · libsodium %2 · OpenSSL %3").arg(sshVersion(), sodiumVersion(), sslVersion());
}

} // namespace depversions
