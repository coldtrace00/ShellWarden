// Part of AppController: the keychain: generating, importing, exporting keys.
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
// keys
// ---------------------------------------------------------------------------

QString AppController::generateKey(const QString &name, int type, const QString &comment,
                                   const QString &passphrase, const QString &confirm, bool savePassphrase)
{
    if (!m_vault.isUnlocked())
        return tr("The vault is locked.");
    if (passphrase != confirm)
        return tr("Passphrases do not match.");

    static const keytools::KeyType types[] = {
        keytools::KeyType::Ed25519, keytools::KeyType::EcdsaP256, keytools::KeyType::EcdsaP384,
        keytools::KeyType::EcdsaP521, keytools::KeyType::Rsa3072, keytools::KeyType::Rsa4096};
    if (type < 0 || type > 5)
        type = 0;

    keytools::KeyMaterial km;
    QString err;
    if (!keytools::generate(types[type], comment, passphrase, &km, &err))
        return err;

    QJsonObject o;
    o.insert(QStringLiteral("id"), newId());
    o.insert(QStringLiteral("name"), name.trimmed().isEmpty() ? km.type : name.trimmed());
    o.insert(QStringLiteral("type"), km.type);
    o.insert(QStringLiteral("fingerprint"), km.fingerprint);
    o.insert(QStringLiteral("publicKey"), km.publicLine);
    o.insert(QStringLiteral("privateKey"), QString::fromUtf8(km.privateKey));
    o.insert(QStringLiteral("encrypted"), km.encrypted);
    o.insert(QStringLiteral("passphrase"), savePassphrase ? passphrase : QString());
    o.insert(QStringLiteral("created"), QDateTime::currentDateTime().toString(Qt::ISODate));
    secure::wipe(km.privateKey);

    QJsonArray keys = section(kKeys);
    keys.append(o);
    setSection(kKeys, keys);
    if (!persist())
        return tr("Could not save the vault.");
    logEvent(ActivityLog::Security, QStringLiteral("key"), tr("Key generated"),
             km.type + QStringLiteral(" · ") + km.fingerprint + (km.encrypted ? QString() : tr(" · no passphrase")),
             o.value(QStringLiteral("name")).toString());
    return {};
}

QVariantMap AppController::importKey(const QString &name, const QString &text,
                                     const QString &passphrase, bool savePassphrase)
{
    QVariantMap r;
    if (!m_vault.isUnlocked()) {
        r.insert(QStringLiteral("error"), tr("The vault is locked."));
        return r;
    }
    keytools::KeyMaterial km;
    QString err;
    const QString comment = name.trimmed();
    switch (keytools::importPrivateKey(text.toUtf8(), passphrase, comment, &km, &err)) {
    case keytools::ImportResult::NeedPassphrase:
        r.insert(QStringLiteral("needPassphrase"), true);
        r.insert(QStringLiteral("error"), tr("This key is protected. Enter its passphrase."));
        return r;
    case keytools::ImportResult::WrongPassphrase:
        r.insert(QStringLiteral("needPassphrase"), true);
        r.insert(QStringLiteral("error"), tr("Wrong passphrase."));
        return r;
    case keytools::ImportResult::Invalid:
        r.insert(QStringLiteral("error"), err);
        return r;
    case keytools::ImportResult::Ok:
        break;
    }

    for (const QJsonValue &v : section(kKeys)) {
        if (v.toObject().value(QStringLiteral("fingerprint")).toString() == km.fingerprint) {
            r.insert(QStringLiteral("error"), tr("This key is already in the keychain (\"%1\").")
                                                  .arg(v.toObject().value(QStringLiteral("name")).toString()));
            return r;
        }
    }

    QJsonObject o;
    o.insert(QStringLiteral("id"), newId());
    o.insert(QStringLiteral("name"), comment.isEmpty() ? km.type : comment);
    o.insert(QStringLiteral("type"), km.type);
    o.insert(QStringLiteral("fingerprint"), km.fingerprint);
    o.insert(QStringLiteral("publicKey"), km.publicLine);
    o.insert(QStringLiteral("privateKey"), QString::fromUtf8(km.privateKey));
    o.insert(QStringLiteral("encrypted"), km.encrypted);
    o.insert(QStringLiteral("passphrase"), (savePassphrase && km.encrypted) ? passphrase : QString());
    o.insert(QStringLiteral("created"), QDateTime::currentDateTime().toString(Qt::ISODate));
    secure::wipe(km.privateKey);

    QJsonArray keys = section(kKeys);
    keys.append(o);
    setSection(kKeys, keys);
    if (!persist())
        r.insert(QStringLiteral("error"), tr("Could not save the vault."));
    else
        r.insert(QStringLiteral("id"), o.value(QStringLiteral("id")).toString());
    if (r.contains(QStringLiteral("id")))
        logEvent(ActivityLog::Security, QStringLiteral("key"), tr("Key imported"),
                 km.type + QStringLiteral(" · ") + km.fingerprint, o.value(QStringLiteral("name")).toString());
    return r;
}

