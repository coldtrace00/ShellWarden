// Part of AppController: opening sessions: routes, challenges, connect parameters.
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
// sessions
// ---------------------------------------------------------------------------

TerminalSession *AppController::createSession(const QString &hostId, const QString &label,
                                              const QString &endpoint, const QString &kind)
{
    auto *s = new TerminalSession(hostId, label, endpoint, kind, this);
    s->setThemeId(m_settings.value(QStringLiteral("terminalTheme"), QStringLiteral("default")).toString());
    connect(s, &TerminalSession::saveSecretRequested, this, &AppController::onSecretVerified);
    connect(s, &TerminalSession::logEvent, this,
            [this, s](int level, const QString &cat, const QString &msg, const QString &detail) {
                logEvent(level, cat, msg, detail, s->label());
            });
    connect(s, &TerminalSession::commandEntered, this, [this, s](const QString &cmd) { rememberCommand(s->hostId(), cmd); });
    m_sessions.append(s);
    emit sessionOpened(m_sessions.count() - 1);
    return s;
}

void AppController::openHost(const QString &id)
{
    const QJsonObject h = findIn(kHosts, id);
    if (h.isEmpty())
        return;
    if (needsChallenge(h)) {   // "ask the master password to connect"
        m_pendingChallenge.insert(id, nullptr);
        emit connectChallenge(id, h.value(QStringLiteral("label")).toString());
        return;
    }
    openHostNow(id);
}

QString AppController::answerChallenge(const QString &hostId, const QString &masterPassword)
{
    const QJsonObject h = findIn(kHosts, hostId);
    if (h.isEmpty())
        return tr("This host no longer exists.");
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_challengeLockedUntil.value(hostId) > now)
        return tr("Too many wrong attempts. Try again in %1 s.").arg((m_challengeLockedUntil.value(hostId) - now) / 1000 + 1);

    const QString label = h.value(QStringLiteral("label")).toString();
    QString err;
    Opened opened;
    if (protectPublicKey().size() == protect::kPublicLen)
        opened = openFor({hostId}, masterPassword, &err);   // proves the password and opens the sealed secrets
    else if (!m_vault.verifyPassword(masterPassword))
        err = tr("Wrong master password.");
    if (!err.isEmpty()) {
        const int fails = m_challengeFails.value(hostId) + 1;
        m_challengeFails.insert(hostId, fails);
        if (fails >= 5) {
            m_challengeLockedUntil.insert(hostId, now + 30000);
            m_challengeFails.insert(hostId, 0);
        }
        logEvent(ActivityLog::Security, QStringLiteral("session"), tr("Connection check failed"), err, label);
        return err;
    }
    m_challengeFails.remove(hostId);
    logEvent(ActivityLog::Security, QStringLiteral("session"), tr("Connection check passed (master password)"), QString(), label);
    const QPointer<TerminalSession> pending = m_pendingChallenge.take(hostId);
    if (pending) {
        pending->setOpenedSecrets(opened);
        startSession(pending, h);
    } else {
        openHostNow(hostId, opened);
    }
    wipe(opened);
    return {};
}

void AppController::cancelChallenge(const QString &hostId)
{
    const QPointer<TerminalSession> pending = m_pendingChallenge.take(hostId);
    m_nextRoute.remove(hostId);
    if (pending)
        pending->abort(tr("Connection cancelled: the master password was not entered."));
}

void AppController::openHostVia(const QString &id, const QString &route)
{
    m_nextRoute.insert(id, route.isEmpty() ? QStringLiteral("direct") : route);
    openHost(id);
}

QString AppController::hostRoute(const QString &id) const
{
    const QString p = findIn(kHosts, id).value(QStringLiteral("proxyId")).toString();
    return p.isEmpty() || findIn(kProxies, p).isEmpty() ? QStringLiteral("direct") : p;
}

