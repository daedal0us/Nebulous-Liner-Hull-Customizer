#include "HullEditorPanel.h"

#include <QCollator>
#include <QComboBox>
#include <QFormLayout>
#include <QSet>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>

#include "nlhc/hull/DressingLayout.h"

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

// Pieces come out of the catalog in prefab order (B, A, C ...). The game lists them
// naturally, so sort by display name with numeric awareness (A, B, C / 1, 2, 3, 10).
QStringList sortedPieceIds(const HullPackage *package, const QStringList &ids)
{
    if (!package || ids.isEmpty())
        return ids;
    QStringList sorted = ids;
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(sorted.begin(), sorted.end(), [package, &collator](const QString &lhs, const QString &rhs) {
        const Piece *left = package->piece(lhs);
        const Piece *right = package->piece(rhs);
        const QString leftName = left ? left->debugName : lhs;
        const QString rightName = right ? right->debugName : rhs;
        return collator.compare(leftName, rightName) < 0;
    });
    return sorted;
}

// Drop items that are no longer candidates (typically a stale "unknown (...)" fallback left
// over from the previously selected hull type).
void pruneToCandidates(QComboBox *box, const QStringList &candidateKeys)
{
    const QSet<QString> allowed(candidateKeys.begin(), candidateKeys.end());
    for (int index = box->count() - 1; index >= 0; --index) {
        if (!allowed.contains(box->itemData(index).toString()))
            box->removeItem(index);
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
        box->setMinimumContentsLength(12);
        connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, i](int index) {
            if (m_updating || !m_segmentBoxes[i])
                return;
            const QVariant data = m_segmentBoxes[i]->itemData(index);
            if (m_config.primary[i].key == data.toString())
                return;
            m_config.primary[i].key = data.toString();
            m_config.primary[i].dressing.clear();
            normalizeAfterPieceChange();
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
    if (!m_package || !m_hullType || segment < 0 || segment >= 3)
        return nullptr;
    const QStringList &ids = segment == 0 ? m_hullType->bows
        : (segment == 1 ? m_hullType->cores : m_hullType->sterns);
    const QString &key = m_config.primary[segment].key;
    for (const QString &id : ids) {
        const Piece *piece = m_package->piece(id);
        if (piece && piece->key.compare(key, Qt::CaseInsensitive) == 0)
            return piece;
    }
    return nullptr;
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
        const QStringList ids = sortedPieceIds(m_package,
            i == 0 ? m_hullType->bows : (i == 1 ? m_hullType->cores : m_hullType->sterns));
        for (const QString &id : ids) {
            const Piece *piece = m_package->piece(id);
            const QString label = piece ? QStringLiteral("%1  [%2]").arg(piece->debugName, piece->key.left(8))
                                        : id;
            box->addItem(label, piece ? piece->key : id);
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
    if (!m_package || !m_hullType) {
        setEnabled(m_editable);
        return;
    }

    const QVector<DressingBinding> bindings =
        bindDressing(pieceForSegment(0), pieceForSegment(1), pieceForSegment(2), m_config);

    if (bindings.isEmpty())
        m_dressingHint->setText(tr("The selected pieces have no dressing points."));

    for (const DressingBinding &binding : bindings) {
        if (!binding.point)
            continue;
        const DressingPoint &point = *binding.point;

        auto *row = new QWidget(m_dressingContainer);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto *label = new QLabel(QStringLiteral("%1 · %2").arg(segmentName(binding.ownerSegment)).arg(binding.pointIndex + 1), row);
        auto *box = new QComboBox(row);
        box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        box->setMinimumContentsLength(10);
        for (int i = 0; i < point.variantNames.size(); ++i)
            box->addItem(QStringLiteral("%1 %2").arg(i + 1).arg(point.variantNames.at(i)), i);
        if (box->count() == 0) {
            for (int i = 0; i < point.variants.size(); ++i)
                box->addItem(QString::number(i), i);
        }
        if (point.allowEmpty)
            box->addItem(tr("(empty)"), point.variants.size());

        const int highest = point.allowEmpty ? point.variants.size() : point.variants.size() - 1;
        const int current = binding.known ? binding.value : -1;
        const int found = box->findData(current);
        if (found >= 0) {
            box->setCurrentIndex(found);
        } else {
            box->addItem(current < 0 ? tr("not configured") : tr("invalid (%1)").arg(current), current);
            box->setCurrentIndex(box->count() - 1);
        }
        Q_UNUSED(highest)

        connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, box, binding](int) {
            if (m_updating)
                return;
            applyDressingValue(binding, box->currentData().toInt(), &m_config);
            notifyChanged();
        });
        rowLayout->addWidget(label);
        rowLayout->addWidget(box, 1);
        m_dressingLayout->addWidget(row);
    }

    m_dressingLayout->addStretch(1);
    setEnabled(m_editable);
}

void HullEditorPanel::normalizeAfterPieceChange()
{
    if (!m_package || !m_hullType)
        return;
    const Piece *bow = pieceForSegment(0);
    const Piece *core = pieceForSegment(1);
    const Piece *stern = pieceForSegment(2);
    normalizeDressing(bow, core, stern, &m_config);
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
        pieceBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        pieceBox->setMinimumContentsLength(10);
        for (const QString &id : sortedPieceIds(m_package, m_hullType->superstructures)) {
            const Piece *piece = m_package->piece(id);
            pieceBox->addItem(piece ? piece->debugName : id, piece ? piece->key : id);
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
        QComboBox *box = m_segmentBoxes[i];
        if (!box)
            continue;

        // Keep only real candidates so a previous hull type cannot leave entries behind.
        QStringList candidateKeys;
        candidateKeys.reserve(box->count());
        for (int index = 0; index < box->count(); ++index) {
            const QString key = box->itemData(index).toString();
            if (m_package && m_package->piece(key))
                candidateKeys.append(key);
        }
        if (m_hullType) {
            const QStringList &ids = i == 0 ? m_hullType->bows : (i == 1 ? m_hullType->cores : m_hullType->sterns);
            candidateKeys.clear();
            for (const QString &id : ids) {
                const Piece *piece = m_package ? m_package->piece(id) : nullptr;
                if (piece)
                    candidateKeys.append(piece->key);
            }
        }
        pruneToCandidates(box, candidateKeys);
        box->setEnabled(m_editable && box->count() > 0);

        const int found = box->findData(m_config.primary[i].key);
        if (found >= 0) {
            box->setCurrentIndex(found);
        } else if (!m_config.primary[i].key.isEmpty()) {
            box->addItem(tr("unknown (%1)").arg(m_config.primary[i].key.left(8)),
                         m_config.primary[i].key);
            box->setCurrentIndex(box->count() - 1);
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
