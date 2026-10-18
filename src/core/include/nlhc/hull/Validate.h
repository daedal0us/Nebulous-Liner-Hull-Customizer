#pragma once

#include <QRandomGenerator>
#include <QString>
#include <QVector>

#include "nlhc/Diagnostics.h"
#include "nlhc/Export.h"
#include "nlhc/asset/HullPackage.h"
#include "nlhc/fleet/HullConfig.h"

namespace nlhc {

NLHC_CORE_EXPORT QVector<Diagnostic> checkConfig(const HullPackage &package, const QString &hullTypeId,
                                                 const RandomHullConfig &config);

// Brings a config into a shape the game can load without crashing. Returns true when
// anything changed. Callers must treat this as an explicit, user-initiated action.
NLHC_CORE_EXPORT bool repairConfig(const HullPackage &package, const QString &hullTypeId,
                                   RandomHullConfig *config);

// Mirrors the game's randomisation: one random piece per segment, one random
// superstructure, tint in [0.35, 0.8], texture variation in [-1000, 1000].
NLHC_CORE_EXPORT RandomHullConfig randomizeConfig(const HullPackage &package, const QString &hullTypeId,
                                                  QRandomGenerator &rng);

} // namespace nlhc
