// Integration tests: the core layer against real OpenSSH, proxies and
// Shadowsocks servers on this machine. tests/integration/setup.sh prepares
// them (Linux, needs root): user "tester" (password Secret-Pass-123), sshd on
// 127.0.0.1:2222, SOCKS5 on 1080 (puser/ppass), HTTP CONNECT on 8888,
// shadowsocks-libev on 8388-8390, shadowsocks-rust (2022) on 8391-8394,
// a web server with big.bin on 8766. ssh-keygen, ssh-agent and puttygen are
// used for the key checks. Run:  sudo tests/integration/setup.sh && ./integration_tests
#include "core/KeyTools.h"
#include "core/KnownHosts.h"
#include "core/ProxyTunnel.h"
#include "core/SshWorker.h"
#include "core/AgentClient.h"
#include "core/Vault.h"
#include "core/PasswordTools.h"
#include "core/FirewallLock.h"
#include "core/Shadowsocks.h"
#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QCryptographicHash>
#include <functional>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <libssh/libssh.h>
#include <sodium.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("  ok   %s\n", msg); else { std::printf("  FAIL %s\n", msg); ++g_fail; } } while (0)

static QString run(const QString &prog, const QStringList &args, int *code = nullptr)
{
    QProcess p;
    p.start(prog, args);
    p.waitForFinished(30000);
    if (code) *code = p.exitCode();
    return QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError()).trimmed();
}

struct Outcome
{
    QList<int> hostKeyPrompts;
    QList<int> secretPrompts;
    bool connected = false;
    QString seenIp;
    QByteArray output;
    QString finishReason;
    bool finishError = false;
};

// Drives one SshWorker. answers: host key → accept; secrets → from queue.
static Outcome connectOnce(const ConnectParams &p, const QByteArray &commands,
                           QStringList secrets = {}, bool acceptHostKey = true)
{
    Outcome o;
    QThread th;
    auto *w = new SshWorker;
    w->moveToThread(&th);
    QObject::connect(&th, &QThread::finished, w, &QObject::deleteLater);
    QEventLoop loop;
    QObject ctx;

    QObject::connect(w, &SshWorker::hostKeyCheck, &ctx, [&](int st, const QString &, const QString &) {
        o.hostKeyPrompts << st;
        QMetaObject::invokeMethod(w, [w, acceptHostKey] { w->answerHostKey(acceptHostKey); }, Qt::QueuedConnection);
    });
    QObject::connect(w, &SshWorker::secretRequired, &ctx, [&](int kind, const QString &, bool) {
        o.secretPrompts << kind;
        const QString s = secrets.isEmpty() ? QString() : secrets.takeFirst();
        const bool cancel = s.isNull();
        QMetaObject::invokeMethod(w, [w, s, cancel] { w->answerSecret(s, cancel); }, Qt::QueuedConnection);
    });
    QObject::connect(w, &SshWorker::connected, &ctx, [&] {
        o.connected = true;
        QTimer::singleShot(300, &ctx, [&, w] {
            QMetaObject::invokeMethod(w, [w, commands] { w->write(commands); }, Qt::QueuedConnection);
        });
    });
    QObject::connect(w, &SshWorker::clientAddressSeen, &ctx, [&](const QString &ip, const QString &, const QString &) { o.seenIp = ip; });
    QObject::connect(w, &SshWorker::dataReceived, &ctx, [&](const QByteArray &d) { o.output += d; });
    QObject::connect(w, &SshWorker::finished, &ctx, [&](const QString &r, bool e) {
        o.finishReason = r; o.finishError = e; loop.quit();
    });
    QTimer::singleShot(30000, &loop, [&] { o.finishReason = "TIMEOUT"; loop.quit(); });

    th.start();
    QMetaObject::invokeMethod(w, [w, p] { w->start(p); }, Qt::QueuedConnection);
    loop.exec();
    th.quit();
    th.wait();
    return o;
}

