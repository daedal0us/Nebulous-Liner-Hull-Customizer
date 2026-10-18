#pragma once

#include <QPair>
#include <QVector>

#include "nlhc/Export.h"
#include "nlhc/asset/Piece.h"
#include "nlhc/fleet/HullConfig.h"

namespace nlhc {

// One dressing point of the resolved assembly, together with every place its value is
// stored in RandomHullConfig. The game calls LoadConfiguration(bow), then (core), then
// (stern) - and by then bow and stern are children of the core, so the core's array also
// covers their points. Values are written to every storage slot to stay self-consistent.
struct DressingBinding
{
    int ownerSegment = 1;              // piece that owns the point: 0 bow, 1 core, 2 stern
    int pointIndex = 0;                // index inside that piece's dressingPoints
    const DressingPoint *point = nullptr;
    QVector<QPair<int, int>> storage;  // (segment array, index) pairs holding this value
    int value = 0;                     // value currently in effect
    bool known = false;                // false when the owning piece is unresolved
};

// Fixed layout for one segment: which points the segment array must contain, in order.
struct DressingSlotRef
{
    int ownerSegment = 1;
    int pointIndex = 0;
};

NLHC_CORE_EXPORT QVector<DressingSlotRef> dressingSlots(int segment, const Piece *bow, const Piece *core,
                                                        const Piece *stern);

NLHC_CORE_EXPORT QVector<DressingBinding> bindDressing(const Piece *bow, const Piece *core, const Piece *stern,
                                                       const RandomHullConfig &config);

// Resizes every segment array to the resolved layout and clamps out-of-range values.
// Returns true when anything changed.
NLHC_CORE_EXPORT bool normalizeDressing(const Piece *bow, const Piece *core, const Piece *stern,
                                        RandomHullConfig *config);

// Writes value into every storage slot of the binding.
NLHC_CORE_EXPORT void applyDressingValue(const DressingBinding &binding, int value, RandomHullConfig *config);

} // namespace nlhc
