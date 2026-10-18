#include "ShipListModel.h"

namespace nlhc {

ShipListModel::ShipListModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void ShipListModel::setDocument(const FleetDocument *document)
{
    beginResetModel();
    m_document = document;
    endResetModel();
}

void ShipListModel::refresh()
{
    beginResetModel();
    endResetModel();
}

int ShipListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_document)
        return 0;
    return m_document->shipCount();
}

int ShipListModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(Column::Count);
}

QVariant ShipListModel::data(const QModelIndex &index, int role) const
{
    if (!m_document || !index.isValid() || index.row() >= m_document->shipCount())
        return {};

    const ShipRecord ship = m_document->ship(index.row());

    if (role == int(Role::ShipIndex))
        return index.row();
    if (role == int(Role::Editable))
        return ship.editable;

    if (role == Qt::DisplayRole) {
        switch (Column(index.column())) {
        case Column::Index:
            return index.row() + 1;
        case Column::Name:
            return ship.name;
        case Column::HullType: {
            const int slash = ship.hullType.lastIndexOf(QLatin1Char('/'));
            return slash >= 0 ? ship.hullType.mid(slash + 1) : ship.hullType;
        }
        case Column::Cost:
            return ship.cost;
        case Column::Count:
            return ship.number;
        default:
            break;
        }
    }

    if (role == Qt::ToolTipRole) {
        if (ship.editable)
            return ship.hullType;
        return ship.readOnlyReason.isEmpty() ? ship.hullType : ship.readOnlyReason;
    }

    if (role == Qt::ForegroundRole && !ship.editable && index.column() == int(Column::Name))
        return QColor(0x8a, 0x8f, 0x98);

    return {};
}

QVariant ShipListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    switch (Column(section)) {
    case Column::Index:
        return QStringLiteral("#");
    case Column::Name:
        return tr("Name");
    case Column::HullType:
        return tr("Hull type");
    case Column::Cost:
        return tr("Cost");
    case Column::Count:
        return tr("Count");
    default:
        return {};
    }
}

} // namespace nlhc
