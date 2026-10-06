#!/usr/bin/env bash
# Build gBASIC's MSIX package (docs/windows_port_status.md §23). Run from an
# "MSYS2 UCRT64" shell at the repository root, with the Windows SDK installed
# (makeappx.exe, signtool.exe).
#
#   tools/build-msix.sh                 # test build, self-signed (see below)
#   MSIX_SIGN=azure tools/build-msix.sh # RELEASE build, Azure Artifact Signing
#   MSIX_PUBLISHER="CN=..." MSIX_PFX=... MSIX_PFX_PASSWORD=... tools/build-msix.sh
#
# RELEASE SIGNING (MSIX_SIGN=azure) uses the account in
# packaging/msix/release-signing.env through Microsoft's signtool plug-in
# (tools/fetch-signing-client.sh), authenticated by `az login` and nothing
# else. Both gbasic.exe and the package are signed and timestamped, and the
# build then READS THE SUBJECT BACK from the signed package and fails unless it
# is the manifest's Publisher and the signature is Valid -- a mismatch is a
# package Windows refuses to install, found here instead of by a user.
#
# THE LAYOUT IS `make install`'s: bin\gbasic.exe beside share\gbasic\stdlib,
# which gb_exe_relative_stdlib finds with no GBASIC_PATH. Plus LICENSE, NOTICE,
# LICENSING.md and THIRD-PARTY-NOTICES.txt (tools/make-notices.sh), since the package IS a
# distribution of them.
#
# SIGNING. With no MSIX_PFX the package is signed with a SELF-SIGNED test
# certificate (packaging/msix/make-test-cert.ps1) in the current user's store;
# it installs only where someone has chosen to trust that certificate. The
# PUBLISHER must equal the signing certificate's subject or Windows refuses the
# package -- so a Store build uses the publisher Partner Center assigns, and a
# winget/website build the subject of a publicly trusted certificate.
#
# Output: build/msix/gbasic-<version>-x64.msix (and the test .cer beside it).
set -euo pipefail
cd "$(dirname "$0")/.."

sdk_bin="$(ls -d "/c/Program Files (x86)/Windows Kits/10/bin/"10.*/x64 2>/dev/null | sort -V | tail -1)"
if [ -z "$sdk_bin" ] || [ ! -x "$sdk_bin/makeappx.exe" ]; then
    echo "build-msix: no Windows SDK (makeappx.exe) found" >&2
    exit 1
fi
makeappx="$sdk_bin/makeappx.exe"
signtool="$sdk_bin/signtool.exe"

make >/dev/null
version="$(./gbasic.exe --version | awk '{print $2}')"
case "$version" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *) echo "build-msix: cannot read a version from ./gbasic.exe --version" >&2; exit 1 ;;
esac
msix_version="$version.0"            # MSIX wants four parts

out=build/msix
stage="$out/stage"
rm -rf "$stage"
mkdir -p "$stage/bin" "$stage/share/gbasic" "$stage/licenses"

cp gbasic.exe "$stage/bin/gbasic.exe"
strip "$stage/bin/gbasic.exe"
cp -r stdlib "$stage/share/gbasic/stdlib"
cp LICENSE NOTICE LICENSING.md "$stage/licenses/"
# Every statically linked library's licence, generated from what the linker
# actually used; it fails the build if one has no entry (tools/make-notices.sh).
bash tools/make-notices.sh "$stage/licenses/THIRD-PARTY-NOTICES.txt" >/dev/null

stage_win="$(cygpath -w "$stage")"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File packaging/msix/make-assets.ps1 \
    -OutDir "$stage_win\\Assets"

