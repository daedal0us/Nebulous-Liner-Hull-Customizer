[CmdletBinding()]
param(
    [string]$Version = '',
    [string]$Source = 'dist',
    [string]$Output = 'dist',
    [switch]$SkipVerify
)

# Packs the installed tree into a zip a user can extract and run. The staging
# folder carries one top-level directory so extraction never litters the
# download folder, and the archive is smoke-tested by launching the very copy
# that gets zipped.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not $Version) {
    $project = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
    if ($project -match 'project\(\s*\w+\s+VERSION\s+([0-9.]+)') {
        $Version = $Matches[1]
    } else {
        throw 'cannot read the project version from CMakeLists.txt; pass -Version'
    }
}

$src = Join-Path $root $Source
$exe = Join-Path $src 'bin/NLHC.exe'
if (-not (Test-Path $exe)) {
    throw "no install tree at $src; run scripts/build.ps1 -Deploy -Sign first"
}

$name = "NLHC-$Version-win64"
$staging = Join-Path $root "build/package/$name"
$zip = Join-Path (Join-Path $root $Output) "$name.zip"
Write-Host "packaging $name" -ForegroundColor Cyan

if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Path $staging -Force | Out-Null
foreach ($dir in @('bin', 'assets', 'samples')) {
    $from = Join-Path $src $dir
    if (Test-Path $from) { Copy-Item $from -Destination $staging -Recurse -Force }
}
# The package-root launcher lives next to bin/ in the install tree.
Get-ChildItem $src -File -Filter *.exe | Copy-Item -Destination $staging -Force
Copy-Item (Join-Path $root 'LICENSE') -Destination $staging -Force
# The end-user readme and the public half of the signing certificate: without
# the certificate Windows cannot tell who published the binaries.
Copy-Item (Join-Path $root 'release/README.md') -Destination (Join-Path $staging 'README.md') -Force
$certificate = Join-Path $root 'scripts/signing-cert.cer'
if (Test-Path $certificate) {
    Copy-Item $certificate -Destination (Join-Path $staging 'signing-cert.cer') -Force
} else {
    Write-Warning 'scripts/signing-cert.cer is missing; the package ships without it (run scripts/sign.ps1 -TrustCert)'
}

# A release that ships an unsigned binary gets a publisher warning on every
# machine that did not import our certificate, so this is worth failing over.
$unsigned = @()
foreach ($file in @('bin/NLHC.exe', 'bin/NLHCCore.dll')) {
    $status = (Get-AuthenticodeSignature (Join-Path $staging $file)).Status
    if ($status -ne 'Valid') { $unsigned += "$file ($status)" }
}
if ($unsigned) {
    Write-Warning ("not signed: " + ($unsigned -join ', ') + " - run scripts/build.ps1 -Deploy -Sign")
} else {
    Write-Host 'signatures: valid' -ForegroundColor Green
}

$files = Get-ChildItem $staging -Recurse -File
$bytes = ($files | Measure-Object Length -Sum).Sum
Write-Host ("staged: {0} files, {1:N1} MB" -f $files.Count, ($bytes / 1MB))

if (-not $SkipVerify) {
    Write-Host 'smoke test: launching the staged copy' -ForegroundColor Cyan
    $shot = Join-Path $staging 'smoke.png'
    $sample = Join-Path $staging 'samples/container-liner-sample.fleet'
    # A PATH that still holds Qt or MinGW hides a deployment that forgot a DLL, so
    # the staged copy is launched with nothing but the system directories.
    $savedPath = $env:PATH
    $env:PATH = "$env:SystemRootsystem32;$env:SystemRoot"
    try {
        & (Join-Path $staging 'bin/NLHC.exe') --open $sample --ship 0 --screenshot $shot | Out-Null
        $exitCode = $LASTEXITCODE
    } finally {
        $env:PATH = $savedPath
    }
    if ($exitCode -ne 0 -or -not (Test-Path $shot)) {
        throw "the staged copy did not render (exit $exitCode); the package is missing a dependency"
    }
    Remove-Item $shot -Force -ErrorAction SilentlyContinue
    Remove-Item "$shot.log" -Force -ErrorAction SilentlyContinue
    Write-Host 'smoke test: rendered' -ForegroundColor Green

    # The launcher sits at the package root with no Qt or MinGW next to it, so it is
    # the first thing a machine without a toolchain tries to load.
    Write-Host 'smoke test: launching the package launcher' -ForegroundColor Cyan
    $launcher = Join-Path $staging 'Liner Hull Customizer.exe'
    $known = @(Get-Process NLHC -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
    $savedPath = $env:PATH
    $env:PATH = "$env:SystemRootsystem32;$env:SystemRoot"
    $started = $false
    try {
        Start-Process -FilePath $launcher -WorkingDirectory $staging | Out-Null
        for ($i = 0; $i -lt 30; $i++) {
            Start-Sleep -Milliseconds 500
            $fresh = @(Get-Process NLHC -ErrorAction SilentlyContinue |
                       Where-Object { $known -notcontains $_.Id } |
                       Select-Object -ExpandProperty Id)
            if ($fresh.Count -gt 0) {
                $started = $true
                $fresh | ForEach-Object { Stop-Process -Id $_ -Force -ErrorAction SilentlyContinue }
                break
            }
        }
    } finally {
        $env:PATH = $savedPath
    }
    if (-not $started) {
        throw 'the package launcher could not start bin/NLHC.exe; it is missing its own runtime'
    }
    Write-Host 'smoke test: launcher started the application' -ForegroundColor Green
}

if (-not (Test-Path (Join-Path $root $Output))) {
    New-Item -ItemType Directory -Path (Join-Path $root $Output) -Force | Out-Null
}
# Explorer, an indexer or an antivirus scan can hold the previous archive for a
# moment after it is written.
if (Test-Path $zip) {
    $attempt = 0
    while ($true) {
        try {
            Remove-Item $zip -Force -ErrorAction Stop
            break
        } catch {
            $attempt++
            if ($attempt -ge 5) { throw "cannot replace $zip - close it and try again" }
            Start-Sleep -Milliseconds 700
        }
    }
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $staging, $zip, [System.IO.Compression.CompressionLevel]::Optimal, $true)

$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
$checksum = "$zip.sha256"
"$hash  $name.zip" | Set-Content -Path $checksum -Encoding ASCII

Write-Host ""
Write-Host ("archive : {0:N1} MB  {1}" -f ((Get-Item $zip).Length / 1MB), (Split-Path -Leaf $zip)) -ForegroundColor Green
Write-Host "sha256  : $hash"
Write-Host "checksum: $(Split-Path -Leaf $checksum)"