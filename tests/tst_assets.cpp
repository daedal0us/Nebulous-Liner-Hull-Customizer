#include <QtTest>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "nlhc/asset/HullPackage.h"
#include "nlhc/fleet/FleetDocument.h"
#include "nlhc/hull/HullAssembler.h"
#include "nlhc/hull/Validate.h"

namespace {

QString assetRoot()
{
    return QStringLiteral(NLHC_SOURCE_DIR "/assets/hulls");
}

QString samplePath(const QString &name)
{
    return QStringLiteral(NLHC_SOURCE_DIR "/samples/") + name;
}

} // namespace

// Guards the committed asset package: it must cover every segment key and every
// dressing index that appears in the sample fleets.
class TstAssets : public QObject
{
    Q_OBJECT

private slots:
    void packageLoads()
    {
        nlhc::HullPackage package;
        QString error;
        QVERIFY2(package.load(assetRoot(), &error), qPrintable(error));
        if (package.hullTypes().isEmpty())
            QSKIP("asset package not generated yet");
    }

    void expectedHullTypesExist()
    {
        nlhc::HullPackage package;
        QString error;
        if (!package.load(assetRoot(), &error) || package.hullTypes().isEmpty())
            QSKIP("asset package not generated yet");

        const QStringList expected = {
            QStringLiteral("Stock/Bulk Hauler"),
            QStringLiteral("Stock/Container Hauler"),
            QStringLiteral("Stock/Container Hauler Refit"),
        };
        for (const QString &id : expected) {
            const nlhc::HullTypeDef *type = package.hullType(id);
            QVERIFY2(type, qPrintable(QStringLiteral("missing hull type %1").arg(id)));
            QVERIFY2(type->editable, qPrintable(QStringLiteral("%1 is not editable").arg(id)));
            QVERIFY(!type->bows.isEmpty());
            QVERIFY(!type->cores.isEmpty());
            QVERIFY(!type->sterns.isEmpty());
            QVERIFY(!type->superstructures.isEmpty());
        }
    }

    void sampleFleetsResolve_data()
    {
        QTest::addColumn<QString>("file");
        QTest::newRow("bulk") << QStringLiteral("bulk-liner-sample.fleet");
        QTest::newRow("container") << QStringLiteral("container-liner-sample.fleet");
    }

    void sampleFleetsResolve()
    {
        QFETCH(QString, file);

        nlhc::HullPackage package;
        QString error;
        if (!package.load(assetRoot(), &error) || package.hullTypes().isEmpty())
            QSKIP("asset package not generated yet");

        nlhc::FleetDocument document;
        QVector<nlhc::Diagnostic> diagnostics;
        QVERIFY2(document.load(samplePath(file), &diagnostics, &error), qPrintable(error));
        document.setPackage(&package);
        QCOMPARE(document.shipCount(), 10);

        for (int row = 0; row < document.shipCount(); ++row) {
            const nlhc::ShipRecord ship = document.ship(row);
            QVERIFY2(ship.editable, qPrintable(QStringLiteral("%1: %2").arg(ship.name, ship.readOnlyReason)));
            const auto config = document.hullConfig(row);
            QVERIFY(config.has_value());

            const QVector<nlhc::Diagnostic> problems =
                nlhc::checkConfig(package, ship.hullType, *config);
            for (const nlhc::Diagnostic &problem : problems) {
                QVERIFY2(problem.severity != nlhc::Severity::Error,
                         qPrintable(QStringLiteral("%1: %2 (%3)")
                                        .arg(ship.name, problem.code, problem.detail)));
            }
        }
    }

    void assemblyHasGeometry_data()
    {
        QTest::addColumn<QString>("file");
        QTest::newRow("bulk") << QStringLiteral("bulk-liner-sample.fleet");
        QTest::newRow("container") << QStringLiteral("container-liner-sample.fleet");
    }

    void assemblyHasGeometry()
    {
        QFETCH(QString, file);

        nlhc::HullPackage package;
        QString error;
        if (!package.load(assetRoot(), &error) || package.hullTypes().isEmpty())
            QSKIP("asset package not generated yet");

        nlhc::FleetDocument document;
        QVector<nlhc::Diagnostic> diagnostics;
        QVERIFY(document.load(samplePath(file), &diagnostics, &error));
        document.setPackage(&package);

        nlhc::HullAssembler assembler;
        for (int row = 0; row < document.shipCount(); ++row) {
            const nlhc::ShipRecord ship = document.ship(row);
            const auto config = document.hullConfig(row);
            QVERIFY(config.has_value());
            const nlhc::AssemblyResult result = assembler.assemble(package, ship.hullType, *config);
            QVERIFY2(result.renderable, qPrintable(ship.name));
            QVERIFY2(!result.items.isEmpty(), qPrintable(QStringLiteral("%1 has no draw items").arg(ship.name)));
            QVERIFY2(result.bounds.valid, qPrintable(QStringLiteral("%1 has an invalid bounds").arg(ship.name)));
            const QVector3D size = result.bounds.size();
            QVERIFY2(size.z() > 10.0f && size.z() < 2000.0f,
                     qPrintable(QStringLiteral("%1 length %2").arg(ship.name).arg(size.z())));
            for (const nlhc::DrawItem &item : result.items)
                QVERIFY2(item.mesh && !item.mesh->indices.isEmpty(), qPrintable(ship.name));
        }
    }

    void packageMatchesCatalogShape()
    {
        QFile catalog(assetRoot() + QStringLiteral("/catalog.json"));
        if (!catalog.open(QIODevice::ReadOnly))
            QSKIP("asset package not generated yet");

        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(catalog.readAll(), &parseError);
        QCOMPARE(parseError.error, QJsonParseError::NoError);
        const QJsonObject root = document.object();
        QCOMPARE(root.value(QStringLiteral("formatVersion")).toInt(), 1);
        QVERIFY(root.value(QStringLiteral("source")).toObject().contains(QStringLiteral("sha256")));

        const QJsonArray pieces = root.value(QStringLiteral("pieces")).toArray();
        if (pieces.isEmpty())
            QSKIP("asset package not generated yet");

        for (const QJsonValue &value : pieces) {
            const QJsonObject piece = value.toObject();
            QVERIFY(piece.contains(QStringLiteral("key")));
            QVERIFY(piece.contains(QStringLiteral("nodes")));
            const QJsonArray dressing = piece.value(QStringLiteral("dressingPoints")).toArray();
            QJsonArray nodes = piece.value(QStringLiteral("nodes")).toArray();
            QVERIFY(!nodes.isEmpty());
        }
    }
};

QTEST_APPLESS_MAIN(TstAssets)

#include "tst_assets.moc"
