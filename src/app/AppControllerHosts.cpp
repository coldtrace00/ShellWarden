// Part of AppController: hosts and importing them (~/.ssh/config, PuTTY, other clients).
#include "AppController.h"
#include "core/Shadowsocks.h"
#include "TerminalSession.h"
#include "core/AgentClient.h"
#include "DarkTitleBar.h"
#include <QWindow>
#include "core/FirewallLock.h"
#include "core/HostImport.h"
#include "core/KeyTools.h"
#include "core/KnownHosts.h"
#include "core/Protected.h"
#include "core/PasswordTools.h"
#include "core/Paths.h"
#include "core/SecureBytes.h"
#include "terminal/TerminalItem.h"

#include <QDesktopServices>
#include <QDir>
#include <QFontDatabase>
#include <QThreadPool>
#ifdef SW_HAVE_SERIAL
#  include <QSerialPortInfo>
#endif

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QSet>
#include <QEvent>
#include <QFile>
#include <memory>
#include <QFileInfo>
#include <QImage>
#include <algorithm>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QColor>
#include <QLibraryInfo>
#include <QLocale>
#include <QQmlEngine>
#include <QPointer>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif
#include <QStandardPaths>
#include <QUuid>

#include "AppControllerPrivate.h"

// ---------------------------------------------------------------------------
// hosts
// ---------------------------------------------------------------------------

QVariantMap AppController::host(const QString &id) const
{
    QJsonObject h = findIn(kHosts, id);
    // a protected password stays sealed: the editor only learns that there is one
    if (isSealed(h, QStringLiteral("password")))
        h.insert(QStringLiteral("passwordProtected"), true);
    h.remove(QStringLiteral("sealed"));
    return h.toVariantMap();
}

