#include <QtTest>

#include <QDir>

#include "MockAssets.h"
#include "SteamLocator.h"

using namespace nlhc;
using namespace nlhc::test;

namespace {

QString makeLibrary(const QString &base, const QString &game)
{
    const QString folder = QDir(base).filePath(
        QStringLiteral("steamapps/common/%1/Saves/Fleets").arg(game));
    QDir().mkpath(folder);
    return QDir::cleanPath(folder);
}

} // namespace

// The Steam lookup itself needs a real installation, so the path rules are
// exercised against a scratch tree instead.
class TstSteamLocator : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void findsTheFleetFolder();
    void skipsLibrariesWithoutTheGame();
    void emptyWhenNothingMatches();

private:
    QString m_root;
};

void TstSteamLocator::initTestCase()
{
    m_root = QDir(scratchRoot()).absoluteFilePath(QStringLiteral("steam"));
    QDir().remove(m_root);
    QVERIFY(QDir().mkpath(m_root));
}

void TstSteamLocator::findsTheFleetFolder()
{
    const QString library = QDir(m_root).filePath(QStringLiteral("library"));
    const QString expected = makeLibrary(library, QStringLiteral("Nebulous"));
    QCOMPARE(fleetFolderIn({library}), expected);
}

void TstSteamLocator::skipsLibrariesWithoutTheGame()
{
    const QString empty = QDir(m_root).filePath(QStringLiteral("empty"));
    const QString library = QDir(m_root).filePath(QStringLiteral("library2"));
    const QString expected = makeLibrary(library, QStringLiteral("Nebulous Fleet Command"));
    QDir().mkpath(QDir(empty).filePath(QStringLiteral("steamapps/common")));
    QCOMPARE(fleetFolderIn({empty, library}), expected);
}

void TstSteamLocator::emptyWhenNothingMatches()
{
    QVERIFY(fleetFolderIn({}).isEmpty());
    QVERIFY(fleetFolderIn({QDir(m_root).filePath(QStringLiteral("does-not-exist"))}).isEmpty());
}

QTEST_MAIN(TstSteamLocator)
#include "tst_steamlocator.moc"
