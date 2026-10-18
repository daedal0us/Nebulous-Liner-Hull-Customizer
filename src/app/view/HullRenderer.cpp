#include "HullRenderer.h"

#include <QColor>
#include <QOpenGLShaderProgram>

#include <cmath>
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

// The exported maps are stored rotated by 180 degrees (the paint mask is the proof: its stencil
// text and the hull symbols only read correctly with both axes inverted), so every lookup undoes
// that rotation. Asserting it here keeps all four maps consistent with each other.
vec2 textureUv() { return vec2(1.0 - vUv.x, 1.0 - vUv.y); }
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

uniform bool uHasDecal;
uniform sampler2D uDecal;
uniform bool uDecalUseAlpha;

uniform float uMetallic;
uniform float uSmoothness;
uniform float uMaskKeep;

uniform bool uCamoPaint;
uniform vec3 uCamoOffset;
uniform float uCamoScale;
uniform float uCamoSplit;

vec3 hash33(vec3 p)
{
    p = vec3(dot(p, vec3(127.1, 311.7, 74.7)),
             dot(p, vec3(269.5, 183.3, 246.1)),
             dot(p, vec3(113.5, 271.9, 124.6)));
    return fract(sin(p) * 43758.5453123);
}

// Value of the cellular field at p. The game paints a per-ship camouflage field, which is
// why the boundary between the two paint colours wanders across panels instead of following
// the stripe mask.
float camoCell(vec3 p)
{
    vec3 base = floor(p);
    vec3 here = fract(p);
    float best = 1e9;
    vec3 pick = base;
    for (int z = -1; z <= 1; ++z) {
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                vec3 cell = vec3(float(x), float(y), float(z));
                float d = length(cell + hash33(base + cell) - here);
                if (d < best) {
                    best = d;
                    pick = base + cell;
                }
            }
        }
    }
    return hash33(pick).x;
}

uniform float uAlbedoMean;      // luminance average of the albedo, used to keep paint brightness
uniform bool uHasBare;          // louvred vent panels painted by the exporter
uniform sampler2D uBare;
uniform float uAlbedoDetail;    // how strongly the albedo modulates the paint (0..1)
uniform float uDecalStrength;   // white markings baked into the albedo
uniform float uExposure;

uniform bool uOutlinePass;
uniform vec3 uOutlineColor;
uniform vec3 uCameraPosition;

uniform int uDebugMode;      // 1 flat material colour, 2 raw paint mask, 3 albedo
uniform vec4 uDebugColor;

out vec4 fragColor;

