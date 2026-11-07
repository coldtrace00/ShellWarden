#pragma once

#include "SshWorker.h"

#include <atomic>
#include <thread>

// ProxyJump without OpenSSH: connects to the jump host, opens a
// "direct-tcpip" channel to the target and copies bytes between that channel
// and one end of a loopback socket pair on its own thread. The other end is
// handed to the target's libssh session (SSH_OPTIONS_FD).
//
// The jump session is used only by the bridge thread after start() returns.
class JumpBridge
{
public:
    JumpBridge() = default;
    ~JumpBridge();
    JumpBridge(const JumpBridge &) = delete;
    JumpBridge &operator=(const JumpBridge &) = delete;

    // Blocking. Returns the socket for the target session, or kInvalidSocket.
    sw_socket_t start(const JumpParams &jump, const ProxyConfig &proxy, const QString &knownHosts,
                      const QString &targetHost, int targetPort, int timeoutSeconds, QString *error);
    void stop();

private:
    bool authenticate(const JumpParams &jump, QString *error);
    void run();

    ssh_session m_session = nullptr;
    ssh_channel m_channel = nullptr;
    sw_socket_t m_local = kInvalidSocket;   // our end of the pair
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
};
