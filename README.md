# ShellWarden — SSH client for Windows 10/11 and Linux

**English** · [Українська](README.uk.md)

An open-source SSH client built with Qt 6 / QML: PuTTY's features, a Termius-style interface, and a strong focus on security.

## Features

**Connections**
- **Hosts**: cards with colour and avatar, groups, search, quick connect `user@host:port`.
- **Tabs**:
  - drag to reorder;
  - status and host colour;
  - proxy, jump and tunnel badges;
  - context menu: duplicate, move to a separate window, split, close.
- **Split view** (Ctrl+Shift+D): two sessions side by side.
- **Separate window**: any session can be moved to its own window and back.
- **Right-click a host**: *Connect via* (a route for this tab only) and *Route* (saved to the host).
- **Jump host (ProxyJump)**: proxy → jump → host.
- **Tunnels** L / R / D (SOCKS5): defined in the host or opened on the fly. A panel with an animated flow diagram and traffic counters.
- **SFTP panel** (works during the SSH session, over the same connection):
  - navigation; address bar with **Tab** completion;
  - permissions, owner, exact size of the selected file;
  - upload and download, drag & drop;
  - rename, delete, new folder;
  - transfer queue with cancel;
  - **edit files in place** (up to 5 MB, Ctrl+S, file permissions are kept) with syntax highlighting and line/column display.
- **Serial (COM)**: baud rate, data bits, parity, flow control.
- **Snippets**: saved commands and **.sh scripts** (import from file), newest first. Stored encrypted in the vault. A script is sent as one block and run by the interpreter from its `#!` line. Send to the current session or to all of them.
- **Server monitor** under the terminal: CPU, RAM, disk, uptime, network (one persistent channel, no extra logins).

**Terminal**
- libvterm: xterm-256color, true colour, Unicode, alt-screen (mc, htop, vim), mouse, bracketed paste.
- 39 colour themes: **Midnight** (default: deep navy, calm colours), ShellWarden, Deep Navy, Classic Dark, Graphite, Blue Team, Dracula, Catppuccin, Rosé Pine, Kanagawa, Tokyo Night, Nord, Ayu, Night Owl, Synthwave '84, GitHub, Solarized… Plus your own themes.
- **Output highlighting**: errors, warnings, OK, IPs, URLs, paths, dates — only in plain text; full-screen programs (vim, mc, htop) are left untouched.
- Terminal bell: off / flash / system sound.
- Any monospace font.
- PuTTY-style: selecting copies, right click pastes.

**Keys (Keychain)**
- Generate Ed25519 / ECDSA / RSA keys with a passphrase.
- Import OpenSSH, PEM and **PuTTY .ppk v2/v3**.
- In the host editor a key is picked from a list; you can also create or import one from there.
- `.pub` is stored as **one OpenSSH line** (`ssh-ed25519 AAAA… comment`). RFC 4716 is available as a separate export.
- Private keys are exported in OpenSSH format with permissions for the current user only.
- **Deploy to servers**: public keys are added to `~/.ssh/authorized_keys` on many servers at once — one key for all or one per server; afterwards the host logs in with the key.
- **SSH agent**: Pageant, the Windows "OpenSSH Authentication Agent" service, ssh-agent, KeePassXC, YubiKey. Agent key login and **agent forwarding** (`ssh -A`) are enabled per host.

