#pragma once

#include <QByteArray>
#include <QString>
#include <sodium.h>

// Small helpers for handling secrets (passwords, passphrases, private keys).
// Qt containers are implicitly shared, so a wipe only affects the buffer the
// caller detaches; we always detach first to make sure we overwrite the bytes
// we actually own.
namespace secure {

inline void wipe(QByteArray &bytes)
{
    if (bytes.isEmpty())
        return;
    bytes.detach();
    sodium_memzero(bytes.data(), static_cast<size_t>(bytes.size()));
    bytes.clear();
}

inline void wipe(QString &text)
{
    if (text.isEmpty())
        return;
    text.detach();
    sodium_memzero(text.data(), static_cast<size_t>(text.size()) * sizeof(QChar));
    text.clear();
}

// RAII guard that wipes a QByteArray when it goes out of scope.
class ByteGuard
{
public:
    explicit ByteGuard(QByteArray &b) : m_bytes(b) {}
    ~ByteGuard() { wipe(m_bytes); }
    ByteGuard(const ByteGuard &) = delete;
    ByteGuard &operator=(const ByteGuard &) = delete;

private:
    QByteArray &m_bytes;
};

} // namespace secure
