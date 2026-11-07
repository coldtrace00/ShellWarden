#include "ActivityLog.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>

#include <sodium.h>

#include <algorithm>
#include <cstring>

namespace {
constexpr int kMaxInMemory = 5000;
const char *const kPubFile = "journal.pub";

const char *levelName(int l)
{
    switch (l) {
    case ActivityLog::Success: return "ok";
    case ActivityLog::Warning: return "warn";
    case ActivityLog::Error: return "error";
    case ActivityLog::Security: return "security";
    default: return "info";
    }
}
int levelFrom(const QString &s)
{
    if (s == QLatin1String("ok")) return ActivityLog::Success;
    if (s == QLatin1String("warn")) return ActivityLog::Warning;
    if (s == QLatin1String("error")) return ActivityLog::Error;
    if (s == QLatin1String("security")) return ActivityLog::Security;
    return ActivityLog::Info;
}

// "activity-2026-11.swlog" -> 2026-11-01
QDate monthOf(const QString &fileName)
{
    return QDate::fromString(fileName.mid(9, 7) + QStringLiteral("-01"), QStringLiteral("yyyy-MM-dd"));
}

QList<QByteArray> readLines(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QList<QByteArray> out;
    for (const QByteArray &l : f.readAll().split('\n'))
        if (!l.trimmed().isEmpty())
            out << l.trimmed();
    return out;
}

// Plain files of older versions: overwrite before deleting (best effort — an
// SSD or a copy-on-write file system may keep old blocks anyway).
void shred(const QString &path)
{
    QFile f(path);
    if (f.open(QIODevice::ReadWrite)) {
        const qint64 n = f.size();
        const QByteArray zeros(int(std::min<qint64>(n, 1 << 20)), '\0');
        for (qint64 done = 0; done < n; done += zeros.size())
            f.write(zeros.constData(), std::min<qint64>(zeros.size(), n - done));
        f.flush();
        f.close();
    }
    QFile::remove(path);
}
} // namespace

ActivityLog::ActivityLog(QString dir, QObject *parent) : QAbstractListModel(parent), m_dir(std::move(dir))
{
    QDir().mkpath(m_dir);
    QFile pub(m_dir + QLatin1Char('/') + QLatin1String(kPubFile));
    if (pub.open(QIODevice::ReadOnly)) {
        const QByteArray k = QByteArray::fromBase64(pub.readAll().trimmed());
        if (k.size() == crypto_box_PUBLICKEYBYTES)
            m_public = k;
    }
    // retention: the owner sets it (setRetentionDays) once settings are read
}

ActivityLog::~ActivityLog()
{
    close();
}

QString ActivityLog::fileFor(const QDate &d) const
{
    return m_dir + QStringLiteral("/activity-") + d.toString(QStringLiteral("yyyy-MM")) + QStringLiteral(".swlog");
}

QJsonObject ActivityLog::toJson(const Entry &e)
{
    QJsonObject o{
        {QStringLiteral("t"), e.time.toString(Qt::ISODateWithMs)},
        {QStringLiteral("l"), QString::fromLatin1(levelName(e.level))},
        {QStringLiteral("c"), e.category},
        {QStringLiteral("m"), e.message},
    };
    if (!e.detail.isEmpty())
        o.insert(QStringLiteral("d"), e.detail);
    if (!e.host.isEmpty())
        o.insert(QStringLiteral("h"), e.host);
    return o;
}

ActivityLog::Entry ActivityLog::fromJson(const QJsonObject &o)
{
    Entry e;
    e.time = QDateTime::fromString(o.value(QStringLiteral("t")).toString(), Qt::ISODate);
    e.level = levelFrom(o.value(QStringLiteral("l")).toString());
    e.category = o.value(QStringLiteral("c")).toString();
    e.message = o.value(QStringLiteral("m")).toString();
    e.detail = o.value(QStringLiteral("d")).toString();
    e.host = o.value(QStringLiteral("h")).toString();
    return e;
}

QByteArray ActivityLog::sealLine(const QByteArray &json, const QByteArray &publicKey)
{
    if (publicKey.size() != crypto_box_PUBLICKEYBYTES)
        return {};
    QByteArray c(json.size() + int(crypto_box_SEALBYTES), Qt::Uninitialized);
    if (crypto_box_seal(reinterpret_cast<unsigned char *>(c.data()), reinterpret_cast<const unsigned char *>(json.constData()),
                        quint64(json.size()), reinterpret_cast<const unsigned char *>(publicKey.constData())) != 0)
        return {};
    return c.toBase64();
}

QByteArray ActivityLog::openLine(const QByteArray &line, const QByteArray &publicKey, const unsigned char *secretKey)
{
    const QByteArray c = QByteArray::fromBase64(line);
    if (!secretKey || publicKey.size() != crypto_box_PUBLICKEYBYTES || c.size() <= int(crypto_box_SEALBYTES))
        return {};
    QByteArray m(c.size() - int(crypto_box_SEALBYTES), Qt::Uninitialized);
    if (crypto_box_seal_open(reinterpret_cast<unsigned char *>(m.data()), reinterpret_cast<const unsigned char *>(c.constData()),
                             quint64(c.size()), reinterpret_cast<const unsigned char *>(publicKey.constData()), secretKey) != 0)
        return {};
    return m;
}

