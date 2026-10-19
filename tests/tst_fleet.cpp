#include <QtTest>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "MockAssets.h"
#include "nlhc/fleet/FleetDocument.h"

using namespace nlhc;

namespace {

QString samplePath(const QString &name)
{
    QStringList candidates;
#ifdef NLHC_SOURCE_DIR
    candidates << QDir(QStringLiteral(NLHC_SOURCE_DIR)).filePath(QStringLiteral("samples/") + name);
#endif
    candidates << QDir::current().filePath(QStringLiteral("../samples/") + name);
    candidates << QDir::current().filePath(QStringLiteral("samples/") + name);
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate))
            return QFileInfo(candidate).absoluteFilePath();
    }
    return QString();
}

QString bulkSample()
{
    const QString found = QFINDTESTDATA("../samples/bulk-liner-sample.fleet");
    return found.isEmpty() ? samplePath(QStringLiteral("bulk-liner-sample.fleet")) : found;
}

QString containerSample()
{
    const QString found = QFINDTESTDATA("../samples/container-liner-sample.fleet");
    return found.isEmpty() ? samplePath(QStringLiteral("container-liner-sample.fleet")) : found;
}

bool hasDiagnostic(const QVector<Diagnostic> &diagnostics, const char *code)
{
    for (const Diagnostic &diagnostic : diagnostics) {
        if (diagnostic.code == QLatin1String(code))
            return true;
    }
    return false;
}

QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll());
}

} // namespace

class TstFleet : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void loadsBulkSample();
    void loadsContainerSample();
    void readsHullConfigDetails();
    void roundTripPreservesSemantics();
    void preservesUnknownContent();
    void changesOnlyTheRequestedValue();
    void keepsBackupBeforeOverwrite();
    void savesEvenWhenTheBackupIsBlocked();
    void reportsMissingHullConfig();
    void reportsUnsupportedHullConfig();
    void reportsPrimaryStructureSize();
    void rejectsInvalidFiles();
    void followsAttachedPackage();

private:
    void writeFleet(const QString &path, const QString &xml);

    QTemporaryDir m_dir{nlhc::test::scratchTemplate(QStringLiteral("nlhc-fleet"))};
    QString m_root;
};

void TstFleet::initTestCase()
{
    QVERIFY2(m_dir.isValid(), qPrintable(m_dir.errorString()));
    m_root = m_dir.path();
}

void TstFleet::writeFleet(const QString &path, const QString &xml)
{
    test::writeBytes(path, xml.toUtf8());
}

void TstFleet::loadsBulkSample()
{
    const QString path = bulkSample();
    QVERIFY2(!path.isEmpty(), "bulk sample not found");

    FleetDocument document;
    QVector<Diagnostic> diagnostics;
    QString error;
    QVERIFY2(document.load(path, &diagnostics, &error), qPrintable(error));

    QCOMPARE(document.shipCount(), 10);
    QCOMPARE(document.fleetVersion(), 3);
    QCOMPARE(document.factionKey(), QStringLiteral("Stock/Protectorate"));
    QVERIFY(!document.fleetName().isEmpty());
    QVERIFY(!document.isDirty());
    QVERIFY(!document.filePath().isEmpty());
    QVERIFY2(diagnostics.isEmpty(), qPrintable(QString::fromLatin1("%1 diagnostics").arg(diagnostics.size())));

    for (int i = 0; i < document.shipCount(); ++i) {
        const ShipRecord ship = document.ship(i);
        QCOMPARE(ship.index, i);
        QCOMPARE(ship.hullType, QStringLiteral("Stock/Bulk Hauler"));
        QVERIFY(!ship.name.isEmpty());
        QVERIFY(!ship.key.isEmpty());
        QVERIFY(document.isRandomHullConfig(i));
        QVERIFY(document.hullConfig(i).has_value());
        QVERIFY(!ship.editable); // no package attached yet
        QVERIFY(!ship.readOnlyReason.isEmpty());
    }
}

void TstFleet::loadsContainerSample()
{
    const QString path = containerSample();
    QVERIFY2(!path.isEmpty(), "container sample not found");

    FleetDocument document;
    QString error;
    QVERIFY2(document.load(path, nullptr, &error), qPrintable(error));
    QCOMPARE(document.shipCount(), 10);

    int moorline = 0;
    int refit = 0;
    for (int i = 0; i < document.shipCount(); ++i) {
        const QString hullType = document.ship(i).hullType;
        if (hullType == QStringLiteral("Stock/Container Hauler"))
            ++moorline;
        else if (hullType == QStringLiteral("Stock/Container Hauler Refit"))
            ++refit;
        else
            QFAIL(qPrintable(QStringLiteral("unexpected hull type %1").arg(hullType)));
    }
    QCOMPARE(moorline, 5);
    QCOMPARE(refit, 5);
}

