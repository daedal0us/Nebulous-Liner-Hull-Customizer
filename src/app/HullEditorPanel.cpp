#include "HullEditorPanel.h"

#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace nlhc {
namespace {

QString segmentName(int segment)
{
    switch (segment) {
    case 0:
        return QObject::tr("Bow");
    case 1:
        return QObject::tr("Core");
    case 2:
        return QObject::tr("Stern");
    default:
        return QStringLiteral("?");
    }
}

void clearLayout(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
}

} // namespace

HullEditorPanel::HullEditorPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);

    auto *segmentsBox = new QGroupBox(tr("Primary structure"), this);
    auto *segmentsForm = new QFormLayout(segmentsBox);
    for (int i = 0; i < 3; ++i) {
        auto *box = new QComboBox(segmentsBox);
        box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, i](int index) {
            if (m_updating || !m_segmentBoxes[i])
                return;
            const QVariant data = m_segmentBoxes[i]->itemData(index);
            m_config.primary[i].key = data.toString();
            m_config.primary[i].dressing.clear();
            rebuildDressing();
            notifyChanged();
        });
        m_segmentBoxes[i] = box;
        segmentsForm->addRow(segmentName(i), box);
    }
    root->addWidget(segmentsBox);

    auto *dressingBox = new QGroupBox(tr("Dressing"), this);
    auto *dressingOuter = new QVBoxLayout(dressingBox);
    m_dressingHint = new QLabel(dressingBox);
    m_dressingHint->setWordWrap(true);
    dressingOuter->addWidget(m_dressingHint);
    auto *scroll = new QScrollArea(dressingBox);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_dressingContainer = new QWidget(scroll);
    m_dressingLayout = new QVBoxLayout(m_dressingContainer);
    m_dressingLayout->setContentsMargins(0, 0, 0, 0);
    scroll->setWidget(m_dressingContainer);
    dressingOuter->addWidget(scroll, 1);
    root->addWidget(dressingBox, 1);

    auto *secondaryBox = new QGroupBox(tr("Superstructure"), this);
    auto *secondaryOuter = new QVBoxLayout(secondaryBox);
    m_secondaryLayout = new QVBoxLayout;
    secondaryOuter->addLayout(m_secondaryLayout);
    m_addSecondary = new QPushButton(tr("Add superstructure"), secondaryBox);
    connect(m_addSecondary, &QPushButton::clicked, this, [this] {
        if (!m_hullType || m_hullType->superstructures.isEmpty())
            return;
        SecondaryConfig config;
        config.key = m_hullType->superstructures.first();
        config.segment = 1;
        config.snap = 0;
        m_config.secondary.append(config);
        rebuildSecondary();
        notifyChanged();
    });
    secondaryOuter->addWidget(m_addSecondary);
    root->addWidget(secondaryBox);
}

void HullEditorPanel::setPackage(const HullPackage *package)
{
    m_package = package;
}

void HullEditorPanel::setHullType(const HullTypeDef *hullType)
{
    m_hullType = hullType;
    rebuildSegments();
    rebuildSecondary();
}

void HullEditorPanel::setConfig(const RandomHullConfig &config)
{
    m_config = config;
    syncFromConfig();
}

void HullEditorPanel::setEditable(bool editable)
{
    m_editable = editable;
    setEnabled(editable);
}

const Piece *HullEditorPanel::pieceForSegment(int segment) const
{
    if (!m_package || segment < 0 || segment >= 3)
        return nullptr;
    return m_package->piece(m_config.primary[segment].key);
}

void HullEditorPanel::rebuildSegments()
{
    m_updating = true;
    for (int i = 0; i < 3; ++i) {
        QComboBox *box = m_segmentBoxes[i];
        if (!box)
            continue;
        box->clear();
        if (!m_hullType || !m_package) {
            box->setEnabled(false);
            continue;
        }
        const QStringList &ids = i == 0 ? m_hullType->bows : (i == 1 ? m_hullType->cores : m_hullType->sterns);
        for (const QString &id : ids) {
            const Piece *piece = m_package->piece(id);
            const QString label = piece ? QStringLiteral("%1  [%2]").arg(piece->debugName, piece->key.left(8))
                                        : id;
            box->addItem(label, id);
        }
        box->setEnabled(m_editable);
    }
    m_updating = false;
    syncFromConfig();
}

void HullEditorPanel::rebuildDressing()
{
    clearLayout(m_dressingLayout);
    m_dressingHint->clear();

    int totalPoints = 0;
    for (int segment = 0; segment < 3; ++segment) {
        const Piece *piece = pieceForSegment(segment);
        if (!piece)
            continue;
        for (const DressingPoint &point : piece->dressingPoints) {
            ++totalPoints;
            auto *row = new QWidget(m_dressingContainer);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            auto *label = new QLabel(QStringLiteral("%1 · %2").arg(segmentName(segment)).arg(point.index), row);
            auto *box = new QComboBox(row);
            for (int i = 0; i < point.variantNames.size(); ++i)
                box->addItem(QStringLiteral("%1 %2").arg(i + 1).arg(point.variantNames.at(i)), i);
            if (box->count() == 0) {
                for (int i = 0; i < point.variants.size(); ++i)
                    box->addItem(QString::number(i), i);
            }
            if (point.allowEmpty)
                box->addItem(tr("(empty)"), point.variants.size());

            int current = 0;
            if (segment < 3 && point.index < m_config.primary[segment].dressing.size())
                current = m_config.primary[segment].dressing.at(point.index);
            const int found = box->findData(current);
            if (found >= 0) {
                box->setCurrentIndex(found);
            } else {
                box->addItem(tr("invalid (%1)").arg(current), current);
                box->setCurrentIndex(box->count() - 1);
            }
            const int guardSegment = segment;
            const int guardPoint = point.index;
            connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [this, box, guardSegment, guardPoint](int) {
                        if (m_updating)
                            return;
                        QVector<int> &dressing = m_config.primary[guardSegment].dressing;
                        const Piece *piece = pieceForSegment(guardSegment);
                        if (!piece)
                            return;
                        if (dressing.size() <= guardPoint)
                            dressing.resize(piece->dressingPoints.size());
                        dressing[guardPoint] = box->currentData().toInt();
                        notifyChanged();
                    });
            rowLayout->addWidget(label);
            rowLayout->addWidget(box, 1);
            m_dressingLayout->addWidget(row);
        }
    }

    if (totalPoints == 0)
        m_dressingHint->setText(tr("The selected pieces have no dressing points."));
    m_dressingLayout->addStretch(1);
    setEnabled(m_editable);
}

