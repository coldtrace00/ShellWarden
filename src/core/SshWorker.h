#pragma once

#include "ProxyTunnel.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

#include <libssh/libssh.h>
#include <libssh/sftp.h>
#include <libssh/callbacks.h>

#include <memory>

class QFile;
class QSocketNotifier;
class QTcpServer;
class QTcpSocket;
class QTimer;
class JumpBridge;

struct ForwardSpec
{
    enum Type { Local = 0, Remote = 1, Dynamic = 2 };
    QString id;
    int type = Local;
    QString bindHost = QStringLiteral("127.0.0.1");
    int bindPort = 0;
    QString destHost;      // Local/Remote only
    int destPort = 0;
};

// An intermediate SSH server (ProxyJump). Credentials come from the vault;
// the jump host's key must already be trusted (connect to it once directly).
struct JumpParams
{
    bool enabled = false;
    QString label;
    QString host;
    int port = 22;
    QString username;
    QString password;
    QByteArray privateKey;
    QString keyPassphrase;
};

struct ConnectParams
{
    QString host;
    int port = 22;
    QString username;
    QString password;          // may be empty → asked interactively
    QByteArray privateKey;     // OpenSSH/PEM text, may be empty
    QString keyPassphrase;     // may be empty → asked if the key is encrypted
    QString knownHostsPath;
    ProxyConfig proxy;         // first hop (target, or jump host when set)
    JumpParams jump;
    QVector<ForwardSpec> forwards;
    bool killSwitch = false;   // never connect without the proxy
    int cols = 120;
    int rows = 32;
    int keepaliveSeconds = 30;
    int connectTimeoutSeconds = 15;
    QString term = QStringLiteral("xterm-256color");
    // Batch mode: run this command instead of opening a shell, report execDone(0, …), disconnect.
    QString execCommand;
    int execTimeoutMs = 10 * 60 * 1000;
    // SSH agent of this computer (Pageant / OpenSSH agent / ssh-agent)
    bool useAgent = false;      // try the agent's keys before the vault key/password
    bool agentForward = false;  // let the server use the agent (ssh -A)
};

// Owns one libssh session: interactive shell, SFTP and port forwards. Lives
// on its own QThread; every public slot must be invoked through a queued
// connection. libssh sessions are not thread-safe, so everything that touches
// the session happens here.
class SshWorker : public QObject
{
    Q_OBJECT
public:
    enum HostKeyStatus { HostUnknown = 0, HostChanged = 1, HostOtherType = 2 };
    Q_ENUM(HostKeyStatus)
    enum SecretKind { SecretPassword = 0, SecretKeyPassphrase = 1, SecretKeyboardInteractive = 2 };
    Q_ENUM(SecretKind)
    enum TransferState { TransferRunning = 0, TransferDone = 1, TransferFailed = 2, TransferCancelled = 3 };
    Q_ENUM(TransferState)

    explicit SshWorker(QObject *parent = nullptr);
    ~SshWorker() override;

public slots:
    void start(const ConnectParams &params);
    void answerHostKey(bool accept);
    void answerSecret(const QString &secret, bool cancelled);
    void write(const QByteArray &data);
    void resize(int cols, int rows);
    void stop();

    // SFTP (reqId/transferId are chosen by the caller)
    void sftpList(int reqId, const QString &path);
    void sftpMkdir(int reqId, const QString &path);
    void sftpRemove(int reqId, const QString &path, bool isDir);
    void sftpRename(int reqId, const QString &from, const QString &to);
    void sftpDownload(int transferId, const QString &remote, const QString &local);
    void sftpUpload(int transferId, const QString &local, const QString &remote);
    void sftpCancel(int transferId);
    // small text files for the built-in editor (whole file in memory, max 5 MB)
    void sftpReadFile(int reqId, const QString &path);
    void sftpWriteFile(int reqId, const QString &path, const QByteArray &data);

    // Run a short command on its own channel (the shell is not touched).
    // Blocks this worker for at most timeoutMs, so keep it quick.
    void exec(int reqId, const QString &command, int timeoutMs);
    // server monitor: one channel that prints `command` every intervalSec seconds;
    // each sample arrives as execDone(1, …). Never blocks the terminal after the start.
    void startMonitor(const QString &command, int intervalSec);
    void stopMonitor();

    // Port forwarding at runtime
    void addForward(const ForwardSpec &spec);
    void removeForward(const QString &id);

signals:
    void status(const QString &text);
    void hostKeyCheck(int status, const QString &keyType, const QString &fingerprint);
    void secretRequired(int kind, const QString &prompt, bool echo, bool retry);
    // A secret that worked; the UI may offer to store it in the vault.
    void secretAccepted(int kind, const QString &secret);
    void connected();
    // What the server sees as our address (from $SSH_CONNECTION on the server).
    void clientAddressSeen(const QString &ip, const QString &port, const QString &serverIp);
    void dataReceived(const QByteArray &data);
    void finished(const QString &reason, bool isError);

