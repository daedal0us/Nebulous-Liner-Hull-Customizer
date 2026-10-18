#include "HullRenderer.h"

#include <QOpenGLShaderProgram>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

namespace nlhc {
namespace {

const char *kVertexShader = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in float aTrim;

uniform mat4 uMvp;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;
uniform float uOutlineWidth;
uniform bool uOutlinePass;

out vec3 vNormal;
out vec2 vUv;
out float vTrim;
out vec3 vWorld;

void main()
{
    vec3 position = aPosition;
    if (uOutlinePass)
        position += normalize(aNormal + vec3(1e-6)) * uOutlineWidth;
    vec4 world = uModel * vec4(position, 1.0);
    vWorld = world.xyz;
    vNormal = uNormalMatrix * aNormal;
    vUv = aUv;
    vTrim = aTrim;
    gl_Position = uMvp * vec4(position, 1.0);
}
)GLSL";

const char *kFragmentShader = R"GLSL(
#version 330 core
in vec3 vNormal;
in vec2 vUv;
in float vTrim;
in vec3 vWorld;

uniform vec4 uBaseColor;
uniform vec4 uTrimColor;
uniform vec4 uTintColor;
uniform bool uUseTint;
uniform bool uPainted;
uniform vec4 uMaterialColor;

uniform bool uHasAlbedo;
uniform sampler2D uAlbedo;
uniform float uAlbedoMix;

uniform bool uHasMask;
uniform sampler2D uMask;
uniform int uMaskChannel;
uniform bool uMaskInvert;
uniform bool uUseVertexTrim;

uniform bool uHasNormalMap;
uniform sampler2D uNormalMap;
uniform float uNormalStrength;

uniform bool uOutlinePass;
uniform vec3 uOutlineColor;
uniform vec3 uCameraPosition;

out vec4 fragColor;

float maskValue()
{
    vec4 texel = texture(uMask, vUv);
    float value = texel.r;
    if (uMaskChannel == 1) value = texel.g;
    else if (uMaskChannel == 2) value = texel.b;
    else if (uMaskChannel == 3) value = texel.a;
    return uMaskInvert ? 1.0 - value : value;
}

void main()
{
    if (uOutlinePass) {
        fragColor = vec4(uOutlineColor, 1.0);
        return;
    }

    vec3 normal = normalize(vNormal);
    if (uHasNormalMap) {
        // Meshes carry no tangents: build a TBN from screen space derivatives.
        vec3 dp1 = dFdx(vWorld);
        vec3 dp2 = dFdy(vWorld);
        vec2 duv1 = dFdx(vUv);
        vec2 duv2 = dFdy(vUv);
        vec3 dp2perp = cross(dp2, normal);
        vec3 dp1perp = cross(normal, dp1);
        vec3 tangent = dp2perp * duv1.x + dp1perp * duv2.x;
        vec3 bitangent = dp2perp * duv1.y + dp1perp * duv2.y;
        float longest = max(dot(tangent, tangent), dot(bitangent, bitangent));
        // UV-degenerate triangles would produce a zero basis; keep the mesh normal there.
        if (longest > 1e-12) {
            float scale = inversesqrt(longest);
            tangent *= scale;
            bitangent *= scale;
            vec3 sampled = texture(uNormalMap, vUv).xyz * 2.0 - 1.0;
            sampled.xy *= uNormalStrength;
            normal = normalize(mat3(tangent, bitangent, normal) * sampled);
        }
    }
    if (!gl_FrontFacing)
        normal = -normal;

    float trim = 0.0;
    if (uUseVertexTrim)
        trim = clamp(vTrim, 0.0, 1.0);
    else if (uHasMask)
        trim = clamp(maskValue(), 0.0, 1.0);

    vec3 painted = mix(uBaseColor.rgb, uTrimColor.rgb, trim);
    vec3 color = uPainted ? painted : uMaterialColor.rgb;

    if (uHasAlbedo) {
        vec3 detail = texture(uAlbedo, vUv).rgb;
        color *= mix(vec3(1.0), detail, clamp(uAlbedoMix, 0.0, 1.0));
    }
    if (uUseTint)
        color *= uTintColor.rgb;

    vec3 keyLight = normalize(vec3(0.35, 0.75, 0.55));
    vec3 fillLight = normalize(vec3(-0.55, 0.15, -0.65));
    float key = max(dot(normal, keyLight), 0.0);
    float fill = max(dot(normal, fillLight), 0.0);
    float ambient = 0.42 + 0.18 * normal.y;

    vec3 viewDir = normalize(uCameraPosition - vWorld);
    float rim = pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0) * 0.18;