void TstFleet::readsHullConfigDetails()
{
    FleetDocument document;
    QVERIFY(document.load(bulkSample(), nullptr, nullptr));
    QCOMPARE(document.ship(6).name, QStringLiteral("Smug Comma"));

    const std::optional<RandomHullConfig> config = document.hullConfig(6);
    QVERIFY(config.has_value());
    QVERIFY(config->primary[RandomHullConfig::Bow].key.startsWith(QStringLiteral("c534a876")));
    QVERIFY(config->primary[RandomHullConfig::Core].key.startsWith(QStringLiteral("d4c9a66d")));
    QVERIFY(config->primary[RandomHullConfig::Stern].key.startsWith(QStringLiteral("2f2b451c")));
    QCOMPARE(config->primary[RandomHullConfig::Bow].dressing, QVector<int>({0, 2}));
    QCOMPARE(config->primary[RandomHullConfig::Core].dressing, QVector<int>({0, 0, 1, 0, 1, 0}));
    QCOMPARE(config->primary[RandomHullConfig::Stern].dressing, QVector<int>());
    QCOMPARE(int(config->secondary.size()), 1);
    QCOMPARE(config->secondary.first().segment, 1);
    QCOMPARE(config->secondary.first().snap, 0);
    QVERIFY(qAbs(config->hullTint.redF() - 0.370176822f) < 1e-5f);
    QCOMPARE(config->hullTint.alphaF(), 1.0f);
    QVERIFY(qAbs(config->textureVariation.x() - (-966.243042f)) < 1e-3f);
    QVERIFY(qAbs(config->textureVariation.y() - (-525.455444f)) < 1e-3f);
    QVERIFY(qAbs(config->textureVariation.z() - 662.268433f) < 1e-3f);
}

void TstFleet::roundTripPreservesSemantics()
{
    FleetDocument document;
    QVERIFY(document.load(bulkSample(), nullptr, nullptr));

    const QString out = QDir(m_root).filePath(QStringLiteral("roundtrip.fleet"));
    QString error;
    QVERIFY2(document.save(out, &error), qPrintable(error));
    QVERIFY(!document.isDirty());

    const QString written = readText(out);
    QVERIFY2(written.startsWith(QStringLiteral("<?xml")), qPrintable(written.left(120)));
    QVERIFY2(written.contains(QStringLiteral("<Fleet")), qPrintable(written.left(300)));
    QVERIFY2(written.contains(QStringLiteral("<TotalPoints>4150</TotalPoints>")), qPrintable(written.left(300)));

    FleetDocument reloaded;
    QVERIFY2(reloaded.load(out, nullptr, &error), qPrintable(error));
    QCOMPARE(reloaded.shipCount(), document.shipCount());
    QCOMPARE(reloaded.fleetName(), document.fleetName());
    QCOMPARE(reloaded.factionKey(), document.factionKey());
    QCOMPARE(reloaded.fleetVersion(), 3);

    for (int i = 0; i < document.shipCount(); ++i) {
        const ShipRecord before = document.ship(i);
        const ShipRecord after = reloaded.ship(i);
        QCOMPARE(after.key, before.key);
        QCOMPARE(after.name, before.name);
        QCOMPARE(after.hullType, before.hullType);
        QCOMPARE(after.cost, before.cost);
        QCOMPARE(after.number, before.number);
        QCOMPARE(after.symbolOption, before.symbolOption);
        const auto configBefore = document.hullConfig(i);
        const auto configAfter = reloaded.hullConfig(i);
        QVERIFY(configBefore.has_value());
        QVERIFY(configAfter.has_value());
        QVERIFY2(*configBefore == *configAfter, qPrintable(QStringLiteral("ship %1 hull config").arg(i)));
    }
}

