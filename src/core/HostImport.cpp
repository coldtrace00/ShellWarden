#include "HostImport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantMap>

namespace hostimport {

namespace {

QString home() { return QStandardPaths::writableLocation(QStandardPaths::HomeLocation); }

QString expandPath(QString p, const QString &host, const QString &user)
{
    p = p.trimmed();
    if (p.size() >= 2 && p.startsWith(QLatin1Char('"')) && p.endsWith(QLatin1Char('"')))
        p = p.mid(1, p.size() - 2);
    if (p.startsWith(QStringLiteral("~/")) || p == QStringLiteral("~"))
        p = home() + p.mid(1);
    p.replace(QStringLiteral("%d"), home());
    p.replace(QStringLiteral("%h"), host);
    p.replace(QStringLiteral("%r"), user);
    p.replace(QStringLiteral("%u"), user);
    return QDir::cleanPath(p);
}

bool isPattern(const QString &h) { return h.contains(QLatin1Char('*')) || h.contains(QLatin1Char('?')) || h.startsWith(QLatin1Char('!')); }

struct Block
{
    QStringList names;
    QVariantMap opts;   // lower-case keys, first value wins (like ssh)
};

void parseInto(const QString &text, const QString &baseDir, QList<Block> &blocks, Block &global, int depth)
{
    Block *cur = blocks.isEmpty() ? &global : &blocks.last();
    bool inMatch = false;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        // "Key value", "Key=value" or "Key = value"
        static const QRegularExpression kv(QStringLiteral("^(\\S+?)\\s*(?:=\\s*|\\s+)(.*)$"));
        const auto m = kv.match(line);
        if (!m.hasMatch())
            continue;
        const QString key = m.captured(1).toLower();
        const QString value = m.captured(2).trimmed();
        if (key == QLatin1String("host")) {
            Block b;
            b.names = value.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            blocks.append(b);
            cur = &blocks.last();
            inMatch = false;
            continue;
        }
        if (key == QLatin1String("match")) {
            inMatch = true;   // conditions we can't evaluate: ignore the block
            continue;
        }
        if (inMatch)
            continue;
        if (key == QLatin1String("include") && depth < 2) {
            for (const QString &pat : value.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
                QString p = expandPath(pat, {}, {});
                if (QFileInfo(p).isRelative())
                    p = baseDir + QLatin1Char('/') + p;
                const QFileInfo fi(p);
                const QStringList files = QDir(fi.absolutePath()).entryList({fi.fileName()}, QDir::Files, QDir::Name);
                for (const QString &f : files) {
                    QFile file(fi.absolutePath() + QLatin1Char('/') + f);
                    if (file.open(QIODevice::ReadOnly))
                        parseInto(QString::fromUtf8(file.readAll()), baseDir, blocks, global, depth + 1);
                }
            }
            cur = blocks.isEmpty() ? &global : &blocks.last();
            continue;
        }
        if (!cur->opts.contains(key))
            cur->opts.insert(key, value);
    }
}

} // namespace

QString sshConfigPath() { return home() + QStringLiteral("/.ssh/config"); }

