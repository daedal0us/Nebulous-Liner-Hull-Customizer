#pragma once

#include <QImage>
#include <QSize>
#include <QString>

#include "RenderStyle.h"
#include "nlhc/asset/HullPackage.h"
#include "nlhc/hull/HullAssembler.h"

namespace nlhc {

// Renders the split viewport (side + orbit) into an image without a window; used by
// the --render-demo CLI mode and by the render smoke test.
class OffscreenRenderer
{
public:
    static QImage renderSplit(const HullPackage *package, const AssemblyResult &assembly,
                              const RenderStyle &style, const QSize &size, QString *error);
};

} // namespace nlhc
