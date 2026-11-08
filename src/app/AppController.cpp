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
               QStringLiteral("useAgent"), QStringLiteral("agentForward"), QStringLiteral("protected")},
              {QStringLiteral("password"), QStringLiteral("notes")}),
      m_keys({QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("type"),
              QStringLiteral("fingerprint"), QStringLiteral("publicKey"), QStringLiteral("encrypted"),
              QStringLiteral("created"), QStringLiteral("protected")},
             {QStringLiteral("passphrase"), QStringLiteral("privateKey")}),   // hasPrivateKey: not only a public key
      m_proxies({QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("type"),
                 QStringLiteral("host"), QStringLiteral("port"), QStringLiteral("username"),
                 QStringLiteral("checkState"), QStringLiteral("exitIp"), QStringLiteral("location"),
                 QStringLiteral("exitCountry"), QStringLiteral("disabled"),
                 QStringLiteral("latencyMs"), QStringLiteral("checkError"), QStringLiteral("checkedAt"),
                 QStringLiteral("checkSummary"), QStringLiteral("checkHint"), QStringLiteral("checkErrors")},
                {QStringLiteral("password")}),
      m_snippets({QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("command"),
                  QStringLiteral("group"), QStringLiteral("autoRun"), QStringLiteral("script")}, {}),
      m_log(paths::logsDir())
{
    m_log.setEnabled(m_settings.value(QStringLiteral("logEnabled"), true).toBool());
    m_log.setRetentionDays(m_settings.value(QStringLiteral("logRetentionDays"), 30).toInt());   // drops whole old months
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

    knownhosts::hashFile(m_knownHostsPath);   // older versions wrote plain host names
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
        if (const QString w = ensureVaultKeys(password); !w.isEmpty())
            logEvent(ActivityLog::Warning, QStringLiteral("vault"), w);
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
        const QString warning = ensureVaultKeys(password);
        logEvent(ActivityLog::Success, QStringLiteral("vault"), tr("Vault unlocked"));
        if (!warning.isEmpty())
            logEvent(ActivityLog::Security, QStringLiteral("vault"), warning);
        migrateRemovedFeatures();
        if (sealAllProtected())
            persist();
        reloadModels();
        refreshKnownHosts();
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
    m_log.close();   // the journal can still be written, but not read
    m_hosts.reset({});
    m_keys.reset({});
    m_proxies.reset({});
    m_snippets.reset({});
    m_autoLock.stop();
    refreshKnownHosts();
    emit vaultStateChanged();
}

