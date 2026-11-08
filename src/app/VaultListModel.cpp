#include "VaultListModel.h"

#include <QJsonObject>

namespace {
QString hasRoleFor(const QString &field)
{
    return QStringLiteral("has") + field.left(1).toUpper() + field.mid(1);
}
} // namespace

VaultListModel::VaultListModel(QStringList roles, QStringList secretFields, QObject *parent)
    : QAbstractListModel(parent), m_roles(std::move(roles)), m_secretFields(std::move(secretFields))
{
    for (const QString &f : m_secretFields)
        m_roles << hasRoleFor(f);
}

int VaultListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant VaultListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const int r = role - Qt::UserRole - 1;
    if (r < 0 || r >= m_roles.size())
        return {};
    const QString &name = m_roles.at(r);
    const QJsonObject obj = m_items.at(index.row()).toObject();

    for (const QString &f : m_secretFields) {
        if (name == hasRoleFor(f))
            return !obj.value(f).toString().isEmpty();
    }
    const QString id = obj.value(QStringLiteral("id")).toString();
    const auto t = m_transient.constFind(id);
    if (t != m_transient.constEnd() && t->contains(name))
        return t->value(name);
    return obj.value(name).toVariant();
}

QHash<int, QByteArray> VaultListModel::roleNames() const
{
    QHash<int, QByteArray> h;
    for (int i = 0; i < m_roles.size(); ++i)
        h.insert(Qt::UserRole + 1 + i, m_roles.at(i).toUtf8());
    return h;
}

void VaultListModel::reset(const QJsonArray &items)
{
    beginResetModel();
    // Strip secrets: the model only ever holds what QML may display.
    QJsonArray safe;
    for (const QJsonValue &v : items) {
        QJsonObject o = v.toObject();
        const QJsonObject sealed = o.take(QStringLiteral("sealed")).toObject();   // protected secrets
        for (const QString &f : m_secretFields) {
            if (!o.value(f).toString().isEmpty() || sealed.contains(f))
                o.insert(f, QStringLiteral("x"));
        }
        if (!m_secretFields.contains(QStringLiteral("privateKey")))
            o.remove(QStringLiteral("privateKey"));   // never the key itself; "x" when it is a secret field
        safe.append(o);
    }
    m_items = safe;
    endResetModel();
    emit countChanged();
}

void VaultListModel::setTransient(const QString &id, const QVariantMap &values)
{
    QVariantMap &t = m_transient[id];
    for (auto it = values.begin(); it != values.end(); ++it)
        t.insert(it.key(), it.value());
    const int row = indexOf(id);
    if (row >= 0)
        emit dataChanged(index(row), index(row));
}

QVariantMap VaultListModel::get(int row) const
{
    QVariantMap m;
    if (row < 0 || row >= m_items.size())
        return m;
    const QModelIndex idx = index(row);
    for (int i = 0; i < m_roles.size(); ++i)
        m.insert(m_roles.at(i), data(idx, Qt::UserRole + 1 + i));
    return m;
}

int VaultListModel::indexOf(const QString &id) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).toObject().value(QStringLiteral("id")).toString() == id)
            return i;
    }
    return -1;
}
