# Security policy

## Reporting a vulnerability

Please report security problems **privately**, through GitHub:
**Security** tab of this repository → **Report a vulnerability**
(GitHub Private Vulnerability Reporting). Do not open a public issue for them.

Useful in a report: the ShellWarden version (Settings → About), the operating
system, what you did, what happened, and what an attacker could gain. A proof
of concept helps but is not required.

What to expect:
- an answer within 7 days;
- a fix as fast as the problem requires, released as a signed update (see
  "Release signing" in the README) and described in [CHANGELOG.md](CHANGELOG.md);
- credit in the changelog if you want it.

## Supported versions

Only the latest release gets security fixes. The in-app updater installs it
(signed with the key in `minisign.pub`).

## Scope

In scope: everything in this repository — the vault and its encryption, the
journal, protected hosts, SSH / SFTP / tunnels, proxies, Tor / Cloudflare /
Shadowsocks routes, the kill switch, the updater, the installer.

What ShellWarden does and does not defend against is described in the
README, section **Threat model**. Bugs in libssh, libsodium, OpenSSL or Qt
should go to those projects; tell us too if ShellWarden needs a newer minimum
version.
