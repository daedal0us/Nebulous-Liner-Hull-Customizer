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

bool HullViewport::inRightPane(int x) const
{
    return x >= int(width() * 0.45);
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

    const int splitX = int(width() * 0.45);
    const int rightWidth = width() - splitX;
    const int leftWidth = splitX;

    functions->glViewport(0, 0, leftWidth, height());
    functions->glScissor(0, 0, leftWidth, height());
    functions->glEnable(GL_SCISSOR_TEST);
    functions->glClearColor(0.11f, 0.12f, 0.14f, 1.0f);
    functions->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_renderer.draw(m_assembly, m_package, m_style, m_side.view(),
                    m_side.projection(float(leftWidth) / float(qMax(1, height()))));

    functions->glViewport(splitX, 0, rightWidth, height());
    functions->glScissor(splitX, 0, rightWidth, height());
    functions->glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
    functions->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_renderer.draw(m_assembly, m_package, m_style, m_orbit.view(),
                    m_orbit.projection(float(rightWidth) / float(qMax(1, height()))));

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

    if (m_buttons & Qt::LeftButton) {
        if (inRightPane(pos.x()))
            m_orbit.orbit(-float(delta.x()) * 0.4f, float(delta.y()) * 0.4f);
        else
            m_side.pan(float(delta.x()), float(delta.y()), float(height()));
        update();
    } else if (m_buttons & (Qt::MiddleButton | Qt::RightButton)) {
        if (inRightPane(pos.x()))
            m_orbit.pan(float(delta.x()), float(delta.y()), float(height()));
        else
            m_side.pan(float(delta.x()), float(delta.y()), float(height()));
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
    if (event->button() == Qt::LeftButton) {
        if (inRightPane(event->position().toPoint().x()))
            m_orbit.reset();
        else
            m_side.reset();
        update();
    }
    QOpenGLWidget::mouseDoubleClickEvent(event);
}

void HullViewport::wheelEvent(QWheelEvent *event)
{
    const float steps = float(event->angleDelta().y()) / 120.0f;
    if (steps == 0.0f)
        return;
    const float factor = qPow(0.88f, steps);
    if (inRightPane(event->position().toPoint().x()))
        m_orbit.zoom(factor);
    else
        m_side.zoom(factor);
    update();
    event->accept();
}

QImage HullViewport::grabImage()
{
    return grabFramebuffer();
}

} // namespace nlhc
