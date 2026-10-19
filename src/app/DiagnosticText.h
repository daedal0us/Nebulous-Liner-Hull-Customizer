#pragma once

#include <QString>

#include "nlhc/Diagnostics.h"
#include "nlhc/asset/HullPackage.h"
#include "nlhc/fleet/FleetDocument.h"

namespace nlhc {

QString diagnosticText(const Diagnostic &diagnostic);
QString segmentName(int segment);
QString readOnlyReasonText(const ShipRecord &ship, const HullPackage &package);

} // namespace nlhc
