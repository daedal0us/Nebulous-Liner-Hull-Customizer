#include <QtTest>

#include <QDir>
#include <QTemporaryDir>

#include "MockAssets.h"
#include "nlhc/fleet/FleetDocument.h"
#include "nlhc/hull/Validate.h"

using namespace nlhc;

namespace {

bool hasCode(const QVector<Diagnostic> &diagnostics, const char *code)
{
    for (const Diagnostic &diagnostic : diagnostics) {
        if (diagnostic.code == QLatin1String(code))
            return true;
    }
    return false;
}

bool hasSeverity(const QVector<Diagnostic> &diagnostics, Severity severity)
{
    for (const Diagnostic &diagnostic : diagnostics) {
        if (diagnostic.severity == severity)
            return true;
    }
    return false;
}

} // namespace

class TstValidate : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // One test per row of the game-side tolerance table.
    void missingHullConfigIsReported();
    void unsupportedHullConfigIsReported();
    void primaryStructureLengthIsReportedAndRepaired();
    void unknownSegmentIsReportedAndRepaired();
    void shortDressingIsReportedAndRepaired();
    void dressingOutOfRangeIsReportedAndRepaired();
    void unknownSecondaryIsReportedAndDropped();
    void secondarySegmentOutOfRangeIsReportedAndRepaired();
    void secondarySnapOutOfRangeIsReportedAndRepaired();
    void largeFleetsKeepEveryRowEditable();

    void cleanConfigProducesNoDiagnostics();
    void repairIsIdempotent();
    void randomizeFollowsGameRules();
    void describeDiagnosticIsHumanReadable();

private:
    QString writeFleet(const QString &name, const QString &xml) const;

    QTemporaryDir m_dir{nlhc::test::scratchTemplate(QStringLiteral("nlhc-validate"))};
    HullPackage m_package;
};

void TstValidate::initTestCase()
{
    QVERIFY2(m_dir.isValid(), qPrintable(m_dir.errorString()));
    QString error;
    QVERIFY2(m_package.load(test::createMockPackage(m_dir.filePath(QStringLiteral("package"))), &error),
             qPrintable(error));
}

QString TstValidate::writeFleet(const QString &name, const QString &xml) const
{
    const QString path = m_dir.filePath(name);
    test::writeBytes(path, xml.toUtf8());
    return path;
}

void TstValidate::missingHullConfigIsReported()
{
    const QString path = writeFleet(QStringLiteral("row1-missing.fleet"),
                                    test::fleetXml({test::shipXml(0, QStringLiteral("Stock/Bulk Hauler"), QString())}));
    FleetDocument document;
    QVector<Diagnostic> diagnostics;
    QVERIFY(document.load(path, &diagnostics, nullptr));
    QVERIFY(hasCode(diagnostics, diag::MissingHullConfig));
    QVERIFY(hasSeverity(diagnostics, Severity::Error));
    QVERIFY(!document.hullConfig(0).has_value());

    document.setPackage(&m_package);
    QVERIFY(!document.ship(0).editable); // the game would rebuild this ship
}

void TstValidate::unsupportedHullConfigIsReported()
{
    QString body = test::defaultHullConfigBody();
    body.replace(QStringLiteral("RandomHullConfiguration"), QStringLiteral("LegacyHullConfiguration"));
    const QString path = writeFleet(QStringLiteral("row1-unsupported.fleet"),
                                    test::fleetXml({test::shipXml(0, QStringLiteral("Stock/Bulk Hauler"), body)}));
    FleetDocument document;
    QVector<Diagnostic> diagnostics;
    QVERIFY(document.load(path, &diagnostics, nullptr));
    QVERIFY(hasCode(diagnostics, diag::UnsupportedHullConfig));
    QVERIFY(!document.isRandomHullConfig(0));
}

