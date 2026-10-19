[CmdletBinding()]
param(
    [string]$DistDir = 'dist',
    [string]$BuildDir = '',
    [switch]$CreateCert,
    [switch]$TrustCert,
    [switch]$Machine,
    [switch]$All,
    [switch]$SkipUnblock,
    [switch]$TrustOnly,
    [string]$CertThumbprint = '',
    [string]$Subject = 'CN=Nebulous Liner Hull Customizer (self-signed)',
    [string]$TimestampServer = 'http://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# signtool handles CNG keys much better than Set-AuthenticodeSignature, so prefer it.
$signtool = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match 'x64' } |
    Sort-Object FullName -Descending | Select-Object -First 1
if (-not $signtool) {
    $cmd = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($cmd) { $signtool = Get-Item $cmd.Source }
}
if (-not $signtool) {
    throw 'signtool.exe not found; install the Windows SDK (Windows App Certification Kit)'
}
$signtoolPath = $signtool.FullName

$cert = $null
if ($CertThumbprint) {
    $cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Thumbprint -eq $CertThumbprint }
    if (-not $cert) { throw "no certificate with thumbprint $CertThumbprint in Cert:\CurrentUser\My" }
} elseif ($CreateCert) {
    $cert = Get-ChildItem Cert:\CurrentUser\My |
        Where-Object { $_.Subject -eq $Subject -and $_.HasPrivateKey } |
        Sort-Object NotAfter -Descending | Select-Object -First 1
    if (-not $cert) {
        Write-Host "creating self-signed code-signing certificate: $Subject" -ForegroundColor Cyan
        $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $Subject -KeyUsage DigitalSignature -KeyAlgorithm RSA -KeyLength 3072 -CertStoreLocation Cert:\CurrentUser\My -NotAfter (Get-Date).AddYears(3)
    }
} else {
    $cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert |
        Sort-Object NotAfter -Descending | Select-Object -First 1
    if (-not $cert) {
        throw 'no code-signing certificate found; re-run with -CreateCert (or pass -CertThumbprint)'
    }
}
Write-Host "certificate: $($cert.Subject) [$($cert.Thumbprint)] valid to $($cert.NotAfter)" -ForegroundColor Cyan

$publicCert = Join-Path $root 'scripts\signing-cert.cer'
Export-Certificate -Cert $cert -FilePath $publicCert -Force | Out-Null
Write-Host "public certificate exported: $publicCert"

# Root makes the chain valid; TrustedPublisher is the store that stops Windows
# calling the publisher 'unknown'. Both are per-user by default and need no
# elevation. -Machine mirrors them into the machine stores for every account on
# this PC, which does need an elevated shell.
function Add-ToStore([string]$Path, [string]$Thumbprint) {
    if (Test-Path (Join-Path $Path $Thumbprint)) {
        Write-Host "  already trusted: $Path"
        return
    }
    Import-Certificate -FilePath $publicCert -CertStoreLocation $Path | Out-Null
    Write-Host "  added to $Path" -ForegroundColor Green
}

function Test-Elevated {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal $identity
    $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if ($TrustCert) {
    Write-Host 'trusting the certificate for the current user' -ForegroundColor Cyan
    Add-ToStore 'Cert:\CurrentUser\Root' $cert.Thumbprint
    Add-ToStore 'Cert:\CurrentUser\TrustedPublisher' $cert.Thumbprint
}

if ($Machine) {
    if (-not (Test-Elevated)) {
        Write-Warning '-Machine needs an elevated shell; run PowerShell as administrator. Skipping machine-wide trust.'
    } else {
        Write-Host 'trusting the certificate for every account on this PC' -ForegroundColor Cyan
        Add-ToStore 'Cert:\LocalMachine\Root' $cert.Thumbprint
        Add-ToStore 'Cert:\LocalMachine\TrustedPublisher' $cert.Thumbprint
    }
}

if ($TrustOnly) {
    Write-Host 'trust stores updated; nothing was signed (-TrustOnly)' -ForegroundColor Cyan
    return
}

$files = @()
$distTarget = if ($DistDir) { Join-Path $root $DistDir } else { '' }
if ($DistDir -and (Test-Path $distTarget)) {
    if ($All) {
        $files += Get-ChildItem $distTarget -Recurse -Include *.exe, *.dll
    } else {
        $files += @('NLHC.exe', 'NLHCCore.dll') |
            ForEach-Object { Join-Path $distTarget "bin\$_" } |
            Where-Object { Test-Path $_ } |
            ForEach-Object { Get-Item $_ }
    }
} elseif ($DistDir) {
    Write-Warning "release folder not found: $DistDir (run scripts/build.ps1 -Deploy first)"
}

# The development build is the binary people double-click while iterating, so
# sign it too instead of only the packaged copy.
if ($BuildDir) {
    $buildTarget = Join-Path $root $BuildDir
    if (-not (Test-Path $buildTarget)) { throw "build folder not found: $buildTarget" }
    $files += @('NLHC.exe', 'NLHCCore.dll') |
        ForEach-Object { Join-Path $buildTarget "bin\$_" } |
        Where-Object { Test-Path $_ } |
        ForEach-Object { Get-Item $_ }
}

if (-not $files) { throw 'nothing to sign' }

# Windows only runs its reputation check on files carrying the mark of the
# web. A freshly built file never has one, but a release that travelled as a
# download does, so clear it before signing.
if (-not $SkipUnblock -and $distTarget -and (Test-Path $distTarget)) {
    Get-ChildItem $distTarget -Recurse -File | Unblock-File -ErrorAction SilentlyContinue
}

# signtool needs a PFX (temporary, random password, deleted afterwards).
$password = [System.Guid]::NewGuid().ToString('N')
$pfx = Join-Path ([System.IO.Path]::GetTempPath()) ("nlhc-sign-" + $password + '.pfx')
Export-PfxCertificate -Cert $cert -FilePath $pfx -Password (ConvertTo-SecureString -String $password -Force -AsPlainText) | Out-Null

$signed = 0
$failed = 0
try {
    foreach ($file in $files) {
        $arguments = @('sign', '/f', $pfx, '/p', $password, '/fd', 'SHA256')
        if ($TimestampServer) { $arguments += @('/tr', $TimestampServer, '/td', 'SHA256') }
        $arguments += $file.FullName
        $output = & $signtoolPath @arguments 2>&1
        if ($LASTEXITCODE -ne 0 -and $TimestampServer) {
            # A blocked timestamp server must not stop a local release.
            Write-Warning "$($file.Name): timestamping failed, signing without a timestamp"
            $arguments = @('sign', '/f', $pfx, '/p', $password, '/fd', 'SHA256', $file.FullName)
            $output = & $signtoolPath @arguments 2>&1
        }
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "$($file.Name): signtool failed: $($output -join ' ')"
            ++$failed
            continue
        }
        $status = (Get-AuthenticodeSignature $file.FullName).Status
        if ($status -ne 'Valid') {
            Write-Warning "$($file.Name): signature does not validate ($status)"
            ++$failed
            continue
        }
        Write-Host "signed $($file.Name) [$($file.Directory.Name)]" -ForegroundColor Green
        ++$signed
    }
} finally {
    Remove-Item $pfx -Force -ErrorAction SilentlyContinue
}

Write-Host "done: $signed signed, $failed failed" -ForegroundColor Cyan
if ($failed -gt 0) { exit 1 }