void ActivityLog::appendToDisk(const QJsonObject &o, const QDateTime &t)
{
    const QByteArray line = sealLine(QJsonDocument(o).toJson(QJsonDocument::Compact), m_public);
    if (line.isEmpty())
        return;
    QFile f(fileFor(t.date()));
    if (f.open(QIODevice::Append)) {
        f.write(line);
        f.write("\n");
    }
}

void ActivityLog::setRetentionDays(int days)
{
    m_retentionDays = std::clamp(days, 1, 3650);
    applyRetention(m_secret != nullptr);   // whole old months go even while locked
    if (m_secret)
        loadRecent();
}

QString ActivityLog::open(const QByteArray &publicKey, const QByteArray &secretKey)
{
    if (publicKey.size() != crypto_box_PUBLICKEYBYTES || secretKey.size() != crypto_box_SECRETKEYBYTES)
        return tr("The journal key in the vault is damaged.");
    close();
    QString warning;
    if (!m_public.isEmpty() && m_public != publicKey)
        warning = tr("The journal's public key file was different from the one in the vault and has been restored. "
                     "Entries written meanwhile may not be readable.");
    if (m_public != publicKey) {
        QSaveFile pub(m_dir + QLatin1Char('/') + QLatin1String(kPubFile));
        if (pub.open(QIODevice::WriteOnly)) {
            pub.write(publicKey.toBase64() + "\n");
            pub.commit();
        }
        m_public = publicKey;
    }
    m_secret = static_cast<unsigned char *>(sodium_malloc(crypto_box_SECRETKEYBYTES));
    if (!m_secret)
        return tr("Not enough secure memory for the journal key.");
    std::memcpy(m_secret, secretKey.constData(), crypto_box_SECRETKEYBYTES);

    migratePlain();
    for (const auto &w : std::as_const(m_waiting))
        appendToDisk(w.second, w.first);
    m_waiting.clear();
    applyRetention(true);
    loadRecent();
    return warning;
}

void ActivityLog::close()
{
    if (m_secret) {
        sodium_free(m_secret);   // zeroes the memory before freeing it
        m_secret = nullptr;
    }
    m_entries.clear();   // what the journal says is not kept in memory while locked
    rebuildView();
}

void ActivityLog::migratePlain()
{
    for (const QFileInfo &fi : QDir(m_dir).entryInfoList({QStringLiteral("activity-*.jsonl")}, QDir::Files, QDir::Name)) {
        const QDate month = monthOf(fi.fileName());
        if (month.isValid()) {
            QByteArray out;
            for (const QByteArray &l : readLines(fi.absoluteFilePath())) {
                if (QJsonDocument::fromJson(l).object().isEmpty())
                    continue;
                out += sealLine(l, m_public) + "\n";
            }
            QFile f(fileFor(month));
            if (!out.isEmpty() && (!f.open(QIODevice::Append) || f.write(out) != out.size()))
                continue;   // could not write the encrypted copy: keep the old file for now
        }
        shred(fi.absoluteFilePath());
    }
}

void ActivityLog::applyRetention(bool withKey)
{
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-m_retentionDays);
    for (const QString &pattern : {QStringLiteral("activity-*.swlog"), QStringLiteral("activity-*.jsonl")}) {
        for (const QFileInfo &fi : QDir(m_dir).entryInfoList({pattern}, QDir::Files)) {
            const QDate month = monthOf(fi.fileName());
            if (!month.isValid())
                continue;
            if (month.addMonths(1) <= cutoff.date()) {   // the whole month is older
                if (pattern.endsWith(QLatin1String("jsonl")))
                    shred(fi.absoluteFilePath());
                else
                    QFile::remove(fi.absoluteFilePath());
                continue;
            }
            if (!withKey || !m_secret || month >= cutoff.date() || pattern.endsWith(QLatin1String("jsonl")))
                continue;
            // the month the limit falls in: keep only the newer entries
            QByteArray kept;
            bool dropped = false;
            for (const QByteArray &l : readLines(fi.absoluteFilePath())) {
                const QByteArray plain = openLine(l, m_public, m_secret);
                const QJsonObject o = QJsonDocument::fromJson(plain).object();
                const QDateTime t = QDateTime::fromString(o.value(QStringLiteral("t")).toString(), Qt::ISODate);
                if (!o.isEmpty() && t.isValid() && t < cutoff) {
                    dropped = true;
                    continue;
                }
                kept += l + "\n";   // newer, or not readable with this key: left alone
            }
            if (dropped) {
                QSaveFile f(fi.absoluteFilePath());
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(kept);
                    f.commit();
                }
            }
        }
    }
}

