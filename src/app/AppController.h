#pragma once

#include "ProxyChecker.h"
#include "SessionListModel.h"
#include "UpdateChecker.h"
#include "SystemLockWatcher.h"
#include "TorManager.h"
#include "CloudflaredManager.h"
#include "BatchRunner.h"
#include <functional>
#include "VaultListModel.h"
#include "core/ActivityLog.h"
#include "core/Vault.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QTimer>
#include <QTranslator>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class TerminalSession;
class QQmlEngine;

// The single object QML talks to ("App"). Owns the vault, the list models,
// open sessions, the kill switch, the activity log and the updater.
class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString appName READ appName CONSTANT)
    Q_PROPERTY(bool vaultExists READ vaultExists NOTIFY vaultStateChanged)
    Q_PROPERTY(bool vaultUnlocked READ vaultUnlocked NOTIFY vaultStateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QObject *hosts READ hosts CONSTANT)
    Q_PROPERTY(QObject *keys READ keys CONSTANT)
    Q_PROPERTY(QObject *proxies READ proxies CONSTANT)
    Q_PROPERTY(QObject *snippets READ snippets CONSTANT)
    Q_PROPERTY(QObject *sessions READ sessions CONSTANT)
    Q_PROPERTY(QObject *log READ log CONSTANT)
    Q_PROPERTY(QObject *updater READ updater CONSTANT)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap directIp READ directIp NOTIFY directIpChanged)
    Q_PROPERTY(QVariantList knownHosts READ knownHosts NOTIFY knownHostsChanged)
    Q_PROPERTY(QString knownHostsPath READ knownHostsPath CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)
    Q_PROPERTY(QVariantList terminalThemes READ terminalThemes NOTIFY terminalThemesChanged)
    Q_PROPERTY(QStringList monospaceFonts READ monospaceFonts CONSTANT)
    Q_PROPERTY(QString defaultFont READ defaultFont CONSTANT)
    Q_PROPERTY(QString dataDir READ dataDir CONSTANT)
    Q_PROPERTY(QString vaultPath READ vaultPath CONSTANT)
    Q_PROPERTY(bool portable READ portable CONSTANT)
    Q_PROPERTY(bool serialAvailable READ serialAvailable CONSTANT)
    Q_PROPERTY(bool firewallSupported READ firewallSupported CONSTANT)
    Q_PROPERTY(bool firewallActive READ firewallActive NOTIFY firewallChanged)
    Q_PROPERTY(QString platform READ platform CONSTANT)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    QString appName() const;
    bool vaultExists() const { return m_vault.exists(); }
    bool vaultUnlocked() const { return m_vault.isUnlocked(); }
    bool busy() const { return m_busy; }
    QObject *hosts() { return &m_hosts; }
    QObject *keys() { return &m_keys; }
    QObject *proxies() { return &m_proxies; }
    QObject *snippets() { return &m_snippets; }
    QObject *sessions() { return &m_sessions; }
    QObject *log() { return &m_log; }
    QObject *updater() { return &m_updater; }
    QVariantMap settings() const;
    QVariantMap directIp() const { return m_directIp; }
    QVariantList knownHosts() const { return m_knownHosts; }
    QString knownHostsPath() const { return m_knownHostsPath; }
    QString version() const;
    QString language() const { return m_language; }
    QVariantList languages() const;
    QVariantList terminalThemes() const;
    QStringList monospaceFonts() const;
    QString defaultFont() const;
    QString dataDir() const;
    QString vaultPath() const;
    bool portable() const;
    bool serialAvailable() const;
    bool firewallSupported() const;
    bool firewallActive() const { return m_firewallActive; }
    QString platform() const;

    // Must be called once the QML engine exists, so it can be retranslated.
    void setEngine(QQmlEngine *engine) { m_engine = engine; }
    // "" = follow the system language.
    void applyLanguage(const QString &code);

    // Vault
    Q_INVOKABLE void createVault(const QString &password, const QString &confirm);
    Q_INVOKABLE void unlockVault(const QString &password);
    Q_INVOKABLE void lockVault();
    Q_INVOKABLE QString changeMasterPassword(const QString &oldPw, const QString &newPw, const QString &confirm);
    Q_INVOKABLE QString backupVault(const QUrl &target);
    Q_INVOKABLE QString restoreVault(const QUrl &source, const QString &password);
    // Forgotten master password: the old vault is moved (not deleted) to the
    // backups folder and the app goes back to "create a new vault".
    Q_INVOKABLE QString startOver();
    Q_INVOKABLE void openDataFolder();
    Q_INVOKABLE QVariantList backups() const;

    // Passwords
    Q_INVOKABLE QString generatePassword(int length = 24, bool symbols = true) const;
    Q_INVOKABLE QVariantMap evaluatePassword(const QString &password) const;

    Q_INVOKABLE QString answerChallenge(const QString &hostId, const QString &masterPassword);
    Q_INVOKABLE void cancelChallenge(const QString &hostId);

    // SSH agent of this computer: {available, name, keys (count, when withKeys), error}
    Q_INVOKABLE QVariantMap agentStatus(bool withKeys = false) const;

    // Hosts
    Q_INVOKABLE QVariantMap host(const QString &id) const;   // full record incl. saved password
    Q_INVOKABLE QString saveHost(const QVariantMap &host);    // returns error or ""
    Q_INVOKABLE void deleteHost(const QString &id);
    Q_INVOKABLE void duplicateHost(const QString &id);
    Q_INVOKABLE QStringList serialPorts() const;
    // Import from ~/.ssh/config ("ssh") or PuTTY saved sessions ("putty").
    Q_INVOKABLE QVariantList importCandidates(const QString &source) const;
    Q_INVOKABLE QString importHosts(const QVariantList &items);   // returns a summary
    Q_INVOKABLE QString sshConfigPath() const;

    // Keys
    Q_INVOKABLE QString generateKey(const QString &name, int type, const QString &comment,
                                    const QString &passphrase, const QString &confirm, bool savePassphrase);
    Q_INVOKABLE QVariantMap importKey(const QString &name, const QString &text,
                                      const QString &passphrase, bool savePassphrase);
    Q_INVOKABLE QString readTextFile(const QUrl &url) const;
    Q_INVOKABLE QString exportPublicKey(const QString &id, const QUrl &url);        // OpenSSH one-line
    Q_INVOKABLE QString exportPublicKeyRfc4716(const QString &id, const QUrl &url); // SSH2 format
    Q_INVOKABLE QString exportPrivateKey(const QString &id, const QUrl &url);
    Q_INVOKABLE QString setKeyPassphrase(const QString &id, const QString &passphrase, bool save);
    Q_INVOKABLE void forgetKeyPassphrase(const QString &id);
    Q_INVOKABLE void renameKey(const QString &id, const QString &name);
    Q_INVOKABLE void deleteKey(const QString &id);
    Q_INVOKABLE QString publicKeyLine(const QString &id) const;
    Q_INVOKABLE QUrl suggestedFile(const QString &fileName) const; // ~/.ssh/<name> when it exists
    Q_INVOKABLE QUrl homeFolder() const;

    // Proxies
    Q_INVOKABLE QVariantMap proxy(const QString &id) const;
    Q_INVOKABLE QString saveProxy(const QVariantMap &proxy);
    Q_INVOKABLE void deleteProxy(const QString &id);
    Q_INVOKABLE void checkProxy(const QString &id);
    Q_INVOKABLE void checkProxyDraft(const QVariantMap &draft); // from the editor, before saving
    Q_INVOKABLE void checkDirectIp();

    // Firewall lock (Windows)
    Q_INVOKABLE QString applyFirewall();
    Q_INVOKABLE QString removeFirewall();

    // Snippets
    Q_INVOKABLE QVariantMap snippet(const QString &id) const;
    Q_INVOKABLE QString saveSnippet(const QVariantMap &snippet);
    Q_INVOKABLE void deleteSnippet(const QString &id);
    Q_INVOKABLE void runSnippet(const QString &id, int sessionIndex, bool allSessions);

    // Sessions
    Q_INVOKABLE void openHost(const QString &id);
    // right-click a host: connect once through another route ("direct" or a proxy id) …
    Q_INVOKABLE void openHostVia(const QString &id, const QString &route);
    // … or keep it as the host's route. hostRoute: "direct" or the proxy id
    Q_INVOKABLE QString hostRoute(const QString &id) const;
    Q_INVOKABLE QString setHostRoute(const QString &id, const QString &route);
    Q_INVOKABLE void checkTor(const QString &mode);   // answer comes as torStatusReady (in the background)
    Q_INVOKABLE void torNewIdentity(const QString &proxyId);
    // Run one command on several hosts (no tabs); progress in App.batch
    Q_INVOKABLE QString runOnHosts(const QVariantList &hostIds, const QString &command, const QString &masterPassword = QString());
    Q_INVOKABLE bool hostIsProtected(const QString &hostId) const;
    // batch runs: keys whose passphrase is not saved ([{keyId, name}]), and the ones typed for this run
    Q_INVOKABLE QVariantList batchKeyNeeds(const QVariantList &hostIds) const;
    Q_INVOKABLE QString setBatchPassphrases(const QVariantMap &map);   // "" or an error
    Q_INVOKABLE QString hostNotes(const QString &hostId) const;
    Q_INVOKABLE QString setHostNotes(const QString &hostId, const QString &notes);
    // [{hostId, keyId}]: append each public key to the host's authorized_keys (results in App.batch)
    Q_INVOKABLE QString installKeys(const QVariantList &pairs, const QString &masterPassword, bool useForHost);
    Q_PROPERTY(QObject *batch READ batchObject CONSTANT)
    QObject *batchObject() { return &m_batch; }
    // Compare a config file between two hosts; both files arrive in App.diffRunner.results
    Q_INVOKABLE QVariantList localShells() const;            // [{id, name, program, args}]
    Q_INVOKABLE void openLocalTerminal(const QString &shellId);   // "" = the one chosen in Settings
    Q_INVOKABLE QString compareFiles(const QString &hostA, const QString &pathA, const QString &hostB, const QString &pathB);
    Q_PROPERTY(QObject *diffRunner READ diffObject CONSTANT)
    QObject *diffObject() { return &m_diff; }
    Q_PROPERTY(QObject *tor READ torObject CONSTANT)
    QObject *torObject() { return &m_tor; }
    Q_INVOKABLE QString countryName(const QString &code) const;   // "UA" → "Україна"
    Q_INVOKABLE int countryFlag(const QString &code) const;       // index in :/assets/flags.png, or -1
    Q_INVOKABLE QString quickConnect(const QString &spec);   // user@host[:port]
    Q_INVOKABLE QString openSerial(const QString &port, int baud);
    Q_INVOKABLE void closeSession(int index);
    Q_INVOKABLE void moveSession(int from, int to);
    Q_INVOKABLE void duplicateSession(int index);

    // Known hosts
    Q_INVOKABLE void refreshKnownHosts();
    Q_INVOKABLE void removeKnownHost(int line);

    // Settings & misc
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE void copyText(const QString &text, bool sensitive = false);
    Q_INVOKABLE void setLanguage(const QString &code);
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void skipUpdate(const QString &version);   // no pop-up for this version
    Q_INVOKABLE void showUpdateDetails();                   // the update window for the last check
    QString updateRepo() const;
    Q_INVOKABLE QString formatBytes(qint64 bytes) const;
    Q_INVOKABLE void beep() const;
    // Own terminal themes: colours, background picture, input colour.
    Q_INVOKABLE QString saveCustomTheme(const QVariantMap &theme);
    // your own picture behind a built-in picture theme; "" on success or an error
    Q_INVOKABLE void deleteCustomTheme(const QString &id);
    Q_INVOKABLE QString inputColorCommand(const QString &color) const;   // bash line that colours what you type   // terminal bell "sound" (Windows system sound)

