// Part of AppController: proxies, the kill switch, IP checks and the firewall lock.
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
// proxies & IP checks
// ---------------------------------------------------------------------------

ProxyConfig AppController::proxyConfig(const QJsonObject &p, const QString &firstHop) const
{
    ProxyConfig c;
    const QString type = p.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("cloudflare")) {
        // the local end of `cloudflared access tcp` for this hostname (0 = not running)
        const int port = m_cf.portFor(firstHop);
        if (port > 0) {
            c.type = ProxyConfig::Tcp;
            c.host = QStringLiteral("127.0.0.1");
            c.port = port;
        }
        return c;
    }
    if (type == QLatin1String("tor")) {
        // Always a local SOCKS5 port; when that Tor is not running the port is
        // closed and the connection fails (never falls back to direct).
        const QString mode = p.value(QStringLiteral("torMode")).toString(QStringLiteral("auto"));
        int port = m_tor.portFor(mode);
        if (port == 0)
            port = mode == QLatin1String("browser") ? TorManager::kBrowserPort
                   : mode == QLatin1String("exe")   ? TorManager::kOwnPort : TorManager::kServicePort;
        c.type = ProxyConfig::Socks5;
        c.host = QStringLiteral("127.0.0.1");
        c.port = port;
        // a separate SOCKS user per proxy (and per "new identity") = separate Tor circuits
        c.username = m_tor.isolationUser(p.value(QStringLiteral("id")).toString());
        c.password = QStringLiteral("tor");
        return c;
    }
    c.type = type == QLatin1String("http") ? ProxyConfig::Http
           : type == QLatin1String("shadowsocks") ? ProxyConfig::Shadowsocks : ProxyConfig::Socks5;
    c.method = p.value(QStringLiteral("method")).toString();
    c.host = p.value(QStringLiteral("host")).toString().trimmed();
    c.port = p.value(QStringLiteral("port")).toVariant().toInt();
    c.username = p.value(QStringLiteral("username")).toString();
    c.password = p.value(QStringLiteral("password")).toString();
    if (c.host.isEmpty() || c.port <= 0)
        c.type = ProxyConfig::None;
    return c;
}

QVariantMap AppController::parseShadowsocksLink(const QString &uri) const
{
    const shadowsocks::Link l = shadowsocks::parseLink(uri);
    return {{QStringLiteral("ok"), l.ok}, {QStringLiteral("error"), l.error}, {QStringLiteral("name"), l.name},
            {QStringLiteral("host"), l.host}, {QStringLiteral("port"), l.port},
            {QStringLiteral("method"), l.method}, {QStringLiteral("password"), l.password}};
}

QStringList AppController::shadowsocksMethods() const
{
    return shadowsocks::methods();
}

QVariantMap AppController::proxy(const QString &id) const
{
    return findIn(kProxies, id).toVariantMap();
}