QString AppController::setHostRoute(const QString &id, const QString &route)
{
    QJsonArray hosts = section(kHosts);
    for (int i = 0; i < hosts.size(); ++i) {
        QJsonObject h = hosts.at(i).toObject();
        if (h.value(QStringLiteral("id")).toString() != id)
            continue;
        const QString proxyId = route == QLatin1String("direct") ? QString() : route;
        if (!proxyId.isEmpty() && findIn(kProxies, proxyId).isEmpty())
            return tr("This proxy no longer exists.");
        h.insert(QStringLiteral("proxyId"), proxyId);
        hosts.replace(i, h);
        setSection(kHosts, hosts);
        persist();
        reloadModels();
        const QString via = proxyId.isEmpty() ? tr("direct") : findIn(kProxies, proxyId).value(QStringLiteral("name")).toString();
        logEvent(ActivityLog::Info, QStringLiteral("host"), tr("Route of the host changed: %1").arg(via),
                 QString(), h.value(QStringLiteral("label")).toString());
        emit notify(tr("%1 now connects: %2").arg(h.value(QStringLiteral("label")).toString(), via), false);
        return {};
    }
    return tr("This host no longer exists.");
}

void AppController::openHostNow(const QString &id, const Opened &opened)
{
    const QJsonObject h = findIn(kHosts, id);
    if (h.isEmpty())
        return;
    const bool serial = h.value(QStringLiteral("type")).toString() == QLatin1String("serial");
    const bool telnet = h.value(QStringLiteral("type")).toString() == QLatin1String("telnet");
    const QString endpoint = telnet
        ? QStringLiteral("telnet://%1:%2").arg(h.value(QStringLiteral("hostname")).toString()).arg(h.value(QStringLiteral("port")).toInt(23))
        : serial
        ? QStringLiteral("%1 · %2").arg(h.value(QStringLiteral("serialPort")).toString())
              .arg(h.value(QStringLiteral("baud")).toInt(115200))
        : QStringLiteral("%1@%2:%3").arg(h.value(QStringLiteral("username")).toString(),
                                         h.value(QStringLiteral("hostname")).toString())
              .arg(h.value(QStringLiteral("port")).toInt());
    TerminalSession *s = createSession(id, h.value(QStringLiteral("label")).toString(), endpoint,
                                       serial ? QStringLiteral("serial") : telnet ? QStringLiteral("telnet") : QStringLiteral("ssh"));
    s->setRouteOverride(m_nextRoute.take(id));   // kept for reconnects of this tab
    connect(s, &TerminalSession::reconnectRequested, this, [this, s] {
        const QJsonObject fresh = findIn(kHosts, s->hostId());
        if (fresh.isEmpty()) {
            s->abort(m_vault.isUnlocked() ? tr("This host no longer exists.") : tr("Unlock the vault to reconnect."));
            return;
        }
        if (needsChallenge(fresh)) {
            m_pendingChallenge.insert(s->hostId(), s);
            emit connectChallenge(s->hostId(), fresh.value(QStringLiteral("label")).toString());
            return;
        }
        startSession(s, fresh);
    });
    s->setOpenedSecrets(opened);
    startSession(s, h);
}

QString AppController::quickConnect(const QString &spec)
{
    static const QRegularExpression re(QStringLiteral(R"(^\s*(?:([^@\s]+)@)?(\[[^\]]+\]|[^:\s]+)(?::(\d{1,5}))?\s*$)"));
    const auto m = re.match(spec);
    if (!m.hasMatch())
        return tr("Use the form user@host or user@host:port");
    QString hostName = m.captured(2);
    if (hostName.startsWith(QLatin1Char('[')))
        hostName = hostName.mid(1, hostName.size() - 2);
    const int port = m.captured(3).isEmpty() ? 22 : m.captured(3).toInt();
    if (port < 1 || port > 65535)
        return tr("Invalid port.");

    QJsonObject h;
    h.insert(QStringLiteral("hostname"), hostName);
    h.insert(QStringLiteral("port"), port);
    h.insert(QStringLiteral("username"), m.captured(1));
    h.insert(QStringLiteral("label"), hostName);

    const QString endpoint = QStringLiteral("%1@%2:%3").arg(m.captured(1), hostName).arg(port);
    TerminalSession *s = createSession(QString(), hostName, endpoint, QStringLiteral("ssh"));
    connect(s, &TerminalSession::reconnectRequested, this, [this, s, h] { startSession(s, h); });
    startSession(s, h);
    return {};
}

