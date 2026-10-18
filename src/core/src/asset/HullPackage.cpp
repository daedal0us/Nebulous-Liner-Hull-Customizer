#include "nlhc/asset/HullPackage.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QtEndian>

#include <cstring>

namespace nlhc {
namespace {

constexpr int kSupportedFormatVersion = 1;
constexpr char kMeshMagic[4] = {'N', 'L', 'H', 'M'};

constexpr quint32 kMeshFlagNormals = 1u << 0;
constexpr quint32 kMeshFlagUv0 = 1u << 1;
constexpr quint32 kMeshFlagIndex32 = 1u << 2;
constexpr quint32 kMeshFlagVertexTrim = 1u << 3;

QString quote(const QString &value)
{
    return QStringLiteral("'%1'").arg(value);
}

bool toVector3D(const QJsonValue &value, QVector3D *out)
{
    const QJsonArray array = value.toArray();
    if (array.size() != 3)
        return false;
    *out = QVector3D(float(array.at(0).toDouble()), float(array.at(1).toDouble()),
                     float(array.at(2).toDouble()));
    return true;
}

bool toQuaternion(const QJsonValue &value, QQuaternion *out)
{
    const QJsonArray array = value.toArray();
    if (array.size() != 4)
        return false;
    *out = QQuaternion(float(array.at(3).toDouble()), float(array.at(0).toDouble()),
                       float(array.at(1).toDouble()), float(array.at(2).toDouble()));
    return true;
}

bool readTransform(const QJsonObject &object, const QString &context, Transform *out, QString *error)
{
    const QJsonValue p = object.value(QLatin1String("p"));
    const QJsonValue q = object.value(QLatin1String("q"));
    if (!p.isUndefined() && !toVector3D(p, &out->translation)) {
        *error = QStringLiteral("%1: 'p' must be an array of 3 numbers").arg(context);
        return false;
    }
    if (!q.isUndefined() && !toQuaternion(q, &out->rotation)) {
        *error = QStringLiteral("%1: 'q' must be an array of 4 numbers").arg(context);
        return false;
    }
    return true;
}

QStringList toStringList(const QJsonValue &value)
{
    QStringList out;
    const QJsonArray array = value.toArray();
    out.reserve(array.size());
    for (const QJsonValue &entry : array)
        out.append(entry.toString());
    return out;
}

QVector<int> toIntVector(const QJsonValue &value)
{
    QVector<int> out;
    const QJsonArray array = value.toArray();
    out.reserve(array.size());
    for (const QJsonValue &entry : array)
        out.append(entry.toInt());
    return out;
}

// Cursor over the little-endian .nlmesh payload.
class MeshReader
{
public:
    explicit MeshReader(const QByteArray &data)
        : m_data(data)
    {
    }

