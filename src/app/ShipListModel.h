#pragma once

#include <QAbstractTableModel>

#include "nlhc/fleet/FleetDocument.h"

namespace nlhc {

class ShipListModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum class Role { ShipIndex = Qt::UserRole + 1, Editable };
    enum class Column { Index = 0, Name, HullType, Cost, Count };

    explicit ShipListModel(QObject *parent = nullptr);

    void setDocument(const FleetDocument *document);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void refresh();

private:
    const FleetDocument *m_document = nullptr;
};

} // namespace nlhc
