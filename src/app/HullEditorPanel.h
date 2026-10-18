#pragma once

#include <QWidget>

#include "nlhc/asset/HullPackage.h"
#include "nlhc/fleet/HullConfig.h"

class QComboBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace nlhc {

// Edits the bow/core/stern pieces, their dressing variants and the superstructure list.
class HullEditorPanel : public QWidget
{
    Q_OBJECT
public:
    explicit HullEditorPanel(QWidget *parent = nullptr);

    void setPackage(const HullPackage *package);
    void setHullType(const HullTypeDef *hullType);
    void setConfig(const RandomHullConfig &config);
    void setEditable(bool editable);

    const RandomHullConfig &config() const { return m_config; }

signals:
    void configChanged(const nlhc::RandomHullConfig &config);

private:
    void rebuildSegments();
    void rebuildDressing();
    void rebuildSecondary();
    void syncFromConfig();
    void notifyChanged();
    void normalizeAfterPieceChange();
    const Piece *pieceForSegment(int segment) const;

    const HullPackage *m_package = nullptr;
    const HullTypeDef *m_hullType = nullptr;
    RandomHullConfig m_config;
    bool m_editable = false;
    bool m_updating = false;

    QComboBox *m_segmentBoxes[3] = {nullptr, nullptr, nullptr};
    QLabel *m_dressingHint = nullptr;
    QVBoxLayout *m_dressingLayout = nullptr;
    QWidget *m_dressingContainer = nullptr;
    QVBoxLayout *m_secondaryLayout = nullptr;
    QPushButton *m_addSecondary = nullptr;
};

} // namespace nlhc