void TstValidate::primaryStructureLengthIsReportedAndRepaired()
{
    const QString body = QStringLiteral("        <HullConfig xsi:type=\"RandomHullConfiguration\">\n"
                                        "          <PrimaryStructure>\n"
                                        "            <SegmentConfiguration><Key>%1</Key><Dressing /></SegmentConfiguration>\n"
                                        "            <SegmentConfiguration><Key>%2</Key><Dressing /></SegmentConfiguration>\n"
                                        "            <SegmentConfiguration><Key>%3</Key><Dressing /></SegmentConfiguration>\n"
                                        "            <SegmentConfiguration><Key>%1</Key><Dressing /></SegmentConfiguration>\n"
                                        "          </PrimaryStructure>\n"
                                        "          <HullTint><r>0.5</r><g>0.5</g><b>0.5</b><a>1</a></HullTint>\n"
                                        "        </HullConfig>\n")
                               .arg(test::keyBow(), test::keyCore(), test::keyStern());
    const QString path = writeFleet(QStringLiteral("row2-primary.fleet"),
                                    test::fleetXml({test::shipXml(0, QStringLiteral("Stock/Bulk Hauler"), body)}));
    FleetDocument document;
    QVector<Diagnostic> diagnostics;
    QVERIFY(document.load(path, &diagnostics, nullptr));
    QVERIFY(hasCode(diagnostics, diag::PrimaryStructureSize));

    // extra segments are dropped, never appended to the config
    const auto config = document.hullConfig(0);
    QVERIFY(config.has_value());
    QCOMPARE(config->primary[RandomHullConfig::Stern].key, test::keyStern());
    QVERIFY(checkConfig(m_package, QStringLiteral("Stock/Bulk Hauler"), *config).isEmpty());
}

void TstValidate::unknownSegmentIsReportedAndRepaired()
{
    RandomHullConfig config = test::bulkConfig();
    config.primary[RandomHullConfig::Core].key = QStringLiteral("deadbeef-0000-0000-0000-000000000000");

    const QVector<Diagnostic> diagnostics = checkConfig(m_package, QStringLiteral("Stock/Bulk Hauler"), config);
    QVERIFY(hasCode(diagnostics, diag::UnknownSegment));
    QVERIFY(hasSeverity(diagnostics, Severity::Error));

    QVERIFY(repairConfig(m_package, QStringLiteral("Stock/Bulk Hauler"), &config));
    QCOMPARE(config.primary[RandomHullConfig::Core].key, test::keyCore());
    QVERIFY(checkConfig(m_package, QStringLiteral("Stock/Bulk Hauler"), config).isEmpty());
}

void TstValidate::shortDressingIsReportedAndRepaired()
{
    RandomHullConfig config = test::dressingConfig({});
    QVector<Diagnostic> diagnostics = checkConfig(m_package, QStringLiteral("Test/Dressing"), config);
    QVERIFY(hasCode(diagnostics, diag::DressingLengthMismatch));
    QVERIFY(hasSeverity(diagnostics, Severity::Warning));

    QVERIFY(repairConfig(m_package, QStringLiteral("Test/Dressing"), &config));
    QCOMPARE(config.primary[RandomHullConfig::Bow].dressing.size(), qsizetype(2));
    diagnostics = checkConfig(m_package, QStringLiteral("Test/Dressing"), config);
    QVERIFY(diagnostics.isEmpty());

    // a longer dressing list than the piece has points is trimmed
    config.primary[RandomHullConfig::Bow].dressing = {0, 0, 0, 0};
    QVERIFY(repairConfig(m_package, QStringLiteral("Test/Dressing"), &config));
    QCOMPARE(config.primary[RandomHullConfig::Bow].dressing.size(), qsizetype(2));
}

void TstValidate::dressingOutOfRangeIsReportedAndRepaired()
{
    RandomHullConfig config = test::dressingConfig({0, 7});
    QVector<Diagnostic> diagnostics = checkConfig(m_package, QStringLiteral("Test/Dressing"), config);
    QVERIFY(hasCode(diagnostics, diag::DressingOutOfRange));
    for (const Diagnostic &diagnostic : diagnostics) {
        if (diagnostic.code == QLatin1String(diag::DressingOutOfRange)) {
            QCOMPARE(diagnostic.segment, int(RandomHullConfig::Bow));
            QCOMPARE(diagnostic.dressingIndex, 1);
        }
    }

    QVERIFY(repairConfig(m_package, QStringLiteral("Test/Dressing"), &config));
    QCOMPARE(config.primary[RandomHullConfig::Bow].dressing.at(1), 1); // allowEmpty slot
    QVERIFY(checkConfig(m_package, QStringLiteral("Test/Dressing"), config).isEmpty());

    // negative indices are repaired too
    config.primary[RandomHullConfig::Bow].dressing = {-4, 0};
    QVERIFY(repairConfig(m_package, QStringLiteral("Test/Dressing"), &config));
    QCOMPARE(config.primary[RandomHullConfig::Bow].dressing.at(0), 0);
    QVERIFY(checkConfig(m_package, QStringLiteral("Test/Dressing"), config).isEmpty());
}

