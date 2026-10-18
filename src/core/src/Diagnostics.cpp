#include "nlhc/Diagnostics.h"

namespace nlhc {
namespace {

QString segmentRole(int segment)
{
    switch (segment) {
    case 0:
        return QStringLiteral("bow");
    case 1:
        return QStringLiteral("core");
    case 2:
        return QStringLiteral("stern");
    default:
        return QString();
    }
}

QString location(const Diagnostic &diagnostic)
{
    QString where;
    if (diagnostic.shipIndex >= 0)
        where = QStringLiteral("Ship %1").arg(diagnostic.shipIndex + 1);
    const QString role = segmentRole(diagnostic.segment);
    if (!role.isEmpty())
        where += where.isEmpty() ? role : QStringLiteral(" %1").arg(role);
    if (diagnostic.dressingIndex >= 0)
        where += where.isEmpty() ? QString() : QStringLiteral(" ");
    if (where.isEmpty())
        return QString();
    return where + QStringLiteral(": ");
}

QString message(const Diagnostic &diagnostic)
{
    if (diagnostic.code == QLatin1String(diag::UnsupportedHullType))
        return QStringLiteral("This hull type cannot be customized and is shown read-only.");
    if (diagnostic.code == QLatin1String(diag::UnknownHullType))
        return QStringLiteral("The asset package does not contain this hull type.");
    if (diagnostic.code == QLatin1String(diag::MissingHullConfig))
        return QStringLiteral("HullConfig is missing; the game rebuilds a random hull for this ship.");
    if (diagnostic.code == QLatin1String(diag::UnsupportedHullConfig))
        return QStringLiteral("Unsupported HullConfig type; the game rebuilds a random hull for this ship.");
    if (diagnostic.code == QLatin1String(diag::PrimaryStructureSize))
        return QStringLiteral("PrimaryStructure must hold exactly 3 segments.");
    if (diagnostic.code == QLatin1String(diag::UnknownSegment))
        return QStringLiteral("This segment key is not offered by the hull type; the game picks a random piece.");
    if (diagnostic.code == QLatin1String(diag::DressingLengthMismatch))
        return QStringLiteral("Fewer dressing entries than dressing points; the game shows every variant "
                              "of the unconfigured points at once.");
    if (diagnostic.code == QLatin1String(diag::DressingOutOfRange)) {
        if (diagnostic.dressingIndex >= 0)
            return QStringLiteral("Dressing choice %1 is out of range; the game hides this dressing point.")
                .arg(diagnostic.dressingIndex);
        return QStringLiteral("A dressing choice is out of range; the game hides that dressing point.");
    }
    if (diagnostic.code == QLatin1String(diag::UnknownSecondary))
        return QStringLiteral("This superstructure key is not offered by the hull type; the game skips it.");
    if (diagnostic.code == QLatin1String(diag::SecondarySegmentOutOfRange))
        return QStringLiteral("The superstructure segment must be 0 (bow), 1 (core) or 2 (stern).");
    if (diagnostic.code == QLatin1String(diag::SecondarySnapOutOfRange))
        return QStringLiteral("The superstructure snap point does not exist on that segment.");
    if (diagnostic.code == QLatin1String(diag::PackageMissing))
        return QStringLiteral("The hull asset package is missing.");
    if (diagnostic.code == QLatin1String(diag::PackageCorrupt))
        return QStringLiteral("The hull asset package is corrupt.");
    if (diagnostic.code == QLatin1String(diag::MeshMissing))
        return QStringLiteral("A mesh referenced by this hull is missing or unreadable.");
    if (diagnostic.code == QLatin1String(diag::TextureMissing))
        return QStringLiteral("A texture referenced by this hull is missing or unreadable.");
    return diagnostic.code.isEmpty() ? QStringLiteral("Unknown problem.") : diagnostic.code;
}

} // namespace

QString describeDiagnostic(const Diagnostic &diagnostic)
{
    QString text = location(diagnostic) + message(diagnostic);
    if (!diagnostic.detail.isEmpty())
        text += QStringLiteral(" (%1)").arg(diagnostic.detail);
    return text;
}

} // namespace nlhc
