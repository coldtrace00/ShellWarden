// Part of AppController: the keys made when the vault is opened (journal,
// protected hosts) and the hosts that ask for the master password, whose
// secrets stay sealed until a connection needs them (core/Protected.h).
#include "AppController.h"
#include "core/Paths.h"
#include "core/Protected.h"
#include "core/SecureBytes.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <sodium.h>

namespace {
const QString kHosts = QStringLiteral("hosts");
const QString kKeys = QStringLiteral("keys");
} // namespace

// ---------------------------------------------------------------------------
// keys made when the vault is opened; protected hosts (core/Protected.h)
// ---------------------------------------------------------------------------

QString AppController::ensureVaultKeys(const QString &password)
{
    QJsonObject &d = m_vault.data();
    bool changed = false;
    // the journal: a key pair of its own, the secret half only in the vault
    QJsonObject j = d.value(QStringLiteral("journal")).toObject();
    QByteArray jpk = QByteArray::fromBase64(j.value(QStringLiteral("pk")).toString().toLatin1());
    QByteArray jsk = QByteArray::fromBase64(j.value(QStringLiteral("sk")).toString().toLatin1());
    if (jpk.size() != crypto_box_PUBLICKEYBYTES || jsk.size() != crypto_box_SECRETKEYBYTES) {
        jpk.resize(crypto_box_PUBLICKEYBYTES);
        jsk.resize(crypto_box_SECRETKEYBYTES);
        crypto_box_keypair(reinterpret_cast<unsigned char *>(jpk.data()), reinterpret_cast<unsigned char *>(jsk.data()));
        d.insert(QStringLiteral("journal"), QJsonObject{{QStringLiteral("pk"), QString::fromLatin1(jpk.toBase64())},
                                                         {QStringLiteral("sk"), QString::fromLatin1(jsk.toBase64())}});
        changed = true;
    }
    m_log.setRetentionDays(m_settings.value(QStringLiteral("logRetentionDays"), 30).toInt());
    QString warning = m_log.open(jpk, jsk);
    secure::wipe(jsk);

    // protected hosts: the public key and the salt; the secret half is derived when needed
    if (d.value(QStringLiteral("protect")).toObject().value(QStringLiteral("pk")).toString().isEmpty()) {
        QByteArray salt(protect::kSaltLen, Qt::Uninitialized);
        randombytes_buf(salt.data(), size_t(salt.size()));
        auto *seed = static_cast<unsigned char *>(sodium_malloc(32));
        auto *sk = static_cast<unsigned char *>(sodium_malloc(protect::kSecretLen));
        QString err;
        if (seed && sk && m_vault.deriveWithVaultCost(password, salt, seed, &err)) {
            QByteArray pk;
            protect::keypairFromSeed(seed, &pk, sk);
            d.insert(QStringLiteral("protect"), QJsonObject{{QStringLiteral("salt"), QString::fromLatin1(salt.toBase64())},
                                                             {QStringLiteral("pk"), QString::fromLatin1(pk.toBase64())}});
            changed = true;
        } else if (warning.isEmpty()) {
            warning = tr("Could not prepare the key for protected hosts: %1").arg(err);
        }
        if (seed) sodium_free(seed);
        if (sk) sodium_free(sk);
    }
    if (changed)
        persist();
    if (!paths::protectionError().isEmpty())
        warning += (warning.isEmpty() ? QString() : QStringLiteral(" ")) + paths::protectionError();
    return warning;
}

QByteArray AppController::protectPublicKey() const
{
    return QByteArray::fromBase64(m_vault.data().value(QStringLiteral("protect")).toObject()
                                      .value(QStringLiteral("pk")).toString().toLatin1());
}

bool AppController::deriveProtectSecret(const QString &master, unsigned char *secret32) const
{
    const QJsonObject prot = m_vault.data().value(QStringLiteral("protect")).toObject();
    const QByteArray salt = QByteArray::fromBase64(prot.value(QStringLiteral("salt")).toString().toLatin1());
    const QByteArray pk = protectPublicKey();
    if (salt.size() != protect::kSaltLen || pk.size() != protect::kPublicLen)
        return false;
    auto *seed = static_cast<unsigned char *>(sodium_malloc(32));
    if (!seed)
        return false;
    QString err;
    bool ok = m_vault.deriveWithVaultCost(master, salt, seed, &err);
    if (ok) {
        QByteArray derived;
        protect::keypairFromSeed(seed, &derived, secret32);
        ok = sodium_memcmp(derived.constData(), pk.constData(), protect::kPublicLen) == 0;   // proves the password
    }
    sodium_free(seed);
    if (!ok)
        sodium_memzero(secret32, protect::kSecretLen);
    return ok;
}

