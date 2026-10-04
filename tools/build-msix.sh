#!/usr/bin/env bash
# Build gBASIC's MSIX package (docs/windows_port_status.md §23). Run from an
# "MSYS2 UCRT64" shell at the repository root, with the Windows SDK installed
# (makeappx.exe, signtool.exe).
#
#   tools/build-msix.sh                 # test build, self-signed (see below)
#   MSIX_PUBLISHER="CN=..." MSIX_PFX=... MSIX_PFX_PASSWORD=... tools/build-msix.sh
#
# THE LAYOUT IS `make install`'s: bin\gbasic.exe beside share\gbasic\stdlib,
# which gb_exe_relative_stdlib finds with no GBASIC_PATH. Plus LICENSE, NOTICE,
# LICENSING.md and every third_party licence, since the package IS a
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
for d in third_party/*/; do
    name="$(basename "$d")"
    for f in "$d"LICENSE* "$d"COPYING* "$d"README*; do
        [ -e "$f" ] && { mkdir -p "$stage/licenses/$name"; cp "$f" "$stage/licenses/$name/"; }
    done
done

stage_win="$(cygpath -w "$stage")"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File packaging/msix/make-assets.ps1 \
    -OutDir "$stage_win\\Assets"

publisher="${MSIX_PUBLISHER:-CN=gBASIC Test Build}"
if [ -z "${MSIX_PFX:-}" ]; then
    MSIX_PFX_PASSWORD="gbasic-test"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File packaging/msix/make-test-cert.ps1 \
        -OutDir "$(cygpath -w "$out")" -Subject "$publisher" -Password "$MSIX_PFX_PASSWORD" >/dev/null
    MSIX_PFX="$out/gbasic-test.pfx"
fi

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
if ! "$makeappx" pack /o /d "$stage_win" /p "$(cygpath -w "$pkg")" > "$out/makeappx.log" 2>&1; then
    cat "$out/makeappx.log" >&2
    exit 1
fi
"$signtool" sign /q /fd SHA256 /f "$(cygpath -w "$MSIX_PFX")" /p "$MSIX_PFX_PASSWORD" \
    "$(cygpath -w "$pkg")"
unset MSYS2_ARG_CONV_EXCL
printf 'built %s (%s bytes), publisher %s\n' "$pkg" "$(stat -c %s "$pkg")" "$publisher"
