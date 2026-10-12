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
#                        AND IT NOW CARRIES xml, xlsx AND sqlite, because
#                        libxml2, zlib and sqlite3 are linked STATICALLY --
#                        see the next block.
#   TIER=full            + libcrypto/libssl and libcurl, dynamically.
#                        The database, internet and AI chapters of the beginner
#                        book all need one of those, and with the lean download
#                        none of them can be followed (DOGFOOD 44).
#
# libxml2, zlib AND sqlite3 ARE LINKED STATICALLY INTO BOTH TIERS, which is a
# correction made 2026-10-11 rather than a new idea: the WINDOWS build has done
# exactly this since M3 (and declares sqlite static at M4), and windows_port_plan.md §4 called it "the strongest single
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
# SQLITE IS THE SAME ARGUMENT AND IT RUNS THE OTHER WAY TOO: static linking
# REMOVES a DT_NEEDED entry, so the full tier links one fewer shared library
# than it did while the lean tier gains `dbframe`, `screener` and `edgar`
# without gaining a dependency. sqlite is public domain and ships as an
# AMALGAMATION -- one .c file, upstream's own recommended way to embed it -- so
# there is no licence obligation and nothing to configure.
#
# THE COMPILE OPTIONS ARE MEASURED AGAINST THE BUILD THEY REPLACE, NOT CHOSEN.
# `PRAGMA compile_options` on ubuntu 22.04's libsqlite3 names 13 features, and
# the `sqlite` module hands a gBASIC program ARBITRARY SQL -- so an option a
# program can reach has to be matched or the static build is a QUIETLY REDUCED
# one, which is the defect class this whole exercise started from. Matched:
# FTS5, RTREE, MATH_FUNCTIONS (sqrt/log in SQL), STMTVTAB,
# UPDATE_DELETE_LIMIT, SOUNDEX, SECURE_DELETE, USE_URI,
# LIKE_DOESNT_MATCH_BLOBS and the two raised limits. Two of those were nearly
# missed by reading the list rather than classifying it:
# LIKE_DOESNT_MATCH_BLOBS changes what `LIKE` ANSWERS, and
# MAX_VARIABLE_NUMBER=250000 is 7x the default, which a wide bulk insert can
# reach. DROPPED, each with a reason: PREUPDATE_HOOK, SESSION and
# UNLOCK_NOTIFY are C-API only and unreachable from the 22 sqlite3_* calls in
# src/eval.c; LOAD_EXTENSION is dropped DELIBERATELY and measured
# behaviour-neutral -- both builds refuse `select load_extension(...)`, because
# gBASIC never calls sqlite3_enable_load_extension, so omitting it changes
# nothing a program could do while closing the door if that call is ever added.
#
# THE VERSION JUMP WAS MEASURED RATHER THAN ASSUMED, because it is the real
# risk here: the published release linked ubuntu 22.04's 3.37.2 and the dev
# machine has 3.46.1, so a golden relying on row order without ORDER BY could
# move under a different query planner. run_sqlite, run_dbframe, run_compile,
# run_string_nul, run_xlsx_cookbook, run_examples and run_negative all pass
# against 3.54.0 statically linked, with no golden moved.
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
#     ^ kept as the measurement it was; libsqlite3 is STATIC now and so is in
#       neither tier's link set. Its soname never varied -- what static linking
#       buys here is PRESENCE (a bare base image has no libsqlite3.so.0, which
#       is why the full archive would not start on one) and one fewer entry.
#   libcurl.so.4      .4     .4     .4     .4     .4     .4   .4      .4
#   libssl.so.3       .3     .3     .3     .3     .3     .3   .3      .3
#   libcrypto.so.3    .3     .3     .3     .3     .3     .3   .3      .3
#   libz.so.1         .1     .1     .1     .1     .1     .1   .1      .1
#   libcrypt          .1     .1     .1     .1   **.2**  **.2** **.2**  .1
#     ^ kept as the measurement it was, and it is the row that MOTIVATED
#       this table. libcrypt is in neither tier's link set now -- not
#       because the soname stopped varying, but because `password_hash`
#       no longer asks a system library at all: the VENDORED yescrypt
#       (third_party/yescrypt, BSD-2) is the backend in both tiers, so
#       the varying soname is simply not consulted. See the note below.
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
# PASSWORD HASHING IS IN BOTH TIERS NOW, AND NOT BY THE ROUTE THE DESIGN NOTE
# PLANNED. `docs/dependency_linking_design.md` step 3 proposed reaching
# libxcrypt by `dlopen`, on the argument that it is two symbols and the cheapest
# place to prove that pattern. The tree already held a better answer: the
# vendored yescrypt (third_party/yescrypt, BSD-2), written for Windows because
# libxcrypt is LGPL, producing and verifying the SAME `$y$` hashes libxcrypt's
# default produces. It was Windows-only for eight days because nobody re-asked
# whether Linux wanted it -- the same omission, in the same tree, that the
# static-linking correction is about.
#
# Measured before choosing, because `dlopen` was not obviously worse:
#   * libcrypt is PRESENT on all eight base images (ubuntu 22.04/24.04/26.04,
#     debian 12/13, fedora 41/43, rocky 9) with NO packages installed, and
#     `readelf --dyn-syms` shows all four of crypt/crypt_r/crypt_ra/
#     crypt_gensalt_ra exported on every one -- so dlopen would in fact have
#     worked. That is the OPPOSITE of what was expected (the guess was that a
#     bare image would lack it, which would have settled the question outright)
#     and it is the reason it was measured rather than argued. The same run
#     confirms the soname split this table records: `.so.1` on all five
#     Debian-family images, `.so.2` on all three RHEL-family ones;
#   * libxcrypt's own default is `$y$j9T$` at both ends of the range in the
#     build images -- 4.4.27 on ubuntu 22.04 and 4.5.2 on fedora 41, each
#     measured by building gBASIC against it IN that image and asking -- so the
#     written scheme would have agreed. (Rocky 9, in the middle at 4.4.18, was
#     NOT measured for this: its dnf hung with no output and the probe was
#     killed rather than guessed at.)
#   * vendoring costs +44 KB of stripped binary and does not move DT_NEEDED.
# So the choice is not about whether dlopen works. It is that vendoring makes
# the soname question, the presence question and the dlsym-returns-NULL failure
# mode all ABSENT rather than handled -- the same three rows static linking won
# on for libxml2 -- for 44 KB. "Present on the eight images we looked at" is the
# argument this script already rejected when it chose to assert a link set
# rather than count one.
#
# THE ONE THING IT COSTS IS STATED, because it is a real behavioural
# difference: the vendored backend reads `$y$` and `$7$` and NOT the legacy
# crypt(3) schemes, so a `$6$` or `$2b$` hash from an existing user table is
# REFUSED BY NAME ("this build verifies yescrypt ($y$) and scrypt ($7$) hashes;
# this one is $6$") rather than answered false -- a false there would tell a
# caller the PASSWORD was wrong and lock a real user out silently. A
# from-source build on a machine with libxcrypt still reads every scheme.
#
# WHAT THE full TIER COSTS AND BUYS, stated plainly because a reader deserves
# to know which download they took: it gains `sqlite`, `webclient`, `http`,
# `smtp` and the libcrypto builtins -- exactly the database, internet and AI
# chapters DOGFOOD 44 is about. `xml`, `xlsx` and `password_hash` are no longer
# on the cost side: BOTH tiers have them.
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