    void sftpListed(int reqId, const QString &path, const QVariantList &entries, const QString &error);
    void sftpDone(int reqId, const QString &error);
    void sftpFileRead(int reqId, const QString &path, const QByteArray &data, const QString &error);
    void sftpProgress(int transferId, qint64 done, qint64 total, int state, const QString &error);

    // [{id,type,bind,dest,state,error,active,bytesIn,bytesOut,idleMs}]
    void forwardsUpdated(const QVariantList &forwards);
    void forwardEvent(const QString &id, bool ok, const QString &message);
    void execDone(int reqId, int exitCode, const QByteArray &out, const QString &error);

private:
    // connection & auth
    void fail(const QString &reason);
    void verifyHostKey();
    void tryAuth();
    bool tryPublicKey();
    bool tryAgent();
    static ssh_channel onAgentChannelRequest(ssh_session session, void *userdata);
    void pumpAgentChannels();
    void kbdStep(int rc);
    void handleAuthResult(int rc, int method);
    void onAuthenticated();
    void readClientAddress(ssh_channel existing = nullptr);
    bool openPipelined();
    void pumpAddress();
    void finishShell();
    int execOn(const QString &command, int timeoutMs, QByteArray *out, QString *error);
    void openShell();
    void pump();
    void cleanup();
    void requestSecret(SecretKind kind, const QString &prompt, bool echo = false, bool retry = false);

    // SFTP
    bool ensureSftp(QString *error);
    void pumpTransfers();
    QString sftpErrorText() const;

    // forwarding
    struct FwdConn;
    struct FwdState;
    void startForward(FwdState &f);
    void onLocalConnection(int fwdIndex);
    void handleSocksData(FwdConn *c);
    bool openChannelFor(FwdConn *c, const QString &host, int port);
    void pumpForwards();
    void closeConn(FwdConn *c);
    void publishForwards(bool force = false);
    int forwardIndex(const QString &id) const;

    ConnectParams m_p;
    ssh_session m_session = nullptr;
    ssh_channel m_channel = nullptr;
    QSocketNotifier *m_notifier = nullptr;
    QTimer *m_pollTimer = nullptr;
    QTimer *m_keepalive = nullptr;
    std::unique_ptr<JumpBridge> m_jump;

    int m_methods = 0;
    bool m_keyTried = false;
    bool m_kbdActive = false;
    int m_kbdIndex = 0;
    bool m_kbdUsedPassword = false;
    int m_passwordAttempts = 0;
    int m_pendingSecret = -1;
    int m_lastMethod = 0;
    bool m_done = false;
    bool m_passwordFromUser = false;
    bool m_passphraseFromUser = false;
    bool m_shellOpen = false;
    bool m_agentTried = false;
    ssh_channel m_monCh = nullptr;
    ssh_channel m_addrCh = nullptr;   // $SSH_CONNECTION, read while the shell starts
    QByteArray m_addrBuf;
    QElapsedTimer m_addrTimer;
    bool m_addrSequential = false;    // the address was read on its own channel before the shell
    QByteArray m_monBuf;
    void pumpMonitor();

    // agent forwarding: channels the server opened to reach our agent
    struct AgentChan { ssh_channel ch = nullptr; QByteArray in; };
    QVector<AgentChan> m_agentChans;
    ssh_callbacks_struct m_callbacks {};

    // SFTP state
    sftp_session m_sftp = nullptr;
    struct Transfer
    {
        int id = 0;
        bool upload = false;
        QString remote, local;
        sftp_file remoteFile = nullptr;
        QFile *localFile = nullptr;
        qint64 done = 0, total = 0;
        qint64 lastReportMs = 0;
        bool started = false;
    };
    QVector<Transfer> m_transfers;
    QElapsedTimer m_clock;

    // forwarding state
    struct FwdConn
    {
        int fwd = -1;
        QTcpSocket *sock = nullptr;
        ssh_channel ch = nullptr;
        bool socksPending = false;
        QByteArray pending;      // data waiting for the other side
        bool dead = false;
    };
    struct FwdState
    {
        ForwardSpec spec;
        QTcpServer *server = nullptr;
        int boundPort = 0;
        QString state;           // "starting" | "active" | "error"
        QString error;
        qint64 bytesIn = 0, bytesOut = 0;
        qint64 lastActivityMs = -1;
        bool removed = false;
    };
    QVector<FwdState> m_forwards;
    QVector<FwdConn *> m_conns;
    QTimer *m_fwdTimer = nullptr;
    QVariantList m_lastPublished;
};

Q_DECLARE_METATYPE(ConnectParams)
Q_DECLARE_METATYPE(ForwardSpec)
