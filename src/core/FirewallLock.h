#pragma once

#include <QString>
#include <QHostAddress>
#include <QStringList>

// Hard kill switch (Windows): a Windows Defender Firewall rule that blocks
// every outbound connection of this executable except to the proxy servers
// (and loopback). Even a bug in the app cannot then reveal the real IP:
// the operating system drops the packets. Changing the rule needs
// administrator rights, so Windows shows a UAC prompt.
namespace firewalllock {

bool supported();                         // Windows only
bool isActive();                          // rule present (no admin needed)
QStringList allowedAddresses();           // IPs in the active rule
// proxyHosts: names or IPs; they are resolved now (IPv4/IPv6).
bool apply(const QStringList &proxyHosts, QString *error);
bool remove(QString *error);

// The remoteip list the rule blocks (everything except `allowed` and loopback).
QStringList blockedRanges(const QList<QHostAddress> &allowed);

} // namespace firewalllock
