#include "PasswordTools.h"

#include <QSet>
#include <cmath>
#include <sodium.h>

namespace passwordtools {

namespace {

const QString kLower = QStringLiteral("abcdefghijkmnpqrstuvwxyz");
const QString kUpper = QStringLiteral("ABCDEFGHJKLMNPQRSTUVWXYZ");
const QString kDigits = QStringLiteral("23456789");
const QString kSymbols = QStringLiteral("!#$%&*+-=?@^_~.:;,()[]{}");

QChar pick(const QString &set)
{
    return set.at(int(randombytes_uniform(uint32_t(set.size()))));
}

const QStringList &commonPasswords()
{
    static const QStringList list = {
        QStringLiteral("password"), QStringLiteral("123456"), QStringLiteral("12345678"), QStringLiteral("qwerty"),
        QStringLiteral("111111"), QStringLiteral("123123"), QStringLiteral("abc123"), QStringLiteral("iloveyou"),
        QStringLiteral("admin"), QStringLiteral("welcome"), QStringLiteral("monkey"), QStringLiteral("dragon"),
        QStringLiteral("letmein"), QStringLiteral("football"), QStringLiteral("master"), QStringLiteral("root"),
        QStringLiteral("toor"), QStringLiteral("passw0rd"), QStringLiteral("qwertyuiop"), QStringLiteral("1q2w3e4r"),
        QStringLiteral("zaq12wsx"), QStringLiteral("asdfgh"), QStringLiteral("princess"), QStringLiteral("sunshine"),
        QStringLiteral("changeme"), QStringLiteral("secret"), QStringLiteral("default"), QStringLiteral("p@ssw0rd"),
        QStringLiteral("йцукен"), QStringLiteral("пароль"), QStringLiteral("qazwsx"), QStringLiteral("trustno1"),
    };
    return list;
}

const QStringList &keyboardRows()
{
    static const QStringList rows = {
        QStringLiteral("qwertyuiop"), QStringLiteral("asdfghjkl"), QStringLiteral("zxcvbnm"),
        QStringLiteral("1234567890"), QStringLiteral("йцукенгшщзхї"), QStringLiteral("фівапролджє"),
        QStringLiteral("ячсмитьбю"), QStringLiteral("abcdefghijklmnopqrstuvwxyz"),
    };
    return rows;
}

} // namespace

QString generate(int length, bool symbols)
{
    length = std::max(length, kGeneratedMinLength);
    const QString all = kLower + kUpper + kDigits + (symbols ? kSymbols : QString());

    QString out;
    out.reserve(length);
    out += pick(kLower);
    out += pick(kUpper);
    out += pick(kDigits);
    if (symbols)
        out += pick(kSymbols);
    while (out.size() < length)
        out += pick(all);

    // Fisher–Yates shuffle so the guaranteed classes are not at the front.
    for (int i = int(out.size()) - 1; i > 0; --i) {
        const int j = int(randombytes_uniform(uint32_t(i + 1)));
        const QChar t = out[i];
        out[i] = out[j];
        out[j] = t;
    }
    return out;
}

Strength evaluate(const QString &pw)
{
    Strength s;
    if (pw.isEmpty()) {
        s.issues << QStringLiteral("empty");
        return s;
    }

    bool lower = false, upper = false, digit = false, symbol = false, other = false;
    for (const QChar ch : pw) {
        if (ch.isLower()) lower = true;
        else if (ch.isUpper()) upper = true;
        else if (ch.isDigit()) digit = true;
        else if (ch.unicode() < 0x80) symbol = true;
        else other = true;
    }
    int pool = 0;
    if (lower) pool += 26;
    if (upper) pool += 26;
    if (digit) pool += 10;
    if (symbol) pool += 32;
    if (other) pool += 40;
    pool = std::max(pool, 10);

    // Effective length: runs of the same char and straight sequences count less.
    const QString low = pw.toLower();
    double effective = 0;
    for (int i = 0; i < low.size(); ++i) {
        double w = 1.0;
        if (i > 0 && low[i] == low[i - 1])
            w = 0.25;
        else if (i > 0 && std::abs(low[i].unicode() - low[i - 1].unicode()) == 1)
            w = 0.4;
        effective += w;
    }
    for (const QString &row : keyboardRows()) {
        for (int len = 4; len <= row.size(); ++len) {
            for (int start = 0; start + len <= row.size(); ++start) {
                if (low.contains(row.mid(start, len))) {
                    effective -= len * 0.6;
                    s.issues << QStringLiteral("pattern");
                    start = row.size();
                    len = row.size() + 1;
                }
            }
        }
    }
    for (const QString &common : commonPasswords()) {
        if (low.contains(common)) {
            effective -= common.size() * 0.8;
            s.issues << QStringLiteral("common");
            break;
        }
    }
    const QSet<QChar> unique(pw.begin(), pw.end());
    if (unique.size() <= pw.size() / 3)
        s.issues << QStringLiteral("repeats");

    effective = std::max(1.0, effective);
    s.bits = effective * std::log2(double(pool));

    if (pw.size() < kGeneratedMinLength)
        s.issues << QStringLiteral("short");
    int classes = int(lower) + int(upper) + int(digit) + int(symbol || other);
    if (classes < 3)
        s.issues << QStringLiteral("classes");

    if (s.bits < 40) s.score = 0;
    else if (s.bits < 60) s.score = 1;
    else if (s.bits < 80) s.score = 2;
    else if (s.bits < 100) s.score = 3;
    else s.score = 4;
    if (s.issues.contains(QStringLiteral("common")))
        s.score = std::min(s.score, 1);

    s.reliable = pw.size() >= kGeneratedMinLength && s.score >= 3;
    s.issues.removeDuplicates();
    return s;
}

} // namespace passwordtools