float maskValue()
{
    vec4 texel = texture(uMask, textureUv());
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
    if (uDebugMode == 1) {
        fragColor = vec4(uDebugColor.rgb, 1.0);
        return;
    }

    vec2 uvTex = textureUv();
    vec3 normal = normalize(vNormal);
    if (uHasNormalMap) {
        // Meshes carry no tangents: build a TBN from screen space derivatives.
        vec3 dp1 = dFdx(vWorld);
        vec3 dp2 = dFdy(vWorld);
        vec2 duv1 = dFdx(uvTex);
        vec2 duv2 = dFdy(uvTex);
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
            vec3 sampled = texture(uNormalMap, uvTex).xyz * 2.0 - 1.0;
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

    vec3 albedoSample = vec3(1.0);
    if (uHasAlbedo)
        albedoSample = texture(uAlbedo, uvTex).rgb;

    if (uDebugMode == 2) {
        float raw = uHasMask ? clamp(texture(uMask, uvTex).r, 0.0, 1.0) : 0.0;
        fragColor = vec4(vec3(raw), 1.0);
        return;
    }
    if (uDebugMode == 3) {
        fragColor = vec4(albedoSample, 1.0);
        return;
    }
    if (uDebugMode == 4) {
        // Atlas coordinate of this fragment, already un-rotated, so a screen pixel can be
        // mapped straight back onto the exported texture.
        fragColor = vec4(uvTex.x, uvTex.y, 0.0, 1.0);
        return;
    }

    vec3 color;
    if (uPainted) {
        // Livery surfaces: the paint colour decides the hue, the albedo only modulates it.
        // Dividing by the texture mean keeps the paint at its authored brightness instead of
        // darkening every surface by the average of the detail map.
        vec3 painted;
        if (uCamoPaint) {
            float camo = step(uCamoSplit, camoCell(vWorld * uCamoScale + uCamoOffset));
            painted = mix(uBaseColor.rgb, uTrimColor.rgb, camo);
        } else {
            painted = mix(uBaseColor.rgb, uTrimColor.rgb, trim);
        }
        // The hull atlas carries near-white panels (vent grilles, radiator stripes) that the game
        // leaves unpainted. Their albedo is the colour, so blend it back over the paint.
        float albedoLuma24 = dot(albedoSample, vec3(0.2990, 0.5870, 0.1140));
        // Louvred vents stay bare metal in the game: the paint stops at the frame. Neither the
        // mask nor the brightness can find them, so the exporter bakes their coverage.
        float bare = (uMaskKeep > 0.5 || !uHasBare)
            ? 0.0 : smoothstep(0.35, 0.70, texture(uBare, uvTex).r);
        if (uMaskKeep > 0.5) {
            // The mask only says where paint goes; the rest keeps its authored albedo, which is
            // what makes a brass truss sit inside an otherwise painted bridge.
            vec3 authored = uMaterialColor.rgb * albedoSample;
            painted = mix(authored, uBaseColor.rgb, trim);
        }
        // The atlas seams are dark; left unbounded the detail map turns the trim bands brown.
        vec3 detail = clamp(albedoSample / max(uAlbedoMean, 0.05), vec3(0.72), vec3(1.30));
        color = painted * mix(vec3(1.0), detail, clamp(uAlbedoDetail, 0.0, 1.0));
        // Bare panels are metals lit by an environment this viewer has no probe for, so the
        // albedo is lifted toward the plate colour the game shows.
        color = mix(color, mix(vec3(0.95), albedoSample, 0.70) * uExposure, bare);
        if (uUseTint)
            color *= uTintColor.rgb;
        color *= uExposure;
    } else {
        // Fixed-colour props (copper doodads, grey deck plating, engine glow) keep their own
        // material colour: no livery tint and no exposure lift, or the copper turns pale. The
        // authored mix decides how much of the albedo counts (doodads are a 0.35 overlay).
        vec3 base = uMaterialColor.rgb;
        float mixAmount = clamp(uAlbedoMix, 0.0, 1.0);
        color = mix(base, base * albedoSample, mixAmount);
    }
    // Near-white albedo pixels are only a weak fallback: without the authored decal they are
    // the grime highlights, not markings, so they must not wash a surface out.
    if (uHasAlbedo && uDecalStrength > 0.0) {
        float luminance = dot(albedoSample, vec3(0.2990, 0.5870, 0.1140));
        float highlight = smoothstep(0.80, 0.96, luminance);
        color = mix(color, vec3(0.93), highlight * clamp(uDecalStrength, 0.0, 1.0) * 0.35);
    }
    // Authored nameplate decal (hull numbers, lettering) laid over paint and fixed surfaces.
    if (uHasDecal) {
        vec4 decalSample = texture(uDecal, uvTex);
        float amount = uDecalUseAlpha
            ? decalSample.a
            : smoothstep(0.30, 0.70, dot(decalSample.rgb, vec3(0.2990, 0.5870, 0.1140)));
        color = mix(color, decalSample.rgb, clamp(amount, 0.0, 1.0) * clamp(uDecalStrength, 0.0, 1.0));
    }

    // Fixed three point rig in world space: key from upper front left, fill from lower right,
    // back light for separation; the directions never follow the camera.
    vec3 keyLight = normalize(vec3(0.45, 0.72, 0.52));
    vec3 fillLight = normalize(vec3(-0.62, 0.18, 0.35));
    vec3 backLight = normalize(vec3(-0.25, 0.35, -0.90));
    float key = max(dot(normal, keyLight), 0.0);
    float fill = max(dot(normal, fillLight), 0.0);
    float back = max(dot(normal, backLight), 0.0);
    float ambient = 0.44 + 0.10 * normal.y;

    vec3 viewDir = normalize(uCameraPosition - vWorld);
    float rim = pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0) * 0.12;

    // HDRP metals: the diffuse term dies down and a tight specular highlight takes over. That
    // is what separates the brass doodads from flat plastic.
    // Only fixed props use the metallic response: the hull plating is authored metallic too,
    // but it is lit by the game's environment probes, which this viewer does not have — applying
    // the lobe there would just darken the livery.
    float metallic = uPainted ? 0.0 : clamp(uMetallic, 0.0, 1.0);
    float smoothness = clamp(uSmoothness, 0.0, 1.0);
    vec3 halfDir = normalize(viewDir + keyLight);
    float specPower = mix(8.0, 320.0, smoothness);
    float spec = pow(max(dot(normal, halfDir), 0.0), specPower);
    vec3 specular = vec3(spec) * (0.04 + 0.85 * metallic) * (0.15 + 0.85 * smoothness) * 1.6;

    float lambert = ambient + 0.62 * key + 0.22 * fill + 0.16 * back;
    vec3 diffuseLit = color * (1.0 - metallic) * lambert;

    // HDRP metals show no diffuse at all: what you see is the environment, tinted by
    // albedo * baseColor. Without a reflection term a white-ish doodad atlas just paints the
    // prop white, so the viewer fakes a sky/ground gradient instead.
    vec3 skyColor = vec3(1.05, 1.02, 0.98);
    vec3 groundColor = vec3(0.28, 0.26, 0.24);
    vec3 envColor = mix(groundColor, skyColor, clamp(0.5 + 0.5 * normal.y, 0.0, 1.0));
    float fresnel = 0.05 + 0.95 * pow(1.0 - max(dot(normal, viewDir), 0.0), 5.0);
    // HDRP metal: the reflection takes its hue from the specular colour and only its brightness
    // from the detail map. The game sees this through a warm environment probe plus filmic
    // tonemapping, which deepens the hue; the viewer has neither, so the hue is deepened here.
    float albedoLuma = dot(albedoSample, vec3(0.2990, 0.5870, 0.1140));
    vec3 metalColor = pow(uMaterialColor.rgb, vec3(1.45));
    metalColor *= mix(0.62, 1.10, clamp(albedoLuma, 0.0, 1.0));
    vec3 metalEnv = metalColor * envColor * (0.95 + 0.40 * fresnel) * (0.45 + 0.55 * smoothness);

    vec3 lit = mix(diffuseLit + vec3(rim), metalEnv + specular, metallic);
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

float HullRenderer::albedoMean(const HullPackage *package, const QString &relativePath)
{
    if (!package || relativePath.isEmpty())
        return 1.0f;
    const auto cached = m_albedoMeans.constFind(relativePath);
    if (cached != m_albedoMeans.cend())
        return cached.value();

    QString error;
    const QImage image = package->texture(relativePath, &error);
    float mean = 1.0f;
    if (!image.isNull()) {
        const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
        double total = 0.0;
        qint64 samples = 0;
        const int step = qMax(1, rgb.width() / 128);
        for (int y = 0; y < rgb.height(); y += step) {
            const QRgb *line = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
            for (int x = 0; x < rgb.width(); x += step) {
                const QRgb pixel = line[x];
                total += 0.2990 * qRed(pixel) + 0.5870 * qGreen(pixel) + 0.1140 * qBlue(pixel);
                ++samples;
            }
        }
        if (samples > 0)
            mean = qBound(0.05f, float(total / samples / 255.0), 1.0f);
    }
    m_albedoMeans.insert(relativePath, mean);
    return mean;
}

QVector4D HullRenderer::debugColor(const QString &id)
{
    const QString key = id.isEmpty() ? QStringLiteral("-") : id;
    auto found = m_debugColors.constFind(key);
    if (found != m_debugColors.cend())
        return QVector4D(found.value(), 1.0f);
    // A golden-ratio hue walk keeps adjacent materials far apart in hue.
    const QColor tone = QColor::fromHsvF(std::fmod(float(m_debugColors.size()) * 0.618034f, 1.0f),
                                         0.72f, 1.0f);
    const QVector3D rgb(tone.redF(), tone.greenF(), tone.blueF());
    m_debugColors.insert(key, rgb);
    m_legend += QStringLiteral("%1 #%2\n").arg(key,
        QString::number(tone.rgb() & 0x00ffffff, 16).rightJustified(6, QLatin1Char('0')));
    return QVector4D(rgb, 1.0f);
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
    program.setUniformValue("uDebugMode", static_cast<int>(m_debugView));

    for (const DrawItem &item : assembly.items) {
        if (!item.mesh)
            continue;
        GpuMesh *gpu = gpuMesh(item.mesh);
        if (!gpu || gpu->indexCount == 0)
            continue;

        const MaterialDef *material = item.material;
        if (m_debugView == DebugView::Materials) {
            program.setUniformValue("uDebugColor", debugColor(material ? material->id : QString()));
        }
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
        program.setUniformValue("uAlbedoMean", albedoMean(package, albedoPath));
        const QString barePath = material ? material->bareMaskPath : QString();
        const GLuint bareId = gpu->hasUv ? texture(package, barePath) : 0;
        program.setUniformValue("uHasBare", bareId != 0);
        program.setUniformValue("uBare", 4);
        if (bareId != 0) {
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, bareId);
        }
        program.setUniformValue("uAlbedoDetail", style.albedoDetail);
        program.setUniformValue("uDecalStrength", style.decalStrength);
        program.setUniformValue("uExposure", style.exposure);
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

        const QString decalPath = material ? material->decalPath : QString();
        const GLuint decalId = gpu->hasUv ? texture(package, decalPath) : 0;
        const bool hasDecal = decalId != 0;
        program.setUniformValue("uHasDecal", hasDecal);
        program.setUniformValue("uDecalUseAlpha",
                                material && material->decalMode == QLatin1String("alpha"));
        program.setUniformValue("uDecal", 3);
        program.setUniformValue("uMaskKeep",
                                material && material->maskMode == QLatin1String("keep") ? 1.0f : 0.0f);
        program.setUniformValue("uCamoPaint", style.camoPaint);
    program.setUniformValue("uCamoOffset", style.camoOffset);
    program.setUniformValue("uCamoScale", style.camoScale);
    program.setUniformValue("uCamoSplit", style.camoSplit);
    program.setUniformValue("uMetallic", material ? material->metallic : 0.0f);
        program.setUniformValue("uSmoothness", material ? material->smoothness : 0.5f);
        if (hasDecal) {
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, decalId);
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
