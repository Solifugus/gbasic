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
# TWO TIERS, AND THE SECOND ONE IS NARROW ON PURPOSE.
#
#   TIER=lean (default)  libm and libc, nothing else. Extract and run, on any
#                        machine at or above the glibc floor, with no
#                        dependency a reader has to satisfy or even read about.
#   TIER=full            + sqlite3, zlib, libxml2, libcrypto/libssl, libcurl.
#                        The database, internet and AI chapters of the beginner
#                        book all need one of those, and with the lean download
#                        none of them can be followed (DOGFOOD 44).
#
# WHY NOT ONE FULLER DOWNLOAD. A container fixes the GLIBC version; it does not
# fix the other libraries, and a DT_NEEDED on a soname the machine does not have
# means the binary does not start AT ALL -- not a degraded feature, a download
# that does nothing. So lean stays exactly as it is and full sits beside it.
#
# WHY THESE FIVE AND NOT THE OTHERS. This script used to say a full build links
# ~50 shared objects whose sonames differ across distributions. True of ~50, and
# MEASURED 2026-09-23 against the nine the `full` tier actually links, by asking
# each distribution in the floor rather than reasoning about it:
#
#                  22.04  24.04  Deb12  Deb13  Rocky9  F41  F43  Ubuntu 26.04
#   libsqlite3.so.0   .0     .0     .0     .0     .0     .0   .0      .0
#   libcurl.so.4      .4     .4     .4     .4     .4     .4   .4      .4
#   libssl.so.3       .3     .3     .3     .3     .3     .3   .3      .3
#   libcrypto.so.3    .3     .3     .3     .3     .3     .3   .3      .3
#   libz.so.1         .1     .1     .1     .1     .1     .1   .1      .1
#   libcrypt          .1     .1     .1     .1   **.2**  **.2** **.2**  .1
#   libxml2           .2     .2     .2     .2     .2     .2   .2    **.16**
#
# SEVEN OF NINE ARE IDENTICAL EVERYWHERE, AND TWO ARE NOT, and both of the two
# are a hard failure -- a DT_NEEDED on a soname the machine spells differently
# means `bin/gbasic` does not start at all, with a message about a `.so` and
# nothing a reader can do.
#
#   libxcrypt  .so.1 on the Debian family, .so.2 on the RHEL family.
#   libxml2    .so.2 for most of the range, .so.16 from libxml2 2.14 (2025) --
#              already true on Ubuntu 26.04.
#
# Both are therefore disabled EXPLICITLY (LIBXCRYPT_AVAILABLE=0,
# LIBXML2_AVAILABLE=0) rather than merely not installed, because libxcrypt
# arrived in an earlier measurement WITHOUT BEING ASKED FOR -- installing
# pkg-config enables every probe in the Makefile -- and a dependency nobody
# chose is one nobody will think to check.
#
# WHAT THE full TIER COSTS AND BUYS, stated plainly because a reader deserves
# to know which download they took: it loses `password_hash`/`password_verify`
# (libxcrypt) and `xml` and `xlsx` (libxml2). It gains `sqlite`, `webclient`,
# `http`, `smtp` and the libcrypto builtins -- which is exactly the database,
# internet and AI chapters DOGFOOD 44 is about. libpq, unixODBC, OpenLDAP, GTK
# and GObject-Introspection stay out; those are the ~50, and the argument
# against them is unchanged.
#
# THE LINK SET IS ASSERTED, not counted. `ldd | wc -l < 20` would have passed a
# binary carrying libcrypt, which is what happened while measuring: a count
# says "few enough" where what must be true is "these and no others".
#
# AND THE MEASUREMENT ABOVE IS WHY THE SMOKE TEST BELOW MATTERS MORE THAN IT
# LOOKS. A first version of this tier was measured against 22.04, Debian 12,
# Rocky 9 and Fedora 41 -- the OLDER half of a range that promises "and newer"
# -- concluded one library split rather than two, and built an artifact that
# would not start on the machine that built it. The run-a-program check caught
# it and refused to publish. Application builders wanting xlsx, XML or password
# hashing build from source or use a per-distro package (tools/build-deb.sh).
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
TIER="${TIER:-lean}"

