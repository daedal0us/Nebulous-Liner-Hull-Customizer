#include <QtTest>

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QDir>
#include <QTemporaryDir>

#include "HullEditorPanel.h"
#include "MockAssets.h"

using namespace nlhc;
using namespace nlhc::test;

namespace {

QList<QCheckBox *> superstructureBoxes(HullEditorPanel &panel)
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QList<QCheckBox *> boxes;
    for (QCheckBox *box : panel.findChildren<QCheckBox *>()) {
        const QString text = box->text().toLower();
        if (text == QLatin1String("bow") || text == QLatin1String("core") || text == QLatin1String("stern"))
            boxes.append(box);
    }
    return boxes;
}

} // namespace

// Covers the removal path MainWindow uses when a superstructure points at a
// mount point the segment does not have, and the per-segment rows it shares.
class TstEditorPanel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void disablingASegmentDropsItsEntry();
    void untickingOneSegmentKeepsTheOthers();
    void segmentsWithoutMountPointsStayDisabled();
    void switchingTheSegmentPieceUpdatesItsRow();

private:
    QString m_root;
    HullPackage m_package;
};

void TstEditorPanel::initTestCase()
{
    m_root = QDir(scratchRoot()).absoluteFilePath(QStringLiteral("editorpanel"));
    QDir().remove(m_root);
    QVERIFY(QDir().mkpath(m_root));
    const QString root = m_root;
    const QString built = createMockPackage(root);
    QVERIFY2(!built.isEmpty(), "mock package was not written");
    QString error;
    QVERIFY2(m_package.load(root, &error), qPrintable(error));
}

void TstEditorPanel::disablingASegmentDropsItsEntry()
{
    HullEditorPanel panel;

    SecondaryConfig bow;
    bow.key = QStringLiteral("bow-piece");
    bow.segment = 0;
    SecondaryConfig core;
    core.key = QStringLiteral("core-piece");
    core.segment = 1;

    RandomHullConfig config;
    config.secondary = {bow, core};
    panel.setConfig(config);
    QCOMPARE(panel.config().secondary.size(), 2);

    int changes = 0;
    connect(&panel, &HullEditorPanel::configChanged, this,
            [&changes](const RandomHullConfig &) { ++changes; });

    panel.disableSecondary(0);
    QCOMPARE(panel.config().secondary.size(), 1);
    QCOMPARE(panel.config().secondary.first().segment, 1);
    QCOMPARE(changes, 1);

    // A segment without an entry must not report a change.
    panel.disableSecondary(2);
    QCOMPARE(panel.config().secondary.size(), 1);
    QCOMPARE(changes, 1);
}

void TstEditorPanel::untickingOneSegmentKeepsTheOthers()
{
    const HullTypeDef *type = m_package.hullType(QStringLiteral("Stock/Bulk Hauler"));
    QVERIFY(type);

    HullEditorPanel panel;
    panel.setPackage(&m_package);
    panel.setHullType(type);
    panel.setEditable(true);

    RandomHullConfig config;
    config.primary[0].key = keyBow();
    config.primary[1].key = keyCore();
    config.primary[2].key = keyStern();
    SecondaryConfig coreEntry;
    coreEntry.key = keySuperstructure();
    coreEntry.segment = 1;
    coreEntry.snap = 0;
    SecondaryConfig sternEntry;
    sternEntry.key = keySuperstructure();
    sternEntry.segment = 2;
    sternEntry.snap = 0;
    config.secondary = {coreEntry, sternEntry};
    panel.setConfig(config);

    QList<QCheckBox *> boxes = superstructureBoxes(panel);
    QCOMPARE(boxes.size(), 3);
    QVERIFY(!boxes.at(0)->isChecked());
    QVERIFY(boxes.at(1)->isChecked());
    QVERIFY(boxes.at(2)->isChecked());

    boxes.at(1)->setChecked(false); // untick the core superstructure

    boxes = superstructureBoxes(panel);
    QCOMPARE(boxes.size(), 3);
    QVERIFY2(!boxes.at(1)->isChecked(), "core stayed ticked");
    QVERIFY2(boxes.at(2)->isChecked(), "unticking core also unticked the stern");
    QCOMPARE(panel.config().secondary.size(), 1);
    QCOMPARE(panel.config().secondary.first().segment, 2);
}

void TstEditorPanel::segmentsWithoutMountPointsStayDisabled()
{
    const HullTypeDef *type = m_package.hullType(QStringLiteral("Test/Dressing"));
    QVERIFY(type);

    HullEditorPanel panel;
    panel.setPackage(&m_package);
    panel.setHullType(type);
    panel.setEditable(true);

    RandomHullConfig config;
    config.primary[0].key = keyDressingBow(); // no superstructure mount points
    config.primary[1].key = keyDressingCore(); // no superstructure mount points
    config.primary[2].key = keyStern();
    SecondaryConfig sternEntry;
    sternEntry.key = keySuperstructure();
    sternEntry.segment = 2;
    sternEntry.snap = 0;
    config.secondary = {sternEntry};
    panel.setConfig(config);

    QList<QCheckBox *> boxes = superstructureBoxes(panel);
    QCOMPARE(boxes.size(), 3);
    QVERIFY2(!boxes.at(0)->isEnabled(), "bow has no mount point but its row is enabled");
    QVERIFY2(!boxes.at(1)->isEnabled(), "core has no mount point but its row is enabled");
    QVERIFY(boxes.at(2)->isEnabled());
    QVERIFY(boxes.at(2)->isChecked());
}

void TstEditorPanel::switchingTheSegmentPieceUpdatesItsRow()
{
    const HullTypeDef *type = m_package.hullType(QStringLiteral("Test/SegmentSwitch"));
    QVERIFY(type);

    HullEditorPanel panel;
    panel.setPackage(&m_package);
    panel.setHullType(type);
    panel.setEditable(true);

    RandomHullConfig config;
    config.primary[0].key = keyBow();          // has a mount point
    config.primary[1].key = keyCore();
    config.primary[2].key = keyStern();
    panel.setConfig(config);

    QList<QCheckBox *> boxes = superstructureBoxes(panel);
    QCOMPARE(boxes.size(), 3);
    QVERIFY2(boxes.at(0)->isEnabled(), "bow has a mount point but its row starts disabled");

    QGroupBox *primaryBox = panel.findChildren<QGroupBox *>().value(0);
    QVERIFY(primaryBox);
    QComboBox *bowBox = primaryBox->findChildren<QComboBox *>().value(0);
    QVERIFY(bowBox);

    // Switch the bow to the piece without superstructure mount points.
    const int bare = bowBox->findData(keyDressingBow());
    QVERIFY(bare >= 0);
    bowBox->setCurrentIndex(bare);
    boxes = superstructureBoxes(panel);
    QCOMPARE(boxes.size(), 3);
    QVERIFY2(!boxes.at(0)->isEnabled(), "row stayed enabled after losing its mount points");

    // And back again.
    const int mounted = bowBox->findData(keyBow());
    QVERIFY(mounted >= 0);
    bowBox->setCurrentIndex(mounted);
    boxes = superstructureBoxes(panel);
    QCOMPARE(boxes.size(), 3);
    QVERIFY2(boxes.at(0)->isEnabled(), "row stayed disabled after regaining its mount points");
}

QTEST_MAIN(TstEditorPanel)
#include "tst_editorpanel.moc"
