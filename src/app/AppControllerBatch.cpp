// Part of AppController: commands and keys on many servers at once, key rotation, host notes.
#include "AppController.h"
#include "core/Shadowsocks.h"
#include "TerminalSession.h"
#include "core/AgentClient.h"
#include "DarkTitleBar.h"
#include <QWindow>
#include "core/FirewallLock.h"
#include "core/HostImport.h"
#include "core/KeyTools.h"
#include "core/KnownHosts.h"
#include "core/Protected.h"
#include "core/PasswordTools.h"
#include "core/Paths.h"
#include "core/SecureBytes.h"
#include "terminal/TerminalItem.h"

#include <QDesktopServices>
#include <QDir>
#include <QFontDatabase>
#include <QThreadPool>
#ifdef SW_HAVE_SERIAL
#  include <QSerialPortInfo>
#endif

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QSet>
#include <QEvent>
#include <QFile>
#include <memory>
#include <QFileInfo>
#include <QImage>
#include <algorithm>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QColor>
#include <QLibraryInfo>
#include <QLocale>
#include <QQmlEngine>
#include <QPointer>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif
#include <QStandardPaths>
#include <QUuid>

#include "AppControllerPrivate.h"

// ---------------------------------------------------------------------------
// run a command on several hosts
// ---------------------------------------------------------------------------

QString AppController::runOnHosts(const QVariantList &hostIds, const QString &command, const QString &masterPassword)
{
    if (!m_vault.isUnlocked())
        return tr("The vault is locked.");
    if (command.trimmed().isEmpty())
        return tr("Enter a command.");
    if (hostIds.isEmpty())
        return tr("Choose at least one host.");
    // one master password unlocks every "ask the master password" host of this run
    QString err;
    Opened opened;
    const bool unlocked = !masterPassword.isEmpty() && openWithMaster(hostIds, masterPassword, &opened, &err);
    if (!masterPassword.isEmpty() && !unlocked)
        return err;
    if (unlocked)
        logEvent(ActivityLog::Security, QStringLiteral("session"), tr("Protected hosts unlocked with the master password for a batch run"));
    QVector<BatchRunner::Job> jobs;
    QStringList names;
    for (const QVariant &v : hostIds) {
        BatchRunner::Job j = batchJob(v.toString(), unlocked, opened);
        if (j.hostId.isEmpty())
            continue;
        names << j.label;
        jobs << j;
    }
    wipe(opened);
    m_batch.run(jobs, command);
    for (QString &p : m_batchPass) secure::wipe(p);
    m_batchPass.clear();
    logEvent(ActivityLog::Info, QStringLiteral("session"), tr("Command run on %n host(s)", nullptr, int(jobs.size())),
             command.left(200), names.join(QStringLiteral(", ")));
    return {};
}

BatchRunner::Job AppController::batchJob(const QString &hostId, bool protectedOk, const Opened &opened) const
{
    BatchRunner::Job j;
    const QJsonObject host = findIn(kHosts, hostId);
    if (host.isEmpty())
        return j;
    const bool killSwitch = m_settings.value(QStringLiteral("killSwitch"), false).toBool();
    j.hostId = hostId;
    j.label = host.value(QStringLiteral("label")).toString();
    j.endpoint = QStringLiteral("%1@%2").arg(host.value(QStringLiteral("username")).toString(),
                                             host.value(QStringLiteral("hostname")).toString());
    const QJsonObject jumpHost = findIn(kHosts, host.value(QStringLiteral("jumpHostId")).toString());
    const QString proxyId = effectiveProxyId(jumpHost.isEmpty() ? host : jumpHost);
    if (host.value(QStringLiteral("type")).toString() == QLatin1String("serial"))
        j.skipReason = tr("Serial connection: not supported here.");
    else if (host.value(QStringLiteral("type")).toString() == QLatin1String("telnet"))
        j.skipReason = tr("Telnet host: commands can only be run over SSH.");
    else if (!protectedOk && hostNeedsMaster(host))
        j.skipReason = tr("Asks for the master password: enter it to include this host.");
    else if (killSwitch && proxyId.isEmpty())
        j.skipReason = tr("Kill switch: this host has no proxy.");
    else {
        j.params = paramsFor(host, jumpHost, opened);
        j.params.killSwitch = killSwitch;
        j.params.keepaliveSeconds = 0;
        // key passphrase typed once in the batch window (not saved anywhere)
        const QString keyId = host.value(QStringLiteral("keyId")).toString();
        if (j.params.keyPassphrase.isEmpty() && m_batchPass.contains(keyId))
            j.params.keyPassphrase = m_batchPass.value(keyId);
        if (!proxyId.isEmpty()) {
            const QJsonObject px = findIn(kProxies, proxyId);
            const QString firstHop = (jumpHost.isEmpty() ? host : jumpHost).value(QStringLiteral("hostname")).toString();
            j.params.proxy = proxyConfig(px, firstHop);
            if (px.value(QStringLiteral("type")).toString() == QLatin1String("cloudflare")) {
                if (!j.params.proxy.isSet())
                    j.skipReason = tr("Cloudflare Tunnel is not started: open this host in a tab once.");
                else
                    j.params.connectTimeoutSeconds = 60;
            }
        }
    }
    return j;
}

