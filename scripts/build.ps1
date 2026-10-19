[CmdletBinding()]
param(
    [ValidateSet('mingw-release', 'mingw-debug')]
    [string]$Preset = 'mingw-release',
    [switch]$SkipTests,
    [switch]$Deploy,
    [switch]$Sign,
    [switch]$NoTrust,
    [string]$Prefix = 'dist'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$qtBin = 'C:\Qt\6.8.0\mingw_64\bin'
$mingwBin = 'C:\Qt\Tools\mingw1310_64\bin'
$cmake = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
$cmakeBin = Split-Path -Parent $cmake
$ninjaDir = 'C:\Qt\Tools\Ninja'

$env:PATH = "$qtBin;$mingwBin;$ninjaDir;$cmakeBin;$env:PATH"
$buildDir = Join-Path $root "build/$Preset"

Write-Host "== configure ($Preset)" -ForegroundColor Cyan
$configureArgs = @()
if ($Sign) {
    # Remember the certificate in the build cache so that a plain
    # 'cmake --build' keeps re-signing every relinked binary, not just this run.
    $cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert |
        Sort-Object NotAfter -Descending | Select-Object -First 1
    if ($cert) { $configureArgs += "-DNLHC_SIGN_CERT=$($cert.Thumbprint)" }
}
& $cmake --preset $Preset @configureArgs
if ($LASTEXITCODE -ne 0) { throw 'configure failed' }

Write-Host '== build' -ForegroundColor Cyan
& $cmake --build --preset $Preset
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

if (-not $SkipTests) {
    Write-Host '== test' -ForegroundColor Cyan
    & (Join-Path $cmakeBin 'ctest.exe') --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'tests failed' }
}

if ($Deploy) {
    $distDir = Join-Path $root $Prefix
    # cmake --install only adds and overwrites, so a leftover tree keeps files
    # that the current asset export no longer produces. Rebuild it from scratch.
    if (Test-Path $distDir) {
        Write-Host "== clean ($distDir)" -ForegroundColor Cyan
        # A shell, an indexer or an antivirus scan can hold the folder itself;
        # clearing its contents gets the same result in that case.
        try {
            Remove-Item $distDir -Recurse -Force -ErrorAction Stop
        } catch {
            Get-ChildItem $distDir -Force | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    Write-Host "== install ($distDir)" -ForegroundColor Cyan
    & $cmake --install $buildDir --prefix $distDir
    if ($LASTEXITCODE -ne 0) { throw 'install failed' }
}

if ($Sign) {
    # Both copies get signed: the packaged one that ships, and the build-tree one
    # that is double-clicked while iterating (Windows judges whichever gets
    # launched, and a relink drops the previous signature).
    Write-Host '== sign binaries' -ForegroundColor Cyan
    $signArgs = @{
        DistDir   = $(if ($Deploy) { $Prefix } else { '' })
        BuildDir  = "build/$Preset"
        TrustCert = -not $NoTrust
    }
    & (Join-Path $PSScriptRoot 'sign.ps1') @signArgs
    if ($LASTEXITCODE -ne 0) { throw 'signing failed' }
}

Write-Host '== done' -ForegroundColor Green
