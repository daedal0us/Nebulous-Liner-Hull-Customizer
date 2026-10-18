#pragma once

#include <QString>
#include <QVector>

#include "nlhc/Export.h"

namespace nlhc {

enum class Severity { Info, Warning, Error };

struct Diagnostic
{
    Severity severity = Severity::Error;
    QString code;
    QString detail;
    int shipIndex = -1;
    int segment = -1;
    int dressingIndex = -1;
};

namespace diag {
inline constexpr const char *UnsupportedHullType = "hull.unsupported";
inline constexpr const char *UnknownHullType = "hull.unknown";
inline constexpr const char *MissingHullConfig = "config.missing";
inline constexpr const char *UnsupportedHullConfig = "config.unsupported";
inline constexpr const char *PrimaryStructureSize = "primary.size";
inline constexpr const char *UnknownSegment = "primary.unknown_segment";
inline constexpr const char *DressingLengthMismatch = "dressing.length_mismatch";
inline constexpr const char *DressingOutOfRange = "dressing.out_of_range";
inline constexpr const char *UnknownSecondary = "secondary.unknown";
inline constexpr const char *SecondarySegmentOutOfRange = "secondary.segment_out_of_range";
inline constexpr const char *SecondarySnapOutOfRange = "secondary.snap_out_of_range";
inline constexpr const char *PackageMissing = "package.missing";
inline constexpr const char *PackageCorrupt = "package.corrupt";
inline constexpr const char *MeshMissing = "asset.mesh_missing";
inline constexpr const char *TextureMissing = "asset.texture_missing";
} // namespace diag

NLHC_CORE_EXPORT QString describeDiagnostic(const Diagnostic &diagnostic);

} // namespace nlhc
