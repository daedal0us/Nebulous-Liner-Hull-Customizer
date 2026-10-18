[CmdletBinding()]
param(
    [string]$DistDir = 'dist',
    [switch]$CreateCert,
    [switch]$TrustCert,
    [switch]$All,
    [string]$CertThumbprint = '',
    [string]$Subject = 'CN=Nebulous Liner Hull Customizer (self-signed)',
    [string]$TimestampServer = 'http://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$target = Join-Path $root $DistDir
if (-not (Test-Path $target)) {
    throw "release folder not found: $target (run scripts/build.ps1 -Deploy first)"
}

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
} elseif ($CreateCert) {
    $cert = Get-ChildItem Cert:\CurrentUser\My |
        Where-Object { $_.Subject -eq $Subject -and $_.HasPrivateKey } |
        Sort-Object NotAfter -Descending | Select-Object -First 1
    if (-not $cert) {
        Write-Host "creating self-signed code-signing certificate: $Subject" -ForegroundColor Cyan
        $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $Subject `
            -KeyUsage DigitalSignature -KeyAlgorithm RSA -KeyLength 3072 `
            -CertStoreLocation Cert:\CurrentUser\My -NotAfter (Get-Date).AddYears(3)
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
if ($TrustCert) {
    Write-Host 'adding the certificate to the current user Trusted Root store' -ForegroundColor Cyan
    Import-Certificate -FilePath $publicCert -CertStoreLocation Cert:\CurrentUser\Root | Out-Null
}

if ($All) {
    $files = Get-ChildItem $target -Recurse -Include *.exe, *.dll
} else {
    $files = @('NLHC.exe', 'NLHCCore.dll') |
        ForEach-Object { Join-Path $target "bin\$_" } |
        Where-Object { Test-Path $_ } |
        ForEach-Object { Get-Item $_ }
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
            $arguments = @('sign', '/f', $pfx, '/p', $password, '/fd', 'SHA256', $file.FullName)
            $output = & $signtoolPath @arguments 2>&1
        }
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "$($file.Name): signtool failed: $($output -join ' ')"
            ++$failed
        } else {
            Write-Host "signed $($file.Name)" -ForegroundColor Green
            ++$signed
        }
    }
} finally {
    Remove-Item $pfx -Force -ErrorAction SilentlyContinue
}

Write-Host "done: $signed signed, $failed failed" -ForegroundColor Cyan
if ($failed -gt 0) { exit 1 }
