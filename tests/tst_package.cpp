#include <QtTest>

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "MockAssets.h"
#include "nlhc/asset/HullPackage.h"

using namespace nlhc;

class TstPackage : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void loadsMockCatalog();
    void resolvesLookups();
    void acceptsEmptyCatalog();
    void parsesMeshBinary();
    void cachesMeshLookups();
    void decodesTexture();
    void reportsMissingCatalog();
    void rejectsInvalidJson();
    void rejectsUnsupportedFormatVersion();
    void rejectsMissingRequiredField();
    void reportsMissingMeshFile();
    void reportsCorruptMesh();
    void reportsTruncatedMesh();
    void reportsUnsupportedMeshVersion();
    void reportsMissingTexture();
    void collectsWarnings();
    void appliesNodeMaterialOverrides();

private:
    QString mockRoot() const { return m_dir.filePath(QStringLiteral("package")); }
    QString scratchDir(const QString &name);

    QTemporaryDir m_dir{nlhc::test::scratchTemplate(QStringLiteral("nlhc-package"))};
};

QString TstPackage::scratchDir(const QString &name)
{
    const QString path = m_dir.filePath(name);
    QDir().mkpath(path);
    return path;
}

void TstPackage::initTestCase()
{
    QVERIFY2(m_dir.isValid(), qPrintable(m_dir.errorString()));
    test::createMockPackage(mockRoot());
}

void TstPackage::loadsMockCatalog()
{
    HullPackage package;
    QString error;
    QVERIFY2(package.load(mockRoot(), &error), qPrintable(error));
    QVERIFY(package.isValid());
    QCOMPARE(package.hullTypes().size(), qsizetype(4));
    QCOMPARE(package.pieces().size(), qsizetype(6));
    QCOMPARE(package.materials().size(), qsizetype(2));
    QCOMPARE(QDir(package.rootDir()), QDir(mockRoot()));
}

void TstPackage::resolvesLookups()
{
    HullPackage package;
    QVERIFY(package.load(mockRoot(), nullptr));

    const HullTypeDef *bulk = package.hullType(QStringLiteral("Stock/Bulk Hauler"));
    QVERIFY(bulk);
    QCOMPARE(bulk->displayName, QStringLiteral("Bulk Hauler"));
    QCOMPARE(bulk->role, QStringLiteral("marauder"));
    QCOMPARE(bulk->bows.size(), qsizetype(1));
    QVERIFY(bulk->editable);

    const HullTypeDef *readOnly = package.hullType(QStringLiteral("Test/ReadOnly"));
    QVERIFY(readOnly);
    QVERIFY(!readOnly->editable);
    QVERIFY(!package.isEditableHullType(QStringLiteral("Test/ReadOnly")));
    QVERIFY(package.isEditableHullType(QStringLiteral("Stock/Bulk Hauler")));
    QVERIFY(!package.isEditableHullType(QStringLiteral("no/such/hull")));
    QVERIFY(package.hullType(QStringLiteral("no/such/hull")) == nullptr);

    const Piece *core = package.piece(QStringLiteral("p:core"));
    QVERIFY(core);
    QCOMPARE(core->key, test::keyCore());
    QCOMPARE(core->nodes.size(), qsizetype(1));
    QCOMPARE(core->nodes.first().meshId, QStringLiteral("m:core"));
    QCOMPARE(core->superstructureSnaps.size(), qsizetype(1));
    QCOMPARE(core->superstructureSnaps.first().translation, QVector3D(0.0f, 5.0f, 0.0f));
    QCOMPARE(core->foreSnap.translation, QVector3D(0.0f, 0.0f, 0.0f));
    QCOMPARE(core->aftSnap.translation, QVector3D(-20.0f, 0.0f, 0.0f));
    QVERIFY(package.piece(QStringLiteral("p:nope")) == nullptr);

    const Piece *dressingBow = package.piece(QStringLiteral("pd:bow"));
    QVERIFY(dressingBow);
    QCOMPARE(dressingBow->dressingPoints.size(), qsizetype(2));
    QCOMPARE(dressingBow->dressingPoints.at(0).variants, QVector<int>({1, 2}));
    QVERIFY(!dressingBow->dressingPoints.at(0).allowEmpty);
    QVERIFY(dressingBow->dressingPoints.at(1).allowEmpty);
    QCOMPARE(dressingBow->nodes.size(), qsizetype(5));

    const MaterialDef *material = package.material(QStringLiteral("t:paint"));
    QVERIFY(material);
    QCOMPARE(material->albedoPath, QStringLiteral("albedo/paint.png"));
    QCOMPARE(material->maskChannel, QStringLiteral("r"));
    QVERIFY(material->paintable);
    QVERIFY(package.material(QStringLiteral("t:nope")) == nullptr);
}

