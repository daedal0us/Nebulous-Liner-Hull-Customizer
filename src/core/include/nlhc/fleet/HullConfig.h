#pragma once

#include <QColor>
#include <QString>
#include <QVector>
#include <QVector3D>

#include <array>

#include "nlhc/Export.h"

namespace nlhc {

struct SegmentConfig
{
    QString key;
    QVector<int> dressing;

    bool operator==(const SegmentConfig &other) const = default;
};

struct SecondaryConfig
{
    QString key;
    int segment = 1;
    int snap = 0;

    bool operator==(const SecondaryConfig &other) const = default;
};

// Mirrors the game's modular hull randomisation rules.
struct RandomHullConfig
{
    static constexpr int SegmentCount = 3;
    enum SegmentRole { Bow = 0, Core = 1, Stern = 2 };

    std::array<SegmentConfig, SegmentCount> primary;
    QVector<SecondaryConfig> secondary;
    QColor hullTint = QColor::fromRgbF(0.6f, 0.6f, 0.6f, 1.0f);
    QVector3D textureVariation;

    bool operator==(const RandomHullConfig &other) const = default;
};

} // namespace nlhc
