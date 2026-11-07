#pragma once

#include "core/SshWorker.h"
#include "terminal/TerminalEmulator.h"

#include <QElapsedTimer>
#include <QObject>
#include <QUrl>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QThread;
class QTimer;
class QIODevice;

// One terminal tab. Lives on the GUI thread. It owns the terminal state
// (TerminalEmulator) and drives either an SshWorker on its own thread or a
// local serial port. Everything QML shows about the connection — state,
// prompts, route, the IP the server sees, tunnels, SFTP — is here.
class TerminalSession : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by App.openHost()")

    Q_PROPERTY(QString hostId READ hostId CONSTANT)
    Q_PROPERTY(QString label READ label CONSTANT)
    Q_PROPERTY(QString endpoint READ endpoint CONSTANT)
    Q_PROPERTY(QString kind READ kind CONSTANT)                // "ssh" | "serial" | "telnet"
    Q_PROPERTY(QString color READ color WRITE setColor NOTIFY changed)
    Q_PROPERTY(QString themeId READ themeId WRITE setThemeId NOTIFY changed)
    Q_PROPERTY(bool detached READ detached WRITE setDetached NOTIFY detachedChanged)
    Q_PROPERTY(int state READ state NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(bool closedWithError READ closedWithError NOTIFY changed)
    Q_PROPERTY(bool activity READ activity NOTIFY activityChanged)

    Q_PROPERTY(int hostKeyStatus READ hostKeyStatus NOTIFY changed)
    Q_PROPERTY(QString hostKeyType READ hostKeyType NOTIFY changed)
    Q_PROPERTY(QString hostKeyFingerprint READ hostKeyFingerprint NOTIFY changed)
    // SSHFP (host key fingerprints published in DNS): "" | checking | match | mismatch | none | error
    Q_PROPERTY(QString sshfpState READ sshfpState NOTIFY changed)
    Q_PROPERTY(bool sshfpSecure READ sshfpSecure NOTIFY changed)   // the DNS answer was DNSSEC-validated

    Q_PROPERTY(int secretKind READ secretKind NOTIFY changed)
    Q_PROPERTY(QString secretPrompt READ secretPrompt NOTIFY changed)
    Q_PROPERTY(bool secretEcho READ secretEcho NOTIFY changed)
    Q_PROPERTY(bool secretRetry READ secretRetry NOTIFY changed)
    Q_PROPERTY(bool canSaveSecret READ canSaveSecret NOTIFY changed)

    Q_PROPERTY(QString routeText READ routeText NOTIFY changed)
    Q_PROPERTY(bool viaProxy READ viaProxy NOTIFY changed)
    Q_PROPERTY(bool viaJump READ viaJump NOTIFY changed)
    Q_PROPERTY(QString jumpLabel READ jumpLabel NOTIFY changed)
    Q_PROPERTY(bool killSwitch READ killSwitch NOTIFY changed)
    Q_PROPERTY(QString expectedIp READ expectedIp NOTIFY changed)
    Q_PROPERTY(QString expectedLocation READ expectedLocation NOTIFY changed)
    Q_PROPERTY(QString expectedCountry READ expectedCountry NOTIFY changed)   // ISO code, e.g. "UA"
    Q_PROPERTY(QString seenIp READ seenIp NOTIFY changed)
    Q_PROPERTY(QString seenPort READ seenPort NOTIFY changed)
    Q_PROPERTY(QString serverIp READ serverIp NOTIFY changed)

    Q_PROPERTY(QVariantList forwards READ forwards NOTIFY forwardsChanged)
    // Server mini-monitor: {ok, error, cpu, load1, cores, memUsed, memTotal, swapUsed, swapTotal,
    //                       diskUsed, diskTotal, uptime, rxRate, txRate} (bytes, bytes/s, seconds)
    Q_PROPERTY(QVariantMap monitor READ monitor NOTIFY monitorChanged)
    Q_PROPERTY(bool monitorOn READ monitorOn NOTIFY monitorChanged)
    Q_PROPERTY(QVariantList transfers READ transfers NOTIFY transfersChanged)
    // Session log: everything the server prints, as plain text (colours stripped)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    // auto-reconnect after a lost connection: seconds left (-1 = not waiting)
    Q_PROPERTY(int reconnectIn READ reconnectIn NOTIFY reconnectChanged)
    Q_PROPERTY(int reconnectAttempt READ reconnectAttempt NOTIFY reconnectChanged)
    // broadcast: what is typed in one broadcasting tab goes to all of them
    Q_PROPERTY(bool broadcast READ broadcast WRITE setBroadcast NOTIFY broadcastChanged)
    Q_PROPERTY(int broadcastCount READ broadcastCount NOTIFY broadcastChanged)
    Q_PROPERTY(QString recordingPath READ recordingPath NOTIFY recordingChanged)

public:
    enum State { Preparing = 0, Connecting, HostKeyPrompt, SecretPrompt, Connected, Closed };
    Q_ENUM(State)

    TerminalSession(QString hostId, QString label, QString endpoint, QString kind, QObject *parent = nullptr);
    ~TerminalSession() override;

    TerminalEmulator *emulator() { return &m_emu; }
    static QString commandFromLine(const QString &line);   // "user@h:~$ ls" -> "ls"

    QString hostId() const { return m_hostId; }
    // route chosen for this tab only (right-click "Connect via"): "direct", a proxy id, or "" = the host's own
    QString routeOverride() const { return m_routeOverride; }
    void setRouteOverride(const QString &r) { m_routeOverride = r; }
    QString label() const { return m_label; }
    QString endpoint() const { return m_endpoint; }
    QString kind() const { return m_kind; }
    QString color() const { return m_color; }
    void setColor(const QString &c);
    QString themeId() const { return m_emu.themeId(); }
    void setThemeId(const QString &id);
    bool detached() const { return m_detached; }
    void setDetached(bool d);
    int state() const { return m_state; }
    QString statusText() const { return m_status; }
    bool closedWithError() const { return m_closedWithError; }
    bool activity() const { return m_activity; }
    int hostKeyStatus() const { return m_hkStatus; }
    QString hostKeyType() const { return m_hkType; }
    QString hostKeyFingerprint() const { return m_hkFingerprint; }
    int secretKind() const { return m_secretKind; }
    QString secretPrompt() const { return m_secretPrompt; }
    bool secretEcho() const { return m_secretEcho; }
    bool secretRetry() const { return m_secretRetry; }
    bool canSaveSecret() const { return m_canSaveSecret; }
    QString routeText() const { return m_route; }
    bool viaProxy() const { return m_viaProxy; }
    bool viaJump() const { return !m_jumpLabel.isEmpty(); }
    QString jumpLabel() const { return m_jumpLabel; }
    bool killSwitch() const { return m_killSwitch; }
    QString expectedIp() const { return m_expectedIp; }
    QString expectedLocation() const { return m_expectedLocation; }
    QString expectedCountry() const { return m_expectedCountry; }
    QString seenIp() const { return m_seenIp; }
    QString seenPort() const { return m_seenPort; }
    QString serverIp() const { return m_serverIp; }
    QString proxyId() const { return m_proxyId; }
    QVariantList forwards() const { return m_forwards; }
    QVariantMap monitor() const { return m_monitor; }
    QString sshfpState() const { return m_sshfpState; }
    bool sshfpSecure() const { return m_sshfpSecure; }
    void setSshfpCheck(bool on) { m_sshfpOn = on; }
    bool monitorOn() const { return m_monOn; }
    void setMonitorConfig(bool on, int intervalSec);
    QVariantList transfers() const;
    bool isActive() const { return m_state != Closed; }

    // Called on application exit: lets every SSH thread finish cleanly.
    static void waitForAllThreads(int msTotal);

    // Set up by AppController before begin().
    void setRoute(const QString &text, bool viaProxy, const QString &proxyId, bool killSwitch,
                  const QString &jumpLabel = {});
    void setPreparing(const QString &text);
    void setExpected(const QString &ip, const QString &location, const QString &country = QString());
    void setCanSaveSecrets(bool can) { m_canSaveSecret = can; }
    void begin(const ConnectParams &params);
    // A shell of this computer (PowerShell, cmd, WSL, bash…)
    void beginLocal(const QString &program, const QStringList &args);
    // Telnet (plain text!), optionally through a SOCKS5 / HTTP proxy
    void beginTelnet(const QString &host, int port, const ProxyConfig &proxy);
    void beginSerial(const QString &port, int baud, int dataBits, const QString &parity,
                     int stopBits, const QString &flow);
    void abort(const QString &reason);      // fail before/while connecting
    void kill(const QString &reason);       // kill switch: drop immediately

    // From the terminal view.
    void write(const QByteArray &data);
    void resizeTerminal(int cols, int rows);

    Q_INVOKABLE void acceptHostKey(bool accept);
    Q_INVOKABLE void submitSecret(const QString &secret, bool save);
    Q_INVOKABLE void cancelSecret();
    Q_INVOKABLE void disconnectSession();
    Q_INVOKABLE void reconnect();
    Q_INVOKABLE void sendText(const QString &text);

    // SFTP — results arrive via sftpListed / sftpDone / transfersChanged
    Q_INVOKABLE int sftpList(const QString &path);
    Q_INVOKABLE int sftpMkdir(const QString &path);
    Q_INVOKABLE int sftpRemove(const QString &path, bool isDir);
    Q_INVOKABLE int sftpRename(const QString &from, const QString &to);
    Q_INVOKABLE int sftpReadFile(const QString &path);                       // -> sftpFileRead
    Q_INVOKABLE int sftpWriteFile(const QString &path, const QString &text); // -> sftpDone
    Q_INVOKABLE int download(const QString &remote, const QUrl &localFolder);
    Q_INVOKABLE int upload(const QUrl &localFile, const QString &remoteFolder);
    Q_INVOKABLE void cancelTransfer(int id);
    Q_INVOKABLE void clearFinishedTransfers();

    // Tunnels at runtime: type "L" | "R" | "D"
    Q_INVOKABLE QString addForward(const QString &type, const QString &bindHost, int bindPort,
                                   const QString &destHost, int destPort);
    Q_INVOKABLE void removeForward(const QString &id);

    Q_INVOKABLE void cancelReconnect();
    void setAutoReconnect(bool on) { m_autoReconnect = on; }
    int reconnectIn() const { return m_reconnectIn; }
    int reconnectAttempt() const { return m_retry; }
    static constexpr int kMaxRetries = 5;

    bool broadcast() const { return m_broadcast; }
    void setBroadcast(bool on);
    int broadcastCount() const;
    // keyboard / paste from the terminal view (goes to the broadcast group too)
    void userInput(const QByteArray &data);
    // Key presses go through the emulator (which turns them into bytes);
    // the view brackets them so those bytes count as typed input.
    void beginUserInput() { ++m_inputDepth; }
    void endUserInput() { --m_inputDepth; }

    Q_INVOKABLE QString startRecording(const QUrl &file);   // returns error or ""
    Q_INVOKABLE void stopRecording();
    bool recording() const { return m_record != nullptr; }
    QString recordingPath() const { return m_recordPath; }

signals:
    void commandEntered(const QString &command);   // a shell line the user ran (see write())
    void recordingChanged();
    void reconnectChanged();
    void broadcastChanged();
    void changed();
    void detachedChanged();
    void activityChanged();
    void sessionRestarted();
    void reconnectRequested();
    void saveSecretRequested(const QString &hostId, int kind, const QString &secret);
    void forwardsChanged();
    void monitorChanged();
    void transfersChanged();
    void sftpListed(int reqId, const QString &path, const QVariantList &entries, const QString &error);
    void sftpDone(int reqId, const QString &error);
    void sftpFileRead(int reqId, const QString &path, const QString &text, bool binary, const QString &error);
    // For the activity log: level (ActivityLog::Level), category, message, detail
    void logEvent(int level, const QString &category, const QString &message, const QString &detail);

private:
    void lookupSshfp(const QString &fingerprint);
    bool m_sshfpOn = true;
    QString m_sshfpState;
    bool m_sshfpSecure = false;
    QString m_dnsHost;
    ProxyConfig m_dnsProxy;
    void parseMonitor(const QByteArray &out, const QString &error);
    QVariantMap m_monitor;
    bool m_monOn = false;
    int m_monInterval = 5;
    int m_monRunning = 0;
    QString m_routeOverride;   // interval of the running monitor channel, 0 = not running
    quint64 m_cpuTotal = 0, m_cpuIdle = 0, m_rx = 0, m_tx = 0;
    qint64 m_netAt = 0;

    void setState(State s, const QString &status = {});
    void stopWorker();
    void stopSerial();
    void telnetIncoming(const QByteArray &raw);
    void telnetSendSize();
    QByteArray m_tnBuf;           // incomplete IAC sequence between reads
    bool m_tnNaws = false;        // server agreed to window-size updates
    void markActivity();
    void incoming(const QByteArray &data);   // terminal + session log
    template<typename F> void onWorker(F &&f);
    static QList<QPointer<QThread>> &liveThreads();

    QString m_hostId, m_label, m_endpoint, m_kind, m_color;
    TerminalEmulator m_emu;
    class QFile *m_record = nullptr;
    bool m_autoReconnect = false;
    bool m_wasConnected = false;
    int m_retry = 0;
    int m_reconnectIn = -1;
    QTimer *m_retryTimer = nullptr;
    bool m_broadcast = false;
    int m_inputDepth = 0;
    static QList<QPointer<TerminalSession>> &broadcastGroup();
    QString m_recordPath;
    QByteArray m_recordTail;    // incomplete escape sequence between chunks
    bool m_detached = false;
    int m_state = Preparing;
    QString m_status;
    bool m_closedWithError = false;
    bool m_activity = false;
    qint64 m_lastActivity = 0;

    int m_hkStatus = 0;
    QString m_hkType, m_hkFingerprint;

    int m_secretKind = -1;
    QString m_secretPrompt;
    bool m_secretEcho = false;
    bool m_secretRetry = false;
    bool m_canSaveSecret = true;
    bool m_saveRequested[3] = {false, false, false};

    QString m_route;
    bool m_viaProxy = false;
    bool m_killSwitch = false;
    QString m_proxyId;
    QString m_jumpLabel;
    QString m_expectedIp, m_expectedLocation, m_expectedCountry;
    QString m_seenIp, m_seenPort, m_serverIp;

    QVariantList m_forwards;
    struct TransferInfo
    {
        int id;
        bool upload;
        QString name, remote, local;
        qint64 done = 0, total = 0;
        int state = 0;
        QString error;
        qint64 startedMs = 0;
        double speed = 0;    // bytes/s
        qint64 base = -1;    // bytes already there when it started (a resumed transfer)
    };
    void noteCommand(const QString &line);
    int m_cmdRow = -1;           // screen row of a command whose Enter is not echoed yet
    quint64 m_cmdPushed = 0;     // TerminalEmulator::linesPushed() at that moment
    QString m_cmdTyped;
    QElapsedTimer m_cmdSince;
    QVector<TransferInfo> m_transfers;
    int m_nextReq = 1;

    int m_cols = 120, m_rows = 32;
    QTimer *m_resizeTimer = nullptr;
    QThread *m_thread = nullptr;
    QPointer<SshWorker> m_worker;
    QIODevice *m_serial = nullptr;
    class LocalPty *m_pty = nullptr;
    QString m_localProgram;
    QStringList m_localArgs;
    bool m_started = false;
};