// Read the same (or another) file on two hosts; the diff is drawn in QML.
QString AppController::compareFiles(const QString &hostA, const QString &pathA, const QString &hostB, const QString &pathB)
{
    if (!m_vault.isUnlocked())
        return tr("The vault is locked.");
    if (pathA.trimmed().isEmpty() || pathB.trimmed().isEmpty())
        return tr("Enter the file path.");
    auto quote = [](QString p) { p = p.trimmed(); p.replace(QLatin1Char('\''), QStringLiteral("'\\''")); return QLatin1Char('\'') + p + QLatin1Char('\''); };
    BatchRunner::Job a = batchJob(hostA), b = batchJob(hostB);
    if (a.hostId.isEmpty() || b.hostId.isEmpty())
        return tr("Choose two hosts.");
    a.command = QStringLiteral("cat -- ") + quote(pathA);
    b.command = QStringLiteral("cat -- ") + quote(pathB);
    m_diff.run({a, b}, QString());
    return {};
}

// ---------------------------------------------------------------------------
// put public keys from the keychain on servers (like ssh-copy-id, many at once)
// ---------------------------------------------------------------------------

bool AppController::hostIsProtected(const QString &hostId) const
{
    return hostNeedsMaster(findIn(kHosts, hostId));
}

QString AppController::installKeys(const QVariantList &pairs, const QString &masterPassword, bool useForHost)
{
    if (!m_vault.isUnlocked())
        return tr("The vault is locked.");
    if (pairs.isEmpty())
        return tr("Choose at least one host.");
    QString err;
    Opened opened;
    QVariantList hostIds;
    for (const QVariant &v : pairs)
        hostIds << v.toMap().value(QStringLiteral("hostId"));
    const bool unlocked = !masterPassword.isEmpty() && openWithMaster(hostIds, masterPassword, &opened, &err);
    if (!masterPassword.isEmpty() && !unlocked)
        return err;
    QVector<BatchRunner::Job> jobs;
    m_keyAssign.clear();
    QStringList names;
    for (const QVariant &v : pairs) {
        const QVariantMap m = v.toMap();
        const QString hostId = m.value(QStringLiteral("hostId")).toString();
        const QString keyId = m.value(QStringLiteral("keyId")).toString();
        QString line = publicKeyLine(keyId).trimmed();
        line.remove(QLatin1Char('\''));   // never break the shell quoting (a quote can only be in the comment)
        BatchRunner::Job j = batchJob(hostId, unlocked, opened);
        if (j.hostId.isEmpty())
            continue;
        if (line.isEmpty()) {
            j.skipReason = tr("Choose a key.");
        } else {
            const QString blob = line.section(QLatin1Char(' '), 1, 1);
            // the script goes as base64, so any login shell (sh, bash, zsh, fish) runs it the same way
            const QString script = QStringLiteral(
                "umask 077\n"
                "mkdir -p ~/.ssh && touch ~/.ssh/authorized_keys && chmod 700 ~/.ssh && chmod 600 ~/.ssh/authorized_keys || exit 1\n"
                "if grep -qF '%1' ~/.ssh/authorized_keys; then echo 'the key was already there'; "
                "else printf '%s\\n' '%2' >> ~/.ssh/authorized_keys && echo 'key added'; fi\n").arg(blob, line);
            j.command = QStringLiteral("echo %1 | base64 -d | sh").arg(QString::fromLatin1(script.toUtf8().toBase64()));
            if (useForHost)
                m_keyAssign.insert(hostId, keyId);
        }
        names << j.label;
        jobs << j;
    }
    wipe(opened);
    m_batch.run(jobs, QString());
    for (QString &p : m_batchPass) secure::wipe(p);
    m_batchPass.clear();
    logEvent(ActivityLog::Security, QStringLiteral("key"), tr("Public keys sent to %n server(s)", nullptr, int(jobs.size())),
             QString(), names.join(QStringLiteral(", ")));
    return {};
}

