#include "Minisign.h"

#include <QCoreApplication>
#include <QStringList>
#include <sodium.h>

namespace minisign {

namespace {
QString tr(const char *s) { return QCoreApplication::translate("Minisign", s); }

QByteArray keyLine(const QString &publicKey)
{
    // accept the bare key or the whole minisign.pub file ("untrusted comment: …\nRWQ…")
    for (const QString &l : publicKey.split(QLatin1Char('\n'))) {
        const QString t = l.trimmed();
        if (!t.isEmpty() && !t.startsWith(QLatin1String("untrusted comment:")))
            return t.toLatin1();
    }
    return {};
}
} // namespace

bool verify(const QByteArray &data, const QByteArray &sigFile, const QString &publicKey,
            QString *error, QString *trustedComment)
{
    auto fail = [error](const QString &e) { if (error) *error = e; return false; };
    if (sodium_init() < 0)
        return fail(tr("libsodium failed to start."));

    const QByteArray pk = QByteArray::fromBase64(keyLine(publicKey));
    if (pk.size() != 2 + 8 + 32 || pk.left(2) != "Ed")
        return fail(tr("The built-in public key is not a minisign key."));

    const QList<QByteArray> lines = sigFile.split('\n');
    if (lines.size() < 4 || !lines.at(0).startsWith("untrusted comment:") || !lines.at(2).startsWith("trusted comment: "))
        return fail(tr("The signature file is damaged."));
    const QByteArray sig = QByteArray::fromBase64(lines.at(1).trimmed());
    const QByteArray comment = lines.at(2).mid(17).trimmed();   // after "trusted comment: "
    const QByteArray globalSig = QByteArray::fromBase64(lines.at(3).trimmed());
    if (sig.size() != 2 + 8 + 64 || globalSig.size() != 64)
        return fail(tr("The signature file is damaged."));
    if (sig.mid(2, 8) != pk.mid(2, 8))
        return fail(tr("The release was signed with a different key."));

    const auto *pkRaw = reinterpret_cast<const unsigned char *>(pk.constData() + 10);
    QByteArray message;
    if (sig.left(2) == "ED") {
        message.resize(crypto_generichash_BYTES_MAX);   // 64
        crypto_generichash(reinterpret_cast<unsigned char *>(message.data()), size_t(message.size()),
                           reinterpret_cast<const unsigned char *>(data.constData()), size_t(data.size()), nullptr, 0);
    } else if (sig.left(2) == "Ed") {
        message = data;
    } else {
        return fail(tr("Unknown signature algorithm."));
    }
    if (crypto_sign_verify_detached(reinterpret_cast<const unsigned char *>(sig.constData() + 10),
                                    reinterpret_cast<const unsigned char *>(message.constData()),
                                    size_t(message.size()), pkRaw) != 0)
        return fail(tr("Wrong signature: the file was changed after it was signed."));

    // the trusted comment is signed too: signature || comment
    const QByteArray global = sig.mid(10, 64) + comment;
    if (crypto_sign_verify_detached(reinterpret_cast<const unsigned char *>(globalSig.constData()),
                                    reinterpret_cast<const unsigned char *>(global.constData()),
                                    size_t(global.size()), pkRaw) != 0)
        return fail(tr("Wrong signature of the trusted comment."));
    if (trustedComment)
        *trustedComment = QString::fromUtf8(comment);
    return true;
}

} // namespace minisign
