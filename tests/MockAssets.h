#pragma once

// Runtime-generated mock asset package shared by the core tests: a cube .nlmesh, a 1x1 PNG
// and a catalog.json with editable hull types.

#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector3D>
#include <QtTest>

#include <cstring>

#include "nlhc/fleet/HullConfig.h"

namespace nlhc::test {

inline QByteArray onePixelPng()
{
    const char *hex = "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c489"
                      "0000000d4944415478da63fccfc0500f000485018084a98c210000000049454e44ae426082";
    return QByteArray::fromHex(QByteArray(hex));
}

inline void writeBytes(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QFAIL(qPrintable(QStringLiteral("cannot write %1").arg(path)));
        return;
    }
    file.write(data);
}

inline QVector<QVector3D> cubeCorners(float half = 1.0f)
{
    return {QVector3D(-half, -half, -half), QVector3D(half, -half, -half), QVector3D(half, half, -half),
            QVector3D(-half, half, -half),   QVector3D(-half, -half, half), QVector3D(half, -half, half),
            QVector3D(half, half, half),     QVector3D(-half, half, half)};
}

inline QVector<quint32> cubeIndices()
{
    return {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 3, 7, 0, 7, 4,
            1, 5, 6, 1, 6, 2, 0, 4, 5, 0, 5, 1, 3, 2, 6, 3, 6, 7};
}

// .nlmesh v1: "NLHM" | version | flags | vertexCount | indexCount | positions | normals | uv0 | indices
inline QByteArray encodeMesh(const QVector<QVector3D> &positions, const QVector<quint32> &indices, quint32 flags = 3)
{
    QByteArray out;
    out.append("NLHM", 4);
    const auto putU32 = [&out](quint32 value) {
        out.append(char(value & 0xFF));
        out.append(char((value >> 8) & 0xFF));
        out.append(char((value >> 16) & 0xFF));
        out.append(char((value >> 24) & 0xFF));
    };
    const auto putF32 = [&putU32](float value) {
        quint32 bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        putU32(bits);
    };
    putU32(1);
    putU32(flags);
    putU32(quint32(positions.size()));
    putU32(quint32(indices.size()));
    for (const QVector3D &p : positions) {
        putF32(p.x());
        putF32(p.y());
        putF32(p.z());
    }
    if (flags & 1u) {
        for (const QVector3D &p : positions) {
            const QVector3D n = p.normalized();
            putF32(n.x());
            putF32(n.y());
            putF32(n.z());
        }
    }
    if (flags & 2u) {
        for (int i = 0; i < positions.size(); ++i) {
            putF32(0.0f);
            putF32(0.0f);
        }
    }
    if (flags & 4u) {
        for (quint32 index : indices)
            putU32(index);
    } else {
        for (quint32 index : indices) {
            out.append(char(index & 0xFF));
            out.append(char((index >> 8) & 0xFF));
        }
    }
    return out;
}

inline QByteArray cubeMesh()
{
    return encodeMesh(cubeCorners(), cubeIndices());
}

inline QString keyBow()
{
    return QStringLiteral("aaaaaaaa-0000-4000-8000-00000000000a");
}
inline QString keyCore()
{
    return QStringLiteral("aaaaaaaa-0000-4000-8000-00000000000b");
}
inline QString keyStern()
{
    return QStringLiteral("aaaaaaaa-0000-4000-8000-00000000000c");
}
inline QString keySuperstructure()
{
    return QStringLiteral("aaaaaaaa-0000-4000-8000-00000000000d");
}
inline QString keyDressingBow()
{
    return QStringLiteral("aaaaaaaa-0000-4000-8000-00000000000e");
}
inline QString keyDressingCore()
{
    return QStringLiteral("aaaaaaaa-0000-4000-8000-00000000000f");
}

inline QJsonArray vec3(float x, float y, float z)
{
    return QJsonArray{double(x), double(y), double(z)};
}

inline QJsonArray identityQuaternion()
{
    return QJsonArray{0.0, 0.0, 0.0, 1.0};
}

inline QJsonObject snap(float x, float y, float z)
{
    QJsonObject object;
    object[QStringLiteral("p")] = vec3(x, y, z);
    object[QStringLiteral("q")] = identityQuaternion();
    return object;
}

