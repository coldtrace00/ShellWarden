#include "SessionListModel.h"
#include "TerminalSession.h"

int SessionListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant SessionListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size() || role != SessionRole)
        return {};
    return QVariant::fromValue(static_cast<QObject *>(m_items.at(index.row())));
}

QHash<int, QByteArray> SessionListModel::roleNames() const
{
    return {{SessionRole, "session"}};
}

void SessionListModel::append(TerminalSession *s)
{
    beginInsertRows({}, int(m_items.size()), int(m_items.size()));
    m_items.append(s);
    endInsertRows();
    emit countChanged();
}

TerminalSession *SessionListModel::takeAt(int row)
{
    if (row < 0 || row >= m_items.size())
        return nullptr;
    beginRemoveRows({}, row, row);
    TerminalSession *s = m_items.takeAt(row);
    endRemoveRows();
    emit countChanged();
    return s;
}

TerminalSession *SessionListModel::at(int row) const
{
    return row >= 0 && row < m_items.size() ? m_items.at(row) : nullptr;
}

void SessionListModel::move(int from, int to)
{
    if (from < 0 || from >= m_items.size() || to < 0 || to >= m_items.size() || from == to)
        return;
    // beginMoveRows wants the destination *before* which the row is inserted.
    if (!beginMoveRows({}, from, from, {}, to > from ? to + 1 : to))
        return;
    m_items.move(from, to);
    endMoveRows();
}