void TstFleet::preservesUnknownContent()
{
    QString xml = test::defaultFleetXml(2);
    xml.replace(QStringLiteral("  <Ships>"),
                QStringLiteral("  <CustomTop attr=\"keep\"><Inner lang=\"x\">inner text</Inner>tail</CustomTop>\n"
                               "  <Ships>"));
    xml.replace(QStringLiteral("      <SocketMap />"),
                QStringLiteral("      <SocketMap><HullSocket><Key>abc</Key><ComponentName>X</ComponentName>"
                               "<Extra flag=\"1\" /></HullSocket></SocketMap>"));
    xml.replace(QStringLiteral("    <Ship>"), QStringLiteral("    <Ship CustomAttr=\"42\">"));
    QVERIFY(xml.contains(QStringLiteral("CustomTop")));

    const QString in = QDir(m_root).filePath(QStringLiteral("unknown-in.fleet"));
    const QString out = QDir(m_root).filePath(QStringLiteral("unknown-out.fleet"));
    writeFleet(in, xml);

    FleetDocument document;
    QString error;
    QVERIFY2(document.load(in, nullptr, &error), qPrintable(error));

    RandomHullConfig config = *document.hullConfig(0);
    config.primary[RandomHullConfig::Bow].dressing = {1, 0};
    config.textureVariation = QVector3D(11.0f, 22.0f, 33.0f);
    QVERIFY(document.setHullConfig(0, config));
    QVERIFY2(document.save(out, &error), qPrintable(error));

    const QString saved = readText(out);
    QVERIFY2(saved.contains(QStringLiteral("CustomTop")), qPrintable(saved.left(400)));
    QVERIFY(saved.contains(QStringLiteral("attr=\"keep\"")));
    QVERIFY(saved.contains(QStringLiteral("inner text")));
    QVERIFY(saved.contains(QStringLiteral("tail")));
    QVERIFY(saved.contains(QStringLiteral("CustomAttr=\"42\"")));
    QVERIFY(saved.contains(QStringLiteral("HullSocket")));
    QVERIFY(saved.contains(QStringLiteral("ComponentName")));
    QVERIFY(saved.contains(QStringLiteral("Extra")));
    QVERIFY(saved.contains(QStringLiteral("flag=\"1\"")));
    QVERIFY(saved.contains(QStringLiteral("xsi:type=\"RandomHullConfiguration\"")));
    QVERIFY(saved.contains(QStringLiteral("<WeaponGroups")));

    FleetDocument reloaded;
    QVERIFY2(reloaded.load(out, nullptr, &error), qPrintable(error));
    QCOMPARE(reloaded.shipCount(), 2);
    const auto reloadedConfig = reloaded.hullConfig(0);
    QVERIFY(reloadedConfig.has_value());
    QVERIFY2(*reloadedConfig == config, "edited hull config did not survive the round trip");
}

void TstFleet::changesOnlyTheRequestedValue()
{
    FleetDocument document;
    QVERIFY(document.load(bulkSample(), nullptr, nullptr));

    QVector<RandomHullConfig> before;
    for (int i = 0; i < document.shipCount(); ++i)
        before.append(*document.hullConfig(i));

    RandomHullConfig changed = before.at(0);
    changed.hullTint = QColor::fromRgbF(0.9f, 0.25f, 0.1f, 1.0f);
    QVERIFY(document.setHullConfig(0, changed));
    QVERIFY(document.isDirty());
    QVERIFY(!document.setHullConfig(0, changed)); // identical config: no change
    QVERIFY(document.isDirty());

    const QString out = QDir(m_root).filePath(QStringLiteral("single-change.fleet"));
    QString error;
    QVERIFY2(document.save(out, &error), qPrintable(error));
    QVERIFY(!document.isDirty());

    const QString saved = readText(out);
    const QString tintText = QString::number(double(changed.hullTint.redF()), 'g', 9);
    QVERIFY2(saved.contains(QStringLiteral("<r>%1</r>").arg(tintText)), qPrintable(saved.mid(0, 600)));
    // untouched values keep their original text
    QVERIFY(saved.contains(QStringLiteral("-844.536743")));
    QVERIFY(saved.contains(QStringLiteral("683.5737")));
    QVERIFY(saved.contains(QStringLiteral("0.370176822")));

    FleetDocument reloaded;
    QVERIFY2(reloaded.load(out, nullptr, &error), qPrintable(error));
    const auto reloadedConfig = reloaded.hullConfig(0);
    QVERIFY(reloadedConfig.has_value());
    QVERIFY2(*reloadedConfig == changed, "first ship tint mismatch");
    for (int i = 1; i < reloaded.shipCount(); ++i) {
        const auto config = reloaded.hullConfig(i);
        QVERIFY(config.has_value());
        QVERIFY2(*config == before.at(i), qPrintable(QStringLiteral("ship %1 changed").arg(i)));
    }
}

