#pragma once

#include <QString>

// Pasting into a terminal is typing: a hidden ESC sequence or line break in
// copied text runs commands. Text copied from a web page can contain
// "\e[201~", which ends bracketed paste early so what follows runs at once.
namespace pasteguard {

// Removes every control character except tab and line breaks: ESC (so no
// escape sequence survives), the other C0 codes, DEL and the C1 range
// (U+0080..U+009F, which some programs read as 8-bit escape codes).
// Line breaks become CR, as terminals expect.
inline QString clean(const QString &in)
{
    QString t = in;
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\r"));
    t.replace(QLatin1Char('\n'), QLatin1Char('\r'));
    QString out;
    out.reserve(t.size());
    for (const QChar c : t) {
        const ushort u = c.unicode();
        if (u == '\t' || u == '\r' || (u >= 0x20 && u != 0x7f && !(u >= 0x80 && u <= 0x9f)))
            out.append(c);
    }
    return out;
}

// More than one line (a line break anywhere runs what is before it) or very long.
inline bool needsConfirm(const QString &cleaned)
{
    return cleaned.contains(QLatin1Char('\r')) || cleaned.size() > 5000;
}

inline int lineCount(const QString &cleaned)
{
    QString t = cleaned;
    while (t.endsWith(QLatin1Char('\r')))
        t.chop(1);
    return int(t.count(QLatin1Char('\r'))) + 1;
}

} // namespace pasteguard
