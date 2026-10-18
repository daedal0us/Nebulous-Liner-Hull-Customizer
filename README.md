# Liner Hull Customizer

**English** · [简体中文](README.zh-CN.md)

A desktop editor for the modular hulls of the Protectorate liners in
*Nebulous: Fleet Command* `.fleet` files. Open a fleet, choose the bow / core / stern pieces,
the dressing variant of every attachment point and any superstructure, preview the assembled
hull in an orthographic side view and an orbitable 3D view, then save the file back — untouched
everywhere you did not edit it.

| Hull type | Ship |
|---|---|
| `Stock/Bulk Hauler` | Marauder |
| `Stock/Container Hauler` | Moorline |
| `Stock/Container Hauler Refit` | Moorline |

Every other hull type is listed read-only, so nothing in a fleet is ever lost.

## Requirements

- Windows 10 or 11 with an OpenGL 3.3 capable driver
- Qt 6.8.0 (MinGW 13.1 kit), CMake 3.21 or newer, Ninja

`CMakePresets.json` expects Qt in `C:/Qt/6.8.0/mingw_64` and the toolchain in
`C:/Qt/Tools/mingw1310_64`. Point `CMAKE_PREFIX_PATH`, `CMAKE_CXX_COMPILER` and
`CMAKE_RC_COMPILER` at your own installation to build somewhere else.

## Build

```powershell
./scripts/build.ps1            # configure, build, run the tests
./scripts/build.ps1 -Deploy    # additionally produce a runnable tree in ./dist
./scripts/package.ps1          # pack ./dist into dist/NLHC-<version>-win64.zip
```

`-Deploy` rebuilds `dist` from scratch instead of installing over the previous tree, so an
archive never carries files the current sources no longer produce. `package.ps1` stages that
tree under a single `NLHC-<version>-win64/` folder, launches the staged copy once to prove the
dependency set is complete, and writes the archive plus a `.sha256` next to it.

The plain CMake flow works as well:

```powershell
cmake --preset mingw-release
cmake --build --preset mingw-release
ctest --preset mingw-release
```

Artifacts: `NLHC.exe` (application), `NLHCCore.dll` (fleet, asset and assembly library) and
`NLHCRender`, a static rendering library shared with the tests.

The archive also carries `Liner Hull Customizer.exe` at its root: a launcher that starts
`bin/NLHC.exe` from wherever the folder was unpacked.

## Using the application

1. **File → Open fleet…** and pick a `.fleet` file. Every ship is listed; the supported hull
   types become editable, the rest stay read-only.
2. Pick the bow / core / stern pieces under **Primary structure**. Each piece exposes its
   dressing points; choose a variant per point (`(empty)` is offered where the game allows an
   empty slot).
3. Edit the **Superstructure** entries (piece / segment / snap point). Combinations that would
   break the hull are refused and explained in **Diagnostics**.
4. **Base color** and **Trim color** default to `#1b405b` and `#f26522`. They are preview
   settings and are not written to the fleet; **Hull tint** is a real fleet field and is written
   back.
5. **File → Save** writes the fleet in place and keeps a `.bak` copy. Unknown XML is preserved
   verbatim, so a file only changes where you changed it.

The viewport is split: the left half is an orthographic side view, the right half is an orbit
view (drag with the left button, zoom with the wheel, pan with the middle or right button,
double-click to reset).

Command line:

```text
NLHC.exe --open <fleet> [--ship <index>] [--screenshot <png>]   # render without the UI
NLHC.exe --render-demo <png>                                     # render the synthetic demo hull
```

## Layout

| Path | Purpose |
|---|---|
| `src/core` | `NLHCCore.dll` — `.fleet` document model, asset package, hull assembly, validation |
| `src/app` | `NLHC.exe` — Qt Widgets UI, OpenGL viewport, translations |
| `assets/hulls` | hull geometry and textures the viewport draws (committed) |
| `samples` | example fleets |
| `i18n` | `nlhc_zh_CN.ts`; the UI language is switchable at runtime |
| `scripts` | build, packaging and release helpers |

## Licence

MIT — see [LICENSE](LICENSE).
