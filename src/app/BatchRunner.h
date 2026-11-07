#pragma once

#include "core/SshWorker.h"

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVector>

// Runs one command on several hosts at once (each over its own short SSH
// connection, no terminal) and collects "ok / error" with the output.
class BatchRunner : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList results READ results NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(int okCount READ okCount NOTIFY changed)
    Q_PROPERTY(int failCount READ failCount NOTIFY changed)
    Q_PROPERTY(int doneCount READ doneCount NOTIFY changed)
public:
    struct Job
    {
        QString hostId, label, endpoint;
        ConnectParams params;
        QString skipReason;     // set = not run
        QString command;        // empty = the command given to run()
        // filled while running
        QString state = QStringLiteral("queued");   // queued | running | ok | fail | skipped
        int exitCode = -1;
        QString output, error;
        qint64 startedMs = 0, ms = 0;
        QPointer<SshWorker> worker;
        bool gotResult = false;
    };

    explicit BatchRunner(QObject *parent = nullptr);
    ~BatchRunner() override;

    void run(const QVector<Job> &jobs, const QString &command);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();

    QVariantList results() const;
    bool running() const { return m_running; }
    int okCount() const;
    int failCount() const;
    int doneCount() const;

    static constexpr int kParallel = 5;

signals:
    void changed();
    void finished(int ok, int failed);

private:
    void startNext();
    void finishJob(int i, const QString &state, const QString &error = QString());
    QVector<Job> m_jobs;
    bool m_running = false;
    int m_active = 0;
    quint64 m_generation = 0;   // ignore late signals of a cancelled run
};