inline QJsonObject node(int parent, float x, float y, float z, const QString &mesh, const QString &material,
                        const QString &name)
{
    QJsonObject object;
    object[QStringLiteral("parent")] = parent;
    object[QStringLiteral("p")] = vec3(x, y, z);
    object[QStringLiteral("q")] = identityQuaternion();
    object[QStringLiteral("s")] = vec3(1.0f, 1.0f, 1.0f);
    object[QStringLiteral("mesh")] = mesh;
    object[QStringLiteral("material")] = material;
    object[QStringLiteral("name")] = name;
    return object;
}

inline void writeCatalog(const QString &rootDir, const QJsonArray &hullTypes, const QJsonArray &pieces,
                         const QJsonArray &materials, int formatVersion = 1)
{
    QJsonObject catalog;
    catalog[QStringLiteral("formatVersion")] = formatVersion;
    QJsonObject source;
    source[QStringLiteral("bundle")] = QStringLiteral("unittest");
    source[QStringLiteral("unity")] = QStringLiteral("mock");
    source[QStringLiteral("extractor")] = QStringLiteral("0.0.0");
    catalog[QStringLiteral("source")] = source;
    catalog[QStringLiteral("hullTypes")] = hullTypes;
    catalog[QStringLiteral("pieces")] = pieces;
    catalog[QStringLiteral("materials")] = materials;
    writeBytes(QDir(rootDir).filePath(QStringLiteral("catalog.json")),
               QJsonDocument(catalog).toJson(QJsonDocument::Indented));
}