QVariantList parseSshConfig(const QString &text, const QString &baseDir)
{
    QList<Block> blocks;
    Block global;
    parseInto(text, baseDir.isEmpty() ? home() + QStringLiteral("/.ssh") : baseDir, blocks, global, 0);

    // options from "Host *" blocks apply to every host, unless the host sets them
    QVariantMap wildcard = global.opts;
    for (const Block &b : std::as_const(blocks)) {
        if (b.names.contains(QStringLiteral("*")))
            for (auto it = b.opts.cbegin(); it != b.opts.cend(); ++it)
                if (!wildcard.contains(it.key()))
                    wildcard.insert(it.key(), it.value());
    }

    QVariantList out;
    for (const Block &b : std::as_const(blocks)) {
        for (const QString &name : b.names) {
            if (isPattern(name))
                continue;
            QVariantMap o = b.opts;
            for (auto it = wildcard.cbegin(); it != wildcard.cend(); ++it)
                if (!o.contains(it.key()))
                    o.insert(it.key(), it.value());
            QString host = o.value(QStringLiteral("hostname"), name).toString();
            host.replace(QStringLiteral("%h"), name);   // "HostName %h.example.com"
            const QString user = o.value(QStringLiteral("user")).toString();
            QString key = o.value(QStringLiteral("identityfile")).toString();
            if (!key.isEmpty())
                key = expandPath(key, host, user);
            QString jump = o.value(QStringLiteral("proxyjump")).toString();
            if (jump.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0)
                jump.clear();
            out << QVariantMap{
                {QStringLiteral("label"), name},
                {QStringLiteral("hostname"), host},
                {QStringLiteral("port"), o.value(QStringLiteral("port"), 22).toInt()},
                {QStringLiteral("username"), user},
                {QStringLiteral("keyFile"), key},
                {QStringLiteral("jump"), jump.section(QLatin1Char(','), 0, 0).trimmed()},
                {QStringLiteral("group"), QStringLiteral("ssh config")},
                {QStringLiteral("source"), QStringLiteral("ssh")},
            };
        }
    }
    return out;
}

QVariantList readSshConfig()
{
    QFile f(sshConfigPath());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return parseSshConfig(QString::fromUtf8(f.readAll()));
}

QVariantList readPuttySessions()
{
    QVariantList out;
    auto add = [&out](const QString &rawName, const QString &protocol, const QString &host, int port,
                      const QString &user, const QString &keyFile) {
        const QString name = QUrl::fromPercentEncoding(rawName.toUtf8());
        if (name == QLatin1String("Default Settings") || host.trimmed().isEmpty())
            return;
        if (!protocol.isEmpty() && protocol != QLatin1String("ssh"))
            return;
        QString h = host.trimmed(), u = user.trimmed();
        if (u.isEmpty() && h.contains(QLatin1Char('@'))) {
            u = h.section(QLatin1Char('@'), 0, 0);
            h = h.section(QLatin1Char('@'), 1);
        }
        out << QVariantMap{
            {QStringLiteral("label"), name},
            {QStringLiteral("hostname"), h},
            {QStringLiteral("port"), port > 0 ? port : 22},
            {QStringLiteral("username"), u},
            {QStringLiteral("keyFile"), keyFile},
            {QStringLiteral("jump"), QString()},
            {QStringLiteral("group"), QStringLiteral("PuTTY")},
            {QStringLiteral("source"), QStringLiteral("putty")},
        };
    };
#ifdef Q_OS_WIN
    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\SimonTatham\\PuTTY\\Sessions"), QSettings::NativeFormat);
    for (const QString &s : reg.childGroups()) {
        reg.beginGroup(s);
        add(s, reg.value(QStringLiteral("Protocol"), QStringLiteral("ssh")).toString(),
            reg.value(QStringLiteral("HostName")).toString(), reg.value(QStringLiteral("PortNumber"), 22).toInt(),
            reg.value(QStringLiteral("UserName")).toString(), reg.value(QStringLiteral("PublicKeyFile")).toString());
        reg.endGroup();
    }
#else
    const QDir dir(home() + QStringLiteral("/.putty/sessions"));
    for (const QString &s : dir.entryList(QDir::Files)) {
        QFile f(dir.filePath(s));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        QVariantMap kv;
        for (const QString &l : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
            const int eq = l.indexOf(QLatin1Char('='));
            if (eq > 0)
                kv.insert(l.left(eq).trimmed(), l.mid(eq + 1).trimmed());
        }
        add(s, kv.value(QStringLiteral("Protocol"), QStringLiteral("ssh")).toString(), kv.value(QStringLiteral("HostName")).toString(),
            kv.value(QStringLiteral("PortNumber"), 22).toInt(), kv.value(QStringLiteral("UserName")).toString(),
            kv.value(QStringLiteral("PublicKeyFile")).toString());
    }
#endif
    return out;
}

} // namespace hostimport
