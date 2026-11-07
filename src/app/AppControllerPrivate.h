// Shared by the AppController*.cpp files (one class, split by topic).
// Internal: include only from those files.
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVariantMap>

namespace {


const QString kHosts = QStringLiteral("hosts");
const QString kKeys = QStringLiteral("keys");
const QString kProxies = QStringLiteral("proxies");
const QString kSnippets = QStringLiteral("snippets");

inline QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

inline const QVariantMap &settingDefaults()
{
    static const QVariantMap d{
        {QStringLiteral("killSwitch"), false},
        {QStringLiteral("defaultProxyId"), QString()},
        {QStringLiteral("fontFamily"), QString()},
        {QStringLiteral("fontSize"), 11},
        {QStringLiteral("copyOnSelect"), true},
        {QStringLiteral("rightClickPaste"), true},
        {QStringLiteral("pasteWarning"), true},                   // ask before pasting several lines
        {QStringLiteral("scrollback"), 10000},
        {QStringLiteral("autoLockMinutes"), 15},
        {QStringLiteral("keepaliveSeconds"), 30},
        {QStringLiteral("language"), QString()},
        {QStringLiteral("terminalTheme"), QStringLiteral("default")},
        {QStringLiteral("sidebarCollapsed"), false},
        {QStringLiteral("updateRepo"), QStringLiteral(APP_GITHUB_REPO)},
        {QStringLiteral("autoCheckUpdates"), true},
        {QStringLiteral("hostProbe"), false},                     // online dot on the host cards (opt-in)
        {QStringLiteral("commandHistory"), true},                 // remember commands per host (in the vault)
        {QStringLiteral("logEnabled"), true},
        {QStringLiteral("logRetentionDays"), 30},                 // the journal is encrypted with the vault
        {QStringLiteral("killSwitchInterval"), 3},
        {QStringLiteral("firewallLock"), false},
        {QStringLiteral("bell"), QStringLiteral("off")},          // off | flash | sound
        {QStringLiteral("highlightOutput"), true},
        {QStringLiteral("autoReconnect"), true},
        {QStringLiteral("lockOnSystemLock"), true},               // Win+L / lock screen / sleep
        {QStringLiteral("sshfpCheck"), true},
        {QStringLiteral("batchRunKey"), QStringLiteral("ctrl")},   // run on hosts: "ctrl" = Ctrl+Enter, "enter" = Enter
        {QStringLiteral("localTerminal"), false},                 // extra: a shell of this computer in a tab
        {QStringLiteral("localShell"), QString()},                // id from localShells(); "" = first                     // look up host keys in DNS (SSHFP)
        {QStringLiteral("monitorEnabled"), true},                 // server mini-monitor under the terminal
        {QStringLiteral("monitorInterval"), 5},                   // seconds
        {QStringLiteral("monitorItems"), QStringLiteral("cpu,ram,disk,uptime,net")},   // also: load, swap
        {QStringLiteral("lastLockMinutes"), 15},                   // restored when "never lock" is unticked
    };
    return d;
}

inline QString locationOf(const QVariantMap &r)
{
    QStringList parts;
    for (const char *k : {"city", "country"}) {
        const QString v = r.value(QString::fromLatin1(k)).toString();
        if (!v.isEmpty())
            parts << v;
    }
    QString loc = parts.join(QStringLiteral(", "));
    const QString org = r.value(QStringLiteral("org")).toString();
    if (!org.isEmpty())
        loc += (loc.isEmpty() ? QString() : QStringLiteral(" · ")) + org;
    return loc;
}


inline QString cfRouteName(const QJsonObject &px)
{
    const QString n = px.value(QStringLiteral("name")).toString().trimmed();
    return n.isEmpty() || n.compare(QLatin1String("Cloudflare Tunnel"), Qt::CaseInsensitive) == 0
               ? QStringLiteral("Cloudflare Tunnel") : QStringLiteral("Cloudflare Tunnel · %1").arg(n);
}

} // namespace
