#pragma once

#include <QObject>
#include <QProcess>
#include <QHash>

// Tor support for the "Tor" proxy type.
//  - "auto":    use a Tor that is already running: Tor Browser (9150) or the Tor service (9050)
//  - "browser": Tor Browser only (127.0.0.1:9150)
//  - "service": the system Tor service only (127.0.0.1:9050)
//  - "exe":     ShellWarden starts tor.exe itself (SOCKS 127.0.0.1:9250) and stops it on exit
// "New identity" uses Tor's stream isolation: every new SOCKS user name gets
// fresh circuits, so it works with any Tor, no control port needed.
class TorManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)          // off | starting | ready | error
    Q_PROPERTY(int progress READ progress NOTIFY changed)        // bootstrap % of our own tor
    Q_PROPERTY(QString message READ message NOTIFY changed)
public:
    static constexpr int kBrowserPort = 9150;
    static constexpr int kServicePort = 9050;
    static constexpr int kOwnPort = 9250;

    explicit TorManager(QObject *parent = nullptr);
    ~TorManager() override;

    QString state() const { return m_state; }
    int progress() const { return m_progress; }
    QString message() const { return m_message; }

    // SOCKS port for a mode; 0 when that Tor is not reachable (own tor: not ready yet).
    int portFor(const QString &mode, QString *which = nullptr) const;
    bool needsStart(const QString &mode) const { return mode == QLatin1String("exe") && m_state != QLatin1String("ready"); }
    void start(const QString &exePath);
    void stop();

    // SOCKS credentials that select the circuit; newIdentity() switches to new ones.
    QString isolationUser(const QString &proxyId) const;
    void newIdentity(const QString &proxyId);

    static bool portOpen(int port, int timeoutMs = 300);   // cached ~4 s
    static bool probe(int port, int timeoutMs);
    static QString findTorExe();   // tor.exe next to the app or in common folders, else ""

signals:
    void changed();
    void ready();
    void failed(const QString &error);

private:
    void setState(const QString &s, const QString &msg = QString());
    QProcess *m_proc = nullptr;
    QString m_state = QStringLiteral("off");
    QString m_message;
    int m_progress = 0;
    QHash<QString, int> m_identity;   // proxyId -> counter
    QString m_session;                // random per app start
};