**Security**
- **Vault**: Argon2id (256 MiB, 3 passes) → XChaCha20-Poly1305. Authenticated header, daily backups (14 days).
- **Password generator**: 19+ characters (length slider) with a strength estimate. A manual password needs 8+ characters, with a warning and an "I accept the risk" button.
- **Auto-lock**: after a set number of minutes or hours, on Windows lock / sleep, or never.
- **Master password per connection** (host → *Advanced protection*): asked even when the vault is unlocked; 5 wrong attempts → 30 s lockout. For real 2FA, set it up on the server (PAM + Google Authenticator) — ShellWarden shows the server's code prompt.
- **Known hosts**: fingerprint check, red warning when a host key changes; optional SSHFP (DNS) lookup.
- **Host notes**: encrypted in the vault like passwords.
- **Routes**: SOCKS5 / HTTP CONNECT / Tor / **Cloudflare Tunnel** (ShellWarden starts `cloudflared access tcp` itself, so port 22 can be closed on the server). DNS is resolved by the proxy. The check shows latency, exit IP and country.
- **IP in the server logs**: `$SSH_CONNECTION` is read and the address that ends up in `auth.log` is shown.
- **Kill switch**:
  - direct connections are forbidden;
  - the proxy is checked every **3 s** (configurable), the exit IP every minute;
  - if the proxy dies or starts exiting from your real IP, sessions are closed at once;
  - the program never falls back to a direct connection.
- **Firewall lock** (Windows): a Windows Defender Firewall rule lets ShellWarden.exe connect only to the proxy and localhost. Even a bug in the program cannot leak the real IP.
- **Activity log**: connections, route, the IP the server saw, vault / key / host changes, kill-switch events. No passwords or keys. Filters, search, CSV export, kept for 12 months.

**Other**
- **Updates**: from GitHub Releases with a mandatory SHA-256 check (`SHA256SUMS`). If the build has a release key (`-DAPP_UPDATE_PUBKEY=…`), only updates with a valid minisign signature (`SHA256SUMS.minisig`) are installed.
- **Languages**: English, Українська, Русский, Français, Español. Switch without restarting.
- **Portable mode**: all data next to the .exe.

## Technology stack

