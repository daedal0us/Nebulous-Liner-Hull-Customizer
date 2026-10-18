#include "nlhc/hull/HullAssembler.h"

#include <QMatrix4x4>

#include <utility>

#include "PieceLookup.h"

namespace nlhc {
namespace {

QMatrix4x4 nodeMatrix(const PieceNode &node)
{
    QMatrix4x4 matrix;
    matrix.translate(node.translation);
    matrix.rotate(node.rotation);
    matrix.scale(node.scale);
    return matrix;
}

QMatrix4x4 snapMatrix(const Transform &transform)
{
    QMatrix4x4 matrix;
    matrix.translate(transform.translation);
    matrix.rotate(transform.rotation);
    return matrix;
}

Bounds transformedBounds(const Bounds &bounds, const QMatrix4x4 &matrix)
{
    Bounds result;
    if (!bounds.valid)
        return result;
    const QVector3D &low = bounds.minimum;
    const QVector3D &high = bounds.maximum;
    for (int i = 0; i < 8; ++i) {
        const QVector3D corner((i & 1) ? high.x() : low.x(), (i & 2) ? high.y() : low.y(),
                               (i & 4) ? high.z() : low.z());
        result.expand(matrix.map(corner));
    }
    return result;
}

Diagnostic makeDiagnostic(Severity severity, const char *code, const QString &detail, int segment = -1,
                          int dressingIndex = -1)
{
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.code = QString::fromLatin1(code);
    diagnostic.detail = detail;
    diagnostic.segment = segment;
    diagnostic.dressingIndex = dressingIndex;
    return diagnostic;
}

// Mirrors how the game loads a saved configuration and picks dressing variants.
QVector<bool> visibleNodes(const Piece &piece, const QVector<int> &dressing, int segment,
                           QVector<Diagnostic> *diagnostics)
{
    const int nodeCount = piece.nodes.size();
    QVector<bool> visible(nodeCount, true);
    if (nodeCount == 0)
        return visible;

    QVector<QVector<int>> children(nodeCount);
    for (int i = 0; i < nodeCount; ++i) {
        const int parent = piece.nodes.at(i).parent;
        if (parent >= 0 && parent < nodeCount && parent != i)
            children[parent].append(i);
    }

    const auto hideSubtree = [&visible, &children, nodeCount](int root) {
        if (root < 0 || root >= nodeCount)
            return;
        QVector<int> pending{root};
        while (!pending.isEmpty()) {
            const int node = pending.takeLast();
            if (!visible.at(node))
                continue;
            visible[node] = false;
            for (int child : children.at(node))
                pending.append(child);
        }
    };

    for (int point = 0; point < piece.dressingPoints.size(); ++point) {
        const DressingPoint &dressingPoint = piece.dressingPoints.at(point);
        const int variantCount = dressingPoint.variants.size();
        int selected = -1; // nothing selected -> the point stays empty
        bool hideAll = true;

        if (point >= dressing.size()) {
            hideAll = false; // the game never calls Select(): every variant stays visible
            diagnostics->append(makeDiagnostic(
                Severity::Warning, diag::DressingLengthMismatch,
                QStringLiteral("%1 of %2 dressing points configured")
                    .arg(dressing.size())
                    .arg(piece.dressingPoints.size()),
                segment, point));
        } else {
            const int choice = dressing.at(point);
            if (choice >= 0 && choice < variantCount) {
                selected = choice;
                hideAll = true;
            } else if (dressingPoint.allowEmpty && choice == variantCount) {
                hideAll = true;
            } else {
                hideAll = true;
                diagnostics->append(makeDiagnostic(
                    Severity::Warning, diag::DressingOutOfRange,
                    QStringLiteral("choice %1 of %2 variants").arg(choice).arg(variantCount), segment, point));
            }
        }

        if (!hideAll)
            continue;
        for (int variant = 0; variant < variantCount; ++variant) {
            if (variant == selected)
                continue;
            hideSubtree(dressingPoint.variants.at(variant));
        }
    }

    return visible;
}

struct PendingItem
{
    const MeshData *mesh = nullptr;
    const MaterialDef *material = nullptr;
    int segment = 1;
    QMatrix4x4 local;
};

} // namespace

AssemblyResult HullAssembler::assemble(const HullPackage &package, const QString &hullTypeId,
                                       const RandomHullConfig &config) const
{
    AssemblyResult result;

    const HullTypeDef *type = package.hullType(hullTypeId);
    if (!type) {
        result.diagnostics.append(makeDiagnostic(Severity::Error, diag::UnknownHullType, hullTypeId));
        return result;
    }

    const Piece *segments[RandomHullConfig::SegmentCount] = {nullptr, nullptr, nullptr};
    for (int i = 0; i < RandomHullConfig::SegmentCount; ++i) {
        const QString &key = config.primary[size_t(i)].key;
        segments[i] = detail::findPieceByKey(package, detail::segmentPieceIds(*type, i), key);
        if (!segments[i])
            result.diagnostics.append(makeDiagnostic(Severity::Error, diag::UnknownSegment,
                                                     key.isEmpty() ? QStringLiteral("no key") : key, i));
    }

    if (!segments[0] && !segments[1] && !segments[2])
        return result;

    // Pure translation layout: bow.aftSnap meets core.foreSnap, stern.foreSnap meets core.aftSnap.
    QVector3D offsets[RandomHullConfig::SegmentCount];
    if (segments[RandomHullConfig::Core]) {
        if (segments[RandomHullConfig::Bow])
            offsets[RandomHullConfig::Bow] =
                segments[RandomHullConfig::Core]->foreSnap.translation - segments[RandomHullConfig::Bow]->aftSnap.translation;
        if (segments[RandomHullConfig::Stern])
            offsets[RandomHullConfig::Stern] =
                segments[RandomHullConfig::Core]->aftSnap.translation - segments[RandomHullConfig::Stern]->foreSnap.translation;
    }

    QVector<PendingItem> pending;
    for (int segment = 0; segment < RandomHullConfig::SegmentCount; ++segment) {
        const Piece *piece = segments[segment];
        if (!piece)
            continue;
        const QVector<bool> visible = visibleNodes(*piece, config.primary[size_t(segment)].dressing, segment,
                                                   &result.diagnostics);
        for (int index = 0; index < piece->nodes.size(); ++index) {
            if (!visible.value(index))
                continue;
            const PieceNode &node = piece->nodes.at(index);
            if (node.meshId.isEmpty())
                continue;
            QString error;
            const MeshData *mesh = package.mesh(node.meshId, &error);
            if (!mesh) {
                result.diagnostics.append(makeDiagnostic(Severity::Error, diag::MeshMissing, error, segment));
                continue;
            }
            PendingItem item;
            item.mesh = mesh;
            item.material = package.material(node.materialId);
            item.segment = segment;
            item.local = nodeMatrix(node);
            pending.append(item);
        }
    }

    // FinalizeSetup: shift the whole assembly so the bound centre sits on z = 0.
    Bounds primaryBounds;
    for (const PendingItem &item : std::as_const(pending)) {
        Bounds bounds = transformedBounds(item.mesh->bounds, item.local);
        if (!bounds.valid)
            continue;
        bounds.minimum += offsets[item.segment];
        bounds.maximum += offsets[item.segment];
        primaryBounds.expand(bounds);
    }
    const float zShift = primaryBounds.valid ? primaryBounds.center().z() : 0.0f;

    const auto baseMatrix = [&offsets, zShift](int segment) {
        QMatrix4x4 matrix;
        matrix.translate(offsets[segment] - QVector3D(0.0f, 0.0f, zShift));
        return matrix;
    };

    for (const PendingItem &item : std::as_const(pending)) {
        DrawItem draw;
        draw.mesh = item.mesh;
        draw.material = item.material;
        draw.world = baseMatrix(item.segment) * item.local;
        result.items.append(draw);
    }

    // Secondary structures hang off a segment snap point; localPosition is 0.
    for (const SecondaryConfig &entry : config.secondary) {
        const Piece *secondary = detail::findPieceByKey(package, type->superstructures, entry.key);
        if (!secondary) {
            result.diagnostics.append(
                makeDiagnostic(Severity::Error, diag::UnknownSecondary,
                               entry.key.isEmpty() ? QStringLiteral("no key") : entry.key));
            continue;
        }
        if (entry.segment < 0 || entry.segment >= RandomHullConfig::SegmentCount) {
            result.diagnostics.append(makeDiagnostic(Severity::Error, diag::SecondarySegmentOutOfRange,
                                                     QString::number(entry.segment)));
            continue;
        }
        const Piece *segment = segments[entry.segment];
        if (!segment)
            continue; // already reported as an unknown primary segment
        if (entry.snap < 0 || entry.snap >= segment->superstructureSnaps.size()) {
            result.diagnostics.append(
                makeDiagnostic(Severity::Error, diag::SecondarySnapOutOfRange, QString::number(entry.snap),
                               entry.segment));
            continue;
        }
        const QMatrix4x4 mount = baseMatrix(entry.segment) * snapMatrix(segment->superstructureSnaps.at(entry.snap));
        for (const PieceNode &node : secondary->nodes) {
            if (node.meshId.isEmpty())
                continue;
            QString error;
            const MeshData *mesh = package.mesh(node.meshId, &error);
            if (!mesh) {
                result.diagnostics.append(
                    makeDiagnostic(Severity::Error, diag::MeshMissing, error, entry.segment));
                continue;
            }
            DrawItem draw;
            draw.mesh = mesh;
            draw.material = package.material(node.materialId);
            draw.world = mount * nodeMatrix(node);
            result.items.append(draw);
        }
    }

    for (const DrawItem &item : std::as_const(result.items))
        result.bounds.expand(transformedBounds(item.mesh->bounds, item.world));

    result.renderable = !result.items.isEmpty();
    return result;
}

} // namespace nlhc