    vec3 lit = color * (ambient + 0.72 * key + 0.28 * fill) + vec3(rim);
    fragColor = vec4(clamp(lit, 0.0, 1.0), 1.0);
}
)GLSL";

int channelIndex(const QString &name)
{
    if (name.compare(QStringLiteral("g"), Qt::CaseInsensitive) == 0)
        return 1;
    if (name.compare(QStringLiteral("b"), Qt::CaseInsensitive) == 0)
        return 2;
    if (name.compare(QStringLiteral("a"), Qt::CaseInsensitive) == 0)
        return 3;
    return 0;
}

QMatrix3x3 normalMatrix(const QMatrix4x4 &model)
{
    return model.normalMatrix();
}

} // namespace

HullRenderer::HullRenderer() = default;

HullRenderer::~HullRenderer()
{
    delete m_program;
}

bool HullRenderer::initialize(QString *error)
{
    if (m_initialized)
        return true;
    if (!initializeOpenGLFunctions()) {
        if (error)
            *error = QStringLiteral("OpenGL 3.3 core profile is not available");
        return false;
    }

    m_program = new QOpenGLShaderProgram;
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)
        || !m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader)
        || !m_program->link()) {
        if (error)
            *error = QStringLiteral("shader build failed: %1").arg(m_program->log());
        delete m_program;
        m_program = nullptr;
        return false;
    }

    m_initialized = true;
    return true;
}

void HullRenderer::release()
{
    for (auto it = m_meshes.begin(); it != m_meshes.end(); ++it) {
        if (it->vao)
            glDeleteVertexArrays(1, &it->vao);
        GLuint buffers[] = {it->positions, it->normals, it->uvs, it->trim, it->indices};
        glDeleteBuffers(5, buffers);
    }
    m_meshes.clear();
    for (GLuint id : m_textures)
        glDeleteTextures(1, &id);
    m_textures.clear();
    m_initialized = false;
}

void HullRenderer::clearCaches()
{
    m_meshes.clear();
    m_textures.clear();
}

HullRenderer::GpuMesh *HullRenderer::gpuMesh(const MeshData *mesh)
{
    auto it = m_meshes.find(mesh);
    if (it != m_meshes.end())
        return &it.value();

    GpuMesh gpu;
    gpu.indexCount = mesh->indices.size();
    gpu.hasNormals = mesh->normals.size() == mesh->positions.size();
    gpu.hasUv = mesh->uvs.size() == mesh->positions.size();
    gpu.hasTrim = mesh->trim.size() == mesh->positions.size();

    glGenVertexArrays(1, &gpu.vao);
    glBindVertexArray(gpu.vao);

    auto upload = [this](GLuint *buffer, GLuint location, int components, const void *data, int count) {
        glGenBuffers(1, buffer);
        glBindBuffer(GL_ARRAY_BUFFER, *buffer);
        glBufferData(GL_ARRAY_BUFFER, qsizetype(count) * components * sizeof(float), data, GL_STATIC_DRAW);
        glEnableVertexAttribArray(location);
        glVertexAttribPointer(location, components, GL_FLOAT, GL_FALSE, 0, nullptr);
    };

    const int vertexCount = mesh->positions.size();
    upload(&gpu.positions, 0, 3, mesh->positions.constData(), vertexCount);

    if (gpu.hasNormals) {
        upload(&gpu.normals, 1, 3, mesh->normals.constData(), vertexCount);
    } else {
        const QVector<float> fallback(vertexCount * 3, 0.0f);
        upload(&gpu.normals, 1, 3, fallback.constData(), vertexCount);
    }
    if (gpu.hasUv) {
        upload(&gpu.uvs, 2, 2, mesh->uvs.constData(), vertexCount);
    } else {
        const QVector<float> fallback(vertexCount * 2, 0.0f);
        upload(&gpu.uvs, 2, 2, fallback.constData(), vertexCount);
    }
    if (gpu.hasTrim) {
        upload(&gpu.trim, 3, 1, mesh->trim.constData(), vertexCount);
    } else {
        const QVector<float> fallback(vertexCount, 0.0f);
        upload(&gpu.trim, 3, 1, fallback.constData(), vertexCount);
    }

    glGenBuffers(1, &gpu.indices);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.indices);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, qsizetype(gpu.indexCount) * sizeof(quint32),
                 mesh->indices.constData(), GL_STATIC_DRAW);

    glBindVertexArray(0);

    return &(*m_meshes.insert(mesh, gpu));
}

