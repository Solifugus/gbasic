#!/usr/bin/env bash
# THE DOWNLOAD: a gBASIC a reader can extract and run.
#
# WHY A CONTAINER RATHER THAN THIS MACHINE. A binary's glibc floor is whatever
# it was BUILT against, and a development box is always the newest thing in the
# room -- built here, gBASIC requires glibc 2.38 and will not start on Ubuntu
# 22.04 LTS, Debian 12, or RHEL/Rocky/Alma 9. Measured, not assumed: the
# __isoc23_* symbol family comes from _GNU_SOURCE on a recent glibc, proven with
# a two-line probe, and `fmod` picks up a 2.38 version from libm. Neither is
# fixable with a compiler flag; both are fixed by building somewhere older.
#
# Building in ubuntu:22.04 (gcc 11, glibc 2.35) yields a floor of 2.34 -- one
# artifact covering RHEL/Rocky/Alma 9, Ubuntu 22.04 LTS, Debian 12 and newer.
#
# THE FLOOR IS ASSERTED, NOT HOPED FOR. That is the whole claim this script
# exists to make, and a claim without a check is how the README came to say
# things that had stopped being true. If a future base image raises the floor,
# this fails rather than shipping a tarball that silently stops working on the
# machines it was built for.
#
# LEAN TIER ONLY, DELIBERATELY. A container fixes the GLIBC version; it does not
# fix the other libraries. A full build links ~50 shared objects, and one made
# on 22.04 expects 22.04-era libpq, libxml2 and libodbc -- sonames that differ on
# newer distributions, so a "portable" full tarball would fail at load with a
# message about a missing .so and nothing a reader could act on. Application
# builders who want the optional modules build from source or use a per-distro
# package (tools/build-deb.sh); that is an honest answer where a broken download
# is not.
#
# RELOCATABLE BY CONSTRUCTION. The layout is what `make install` produces, and
# the interpreter resolves <prefix>/share/gbasic/stdlib from /proc/self/exe
# (gb_exe_relative_stdlib), so the tree runs where it is unpacked with no
# GBASIC_PATH and no install step. Asserted below by running the binary from a
# DIFFERENT directory with the environment cleared -- the property the whole
# tarball rests on, and one that was NOT true before 0.2.0.
#
# REPRO_CHECK=1 builds twice and requires byte-identical output.
set -euo pipefail
cd "$(dirname "$0")/.."

IMAGE="${IMAGE:-docker.io/library/ubuntu:22.04}"
GLIBC_MAX="${GLIBC_MAX:-2.35}"
OUT="${OUT:-dist}"
PREFIX_IN_TARBALL="/usr/local"

runtime=""
for c in podman docker; do command -v "$c" >/dev/null 2>&1 && { runtime="$c"; break; }; done
[ -n "$runtime" ] || { echo "FAIL: need podman or docker to build against an older base" >&2; exit 1; }

version="$(sed -n 's/.*printf("gBASIC \([0-9][^\\]*\)\\n").*/\1/p' src/main.c | head -1)"
[ -n "$version" ] || { echo "FAIL: could not read the version from src/main.c" >&2; exit 1; }
arch="$(uname -m)"
name="gbasic-${version}-linux-${arch}"

echo "== building $name in $IMAGE =="
rm -rf "$OUT"; mkdir -p "$OUT"

"$runtime" run --rm -v "$PWD":/src:ro -v "$PWD/$OUT":/out:Z "$IMAGE" bash -euo pipefail -c "
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null
# NOTHING OPTIONAL, and pkg-config is deliberately absent: without it the
# Makefile disables every optional module, which is the leanest configuration
# anyone can build and exactly what this tier ships.
apt-get install -y -qq build-essential bison >/dev/null

mkdir -p /work && cp -r /src /work/gbasic && cd /work/gbasic
make clean >/dev/null 2>&1 || true
make >/tmp/build.log 2>&1 || { echo 'FAIL: build'; tail -20 /tmp/build.log; exit 1; }

# Staged with the SAME install target the project ships, so the tarball layout
# and an installed tree cannot drift apart.
make install PREFIX='$PREFIX_IN_TARBALL' DESTDIR=/work/stage >/dev/null

