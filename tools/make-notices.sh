#!/usr/bin/env bash
# Write THIRD-PARTY-NOTICES.txt for the Windows gbasic.exe, from what the LINKER
# actually used (docs/windows_port_status.md §25). Run in an "MSYS2 UCRT64"
# shell at the repository root:
#
#   tools/make-notices.sh [OUTPUT]      # default build/notices/THIRD-PARTY-NOTICES.txt
#
# HOW IT KNOWS WHAT SHIPS. It relinks gbasic.exe to a scratch file with
# -Wl,--trace, which prints every archive the linker took code from, and maps
# each to its owner:
#   - an archive under /ucrt64 -> the MSYS2 package owning it (pacman -Qo);
#   - libcurl.a from the pinned build (tools/build-curl-windows.sh) -> curl;
#   - libgbasic.a is gBASIC's own, but it also carries the vendored yescrypt
#     objects when the build includes them -> yescrypt.
# Every owner must have a line in packaging/windows/notices.manifest, or this
# FAILS naming the archive -- so a library added to the build without its
# notice cannot ship quietly. Versions are read from what is installed, so the
# notices describe the binary built, not the one somebody remembered.
set -euo pipefail
cd "$(dirname "$0")/.."

out="${1:-build/notices/THIRD-PARTY-NOTICES.txt}"
manifest="packaging/windows/notices.manifest"
work="build/notices"
mkdir -p "$work" "$(dirname "$out")"

make >/dev/null 2>&1 || { make; exit 1; }
make gbasic-lsp >/dev/null 2>&1 || { make gbasic-lsp; exit 1; }

# Every binary that ships, relinked as make would link it, with the trace on.
# Both: gbasic-lsp is in the package too, and it alone carries cJSON.
: > "$work/trace.log"
for bin in gbasic gbasic-lsp; do
    link="$(make -n -B "$bin" 2>/dev/null | grep -E -- "-o $bin(\.exe)? " | tail -1)"
    if [ -z "$link" ]; then
        echo "make-notices: could not find $bin's link command in make -n" >&2
        exit 1
    fi
    link="$(printf '%s' "$link" | sed -E "s#-o $bin(\.exe)? #-o $work/trace.exe #")"
    eval "$link -Wl,--trace" >> "$work/trace.log" 2>&1 || {
        cat "$work/trace.log" >&2
        echo "make-notices: the traced link of $bin failed" >&2
        exit 1
    }
    rm -f "$work/trace.exe"
done

# Archives the linker opened: lines like "/path/libfoo.a(member.o)" or "/path/libfoo.a".
archives="$(sed -n -E 's/^([^(]*\.a)(\(.*)?$/\1/p' "$work/trace.log" | sort -u)"
# Objects linked directly from a vendored tree: third_party/<name>/x.o -> <name>.
vendored="$(sed -n -E 's#^(.*/)?third_party/([^/]+)/[^/(]+\.o$#\2#p' "$work/trace.log" | sort -u)"

owners=""
unowned=""
add_owner() { case " $owners " in *" $1 "*) ;; *) owners="$owners $1" ;; esac; }
for v in $vendored; do add_owner "$v"; done
while IFS= read -r a; do
    [ -n "$a" ] || continue
    case "$a" in
        libgbasic.a|*/libgbasic.a)
            if ar t libgbasic.a | grep -q '^yescrypt'; then add_owner yescrypt; fi
            continue ;;
        *gbasic-deps/curl*/libcurl.a)
            add_owner curl
            continue ;;
    esac
    unix="$(cygpath -u "$a")"
    unix="$(realpath -m "$unix")"
    owner="$(pacman -Qoq "$unix" 2>/dev/null || true)"
    if [ -z "$owner" ]; then
        unowned="$unowned
  $a"
    else
        add_owner "$owner"
    fi
done <<< "$archives"

if [ -n "$unowned" ]; then
    printf 'make-notices: no package owns these archives, so their licence is unknown:%s\n' "$unowned" >&2
    exit 1
