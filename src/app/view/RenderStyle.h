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
};

} // namespace nlhc