QString AppController::saveProxy(const QVariantMap &p)
{
    if (!m_vault.isUnlocked())
        return tr("The vault is locked.");
    QJsonObject o = QJsonObject::fromVariantMap(p);
    const bool cloudflare = o.value(QStringLiteral("type")).toString() == QLatin1String("cloudflare");
    if (cloudflare) {
        // no address of its own: the host's address is the Cloudflare hostname.
        // username/password hold the optional Access service token (id/secret).
        o.insert(QStringLiteral("host"), QStringLiteral("cloudflared"));
        o.insert(QStringLiteral("port"), 1);
        if (o.value(QStringLiteral("name")).toString().trimmed().isEmpty())
            o.insert(QStringLiteral("name"), QStringLiteral("Cloudflare Tunnel"));
    }
    if (o.value(QStringLiteral("type")).toString() == QLatin1String("tor")) {
        o.insert(QStringLiteral("host"), QStringLiteral("127.0.0.1"));
        const QString mode = o.value(QStringLiteral("torMode")).toString(QStringLiteral("auto"));
        o.insert(QStringLiteral("port"), mode == QLatin1String("browser") ? TorManager::kBrowserPort
                                         : mode == QLatin1String("exe") ? TorManager::kOwnPort : TorManager::kServicePort);
        o.remove(QStringLiteral("username"));
        o.remove(QStringLiteral("password"));
    }
    if (o.value(QStringLiteral("type")).toString() == QLatin1String("shadowsocks")) {
        if (!shadowsocks::isSupported(o.value(QStringLiteral("method")).toString()))
            return tr("Choose the Shadowsocks cipher.");
        if (const QString p = shadowsocks::keyProblem(o.value(QStringLiteral("method")).toString(),
                                                      o.value(QStringLiteral("password")).toString()); !p.isEmpty())
            return p;
        o.remove(QStringLiteral("username"));
    } else {
        o.remove(QStringLiteral("method"));
    }
    const QString host = o.value(QStringLiteral("host")).toString().trimmed();
    const int port = o.value(QStringLiteral("port")).toVariant().toInt();
    if (host.isEmpty())
        return tr("Enter the proxy address.");
    if (port < 1 || port > 65535)
        return tr("Port must be between 1 and 65535.");
    o.insert(QStringLiteral("host"), host);
    o.insert(QStringLiteral("port"), port);
    if (o.value(QStringLiteral("name")).toString().trimmed().isEmpty())
        o.insert(QStringLiteral("name"), QStringLiteral("%1:%2").arg(host).arg(port));
    // Never persist transient check results.
    for (const char *k : {"checkState", "exitIp", "location", "exitCountry", "latencyMs", "checkError", "checkedAt", "hasPassword",
                          "checkSummary", "checkHint", "checkErrors"})
        o.remove(QString::fromLatin1(k));

    QJsonArray list = section(kProxies);
    QString id = o.value(QStringLiteral("id")).toString();
    bool replaced = false;
    for (int i = 0; !id.isEmpty() && i < list.size(); ++i) {
        if (list.at(i).toObject().value(QStringLiteral("id")).toString() == id) {
            if (!o.contains(QStringLiteral("disabled")) && list.at(i).toObject().value(QStringLiteral("disabled")).toBool())
                o.insert(QStringLiteral("disabled"), true);   // editing does not switch it back on
            list.replace(i, o);
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        id = newId();
        o.insert(QStringLiteral("id"), id);
        list.append(o);
    }
    setSection(kProxies, list);
    if (!persist())
        return tr("Could not save the vault.");
    logEvent(ActivityLog::Info, QStringLiteral("proxy"), replaced ? tr("Proxy changed") : tr("Proxy added"),
             QStringLiteral("%1 %2:%3").arg(o.value(QStringLiteral("type")).toString().toUpper(), host).arg(port),
             o.value(QStringLiteral("name")).toString());
    checkProxy(id);
    return {};
}

void AppController::deleteProxy(const QString &id)
{
    logEvent(ActivityLog::Warning, QStringLiteral("proxy"), tr("Proxy deleted"), QString(),
             findIn(kProxies, id).value(QStringLiteral("name")).toString());
    QJsonArray list = section(kProxies);
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).toObject().value(QStringLiteral("id")).toString() == id) {
            list.removeAt(i);
            break;
        }
    }
    setSection(kProxies, list);
    QJsonArray hosts = section(kHosts);
    for (int i = 0; i < hosts.size(); ++i) {
        QJsonObject h = hosts.at(i).toObject();
        if (h.value(QStringLiteral("proxyId")).toString() == id) {
            h.insert(QStringLiteral("proxyId"), QString());
            hosts.replace(i, h);
        }
    }
    setSection(kHosts, hosts);
    if (m_settings.value(QStringLiteral("defaultProxyId")).toString() == id)
        setSetting(QStringLiteral("defaultProxyId"), QString());
    persist();
}