void TstValidate::unknownSecondaryIsReportedAndDropped()
{
    RandomHullConfig config = test::bulkConfig();
    SecondaryConfig entry;
    entry.key = QStringLiteral("ffffffff-0000-0000-0000-00000000ffff");
    entry.segment = RandomHullConfig::Core;
    entry.snap = 0;
    config.secondary = {entry};

    QVector<Diagnostic> diagnostics = checkConfig(m_package, QStringLiteral("Stock/Bulk Hauler"), config);
    QVERIFY(hasCode(diagnostics, diag::UnknownSecondary));
    QVERIFY(hasSeverity(diagnostics, Severity::Error));

    QVERIFY(repairConfig(m_package, QStringLiteral("Stock/Bulk Hauler"), &config));
    QVERIFY(config.secondary.isEmpty()); // the game skips it, so the tool drops it too
    QVERIFY(checkConfig(m_package, QStringLiteral("Stock/Bulk Hauler"), config).isEmpty());
}

void TstValidate::secondarySegmentOutOfRangeIsReportedAndRepaired()
{
    RandomHullConfig config = test::bulkConfig();
    SecondaryConfig entry;
    entry.key = test::keySuperstructure();
    entry.segment = 9;
    entry.snap = 0;
    config.secondary = {entry};

    QVector<Diagnostic> diagnostics = checkConfig(m_package, QStringLiteral("Stock/Bulk Hauler"), config);
    QVERIFY(hasCode(diagnostics, diag::SecondarySegmentOutOfRange));

    QVERIFY(repairConfig(m_package, QStringLiteral("Stock/Bulk Hauler"), &config));
    QCOMPARE(int(config.secondary.size()), 1);
    QCOMPARE(config.secondary.first().segment, int(RandomHullConfig::Stern));
    QCOMPARE(config.secondary.first().snap, 0);
    QVERIFY(checkConfig(m_package, QStringLiteral("Stock/Bulk Hauler"), config).isEmpty());
}

void TstValidate::secondarySnapOutOfRangeIsReportedAndRepaired()
{
    RandomHullConfig config = test::bulkConfig();
    SecondaryConfig entry;
    entry.key = test::keySuperstructure();
    entry.segment = RandomHullConfig::Core;
    entry.snap = 9;
    config.secondary = {entry};

    QVector<Diagnostic> diagnostics = checkConfig(m_package, QStringLiteral("Stock/Bulk Hauler"), config);
    QVERIFY(hasCode(diagnostics, diag::SecondarySnapOutOfRange));

    QVERIFY(repairConfig(m_package, QStringLiteral("Stock/Bulk Hauler"), &config));
    QCOMPARE(int(config.secondary.size()), 1);
    QCOMPARE(config.secondary.first().snap, 0);
    QVERIFY(checkConfig(m_package, QStringLiteral("Stock/Bulk Hauler"), config).isEmpty());
}

void TstValidate::largeFleetsKeepEveryRowEditable()
{
    // Fleets in the game's own save folder carry far more than ten ships, so the row
    // position says nothing about whether a ship can be customized.
    const QString path = writeFleet(QStringLiteral("twelve.fleet"), test::defaultFleetXml(12));
    FleetDocument document;
    QVector<Diagnostic> diagnostics;
    QVERIFY(document.load(path, &diagnostics, nullptr));
    QCOMPARE(document.shipCount(), 12);

    document.setPackage(&m_package);
    for (int i = 0; i < document.shipCount(); ++i) {
        QVERIFY2(document.ship(i).editable,
                 qPrintable(QStringLiteral("ship %1 should be editable: %2")
                                .arg(i)
                                .arg(document.ship(i).readOnlyReason)));
        QVERIFY(!document.ship(i).key.isEmpty());
    }

    // nothing is lost when the fleet is written back
    const QString out = m_dir.filePath(QStringLiteral("twelve-out.fleet"));
    QString error;
    QVERIFY2(document.save(out, &error), qPrintable(error));
    FleetDocument reloaded;
    QVERIFY(reloaded.load(out, nullptr, nullptr));
    QCOMPARE(reloaded.shipCount(), 12);
    QCOMPARE(reloaded.ship(11).key, document.ship(11).key);
}

void TstValidate::cleanConfigProducesNoDiagnostics()
{
    QVERIFY(checkConfig(m_package, QStringLiteral("Stock/Bulk Hauler"), test::bulkConfig()).isEmpty());
    QVERIFY(checkConfig(m_package, QStringLiteral("Test/Dressing"), test::dressingConfig({1, 1})).isEmpty());

    const QVector<Diagnostic> unknownHull =
        checkConfig(m_package, QStringLiteral("no/such/hull"), test::bulkConfig());
    QVERIFY(hasCode(unknownHull, diag::UnknownHullType));

    const QVector<Diagnostic> readOnly =
        checkConfig(m_package, QStringLiteral("Test/ReadOnly"), test::bulkConfig());
    QVERIFY(hasCode(readOnly, diag::UnsupportedHullType));
    QVERIFY(hasSeverity(readOnly, Severity::Info));
}

