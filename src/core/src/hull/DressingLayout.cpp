#include "nlhc/hull/DressingLayout.h"

#include <QHash>

#include <optional>

namespace nlhc {
namespace {

const Piece *pieceAt(const Piece *bow, const Piece *core, const Piece *stern, int segment)
{
    switch (segment) {
    case 0:
        return bow;
    case 1:
        return core;
    case 2:
        return stern;
    default:
        return nullptr;
    }
}

int highestChoice(const DressingPoint &point)
{
    const int variants = point.variants.size();
    return point.allowEmpty ? variants : variants - 1;
}

void appendSlots(QVector<DressingSlotRef> *slotRefs, const Piece *piece, int segment)
{
    if (!piece)
        return;
    for (int index = 0; index < piece->dressingPoints.size(); ++index)
        slotRefs->append({segment, index});
}

int slotPosition(const QVector<DressingSlotRef> &slotRefs, int owner, int pointIndex)
{
    for (int index = 0; index < slotRefs.size(); ++index) {
        if (slotRefs.at(index).ownerSegment == owner && slotRefs.at(index).pointIndex == pointIndex)
            return index;
    }
    return -1;
}

} // namespace

QVector<DressingSlotRef> dressingSlots(int segment, const Piece *bow, const Piece *core, const Piece *stern)
{
    QVector<DressingSlotRef> slotRefs;
    switch (segment) {
    case 0:
        appendSlots(&slotRefs, bow, 0);
        break;
    case 2:
        appendSlots(&slotRefs, stern, 2);
        break;
    case 1:
        // The core is the parent of both, so its array also carries their points, in
        // hierarchy order: own points first, then the bow, then the stern.
        appendSlots(&slotRefs, core, 1);
        appendSlots(&slotRefs, bow, 0);
        appendSlots(&slotRefs, stern, 2);
        break;
    default:
        break;
    }
    return slotRefs;
}

QVector<DressingBinding> bindDressing(const Piece *bow, const Piece *core, const Piece *stern,
                                      const RandomHullConfig &config)
{
    QVector<DressingBinding> bindings;
    const QVector<DressingSlotRef> coreSlots = dressingSlots(1, bow, core, stern);

    for (int owner = 0; owner < RandomHullConfig::SegmentCount; ++owner) {
        const Piece *piece = pieceAt(bow, core, stern, owner);
        if (!piece)
            continue;
        const QVector<DressingSlotRef> ownerSlots = dressingSlots(owner, bow, core, stern);
        const QVector<int> &ownerArray = config.primary[size_t(owner)].dressing;
        const QVector<int> &coreArray = config.primary[size_t(1)].dressing;

        for (int pointIndex = 0; pointIndex < piece->dressingPoints.size(); ++pointIndex) {
            DressingBinding binding;
            binding.ownerSegment = owner;
            binding.pointIndex = pointIndex;
            binding.point = &piece->dressingPoints.at(pointIndex);

            const int ownSlot = slotPosition(ownerSlots, owner, pointIndex);
            if (ownSlot >= 0)
                binding.storage.append({owner, ownSlot});
            const int coreSlot = slotPosition(coreSlots, owner, pointIndex);
            if (owner != 1 && coreSlot >= 0)
                binding.storage.append({1, coreSlot});

            // LoadConfiguration runs bow -> core -> stern, so for bow/stern points the core
            // array is the later write and therefore wins when it holds a value.
            std::optional<int> effective;
            if (owner != 1 && coreSlot >= 0 && coreSlot < coreArray.size())
                effective = coreArray.at(coreSlot);
            if (!effective.has_value() && ownSlot >= 0 && ownSlot < ownerArray.size())
                effective = ownerArray.at(ownSlot);

            binding.known = effective.has_value();
            binding.value = qBound(0, effective.value_or(0), qMax(0, highestChoice(*binding.point)));
            bindings.append(binding);
        }
    }
    return bindings;
}

bool normalizeDressing(const Piece *bow, const Piece *core, const Piece *stern, RandomHullConfig *config)
{
    if (!config)
        return false;

    bool changed = false;
    for (int segment = 0; segment < RandomHullConfig::SegmentCount; ++segment) {
        const QVector<DressingSlotRef> slotRefs = dressingSlots(segment, bow, core, stern);
        QVector<int> &array = config->primary[size_t(segment)].dressing;
        if (array.size() != slotRefs.size()) {
            array.resize(slotRefs.size());
            changed = true;
        }
    }

    for (const DressingBinding &binding : bindDressing(bow, core, stern, *config)) {
        for (const auto &location : binding.storage) {
            if (location.second >= config->primary[size_t(location.first)].dressing.size())
                continue;
            QVector<int> &target = config->primary[size_t(location.first)].dressing;
            if (target.at(location.second) != binding.value) {
                target[location.second] = binding.value;
                changed = true;
            }
        }
    }
    return changed;
}

void applyDressingValue(const DressingBinding &binding, int value, RandomHullConfig *config)
{
    if (!config)
        return;
    const int clamped = binding.point ? qBound(0, value, qMax(0, highestChoice(*binding.point))) : value;
    for (const auto &location : binding.storage) {
        QVector<int> &array = config->primary[size_t(location.first)].dressing;
        if (location.second >= array.size())
            array.resize(location.second + 1);
        array[location.second] = clamped;
    }
}

} // namespace nlhc