QString AppController::saveHost(const QVariantMap &h)
{
    if (!m_vault.isUnlocked())
        return tr("The vault is locked.");
    QJsonObject o = QJsonObject::fromVariantMap(h);
    const bool serial = o.value(QStringLiteral("type")).toString() == QLatin1String("serial");
    if (serial) {
        QString port = o.value(QStringLiteral("serialPort")).toString().section(QStringLiteral(" — "), 0, 0).trimmed();
        if (port.isEmpty())
            return tr("Choose a serial port.");
        o.insert(QStringLiteral("serialPort"), port);
        o.insert(QStringLiteral("hostname"), port);
        o.insert(QStringLiteral("port"), 22);
        if (o.value(QStringLiteral("label")).toString().trimmed().isEmpty())
            o.insert(QStringLiteral("label"), port);
        const bool isNew = o.value(QStringLiteral("id")).toString().isEmpty();
        if (!upsert(kHosts, o))
            return tr("Could not save the vault.");
        logEvent(ActivityLog::Info, QStringLiteral("host"), isNew ? tr("Host added") : tr("Host changed"),
                 port, o.value(QStringLiteral("label")).toString());
        return {};
    }
    const QString hostname = o.value(QStringLiteral("hostname")).toString().trimmed();
    if (hostname.isEmpty())
        return tr("Enter a host name or IP address.");
    if (!o.value(QStringLiteral("jumpHostId")).toString().isEmpty()
        && o.value(QStringLiteral("jumpHostId")).toString() == o.value(QStringLiteral("id")).toString())
        return tr("A host cannot be its own jump host.");
    if (hostname.contains(QLatin1Char(' ')))
        return tr("The host name must not contain spaces.");
    const int port = o.value(QStringLiteral("port")).toVariant().toInt();
    if (port < 1 || port > 65535)
        return tr("Port must be between 1 and 65535.");

    o.insert(QStringLiteral("hostname"), hostname);
    o.insert(QStringLiteral("port"), port);
    o.insert(QStringLiteral("username"), o.value(QStringLiteral("username")).toString().trimmed());
    if (o.value(QStringLiteral("label")).toString().trimmed().isEmpty())
        o.insert(QStringLiteral("label"), hostname);
    // tunnels: keep only well-formed entries
    QJsonArray fw;
    for (const QJsonValue &v : o.value(QStringLiteral("forwards")).toArray()) {
        QJsonObject f = v.toObject();
        const QString t = f.value(QStringLiteral("type")).toString();
        const int bp = f.value(QStringLiteral("bindPort")).toVariant().toInt();
        const int dp = f.value(QStringLiteral("destPort")).toVariant().toInt();
        if ((t != QLatin1String("L") && t != QLatin1String("R") && t != QLatin1String("D")) || bp < 1 || bp > 65535)
            continue;
        if (t != QLatin1String("D") && (dp < 1 || dp > 65535 || f.value(QStringLiteral("destHost")).toString().trimmed().isEmpty()))
            continue;
        if (f.value(QStringLiteral("id")).toString().isEmpty())
            f.insert(QStringLiteral("id"), newId());
        fw.append(f);
    }
    o.insert(QStringLiteral("forwards"), fw);

    // removed features: in-app 2FA (TOTP) — never stored again
    o.remove(QStringLiteral("require2fa"));
    o.remove(QStringLiteral("totpSecret"));
    o.remove(QStringLiteral("passwordProtected"));
    const bool isNew = o.value(QStringLiteral("id")).toString().isEmpty();
    const QJsonObject before = findIn(kHosts, o.value(QStringLiteral("id")).toString());
    // Turning "ask for the master password" off needs that password (unprotectHost) first.
    if (!isNew && isSealed(before) && !o.value(QStringLiteral("confirmMaster")).toBool())
        return tr("Enter the master password to turn off \"ask for the master password\".");
    // A sealed password the editor could not show is kept unless a new one was typed
    // or "remove the saved password" was chosen.
    o.remove(QStringLiteral("sealed"));
    o.remove(QStringLiteral("protected"));
    const bool clearPassword = o.take(QStringLiteral("clearPassword")).toBool();
    const bool keepPassword = o.take(QStringLiteral("keepPassword")).toBool();
    if (o.value(QStringLiteral("password")).toString().isEmpty() && keepPassword && !clearPassword) {
        if (isSealed(before, QStringLiteral("password"))) {
            QJsonObject sealed;
            sealed.insert(QStringLiteral("password"), before.value(QStringLiteral("sealed")).toObject().value(QStringLiteral("password")));
            o.insert(QStringLiteral("sealed"), sealed);
            o.insert(QStringLiteral("protected"), true);
        } else {
            o.insert(QStringLiteral("password"), before.value(QStringLiteral("password")));   // unprotected just now
        }
    }
    if (o.value(QStringLiteral("confirmMaster")).toBool())
        sealObject(o, {QStringLiteral("password")});
    if (!upsert(kHosts, o))
        return tr("Could not save the vault.");
    if (o.value(QStringLiteral("confirmMaster")).toBool() && sealAllProtected())   // its key too
        persist();
    if (o.value(QStringLiteral("noHistory")).toBool() && !before.value(QStringLiteral("noHistory")).toBool())
        clearCommandHistory(o.value(QStringLiteral("id")).toString());   // turned off: forget what was kept
    if (o.value(QStringLiteral("confirmMaster")).toBool() != before.value(QStringLiteral("confirmMaster")).toBool())
        logEvent(ActivityLog::Security, QStringLiteral("host"),
                 o.value(QStringLiteral("confirmMaster")).toBool() ? tr("Master password on connect enabled for host")
                                                                   : tr("Master password on connect disabled for host"),
                 QString(), o.value(QStringLiteral("label")).toString());
    logEvent(ActivityLog::Info, QStringLiteral("host"), isNew ? tr("Host added") : tr("Host changed"),
             QStringLiteral("%1@%2:%3").arg(o.value(QStringLiteral("username")).toString(), hostname).arg(port),
             o.value(QStringLiteral("label")).toString());
    if (!isNew)
        applyTunnelsLive(o.value(QStringLiteral("id")).toString(), before.value(QStringLiteral("forwards")).toArray(), fw);
    return {};
}

