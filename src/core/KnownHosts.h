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

} // namespace knownhosts
