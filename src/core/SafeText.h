#pragma once
#include <QByteArray>
#include <QString>

// Text that comes from the other side (a proxy's reply, a server's disconnect
// or error message, a key file's header) and is put into our messages.
// It is shown as plain text everywhere, but it is also cut to a sane length
// and cleaned, so it cannot fill the screen, hide parts of the message
// (control and bidi characters) or look like markup ("<" and ">" become
// "‹" and "›").
namespace safetext {

inline QString snippet(const QString &in, int maxChars = 200)
{
    QString out;
    out.reserve(qMin<qsizetype>(in.size(), maxChars + 1));
    bool space = false;
    for (const QChar ch : in) {
        const char16_t u = ch.unicode();
        const bool bidi = (u >= 0x202A && u <= 0x202E) || (u >= 0x2066 && u <= 0x2069)
                          || u == 0x200E || u == 0x200F || u == 0x061C;
        const bool invisible = u == 0x200B || u == 0x200C || u == 0x200D || u == 0xFEFF || u == 0x00AD;
        if (ch.isSpace() || ch.category() == QChar::Other_Control) {
            space = !out.isEmpty();          // runs of whitespace / controls → one space
            continue;
        }
        if (bidi || invisible || ch.category() == QChar::Other_Format)
            continue;
        if (space) { out += QLatin1Char(' '); space = false; }
        if (u == '<') out += QChar(0x2039);
        else if (u == '>') out += QChar(0x203A);
        else out += ch;
        if (out.size() >= maxChars && !ch.isHighSurrogate()) {
            out += QChar(0x2026);
            break;
        }
    }
    return out;
}

inline QString snippet(const QByteArray &raw, int maxChars = 200)
{
    return snippet(QString::fromUtf8(raw), maxChars);
}

inline QString snippet(const char *raw, int maxChars = 200)
{
    return snippet(QString::fromUtf8(raw ? raw : ""), maxChars);
}

} // namespace safetext