// Two editable hull types:
//   "Stock/Bulk Hauler"  bow/core/stern with single cube nodes + one superstructure snap on the core
//   "Test/Dressing"      a bow with two dressing points (2 variants + 1 allowEmpty variant)
inline QString createMockPackage(const QString &rootDir)
{
    const QDir dir(rootDir);
    QDir().mkpath(rootDir);

    for (const QString &id : QStringList{QStringLiteral("bow"), QStringLiteral("core"), QStringLiteral("stern"),
                                         QStringLiteral("sup"), QStringLiteral("pdbow"), QStringLiteral("pdcore")}) {
        writeBytes(dir.filePath(QStringLiteral("mesh/%1.nlmesh").arg(id)), cubeMesh());
    }
    writeBytes(dir.filePath(QStringLiteral("albedo/paint.png")), onePixelPng());
    writeBytes(dir.filePath(QStringLiteral("mask/paint.png")), onePixelPng());

    const QString paint = QStringLiteral("t:paint");
    QJsonArray pieces;

    {
        QJsonObject piece;
        piece[QStringLiteral("id")] = QStringLiteral("p:bow");
        piece[QStringLiteral("key")] = keyBow();
        piece[QStringLiteral("debugName")] = QStringLiteral("Mock bow");
        piece[QStringLiteral("kind")] = QStringLiteral("primary");
        piece[QStringLiteral("foreSnap")] = snap(9.0f, 0.0f, 0.0f);
        piece[QStringLiteral("aftSnap")] = snap(5.0f, 0.0f, 0.0f);
        piece[QStringLiteral("superstructureSnaps")] = QJsonArray{snap(0.0f, 2.0f, 0.0f)};
        piece[QStringLiteral("dressingPoints")] = QJsonArray{};
        piece[QStringLiteral("nodes")] =
            QJsonArray{node(-1, 0.0f, 0.0f, 3.0f, QStringLiteral("m:bow"), paint, QStringLiteral("bow"))};
        piece[QStringLiteral("meshIds")] = QJsonArray{QStringLiteral("m:bow")};
        pieces.append(piece);
    }
    {
        QJsonObject piece;
        piece[QStringLiteral("id")] = QStringLiteral("p:core");
        piece[QStringLiteral("key")] = keyCore();
        piece[QStringLiteral("debugName")] = QStringLiteral("Mock core");
        piece[QStringLiteral("kind")] = QStringLiteral("primary");
        piece[QStringLiteral("foreSnap")] = snap(0.0f, 0.0f, 0.0f);
        piece[QStringLiteral("aftSnap")] = snap(-20.0f, 0.0f, 0.0f);
        piece[QStringLiteral("superstructureSnaps")] = QJsonArray{snap(0.0f, 5.0f, 0.0f)};
        piece[QStringLiteral("dressingPoints")] = QJsonArray{};
        piece[QStringLiteral("nodes")] =
            QJsonArray{node(-1, 0.0f, 0.0f, 3.0f, QStringLiteral("m:core"), paint, QStringLiteral("core"))};
        piece[QStringLiteral("meshIds")] = QJsonArray{QStringLiteral("m:core")};
        pieces.append(piece);
    }
    {
        QJsonObject piece;
        piece[QStringLiteral("id")] = QStringLiteral("p:stern");
        piece[QStringLiteral("key")] = keyStern();
        piece[QStringLiteral("debugName")] = QStringLiteral("Mock stern");
        piece[QStringLiteral("kind")] = QStringLiteral("primary");
        piece[QStringLiteral("foreSnap")] = snap(5.0f, 0.0f, 0.0f);
        piece[QStringLiteral("aftSnap")] = snap(-30.0f, 0.0f, 0.0f);
        piece[QStringLiteral("superstructureSnaps")] = QJsonArray{snap(0.0f, 0.0f, -8.0f)};
        piece[QStringLiteral("dressingPoints")] = QJsonArray{};
        piece[QStringLiteral("nodes")] =
            QJsonArray{node(-1, 0.0f, 0.0f, 3.0f, QStringLiteral("m:stern"), paint, QStringLiteral("stern"))};
        piece[QStringLiteral("meshIds")] = QJsonArray{QStringLiteral("m:stern")};
        pieces.append(piece);
    }
    {
        QJsonObject piece;
        piece[QStringLiteral("id")] = QStringLiteral("p:sup");
        piece[QStringLiteral("key")] = keySuperstructure();
        piece[QStringLiteral("debugName")] = QStringLiteral("Mock superstructure");
        piece[QStringLiteral("kind")] = QStringLiteral("secondary");
        piece[QStringLiteral("nodes")] =
            QJsonArray{node(-1, 0.0f, 0.0f, 0.0f, QStringLiteral("m:sup"), paint, QStringLiteral("sup"))};
        piece[QStringLiteral("meshIds")] = QJsonArray{QStringLiteral("m:sup")};
        pieces.append(piece);
    }
    {
        QJsonObject piece;
        piece[QStringLiteral("id")] = QStringLiteral("pd:bow");
        piece[QStringLiteral("key")] = keyDressingBow();
        piece[QStringLiteral("debugName")] = QStringLiteral("Mock dressing bow");
        piece[QStringLiteral("kind")] = QStringLiteral("primary");
        piece[QStringLiteral("foreSnap")] = snap(9.0f, 0.0f, 0.0f);
        piece[QStringLiteral("aftSnap")] = snap(5.0f, 0.0f, 0.0f);
        piece[QStringLiteral("superstructureSnaps")] = QJsonArray{};
        QJsonObject point0;
        point0[QStringLiteral("index")] = 0;
        point0[QStringLiteral("allowEmpty")] = false;
        point0[QStringLiteral("variants")] = QJsonArray{1, 2};
        point0[QStringLiteral("variantNames")] = QJsonArray{QStringLiteral("A"), QStringLiteral("B")};
        QJsonObject point1;
        point1[QStringLiteral("index")] = 1;
        point1[QStringLiteral("allowEmpty")] = true;
        point1[QStringLiteral("variants")] = QJsonArray{4};
        point1[QStringLiteral("variantNames")] = QJsonArray{QStringLiteral("E")};
        piece[QStringLiteral("dressingPoints")] = QJsonArray{point0, point1};
        piece[QStringLiteral("nodes")] =
            QJsonArray{node(-1, 0.0f, 0.0f, 3.0f, QString(), paint, QStringLiteral("root")),
                       node(0, 0.0f, 0.0f, 0.0f, QStringLiteral("m:pdbow"), paint, QStringLiteral("variant_a")),
                       node(0, 0.0f, 0.0f, 0.0f, QStringLiteral("m:pdbow"), paint, QStringLiteral("variant_b")),
                       node(2, 0.0f, 0.0f, 10.0f, QStringLiteral("m:pdbow"), paint, QStringLiteral("b_child")),
                       node(0, 0.0f, 0.0f, 0.0f, QStringLiteral("m:pdbow"), paint, QStringLiteral("variant_e"))};
        piece[QStringLiteral("meshIds")] = QJsonArray{QStringLiteral("m:pdbow")};
        pieces.append(piece);
    }
    {
        QJsonObject piece;
        piece[QStringLiteral("id")] = QStringLiteral("pd:core");
        piece[QStringLiteral("key")] = keyDressingCore();
        piece[QStringLiteral("debugName")] = QStringLiteral("Mock dressing core");
        piece[QStringLiteral("kind")] = QStringLiteral("primary");
        piece[QStringLiteral("foreSnap")] = snap(0.0f, 0.0f, 0.0f);
        piece[QStringLiteral("aftSnap")] = snap(-20.0f, 0.0f, 0.0f);
        piece[QStringLiteral("superstructureSnaps")] = QJsonArray{};
        piece[QStringLiteral("dressingPoints")] = QJsonArray{};
        piece[QStringLiteral("nodes")] = QJsonArray{
            node(-1, 0.0f, 0.0f, 3.0f, QStringLiteral("m:pdcore"), paint, QStringLiteral("dressing_core"))};
        piece[QStringLiteral("meshIds")] = QJsonArray{QStringLiteral("m:pdcore")};
        pieces.append(piece);
    }

    QJsonArray materials;
    {
        QJsonObject material;
        material[QStringLiteral("id")] = paint;
        material[QStringLiteral("paintable")] = true;
        material[QStringLiteral("baseColor")] = vec3(0.8f, 0.8f, 0.8f);
        material[QStringLiteral("albedo")] = QStringLiteral("albedo/paint.png");
        material[QStringLiteral("albedoMode")] = QStringLiteral("multiply");
        material[QStringLiteral("albedoMix")] = 0.0;
        material[QStringLiteral("mask")] = QStringLiteral("mask/paint.png");
        material[QStringLiteral("maskChannel")] = QStringLiteral("r");
        material[QStringLiteral("maskInvert")] = false;
        material[QStringLiteral("trimBaked")] = false;
        materials.append(material);
    }

    QJsonArray hullTypes;
    {
        QJsonObject hullType;
        hullType[QStringLiteral("id")] = QStringLiteral("Stock/Bulk Hauler");
        hullType[QStringLiteral("displayName")] = QStringLiteral("Bulk Hauler");
        hullType[QStringLiteral("role")] = QStringLiteral("marauder");
        hullType[QStringLiteral("prefabPath")] = QStringLiteral("Assets/Mock/Bulk Hauler.prefab");
        hullType[QStringLiteral("editable")] = true;
        hullType[QStringLiteral("bows")] = QJsonArray{QStringLiteral("p:bow")};
        hullType[QStringLiteral("cores")] = QJsonArray{QStringLiteral("p:core")};
        hullType[QStringLiteral("sterns")] = QJsonArray{QStringLiteral("p:stern")};
        hullType[QStringLiteral("superstructures")] = QJsonArray{QStringLiteral("p:sup")};
        hullTypes.append(hullType);
    }
    {
        QJsonObject hullType;
        hullType[QStringLiteral("id")] = QStringLiteral("Test/Dressing");
        hullType[QStringLiteral("displayName")] = QStringLiteral("Dressing Test");
        hullType[QStringLiteral("role")] = QStringLiteral("moorline");
        hullType[QStringLiteral("editable")] = true;
        hullType[QStringLiteral("bows")] = QJsonArray{QStringLiteral("pd:bow")};
        hullType[QStringLiteral("cores")] = QJsonArray{QStringLiteral("pd:core")};
        hullType[QStringLiteral("sterns")] = QJsonArray{QStringLiteral("p:stern")};
        hullType[QStringLiteral("superstructures")] = QJsonArray{QStringLiteral("p:sup")};
        hullTypes.append(hullType);
    }
    {
        // Read-only hull type that also references a piece the catalog does not define.
        QJsonObject hullType;
        hullType[QStringLiteral("id")] = QStringLiteral("Test/ReadOnly");
        hullType[QStringLiteral("displayName")] = QStringLiteral("Read Only");
        hullType[QStringLiteral("editable")] = false;
        hullType[QStringLiteral("bows")] = QJsonArray{QStringLiteral("p:missing")};
        hullType[QStringLiteral("cores")] = QJsonArray{QStringLiteral("p:core")};
        hullType[QStringLiteral("sterns")] = QJsonArray{QStringLiteral("p:stern")};
        hullType[QStringLiteral("superstructures")] = QJsonArray{};
        hullTypes.append(hullType);
    }

    writeCatalog(rootDir, hullTypes, pieces, materials);
    return rootDir;
}

