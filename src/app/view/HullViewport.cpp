#include "HullViewport.h"

#include <QMouseEvent>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QWheelEvent>

namespace nlhc {

HullViewport::HullViewport(QWidget *parent)
    : QOpenGLWidget(parent)
{
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    setFormat(format);
    setMouseTracking(true);
    setMinimumSize(480, 320);
}

HullViewport::~HullViewport()
{
    makeCurrent();
    m_renderer.release();
    doneCurrent();
}

void HullViewport::setPackage(const HullPackage *package)
{
    m_package = package;
    makeCurrent();
    m_renderer.clearCaches();
    doneCurrent();
    update();
}

void HullViewport::setAssembly(const AssemblyResult &assembly)
{
    m_assembly = assembly;
    if (!m_framed && m_assembly.bounds.valid) {
        m_side.frame(m_assembly.bounds);
        m_orbit.frame(m_assembly.bounds);
        m_framed = true;
    }
    update();
}

void HullViewport::setStyle(const RenderStyle &style)
{
    m_style = style;
    update();
}

void HullViewport::resetViews()
{
    m_side.reset();
    m_orbit.reset();
    update();
}

int HullViewport::splitY() const
{
    return qBound(1, int(height() * kSidePaneFraction), qMax(1, height() - 1));
}

bool HullViewport::inTopPane(int y) const
{
    return y < splitY();
}

void HullViewport::initializeGL()
{
    QString error;
    if (!m_renderer.initialize(&error)) {
        m_rendererError = error;
        qWarning("HullViewport: %s", qPrintable(error));
    }
}

void HullViewport::paintGL()
{
    auto *functions = context()->functions();
    functions->glEnable(GL_DEPTH_TEST);
    functions->glClearDepthf(1.0f);

    const qreal dpr = devicePixelRatioF();
    const int pixelWidth = qMax(1, int(width() * dpr));
    const int pixelHeight = qMax(1, int(height() * dpr));
    const int topHeight = qBound(1, int(pixelHeight * kSidePaneFraction), pixelHeight - 1);
    const int bottomHeight = pixelHeight - topHeight;

    // OpenGL's origin is bottom-left: the side view occupies the upper band.
    functions->glEnable(GL_SCISSOR_TEST);

    functions->glViewport(0, bottomHeight, pixelWidth, topHeight);
    functions->glScissor(0, bottomHeight, pixelWidth, topHeight);
    functions->glClearColor(0.11f, 0.12f, 0.14f, 1.0f);
    functions->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_renderer.draw(m_assembly, m_package, m_style, m_side.view(),
                    m_side.projection(float(pixelWidth) / float(topHeight)));

    functions->glViewport(0, 0, pixelWidth, bottomHeight);
    functions->glScissor(0, 0, pixelWidth, bottomHeight);
    functions->glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
    functions->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_renderer.draw(m_assembly, m_package, m_style, m_orbit.view(),
                    m_orbit.projection(float(pixelWidth) / float(bottomHeight)));

    // 1px separator between the two panes.
    functions->glScissor(0, bottomHeight - 1, pixelWidth, 1);
    functions->glClearColor(0.28f, 0.30f, 0.34f, 1.0f);
    functions->glClear(GL_COLOR_BUFFER_BIT);

    functions->glDisable(GL_SCISSOR_TEST);
}

void HullViewport::resizeGL(int w, int h)
{
    Q_UNUSED(w)
    Q_UNUSED(h)
}

void HullViewport::mousePressEvent(QMouseEvent *event)
{
    m_lastPos = event->position().toPoint();
    m_buttons = event->buttons();
    m_draggingLeft = event->button() == Qt::LeftButton;
    QOpenGLWidget::mousePressEvent(event);
}

void HullViewport::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    const QPoint delta = pos - m_lastPos;
    m_lastPos = pos;

    if (inTopPane(pos.y()))
        return; // the side view is a fixed reference strip

    const float paneHeight = float(qMax(1, height() - splitY()));
    if (m_buttons & Qt::LeftButton) {
        m_orbit.orbit(-float(delta.x()) * kOrbitDegreesPerPixel,
                      float(delta.y()) * kOrbitDegreesPerPixel);
        update();
    } else if (m_buttons & (Qt::MiddleButton | Qt::RightButton)) {
        m_orbit.pan(float(delta.x()), float(delta.y()), paneHeight);
        update();
    }
    QOpenGLWidget::mouseMoveEvent(event);
}

void HullViewport::mouseReleaseEvent(QMouseEvent *event)
{
    m_buttons = event->buttons();
    m_draggingLeft = false;
    QOpenGLWidget::mouseReleaseEvent(event);
}

void HullViewport::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !inTopPane(event->position().toPoint().y())) {
        m_orbit.reset();
        update();
    }
    QOpenGLWidget::mouseDoubleClickEvent(event);
}

void HullViewport::wheelEvent(QWheelEvent *event)
{
    const float steps = float(event->angleDelta().y()) / 120.0f;
    if (steps == 0.0f)
        return;
    if (inTopPane(event->position().toPoint().y())) {
        event->ignore();
        return; // fixed side view
    }
    m_orbit.zoom(qPow(0.88f, steps));
    update();
    event->accept();
}

void HullViewport::setDebugView(HullRenderer::DebugView view)
{
    m_renderer.setDebugView(view);
    update();
}

QString HullViewport::debugLegend() const
{
    return m_renderer.debugLegend();
}

QImage HullViewport::grabImage()
{
    return grabFramebuffer();
}

} // namespace nlhc