QString AppController::rotateKey(const QString &oldKeyId, const QString &newKeyId, const QVariantList &hostIds,
                                 const QString &masterPassword)
{
    if (m_batch.running())
        return tr("Another run is still going.");
    const QJsonObject oldKey = findIn(kKeys, oldKeyId), newKey = findIn(kKeys, newKeyId);
    if (oldKey.isEmpty() || newKey.isEmpty())
        return tr("Choose the old and the new key.");
    const QString oldBlob = publicKeyLine(oldKeyId).section(QLatin1Char(' '), 1, 1);
    const QString newBlob = publicKeyLine(newKeyId).section(QLatin1Char(' '), 1, 1);
    if (oldKeyId == newKeyId || oldBlob.isEmpty() || oldBlob == newBlob)
        return tr("The new key must be a different key.");
    if (newKey.value(QStringLiteral("privateKey")).toString().isEmpty() && !isSealed(newKey, QStringLiteral("privateKey")))
        return tr("Only the public part of the new key is in the keychain, so ShellWarden cannot log in with it. Choose a key with its private part.");
    // the new key itself may be protected: opened with the master password, kept until step 3
    Opened newOpened;
    if (isSealed(newKey)) {
        if (masterPassword.isEmpty())
            return tr("The new key is protected by the master password: enter it.");
        auto *sk = static_cast<unsigned char *>(sodium_malloc(protect::kSecretLen));
        if (!sk || !deriveProtectSecret(masterPassword, sk)) {
            if (sk) sodium_free(sk);
            return tr("Wrong master password.");
        }
        const QJsonObject sealed = newKey.value(QStringLiteral("sealed")).toObject();
        for (auto it = sealed.begin(); it != sealed.end(); ++it) {
            QByteArray plain;
            if (protect::open(it.value().toString().toLatin1(), protectPublicKey(), sk, &plain))
                newOpened.insert(newKeyId + QLatin1Char('/') + it.key(), QString::fromUtf8(plain));
            secure::wipe(plain);
        }
        sodium_free(sk);
    }
    secure::wipe(m_rotateNewKey);
    m_rotateNewKey = secretOf(newKey, QStringLiteral("privateKey"), newOpened).toUtf8();
    // the new key's passphrase is needed again in step 2, after installKeys() forgets the typed ones
    m_rotateNewPass = m_batchPass.value(newKeyId);
    if (m_rotateNewPass.isEmpty())
        m_rotateNewPass = secretOf(newKey, QStringLiteral("passphrase"), newOpened);
    wipe(newOpened);
    if (newKey.value(QStringLiteral("encrypted")).toBool() && m_rotateNewPass.isEmpty())
        return tr("Enter the passphrase of the new key: it is needed to check that the new key works.");
    QVariantList pairs;
    for (const QVariant &h : hostIds)
        pairs << QVariantMap{{QStringLiteral("hostId"), h.toString()}, {QStringLiteral("keyId"), newKeyId}};
    const QString err = installKeys(pairs, masterPassword, false);
    if (!err.isEmpty()) {
        secure::wipe(m_rotateNewPass);
        secure::wipe(m_rotateNewKey);
        return err;
    }
    m_rotateOld = oldKeyId;
    m_rotateNew = newKeyId;
    m_rotateUnlocked = !masterPassword.isEmpty();
    m_rotateFailed.clear();
    m_rotatePhase = QStringLiteral("adding");
    emit rotateChanged();
    logEvent(ActivityLog::Security, QStringLiteral("key"), tr("Key replacement started"),
             oldKey.value(QStringLiteral("name")).toString() + QStringLiteral(" → ") + newKey.value(QStringLiteral("name")).toString());
    return {};
}

