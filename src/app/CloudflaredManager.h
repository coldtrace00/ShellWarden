#pragma once

#include <QHash>
#include <QObject>
#include <QProcess>

// Route "Cloudflare Tunnel": the server runs `cloudflared tunnel` and is
// published as ssh://… on a hostname in the user's Cloudflare zone. Here we run
//     cloudflared access tcp --hostname <host> --url 127.0.0.1:<free port>
// and SSH goes through that local port, so the server needs no open port and
// its IP stays hidden. Zero Trust login (browser) or a service token work.
// One cloudflared per hostname, kept while the app runs, stopped on exit.
class CloudflaredManager : public QObject
{
    Q_OBJECT
public:
    explicit CloudflaredManager(QObject *parent = nullptr);
    ~CloudflaredManager() override;

    static QString findExe(const QString &custom = QString());   // "" when not found
    static QString version(const QString &exe);                  // "" when it does not run

    // starts (or reuses) the tunnel to `hostname`; ready()/failed() follow.
    // Returns false right away when cloudflared cannot be started.
    bool start(const QString &hostname, const QString &exe, const QString &tokenId,
               const QString &tokenSecret, QString *error);
    bool isReady(const QString &hostname) const;
    int portFor(const QString &hostname) const;   // 0 when not running
    void stopAll();

signals:
    void ready(const QString &hostname, int port);
    void failed(const QString &hostname, const QString &error);

private:
    struct Tunnel {
        QProcess *proc = nullptr;
        int port = 0;
        bool ready = false;
        QString log;
    };
    void poll(const QString &hostname, int attempt);
    QHash<QString, Tunnel> m_tunnels;
};
