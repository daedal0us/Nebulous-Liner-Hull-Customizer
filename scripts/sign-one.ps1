[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [string]$Thumbprint = '',
    [string]$TimestampServer = 'http://timestamp.digicert.com'
)

# Post-build hook: keeps NLHC.exe signed after every relink, because a relink
# throws the previous signature away and Windows then reports an unknown
# publisher. Failures are warnings, never build errors - a checkout without the
# certificate must still compile.
$ErrorActionPreference = 'Stop'

if (-not $Thumbprint) {
    $cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert |
        Sort-Object NotAfter -Descending | Select-Object -First 1
    if (-not $cert) {
        Write-Host "signing skipped: no code-signing certificate (run scripts/sign.ps1 -CreateCert -TrustCert)" -ForegroundColor Yellow
        return
    }
    $Thumbprint = $cert.Thumbprint
}

$signtool = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match 'x64' } |
    Sort-Object FullName -Descending | Select-Object -First 1
if (-not $signtool) {
    Write-Host 'signing skipped: signtool.exe not found (install the Windows SDK)' -ForegroundColor Yellow
    return
}

$arguments = @('sign', '/sha1', $Thumbprint, '/fd', 'SHA256', '/q')
if ($TimestampServer) { $arguments += @('/tr', $TimestampServer, '/td', 'SHA256') }
$arguments += $Path
$output = & $signtool.FullName @arguments 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "signing skipped for $(Split-Path -Leaf $Path): $($output -join ' ')" -ForegroundColor Yellow
    return
}
$status = (Get-AuthenticodeSignature $Path).Status
Write-Host "signed $(Split-Path -Leaf $Path) [$status]" -ForegroundColor Green