// Runs a script on every server that passed the previous step, logged in with ONLY
// the new key (no password, no agent, no old key). Servers that failed before are
// listed as skipped with the reason.
void AppController::rotateNewKeyStep(const QString &phase, const QString &script, const QString &failedBefore)
{
    const QJsonObject newKey = findIn(kKeys, m_rotateNew);
    const QString command = QStringLiteral("echo %1 | base64 -d | sh").arg(QString::fromLatin1(script.toUtf8().toBase64()));
    QVector<BatchRunner::Job> jobs;
    for (const QVariant &r : m_batch.results()) {
        const QVariantMap m = r.toMap();
        const QString hostId = m.value(QStringLiteral("hostId")).toString();
        BatchRunner::Job j = batchJob(hostId, m_rotateUnlocked);
        if (j.hostId.isEmpty())
            continue;
        if (m.value(QStringLiteral("state")).toString() != QLatin1String("ok")) {
            j.skipReason = failedBefore.arg(m.value(QStringLiteral("error")).toString());
        } else if (j.skipReason.isEmpty()) {
            j.params.privateKey = m_rotateNewKey;
            j.params.keyPassphrase = m_rotateNewPass;
            secure::wipe(j.params.password);
            j.params.useAgent = false;
            j.params.agentForward = false;
            j.command = command;
        }
        jobs << j;
    }
    m_rotatePhase = phase;
    emit rotateChanged();
    m_batch.run(jobs, QString());
}

void AppController::rotateSecondStep()
{
    const QString oldBlob = publicKeyLine(m_rotateOld).section(QLatin1Char(' '), 1, 1);
    const QString newBlob = publicKeyLine(m_rotateNew).section(QLatin1Char(' '), 1, 1);
    // we got in with the new key; remove the old one only when the new one is there
    rotateNewKeyStep(QStringLiteral("removing"), QStringLiteral(
        "umask 077\n"
        "f=~/.ssh/authorized_keys\n"
        "grep -qF '%1' \"$f\" || { echo 'the new key is missing, the old key was kept'; exit 1; }\n"
        "if grep -qF '%2' \"$f\"; then grep -vF '%2' \"$f\" > \"$f.sw\" && cat \"$f.sw\" > \"$f\" && rm -f \"$f.sw\" && echo 'old key removed'; "
        "else echo 'the old key was not there'; fi\n").arg(newBlob, oldBlob),
        tr("The new key could not be added, nothing changed: %1"));
}

void AppController::rotateThirdStep()
{
    const QString oldBlob = publicKeyLine(m_rotateOld).section(QLatin1Char(' '), 1, 1);
    const QString newBlob = publicKeyLine(m_rotateNew).section(QLatin1Char(' '), 1, 1);
    // a fresh login with the new key: is the new key there and the old one really gone?
    rotateNewKeyStep(QStringLiteral("verifying"), QStringLiteral(
        "f=~/.ssh/authorized_keys\n"
        "grep -qF '%1' \"$f\" || { echo 'the new key is missing'; exit 1; }\n"
        "if grep -qF '%2' \"$f\"; then echo 'the old key is still there'; exit 1; fi\n"
        "echo verified\n").arg(newBlob, oldBlob),
        tr("The new key did not work here, so the old key was kept: %1"));
}

void AppController::rotateFinish()
{
    QJsonArray hosts = section(kHosts);
    int changed = 0;
    for (const QVariant &r : m_batch.results()) {
        const QVariantMap m = r.toMap();
        if (m.value(QStringLiteral("state")).toString() != QLatin1String("ok"))
            continue;
        const QString hostId = m.value(QStringLiteral("hostId")).toString();
        for (int i = 0; i < hosts.size(); ++i) {
            QJsonObject o = hosts.at(i).toObject();
            if (o.value(QStringLiteral("id")).toString() == hostId) {
                o.insert(QStringLiteral("keyId"), m_rotateNew);   // log in with the new key from now on
                hosts.replace(i, o);
                ++changed;
            }
        }
    }
    if (changed) {
        setSection(kHosts, hosts);
        sealAllProtected();   // a protected host's new key is protected too
        persist();
        m_hosts.reset(section(kHosts));
    }
    logEvent(ActivityLog::Security, QStringLiteral("key"), tr("Key replaced on %n server(s)", nullptr, changed),
             tr("%1 of %2 servers").arg(changed).arg(m_batch.results().size()));
    secure::wipe(m_rotateNewPass);
    secure::wipe(m_rotateNewKey);
    m_rotatePhase = QStringLiteral("done");
    emit rotateChanged();
}

