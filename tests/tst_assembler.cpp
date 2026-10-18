#include <QtTest>

#include <QMatrix4x4>
#include <QTemporaryDir>

#include "MockAssets.h"
#include "nlhc/hull/HullAssembler.h"

using namespace nlhc;

namespace {

void verifyVector(const QVector3D &actual, const QVector3D &expected, float tolerance, const char *what)
{
    const bool ok = qAbs(actual.x() - expected.x()) <= tolerance && qAbs(actual.y() - expected.y()) <= tolerance
                    && qAbs(actual.z() - expected.z()) <= tolerance;
    QVERIFY2(ok, qPrintable(QStringLiteral("%1: expected (%2, %3, %4), got (%5, %6, %7)")
                                .arg(QLatin1String(what))
                                .arg(expected.x())
                                .arg(expected.y())
                                .arg(expected.z())
                                .arg(actual.x())
                                .arg(actual.y())
                                .arg(actual.z())));
}

int countByMesh(const AssemblyResult &result, const MeshData *mesh)
{
    int count = 0;
    for (const DrawItem &item : result.items) {
        if (item.mesh == mesh)
            ++count;
    }
    return count;
}

int countCode(const QVector<Diagnostic> &diagnostics, const char *code)
{
    int count = 0;
    for (const Diagnostic &diagnostic : diagnostics) {
        if (diagnostic.code == QLatin1String(code))
            ++count;
    }
    return count;
}

QVector3D worldOrigin(const DrawItem &item)
{
    return item.world.map(QVector3D(0.0f, 0.0f, 0.0f));
}

} // namespace

class TstAssembler : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void assemblesThreeSegmentsByTranslation();
    void centresAssemblyOnZ();
    void reportsUnknownHullType();
    void reportsUnknownSegment();
    void selectsDressingVariant();
    void keepsAllVariantsWhenDressingIsShort();
    void emptiesDressingPointOutOfRange();
    void honoursAllowEmptyDressing();
    void mountsSuperstructureOnSnap();
    void reportsSuperstructureOutOfRange();
    void appliesHullTypeMaterialOverrides();

private:
    const MeshData *mesh(const char *id) const { return m_package.mesh(QString::fromLatin1(id), nullptr); }

    QTemporaryDir m_dir{nlhc::test::scratchTemplate(QStringLiteral("nlhc-assembler"))};
    HullPackage m_package;
    HullAssembler m_assembler;
};

void TstAssembler::initTestCase()
{
    QVERIFY2(m_dir.isValid(), qPrintable(m_dir.errorString()));
    test::createMockPackage(m_dir.filePath(QStringLiteral("package")));
    QString error;
    QVERIFY2(m_package.load(m_dir.filePath(QStringLiteral("package")), &error), qPrintable(error));
}

void TstAssembler::assemblesThreeSegmentsByTranslation()
{
    const AssemblyResult result = m_assembler.assemble(m_package, QStringLiteral("Stock/Bulk Hauler"),
                                                       test::bulkConfig());
    QVERIFY(result.renderable);
    QCOMPARE(int(result.items.size()), 3);
    QVERIFY2(result.diagnostics.isEmpty(),
             qPrintable(QStringLiteral("%1 diagnostics").arg(result.diagnostics.size())));

    // bow.localPosition   = core.foreSnap - bow.aftSnap  = (0,0,0) - (5,0,0)
    // stern.localPosition = core.aftSnap  - stern.foreSnap = (-20,0,0) - (5,0,0)
    // every mock node sits at z = 3, and the centring shift is exactly -3, so z lands on 0
    verifyVector(worldOrigin(result.items.at(0)), QVector3D(-5.0f, 0.0f, 0.0f), 1e-4f, "bow node");
    verifyVector(worldOrigin(result.items.at(1)), QVector3D(0.0f, 0.0f, 0.0f), 1e-4f, "core node");
    verifyVector(worldOrigin(result.items.at(2)), QVector3D(-25.0f, 0.0f, 0.0f), 1e-4f, "stern node");

    verifyVector(result.bounds.minimum, QVector3D(-26.0f, -1.0f, -1.0f), 1e-4f, "bounds minimum");
    verifyVector(result.bounds.maximum, QVector3D(1.0f, 1.0f, 1.0f), 1e-4f, "bounds maximum");
}

