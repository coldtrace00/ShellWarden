#pragma once

#include <QByteArray>
#include <QString>

// Verifies minisign signatures (https://jedisct1.github.io/minisign/) with
// libsodium: Ed25519 over BLAKE2b-512 of the file ("ED", the default) or over
// the file itself ("Ed", legacy), plus the signed "trusted comment".
// Used by the updater: releases carry SHA256SUMS + SHA256SUMS.minisig.
namespace minisign {

// publicKey: the base64 line of minisign.pub ("RWQ…"), or the whole file
bool verify(const QByteArray &data, const QByteArray &sigFile, const QString &publicKey,
            QString *error, QString *trustedComment = nullptr);

} // namespace minisign