void TstValidate::repairIsIdempotent()
{
    RandomHullConfig config = test::bulkConfig();
    QVERIFY(!repairConfig(m_package, QStringLiteral("Stock/Bulk Hauler"), &config)); // already valid
    QVERIFY(!repairConfig(m_package, QStringLiteral("no/such/hull"), &config));
    QVERIFY(!repairConfig(m_package, QStringLiteral("Stock/Bulk Hauler"), nullptr));
}

void TstValidate::randomizeFollowsGameRules()
{
    QRandomGenerator rng(0x51ED270Bu);
    for (int round = 0; round < 50; ++round) {
        const RandomHullConfig config = randomizeConfig(m_package, QStringLiteral("Stock/Bulk Hauler"), rng);
        for (int segment = 0; segment < RandomHullConfig::SegmentCount; ++segment)
            QVERIFY(!config.primary[size_t(segment)].key.isEmpty());
        QCOMPARE(int(config.secondary.size()), 1); // randomisation always adds one
        QVERIFY(checkConfig(m_package, QStringLiteral("Stock/Bulk Hauler"), config).isEmpty());

        const float tint = config.hullTint.redF();
        QVERIFY2(tint >= 0.35f && tint <= 0.8f, qPrintable(QString::number(tint)));
        QCOMPARE(config.hullTint.greenF(), tint);
        QCOMPARE(config.hullTint.blueF(), tint);
        for (const float component : {config.textureVariation.x(), config.textureVariation.y(),
                                      config.textureVariation.z()}) {
            QVERIFY2(component >= -1000.0f && component <= 1000.0f, qPrintable(QString::number(component)));
        }
    }

    const RandomHullConfig dressing = randomizeConfig(m_package, QStringLiteral("Test/Dressing"), rng);
    QCOMPARE(dressing.primary[RandomHullConfig::Bow].dressing.size(), qsizetype(2));
    QVERIFY(checkConfig(m_package, QStringLiteral("Test/Dressing"), dressing).isEmpty());
}

void TstValidate::describeDiagnosticIsHumanReadable()
{
    Diagnostic diagnostic;
    diagnostic.severity = Severity::Error;
    diagnostic.code = QString::fromLatin1(diag::UnknownSegment);
    diagnostic.detail = QStringLiteral("deadbeef");
    diagnostic.shipIndex = 2;
    diagnostic.segment = RandomHullConfig::Bow;
    const QString text = describeDiagnostic(diagnostic);
    QVERIFY(!text.isEmpty());
    QVERIFY2(text.contains(QStringLiteral("Ship 3")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("bow")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("deadbeef")), qPrintable(text));

    diagnostic.code = QString::fromLatin1(diag::DressingOutOfRange);
    diagnostic.dressingIndex = 4;
    QVERIFY(describeDiagnostic(diagnostic).contains(QStringLiteral("4")));

    diagnostic.code = QStringLiteral("custom.unknown");
    QVERIFY(!describeDiagnostic(diagnostic).isEmpty());

    // every diagnostic code the core emits has a description
    for (const char *code : {diag::UnsupportedHullType, diag::UnknownHullType, diag::MissingHullConfig,
                             diag::UnsupportedHullConfig, diag::PrimaryStructureSize, diag::UnknownSegment,
                             diag::DressingLengthMismatch, diag::DressingOutOfRange, diag::UnknownSecondary,
                             diag::SecondarySegmentOutOfRange, diag::SecondarySnapOutOfRange, diag::PackageMissing,
                             diag::PackageCorrupt, diag::MeshMissing, diag::TextureMissing}) {
        Diagnostic each;
        each.code = QString::fromLatin1(code);
        QVERIFY2(!describeDiagnostic(each).isEmpty(), code);
        QVERIFY2(!describeDiagnostic(each).contains(QString::fromLatin1(code)), code);
    }
}

void TstValidate::cleanupTestCase()
{
    // The scratch dir lives under <build>/test-tmp; removing it here keeps even an
    // interrupted run from leaving a directory behind.
    QDir(m_dir.path()).removeRecursively();
}

QTEST_GUILESS_MAIN(TstValidate)

#include "tst_validate.moc"
