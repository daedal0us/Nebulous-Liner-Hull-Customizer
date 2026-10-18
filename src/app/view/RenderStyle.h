#pragma once

#include <QColor>
#include <QVector3D>

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
    // The game paints a per-ship procedural camouflage field instead of a flat stripe
    // mask: the boundary between the two paint colours is a cellular field seeded by the
    // ship's TextureVariation and evaluated in object space (OSP_Hull_Proc, _PositionOffset).
    bool camoPaint = true;
    QVector3D camoOffset;
    float camoScale = 1.6f;       // cells per metre
    float camoSplit = 0.55f;      // hash above which a cell takes the trim colour
    float albedoDetail = 0.55f;   // how strongly the albedo modulates the paint
    float decalStrength = 0.85f;  // near-white albedo pixels kept as white markings
    float exposure = 1.35f;       // lifts the paint to the brightness the game shows
};

} // namespace nlhc
