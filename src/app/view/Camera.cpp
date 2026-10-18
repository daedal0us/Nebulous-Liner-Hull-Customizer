#include "Camera.h"

#include <QtMath>

namespace nlhc {
namespace {

float halfExtent(const Bounds &bounds)
{
    const QVector3D size = bounds.size();
    const float longest = qMax(size.z(), qMax(size.y(), size.x()));
    return longest > 0.0f ? longest : 100.0f;
}

} // namespace

void OrbitCamera::frame(const Bounds &bounds)
{
    m_target = bounds.center();
    m_distance = halfExtent(bounds) * 1.6f;
    m_yaw = 35.0f;
    m_pitch = 18.0f;
    m_defaultsTarget = m_target;
    m_defaultsDistance = m_distance;
}

QMatrix4x4 OrbitCamera::view() const
{
    const float yawRad = qDegreesToRadians(m_yaw);
    const float pitchRad = qDegreesToRadians(m_pitch);
    const QVector3D eye = m_target
        + QVector3D(qCos(pitchRad) * qSin(yawRad), qSin(pitchRad), qCos(pitchRad) * qCos(yawRad))
            * m_distance;
    QMatrix4x4 matrix;
    matrix.lookAt(eye, m_target, QVector3D(0.0f, 1.0f, 0.0f));
    return matrix;
}

QMatrix4x4 OrbitCamera::projection(float aspect) const
{
    QMatrix4x4 matrix;
    matrix.perspective(m_fov, aspect > 0.0f ? aspect : 1.0f, m_distance * 0.01f, m_distance * 12.0f + 1000.0f);
    return matrix;
}

void OrbitCamera::orbit(float deltaYawDegrees, float deltaPitchDegrees)
{
    m_yaw += deltaYawDegrees;
    m_pitch = qBound(-89.0f, m_pitch + deltaPitchDegrees, 89.0f);
}

void OrbitCamera::zoom(float factor)
{
    m_distance = qBound(1.0f, m_distance * factor, 10000.0f);
}

void OrbitCamera::pan(float dxPixels, float dyPixels, float viewportHeight)
{
    if (viewportHeight <= 0.0f)
        return;
    const float worldPerPixel = (2.0f * m_distance * qTan(qDegreesToRadians(m_fov) * 0.5f)) / viewportHeight;
    const float yawRad = qDegreesToRadians(m_yaw);
    const QVector3D right(qCos(yawRad), 0.0f, -qSin(yawRad));
    const QVector3D up = QVector3D::crossProduct(right, m_target - view().inverted().column(3).toVector3D());
    m_target -= right * (dxPixels * worldPerPixel);
    m_target += up.normalized() * (dyPixels * worldPerPixel);
}

void OrbitCamera::reset()
{
    m_target = m_defaultsTarget;
    m_distance = m_defaultsDistance;
    m_yaw = 35.0f;
    m_pitch = 18.0f;
}

void SideCamera::frame(const Bounds &bounds)
{
    m_center = bounds.center();
    const QVector3D size = bounds.size();
    m_extent = QVector2D(qMax(size.x(), size.y()) * 0.5f, size.z() * 0.5f);
    m_zoom = 1.0f;
    m_pan = QVector2D();
}

QMatrix4x4 SideCamera::view() const
{
    // Look along -X: the ship's long axis (+Z) maps to screen X, +Y stays up.
    const QVector3D eye = m_center + QVector3D(3000.0f, 0.0f, 0.0f);
    QMatrix4x4 matrix;
    matrix.lookAt(eye, m_center, QVector3D(0.0f, 1.0f, 0.0f));
    return matrix;
}

QMatrix4x4 SideCamera::projection(float aspect) const
{
    const float safeAspect = aspect > 0.0f ? aspect : 1.0f;
    float halfHeight = qMax(m_extent.x(), 1.0f) * 1.1f * m_zoom;
    float halfWidth = halfHeight * safeAspect;
    const float requiredWidth = qMax(m_extent.y(), 1.0f) * 1.1f * m_zoom;
    if (halfWidth < requiredWidth) {
        halfWidth = requiredWidth;
        halfHeight = halfWidth / safeAspect;
    }
    QMatrix4x4 matrix;
    matrix.ortho(-halfWidth + m_pan.x(), halfWidth + m_pan.x(),
                 -halfHeight + m_pan.y(), halfHeight + m_pan.y(),
                 -5000.0f, 5000.0f);
    return matrix;
}

void SideCamera::zoom(float factor)
{
    m_zoom = qBound(0.05f, m_zoom * factor, 50.0f);
}

void SideCamera::pan(float dxPixels, float dyPixels, float viewportHeight)
{
    if (viewportHeight <= 0.0f)
        return;
    const float worldPerPixel = (2.0f * qMax(m_extent.x(), 1.0f) * 1.1f * m_zoom) / viewportHeight;
    // Screen X is -Z, screen Y is +Y for the -X view direction.
    m_pan += QVector2D(-dxPixels * worldPerPixel, dyPixels * worldPerPixel);
}

void SideCamera::reset()
{
    m_zoom = 1.0f;
    m_pan = QVector2D();
}

} // namespace nlhc
