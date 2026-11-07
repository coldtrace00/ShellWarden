#include "TorManager.h"
#include "core/Paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QDateTime>
#include <QHash>
#include <QMutex>

TorManager::TorManager(QObject *parent)
    : QObject(parent)
{
    m_session = QString::number(QRandomGenerator::system()->generate64(), 36);
}

TorManager::~TorManager()
{
    stop();
}

void TorManager::setState(const QString &s, const QString &msg)
{
    m_state = s;
    m_message = msg;
    emit changed();
}

bool TorManager::portOpen(int port, int timeoutMs)
{
    // Results are cached for a few seconds: on Windows a closed local port
    // only fails after the timeout, and this is asked often.
    static QHash<int, QPair<qint64, bool>> cache;
    static QMutex mutex;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    {
        QMutexLocker l(&mutex);
        const auto it = cache.constFind(port);
        if (it != cache.constEnd() && now - it->first < 4000)
            return it->second;
    }
    const bool ok = probe(port, timeoutMs);
    QMutexLocker l(&mutex);
    cache.insert(port, {now, ok});
    return ok;
}

bool TorManager::probe(int port, int timeoutMs)
{
    QTcpSocket s;
    s.connectToHost(QStringLiteral("127.0.0.1"), quint16(port));
    const bool ok = s.waitForConnected(timeoutMs);
    s.abort();
    return ok;
}

QString TorManager::findTorExe()
{
#ifdef Q_OS_WIN
    const QString exe = QStringLiteral("tor.exe");
#else
    const QString exe = QStringLiteral("tor");
#endif
    const QString app = QCoreApplication::applicationDirPath();
    const QStringList dirs = {app + QStringLiteral("/tor"), app, paths::dataDir() + QStringLiteral("/tor")};
    for (const QString &d : dirs)
        if (QFileInfo::exists(d + QLatin1Char('/') + exe))
            return QDir::toNativeSeparators(d + QLatin1Char('/') + exe);
    return QStandardPaths::findExecutable(exe.section(QLatin1Char('.'), 0, 0));
}

int TorManager::portFor(const QString &mode, QString *which) const
{
    auto tell = [which](const QString &w) { if (which) *which = w; };
    if (mode == QLatin1String("exe")) {
        tell(QStringLiteral("own"));
        return m_state == QLatin1String("ready") ? kOwnPort : 0;
    }
    if (mode == QLatin1String("browser") || mode == QLatin1String("auto")) {
        if (portOpen(kBrowserPort)) { tell(QStringLiteral("browser")); return kBrowserPort; }
        if (mode == QLatin1String("browser")) { tell(QStringLiteral("browser")); return 0; }
    }
    if (portOpen(kServicePort)) { tell(QStringLiteral("service")); return kServicePort; }
    // our own tor may already run (started for another proxy)
    if (mode == QLatin1String("auto") && m_state == QLatin1String("ready")) { tell(QStringLiteral("own")); return kOwnPort; }
    tell(mode == QLatin1String("auto") ? QString() : QStringLiteral("service"));
    return 0;
}

void TorManager::start(const QString &exePath)
{
    if (m_proc && m_proc->state() != QProcess::NotRunning)
        return;
    QString exe = exePath.trimmed();
    if (exe.isEmpty())
        exe = findTorExe();
    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        setState(QStringLiteral("error"), tr("tor.exe not found. Download the \"Tor Expert Bundle\" from torproject.org and choose tor.exe."));
        emit failed(m_message);
        return;
    }
    if (portOpen(kOwnPort, 200)) {   // a tor we started earlier is still running
        m_progress = 100;
        setState(QStringLiteral("ready"));
        emit ready();
        return;
    }
    const QString dataDir = paths::dataDir() + QStringLiteral("/tor");
    QDir().mkpath(dataDir);
    delete m_proc;
    m_proc = new QProcess(this);
    m_proc->setProgram(exe);
    m_proc->setWorkingDirectory(QFileInfo(exe).absolutePath());
    m_proc->setArguments({QStringLiteral("--SocksPort"), QStringLiteral("127.0.0.1:%1 IsolateSOCKSAuth").arg(kOwnPort),
                          QStringLiteral("--DataDirectory"), QDir::toNativeSeparators(dataDir),
                          QStringLiteral("--ClientOnly"), QStringLiteral("1"),
                          QStringLiteral("--Log"), QStringLiteral("notice stdout")});
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    m_progress = 0;
    setState(QStringLiteral("starting"), tr("Starting Tor…"));
    connect(m_proc, &QProcess::readyRead, this, [this] {
        static const QRegularExpression re(QStringLiteral("Bootstrapped (\\d+)%"));
        while (m_proc->canReadLine()) {
            const QString line = QString::fromUtf8(m_proc->readLine()).trimmed();
            const auto m = re.match(line);
            if (m.hasMatch()) {
                m_progress = m.captured(1).toInt();
                if (m_progress >= 100) {
                    setState(QStringLiteral("ready"));
                    emit ready();
                } else {
                    setState(QStringLiteral("starting"), tr("Connecting to the Tor network: %1%").arg(m_progress));
                }
            } else if (line.contains(QLatin1String("[err]"))) {
                m_message = line.section(QStringLiteral("[err] "), 1);
            }
        }
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            setState(QStringLiteral("error"), tr("Could not start tor: %1").arg(m_proc->errorString()));
            emit failed(m_message);
        }
    });
    connect(m_proc, &QProcess::finished, this, [this](int code) {
        if (m_state == QLatin1String("off"))
            return;
        const QString why = m_message.isEmpty() || m_state != QLatin1String("error") ? tr("tor stopped (exit code %1)").arg(code) : m_message;
        setState(QStringLiteral("error"), why);
        emit failed(why);
    });
    m_proc->start();
}

void TorManager::stop()
{
    if (!m_proc)
        return;
    m_state = QStringLiteral("off");
    if (m_proc->state() != QProcess::NotRunning) {
        m_proc->terminate();
        if (!m_proc->waitForFinished(1500))
            m_proc->kill();
    }
    m_progress = 0;
    emit changed();
}

QString TorManager::isolationUser(const QString &proxyId) const
{
    return QStringLiteral("sw-%1-%2-%3").arg(m_session, proxyId.left(8)).arg(m_identity.value(proxyId));
}

void TorManager::newIdentity(const QString &proxyId)
{
    m_identity[proxyId] = m_identity.value(proxyId) + 1;
}
