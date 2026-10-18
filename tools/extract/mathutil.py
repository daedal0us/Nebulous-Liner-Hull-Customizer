"""Small quaternion / matrix helpers (Unity conventions: q = x, y, z, w)."""

from __future__ import annotations

import math

IDENTITY_QUAT = (0.0, 0.0, 0.0, 1.0)
ZERO = (0.0, 0.0, 0.0)


def quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def quat_conjugate(q):
    return (-q[0], -q[1], -q[2], q[3])


def quat_normalize(q):
    length = math.sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3])
    if length < 1e-12:
        return IDENTITY_QUAT
    return (q[0] / length, q[1] / length, q[2] / length, q[3] / length)


def quat_rotate(q, v):
    """Rotate vector v by unit quaternion q."""
    x, y, z, w = q
    vx, vy, vz = v
    tx = 2.0 * (y * vz - z * vy)
    ty = 2.0 * (z * vx - x * vz)
    tz = 2.0 * (x * vy - y * vx)
    return (vx + w * tx + (y * tz - z * ty),
            vy + w * ty + (z * tx - x * tz),
            vz + w * tz + (x * ty - y * tx))


def quat_axis_angle(axis, radians):
    half = radians * 0.5
    s = math.sin(half)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half))


def transform_point(position, rotation, scale, point):
    scaled = (point[0] * scale[0], point[1] * scale[1], point[2] * scale[2])
    rotated = quat_rotate(rotation, scaled)
    return (position[0] + rotated[0], position[1] + rotated[1],
            position[2] + rotated[2])


def transform_normal(rotation, scale, normal):
    """Inverse transpose of the linear part, then normalised."""
    sx = 1.0 / scale[0] if scale[0] else 0.0
    sy = 1.0 / scale[1] if scale[1] else 0.0
    sz = 1.0 / scale[2] if scale[2] else 0.0
    scaled = (normal[0] * sx, normal[1] * sy, normal[2] * sz)
    rotated = quat_rotate(rotation, scaled)
    length = math.sqrt(rotated[0] ** 2 + rotated[1] ** 2 + rotated[2] ** 2)
    if length < 1e-12:
        return (0.0, 0.0, 0.0)
    return (rotated[0] / length, rotated[1] / length, rotated[2] / length)


def matrix_of(position, rotation, scale):
    """Row major 4x4 as nested tuples, for debugging / tests."""
    x, y, z, w = rotation
    m00 = (1 - 2 * (y * y + z * z)) * scale[0]
    m01 = 2 * (x * y - z * w) * scale[0]
    m02 = 2 * (x * z + y * w) * scale[0]
    m10 = 2 * (x * y + z * w) * scale[1]
    m11 = (1 - 2 * (x * x + z * z)) * scale[1]
    m12 = 2 * (y * z - x * w) * scale[1]
    m20 = 2 * (x * z - y * w) * scale[2]
    m21 = 2 * (y * z + x * w) * scale[2]
    m22 = (1 - 2 * (x * x + y * y)) * scale[2]
    return ((m00, m01, m02, position[0]),
            (m10, m11, m12, position[1]),
            (m20, m21, m22, position[2]),
            (0.0, 0.0, 0.0, 1.0))