void HullEditorPanel::rebuildSecondary()
{
    clearLayout(m_secondaryLayout);
    if (!m_package || !m_hullType) {
        m_addSecondary->setEnabled(false);
        return;
    }
    m_addSecondary->setEnabled(m_editable);

    for (int index = 0; index < m_config.secondary.size(); ++index) {
        auto *row = new QWidget(m_dressingContainer);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);

        auto *pieceBox = new QComboBox(row);
        for (const QString &id : m_hullType->superstructures) {
            const Piece *piece = m_package->piece(id);
            pieceBox->addItem(piece ? piece->debugName : id, id);
        }
        auto *segmentBox = new QComboBox(row);
        for (int segment = 0; segment < 3; ++segment)
            segmentBox->addItem(segmentName(segment), segment);
        auto *snapBox = new QComboBox(row);
        auto *remove = new QPushButton(tr("Remove"), row);

        const SecondaryConfig config = m_config.secondary.at(index);
        int pieceIndex = pieceBox->findData(config.key);
        if (pieceIndex < 0 && !config.key.isEmpty()) {
            pieceBox->addItem(tr("unknown (%1)").arg(config.key), config.key);
            pieceIndex = pieceBox->count() - 1;
        }
        pieceBox->setCurrentIndex(qMax(0, pieceIndex));
        segmentBox->setCurrentIndex(qBound(0, config.segment, 2));

        auto fillSnaps = [this, snapBox](int segment, int selected) {
            const_cast<HullEditorPanel *>(this)->m_updating = true;
            snapBox->clear();
            const Piece *piece = pieceForSegment(segment);
            const int count = piece ? piece->superstructureSnaps.size() : 0;
            for (int i = 0; i < count; ++i)
                snapBox->addItem(QString::number(i), i);
            const int found = snapBox->findData(selected);
            if (found >= 0) {
                snapBox->setCurrentIndex(found);
            } else {
                snapBox->addItem(tr("invalid (%1)").arg(selected), selected);
                snapBox->setCurrentIndex(snapBox->count() - 1);
            }
            const_cast<HullEditorPanel *>(this)->m_updating = false;
        };
        fillSnaps(config.segment, config.snap);

        const int guardIndex = index;
        connect(pieceBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, pieceBox, guardIndex](int) {
            if (m_updating || guardIndex >= m_config.secondary.size())
                return;
            m_config.secondary[guardIndex].key = pieceBox->currentData().toString();
            notifyChanged();
        });
        connect(segmentBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, segmentBox, snapBox, fillSnaps, guardIndex](int) {
                    if (m_updating || guardIndex >= m_config.secondary.size())
                        return;
                    m_config.secondary[guardIndex].segment = segmentBox->currentData().toInt();
                    fillSnaps(segmentBox->currentData().toInt(), 0);
                    m_config.secondary[guardIndex].snap = snapBox->currentData().toInt();
                    notifyChanged();
                });
        connect(snapBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, snapBox, guardIndex](int) {
            if (m_updating || guardIndex >= m_config.secondary.size())
                return;
            m_config.secondary[guardIndex].snap = snapBox->currentData().toInt();
            notifyChanged();
        });
        connect(remove, &QPushButton::clicked, this, [this, guardIndex] {
            if (guardIndex < m_config.secondary.size()) {
                m_config.secondary.remove(guardIndex);
                rebuildSecondary();
                notifyChanged();
            }
        });

        rowLayout->addWidget(pieceBox, 2);
        rowLayout->addWidget(segmentBox, 1);
        rowLayout->addWidget(snapBox, 1);
        rowLayout->addWidget(remove);
        m_secondaryLayout->addWidget(row);
    }
    setEnabled(m_editable);
}

void HullEditorPanel::syncFromConfig()
{
    m_updating = true;
    for (int i = 0; i < 3; ++i) {
        if (!m_segmentBoxes[i])
            continue;
        const int found = m_segmentBoxes[i]->findData(m_config.primary[i].key);
        if (found >= 0) {
            m_segmentBoxes[i]->setCurrentIndex(found);
        } else if (!m_config.primary[i].key.isEmpty()) {
            m_segmentBoxes[i]->addItem(tr("unknown (%1)").arg(m_config.primary[i].key.left(8)),
                                       m_config.primary[i].key);
            m_segmentBoxes[i]->setCurrentIndex(m_segmentBoxes[i]->count() - 1);
        }
    }
    m_updating = false;
    rebuildDressing();
    rebuildSecondary();
}

void HullEditorPanel::notifyChanged()
{
    emit configChanged(m_config);
}

} // namespace nlhc
