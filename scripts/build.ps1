[CmdletBinding()]
param(
    [ValidateSet('mingw-release', 'mingw-debug')]
    [string]$Preset = 'mingw-release',
    [switch]$SkipTests,
    [switch]$Deploy,
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
& $cmake --preset $Preset
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
    Write-Host "== install ($distDir)" -ForegroundColor Cyan
    & $cmake --install $buildDir --prefix $distDir
    if ($LASTEXITCODE -ne 0) { throw 'install failed' }
}

Write-Host '== done' -ForegroundColor Green
