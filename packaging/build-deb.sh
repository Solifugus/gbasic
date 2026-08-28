#!/usr/bin/env bash
set -euo pipefail

# Build a .deb for a gBASIC application.
#
# THE MODEL IS A VENDORED RUNTIME. The package carries its own interpreter and
# its own copy of the stdlib under /usr/lib/<app>/, and does NOT depend on a
# system gbasic package. That costs about 2 MB and buys three things a shipped
# product actually needs:
#
#   * The dependency set is the APPLICATION's. A lean build for a server drops
#     GTK, GObject-introspection, libpq, libxml2 and libcurl -- measured, 48
#     shared libraries down to 10 -- so installing a file-transfer service does
#     not pull a desktop toolkit onto a server, which is an objection a
#     customer's security review will actually raise.
#   * The application cannot change under the operator. A system-wide
#     interpreter upgrade cannot alter this service's behaviour, which matters
#     when the thing you shipped is audited.
#   * The stdlib path is COMPILED IN (GBASIC_DEFAULT_STDLIB, set from
#     STDLIBDIR), so nothing depends on GBASIC_PATH being right in a unit file.
#
# What is deliberately NOT vendored: libssl, libcrypto, libsqlite3, zlib. Those
# are the system's, patched by the system. For a security product that is the
# whole point -- an auditor can see you are not shipping a frozen OpenSSL.
#
# Usage:
#   packaging/build-deb.sh packaging/example-app
#
# The application directory must contain a `package.conf` (see the example).

here="$(cd "$(dirname "$0")/.." && pwd)"
appdir="${1:-}"
if [ -z "$appdir" ] || [ ! -f "$appdir/package.conf" ]; then
    printf 'usage: %s <app-dir>   (must contain package.conf)\n' "$0" >&2
    exit 2
fi
appdir="$(cd "$appdir" && pwd)"

# package.conf is `key=value`, sourced. Keep it dumb: it is read by this script
# and by a human, and both should agree on what it says.
# shellcheck source=/dev/null
. "$appdir/package.conf"

: "${NAME:?package.conf must set NAME}"
: "${VERSION:?package.conf must set VERSION}"
: "${SUMMARY:?package.conf must set SUMMARY}"
: "${ENTRY:?package.conf must set ENTRY (the .bas file run by the service)}"
DEPENDS="${DEPENDS:-}"
BUILD_FLAGS="${BUILD_FLAGS:-}"
ARCH="$(dpkg --print-architecture)"

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
# mktemp -d gives 0700; the package ROOT becomes that mode and dpkg would then
# install a directory nobody but root can traverse.
chmod 0755 "$stage"

# Where the app's own interpreter and stdlib live once installed. Overridable
# so the package can be built for /opt, and so the smoke test can build one
# rooted somewhere writable and actually RUN it -- a packaging script that
# cannot be tested without root is a packaging script nobody tests.
RUNTIME="${RUNTIME:-/usr/lib/$NAME}"
printf '==> building %s %s (%s)\n' "$NAME" "$VERSION" "$ARCH"

# 1. The interpreter, built lean, with its stdlib path compiled in.
printf '==> lean interpreter: make %s\n' "$BUILD_FLAGS"
make -C "$here" clean >/dev/null
# shellcheck disable=SC2086
make -C "$here" $BUILD_FLAGS \
     STDLIBDIR="$RUNTIME/stdlib" BINDIR="$RUNTIME" DOCDIR="/usr/share/doc/$NAME" \
     >/dev/null
# shellcheck disable=SC2086
make -C "$here" install DESTDIR="$stage" $BUILD_FLAGS \
     STDLIBDIR="$RUNTIME/stdlib" BINDIR="$RUNTIME" DOCDIR="/usr/share/doc/$NAME" \
     >/dev/null

libs=$(ldd "$stage$RUNTIME/gbasic" | wc -l)
printf '    interpreter links %s shared libraries\n' "$libs"

# 2. The application itself, in its OWN directory. This is not tidiness: a bare
#    `load NAME` searches the source file's directory tree RECURSIVELY and
#    FIRST, ahead of the compiled-in stdlib, so anything named <name>.bas that
#    lands beneath the app would silently replace a shipped library. Keeping
#    the app directory root-owned and containing only the app is half the
#    defence; loading by absolute path (as the example does) is the other half.
#    @RUNTIME@ in the sources is substituted here, so an application can load
#    by absolute path (the safe form) without being nailed to one prefix.
install -d "$stage$RUNTIME/app"
for f in "$appdir"/app/*.bas; do
    sed -e "s|@RUNTIME@|$RUNTIME|g" "$f" > "$stage$RUNTIME/app/$(basename "$f")"
    chmod 0644 "$stage$RUNTIME/app/$(basename "$f")"
done
if grep -RIlq '@RUNTIME@' "$stage$RUNTIME/app/"; then
    printf 'error: @RUNTIME@ survived substitution\n' >&2; exit 1
fi

# 3. Config, state, and the unit.
install -d "$stage/etc/$NAME" "$stage/var/lib/$NAME" "$stage/lib/systemd/system"
[ -f "$appdir/conf/$NAME.conf" ] && install -m 0640 "$appdir/conf/$NAME.conf" "$stage/etc/$NAME/"
sed -e "s|@NAME@|$NAME|g" -e "s|@RUNTIME@|$RUNTIME|g" -e "s|@ENTRY@|$ENTRY|g" \
    "$here/packaging/service.template" > "$stage/lib/systemd/system/$NAME.service"
chmod 0644 "$stage/lib/systemd/system/$NAME.service"

# 4. Control metadata. Installed-Size is what dpkg reports to the operator.
installed_kb=$(du -sk "$stage" | cut -f1)
install -d "$stage/DEBIAN"
cat > "$stage/DEBIAN/control" <<EOF
Package: $NAME
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Depends: $DEPENDS
Installed-Size: $installed_kb
Maintainer: ${MAINTAINER:-unspecified <root@localhost>}
Description: $SUMMARY
 Built with gBASIC. The package carries its own interpreter and standard
 library under $RUNTIME; system OpenSSL, SQLite and zlib are used, not
 vendored.
EOF

# Config files are marked conffiles so dpkg does not overwrite operator edits
# on upgrade -- the single most common way a package upgrade breaks a service.
if [ -f "$stage/etc/$NAME/$NAME.conf" ]; then
    printf '/etc/%s/%s.conf\n' "$NAME" "$NAME" > "$stage/DEBIAN/conffiles"
fi

sed -e "s|@NAME@|$NAME|g" "$here/packaging/postinst.template" > "$stage/DEBIAN/postinst"
sed -e "s|@NAME@|$NAME|g" "$here/packaging/prerm.template"   > "$stage/DEBIAN/prerm"
chmod 0755 "$stage/DEBIAN/postinst" "$stage/DEBIAN/prerm"

out="$here/packaging/${NAME}_${VERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$stage" "$out" >/dev/null
printf '==> %s\n' "$out"
printf '    %s\n' "$(du -h "$out" | cut -f1)"
printf '\nInstall:  sudo apt install %s\n' "$out"
printf 'Verify:   systemctl status %s && curl -s localhost:PORT/health\n' "$NAME"
