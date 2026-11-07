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

namespace {

const QString kHosts = QStringLiteral("hosts");
const QString kKeys = QStringLiteral("keys");
const QString kProxies = QStringLiteral("proxies");
const QString kSnippets = QStringLiteral("snippets");

QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

const QVariantMap &settingDefaults()
{
    static const QVariantMap d{
        {QStringLiteral("killSwitch"), false},
        {QStringLiteral("defaultProxyId"), QString()},
        {QStringLiteral("fontFamily"), QString()},
        {QStringLiteral("fontSize"), 11},
        {QStringLiteral("copyOnSelect"), true},
        {QStringLiteral("rightClickPaste"), true},
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

QString locationOf(const QVariantMap &r)
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

} // namespace

AppController::AppController(QObject *parent)
    : QObject(parent),
      m_settings(paths::settingsFile(), QSettings::IniFormat),
      m_vault(paths::vaultFile()),
      m_knownHostsPath(paths::knownHostsFile()),
      m_hosts({QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("group"),
               QStringLiteral("hostname"), QStringLiteral("port"), QStringLiteral("username"),
               QStringLiteral("keyId"), QStringLiteral("proxyId"), QStringLiteral("jumpHostId"),
               QStringLiteral("type"), QStringLiteral("color"), QStringLiteral("theme"),
               QStringLiteral("serialPort"), QStringLiteral("baud"), QStringLiteral("forwards"),
               QStringLiteral("confirmMaster"), QStringLiteral("alwaysAskPassphrase"),
               QStringLiteral("useAgent"), QStringLiteral("agentForward")},
              {QStringLiteral("password"), QStringLiteral("notes")}),
      m_keys({QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("type"),
              QStringLiteral("fingerprint"), QStringLiteral("publicKey"), QStringLiteral("encrypted"),
              QStringLiteral("created")},
             {QStringLiteral("passphrase")}),
      m_proxies({QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("type"),
                 QStringLiteral("host"), QStringLiteral("port"), QStringLiteral("username"),
                 QStringLiteral("checkState"), QStringLiteral("exitIp"), QStringLiteral("location"),
                 QStringLiteral("exitCountry"), QStringLiteral("disabled"),
                 QStringLiteral("latencyMs"), QStringLiteral("checkError"), QStringLiteral("checkedAt")},
                {QStringLiteral("password")}),
      m_snippets({QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("command"),
                  QStringLiteral("group"), QStringLiteral("autoRun"), QStringLiteral("script")}, {}),
      m_log(paths::logsDir())
{
    m_log.setEnabled(m_settings.value(QStringLiteral("logEnabled"), true).toBool());
    TerminalEmulator::setCustomThemes(customThemes());
    dropRemovedThemes();

    // Kill switch: cheap proxy probe every few seconds, exit-IP check every minute.
    m_probeTimer.setInterval(std::max(1, m_settings.value(QStringLiteral("killSwitchInterval"), 3).toInt()) * 1000);
    connect(&m_probeTimer, &QTimer::timeout, this, &AppController::runProbe);
    m_probeTimer.start();
    m_exitIpTimer.setInterval(60000);
    connect(&m_exitIpTimer, &QTimer::timeout, this, &AppController::runExitIpCheck);
    m_exitIpTimer.start();

    m_firewallActive = firewalllock::supported() && m_settings.value(QStringLiteral("firewallLock"), false).toBool()
                       && firewalllock::isActive();
    connect(&m_updater, &UpdateChecker::quitRequested, qApp, &QCoreApplication::quit, Qt::QueuedConnection);
    m_hostProbePool.setMaxThreadCount(8);
    // command history is written to the vault in batches, not on every Enter
    m_historySave.setSingleShot(true);
    m_historySave.setInterval(20000);
    connect(&m_historySave, &QTimer::timeout, this, &AppController::flushHistory);
    connect(qApp, &QCoreApplication::aboutToQuit, this, &AppController::flushHistory);
    connect(this, &AppController::vaultStateChanged, this, [this] {
        if (m_vault.isUnlocked()) {
            loadHistory();
            QTimer::singleShot(500, this, &AppController::probeHosts);
        } else {
            ++m_hostProbeGen;
            m_lastHostProbe = 0;
            m_hostStatus.clear();
            emit hostStatusChanged();
        }
    });
    connect(&m_updater, &UpdateChecker::changed, this, [this] {
        const QString st = m_updater.state();
        if (st == m_updaterState)
            return;
        m_updaterState = st;
        if (!m_updater.automatic()) {
            // "Check now" in the settings: show what the new version brings right away
            if (st == QLatin1String("available"))
                emit updateDialogRequested(QStringLiteral("available"));
            return;
        }
        if (st == QLatin1String("available")
            && m_settings.value(QStringLiteral("skippedUpdate")).toString() != m_updater.latestVersion())
            emit updateDialogRequested(QStringLiteral("available"));
        else if (st == QLatin1String("uptodate") && m_justUpdated
                 && UpdateChecker::compareVersions(m_updater.latestVersion(), version()) == 0
                 && !m_updater.localizedNotes().isEmpty())
            emit updateDialogRequested(QStringLiteral("updated"));
        if (st == QLatin1String("uptodate") || st == QLatin1String("available"))
            m_justUpdated = false;
    });
    {
        // "What's new" once after an update (not on the very first start)
        const QString last = m_settings.value(QStringLiteral("lastRunVersion")).toString();
        m_justUpdated = !last.isEmpty() && UpdateChecker::compareVersions(last, version()) < 0;
        m_settings.setValue(QStringLiteral("lastRunVersion"), version());
    }

    m_autoLock.setSingleShot(true);
    connect(&m_autoLock, &QTimer::timeout, this, [this] {
        if (m_vault.isUnlocked()) {
            lockVault();
            emit notify(tr("Vault locked after inactivity."), false);
        }
    });
    qApp->installEventFilter(this);

    // lock the vault together with the computer (Win+L, lock screen, sleep)
    connect(&m_batch, &BatchRunner::finished, this, [this] { onBatchFinished(); });
    connect(&m_systemLock, &SystemLockWatcher::systemLocked, this, [this](const QString &reason) {
        if (!m_vault.isUnlocked() || !m_settings.value(QStringLiteral("lockOnSystemLock"), true).toBool())
            return;
        lockVault();
        logEvent(ActivityLog::Security, QStringLiteral("vault"),
                 reason == QLatin1String("sleep") ? tr("Vault locked: the computer went to sleep")
                                                  : tr("Vault locked: the computer was locked"));
    });

    m_clipboardClear.setSingleShot(true);
    m_clipboardClear.setInterval(30000);
    connect(&m_clipboardClear, &QTimer::timeout, this, [this] {
        QClipboard *cb = QGuiApplication::clipboard();
        if (cb->text() == m_sensitiveClipboard)
            cb->clear();
        secure::wipe(m_sensitiveClipboard);
    });

    refreshKnownHosts();
    logEvent(ActivityLog::Info, QStringLiteral("app"), tr("%1 %2 started").arg(appName(), version()),
             paths::isPortable() ? tr("portable mode · %1").arg(paths::dataDir()) : paths::dataDir());
    if (m_settings.value(QStringLiteral("autoCheckUpdates"), true).toBool() && !updateRepo().isEmpty())
        QTimer::singleShot(4000, this, [this] { m_updater.check(updateRepo(), true); });
}

AppController::~AppController()
{
    qApp->removeEventFilter(this);
    ++m_hostProbeGen;
    m_hostProbePool.clear();
    m_hostProbePool.waitForDone(7000);
    const auto items = m_sessions.items();
    for (TerminalSession *s : items)
        delete s;
    TerminalSession::waitForAllThreads(5000);
}

QString AppController::version() const { return QCoreApplication::applicationVersion(); }
QString AppController::appName() const { return QCoreApplication::applicationName(); }
QString AppController::dataDir() const { return QDir::toNativeSeparators(paths::dataDir()); }
QString AppController::vaultPath() const { return QDir::toNativeSeparators(paths::vaultFile()); }
bool AppController::portable() const { return paths::isPortable(); }
bool AppController::firewallSupported() const { return firewalllock::supported(); }
QString AppController::defaultFont() const { return TerminalItem::defaultFontFamily(); }
QVariantList AppController::terminalThemes() const { return TerminalEmulator::themesForQml(); }

// ---------------------------------------------------------------------------
// custom terminal themes (not secret: kept in settings.ini as JSON)
// ---------------------------------------------------------------------------

QVariantList AppController::customThemes() const
{
    const QByteArray json = m_settings.value(QStringLiteral("customThemes")).toByteArray();
    return QJsonDocument::fromJson(json).array().toVariantList();
}

void AppController::storeCustomThemes(const QVariantList &list)
{
    m_settings.setValue(QStringLiteral("customThemes"),
                        QJsonDocument(QJsonArray::fromVariantList(list)).toJson(QJsonDocument::Compact));
    m_settings.sync();
    TerminalEmulator::setCustomThemes(list);
    for (int i = 0; i < m_sessions.count(); ++i)
        m_sessions.at(i)->emulator()->reloadTheme();
    emit terminalThemesChanged();
}

// Built-in themes that were removed or renamed -> what to use instead.
static QString replacedThemeId(const QString &id)
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("truedevops"), QStringLiteral("default")}, {QStringLiteral("truedevops-frost"), QStringLiteral("default")},
        {QStringLiteral("matrix"), QStringLiteral("default")}, {QStringLiteral("redteam"), QStringLiteral("default")},
        {QStringLiteral("monokai"), QStringLiteral("default")}, {QStringLiteral("amber"), QStringLiteral("default")},
        // drafts of the new default theme
        {QStringLiteral("cand8"), QStringLiteral("shellwarden")}, {QStringLiteral("cand6"), QStringLiteral("default")},
        {QStringLiteral("cand7"), QStringLiteral("default")}, {QStringLiteral("cand2"), QStringLiteral("default")},
        {QStringLiteral("cand5"), QStringLiteral("deepnavy")},
        // Midnight became the default theme (id "default")
        {QStringLiteral("midnight"), QStringLiteral("default")},
    };
    return map.value(id, id);
}

// The built-in picture themes were removed: whoever used one gets the default theme back,
// and the pictures copied for them are deleted.
void AppController::dropRemovedThemes()
{
    const QString cur = m_settings.value(QStringLiteral("terminalTheme")).toString();
    const QString now = replacedThemeId(cur);
    if (now != cur)
        m_settings.setValue(QStringLiteral("terminalTheme"), now);
    if (m_settings.contains(QStringLiteral("themePictures"))) {
        const QVariantMap pics = QJsonDocument::fromJson(m_settings.value(QStringLiteral("themePictures")).toByteArray()).object().toVariantMap();
        for (const QVariant &v : pics) {
            const QString img = v.toMap().value(QStringLiteral("image")).toString();
            if (img.startsWith(paths::dataDir() + QStringLiteral("/backgrounds/")))
                QFile::remove(img);
        }
        m_settings.remove(QStringLiteral("themePictures"));
    }
    m_settings.sync();
}

