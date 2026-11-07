# ShellWarden — SSH-клієнт для Windows 10/11 і Linux

[English](README.md) · **Українська**

Відкритий SSH-клієнт на Qt 6 / QML: можливості PuTTY, інтерфейс у стилі Termius, акцент на безпеці.

## Можливості

**Підключення**
- **Хости**: картки з кольором і аватаром, групи, пошук, швидке підключення `user@host:port`.
- **Вкладки**:
  - перетягування;
  - статус і колір хоста;
  - значки проксі, jump і тунелів;
  - контекстне меню: дублювати, в окреме вікно, розділити, закрити.
- **Розділений екран** (Ctrl+Shift+D): дві сесії поруч.
- **Окреме вікно**: будь-яку сесію можна винести в окреме вікно і повернути назад.
- **Права кнопка на хості**: *Підключитися через* (маршрут лише для цієї вкладки) і *Маршрут* (зберігається в хості).
- **Jump host (ProxyJump)**: маршрут проксі → jump → хост.
- **Тунелі** L / R / D (SOCKS5): задаються в хості або відкриваються на льоту. Панель з анімованою схемою потоку і лічильниками трафіку.
- **SFTP-панель** (працює прямо під час SSH-сесії, через те саме з'єднання):
  - навігація; адресний рядок з доповненням назв через **Tab**;
  - права доступу, власник, точний розмір вибраного файлу;
  - завантаження і вивантаження, drag & drop;
  - перейменування, видалення, нова тека;
  - черга передавання зі скасуванням;
  - **редагування файлу прямо тут** (до 5 МБ, Ctrl+S, права файлу зберігаються) з підсвічуванням синтаксису і показом рядка/стовпця.
- **Serial (COM)**: швидкість, біти, парність, керування потоком.
- **Сніпети**: збережені команди та **скрипти .sh** (імпорт з файлу), нові — зверху. Зберігаються зашифрованими у сховищі. Скрипт надсилається одним блоком і виконується інтерпретатором з рядка `#!`. Відправка в поточну сесію або в усі одразу.
- **Монітор сервера** під терміналом: CPU, RAM, диск, uptime, мережа (один постійний канал, без додаткових входів).

**Термінал**
- libvterm: xterm-256color, true color, Unicode, alt-screen (mc, htop, vim), миша, bracketed paste.
- 39 колірних тем: **Midnight** (за замовчуванням: глибокий темно-синій, спокійні кольори), ShellWarden, Deep Navy, Classic Dark, Graphite, Blue Team, Dracula, Catppuccin, Rosé Pine, Kanagawa, Tokyo Night, Nord, Ayu, Night Owl, Synthwave '84, GitHub, Solarized… І власні теми.
- **Підсвічування виводу**: помилки, попередження, OK, IP, URL, шляхи, дати — лише у звичайному тексті, повноекранні програми (vim, mc, htop) не змінюються.
- Сигнал терміналу (Backspace у порожньому рядку тощо): нічого / блимання / системний звук.
- Вибір моноширинного шрифту; за замовчуванням лишається поточний.
- Як у PuTTY: виділення копіює, права кнопка вставляє.

**Ключі (Keychain)**
- Генерація Ed25519 / ECDSA / RSA під паролем.
- Імпорт OpenSSH, PEM і **PuTTY .ppk v2/v3**.
- У редакторі хоста ключ вибирається зі списку, звідти ж можна створити новий або імпортувати.
- `.pub` зберігається **одним рядком OpenSSH** (`ssh-ed25519 AAAA… comment`). RFC 4716 — лише окремим експортом.
- Приватний ключ експортується в OpenSSH-форматі з правами лише для поточного користувача.
- **На сервери**: публічні ключі додаються в `~/.ssh/authorized_keys` на багатьох серверах одразу — один ключ для всіх або свій для кожного; після цього хост може входити ключем.
- **SSH-агент**: Pageant, служба Windows «OpenSSH Authentication Agent», ssh-agent, KeePassXC, YubiKey. Вхід ключами агента і **agent forwarding** (`ssh -A`) — вмикаються в хості.

**Безпека**
- **Сховище**: Argon2id (256 МіБ, 3 проходи) → XChaCha20-Poly1305. Заголовок автентифікований, щоденні резервні копії (14 днів).
- **Генератор паролів**: від 19 символів (довжина — повзунком), з оцінкою стійкості. Шкала довжини з позначками 8 і 19. Ручний пароль — від 8 символів, з попередженням і кнопкою «Я приймаю ризик».
- **Автоблокування**: через задану кількість хвилин або годин, при блокуванні Windows / сні, або «ніколи не блокувати».
- **Майстер-пароль для підключення** (розділ «Розширений захист» хоста): питається навіть при розблокованому сховищі; 5 невдалих спроб → блокування на 30 с. Для справжньої 2FA налаштуйте її на самому сервері (PAM + Google Authenticator) — ShellWarden покаже запит коду сервера.
- **Known hosts**: перевірка відбитка, червоне попередження при зміні ключа; за бажанням перевірка SSHFP (DNS).
- **Нотатки до хоста**: зашифровані у сховищі, як паролі.
- **Маршрути**: SOCKS5 / HTTP CONNECT / Tor / **Cloudflare Tunnel** (ShellWarden сам запускає `cloudflared access tcp`; порт 22 на сервері можна закрити). DNS резолвить проксі. Перевірка показує затримку, вихідний IP і країну.
- **IP у логах сервера**: читається `$SSH_CONNECTION` і показується адреса, яка потрапить в `auth.log`.
- **Kill switch**:
  - прямі з'єднання заборонені;
  - проксі перевіряється кожні **3 с** (інтервал налаштовується), вихідний IP — щохвилини;
  - якщо проксі впав або почав виходити з реального IP, сесії закриваються одразу;
  - на пряме з'єднання програма не перемикається ніколи.
- **Блокування брандмауером** (Windows): правило Windows Defender Firewall дозволяє ShellWarden.exe з'єднання лише з проксі і localhost. Навіть помилка в програмі не розкриє реальний IP.
- **Журнал подій**: підключення, маршрут, IP, який бачив сервер, зміни сховища, ключів і хостів, спрацювання kill switch. Без паролів і ключів. Фільтри, пошук, експорт CSV, зберігається 12 місяців.

**Інше**
- **Оновлення**: через GitHub Releases з обов'язковою перевіркою SHA-256 (файл `SHA256SUMS`). Ставляться лише оновлення, підписані ключем проєкту minisign (`SHA256SUMS.minisig`, ключ — у `minisign.pub`).
- **Мови**: English, Українська, Русский, Français, Español. Перемикаються без перезапуску.
- **Portable-режим**: усі дані поруч з .exe.

## Стек технологій

| Шар | Що використовується |
|---|---|
| Мова | **C++17** (логіка, мережа, криптографія) і **QML** (інтерфейс) |
| Фреймворк | **Qt 6.5+** (перевірено на 6.12): Core, Gui, Quick, Quick Controls 2 (стиль Basic), Network, LinguistTools; за наявності — SerialPort (COM-порти) і DBus (Linux: блокування при сні / блокуванні екрана) |
| SSH | **libssh** 0.10 / 0.11: сесії, SFTP, тунелі L/R/D, SSH-агент і agent forwarding |
| Криптографія | **libsodium**: Argon2id, XChaCha20-Poly1305, BLAKE2b, Ed25519 (перевірка підпису minisign); **OpenSSL libcrypto** — лише для імпорту PuTTY `.ppk` |
| Термінал | **libvterm** (xterm-256color, true color, Unicode); рендер власним `QQuickItem` |
| Підсвічування коду | власний `QSyntaxHighlighter` (Shell, Python, YAML, JSON, INI, nginx, Dockerfile, JS, C/C++, SQL, XML, Markdown) |
| Мережа | SOCKS5 / HTTP CONNECT / Tor (`tor`) / Cloudflare Tunnel (`cloudflared`); перевірка IP — ipinfo.io / ipify |
| Windows API | Winsock, DWM (темний заголовок вікна), WTS (блокування сесії), Windows Firewall через `netsh`, named pipe / WM_COPYDATA (OpenSSH-агент, Pageant) |
| Збірка | **CMake 3.21+**, **Ninja**; Windows — MinGW-w64 з Qt, бібліотеки з **MSYS2 UCRT64**, `windeployqt`; Linux — GCC/Clang і пакети дистрибутива |
| Пакування | **Inno Setup 6/7** (`setup.exe`), portable `.zip`, CPack (`.deb`, `.tar.gz`), **AppImage** (linuxdeploy + Qt-плагін) |
| Релізи | GitHub Releases, `SHA256SUMS` з підписом **minisign**; за бажанням Authenticode (`signtool`) |
| Переклади | Qt Linguist (`.ts` → `.qm`): English, Українська, Русский, Français, Español |
| Ресурси | прапори — [flag-icons](https://github.com/lipis/flag-icons) (MIT), назви країн — [i18n-iso-countries](https://github.com/michaelwittig/node-i18n-iso-countries) (MIT) |

### Як це влаштовано
- **UI-потік** (QML + `AppController`) ніколи не блокується мережею: кожна SSH-сесія має власний `SshWorker` в окремому потоці; звідти ж ідуть SFTP, тунелі, агент і монітор сервера (один постійний канал).
- **Маршрут** будується ланцюжком: `ProxyTunnel` (SOCKS5/HTTP/Tor/Cloudflare) → `JumpBridge` (jump-хост) → цільовий сервер. Kill switch і `FirewallLock` стежать, щоб з'єднання не пішло напряму.
- **Сховище** (`Vault`) — один зашифрований файл; секрети в пам'яті тримаються в `SecureBytes` і затираються після використання.
- **Оновлення** (`UpdateChecker`) — GitHub Releases API → вибір файлу під систему → перевірка `SHA256SUMS` і її підпису minisign → запуск інсталятора.

## Збірка і запуск

### Windows: що потрібно один раз
- **Qt 6.5 або новіший** (перевірено на 6.12) з [офіційного інсталятора](https://www.qt.io/download-qt-installer-oss), компоненти: *MinGW 64-bit*, а в *Developer and Designer Tools* — *MinGW*, *CMake*, *Ninja*.
- **MSYS2** у `C:\msys64` ([msys2.org](https://www.msys2.org)) — звідти беруться libssh, libsodium, libvterm. Ставити їх вручну не треба, це зробить `build.bat`.
- За бажанням **Inno Setup 6 або 7** ([jrsoftware.org](https://jrsoftware.org/isdl.php)) — для інсталятора `setup.exe`.

```
git clone https://github.com/coldtrace00/ShellWarden.git
cd ShellWarden
```

### Windows — один клік
Двічі клацніть **`build.bat`** у теці проєкту (або запустіть його з командного рядка).
Скрипт:
1. знаходить Qt (`C:\Qt\6.*\mingw_64`), MinGW, CMake, Ninja і MSYS2 (`C:\msys64`);
2. ставить у MSYS2 libssh, libsodium і libvterm (лише те, чого ще немає);
3. збирає програму в `build\` і запускає її.

Скрипт лише читає `C:\Qt` і нічого там не змінює.

**Інсталятор і portable-версія:**
```
build.bat package
```
У теці `dist\` з'являться:
- `ShellWarden-<версія>-portable.zip`;
- `ShellWarden-<версія>-setup.exe` — лише якщо встановлено [Inno Setup 6 або 7](https://jrsoftware.org/isdl.php). Під час встановлення питає теку, і всі дані потім лежать саме там, у `data\`;
- `release\` — обидва файли плюс `SHA256SUMS` і підпис `SHA256SUMS.minisig`, готові для завантаження в реліз на GitHub.

Повний лог збірки — `build\build.log`.

### Linux
```
./build.sh            # збірка і запуск (за потреби запропонує встановити залежності)
./build.sh package    # .deb і .tar.gz у build-linux/
./build.sh appimage   # один файл ShellWarden-<версія>-x86_64.AppImage для будь-якого дистрибутива
```
Скрипт підтримує Ubuntu/Debian, Fedora і Arch.

## Релізи та кнопка «Оновити» в програмі

Програма перевіряє **останній реліз** репозиторію `coldtrace00/ShellWarden` (вбудовано під час збірки, змінюється в *Налаштування → Оновлення*). Щоб оновлення спрацювало:

1. Збільшіть версію в `CMakeLists.txt`: `project(ShellWarden VERSION 0.3.1 …)` — вона має бути **більшою**, ніж у встановленої програми.
2. Зберіть: `build.bat package`. У `dist\release\` будуть рівно ті файли, що треба завантажити:
   - `ShellWarden-0.3.1-setup.exe` — для встановленої версії;
   - `ShellWarden-0.3.1-portable.zip` — для portable;
   - `SHA256SUMS` — контрольні суми (без нього програма оновлення не поставить);
   - `SHA256SUMS.minisig` — підпис (див. *Підпис релізів* нижче; `build.bat package` питає пароль ключа і робить його сам).
   
   Linux-збірку (`./build.sh appimage`) покладіть у ту саму теку і підпишіть ще раз: `tools/sign_release.sh <тека> 0.3.1` (він перераховує `SHA256SUMS` для всіх файлів).
3. На GitHub: **Releases → Draft a new release → Choose a tag** → `v0.3.1` → *Create new tag*, перетягніть усі файли з `dist\release\`, натисніть **Publish release**. Чернетки (*draft*) і *pre-release* програма не бачить.
4. У програмі: *Налаштування → Оновлення →* **Перевірити зараз** → **Завантажити** → **Встановити**. Інсталятор тихо ставить нову версію в ту саму теку; `data\` (сховище, ключі, налаштування) не чіпається. Portable-версія відкриває теку з перевіреним zip — розпакуйте його поверх старої.

Перевірка також іде автоматично через 4 с після запуску (вимикається там же).

### Підпис релізів
Кожен офіційний реліз підписано [minisign](https://jedisct1.github.io/minisign/). Публічний ключ лежить у [`minisign.pub`](minisign.pub) і вшитий у програму:

```
RWTHLqknA+zq3S01ZgYde9o4CH9nluIpmb4DfczkFneGo8f3I25KR5Mi
```

Програма ставить оновлення, лише якщо `SHA256SUMS.minisig` — правильний підпис `SHA256SUMS` саме цим ключем, а завантажений файл збігається з контрольною сумою. Підкинутий у реліз файл (наприклад, після крадіжки акаунта GitHub) буде відхилено.

Перевірити реліз самостійно:
```
minisign -Vm SHA256SUMS -P RWTHLqknA+zq3S01ZgYde9o4CH9nluIpmb4DfczkFneGo8f3I25KR5Mi
sha256sum -c SHA256SUMS        # Windows: certutil -hashfile <файл> SHA256
```

Для автора:
- `build.bat package` сам підписує `dist\release\` (minisign питає пароль ключа). Секретний ключ шукається в `%MINISIGN_KEY%`, `Desktop\minisign-keys\minisign.key` і `%USERPROFILE%\.minisign\minisign.key`.
- Підписати вручну: `powershell -ExecutionPolicy Bypass -File tools\sign_release.ps1 -Folder dist\release -Version 0.3.1` (Linux: `tools/sign_release.sh dist/release 0.3.1`). Для Authenticode додайте `-CertThumbprint`.
- Форки: покладіть свій ключ у `minisign.pub` (`minisign -G -p minisign.pub -s minisign.key`) або збирайте з `-DAPP_UPDATE_PUBKEY=none`, щоб вимкнути перевірку.

## Де зберігаються дані

| Режим | Тека |
|---|---|
| Встановлений через `setup.exe` | `<тека встановлення>\data\` |
| Portable (zip) | `data\` поруч з .exe — достатньо порожнього файлу `portable` біля програми |
| Запуск з `build\`, .deb, AppImage | `%APPDATA%\ShellWarden\` (Linux: `~/.local/share/ShellWarden/`) |

У теці лежать:
- `vault.swv` — сховище (хости, паролі, ключі, нотатки — усе зашифроване), поруч `vault.swv.bak`;
- `backups/` — щоденні копії;
- `known_hosts`;
- `settings.ini`;
- `logs/` — журнал подій;
- `backgrounds/` — картинки власних тем (якщо їх додано).

Інсталятор (`build.bat package` з Inno Setup 6/7) питає теку, і саме там усе й зберігається: `<тека>\data` (теці даються права на запис, при видаленні програми вона лишається). Нова установка починається з чистого аркуша: нічого з `%APPDATA%` не підтягується (перенести дані можна через «Зберегти резервну копію» / «Відновити»). Оновлення ставляться тим самим інсталятором.

Для встановленої і portable-версії там же: `keys/` (сюди за замовчуванням зберігаються експортовані ключі), `updates/` (завантажені оновлення), `cache/`, `tor/` (якщо tor.exe запускає ShellWarden). У реєстр, `%APPDATA%` і теки користувача програма тоді нічого не пише; portable-архів (`build.bat package`) уже містить файл `portable`.

**У репозиторій нічого з цього не потрапляє**: `.gitignore` виключає `data/`, `*.swv`, `*.key`, `build*/` і `dist/`.

### Незалежна перевірка шифрування
```
pip install argon2-cffi cryptography
python tools\verify_vault.py "%APPDATA%\ShellWarden\vault.swv"
```
Скрипт розшифровує сховище еталонними бібліотеками, без коду ShellWarden і libsodium:
- Argon2id — argon2-cffi;
- XChaCha20-Poly1305 — `cryptography` (OpenSSL) і HChaCha20 з тестовим вектором.

Неправильний пароль або змінений хоч один байт дає FAILED.

## Гарячі клавіші

| | |
|---|---|
| Ctrl+Shift+C / Ctrl+Shift+V, Shift+Insert | копіювати / вставити |
| Shift+PgUp / PgDn | прокрутка історії |
| Ctrl + колесо, Ctrl +/- | розмір шрифту |
| Ctrl+Tab, Ctrl+Shift+Tab | перемикання вкладок |
| Ctrl+Shift+W | закрити вкладку |
| Ctrl+Shift+D | розділений екран |
| Ctrl+Shift+B | сховати / показати бічне меню |
| Ctrl+Shift+T | хости |
| Ctrl+Shift+L | заблокувати сховище |

Звичайні Ctrl+C, Ctrl+W, Ctrl+R, Tab тощо завжди йдуть на сервер.

## Структура

```
src/core        Vault, KeyTools (+.ppk), KnownHosts, SshWorker (потік), SFTP, тунелі,
                JumpBridge, ProxyTunnel, FirewallLock, AgentClient, Minisign, ActivityLog, Paths
src/terminal    TerminalEmulator (libvterm, теми), TerminalItem (рендер у Qt Quick)
src/app         AppController, моделі, TerminalSession, ProxyChecker, UpdateChecker,
                CloudflaredManager, TorManager, SyntaxHighlighter, DarkTitleBar
qml             інтерфейс
i18n            переклади (*.ts) і translations.py
branding        логотип, іконки .png / .ico
packaging       Inno Setup (Windows), .desktop (Linux)
tools           verify_vault.py, sign_release.sh / .ps1 (підпис релізів)
```

Ліцензія: MIT.
