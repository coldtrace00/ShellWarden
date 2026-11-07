#pragma once

#include <QByteArray>
#include <QString>

// Talks to the SSH key agent of this computer:
//  - Windows: the OpenSSH agent (named pipe \\.\pipe\openssh-ssh-agent), or Pageant
//    (PuTTY) through its WM_COPYDATA window.
//  - Linux: the agent in $SSH_AUTH_SOCK (ssh-agent, gpg-agent, KeePassXC, …).
// Used for logging in with agent keys and for agent forwarding.
namespace agent {

QString available();                    // "Pageant", "OpenSSH agent", "ssh-agent", or "" when none
int identityCount(QString *error = nullptr);   // keys the agent offers, -1 on error

// One request/response; both are full agent messages (4-byte length + body).
QByteArray request(const QByteArray &message, QString *error);

#ifdef _WIN32
// libssh can only use an agent through a socket on Windows: this starts (once)
// a tiny 127.0.0.1 bridge that relays to the agent, and returns a connected
// socket for ssh_set_agent_socket(), or -1.
long long bridgeSocket(QString *error);
#endif

} // namespace agent