QString AppController::saveCustomTheme(const QVariantMap &theme)
{
    QVariantMap t = theme;
    QString id = t.value(QStringLiteral("id")).toString();
    if (!id.startsWith(QStringLiteral("custom-")))
        id = QStringLiteral("custom-") + newId().left(8);
    t.insert(QStringLiteral("id"), id);
    if (t.value(QStringLiteral("name")).toString().trimmed().isEmpty())
        t.insert(QStringLiteral("name"), tr("My theme"));

    // The picture is copied next to the vault, so the theme keeps working if
    // the original file is moved, and portable mode stays portable.
    QString img = t.value(QStringLiteral("bgImage")).toString();
    if (img.startsWith(QStringLiteral("file:")))
        img = QUrl(img).toLocalFile();
    const QString bgDir = paths::dataDir() + QStringLiteral("/backgrounds");
    if (!img.isEmpty() && !QFileInfo(img).absolutePath().startsWith(QFileInfo(bgDir).absoluteFilePath())) {
        if (!QFileInfo::exists(img))
            return tr("Image not found: %1").arg(img);
        if (QFileInfo(img).size() > 25 * 1024 * 1024)
            return tr("The image is too large (max 25 MB).");
        QDir().mkpath(bgDir);
        const QString dst = bgDir + QLatin1Char('/') + id + QLatin1Char('.') + QFileInfo(img).suffix().toLower();
        QFile::remove(dst);
        if (!QFile::copy(img, dst))
            return tr("Cannot copy the image to %1").arg(bgDir);
        img = dst;
    }
    t.insert(QStringLiteral("bgImage"), img);

    QVariantList list = customThemes();
    bool replaced = false;
    for (QVariant &v : list) {
        if (v.toMap().value(QStringLiteral("id")).toString() == id) {
            v = t;
            replaced = true;
        }
    }
    if (!replaced)
        list.prepend(t);
    storeCustomThemes(list);
    return id;
}

void AppController::deleteCustomTheme(const QString &id)
{
    QVariantList list = customThemes();
    for (qsizetype i = 0; i < list.size(); ++i) {
        const QVariantMap m = list.at(i).toMap();
        if (m.value(QStringLiteral("id")).toString() == id) {
            const QString img = m.value(QStringLiteral("bgImage")).toString();
            if (img.startsWith(paths::dataDir() + QStringLiteral("/backgrounds/")))
                QFile::remove(img);
            list.removeAt(i);
            break;
        }
    }
    if (m_settings.value(QStringLiteral("terminalTheme")).toString() == id)
        setSetting(QStringLiteral("terminalTheme"), QStringLiteral("default"));
    storeCustomThemes(list);
}

QString AppController::inputColorCommand(const QString &color) const
{
    return TerminalEmulator::inputColorCommand(QColor(color));
}

QString AppController::platform() const
{
#ifdef Q_OS_WIN
    return QStringLiteral("windows");
#else
    return QStringLiteral("linux");
#endif
}

bool AppController::serialAvailable() const
{
#ifdef SW_HAVE_SERIAL
    return true;
#else
    return false;
#endif
}

QStringList AppController::monospaceFonts() const
{
    QStringList out;
    for (const QString &f : QFontDatabase::families()) {
        if (QFontDatabase::isFixedPitch(f) && !QFontDatabase::isPrivateFamily(f) && !f.startsWith(QLatin1Char('@')))
            out << f;
    }
    out.removeDuplicates();
    out.sort(Qt::CaseInsensitive);
    return out;
}

void AppController::beep() const
{
#ifdef Q_OS_WIN
    MessageBeep(MB_OK);
#endif
}

QString AppController::formatBytes(qint64 b) const
{
    if (b < 1024) return QStringLiteral("%1 B").arg(b);
    if (b < 1024 * 1024) return QStringLiteral("%1 KB").arg(b / 1024.0, 0, 'f', 1);
    if (b < 1024ll * 1024 * 1024) return QStringLiteral("%1 MB").arg(b / 1048576.0, 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(b / 1073741824.0, 0, 'f', 2);
}

void AppController::logEvent(int level, const QString &cat, const QString &msg, const QString &detail,
                             const QString &host)
{
    m_log.add(level, cat, msg, detail, host);
}

// ---------------------------------------------------------------------------
// vault
// ---------------------------------------------------------------------------

void AppController::setBusy(bool b)
{
    if (m_busy != b) {
        m_busy = b;
        emit busyChanged();
    }
}

void AppController::createVault(const QString &password, const QString &confirm)
{
    if (password != confirm) {
        emit vaultError(tr("Passwords do not match."));
        return;
    }
    setBusy(true);
    // Let the UI paint the busy state before Argon2id takes ~1 s.
    QTimer::singleShot(60, this, [this, password] {
        QString err;
        const bool ok = m_vault.create(password, &err);
        setBusy(false);
        if (!ok) {
            emit vaultError(err);
            return;
        }
        logEvent(ActivityLog::Security, QStringLiteral("vault"), tr("Vault created"), paths::vaultFile());
        reloadModels();
        restartAutoLock();
        emit vaultStateChanged();
    });
}

void AppController::unlockVault(const QString &password)
{
    setBusy(true);
    QTimer::singleShot(60, this, [this, password] {
        QString err;
        const bool ok = m_vault.unlock(password, &err);
        setBusy(false);
        if (!ok) {
            logEvent(ActivityLog::Security, QStringLiteral("vault"), tr("Failed unlock attempt"), err);
            emit vaultError(err);
            return;
        }
        logEvent(ActivityLog::Success, QStringLiteral("vault"), tr("Vault unlocked"));
        migrateRemovedFeatures();
        reloadModels();
        restartAutoLock();
        emit vaultStateChanged();
    });
}

// The in-app 2FA (TOTP) was removed: its secret lived in this same vault, so it
// protected no more than "ask the master password to connect". Hosts that had it
// get that option instead, and the TOTP secret is dropped from the vault.
void AppController::migrateRemovedFeatures()
{
    QJsonArray hosts = section(kHosts);
    bool changed = false;
    for (int i = 0; i < hosts.size(); ++i) {
        QJsonObject h = hosts.at(i).toObject();
        const QString theme = h.value(QStringLiteral("theme")).toString();
        if (!theme.isEmpty() && replacedThemeId(theme) != theme) {
            h.insert(QStringLiteral("theme"), replacedThemeId(theme) == QLatin1String("default") ? QString() : replacedThemeId(theme));
            hosts.replace(i, h);
            changed = true;
        }
        if (!h.contains(QStringLiteral("require2fa")) && !h.contains(QStringLiteral("totpSecret")))
            continue;
        if (h.value(QStringLiteral("require2fa")).toBool())
            h.insert(QStringLiteral("confirmMaster"), true);
        h.remove(QStringLiteral("require2fa"));
        h.remove(QStringLiteral("totpSecret"));
        hosts.replace(i, h);
        changed = true;
    }
    if (!changed)
        return;
    setSection(kHosts, hosts);
    persist();
}

void AppController::lockVault()
{
    flushHistory();
    m_history.clear();
    if (m_vault.isUnlocked())
        logEvent(ActivityLog::Info, QStringLiteral("vault"), tr("Vault locked"));
    m_vault.lock();
    m_hosts.reset({});
    m_keys.reset({});
    m_proxies.reset({});
    m_snippets.reset({});
    m_autoLock.stop();
    emit vaultStateChanged();
}

QString AppController::changeMasterPassword(const QString &oldPw, const QString &newPw, const QString &confirm)
{
    if (newPw != confirm)
        return tr("New passwords do not match.");
    if (newPw.size() < passwordtools::kManualMinLength)
        return tr("The master password must be at least 8 characters.");
    QString err;
    if (!m_vault.changePassword(oldPw, newPw, &err)) {
        logEvent(ActivityLog::Security, QStringLiteral("vault"), tr("Master password change failed"), err);
        return err;
    }
    logEvent(ActivityLog::Security, QStringLiteral("vault"), tr("Master password changed"));
    return {};
}

QJsonArray AppController::section(const QString &name) const
{
    return m_vault.data().value(name).toArray();
}

void AppController::setSection(const QString &name, const QJsonArray &items)
{
    m_vault.data().insert(name, items);
}

QJsonObject AppController::findIn(const QString &sectionName, const QString &id) const
{
    if (id.isEmpty())
        return {};
    for (const QJsonValue &v : section(sectionName)) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("id")).toString() == id)
            return o;
    }
    return {};
}

bool AppController::persist()
{
    QString err;
    if (!m_vault.save(&err)) {
        emit notify(err, true);
        logEvent(ActivityLog::Error, QStringLiteral("vault"), tr("Could not save the vault."), err);
        return false;
    }
    dailyBackup();
    reloadModels();
    return true;
}

void AppController::reloadModels()
{
    m_hosts.reset(section(kHosts));
    m_keys.reset(section(kKeys));
    m_proxies.reset(section(kProxies));
    {
        // Newest snippet first (they are appended when created).
        const QJsonArray all = section(kSnippets);
        QJsonArray rev;
        for (qsizetype i = all.size() - 1; i >= 0; --i)
            rev.append(all.at(i));
        m_snippets.reset(rev);
    }
}