GLuint HullRenderer::texture(const HullPackage *package, const QString &relativePath)
{
    if (!package || relativePath.isEmpty())
        return 0;
    auto it = m_textures.find(relativePath);
    if (it != m_textures.end())
        return it.value();

    QString error;
    const QImage image = package->texture(relativePath, &error);
    GLuint id = 0;
    if (!image.isNull()) {
        const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba.width(), rgba.height(), 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, rgba.constBits());
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    m_textures.insert(relativePath, id);
    return id;
}

void HullRenderer::drawItems(const AssemblyResult &assembly, const HullPackage *package,
                             const RenderStyle &style, const QMatrix4x4 &view,
                             const QMatrix4x4 &projection, bool outlinePass)
{
    QOpenGLShaderProgram &program = *m_program;
    program.bind();
    program.setUniformValue("uOutlinePass", outlinePass);
    program.setUniformValue("uBaseColor", style.baseColor);
    program.setUniformValue("uTrimColor", style.trimColor);
    program.setUniformValue("uUseTint", style.applyHullTint);
    program.setUniformValue("uTintColor", style.hullTint);

    const float outlineWidth = qMax(assembly.bounds.radius() * 0.0035f, 0.02f);
    program.setUniformValue("uOutlineWidth", outlineWidth);
    program.setUniformValue("uOutlineColor", QVector3D(0.06f, 0.07f, 0.09f));

    const QMatrix4x4 viewProjection = projection * view;
    program.setUniformValue("uCameraPosition", view.inverted().column(3).toVector3D());
    program.setUniformValue("uAlbedo", 0);
    program.setUniformValue("uMask", 1);
    program.setUniformValue("uNormalMap", 2);

    for (const DrawItem &item : assembly.items) {
        if (!item.mesh)
            continue;
        GpuMesh *gpu = gpuMesh(item.mesh);
        if (!gpu || gpu->indexCount == 0)
            continue;

        const MaterialDef *material = item.material;
        const bool painted = material ? material->paintable : true;
        program.setUniformValue("uPainted", painted);
        program.setUniformValue("uMaterialColor", material ? material->baseColor : QColor(Qt::gray));
        program.setUniformValue("uUseVertexTrim", gpu->hasTrim);

        const QString albedoPath = material ? material->albedoPath : QString();
        const GLuint albedoId = texture(package, albedoPath);
        const bool hasAlbedo = albedoId != 0 && gpu->hasUv;
        // A "multiply" albedo is a greyscale detail map and should always modulate the paint
        // colour at full strength; "overlay" uses the authored mix factor.
        float albedoMix = 0.0f;
        if (material) {
            albedoMix = material->albedoMode == QLatin1String("multiply")
                ? 1.0f
                : material->albedoMix;
        }
        program.setUniformValue("uHasAlbedo", hasAlbedo);
        program.setUniformValue("uAlbedoMix", albedoMix);
        if (hasAlbedo) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, albedoId);
        }

        const QString maskPath = material ? material->maskPath : QString();
        const GLuint maskId = (!gpu->hasTrim) ? texture(package, maskPath) : 0;
        const bool hasMask = maskId != 0 && gpu->hasUv;
        program.setUniformValue("uHasMask", hasMask);
        program.setUniformValue("uMaskChannel", material ? channelIndex(material->maskChannel) : 0);
        program.setUniformValue("uMaskInvert", material ? material->maskInvert : false);
        if (hasMask) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, maskId);
        }

        const QString normalPath = material ? material->normalPath : QString();
        const GLuint normalId = gpu->hasUv ? texture(package, normalPath) : 0;
        const bool hasNormalMap = normalId != 0;
        program.setUniformValue("uHasNormalMap", hasNormalMap);
        program.setUniformValue("uNormalStrength", material ? material->normalStrength : 1.0f);
        if (hasNormalMap) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, normalId);
        }

        program.setUniformValue("uModel", item.world);
        program.setUniformValue("uMvp", viewProjection * item.world);
        program.setUniformValue("uNormalMatrix", normalMatrix(item.world));

        glBindVertexArray(gpu->vao);
        glDrawElements(GL_TRIANGLES, gpu->indexCount, GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
    program.release();
}

void HullRenderer::draw(const AssemblyResult &assembly, const HullPackage *package,
                        const RenderStyle &style, const QMatrix4x4 &view,
                        const QMatrix4x4 &projection)
{
    if (!m_initialized || !m_program)
        return;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    drawItems(assembly, package, style, view, projection, false);

    if (style.outline) {
        glCullFace(GL_FRONT);
        drawItems(assembly, package, style, view, projection, true);
        glCullFace(GL_BACK);
    }

    glBindVertexArray(0);
    glUseProgram(0);
}

} // namespace nlhc
