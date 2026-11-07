#include "FirewallLock.h"

#include <QCoreApplication>
#include <QDir>
#include <QHostAddress>
#include <QHostInfo>
#include <QObject>
#include <QProcess>
#include <QSet>

#include <algorithm>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <shellapi.h>
#endif

namespace firewalllock {

namespace {

[[maybe_unused]] QString ruleName()
{
    return QCoreApplication::applicationName() + QStringLiteral(" kill switch");
}

QString ipv4(quint32 v) { return QHostAddress(v).toString(); }

// Complement of the allowed IPv4 addresses (and 127.0.0.0/8) as netsh ranges.
[[maybe_unused]] QStringList blockedV4(QList<quint32> allowed)
{
    allowed.append(0x7F000000u);       // 127.0.0.0 … 127.255.255.255 are kept open
    std::sort(allowed.begin(), allowed.end());
    QStringList ranges;
    quint64 start = 0;
    auto emitRange = [&](quint64 from, quint64 to) {
        if (from <= to)
            ranges << ipv4(quint32(from)) + QLatin1Char('-') + ipv4(quint32(to));
    };
    for (quint32 a : allowed) {
        quint64 lo = a, hi = a;
        if (a == 0x7F000000u)
            hi = 0x7FFFFFFFu;
        if (lo > start)
            emitRange(start, lo - 1);
        start = std::max<quint64>(start, hi + 1);
    }
    emitRange(start, 0xFFFFFFFFull);
    return ranges;
}

[[maybe_unused]] QStringList blockedV6(const QList<QHostAddress> &allowed)
{
    // Simple and strict: everything except ::1 and the proxies' IPv6 is blocked.
    // Proxies with IPv6 addresses get exact holes via two ranges around them.
    QStringList r;
    if (allowed.isEmpty()) {
        r << QStringLiteral("::2-ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
        return r;
    }
    // Keep it readable: block "::2 … before first", "after last … ffff…" and
    // the gaps in between, using QHostAddress arithmetic on 128-bit values.
    QList<Q_IPV6ADDR> list;
    for (const QHostAddress &a : allowed)
        list << a.toIPv6Address();
    std::sort(list.begin(), list.end(), [](const Q_IPV6ADDR &x, const Q_IPV6ADDR &y) {
        return std::lexicographical_compare(x.c, x.c + 16, y.c, y.c + 16);
    });
    auto step = [](Q_IPV6ADDR a, int delta) {
        for (int i = 15; i >= 0; --i) {
            const int v = int(a.c[i]) + delta;
            if (v >= 0 && v <= 255) { a.c[i] = quint8(v); break; }
            a.c[i] = quint8(delta > 0 ? 0 : 255);
        }
        return a;
    };
    Q_IPV6ADDR start{};
    start.c[15] = 2;
    for (const Q_IPV6ADDR &a : list) {
        const Q_IPV6ADDR before = step(a, -1);
        if (!std::lexicographical_compare(before.c, before.c + 16, start.c, start.c + 16))
            r << QHostAddress(start).toString() + QLatin1Char('-') + QHostAddress(before).toString();
        start = step(a, +1);
    }
    r << QHostAddress(start).toString() + QStringLiteral("-ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
    return r;
}

#ifdef Q_OS_WIN
bool runElevated(const QString &cmdLine, QString *error)
{
    std::wstring params = (QStringLiteral("/c ") + cmdLine).toStdWString();
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof sei;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) {
        if (error)
            *error = QObject::tr("Administrator rights were not granted.");
        return false;
    }
    WaitForSingleObject(sei.hProcess, 60000);
    DWORD code = 1;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    if (code != 0 && error)
        *error = QObject::tr("netsh failed (code %1).").arg(code);
    return code == 0;
}
#endif

} // namespace

QStringList blockedRanges(const QList<QHostAddress> &allowed)
{
    QList<quint32> v4;
    QList<QHostAddress> v6;
    for (const QHostAddress &a : allowed) {
        if (a.protocol() == QAbstractSocket::IPv4Protocol)
            v4 << a.toIPv4Address();
        else
            v6 << a;
    }
    return blockedV4(v4) + blockedV6(v6);
}

bool supported()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool isActive()
{
#ifdef Q_OS_WIN
    QProcess p;
    p.start(QStringLiteral("netsh"), {QStringLiteral("advfirewall"), QStringLiteral("firewall"), QStringLiteral("show"),
                                      QStringLiteral("rule"), QStringLiteral("name=") + ruleName()});
    p.waitForFinished(8000);
    return p.exitCode() == 0 && p.readAllStandardOutput().contains(ruleName().toLocal8Bit());
#else
    return false;
#endif
}

QStringList allowedAddresses()
{
    // Reconstructed from the rule is fragile; the caller keeps its own copy.
    return {};
}

bool apply(const QStringList &proxyHosts, QString *error)
{
#ifdef Q_OS_WIN
    QList<quint32> v4;
    QList<QHostAddress> v6;
    QSet<QString> seen;
    for (const QString &h : proxyHosts) {
        QList<QHostAddress> addrs;
        QHostAddress direct;
        if (direct.setAddress(h))
            addrs << direct;
        else
            addrs = QHostInfo::fromName(h).addresses();
        if (addrs.isEmpty()) {
            if (error)
                *error = QObject::tr("Cannot resolve proxy %1.").arg(h);
            return false;
        }
        for (const QHostAddress &a : addrs) {
            if (seen.contains(a.toString()))
                continue;
            seen.insert(a.toString());
            if (a.protocol() == QAbstractSocket::IPv4Protocol)
                v4 << a.toIPv4Address();
            else
                v6 << a;
        }
    }
    if (v4.isEmpty() && v6.isEmpty()) {
        if (error)
            *error = QObject::tr("Add at least one proxy first.");
        return false;
    }
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    QList<QHostAddress> all;
    for (quint32 a : v4) all << QHostAddress(a);
    all += v6;
    const QString ranges = blockedRanges(all).join(QLatin1Char(','));
    const QString name = ruleName();
    const QString cmd =
        QStringLiteral("netsh advfirewall firewall delete rule name=\"%1\" >nul 2>&1 & "
                       "netsh advfirewall firewall add rule name=\"%1\" dir=out action=block enable=yes "
                       "profile=any program=\"%2\" remoteip=%3")
            .arg(name, exe, ranges);
    return runElevated(cmd, error);
#else
    Q_UNUSED(proxyHosts);
    if (error)
        *error = QObject::tr("The firewall lock is available on Windows only.");
    return false;
#endif
}

bool remove(QString *error)
{
#ifdef Q_OS_WIN
    return runElevated(QStringLiteral("netsh advfirewall firewall delete rule name=\"%1\"").arg(ruleName()), error);
#else
    if (error)
        *error = QObject::tr("The firewall lock is available on Windows only.");
    return false;
#endif
}

} // namespace firewalllock