bool AppController::upsert(const QString &sectionName, QJsonObject &o)
{
    QJsonArray list = section(sectionName);
    QString id = o.value(QStringLiteral("id")).toString();
    bool replaced = false;
    for (int i = 0; !id.isEmpty() && i < list.size(); ++i) {
        if (list.at(i).toObject().value(QStringLiteral("id")).toString() == id) {
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
    setSection(sectionName, list);
    return persist();
}

void AppController::dailyBackup()
{
    // One encrypted copy per day, the last 14 are kept.
    const QString dir = paths::backupsDir();
    QDir().mkpath(dir);
    const QString today = dir + QStringLiteral("/vault-") + QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"))
                          + QStringLiteral(".swv");
    if (!QFileInfo::exists(today))
        QFile::copy(paths::vaultFile(), today);
    QFileInfoList all = QDir(dir).entryInfoList({QStringLiteral("vault-*.swv")}, QDir::Files, QDir::Name);
    while (all.size() > 14)
        QFile::remove(all.takeFirst().absoluteFilePath());
}

QVariantList AppController::backups() const
{
    QVariantList out;
    const QFileInfoList all = QDir(paths::backupsDir()).entryInfoList({QStringLiteral("vault-*.swv")}, QDir::Files,
                                                                       QDir::Name | QDir::Reversed);
    for (const QFileInfo &fi : all)
        out << QVariantMap{{QStringLiteral("name"), fi.fileName()},
                           {QStringLiteral("url"), QUrl::fromLocalFile(fi.absoluteFilePath())},
                           {QStringLiteral("date"), fi.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"))},
                           {QStringLiteral("size"), formatBytes(fi.size())}};
    return out;
}

QString AppController::backupVault(const QUrl &target)
{
    const QString path = target.toLocalFile();
    if (path.isEmpty())
        return tr("Choose a file.");
    QFile::remove(path);
    if (!QFile::copy(paths::vaultFile(), path))
        return tr("Cannot write %1").arg(path);
    logEvent(ActivityLog::Info, QStringLiteral("vault"), tr("Vault backup saved"), path);
    return {};
}

QString AppController::startOver()
{
    if (m_vault.isUnlocked())
        return tr("Lock the vault first.");
    const QString dir = paths::backupsDir();
    QDir().mkpath(dir);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString keep = dir + QStringLiteral("/forgotten-") + stamp + QStringLiteral(".swv");
    if (QFileInfo::exists(paths::vaultFile()) && !QFile::rename(paths::vaultFile(), keep)) {
        if (!QFile::copy(paths::vaultFile(), keep) || !QFile::remove(paths::vaultFile()))
            return tr("Cannot move the old vault: %1").arg(paths::vaultFile());
    }
    const QString bak = paths::vaultFile() + QStringLiteral(".bak");
    if (QFileInfo::exists(bak))
        QFile::rename(bak, keep + QStringLiteral(".bak"));
    logEvent(ActivityLog::Security, QStringLiteral("vault"), tr("Started over with a new vault"),
             tr("Old vault kept as %1").arg(keep));
    emit vaultStateChanged();
    return {};
}

QString AppController::restoreVault(const QUrl &source, const QString &password)
{
    const QString path = source.toLocalFile();
    Vault probe(path);
    QString err;
    if (!probe.unlock(password, &err))
        return err;
    probe.lock();
    // keep the current vault just in case
    const QString keep = paths::backupsDir() + QStringLiteral("/before-restore-")
                         + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QStringLiteral(".swv");
    QDir().mkpath(paths::backupsDir());
    QFile::copy(paths::vaultFile(), keep);
    m_vault.lock();
    QFile::remove(paths::vaultFile());
    if (!QFile::copy(path, paths::vaultFile())) {
        QFile::copy(keep, paths::vaultFile());
        return tr("Cannot replace the vault file.");
    }
    if (!m_vault.unlock(password, &err))
        return err;
    migrateRemovedFeatures();
    reloadModels();
    restartAutoLock();
    emit vaultStateChanged();
    logEvent(ActivityLog::Security, QStringLiteral("vault"), tr("Vault restored from backup"), path);
    return {};
}

void AppController::openDataFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(paths::dataDir()));
}

QString AppController::generatePassword(int length, bool symbols) const
{
    return passwordtools::generate(length, symbols);
}

QVariantMap AppController::evaluatePassword(const QString &password) const
{
    const passwordtools::Strength st = passwordtools::evaluate(password);
    QStringList hints;
    for (const QString &i : st.issues) {
        if (i == QLatin1String("short")) hints << tr("shorter than 19 characters");
        else if (i == QLatin1String("common")) hints << tr("contains a very common password");
        else if (i == QLatin1String("pattern")) hints << tr("keyboard or alphabet sequence");
        else if (i == QLatin1String("repeats")) hints << tr("too many repeated characters");
        else if (i == QLatin1String("classes")) hints << tr("mix upper/lower case, digits and symbols");
    }
    static const char *labels[] = {QT_TR_NOOP("Very weak"), QT_TR_NOOP("Weak"), QT_TR_NOOP("Fair"),
                                   QT_TR_NOOP("Good"), QT_TR_NOOP("Strong")};
    return {{QStringLiteral("score"), st.score},
            {QStringLiteral("bits"), int(st.bits)},
            {QStringLiteral("reliable"), st.reliable},
            {QStringLiteral("label"), tr(labels[st.score])},
            {QStringLiteral("hints"), hints.join(QStringLiteral(" · "))}};
}

QUrl AppController::homeFolder() const
{
    return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
}

QStringList AppController::serialPorts() const
{
    QStringList out;
#ifdef SW_HAVE_SERIAL
    for (const QSerialPortInfo &i : QSerialPortInfo::availablePorts())
        out << i.portName() + (i.description().isEmpty() ? QString() : QStringLiteral(" — ") + i.description());
#endif
    return out;
}

// ---------------------------------------------------------------------------
// hosts
// ---------------------------------------------------------------------------

QVariantMap AppController::host(const QString &id) const
{
    return findIn(kHosts, id).toVariantMap();
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
    const bool isNew = o.value(QStringLiteral("id")).toString().isEmpty();
    const QJsonObject before = findIn(kHosts, o.value(QStringLiteral("id")).toString());
    if (!upsert(kHosts, o))
        return tr("Could not save the vault.");
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

// ---------------------------------------------------------------------------
// keys
// ---------------------------------------------------------------------------

QString AppController::generateKey(const QString &name, int type, const QString &comment,
                                   const QString &passphrase, const QString &confirm, bool savePassphrase)
{
    if (!m_vault.isUnlocked())
        return tr("The vault is locked.");
    if (passphrase != confirm)
        return tr("Passphrases do not match.");

    static const keytools::KeyType types[] = {
        keytools::KeyType::Ed25519, keytools::KeyType::EcdsaP256, keytools::KeyType::EcdsaP384,
        keytools::KeyType::EcdsaP521, keytools::KeyType::Rsa3072, keytools::KeyType::Rsa4096};
    if (type < 0 || type > 5)
        type = 0;

    keytools::KeyMaterial km;
    QString err;
    if (!keytools::generate(types[type], comment, passphrase, &km, &err))
        return err;

    QJsonObject o;
    o.insert(QStringLiteral("id"), newId());
    o.insert(QStringLiteral("name"), name.trimmed().isEmpty() ? km.type : name.trimmed());
    o.insert(QStringLiteral("type"), km.type);
    o.insert(QStringLiteral("fingerprint"), km.fingerprint);
    o.insert(QStringLiteral("publicKey"), km.publicLine);
    o.insert(QStringLiteral("privateKey"), QString::fromUtf8(km.privateKey));
    o.insert(QStringLiteral("encrypted"), km.encrypted);
    o.insert(QStringLiteral("passphrase"), savePassphrase ? passphrase : QString());
    o.insert(QStringLiteral("created"), QDateTime::currentDateTime().toString(Qt::ISODate));
    secure::wipe(km.privateKey);

    QJsonArray keys = section(kKeys);
    keys.append(o);
    setSection(kKeys, keys);
    if (!persist())
        return tr("Could not save the vault.");
    logEvent(ActivityLog::Security, QStringLiteral("key"), tr("Key generated"),
             km.type + QStringLiteral(" · ") + km.fingerprint + (km.encrypted ? QString() : tr(" · no passphrase")),
             o.value(QStringLiteral("name")).toString());
    return {};
}

QVariantMap AppController::importKey(const QString &name, const QString &text,
                                     const QString &passphrase, bool savePassphrase)
{
    QVariantMap r;
    if (!m_vault.isUnlocked()) {
        r.insert(QStringLiteral("error"), tr("The vault is locked."));
        return r;
    }
    keytools::KeyMaterial km;
    QString err;
    const QString comment = name.trimmed();
    switch (keytools::importPrivateKey(text.toUtf8(), passphrase, comment, &km, &err)) {
    case keytools::ImportResult::NeedPassphrase:
        r.insert(QStringLiteral("needPassphrase"), true);
        r.insert(QStringLiteral("error"), tr("This key is protected. Enter its passphrase."));
        return r;
    case keytools::ImportResult::WrongPassphrase:
        r.insert(QStringLiteral("needPassphrase"), true);
        r.insert(QStringLiteral("error"), tr("Wrong passphrase."));
        return r;
    case keytools::ImportResult::Invalid:
        r.insert(QStringLiteral("error"), err);
        return r;
    case keytools::ImportResult::Ok:
        break;
    }

    for (const QJsonValue &v : section(kKeys)) {
        if (v.toObject().value(QStringLiteral("fingerprint")).toString() == km.fingerprint) {
            r.insert(QStringLiteral("error"), tr("This key is already in the keychain (\"%1\").")
                                                  .arg(v.toObject().value(QStringLiteral("name")).toString()));
            return r;
        }
    }

    QJsonObject o;
    o.insert(QStringLiteral("id"), newId());
    o.insert(QStringLiteral("name"), comment.isEmpty() ? km.type : comment);
    o.insert(QStringLiteral("type"), km.type);
    o.insert(QStringLiteral("fingerprint"), km.fingerprint);
    o.insert(QStringLiteral("publicKey"), km.publicLine);
    o.insert(QStringLiteral("privateKey"), QString::fromUtf8(km.privateKey));
    o.insert(QStringLiteral("encrypted"), km.encrypted);
    o.insert(QStringLiteral("passphrase"), (savePassphrase && km.encrypted) ? passphrase : QString());
    o.insert(QStringLiteral("created"), QDateTime::currentDateTime().toString(Qt::ISODate));
    secure::wipe(km.privateKey);

    QJsonArray keys = section(kKeys);
    keys.append(o);
    setSection(kKeys, keys);
    if (!persist())
        r.insert(QStringLiteral("error"), tr("Could not save the vault."));
    else
        r.insert(QStringLiteral("id"), o.value(QStringLiteral("id")).toString());
    if (r.contains(QStringLiteral("id")))
        logEvent(ActivityLog::Security, QStringLiteral("key"), tr("Key imported"),
                 km.type + QStringLiteral(" · ") + km.fingerprint, o.value(QStringLiteral("name")).toString());
    return r;
}

QString AppController::readTextFile(const QUrl &url) const
{
    QFile f(url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (!f.open(QIODevice::ReadOnly) || f.size() > 1024 * 1024)
        return {};
    return QString::fromUtf8(f.readAll());
}

QUrl AppController::suggestedFile(const QString &fileName) const
{
    QDir().mkpath(paths::keysDir());
    return QUrl::fromLocalFile(paths::keysDir() + QLatin1Char('/') + fileName);
}

QString AppController::publicKeyLine(const QString &id) const
{
    return findIn(kKeys, id).value(QStringLiteral("publicKey")).toString();
}

QString AppController::exportPublicKey(const QString &id, const QUrl &url)
{
    const QString line = publicKeyLine(id);
    if (line.isEmpty())
        return tr("Key not found.");
    QString err;
    // One line, OpenSSH format, LF, no BOM — ready for authorized_keys.
    if (!keytools::writeTextFile(url.toLocalFile(), line.toUtf8(), false, &err))
        return err;
    return {};
}

QString AppController::exportPublicKeyRfc4716(const QString &id, const QUrl &url)
{
    const QString line = publicKeyLine(id);
    if (line.isEmpty())
        return tr("Key not found.");
    QString err;
    if (!keytools::writeTextFile(url.toLocalFile(), keytools::toRfc4716(line).toUtf8(), false, &err))
        return err;
    return {};
}

QString AppController::exportPrivateKey(const QString &id, const QUrl &url)
{
    const QJsonObject k = findIn(kKeys, id);
    if (k.isEmpty())
        return tr("Key not found.");
    QByteArray text = k.value(QStringLiteral("privateKey")).toString().toUtf8();
    secure::ByteGuard g(text);
    QString err;
    if (!keytools::writeTextFile(url.toLocalFile(), text, true, &err))
        return err;
    logEvent(ActivityLog::Security, QStringLiteral("key"), tr("Private key exported"), url.toLocalFile(),
             k.value(QStringLiteral("name")).toString());
    return {};
}

QString AppController::setKeyPassphrase(const QString &id, const QString &passphrase, bool save)
{
    QJsonArray keys = section(kKeys);
    for (int i = 0; i < keys.size(); ++i) {
        QJsonObject o = keys.at(i).toObject();
        if (o.value(QStringLiteral("id")).toString() != id)
            continue;
        if (!keytools::checkPassphrase(o.value(QStringLiteral("privateKey")).toString().toUtf8(), passphrase))
            return tr("Wrong passphrase.");
        o.insert(QStringLiteral("passphrase"), save ? passphrase : QString());
        keys.replace(i, o);
        setSection(kKeys, keys);
        return persist() ? QString() : tr("Could not save the vault.");
    }
    return tr("Key not found.");
}

void AppController::forgetKeyPassphrase(const QString &id)
{
    QJsonArray keys = section(kKeys);
    for (int i = 0; i < keys.size(); ++i) {
        QJsonObject o = keys.at(i).toObject();
        if (o.value(QStringLiteral("id")).toString() == id) {
            o.insert(QStringLiteral("passphrase"), QString());
            keys.replace(i, o);
            setSection(kKeys, keys);
            persist();
            return;
        }
    }
}

void AppController::renameKey(const QString &id, const QString &name)
{
    if (name.trimmed().isEmpty())
        return;
    QJsonArray keys = section(kKeys);
    for (int i = 0; i < keys.size(); ++i) {
        QJsonObject o = keys.at(i).toObject();
        if (o.value(QStringLiteral("id")).toString() == id) {
            o.insert(QStringLiteral("name"), name.trimmed());
            keys.replace(i, o);
            setSection(kKeys, keys);
            persist();
            return;
        }
    }
}

void AppController::deleteKey(const QString &id)
{
    logEvent(ActivityLog::Warning, QStringLiteral("key"), tr("Key deleted"),
             findIn(kKeys, id).value(QStringLiteral("fingerprint")).toString(),
             findIn(kKeys, id).value(QStringLiteral("name")).toString());
    QJsonArray keys = section(kKeys);
    for (int i = 0; i < keys.size(); ++i) {
        if (keys.at(i).toObject().value(QStringLiteral("id")).toString() == id) {
            keys.removeAt(i);
            break;
        }
    }
    setSection(kKeys, keys);
    // Hosts that used this key fall back to password authentication.
    QJsonArray hosts = section(kHosts);
    for (int i = 0; i < hosts.size(); ++i) {
        QJsonObject h = hosts.at(i).toObject();
        if (h.value(QStringLiteral("keyId")).toString() == id) {
            h.insert(QStringLiteral("keyId"), QString());
            hosts.replace(i, h);
        }
    }
    setSection(kHosts, hosts);
    persist();
}

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
        if (o.value(QStringLiteral("password")).toString().isEmpty())
            return tr("Enter the Shadowsocks password.");
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
    for (const char *k : {"checkState", "exitIp", "location", "exitCountry", "latencyMs", "checkError", "checkedAt", "hasPassword"})
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
                                {QStringLiteral("checkError"), QString()}});
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
                                    {QStringLiteral("checkError"), r.value(QStringLiteral("error"))}});
        return;
    }
    m_checker.check(proxyConfig(p), [this, id](const QVariantMap &r) { storeCheck(id, r); });
}

