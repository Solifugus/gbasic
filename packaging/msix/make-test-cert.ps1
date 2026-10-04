# A SELF-SIGNED code-signing certificate for LOCAL TESTING of the MSIX package.
#
# Nothing outside this machine trusts it, which is the point: winget and the
# website need a certificate that chains to a public root (Azure Trusted Signing
# or a purchased one), and the Store re-signs with Microsoft's own. This one
# only lets a developer install their own build.
#
# It is created in the CURRENT USER's personal store and exported to -OutDir as
# gbasic-test.pfx (password: -Password) and gbasic-test.cer. It is NOT trusted
# by anything until someone chooses to trust it -- installing the .cer into
# LocalMachine\TrustedPeople is a security decision this script does not make.
#
# Usage: make-test-cert.ps1 -OutDir <dir> -Subject "CN=..." -Password <pw>
param(
    [Parameter(Mandatory = $true)][string]$OutDir,
    [Parameter(Mandatory = $true)][string]$Subject,
    [Parameter(Mandatory = $true)][string]$Password
)
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$pfx = Join-Path $OutDir 'gbasic-test.pfx'
$cer = Join-Path $OutDir 'gbasic-test.cer'

# Reuse one per subject rather than minting a new certificate every build:
# a reinstall signed by a DIFFERENT certificate would need trusting again.
$cert = Get-ChildItem Cert:\CurrentUser\My |
    Where-Object { $_.Subject -eq $Subject -and $_.NotAfter -gt (Get-Date).AddDays(7) -and $_.HasPrivateKey } |
    Sort-Object NotAfter -Descending | Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $Subject `
        -KeyUsage DigitalSignature -FriendlyName 'gBASIC MSIX test signing' `
        -CertStoreLocation Cert:\CurrentUser\My `
        -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
}
$secure = ConvertTo-SecureString -String $Password -Force -AsPlainText
Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $secure | Out-Null
Export-Certificate -Cert $cert -FilePath $cer | Out-Null
Write-Output $cert.Thumbprint
