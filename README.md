# ShellWarden — SSH client for Windows 10/11 and Linux

**English** · [Українська](README.uk.md)

An open-source SSH client built with Qt 6 / QML: PuTTY's features, a Termius-style interface, and a strong focus on security.

## Features

**Connections**
- **Hosts**: cards with colour and avatar, groups, search, quick connect `user@host:port`.
- **Online dot** on each card (green / red, with the response time; off by default, Settings → *Show which hosts are online*): while the host list is open, one short connection per host every 5 minutes, through the host's own proxy. Hosts via Tor, Cloudflare or a jump host, and direct ones while the kill switch is on, are not checked.
- **Tabs**:
  - drag to reorder;
  - status and host colour;
  - proxy, jump and tunnel badges;
  - context menu: duplicate, move to a separate window, split, close.
- **Split view** (Ctrl+Shift+D): two sessions side by side.
- **Separate window**: any session can be moved to its own window and back.
- **Right-click a host**: *Connect once via* (a route for this tab only) and *Always connect via* (saved to the host).
- **Jump host (ProxyJump)**: proxy → jump → host.
- **Tunnels** L / R / D (SOCKS5): defined in the host or opened on the fly. A panel with an animated flow diagram and traffic counters.
- **SFTP panel** (works during the SSH session, over the same connection):
  - navigation; address bar with **Tab** completion;
  - permissions, owner, exact size of the selected file;
  - one **Transfer ▾** button: *To the server* (choose files on this PC) or *To this PC* (save the files selected on the server; select several with Ctrl / Shift + click); Explorer drag & drop also uploads;
  - **resume**: a broken transfer continues from where it stopped when you start it again (download → `.part` next to the file, upload → `<name>.swpart` on the server, renamed at the end, permissions kept);
  - rename, delete, new folder;
  - transfer queue with cancel;
  - **edit files in place** (up to 5 MB, Ctrl+S, file permissions are kept) with syntax highlighting and line/column display.
