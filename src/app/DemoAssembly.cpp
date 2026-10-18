#include "DemoAssembly.h"

#include <QVector>
#include <QVector2D>
#include <QVector3D>

namespace nlhc::demo {
namespace {

MeshData makeBox(const QVector3D &center, const QVector3D &half, float uvScale = 1.0f)
{
    struct Face
    {
        QVector3D normal;
        QVector3D a;
        QVector3D b;
        QVector3D c;
        QVector3D d;
    };

    const float x = half.x();
    const float y = half.y();
    const float z = half.z();
    const QVector<Face> faces = {
        {QVector3D(0, 0, 1), {x, -y, z}, {x, y, z}, {-x, y, z}, {-x, -y, z}},
        {QVector3D(0, 0, -1), {-x, -y, -z}, {-x, y, -z}, {x, y, -z}, {x, -y, -z}},
        {QVector3D(1, 0, 0), {x, -y, -z}, {x, y, -z}, {x, y, z}, {x, -y, z}},
        {QVector3D(-1, 0, 0), {-x, -y, z}, {-x, y, z}, {-x, y, -z}, {-x, -y, -z}},
        {QVector3D(0, 1, 0), {-x, y, z}, {x, y, z}, {x, y, -z}, {-x, y, -z}},
        {QVector3D(0, -1, 0), {-x, -y, -z}, {x, -y, -z}, {x, -y, z}, {-x, -y, z}},
    };

    MeshData mesh;
    for (const Face &face : faces) {
        const quint32 base = quint32(mesh.positions.size());
        const QVector<QVector3D> corners = {face.a, face.b, face.c, face.d};
        for (const QVector3D &corner : corners) {
            const QVector3D world = center + corner;
            mesh.positions.append(world);
            mesh.normals.append(face.normal);
            mesh.uvs.append(QVector2D(world.z() * uvScale, world.y() * uvScale));
            mesh.bounds.expand(world);
        }
        mesh.indices << base + 0 << base + 1 << base + 2 << base + 0 << base + 2 << base + 3;
    }
    return mesh;
}

} // namespace

AssemblyResult buildFixedOnlyAssembly()
{
    static QVector<MeshData> meshes;
    static QVector<MaterialDef> materials;
    if (meshes.isEmpty()) {
        meshes.append(makeBox(QVector3D(0.0f, 0.0f, 0.0f), QVector3D(6.0f, 4.0f, 10.0f)));
        MaterialDef copper;
        copper.id = QStringLiteral("demo.fixed");
        copper.paintable = false;
        copper.baseColor = QColor(0x83, 0x73, 0x64);
        materials.append(copper);
    }

    AssemblyResult result;
    result.renderable = true;
    QMatrix4x4 identity;
    result.items.append({&meshes[0], &materials[0], identity});
    result.bounds.expand(meshes[0].bounds);
    return result;
}

AssemblyResult buildDemoAssembly()
{
    static QVector<MeshData> meshes;
    static QVector<MaterialDef> materials;
    if (meshes.isEmpty()) {
        meshes.append(makeBox(QVector3D(0, 0, 18.0f), QVector3D(6.0f, 5.0f, 10.0f)));   // core
        meshes.append(makeBox(QVector3D(0, 0, 34.0f), QVector3D(4.0f, 3.5f, 6.0f)));   // bow
        meshes.append(makeBox(QVector3D(0, 0, -2.0f), QVector3D(5.0f, 4.5f, 10.0f)));  // stern
        meshes.append(makeBox(QVector3D(0, 7.0f, 20.0f), QVector3D(3.0f, 2.0f, 5.0f)));// superstructure

        MaterialDef hull;
        hull.id = QStringLiteral("demo.hull");
        hull.paintable = true;
        materials.append(hull);
        MaterialDef structure;
        structure.id = QStringLiteral("demo.structure");
        structure.paintable = false;
        structure.baseColor = QColor(0x6a, 0x6f, 0x76);
        materials.append(structure);
    }

    AssemblyResult result;
    result.renderable = true;
    const MaterialDef *hullMat = &materials[0];
    const MaterialDef *structureMat = &materials[1];

    QMatrix4x4 identity;
    result.items.append({&meshes[0], hullMat, identity});
    result.items.append({&meshes[1], hullMat, identity});
    result.items.append({&meshes[2], hullMat, identity});
    result.items.append({&meshes[3], structureMat, identity});

    for (const MeshData &mesh : meshes)
        result.bounds.expand(mesh.bounds);
    return result;
}

} // namespace nlhc::demo