static QString cfRouteName(const QJsonObject &px)
{
    const QString n = px.value(QStringLiteral("name")).toString().trimmed();
    return n.isEmpty() || n.compare(QLatin1String("Cloudflare Tunnel"), Qt::CaseInsensitive) == 0
               ? QStringLiteral("Cloudflare Tunnel") : QStringLiteral("Cloudflare Tunnel · %1").arg(n);
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

// ---------------------------------------------------------------------------
// snippets
// ---------------------------------------------------------------------------

QVariantMap AppController::snippet(const QString &id) const
{
    return findIn(kSnippets, id).toVariantMap();
}

QString AppController::saveSnippet(const QVariantMap &sn)
{
    if (!m_vault.isUnlocked())
        return tr("The vault is locked.");
    QJsonObject o = QJsonObject::fromVariantMap(sn);
    if (o.value(QStringLiteral("command")).toString().trimmed().isEmpty())
        return tr("Enter a command.");
    if (o.value(QStringLiteral("name")).toString().trimmed().isEmpty())
        o.insert(QStringLiteral("name"), o.value(QStringLiteral("command")).toString().section(QLatin1Char('\n'), 0, 0).left(40));
    return upsert(kSnippets, o) ? QString() : tr("Could not save the vault.");
}

void AppController::deleteSnippet(const QString &id)
{
    QJsonArray list = section(kSnippets);
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).toObject().value(QStringLiteral("id")).toString() == id) {
            list.removeAt(i);
            break;
        }
    }
    setSection(kSnippets, list);
    persist();
}

void AppController::runSnippet(const QString &id, int sessionIndex, bool allSessions)
{
    const QJsonObject sn = findIn(kSnippets, id);
    if (sn.isEmpty())
        return;
    QString cmd = sn.value(QStringLiteral("command")).toString();
    if (sn.value(QStringLiteral("script")).toBool()) {
        // Whole script (e.g. a .sh file): sent as one base64 line and run by the
        // interpreter from its #! line (default sh). Typing it line by line would
        // let the shell expand tabs, aliases and history, and stop at the first
        // interactive prompt. The leading space keeps it out of bash history.
        QString interp = QStringLiteral("sh");
        const QString first = cmd.section(QLatin1Char('\n'), 0, 0).trimmed();
        if (first.startsWith(QStringLiteral("#!"))) {
            QString she = first.mid(2).trimmed();
            static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9_./ -]+$"));
            if (safe.match(she).hasMatch())
                interp = she;
        }
        QString body = cmd;
        body.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        // Lines of at most ~900 chars: a shell without line editing (dash) reads
        // through the tty's canonical mode, which cuts lines at 4096 bytes.
        const QString b64 = QString::fromLatin1(body.toUtf8().toBase64());
        QString lines;
        for (qsizetype i = 0; i < b64.size(); i += 900)
            lines += QStringLiteral("echo ") + b64.mid(i, 900) + QLatin1Char('\r');
        cmd = QStringLiteral(" (") + lines + QStringLiteral(") | base64 -d | ") + interp + QLatin1Char('\r');
    } else {
        cmd.replace(QStringLiteral("\r\n"), QStringLiteral("\r"));
        cmd.replace(QLatin1Char('\n'), QLatin1Char('\r'));
        if (sn.value(QStringLiteral("autoRun")).toBool() && !cmd.endsWith(QLatin1Char('\r')))
            cmd += QLatin1Char('\r');
    }
    int sent = 0;
    for (int i = 0; i < m_sessions.count(); ++i) {
        TerminalSession *s = m_sessions.at(i);
        if ((allSessions || i == sessionIndex) && s->state() == TerminalSession::Connected) {
            s->sendText(cmd);
            ++sent;
        }
    }
    if (sent)
        logEvent(ActivityLog::Info, QStringLiteral("snippet"), tr("Snippet sent to %n session(s)", nullptr, sent),
                 sn.value(QStringLiteral("name")).toString());
}

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

