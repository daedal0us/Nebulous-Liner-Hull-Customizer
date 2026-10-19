#include "SteamLocator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>
#include <QTextStream>

namespace nlhc {
namespace {

// Steam moved libraries around a lot over the years, so accept both spellings
// of the registry key.
QString steamInstallPath()
{
    const QStringList candidates = {
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"),
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam"),
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Valve\\Steam"),
    };
    for (const QString &key : candidates) {
        QSettings steam(key, QSettings::NativeFormat);
        for (const QString &name : {QStringLiteral("SteamPath"), QStringLiteral("InstallPath")}) {
            const QString value = steam.value(name).toString().trimmed();
            if (!value.isEmpty() && QFileInfo(value).isDir())
                return QDir::cleanPath(QDir::fromNativeSeparators(value));
        }
    }
    return QString();
}

// libraryfolders.vdf is a small Valve KeyValues file: every library is one
// "path" "D:\\SteamLibrary" line.
QStringList librariesFromVdf(const QString &steamPath)
{
    QStringList roots;
    QFile file(QDir(steamPath).filePath(QStringLiteral("steamapps/libraryfolders.vdf")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return roots;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (!line.startsWith(QLatin1String("\"path\""), Qt::CaseInsensitive))
            continue;
        const int first = line.indexOf(QLatin1Char('"'), 6);
        const int second = line.indexOf(QLatin1Char('"'), first + 1);
        if (first < 0 || second < 0)
            continue;
        QString path = line.mid(first + 1, second - first - 1);
        path.replace(QLatin1String("\\\\"), QLatin1String("\\"));
        if (QFileInfo(path).isDir())
            roots.append(QDir::cleanPath(QDir::fromNativeSeparators(path)));
    }
    return roots;
}

} // namespace

QStringList steamLibraryRoots()
{
    const QString steamPath = steamInstallPath();
    if (steamPath.isEmpty())
        return {};

    QStringList roots{steamPath};
    for (const QString &root : librariesFromVdf(steamPath)) {
        if (!roots.contains(root))
            roots.append(root);
    }
    return roots;
}

QString fleetFolderIn(const QStringList &libraryRoots)
{
    const QStringList gameFolders = {QStringLiteral("Nebulous"),
                                     QStringLiteral("Nebulous Fleet Command")};
    for (const QString &root : libraryRoots) {
        for (const QString &game : gameFolders) {
            const QString folder = QDir(root).filePath(
                QStringLiteral("steamapps/common/%1/Saves/Fleets").arg(game));
            if (QFileInfo(folder).isDir())
                return QDir::cleanPath(folder);
        }
    }
    return QString();
}

QString locateFleetFolder()
{
    return fleetFolderIn(steamLibraryRoots());
}

} // namespace nlhc