// Tunnels changed in the host editor go straight into its open sessions:
// new ones are opened, removed ones closed — no reconnect needed.
void AppController::applyTunnelsLive(const QString &hostId, const QJsonArray &before, const QJsonArray &after)
{
    auto key = [](const QString &type, int port) { return type + QLatin1Char(':') + QString::number(port); };
    QSet<QString> oldKeys, newKeys;
    for (const QJsonValue &v : before)
        oldKeys << key(v.toObject().value(QStringLiteral("type")).toString(), v.toObject().value(QStringLiteral("bindPort")).toVariant().toInt());
    for (const QJsonValue &v : after)
        newKeys << key(v.toObject().value(QStringLiteral("type")).toString(), v.toObject().value(QStringLiteral("bindPort")).toVariant().toInt());
    int touched = 0;
    for (int i = 0; i < m_sessions.count(); ++i) {
        TerminalSession *s = m_sessions.at(i);
        if (s->hostId() != hostId || s->state() != TerminalSession::Connected)
            continue;
        QHash<QString, QString> live;   // key → forward id
        for (const QVariant &v : s->forwards()) {
            const QVariantMap m = v.toMap();
            live.insert(key(m.value(QStringLiteral("type")).toString(),
                            m.value(QStringLiteral("bind")).toString().section(QLatin1Char(':'), -1).toInt()),
                        m.value(QStringLiteral("id")).toString());
        }
        bool changed = false;
        for (const QJsonValue &v : after) {
            const QJsonObject f = v.toObject();
            const QString k = key(f.value(QStringLiteral("type")).toString(), f.value(QStringLiteral("bindPort")).toVariant().toInt());
            if (live.contains(k))
                continue;
            if (f.contains(QStringLiteral("enabled")) && !f.value(QStringLiteral("enabled")).toBool())
                continue;   // "manual" tunnel: the user starts it from the Tunnels panel
            s->addForward(f.value(QStringLiteral("type")).toString(), f.value(QStringLiteral("bindHost")).toString(),
                          f.value(QStringLiteral("bindPort")).toVariant().toInt(), f.value(QStringLiteral("destHost")).toString(),
                          f.value(QStringLiteral("destPort")).toVariant().toInt());
            changed = true;
        }
        for (const QString &k : std::as_const(oldKeys)) {
            if (!newKeys.contains(k) && live.contains(k)) {
                s->removeForward(live.value(k));
                changed = true;
            }
        }
        touched += changed ? 1 : 0;
    }
    if (touched)
        emit notify(tr("Tunnels updated in %n open session(s) — no reconnect needed.", nullptr, touched), false);
}

// ---------------------------------------------------------------------------
// import from ~/.ssh/config and PuTTY
// ---------------------------------------------------------------------------

QVariantList AppController::importCandidates(const QString &source) const
{
    QVariantList list = source == QLatin1String("putty") ? hostimport::readPuttySessions() : hostimport::readSshConfig();
    const QJsonArray hosts = section(kHosts);
    for (QVariant &v : list) {
        QVariantMap m = v.toMap();
        bool exists = false;
        for (const QJsonValue &h : hosts) {
            const QJsonObject o = h.toObject();
            if (o.value(QStringLiteral("hostname")).toString().compare(m.value(QStringLiteral("hostname")).toString(), Qt::CaseInsensitive) == 0
                && o.value(QStringLiteral("port")).toInt() == m.value(QStringLiteral("port")).toInt()
                && o.value(QStringLiteral("username")).toString() == m.value(QStringLiteral("username")).toString())
                exists = true;
        }
        m.insert(QStringLiteral("exists"), exists);
        m.insert(QStringLiteral("keyExists"), !m.value(QStringLiteral("keyFile")).toString().isEmpty()
                                                 && QFileInfo::exists(m.value(QStringLiteral("keyFile")).toString()));
        v = m;
    }
    return list;
}

QString AppController::sshConfigPath() const { return QDir::toNativeSeparators(hostimport::sshConfigPath()); }

