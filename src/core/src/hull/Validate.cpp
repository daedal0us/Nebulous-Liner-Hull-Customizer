#include "nlhc/hull/Validate.h"

#include <QColor>
#include <QVector3D>

#include <utility>

#include "PieceLookup.h"

namespace nlhc {
namespace {

Diagnostic make(Severity severity, const char *code, const QString &detail, int segment = -1,
                int dressingIndex = -1)
{
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.code = QString::fromLatin1(code);
    diagnostic.detail = detail;
    diagnostic.segment = segment;
    diagnostic.dressingIndex = dressingIndex;
    return diagnostic;
}

// Highest choice a dressing point accepts: variantCount-1, plus one slot for "empty".
int highestChoice(const DressingPoint &point)
{
    const int variantCount = point.variants.size();
    return point.allowEmpty ? variantCount : variantCount - 1;
}

const Piece *primaryPiece(const HullPackage &package, const HullTypeDef &type, int segment, const QString &key)
{
    if (segment < 0 || segment >= RandomHullConfig::SegmentCount)
        return nullptr;
    return detail::findPieceByKey(package, detail::segmentPieceIds(type, segment), key);
}

} // namespace

QVector<Diagnostic> checkConfig(const HullPackage &package, const QString &hullTypeId,
                                const RandomHullConfig &config)
{
    QVector<Diagnostic> diagnostics;

    const HullTypeDef *type = package.hullType(hullTypeId);
    if (!type) {
        diagnostics.append(make(Severity::Error, diag::UnknownHullType, hullTypeId));
        return diagnostics;
    }
    if (!type->editable)
        diagnostics.append(make(Severity::Info, diag::UnsupportedHullType, hullTypeId));

    for (int segment = 0; segment < RandomHullConfig::SegmentCount; ++segment) {
        const SegmentConfig &segmentConfig = config.primary[size_t(segment)];
        const Piece *piece = primaryPiece(package, *type, segment, segmentConfig.key);
        if (!piece) {
            diagnostics.append(make(Severity::Error, diag::UnknownSegment,
                                    segmentConfig.key.isEmpty() ? QStringLiteral("no key") : segmentConfig.key,
                                    segment));
            continue;
        }

        const int pointCount = piece->dressingPoints.size();
        const int configured = segmentConfig.dressing.size();
        if (configured < pointCount) {
            diagnostics.append(make(Severity::Warning, diag::DressingLengthMismatch,
                                    QStringLiteral("%1 of %2 dressing points configured").arg(configured).arg(pointCount),
                                    segment));
        }
        const int limit = qMin(configured, pointCount);
        for (int point = 0; point < limit; ++point) {
            const int choice = segmentConfig.dressing.at(point);
            const int highest = highestChoice(piece->dressingPoints.at(point));
            if (choice < 0 || choice > highest) {
                diagnostics.append(make(Severity::Warning, diag::DressingOutOfRange,
                                        QStringLiteral("choice %1, valid 0..%2").arg(choice).arg(highest), segment,
                                        point));
            }
        }
    }

    for (const SecondaryConfig &entry : config.secondary) {
        const Piece *piece = detail::findPieceByKey(package, type->superstructures, entry.key);
        if (!piece) {
            diagnostics.append(make(Severity::Error, diag::UnknownSecondary,
                                    entry.key.isEmpty() ? QStringLiteral("no key") : entry.key));
            continue;
        }
        if (entry.segment < 0 || entry.segment >= RandomHullConfig::SegmentCount) {
            diagnostics.append(make(Severity::Error, diag::SecondarySegmentOutOfRange,
                                    QString::number(entry.segment)));
            continue;
        }
        const Piece *segmentPiece = primaryPiece(package, *type, entry.segment,
                                                 config.primary[size_t(entry.segment)].key);
        if (!segmentPiece)
            continue; // the segment problem is already reported above
        if (entry.snap < 0 || entry.snap >= segmentPiece->superstructureSnaps.size()) {
            diagnostics.append(make(Severity::Error, diag::SecondarySnapOutOfRange,
                                    QStringLiteral("%1 of %2 snap points")
                                        .arg(entry.snap)
                                        .arg(segmentPiece->superstructureSnaps.size()),
                                    entry.segment));
        }
    }

    return diagnostics;
}

bool repairConfig(const HullPackage &package, const QString &hullTypeId, RandomHullConfig *config)
{
    if (!config)
        return false;
    const HullTypeDef *type = package.hullType(hullTypeId);
    if (!type)
        return false;

    bool changed = false;

    for (int segment = 0; segment < RandomHullConfig::SegmentCount; ++segment) {
        SegmentConfig &segmentConfig = config->primary[size_t(segment)];
        const Piece *piece = primaryPiece(package, *type, segment, segmentConfig.key);
        if (!piece) {
            const QVector<const Piece *> candidates =
                detail::candidatePieces(package, detail::segmentPieceIds(*type, segment));
            if (candidates.isEmpty())
                continue;
            piece = candidates.first();
            segmentConfig.key = piece->key;
            changed = true;
        } else if (segmentConfig.key != piece->key) {
            segmentConfig.key = piece->key;
            changed = true;
        }

        const int pointCount = piece->dressingPoints.size();
        if (segmentConfig.dressing.size() > pointCount) {
            segmentConfig.dressing.resize(pointCount);
            changed = true;
        }
        while (segmentConfig.dressing.size() < pointCount) {
            segmentConfig.dressing.append(0);
            changed = true;
        }
        for (int point = 0; point < pointCount; ++point) {
            const int highest = highestChoice(piece->dressingPoints.at(point));
            const int current = segmentConfig.dressing.at(point);
            const int repaired = highest < 0 ? 0 : qBound(0, current, highest);
            if (repaired != current) {
                segmentConfig.dressing[point] = repaired;
                changed = true;
            }
        }
    }

    QVector<SecondaryConfig> kept;
    kept.reserve(config->secondary.size());
    for (SecondaryConfig entry : std::as_const(config->secondary)) {
        const Piece *piece = detail::findPieceByKey(package, type->superstructures, entry.key);
        if (!piece) { // the game drops unknown superstructures: do the same
            changed = true;
            continue;
        }
        if (entry.key != piece->key) {
            entry.key = piece->key;
            changed = true;
        }
        const int segment = qBound(0, entry.segment, RandomHullConfig::SegmentCount - 1);
        if (segment != entry.segment) {
            entry.segment = segment;
            changed = true;
        }
        const Piece *segmentPiece = detail::findPieceByKey(
            package, detail::segmentPieceIds(*type, entry.segment), config->primary[size_t(entry.segment)].key);
        const int snapCount = segmentPiece ? segmentPiece->superstructureSnaps.size() : 0;
        if (snapCount == 0) { // no mount point left: the entry cannot be loaded
            changed = true;
            continue;
        }
        const int snap = qBound(0, entry.snap, snapCount - 1);
        if (snap != entry.snap) {
            entry.snap = snap;
            changed = true;
        }
        kept.append(entry);
    }
    if (kept != config->secondary) {
        config->secondary = kept;
        changed = true;
    }

    return changed;
}

RandomHullConfig randomizeConfig(const HullPackage &package, const QString &hullTypeId, QRandomGenerator &rng)
{
    RandomHullConfig config;
    config.secondary.clear();

    const HullTypeDef *type = package.hullType(hullTypeId);
    if (!type)
        return config;

    for (int segment = 0; segment < RandomHullConfig::SegmentCount; ++segment) {
        SegmentConfig &segmentConfig = config.primary[size_t(segment)];
        const QVector<const Piece *> candidates =
            detail::candidatePieces(package, detail::segmentPieceIds(*type, segment));
        if (candidates.isEmpty())
            continue;
        const Piece *piece = candidates.at(int(rng.bounded(int(candidates.size()))));
        segmentConfig.key = piece->key;
        for (const DressingPoint &point : piece->dressingPoints) {
            const int highest = highestChoice(point);
            segmentConfig.dressing.append(highest <= 0 ? 0 : int(rng.bounded(highest + 1)));
        }
    }

    QVector<int> mountable;
    for (int segment = 0; segment < RandomHullConfig::SegmentCount; ++segment) {
        const Piece *piece = primaryPiece(package, *type, segment, config.primary[size_t(segment)].key);
        if (piece && !piece->superstructureSnaps.isEmpty())
            mountable.append(segment);
    }
    const QVector<const Piece *> secondaries = detail::candidatePieces(package, type->superstructures);
    if (!secondaries.isEmpty() && !mountable.isEmpty()) {
        SecondaryConfig entry;
        entry.key = secondaries.at(int(rng.bounded(int(secondaries.size()))))->key;
        entry.segment = mountable.at(int(rng.bounded(int(mountable.size()))));
        const Piece *piece = primaryPiece(package, *type, entry.segment, config.primary[size_t(entry.segment)].key);
        entry.snap = int(rng.bounded(int(piece->superstructureSnaps.size())));
        config.secondary.append(entry);
    }

    const float tint = float(0.35 + rng.generateDouble() * (0.8 - 0.35));
    config.hullTint = QColor::fromRgbF(tint, tint, tint, 1.0f);
    const auto variation = [&rng]() { return float(-1000.0 + rng.generateDouble() * 2000.0); };
    config.textureVariation = QVector3D(variation(), variation(), variation());
    return config;
}

} // namespace nlhc
