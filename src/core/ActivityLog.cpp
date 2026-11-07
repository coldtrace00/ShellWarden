#include "ActivityLog.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>

namespace {
constexpr int kMaxInMemory = 5000;
constexpr int kRetentionMonths = 12;

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
} // namespace

ActivityLog::ActivityLog(QString dir, QObject *parent) : QAbstractListModel(parent), m_dir(std::move(dir))
{
    QDir().mkpath(m_dir);
    // retention
    const QDate cutoff = QDate::currentDate().addMonths(-kRetentionMonths);
    for (const QFileInfo &fi : QDir(m_dir).entryInfoList({QStringLiteral("activity-*.jsonl")}, QDir::Files)) {
        const QDate d = QDate::fromString(fi.baseName().mid(9) + QStringLiteral("-01"), QStringLiteral("yyyy-MM-dd"));
        if (d.isValid() && d < cutoff)
            QFile::remove(fi.absoluteFilePath());
    }
    loadRecent();
}

QString ActivityLog::currentFile() const
{
    return m_dir + QStringLiteral("/activity-") + QDate::currentDate().toString(QStringLiteral("yyyy-MM"))
           + QStringLiteral(".jsonl");
}

void ActivityLog::loadRecent()
{
    QFileInfoList files = QDir(m_dir).entryInfoList({QStringLiteral("activity-*.jsonl")}, QDir::Files, QDir::Name);
    // the two most recent months are enough for the UI
    while (files.size() > 2)
        files.removeFirst();
    for (const QFileInfo &fi : files) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly))
            continue;
        while (!f.atEnd()) {
            const QJsonObject o = QJsonDocument::fromJson(f.readLine()).object();
            if (o.isEmpty())
                continue;
            Entry e;
            e.time = QDateTime::fromString(o.value(QStringLiteral("t")).toString(), Qt::ISODate);
            e.level = levelFrom(o.value(QStringLiteral("l")).toString());
            e.category = o.value(QStringLiteral("c")).toString();
            e.message = o.value(QStringLiteral("m")).toString();
            e.detail = o.value(QStringLiteral("d")).toString();
            e.host = o.value(QStringLiteral("h")).toString();
            m_entries.append(e);
        }
    }
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

    QJsonObject o{
        {QStringLiteral("t"), e.time.toString(Qt::ISODateWithMs)},
        {QStringLiteral("l"), QString::fromLatin1(levelName(level))},
        {QStringLiteral("c"), category},
        {QStringLiteral("m"), message},
    };
    if (!detail.isEmpty())
        o.insert(QStringLiteral("d"), detail);
    if (!host.isEmpty())
        o.insert(QStringLiteral("h"), host);
    QFile f(currentFile());
    if (f.open(QIODevice::Append)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.write("\n");
    }

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
    for (const QFileInfo &fi : QDir(m_dir).entryInfoList({QStringLiteral("activity-*.jsonl")}, QDir::Files))
        QFile::remove(fi.absoluteFilePath());
    m_entries.clear();
    rebuildView();
}
