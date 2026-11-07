#include "FileNames.h"

#include <QRegularExpression>

namespace filenames {

QString safeLocal(const QString &remoteName)
{
    QString n;
    n.reserve(remoteName.size());
    for (const QChar c : remoteName) {
        const ushort u = c.unicode();
        const bool bad = u < 0x20 || u == 0x7f || (u >= 0x80 && u < 0xa0)   // C0, DEL, C1 controls
                         || QStringLiteral("<>:\"/\\|?*").contains(c)
                         || u == 0x202e || u == 0x202d || u == 0x2066 || u == 0x2067 || u == 0x2068   // bidi overrides:
                         || u == 0x202a || u == 0x202b || u == 0x202c || u == 0x2069;                 // "exe.txt" tricks
        n.append(bad ? QLatin1Char('_') : c);
    }
    // Windows ignores trailing dots and spaces ("a.bat." is "a.bat"), and leading spaces are confusing
    while (!n.isEmpty() && (n.endsWith(QLatin1Char('.')) || n.endsWith(QLatin1Char(' '))))
        n.chop(1);
    while (n.startsWith(QLatin1Char(' ')))
        n.remove(0, 1);
    if (n.isEmpty() || n == QLatin1String(".") || n == QLatin1String(".."))
        n = QStringLiteral("download");

    // CON, PRN, AUX, NUL, COM1-9, LPT1-9 — also with any extension ("nul.txt")
    static const QRegularExpression reserved(QStringLiteral("^(con|prn|aux|nul|com[0-9¹²³]|lpt[0-9¹²³])(\\..*)?$"),
                                             QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(n).hasMatch())
        n.prepend(QLatin1Char('_'));

    if (n.size() > 200) {
        const int dot = n.lastIndexOf(QLatin1Char('.'));
        const QString ext = dot > 0 && n.size() - dot <= 16 ? n.mid(dot) : QString();
        n = n.left(200 - ext.size()) + ext;
    }
    return n;
}

} // namespace filenames