signals:
    // "available": a new version was found at start; "updated": first start after an update
    void updateDialogRequested(const QString &mode);
    void vaultStateChanged();
    void vaultError(const QString &message);
    void busyChanged();
    void settingsChanged();
    void directIpChanged();
    void knownHostsChanged();
    void proxyDraftChecked(const QVariantMap &result);
    void notify(const QString &message, bool isError);
    void sessionOpened(int index);
    void languageChanged();
    void firewallChanged();
    void terminalThemesChanged();
    void torStatusReady(const QVariantMap &info);
    // Before connecting to a host with "ask the master password": QML shows the dialog.
    void connectChallenge(const QString &hostId, const QString &label);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void dropRemovedThemes();
    QJsonArray section(const QString &name) const;
    void setSection(const QString &name, const QJsonArray &items);
    QJsonObject findIn(const QString &section, const QString &id) const;
    bool upsert(const QString &section, QJsonObject &obj);   // returns persisted
    bool persist();
    void reloadModels();
    void setBusy(bool b);
    void dailyBackup();
    TerminalSession *createSession(const QString &hostId, const QString &label, const QString &endpoint,
                                   const QString &kind);
    void startSession(TerminalSession *s, const QJsonObject &host);
    void migrateRemovedFeatures();
    QHash<QString, QString> m_nextRoute;   // hostId -> route for the tab about to open
    ProxyConfig proxyConfig(const QJsonObject &p, const QString &firstHop = QString()) const;
    QVariantMap cloudflaredCheck(const QString &customPath) const;
    ConnectParams paramsFor(const QJsonObject &host, const QJsonObject &jumpHost) const;
    BatchRunner::Job batchJob(const QString &hostId, bool protectedOk = false) const;
    void onBatchFinished();
    QHash<QString, QString> m_keyAssign;
    QHash<QString, QString> m_batchPass;     // keyId -> passphrase for the next batch run only   // hostId -> keyId to set after a key install
    QString effectiveProxyId(const QJsonObject &host) const;
    void runProbe();          // every 3 s: is each used proxy alive?
    void runExitIpCheck();    // every 60 s: does it still hide the real IP?
    void killSessionsVia(const QString &proxyId, const QString &reason);
    void storeCheck(const QString &proxyId, const QVariantMap &r);
    void onSecretVerified(const QString &hostId, int kind, const QString &secret);
    void restartAutoLock();
    QVariantList customThemes() const;
    void applyTunnelsLive(const QString &hostId, const QJsonArray &before, const QJsonArray &after);
    void storeCustomThemes(const QVariantList &list);
    bool needsChallenge(const QJsonObject &host) const;
    void openHostNow(const QString &id);
    void logEvent(int level, const QString &cat, const QString &msg, const QString &detail = {},
                  const QString &host = {});

    QSettings m_settings;
    Vault m_vault;
    QString m_knownHostsPath;
    bool m_busy = false;

    VaultListModel m_hosts;
    VaultListModel m_keys;
    VaultListModel m_proxies;
    VaultListModel m_snippets;
    SessionListModel m_sessions;
    ProxyChecker m_checker;
    ActivityLog m_log;
    UpdateChecker m_updater;
    QString m_updaterState;
    bool m_justUpdated = false;   // first start of this version after an update

    QVariantMap m_directIp;
    QVariantList m_knownHosts;
    QTimer m_probeTimer;
    SystemLockWatcher m_systemLock;
    mutable TorManager m_tor;
    CloudflaredManager m_cf;
    BatchRunner m_batch;
    BatchRunner m_diff;
    QTimer m_exitIpTimer;
    QSet<QString> m_probing;
    QTimer m_autoLock;
    QTimer m_clipboardClear;
    QString m_sensitiveClipboard;
    bool m_firewallActive = false;
    QHash<QString, QPointer<TerminalSession>> m_pendingChallenge;   // hostId → session to reconnect (null = new tab)
    QHash<QString, int> m_challengeFails;
    QHash<QString, qint64> m_challengeLockedUntil;

    QQmlEngine *m_engine = nullptr;
    QString m_language;          // effective code: en, uk, ru, fr, es
    QTranslator m_appTranslator;
    QTranslator m_qtTranslator;  // Qt's own strings (file dialogs, …)
};
