# Changelog

Versions follow [semantic versioning](https://semver.org): `MAJOR.MINOR.PATCH`.
While the major version is 0, a new MINOR may change behaviour; PATCH releases
only fix things. Pre-releases are marked as such on GitHub and are not offered
by the in-app updater.

## 0.5.0 — 2026-11-08

New features are why this is 0.5 and not 0.4.2 (see the versioning note above).

### Added
- **Key passphrase: add, change or remove** (Keys → ⋯). The key file is written
  again with the new passphrase (OpenSSH format, bcrypt_pbkdf); the key itself
  and its fingerprint stay the same, so nothing changes on the servers. A key
  protected by the master password asks for it. Entries that hold only a public
  key now say so instead of "No passphrase".
- **The journal shows how you signed in**: `key "Work laptop" (SHA256:…)`, a key
  from the SSH agent, password, keyboard-interactive — or several, when the
  server asked for two factors.

### Security
- **SSH agent on Windows: no port open to other programs.** libssh got the agent
  through a 127.0.0.1 port that any process — also another Windows user on the
  same PC (RDP, terminal server) — could connect to and use the keys. Now every
  agent connection is a private socket pair made for it (the other end is
  checked); nothing listens.
- **A changed master password also re-encrypts the backups** (`backups/*.swv`
  and `vault.swv.bak`), protected secrets inside them included. Backups made
  with an even older password are listed and can be deleted.
- **The IP check through Shadowsocks uses HTTPS inside the tunnel**, with the
  certificate checked, so the Shadowsocks server cannot fake the country or IP.
- **Honest documentation of the rich-text limit.** Qt reads an image inside
  rich text from `file://server/…` without asking the URL check (also in current
  Qt), so only plain text protects there. README, `QmlSafety.h` and a test now
  say exactly that; the test fails if Qt ever changes, so the docs can follow.

### Fixed
- The tunnels integration test could miss the forward states (a nested event
  loop inside a slot); it now runs from the main loop. 8 of 8 runs pass.

## 0.4.1 — 2026-11-08

Fixes from the second review.

### Security
- **Tooltips could load from a Windows share.** The shared tooltip of the Qt
  style shows text as rich text, and the "online" dot's tooltip shows the reply
  of an HTTP proxy that refused a connection. A proxy owner (or anyone on the way
  to it — that connection is not encrypted) could put `<img src='file://server/…'>`
  in it and, on hover, make Windows connect to that share outside the proxy and
  kill switch (real IP, NTLM hash). Now: the tooltip shows plain text; the
  interface refuses every address that is not built in or on this computer —
  also `file://server/…` and `\\server\share` (QML URL interceptor + network
  access); proxy replies are cut to 64 printable characters; server error
  messages are cleaned; `$SSH_CONNECTION` is used only if it really is
  "address port address port".
- New test `qml_safety_tests` (also on Windows with the Qt of the releases) and
  a CI check `tests/check_qml_text.py` (every QML text is plain text, no raw
  Button/MenuItem/CheckBox whose label shows HTML — in Qt 6.4 such a Button even
  crashes on `<img>`).

### Fixed
- **A failed master password change could leave the vault without its key**
  (open on screen, but every save failed until restart). The current password
  is now checked against the key in memory; if the new file cannot be written,
  the old password stays in force. Unit tests for both.
- Background threads (Shadowsocks relays, the Windows agent bridge, the
  Shadowsocks IP check) are stopped and waited for when the program closes
  (they wait in half-second steps: on Windows a cut socket does not wake them).
- "Install keys on servers" no longer grows past its right edge in Ukrainian,
  Russian or French: the mode buttons wrap, the key is chosen on its own line.
- `tests/integration/setup.sh | tail` no longer hangs; `sodium_init()` is
  checked in all tests.

### Other
- README: Qt 6.4 or newer (CI tests 6.4.2 and 6.12).
- AppController split into Hosts, Keys, Proxies, Sessions and Batch files.

## 0.4.0 — 2026-11-07

### Security
- **Activity journal encrypted on disk.** Each entry is a sealed box for the
  journal's public key; the secret key lives in the vault. Written even while the
  vault is locked, readable only while it is open. Old plain journals are
  encrypted and removed on the first unlock. Kept 30 days by default (7/30/90/365),
  or turned off.
- **Data folder private to the user**: Linux `0700`/`0600`; Windows access list
  (user, SYSTEM, Administrators) set by the installer and checked at start.
  Linux updates are downloaded into the data folder, not the shared `/tmp`.
- **Protected hosts are really protected**: for hosts that ask for the master
  password, the password and private key stay sealed while the vault is open
  and are opened only to connect (Argon2id with the vault's cost).
- **SFTP downloads**: file names from the server are sanitized (no path out of
  the chosen folder on Windows, no reserved names or control characters).
- **Text from servers is never rich text** and the interface has no network
  access: a file name or message like `<img src=…>` can no longer make the program
  load something from the network or a Windows share (which would bypass the proxy).
- **Pasting**: control characters (ESC…) are removed; multi-line text is shown
  and confirmed first.
- **Updates**: the signed version must match the release and be newer (no
  downgrade); `SHA256SUMS` lines must name the exact file.
- **Minimum library versions**: libssh 0.10.6 (Terrapin), libsodium 1.0.18,
  OpenSSL 3.0 — at build time and again at start.
- `known_hosts` host names are hashed (like `HashKnownHosts`).
- Command history skips database shells (`mysql>`, `psql`…) and commands that
  look like they contain a password or token; can be turned off per host.
- Smaller fixes: local socket pair checks it got its own connection, terminal
  title limited to 4 KB, PuTTY key Argon2 limited to 1 GiB, vault limited to
  1 GiB, `.bak` replaced atomically, Shadowsocks encryption errors handled.
- README: honest limits of the firewall lock and of the kill switch timing,
  a threat model, the external services the program contacts.

### Added
- **Shadowsocks 2022** (`2022-blake3-aes-256-gcm`, `…-aes-128-gcm`,
  `…-chacha20-poly1305`), including identity headers for multi-user servers.
- Tests in the repository (unit, integration, fuzzing) and GitHub Actions CI
  (Linux, ASan + UBSan, fuzzing, Windows build).
- SECURITY.md, this changelog.

## 0.3.3 — 2026-11-07
- Shadowsocks proxy type (AEAD 2017), proxies can be turned off, "via proxy /
  direct" next to the server's view of your IP, single-pane SFTP with a Transfer
  menu and multi-select download, resume of broken transfers, command history,
  key replacement in 3 checked steps, online dot (opt-in), clearer IP-check errors.

## 0.3.1 — 2026-11-07
- Faster connect, update window with release notes in the app language.

## 0.3.0 — 2026-11-07
- First public release.
