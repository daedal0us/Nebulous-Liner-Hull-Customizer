#pragma once

#include <QMatrix4x4>
#include <QVector2D>
#include <QVector3D>

#include "nlhc/asset/MeshData.h"

namespace nlhc {

class OrbitCamera
{
public:
    void frame(const Bounds &bounds);
    QMatrix4x4 view() const;
    QMatrix4x4 projection(float aspect) const;

    void orbit(float deltaYawDegrees, float deltaPitchDegrees);
    void setAngles(float yawDegrees, float pitchDegrees);
    void zoom(float factor);
    void pan(float dxPixels, float dyPixels, float viewportHeight);
    void reset();

    QVector3D target() const { return m_target; }

private:
    QVector3D m_target;
    float m_distance = 200.0f;
    float m_yaw = 35.0f;
    float m_pitch = 18.0f;
    float m_fov = 40.0f;
    QVector3D m_defaultsTarget;
    float m_defaultsDistance = 200.0f;
};

class SideCamera
{
public:
    void frame(const Bounds &bounds);
    QMatrix4x4 view() const;
    QMatrix4x4 projection(float aspect) const;

    void zoom(float factor);
    void pan(float dxPixels, float dyPixels, float viewportHeight);
    void reset();

private:
    QVector3D m_center;
    QVector2D m_extent = QVector2D(50.0f, 10.0f);
    float m_zoom = 1.0f;
    QVector2D m_pan;
};

} // namespace nlhc