inline RandomHullConfig bulkConfig()
{
    RandomHullConfig config;
    config.primary[0].key = keyBow();
    config.primary[1].key = keyCore();
    config.primary[2].key = keyStern();
    config.hullTint = QColor::fromRgbF(0.5f, 0.5f, 0.5f, 1.0f);
    config.textureVariation = QVector3D(1.0f, 2.0f, 3.0f);
    return config;
}

inline RandomHullConfig dressingConfig(const QVector<int> &dressing)
{
    RandomHullConfig config;
    config.primary[0].key = keyDressingBow();
    config.primary[0].dressing = dressing;
    config.primary[1].key = keyDressingCore();
    config.primary[2].key = keyStern();
    return config;
}


// ---------------------------------------------------------------------------------------------
// .fleet helpers
// ---------------------------------------------------------------------------------------------

inline QString defaultHullConfigBody()
{
    return QStringLiteral("        <HullConfig xsi:type=\"RandomHullConfiguration\">\n"
                          "          <PrimaryStructure>\n"
                          "            <SegmentConfiguration>\n"
                          "              <Key>%1</Key>\n"
                          "              <Dressing />\n"
                          "            </SegmentConfiguration>\n"
                          "            <SegmentConfiguration>\n"
                          "              <Key>%2</Key>\n"
                          "              <Dressing />\n"
                          "            </SegmentConfiguration>\n"
                          "            <SegmentConfiguration>\n"
                          "              <Key>%3</Key>\n"
                          "              <Dressing />\n"
                          "            </SegmentConfiguration>\n"
                          "          </PrimaryStructure>\n"
                          "          <SecondaryStructure />\n"
                          "          <HullTint>\n"
                          "            <r>0.5</r>\n"
                          "            <g>0.5</g>\n"
                          "            <b>0.5</b>\n"
                          "            <a>1</a>\n"
                          "          </HullTint>\n"
                          "          <TextureVariation>\n"
                          "            <x>1</x>\n"
                          "            <y>2</y>\n"
                          "            <z>3</z>\n"
                          "          </TextureVariation>\n"
                          "        </HullConfig>\n")
        .arg(keyBow(), keyCore(), keyStern());
}