void TstPackage::acceptsEmptyCatalog()
{
    const QString root = scratchDir(QStringLiteral("empty"));
    test::writeCatalog(root, QJsonArray(), QJsonArray(), QJsonArray());
    HullPackage package;
    QString error;
    QVERIFY2(package.load(root, &error), qPrintable(error));
    QVERIFY(package.isValid());
    QVERIFY(package.hullTypes().isEmpty());
}

void TstPackage::parsesMeshBinary()
{
    HullPackage package;
    QVERIFY(package.load(mockRoot(), nullptr));

    QString error;
    const MeshData *mesh = package.mesh(QStringLiteral("m:bow"), &error);
    QVERIFY2(mesh, qPrintable(error));
    QVERIFY(error.isEmpty());
    QCOMPARE(mesh->positions.size(), qsizetype(8));
    QCOMPARE(mesh->normals.size(), qsizetype(8));
    QCOMPARE(mesh->uvs.size(), qsizetype(8));
    QCOMPARE(mesh->indices.size(), qsizetype(36));
    QVERIFY(mesh->trim.isEmpty());
    QVERIFY(mesh->bounds.valid);
    QCOMPARE(mesh->bounds.minimum, QVector3D(-1.0f, -1.0f, -1.0f));
    QCOMPARE(mesh->bounds.maximum, QVector3D(1.0f, 1.0f, 1.0f));
    QCOMPARE(mesh->bounds.size(), QVector3D(2.0f, 2.0f, 2.0f));
    QVERIFY(qAbs(mesh->bounds.radius() - float(QVector3D(1, 1, 1).length())) < 1e-5f);
}

void TstPackage::cachesMeshLookups()
{
    HullPackage package;
    QVERIFY(package.load(mockRoot(), nullptr));
    const MeshData *first = package.mesh(QStringLiteral("m:core"), nullptr);
    const MeshData *second = package.mesh(QStringLiteral("m:core"), nullptr);
    QVERIFY(first);
    QCOMPARE(first, second); // lazy cache: same object

    QString error;
    QVERIFY(package.mesh(QString(), &error) == nullptr);
    QVERIFY(!error.isEmpty());
}

void TstPackage::decodesTexture()
{
    HullPackage package;
    QVERIFY(package.load(mockRoot(), nullptr));

    QString error;
    const QImage albedo = package.texture(QStringLiteral("albedo/paint.png"), &error);
    QVERIFY2(!albedo.isNull(), qPrintable(error));
    QCOMPARE(albedo.size(), QSize(1, 1));

    const QImage mask = package.texture(QStringLiteral("mask/paint.png"), nullptr);
    QVERIFY(!mask.isNull());
}

void TstPackage::reportsMissingCatalog()
{
    HullPackage package;
    QString error;
    QVERIFY(!package.load(scratchDir(QStringLiteral("no-catalog")), &error));
    QVERIFY2(error.contains(QStringLiteral("catalog.json")), qPrintable(error));
    QVERIFY(!package.isValid());
}

void TstPackage::rejectsInvalidJson()
{
    const QString root = scratchDir(QStringLiteral("bad-json"));
    test::writeBytes(QDir(root).filePath(QStringLiteral("catalog.json")), QByteArray("{ not json"));
    HullPackage package;
    QString error;
    QVERIFY(!package.load(root, &error));
    QVERIFY2(error.contains(QStringLiteral("not valid JSON")), qPrintable(error));
}

void TstPackage::rejectsUnsupportedFormatVersion()
{
    const QString root = scratchDir(QStringLiteral("bad-version"));
    test::writeCatalog(root, QJsonArray(), QJsonArray(), QJsonArray(), 7);
    HullPackage package;
    QString error;
    QVERIFY(!package.load(root, &error));
    QVERIFY2(error.contains(QStringLiteral("formatVersion")), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("7")), qPrintable(error));
}

void TstPackage::rejectsMissingRequiredField()
{
    const QString root = scratchDir(QStringLiteral("missing-field"));
    QJsonObject piece;
    piece[QStringLiteral("key")] = QStringLiteral("no-id-here");
    test::writeCatalog(root, QJsonArray(), QJsonArray{piece}, QJsonArray());
    HullPackage package;
    QString error;
    QVERIFY(!package.load(root, &error));
    QVERIFY2(error.contains(QStringLiteral("pieces[0]")), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("id")), qPrintable(error));
}