    quint32 u32() { return read<quint32>(); }
    quint16 u16() { return read<quint16>(); }
    float f32()
    {
        const quint32 bits = read<quint32>();
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    bool atEnd() const { return m_pos == m_data.size(); }
    bool overflowed() const { return m_overflow; }

private:
    template<typename T>
    T read()
    {
        if (m_overflow || m_pos + int(sizeof(T)) > m_data.size()) {
            m_overflow = true;
            return T();
        }
        const T value = qFromLittleEndian<T>(reinterpret_cast<const uchar *>(m_data.constData() + m_pos));
        m_pos += int(sizeof(T));
        return value;
    }

    const QByteArray &m_data;
    int m_pos = 0;
    bool m_overflow = false;
};

bool parseMesh(const QByteArray &data, const QString &id, MeshData *out, QString *error)
{
    const QString context = QStringLiteral("mesh %1").arg(quote(id));
    if (data.size() < 20) {
        *error = QStringLiteral("%1: file is truncated (%2 bytes)").arg(context).arg(data.size());
        return false;
    }
    if (std::memcmp(data.constData(), kMeshMagic, 4) != 0) {
        *error = QStringLiteral("%1: bad magic, expected 'NLHM'").arg(context);
        return false;
    }

    MeshReader reader(data);
    reader.u32(); // magic
    const quint32 version = reader.u32();
    const quint32 flags = reader.u32();
    const quint32 vertexCount = reader.u32();
    const quint32 indexCount = reader.u32();
    if (version != 1) {
        *error = QStringLiteral("%1: unsupported version %2 (expected 1)").arg(context).arg(version);
        return false;
    }
    if (vertexCount == 0) {
        *error = QStringLiteral("%1: contains no vertices").arg(context);
        return false;
    }

    out->positions.resize(int(vertexCount));
    for (quint32 i = 0; i < vertexCount; ++i) {
        const float x = reader.f32();
        const float y = reader.f32();
        const float z = reader.f32();
        out->positions[int(i)] = QVector3D(x, y, z);
        out->bounds.expand(out->positions[int(i)]);
    }
    if (flags & kMeshFlagNormals) {
        out->normals.resize(int(vertexCount));
        for (quint32 i = 0; i < vertexCount; ++i)
            out->normals[int(i)] = QVector3D(reader.f32(), reader.f32(), reader.f32());
    }
    if (flags & kMeshFlagUv0) {
        out->uvs.resize(int(vertexCount));
        for (quint32 i = 0; i < vertexCount; ++i)
            out->uvs[int(i)] = QVector2D(reader.f32(), reader.f32());
    }
    if (flags & kMeshFlagVertexTrim) {
        out->trim.resize(int(vertexCount));
        for (quint32 i = 0; i < vertexCount; ++i)
            out->trim[int(i)] = reader.f32();
    }
    out->indices.resize(int(indexCount));
    for (quint32 i = 0; i < indexCount; ++i)
        out->indices[int(i)] = (flags & kMeshFlagIndex32) ? reader.u32() : quint32(reader.u16());

    if (reader.overflowed()) {
        *error = QStringLiteral("%1: data is truncated for %2 vertices / %3 indices")
                     .arg(context)
                     .arg(vertexCount)
                     .arg(indexCount);
        return false;
    }
    return true;
}

} // namespace

struct HullPackage::Impl
{
    bool valid = false;
    QString rootDir;
    QVector<HullTypeDef> hullTypes;
    QVector<Piece> pieces;
    QVector<MaterialDef> materials;
    QHash<QString, int> hullTypeIndex;
    QHash<QString, int> pieceIndex;
    QHash<QString, int> materialIndex;
    QStringList warnings;

    // shared_ptr keeps the returned MeshData* stable across later insertions (QHash rehash).
    mutable QHash<QString, std::shared_ptr<MeshData>> meshCache;
    mutable QHash<QString, QImage> textureCache;
    mutable QHash<QString, QString> meshErrors;
    mutable QHash<QString, QString> textureErrors;

    QStringList meshCandidates(const QString &id) const
    {
        QString bare = id;
        const int colon = bare.indexOf(QLatin1Char(':'));
        if (colon >= 0)
            bare = bare.mid(colon + 1);
        const QDir meshDir(QDir(rootDir).filePath(QStringLiteral("mesh")));
        QStringList candidates;
        candidates << meshDir.filePath(bare + QStringLiteral(".nlmesh"));
        candidates << meshDir.filePath(id + QStringLiteral(".nlmesh"));
        return candidates;
    }
};

HullPackage::HullPackage()
    : d(std::make_unique<Impl>())
{
}

HullPackage::~HullPackage() = default;

bool HullPackage::load(const QString &rootDir, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    d = std::make_unique<Impl>();
    d->rootDir = QDir(rootDir).absolutePath();

    const QString catalogPath = QDir(d->rootDir).filePath(QStringLiteral("catalog.json"));
    QFile file(catalogPath);
    if (!file.exists())
        return fail(QStringLiteral("catalog.json not found in %1").arg(quote(d->rootDir)));
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("cannot open %1: %2").arg(quote(catalogPath), file.errorString()));

    QJsonParseError parseError{};
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(QStringLiteral("catalog.json is not valid JSON: %1 (offset %2)")
                        .arg(parseError.errorString())
                        .arg(parseError.offset));
    if (!json.isObject())
        return fail(QStringLiteral("catalog.json: top level value must be an object"));

    const QJsonObject root = json.object();
    const QJsonValue formatVersion = root.value(QLatin1String("formatVersion"));
    if (!formatVersion.isDouble())
        return fail(QStringLiteral("catalog.json: missing 'formatVersion'"));
    if (formatVersion.toInt() != kSupportedFormatVersion)
        return fail(QStringLiteral("catalog.json: unsupported formatVersion %1 (expected %2)")
                        .arg(formatVersion.toInt())
                        .arg(kSupportedFormatVersion));

