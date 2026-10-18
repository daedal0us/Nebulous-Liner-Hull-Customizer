#pragma once

#include <QVector>
#include <QVector2D>
#include <QVector3D>

#include "nlhc/Export.h"

namespace nlhc {

struct NLHC_CORE_EXPORT Bounds
{
    QVector3D minimum;
    QVector3D maximum;
    bool valid = false;

    void expand(const QVector3D &point);
    void expand(const Bounds &other);
    QVector3D center() const;
    QVector3D size() const;
    float radius() const;
};

struct NLHC_CORE_EXPORT MeshData
{
    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    QVector<QVector2D> uvs;
    QVector<float> trim;
    QVector<quint32> indices;
    Bounds bounds;
};

} // namespace nlhc