- **Serial (COM)**: baud rate, data bits, parity, flow control.
- **Command history per host** (Ctrl+Shift+H or the clock button): search the commands you ran on this server, put one into the prompt or run it again. Stored encrypted in the vault; a command typed with a space in front is not remembered (like bash's `ignorespace`).
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
- **Replace a key on servers** (Keychain → On servers → *Replace an old key*): three safe steps per server — log in as now and add the new key; log in with **only** the new key and remove the old one; log in with only the new key once more and check that the old key is really gone. Where the new key does not work, the old one stays, so you cannot lock yourself out. Only servers that passed all three steps are switched to the new key.
- **SSH agent**: Pageant, the Windows "OpenSSH Authentication Agent" service, ssh-agent, KeePassXC, YubiKey. Agent key login and **agent forwarding** (`ssh -A`) are enabled per host.

**Security**
- **Vault**: Argon2id (256 MiB, 3 passes) → XChaCha20-Poly1305. Authenticated header, daily backups (14 days).
- **Password generator**: 19+ characters (length slider) with a strength estimate. A manual password needs 8+ characters, with a warning and an "I accept the risk" button.
- **Auto-lock**: after a set number of minutes or hours, on Windows lock / sleep, or never.
- **Master password per connection** (host → *Advanced protection*): asked even when the vault is unlocked; 5 wrong attempts → 30 s lockout. The host's password and its private key (with the passphrase) stay **encrypted a second time** while the vault is open — sealed with a key pair whose secret half is derived from the master password (Argon2id with the vault's own cost) only for the moment of connecting, then wiped. A key used by such a host is protected for every host that uses it. Turning the option off asks for the master password. Once connected, the session keeps what it needs in memory until the tab is closed. For real 2FA, set it up on the server (PAM + Google Authenticator) — ShellWarden shows the server's code prompt.
- **Known hosts**: fingerprint check, red warning when a host key changes; optional SSHFP (DNS) lookup. Host names in the file are **hashed** like OpenSSH's `HashKnownHosts`, so the file does not list where you connect (the Known hosts page shows the names of your own hosts while the vault is open).
- **Pasting**: control characters (ESC…) are always removed from pasted text, so copied text cannot end bracketed paste early and run commands; text with line breaks (or very long) is shown first and pasted only after you confirm, like Windows Terminal.
- **SFTP downloads**: file names from the server are made safe (no `..\`, `/`, `\`, `:`, control or bidi characters, no `CON`/`NUL`…), so a hostile server cannot write outside the folder you chose.
- **Text from servers is shown as plain text** (file names, messages, banners, tooltips, proxy replies): never as rich text. On top of that, the interface itself cannot load anything from the network or from another computer (`http`, `file://server/…`, `\\server\share`): every address it would open is checked, so even text that slipped through could not reveal your IP or send a Windows (NTLM) login to a share. A test (`qml_safety_tests`) and a CI check (`tests/check_qml_text.py`) keep it so.
- **Host notes**: encrypted in the vault like passwords.
- **Routes**: SOCKS5 / HTTP CONNECT / **Shadowsocks** / Tor / **Cloudflare Tunnel** (ShellWarden starts `cloudflared access tcp` itself, so port 22 can be closed on the server). DNS is resolved by the proxy. The check shows latency, exit IP and country.
  - **Shadowsocks** (built in, no extra program). Everything up to the Shadowsocks server is encrypted and looks like random bytes, so the network in between sees neither the target server nor that it is SSH — useful where SSH or proxies are blocked. Paste an `ss://` link (Outline and others) or fill in the fields. Links that need a plugin (`plugin=`) are not supported.
    - **Shadowsocks 2022** (recommended): `2022-blake3-aes-256-gcm`, `2022-blake3-aes-128-gcm`, `2022-blake3-chacha20-poly1305`; the password is the server's base64 key; identity headers (`iPSK:uPSK`, multi-user servers) for the aes ciphers. Protects against replayed connections; the server's clock must be within ±30 s.
    - **Older AEAD (2017)**: `chacha20-ietf-poly1305`, `aes-256-gcm`, `aes-128-gcm`. Honest limits of that protocol (not of this code): the key comes from the password through MD5 without salt, there is no protection against replayed connections, and active probing of the server can reveal it. Use it only where the server has nothing newer.
  - Every proxy can be **turned off** without deleting it: its hosts then connect directly (with the kill switch on — through the kill-switch proxy, never directly).
  - Next to "Server sees you as" the terminal shows how the session goes: *via proxy*, *via jump host* or *direct*.
- **IP in the server logs**: `$SSH_CONNECTION` is read and the address that ends up in `auth.log` is shown.
- **Kill switch**:
  - direct connections are forbidden;
  - the proxy is checked every **3 s** (configurable), the exit IP every minute;
  - if the proxy dies, sessions are closed within those 3 s; if it starts exiting from your real IP, they are closed at the next exit-IP check — **up to a minute later**;
  - the program never falls back to a direct connection.
- **Firewall lock** (Windows): a Windows Defender Firewall rule lets ShellWarden.exe connect only to the proxy and localhost, so a mistake in the program itself cannot send SSH traffic past the proxy. Limits: the rule is for ShellWarden.exe only — `tor.exe` and `cloudflared.exe` are separate programs, and when a proxy is given by **name**, Windows' DNS service (not ShellWarden) looks that name up. For the strictest setup give the proxy as an IP address.
- **Activity log**: connections, route, the IP the server saw, vault / key / host changes, kill-switch events. No passwords or keys. Filters, search, CSV export. **Encrypted on disk**: every entry is a sealed box for the journal's public key, so it is written even while the vault is locked (e.g. a failed unlock attempt) but can only be read with the vault open; nothing about where you connected is on disk in the clear. Kept 30 days by default (7 / 30 / 90 / 365, Settings), or turned off. A plain journal of an older version is encrypted and removed on the first unlock.
- **Data folder is private**: on Linux the folder is `0700` and its files `0600`; on Windows it gets its own access list (you, SYSTEM, Administrators) — the installer sets it, and the program checks it at every start.

**Other**
- **Updates**: from GitHub Releases with a mandatory SHA-256 check (`SHA256SUMS`). Only updates signed with the project's minisign key (`SHA256SUMS.minisig`, key in `minisign.pub`) are installed. The version in the signed comment, in the release tag and in the file name must agree and be **newer** than the installed one, so an old signed release cannot be pushed as an "update" (no downgrade).
- **Libraries**: libssh ≥ 0.10.6 (Terrapin, CVE-2023-48795), libsodium ≥ 1.0.18 and OpenSSL ≥ 3.0 are required at build time and checked again at start (a swapped older DLL is refused).
- **Languages**: English, Українська, Русский, Français, Español. Switch without restarting.
- **Portable mode**: all data next to the .exe.

## Threat model

What ShellWarden protects against, and what it does not.

**Protects**
- **A stolen or copied disk / backup**: the vault, its backups and the journal are encrypted (Argon2id + XChaCha20-Poly1305; sealed boxes for the journal); `known_hosts` lists no host names. Without the master password they say nothing about your servers.
- **Another user account on the same computer**: the data folder is closed to them (Linux `0700`/`0600`, Windows access list).
- **The network between you and the server** (ISP, Wi-Fi, a censor): SSH itself, plus proxies, Tor or Shadowsocks so it does not see where you connect; the kill switch never connects directly.
- **A hostile or compromised server, or the owner of an HTTP proxy** (and anyone between you and that proxy — its connection is not encrypted): it cannot write outside the folder you download into, cannot make the program load anything from the network or a Windows share by sending text, and a changed host key is shown in red.
- **Pasted text** from web pages: control characters are removed and multi-line text is shown first.
- **A forged update**: only releases signed with the project key, with a matching and newer version, are installed.

**Does not protect**
- **Malware on your own computer, or someone with administrator rights on it**: they can read the memory of the running program (while the vault is open, its contents are decrypted there) or record your keystrokes. No local program can defend against that.
- **A server you log in to** sees what you type there; with agent forwarding, root on that server can use your agent while you are connected.
- **Who runs your proxy / Shadowsocks / Tor exit** sees where you connect (but not inside SSH).
- **A weak master password**: Argon2id makes guessing slow, not impossible.
- **Open sessions after the vault locks**: they keep running and keep the secrets they logged in with in memory until their tab is closed.

## Tests

`cmake -DSHELLWARDEN_BUILD_TESTS=ON`, then:
- `tests/unit_tests` — no network: vault (wrong password, damaged file, forged KDF settings, a failed password change), protected secrets, the encrypted journal, file names from servers, pasting, command history filters, hashed known_hosts, minisign and the update checks (incl. downgrade), Shadowsocks key derivation, BLAKE3 and ss:// links, text from proxies and servers, stopping background threads on exit;
- `tests/qml_safety_tests` — offscreen: the interface loads nothing from the network or a Windows share, not even from rich text, images or tooltips; `tests/check_qml_text.py` checks that every text in `qml/` is plain text;
- `tests/integration_tests` — against real OpenSSH, ssh-keygen, puttygen, ssh-agent, SOCKS5 / HTTP proxies, shadowsocks-libev and shadowsocks-rust (`sudo tests/integration/setup.sh` starts them): keys, SSH, agent, proxies, host key change, .ppk import, SFTP, tunnels, jump host, all six Shadowsocks ciphers;
- `tests/fuzz/` — libFuzzer targets for everything that reads outside input (`.ppk`/OpenSSH keys, `ss://`, `~/.ssh/config`, vault file, signatures, known_hosts, journal lines, the Shadowsocks stream).

GitHub Actions (`.github/workflows/ci.yml`) runs all of them on every push — the Linux build, ASan + UBSan, fuzzing, and a Windows (MSYS2) build with the unit tests. Releases are still built and signed on the maintainer's computer.

Security problems: see [SECURITY.md](SECURITY.md). Changes: [CHANGELOG.md](CHANGELOG.md).

## Network connections and external services

ShellWarden has **no telemetry, analytics or crash reports**. Besides your own servers it talks only to the services below, and each of them for one reason.

| Service | What for | When | What it learns |
|---|---|---|---|
| **ipinfo.io** (`https://ipinfo.io/json`) | Find out which IP address and country a route exits from. | Before each SSH connection (through the same route as the connection: through the proxy, or directly); when you press *Check* on a proxy or on "Your real IP"; with the kill switch on — every minute for each proxy in use, and once when the kill switch is turned on. | The IP the request comes from (the proxy's exit or yours) and the User-Agent `ShellWarden-SSH-Client`. Not which server you connect to. |
| **api.ipify.org** (`https://api.ipify.org?format=json`) | The same, as a fallback. | Only when ipinfo.io did not answer. | The same as above. |
| **dns.google** (`https://dns.google/resolve`, DNS over HTTPS) | Look up SSHFP records for a server's host key. | Only if *Check new host keys in DNS (SSHFP)* is on in Settings, and only when a host key is new or has changed. Goes through the host's proxy. | The server's host name. |
| **api.github.com** and GitHub release downloads | Check for and download updates of ShellWarden (`coldtrace00/ShellWarden`). | 4 s after start if *Check for updates on start* is on (default), and when you press *Check now*. A download is verified with SHA-256 and the minisign signature before anything is installed. | Your IP and that you use ShellWarden. Turn the automatic check off in Settings if you do not want this. |

Through a **Shadowsocks** proxy the IP check goes over plain HTTP (port 80): from your computer to the Shadowsocks server it is encrypted by Shadowsocks; only the short hop from that server to the IP service is unencrypted, and it carries nothing but the request "what is my IP".

**Other connections, all to things you set up yourself:**
- your servers (SSH, Telnet), the proxies you added, jump hosts;
- the proxy check (with the kill switch on, every 3 s): only a TCP connection to the proxy itself, no request to the internet;
- the online dot on host cards (off by default): one short connection to each of your hosts every 5 minutes, without logging in;
- **Tor**: the `tor` program started by ShellWarden or your Tor Browser on 127.0.0.1 — Tor itself connects to the Tor network;
- **Cloudflare Tunnel**: the `cloudflared` program started by ShellWarden connects to Cloudflare (and may open your browser for Cloudflare Access login).

Links in the program (Tor download page, Cloudflare docs, this repository) open in your browser only when you click them.

If ipinfo.io and api.ipify.org are both unreachable (blocked by your network or by the proxy), the check shows both errors, e.g. `no IP service answered (ipinfo.io: …; api.ipify.org: …)`. With the kill switch off the connection still goes ahead; with it on, a connection whose route cannot be checked is blocked.

## Technology stack

| Layer | What is used |
|---|---|
| Language | **C++17** (logic, networking, crypto) and **QML** (UI) |
| Framework | **Qt 6.4+** (CI builds and tests on 6.4.2 and 6.12; releases are built with 6.12): Core, Gui, Quick, Quick Controls 2 (Basic style), Network, LinguistTools; when available — SerialPort (COM ports) and DBus (Linux: lock on sleep / screen lock) |
| SSH | **libssh** 0.10 / 0.11: sessions, SFTP, L/R/D tunnels, SSH agent and agent forwarding |
| Cryptography | **libsodium**: Argon2id, XChaCha20-Poly1305, BLAKE2b, Ed25519 (minisign signature check); **OpenSSL libcrypto** — only for PuTTY `.ppk` import |
| Terminal | **libvterm** (xterm-256color, true colour, Unicode); rendered by a custom `QQuickItem` |
| Code highlighting | custom `QSyntaxHighlighter` (Shell, Python, YAML, JSON, INI, nginx, Dockerfile, JS, C/C++, SQL, XML, Markdown) |
| Networking | SOCKS5 / HTTP CONNECT / Shadowsocks (own AEAD implementation: libsodium + OpenSSL) / Tor (`tor`) / Cloudflare Tunnel (`cloudflared`); IP check via ipinfo.io / ipify |
| Windows APIs | Winsock, DWM (dark title bar), WTS (session lock), Windows Firewall via `netsh`, named pipe / WM_COPYDATA (OpenSSH agent, Pageant) |
| Build | **CMake 3.21+**, **Ninja**; Windows — MinGW-w64 from the Qt kit, libraries from **MSYS2 UCRT64**, `windeployqt`; Linux — GCC/Clang and distribution packages |
| Packaging | **Inno Setup 6/7** (`setup.exe`), portable `.zip`, CPack (`.deb`, `.tar.gz`), **AppImage** (linuxdeploy + Qt plugin) |
| Releases | GitHub Releases, `SHA256SUMS` signed with **minisign**; optional Authenticode (`signtool`) |
| Translations | Qt Linguist (`.ts` → `.qm`) |
| Assets | flags — [flag-icons](https://github.com/lipis/flag-icons) (MIT), country names — [i18n-iso-countries](https://github.com/michaelwittig/node-i18n-iso-countries) (MIT) |

### How it fits together
- The **UI thread** (QML + `AppController`) never blocks on the network: every SSH session has its own `SshWorker` in a separate thread, which also runs SFTP, tunnels, the agent and the server monitor (one persistent channel).
- The **route** is a chain: `ProxyTunnel` (SOCKS5/HTTP/Shadowsocks/Tor/Cloudflare) → `JumpBridge` (jump host) → target server. The kill switch and `FirewallLock` make sure nothing goes out directly.
- The **vault** (`Vault`) is a single encrypted file; secrets in memory live in `SecureBytes` and are wiped after use.
- **Updates** (`UpdateChecker`): GitHub Releases API → pick the package for this system → verify `SHA256SUMS` and its minisign signature → run the installer.

## Build and run

### Windows: one-time prerequisites
- **Qt 6.4 or newer** (releases are built with 6.12) from the [official installer](https://www.qt.io/download-qt-installer-oss), components: *MinGW 64-bit*, and under *Developer and Designer Tools* — *MinGW*, *CMake*, *Ninja*.
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
- `release\` — both files plus `SHA256SUMS` and the signature `SHA256SUMS.minisig`, ready to upload to a GitHub release.

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
   - `SHA256SUMS` — checksums (without it the program will not install the update);
   - `SHA256SUMS.minisig` — the signature (see *Release signing* below; `build.bat package` asks for the key password and makes it).

   Put the Linux build (`./build.sh appimage`) into the same folder and sign again: `tools/sign_release.sh <folder> 0.3.1` (it recomputes `SHA256SUMS` for all files).
3. On GitHub: **Releases → Draft a new release → Choose a tag** → `v0.3.1` → *Create new tag*, drag in all files from `dist\release\`, click **Publish release**. Drafts and pre-releases are not seen by the program.

   The release description is what users see in the update window (and once more as *What's new* after the update). Write it once per language; the program shows the part in its own language, English if there is none:
   ```
   <!-- lang:en -->
   ### What's new
   - Faster connect

   <!-- lang:uk -->
   ### Що нового
   - Швидше підключення
   ```
   Codes: `en`, `uk`, `ru`, `fr`, `es`. Notes without these markers are shown as they are.
4. At start the program shows a window with the changes and **Update now** / **Later** / **Skip this version**. Or by hand: *Settings → Updates →* **Check now** → **Download** → **Install**. The installer silently installs the new version into the same folder; `data\` (vault, keys, settings) is not touched. The portable version opens the folder with the verified zip — extract it over the old one.

The check also runs automatically 4 s after start (can be turned off in the same place).

### Release signing
Every official release is signed with [minisign](https://jedisct1.github.io/minisign/). The public key is in [`minisign.pub`](minisign.pub) and is built into the program:

```
RWTHLqknA+zq3S01ZgYde9o4CH9nluIpmb4DfczkFneGo8f3I25KR5Mi
```

The program installs an update only if `SHA256SUMS.minisig` is a valid signature of `SHA256SUMS` made with this key, and the downloaded file matches its checksum. A fake file put into a release (for example, after a stolen GitHub account) is refused.

Verify a release yourself:
```
minisign -Vm SHA256SUMS -P RWTHLqknA+zq3S01ZgYde9o4CH9nluIpmb4DfczkFneGo8f3I25KR5Mi
sha256sum -c SHA256SUMS        # Windows: certutil -hashfile <file> SHA256
```

For maintainers:
- `build.bat package` signs `dist\release\` itself (minisign asks for the key password). The secret key is looked up in `%MINISIGN_KEY%`, `Desktop\minisign-keys\minisign.key` and `%USERPROFILE%\.minisign\minisign.key`.
- To sign by hand: `powershell -ExecutionPolicy Bypass -File tools\sign_release.ps1 -Folder dist\release -Version 0.3.1` (Linux: `tools/sign_release.sh dist/release 0.3.1`). Add `-CertThumbprint` for Authenticode.
- Forks: put your own key into `minisign.pub` (`minisign -G -p minisign.pub -s minisign.key`), or build with `-DAPP_UPDATE_PUBKEY=none` to disable the check.

## Where data is stored

| Mode | Folder |
|---|---|
| Installed with `setup.exe` | `<install folder>\data\` |
| Portable (zip) | `data\` next to the .exe — an empty file named `portable` next to the program is enough |
| Run from `build\`, .deb, AppImage | `%APPDATA%\ShellWarden\` (Linux: `~/.local/share/ShellWarden/`) |

The folder contains:
- `vault.swv` — the vault (hosts, passwords, keys, notes — all encrypted), with `vault.swv.bak` next to it;
- `backups/` — daily copies;
- `known_hosts` — host names hashed;
- `settings.ini`;
- `logs/` — activity log, encrypted (`journal.pub` is its public key; the secret key is in the vault);
- `backgrounds/` — pictures of your own themes (if any).

The installer asks for a folder, and that is where everything is kept: `<folder>\data` (writable for the user who installed, closed to other accounts on the computer, and kept when the program is uninstalled). A new install starts from scratch: nothing is taken from `%APPDATA%` (move data with *Save backup* / *Restore*). Updates are installed by the same installer.

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
| Ctrl+Shift+H | command history of this host |
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