stage=/work/stage$PREFIX_IN_TARBALL
strip \"\$stage/bin/gbasic\"
cp /work/gbasic/README.md \"\$stage/\"
# REPRODUCIBLE, because a checksum nobody can reproduce cannot be checked
# against anything. Measured before this: two builds of the SAME COMMIT in the
# SAME IMAGE produced d0154895... and 1182af4c... -- so the hash on the download
# page attested to one particular run of this script and not to v\$version, and
# no one could confirm the bytes they fetched came from the tag they name.
#
# Three sources of drift, all of them clocks or orderings rather than content:
#   --mtime      every file carries the moment `cp -r` touched it
#   --sort=name  tar walks the directory in whatever order the filesystem gives
#   gzip -n      gzip stamps the compression time into its own header
# SOURCE_DATE_EPOCH is the convention for the first, so it is honoured when set
# and pinned otherwise. This is the same reasoning the xlsx writer already
# applies to ZIP mod-times, where a clock makes byte comparison useless.
SOURCE_EPOCH=\${SOURCE_DATE_EPOCH:-1000000000}
tar -C /work/stage$PREFIX_IN_TARBALL --owner=0 --group=0 --numeric-owner \\
    --mtime="@\$SOURCE_EPOCH" --sort=name \\
    --transform 's,^\\.,${name},' -cf - . | gzip -n -9 > /out/${name}.tar.gz
echo 'built' \$(du -h /out/${name}.tar.gz | cut -f1)
"

echo
echo "== verifying the artifact =="
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
tar -C "$work" -xzf "$OUT/${name}.tar.gz"
root="$work/$name"
status=0
ok()   { printf 'ok   %s\n' "$1"; }
bad()  { printf 'FAIL %s\n' "$1"; status=1; }

[ -x "$root/bin/gbasic" ] && ok "the binary is there and executable" || bad "no bin/gbasic"

# THE CLAIM THIS SCRIPT EXISTS TO MAKE.
floor="$(objdump -T "$root/bin/gbasic" | grep -oE 'GLIBC_[0-9.]+' | sort -Vu | tail -1 | sed 's/GLIBC_//')"
if [ "$(printf '%s\n%s\n' "$floor" "$GLIBC_MAX" | sort -V | head -1)" = "$floor" ]; then
    ok "glibc floor is $floor (at or below the $GLIBC_MAX this tier promises)"
else
    bad "glibc floor is $floor, ABOVE the $GLIBC_MAX this tier promises"
fi

libs="$(ldd "$root/bin/gbasic" | wc -l)"
[ "$libs" -lt 20 ] && ok "links $libs shared libraries (lean)" \
                   || bad "links $libs shared libraries -- this is not a lean build"

# RUNS AT ALL, from somewhere that is not the tarball.
#
# A REAL FILE, not process substitution: <(...) is a PIPE, and the interpreter
# needs a seekable source, so the first draft of this check reported
# "it did not run a program: /dev/fd/63: Illegal seek" -- a failure message
# pointing at the artifact when the defect was in the check.
printf 'print 2 + 3\n' > "$work/smoke.bas"
got="$( cd "$work" && env -u GBASIC_PATH "$root/bin/gbasic" "$work/smoke.bas" 2>&1 || true )"
[ "$got" = "5" ] && ok "it runs a program" || bad "it did not run a program: $got"

# THE RELOCATION PROPERTY, which is what makes this a download and not an
# installer. Run from an unrelated directory, environment cleared, resolving a
# stdlib library -- with the tree at a path no build ever knew about.
moved="$work/moved-somewhere-else"
mv "$root" "$moved"
printf 'load dates\nprint "stdlib resolved"\n' > "$work/d.bas"
got="$( cd /tmp && env -u GBASIC_PATH "$moved/bin/gbasic" "$work/d.bas" 2>&1 | tail -1 || true )"
[ "$got" = "stdlib resolved" ] \
    && ok "it finds its own stdlib after being moved (no GBASIC_PATH, no install)" \
    || bad "it did not find its stdlib after being moved: $got"

# THE REPRODUCIBILITY CLAIM, CHECKABLE. Opt-in because it doubles the build, and
# a claim nobody can run is the kind that quietly stops being true -- this one
# WAS false until 2026-09-20, and nothing said so.
if [ "${REPRO_CHECK:-0}" = "1" ] && [ "$status" = 0 ]; then
    first="$(sha256sum "$OUT/${name}.tar.gz" | awk '{print $1}')"
    echo
    echo "== REPRO: building a second time and comparing =="
    keep="$(mktemp -d)"; mv "$OUT/${name}.tar.gz" "$keep/"
    REPRO_CHECK=0 "$0" >/dev/null 2>&1 || { echo "FAIL repro (second build failed)"; status=1; }
    second="$(sha256sum "$OUT/${name}.tar.gz" 2>/dev/null | awk '{print $1}')"
    if [ "$first" = "$second" ]; then
        ok "two builds of this commit are byte-identical"
    else
        bad "two builds differ: $first vs $second"
    fi
    rm -rf "$keep"
fi

echo
if [ "$status" = 0 ]; then
    echo "$OUT/${name}.tar.gz is ready"
    ( cd "$OUT" && sha256sum "${name}.tar.gz" | tee "${name}.tar.gz.sha256" )
else
    echo "artifact NOT usable; not publishing" >&2
fi
exit "$status"
