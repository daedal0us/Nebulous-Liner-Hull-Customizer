#include <QtTest>

#include "HullEditorPanel.h"

using namespace nlhc;

// Covers the removal path MainWindow uses when a superstructure points at a
// mount point the segment does not have.
class TstEditorPanel : public QObject
{
    Q_OBJECT

private slots:
    void disablingASegmentDropsItsEntry();
};

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

QTEST_MAIN(TstEditorPanel)
#include "tst_editorpanel.moc"
