#pragma once

#include <QHash>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QString>

#include "RenderStyle.h"
#include "nlhc/asset/HullPackage.h"
#include "nlhc/hull/HullAssembler.h"

class QOpenGLShaderProgram;

namespace nlhc {

// Draws an AssemblyResult with the base/trim/albedo/mask shading model described in
// Implements the documented shading model. Requires a current OpenGL 3.3 core context.
class HullRenderer : protected QOpenGLFunctions_3_3_Core
{
public:
    HullRenderer();
    ~HullRenderer();

    bool initialize(QString *error);
    void release();
    bool isInitialized() const { return m_initialized; }

    void draw(const AssemblyResult &assembly, const HullPackage *package, const RenderStyle &style,
              const QMatrix4x4 &view, const QMatrix4x4 &projection);
    void clearCaches();

private:
    struct GpuMesh
    {
        GLuint vao = 0;
        GLuint positions = 0;
        GLuint normals = 0;
        GLuint uvs = 0;
        GLuint trim = 0;
        GLuint indices = 0;
        int indexCount = 0;
        bool hasNormals = false;
        bool hasUv = false;
        bool hasTrim = false;
    };

    GpuMesh *gpuMesh(const MeshData *mesh);
    GLuint texture(const HullPackage *package, const QString &relativePath);
    float albedoMean(const HullPackage *package, const QString &relativePath);
    void drawItems(const AssemblyResult &assembly, const HullPackage *package, const RenderStyle &style,
                   const QMatrix4x4 &view, const QMatrix4x4 &projection, bool outlinePass);

    bool m_initialized = false;
    QOpenGLShaderProgram *m_program = nullptr;
    QHash<const MeshData *, GpuMesh> m_meshes;
    QHash<QString, GLuint> m_textures;
    QHash<QString, float> m_albedoMeans;
    QHash<QString, float> m_uniformCache;
};

} // namespace nlhc