    for (const char *key : {"hullTypes", "pieces", "materials"}) {
        if (!root.value(QLatin1String(key)).isArray())
            return fail(QStringLiteral("catalog.json: '%1' must be an array").arg(QLatin1String(key)));
    }

    // hullTypes
    const QJsonArray hullTypes = root.value(QLatin1String("hullTypes")).toArray();
    for (int i = 0; i < hullTypes.size(); ++i) {
        const QString context = QStringLiteral("catalog.json: hullTypes[%1]").arg(i);
        const QJsonObject object = hullTypes.at(i).toObject();
        HullTypeDef def;
        def.id = object.value(QLatin1String("id")).toString();
        if (def.id.isEmpty())
            return fail(QStringLiteral("%1 is missing 'id'").arg(context));
        def.displayName = object.value(QLatin1String("displayName")).toString(def.id);
        def.role = object.value(QLatin1String("role")).toString();
        def.prefabPath = object.value(QLatin1String("prefabPath")).toString();
        def.editable = object.value(QLatin1String("editable")).toBool(false);
        def.bows = toStringList(object.value(QLatin1String("bows")));
        def.cores = toStringList(object.value(QLatin1String("cores")));
        def.sterns = toStringList(object.value(QLatin1String("sterns")));
        def.superstructures = toStringList(object.value(QLatin1String("superstructures")));
        if (d->hullTypeIndex.contains(def.id))
            d->warnings.append(QStringLiteral("catalog.json: duplicate hull type %1").arg(quote(def.id)));
        d->hullTypeIndex.insert(def.id, d->hullTypes.size());
        d->hullTypes.append(def);
    }

    // materials
    const QJsonArray materials = root.value(QLatin1String("materials")).toArray();
    for (int i = 0; i < materials.size(); ++i) {
        const QString context = QStringLiteral("catalog.json: materials[%1]").arg(i);
        const QJsonObject object = materials.at(i).toObject();
        MaterialDef def;
        def.id = object.value(QLatin1String("id")).toString();
        if (def.id.isEmpty())
            return fail(QStringLiteral("%1 is missing 'id'").arg(context));
        def.paintable = object.value(QLatin1String("paintable")).toBool(true);
        QVector3D baseColor(1.0f, 1.0f, 1.0f);
        if (object.contains(QLatin1String("baseColor")) && !toVector3D(object.value(QLatin1String("baseColor")), &baseColor))
            return fail(QStringLiteral("%1: 'baseColor' must be an array of 3 numbers").arg(context));
        def.baseColor = QColor::fromRgbF(baseColor.x(), baseColor.y(), baseColor.z());
        def.albedoPath = object.value(QLatin1String("albedo")).toString();
        def.albedoMode = object.value(QLatin1String("albedoMode")).toString(QStringLiteral("multiply"));
        def.albedoMix = float(object.value(QLatin1String("albedoMix")).toDouble(0.0));
        def.maskPath = object.value(QLatin1String("mask")).toString();
        def.maskChannel = object.value(QLatin1String("maskChannel")).toString(QStringLiteral("r"));
        def.maskInvert = object.value(QLatin1String("maskInvert")).toBool(false);
        def.trimBaked = object.value(QLatin1String("trimBaked")).toBool(false);
        if (d->materialIndex.contains(def.id))
            d->warnings.append(QStringLiteral("catalog.json: duplicate material %1").arg(quote(def.id)));
        d->materialIndex.insert(def.id, d->materials.size());
        d->materials.append(def);
    }