void AppController::checkProxy(const QString &id)
{
    const QJsonObject p = findIn(kProxies, id);
    if (p.isEmpty())
        return;
    m_proxies.setTransient(id, {{QStringLiteral("checkState"), QStringLiteral("checking")},
                                {QStringLiteral("checkError"), QString()}, {QStringLiteral("checkSummary"), QString()},
                                {QStringLiteral("checkHint"), QString()}, {QStringLiteral("checkErrors"), QVariant()}});
    if (p.value(QStringLiteral("type")).toString() == QLatin1String("tor")
        && m_tor.needsStart(p.value(QStringLiteral("torMode")).toString())) {
        if (m_tor.state() != QLatin1String("starting"))
            m_tor.start(p.value(QStringLiteral("torPath")).toString());
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(&m_tor, &TorManager::changed, this, [this, id, conn] {
            const QString st = m_tor.state();
            if (st == QLatin1String("ready")) { disconnect(*conn); checkProxy(id); }
            else if (st == QLatin1String("error") || st == QLatin1String("off")) {
                disconnect(*conn);
                m_proxies.setTransient(id, {{QStringLiteral("checkState"), QStringLiteral("fail")},
                                            {QStringLiteral("checkError"), m_tor.message()}});
            }
        });
        return;
    }
    if (p.value(QStringLiteral("type")).toString() == QLatin1String("cloudflare")) {
        const QVariantMap r = cloudflaredCheck(p.value(QStringLiteral("cfPath")).toString());
        m_proxies.setTransient(id, {{QStringLiteral("checkState"), r.value(QStringLiteral("ok")).toBool() ? QStringLiteral("ok") : QStringLiteral("fail")},
                                    {QStringLiteral("location"), r.value(QStringLiteral("location"))},
                                    {QStringLiteral("checkError"), r.value(QStringLiteral("error"))},
                                    {QStringLiteral("checkSummary"), r.value(QStringLiteral("summary"))},
                                    {QStringLiteral("checkHint"), r.value(QStringLiteral("hint"))},
                                    {QStringLiteral("checkErrors"), r.value(QStringLiteral("errors"))}});
        return;
    }
    m_checker.check(proxyConfig(p), [this, id](const QVariantMap &r) { storeCheck(id, r); });
}


QVariantMap AppController::cloudflaredCheck(const QString &customPath) const
{
    QVariantMap r;
    const QString exe = CloudflaredManager::findExe(customPath);
    const QString ver = CloudflaredManager::version(exe);
    r.insert(QStringLiteral("cloudflare"), true);
    r.insert(QStringLiteral("exe"), exe);
    r.insert(QStringLiteral("version"), ver);
    r.insert(QStringLiteral("ok"), !ver.isEmpty());
    if (exe.isEmpty())
        r.insert(QStringLiteral("error"), tr("cloudflared was not found. Install it (winget install Cloudflare.cloudflared) or choose cloudflared.exe."));
    else if (ver.isEmpty())
        r.insert(QStringLiteral("error"), tr("%1 does not run.").arg(QDir::toNativeSeparators(exe)));
    else
        r.insert(QStringLiteral("location"), tr("via Cloudflare · %1").arg(ver));
    return r;
}

void AppController::storeCheck(const QString &proxyId, const QVariantMap &r)
{
    const bool ok = r.value(QStringLiteral("ok")).toBool();
    m_proxies.setTransient(proxyId, {
        {QStringLiteral("checkState"), ok ? QStringLiteral("ok") : QStringLiteral("fail")},
        {QStringLiteral("exitIp"), r.value(QStringLiteral("ip"))},
        {QStringLiteral("location"), locationOf(r)},
        {QStringLiteral("exitCountry"), r.value(QStringLiteral("country"))},
        {QStringLiteral("latencyMs"), r.value(QStringLiteral("latencyMs"))},
        {QStringLiteral("checkError"), r.value(QStringLiteral("error"))},
        {QStringLiteral("checkSummary"), r.value(QStringLiteral("summary"))},
        {QStringLiteral("checkHint"), r.value(QStringLiteral("hint"))},
        {QStringLiteral("checkErrors"), r.value(QStringLiteral("errors"))},
        {QStringLiteral("checkedAt"), r.value(QStringLiteral("checkedAt"))},
    });
}

void AppController::checkProxyDraft(const QVariantMap &draft)
{
    if (draft.value(QStringLiteral("type")).toString() == QLatin1String("cloudflare")) {
        emit proxyDraftChecked(cloudflaredCheck(draft.value(QStringLiteral("cfPath")).toString()));
        return;
    }
    if (draft.value(QStringLiteral("type")).toString() == QLatin1String("tor")
        && m_tor.needsStart(draft.value(QStringLiteral("torMode")).toString())) {
        if (m_tor.state() != QLatin1String("starting"))
            m_tor.start(draft.value(QStringLiteral("torPath")).toString());
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(&m_tor, &TorManager::changed, this, [this, draft, conn] {
            const QString st = m_tor.state();
            if (st == QLatin1String("ready")) { disconnect(*conn); checkProxyDraft(draft); }
            else if (st == QLatin1String("error") || st == QLatin1String("off")) {
                disconnect(*conn);
                emit proxyDraftChecked({{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_tor.message()}});
            }
        });
        return;
    }
    const ProxyConfig c = proxyConfig(QJsonObject::fromVariantMap(draft));
    if (!c.isSet()) {
        emit proxyDraftChecked({{QStringLiteral("ok"), false},
                                {QStringLiteral("error"), tr("Enter the proxy address and port.")}});
        return;
    }
    m_checker.check(c, [this](const QVariantMap &r) {
        QVariantMap out = r;
        out.insert(QStringLiteral("location"), locationOf(r));
        emit proxyDraftChecked(out);
    });
}

