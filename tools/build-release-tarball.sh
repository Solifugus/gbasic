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
#                        AND IT NOW CARRIES xml AND xlsx, because libxml2 and
#                        zlib are linked STATICALLY -- see the next block.
#   TIER=full            + sqlite3, libcrypto/libssl, libcurl, dynamically.
#                        The database, internet and AI chapters of the beginner
#                        book all need one of those, and with the lean download
#                        none of them can be followed (DOGFOOD 44).
#
# libxml2 AND zlib ARE LINKED STATICALLY INTO BOTH TIERS, which is a correction
# made 2026-10-11 rather than a new idea: the WINDOWS build has done exactly
# this since M3, and windows_port_plan.md §4 called it "the strongest single
# argument for a Windows build existing" ON THE GROUNDS THAT THE LINUX TARBALLS
# CANNOT. The premise was sound and the conclusion did not follow -- nothing
# about static linking is Windows-specific, and the Linux exclusion was reasoned
# from DYNAMIC linking and then never re-asked. All four copies of that claim
# are struck; see docs/dependency_linking_design.md.
#
# WHAT IT BUYS, MEASURED: lean goes 820K -> 2.2M binary and ~800K -> 1.5M
# tarball, DT_NEEDED STAYS libc AND libm -- libz.so.1 LEFT the list rather than
# joining it -- the glibc floor stays 2.34, and `xlsx`, `xml` and
# compress/uncompress work on a bare ubuntu:22.04 or debian:12 with no packages
# installed. Seven libraries that could not load on EITHER published archive now
# load: ownership, finio_all, finio_camt, finio_iso20022, finio_pain001 (and on
# the full tier, insiders and mdna). xlsx is the highest-value capability in the
# optional set, spreadsheets being a business staple.
#
# WHY STATIC AND NOT dlopen, WHICH WAS THE OTHER CANDIDATE: the XML reader does
# 36 STRUCT FIELD READS (node->children, ->next, ->name, ->type), and dlsym
# hands back function pointers while struct offsets still come from the headers
# at compile time -- so a layout that moved between versions would be read with
# the wrong offsets, which is a crash or silent garbage rather than an error. A
# soname bump from .so.2 to .so.16 is precisely the announcement that something
# in the ABI changed. Static linking removes the soname question, the presence
# question AND the ABI question at once.
#
# UBUNTU'S OWN libxml2.a WILL NOT DO, which is why this builds from source: it
# is compiled against ICU, and `encoding.o` -- which any parse needs -- refers to
# ucnv_*, so the linker drags in ICU, whose data alone is ~30 MB. Built
# --without-icu --without-lzma the archive needs only -lm -lz.
#
# THE VERSION IS PINNED AND THE CHECKSUM VERIFIED, and pinning is a GAIN rather
# than a cost: the dev machine builds against 2.15.2 and Windows already pins
# 2.15.4, while a release built against the container's packages would have used
# ubuntu 22.04's 2.9.13 -- so the three builds disagreed about which libxml2
# gBASIC's goldens describe. They agree now.
#
# THE OBLIGATION THIS TAKES ON, stated because it is the strongest argument
# against the whole arrangement: building libxml2 ourselves means a security fix
# in it becomes OURS to pick up rather than apt's. LIBXML2_VERSION below is the
# one place to change, and it wants watching.
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
# (libxcrypt, whose soname is the other one that varies -- and which is two
# symbols and the cheapest place to prove dlopen, per the design note). It
# gains `sqlite`, `webclient`, `http`, `smtp` and the libcrypto builtins --
# exactly the database, internet and AI chapters DOGFOOD 44 is about. `xml` and
# `xlsx` are no longer on that list: BOTH tiers have them, statically.
# libpq, unixODBC, OpenLDAP, GTK and GObject-Introspection stay out. The first
# two are candidates for dlopen if a consumer asks; GTK and GI are refused by
# BOTH techniques and that is the answer rather than a gap, because they need
# runtime data a binary cannot carry (typelibs, themes, schemas, a display).
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

