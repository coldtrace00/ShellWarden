#pragma once

#include <QString>
#include <QVariantList>

// Reading existing host lists so moving to ShellWarden is one click.
// Every entry is a map: label, hostname, port, username, keyFile, jump, group, source.
namespace hostimport {

// OpenSSH client config (~/.ssh/config). Wildcard patterns (*, ?, !) and
// Match blocks are skipped; Include is followed (one level, relative to ~/.ssh).
QVariantList parseSshConfig(const QString &text, const QString &baseDir = {});
QVariantList readSshConfig();            // ~/.ssh/config, empty if missing
QString sshConfigPath();

// PuTTY saved sessions: the Windows registry
// (HKCU\Software\SimonTatham\PuTTY\Sessions) or ~/.putty/sessions on Linux.
// Only SSH sessions are returned ("Default Settings" is skipped).
QVariantList readPuttySessions();

} // namespace hostimport
