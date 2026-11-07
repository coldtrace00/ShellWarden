#pragma once

#include <QString>
#include <QStringList>

namespace passwordtools {

constexpr int kGeneratedMinLength = 19;   // generated passwords are never shorter
constexpr int kManualMinLength = 8;       // hand-typed minimum (with a warning below 19)

// Cryptographically random password (libsodium randombytes_uniform, no modulo
// bias). Always contains lower, upper, digit and — if `symbols` — a symbol.
// Visually ambiguous characters (0 O o 1 l I |) are excluded.
QString generate(int length = 24, bool symbols = true);

struct Strength
{
    int score = 0;        // 0 very weak … 4 strong
    double bits = 0;      // estimated entropy
    QStringList issues;   // short machine-readable reasons
    bool reliable = false;
};

// Conservative estimate: character pool × length, minus penalties for
// repeats, sequences, keyboard patterns and very common passwords.
Strength evaluate(const QString &password);

} // namespace passwordtools