# THE ONE PLACE libxml2'S VERSION IS WRITTEN, and the checksum is verified
# against GNOME's own published .sha256sum rather than trusted from the
# download. 2.15.4 matches what the Windows build already pins and is the same
# branch the development machine builds against, so all three agree about which
# libxml2 the goldens describe.
LIBXML2_VERSION="2.15.4"
LIBXML2_SHA256="98087fd181d9070724f3fbc65c7377db03038eb92bd882374daff44940138821"
LIBXML2_SERIES="${LIBXML2_VERSION%.*}"

# WHAT BOTH TIERS GET: libxml2 and zlib, statically, from the build below.
# `-Wl,-Bstatic -lz -Wl,-Bdynamic` rather than a path, so it does not hardcode
# the multiarch directory; it is passed once, inside LIBXML2_LIBS, because the
# Makefile puts both LIBXML2_LIBS and ZLIB_LIBS on one link line and two
# -Bstatic/-Bdynamic pairs for the same archive is one more thing to get wrong.
STATIC_XML="LIBXML2_AVAILABLE=1 LIBXML2_CFLAGS=-I/opt/xml2/include/libxml2"
STATIC_XML_LIBS="/opt/xml2/lib/libxml2.a -Wl,-Bstatic -lz -Wl,-Bdynamic -lm"

# WHAT IS SATISFIED WITHOUT APPEARING IN EXPECTED_NEEDED, which the library
# sweep below needs told because a static library leaves no trace in the link
# set -- that is the whole point of it.
#
# THE SWEEP CAUGHT THIS ITSELF, which is worth recording as the one time a
# tripwire here fired on a GOOD change: with libxml2 made static and this line
# missing, the lean build measured 16 libraries unloadable against 21
# predicted, named the five it had over-predicted (ownership, finio_all,
# finio_camt, finio_iso20022, finio_pain001 -- exactly the libxml2-only ones)
# and REFUSED TO PUBLISH. A declaration that had not caught up with the build
# is the same failure as a build that had not caught up with the declaration,
# and the sweep cannot tell which it is -- so it stops.
TIER_STATIC_CAPS="libxml2"

case "$TIER" in
    lean)
        # EVERY EXCLUSION IS EXPLICIT, which is a change: this tier used to rely
        # on pkg-config being ABSENT so the Makefile's probes all failed. That
        # is the arrangement that let libxcrypt in without being asked for (see
        # the header), and libxml2's own ./configure wants pkg-config, so the
        # absence trick is no longer available even if it were wise. Naming the
        # switches means a NEW optional module defaults to off here rather than
        # to whatever the build image happens to carry -- and
        # tests/run_packaging.sh already fails if a switch exists that nothing
        # names.
        TIER_PACKAGES="pkg-config zlib1g-dev"
        TIER_MAKE_ARGS="GIO_AVAILABLE=0 GIR_AVAILABLE=0 GTK_AVAILABLE=0 \
            LDAP_AVAILABLE=0 LIBCRYPTO_AVAILABLE=0 LIBCURL_AVAILABLE=0 \
            LIBPQ_AVAILABLE=0 LIBSSL_AVAILABLE=0 LIBXCRYPT_AVAILABLE=0 \
            ODBC_AVAILABLE=0 SQLITE3_AVAILABLE=0 ZLIB_AVAILABLE=1"
        EXPECTED_NEEDED="libc.so.6 libm.so.6"
        TIER_SUFFIX=""
        ;;
    full)
        TIER_PACKAGES="pkg-config libsqlite3-dev zlib1g-dev libssl-dev libcurl4-openssl-dev"
        # libz.so.1 is GONE from the link set: zlib is static now, in both
        # tiers, so the full tier links one FEWER shared library than it did
        # while gaining xml and xlsx.
        TIER_MAKE_ARGS="LIBXCRYPT_AVAILABLE=0 GIO_AVAILABLE=0 GIR_AVAILABLE=0 \
            GTK_AVAILABLE=0 LDAP_AVAILABLE=0 LIBPQ_AVAILABLE=0 ODBC_AVAILABLE=0 \
            ZLIB_AVAILABLE=1"
        EXPECTED_NEEDED="libc.so.6 libcrypto.so.3 libcurl.so.4 libm.so.6 libsqlite3.so.0 libssl.so.3"
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
apt-get install -y -qq build-essential bison curl ca-certificates xz-utils $TIER_PACKAGES >/dev/null