# THE ONE PLACE SQLITE'S VERSION IS WRITTEN. sqlite.org publishes a SHA3-256
# for every download (the fifth field of the PRODUCT line in download.html) and
# that is what is verified, rather than a SHA-256 computed here -- so the chain
# of trust ends at upstream rather than at whoever edited this file. Same
# ongoing obligation as libxml2: a security fix becomes ours to pick up.
SQLITE_VERSION="3.54.0"
SQLITE_ZIP="sqlite-amalgamation-3540000"
SQLITE_YEAR="2026"
SQLITE_SHA3="7b670a62fdfbd672b75fef004cb703c8a3e87d3a5cc7d675b4a08337004a2d93"

# EVERY FLAG HERE MUST APPEAR IN `PRAGMA compile_options`, asserted against the
# staged binary below. That is not a restatement of the build: it is what
# catches a flag upstream has RETIRED, which is not hypothetical -- the first
# draft of this list carried -DSQLITE_ENABLE_JSON1, matching the distro, and
# the assertion found it absent because JSON became unconditional in 3.38 and
# the option was removed. A dead flag in a list like this reads exactly like a
# live one. JSON is asserted behaviourally instead, by calling json_extract.
SQLITE_OPTS="-DSQLITE_ENABLE_FTS5 -DSQLITE_ENABLE_MATH_FUNCTIONS \
    -DSQLITE_ENABLE_RTREE -DSQLITE_ENABLE_STMTVTAB \
    -DSQLITE_ENABLE_UPDATE_DELETE_LIMIT -DSQLITE_SOUNDEX \
    -DSQLITE_SECURE_DELETE -DSQLITE_USE_URI \
    -DSQLITE_LIKE_DOESNT_MATCH_BLOBS -DSQLITE_MAX_VARIABLE_NUMBER=250000 \
    -DSQLITE_MAX_DEFAULT_PAGE_SIZE=32768 -DSQLITE_THREADSAFE=1 \
    -DSQLITE_OMIT_LOAD_EXTENSION"

