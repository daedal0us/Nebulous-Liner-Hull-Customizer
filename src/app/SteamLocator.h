#pragma once

#include <QString>
#include <QStringList>

namespace nlhc {

// Every Steam library root this machine knows about: the install path from the
// registry plus the entries in its steamapps/libraryfolders.vdf.
QStringList steamLibraryRoots();

// First <root>/steamapps/common/<game>/Saves/Fleets that exists, or an empty
// string. Pure lookup so it can be tested without Steam installed.
QString fleetFolderIn(const QStringList &libraryRoots);

// steamLibraryRoots() fed through fleetFolderIn().
QString locateFleetFolder();

} // namespace nlhc
