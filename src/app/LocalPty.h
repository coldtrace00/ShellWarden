#pragma once

#include <QObject>
#include <QStringList>
#include <atomic>
#include <thread>

// A shell of this computer in a pseudo terminal:
//  - Windows 10 1809+: ConPTY (PowerShell, cmd, WSL, Git Bash…)
//  - Linux: forkpty() with the user's $SHELL
class LocalPty : public QObject
{
    Q_OBJECT
public:
    explicit LocalPty(QObject *parent = nullptr);
    ~LocalPty() override;

    // program + arguments; workDir may be empty (= home folder)
    bool start(const QString &program, const QStringList &args, int cols, int rows, QString *error);
    void write(const QByteArray &data);
    void resize(int cols, int rows);
    void stop();
    bool running() const { return m_running; }

    static bool supported();

signals:
    void dataReceived(const QByteArray &data);
    void finished(int exitCode);

private:
    void readLoop();
    std::thread m_reader;
    std::atomic<bool> m_running{false};
#ifdef Q_OS_WIN
    void *m_hpc = nullptr;        // HPCON
    void *m_inWrite = nullptr;    // HANDLE: we write keys here
    void *m_outRead = nullptr;    // HANDLE: we read output here
    void *m_process = nullptr;    // HANDLE
#else
    int m_master = -1;
    int m_pid = -1;
#endif
};
