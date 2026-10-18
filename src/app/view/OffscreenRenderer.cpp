#include "OffscreenRenderer.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_3_Core>
#include <QSurfaceFormat>

#include "Camera.h"
#include "HullRenderer.h"

namespace nlhc {
namespace {

QSurfaceFormat renderFormat()
{
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    return format;
}

} // namespace

QImage OffscreenRenderer::renderSplit(const HullPackage *package, const AssemblyResult &assembly,
                                      const RenderStyle &style, const QSize &size, QString *error)
{
    const QSurfaceFormat format = renderFormat();

    QOffscreenSurface surface;
    surface.setFormat(format);
    surface.create();
    if (!surface.isValid()) {
        if (error)
            *error = QStringLiteral("offscreen surface is not valid");
        return {};
    }

    QOpenGLContext context;
    context.setFormat(format);
    if (!context.create() || !context.makeCurrent(&surface)) {
        if (error)
            *error = QStringLiteral("failed to create an OpenGL 3.3 core context");
        return {};
    }

    HullRenderer renderer;
    QImage image;
    do {
        if (!renderer.initialize(error))
            break;

        QOpenGLFramebufferObject fbo(size, QOpenGLFramebufferObject::Depth);
        if (!fbo.isValid()) {
            if (error)
                *error = QStringLiteral("failed to create the framebuffer object");
            break;
        }
        fbo.bind();

        auto *functions = context.functions();
        functions->glEnable(GL_DEPTH_TEST);
        functions->glClearDepthf(1.0f);

        const int splitX = int(size.width() * 0.45);
        const int leftWidth = splitX;
        const int rightWidth = size.width() - splitX;

        SideCamera side;
        side.frame(assembly.bounds);
        OrbitCamera orbit;
        orbit.frame(assembly.bounds);

        functions->glViewport(0, 0, leftWidth, size.height());
        functions->glScissor(0, 0, leftWidth, size.height());
        functions->glEnable(GL_SCISSOR_TEST);
        functions->glClearColor(0.11f, 0.12f, 0.14f, 1.0f);
        functions->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderer.draw(assembly, package, style,
                      side.view(), side.projection(float(leftWidth) / float(size.height())));

        functions->glViewport(splitX, 0, rightWidth, size.height());
        functions->glScissor(splitX, 0, rightWidth, size.height());
        functions->glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
        functions->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderer.draw(assembly, package, style,
                      orbit.view(), orbit.projection(float(rightWidth) / float(size.height())));

        functions->glDisable(GL_SCISSOR_TEST);
        functions->glFinish();

        image = fbo.toImage();
        fbo.release();
        renderer.release();
    } while (false);

    context.doneCurrent();
    return image;
}

} // namespace nlhc
