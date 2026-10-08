#!/usr/bin/env bash
# Build the libcurl that gbasic.exe links STATICALLY on Windows (milestone M5).
#
#     tools/build-curl-windows.sh [PREFIX]      (in an MSYS2 UCRT64 shell)
#
# PREFIX defaults to ~/gbasic-deps/curl-schannel, which is where the Makefile's
# Windows block looks; it switches webclient/http/smtp ON exactly when
# PREFIX/lib/libcurl.a exists, so a machine that has not run this still builds.
#
# WHY NOT MSYS2's OWN libcurl. It is built against OpenSSL and statically pulls
# in libidn2 and libunistring (LGPL) plus HTTP/3, SSH, brotli and zstd. This one:
#   - TLS is SCHANNEL, Windows' own: it trusts the Windows certificate store
#     and is patched by Windows Update, and no OpenSSL is linked at all;
#   - protocols are HTTP(S) and SMTP(S) only -- what webclient, http and smtp
#     use. `file:` is off as well; gBASIC refuses that scheme before libcurl
#     sees it, and a protocol not compiled in cannot be reached by a redirect;
#   - its only dependency is zlib (Zlib licence) and Windows' own libraries, so
#     gbasic.exe stays free of LGPL code (docs/windows_port_status.md §19).
#
# The source is PINNED by version AND SHA-256, and a tarball that does not match
# is refused rather than built. The hash was checked against a second source
# when it was pinned: MSYS2's own recipe for the same release records it.
set -euo pipefail

VERSION=8.22.0
SHA256=f7ef3ae8a22e521f289803fe93543eb64c329b58aa73a9e224dfd915a2a5f4f7
PREFIX="${1:-$HOME/gbasic-deps/curl-schannel}"
SRC="$HOME/gbasic-deps/src"
TARBALL="curl-$VERSION.tar.xz"

case "${MSYSTEM:-}" in
    UCRT64) ;;
    *) echo "build-curl-windows: run this in an MSYS2 UCRT64 shell (MSYSTEM=${MSYSTEM:-unset})" >&2; exit 1 ;;
esac

mkdir -p "$SRC"
cd "$SRC"
if [ ! -f "$TARBALL" ]; then
    echo "downloading $TARBALL from curl.se"
    curl -fsSLO "https://curl.se/download/$TARBALL"
fi
got="$(sha256sum "$TARBALL" | cut -d' ' -f1)"
if [ "$got" != "$SHA256" ]; then
    echo "build-curl-windows: $TARBALL has sha256 $got, expected $SHA256 -- REFUSING to build it" >&2
    exit 1
fi

rm -rf "curl-$VERSION"
tar -xf "$TARBALL"
cd "curl-$VERSION"

./configure --prefix="$PREFIX" \
    --disable-shared --enable-static \
    --with-schannel --without-openssl \
    --with-zlib \
    --without-libpsl --without-libidn2 --without-zstd --without-brotli \
    --without-nghttp2 --without-nghttp3 --without-ngtcp2 \
    --without-libssh2 --without-libssh --without-librtmp --without-libgsasl \
    --enable-http --enable-smtp \
    --disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp \
    --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap \
    --disable-smb --disable-gopher --disable-mqtt --disable-ipfs \
    --disable-manual --disable-docs \
    CFLAGS="-O2" > configure.log 2>&1 || { tail -30 configure.log; exit 1; }

make -j"$(nproc)" > make.log 2>&1 || { tail -30 make.log; exit 1; }
make install > install.log 2>&1 || { tail -30 install.log; exit 1; }

echo "installed: $PREFIX/lib/libcurl.a"
"$PREFIX/bin/curl-config" --version
echo "ssl backends: $("$PREFIX/bin/curl-config" --ssl-backends)"
echo "protocols:    $("$PREFIX/bin/curl-config" --protocols | tr '\n' ' ')"
echo "static libs:  $("$PREFIX/bin/curl-config" --static-libs)"