    // pieces
    const QJsonArray pieces = root.value(QLatin1String("pieces")).toArray();
    for (int i = 0; i < pieces.size(); ++i) {
        const QString context = QStringLiteral("catalog.json: pieces[%1]").arg(i);
        const QJsonObject object = pieces.at(i).toObject();
        Piece piece;
        piece.id = object.value(QLatin1String("id")).toString();
        if (piece.id.isEmpty())
            return fail(QStringLiteral("%1 is missing 'id'").arg(context));
        piece.key = object.value(QLatin1String("key")).toString();
        if (piece.key.isEmpty())
            return fail(QStringLiteral("%1 is missing 'key'").arg(context));
        piece.debugName = object.value(QLatin1String("debugName")).toString();
        const QString kind = object.value(QLatin1String("kind")).toString(QStringLiteral("primary"));
        piece.kind = kind == QLatin1String("secondary") ? PieceKind::Secondary : PieceKind::Primary;

        if (object.contains(QLatin1String("foreSnap")) && !readTransform(object.value(QLatin1String("foreSnap")).toObject(), context + QStringLiteral(".foreSnap"), &piece.foreSnap, error))
            return false;
        if (object.contains(QLatin1String("aftSnap")) && !readTransform(object.value(QLatin1String("aftSnap")).toObject(), context + QStringLiteral(".aftSnap"), &piece.aftSnap, error))
            return false;

        const QJsonArray snaps = object.value(QLatin1String("superstructureSnaps")).toArray();
        for (int s = 0; s < snaps.size(); ++s) {
            Transform transform;
            if (!readTransform(snaps.at(s).toObject(), QStringLiteral("%1.superstructureSnaps[%2]").arg(context).arg(s), &transform, error))
                return false;
            piece.superstructureSnaps.append(transform);
        }

        const QJsonArray dressingPoints = object.value(QLatin1String("dressingPoints")).toArray();
        for (int p = 0; p < dressingPoints.size(); ++p) {
            const QJsonObject pointObject = dressingPoints.at(p).toObject();
            DressingPoint point;
            point.index = pointObject.value(QLatin1String("index")).toInt(p);
            point.allowEmpty = pointObject.value(QLatin1String("allowEmpty")).toBool(false);
            point.variants = toIntVector(pointObject.value(QLatin1String("variants")));
            point.variantNames = toStringList(pointObject.value(QLatin1String("variantNames")));
            piece.dressingPoints.append(point);
        }

        const QJsonArray nodes = object.value(QLatin1String("nodes")).toArray();
        for (int n = 0; n < nodes.size(); ++n) {
            const QJsonObject nodeObject = nodes.at(n).toObject();
            const QString nodeContext = QStringLiteral("%1.nodes[%2]").arg(context).arg(n);
            PieceNode node;
            node.parent = nodeObject.value(QLatin1String("parent")).toInt(-1);
            QVector3D translation;
            if (nodeObject.contains(QLatin1String("p"))) {
                if (!toVector3D(nodeObject.value(QLatin1String("p")), &translation)) {
                    *error = QStringLiteral("%1: 'p' must be an array of 3 numbers").arg(nodeContext);
                    return false;
                }
                node.translation = translation;
            }
            QQuaternion rotation;
            if (nodeObject.contains(QLatin1String("q"))) {
                if (!toQuaternion(nodeObject.value(QLatin1String("q")), &rotation)) {
                    *error = QStringLiteral("%1: 'q' must be an array of 4 numbers").arg(nodeContext);
                    return false;
                }
                node.rotation = rotation;
            }
            QVector3D scale(1.0f, 1.0f, 1.0f);
            if (nodeObject.contains(QLatin1String("s")) && !toVector3D(nodeObject.value(QLatin1String("s")), &scale)) {
                *error = QStringLiteral("%1: 's' must be an array of 3 numbers").arg(nodeContext);
                return false;
            }
            node.scale = scale;
            node.meshId = nodeObject.value(QLatin1String("mesh")).toString();
            node.materialId = nodeObject.value(QLatin1String("material")).toString();
            node.name = nodeObject.value(QLatin1String("name")).toString();
            if (node.parent >= n)
                d->warnings.append(QStringLiteral("%1: parent %2 is not an earlier node").arg(nodeContext).arg(node.parent));
            piece.nodes.append(node);
        }

        piece.meshIds = toStringList(object.value(QLatin1String("meshIds")));
        if (d->pieceIndex.contains(piece.id))
            d->warnings.append(QStringLiteral("catalog.json: duplicate piece %1").arg(quote(piece.id)));
        d->pieceIndex.insert(piece.id, d->pieces.size());
        d->pieces.append(piece);
    }

