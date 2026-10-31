# One-time setup: create a self-signed code-signing certificate, trust it
# machine-wide (asks UAC once), then sign the built client exe.
# Without this, SmartScreen warns "Windows protected your PC" on every
# launch because a locally-built exe has no publisher and no cloud
# reputation. Re-run any time; build-client.ps1 re-signs automatically.

$ErrorActionPreference = "Stop"
$root    = Split-Path -Parent $PSScriptRoot   # mirror-launcher\
$exe     = Join-Path $root "client\airplay-windows.exe"
$cer     = Join-Path $PSScriptRoot "mirror-client-code.cer"
$subject = "CN=AirPlay Mirror Client"

# 1) Certificate in CurrentUser\My (no admin needed to create)
$cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert -ErrorAction SilentlyContinue |
        Where-Object { $_.Subject -eq $subject } |
        Sort-Object NotAfter -Descending | Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject `
        -KeyUsage DigitalSignature -FriendlyName "AirPlay Mirror Client Code Signing" `
        -CertStoreLocation Cert:\CurrentUser\My -NotAfter (Get-Date).AddYears(5)
    Write-Host "certificate created: $($cert.Thumbprint)"
} else {
    Write-Host "certificate exists:  $($cert.Thumbprint)"
}

# 2) Export the public certificate for the machine-wide trust import
Export-Certificate -Cert $cert -FilePath $cer | Out-Null
Write-Host "exported: $cer"

# 3) Trust machine-wide (Root + Trusted Publishers). Requires admin.
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
           ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if ($isAdmin) {
    Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
    Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null
    Write-Host "trusted (already elevated)."
} else {
    $helper = Join-Path $PSScriptRoot "_trust_elevated.ps1"
    Set-Content -Path $helper -Encoding UTF8 -Value (
        "Import-Certificate -FilePath '$cer' -CertStoreLocation Cert:\LocalMachine\Root | Out-Null; " +
        "Import-Certificate -FilePath '$cer' -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null; " +
        "Write-Host 'cert imported to Root + TrustedPublisher'")
    Start-Process powershell -Verb RunAs -ArgumentList "-NoProfile","-ExecutionPolicy","Bypass","-File",$helper -Wait
    Remove-Item $helper -ErrorAction SilentlyContinue
    Write-Host "elevated import finished."
}

# 4) Sign the exe and verify the chain
if (Test-Path $exe) {
    Set-AuthenticodeSignature -FilePath $exe -Certificate $cert -HashAlgorithm SHA256 | Out-Null
    $chk = Get-AuthenticodeSignature $exe
    Write-Host ("signature: " + $chk.Status + "  signer: " + $chk.SignerCertificate.Subject)
} else {
    Write-Host "exe not found: $exe (run build-client.ps1 first)"
}
