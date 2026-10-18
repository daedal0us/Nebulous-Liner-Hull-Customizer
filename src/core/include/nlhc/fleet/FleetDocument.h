#pragma once

#include <QString>
#include <QVector>

#include <memory>
#include <optional>

#include "nlhc/Diagnostics.h"
#include "nlhc/Export.h"
#include "nlhc/asset/HullPackage.h"
#include "nlhc/fleet/HullConfig.h"
#include "nlhc/fleet/ShipRecord.h"

namespace nlhc {

// Non-destructive .fleet editor: unknown XML survives a load/save round trip.
class NLHC_CORE_EXPORT FleetDocument
{
public:
    FleetDocument();
    ~FleetDocument();

    bool load(const QString &path, QVector<Diagnostic> *diagnostics, QString *error);
    bool save(const QString &path, QString *error);

    bool isDirty() const;
    QString filePath() const;
    QString fleetName() const;
    QString factionKey() const;
    int fleetVersion() const;
    int shipCount() const;

    // Package is not owned; it is only consulted to decide whether a ship is editable.
    void setPackage(const HullPackage *package);
    const HullPackage *package() const;

    ShipRecord ship(int index) const;

    bool isRandomHullConfig(int index) const;
    std::optional<RandomHullConfig> hullConfig(int index) const;
    bool setHullConfig(int index, const RandomHullConfig &config);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace nlhc