QString AppController::openSerial(const QString &portText, int baud)
{
    const QString port = portText.section(QStringLiteral(" — "), 0, 0).trimmed();
    if (port.isEmpty())
        return tr("Choose a serial port.");
    QJsonObject h{{QStringLiteral("type"), QStringLiteral("serial")}, {QStringLiteral("serialPort"), port},
                  {QStringLiteral("baud"), baud}, {QStringLiteral("label"), port}};
    TerminalSession *s = createSession(QString(), port, QStringLiteral("%1 · %2").arg(port).arg(baud), QStringLiteral("serial"));
    connect(s, &TerminalSession::reconnectRequested, this, [this, s, h] { startSession(s, h); });
    startSession(s, h);
    return {};
}

void AppController::closeSession(int index)
{
    if (TerminalSession *s = m_sessions.takeAt(index))
        s->deleteLater();
}

void AppController::moveSession(int from, int to)
{
    m_sessions.move(from, to);
}

void AppController::duplicateSession(int index)
{
    TerminalSession *s = m_sessions.at(index);
    if (!s)
        return;
    if (!s->hostId().isEmpty())
        openHost(s->hostId());
    else if (s->kind() == QLatin1String("local"))
        openLocalTerminal(QString());
    else if (s->kind() == QLatin1String("ssh"))
        quickConnect(s->endpoint());
}

// Address, user, secrets and jump host of a saved host (no proxy, no tunnels).
ConnectParams AppController::paramsFor(const QJsonObject &host, const QJsonObject &jumpHost, const Opened &opened) const
{
    ConnectParams p;
    p.host = host.value(QStringLiteral("hostname")).toString();
    p.port = host.value(QStringLiteral("port")).toInt(22);
    p.username = host.value(QStringLiteral("username")).toString();
    if (p.username.isEmpty())
        p.username = qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER", QStringLiteral("root")));
    p.password = secretOf(host, QStringLiteral("password"), opened);
    p.knownHostsPath = m_knownHostsPath;
    p.keepaliveSeconds = m_settings.value(QStringLiteral("keepaliveSeconds"), 30).toInt();
    p.useAgent = host.value(QStringLiteral("useAgent")).toBool();
    p.agentForward = host.value(QStringLiteral("agentForward")).toBool();

    const QJsonObject key = findIn(kKeys, host.value(QStringLiteral("keyId")).toString());
    if (!key.isEmpty()) {
        p.privateKey = secretOf(key, QStringLiteral("privateKey"), opened).toUtf8();
        p.keyPassphrase = host.value(QStringLiteral("alwaysAskPassphrase")).toBool()
                              ? QString() : secretOf(key, QStringLiteral("passphrase"), opened);
    }
    QString jumpLabel;
    if (!jumpHost.isEmpty()) {
        p.jump.enabled = true;
        jumpLabel = jumpHost.value(QStringLiteral("label")).toString();
        p.jump.label = jumpLabel;
        p.jump.host = jumpHost.value(QStringLiteral("hostname")).toString();
        p.jump.port = jumpHost.value(QStringLiteral("port")).toInt(22);
        p.jump.username = jumpHost.value(QStringLiteral("username")).toString();
        if (p.jump.username.isEmpty())
            p.jump.username = p.username;
        p.jump.password = secretOf(jumpHost, QStringLiteral("password"), opened);
        const QJsonObject jk = findIn(kKeys, jumpHost.value(QStringLiteral("keyId")).toString());
        if (!jk.isEmpty()) {
            p.jump.privateKey = secretOf(jk, QStringLiteral("privateKey"), opened).toUtf8();
            p.jump.keyPassphrase = secretOf(jk, QStringLiteral("passphrase"), opened);
        }
    }
    return p;
}

