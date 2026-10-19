[CmdletBinding()]
param(
    [string]$DistDir = 'dist',
    [string]$BuildDir = 'build/mingw-release'
)

# Reports everything that decides whether Windows will trust a binary, so a
# "still blocked" report can be narrowed down instead of guessed at.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$manifestMarkers = @('requestedExecutionLevel', 'permonitorv2', 'longPathAware')
$problems = 0

function Get-TrustState([string]$Thumbprint, [string]$Scope, [string]$Store) {
    Test-Path (Join-Path "Cert:\$Scope\$Store" $Thumbprint)
}

$targets = @()
foreach ($dir in @($DistDir, $BuildDir)) {
    if (-not $dir) { continue }
    $bin = Join-Path (Join-Path $root $dir) 'bin'
    foreach ($name in @('NLHC.exe', 'NLHCCore.dll')) {
        $path = Join-Path $bin $name
        if (Test-Path $path) { $targets += Get-Item $path }
    }
}
if (-not $targets) { throw "no binaries found under $DistDir or $BuildDir" }

foreach ($file in $targets) {
    Write-Host "$($file.FullName)" -ForegroundColor Cyan
    $sig = Get-AuthenticodeSignature $file.FullName
    $ok = $true

    $valid = $sig.Status -eq 'Valid'
    Write-Host "  signature        : $($sig.Status)"
    if (-not $valid) { $ok = $false }

    $stamped = [bool]$sig.TimeStamperCertificate
    Write-Host "  timestamp        : $(if ($stamped) { 'yes' } else { 'NO - expires with the certificate' })"
    if (-not $stamped) { $ok = $false }

    $motw = [bool](Get-Item -LiteralPath $file.FullName -Stream Zone.Identifier -ErrorAction SilentlyContinue)
    Write-Host "  mark of the web  : $(if ($motw) { 'PRESENT - run Unblock-File' } else { 'none' })"
    if ($motw) { $ok = $false }

    $thumb = $sig.SignerCertificate.Thumbprint
    if ($thumb) {
        $userRoot = Get-TrustState $thumb 'CurrentUser' 'Root'
        $userPub = Get-TrustState $thumb 'CurrentUser' 'TrustedPublisher'
        $machineRoot = Get-TrustState $thumb 'LocalMachine' 'Root'
        $machinePub = Get-TrustState $thumb 'LocalMachine' 'TrustedPublisher'
        Write-Host "  trusted root     : user=$(if ($userRoot) { 'yes' } else { 'no' }) machine=$(if ($machineRoot) { 'yes' } else { 'no' })"
        Write-Host "  trusted publisher: user=$(if ($userPub) { 'yes' } else { 'no' }) machine=$(if ($machinePub) { 'yes' } else { 'no' })"
        if (-not ($userRoot -or $machineRoot)) { $ok = $false }
        Write-Host "  certificate      : $($sig.SignerCertificate.Subject)"
        Write-Host "                     expires $($sig.SignerCertificate.NotAfter)"
    }

    $version = $file.VersionInfo.FileVersion
    Write-Host "  version info     : $(if ($version) { $version } else { 'MISSING' })"
    if (-not $version -and $file.Extension -eq '.exe') { $ok = $false }

    if ($file.Extension -eq '.exe') {
        $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
        $text = [System.Text.Encoding]::UTF8.GetString($bytes)
        $missing = $manifestMarkers | Where-Object { -not $text.Contains($_) }
        Write-Host "  manifest         : $(if ($missing) { 'MISSING ' + ($missing -join ',') } else { 'asInvoker + permonitorv2 + longPathAware' })"
        if ($missing) { $ok = $false }
    }

    if ($ok) {
        Write-Host '  verdict          : OK' -ForegroundColor Green
    } else {
        Write-Host '  verdict          : NEEDS ATTENTION (see the lines above)' -ForegroundColor Yellow
        ++$problems
    }
    Write-Host ''
}

if ($problems -gt 0) {
    Write-Host "$problems file(s) need attention." -ForegroundColor Yellow
    exit 1
}
Write-Host 'all binaries are signed, timestamped, trusted, unblocked and manifested.' -ForegroundColor Green