# libxml2, PINNED AND CHECKSUMMED, built static. A downloaded dependency with
# no checksum is a supply chain somebody else controls, and this one ends up
# inside a binary people are asked to trust.
cd /tmp
curl -sfLO https://download.gnome.org/sources/libxml2/${LIBXML2_SERIES}/libxml2-${LIBXML2_VERSION}.tar.xz \
    || { echo 'FAIL: could not fetch libxml2 ${LIBXML2_VERSION}'; exit 1; }
printf '%s  %s\n' '${LIBXML2_SHA256}' 'libxml2-${LIBXML2_VERSION}.tar.xz' | sha256sum -c - \
    || { echo 'FAIL: libxml2 tarball does not match the pinned checksum -- NOT building'; exit 1; }
tar xf libxml2-${LIBXML2_VERSION}.tar.xz
cd libxml2-${LIBXML2_VERSION}
# --without-icu is the load-bearing flag: with it, encoding.o pulls ICU into
# the link and the binary grows by tens of megabytes of locale data.
./configure --prefix=/opt/xml2 --enable-static --disable-shared \
    --without-icu --without-lzma --without-python --without-http \
    --without-legacy --without-debug --with-zlib >/tmp/xml2conf.log 2>&1 \
    || { echo 'FAIL: libxml2 configure'; tail -20 /tmp/xml2conf.log; exit 1; }
make -j\$(nproc) >/tmp/xml2make.log 2>&1 && make install >>/tmp/xml2make.log 2>&1 \
    || { echo 'FAIL: libxml2 build'; tail -20 /tmp/xml2make.log; exit 1; }
echo \"libxml2 ${LIBXML2_VERSION} static: \$(du -h /opt/xml2/lib/libxml2.a | cut -f1) archive\"

mkdir -p /work && cp -r /src /work/gbasic && cd /work/gbasic
make clean >/dev/null 2>&1 || true
make $TIER_MAKE_ARGS $STATIC_XML 'LIBXML2_LIBS='\"$STATIC_XML_LIBS\" \
     ZLIB_CFLAGS= 'ZLIB_LIBS=-Wl,-Bstatic -lz -Wl,-Bdynamic' \
     >/tmp/build.log 2>&1 || { echo 'FAIL: build'; tail -20 /tmp/build.log; exit 1; }

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

