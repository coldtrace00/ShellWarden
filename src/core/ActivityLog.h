#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QVector>

// Action journal: who connected where, which route and IP were used, what
// changed in the vault, when the kill switch fired. Never contains passwords,
// passphrases or private keys.
//
// Stored as JSON lines in <data>/logs/activity-YYYY-MM.jsonl (one file per
// month, older than `retentionMonths` are deleted).
class ActivityLog : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(QString directory READ directory CONSTANT)
public:
    enum Level { Info = 0, Success = 1, Warning = 2, Error = 3, Security = 4 };
    Q_ENUM(Level)
    enum Roles { TimeRole = Qt::UserRole + 1, LevelRole, CategoryRole, MessageRole, DetailRole, HostRole };

    struct Entry
    {
        QDateTime time;
        int level = Info;
        QString category;   // vault, session, proxy, killswitch, key, host, sftp, tunnel, update, settings
        QString message;
        QString detail;
        QString host;
    };

    explicit ActivityLog(QString dir, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_view.size()); }
    QString filter() const { return m_filter; }
    void setFilter(const QString &f);
    QString directory() const { return m_dir; }

    void add(int level, const QString &category, const QString &message,
             const QString &detail = {}, const QString &host = {});

    Q_INVOKABLE QString exportTo(const QUrl &url) const;
    Q_INVOKABLE void clearAll();
    void setEnabled(bool on) { m_enabled = on; }

signals:
    void countChanged();
    void filterChanged();

private:
    void loadRecent();
    void rebuildView();
    QString currentFile() const;
    bool matches(const Entry &e) const;

    QString m_dir;
    QVector<Entry> m_entries;   // newest last, capped
    QVector<int> m_view;        // indices into m_entries, newest first
    QString m_filter;
    bool m_enabled = true;
};