sign_mode="${MSIX_SIGN:-test}"
case "$sign_mode" in
    azure)
        . packaging/msix/release-signing.env
        publisher="$MSIX_PUBLISHER"
        dlib="${ARTIFACT_SIGNING_DLIB:-build/signing/client/bin/x64/Azure.CodeSigning.Dlib.dll}"
        if [ ! -f "$dlib" ]; then
            echo "build-msix: no signing client at $dlib -- run tools/fetch-signing-client.sh" >&2
            exit 1
        fi
        # Azure CLI credentials ONLY: of the client's ten ways to find a
        # credential, the rest either do not apply here or would sign as
        # whatever identity they happened to find first.
        meta="$out/artifact-signing.json"
        cat > "$meta" <<JSON
{
  "Endpoint": "$ARTIFACT_SIGNING_ENDPOINT",
  "CodeSigningAccountName": "$ARTIFACT_SIGNING_ACCOUNT",
  "CertificateProfileName": "$ARTIFACT_SIGNING_PROFILE",
  "ExcludeCredentials": [
    "EnvironmentCredential",
    "ManagedIdentityCredential",
    "WorkloadIdentityCredential",
    "SharedTokenCacheCredential",
    "VisualStudioCredential",
    "VisualStudioCodeCredential",
    "AzurePowerShellCredential",
    "AzureDeveloperCliCredential",
    "InteractiveBrowserCredential"
  ]
}
JSON
        ;;
    test)
        publisher="${MSIX_PUBLISHER:-CN=gBASIC Test Build}"
        if [ -z "${MSIX_PFX:-}" ]; then
            MSIX_PFX_PASSWORD="gbasic-test"
            powershell.exe -NoProfile -ExecutionPolicy Bypass -File packaging/msix/make-test-cert.ps1 \
                -OutDir "$(cygpath -w "$out")" -Subject "$publisher" -Password "$MSIX_PFX_PASSWORD" >/dev/null
            MSIX_PFX="$out/gbasic-test.pfx"
        fi
        ;;
    *)
        echo "build-msix: MSIX_SIGN must be test or azure, not '$sign_mode'" >&2
        exit 1
        ;;
esac

# One signing call for both modes. Azure signing is timestamped by Microsoft's
# own authority, so a signature outlives the short-lived certificate behind it.
sign_files() {
    if [ "$sign_mode" = azure ]; then
        "$signtool" sign /q /fd SHA256 /tr "http://timestamp.acs.microsoft.com" /td SHA256 \
            /dlib "$(cygpath -w "$dlib")" /dmdf "$(cygpath -w "$meta")" "$@"
    else
        "$signtool" sign /q /fd SHA256 /f "$(cygpath -w "$MSIX_PFX")" /p "$MSIX_PFX_PASSWORD" "$@"
    fi
}

sed -e "s|@IDENTITY_NAME@|${MSIX_IDENTITY_NAME:-gBASIC.gbasic}|" \
    -e "s|@PUBLISHER@|$publisher|" \
    -e "s|@PUBLISHER_DISPLAY@|${MSIX_PUBLISHER_DISPLAY:-gBASIC}|" \
    -e "s|@VERSION@|$msix_version|" \
    packaging/msix/AppxManifest.xml.in > "$stage/AppxManifest.xml"

pkg="$out/gbasic-$version-x64.msix"
rm -f "$pkg"
# MSYS2 rewrites any argument that looks like a POSIX path -- including the
# SDK tools' own switches, /o /d /p -- before a native program sees it.
export MSYS2_ARG_CONV_EXCL='*'
if [ "$sign_mode" = azure ]; then
    # The exe too, for anywhere it travels without the package around it.
    sign_files "$(cygpath -w "$stage/bin/gbasic.exe")"
fi
if ! "$makeappx" pack /o /d "$stage_win" /p "$(cygpath -w "$pkg")" > "$out/makeappx.log" 2>&1; then
    cat "$out/makeappx.log" >&2
    exit 1
fi
if ! sign_files "$(cygpath -w "$pkg")"; then
    # Measured: signtool REFUSES a package whose manifest Publisher is not the
    # certificate's subject, and says only "SignerSign() failed ... 0x8007000b"
    # / "An unexpected internal error". Name the likely cause.
    printf 'build-msix: signing the package failed. If signtool said 0x8007000b, the\n' >&2
    printf 'manifest Publisher is not the certificate subject. Publisher was:\n  %s\n' "$publisher" >&2
    exit 1
fi
unset MSYS2_ARG_CONV_EXCL

# What Windows will compare against the manifest is the certificate the
# signature actually carries, so ask the signed file rather than trusting the
# preview the portal showed.
got="$(powershell.exe -NoProfile -Command \
    "(Get-AuthenticodeSignature '$(cygpath -w "$pkg")').SignerCertificate.Subject" | tr -d '\r')"
if [ "$got" != "$publisher" ]; then
    printf 'build-msix: the package is signed by\n  %s\nbut the manifest says\n  %s\n' "$got" "$publisher" >&2
    exit 1
fi
if [ "$sign_mode" = azure ]; then
    status="$(powershell.exe -NoProfile -Command \
        "(Get-AuthenticodeSignature '$(cygpath -w "$pkg")').Status" | tr -d '\r')"
    if [ "$status" != "Valid" ]; then
        echo "build-msix: the release signature is not Valid ($status)" >&2
        exit 1
    fi
fi
printf 'built %s (%s bytes), signed (%s) by %s\n' "$pkg" "$(stat -c %s "$pkg")" "$sign_mode" "$publisher"
