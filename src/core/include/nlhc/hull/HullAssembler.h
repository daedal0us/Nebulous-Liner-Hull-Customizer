#pragma once

#include <QMatrix4x4>
#include <QString>
#include <QVector>

#include "nlhc/Diagnostics.h"
#include "nlhc/Export.h"
#include "nlhc/asset/HullPackage.h"
#include "nlhc/asset/MeshData.h"
#include "nlhc/fleet/HullConfig.h"

namespace nlhc {

struct DrawItem
{
    const MeshData *mesh = nullptr;
    const MaterialDef *material = nullptr;
    QMatrix4x4 world;
};

struct AssemblyResult
{
    QVector<DrawItem> items;
    Bounds bounds;
    QVector<Diagnostic> diagnostics;
    bool renderable = false;
};

class NLHC_CORE_EXPORT HullAssembler
{
public:
    AssemblyResult assemble(const HullPackage &package, const QString &hullTypeId,
                            const RandomHullConfig &config) const;
};

} // namespace nlhc
