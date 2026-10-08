# Write the winget manifests for a gBASIC release (docs/windows_port_status.md §27).
#
#   tools\make-winget-manifests.ps1 -Version 0.6.0 -Msix build\msix\gbasic-0.6.0-x64.msix `
#       [-Url https://github.com/solifugus/gbasic/releases/download/v0.6.0/gbasic-0.6.0-x64.msix] `
#       [-OutDir build\winget]
#
# Writes manifests\t\Tedderland\gBASIC\<version>\ -- the path winget-pkgs uses --
# as the three files a multi-file manifest needs (version, installer,
# defaultLocale). Nothing here is typed by hand that can be read from the
# package instead:
#   - InstallerSha256 and SignatureSha256 come from `winget hash --msix`, which
#     is what winget's own validation recomputes;
#   - PackageFamilyName is COMPUTED from the Publisher the package was signed
#     with, and the script refuses to continue if that does not reproduce a
#     known-good value -- a wrong family name is a package winget installs and
#     then cannot find to upgrade or uninstall.
# Validate the result with:  winget validate --manifest <dir>
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$Msix,
    [string]$Url = "",
    [string]$OutDir = "build\winget"
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..").Path
Set-Location $root

if (-not $Url) {
    $leaf = Split-Path $Msix -Leaf
    $Url = "https://github.com/solifugus/gbasic/releases/download/v$Version/$leaf"
}
if (-not (Test-Path $Msix)) { throw "no package at $Msix" }

# --- the publisher, read from the same file the build signs with -----------------
$env_file = Get-Content packaging\msix\release-signing.env
$publisher = ($env_file | Where-Object { $_ -match '^MSIX_PUBLISHER=' }) -replace '^MSIX_PUBLISHER="(.*)"$', '$1'
$identityName = 'gBASIC.gbasic'

# Windows' publisher id: SHA-256 of the publisher as UTF-16LE, first 8 bytes,
# as 13 characters of Crockford-style base32 (0-9 a-z without i l o u).
function Get-PublisherId([string]$p) {
    $hash = [Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::Unicode.GetBytes($p))
    $bits = ($hash[0..7] | ForEach-Object { [Convert]::ToString($_, 2).PadLeft(8, '0') }) -join ''
    $bits += '0'                                   # 64 bits -> 65, a multiple of 5
    $alphabet = '0123456789abcdefghjkmnpqrstvwxyz'
    $out = ''
    for ($i = 0; $i -lt 65; $i += 5) { $out += $alphabet[[Convert]::ToInt32($bits.Substring($i, 5), 2)] }
    return $out
}
$pfn = "${identityName}_$(Get-PublisherId $publisher)"
# The check that makes the computation trustworthy: the signed package installed
# on the build machine on 2026-10-05 reported exactly this.
if ($publisher -eq 'CN=Matthew Tedder, O=Matthew Tedder, L=Fayetteville, S=ny, C=US' -and
    $pfn -ne 'gBASIC.gbasic_k6g5nvxez8q90') {
    throw "computed PackageFamilyName $pfn does not match the installed package's gBASIC.gbasic_k6g5nvxez8q90"
}

# --- the hashes winget validation recomputes --------------------------------------
$hashOut = winget hash --msix $Msix 2>&1 | Out-String
$installerSha = [regex]::Match($hashOut, 'InstallerSha256:\s*([0-9a-fA-F]{64})').Groups[1].Value.ToUpper()
$signatureSha = [regex]::Match($hashOut, 'SignatureSha256:\s*([0-9a-fA-F]{64})').Groups[1].Value.ToUpper()
if (-not $installerSha -or -not $signatureSha) { throw "winget hash did not report both hashes:`n$hashOut" }

$id = 'Tedderland.gBASIC'
$dir = Join-Path $OutDir "manifests\t\Tedderland\gBASIC\$Version"
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$schema = '1.10.0'
$enc = New-Object Text.UTF8Encoding $false            # winget-pkgs: UTF-8, no BOM

[IO.File]::WriteAllText((Join-Path $dir "$id.yaml"), @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.version.$schema.schema.json
PackageIdentifier: $id
PackageVersion: $Version
DefaultLocale: en-US
ManifestType: version
ManifestVersion: $schema
"@ + "`n", $enc)

[IO.File]::WriteAllText((Join-Path $dir "$id.installer.yaml"), @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.installer.$schema.schema.json
PackageIdentifier: $id
PackageVersion: $Version
Platform:
- Windows.Desktop
MinimumOSVersion: 10.0.18362.0
InstallerType: msix
PackageFamilyName: $pfn
Commands:
- gbasic
- gbasic-lsp
Installers:
- Architecture: x64
  InstallerUrl: $Url
  InstallerSha256: $installerSha
  SignatureSha256: $signatureSha
ManifestType: installer
ManifestVersion: $schema
"@ + "`n", $enc)

[IO.File]::WriteAllText((Join-Path $dir "$id.locale.en-US.yaml"), @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.defaultLocale.$schema.schema.json
PackageIdentifier: $id
PackageVersion: $Version
PackageLocale: en-US
Publisher: Matthew Tedder
PublisherUrl: https://tedderland.com
PublisherSupportUrl: https://github.com/solifugus/gbasic/issues
Author: Matthew C. Tedder
PackageName: gBASIC
PackageUrl: https://github.com/solifugus/gbasic
License: Apache-2.0
LicenseUrl: https://github.com/solifugus/gbasic/blob/master/LICENSE
Copyright: Copyright 2026 Matthew C. Tedder
ShortDescription: A modern BASIC-family language -- the gbasic interpreter and its standard library.
Description: |-
  gBASIC is a modern BASIC-family language with a batteries-included standard
  library: money and dates as real types, records and arrays, actors, a web
  server, SQLite and ODBC, xlsx and XML, and more. This package installs the
  gbasic interpreter, its standard library, and gbasic-lsp, the language server
  editors use for live errors.
Moniker: gbasic
Tags:
- basic
- interpreter
- programming-language
ReleaseNotesUrl: https://github.com/solifugus/gbasic/releases/tag/v$Version
ManifestType: defaultLocale
ManifestVersion: $schema
"@ + "`n", $enc)

Write-Output "wrote $dir"
Write-Output "  PackageFamilyName $pfn"
Write-Output "  InstallerUrl      $Url"