void TstAssembler::centresAssemblyOnZ()
{
    const AssemblyResult result = m_assembler.assemble(m_package, QStringLiteral("Stock/Bulk Hauler"),
                                                       test::bulkConfig());
    QVERIFY(result.bounds.valid);
    QVERIFY2(qAbs(result.bounds.center().z()) < 1e-4f, qPrintable(QString::number(result.bounds.center().z())));
    // The whole assembly keeps its shape, it only moves along z.
    verifyVector(result.bounds.size(), QVector3D(27.0f, 2.0f, 2.0f), 1e-4f, "bounds size");
}

void TstAssembler::reportsUnknownHullType()
{
    const AssemblyResult result = m_assembler.assemble(m_package, QStringLiteral("no/such/hull"),
                                                       test::bulkConfig());
    QVERIFY(!result.renderable);
    QVERIFY(result.items.isEmpty());
    QCOMPARE(int(result.diagnostics.size()), 1);
    QCOMPARE(result.diagnostics.first().code, QString::fromLatin1(diag::UnknownHullType));
}

void TstAssembler::reportsUnknownSegment()
{
    RandomHullConfig config = test::bulkConfig();
    config.primary[RandomHullConfig::Bow].key = QStringLiteral("11111111-2222-3333-4444-555555555555");
    const AssemblyResult result = m_assembler.assemble(m_package, QStringLiteral("Stock/Bulk Hauler"), config);
    QVERIFY(result.renderable); // the remaining segments still draw
    QCOMPARE(int(result.items.size()), 2);
    QCOMPARE(countCode(result.diagnostics, diag::UnknownSegment), 1);
    QCOMPARE(result.diagnostics.first().segment, int(RandomHullConfig::Bow));
    QCOMPARE(result.diagnostics.first().severity, Severity::Error);
}

void TstAssembler::selectsDressingVariant()
{
    const MeshData *bowMesh = mesh("m:pdbow");
    QVERIFY(bowMesh);

    const AssemblyResult first = m_assembler.assemble(m_package, QStringLiteral("Test/Dressing"),
                                                      test::dressingConfig({0, 0}));
    QVERIFY(first.renderable);
    QVERIFY(first.diagnostics.isEmpty());
    // variant A (point 0) + variant E (point 1)
    QCOMPARE(countByMesh(first, bowMesh), 2);
    // bow variants sit at z = 0 while core/stern sit at z = 3: bounds z = [-1, 4], centre 1.5
    verifyVector(first.bounds.maximum, QVector3D(1.0f, 1.0f, 2.5f), 1e-4f, "bounds maximum, variant A");
    verifyVector(first.bounds.minimum, QVector3D(-26.0f, -1.0f, -2.5f), 1e-4f, "bounds minimum, variant A");

    const AssemblyResult second = m_assembler.assemble(m_package, QStringLiteral("Test/Dressing"),
                                                       test::dressingConfig({1, 0}));
    QVERIFY(second.renderable);
    QVERIFY(second.diagnostics.isEmpty());
    // variant B + its child + variant E
    QCOMPARE(countByMesh(second, bowMesh), 3);
    // the child sits 10 forward of the variant root, so the assembly is re-centred around z = 8
    verifyVector(second.bounds.maximum, QVector3D(1.0f, 1.0f, 6.0f), 1e-4f, "bounds maximum, variant B");
    verifyVector(second.bounds.minimum, QVector3D(-26.0f, -1.0f, -6.0f), 1e-4f, "bounds minimum, variant B");
}

