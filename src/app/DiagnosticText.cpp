#include "DiagnosticText.h"

#include <QCoreApplication>

namespace nlhc {
namespace {

QString segmentLabel(int segment)
{
    switch (segment) {
    case 0:
        return QCoreApplication::translate("Diagnostic", "bow");
    case 1:
        return QCoreApplication::translate("Diagnostic", "core");
    case 2:
        return QCoreApplication::translate("Diagnostic", "stern");
    default:
        return QStringLiteral("-");
    }
}

} // namespace

QString segmentName(int segment)
{
    return segmentLabel(segment);
}

QString diagnosticText(const Diagnostic &diagnostic)
{
    const QString code = diagnostic.code;
    const QString tail = diagnostic.detail.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(diagnostic.detail);

    if (code == QLatin1String(diag::UnsupportedHullType))
        return QCoreApplication::translate("Diagnostic", "This hull type cannot be customized.") + tail;
    if (code == QLatin1String(diag::UnknownHullType))
        return QCoreApplication::translate("Diagnostic", "The asset package does not contain this hull type.") + tail;
    if (code == QLatin1String(diag::MissingHullConfig))
        return QCoreApplication::translate("Diagnostic", "The ship has no hull configuration; the game will randomize it.") + tail;
    if (code == QLatin1String(diag::UnsupportedHullConfig))
        return QCoreApplication::translate("Diagnostic", "The hull configuration type is not supported; the game will randomize it.") + tail;
    if (code == QLatin1String(diag::PrimaryStructureSize))
        return QCoreApplication::translate("Diagnostic", "The hull needs exactly three primary segments (bow, core, stern).") + tail;
    if (code == QLatin1String(diag::UnknownSegment))
        return QCoreApplication::translate("Diagnostic", "A %1 segment is unknown to the asset package; the game would randomize it.")
                   .arg(segmentLabel(diagnostic.segment)) + tail;
    if (code == QLatin1String(diag::DressingLengthMismatch))
        return QCoreApplication::translate("Diagnostic", "The %1 segment lists %2 dressing values but has %3 dressing points.")
                   .arg(segmentLabel(diagnostic.segment))
                   .arg(QString::number(diagnostic.dressingIndex)) + tail;
    if (code == QLatin1String(diag::DressingOutOfRange))
        return QCoreApplication::translate("Diagnostic", "The %1 segment dressing #%2 selects a variant that does not exist.")
                   .arg(segmentLabel(diagnostic.segment))
                   .arg(QString::number(diagnostic.dressingIndex + 1)) + tail;
    if (code == QLatin1String(diag::UnknownSecondary))
        return QCoreApplication::translate("Diagnostic", "A superstructure is unknown to the asset package; the game would drop it.") + tail;
    if (code == QLatin1String(diag::SecondarySegmentOutOfRange))
        return QCoreApplication::translate("Diagnostic", "A superstructure targets a segment that does not exist; the game would crash.") + tail;
    if (code == QLatin1String(diag::SecondarySnapOutOfRange))
        return QCoreApplication::translate("Diagnostic", "A superstructure targets a snap point that does not exist; the game would crash.") + tail;
    if (code == QLatin1String(diag::PackageMissing))
        return QCoreApplication::translate("Diagnostic", "The hull asset package is missing.") + tail;
    if (code == QLatin1String(diag::PackageCorrupt))
        return QCoreApplication::translate("Diagnostic", "The hull asset package could not be read.") + tail;
    if (code == QLatin1String(diag::MeshMissing))
        return QCoreApplication::translate("Diagnostic", "A hull mesh is missing from the asset package.") + tail;
    if (code == QLatin1String(diag::TextureMissing))
        return QCoreApplication::translate("Diagnostic", "A hull texture is missing from the asset package.") + tail;
    return diagnostic.detail;
}

QString readOnlyReasonText(const ShipRecord &ship, const HullPackage &package)
{
    if (ship.index >= 10)
        return QCoreApplication::translate("Diagnostic", "The game only loads the first ten ships of a fleet.");
    if (!package.isValid())
        return QCoreApplication::translate("Diagnostic", "No hull asset package is loaded.");
    if (!package.isEditableHullType(ship.hullType))
        return QCoreApplication::translate("Diagnostic", "Hull type '%1' is not supported for customization.").arg(ship.hullType);
    return QCoreApplication::translate("Diagnostic", "This ship has no random hull configuration.");
}

} // namespace nlhc