fi

missing=""
for o in $owners; do
    grep -q "^$o	" "$manifest" || missing="$missing $o"
done
if [ -n "$missing" ]; then
    printf 'make-notices: linked into gbasic.exe or gbasic-lsp.exe but not in %s:%s\n' "$manifest" "$missing" >&2
    printf 'Add a line for each with where its licence text lives.\n' >&2
    exit 1
fi

# --- the two this tree builds itself -----------------------------------------
curl_version="$(sed -n 's/^VERSION=//p' tools/build-curl-windows.sh)"
curl_sha="$(sed -n 's/^SHA256=//p' tools/build-curl-windows.sh)"
curl_tar="$HOME/gbasic-deps/src/curl-$curl_version.tar.xz"
curl_text() {
    if [ ! -f "$curl_tar" ] || [ "$(sha256sum "$curl_tar" | cut -d' ' -f1)" != "$curl_sha" ]; then
        echo "make-notices: need the pinned $curl_tar (tools/build-curl-windows.sh fetches it)" >&2
        exit 1
    fi
    tar -xJOf "$curl_tar" "curl-$curl_version/COPYING"
}
yescrypt_version="$(sed -n -E 's/^- \*\*Version:\*\* ([0-9][0-9.]*[0-9]).*/\1/p' third_party/yescrypt/README.md | head -1)"
if [ -z "$yescrypt_version" ]; then
    echo "make-notices: no '- **Version:** X.Y.Z' line in third_party/yescrypt/README.md" >&2
    exit 1
fi
yescrypt_text() {
    # No licence FILE in the release: the terms are each source's header. Quote
    # the header of every file the build compiles, deduplicated.
    for f in yescrypt-opt.c yescrypt-platform.c yescrypt-common.c sha256.c insecure_memzero.c; do
        awk 'NR==1 && !/^\/\*/ {exit} {print} /\*\// {exit}' "third_party/yescrypt/$f"
        echo
    done | awk 'BEGIN{RS=""; ORS="\n\n"} !seen[$0]++'
}

version_of() {
    case "$1" in
        curl) printf '%s' "$curl_version" ;;
        yescrypt) printf '%s' "$yescrypt_version" ;;
        cjson) awk '/#define CJSON_VERSION_(MAJOR|MINOR|PATCH)/ {v = v (v ? "." : "") $3} END {print v}' third_party/cjson/cJSON.h | tr -d '
' ;;
        *) pacman -Q "$1" | awk '{print $2}' ;;
    esac
}

{
    printf 'gBASIC for Windows -- third-party notices\n'
    printf '=========================================\n\n'
    printf 'gBASIC itself is licensed under the Apache License 2.0; see LICENSE and NOTICE.\n'
    printf 'gbasic.exe also contains the following third-party code, linked statically.\n'
    printf 'Each is listed with its version and the full text of its licence.\n'
    printf 'This file was generated by tools/make-notices.sh from the archives the linker used.\n\n'
    for o in $owners; do
        line="$(grep "^$o	" "$manifest")"
        name="$(printf '%s' "$line" | cut -f2)"
        licence="$(printf '%s' "$line" | cut -f3)"
        text="$(printf '%s' "$line" | cut -f4)"
        printf -- '-------------------------------------------------------------------------------\n'
        printf '%s %s\n' "$name" "$(version_of "$o")"
        printf 'Licence: %s\n' "$licence"
        printf -- '-------------------------------------------------------------------------------\n\n'
        case "$text" in
            @curl) curl_text ;;
            @yescrypt) yescrypt_text ;;
            /*) cat "$text" ;;
            third_party/*) cat "$text" ;;
        esac
        printf '\n\n'
    done
} > "$out"

count="$(printf '%s\n' $owners | wc -l)"
printf 'wrote %s: %s components (%s)\n' "$out" "$count" "$(printf '%s ' $owners | sed 's/mingw-w64-ucrt-x86_64-//g')"
