#pragma once

#include <QOpenGLWidget>
#include <QPoint>

#include "Camera.h"
#include "HullRenderer.h"
#include "RenderStyle.h"
#include "nlhc/asset/HullPackage.h"
#include "nlhc/hull/HullAssembler.h"

namespace nlhc {

// Left pane: orthographic side view. Right pane: orbit view with mouse rotation.
// The side view is a fixed reference strip (the game's editor shows it small); the 3D
// pane takes the rest and is the only interactive one.
inline constexpr double kSidePaneFraction = 0.28;
inline constexpr float kOrbitDegreesPerPixel = 0.15f;

class HullViewport : public QOpenGLWidget
{
    Q_OBJECT
public:
    explicit HullViewport(QWidget *parent = nullptr);
    ~HullViewport() override;

    void setPackage(const HullPackage *package);
    void setAssembly(const AssemblyResult &assembly);
    void setStyle(const RenderStyle &style);
    const RenderStyle &style() const { return m_style; }
    void resetViews();
    void setOrbitAngles(float yawDegrees, float pitchDegrees);
    QImage grabImage();

    void setDebugView(HullRenderer::DebugView view);
    QString debugLegend() const;

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int width, int height) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    bool inTopPane(int y) const;
    int splitY() const;
    void applyStyleDefaults();

    const HullPackage *m_package = nullptr;
    AssemblyResult m_assembly;
    RenderStyle m_style;
    HullRenderer m_renderer;
    QString m_rendererError;
    SideCamera m_side;
    OrbitCamera m_orbit;
    bool m_framed = false;

    QPoint m_lastPos;
    Qt::MouseButtons m_buttons;
    bool m_draggingLeft = false;
};

} // namespace nlhc
