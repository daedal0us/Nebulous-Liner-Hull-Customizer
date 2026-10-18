#pragma once

#include <QColor>

namespace nlhc {

struct RenderStyle
{
    QColor baseColor = QColor(0x1b, 0x40, 0x5b);
    QColor trimColor = QColor(0xf2, 0x65, 0x22);
    bool applyHullTint = false;
    QColor hullTint = QColor(153, 153, 153);
    bool outline = true;
    bool showGrid = false;

    // Colour model for livery surfaces (see HullRenderer's fragment shader).
    float albedoDetail = 0.55f;   // how strongly the albedo modulates the paint
    float decalStrength = 0.85f;  // near-white albedo pixels kept as white markings
    float exposure = 1.35f;       // lifts the paint to the brightness the game shows
};

} // namespace nlhc