void TstFleet::keepsBackupBeforeOverwrite()
{
    FleetDocument document;
    QVERIFY(document.load(bulkSample(), nullptr, nullptr));

    const QString out = QDir(m_root).filePath(QStringLiteral("backup.fleet"));
    QString error;
    QVERIFY2(document.save(out, &error), qPrintable(error));
    const QByteArray firstContent = [&out] {
        QFile file(out);
        file.open(QIODevice::ReadOnly);
        return file.readAll();
    }();
    QVERIFY(!QFileInfo::exists(out + QStringLiteral(".bak"))); // nothing existed before the first save

    RandomHullConfig config = *document.hullConfig(1);
    config.hullTint = QColor::fromRgbF(0.4f, 0.4f, 0.4f, 1.0f);
    QVERIFY(document.setHullConfig(1, config));
    QVERIFY2(document.save(out, &error), qPrintable(error));

    const QString backup = out + QStringLiteral(".bak");
    QVERIFY(QFileInfo::exists(backup));
    QFile file(backup);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), firstContent);
}

void TstFleet::savesEvenWhenTheBackupIsBlocked()
{
    FleetDocument document;
    QVERIFY(document.load(bulkSample(), nullptr, nullptr));

    const QString out = QDir(m_root).filePath(QStringLiteral("blocked-backup.fleet"));
    QString error;
    QString notice;
    QVERIFY2(document.save(out, &error, &notice), qPrintable(error));
    QVERIFY(notice.isEmpty());

    // Nothing can be written to that path, so the backup is skipped instead of
    // costing the user the save.
    const QString backup = out + QStringLiteral(".bak");
    QVERIFY(QDir(m_root).mkdir(QStringLiteral("blocked-backup.fleet.bak")));

    RandomHullConfig config = *document.hullConfig(1);
    config.hullTint = QColor::fromRgbF(0.2f, 0.5f, 0.3f, 1.0f);
    QVERIFY(document.setHullConfig(1, config));
    QVERIFY2(document.save(out, &error, &notice), qPrintable(error));
    QVERIFY(!notice.isEmpty());
    QVERIFY(QFileInfo(backup).isDir());

    FleetDocument reloaded;
    QVERIFY2(reloaded.load(out, nullptr, &error), qPrintable(error));
    const auto reloadedConfig = reloaded.hullConfig(1);
    QVERIFY(reloadedConfig.has_value());
    QVERIFY(*reloadedConfig == config);
}

void TstFleet::reportsMissingHullConfig()
{
    const QString path = QDir(m_root).filePath(QStringLiteral("no-config.fleet"));
    writeFleet(path, test::fleetXml({test::shipXml(0, QStringLiteral("Stock/Bulk Hauler"), QString())}));

    FleetDocument document;
    QVector<Diagnostic> diagnostics;
    QString error;
    QVERIFY2(document.load(path, &diagnostics, &error), qPrintable(error));
    QCOMPARE(document.shipCount(), 1);
    QVERIFY(!document.isRandomHullConfig(0));
    QVERIFY(!document.hullConfig(0).has_value());
    QVERIFY(hasDiagnostic(diagnostics, diag::MissingHullConfig));
    QCOMPARE(diagnostics.first().shipIndex, 0);
    QCOMPARE(diagnostics.first().severity, Severity::Error);

    RandomHullConfig config;
    QVERIFY(!document.setHullConfig(0, config)); // cannot write into a missing config
    QVERIFY(!document.isDirty());
}

void TstFleet::reportsUnsupportedHullConfig()
{
    QString body = test::defaultHullConfigBody();
    body.replace(QStringLiteral("xsi:type=\"RandomHullConfiguration\""),
                 QStringLiteral("xsi:type=\"StaticHullConfiguration\""));
    const QString path = QDir(m_root).filePath(QStringLiteral("other-config.fleet"));
    writeFleet(path, test::fleetXml({test::shipXml(0, QStringLiteral("Stock/Bulk Hauler"), body)}));

    FleetDocument document;
    QVector<Diagnostic> diagnostics;
    QVERIFY(document.load(path, &diagnostics, nullptr));
    QVERIFY(!document.isRandomHullConfig(0));
    QVERIFY(!document.hullConfig(0).has_value());
    QVERIFY(hasDiagnostic(diagnostics, diag::UnsupportedHullConfig));
    QVERIFY2(diagnostics.first().detail.contains(QStringLiteral("StaticHullConfiguration")),
             qPrintable(diagnostics.first().detail));
    QVERIFY(!document.setHullConfig(0, RandomHullConfig()));
}