void TstPackage::reportsMissingMeshFile()
{
    HullPackage package;
    QVERIFY(package.load(mockRoot(), nullptr));
    QString error;
    QVERIFY(package.mesh(QStringLiteral("m:absent"), &error) == nullptr);
    QVERIFY2(error.contains(QStringLiteral("not found")), qPrintable(error));
}

void TstPackage::reportsCorruptMesh()
{
    const QString root = scratchDir(QStringLiteral("bad-mesh"));
    test::createMockPackage(root);
    test::writeBytes(QDir(root).filePath(QStringLiteral("mesh/bow.nlmesh")),
                     QByteArray(40, 'x')); // long enough to reach the magic check
    HullPackage package;
    QVERIFY(package.load(root, nullptr));
    QString error;
    QVERIFY(package.mesh(QStringLiteral("m:bow"), &error) == nullptr);
    QVERIFY2(error.contains(QStringLiteral("magic")), qPrintable(error));
}

void TstPackage::reportsTruncatedMesh()
{
    const QString root = scratchDir(QStringLiteral("truncated-mesh"));
    test::createMockPackage(root);
    QByteArray data = test::cubeMesh();
    data.chop(16);
    test::writeBytes(QDir(root).filePath(QStringLiteral("mesh/core.nlmesh")), data);
    HullPackage package;
    QVERIFY(package.load(root, nullptr));
    QString error;
    QVERIFY(package.mesh(QStringLiteral("m:core"), &error) == nullptr);
    QVERIFY2(error.contains(QStringLiteral("truncated")), qPrintable(error));
}

void TstPackage::reportsUnsupportedMeshVersion()
{
    const QString root = scratchDir(QStringLiteral("bad-mesh-version"));
    test::createMockPackage(root);
    QByteArray data = test::cubeMesh();
    data[4] = char(2);
    test::writeBytes(QDir(root).filePath(QStringLiteral("mesh/stern.nlmesh")), data);
    HullPackage package;
    QVERIFY(package.load(root, nullptr));
    QString error;
    QVERIFY(package.mesh(QStringLiteral("m:stern"), &error) == nullptr);
    QVERIFY2(error.contains(QStringLiteral("version")), qPrintable(error));
}

void TstPackage::reportsMissingTexture()
{
    HullPackage package;
    QVERIFY(package.load(mockRoot(), nullptr));
    QString error;
    QVERIFY(package.texture(QStringLiteral("albedo/absent.png"), &error).isNull());
    QVERIFY2(error.contains(QStringLiteral("absent.png")), qPrintable(error));

    error.clear();
    QVERIFY(package.texture(QString(), &error).isNull());
    QVERIFY2(error.contains(QStringLiteral("empty")), qPrintable(error));
}

void TstPackage::collectsWarnings()
{
    HullPackage package;
    QVERIFY(package.load(mockRoot(), nullptr));
    const QStringList warnings = package.warnings();
    QVERIFY(!warnings.isEmpty());
    bool found = false;
    for (const QString &warning : warnings) {
        if (warning.contains(QStringLiteral("p:missing")))
            found = true;
    }
    QVERIFY2(found, qPrintable(warnings.join(QStringLiteral("; "))));
}

void TstPackage::appliesNodeMaterialOverrides()
{
    HullPackage package;
    QVERIFY(package.load(mockRoot(), nullptr));
    const HullTypeDef *dressing = package.hullType(QStringLiteral("Test/Dressing"));
    const HullTypeDef *bulk = package.hullType(QStringLiteral("Stock/Bulk Hauler"));
    QVERIFY(dressing);
    QVERIFY(bulk);

    const Piece *shared = package.pieceFor(*dressing, QStringLiteral("pd:bow"));
    QVERIFY(shared);
    QCOMPARE(shared->nodes.at(1).materialId, QStringLiteral("t:fixed"));
    // The remaining nodes keep the catalog values.
    QCOMPARE(shared->nodes.at(0).materialId, QStringLiteral("t:paint"));

    // Another hull type resolves the same piece without the override, and the canonical entry
    // in the package is never mutated.
    QCOMPARE(package.pieceFor(*bulk, QStringLiteral("pd:bow")), package.piece(QStringLiteral("pd:bow")));
    QCOMPARE(package.piece(QStringLiteral("pd:bow"))->nodes.at(1).materialId, QStringLiteral("t:paint"));
}

void TstPackage::cleanupTestCase()
{
    // The scratch dir lives under <build>/test-tmp; removing it here keeps even an
    // interrupted run from leaving a directory behind.
    QDir(m_dir.path()).removeRecursively();
}

QTEST_GUILESS_MAIN(TstPackage)

#include "tst_package.moc"
