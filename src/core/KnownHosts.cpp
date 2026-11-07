#include "KnownHosts.h"
#include "KeyTools.h"

#include <QDir>
#include <QFile>
#include <QMessageAuthenticationCode>
#include <QSaveFile>
#include <QRandomGenerator>
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

bool matches(const QString &hostsField, const QString &host, int port)
{
    return fieldMatches(hostsField.toUtf8(), hostPattern(host, port));
}

QByteArray hashedName(const QString &pattern, const QByteArray &salt)
{
    QByteArray s = salt;
    if (s.isEmpty()) {
        s.resize(20);
        QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(s.data()), 5);
    }
    const QByteArray mac = QMessageAuthenticationCode::hash(pattern.toUtf8(), s, QCryptographicHash::Sha1);
    return "|1|" + s.toBase64() + '|' + mac.toBase64();
}

int hashFile(const QString &path)
{
    const QList<QByteArray> lines = readLines(path);
    QList<QByteArray> out;
    int hashed = 0;
    for (const QByteArray &raw : lines) {
        const QByteArray l = raw.trimmed();
        const int sp = l.indexOf(' ');
        // comments, marker lines (@cert-authority …) and wildcard patterns stay as they are:
        // a hashed pattern could no longer match anything
        if (l.isEmpty() || l.startsWith('#') || l.startsWith('@') || sp <= 0) {
            out << raw;
            continue;
        }
        const QByteArray field = l.left(sp), rest = l.mid(sp);
        bool plain = false;
        for (const QByteArray &item : field.split(','))
            plain = plain || (!item.startsWith("|1|") && !item.isEmpty());
        if (!plain || field.contains('*') || field.contains('?') || field.contains('!')) {
            out << raw;
            continue;
        }
        for (const QByteArray &item : field.split(',')) {
            if (item.isEmpty())
                continue;
            out << (item.startsWith("|1|") ? item : hashedName(QString::fromUtf8(item), {})) + rest;
            ++hashed;
        }
    }
    if (hashed && !writeLines(path, out))
        return -1;
    return hashed;
}

} // namespace knownhosts
