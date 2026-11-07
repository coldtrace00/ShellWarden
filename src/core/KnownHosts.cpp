#include "KnownHosts.h"
#include "KeyTools.h"

#include <QDir>
#include <QFile>
#include <QMessageAuthenticationCode>
#include <QSaveFile>
#include <QStandardPaths>

namespace knownhosts {

namespace {

QList<QByteArray> readLines(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QByteArray all = f.readAll();
    all.replace("\r\n", "\n");
    QList<QByteArray> lines = all.split('\n');
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    return lines;
}

bool writeLines(const QString &path, const QList<QByteArray> &lines)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    for (const QByteArray &l : lines) {
        f.write(l);
        f.write("\n");
    }
    return f.commit();
}

// Does a single host field (comma-separated, possibly hashed) match pattern?
bool fieldMatches(const QByteArray &field, const QString &pattern)
{
    const QByteArray pat = pattern.toUtf8();
    for (const QByteArray &item : field.split(',')) {
        if (item == pat)
            return true;
        if (item.startsWith("|1|")) { // |1|salt|hmac-sha1(salt, host)
            const QList<QByteArray> parts = item.split('|');
            if (parts.size() == 4) {
                const QByteArray salt = QByteArray::fromBase64(parts.at(2));
                const QByteArray mac = QMessageAuthenticationCode::hash(pat, salt, QCryptographicHash::Sha1);
                if (mac.toBase64() == parts.at(3))
                    return true;
            }
        }
    }
    return false;
}

} // namespace

QString defaultPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/known_hosts");
}

QString hostPattern(const QString &host, int port)
{
    return port == 22 ? host : QStringLiteral("[%1]:%2").arg(host).arg(port);
}

QVector<Entry> load(const QString &path)
{
    QVector<Entry> out;
    const QList<QByteArray> lines = readLines(path);
    for (int i = 0; i < lines.size(); ++i) {
        const QByteArray l = lines.at(i).trimmed();
        if (l.isEmpty() || l.startsWith('#'))
            continue;
        QList<QByteArray> parts = l.simplified().split(' ');
        if (!parts.isEmpty() && parts.first().startsWith('@')) // @cert-authority / @revoked
            parts.removeFirst();
        if (parts.size() < 3)
            continue;
        Entry e;
        e.line = i;
        e.hosts = QString::fromUtf8(parts.at(0));
        e.keyType = QString::fromLatin1(parts.at(1));
        e.fingerprint = keytools::fingerprintOfBlob(parts.at(2));
        out.append(e);
    }
    return out;
}

bool removeLine(const QString &path, int line)
{
    QList<QByteArray> lines = readLines(path);
    if (line < 0 || line >= lines.size())
        return false;
    lines.removeAt(line);
    return writeLines(path, lines);
}

int removeHost(const QString &path, const QString &host, int port)
{
    const QString pattern = hostPattern(host, port);
    QList<QByteArray> lines = readLines(path);
    int removed = 0;
    for (int i = lines.size() - 1; i >= 0; --i) {
        const QByteArray l = lines.at(i).trimmed();
        if (l.isEmpty() || l.startsWith('#'))
            continue;
        QList<QByteArray> parts = l.simplified().split(' ');
        if (!parts.isEmpty() && parts.first().startsWith('@'))
            parts.removeFirst();
        if (!parts.isEmpty() && fieldMatches(parts.first(), pattern)) {
            lines.removeAt(i);
            ++removed;
        }
    }
    if (removed)
        writeLines(path, lines);
    return removed;
}

} // namespace knownhosts