void AppController::onBatchFinished()
{
    if (m_rotatePhase == QLatin1String("adding")) {
        QTimer::singleShot(0, this, &AppController::rotateSecondStep);   // not from inside the runner's signal
        return;
    }
    if (m_rotatePhase == QLatin1String("removing")) {
        QTimer::singleShot(0, this, &AppController::rotateThirdStep);
        return;
    }
    if (m_rotatePhase == QLatin1String("verifying")) {
        rotateFinish();   // only servers that passed the check switch to the new key
        return;
    }
    if (m_keyAssign.isEmpty())
        return;
    // after a successful key install: let the host log in with that key from now on
    QJsonArray hosts = section(kHosts);
    int changed = 0;
    for (const QVariant &r : m_batch.results()) {
        const QVariantMap m = r.toMap();
        const QString hostId = m.value(QStringLiteral("hostId")).toString();
        if (m.value(QStringLiteral("state")).toString() != QLatin1String("ok") || !m_keyAssign.contains(hostId))
            continue;
        for (int i = 0; i < hosts.size(); ++i) {
            QJsonObject o = hosts.at(i).toObject();
            if (o.value(QStringLiteral("id")).toString() == hostId) {
                o.insert(QStringLiteral("keyId"), m_keyAssign.value(hostId));
                hosts.replace(i, o);
                ++changed;
            }
        }
    }
    m_keyAssign.clear();
    if (changed) {
        setSection(kHosts, hosts);
        persist();
        m_hosts.reset(section(kHosts));
        emit notify(tr("The key is now used to log in on %n server(s).", nullptr, changed), false);
    }
}

// notes of a host (stored encrypted in the vault with the host)
QString AppController::hostNotes(const QString &hostId) const
{
    return findIn(kHosts, hostId).value(QStringLiteral("notes")).toString();
}

QString AppController::setHostNotes(const QString &hostId, const QString &notes)
{
    QJsonArray hosts = section(kHosts);
    for (int i = 0; i < hosts.size(); ++i) {
        QJsonObject o = hosts.at(i).toObject();
        if (o.value(QStringLiteral("id")).toString() != hostId)
            continue;
        o.insert(QStringLiteral("notes"), notes);
        hosts.replace(i, o);
        setSection(kHosts, hosts);
        if (!persist())
            return tr("Could not save the vault.");
        m_hosts.reset(section(kHosts));
        return {};
    }
    return tr("This host no longer exists.");
}

// Keys of these hosts that need a passphrase which is not saved in the vault:
// the batch window asks for each of them once.
QVariantList AppController::batchKeyNeeds(const QVariantList &hostIds, const QVariantList &extraKeyIds) const
{
    QVariantList out;
    QSet<QString> seen;
    for (const QVariant &v : extraKeyIds) {   // e.g. the new key of a key replacement
        const QJsonObject k = findIn(kKeys, v.toString());
        if (k.isEmpty() || seen.contains(v.toString()))
            continue;
        if (k.value(QStringLiteral("encrypted")).toBool() && k.value(QStringLiteral("passphrase")).toString().isEmpty()
            && !isSealed(k, QStringLiteral("passphrase"))) {
            seen.insert(v.toString());
            out << QVariantMap{{QStringLiteral("keyId"), v.toString()}, {QStringLiteral("name"), k.value(QStringLiteral("name")).toString()}};
        }
    }
    for (const QVariant &v : hostIds) {
        const QJsonObject h = findIn(kHosts, v.toString());
        const QString keyId = h.value(QStringLiteral("keyId")).toString();
        if (keyId.isEmpty() || seen.contains(keyId))
            continue;
        const QJsonObject k = findIn(kKeys, keyId);
        if (k.isEmpty())
            continue;
        const bool encrypted = k.value(QStringLiteral("encrypted")).toBool();
        const bool saved = (!k.value(QStringLiteral("passphrase")).toString().isEmpty() || isSealed(k, QStringLiteral("passphrase")))
                           && !h.value(QStringLiteral("alwaysAskPassphrase")).toBool();
        if (encrypted && !saved) {
            seen.insert(keyId);
            out << QVariantMap{{QStringLiteral("keyId"), keyId}, {QStringLiteral("name"), k.value(QStringLiteral("name")).toString()}};
        }
    }
    return out;
}

QString AppController::setBatchPassphrases(const QVariantMap &map)
{
    for (QString &p : m_batchPass) secure::wipe(p);
    m_batchPass.clear();
    for (auto it = map.begin(); it != map.end(); ++it) {
        const QString pass = it.value().toString();
        const QJsonObject k = findIn(kKeys, it.key());
        if (pass.isEmpty())
            continue;
        // a protected key cannot be checked before the master password opens it: it is checked at login
        if (!isSealed(k, QStringLiteral("privateKey"))
            && !keytools::checkPassphrase(k.value(QStringLiteral("privateKey")).toString().toUtf8(), pass))
            return tr("Wrong passphrase for the key \"%1\".").arg(k.value(QStringLiteral("name")).toString());
        m_batchPass.insert(it.key(), pass);
    }
    return {};
}