void TstAssembler::keepsAllVariantsWhenDressingIsShort()
{
    const MeshData *bowMesh = mesh("m:pdbow");
    QVERIFY(bowMesh);

    const AssemblyResult empty = m_assembler.assemble(m_package, QStringLiteral("Test/Dressing"),
                                                      test::dressingConfig({}));
    QVERIFY(empty.renderable);
    QCOMPARE(countByMesh(empty, bowMesh), 4); // every variant of both points stays visible
    QCOMPARE(countCode(empty.diagnostics, diag::DressingLengthMismatch), 2);
    QCOMPARE(empty.diagnostics.first().severity, Severity::Warning);

    const AssemblyResult partial = m_assembler.assemble(m_package, QStringLiteral("Test/Dressing"),
                                                        test::dressingConfig({1}));
    QVERIFY(partial.renderable);
    QCOMPARE(countByMesh(partial, bowMesh), 3); // variant B + child, plus every variant of point 1
    QCOMPARE(countCode(partial.diagnostics, diag::DressingLengthMismatch), 1);
    QCOMPARE(partial.diagnostics.first().dressingIndex, 1);
}

void TstAssembler::emptiesDressingPointOutOfRange()
{
    const MeshData *bowMesh = mesh("m:pdbow");
    QVERIFY(bowMesh);

    // point 0 out of range (empty) and point 1 on its allowEmpty slot (empty as well)
    RandomHullConfig config = test::dressingConfig({5, 1});
    const AssemblyResult result = m_assembler.assemble(m_package, QStringLiteral("Test/Dressing"), config);
    QVERIFY(result.renderable); // core and stern still draw
    QCOMPARE(countByMesh(result, bowMesh), 0);
    QCOMPARE(countCode(result.diagnostics, diag::DressingOutOfRange), 1);
    QCOMPARE(result.diagnostics.first().dressingIndex, 0);
    QCOMPARE(result.diagnostics.first().segment, int(RandomHullConfig::Bow));
    QCOMPARE(int(result.items.size()), 2);

    // only point 0 is emptied, point 1 keeps its variant
    const AssemblyResult partial = m_assembler.assemble(m_package, QStringLiteral("Test/Dressing"),
                                                        test::dressingConfig({5, 0}));
    QCOMPARE(countByMesh(partial, bowMesh), 1);

    // Negative choices behave like an out of range index, not like a crash.
    const AssemblyResult negative = m_assembler.assemble(m_package, QStringLiteral("Test/Dressing"),
                                                         test::dressingConfig({-3, 1}));
    QCOMPARE(countByMesh(negative, bowMesh), 0);
    QCOMPARE(countCode(negative.diagnostics, diag::DressingOutOfRange), 1);
}

void TstAssembler::honoursAllowEmptyDressing()
{
    const MeshData *bowMesh = mesh("m:pdbow");
    QVERIFY(bowMesh);

    // point 1 has one variant and allowEmpty: choice 1 == variants.size() means "empty"
    const AssemblyResult allowsEmpty = m_assembler.assemble(m_package, QStringLiteral("Test/Dressing"),
                                                            test::dressingConfig({1, 1}));
    QVERIFY(allowsEmpty.diagnostics.isEmpty());
    QCOMPARE(countByMesh(allowsEmpty, bowMesh), 2);

    // choice 2 is out of range even though the point allows an empty slot
    const AssemblyResult outOfRange = m_assembler.assemble(m_package, QStringLiteral("Test/Dressing"),
                                                           test::dressingConfig({1, 2}));
    QCOMPARE(countCode(outOfRange.diagnostics, diag::DressingOutOfRange), 1);
    QCOMPARE(outOfRange.diagnostics.first().dressingIndex, 1);
    QCOMPARE(countByMesh(outOfRange, bowMesh), 2);
}