| Layer | What is used |
|---|---|
| Language | **C++17** (logic, networking, crypto) and **QML** (UI) |
| Framework | **Qt 6.5+** (tested with 6.12): Core, Gui, Quick, Quick Controls 2 (Basic style), Network, LinguistTools; when available — SerialPort (COM ports) and DBus (Linux: lock on sleep / screen lock) |
| SSH | **libssh** 0.10 / 0.11: sessions, SFTP, L/R/D tunnels, SSH agent and agent forwarding |
| Cryptography | **libsodium**: Argon2id, XChaCha20-Poly1305, BLAKE2b, Ed25519 (minisign signature check); **OpenSSL libcrypto** — only for PuTTY `.ppk` import |
| Terminal | **libvterm** (xterm-256color, true colour, Unicode); rendered by a custom `QQuickItem` |
| Code highlighting | custom `QSyntaxHighlighter` (Shell, Python, YAML, JSON, INI, nginx, Dockerfile, JS, C/C++, SQL, XML, Markdown) |
| Networking | SOCKS5 / HTTP CONNECT / Tor (`tor`) / Cloudflare Tunnel (`cloudflared`); IP check via ipinfo.io / ipify |
| Windows APIs | Winsock, DWM (dark title bar), WTS (session lock), Windows Firewall via `netsh`, named pipe / WM_COPYDATA (OpenSSH agent, Pageant) |
| Build | **CMake 3.21+**, **Ninja**; Windows — MinGW-w64 from the Qt kit, libraries from **MSYS2 UCRT64**, `windeployqt`; Linux — GCC/Clang and distribution packages |
| Packaging | **Inno Setup 6/7** (`setup.exe`), portable `.zip`, CPack (`.deb`, `.tar.gz`), **AppImage** (linuxdeploy + Qt plugin) |
| Releases | GitHub Releases, `SHA256SUMS`; optional **minisign** signature and Authenticode (`signtool`) |
| Translations | Qt Linguist (`.ts` → `.qm`) |
| Assets | flags — [flag-icons](https://github.com/lipis/flag-icons) (MIT), country names — [i18n-iso-countries](https://github.com/michaelwittig/node-i18n-iso-countries) (MIT) |

### How it fits together
- The **UI thread** (QML + `AppController`) never blocks on the network: every SSH session has its own `SshWorker` in a separate thread, which also runs SFTP, tunnels, the agent and the server monitor (one persistent channel).
- The **route** is a chain: `ProxyTunnel` (SOCKS5/HTTP/Tor/Cloudflare) → `JumpBridge` (jump host) → target server. The kill switch and `FirewallLock` make sure nothing goes out directly.
- The **vault** (`Vault`) is a single encrypted file; secrets in memory live in `SecureBytes` and are wiped after use.
- **Updates** (`UpdateChecker`): GitHub Releases API → pick the package for this system → verify `SHA256SUMS` (and minisign when the build has a key) → run the installer.

## Build and run

### Windows: one-time prerequisites
- **Qt 6.5 or newer** (tested with 6.12) from the [official installer](https://www.qt.io/download-qt-installer-oss), components: *MinGW 64-bit*, and under *Developer and Designer Tools* — *MinGW*, *CMake*, *Ninja*.
- **MSYS2** in `C:\msys64` ([msys2.org](https://www.msys2.org)) — libssh, libsodium and libvterm come from there. You do not need to install them yourself, `build.bat` does it.
- Optionally **Inno Setup 6 or 7** ([jrsoftware.org](https://jrsoftware.org/isdl.php)) — for the `setup.exe` installer.

```
git clone https://github.com/coldtrace00/ShellWarden.git
cd ShellWarden
```

### Windows — one click
Double-click **`build.bat`** in the project folder (or run it from a command prompt).
The script:
1. finds Qt (`C:\Qt\6.*\mingw_64`), MinGW, CMake, Ninja and MSYS2 (`C:\msys64`);
2. installs libssh, libsodium and libvterm into MSYS2 (only what is missing);
3. builds the program into `build\` and starts it.

The script only reads `C:\Qt` and never changes anything there.

**Installer and portable version:**
```
build.bat package
```
`dist\` will contain:
- `ShellWarden-<version>-portable.zip`;
- `ShellWarden-<version>-setup.exe` — only if [Inno Setup 6 or 7](https://jrsoftware.org/isdl.php) is installed. It asks for a folder, and all data then lives right there, in `data\`;
- `release\` — both files plus `SHA256SUMS`, ready to upload to a GitHub release.

Full build log: `build\build.log`.

### Linux
```
./build.sh            # build and run (offers to install dependencies if needed)
./build.sh package    # .deb and .tar.gz in build-linux/
./build.sh appimage   # a single ShellWarden-<version>-x86_64.AppImage for any distribution
```
Supports Ubuntu/Debian, Fedora and Arch.

## Releases and the in-app "Update" button

The program checks the **latest release** of `coldtrace00/ShellWarden` (built in at compile time, can be changed in *Settings → Updates*). For an update to work:

1. Bump the version in `CMakeLists.txt`: `project(ShellWarden VERSION 0.3.1 …)` — it must be **higher** than the installed version.
2. Build: `build.bat package`. `dist\release\` then holds exactly the files to upload:
   - `ShellWarden-0.3.1-setup.exe` — for the installed version;
   - `ShellWarden-0.3.1-portable.zip` — for the portable version;
   - `SHA256SUMS` — checksums (without it the program will not install the update).

   Put the Linux build (`./build.sh appimage`) into the same folder and recompute the sums: `sha256sum *.exe *.zip *.AppImage > SHA256SUMS`.
3. On GitHub: **Releases → Draft a new release → Choose a tag** → `v0.3.1` → *Create new tag*, drag in all files from `dist\release\`, click **Publish release**. Drafts and pre-releases are not seen by the program.
4. In the program: *Settings → Updates →* **Check now** → **Download** → **Install**. The installer silently installs the new version into the same folder; `data\` (vault, keys, settings) is not touched. The portable version opens the folder with the verified zip — extract it over the old one.

The check also runs automatically 4 s after start (can be turned off in the same place).

### Release signing (optional)
Once: `minisign -G -p minisign.pub -s ~/.minisign/minisign.key` (the secret key stays with you, protected by its own password).
Build with `-DAPP_UPDATE_PUBKEY="RWQ…"` (second line of `minisign.pub`; for `build.bat`: `set APP_UPDATE_PUBKEY=RWQ…`). Before publishing a release:
```
tools/sign_release.sh dist/release 0.4.0                                   # Linux
powershell -File tools\sign_release.ps1 -Folder dist\release -Version 0.4.0  # Windows (+ -CertThumbprint for Authenticode)
```
The script creates `SHA256SUMS` and `SHA256SUMS.minisig` — upload them to the release together with the files.

## Where data is stored

| Mode | Folder |
|---|---|
| Installed with `setup.exe` | `<install folder>\data\` |
| Portable (zip) | `data\` next to the .exe — an empty file named `portable` next to the program is enough |
| Run from `build\`, .deb, AppImage | `%APPDATA%\ShellWarden\` (Linux: `~/.local/share/ShellWarden/`) |

The folder contains:
- `vault.swv` — the vault (hosts, passwords, keys, notes — all encrypted), with `vault.swv.bak` next to it;
- `backups/` — daily copies;
- `known_hosts`;
- `settings.ini`;
- `logs/` — activity log;
- `backgrounds/` — pictures of your own themes (if any).

The installer asks for a folder, and that is where everything is kept: `<folder>\data` (the folder gets write permissions and stays when the program is uninstalled). A new install starts from scratch: nothing is taken from `%APPDATA%` (move data with *Save backup* / *Restore*). Updates are installed by the same installer.

For the installed and portable versions the same folder also holds `keys/` (default location for exported keys), `updates/` (downloaded updates), `cache/`, `tor/` (if ShellWarden starts tor.exe). The program then writes nothing to the registry, `%APPDATA%` or user folders; the portable archive (`build.bat package`) already contains the `portable` file.

**None of this is in the repository**: `.gitignore` excludes `data/`, `*.swv`, `*.key`, `build*/` and `dist/`.

### Independent encryption check
```
pip install argon2-cffi cryptography
python tools\verify_vault.py "%APPDATA%\ShellWarden\vault.swv"
```
The script decrypts the vault with reference libraries, without ShellWarden or libsodium code:
- Argon2id — argon2-cffi;
- XChaCha20-Poly1305 — `cryptography` (OpenSSL) and HChaCha20 with a test vector.

A wrong password or a single changed byte gives FAILED.

## Keyboard shortcuts

| | |
|---|---|
| Ctrl+Shift+C / Ctrl+Shift+V, Shift+Insert | copy / paste |
| Shift+PgUp / PgDn | scroll history |
| Ctrl + wheel, Ctrl +/- | font size |
| Ctrl+Tab, Ctrl+Shift+Tab | switch tabs |
| Ctrl+Shift+W | close tab |
| Ctrl+Shift+D | split view |
| Ctrl+Shift+B | hide / show the sidebar |
| Ctrl+Shift+T | hosts |
| Ctrl+Shift+L | lock the vault |

Plain Ctrl+C, Ctrl+W, Ctrl+R, Tab etc. always go to the server.

## Project layout

```
src/core        Vault, KeyTools (+.ppk), KnownHosts, SshWorker (thread), SFTP, tunnels,
                JumpBridge, ProxyTunnel, FirewallLock, AgentClient, Minisign, ActivityLog, Paths
src/terminal    TerminalEmulator (libvterm, themes), TerminalItem (Qt Quick rendering)
src/app         AppController, models, TerminalSession, ProxyChecker, UpdateChecker,
                CloudflaredManager, TorManager, SyntaxHighlighter, DarkTitleBar
qml             user interface
i18n            translations (*.ts) and translations.py
branding        logo, .png / .ico icons
packaging       Inno Setup (Windows), .desktop (Linux)
tools           verify_vault.py, sign_release.sh / .ps1 (release signing)
```

License: MIT.
