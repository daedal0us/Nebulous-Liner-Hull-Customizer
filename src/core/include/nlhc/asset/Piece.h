#pragma once

#include <QColor>
#include <QQuaternion>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector3D>

#include "nlhc/Export.h"

namespace nlhc {

struct Transform
{
    QVector3D translation;
    QQuaternion rotation;
};

struct PieceNode
{
    int parent = -1;
    QVector3D translation;
    QQuaternion rotation;
    QVector3D scale = QVector3D(1.0f, 1.0f, 1.0f);
    QString meshId;
    QString materialId;
    QString name;
};

struct DressingPoint
{
    int index = 0;
    bool allowEmpty = false;
    QVector<int> variants;
    QStringList variantNames;
};

enum class PieceKind { Primary, Secondary };

struct Piece
{
    QString id;
    QString key;
    QString debugName;
    PieceKind kind = PieceKind::Primary;
    Transform foreSnap;
    Transform aftSnap;
    QVector<Transform> superstructureSnaps;
    QVector<DressingPoint> dressingPoints;
    QVector<PieceNode> nodes;
    QStringList meshIds;
};

struct MaterialDef
{
    QString id;
    bool paintable = true;
    QColor baseColor = QColor::fromRgbF(1.0f, 1.0f, 1.0f);
    QString albedoPath;
    QString albedoMode = QStringLiteral("multiply");
    float albedoMix = 0.0f;
    QString maskPath;
    QString maskChannel = QStringLiteral("r");
    bool maskInvert = false;
    bool trimBaked = false;
    QString normalPath;
    float normalStrength = 1.0f;
    // _nameplateDecal: white markings (hull numbers, lettering) laid over the paint.
    QString decalPath;
    QString decalMode = QStringLiteral("luminance");
    // HDRP _MaskMap: R = metallic, A = smoothness. Metals (the brass doodads) are what make a
    // fixed prop read as metal instead of flat plastics.
    float metallic = 0.0f;
    float smoothness = 0.5f;
};

struct HullTypeDef
{
    QString id;
    QString displayName;
    QString role;
    QString prefabPath;
    bool editable = false;
    QStringList bows;
    QStringList cores;
    QStringList sterns;
    QStringList superstructures;
    // piece id -> (node index -> material id). A shared piece can bind different materials per
    // hull type: the game stores the material on the hull type's segment instance, not on the
    // piece asset, so Bulk Hauler and Container Hauler reuse one mesh with different paint.
    QHash<QString, QHash<int, QString>> nodeMaterials;
};

} // namespace nlhc