# EVERY LIBRARY IN THE ARCHIVE, AGAINST THE BINARY SHIPPED BESIDE IT.
#
# THIS CHECK EXISTS BECAUSE ITS ABSENCE SHIPPED. Of the 65 libraries in the
# 0.7.0 `-full` tarball, 13 could not be `load`ed by the binary next to them --
# seven needing libxml2, four GObject-Introspection, one unixODBC, one libpq --
# and 21 on the lean one. The tier design above is why, and it is sound; what
# was missing is that nothing connected the two, so a reader following the
# library catalogue to `load ownership` met a runtime error on line 1 with
# nothing wrong with what they typed. Reported from outside, three times for
# three different sets (a stale version pin, then the GUI libraries, then this).
#
# IT IS NOT A LIST OF EXPECTED FAILURES. tools/library-needs.sh derives what
# each library needs from its own `load` lines, transitively; a need is
# satisfied when EXPECTED_NEEDED -- the link set this tier already declares --
# carries a soname beginning with it. So the PREDICTION follows the tier
# automatically: add libxml2 to a tier and the seven XML libraries move from
# unloadable to loadable with no second edit, and the measurement below either
# agrees or the archive is not published.
#
# THE PROBE LIVES OUTSIDE THE EXTRACTED TREE, which cost a measurement to
# learn: with the probe above the stdlib, run_library_depth's rule fires --
# a library may be BESIDE the loading file and never below it -- and all 65
# report a failure that has nothing to do with the build.
echo
probe="$(mktemp -d)"
needs_file="$work/needs"
./tools/library-needs.sh > "$needs_file"
predicted="$work/predicted"; : > "$predicted"
while read -r lib caps; do
    [ -n "${caps:-}" ] || continue
    ok_all=1
    # COMMAS TO SPACES RATHER THAN `IFS=,`, which cost a measurement: setting
    # IFS for the outer split leaks into the INNER loop, so $EXPECTED_NEEDED
    # stops splitting on whitespace, arrives as one long word, matches nothing,
    # and every dependent library is predicted unloadable -- 21 where the truth
    # is 13. A prediction that is wrong in the pessimistic direction still
    # fails the comparison, which is the only reason this was caught here
    # rather than in a release.
    for c in $(printf '%s' "$caps" | tr ',' ' '); do
        have=0
        # Linked dynamically: it appears in the declared link set.
        for n in $EXPECTED_NEEDED; do
            case "$n" in "$c"*) have=1 ;; esac
        done
        # Linked statically: it appears nowhere, so the tier declares it.
        for n in $TIER_STATIC_CAPS; do
            [ "$n" = "$c" ] && have=1
        done
        [ "$have" = 1 ] || ok_all=0
    done
    [ "$ok_all" = 1 ] || printf '%s\n' "$lib" >> "$predicted"
done < "$needs_file"

measured="$work/measured"; : > "$measured"
swept=0
for f in "$root"/share/gbasic/stdlib/*.bas; do
    lib="$(basename "$f" .bas)"
    swept=$((swept + 1))
    printf 'load %s\nprint "loaded"\n' "$lib" > "$probe/p.bas"
    # stdout only: a machine with gBASIC already installed emits a
    # resolved-twice WARNING on stderr for several libraries, which is a fact
    # about that machine and not about this archive.
    got="$( cd "$probe" && env -u GBASIC_PATH timeout -k 2 30 "$root/bin/gbasic" p.bas 2>/dev/null || true )"
    [ "$got" = "loaded" ] || printf '%s\n' "$lib" >> "$measured"
done
rm -rf "$probe"
sort -o "$predicted" "$predicted"
sort -o "$measured" "$measured"
n_pred=$(wc -l < "$predicted" | tr -d ' ')
n_meas=$(wc -l < "$measured" | tr -d ' ')

if [ "$swept" -lt 40 ]; then
    bad "the library sweep only tried $swept libraries -- it is measuring almost nothing"
elif cmp -s "$predicted" "$measured"; then
    ok "$swept libraries swept; exactly the $n_meas the derivation predicts cannot load on $TIER"
    [ "$n_meas" -gt 0 ] && printf '     cannot load: %s\n' "$(tr '\n' ' ' < "$measured")"
else
    bad "the libraries that cannot load are not the ones predicted ($n_meas measured, $n_pred predicted)"
    printf '     only measured:  %s\n' "$(comm -13 "$predicted" "$measured" | tr '\n' ' ')"
    printf '     only predicted: %s\n' "$(comm -23 "$predicted" "$measured" | tr '\n' ' ')"
    printf '     Either a library gained or lost a dependency, or this tier'"'"'s link set changed.\n'
    printf '     Fix docs/reference.md via tools/library-needs.sh -- do not pad the prediction.\n'
fi

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
