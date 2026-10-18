#pragma once

#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

#include "nlhc/Export.h"
#include "nlhc/asset/MeshData.h"
#include "nlhc/asset/Piece.h"

namespace nlhc {

// Reads assets/hulls/catalog.json plus the referenced mesh/texture blobs.
class NLHC_CORE_EXPORT HullPackage
{
public:
    HullPackage();
    ~HullPackage();

    bool load(const QString &rootDir, QString *error);
    bool isValid() const;
    QString rootDir() const;

    const QVector<HullTypeDef> &hullTypes() const;
    const QVector<Piece> &pieces() const;
    const QVector<MaterialDef> &materials() const;

    const HullTypeDef *hullType(const QString &id) const;
    const Piece *piece(const QString &id) const;
    const MaterialDef *material(const QString &id) const;
    const MeshData *mesh(const QString &id, QString *error) const;
    QImage texture(const QString &relativePath, QString *error) const;

    bool isEditableHullType(const QString &hullTypeId) const;
    QStringList warnings() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace nlhc