bool AppController::needsChallenge(const QJsonObject &h) const
{
    return h.value(QStringLiteral("confirmMaster")).toBool();
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
    if (h.value(QStringLiteral("confirmMaster")).toBool() && !m_vault.verifyPassword(masterPassword))
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
    if (pending)
        startSession(pending, h);
    else
        openHostNow(hostId);
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

void AppController::openHostNow(const QString &id)
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
ConnectParams AppController::paramsFor(const QJsonObject &host, const QJsonObject &jumpHost) const
{
    ConnectParams p;
    p.host = host.value(QStringLiteral("hostname")).toString();
    p.port = host.value(QStringLiteral("port")).toInt(22);
    p.username = host.value(QStringLiteral("username")).toString();
    if (p.username.isEmpty())
        p.username = qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER", QStringLiteral("root")));
    p.password = host.value(QStringLiteral("password")).toString();
    p.knownHostsPath = m_knownHostsPath;
    p.keepaliveSeconds = m_settings.value(QStringLiteral("keepaliveSeconds"), 30).toInt();
    p.useAgent = host.value(QStringLiteral("useAgent")).toBool();
    p.agentForward = host.value(QStringLiteral("agentForward")).toBool();

    const QJsonObject key = findIn(kKeys, host.value(QStringLiteral("keyId")).toString());
    if (!key.isEmpty()) {
        p.privateKey = key.value(QStringLiteral("privateKey")).toString().toUtf8();
        p.keyPassphrase = host.value(QStringLiteral("alwaysAskPassphrase")).toBool()
                              ? QString() : key.value(QStringLiteral("passphrase")).toString();
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
        p.jump.password = jumpHost.value(QStringLiteral("password")).toString();
        const QJsonObject jk = findIn(kKeys, jumpHost.value(QStringLiteral("keyId")).toString());
        if (!jk.isEmpty()) {
            p.jump.privateKey = jk.value(QStringLiteral("privateKey")).toString().toUtf8();
            p.jump.keyPassphrase = jk.value(QStringLiteral("passphrase")).toString();
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

    ConnectParams p = paramsFor(host, jumpHost);
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

QString AppController::updateRepo() const
{
    // an empty value (saved by an older build) falls back to the built-in repository
    const QString repo = m_settings.value(QStringLiteral("updateRepo")).toString().trimmed();
    return repo.isEmpty() ? QStringLiteral(APP_GITHUB_REPO) : repo;
}

void AppController::checkForUpdates()
{
    m_updater.check(updateRepo());
}

void AppController::probeHosts()
{
    if (!m_vault.isUnlocked() || !m_settings.value(QStringLiteral("hostProbe"), false).toBool()) {
        if (!m_hostStatus.isEmpty()) {
            m_hostStatus.clear();
            emit hostStatusChanged();
        }
        return;
    }
    // At most every 5 minutes, and only while the host list is on screen (the page
    // asks). Every check is one short connection that the server writes to its log;
    // this rate stays far below what fail2ban & co. count as an attack.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastHostProbe && now - m_lastHostProbe < 5 * 60 * 1000)
        return;
    m_lastHostProbe = now;
    const quint64 gen = ++m_hostProbeGen;
    const bool killSwitch = m_settings.value(QStringLiteral("killSwitch"), false).toBool();

    struct Job { QString id, host; int port = 22; bool ssh = true; ProxyConfig pc; };
    QVector<Job> jobs;
    QVariantMap status = m_hostStatus;
    auto skip = [&status](const QString &id, const QString &note) {
        status.insert(id, QVariantMap{{QStringLiteral("state"), QStringLiteral("skip")}, {QStringLiteral("note"), note}});
    };
    const QJsonArray hosts = section(kHosts);
    QSet<QString> ids;
    for (const QJsonValue &v : hosts) {
        const QJsonObject h = v.toObject();
        const QString id = h.value(QStringLiteral("id")).toString();
        const QString type = h.value(QStringLiteral("type")).toString(QStringLiteral("ssh"));
        ids.insert(id);
        if (type == QLatin1String("serial"))
            continue;
        if (!h.value(QStringLiteral("jumpHostId")).toString().isEmpty()) {
            skip(id, tr("Behind a jump host: checked only when you connect."));
            continue;
        }
        const QString proxyId = effectiveProxyId(h);
        Job j;
        j.id = id;
        j.host = h.value(QStringLiteral("hostname")).toString().trimmed();
        j.ssh = type != QLatin1String("telnet");
        j.port = h.value(QStringLiteral("port")).toInt(j.ssh ? 22 : 23);
        if (j.host.isEmpty())
            continue;
        if (proxyId.isEmpty()) {
            if (killSwitch) {   // never a direct packet while the kill switch is on
                skip(id, tr("Kill switch is on: not checked directly."));
                continue;
            }
        } else {
            const QJsonObject px = findIn(kProxies, proxyId);
            const QString ptype = px.value(QStringLiteral("type")).toString();
            if (ptype == QLatin1String("tor") || ptype == QLatin1String("cloudflare")) {
                skip(id, ptype == QLatin1String("tor") ? tr("Via Tor: not checked in the background.")
                                                       : tr("Via Cloudflare Tunnel: checked only when you connect."));
                continue;
            }
            j.pc = proxyConfig(px);
            if (!j.pc.isSet()) {
                skip(id, tr("The proxy of this host is not complete."));
                continue;
            }
        }
        jobs.append(j);
    }
    for (auto it = status.begin(); it != status.end();)   // deleted hosts
        it = ids.contains(it.key()) ? std::next(it) : status.erase(it);
    m_hostStatus = status;
    emit hostStatusChanged();

    for (const Job &job : std::as_const(jobs)) {
        m_hostProbePool.start([this, gen, job] {
            QElapsedTimer t;
            t.start();
            QString err;
            const sw_socket_t s = job.pc.isSet() ? proxytunnel::open(job.pc, job.host, job.port, 8000, &err)
                                                 : proxytunnel::connectDirect(job.host, job.port, 5000, &err);
            const int ms = int(t.elapsed());
            bool ok = s != kInvalidSocket;
            if (ok && job.ssh) {
                char buf[64];
                const int n = proxytunnel::readSome(s, buf, sizeof buf, 4000);
                ok = n >= 4 && QByteArray(buf, n).startsWith("SSH-");
                if (!ok)
                    err = tr("The port is open, but no SSH server answered.");
            }
            if (s != kInvalidSocket)
                proxytunnel::closeSocket(s);
            const bool viaProxy = job.pc.isSet();
            QMetaObject::invokeMethod(this, [this, gen, id = job.id, ok, ms, err, viaProxy] {
                if (gen != m_hostProbeGen)
                    return;
                m_hostStatus.insert(id, QVariantMap{{QStringLiteral("state"), ok ? QStringLiteral("online") : QStringLiteral("offline")},
                                                    {QStringLiteral("ms"), ms},
                                                    {QStringLiteral("proxy"), viaProxy},
                                                    {QStringLiteral("note"), ok ? QString() : err}});
                emit hostStatusChanged();
            }, Qt::QueuedConnection);
        });
    }
}

static const QString kHistory = QStringLiteral("history");
static constexpr int kHistoryMax = 300;

void AppController::loadHistory()
{
    m_history.clear();
    for (const QJsonValue &v : section(kHistory)) {
        const QJsonObject o = v.toObject();
        QStringList cmds;
        for (const QJsonValue &c : o.value(QStringLiteral("commands")).toArray())
            cmds << c.toString();
        m_history.insert(o.value(QStringLiteral("hostId")).toString(), cmds);
    }
    m_historyDirty = false;
}

void AppController::flushHistory()
{
    m_historySave.stop();
    if (!m_historyDirty || !m_vault.isUnlocked())
        return;
    QJsonArray arr;
    for (auto it = m_history.cbegin(); it != m_history.cend(); ++it) {
        if (it.value().isEmpty() || findIn(kHosts, it.key()).isEmpty())
            continue;   // deleted hosts take their history with them
        arr.append(QJsonObject{{QStringLiteral("hostId"), it.key()},
                               {QStringLiteral("commands"), QJsonArray::fromStringList(it.value())}});
    }
    setSection(kHistory, arr);
    m_historyDirty = false;
    persist();
}

void AppController::rememberCommand(const QString &hostId, const QString &command)
{
    if (hostId.isEmpty() || !m_vault.isUnlocked() || !m_settings.value(QStringLiteral("commandHistory"), true).toBool())
        return;
    QStringList &list = m_history[hostId];
    list.removeAll(command);          // the same command again moves to the top
    list.prepend(command);
    while (list.size() > kHistoryMax)
        list.removeLast();
    m_historyDirty = true;
    if (!m_historySave.isActive())
        m_historySave.start();
}

QStringList AppController::commandHistory(const QString &hostId) const
{
    return m_history.value(hostId);
}

void AppController::forgetCommand(const QString &hostId, const QString &command)
{
    if (m_history[hostId].removeAll(command) > 0) {
        m_historyDirty = true;
        flushHistory();
    }
}

void AppController::clearCommandHistory(const QString &hostId)
{
    m_history.remove(hostId);
    m_historyDirty = true;
    flushHistory();
}

void AppController::skipUpdate(const QString &version)
{
    m_settings.setValue(QStringLiteral("skippedUpdate"), version);
}

void AppController::showUpdateDetails()
{
    const QString st = m_updater.state();
    const bool newer = st == QLatin1String("available") || st == QLatin1String("downloading")
                       || st == QLatin1String("ready")
                       || UpdateChecker::compareVersions(m_updater.latestVersion(), version()) > 0;
    emit updateDialogRequested(newer ? QStringLiteral("available") : QStringLiteral("current"));
}

void AppController::onSecretVerified(const QString &hostId, int kind, const QString &secret)
{
    if (!m_vault.isUnlocked())
        return;
    if (kind == SshWorker::SecretPassword) {
        QJsonArray hosts = section(kHosts);
        for (int i = 0; i < hosts.size(); ++i) {
            QJsonObject h = hosts.at(i).toObject();
            if (h.value(QStringLiteral("id")).toString() == hostId) {
                h.insert(QStringLiteral("password"), secret);
                hosts.replace(i, h);
                setSection(kHosts, hosts);
                if (persist())
                    emit notify(tr("Password saved to the vault."), false);
                return;
            }
        }
    } else if (kind == SshWorker::SecretKeyPassphrase) {
        const QString keyId = findIn(kHosts, hostId).value(QStringLiteral("keyId")).toString();
        QJsonArray keys = section(kKeys);
        for (int i = 0; i < keys.size(); ++i) {
            QJsonObject k = keys.at(i).toObject();
            if (k.value(QStringLiteral("id")).toString() == keyId) {
                k.insert(QStringLiteral("passphrase"), secret);
                keys.replace(i, k);
                setSection(kKeys, keys);
                if (persist())
                    emit notify(tr("Key passphrase saved to the vault."), false);
                return;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// known hosts
// ---------------------------------------------------------------------------

void AppController::refreshKnownHosts()
{
    m_knownHosts.clear();
    for (const knownhosts::Entry &e : knownhosts::load(m_knownHostsPath)) {
        m_knownHosts.append(QVariantMap{
            {QStringLiteral("line"), e.line},
            {QStringLiteral("hosts"), e.hosts},
            {QStringLiteral("keyType"), e.keyType},
            {QStringLiteral("fingerprint"), e.fingerprint},
        });
    }
    emit knownHostsChanged();
}

void AppController::removeKnownHost(int line)
{
    knownhosts::removeLine(m_knownHostsPath, line);
    refreshKnownHosts();
}

// ---------------------------------------------------------------------------
// language
// ---------------------------------------------------------------------------

namespace {
const QStringList kLanguages = {QStringLiteral("en"), QStringLiteral("uk"), QStringLiteral("ru"),
                                QStringLiteral("fr"), QStringLiteral("es")};
}

QVariantList AppController::languages() const
{
    // Native names: a user should find their language whatever is active.
    return {
        QVariantMap{{QStringLiteral("code"), QStringLiteral("en")}, {QStringLiteral("name"), QStringLiteral("English")}},
        QVariantMap{{QStringLiteral("code"), QStringLiteral("uk")}, {QStringLiteral("name"), QStringLiteral("Українська")}},
        QVariantMap{{QStringLiteral("code"), QStringLiteral("fr")}, {QStringLiteral("name"), QStringLiteral("Français")}},
        QVariantMap{{QStringLiteral("code"), QStringLiteral("es")}, {QStringLiteral("name"), QStringLiteral("Español")}},
        QVariantMap{{QStringLiteral("code"), QStringLiteral("ru")}, {QStringLiteral("name"), QStringLiteral("Русский")}},
    };
}

void AppController::setLanguage(const QString &code)
{
    m_settings.setValue(QStringLiteral("language"), code);
    emit settingsChanged();
    applyLanguage(code);
}

void AppController::applyLanguage(const QString &requested)
{
    QString code = requested;
    if (code.isEmpty()) {
        code = QStringLiteral("en");
        for (const QString &ui : QLocale::system().uiLanguages()) {
            const QString lang = ui.section(QLatin1Char('-'), 0, 0).toLower();
            if (kLanguages.contains(lang)) {
                code = lang;
                break;
            }
        }
    }
    if (!kLanguages.contains(code))
        code = QStringLiteral("en");

    QCoreApplication::removeTranslator(&m_appTranslator);
    QCoreApplication::removeTranslator(&m_qtTranslator);
    if (code != QLatin1String("en")) {
        if (m_appTranslator.load(QStringLiteral(":/i18n/shellwarden_") + code))
            QCoreApplication::installTranslator(&m_appTranslator);
        // Qt's own translations ship next to the app (windeployqt) or with Qt.
        // windeployqt ships "qt_<lang>.qm"; a Qt install has "qtbase_<lang>.qm".
        const QStringList dirs = {QCoreApplication::applicationDirPath() + QStringLiteral("/translations"),
                                  QLibraryInfo::path(QLibraryInfo::TranslationsPath)};
        bool qtLoaded = false;
        for (const QString &dir : dirs) {
            for (const QString &base : {QStringLiteral("qt_"), QStringLiteral("qtbase_")}) {
                if (!qtLoaded && m_qtTranslator.load(base + code, dir))
                    qtLoaded = true;
            }
        }
        if (qtLoaded)
            QCoreApplication::installTranslator(&m_qtTranslator);
    }

    m_language = code;
    m_updater.setLanguage(code);
    if (m_engine)
        m_engine->retranslate();
    emit languageChanged();
}

// ---------------------------------------------------------------------------
// settings & misc
// ---------------------------------------------------------------------------

QVariantMap AppController::settings() const
{
    QVariantMap out;
    const QVariantMap &d = settingDefaults();
    for (auto it = d.begin(); it != d.end(); ++it)
        out.insert(it.key(), m_settings.value(it.key(), it.value()));
    // QSettings returns strings for bools/ints from the INI/registry ("false" would
    // be true in QML): convert every value to the type of its default.
    for (auto it = d.begin(); it != d.end(); ++it) {
        const int t = it.value().typeId();
        if (t == QMetaType::Bool)
            out[it.key()] = out.value(it.key()).toBool();
        else if (t == QMetaType::Int)
            out[it.key()] = out.value(it.key()).toInt();
    }
    return out;
}

void AppController::setSetting(const QString &key, const QVariant &value)
{
    if (!settingDefaults().contains(key))
        return;
    m_settings.setValue(key, value);
    emit settingsChanged();

    if (key == QLatin1String("autoLockMinutes"))
        restartAutoLock();
    if (key == QLatin1String("hostProbe") || key == QLatin1String("killSwitch") || key == QLatin1String("defaultProxyId")) {
        m_lastHostProbe = 0;   // the route rules changed: check again now
        QTimer::singleShot(0, this, &AppController::probeHosts);
    }
    if (key == QLatin1String("logEnabled"))
        m_log.setEnabled(value.toBool());
    if (key == QLatin1String("monitorEnabled") || key == QLatin1String("monitorInterval")) {
        for (TerminalSession *s : m_sessions.items())
            s->setMonitorConfig(m_settings.value(QStringLiteral("monitorEnabled"), true).toBool(),
                                m_settings.value(QStringLiteral("monitorInterval"), 5).toInt());
    }
    if (key == QLatin1String("killSwitchInterval"))
        m_probeTimer.setInterval(std::max(1, value.toInt()) * 1000);
    if (key == QLatin1String("terminalTheme")) {
        for (TerminalSession *s : m_sessions.items()) {
            const QJsonObject h = findIn(kHosts, s->hostId());
            if (h.value(QStringLiteral("theme")).toString().isEmpty())
                s->setThemeId(value.toString());
        }
    }
    if (key == QLatin1String("killSwitch"))
        logEvent(ActivityLog::Security, QStringLiteral("killswitch"),
                 value.toBool() ? tr("Kill switch enabled") : tr("Kill switch disabled"));

    if (key == QLatin1String("killSwitch") && value.toBool()) {
        // Turning the kill switch on drops sessions that are not proxied.
        int killed = 0;
        for (TerminalSession *s : m_sessions.items()) {
            if (s->isActive() && !s->viaProxy()) {
                s->kill(tr("Kill switch enabled: direct session closed."));
                ++killed;
            }
        }
        if (killed)
            emit notify(tr("Kill switch closed %1 direct session(s).").arg(killed), true);
        if (!m_directIp.value(QStringLiteral("ok")).toBool())
            checkDirectIp(); // needed for the "proxy leaks real IP" check
    }
}

void AppController::copyText(const QString &text, bool sensitive)
{
    QGuiApplication::clipboard()->setText(text);
    if (sensitive) {
        m_sensitiveClipboard = text;
        m_clipboardClear.start();
    }
}

void AppController::restartAutoLock()
{
    const int minutes = m_settings.value(QStringLiteral("autoLockMinutes"), 15).toInt();
    if (minutes > 0 && m_vault.isUnlocked())
        m_autoLock.start(minutes * 60 * 1000);
    else
        m_autoLock.stop();
}

bool AppController::eventFilter(QObject *obj, QEvent *event)
{
    switch (event->type()) {
    case QEvent::Show:
        // every window of the app (main, detached terminals): dark title bar on Windows
        if (obj->isWindowType())
            applyDarkTitleBar(static_cast<QWindow *>(obj));
        break;
    case QEvent::KeyPress:
    case QEvent::MouseButtonPress:
    case QEvent::Wheel:
        if (m_autoLock.isActive())
            m_autoLock.start(); // restart with the same interval
        break;
    default:
        break;
    }
    return QObject::eventFilter(obj, event);
}

// ---------------------------------------------------------------------------
// countries: flag picture and name in the interface language
// ---------------------------------------------------------------------------

namespace {
const QJsonObject &countryTable()
{
    static const QJsonObject names = [] {
        QFile f(QStringLiteral(":/assets/countries.json"));
        return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
    }();
    return names;
}
} // namespace

QString AppController::countryName(const QString &code) const
{
    const QJsonObject n = countryTable().value(code.trimmed().toUpper()).toObject();
    const QString local = n.value(m_language).toString();
    return local.isEmpty() ? n.value(QStringLiteral("en")).toString() : local;
}

int AppController::countryFlag(const QString &code) const
{
    const QJsonObject n = countryTable().value(code.trimmed().toUpper()).toObject();
    return n.contains(QStringLiteral("flag")) ? n.value(QStringLiteral("flag")).toInt() : -1;
}

// ---------------------------------------------------------------------------
// Tor
// ---------------------------------------------------------------------------

void AppController::checkTor(const QString &mode)
{
    // in the background, so the proxy editor never freezes
    QPointer<AppController> self(this);
    const bool ownReady = m_tor.state() == QLatin1String("ready");
    QThreadPool::globalInstance()->start([self, mode, ownReady] {
        const bool browser = TorManager::probe(TorManager::kBrowserPort, 300);
        const bool service = TorManager::probe(TorManager::kServicePort, 300);
        QString which;
        int port = 0;
        if (mode == QLatin1String("exe")) { which = QStringLiteral("own"); port = ownReady ? TorManager::kOwnPort : 0; }
        else if ((mode == QLatin1String("browser") || mode == QLatin1String("auto")) && browser) { which = QStringLiteral("browser"); port = TorManager::kBrowserPort; }
        else if (mode != QLatin1String("browser") && service) { which = QStringLiteral("service"); port = TorManager::kServicePort; }
        else if (mode == QLatin1String("auto") && ownReady) { which = QStringLiteral("own"); port = TorManager::kOwnPort; }
        const QString exe = TorManager::findTorExe();
        QMetaObject::invokeMethod(self, [self, mode, port, which, browser, service, exe] {
            if (!self) return;
            emit self->torStatusReady(QVariantMap{{QStringLiteral("mode"), mode}, {QStringLiteral("port"), port},
                {QStringLiteral("which"), which}, {QStringLiteral("browser"), browser}, {QStringLiteral("service"), service},
                {QStringLiteral("exe"), exe}, {QStringLiteral("checked"), true}});
        }, Qt::QueuedConnection);
    });
}

void AppController::torNewIdentity(const QString &proxyId)
{
    m_tor.newIdentity(proxyId);
    logEvent(ActivityLog::Info, QStringLiteral("proxy"), tr("Tor: new identity"), QString(),
             findIn(kProxies, proxyId).value(QStringLiteral("name")).toString());
    checkProxy(proxyId);   // shows the new exit IP and country
    emit notify(tr("New Tor identity: new connections use a different exit. Open sessions keep their route until reconnect."), false);
}

// ---------------------------------------------------------------------------
// run a command on several hosts
// ---------------------------------------------------------------------------

QString AppController::runOnHosts(const QVariantList &hostIds, const QString &command, const QString &masterPassword)
{
    if (!m_vault.isUnlocked())
        return tr("The vault is locked.");
    if (command.trimmed().isEmpty())
        return tr("Enter a command.");
    if (hostIds.isEmpty())
        return tr("Choose at least one host.");
    // one master password unlocks every "ask the master password" host of this run
    const bool unlocked = !masterPassword.isEmpty() && m_vault.verifyPassword(masterPassword);
    if (!masterPassword.isEmpty() && !unlocked)
        return tr("Wrong master password.");
    if (unlocked)
        logEvent(ActivityLog::Security, QStringLiteral("session"), tr("Protected hosts unlocked with the master password for a batch run"));
    QVector<BatchRunner::Job> jobs;
    QStringList names;
    for (const QVariant &v : hostIds) {
        BatchRunner::Job j = batchJob(v.toString(), unlocked);
        if (j.hostId.isEmpty())
            continue;
        names << j.label;
        jobs << j;
    }
    m_batch.run(jobs, command);
    for (QString &p : m_batchPass) secure::wipe(p);
    m_batchPass.clear();
    logEvent(ActivityLog::Info, QStringLiteral("session"), tr("Command run on %n host(s)", nullptr, int(jobs.size())),
             command.left(200), names.join(QStringLiteral(", ")));
    return {};
}

BatchRunner::Job AppController::batchJob(const QString &hostId, bool protectedOk) const
{
    BatchRunner::Job j;
    const QJsonObject host = findIn(kHosts, hostId);
    if (host.isEmpty())
        return j;
    const bool killSwitch = m_settings.value(QStringLiteral("killSwitch"), false).toBool();
    j.hostId = hostId;
    j.label = host.value(QStringLiteral("label")).toString();
    j.endpoint = QStringLiteral("%1@%2").arg(host.value(QStringLiteral("username")).toString(),
                                             host.value(QStringLiteral("hostname")).toString());
    const QJsonObject jumpHost = findIn(kHosts, host.value(QStringLiteral("jumpHostId")).toString());
    const QString proxyId = effectiveProxyId(jumpHost.isEmpty() ? host : jumpHost);
    if (host.value(QStringLiteral("type")).toString() == QLatin1String("serial"))
        j.skipReason = tr("Serial connection: not supported here.");
    else if (host.value(QStringLiteral("type")).toString() == QLatin1String("telnet"))
        j.skipReason = tr("Telnet host: commands can only be run over SSH.");
    else if (!protectedOk && host.value(QStringLiteral("confirmMaster")).toBool())
        j.skipReason = tr("Asks for the master password: enter it to include this host.");
    else if (killSwitch && proxyId.isEmpty())
        j.skipReason = tr("Kill switch: this host has no proxy.");
    else {
        j.params = paramsFor(host, jumpHost);
        j.params.killSwitch = killSwitch;
        j.params.keepaliveSeconds = 0;
        // key passphrase typed once in the batch window (not saved anywhere)
        const QString keyId = host.value(QStringLiteral("keyId")).toString();
        if (j.params.keyPassphrase.isEmpty() && m_batchPass.contains(keyId))
            j.params.keyPassphrase = m_batchPass.value(keyId);
        if (!proxyId.isEmpty()) {
            const QJsonObject px = findIn(kProxies, proxyId);
            const QString firstHop = (jumpHost.isEmpty() ? host : jumpHost).value(QStringLiteral("hostname")).toString();
            j.params.proxy = proxyConfig(px, firstHop);
            if (px.value(QStringLiteral("type")).toString() == QLatin1String("cloudflare")) {
                if (!j.params.proxy.isSet())
                    j.skipReason = tr("Cloudflare Tunnel is not started: open this host in a tab once.");
                else
                    j.params.connectTimeoutSeconds = 60;
            }
        }
    }
    return j;
}

// Read the same (or another) file on two hosts; the diff is drawn in QML.
QString AppController::compareFiles(const QString &hostA, const QString &pathA, const QString &hostB, const QString &pathB)
{
    if (!m_vault.isUnlocked())
        return tr("The vault is locked.");
    if (pathA.trimmed().isEmpty() || pathB.trimmed().isEmpty())
        return tr("Enter the file path.");
    auto quote = [](QString p) { p = p.trimmed(); p.replace(QLatin1Char('\''), QStringLiteral("'\\''")); return QLatin1Char('\'') + p + QLatin1Char('\''); };
    BatchRunner::Job a = batchJob(hostA), b = batchJob(hostB);
    if (a.hostId.isEmpty() || b.hostId.isEmpty())
        return tr("Choose two hosts.");
    a.command = QStringLiteral("cat -- ") + quote(pathA);
    b.command = QStringLiteral("cat -- ") + quote(pathB);
    m_diff.run({a, b}, QString());
    return {};
}

// ---------------------------------------------------------------------------
// local terminal (optional, Settings → Terminal)
// ---------------------------------------------------------------------------

QVariantList AppController::localShells() const
{
    QVariantList out;
    auto add = [&out](const QString &id, const QString &name, const QString &program, const QStringList &args = {}) {
        if (!program.isEmpty() && QFileInfo::exists(program))
            out << QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), name},
                               {QStringLiteral("program"), program}, {QStringLiteral("args"), args}};
    };
#ifdef Q_OS_WIN
    const QString sys = qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows")) + QStringLiteral("\\System32");
    add(QStringLiteral("pwsh"), QStringLiteral("PowerShell 7"), QStandardPaths::findExecutable(QStringLiteral("pwsh")), {QStringLiteral("-NoLogo")});
    add(QStringLiteral("powershell"), QStringLiteral("Windows PowerShell"), sys + QStringLiteral("\\WindowsPowerShell\\v1.0\\powershell.exe"), {QStringLiteral("-NoLogo")});
    add(QStringLiteral("cmd"), tr("Command Prompt (cmd)"), sys + QStringLiteral("\\cmd.exe"));
    add(QStringLiteral("wsl"), QStringLiteral("WSL (Linux)"), sys + QStringLiteral("\\wsl.exe"));
    for (const QString &g : {qEnvironmentVariable("ProgramFiles") + QStringLiteral("\\Git\\bin\\bash.exe"),
                             qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("\\Programs\\Git\\bin\\bash.exe")}) {
        if (QFileInfo::exists(g)) { add(QStringLiteral("gitbash"), QStringLiteral("Git Bash"), g, {QStringLiteral("--login"), QStringLiteral("-i")}); break; }
    }
#else
    const QString sh = qEnvironmentVariable("SHELL");
    if (!sh.isEmpty())
        add(QStringLiteral("default"), tr("Default shell (%1)").arg(QFileInfo(sh).fileName()), sh, {QStringLiteral("-l")});
    for (const char *n : {"bash", "zsh", "fish"}) {
        const QString p = QStandardPaths::findExecutable(QString::fromLatin1(n));
        if (!p.isEmpty() && p != sh)
            add(QString::fromLatin1(n), QString::fromLatin1(n), p, {QStringLiteral("-l")});
    }
#endif
    return out;
}

void AppController::openLocalTerminal(const QString &shellId)
{
    const QVariantList shells = localShells();
    if (shells.isEmpty()) {
        emit notify(tr("No shell found on this computer."), true);
        return;
    }
    const QString want = shellId.isEmpty() ? m_settings.value(QStringLiteral("localShell")).toString() : shellId;
    QVariantMap sh = shells.first().toMap();
    for (const QVariant &v : shells)
        if (v.toMap().value(QStringLiteral("id")).toString() == want)
            sh = v.toMap();
    const QString program = sh.value(QStringLiteral("program")).toString();
    const QStringList args = sh.value(QStringLiteral("args")).toStringList();
    const QString tabName = sh.value(QStringLiteral("id")).toString() == QLatin1String("default")
                                ? QFileInfo(program).fileName() : sh.value(QStringLiteral("name")).toString();
    TerminalSession *s = createSession(QString(), tabName, tr("this computer"), QStringLiteral("local"));
    connect(s, &TerminalSession::reconnectRequested, s, [s, program, args] { s->beginLocal(program, args); });
    s->beginLocal(program, args);
    logEvent(ActivityLog::Info, QStringLiteral("session"), tr("Local terminal opened"), program);
}

QVariantMap AppController::agentStatus(bool withKeys) const
{
    QVariantMap m;
    const QString name = agent::available();
    m.insert(QStringLiteral("name"), name);
    m.insert(QStringLiteral("available"), !name.isEmpty());
    if (withKeys && !name.isEmpty()) {
        QString err;
        const int n = agent::identityCount(&err);
        m.insert(QStringLiteral("keys"), n);
        if (n < 0)
            m.insert(QStringLiteral("error"), err.isEmpty() ? tr("The agent did not answer.") : err);
    }
    return m;
}

// ---------------------------------------------------------------------------
// put public keys from the keychain on servers (like ssh-copy-id, many at once)
// ---------------------------------------------------------------------------

bool AppController::hostIsProtected(const QString &hostId) const
{
    const QJsonObject h = findIn(kHosts, hostId);
    return h.value(QStringLiteral("confirmMaster")).toBool();
}

QString AppController::installKeys(const QVariantList &pairs, const QString &masterPassword, bool useForHost)
{
    if (!m_vault.isUnlocked())
        return tr("The vault is locked.");
    if (pairs.isEmpty())
        return tr("Choose at least one host.");
    const bool unlocked = !masterPassword.isEmpty() && m_vault.verifyPassword(masterPassword);
    if (!masterPassword.isEmpty() && !unlocked)
        return tr("Wrong master password.");
    QVector<BatchRunner::Job> jobs;
    m_keyAssign.clear();
    QStringList names;
    for (const QVariant &v : pairs) {
        const QVariantMap m = v.toMap();
        const QString hostId = m.value(QStringLiteral("hostId")).toString();
        const QString keyId = m.value(QStringLiteral("keyId")).toString();
        QString line = publicKeyLine(keyId).trimmed();
        line.remove(QLatin1Char('\''));   // never break the shell quoting (a quote can only be in the comment)
        BatchRunner::Job j = batchJob(hostId, unlocked);
        if (j.hostId.isEmpty())
            continue;
        if (line.isEmpty()) {
            j.skipReason = tr("Choose a key.");
        } else {
            const QString blob = line.section(QLatin1Char(' '), 1, 1);
            // the script goes as base64, so any login shell (sh, bash, zsh, fish) runs it the same way
            const QString script = QStringLiteral(
                "umask 077\n"
                "mkdir -p ~/.ssh && touch ~/.ssh/authorized_keys && chmod 700 ~/.ssh && chmod 600 ~/.ssh/authorized_keys || exit 1\n"
                "if grep -qF '%1' ~/.ssh/authorized_keys; then echo 'the key was already there'; "
                "else printf '%s\\n' '%2' >> ~/.ssh/authorized_keys && echo 'key added'; fi\n").arg(blob, line);
            j.command = QStringLiteral("echo %1 | base64 -d | sh").arg(QString::fromLatin1(script.toUtf8().toBase64()));
            if (useForHost)
                m_keyAssign.insert(hostId, keyId);
        }
        names << j.label;
        jobs << j;
    }
    m_batch.run(jobs, QString());
    for (QString &p : m_batchPass) secure::wipe(p);
    m_batchPass.clear();
    logEvent(ActivityLog::Security, QStringLiteral("key"), tr("Public keys sent to %n server(s)", nullptr, int(jobs.size())),
             QString(), names.join(QStringLiteral(", ")));
    return {};
}

QString AppController::rotateKey(const QString &oldKeyId, const QString &newKeyId, const QVariantList &hostIds,
                                 const QString &masterPassword)
{
    if (m_batch.running())
        return tr("Another run is still going.");
    const QJsonObject oldKey = findIn(kKeys, oldKeyId), newKey = findIn(kKeys, newKeyId);
    if (oldKey.isEmpty() || newKey.isEmpty())
        return tr("Choose the old and the new key.");
    const QString oldBlob = publicKeyLine(oldKeyId).section(QLatin1Char(' '), 1, 1);
    const QString newBlob = publicKeyLine(newKeyId).section(QLatin1Char(' '), 1, 1);
    if (oldKeyId == newKeyId || oldBlob.isEmpty() || oldBlob == newBlob)
        return tr("The new key must be a different key.");
    if (newKey.value(QStringLiteral("privateKey")).toString().isEmpty())
        return tr("Only the public part of the new key is in the keychain, so ShellWarden cannot log in with it. Choose a key with its private part.");
    // the new key's passphrase is needed again in step 2, after installKeys() forgets the typed ones
    m_rotateNewPass = m_batchPass.value(newKeyId);
    if (m_rotateNewPass.isEmpty())
        m_rotateNewPass = newKey.value(QStringLiteral("passphrase")).toString();
    if (newKey.value(QStringLiteral("encrypted")).toBool() && m_rotateNewPass.isEmpty())
        return tr("Enter the passphrase of the new key: it is needed to check that the new key works.");
    QVariantList pairs;
    for (const QVariant &h : hostIds)
        pairs << QVariantMap{{QStringLiteral("hostId"), h.toString()}, {QStringLiteral("keyId"), newKeyId}};
    const QString err = installKeys(pairs, masterPassword, false);
    if (!err.isEmpty()) {
        secure::wipe(m_rotateNewPass);
        return err;
    }
    m_rotateOld = oldKeyId;
    m_rotateNew = newKeyId;
    m_rotateUnlocked = !masterPassword.isEmpty();
    m_rotateFailed.clear();
    m_rotatePhase = QStringLiteral("adding");
    emit rotateChanged();
    logEvent(ActivityLog::Security, QStringLiteral("key"), tr("Key replacement started"),
             oldKey.value(QStringLiteral("name")).toString() + QStringLiteral(" → ") + newKey.value(QStringLiteral("name")).toString());
    return {};
}

// Runs a script on every server that passed the previous step, logged in with ONLY
// the new key (no password, no agent, no old key). Servers that failed before are
// listed as skipped with the reason.
void AppController::rotateNewKeyStep(const QString &phase, const QString &script, const QString &failedBefore)
{
    const QJsonObject newKey = findIn(kKeys, m_rotateNew);
    const QString command = QStringLiteral("echo %1 | base64 -d | sh").arg(QString::fromLatin1(script.toUtf8().toBase64()));
    QVector<BatchRunner::Job> jobs;
    for (const QVariant &r : m_batch.results()) {
        const QVariantMap m = r.toMap();
        const QString hostId = m.value(QStringLiteral("hostId")).toString();
        BatchRunner::Job j = batchJob(hostId, m_rotateUnlocked);
        if (j.hostId.isEmpty())
            continue;
        if (m.value(QStringLiteral("state")).toString() != QLatin1String("ok")) {
            j.skipReason = failedBefore.arg(m.value(QStringLiteral("error")).toString());
        } else if (j.skipReason.isEmpty()) {
            j.params.privateKey = newKey.value(QStringLiteral("privateKey")).toString().toUtf8();
            j.params.keyPassphrase = m_rotateNewPass;
            secure::wipe(j.params.password);
            j.params.useAgent = false;
            j.params.agentForward = false;
            j.command = command;
        }
        jobs << j;
    }
    m_rotatePhase = phase;
    emit rotateChanged();
    m_batch.run(jobs, QString());
}

void AppController::rotateSecondStep()
{
    const QString oldBlob = publicKeyLine(m_rotateOld).section(QLatin1Char(' '), 1, 1);
    const QString newBlob = publicKeyLine(m_rotateNew).section(QLatin1Char(' '), 1, 1);
    // we got in with the new key; remove the old one only when the new one is there
    rotateNewKeyStep(QStringLiteral("removing"), QStringLiteral(
        "umask 077\n"
        "f=~/.ssh/authorized_keys\n"
        "grep -qF '%1' \"$f\" || { echo 'the new key is missing, the old key was kept'; exit 1; }\n"
        "if grep -qF '%2' \"$f\"; then grep -vF '%2' \"$f\" > \"$f.sw\" && cat \"$f.sw\" > \"$f\" && rm -f \"$f.sw\" && echo 'old key removed'; "
        "else echo 'the old key was not there'; fi\n").arg(newBlob, oldBlob),
        tr("The new key could not be added, nothing changed: %1"));
}

void AppController::rotateThirdStep()
{
    const QString oldBlob = publicKeyLine(m_rotateOld).section(QLatin1Char(' '), 1, 1);
    const QString newBlob = publicKeyLine(m_rotateNew).section(QLatin1Char(' '), 1, 1);
    // a fresh login with the new key: is the new key there and the old one really gone?
    rotateNewKeyStep(QStringLiteral("verifying"), QStringLiteral(
        "f=~/.ssh/authorized_keys\n"
        "grep -qF '%1' \"$f\" || { echo 'the new key is missing'; exit 1; }\n"
        "if grep -qF '%2' \"$f\"; then echo 'the old key is still there'; exit 1; fi\n"
        "echo verified\n").arg(newBlob, oldBlob),
        tr("The new key did not work here, so the old key was kept: %1"));
}

void AppController::rotateFinish()
{
    QJsonArray hosts = section(kHosts);
    int changed = 0;
    for (const QVariant &r : m_batch.results()) {
        const QVariantMap m = r.toMap();
        if (m.value(QStringLiteral("state")).toString() != QLatin1String("ok"))
            continue;
        const QString hostId = m.value(QStringLiteral("hostId")).toString();
        for (int i = 0; i < hosts.size(); ++i) {
            QJsonObject o = hosts.at(i).toObject();
            if (o.value(QStringLiteral("id")).toString() == hostId) {
                o.insert(QStringLiteral("keyId"), m_rotateNew);   // log in with the new key from now on
                hosts.replace(i, o);
                ++changed;
            }
        }
    }
    if (changed) {
        setSection(kHosts, hosts);
        persist();
        m_hosts.reset(section(kHosts));
    }
    logEvent(ActivityLog::Security, QStringLiteral("key"), tr("Key replaced on %n server(s)", nullptr, changed),
             tr("%1 of %2 servers").arg(changed).arg(m_batch.results().size()));
    secure::wipe(m_rotateNewPass);
    m_rotatePhase = QStringLiteral("done");
    emit rotateChanged();
}

void AppController::onBatchFinished()
{
    if (m_rotatePhase == QLatin1String("adding")) {
        QTimer::singleShot(0, this, &AppController::rotateSecondStep);   // not from inside the runner's signal
        return;
    }
    if (m_rotatePhase == QLatin1String("removing")) {
        QTimer::singleShot(0, this, &AppController::rotateThirdStep);
        return;
    }
    if (m_rotatePhase == QLatin1String("verifying")) {
        rotateFinish();   // only servers that passed the check switch to the new key
        return;
    }
    if (m_keyAssign.isEmpty())
        return;
    // after a successful key install: let the host log in with that key from now on
    QJsonArray hosts = section(kHosts);
    int changed = 0;
    for (const QVariant &r : m_batch.results()) {
        const QVariantMap m = r.toMap();
        const QString hostId = m.value(QStringLiteral("hostId")).toString();
        if (m.value(QStringLiteral("state")).toString() != QLatin1String("ok") || !m_keyAssign.contains(hostId))
            continue;
        for (int i = 0; i < hosts.size(); ++i) {
            QJsonObject o = hosts.at(i).toObject();
            if (o.value(QStringLiteral("id")).toString() == hostId) {
                o.insert(QStringLiteral("keyId"), m_keyAssign.value(hostId));
                hosts.replace(i, o);
                ++changed;
            }
        }
    }
    m_keyAssign.clear();
    if (changed) {
        setSection(kHosts, hosts);
        persist();
        m_hosts.reset(section(kHosts));
        emit notify(tr("The key is now used to log in on %n server(s).", nullptr, changed), false);
    }
}

// notes of a host (stored encrypted in the vault with the host)
QString AppController::hostNotes(const QString &hostId) const
{
    return findIn(kHosts, hostId).value(QStringLiteral("notes")).toString();
}

QString AppController::setHostNotes(const QString &hostId, const QString &notes)
{
    QJsonArray hosts = section(kHosts);
    for (int i = 0; i < hosts.size(); ++i) {
        QJsonObject o = hosts.at(i).toObject();
        if (o.value(QStringLiteral("id")).toString() != hostId)
            continue;
        o.insert(QStringLiteral("notes"), notes);
        hosts.replace(i, o);
        setSection(kHosts, hosts);
        if (!persist())
            return tr("Could not save the vault.");
        m_hosts.reset(section(kHosts));
        return {};
    }
    return tr("This host no longer exists.");
}

// Keys of these hosts that need a passphrase which is not saved in the vault:
// the batch window asks for each of them once.
QVariantList AppController::batchKeyNeeds(const QVariantList &hostIds, const QVariantList &extraKeyIds) const
{
    QVariantList out;
    QSet<QString> seen;
    for (const QVariant &v : extraKeyIds) {   // e.g. the new key of a key replacement
        const QJsonObject k = findIn(kKeys, v.toString());
        if (k.isEmpty() || seen.contains(v.toString()))
            continue;
        if (k.value(QStringLiteral("encrypted")).toBool() && k.value(QStringLiteral("passphrase")).toString().isEmpty()) {
            seen.insert(v.toString());
            out << QVariantMap{{QStringLiteral("keyId"), v.toString()}, {QStringLiteral("name"), k.value(QStringLiteral("name")).toString()}};
        }
    }
    for (const QVariant &v : hostIds) {
        const QJsonObject h = findIn(kHosts, v.toString());
        const QString keyId = h.value(QStringLiteral("keyId")).toString();
        if (keyId.isEmpty() || seen.contains(keyId))
            continue;
        const QJsonObject k = findIn(kKeys, keyId);
        if (k.isEmpty())
            continue;
        const bool encrypted = k.value(QStringLiteral("encrypted")).toBool();
        const bool saved = !k.value(QStringLiteral("passphrase")).toString().isEmpty() && !h.value(QStringLiteral("alwaysAskPassphrase")).toBool();
        if (encrypted && !saved) {
            seen.insert(keyId);
            out << QVariantMap{{QStringLiteral("keyId"), keyId}, {QStringLiteral("name"), k.value(QStringLiteral("name")).toString()}};
        }
    }
    return out;
}

QString AppController::setBatchPassphrases(const QVariantMap &map)
{
    for (QString &p : m_batchPass) secure::wipe(p);
    m_batchPass.clear();
    for (auto it = map.begin(); it != map.end(); ++it) {
        const QString pass = it.value().toString();
        const QJsonObject k = findIn(kKeys, it.key());
        if (pass.isEmpty())
            continue;
        if (!keytools::checkPassphrase(k.value(QStringLiteral("privateKey")).toString().toUtf8(), pass))
            return tr("Wrong passphrase for the key \"%1\".").arg(k.value(QStringLiteral("name")).toString());
        m_batchPass.insert(it.key(), pass);
    }
    return {};
}