QString AppController::changeMasterPassword(const QString &oldPw, const QString &newPw, const QString &confirm)
{
    if (newPw != confirm)
        return tr("New passwords do not match.");
    if (newPw.size() < passwordtools::kManualMinLength)
        return tr("The master password must be at least 8 characters.");
    QString err;
    // Protected secrets are sealed for a key pair that comes from the master
    // password: open them with the old one and seal them for the new one,
    // in memory, before the vault is written with the new password.
    const QJsonObject before = m_vault.data();
    const QJsonObject prot = before.value(QStringLiteral("protect")).toObject();
    if (!prot.isEmpty()) {
        auto *oldSk = static_cast<unsigned char *>(sodium_malloc(protect::kSecretLen));
        auto *newSk = static_cast<unsigned char *>(sodium_malloc(protect::kSecretLen));
        auto *seed = static_cast<unsigned char *>(sodium_malloc(32));
        auto freeAll = [&] { sodium_free(oldSk); sodium_free(newSk); sodium_free(seed); };
        if (!oldSk || !newSk || !seed) {
            freeAll();
            return tr("Not enough secure memory.");
        }
        if (!deriveProtectSecret(oldPw, oldSk)) {
            freeAll();
            logEvent(ActivityLog::Security, QStringLiteral("vault"), tr("Master password change failed"), tr("Wrong master password."));
            return tr("Wrong master password.");
        }
        QByteArray salt(protect::kSaltLen, Qt::Uninitialized);
        randombytes_buf(salt.data(), size_t(salt.size()));
        if (!m_vault.deriveWithVaultCost(newPw, salt, seed, &err)) {
            freeAll();
            return err;
        }
        QByteArray newPk;
        protect::keypairFromSeed(seed, &newPk, newSk);
        const QByteArray oldPk = QByteArray::fromBase64(prot.value(QStringLiteral("pk")).toString().toLatin1());
        bool ok = true;
        for (const QString &sec : {QString(kHosts), QString(kKeys)}) {
            QJsonArray list = section(sec);
            for (int i = 0; i < list.size(); ++i) {
                QJsonObject o = list.at(i).toObject();
                QJsonObject sealed = o.value(QStringLiteral("sealed")).toObject();
                if (sealed.isEmpty())
                    continue;
                for (auto it = sealed.begin(); it != sealed.end(); ++it) {
                    QByteArray plain;
                    ok = protect::open(it.value().toString().toLatin1(), oldPk, oldSk, &plain) && ok;
                    it.value() = QString::fromLatin1(protect::seal(plain, newPk));
                    sodium_memzero(plain.data(), size_t(plain.size()));
                }
                o.insert(QStringLiteral("sealed"), sealed);
                list.replace(i, o);
            }
            setSection(sec, list);
        }
        m_vault.data().insert(QStringLiteral("protect"), QJsonObject{{QStringLiteral("salt"), QString::fromLatin1(salt.toBase64())},
                                                                    {QStringLiteral("pk"), QString::fromLatin1(newPk.toBase64())}});
        freeAll();
        if (!ok) {
            m_vault.data() = before;
            return tr("A protected secret could not be decrypted; nothing was changed.");
        }
    }
    if (!m_vault.changePassword(oldPw, newPw, &err)) {
        m_vault.data() = before;   // keep the seals that match the (unchanged) password
        logEvent(ActivityLog::Security, QStringLiteral("vault"), tr("Master password change failed"), err);
        return err;
    }
    // The backups were made under the old password: a password changed because
    // it may have leaked must not keep opening them.
    m_pwReport = reencryptBackups(oldPw);
    logEvent(ActivityLog::Security, QStringLiteral("vault"), tr("Master password changed"),
             tr("%n backup(s) re-encrypted with the new password", "", m_pwReport.value(QStringLiteral("reencrypted")).toInt())
                 + (m_pwReport.value(QStringLiteral("stale")).toList().isEmpty()
                        ? QString()
                        : QStringLiteral(" · ") + tr("%n older backup(s) not opened by the old password", "",
                                                     int(m_pwReport.value(QStringLiteral("stale")).toList().size()))));
    return {};
}

QVariantMap AppController::reencryptBackups(const QString &oldPw)
{
    int done = 0;
    QStringList stale;
    const QJsonObject newProtect = m_vault.data().value(QStringLiteral("protect")).toObject();
    const QByteArray newPk = QByteArray::fromBase64(newProtect.value(QStringLiteral("pk")).toString().toLatin1());
    Vault::KeyCache cache;
    // old protect secret keys by their salt (most backups share one)
    QHash<QByteArray, unsigned char *> oldSks;
    auto oldSkFor = [&](const QJsonObject &prot) -> unsigned char * {
        const QByteArray salt = QByteArray::fromBase64(prot.value(QStringLiteral("salt")).toString().toLatin1());
        const QByteArray pk = QByteArray::fromBase64(prot.value(QStringLiteral("pk")).toString().toLatin1());
        if (salt.size() != protect::kSaltLen || pk.size() != protect::kPublicLen)
            return nullptr;
        if (oldSks.contains(salt))
            return oldSks.value(salt);
        auto *seed = static_cast<unsigned char *>(sodium_malloc(32));
        auto *sk = static_cast<unsigned char *>(sodium_malloc(protect::kSecretLen));
        QString err;
        bool ok = seed && sk && m_vault.deriveWithVaultCost(oldPw, salt, seed, &err);
        if (ok) {
            QByteArray derived;
            protect::keypairFromSeed(seed, &derived, sk);
            ok = sodium_memcmp(derived.constData(), pk.constData(), protect::kPublicLen) == 0;
        }
        if (seed)
            sodium_free(seed);
        if (!ok && sk) {
            sodium_free(sk);
            sk = nullptr;
        }
        oldSks.insert(salt, sk);
        return sk;
    };

    const QDir dir(paths::backupsDir());
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.swv"), QStringLiteral("*.swv.bak")}, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files) {
        QJsonObject data;
        if (Vault::readFile(fi.absoluteFilePath(), oldPw, &cache, &data) != Vault::ReadResult::Ok) {
            stale << fi.fileName();
            continue;
        }
        // protected secrets inside: open with the old protect key, seal for the new one
        bool ok = true;
        const QJsonObject prot = data.value(QStringLiteral("protect")).toObject();
        if (!prot.isEmpty()) {
            unsigned char *sk = oldSkFor(prot);
            const QByteArray oldPk = QByteArray::fromBase64(prot.value(QStringLiteral("pk")).toString().toLatin1());
            ok = sk && newPk.size() == protect::kPublicLen;
            for (const QString &sec : {QString(kHosts), QString(kKeys)}) {
                QJsonArray list = data.value(sec).toArray();
                for (int i = 0; ok && i < list.size(); ++i) {
                    QJsonObject o = list.at(i).toObject();
                    QJsonObject sealed = o.value(QStringLiteral("sealed")).toObject();
                    if (sealed.isEmpty())
                        continue;
                    for (auto it = sealed.begin(); ok && it != sealed.end(); ++it) {
                        QByteArray plain;
                        ok = protect::open(it.value().toString().toLatin1(), oldPk, sk, &plain);
                        if (ok) {
                            const QByteArray box = protect::seal(plain, newPk);
                            ok = !box.isEmpty();
                            it.value() = QString::fromLatin1(box);
                        }
                        secure::wipe(plain);
                    }
                    o.insert(QStringLiteral("sealed"), sealed);
                    list.replace(i, o);
                }
                data.insert(sec, list);
            }
            data.insert(QStringLiteral("protect"), newProtect);
        }
        if (!ok || !m_vault.writeCopy(fi.absoluteFilePath(), data, nullptr)) {
            stale << fi.fileName();
            continue;
        }
        ++done;
    }
    for (unsigned char *sk : std::as_const(oldSks))
        if (sk)
            sodium_free(sk);
    QVariantList staleList;
    for (const QString &n : std::as_const(stale))
        staleList << n;
    return {{QStringLiteral("reencrypted"), done}, {QStringLiteral("stale"), staleList}};
}