void ActivityLog::loadRecent()
{
    m_entries.clear();
    QFileInfoList files = QDir(m_dir).entryInfoList({QStringLiteral("activity-*.swlog")}, QDir::Files, QDir::Name);
    // the two most recent months are enough for the page
    while (files.size() > 2)
        files.removeFirst();
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-m_retentionDays);
    for (const QFileInfo &fi : files) {
        for (const QByteArray &l : readLines(fi.absoluteFilePath())) {
            const QJsonObject o = QJsonDocument::fromJson(openLine(l, m_public, m_secret)).object();
            if (o.isEmpty())
                continue;
            const Entry e = fromJson(o);
            if (e.time.isValid() && e.time >= cutoff)
                m_entries.append(e);
        }
    }
    std::stable_sort(m_entries.begin(), m_entries.end(), [](const Entry &a, const Entry &b) { return a.time < b.time; });
    if (m_entries.size() > kMaxInMemory)
        m_entries.remove(0, m_entries.size() - kMaxInMemory);
    rebuildView();
}

bool ActivityLog::matches(const Entry &e) const
{
    if (m_filter.isEmpty())
        return true;
    if (m_filter.startsWith(QLatin1String("cat:")))
        return e.category == m_filter.mid(4);
    if (m_filter == QLatin1String("level:problems"))
        return e.level >= Warning;
    return e.message.contains(m_filter, Qt::CaseInsensitive) || e.detail.contains(m_filter, Qt::CaseInsensitive)
           || e.host.contains(m_filter, Qt::CaseInsensitive);
}

void ActivityLog::rebuildView()
{
    beginResetModel();
    m_view.clear();
    for (int i = int(m_entries.size()) - 1; i >= 0; --i) {
        if (matches(m_entries.at(i)))
            m_view.append(i);
    }
    endResetModel();
    emit countChanged();
}

void ActivityLog::setFilter(const QString &f)
{
    if (f == m_filter)
        return;
    m_filter = f;
    rebuildView();
    emit filterChanged();
}

void ActivityLog::add(int level, const QString &category, const QString &message,
                      const QString &detail, const QString &host)
{
    if (!m_enabled)
        return;
    Entry e;
    e.time = QDateTime::currentDateTime();
    e.level = level;
    e.category = category;
    e.message = message;
    e.detail = detail;
    e.host = host;

    const QJsonObject o = toJson(e);
    if (m_public.isEmpty()) {   // very first start, before any vault exists
        if (m_waiting.size() < 500)
            m_waiting.append({e.time, o});
    } else {
        appendToDisk(o, e.time);
    }
    if (!m_secret)
        return;   // locked: written, but not shown or kept in memory

    m_entries.append(e);
    if (m_entries.size() > kMaxInMemory) {
        m_entries.removeFirst();
        rebuildView();
        return;
    }
    if (matches(e)) {
        beginInsertRows({}, 0, 0);
        m_view.prepend(int(m_entries.size()) - 1);
        endInsertRows();
        emit countChanged();
    }
}

int ActivityLog::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_view.size());
}

QVariant ActivityLog::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_view.size())
        return {};
    const Entry &e = m_entries.at(m_view.at(index.row()));
    switch (role) {
    case TimeRole: return e.time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    case LevelRole: return e.level;
    case CategoryRole: return e.category;
    case MessageRole: return e.message;
    case DetailRole: return e.detail;
    case HostRole: return e.host;
    default: return {};
    }
}

QHash<int, QByteArray> ActivityLog::roleNames() const
{
    return {{TimeRole, "time"}, {LevelRole, "level"}, {CategoryRole, "category"},
            {MessageRole, "message"}, {DetailRole, "detail"}, {HostRole, "host"}};
}

QString ActivityLog::exportTo(const QUrl &url) const
{
    if (!m_secret)
        return tr("Open the vault to export the journal.");
    QSaveFile f(url.toLocalFile());
    if (!f.open(QIODevice::WriteOnly))
        return f.errorString();
    f.write("\xEF\xBB\xBF"); // BOM so Excel opens Cyrillic correctly
    f.write("time;level;category;host;message;detail\r\n");
    auto esc = [](QString s) {
        s.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + s + QLatin1Char('"');
    };
    for (const Entry &e : m_entries) {
        const QString line = QStringList{e.time.toString(Qt::ISODate), QString::fromLatin1(levelName(e.level)),
                                         e.category, esc(e.host), esc(e.message), esc(e.detail)}
                                 .join(QLatin1Char(';'));
        f.write(line.toUtf8() + "\r\n");
    }
    return f.commit() ? QString() : f.errorString();
}

void ActivityLog::clearAll()
{
    for (const QFileInfo &fi : QDir(m_dir).entryInfoList({QStringLiteral("activity-*.swlog")}, QDir::Files))
        QFile::remove(fi.absoluteFilePath());
    for (const QFileInfo &fi : QDir(m_dir).entryInfoList({QStringLiteral("activity-*.jsonl")}, QDir::Files))
        shred(fi.absoluteFilePath());
    m_entries.clear();
    m_waiting.clear();
    rebuildView();
}
