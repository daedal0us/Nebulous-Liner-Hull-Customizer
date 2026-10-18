#pragma once

#include <QString>
#include <QStringList>

#include "nlhc/asset/HullPackage.h"
#include "nlhc/fleet/HullConfig.h"

namespace nlhc::detail {

inline const QStringList &segmentPieceIds(const HullTypeDef &type, int segment)
{
    switch (segment) {
    case 0:
        return type.bows;
    case 1:
        return type.cores;
    default:
        return type.sterns;
    }
}

// catalog piece ids referenced by a hull type, resolved to real pieces (order kept).
inline QVector<const Piece *> candidatePieces(const HullPackage &package, const QStringList &ids)
{
    QVector<const Piece *> pieces;
    pieces.reserve(ids.size());
    for (const QString &id : ids) {
        const Piece *piece = package.piece(id);
        if (piece && !pieces.contains(piece))
            pieces.append(piece);
    }
    return pieces;
}

// A .fleet stores piece keys (GUIDs); catalog hull types list piece ids. Also tolerates
// catalogs that list keys directly.
inline const Piece *findPieceByKey(const HullPackage &package, const QStringList &ids, const QString &key)
{
    if (key.isEmpty())
        return nullptr;
    const QVector<const Piece *> candidates = candidatePieces(package, ids);
    for (const Piece *piece : candidates) {
        if (piece->key.compare(key, Qt::CaseInsensitive) == 0 || piece->id.compare(key, Qt::CaseInsensitive) == 0)
            return piece;
    }
    return nullptr;
}

} // namespace nlhc::detail