QString AppController::deleteBackups(const QStringList &names)
{
    const QDir dir(paths::backupsDir());
    QStringList failed;
    for (const QString &n : names) {
        // only plain file names inside the backups folder
        if (n.contains(QLatin1Char('/')) || n.contains(QLatin1Char('\\')) || n.startsWith(QLatin1Char('.'))
            || !(n.endsWith(QLatin1String(".swv")) || n.endsWith(QLatin1String(".swv.bak"))))
            continue;
        if (!QFile::remove(dir.filePath(n)))
            failed << n;
    }
    logEvent(ActivityLog::Security, QStringLiteral("vault"), tr("Old backups deleted"),
             QString::number(names.size() - failed.size()));
    return failed.isEmpty() ? QString() : tr("Could not delete: %1").arg(failed.join(QStringLiteral(", ")));
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
    if (findIn(kHosts, hostId).value(QStringLiteral("noHistory")).toBool())
        return;   // turned off for this host
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
                if (h.value(QStringLiteral("confirmMaster")).toBool())
                    sealObject(h, {QStringLiteral("password")});
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
                if (isSealed(k))
                    sealObject(k, {QStringLiteral("passphrase")});
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
    // Names in the file are hashed; while the vault is open, show which of your
    // hosts an entry belongs to (the file itself never says).
    const QJsonArray hosts = m_vault.isUnlocked() ? section(kHosts) : QJsonArray();
    for (const knownhosts::Entry &e : knownhosts::load(m_knownHostsPath)) {
        QString name;
        if (e.hosts.startsWith(QLatin1String("|1|"))) {
            for (const QJsonValue &v : hosts) {
                const QJsonObject h = v.toObject();
                const QString hn = h.value(QStringLiteral("hostname")).toString();
                const int port = h.value(QStringLiteral("port")).toInt(22);
                if (!hn.isEmpty() && knownhosts::matches(e.hosts, hn, port)) {
                    name = QStringLiteral("%1 · %2").arg(h.value(QStringLiteral("label")).toString(), knownhosts::hostPattern(hn, port));
                    break;
                }
            }
        } else {
            name = e.hosts;
        }
        m_knownHosts.append(QVariantMap{
            {QStringLiteral("line"), e.line},
            {QStringLiteral("hosts"), e.hosts},
            {QStringLiteral("name"), name},
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
    if (key == QLatin1String("logRetentionDays"))
        m_log.setRetentionDays(value.toInt());
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
