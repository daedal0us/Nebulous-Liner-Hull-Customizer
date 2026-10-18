#pragma once

#include <QString>

namespace nlhc {

struct ShipRecord
{
    int index = -1;
    QString key;
    QString name;
    QString hullType;
    int cost = 0;
    int number = 0;
    int symbolOption = 0;
    bool editable = false;
    QString readOnlyReason;
};

} // namespace nlhc