void TstAssembler::mountsSuperstructureOnSnap()
{
    RandomHullConfig config = test::bulkConfig();
    SecondaryConfig secondary;
    secondary.key = test::keySuperstructure();
    secondary.segment = RandomHullConfig::Core;
    secondary.snap = 0;
    config.secondary.append(secondary);

    const AssemblyResult result = m_assembler.assemble(m_package, QStringLiteral("Stock/Bulk Hauler"), config);
    QVERIFY(result.renderable);
    QVERIFY(result.diagnostics.isEmpty());
    QCOMPARE(int(result.items.size()), 4);

    const MeshData *supMesh = mesh("m:sup");
    QVERIFY(supMesh);
    QCOMPARE(countByMesh(result, supMesh), 1);
    for (const DrawItem &item : result.items) {
        if (item.mesh != supMesh)
            continue;
        // core localPosition (0,0,-3) + core snap (0,5,0), localPosition of the mount is 0
        verifyVector(worldOrigin(item), QVector3D(0.0f, 5.0f, -3.0f), 1e-4f, "superstructure origin");
    }
    verifyVector(result.bounds.maximum, QVector3D(1.0f, 6.0f, 1.0f), 1e-4f, "bounds with superstructure");
}

void TstAssembler::reportsSuperstructureOutOfRange()
{
    RandomHullConfig config = test::bulkConfig();
    SecondaryConfig badSegment;
    badSegment.key = test::keySuperstructure();
    badSegment.segment = 7;
    badSegment.snap = 0;
    SecondaryConfig badSnap;
    badSnap.key = test::keySuperstructure();
    badSnap.segment = RandomHullConfig::Core;
    badSnap.snap = 9;
    SecondaryConfig unknownPiece;
    unknownPiece.key = QStringLiteral("99999999-0000-0000-0000-000000000000");
    unknownPiece.segment = RandomHullConfig::Core;
    unknownPiece.snap = 0;
    config.secondary = {badSegment, badSnap, unknownPiece};

    const AssemblyResult result = m_assembler.assemble(m_package, QStringLiteral("Stock/Bulk Hauler"), config);
    QVERIFY(result.renderable);
    QCOMPARE(int(result.items.size()), 3); // primary structure unaffected
    QCOMPARE(countCode(result.diagnostics, diag::SecondarySegmentOutOfRange), 1);
    QCOMPARE(countCode(result.diagnostics, diag::SecondarySnapOutOfRange), 1);
    QCOMPARE(countCode(result.diagnostics, diag::UnknownSecondary), 1);
}

// A mesh shared by two hull types must paint with each hull type's own material.
void TstAssembler::appliesHullTypeMaterialOverrides()
{
    RandomHullConfig config;
    config.primary[0].key = test::keyDressingBow();
    config.primary[1].key = test::keyDressingCore();
    config.primary[2].key = test::keyStern();

    const AssemblyResult overridden = m_assembler.assemble(m_package, QStringLiteral("Test/Dressing"), config);
    const AssemblyResult plain = m_assembler.assemble(m_package, QStringLiteral("Stock/Bulk Hauler"), config);

    bool sawFixed = false;
    for (const DrawItem &item : overridden.items) {
        if (item.material && item.material->id == QLatin1String("t:fixed"))
            sawFixed = true;
    }
    QVERIFY2(sawFixed, "the hull type override never reached the assembly");

    for (const DrawItem &item : plain.items) {
        if (item.material)
            QVERIFY2(item.material->id != QLatin1String("t:fixed"),
                     "a hull type without an override picked up another type's material");
    }
    // The canonical piece stays untouched.
    const Piece *piece = m_package.piece(QStringLiteral("pd:bow"));
    QVERIFY(piece);
    QCOMPARE(piece->nodes.at(0).materialId, QStringLiteral("t:paint"));
}

void TstAssembler::cleanupTestCase()
{
    // The scratch dir lives under <build>/test-tmp; removing it here keeps even an
    // interrupted run from leaving a directory behind.
    QDir(m_dir.path()).removeRecursively();
}

QTEST_GUILESS_MAIN(TstAssembler)

#include "tst_assembler.moc"