QString AppController::importHosts(const QVariantList &items)
{
    if (!m_vault.isUnlocked())
        return tr("The vault is locked.");
    int added = 0, keys = 0;
    QStringList lockedKeys;
    QHash<QString, QString> idByLabel;   // for ProxyJump
    QList<QPair<QString, QString>> jumps; // host id → jump alias
    for (const QJsonValue &h : section(kHosts))
        idByLabel.insert(h.toObject().value(QStringLiteral("label")).toString(), h.toObject().value(QStringLiteral("id")).toString());

    for (const QVariant &v : items) {
        const QVariantMap m = v.toMap();
        QVariantMap host{
            {QStringLiteral("label"), m.value(QStringLiteral("label"))},
            {QStringLiteral("hostname"), m.value(QStringLiteral("hostname"))},
            {QStringLiteral("port"), m.value(QStringLiteral("port"))},
            {QStringLiteral("username"), m.value(QStringLiteral("username"))},
            {QStringLiteral("group"), m.value(QStringLiteral("group"))},
        };
        // key file → keychain (only keys without a passphrase can be read here)
        const QString keyFile = m.value(QStringLiteral("keyFile")).toString();
        if (!keyFile.isEmpty() && QFileInfo::exists(keyFile)) {
            QFile f(keyFile);
            if (f.open(QIODevice::ReadOnly) && f.size() < 64 * 1024) {
                const QString text = QString::fromUtf8(f.readAll());
                keytools::KeyMaterial km;
                QString err;
                const auto res = keytools::importPrivateKey(text.toUtf8(), QString(), QFileInfo(keyFile).fileName(), &km, &err);
                if (res == keytools::ImportResult::Ok) {
                    QString keyId;
                    for (const QJsonValue &k : section(kKeys))
                        if (k.toObject().value(QStringLiteral("fingerprint")).toString() == km.fingerprint)
                            keyId = k.toObject().value(QStringLiteral("id")).toString();
                    if (keyId.isEmpty()) {
                        keyId = importKey(QFileInfo(keyFile).fileName(), text, QString(), false).value(QStringLiteral("id")).toString();
                        keys += keyId.isEmpty() ? 0 : 1;
                    }
                    host.insert(QStringLiteral("keyId"), keyId);
                } else if (res == keytools::ImportResult::NeedPassphrase) {
                    lockedKeys << QDir::toNativeSeparators(keyFile);
                }
            }
        }
        const QString e = saveHost(host);
        if (!e.isEmpty())
            continue;
        ++added;
        // saveHost assigned an id: find it by label + hostname
        for (const QJsonValue &h : section(kHosts)) {
            const QJsonObject o = h.toObject();
            if (o.value(QStringLiteral("label")).toString() == host.value(QStringLiteral("label")).toString()
                && o.value(QStringLiteral("hostname")).toString() == host.value(QStringLiteral("hostname")).toString()) {
                idByLabel.insert(o.value(QStringLiteral("label")).toString(), o.value(QStringLiteral("id")).toString());
                const QString jump = m.value(QStringLiteral("jump")).toString();
                if (!jump.isEmpty())
                    jumps << qMakePair(o.value(QStringLiteral("id")).toString(), jump);
            }
        }
    }
    // ProxyJump: point at the imported (or existing) host with that name
    if (!jumps.isEmpty()) {
        QJsonArray hosts = section(kHosts);
        for (int i = 0; i < hosts.size(); ++i) {
            QJsonObject o = hosts.at(i).toObject();
            for (const auto &j : std::as_const(jumps)) {
                const QString alias = j.second.section(QLatin1Char('@'), -1).section(QLatin1Char(':'), 0, 0);
                if (o.value(QStringLiteral("id")).toString() == j.first && idByLabel.contains(alias)
                    && idByLabel.value(alias) != j.first) {
                    o.insert(QStringLiteral("jumpHostId"), idByLabel.value(alias));
                    hosts.replace(i, o);
                }
            }
        }
        setSection(kHosts, hosts);
        persist();
    }
    logEvent(ActivityLog::Info, QStringLiteral("host"), tr("Hosts imported"), tr("%1 hosts, %2 keys").arg(added).arg(keys));
    QString msg = tr("Imported %1 host(s) and %2 key(s).").arg(added).arg(keys);
    if (!lockedKeys.isEmpty())
        msg += QLatin1Char(' ') + tr("Keys with a passphrase were not imported — add them in Keychain → Import: %1").arg(lockedKeys.join(QStringLiteral(", ")));
    return msg;
}

void AppController::deleteHost(const QString &id)
{
    logEvent(ActivityLog::Warning, QStringLiteral("host"), tr("Host deleted"), QString(),
             findIn(kHosts, id).value(QStringLiteral("label")).toString());
    QJsonArray hosts = section(kHosts);
    for (int i = 0; i < hosts.size(); ++i) {
        if (hosts.at(i).toObject().value(QStringLiteral("id")).toString() == id) {
            hosts.removeAt(i);
            break;
        }
    }
    for (int i = 0; i < hosts.size(); ++i) {
        QJsonObject h = hosts.at(i).toObject();
        if (h.value(QStringLiteral("jumpHostId")).toString() == id) {
            h.insert(QStringLiteral("jumpHostId"), QString());
            hosts.replace(i, h);
        }
    }
    setSection(kHosts, hosts);
    persist();
}

void AppController::duplicateHost(const QString &id)
{
    QJsonObject o = findIn(kHosts, id);
    if (o.isEmpty())
        return;
    o.insert(QStringLiteral("id"), newId());
    o.insert(QStringLiteral("label"), o.value(QStringLiteral("label")).toString() + tr(" (copy)"));
    QJsonArray hosts = section(kHosts);
    hosts.append(o);
    setSection(kHosts, hosts);
    persist();
}