void AppController::checkDirectIp()
{
    m_directIp = {{QStringLiteral("checking"), true}};
    emit directIpChanged();
    m_checker.check(ProxyConfig{}, [this](const QVariantMap &r) {
        m_directIp = r;
        m_directIp.insert(QStringLiteral("location"), locationOf(r));
        emit directIpChanged();
    });
}

QString AppController::effectiveProxyId(const QJsonObject &host) const
{
    // A proxy that is turned off counts as "no proxy": the host connects directly,
    // or, with the kill switch on, through the default proxy (never directly).
    const QString own = host.value(QStringLiteral("proxyId")).toString();
    if (proxyUsable(own))
        return own;
    if (m_settings.value(QStringLiteral("killSwitch"), false).toBool()) {
        const QString def = m_settings.value(QStringLiteral("defaultProxyId")).toString();
        if (proxyUsable(def))
            return def;
    }
    return {};
}

bool AppController::proxyUsable(const QString &id) const
{
    if (id.isEmpty())
        return false;
    const QJsonObject p = findIn(kProxies, id);
    return !p.isEmpty() && !p.value(QStringLiteral("disabled")).toBool();
}

void AppController::setProxyEnabled(const QString &id, bool on)
{
    if (!m_vault.isUnlocked())
        return;
    QJsonArray list = section(kProxies);
    for (int i = 0; i < list.size(); ++i) {
        QJsonObject o = list.at(i).toObject();
        if (o.value(QStringLiteral("id")).toString() != id)
            continue;
        if (on)
            o.remove(QStringLiteral("disabled"));
        else
            o.insert(QStringLiteral("disabled"), true);
        list.replace(i, o);
        setSection(kProxies, list);
        persist();
        logEvent(ActivityLog::Info, QStringLiteral("proxy"), on ? tr("Proxy turned on") : tr("Proxy turned off"),
                 on ? QString() : tr("hosts that use it connect directly (or through the default proxy while the kill switch is on)"),
                 o.value(QStringLiteral("name")).toString());
        if (on)
            checkProxy(id);
        else
            m_proxies.setTransient(id, {{QStringLiteral("checkState"), QString()}});
        return;
    }
}

int AppController::hostsUsingProxy(const QString &id) const
{
    int n = 0;
    const QJsonArray hosts = section(kHosts);
    for (const QJsonValue &v : hosts)
        if (v.toObject().value(QStringLiteral("proxyId")).toString() == id)
            ++n;
    if (m_settings.value(QStringLiteral("defaultProxyId")).toString() == id)
        ++n;
    return n;
}

void AppController::killSessionsVia(const QString &proxyId, const QString &reason)
{
    int n = 0;
    for (TerminalSession *s : m_sessions.items()) {
        if (s->isActive() && s->viaProxy() && s->proxyId() == proxyId) {
            s->kill(reason);
            ++n;
        }
    }
    if (n) {
        emit notify(reason, true);
        logEvent(ActivityLog::Security, QStringLiteral("killswitch"), tr("Kill switch fired"), reason);
    }
}

