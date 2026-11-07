#include "BatchRunner.h"

#include "core/SecureBytes.h"

#include <QDateTime>
#include <QThread>

BatchRunner::BatchRunner(QObject *parent)
    : QObject(parent)
{
}

BatchRunner::~BatchRunner()
{
    cancel();
}

void BatchRunner::run(const QVector<Job> &jobs, const QString &command)
{
    cancel();
    ++m_generation;
    m_jobs = jobs;
    for (Job &j : m_jobs) {
        j.params.execCommand = j.command.isEmpty() ? command : j.command;
        if (!j.skipReason.isEmpty()) {
            j.state = QStringLiteral("skipped");
            j.error = j.skipReason;
        }
    }
    m_running = true;
    m_active = 0;
    emit changed();
    startNext();
}

void BatchRunner::startNext()
{
    for (int i = 0; i < m_jobs.size() && m_active < kParallel; ++i) {
        Job &j = m_jobs[i];
        if (j.state != QLatin1String("queued"))
            continue;
        j.state = QStringLiteral("running");
        j.startedMs = QDateTime::currentMSecsSinceEpoch();
        ++m_active;

        auto *thread = new QThread;
        auto *w = new SshWorker;
        w->moveToThread(thread);
        j.worker = w;
        const quint64 gen = m_generation;
        connect(thread, &QThread::finished, w, &QObject::deleteLater);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        // no one can answer prompts here: refuse them and explain
        connect(w, &SshWorker::hostKeyCheck, this, [this, i, gen, w](int status, const QString &, const QString &) {
            QMetaObject::invokeMethod(w, "answerHostKey", Qt::QueuedConnection, Q_ARG(bool, false));
            if (gen == m_generation)
                m_jobs[i].error = status == SshWorker::HostUnknown
                    ? tr("The server's key is not trusted yet: connect to it once in a tab.")
                    : tr("The server's key has CHANGED: open it in a tab and check the key.");
        });
        connect(w, &SshWorker::secretRequired, this, [this, i, gen, w](int kind, const QString &, bool, bool) {
            QMetaObject::invokeMethod(w, "answerSecret", Qt::QueuedConnection, Q_ARG(QString, QString()), Q_ARG(bool, true));
            if (gen == m_generation)
                m_jobs[i].error = kind == SshWorker::SecretKeyPassphrase
                    ? tr("The key passphrase is not saved in the vault.")
                    : tr("The password is not saved in the vault (or was rejected).");
        });
        connect(w, &SshWorker::execDone, this, [this, i, gen](int, int code, const QByteArray &out, const QString &err) {
            if (gen != m_generation)
                return;
            Job &jj = m_jobs[i];
            jj.gotResult = true;
            jj.exitCode = code;
            jj.output = QString::fromUtf8(out);
            if (!err.isEmpty())
                jj.error = err;
        });
        connect(w, &SshWorker::finished, this, [this, i, gen, thread](const QString &reason, bool isError) {
            thread->quit();
            if (gen != m_generation)
                return;
            Job &jj = m_jobs[i];
            if (jj.gotResult && jj.error.isEmpty() && jj.exitCode == 0)
                finishJob(i, QStringLiteral("ok"));
            else if (jj.gotResult)
                finishJob(i, QStringLiteral("fail"),
                          jj.error.isEmpty() ? tr("Exit code %1").arg(jj.exitCode) : jj.error);
            else
                finishJob(i, QStringLiteral("fail"), jj.error.isEmpty() ? (isError ? reason : tr("Disconnected.")) : jj.error);
        });
        thread->start();
        const ConnectParams p = j.params;
        QMetaObject::invokeMethod(w, [w, p] { w->start(p); }, Qt::QueuedConnection);
    }
    if (m_active == 0 && m_running) {
        m_running = false;
        emit changed();
        emit finished(okCount(), failCount());
    }
}

void BatchRunner::finishJob(int i, const QString &state, const QString &error)
{
    Job &j = m_jobs[i];
    j.state = state;
    if (!error.isEmpty())
        j.error = error;
    j.ms = QDateTime::currentMSecsSinceEpoch() - j.startedMs;
    secure::wipe(j.params.password);
    --m_active;
    emit changed();
    startNext();
}

void BatchRunner::cancel()
{
    if (!m_running)
        return;
    ++m_generation;
    for (Job &j : m_jobs) {
        if (j.state == QLatin1String("running") && j.worker)
            QMetaObject::invokeMethod(j.worker, "stop", Qt::QueuedConnection);
        if (j.state == QLatin1String("running") || j.state == QLatin1String("queued")) {
            j.state = QStringLiteral("fail");
            j.error = tr("Cancelled");
        }
    }
    m_active = 0;
    m_running = false;
    emit changed();
}

void BatchRunner::clear()
{
    cancel();
    m_jobs.clear();
    emit changed();
}

QVariantList BatchRunner::results() const
{
    QVariantList out;
    for (const Job &j : m_jobs)
        out << QVariantMap{{QStringLiteral("hostId"), j.hostId}, {QStringLiteral("label"), j.label},
                           {QStringLiteral("endpoint"), j.endpoint}, {QStringLiteral("state"), j.state},
                           {QStringLiteral("exitCode"), j.exitCode}, {QStringLiteral("output"), j.output},
                           {QStringLiteral("error"), j.error}, {QStringLiteral("ms"), j.ms}};
    return out;
}

int BatchRunner::okCount() const
{
    int n = 0;
    for (const Job &j : m_jobs) n += j.state == QLatin1String("ok");
    return n;
}

int BatchRunner::failCount() const
{
    int n = 0;
    for (const Job &j : m_jobs) n += j.state == QLatin1String("fail") || j.state == QLatin1String("skipped");
    return n;
}

int BatchRunner::doneCount() const
{
    int n = 0;
    for (const Job &j : m_jobs) n += j.state != QLatin1String("queued") && j.state != QLatin1String("running");
    return n;
}