void TstFleet::reportsPrimaryStructureSize()
{
    const QString body = QStringLiteral("        <HullConfig xsi:type=\"RandomHullConfiguration\">\n"
                                        "          <PrimaryStructure>\n"
                                        "            <SegmentConfiguration><Key>%1</Key><Dressing /></SegmentConfiguration>\n"
                                        "            <SegmentConfiguration><Key>%2</Key><Dressing /></SegmentConfiguration>\n"
                                        "          </PrimaryStructure>\n"
                                        "          <HullTint><r>0.5</r><g>0.5</g><b>0.5</b><a>1</a></HullTint>\n"
                                        "        </HullConfig>\n")
                               .arg(test::keyBow(), test::keyCore());
    const QString path = QDir(m_root).filePath(QStringLiteral("short-primary.fleet"));
    writeFleet(path, test::fleetXml({test::shipXml(0, QStringLiteral("Stock/Bulk Hauler"), body)}));

    FleetDocument document;
    QVector<Diagnostic> diagnostics;
    QVERIFY(document.load(path, &diagnostics, nullptr));
    QVERIFY(hasDiagnostic(diagnostics, diag::PrimaryStructureSize));

    const auto config = document.hullConfig(0);
    QVERIFY(config.has_value());
    QCOMPARE(config->primary[RandomHullConfig::Bow].key, test::keyBow());
    QCOMPARE(config->primary[RandomHullConfig::Core].key, test::keyCore());
    QVERIFY(config->primary[RandomHullConfig::Stern].key.isEmpty());
    // the missing texture variation falls back to the origin instead of failing
    QCOMPARE(config->textureVariation, QVector3D(0.0f, 0.0f, 0.0f));
}

void TstFleet::rejectsInvalidFiles()
{
    FleetDocument document;
    QString error;

    QVERIFY(!document.load(QDir(m_root).filePath(QStringLiteral("does-not-exist.fleet")), nullptr, &error));
    QVERIFY(!error.isEmpty());

    const QString garbage = QDir(m_root).filePath(QStringLiteral("garbage.fleet"));
    writeFleet(garbage, QStringLiteral("<Fleet><Ships>"));
    error.clear();
    QVERIFY(!document.load(garbage, nullptr, &error));
    QVERIFY2(error.contains(QStringLiteral("XML")), qPrintable(error));

    const QString wrongRoot = QDir(m_root).filePath(QStringLiteral("wrong-root.fleet"));
    writeFleet(wrongRoot, QStringLiteral("<?xml version=\"1.0\"?><NotAFleet />"));
    error.clear();
    QVERIFY(!document.load(wrongRoot, nullptr, &error));
    QVERIFY2(error.contains(QStringLiteral("Fleet")), qPrintable(error));
    QVERIFY(!document.save(QDir(m_root).filePath(QStringLiteral("never.fleet")), &error));
}

void TstFleet::followsAttachedPackage()
{
    const QString path = QDir(m_root).filePath(QStringLiteral("package.fleet"));
    QVector<QString> ships{test::shipXml(0, QStringLiteral("Stock/Bulk Hauler"), test::defaultHullConfigBody()),
                           test::shipXml(1, QStringLiteral("Some/Other Hull"), test::defaultHullConfigBody())};
    writeFleet(path, test::fleetXml(ships));

    HullPackage package;
    QVERIFY(package.load(test::createMockPackage(QDir(m_root).filePath(QStringLiteral("package"))), nullptr));

    FleetDocument document;
    QVERIFY(document.load(path, nullptr, nullptr));
    QVERIFY(!document.ship(0).editable);
    QVERIFY(document.package() == nullptr);

    document.setPackage(&package);
    QVERIFY(document.package() == &package);
    QVERIFY(document.ship(0).editable);
    QVERIFY(document.ship(0).readOnlyReason.isEmpty());
    QVERIFY(!document.ship(1).editable);
    QVERIFY(document.ship(1).readOnlyReason.contains(QStringLiteral("Some/Other Hull")));
}

void TstFleet::cleanupTestCase()
{
    // The scratch dir lives under <build>/test-tmp; removing it here keeps even an
    // interrupted run from leaving a directory behind.
    QDir(m_dir.path()).removeRecursively();
}

QTEST_GUILESS_MAIN(TstFleet)

#include "tst_fleet.moc"