# WHAT BOTH TIERS GET: libxml2 and zlib, statically, from the build below.
# `-Wl,-Bstatic -lz -Wl,-Bdynamic` rather than a path, so it does not hardcode
# the multiarch directory; it is passed once, inside LIBXML2_LIBS, because the
# Makefile puts both LIBXML2_LIBS and ZLIB_LIBS on one link line and two
# -Bstatic/-Bdynamic pairs for the same archive is one more thing to get wrong.
STATIC_XML="LIBXML2_AVAILABLE=1 LIBXML2_CFLAGS=-I/opt/xml2/include/libxml2"
STATIC_XML_LIBS="/opt/xml2/lib/libxml2.a -Wl,-Bstatic -lz -Wl,-Bdynamic -lm"
STATIC_SQLITE="SQLITE3_AVAILABLE=1 SQLITE3_CFLAGS=-I/opt/sqlite SQLITE3_LIBS=/opt/sqlite/libsqlite3.a"

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
TIER_STATIC_CAPS="libxml2 libsqlite3"
# The third-party code compiled INTO the binary, which is what the notices file
# must name. SEPARATE FROM TIER_STATIC_CAPS ABOVE, which is a list of `load`-able
# CAPABILITIES: yescrypt provides no module and so is absent there, while being
# exactly the kind of redistribution a licence cares about. Both tiers carry the
# same four, because all four are static in both.
NOTICE_STATIC="libxml2 zlib sqlite yescrypt"

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
            ODBC_AVAILABLE=0 ZLIB_AVAILABLE=1 YESCRYPT_VENDORED=1"
        EXPECTED_NEEDED="libc.so.6 libm.so.6"
        TIER_SUFFIX=""
        ;;
    full)
        # No libsqlite3-dev: sqlite comes from the pinned amalgamation now, in
        # both tiers, so the package would only invite the Makefile's probe to
        # find a DIFFERENT sqlite than the one being linked.
        TIER_PACKAGES="pkg-config zlib1g-dev libssl-dev libcurl4-openssl-dev"
        # libz.so.1 AND libsqlite3.so.0 are both GONE from the link set, so the
        # full tier links TWO FEWER shared libraries than it did while gaining
        # xml and xlsx.
        TIER_MAKE_ARGS="LIBXCRYPT_AVAILABLE=0 GIO_AVAILABLE=0 GIR_AVAILABLE=0 \
            GTK_AVAILABLE=0 LDAP_AVAILABLE=0 LIBPQ_AVAILABLE=0 ODBC_AVAILABLE=0 \
            ZLIB_AVAILABLE=1 YESCRYPT_VENDORED=1"
        EXPECTED_NEEDED="libc.so.6 libcrypto.so.3 libcurl.so.4 libm.so.6 libssl.so.3"
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
apt-get install -y -qq build-essential bison curl ca-certificates xz-utils unzip openssl $TIER_PACKAGES >/dev/null

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

# sqlite, from the AMALGAMATION -- upstream's own recommended way to embed it,
# one .c file with no build system to argue with. Verified against the SHA3-256
# sqlite.org publishes, not a hash computed on this machine.
cd /tmp
curl -sfLO https://sqlite.org/${SQLITE_YEAR}/${SQLITE_ZIP}.zip \
    || { echo 'FAIL: could not fetch ${SQLITE_ZIP}'; exit 1; }
