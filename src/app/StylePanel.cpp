#include "StylePanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>

namespace nlhc {
namespace {

QString colorText(const QColor &color)
{
    return color.name(QColor::HexRgb);
}

void paintButton(QPushButton *button, const QColor &color)
{
    const bool dark = color.lightnessF() < 0.55;
    button->setText(colorText(color));
    button->setStyleSheet(QStringLiteral("background-color: %1; color: %2; padding: 4px;")
                              .arg(color.name(), dark ? QStringLiteral("#ffffff") : QStringLiteral("#101010")));
}

} // namespace

StylePanel::StylePanel(QWidget *parent)
    : QGroupBox(parent)
{
    auto *layout = new QGridLayout(this);

    m_baseButton = new QPushButton(this);
    m_trimButton = new QPushButton(this);
    connect(m_baseButton, &QPushButton::clicked, this, [this] { pickColor(false); });
    connect(m_trimButton, &QPushButton::clicked, this, [this] { pickColor(true); });

    m_tintSlider = new QSlider(Qt::Horizontal, this);
    m_tintSlider->setRange(35, 80);
    m_tintSlider->setValue(60);
    m_tintEnabled = new QCheckBox(this);
    m_tintLabel = new QLabel(this);

    auto *baseLabel = new QLabel(this);
    baseLabel->setText(tr("Base color"));
    auto *trimLabel = new QLabel(this);
    trimLabel->setText(tr("Trim color"));
    layout->addWidget(baseLabel, 0, 0);
    layout->addWidget(m_baseButton, 0, 1);
    layout->addWidget(trimLabel, 1, 0);
    layout->addWidget(m_trimButton, 1, 1);
    layout->addWidget(new QLabel(tr("Hull tint"), this), 2, 0);
    layout->addWidget(m_tintSlider, 2, 1);
    layout->addWidget(m_tintEnabled, 2, 2);
    layout->addWidget(m_tintLabel, 2, 3);
    layout->setColumnStretch(1, 1);

    connect(m_tintSlider, &QSlider::valueChanged, this, [this](int value) {
        m_style.hullTint = QColor::fromRgbF(value / 100.0f, value / 100.0f, value / 100.0f);
        updateLabels();
        emitStyle();
    });
    connect(m_tintEnabled, &QCheckBox::toggled, this, [this](bool enabled) {
        m_style.applyHullTint = enabled;
        emitStyle();
    });

    m_tintEnabled->setText(tr("Preview in-game tint"));
    updateLabels();
}

void StylePanel::setStyle(const RenderStyle &style)
{
    m_style = style;
    paintButton(m_baseButton, m_style.baseColor);
    paintButton(m_trimButton, m_style.trimColor);
    m_tintSlider->blockSignals(true);
    m_tintSlider->setValue(int(qRound(m_style.hullTint.lightnessF() * 100.0f)));
    m_tintSlider->blockSignals(false);
    m_tintEnabled->blockSignals(true);
    m_tintEnabled->setChecked(m_style.applyHullTint);
    m_tintEnabled->blockSignals(false);
    updateLabels();
}

void StylePanel::setHullTint(const QColor &tint)
{
    m_style.hullTint = tint;
    m_tintSlider->blockSignals(true);
    m_tintSlider->setValue(int(qRound(tint.lightnessF() * 100.0f)));
    m_tintSlider->blockSignals(false);
    updateLabels();
}

void StylePanel::pickColor(bool trim)
{
    const QColor current = trim ? m_style.trimColor : m_style.baseColor;
    const QColor chosen = QColorDialog::getColor(current, this, trim ? tr("Trim color") : tr("Base color"));
    if (!chosen.isValid())
        return;
    if (trim) {
        m_style.trimColor = chosen;
        paintButton(m_trimButton, chosen);
    } else {
        m_style.baseColor = chosen;
        paintButton(m_baseButton, chosen);
    }
    emitStyle();
}

void StylePanel::emitStyle()
{
    emit styleChanged(m_style);
}

void StylePanel::updateLabels()
{
    paintButton(m_baseButton, m_style.baseColor);
    paintButton(m_trimButton, m_style.trimColor);
    m_tintLabel->setText(QStringLiteral("%1%").arg(m_tintSlider->value()));
}

} // namespace nlhc