case "$TIER" in
    lean)
        # pkg-config is deliberately absent: without it the Makefile disables
        # every optional module, which is the leanest configuration anyone can
        # build and exactly what this tier ships.
        TIER_PACKAGES=""
        TIER_MAKE_ARGS=""
        EXPECTED_NEEDED="libc.so.6 libm.so.6"
        TIER_SUFFIX=""
        ;;
    full)
        TIER_PACKAGES="pkg-config libsqlite3-dev zlib1g-dev libssl-dev libcurl4-openssl-dev"
        TIER_MAKE_ARGS="LIBXCRYPT_AVAILABLE=0 LIBXML2_AVAILABLE=0"
        EXPECTED_NEEDED="libc.so.6 libcrypto.so.3 libcurl.so.4 libm.so.6 libsqlite3.so.0 libssl.so.3 libz.so.1"
        TIER_SUFFIX="-full"
        ;;
    *)
        echo "FAIL: TIER must be 'lean' or 'full', not '$TIER'" >&2; exit 1 ;;
esac

runtime=""
for c in podman docker; do command -v "$c" >/dev/null 2>&1 && { runtime="$c"; break; }; done
[ -n "$runtime" ] || { echo "FAIL: need podman or docker to build against an older base" >&2; exit 1; }

version="$(sed -n 's/.*printf("gBASIC \([0-9][^\\]*\)\\n").*/\1/p' src/main.c | head -1)"
[ -n "$version" ] || { echo "FAIL: could not read the version from src/main.c" >&2; exit 1; }
arch="$(uname -m)"
name="gbasic-${version}-linux-${arch}${TIER_SUFFIX}"

echo "== building $name in $IMAGE (tier: $TIER) =="
# NOT `rm -rf "$OUT"`: the two tiers publish side by side, and wiping the
# directory would mean building one deleted the other.
mkdir -p "$OUT"
rm -f "$OUT/${name}.tar.gz" "$OUT/${name}.tar.gz.sha256"

"$runtime" run --rm -v "$PWD":/src:ro -v "$PWD/$OUT":/out:Z "$IMAGE" bash -euo pipefail -c "
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null
apt-get install -y -qq build-essential bison $TIER_PACKAGES >/dev/null

mkdir -p /work && cp -r /src /work/gbasic && cd /work/gbasic
make clean >/dev/null 2>&1 || true
make $TIER_MAKE_ARGS >/tmp/build.log 2>&1 || { echo 'FAIL: build'; tail -20 /tmp/build.log; exit 1; }

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
# NOTE THE COMMENTS BELOW LIVE INSIDE A DOUBLE-QUOTED HOST STRING, so a
# backtick here is command substitution the host runs before the container
# ever starts. This block used to quote 'cp -r' in backticks, and every
# release build silently executed it: cp with no operands, whose error
# landed in the build log looking like a packaging failure. Harmless by
# luck rather than by design -- and the first attempt to document the fix
# reintroduced it, by quoting the error message in backticks too. Single
# quotes only, in every comment from here to the end of the string.
#   --mtime      every file carries the moment 'cp -r' touched it
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

# THE LINK SET, NAMED. A count would have passed the binary that picked up
# libcrypt without being asked (see the header): counting says "few enough",
# and what must be true is "these and no others", since one extra DT_NEEDED on
# a soname a distribution spells differently is a download that will not start.
# ld.so does not care about order, so both sides are sorted.
needed="$(objdump -p "$root/bin/gbasic" | awk '/NEEDED/ {print $2}' | sed 's/\.so\.\([0-9]*\).*/.so.\1/' | sort -u | tr '\n' ' ' | sed 's/ $//')"
want="$(printf '%s\n' $EXPECTED_NEEDED | sort -u | tr '\n' ' ' | sed 's/ $//')"
if [ "$needed" = "$want" ]; then
    ok "links exactly the $TIER tier's libraries: $needed"
else
    bad "link set is not what the $TIER tier promises"
    printf '     got:  %s\n' "$needed"
    printf '     want: %s\n' "$want"
fi

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
    TIER="$TIER" REPRO_CHECK=0 "$0" >/dev/null 2>&1 || { echo "FAIL repro (second build failed)"; status=1; }
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
