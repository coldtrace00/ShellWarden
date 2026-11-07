#include "CloudflaredManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

CloudflaredManager::CloudflaredManager(QObject *parent) : QObject(parent) {}

CloudflaredManager::~CloudflaredManager() { stopAll(); }

QString CloudflaredManager::findExe(const QString &custom)
{
    if (!custom.trimmed().isEmpty())
        return QFileInfo::exists(custom.trimmed()) ? custom.trimmed() : QString();
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("cloudflared.exe");
    const QString app = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        app + QStringLiteral("/cloudflared.exe"),
        app + QStringLiteral("/cloudflared/cloudflared.exe"),
        qEnvironmentVariable("ProgramFiles(x86)", QStringLiteral("C:/Program Files (x86)")) + QStringLiteral("/cloudflared/cloudflared.exe"),
        qEnvironmentVariable("ProgramFiles", QStringLiteral("C:/Program Files")) + QStringLiteral("/cloudflared/cloudflared.exe"),
        qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/Microsoft/WinGet/Links/cloudflared.exe"),
    };
    for (const QString &c : candidates)
        if (QFileInfo::exists(c))
            return QDir::toNativeSeparators(c);
    Q_UNUSED(name)
#endif
    return QStandardPaths::findExecutable(QStringLiteral("cloudflared"));
}

QString CloudflaredManager::version(const QString &exe)
{
    if (exe.isEmpty())
        return {};
    QProcess p;
    p.start(exe, {QStringLiteral("--version")});
    if (!p.waitForFinished(5000)) {
        p.kill();
        return {};
    }
    const QString out = QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError()).trimmed();
    return out.section(QLatin1Char('\n'), 0, 0);
}

bool CloudflaredManager::isReady(const QString &hostname) const
{
    const auto it = m_tunnels.constFind(hostname.toLower());
    return it != m_tunnels.constEnd() && it->ready && it->proc && it->proc->state() == QProcess::Running;
}

int CloudflaredManager::portFor(const QString &hostname) const
{
    return isReady(hostname) ? m_tunnels.value(hostname.toLower()).port : 0;
}

bool CloudflaredManager::start(const QString &hostnameIn, const QString &exeIn, const QString &tokenId,
                               const QString &tokenSecret, QString *error)
{
    const QString hostname = hostnameIn.trimmed().toLower();
    if (hostname.isEmpty()) {
        if (error) *error = tr("The host has no address (the hostname published in Cloudflare).");
        return false;
    }
    auto it = m_tunnels.find(hostname);
    if (it != m_tunnels.end() && it->proc && it->proc->state() != QProcess::NotRunning)
        return true;   // running or starting: the caller waits for ready()/failed()

    const QString exe = findExe(exeIn);
    if (exe.isEmpty()) {
        if (error) *error = exeIn.trimmed().isEmpty()
                                ? tr("cloudflared was not found. Install it (winget install Cloudflare.cloudflared) or choose cloudflared.exe in the proxy.")
                                : tr("cloudflared was not found at %1.").arg(exeIn);
        return false;
    }
    // a free local port
    int port = 0;
    {
        QTcpServer probe;
        if (probe.listen(QHostAddress::LocalHost, 0))
            port = probe.serverPort();
    }
    if (port == 0) {
        if (error) *error = tr("No free local port for cloudflared.");
        return false;
    }

    Tunnel t;
    t.port = port;
    t.proc = new QProcess(this);
    t.proc->setProcessChannelMode(QProcess::MergedChannels);
    QStringList args = {QStringLiteral("access"), QStringLiteral("tcp"),
                        QStringLiteral("--hostname"), hostname,
                        QStringLiteral("--url"), QStringLiteral("127.0.0.1:%1").arg(port)};
    if (!tokenId.trimmed().isEmpty() && !tokenSecret.isEmpty()) {
        // service token: no browser login (good for automation); passed through
        // the environment so it is not visible in the process list
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("TUNNEL_SERVICE_TOKEN_ID"), tokenId.trimmed());
        env.insert(QStringLiteral("TUNNEL_SERVICE_TOKEN_SECRET"), tokenSecret);
        t.proc->setProcessEnvironment(env);
    }
    QPointer<QProcess> proc = t.proc;
    connect(t.proc, &QProcess::readyRead, this, [this, hostname, proc] {
        if (!proc) return;
        auto i = m_tunnels.find(hostname);
        if (i == m_tunnels.end()) return;
        i->log += QString::fromUtf8(proc->readAll());
        if (i->log.size() > 8000) i->log = i->log.right(8000);
    });
    connect(t.proc, &QProcess::finished, this, [this, hostname, proc](int code, QProcess::ExitStatus) {
        auto i = m_tunnels.find(hostname);
        if (i == m_tunnels.end() || i->proc != proc) return;
        const bool wasReady = i->ready;
        QString last = i->log.trimmed().section(QLatin1Char('\n'), -2);
        i->proc->deleteLater();
        m_tunnels.erase(i);
        if (!wasReady)
            emit failed(hostname, tr("cloudflared stopped (code %1). %2").arg(code).arg(last));
    });
    connect(t.proc, &QProcess::errorOccurred, this, [this, hostname, proc](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        auto i = m_tunnels.find(hostname);
        if (i == m_tunnels.end() || i->proc != proc) return;
        i->proc->deleteLater();
        m_tunnels.erase(i);
        emit failed(hostname, tr("cloudflared could not be started."));
    });
    m_tunnels.insert(hostname, t);
    t.proc->start(exe, args);
    QTimer::singleShot(200, this, [this, hostname] { poll(hostname, 0); });
    return true;
}

void CloudflaredManager::poll(const QString &hostname, int attempt)
{
    auto it = m_tunnels.find(hostname);
    if (it == m_tunnels.end() || !it->proc || it->proc->state() == QProcess::NotRunning)
        return;   // finished() reports it
    QTcpSocket s;
    s.connectToHost(QStringLiteral("127.0.0.1"), quint16(it->port));
    if (s.waitForConnected(150)) {
        s.abort();
        it->ready = true;
        emit ready(hostname, it->port);
        return;
    }
    if (attempt > 75) {   // ~15 s
        const QString last = it->log.trimmed().section(QLatin1Char('\n'), -2);
        it->proc->kill();
        emit failed(hostname, tr("cloudflared did not open its local port. %1").arg(last));
        return;
    }
    QTimer::singleShot(200, this, [this, hostname, attempt] { poll(hostname, attempt + 1); });
}

void CloudflaredManager::stopAll()
{
    for (auto it = m_tunnels.begin(); it != m_tunnels.end(); ++it) {
        if (it->proc) {
            it->proc->disconnect(this);
            it->proc->kill();
            it->proc->waitForFinished(1000);
            delete it->proc;
        }
    }
    m_tunnels.clear();
}
