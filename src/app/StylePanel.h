#pragma once

#include <QGroupBox>

#include "view/RenderStyle.h"
#include "nlhc/fleet/HullConfig.h"

class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;

namespace nlhc {

class StylePanel : public QGroupBox
{
    Q_OBJECT
public:
    explicit StylePanel(QWidget *parent = nullptr);

    void setStyle(const RenderStyle &style);
    const RenderStyle &style() const { return m_style; }

    void setHullTint(const QColor &tint);
    void setTextureVariation(const QVector3D &variation);

signals:
    void styleChanged(const nlhc::RenderStyle &style);
    void textureVariationRerollRequested();

private:
    void pickColor(bool trim);
    void emitStyle();
    void updateLabels();

    RenderStyle m_style;
    QPushButton *m_baseButton = nullptr;
    QPushButton *m_trimButton = nullptr;
    QSlider *m_tintSlider = nullptr;
    QCheckBox *m_tintEnabled = nullptr;
    QLabel *m_tintLabel = nullptr;
    QLabel *m_variationLabel = nullptr;
};

} // namespace nlhc