inline QString shipKey(int index)
{
    return QStringLiteral("00000000-0000-4000-8000-%1").arg(index + 1, 12, 10, QLatin1Char('0'));
}

inline QString shipXml(int index, const QString &hullType, const QString &hullConfigXml,
                       const QString &extraShipXml = QString())
{
    return QStringLiteral("    <Ship>\n"
                          "      <Key>%1</Key>\n"
                          "      <Name>Ship %2</Name>\n"
                          "      <Cost>10</Cost>\n"
                          "      <Number>%2</Number>\n"
                          "      <SymbolOption>0</SymbolOption>\n"
                          "      <HullType>%3</HullType>\n"
                          "%4"
                          "      <SocketMap />\n"
                          "      <WeaponGroups />\n"
                          "%5"
                          "    </Ship>\n")
        .arg(shipKey(index))
        .arg(index + 1)
        .arg(hullType, hullConfigXml, extraShipXml);
}

inline QString fleetXml(const QVector<QString> &ships, const QString &extraRootXml = QString())
{
    QString xml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Fleet xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
        "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\n"
        "  <Name>Mock Fleet</Name>\n"
        "  <Version>3</Version>\n"
        "  <TotalPoints>10</TotalPoints>\n"
        "  <FactionKey>Stock/Protectorate</FactionKey>\n");
    xml += extraRootXml;
    xml += QStringLiteral("  <Ships>\n");
    for (const QString &ship : ships)
        xml += ship;
    xml += QStringLiteral("  </Ships>\n  <MissileTypes />\n  <CraftTypes />\n</Fleet>\n");
    return xml;
}

inline QString defaultFleetXml(int shipCount, const QString &hullType = QStringLiteral("Stock/Bulk Hauler"))
{
    QVector<QString> ships;
    ships.reserve(shipCount);
    for (int i = 0; i < shipCount; ++i)
        ships.append(shipXml(i, hullType, defaultHullConfigBody()));
    return fleetXml(ships);
}

} // namespace nlhc::test
