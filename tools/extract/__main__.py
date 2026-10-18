"""python -m tools.extract --bundle <path> --out assets/hulls [--verify]"""

from __future__ import annotations

import argparse
import sys

from . import __version__
from . import export, verify as verify_module


def parse_bytes(text):
    if text is None:
        return None
    text = text.strip().upper()
    scale = 1
    for suffix, factor in (("GB", 1 << 30), ("MB", 1 << 20), ("KB", 1 << 10)):
        if text.endswith(suffix):
            scale = factor
            text = text[:-len(suffix)]
            break
    return int(float(text) * scale)


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="python -m tools.extract",
        description="Build the assets/hulls package from a compiled asset bundle")
    parser.add_argument("--bundle", required=True,
                        help="path to the UnityFS bundle (e.g. .../AssetBundles/stock-f2)")
    parser.add_argument("--out", required=True, help="output package directory")
    parser.add_argument("--verify", action="store_true",
                        help="assert the acceptance thresholds and exit non-zero")
    parser.add_argument("--aa-dir", default=None,
                        help="Addressables output directory holding catalog.json "
                             "and StandaloneWindows64/*.bundle")
    parser.add_argument("--tex-size", type=int, default=None,
                        help="shorthand for --albedo-size/--mask-size/--normal-size")
    parser.add_argument("--albedo-size", type=int, default=None,
                        help="maximum albedo edge in pixels (default 512)")
    parser.add_argument("--mask-size", type=int, default=None,
                        help="maximum paint mask edge in pixels (default 512)")
    parser.add_argument("--normal-size", type=int, default=None,
                        help="maximum normal map edge in pixels (default 512)")
    parser.add_argument("--no-normals", action="store_true",
                        help="skip the normal map slot entirely")
    parser.add_argument("--max-bytes", default="60MB",
                        help="size budget for the output package (default 60MB)")
    parser.add_argument("--samples", default="samples",
                        help="directory holding the reference .fleet files")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)

    max_bytes = parse_bytes(args.max_bytes)
    sizes = {}
    for slot in ("albedo", "mask", "normal"):
        size = getattr(args, "%s_size" % slot)
        if size is None:
            size = args.tex_size if args.tex_size else export.DEFAULT_SIZES[slot]
        sizes[slot] = size
    log = export.Logger(None if args.quiet else sys.stdout)
    print("nlhc extractor %s" % __version__, file=sys.stderr)
    catalog, stats = export.extract(args.bundle, args.out, sizes=sizes, log=log,
                                    max_bytes=max_bytes, aa_dir=args.aa_dir,
                                    normals=not args.no_normals)
    if stats["textureFailures"]:
        for label, message in stats["textureFailures"][:5]:
            print("  texture %s: %s" % (label, message), file=sys.stderr)

    if not args.verify:
        return 0
    report, extra = verify_module.verify(catalog, args.out, stats,
                                        max_bytes=max_bytes,
                                        samples=args.samples)
    print()
    print("verification report")
    print("-------------------")
    report.render(sys.stdout)
    print()
    print("stats: %d pieces, %d meshes (%d vertices), %d materials, "
          "%d texture failures, %.2f MiB" % (
              stats["pieces"], extra["meshFiles"], extra["vertices"],
              stats["materials"], len(stats["textureFailures"]),
              extra["totalBytes"] / 1048576.0))
    print("textures: %d albedo, %d mask, %d normal at %s px (normal coverage %.0f%%)"
          % (extra["textureCounts"]["albedo"], extra["textureCounts"]["mask"],
             extra["textureCounts"]["normal"],
             "/".join(str(sizes[slot]) for slot in ("albedo", "mask", "normal")),
             extra["normalCoverage"] * 100.0))
    print("extracted in %.1fs" % stats["extractSeconds"])
    if report.failed:
        print("\n%d check(s) failed" % len(report.failed))
        return 1
    print("\nall checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