// Every few seconds: TCP + SOCKS handshake with each proxy that carries an
// open session. Runs on the thread pool; no traffic leaves the proxy.
void AppController::runProbe()
{
    if (!m_settings.value(QStringLiteral("killSwitch"), false).toBool())
        return;
    QSet<QString> used;
    for (TerminalSession *s : m_sessions.items()) {
        if (s->isActive() && s->viaProxy() && !s->proxyId().isEmpty())
            used.insert(s->proxyId());
    }
    for (const QString &proxyId : used) {
        if (m_probing.contains(proxyId))
            continue;
        const QJsonObject p = findIn(kProxies, proxyId);
        if (p.isEmpty())
            continue; // vault locked: the tunnel itself still enforces "no fallback"
        if (p.value(QStringLiteral("type")).toString() == QLatin1String("cloudflare"))
            continue; // a local cloudflared, not a proxy: its session ends when it stops
        m_probing.insert(proxyId);
        const ProxyConfig cfg = proxyConfig(p);
        const QString name = p.value(QStringLiteral("name")).toString();
        QPointer<AppController> self(this);
        QThreadPool::globalInstance()->start([self, cfg, proxyId, name] {
            QString err;
            const bool ok = proxytunnel::probe(cfg, 2500, &err);
            if (!self)
                return;
            QMetaObject::invokeMethod(self, [self, ok, err, proxyId, name] {
                if (!self)
                    return;
                self->m_probing.remove(proxyId);
                if (ok)
                    return;
                self->m_proxies.setTransient(proxyId, {{QStringLiteral("checkState"), QStringLiteral("fail")},
                                                       {QStringLiteral("checkError"), err}});
                self->killSessionsVia(proxyId, tr("Kill switch: proxy \"%1\" stopped responding (%2). Session closed.")
                                                    .arg(name, err.isEmpty() ? tr("no answer") : err));
            }, Qt::QueuedConnection);
        });
    }
}

// Every minute: ask an IP service *through* each used proxy whether it still
// hides the real address.
void AppController::runExitIpCheck()
{
    if (!m_settings.value(QStringLiteral("killSwitch"), false).toBool())
        return;
    QSet<QString> used;
    for (TerminalSession *s : m_sessions.items()) {
        if (s->isActive() && s->viaProxy() && !s->proxyId().isEmpty())
            used.insert(s->proxyId());
    }
    for (const QString &proxyId : used) {
        const QJsonObject p = findIn(kProxies, proxyId);
        if (p.isEmpty())
            continue;
        if (p.value(QStringLiteral("type")).toString() == QLatin1String("cloudflare"))
            continue;
        const QString name = p.value(QStringLiteral("name")).toString();
        m_checker.check(proxyConfig(p), [this, proxyId, name](const QVariantMap &r) {
            storeCheck(proxyId, r);
            const bool ok = r.value(QStringLiteral("ok")).toBool();
            if (!ok) {
                killSessionsVia(proxyId, tr("Kill switch: proxy \"%1\" stopped responding (%2). Session closed.")
                                             .arg(name, r.value(QStringLiteral("error")).toString()));
            } else if (m_directIp.value(QStringLiteral("ok")).toBool()
                       && r.value(QStringLiteral("ip")) == m_directIp.value(QStringLiteral("ip"))) {
                killSessionsVia(proxyId, tr("Kill switch: proxy \"%1\" now exits from your real IP. Session closed.").arg(name));
            }
        });
    }
}

// ---------------------------------------------------------------------------
// firewall lock
// ---------------------------------------------------------------------------

QString AppController::applyFirewall()
{
    QStringList hosts;
    for (const QJsonValue &v : section(kProxies)) {
        const QString h = v.toObject().value(QStringLiteral("host")).toString().trimmed();
        if (!h.isEmpty())
            hosts << h;
    }
    QString err;
    if (!firewalllock::apply(hosts, &err)) {
        logEvent(ActivityLog::Error, QStringLiteral("killswitch"), tr("Firewall lock failed"), err);
        return err;
    }
    m_firewallActive = true;
    m_settings.setValue(QStringLiteral("firewallLock"), true);
    emit firewallChanged();
    emit settingsChanged();
    logEvent(ActivityLog::Security, QStringLiteral("killswitch"), tr("Firewall lock enabled"), hosts.join(QStringLiteral(", ")));
    return {};
}

QString AppController::removeFirewall()
{
    QString err;
    if (!firewalllock::remove(&err))
        return err;
    m_firewallActive = false;
    m_settings.setValue(QStringLiteral("firewallLock"), false);
    emit firewallChanged();
    emit settingsChanged();
    logEvent(ActivityLog::Warning, QStringLiteral("killswitch"), tr("Firewall lock removed"));
    return {};
}
