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

// A connected socket for ssh_set_agent_socket() that relays to request(), or
// -1. libssh can use an agent only through a socket, so on Windows (pipe /
// Pageant window) every agent login gets one. No port is left listening: it
// is a private pair made for this one call. Works everywhere (tested on Linux).
long long bridgeSocket(QString *error);

} // namespace agent