// Connects, runs `script` once the shell is up, waits until script calls done().
struct Live
{
    SshWorker *w = nullptr;
    QEventLoop *loop = nullptr;
    QStringList log;
    QVariantList listed;
    QString listedPath, lastError;
    QVariantList forwards;
    QHash<int, int> transferState;
    bool connected = false;
    QString finishReason;
};
static void runLive(const ConnectParams &p, std::function<void(Live &)> script, int timeoutMs = 30000)
{
    Live L;
    QThread th;
    auto *w = new SshWorker;
    w->moveToThread(&th);
    QObject::connect(&th, &QThread::finished, w, &QObject::deleteLater);
    QEventLoop loop;
    QObject ctx;
    L.w = w;
    L.loop = &loop;
    QObject::connect(w, &SshWorker::hostKeyCheck, &ctx, [w](int, const QString &, const QString &) {
        QMetaObject::invokeMethod(w, [w] { w->answerHostKey(true); }, Qt::QueuedConnection);
    });
    QObject::connect(w, &SshWorker::secretRequired, &ctx, [w](int, const QString &, bool, bool) {
        QMetaObject::invokeMethod(w, [w] { w->answerSecret(QString(), true); }, Qt::QueuedConnection);
    });
    QObject::connect(w, &SshWorker::connected, &ctx, [&] { L.connected = true; script(L); });
    QObject::connect(w, &SshWorker::sftpListed, &ctx, [&](int, const QString &path, const QVariantList &e, const QString &err) {
        L.listed = e; L.listedPath = path; L.lastError = err; L.log << "listed";
    });
    QObject::connect(w, &SshWorker::sftpDone, &ctx, [&](int, const QString &err) { L.lastError = err; L.log << "done"; });
    QObject::connect(w, &SshWorker::sftpProgress, &ctx, [&](int id, qint64, qint64, int st, const QString &err) {
        L.transferState[id] = st; if (!err.isEmpty()) L.lastError = err;
    });
    QObject::connect(w, &SshWorker::forwardsUpdated, &ctx, [&](const QVariantList &f) { L.forwards = f; });
    QObject::connect(w, &SshWorker::finished, &ctx, [&](const QString &r, bool) { L.finishReason = r; loop.quit(); });
    QTimer::singleShot(timeoutMs, &loop, [&] { L.finishReason = "TIMEOUT"; loop.quit(); });
    th.start();
    QMetaObject::invokeMethod(w, [w, p] { w->start(p); }, Qt::QueuedConnection);
    loop.exec();
    th.quit();
    th.wait();
}
template<typename F> static void onW(SshWorker *w, F f) { QMetaObject::invokeMethod(w, [w, f] { f(w); }, Qt::QueuedConnection); }
static bool waitUntil(std::function<bool()> cond, int ms = 10000)
{
    QElapsedTimer t; t.start();
    while (!cond() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return cond();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    sodium_init();
    ssh_init();
    QTemporaryDir tmp;
    const QString dir = tmp.path();

    // ------------------------------------------------------------------
    std::printf("[keys]\n");
    struct { keytools::KeyType t; const char *name; } types[] = {
        {keytools::KeyType::Ed25519, "ed25519"}, {keytools::KeyType::EcdsaP256, "ecdsa256"},
        {keytools::KeyType::EcdsaP521, "ecdsa521"}, {keytools::KeyType::Rsa3072, "rsa3072"}};
    keytools::KeyMaterial edKey;
    for (auto &t : types) {
        keytools::KeyMaterial km;
        QString err;
        const bool ok = keytools::generate(t.t, "user@test box", "pa ss-фраза", &km, &err);
        CHECK(ok, qPrintable(QString("%1 generated").arg(t.name)));
        if (!ok) { std::printf("       %s\n", qPrintable(err)); continue; }
        if (t.t == keytools::KeyType::Ed25519) edKey = km;

        const QString priv = dir + "/" + t.name, pub = priv + ".pub";
        keytools::writeTextFile(priv, km.privateKey, true, &err);
        keytools::writeTextFile(pub, km.publicLine.toUtf8(), false, &err);

        QFile f(pub); f.open(QIODevice::ReadOnly);
        const QByteArray pubBytes = f.readAll();
        CHECK(pubBytes.count('\n') == 1 && pubBytes.endsWith('\n') && !pubBytes.contains('\r'),
              ".pub is exactly one LF-terminated line");
        CHECK(pubBytes.startsWith(km.type.toUtf8() + ' ') && pubBytes.trimmed().endsWith("user@test box"),
              ".pub is '<type> <base64> <comment>'");

        int code = 0;
        const QString derived = run("ssh-keygen", {"-y", "-P", "pa ss-фраза", "-f", priv}, &code);
        CHECK(code == 0 && km.publicLine.startsWith(derived.section(' ', 0, 1)),
              "ssh-keygen opens the private key with the passphrase and derives the same public key");
        run("ssh-keygen", {"-y", "-P", "wrong", "-f", priv}, &code);
        CHECK(code != 0, "ssh-keygen rejects a wrong passphrase");
        const QString fp = run("ssh-keygen", {"-l", "-f", pub});
        CHECK(fp.contains(km.fingerprint), "fingerprint matches ssh-keygen -l");
        CHECK(keytools::isEncrypted(km.privateKey), "private key is detected as encrypted");

        // authorized_keys acceptance: ssh-keygen -l on an authorized_keys file
        const QString ak = dir + "/authorized_keys_" + t.name;
        QFile::copy(pub, ak);
        run("ssh-keygen", {"-l", "-f", ak}, &code);
        CHECK(code == 0, ".pub works as an authorized_keys file");

        // RFC 4716 round trip
        const QString rfc = dir + "/" + t.name + "_ssh2.pub";
        keytools::writeTextFile(rfc, keytools::toRfc4716(km.publicLine).toUtf8(), false, &err);
        const QString back = run("ssh-keygen", {"-i", "-m", "RFC4716", "-f", rfc});
        CHECK(back.section(' ', 0, 1) == km.publicLine.section(' ', 0, 1), "RFC 4716 export converts back to the same key");

        keytools::KeyMaterial imp;
        CHECK(keytools::importPrivateKey(km.privateKey, "", "x", &imp, &err) == keytools::ImportResult::NeedPassphrase, "import asks for passphrase");
        CHECK(keytools::importPrivateKey(km.privateKey, "bad", "x", &imp, &err) == keytools::ImportResult::WrongPassphrase, "import detects wrong passphrase");
        CHECK(keytools::importPrivateKey(km.privateKey, "pa ss-фраза", "x", &imp, &err) == keytools::ImportResult::Ok
              && imp.fingerprint == km.fingerprint, "import with passphrase gives same fingerprint");
    }
    {
        // Import a key created by OpenSSH itself
        run("ssh-keygen", {"-q", "-t", "ed25519", "-N", "abc12345", "-C", "ext", "-f", dir + "/ext"});
        QFile f(dir + "/ext"); f.open(QIODevice::ReadOnly);
        keytools::KeyMaterial imp; QString err;
        const auto r = keytools::importPrivateKey(f.readAll(), "abc12345", "ext", &imp, &err);
        const QString fp = run("ssh-keygen", {"-l", "-f", dir + "/ext.pub"});
        CHECK(r == keytools::ImportResult::Ok && fp.contains(imp.fingerprint), "imports an OpenSSH-generated encrypted key");
        run("ssh-keygen", {"-q", "-t", "ed25519", "-N", "", "-f", dir + "/plain"});
        QFile f2(dir + "/plain"); f2.open(QIODevice::ReadOnly);
        CHECK(!keytools::isEncrypted(f2.readAll()), "unencrypted OpenSSH key detected as unencrypted");
    }

    // ------------------------------------------------------------------
    std::printf("[vault]\n");
    {
        const QString vp = dir + "/v/vault.swv";
        Vault v(vp); QString err;
        CHECK(!v.create("short", &err), "rejects short master password");
        CHECK(v.create("correct horse battery", &err), "creates vault");
        v.data().insert("hosts", QJsonArray{QJsonObject{{"id", "1"}, {"password", "s3cret!"}}});
        CHECK(v.save(&err), "saves vault");
        QFile raw(vp); raw.open(QIODevice::ReadOnly);
        CHECK(!raw.readAll().contains("s3cret!"), "file does not contain plaintext secrets");
        v.lock();
        Vault v2(vp);
        CHECK(!v2.unlock("wrong password!", &err) && err.contains("Wrong"), "wrong password rejected");
        CHECK(v2.unlock("correct horse battery", &err), "unlocks with right password");
        CHECK(v2.data().value("hosts").toArray().at(0).toObject().value("password").toString() == "s3cret!", "data round-trips");
        CHECK(v2.changePassword("correct horse battery", "new password 42", &err), "changes master password");
        Vault v3(vp);
        CHECK(!v3.unlock("correct horse battery", &err) && v3.unlock("new password 42", &err), "only new password works after change");
        // Tamper: flip one ciphertext byte
        QFile t(vp); t.open(QIODevice::ReadWrite); QByteArray b = t.readAll(); b[b.size() - 5] ^= 1; t.seek(0); t.write(b); t.close();
        Vault v4(vp);
        CHECK(!v4.unlock("new password 42", &err), "tampered file is rejected (AEAD)");
    }

    // ------------------------------------------------------------------
    std::printf("[ssh]\n");
    const QString kh = dir + "/known_hosts";
    ConnectParams base;
    base.host = "127.0.0.1";
    base.port = 2222;
    base.username = "tester";
    base.knownHostsPath = kh;
    base.connectTimeoutSeconds = 8;
    const QByteArray cmd = "echo MARK$((40+2)); exit\n";

    {
        ConnectParams p = base; p.password = "Secret-Pass-123";
        Outcome o = connectOnce(p, cmd);
        CHECK(o.hostKeyPrompts == QList<int>{SshWorker::HostUnknown}, "unknown host key prompts once");
        CHECK(o.connected && o.output.contains("MARK42"), "password auth + shell works");
        CHECK(o.seenIp == "127.0.0.1", "reads client IP from $SSH_CONNECTION");
        CHECK(!o.finishError, qPrintable("clean exit: " + o.finishReason));
        CHECK(QFile(kh).exists() && knownhosts::load(kh).size() >= 1, "host key saved to known_hosts");
    }
    {
        ConnectParams p = base;
        Outcome o = connectOnce(p, cmd, {"wrong-pass", "Secret-Pass-123"});
        CHECK(o.hostKeyPrompts.isEmpty(), "known host: no prompt second time");
        CHECK(o.secretPrompts == (QList<int>{SshWorker::SecretPassword, SshWorker::SecretPassword}), "asks password, re-asks after wrong one");
        CHECK(o.connected && o.output.contains("MARK42"), "connects after correct password");
    }
    {
        // Public key auth with encrypted key; passphrase asked interactively
        QDir().mkpath("/home/tester/.ssh");
        QFile ak("/home/tester/.ssh/authorized_keys");
        ak.open(QIODevice::WriteOnly); ak.write(edKey.publicLine.toUtf8() + "\n"); ak.close();
        run("chown", {"-R", "tester:tester", "/home/tester/.ssh"});
        run("chmod", {"700", "/home/tester/.ssh"});
        run("chmod", {"600", "/home/tester/.ssh/authorized_keys"});

        ConnectParams p = base; p.privateKey = edKey.privateKey;
        Outcome o = connectOnce(p, cmd, {"bad", "pa ss-фраза"});
        CHECK(o.secretPrompts == (QList<int>{SshWorker::SecretKeyPassphrase, SshWorker::SecretKeyPassphrase}), "asks key passphrase, re-asks after wrong one");
        CHECK(o.connected && o.output.contains("MARK42"), "public-key auth with encrypted key works");

        ConnectParams p2 = base; p2.privateKey = edKey.privateKey; p2.keyPassphrase = "pa ss-фраза";
        Outcome o2 = connectOnce(p2, cmd);
        CHECK(o2.secretPrompts.isEmpty() && o2.connected, "saved passphrase: no prompt at all");
    }
    {
        std::printf("[agent]\n");
        // a fresh ssh-agent holding the (decrypted) ed25519 key
        const QString sock = dir + "/agent.sock";
        QFile::remove(sock);
        run("ssh-agent", {"-a", sock});
        qputenv("SSH_AUTH_SOCK", sock.toUtf8());
        const QString kf = dir + "/agentkey";
        { QFile f(kf); f.open(QIODevice::WriteOnly); f.write(edKey.privateKey); f.close(); QFile::setPermissions(kf, QFile::ReadOwner | QFile::WriteOwner); }
        run("ssh-keygen", {"-p", "-P", "pa ss-фраза", "-N", "", "-f", kf});
        int code = -1; run("ssh-add", {kf}, &code);
        CHECK(code == 0, "test agent holds the key");
        CHECK(agent::available() == "ssh-agent" && agent::identityCount() == 1, "AgentClient sees 1 key in the agent");
        ConnectParams p = base; p.useAgent = true; p.agentForward = true;
        Outcome o = connectOnce(p, "echo AG$(ssh-add -l 2>/dev/null | grep -c ED25519)X; exit\n");
        CHECK(o.secretPrompts.isEmpty() && o.connected, "logs in with the agent's key, nothing asked");
        CHECK(o.output.contains("AG1X"), "agent forwarding: the server sees the key through ssh -A");
        ConnectParams p2 = base; p2.useAgent = true;
        Outcome o2 = connectOnce(p2, "echo AG$(ssh-add -l >/dev/null 2>&1; echo $?)X; exit\n");
        CHECK(o2.connected && o2.output.contains("AG2X"), "no forwarding unless asked");
        run("sh", {"-c", "SSH_AUTH_SOCK=" + sock + " ssh-agent -k"});
        run("pkill", {"-x", "ssh-agent"});
        qunsetenv("SSH_AUTH_SOCK");
    }
    {
        std::printf("[proxy]\n");
        ConnectParams p = base; p.password = "Secret-Pass-123";
        p.host = "localhost"; // must be resolved by the proxy
        p.proxy.type = ProxyConfig::Socks5; p.proxy.host = "127.0.0.1"; p.proxy.port = 1080;
        p.proxy.username = "puser"; p.proxy.password = "ppass";
        p.killSwitch = true;
        Outcome o = connectOnce(p, cmd);
        CHECK(o.connected && o.output.contains("MARK42"), "SOCKS5 with auth works");
        QFile log(QString::fromLocal8Bit(qgetenv("SW_TEST_SOCKS_LOG").isEmpty() ? QByteArray("/tmp/sw-test/socks.log") : qgetenv("SW_TEST_SOCKS_LOG"))); log.open(QIODevice::ReadOnly);
        CHECK(log.readAll().contains("atyp=3 host=localhost port=2222"), "SOCKS5 got the host NAME (remote DNS, no local lookup)");

        ConnectParams bad = p; bad.proxy.password = "nope";
        Outcome ob = connectOnce(bad, cmd);
        CHECK(ob.finishError && ob.finishReason.contains("rejected the username"), qPrintable("bad proxy credentials: " + ob.finishReason));

        ConnectParams h = base; h.password = "Secret-Pass-123";
        h.proxy.type = ProxyConfig::Http; h.proxy.host = "127.0.0.1"; h.proxy.port = 8888;
        Outcome oh = connectOnce(h, cmd);
        CHECK(oh.connected && oh.output.contains("MARK42"), qPrintable("HTTP CONNECT proxy works " + oh.finishReason));

        ConnectParams dead = base; dead.password = "x";
        dead.proxy.type = ProxyConfig::Socks5; dead.proxy.host = "127.0.0.1"; dead.proxy.port = 1999;
        Outcome od = connectOnce(dead, cmd);
        CHECK(od.finishError && od.finishReason.contains("NOT attempted") && !od.connected,
              qPrintable("dead proxy never falls back to direct: " + od.finishReason));

        ConnectParams ks = base; ks.killSwitch = true;
        Outcome ok2 = connectOnce(ks, cmd);
        CHECK(ok2.finishError && ok2.finishReason.contains("Kill switch") && ok2.hostKeyPrompts.isEmpty(),
              "kill switch blocks a host without proxy before any network traffic");
    }
    {
        std::printf("[host key change]\n");
        // Replace the stored key for [127.0.0.1]:2222 with another one.
        QFile f(kh); f.open(QIODevice::ReadOnly); QByteArray all = f.readAll(); f.close();
        QFile other("/etc/ssh/ssh_host_ed25519_key.pub");
        run("ssh-keygen", {"-q", "-t", "ed25519", "-N", "", "-f", dir + "/fakehost"});
        QFile fake(dir + "/fakehost.pub"); fake.open(QIODevice::ReadOnly);
        const QList<QByteArray> parts = fake.readAll().split(' ');
        QByteArray patched;
        for (const QByteArray &line : all.split('\n')) {
            if (line.isEmpty()) continue;
            QList<QByteArray> c = line.split(' ');
            if (c.size() >= 3 && c[1] == "ssh-ed25519") { c[2] = parts[1]; }
            patched += c.join(' ') + "\n";
        }
        QFile w(kh); w.open(QIODevice::WriteOnly); w.write(patched); w.close();

        ConnectParams p = base; p.password = "Secret-Pass-123";
        Outcome o = connectOnce(p, cmd, {}, /*accept*/ false);
        CHECK(o.hostKeyPrompts == QList<int>{SshWorker::HostChanged}, "changed host key is reported as CHANGED");
        CHECK(!o.connected && o.finishError, "rejecting a changed key aborts the connection");

        Outcome o2 = connectOnce(p, cmd, {}, true);
        CHECK(o2.connected, "accepting replaces the key and connects");
        Outcome o3 = connectOnce(p, cmd);
        CHECK(o3.hostKeyPrompts.isEmpty() && o3.connected, "after replacement no further prompts");
        { QFile dbg(kh); dbg.open(QIODevice::ReadOnly); std::printf("---known_hosts---\n%s-----\n", dbg.readAll().constData()); }
        int ed = 0;
        int plain = 0;
        for (const auto &e : knownhosts::load(kh)) {
            if (e.keyType == "ssh-ed25519" && knownhosts::matches(e.hosts, "127.0.0.1", 2222)) ++ed;
            if (!e.hosts.startsWith("|1|")) ++plain;
        }
        CHECK(ed == 1, "old conflicting entry was removed (one ed25519 entry)");
        CHECK(plain == 0, "known_hosts names are hashed after the key was accepted");
    }


    {
        std::printf("[passwords]\n");
        bool allOk = true;
        for (int i = 0; i < 200; ++i) {
            const QString pw = passwordtools::generate(19, true);
            if (pw.size() != 19 || !std::any_of(pw.begin(), pw.end(), [](QChar c){return c.isUpper();})
                || !std::any_of(pw.begin(), pw.end(), [](QChar c){return c.isDigit();})) allOk = false;
        }
        CHECK(allOk, "generated passwords: 19 chars, mixed classes");
        CHECK(passwordtools::generate(5).size() == 19, "generator never goes below 19");
        CHECK(passwordtools::evaluate(passwordtools::generate(24)).reliable, "generated 24-char password is reliable");
        CHECK(!passwordtools::evaluate("qwerty123").reliable && passwordtools::evaluate("qwerty123").score <= 1, "qwerty123 is weak");
        CHECK(!passwordtools::evaluate("Password2024!").reliable, "short common password not reliable");
        CHECK(passwordtools::evaluate("correct horse battery staple tree").score >= 3, "long passphrase scores well");
    }
    {
        std::printf("[firewall ranges]\n");
        const QStringList r = firewalllock::blockedRanges({QHostAddress("10.0.0.5")});
        CHECK(r.contains("0.0.0.0-10.0.0.4") && r.contains("10.0.0.6-126.255.255.255") && r.contains("128.0.0.0-255.255.255.255"),
              qPrintable("v4 complement around proxy + loopback: " + r.join(",")));
        const QStringList r6 = firewalllock::blockedRanges({QHostAddress("1.2.3.4"), QHostAddress("2001:db8::1")});
        CHECK(r6.contains("2001:db8::2-ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff") && r6.contains("::2-2001:db8::"),
              qPrintable("v6 complement: " + r6.mid(3).join(",")));
    }
    {
        std::printf("[ppk import]\n");
        struct { const char *type; const char *bits; const char *ver; } kinds[] = {
            {"ed25519", "", "3"}, {"rsa", "2048", "3"}, {"ecdsa", "256", "3"}, {"ed25519", "", "2"}, {"rsa", "2048", "2"}};
        for (auto &k : kinds) {
            const QString base = dir + QString("/pp_%1_%2").arg(k.type, k.ver);
            QFile pf(dir + "/pass.txt"); pf.open(QIODevice::WriteOnly); pf.write("ppk pass фраза"); pf.close();
            QStringList args{"-t", k.type, "-o", base + ".ppk", "--new-passphrase", dir + "/pass.txt", "-q",
                             "--ppk-param", QString("version=") + k.ver};
            if (*k.bits) args << "-b" << k.bits;
            int code = 0; run("puttygen", args, &code);
            const QString pubOssh = run("puttygen", {base + ".ppk", "-L", "--old-passphrase", dir + "/pass.txt"});
            QFile f(base + ".ppk"); f.open(QIODevice::ReadOnly);
            const QByteArray text = f.readAll();
            keytools::KeyMaterial km; QString err;
            const auto need = keytools::importPrivateKey(text, "", "x", &km, &err);
            const auto wrong = keytools::importPrivateKey(text, "nope", "x", &km, &err);
            const auto r = keytools::importPrivateKey(text, "ppk pass фраза", "", &km, &err);
            const QString label = QString("ppk v%1 %2").arg(k.ver, k.type);
            CHECK(code == 0 && need == keytools::ImportResult::NeedPassphrase && wrong == keytools::ImportResult::WrongPassphrase,
                  qPrintable(label + ": asks for / rejects wrong passphrase"));
            CHECK(r == keytools::ImportResult::Ok && pubOssh.section(' ', 0, 1) == km.publicLine.section(' ', 0, 1),
                  qPrintable(label + ": same public key as puttygen " + err));
            // converted key opens in ssh-keygen with the same passphrase
            keytools::writeTextFile(base + ".conv", km.privateKey, true, &err);
            const QString d = run("ssh-keygen", {"-y", "-P", "ppk pass фраза", "-f", base + ".conv"}, &code);
            CHECK(code == 0 && d.section(' ', 0, 1) == km.publicLine.section(' ', 0, 1), qPrintable(label + ": converted key works in ssh-keygen"));
        }
        run("puttygen", {"-t", "ed25519", "-o", dir + "/plain.ppk", "--new-passphrase", "/dev/null", "-q"});
        QFile f(dir + "/plain.ppk"); f.open(QIODevice::ReadOnly);
        keytools::KeyMaterial km; QString err;
        CHECK(keytools::importPrivateKey(f.readAll(), "", "x", &km, &err) == keytools::ImportResult::Ok && !km.encrypted,
              "unencrypted ppk imports");
    }
    {
        std::printf("[sftp]\n");
        ConnectParams p = base; p.password = "Secret-Pass-123";
        QByteArray payload(3 * 1024 * 1024 + 123, 0);
        for (int i = 0; i < payload.size(); ++i) payload[i] = char((i * 31 + i / 7) & 0xff);
        QFile up(dir + "/up.bin"); up.open(QIODevice::WriteOnly); up.write(payload); up.close();
        bool ok = true;
        runLive(p, [&](Live &L) {
            SshWorker *w = L.w;
            onW(w, [](SshWorker *w) { w->sftpList(1, ""); });
            if (!waitUntil([&] { return L.log.contains("listed"); })) ok = false;
            CHECK(L.lastError.isEmpty() && L.listedPath == "/home/tester", qPrintable("lists home dir: " + L.listedPath + " " + L.lastError));
            onW(w, [](SshWorker *w) { w->sftpMkdir(2, "/home/tester/sw_test"); });
            waitUntil([&] { return L.log.count("done") >= 1; });
            const QString local = dir + "/up.bin";
            onW(w, [local](SshWorker *w) { w->sftpUpload(10, local, "/home/tester/sw_test/up.bin"); });
            waitUntil([&] { return L.transferState.value(10, 0) != 0; }, 20000);
            CHECK(L.transferState.value(10) == SshWorker::TransferDone, "upload 3 MB finished");
            const QString back = dir + "/down.bin";
            onW(w, [back](SshWorker *w) { w->sftpDownload(11, "/home/tester/sw_test/up.bin", back); });
            waitUntil([&] { return L.transferState.value(11, 0) != 0; }, 20000);
            QFile d(back); d.open(QIODevice::ReadOnly);
            CHECK(L.transferState.value(11) == SshWorker::TransferDone && d.readAll() == payload, "download returns identical bytes");
            onW(w, [](SshWorker *w) { w->sftpRename(3, "/home/tester/sw_test/up.bin", "/home/tester/sw_test/renamed.bin"); });
            onW(w, [](SshWorker *w) { w->sftpList(4, "/home/tester/sw_test"); });
            L.log.clear();
            waitUntil([&] { return L.log.contains("listed"); });
            CHECK(L.listed.size() == 1 && L.listed.first().toMap().value("name") == "renamed.bin"
                  && L.listed.first().toMap().value("size").toLongLong() == payload.size(), "rename + list shows the file with right size");
            onW(w, [](SshWorker *w) { w->sftpRemove(5, "/home/tester/sw_test/renamed.bin", false); w->sftpRemove(6, "/home/tester/sw_test", true); });
            L.log.clear();
            waitUntil([&] { return L.log.count("done") >= 2; });
            CHECK(L.lastError.isEmpty(), "remove file and folder");
            onW(w, [](SshWorker *w) { w->sftpList(7, "/nonexistent/x"); });
            L.log.clear();
            waitUntil([&] { return L.log.contains("listed"); });
            CHECK(!L.lastError.isEmpty(), qPrintable("listing a missing folder reports an error: " + L.lastError));
            onW(w, [](SshWorker *w) { w->stop(); });
        });
        CHECK(ok, "sftp script ran");
    }
    {
        std::printf("[tunnels]\n");
        // local echo-ish target: a TCP server that replies with "pong:<data>"
        QTcpServer target; target.listen(QHostAddress::LocalHost, 0);
        const int tport = target.serverPort();
        QObject::connect(&target, &QTcpServer::newConnection, [&] {
            QTcpSocket *s = target.nextPendingConnection();
            QObject::connect(s, &QTcpSocket::readyRead, [s] { s->write("pong:" + s->readAll()); });
        });
        auto roundTrip = [&](int port, const QByteArray &socksPrefix, const QByteArray &msg) -> QByteArray {
            QTcpSocket c; c.connectToHost(QHostAddress::LocalHost, quint16(port));
            if (!waitUntil([&] { return c.state() == QAbstractSocket::ConnectedState; }, 5000)) return "noconnect";
            QByteArray got;
            QObject::connect(&c, &QTcpSocket::readyRead, [&] { got += c.readAll(); });
            if (!socksPrefix.isEmpty()) {
                c.write("\x05\x01\x00", 3);
                waitUntil([&] { return got.size() >= 2; }, 5000);
                got.clear();
                c.write(socksPrefix);
                waitUntil([&] { return got.size() >= 10; }, 5000);
                if (got.size() < 2 || got[1] != 0) return "socksfail";
                got.clear();
            }
            c.write(msg);
            waitUntil([&] { return got.contains(msg); }, 5000);
            return got;
        };
        ConnectParams p = base; p.password = "Secret-Pass-123";
        ForwardSpec L1; L1.id = "L"; L1.type = ForwardSpec::Local; L1.bindPort = 18080; L1.destHost = "127.0.0.1"; L1.destPort = tport;
        ForwardSpec D1; D1.id = "D"; D1.type = ForwardSpec::Dynamic; D1.bindPort = 18081;
        ForwardSpec R1; R1.id = "R"; R1.type = ForwardSpec::Remote; R1.bindHost = "127.0.0.1"; R1.bindPort = 18082; R1.destHost = "127.0.0.1"; R1.destPort = tport;
        p.forwards = {L1, D1, R1};
        runLive(p, [&](Live &Lv) {
            waitUntil([&] { return Lv.forwards.size() == 3; }, 5000);
            int active = 0;
            for (const QVariant &f : Lv.forwards) active += f.toMap().value("state") == "active";
            CHECK(active == 3, qPrintable(QString("all 3 tunnels active (%1)").arg(active)));
            CHECK(roundTrip(18080, {}, "hello-L") == "pong:hello-L", "local forward -L carries data");
            QByteArray req("\x05\x01\x00\x03", 4); req.append(char(9)); req.append("localhost");
            req.append(char(tport >> 8)); req.append(char(tport & 0xff));
            CHECK(roundTrip(18081, req, "hello-D") == "pong:hello-D", "dynamic forward -D (SOCKS5, remote DNS) carries data");
            // remote forward: connect on the *server* side port 18082 (same machine here)
            CHECK(roundTrip(18082, {}, "hello-R") == "pong:hello-R", "remote forward -R carries data");
            QTimer::singleShot(400, [&] {});
            waitUntil([&] { for (const QVariant &f : Lv.forwards) if (f.toMap().value("bytesIn").toLongLong() > 0) return true; return false; }, 2000);
            bool counted = false;
            for (const QVariant &f : Lv.forwards) counted = counted || f.toMap().value("bytesIn").toLongLong() > 0;
            CHECK(counted, "traffic counters update");
            onW(Lv.w, [](SshWorker *w) { w->removeForward("L"); });
            waitUntil([&] { return Lv.forwards.size() == 2; }, 3000);
            CHECK(Lv.forwards.size() == 2 && roundTrip(18080, {}, "x") == "noconnect", "removed tunnel stops listening");
            onW(Lv.w, [](SshWorker *w) { w->stop(); });
        });
    }
    {
        std::printf("[jump host]\n");
        ConnectParams p = base; p.password = "Secret-Pass-123";
        p.host = "localhost";             // target as seen from the jump host
        p.jump.enabled = true; p.jump.host = "127.0.0.1"; p.jump.port = 2222; p.jump.username = "tester";
        p.jump.password = "Secret-Pass-123";
        // target "localhost:2222" must be trusted too
        Outcome o = connectOnce(p, cmd);
        CHECK(o.connected && o.output.contains("MARK42"), qPrintable("connects through jump host: " + o.finishReason));
        CHECK(o.seenIp == "127.0.0.1" || o.seenIp == "::1", qPrintable("target sees the jump host's address: " + o.seenIp));
        ConnectParams bad = p; bad.jump.password = "wrong";
        Outcome ob = connectOnce(bad, cmd);
        CHECK(!ob.connected && ob.finishReason.contains("Jump host failed"), qPrintable("bad jump credentials fail cleanly: " + ob.finishReason));
        ConnectParams viaProxy = p; viaProxy.proxy.type = ProxyConfig::Socks5; viaProxy.proxy.host = "127.0.0.1"; viaProxy.proxy.port = 1080;
        viaProxy.proxy.username = "puser"; viaProxy.proxy.password = "ppass";
        Outcome op = connectOnce(viaProxy, cmd);
        CHECK(op.connected && op.output.contains("MARK42"), "proxy → jump host → target works");
    }
    {
        std::printf("[proxy probe]\n");
        ProxyConfig good; good.type = ProxyConfig::Socks5; good.host = "127.0.0.1"; good.port = 1080; good.username = "puser"; good.password = "ppass";
        QString err;
        CHECK(proxytunnel::probe(good, 2000, &err), "probe: working SOCKS5 with auth");
        ProxyConfig badAuth = good; badAuth.password = "x";
        CHECK(!proxytunnel::probe(badAuth, 2000, &err), qPrintable("probe: wrong auth detected: " + err));
        ProxyConfig dead = good; dead.port = 1999;
        QElapsedTimer t; t.start();
        CHECK(!proxytunnel::probe(dead, 2000, &err) && t.elapsed() < 2500, "probe: dead proxy detected fast");
        ProxyConfig http; http.type = ProxyConfig::Http; http.host = "127.0.0.1"; http.port = 8888;
        CHECK(proxytunnel::probe(http, 2000, &err), "probe: HTTP proxy alive");
    }
    {
        std::printf("[shadowsocks]\n");
        // test-only keys, the same as in tests/integration/setup.sh (servers listen on 127.0.0.1 only)
        const QString www = qEnvironmentVariable("SW_TEST_WWW", QStringLiteral("/tmp/sw-test/www"));
        auto sshVia = [&](const ProxyConfig &pc) {
            ConnectParams p = base; p.password = "Secret-Pass-123"; p.proxy = pc; p.connectTimeoutSeconds = 8;
            return connectOnce(p, cmd);
        };
        struct C { int port; const char *method; const char *key; } cs[] = {
            {8388, "chacha20-ietf-poly1305", "Test-SS-Pass-1"},
            {8389, "aes-256-gcm", "Test-SS-Pass-1"},
            {8390, "aes-128-gcm", "Test-SS-Pass-1"},
            {8391, "2022-blake3-aes-256-gcm", "c3dTZXN0S2V5LTMyLWJ5dGVzLWFlczI1Ni0wMDAwMDA="},
            {8392, "2022-blake3-aes-128-gcm", "c3dUZXN0S2V5LWFlczEyOA=="},
            {8393, "2022-blake3-chacha20-poly1305", "c3dUZXN0S2V5LTMyLWJ5dGVzLWNoYWNoYTIwLTAwMDA="},
            // identity header (multi-user server): server key : user key
            {8394, "2022-blake3-aes-256-gcm", "c3dUZXN0LWlkZW50aXR5LWtleS0zMi1ieXRlcy0wMDA=:c3dUZXN0LXVzZXIta2V5LTMyLWJ5dGVzLTAwMDAwMDA="},
        };
        for (const C &c : cs) {
            ProxyConfig pc; pc.type = ProxyConfig::Shadowsocks; pc.host = "127.0.0.1"; pc.port = c.port;
            pc.method = c.method; pc.password = c.key;
            const Outcome o = sshVia(pc);
            CHECK(o.connected && o.output.contains("MARK42"),
                  qPrintable(QStringLiteral("SSH through Shadowsocks %1 (port %2) %3").arg(c.method).arg(c.port).arg(o.finishReason)));
        }
        {
            ProxyConfig pc; pc.type = ProxyConfig::Shadowsocks; pc.host = "127.0.0.1"; pc.port = 8388; pc.method = "chacha20-ietf-poly1305"; pc.password = "wrong";
            const Outcome o = sshVia(pc);
            CHECK(!o.connected && o.finishReason.contains("Shadowsocks") && o.finishReason.contains("NOT attempted"),
                  qPrintable("wrong Shadowsocks password: clear error, no direct fallback: " + o.finishReason));
            ProxyConfig k = pc; k.port = 8391; k.method = "2022-blake3-aes-256-gcm"; k.password = cs[5].key;   // a key, but not this server's
            const Outcome o2 = sshVia(k);
            CHECK(!o2.connected && o2.finishReason.contains("Shadowsocks"), qPrintable("wrong Shadowsocks 2022 key: " + o2.finishReason));
        }
        for (const C *c : {&cs[1], &cs[5]}) {   // 2 MB both ways through the tunnel (many chunks)
            ProxyConfig pc; pc.type = ProxyConfig::Shadowsocks; pc.host = "127.0.0.1"; pc.port = c->port; pc.method = c->method; pc.password = c->key;
            QString err;
            const sw_socket_t s = shadowsocks::open(pc, "127.0.0.1", 8766, 5000, false, &err);
            QByteArray resp;
            if (s != kInvalidSocket) {
                proxytunnel::writeAll(s, "GET /big.bin HTTP/1.0\r\nHost: x\r\n\r\n", 3000);
                char buf[65536];
                for (;;) { const int n = proxytunnel::readSome(s, buf, sizeof buf, 3000); if (n <= 0) break; resp.append(buf, n); }
                proxytunnel::closeSocket(s);
            }
            const int body = int(resp.indexOf("\r\n\r\n"));
            QFile f(www + "/big.bin"); f.open(QIODevice::ReadOnly);
            CHECK(body > 0 && resp.mid(body + 4) == f.readAll(),
                  qPrintable(QStringLiteral("2 MB over HTTP through %1 (%2 bytes) %3").arg(c->method).arg(resp.size()).arg(err)));
        }
    }
    {
        std::printf("[loopback pair]\n");
        sw_socket_t a = kInvalidSocket, b = kInvalidSocket; QString err;
        CHECK(proxytunnel::loopbackPair(&a, &b, &err), qPrintable("loopback pair " + err));
        CHECK(proxytunnel::writeAll(a, "ping", 1000), "write one end");
        char buf[8] = {}; const int n = proxytunnel::readSome(b, buf, sizeof buf, 1000);
        CHECK(n == 4 && QByteArray(buf, 4) == "ping", "read the other end");
        proxytunnel::closeSocket(a); proxytunnel::closeSocket(b);
    }
    std::printf("\n%s (%d failures)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
    return g_fail ? 1 : 0;
}