    for (const HullTypeDef &def : std::as_const(d->hullTypes)) {
        for (const QStringList *list : {&def.bows, &def.cores, &def.sterns, &def.superstructures}) {
            for (const QString &id : *list) {
                if (!d->pieceIndex.contains(id))
                    d->warnings.append(QStringLiteral("catalog.json: hull type %1 references unknown piece %2")
                                           .arg(quote(def.id), quote(id)));
            }
        }
    }

    d->valid = true;
    return true;
}

bool HullPackage::isValid() const
{
    return d->valid;
}

QString HullPackage::rootDir() const
{
    return d->rootDir;
}

const QVector<HullTypeDef> &HullPackage::hullTypes() const
{
    return d->hullTypes;
}

const QVector<Piece> &HullPackage::pieces() const
{
    return d->pieces;
}

const QVector<MaterialDef> &HullPackage::materials() const
{
    return d->materials;
}

const HullTypeDef *HullPackage::hullType(const QString &id) const
{
    const auto it = d->hullTypeIndex.constFind(id);
    if (it == d->hullTypeIndex.cend())
        return nullptr;
    return &d->hullTypes.at(it.value());
}

const Piece *HullPackage::piece(const QString &id) const
{
    const auto it = d->pieceIndex.constFind(id);
    if (it == d->pieceIndex.cend())
        return nullptr;
    return &d->pieces.at(it.value());
}

const MaterialDef *HullPackage::material(const QString &id) const
{
    const auto it = d->materialIndex.constFind(id);
    if (it == d->materialIndex.cend())
        return nullptr;
    return &d->materials.at(it.value());
}

const MeshData *HullPackage::mesh(const QString &id, QString *error) const
{
    if (id.isEmpty()) {
        if (error)
            *error = QStringLiteral("empty mesh id");
        return nullptr;
    }
    const auto cached = d->meshCache.constFind(id);
    if (cached != d->meshCache.cend())
        return cached.value().get();
    const auto known = d->meshErrors.constFind(id);
    if (known != d->meshErrors.cend()) {
        if (error)
            *error = known.value();
        return nullptr;
    }

    QString failure;
    const QStringList candidates = d->meshCandidates(id);
    QString path;
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            path = candidate;
            break;
        }
    }
    if (path.isEmpty()) {
        failure = QStringLiteral("mesh %1: file not found (looked for %2)")
                      .arg(quote(id), candidates.join(QStringLiteral(", ")));
        d->meshErrors.insert(id, failure);
        if (error)
            *error = failure;
        return nullptr;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        failure = QStringLiteral("mesh %1: cannot open %2: %3").arg(quote(id), quote(path), file.errorString());
        d->meshErrors.insert(id, failure);
        if (error)
            *error = failure;
        return nullptr;
    }

    MeshData data;
    if (!parseMesh(file.readAll(), id, &data, &failure)) {
        d->meshErrors.insert(id, failure);
        if (error)
            *error = failure;
        return nullptr;
    }
    auto stored = std::make_shared<MeshData>(std::move(data));
    MeshData *raw = stored.get();
    d->meshCache.insert(id, std::move(stored));
    return raw;
}

QImage HullPackage::texture(const QString &relativePath, QString *error) const
{
    if (relativePath.isEmpty()) {
        if (error)
            *error = QStringLiteral("empty texture path");
        return QImage();
    }
    const auto cached = d->textureCache.constFind(relativePath);
    if (cached != d->textureCache.cend())
        return cached.value();
    const auto known = d->textureErrors.constFind(relativePath);
    if (known != d->textureErrors.cend()) {
        if (error)
            *error = known.value();
        return QImage();
    }

    const QString path = QDir(d->rootDir).filePath(relativePath);
    const QImage image(path);
    if (image.isNull()) {
        const QString failure = QStringLiteral("texture %1: cannot decode %2").arg(quote(relativePath), quote(path));
        d->textureErrors.insert(relativePath, failure);
        if (error)
            *error = failure;
        return QImage();
    }
    d->textureCache.insert(relativePath, image);
    return image;
}

bool HullPackage::isEditableHullType(const QString &hullTypeId) const
{
    const HullTypeDef *def = hullType(hullTypeId);
    return def != nullptr && def->editable;
}

QStringList HullPackage::warnings() const
{
    return d->warnings;
}

} // namespace nlhc
