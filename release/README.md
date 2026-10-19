# Liner Hull Customizer 0.1.0

Portable build of the hull editor for *Nebulous: Fleet Command* liner fleets.
Extract this folder anywhere and run `bin\NLHC.exe` — no installer, no
dependencies to install, and nothing is written outside this folder apart from
the `.bak` file next to every fleet you save.

## Requirements

Windows 10 or 11 with an OpenGL 3.3 capable graphics driver.

## First run

The binaries are digitally signed, but with a certificate this PC has not trusted
yet, so Windows calls the publisher unknown. To trust it for your account, run
this once in PowerShell from this folder:

```powershell
Import-Certificate -FilePath .\signing-cert.cer -CertStoreLocation Cert:\CurrentUser\Root
Import-Certificate -FilePath .\signing-cert.cer -CertStoreLocation Cert:\CurrentUser\TrustedPublisher
```

## Using it

Open a fleet with **File → Open fleet…**, pick the bow / core / stern pieces and
the dressing variant of each attachment point, and save. The viewport shows the
assembled hull in a flat side view and in a 3D view you can orbit with the mouse.
The interface language is switchable under **Language**.

`samples` holds two fleets to try it with.

## Licence

MIT. This package bundles Qt 6.8, which is licensed under the GNU Lesser General
Public License v3; the Qt libraries are shipped unmodified as DLLs in `bin`.