QString AppController::readTextFile(const QUrl &url) const
{
    QFile f(url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (!f.open(QIODevice::ReadOnly) || f.size() > 1024 * 1024)
        return {};
    return QString::fromUtf8(f.readAll());
}

QUrl AppController::suggestedFile(const QString &fileName) const
{
    QDir().mkpath(paths::keysDir());
    return QUrl::fromLocalFile(paths::keysDir() + QLatin1Char('/') + fileName);
}

QString AppController::publicKeyLine(const QString &id) const
{
    return findIn(kKeys, id).value(QStringLiteral("publicKey")).toString();
}

QString AppController::exportPublicKey(const QString &id, const QUrl &url)
{
    const QString line = publicKeyLine(id);
    if (line.isEmpty())
        return tr("Key not found.");
    QString err;
    // One line, OpenSSH format, LF, no BOM — ready for authorized_keys.
    if (!keytools::writeTextFile(url.toLocalFile(), line.toUtf8(), false, &err))
        return err;
    return {};
}

QString AppController::exportPublicKeyRfc4716(const QString &id, const QUrl &url)
{
    const QString line = publicKeyLine(id);
    if (line.isEmpty())
        return tr("Key not found.");
    QString err;
    if (!keytools::writeTextFile(url.toLocalFile(), keytools::toRfc4716(line).toUtf8(), false, &err))
        return err;
    return {};
}

QString AppController::exportPrivateKey(const QString &id, const QUrl &url, const QString &masterPassword)
{
    const QJsonObject k = findIn(kKeys, id);
    if (k.isEmpty())
        return tr("Key not found.");
    QString plain = k.value(QStringLiteral("privateKey")).toString();
    if (isSealed(k, QStringLiteral("privateKey"))) {   // protected: the master password opens it for this export
        auto *sk = static_cast<unsigned char *>(sodium_malloc(protect::kSecretLen));
        QByteArray opened;
        const bool ok = sk && deriveProtectSecret(masterPassword, sk)
                        && protect::open(k.value(QStringLiteral("sealed")).toObject().value(QStringLiteral("privateKey")).toString().toLatin1(),
                                         protectPublicKey(), sk, &opened);
        if (sk)
            sodium_free(sk);
        if (!ok)
            return tr("Wrong master password.");
        plain = QString::fromUtf8(opened);
        secure::wipe(opened);
    }
    QByteArray text = plain.toUtf8();
    secure::wipe(plain);
    secure::ByteGuard g(text);
    QString err;
    if (!keytools::writeTextFile(url.toLocalFile(), text, true, &err))
        return err;
    logEvent(ActivityLog::Security, QStringLiteral("key"), tr("Private key exported"), url.toLocalFile(),
             k.value(QStringLiteral("name")).toString());
    return {};
}

QString AppController::setKeyPassphrase(const QString &id, const QString &passphrase, bool save)
{
    QJsonArray keys = section(kKeys);
    for (int i = 0; i < keys.size(); ++i) {
        QJsonObject o = keys.at(i).toObject();
        if (o.value(QStringLiteral("id")).toString() != id)
            continue;
        if (isSealed(o))
            return tr("This key is protected by the master password (a host that uses it asks for it). "
                      "Its saved passphrase is protected the same way.");
        if (!keytools::checkPassphrase(o.value(QStringLiteral("privateKey")).toString().toUtf8(), passphrase))
            return tr("Wrong passphrase.");
        o.insert(QStringLiteral("passphrase"), save ? passphrase : QString());
        keys.replace(i, o);
        setSection(kKeys, keys);
        return persist() ? QString() : tr("Could not save the vault.");
    }
    return tr("Key not found.");
}

QVariantMap AppController::keyPassphraseState(const QString &id) const
{
    const QJsonObject k = findIn(kKeys, id);
    const bool sealedPass = isSealed(k, QStringLiteral("passphrase"));
    return {
        {QStringLiteral("encrypted"), k.value(QStringLiteral("encrypted")).toBool()},
        {QStringLiteral("saved"), sealedPass || !k.value(QStringLiteral("passphrase")).toString().isEmpty()},
        {QStringLiteral("protected"), isSealed(k, QStringLiteral("privateKey"))},
    };
}

QString AppController::changeKeyPassphrase(const QString &id, const QString &current, const QString &newPassphrase,
                                           const QString &confirm, bool saveNew, const QString &masterPassword)
{
    if (newPassphrase != confirm)
        return tr("The new passphrases do not match.");
    QJsonArray keys = section(kKeys);
    int index = -1;
    for (int i = 0; i < keys.size(); ++i)
        if (keys.at(i).toObject().value(QStringLiteral("id")).toString() == id)
            index = i;
    if (index < 0)
        return tr("Key not found.");
    QJsonObject o = keys.at(index).toObject();
    if (o.value(QStringLiteral("privateKey")).toString().isEmpty() && !isSealed(o, QStringLiteral("privateKey")))
        return tr("Only the public key of \"%1\" is stored, so there is no private key to protect.")
            .arg(o.value(QStringLiteral("name")).toString());
    const bool wasEncrypted = o.value(QStringLiteral("encrypted")).toBool();
    const bool isProtected = isSealed(o, QStringLiteral("privateKey"));

    // the key text and its saved passphrase, opened with the master password if protected
    QString keyText = o.value(QStringLiteral("privateKey")).toString();
    QString savedPass = o.value(QStringLiteral("passphrase")).toString();
    if (isSealed(o)) {
        auto *sk = static_cast<unsigned char *>(sodium_malloc(protect::kSecretLen));
        if (!sk)
            return tr("Not enough secure memory.");
        bool ok = deriveProtectSecret(masterPassword, sk);
        const QJsonObject sealed = o.value(QStringLiteral("sealed")).toObject();
        for (const QString &f : {QStringLiteral("privateKey"), QStringLiteral("passphrase")}) {
            if (!ok || !sealed.contains(f))
                continue;
            QByteArray plain;
            ok = protect::open(sealed.value(f).toString().toLatin1(), protectPublicKey(), sk, &plain);
            (f == QLatin1String("privateKey") ? keyText : savedPass) = QString::fromUtf8(plain);
            secure::wipe(plain);
        }
        sodium_free(sk);
        if (!ok) {
            secure::wipe(keyText);
            secure::wipe(savedPass);
            return tr("Wrong master password.");
        }
    }
    QByteArray text = keyText.toUtf8();
    secure::wipe(keyText);
    secure::ByteGuard textGuard(text);
    QString oldPass = (current.isEmpty() && wasEncrypted) ? savedPass : current;
    secure::wipe(savedPass);

    QByteArray newText;
    QString err;
    const auto rc = keytools::changePassphrase(text, oldPass, newPassphrase, &newText, &err);
    secure::wipe(oldPass);
    if (rc != keytools::PassphraseChange::Ok)
        return (rc == keytools::PassphraseChange::WrongPassphrase && current.isEmpty())
                   ? tr("Enter the current passphrase of the key.") : err;
    secure::ByteGuard newGuard(newText);

    const bool nowEncrypted = !newPassphrase.isEmpty();
    o.insert(QStringLiteral("privateKey"), QString::fromUtf8(newText));
    o.insert(QStringLiteral("encrypted"), nowEncrypted);
    o.insert(QStringLiteral("passphrase"), (nowEncrypted && saveNew) ? newPassphrase : QString());
    QJsonObject sealed = o.value(QStringLiteral("sealed")).toObject();
    sealed.remove(QStringLiteral("privateKey"));
    sealed.remove(QStringLiteral("passphrase"));
    if (sealed.isEmpty())
        o.remove(QStringLiteral("sealed"));
    else
        o.insert(QStringLiteral("sealed"), sealed);
    if (isProtected && !sealObject(o, {QStringLiteral("privateKey"), QStringLiteral("passphrase")}))
        return tr("Could not protect the key with the master password; nothing was changed.");

    const QJsonArray before = keys;
    keys.replace(index, o);
    setSection(kKeys, keys);
    if (!persist()) {
        setSection(kKeys, before);
        return tr("Could not save the vault.");
    }
    const QString what = !nowEncrypted ? tr("Key passphrase removed")
                         : wasEncrypted ? tr("Key passphrase changed") : tr("Key passphrase added");
    logEvent(ActivityLog::Security, QStringLiteral("key"), what, o.value(QStringLiteral("fingerprint")).toString(),
             o.value(QStringLiteral("name")).toString());
    return {};
}

void AppController::forgetKeyPassphrase(const QString &id)
{
    QJsonArray keys = section(kKeys);
    for (int i = 0; i < keys.size(); ++i) {
        QJsonObject o = keys.at(i).toObject();
        if (o.value(QStringLiteral("id")).toString() == id) {
            o.insert(QStringLiteral("passphrase"), QString());
            if (isSealed(o, QStringLiteral("passphrase"))) {   // a protected passphrase goes too
                QJsonObject sealed = o.value(QStringLiteral("sealed")).toObject();
                sealed.remove(QStringLiteral("passphrase"));
                o.insert(QStringLiteral("sealed"), sealed);
            }
            keys.replace(i, o);
            setSection(kKeys, keys);
            persist();
            return;
        }
    }
}

void AppController::renameKey(const QString &id, const QString &name)
{
    if (name.trimmed().isEmpty())
        return;
    QJsonArray keys = section(kKeys);
    for (int i = 0; i < keys.size(); ++i) {
        QJsonObject o = keys.at(i).toObject();
        if (o.value(QStringLiteral("id")).toString() == id) {
            o.insert(QStringLiteral("name"), name.trimmed());
            keys.replace(i, o);
            setSection(kKeys, keys);
            persist();
            return;
        }
    }
}

void AppController::deleteKey(const QString &id)
{
    logEvent(ActivityLog::Warning, QStringLiteral("key"), tr("Key deleted"),
             findIn(kKeys, id).value(QStringLiteral("fingerprint")).toString(),
             findIn(kKeys, id).value(QStringLiteral("name")).toString());
    QJsonArray keys = section(kKeys);
    for (int i = 0; i < keys.size(); ++i) {
        if (keys.at(i).toObject().value(QStringLiteral("id")).toString() == id) {
            keys.removeAt(i);
            break;
        }
    }
    setSection(kKeys, keys);
    // Hosts that used this key fall back to password authentication.
    QJsonArray hosts = section(kHosts);
    for (int i = 0; i < hosts.size(); ++i) {
        QJsonObject h = hosts.at(i).toObject();
        if (h.value(QStringLiteral("keyId")).toString() == id) {
            h.insert(QStringLiteral("keyId"), QString());
            hosts.replace(i, h);
        }
    }
    setSection(kHosts, hosts);
    persist();
}
