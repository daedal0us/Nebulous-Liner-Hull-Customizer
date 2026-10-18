#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "nlhc/asset/HullPackage.h"
#include "nlhc/fleet/FleetDocument.h"
#include "nlhc/hull/DressingLayout.h"
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

    void dressingEditsSurviveRoundTrip()
    {
        nlhc::HullPackage package;
        QString error;
        if (!package.load(assetRoot(), &error) || package.hullTypes().isEmpty())
            QSKIP("asset package not generated yet");

        nlhc::FleetDocument document;
        QVector<nlhc::Diagnostic> diagnostics;
        QVERIFY(document.load(samplePath(QStringLiteral("bulk-liner-sample.fleet")), &diagnostics, &error));
        document.setPackage(&package);
        QVERIFY(document.shipCount() > 0);

        nlhc::ShipRecord ship = document.ship(0);
        auto config = document.hullConfig(0);
        QVERIFY(config.has_value());

        const nlhc::HullTypeDef *type = package.hullType(ship.hullType);
        QVERIFY(type);
        const auto findPiece = [&package](const QStringList &ids, const QString &key) -> const nlhc::Piece * {
            for (const QString &id : ids) {
                const nlhc::Piece *piece = package.piece(id);
                if (piece && piece->key == key)
                    return piece;
            }
            return nullptr;
        };
        const nlhc::Piece *bow = findPiece(type->bows, config->primary[0].key);
        const nlhc::Piece *core = findPiece(type->cores, config->primary[1].key);
        const nlhc::Piece *stern = findPiece(type->sterns, config->primary[2].key);
        QVERIFY(bow && core && stern);

        nlhc::normalizeDressing(bow, core, stern, &*config); // sample files may already be consistent
        const QVector<nlhc::DressingBinding> bindings = nlhc::bindDressing(bow, core, stern, *config);
        QVERIFY(!bindings.isEmpty());

        nlhc::DressingBinding target;
        bool found = false;
        for (const nlhc::DressingBinding &binding : bindings) {
            if (binding.point && binding.point->variants.size() > 1) {
                target = binding;
                found = true;
                break;
            }
        }
        QVERIFY2(found, "no dressing point with more than one variant");

        const int newValue = (target.value + 1) % qMax(1, target.point->variants.size());
        nlhc::applyDressingValue(target, newValue, &*config);
        document.setHullConfig(0, *config);

        // %TEMP% is not usable in this environment, so keep the scratch file next to the binary.
        const QString path = QDir(QCoreApplication::applicationDirPath())
                                 .filePath(QStringLiteral("nlhc-roundtrip-test.fleet"));
        QFile::remove(path);
        QVERIFY2(document.save(path, &error), qPrintable(error));

        nlhc::FleetDocument reloaded;
        QVERIFY(reloaded.load(path, &diagnostics, &error));
        reloaded.setPackage(&package);
        const auto reloadedConfig = reloaded.hullConfig(0);
        QVERIFY(reloadedConfig.has_value());
        for (const nlhc::DressingBinding &binding : bindings) {
            if (binding.ownerSegment != target.ownerSegment || binding.pointIndex != target.pointIndex)
                continue;
            for (const auto &location : binding.storage) {
                const QVector<int> &array = reloadedConfig->primary[size_t(location.first)].dressing;
                QVERIFY(location.second < array.size());
                QCOMPARE(array.at(location.second), newValue);
            }
        }
        QFile::remove(path);
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
