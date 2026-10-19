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
    // A failed .bak is not a failed save: `notice` carries the reason the copy was
    // skipped, so the caller can say so without treating the save as lost.
    bool save(const QString &path, QString *error, QString *notice = nullptr);
    // The exact bytes save() would write, for handing the file to another process.
    QByteArray serialized() const;
    void markSaved(const QString &path);

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

// Writes already serialized fleet bytes. Exposed so an elevated helper process can
// finish a save the application itself was denied, keeping the same .bak rules.
NLHC_CORE_EXPORT bool writeFleetFile(const QString &path, const QByteArray &payload,
                                     QString *error, QString *notice = nullptr);

} // namespace nlhc