bool AppController::isSealed(const QJsonObject &o, const QString &field)
{
    const QJsonObject sealed = o.value(QStringLiteral("sealed")).toObject();
    return field.isEmpty() ? !sealed.isEmpty() : sealed.contains(field);
}

bool AppController::sealObject(QJsonObject &o, const QStringList &fields) const
{
    const QByteArray pk = protectPublicKey();
    if (pk.size() != protect::kPublicLen)
        return false;
    QJsonObject sealed = o.value(QStringLiteral("sealed")).toObject();
    bool changed = false;
    for (const QString &f : fields) {
        QString plain = o.value(f).toString();
        if (plain.isEmpty())
            continue;
        QByteArray bytes = plain.toUtf8();
        const QByteArray box = protect::seal(bytes, pk);
        secure::wipe(bytes);
        secure::wipe(plain);
        if (box.isEmpty())
            return false;
        sealed.insert(f, QString::fromLatin1(box));
        o.remove(f);
        changed = true;
    }
    if (changed) {
        o.insert(QStringLiteral("sealed"), sealed);
        o.insert(QStringLiteral("protected"), true);
    }
    return changed;
}

// Hosts that ask for the master password: their password and the private key
// they use (with its passphrase) are sealed. A key used by such a host stays
// sealed for every host that uses it — those hosts then ask too.
bool AppController::sealAllProtected()
{
    if (protectPublicKey().size() != protect::kPublicLen)
        return false;
    QJsonArray hosts = section(kHosts), keys = section(kKeys);
    QSet<QString> keyIds;
    bool changed = false;
    for (int i = 0; i < hosts.size(); ++i) {
        QJsonObject h = hosts.at(i).toObject();
        if (!h.value(QStringLiteral("confirmMaster")).toBool())
            continue;
        if (sealObject(h, {QStringLiteral("password")})) {
            hosts.replace(i, h);
            changed = true;
        }
        if (!h.value(QStringLiteral("keyId")).toString().isEmpty())
            keyIds.insert(h.value(QStringLiteral("keyId")).toString());
    }
    for (int i = 0; i < keys.size(); ++i) {
        QJsonObject k = keys.at(i).toObject();
        if (!keyIds.contains(k.value(QStringLiteral("id")).toString()))
            continue;
        if (sealObject(k, {QStringLiteral("privateKey"), QStringLiteral("passphrase")})) {
            keys.replace(i, k);
            changed = true;
        }
    }
    if (changed) {
        setSection(kHosts, hosts);
        setSection(kKeys, keys);
    }
    return changed;
}

bool AppController::hostNeedsMaster(const QJsonObject &host) const
{
    if (host.value(QStringLiteral("confirmMaster")).toBool() || isSealed(host))
        return true;
    if (isSealed(findIn(kKeys, host.value(QStringLiteral("keyId")).toString())))
        return true;
    const QJsonObject jump = findIn(kHosts, host.value(QStringLiteral("jumpHostId")).toString());
    return !jump.isEmpty() && (jump.value(QStringLiteral("confirmMaster")).toBool() || isSealed(jump)
                               || isSealed(findIn(kKeys, jump.value(QStringLiteral("keyId")).toString())));
}

AppController::Opened AppController::openFor(const QStringList &hostIds, const unsigned char *secret32) const
{
    Opened out;
    const QByteArray pk = protectPublicKey();
    auto openObj = [&](const QJsonObject &o) {
        const QJsonObject sealed = o.value(QStringLiteral("sealed")).toObject();
        const QString id = o.value(QStringLiteral("id")).toString();
        for (auto it = sealed.begin(); it != sealed.end(); ++it) {
            QByteArray plain;
            if (protect::open(it.value().toString().toLatin1(), pk, secret32, &plain)) {
                out.insert(id + QLatin1Char('/') + it.key(), QString::fromUtf8(plain));
                secure::wipe(plain);
            }
        }
    };
    for (const QString &hid : hostIds) {
        const QJsonObject h = findIn(kHosts, hid);
        const QJsonObject jump = findIn(kHosts, h.value(QStringLiteral("jumpHostId")).toString());
        for (const QJsonObject &x : {h, jump}) {
            if (x.isEmpty())
                continue;
            openObj(x);
            openObj(findIn(kKeys, x.value(QStringLiteral("keyId")).toString()));
        }
    }
    return out;
}

