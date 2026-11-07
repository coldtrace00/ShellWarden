#pragma once

#include <QString>
#include <QVector>

// Reads and edits the application's known_hosts file (OpenSSH format, so it
// can be copied to/from ~/.ssh/known_hosts). libssh does the actual host key
// verification against this same file.
namespace knownhosts {

struct Entry
{
    int line = 0;          // 0-based line number in the file
    QString hosts;         // "example.com", "[example.com]:2222", or hashed "|1|…"
    QString keyType;
    QString fingerprint;   // SHA256:…
};

QString defaultPath();
QVector<Entry> load(const QString &path);
bool removeLine(const QString &path, int line);

// Removes every entry that matches host/port (used before replacing a key
// the user confirmed has legitimately changed).
int removeHost(const QString &path, const QString &host, int port);

// Host pattern as OpenSSH writes it: "host" for 22, "[host]:port" otherwise.
QString hostPattern(const QString &host, int port);

// Does a host field from the file (plain or hashed "|1|salt|hmac") name host:port?
bool matches(const QString &hostsField, const QString &host, int port);

// Like OpenSSH's HashKnownHosts: host names in the file are replaced by
// "|1|salt|HMAC-SHA1(salt, name)", so the file no longer tells where you
// connect; checking a given host still works. Lines with several names become
// one line per name; wildcard patterns and @markers are left alone.
// Returns how many names were hashed (-1 if the file could not be written).
int hashFile(const QString &path);
QByteArray hashedName(const QString &pattern, const QByteArray &salt);   // salt empty = random

} // namespace knownhosts
