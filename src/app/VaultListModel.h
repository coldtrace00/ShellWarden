#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QJsonArray>
#include <QVariantMap>

// List model over one array of the vault ("hosts", "keys", "proxies").
// Secret fields are never exposed as roles; instead "has<Field>" roles tell
// QML whether a secret is stored (e.g. hasPassword, hasPassphrase).
// Transient values (proxy check results etc.) live next to the data and are
// never persisted.
class VaultListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    VaultListModel(QStringList roles, QStringList secretFields, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_items.size()); }
    void reset(const QJsonArray &items);
    void setTransient(const QString &id, const QVariantMap &values);
    QVariantMap transient(const QString &id) const { return m_transient.value(id); }

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int indexOf(const QString &id) const;

signals:
    void countChanged();

private:
    QStringList m_roles;
    QStringList m_secretFields;
    QJsonArray m_items;
    QHash<QString, QVariantMap> m_transient;
};