AppController::Opened AppController::openFor(const QStringList &hostIds, const QString &master, QString *error) const
{
    auto *sk = static_cast<unsigned char *>(sodium_malloc(protect::kSecretLen));
    if (!sk) {
        if (error) *error = tr("Not enough secure memory.");
        return {};
    }
    Opened out;
    if (deriveProtectSecret(master, sk))
        out = openFor(hostIds, sk);
    else if (error)
        *error = tr("Wrong master password.");
    sodium_free(sk);
    return out;
}

// A batch run with the master password: checks it (also when nothing is sealed
// yet) and opens the sealed secrets of these hosts.
bool AppController::openWithMaster(const QVariantList &hostIds, const QString &master, Opened *opened, QString *error) const
{
    QStringList ids;
    for (const QVariant &v : hostIds)
        ids << v.toString();
    if (protectPublicKey().size() != protect::kPublicLen) {
        if (m_vault.verifyPassword(master))
            return true;
        if (error) *error = tr("Wrong master password.");
        return false;
    }
    QString err;
    *opened = openFor(ids, master, &err);
    if (!err.isEmpty()) {
        if (error) *error = err;
        return false;
    }
    return true;
}

void AppController::wipe(Opened &o)
{
    for (auto it = o.begin(); it != o.end(); ++it)
        secure::wipe(it.value());
    o.clear();
}

QString AppController::secretOf(const QJsonObject &o, const QString &field, const Opened &opened) const
{
    const QString plain = o.value(field).toString();
    if (!plain.isEmpty() || !isSealed(o, field))
        return plain;
    return opened.value(o.value(QStringLiteral("id")).toString() + QLatin1Char('/') + field);
}

bool AppController::keyIsProtected(const QString &id) const
{
    return isSealed(findIn(kKeys, id));
}

QString AppController::unprotectHost(const QString &hostId, const QString &masterPassword)
{
    QJsonObject h = findIn(kHosts, hostId);
    if (h.isEmpty())
        return tr("This host no longer exists.");
    QString err;
    Opened opened = openFor({hostId}, masterPassword, &err);
    if (!err.isEmpty())
        return err;
    // the host's own secrets
    QJsonArray hosts = section(kHosts), keys = section(kKeys);
    auto unseal = [&opened](QJsonObject &o) {
        const QJsonObject sealed = o.value(QStringLiteral("sealed")).toObject();
        const QString id = o.value(QStringLiteral("id")).toString();
        for (auto it = sealed.begin(); it != sealed.end(); ++it)
            o.insert(it.key(), opened.value(id + QLatin1Char('/') + it.key()));
        o.remove(QStringLiteral("sealed"));
        o.remove(QStringLiteral("protected"));
    };
    const QString keyId = h.value(QStringLiteral("keyId")).toString();
    bool keyStillProtected = false;   // another host that asks uses the same key
    for (int i = 0; i < hosts.size(); ++i) {
        QJsonObject o = hosts.at(i).toObject();
        if (o.value(QStringLiteral("id")).toString() == hostId) {
            unseal(o);
            o.insert(QStringLiteral("confirmMaster"), false);
            hosts.replace(i, o);
        } else if (!keyId.isEmpty() && o.value(QStringLiteral("keyId")).toString() == keyId
                   && o.value(QStringLiteral("confirmMaster")).toBool()) {
            keyStillProtected = true;
        }
    }
    for (int i = 0; i < keys.size() && !keyStillProtected; ++i) {
        QJsonObject k = keys.at(i).toObject();
        if (k.value(QStringLiteral("id")).toString() == keyId && isSealed(k)) {
            unseal(k);
            keys.replace(i, k);
        }
    }
    wipe(opened);
    setSection(kHosts, hosts);
    setSection(kKeys, keys);
    if (!persist())
        return tr("Could not save the vault.");
    logEvent(ActivityLog::Security, QStringLiteral("host"), tr("Master password on connect disabled for host"), QString(),
             h.value(QStringLiteral("label")).toString());
    return {};
}