void AppController::startSession(TerminalSession *s, const QJsonObject &host)
{
    s->setAutoReconnect(m_settings.value(QStringLiteral("autoReconnect"), true).toBool());
    s->setMonitorConfig(m_settings.value(QStringLiteral("monitorEnabled"), true).toBool(),
                        m_settings.value(QStringLiteral("monitorInterval"), 5).toInt());
    s->setSshfpCheck(m_settings.value(QStringLiteral("sshfpCheck"), true).toBool());
    const QString theme = host.value(QStringLiteral("theme")).toString();
    s->setThemeId(theme.isEmpty() ? m_settings.value(QStringLiteral("terminalTheme"), QStringLiteral("default")).toString()
                                  : theme);
    s->setColor(host.value(QStringLiteral("color")).toString());

    if (host.value(QStringLiteral("type")).toString() == QLatin1String("serial")) {
        // A local cable: no network, so neither proxy nor kill switch apply.
        s->setRoute(tr("Serial"), false, QString(), false);
        s->beginSerial(host.value(QStringLiteral("serialPort")).toString(), host.value(QStringLiteral("baud")).toInt(115200),
                       host.value(QStringLiteral("dataBits")).toInt(8), host.value(QStringLiteral("parity")).toString(),
                       host.value(QStringLiteral("stopBits")).toInt(1), host.value(QStringLiteral("flow")).toString());
        return;
    }

    const bool killSwitch = m_settings.value(QStringLiteral("killSwitch"), false).toBool();
    const QJsonObject jumpHost = findIn(kHosts, host.value(QStringLiteral("jumpHostId")).toString());
    // With a jump host, the proxy rule applies to the first hop.
    // "Connect via" from the right-click menu wins for this tab.
    const QString override = s->routeOverride();
    const QString proxyId = override == QLatin1String("direct") ? QString()
                          : proxyUsable(override) ? override
                          : effectiveProxyId(jumpHost.isEmpty() ? host : jumpHost);

    if (killSwitch && proxyId.isEmpty()) {
        const QJsonObject own = findIn(kProxies, (jumpHost.isEmpty() ? host : jumpHost).value(QStringLiteral("proxyId")).toString());
        s->setRoute(tr("Blocked by kill switch"), false, QString(), true);
        if (!own.isEmpty() && own.value(QStringLiteral("disabled")).toBool())
            s->abort(tr("Kill switch is on: proxy \"%1\" is turned off and no default proxy is set. "
                        "Direct connection blocked.").arg(own.value(QStringLiteral("name")).toString()));
        else
            s->abort(tr("Kill switch is on: this host has no proxy and no default proxy is set. "
                        "Direct connection blocked."));
        return;
    }

    if (host.value(QStringLiteral("type")).toString() == QLatin1String("telnet")) {
        // plain TCP; a proxy (SOCKS5 / HTTP / Tor) is used when one is set
        const QJsonObject px = findIn(kProxies, proxyId);
        if (px.value(QStringLiteral("type")).toString() == QLatin1String("cloudflare")) {
            s->abort(tr("The Cloudflare Tunnel route is for SSH hosts."));
            return;
        }
        if (px.value(QStringLiteral("type")).toString() == QLatin1String("shadowsocks")) {
            s->abort(tr("The Shadowsocks route is for SSH hosts."));
            return;
        }
        if (px.value(QStringLiteral("type")).toString() == QLatin1String("tor")
            && m_tor.needsStart(px.value(QStringLiteral("torMode")).toString())) {
            m_tor.start(px.value(QStringLiteral("torPath")).toString());
            s->abort(tr("Tor is starting — press Reconnect in a moment."));
            return;
        }
        const ProxyConfig pc = proxyId.isEmpty() ? ProxyConfig{} : proxyConfig(px);
        s->setRoute(proxyId.isEmpty() ? tr("Telnet · direct · not encrypted")
                                      : tr("Telnet · %1 · not encrypted").arg(px.value(QStringLiteral("name")).toString()),
                    !proxyId.isEmpty(), proxyId, killSwitch);
        s->beginTelnet(host.value(QStringLiteral("hostname")).toString(), host.value(QStringLiteral("port")).toInt(23), pc);
        return;
    }

    // Tor started by ShellWarden itself: start it first, then come back here.
    {
        const QJsonObject px = findIn(kProxies, proxyId);
        if (px.value(QStringLiteral("type")).toString() == QLatin1String("tor")
            && m_tor.needsStart(px.value(QStringLiteral("torMode")).toString())) {
            s->setRoute(QStringLiteral("Tor · %1").arg(px.value(QStringLiteral("name")).toString()), true, proxyId, killSwitch);
            s->setPreparing(tr("Starting Tor…"));
            QPointer<TerminalSession> guard(s);
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(&m_tor, &TorManager::changed, this, [this, guard, host, conn] {
                if (!guard || guard->state() != TerminalSession::Preparing) { disconnect(*conn); return; }
                const QString st = m_tor.state();
                if (st == QLatin1String("ready")) { disconnect(*conn); startSession(guard, host); }
                else if (st == QLatin1String("error")) { disconnect(*conn); guard->abort(tr("Tor: %1").arg(m_tor.message())); }
                else guard->setPreparing(m_tor.message());
            });
            m_tor.start(px.value(QStringLiteral("torPath")).toString());
            return;
        }
    }

    // Cloudflare Tunnel: start cloudflared for the first hop's hostname, then come back here.
    const QString firstHop = (jumpHost.isEmpty() ? host : jumpHost).value(QStringLiteral("hostname")).toString();
    {
        const QJsonObject px = findIn(kProxies, proxyId);
        if (px.value(QStringLiteral("type")).toString() == QLatin1String("cloudflare") && !m_cf.isReady(firstHop)) {
            s->setRoute(cfRouteName(px), true, proxyId, killSwitch);
            s->setPreparing(tr("Starting cloudflared… (the first time a browser window may ask you to log in to Cloudflare Access)"));
            QPointer<TerminalSession> guard(s);
            const QString key = firstHop.trimmed().toLower();
            auto okConn = std::make_shared<QMetaObject::Connection>();
            auto failConn = std::make_shared<QMetaObject::Connection>();
            *okConn = connect(&m_cf, &CloudflaredManager::ready, this, [this, guard, host, key, okConn, failConn](const QString &h, int) {
                if (h != key) return;
                disconnect(*okConn); disconnect(*failConn);
                if (guard && guard->state() == TerminalSession::Preparing) startSession(guard, host);
            });
            *failConn = connect(&m_cf, &CloudflaredManager::failed, this, [guard, key, okConn, failConn](const QString &h, const QString &e) {
                if (h != key) return;
                disconnect(*okConn); disconnect(*failConn);
                if (guard) guard->abort(tr("Cloudflare Tunnel: %1").arg(e));
            });
            QString err;
            if (!m_cf.start(firstHop, px.value(QStringLiteral("cfPath")).toString(), px.value(QStringLiteral("username")).toString(),
                            px.value(QStringLiteral("password")).toString(), &err)) {
                disconnect(*okConn); disconnect(*failConn);
                s->abort(tr("Cloudflare Tunnel: %1").arg(err));
            }
            return;
        }
    }

    Opened opened = s->takeOpenedSecrets();   // a protected host: opened with the master password just now
    if (opened.isEmpty() && (isSealed(host) || isSealed(findIn(kKeys, host.value(QStringLiteral("keyId")).toString()))
                             || isSealed(jumpHost) || isSealed(findIn(kKeys, jumpHost.value(QStringLiteral("keyId")).toString())))) {
        s->abort(tr("This host asks for the master password."));
        return;
    }
    ConnectParams p = paramsFor(host, jumpHost, opened);
    wipe(opened);
    p.killSwitch = killSwitch;
    s->setCanSaveSecrets(m_vault.isUnlocked() && !s->hostId().isEmpty());

    for (const QJsonValue &v : host.value(QStringLiteral("forwards")).toArray()) {
        const QJsonObject f = v.toObject();
        if (f.contains(QStringLiteral("enabled")) && !f.value(QStringLiteral("enabled")).toBool())
            continue;
        ForwardSpec spec;
        spec.id = f.value(QStringLiteral("id")).toString();
        const QString t = f.value(QStringLiteral("type")).toString();
        spec.type = t == QLatin1String("R") ? ForwardSpec::Remote : t == QLatin1String("D") ? ForwardSpec::Dynamic : ForwardSpec::Local;
        spec.bindHost = f.value(QStringLiteral("bindHost")).toString(QStringLiteral("127.0.0.1"));
        spec.bindPort = f.value(QStringLiteral("bindPort")).toVariant().toInt();
        spec.destHost = f.value(QStringLiteral("destHost")).toString();
        spec.destPort = f.value(QStringLiteral("destPort")).toVariant().toInt();
        p.forwards << spec;
    }

    const QString jumpLabel = p.jump.enabled ? p.jump.label : QString();
    const QString viaJump = jumpLabel.isEmpty() ? QString() : tr(" → jump %1").arg(jumpLabel);

    if (proxyId.isEmpty()) {
        const QJsonObject off = findIn(kProxies, (jumpHost.isEmpty() ? host : jumpHost).value(QStringLiteral("proxyId")).toString());
        const QString direct = !off.isEmpty() && off.value(QStringLiteral("disabled")).toBool() && override != QLatin1String("direct")
                                   ? tr("Direct · proxy \"%1\" is off").arg(off.value(QStringLiteral("name")).toString())
                                   : tr("Direct");
        s->setRoute(direct + viaJump, false, QString(), false, jumpLabel);
        s->setPreparing(tr("Connecting…"));
        s->setExpected(QString(), tr("checking…"));
        QPointer<TerminalSession> guard(s);
        m_checker.check(ProxyConfig{}, [this, guard](const QVariantMap &r) {
            m_directIp = r;
            m_directIp.insert(QStringLiteral("location"), locationOf(r));
            emit directIpChanged();
            if (guard)
                guard->setExpected(r.value(QStringLiteral("ip")).toString(),
                                   r.value(QStringLiteral("ok")).toBool() ? locationOf(r) : tr("unknown"),
                                   r.value(QStringLiteral("country")).toString());
        });
        s->begin(p);
        return;
    }

    const QJsonObject px = findIn(kProxies, proxyId);
    p.proxy = proxyConfig(px, firstHop);
    if (px.value(QStringLiteral("type")).toString() == QLatin1String("cloudflare")) {
        if (!p.proxy.isSet()) {   // cloudflared stopped meanwhile: never fall back to direct
            s->abort(tr("Cloudflare Tunnel: cloudflared is not running. Press Reconnect."));
            return;
        }
        p.connectTimeoutSeconds = 60;   // a Cloudflare Access login may be waiting in the browser
        s->setRoute(cfRouteName(px) + viaJump,
                    true, proxyId, killSwitch, jumpLabel);
        s->setExpected(QString(), QStringLiteral("Cloudflare"));
        s->setPreparing(tr("Connecting through Cloudflare…"));
        s->begin(p);
        return;
    }
    const QString typeName = px.value(QStringLiteral("type")).toString() == QLatin1String("tor") ? QStringLiteral("Tor")
                             : p.proxy.type == ProxyConfig::Http ? QStringLiteral("HTTP")
                             : p.proxy.type == ProxyConfig::Shadowsocks ? QStringLiteral("Shadowsocks") : QStringLiteral("SOCKS5");
    s->setRoute(QStringLiteral("%1 · %2").arg(typeName, px.value(QStringLiteral("name")).toString()) + viaJump,
                true, proxyId, killSwitch, jumpLabel);
    s->setPreparing(tr("Checking proxy…"));
    s->setExpected(QString(), tr("checking…"));
    m_proxies.setTransient(proxyId, {{QStringLiteral("checkState"), QStringLiteral("checking")}});

    QPointer<TerminalSession> guard(s);
    m_checker.check(p.proxy, [this, guard, p, proxyId, killSwitch](const QVariantMap &r) {
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
        if (!guard || guard->state() != TerminalSession::Preparing)
            return; // tab closed or cancelled meanwhile

        if (ok) {
            const QString ip = r.value(QStringLiteral("ip")).toString();
            guard->setExpected(ip, locationOf(r), r.value(QStringLiteral("country")).toString());
            if (killSwitch && m_directIp.value(QStringLiteral("ok")).toBool()
                && ip == m_directIp.value(QStringLiteral("ip")).toString()) {
                guard->abort(tr("Kill switch: the proxy exits from your real IP (%1). Connection blocked.").arg(ip));
                return;
            }
        } else if (killSwitch) {
            guard->abort(tr("Kill switch: proxy check failed (%1). Connection blocked.")
                             .arg(r.value(QStringLiteral("error")).toString()));
            return;
        } else {
            guard->setExpected(QString(), tr("proxy check failed: %1").arg(r.value(QStringLiteral("error")).toString()));
        }
        guard->begin(p);
    });
}
