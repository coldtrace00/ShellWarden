#pragma once

#include <QString>

// Where the application keeps its data.
//
// Portable mode: if a file named "portable" (any content) or a folder named
// "data" exists next to the executable, everything is stored in <exe dir>/data
// — nothing is written to %APPDATA%, the registry or the user's folders:
//   data/vault.swv       hosts, passwords, keys (encrypted)
//   data/settings.ini    settings
//   data/known_hosts     trusted server keys
//   data/logs/           activity log
//   data/backups/        daily vault backups
//   data/keys/           where exported keys (.pub / private) are offered to be saved
//   data/backgrounds/    pictures of own terminal themes
//   data/updates/        downloaded updates
//   data/cache/          Qt's QML cache
//   data/tor/            tor.exe started by ShellWarden
// Installed mode: %APPDATA%\<AppName> on Windows, ~/.local/share/<AppName>
// on Linux. Either way the folder is made private to the user (PrivateDir.h).
namespace paths {

void init();                 // call once, after QCoreApplication exists
bool isPortable();
bool isInstalled();          // installed with the setup (Windows: unins000.exe next to the exe)
QString dataDir();
QString vaultFile();
QString knownHostsFile();
QString settingsFile();      // INI file (also in installed mode: no registry)
QString logsDir();
QString backupsDir();
QString keysDir();           // portable: data/keys; installed: ~/.ssh (or home)
QString updatesDir();
QString cacheDir();
// Empty when the data folder is private to this user (see PrivateDir.h).
QString protectionError();

} // namespace paths
