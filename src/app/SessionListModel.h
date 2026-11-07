#pragma once

#include <QAbstractListModel>
#include <QPointer>
#include <QVector>

class TerminalSession;

// Open terminal tabs. A real model (not a plain list property) so that adding
// or closing a tab never recreates the other tabs' terminal views.
class SessionListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles { SessionRole = Qt::UserRole + 1 };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_items.size()); }
    void append(TerminalSession *s);
    TerminalSession *takeAt(int row);
    void move(int from, int to);
    Q_INVOKABLE TerminalSession *at(int row) const;
    const QVector<TerminalSession *> &items() const { return m_items; }

signals:
    void countChanged();

private:
    QVector<TerminalSession *> m_items;
};
