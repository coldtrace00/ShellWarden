#include "Paths.h"
#include "PrivateDir.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace paths {

namespace {
QString g_dataDir;
QString g_protectError;
bool g_portable = false;

void copyIfMissing(const QString &from, const QString &to)
{
    if (QFileInfo::exists(from) && !QFileInfo::exists(to))
        QFile::copy(from, to);
}
} // namespace

void init()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    g_portable = QFileInfo::exists(exeDir + QStringLiteral("/portable"))
                 || QFileInfo::exists(exeDir + QStringLiteral("/portable.txt"))
                 || QFileInfo(exeDir + QStringLiteral("/data")).isDir();
    if (g_portable)
        g_dataDir = exeDir + QStringLiteral("/data");
    else
        g_dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(g_dataDir);
    QDir().mkpath(logsDir());
    if (g_portable) {
        QDir().mkpath(backupsDir());
        QDir().mkpath(keysDir());
        // Qt keeps compiled QML in the user's cache folder; in portable mode it stays here
        QDir().mkpath(cacheDir());
        qputenv("QML_DISK_CACHE_PATH", QDir::toNativeSeparators(cacheDir()).toLocal8Bit());
        // the graphics shader cache would go to the user's cache folder too: not kept at all
        qputenv("QT_DISABLE_SHADER_DISK_CACHE", "1");
    }

    // One-time migration from the 0.1 layout (%APPDATA%/Shellwave/Shellwave).
    if (!g_portable) {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        for (const QString &legacy : {base + QStringLiteral("/Shellwave/Shellwave")}) {
            if (QDir::cleanPath(legacy) == QDir::cleanPath(g_dataDir))
                continue;
            copyIfMissing(legacy + QStringLiteral("/vault.swv"), vaultFile());
            copyIfMissing(legacy + QStringLiteral("/known_hosts"), knownHostsFile());
        }
    }

    // vault, backups, journal, known_hosts and settings: readable only by this user
    QDir().mkpath(updatesDir());
    if (!privatedir::protect(g_dataDir, &g_protectError) && g_protectError.isEmpty())
        g_protectError = QStringLiteral("?");
}

QString protectionError() { return g_protectError; }

bool isPortable() { return g_portable; }
bool isInstalled()
{
#ifdef Q_OS_WIN
    return QFileInfo::exists(QCoreApplication::applicationDirPath() + QStringLiteral("/unins000.exe"));
#else
    return false;
#endif
}
QString dataDir() { return g_dataDir; }
QString vaultFile() { return g_dataDir + QStringLiteral("/vault.swv"); }
QString knownHostsFile() { return g_dataDir + QStringLiteral("/known_hosts"); }
QString settingsFile() { return g_dataDir + QStringLiteral("/settings.ini"); }
QString logsDir() { return g_dataDir + QStringLiteral("/logs"); }
QString backupsDir() { return g_dataDir + QStringLiteral("/backups"); }
QString keysDir()
{
    if (g_portable)
        return g_dataDir + QStringLiteral("/keys");
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    return QFileInfo(home + QStringLiteral("/.ssh")).isDir() ? home + QStringLiteral("/.ssh") : home;
}
QString updatesDir()
{
#ifdef Q_OS_WIN
    // %TEMP% on Windows belongs to the user alone
    return g_portable ? g_dataDir + QStringLiteral("/updates")
                      : QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QLatin1Char('/')
                            + QCoreApplication::applicationName() + QStringLiteral("-update");
#else
    // never /tmp: it is shared by all users, and a downloaded update is about to be run
    return g_dataDir + QStringLiteral("/updates");
#endif
}
QString cacheDir() { return g_dataDir + QStringLiteral("/cache"); }

} // namespace paths
