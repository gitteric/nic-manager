# Creates the self-signed code signing certificate for NIC Manager.
#
#   signing\nicmanager.pfx   private key + certificate (NOT committed, see .gitignore)
#   res\nicmanager.cer       public certificate, embedded into the exe so the
#                            tray menu can install it ("Install signing certificate")
#
# Run once from the project folder:  powershell -ExecutionPolicy Bypass -File tools\make-cert.ps1
# The build signs automatically when signing\nicmanager.pfx exists and the
# password is in the environment variable NICMGR_SIGN_PASSWORD.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$pfx = Join-Path $root 'signing\nicmanager.pfx'
$cer = Join-Path $root 'res\nicmanager.cer'
if (Test-Path $pfx) { throw "$pfx already exists. Delete it first if you really want a new certificate." }

$password = Read-Host -AsSecureString 'Password for the .pfx'
New-Item -ItemType Directory -Force (Split-Path $pfx) | Out-Null

# Code signing only (EKU 1.3.6.1.5.5.7.3.3) and not a CA, so the certificate
# cannot issue other certificates even once it is trusted as a root.
$cert = New-SelfSignedCertificate -Type CodeSigningCert `
    -Subject 'CN=NIC Manager' `
    -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 `
    -KeyUsage DigitalSignature `
    -TextExtension @('2.5.29.19={text}ca=false') `
    -NotAfter (Get-Date).AddYears(20) `
    -CertStoreLocation 'Cert:\CurrentUser\My'

Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $password | Out-Null
Export-Certificate -Cert $cert -FilePath $cer -Type CERT | Out-Null
# The key now lives only in the .pfx.
Remove-Item "Cert:\CurrentUser\My\$($cert.Thumbprint)"

Write-Host "Created $pfx and $cer (thumbprint $($cert.Thumbprint))."
Write-Host 'For GitHub releases, add these repository secrets:'
Write-Host '  SIGNING_PFX_BASE64    = output of: [Convert]::ToBase64String([IO.File]::ReadAllBytes("signing\nicmanager.pfx"))'
Write-Host '  SIGNING_PFX_PASSWORD  = the password above'