got3=\$(openssl dgst -sha3-256 -r ${SQLITE_ZIP}.zip | cut -d' ' -f1)
[ \"\$got3\" = '${SQLITE_SHA3}' ] \
    || { echo \"FAIL: ${SQLITE_ZIP}.zip SHA3-256 is \$got3, not the pinned ${SQLITE_SHA3} -- NOT building\"; exit 1; }
unzip -q ${SQLITE_ZIP}.zip
mkdir -p /opt/sqlite
cc -O2 -c /tmp/${SQLITE_ZIP}/sqlite3.c -o /tmp/sqlite3.o $SQLITE_OPTS \
    || { echo 'FAIL: sqlite compile'; exit 1; }
ar rcs /opt/sqlite/libsqlite3.a /tmp/sqlite3.o
cp /tmp/${SQLITE_ZIP}/sqlite3.h /tmp/${SQLITE_ZIP}/sqlite3ext.h /opt/sqlite/
echo \"sqlite ${SQLITE_VERSION} static: \$(du -h /opt/sqlite/libsqlite3.a | cut -f1) archive\"

mkdir -p /work && cp -r /src /work/gbasic && cd /work/gbasic
make clean >/dev/null 2>&1 || true

# ONE ARGUMENT LIST FOR BOTH make INVOCATIONS, AND IT HAS TO BE ONE RATHER THAN
# TWO COPIES. \`install\` depends on \`gbasic\`, and since 2026-10-11 every object
# depends on .buildswitch-stamp -- so an install invoked WITHOUT these switches
# recomputes the signature from whatever pkg-config happens to find, rewrites
# the stamp, REBUILDS THE WHOLE TREE WITH THE WRONG SWITCHES and stages that
# binary. MEASURED, because this shipped: the lean artifact came out at 832K
# with libcrypt.so.1 and libz.so.1 in its link set and no xml, no sqlite and no
# static zlib, and the switches the tier had carefully named were all ignored.
# Caught by the link-set check below, which is what it is for.
#
# An ARRAY rather than a string, because LIBXML2_LIBS and ZLIB_LIBS each hold
# spaces and a flat \$args would word-split them into separate make arguments.
# GBASIC-MAKE:begin -- the region tests/run_library_tiers.sh reads. A marker
# rather than a regex over the whole file, because the dependency builds above
# run their OWN make and 'make install' legitimately, with none of these
# switches: a tripwire that reports libxml2's install as the defect is one
# somebody turns off.
args=($TIER_MAKE_ARGS $STATIC_XML $STATIC_SQLITE)
args+=(\"LIBXML2_LIBS=$STATIC_XML_LIBS\" 'ZLIB_CFLAGS=' 'ZLIB_LIBS=-Wl,-Bstatic -lz -Wl,-Bdynamic')
make \"\${args[@]}\" >/tmp/build.log 2>&1 || { echo 'FAIL: build'; tail -20 /tmp/build.log; exit 1; }

# Staged with the SAME install target the project ships, so the tarball layout
# and an installed tree cannot drift apart.
make install \"\${args[@]}\" PREFIX='$PREFIX_IN_TARBALL' DESTDIR=/work/stage >/dev/null
# GBASIC-MAKE:end

stage=/work/stage$PREFIX_IN_TARBALL

# THIRD-PARTY NOTICES, AND THE OBLIGATION IS DERIVED FROM THE BINARY.
#
# THIS WAS MISSING AND THE OMISSION WAS MINE. Until static linking landed it
# would have been unnecessary: 'the Linux gbasic links its optional libraries
# dynamically, from the system' is what LICENSING.md said, and a dynamically
# linked binary redistributes nothing. Linking libxml2, zlib and sqlite
# statically, and compiling in the vendored yescrypt, makes every download a
# REDISTRIBUTION of that code -- and libxml2's MIT licence and yescrypt's BSD-2
# each require their notice to travel with a binary. Nothing non-compliant has
# been published: the 0.7.0 archives predate all of it. This is the step that
# makes the three static ones shippable.
#
# WHAT SHIPS IS ASKED OF THE LINKER RATHER THAN OF A LIST. A third-party symbol
# DEFINED in our binary means we carry that code; one left UNDEFINED means the
# system supplies it at load time and we redistribute nothing. That is the line
# the licences draw, so it is the line this draws -- read from the UNSTRIPPED
# binary, before the strip below takes the symbol table away.
#
# AND IT IS CHECKED IN BOTH DIRECTIONS, because each direction fails
# differently and the first draft of this had only one. Deriving alone catches a
# component that gained static linking with no notice; it CANNOT catch a probe
# symbol that is simply wrong, which silently omits a required notice instead.
# That is not hypothetical: the first draft probed 'zlibVersion' and
# 'sqlite3_open', NEITHER OF WHICH gBASIC CALLS -- and a static archive
# contributes only what is referenced, so zlib's notice would have been left out
# in silence. So the expected set is declared as well, and the two must agree.
# Every probe below is one measured to appear in this binary's symbol table.
nm --defined-only /work/gbasic/gbasic | awk '{print \$NF}' | sort -u > /tmp/defined.txt
nm --undefined-only /work/gbasic/gbasic | awk '{print \$NF}' | sed 's/@.*//' | sort -u > /tmp/undefined.txt
have() { grep -qx \"\$1\" /tmp/defined.txt; }
dynamic() { grep -qx \"\$1\" /tmp/undefined.txt; }

notices=\$stage/share/doc/gbasic/THIRD-PARTY-NOTICES.txt
mkdir -p \"\$(dirname \"\$notices\")\"
{
    echo 'gBASIC for Linux -- third-party notices'
    echo '======================================='
    echo
    echo 'gBASIC itself is under the Apache License 2.0; see LICENSE and NOTICE.'
    echo 'The components below are compiled INTO bin/gbasic, so this download is a'
    echo 'redistribution of each, and each is listed with its version and the full'
    echo 'text of its licence. Anything the binary LOADS from the system at run'
    echo 'time is deliberately not here: it is not redistributed, and it carries'
    echo 'its own notice on the machine that provides it.'
    echo
    echo 'Generated by tools/build-release-tarball.sh from the sources it compiled,'
    echo 'for the components whose symbols it found defined in the binary.'
    echo
} > \"\$notices\"

emit() {
    printf -- '-------------------------------------------------------------------------------\n' >> \"\$notices\"
    printf '%s\n' \"\$1\" >> \"\$notices\"
    printf -- '-------------------------------------------------------------------------------\n\n' >> \"\$notices\"
    cat >> \"\$notices\"
    printf '\n\n' >> \"\$notices\"
}

carried=''
missing=''
# libxml2 -- MIT. The licence text comes from the PINNED tarball just compiled,
# not from a copy in this repository, so it describes what actually shipped.
if have xmlCtxtReadMemory; then
    # BOTH SPELLINGS ARE TRIED RATHER THAN ONE GUESSED. Upstream has called this
    # file 'Copyright' historically and distributions package it as 'COPYING';
    # which one a given tarball carries is a fact about that tarball, and the
    # only honest options are to look or to fail naming what was looked for.
    xmltext=''
    for cand in Copyright COPYING LICENSE; do
        if [ -s /tmp/libxml2-${LIBXML2_VERSION}/\$cand ]; then xmltext=/tmp/libxml2-${LIBXML2_VERSION}/\$cand; break; fi
    done
    if [ -n \"\$xmltext\" ]; then
        emit 'libxml2 ${LIBXML2_VERSION}   Licence: MIT' < \"\$xmltext\"
        carried=\"\$carried libxml2\"
    else
        missing=\"\$missing libxml2(no Copyright/COPYING/LICENSE in the tarball)\"
    fi
fi
# zlib -- the distribution's static libz.a. Its terms live in zlib.h's own
# header comment, which zlib1g-dev installs.
if have inflateEnd; then
    zver=\$(sed -n 's/^#define ZLIB_VERSION \"\\(.*\\)\"/\\1/p' /usr/include/zlib.h | head -1)
    ztext=\$(awk '/^  Copyright/,/^\\*\\//' /usr/include/zlib.h | sed 's,^\\*/,,')
    if [ -n \"\$ztext\" ]; then
        printf '%s\n' \"\$ztext\" | emit \"zlib \${zver:-unknown}   Licence: Zlib\"
        carried=\"\$carried zlib\"
    else
        missing=\"\$missing zlib(no-text)\"
    fi
fi
# SQLite -- public domain, so no notice is REQUIRED. Stated anyway, because a
# reader asking what is inside the binary deserves the whole list and not only
# the parts with a legal obligation attached.
if have sqlite3_open_v2; then
    # ANCHORED ON THE SENTENCE, not on line numbers: the amalgamation opens with
    # a comment carrying upstream's dedication, and a line-counted slice would
    # quietly produce the wrong text if that header is ever reformatted. The
    # dedication is also the only part that matters, SQLite being public domain
    # and so requiring no notice at all -- this entry exists because a reader
    # asking what is inside the binary deserves the whole list.
    # FIRST MATCH ONLY, and that correction came from running it: the
    # amalgamation embeds the header of every file it was built from, so a plain
    # awk RANGE matched all ~300 of them and produced 36 KB of the same blessing
    # repeated. A synthetic one-header fixture passed; only the real file showed
    # it.
    #
    # AND THE DIVIDER IS FOUR LITERAL ASTERISKS, NOT AN INTERVAL EXPRESSION.
    # The first fix wrote /^\\*{4,}/, which is correct in gawk -- the dev
    # machine's awk -- and matches NOTHING in mawk, which is what this base
    # image provides. So the exit never fired, the whole 9 MB amalgamation went
    # into the notices file, and EVERY CHECK IN THIS SCRIPT STILL PASSED: the
    # component was carried, so the declared-vs-derived comparison was happy.
    # Measured, not reasoned: mawk matches the literal form and not the
    # interval. Same shape as the libcurl floor defect -- the development
    # machine is the newest and most capable thing in the room, and the RELEASE
    # CONTAINER is the environment that decides. Hence the size bound below.
    sqtext=\$(awk '/disclaims copyright/{f=1} f{print} f && /^\\*\\*\\*\\*/{exit}' /tmp/${SQLITE_ZIP}/sqlite3.c | sed -e 's/^\\*\\*//' -e '/^\\*\\*\\*\\*/d')
    if [ -n \"\$(printf '%s' \"\$sqtext\" | tr -d '[:space:]')\" ]; then
        printf '%s\n' \"\$sqtext\" | emit 'SQLite ${SQLITE_VERSION}   Licence: public domain (no notice required; stated for completeness)'
        carried=\"\$carried sqlite\"
    else
        missing=\"\$missing sqlite(no dedication found in sqlite3.c)\"
    fi
fi
# yescrypt -- BSD-2. The release carries no licence FILE, so the terms are each
# source file's header; quote the header of every file compiled, deduplicated.
# Same rule tools/make-notices.sh uses on Windows, so the two platforms agree.
if have yescrypt_r; then
    yver=\$(sed -n -E 's/^- \\*\\*Version:\\*\\* ([0-9][0-9.]*[0-9]).*/\\1/p' /work/gbasic/third_party/yescrypt/README.md | head -1)
    ytext=\$(for f in yescrypt-opt.c yescrypt-platform.c yescrypt-common.c sha256.c insecure_memzero.c; do
                awk 'NR==1 && !/^\\/\\*/ {exit} {print} /\\*\\// {exit}' /work/gbasic/third_party/yescrypt/\$f; echo
            done | awk 'BEGIN{RS=\"\"; ORS=\"\n\n\"} !seen[\$0]++')
    if [ -n \"\$ytext\" ] && [ -n \"\$yver\" ]; then
        printf '%s\n' \"\$ytext\" | emit \"yescrypt \$yver   Licence: BSD-2-Clause\"
        carried=\"\$carried yescrypt\"
    else
        missing=\"\$missing yescrypt(no-text)\"
    fi
fi

# NO LICENCE TEXT IS MEGABYTES, and this bound exists because a silently
# OVERSIZED notice passed every other check here. The absence guards above ask
# whether a component was carried; nothing asked whether what was carried was
# plausible, and an extraction that runs away produces a file that is complete,
# correct at the top, and 9 MB. A licence is a page or two; 256 KB is generous
# for every component in the table and still catches a runaway by three orders
# of magnitude.
nbytes=\$(wc -c < \"\$notices\")
[ \"\$nbytes\" -le 262144 ] \
    || { echo \"FAIL: THIRD-PARTY-NOTICES.txt is \$nbytes bytes -- no licence text is that long, so an extraction has run away\"; exit 1; }
[ \"\$nbytes\" -ge 1024 ] \
    || { echo \"FAIL: THIRD-PARTY-NOTICES.txt is only \$nbytes bytes -- too short to carry four licences\"; exit 1; }

got_notices=\$(printf '%s\n' \$carried | sort | tr '\n' ' ' | sed 's/ \$//')
want_notices=\$(printf '%s\n' $NOTICE_STATIC | sort | tr '\n' ' ' | sed 's/ \$//')
[ -z \"\$missing\" ] \
    || { echo \"FAIL: compiled into the binary with no licence text to ship:\$missing\"; exit 1; }
[ \"\$got_notices\" = \"\$want_notices\" ] \
    || { echo \"FAIL: the notices carry [\$got_notices] where this tier declares [\$want_notices] -- a missing name means either a component lost its notice or its probe symbol is wrong\"; exit 1; }

# THE CONTROL, and it is what keeps this honest as the tiers change: a
# component the binary LOADS must still be loaded rather than carried. If a
# later change links libcrypto or libcurl statically, a new redistribution
# obligation appears with nothing to announce it. The second branch is the
# VACUITY guard: a symbol that is neither defined NOR undefined means the probe
# name is wrong, so for the tier that really links the library it must be
# present as an undefined reference -- otherwise this loop proves nothing.
for pair in 'EVP_PKEY_free libcrypto.so.3' 'curl_easy_init libcurl.so.4'; do
    sym=\${pair%% *}; soname=\${pair##* }
    if have \"\$sym\"; then
        echo \"FAIL: \$soname is compiled INTO the binary (\$sym is defined), so it is redistributed and needs a notice here\"
        exit 1
    fi
    case ' $EXPECTED_NEEDED ' in
        *\" \$soname \"*)
            dynamic \"\$sym\" || { echo \"FAIL: this tier links \$soname but \$sym is neither defined nor undefined -- the probe name is wrong and this control is vacuous\"; exit 1; } ;;
    esac
done
echo \"notices: \$got_notices\"

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

# THE STATIC SQLITE IS THE ONE THIS SCRIPT ASKED FOR, both ways round.
#
# WHY THIS IS NOT A RESTATEMENT OF THE BUILD: a -D flag the compiler accepted
# proves only that it was PASSED. sqlite reports what it was actually built
# with, so comparing the two catches a flag upstream has RETIRED -- which is
# not hypothetical, it caught -DSQLITE_ENABLE_JSON1 on the first draft of the
# list. Every flag must be reported; there is no exemption list, because an
# exemption is where a dead flag would go to be forgotten.
#
# THE BEHAVIOURAL HALF IS SEPARATE AND BOTH ARE NEEDED: compile_options is
# sqlite's own report about itself, so it cannot tell you the feature WORKS
# through this binary -- and the capability a reader came for is `xlsx`-shaped,
# a real query, not a pragma. So FTS5 is MATCHED against, maths is COMPUTED,
# and json_extract is called because JSON carries no option to assert.
printf 'load sqlite\ndb = sqlite.connect(":memory:")\nfor each o in sqlite.query(db, "pragma compile_options")\n  print o.compile_options\nnext\n' > "$work/sqopts.bas"
sqopts="$( cd "$work" && env -u GBASIC_PATH "$root/bin/gbasic" "$work/sqopts.bas" 2>&1 || true )"
missing=""
for f in $SQLITE_OPTS; do
    t="${f#-DSQLITE_}"
    printf '%s\n' "$sqopts" | grep -qx "$t" || missing="$missing $t"
done
if [ -z "$missing" ] && printf '%s\n' "$sqopts" | grep -q .; then
    ok "static sqlite reports every option this script asked for"
else
    bad "static sqlite is not built as asked -- missing:${missing:- (it reported nothing at all)}"
fi

cat > "$work/sqcap.bas" <<'SQCAP'
load sqlite
db = sqlite.connect(":memory:")
r = sqlite.query(db, "select sqlite_version() as v, sqrt(16) as s, soundex('Robert') as x, json_extract('{\"a\":7}', '$.a') as j")
sqlite.exec(db, "create virtual table ft using fts5(body)")
sqlite.exec(db, "insert into ft(body) values ('statically linked')")
sqlite.exec(db, "create virtual table rt using rtree(id, x0, x1)")
m = sqlite.query(db, "select count(*) as n from ft where body match 'statically'")
print r[0].v + " " + string(r[0].s) + " " + r[0].x + " " + string(r[0].j) + " " + string(m[0].n)
SQCAP
got="$( cd "$work" && env -u GBASIC_PATH "$root/bin/gbasic" "$work/sqcap.bas" 2>&1 || true )"
if [ "$got" = "$SQLITE_VERSION 4 R163 7 1" ]; then
    ok "sqlite $SQLITE_VERSION answers a query, and FTS5, RTREE, maths and JSON all work"
else
    bad "the sqlite capability probe did not answer as expected: $got"
fi

# PASSWORD HASHING, AND THE HALF THAT MATTERS IS INTEROPERABILITY.
#
# "password_hash works" is the easy claim and the uninteresting one: a backend
# that hashed with anything at all would satisfy it, and the result would be a
# download whose hashes nothing else can read. A PASSWORD HASH IS DATA -- made
# on one machine, checked on another, years apart -- so the load-bearing check
# is that this binary verifies a hash LIBXCRYPT ACTUALLY WROTE. The vector is
# the one tests/windows/password_interop.bas carries, written by libxcrypt
# 4.5.1 and independently checked from outside gBASIC by perl's crypt() over
# the system libxcrypt.
#
# AND THE REFUSAL IS ASSERTED, not just the success, because the dangerous
# direction is the quiet one. The vendored backend cannot read the legacy
# crypt(3) schemes, and if a `$6$` hash were answered FALSE the caller would be
# told the PASSWORD was wrong -- locking a real user out with nothing said,
# which is strictly worse than refusing. So a $6$ hash must RAISE and name the
# scheme, and must never come back true or false.
cat > "$work/pwcap.bas" <<'PWCAP'
' A hash libxcrypt wrote, on a machine that has no libxcrypt.
LIBXCRYPT_WROTE = "$y$j9T$fGpSWSCTrdZaMT69NAhOq1$5HH7Q2Qs8VNW63J3cYrSvAiaznpE6uh9Zp/oPFIHGE1"
SHA512CRYPT = "$6$gbasicvector$y.ijOURDub5b/769rTfMphR8BpM7c62qzp9U.YKfrmDnbEemgudN0R9y9LDSFWNDR7o7F179EZa5nYAdwyCsV/"
PW = "correct horse battery staple"

h = password_hash(PW)
out = left(h, 7)
out = out + " " + string(password_verify(PW, h))
out = out + " " + string(password_verify("Correct horse battery staple", h))
out = out + " " + string(password_verify(PW, LIBXCRYPT_WROTE))
out = out + " " + password_hash_cost().prefix

on error goto next
legacy = password_verify(PW, SHA512CRYPT)
if error then
    if contains(error.message, "this one is $6$") then
        out = out + " named"
    else
        out = out + " WRONG-REASON"
    end if
    error.clear()
else
    ' Either answer here is a defect, and they are different defects.
    out = out + " ANSWERED-" + string(legacy)
end if
print out
PWCAP
got="$( cd "$work" && env -u GBASIC_PATH "$root/bin/gbasic" "$work/pwcap.bas" 2>&1 || true )"
if [ "$got" = "\$y\$j9T\$ true false true \$y\$j9T\$ named" ]; then
    ok "password hashing works, reads a hash libxcrypt wrote, and names what it cannot read"
else
    bad "the password probe did not answer as expected: $got"
fi

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

# `has_builtin` TELLS THE TRUTH ABOUT **THIS** ARTIFACT.
#
# THE ONE CHECK run_capabilities.sh SAYS IT CANNOT MAKE. Its own words: "it runs
# against the binary this suite built and the suite builds everything, so the
# case the defect SHIPPED IN -- a build with the dependency absent -- is only
# reachable from a lean compile." This is that compile, so the artifact is the
# only place the invariant can be measured -- and the defect was MEASURED ON THE
# PUBLISHED 0.7.0 ARTIFACTS: has_builtin affirmed `sha256`, `password_hash` and
# `compress` on builds that cannot run any of them, so a program that probed
# before calling crashed on the branch it had checked for, and it DENIED three it
# could run. has_builtin is documented as how a program degrades gracefully
# across releases; on two of three published artifacts it could not answer the
# question it exists for.
#
# THE INVARIANT IS BUILD-AGNOSTIC, which is what lets one rule serve both tiers:
# has_builtin(n) is true EXACTLY WHEN calling n does not refuse for
# unavailability. It fails in BOTH directions, which matters because the two
# defects point opposite ways.
#
# THE TABLE IS READ OUT OF run_capabilities.sh RATHER THAN COPIED, because two
# representations of one list drift and the direction that drifts SILENTLY is a
# builtin added there and not here -- this would go on passing while saying
# nothing about the new name. The row count is floored for the same reason: an
# extraction that breaks must fail rather than sweep nothing.
hb_table="$(sed -n "/^HB_TABLE='/,/^'\$/p" tests/run_capabilities.sh | sed '1d;$d' | grep . || true)"
hb_rows="$(printf '%s\n' "$hb_table" | grep -c . || true)"
hbp="$(mktemp -d)"
if [ "${hb_rows:-0}" -lt 15 ]; then
    bad "read $hb_rows rows out of run_capabilities.sh's HB_TABLE -- expected the optional-builtin list, so this check is measuring nothing"
else
    hb_bad=""
    hb_n=0
    while IFS= read -r row; do
        [ -n "$row" ] || continue
        nm="${row%%:*}"
        hbargs="${row#*:}"
        printf 'on error goto next\nx = %s(%s)\nif error then\n  print error.message\n  error.clear()\nelse\n  print "WORKS"\nend if\n' "$nm" "$hbargs" > "$hbp/call.bas"
        msg="$( cd "$hbp" && env -u GBASIC_PATH "$root/bin/gbasic" call.bas 2>&1 | head -1 )"
        printf 'print string(has_builtin("%s"))\n' "$nm" > "$hbp/says.bas"
        says="$( cd "$hbp" && env -u GBASIC_PATH "$root/bin/gbasic" says.bas 2>&1 | head -1 )"
        hb_n=$((hb_n + 1))
        case "$msg" in
            *'requires OpenSSL'*|*'requires zlib'*|*'not available in this build'*|*'support is unavailable'*|*'rebuild with it installed'*)
                [ "$says" = "true" ] && hb_bad="$hb_bad
       $nm: refuses as unavailable, yet has_builtin says true" ;;
            *'unknown function'*|*'undefined function'*)
                # Not a clean refusal at all, whatever has_builtin says: the name
                # left the dispatch instead of degrading. That is how `compress`
                # failed, its name test having sat inside its own '#if'.
                hb_bad="$hb_bad
       $nm: vanished from the dispatch instead of refusing -- $msg" ;;
            *)
                [ "$says" = "true" ] || hb_bad="$hb_bad
       $nm: works, yet has_builtin says $says" ;;
        esac
    done <<HBROWS
$hb_table
HBROWS
    if [ -z "$(printf '%s' "$hb_bad" | tr -d '[:space:]')" ]; then
        ok "has_builtin agrees with what this artifact can do ($hb_n optional builtins probed)"
    else
        bad "has_builtin disagrees with this artifact:$hb_bad"
    fi
fi
rm -rf "$hbp"

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
