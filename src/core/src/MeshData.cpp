#include "nlhc/asset/MeshData.h"

#include <QVector3D>

namespace nlhc {

void Bounds::expand(const QVector3D &point)
{
    if (!valid) {
        minimum = point;
        maximum = point;
        valid = true;
        return;
    }
    minimum.setX(qMin(minimum.x(), point.x()));
    minimum.setY(qMin(minimum.y(), point.y()));
    minimum.setZ(qMin(minimum.z(), point.z()));
    maximum.setX(qMax(maximum.x(), point.x()));
    maximum.setY(qMax(maximum.y(), point.y()));
    maximum.setZ(qMax(maximum.z(), point.z()));
}

void Bounds::expand(const Bounds &other)
{
    if (!other.valid)
        return;
    expand(other.minimum);
    expand(other.maximum);
}

QVector3D Bounds::center() const
{
    if (!valid)
        return QVector3D();
    return (minimum + maximum) * 0.5f;
}

QVector3D Bounds::size() const
{
    if (!valid)
        return QVector3D();
    return maximum - minimum;
}

float Bounds::radius() const
{
    if (!valid)
        return 0.0f;
    return size().length() * 0.5f;
}

} // namespace nlhc
