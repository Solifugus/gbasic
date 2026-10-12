#!/usr/bin/env bash
# WHICH LIBRARIES THE BINARY BESIDE THEM CAN ACTUALLY RUN.
#
# THE DEFECT THIS EXISTS FOR, MEASURED ON A PUBLISHED ARTIFACT. Of the 65
# libraries in the 0.7.0 `-full` tarball, 13 cannot be `load`ed by the binary
# shipped next to them (21 on the lean one): seven need libxml2, four need
# GObject-Introspection, one unixODBC, one libpq. The files are there; four
# subsystems were compiled out of the binary beside them, for reasons
# tools/build-release-tarball.sh argues and measures and which are SOUND --
# linking libxml2 produces a download that will not start at all on part of the
# range it promises.
#
# SO THE DEFECT IS NOT THE BUILD. It is that nothing in the tree connected the
# two, and a reader following the library catalogue to `load ownership` met a
# runtime error on line 1 with nothing wrong with what they typed. THIS IS THE
# THIRD INSTANCE OF THAT CLASS -- a stale version pin, then the GUI libraries,
# then this -- each reported from outside, each "the files are in the archive
# and the binary cannot run them", and nothing ever asked the question
# mechanically.
#
# THE ANSWER IS DERIVED, NOT LISTED. Every library already states what it needs,
# in its own `load` lines; `tools/library-needs.sh` resolves those transitively
# and the reference's table must agree with it in BOTH directions. A hand-kept
# table would be a second statement of a fact the source already carries, which
# is the drift this suite exists to prevent rather than to re-create.
#
# TRANSITIVITY IS THE HALF A GREP MISSES, and it is most of the interesting
# rows: `finio_all` contains no XML of its own and cannot load without libxml2,
# because it loads `finio_camt` which loads `xml`. A one-level scan reports it
# as needing nothing.
#
# WHAT THIS SUITE CANNOT DO, AND SAYS SO: it cannot MEASURE the claim, because
# measuring it means building the interpreter several ways and this machine's
# build has every module in it. The measured half lives in
# tools/build-release-tarball.sh, which has both tiers in front of it already
# and now refuses to publish an archive whose unloadable set is not exactly
# what this derivation predicts. The two halves were checked against each other
# by hand first: the derivation says 21 libraries need something, and 21 is
# exactly what fails on the published lean archive, library for library.
#
# Headless, no network, never skips.
. "$(dirname "$0")/portable.sh"
set -euo pipefail

cd "$(dirname "$0")/.."
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

REF="docs/reference.md"

# ---------------------------------------------------------------------------
printf 'TIER the derivation runs and is not vacuous\n'
./tools/library-needs.sh > "$work/derived" 2>"$work/derr" || { cat "$work/derr"; fail "the derivation did not run"; }
total=$(wc -l < "$work/derived" | tr -d ' ')
withdep=$(awk 'NF>1' "$work/derived" | wc -l | tr -d ' ')
libcount=$(ls stdlib/*.bas | wc -l | tr -d ' ')
if [ "$total" != "$libcount" ]; then
    fail "the derivation reported $total libraries, stdlib holds $libcount"
elif [ "$withdep" -lt 15 ]; then
    # A FLOOR, because a derivation that silently stopped recognising `load`
    # lines would report every library as dependency-free and this whole suite
    # would agree with an empty table. 21 today; a build that removed a module
    # could legitimately lower it, not to single figures.
    fail "only $withdep libraries were found to need anything, wanted at least 15 -- the derivation has probably stopped reading `load` lines"
else
    pass "$total libraries, $withdep needing an optional dependency"
fi

# TRANSITIVITY, asserted on a case that proves it rather than on the mechanism:
# `finio_all` has no `load xml` of its own.
if grep -q '^load xml' stdlib/finio_all.bas; then
    fail "finio_all now loads xml directly, so it no longer tests transitivity -- pick another case"
elif grep -qx 'finio_all libxml2' "$work/derived"; then
    pass "transitivity: finio_all needs libxml2 through finio_camt, not directly"
else
    fail "transitivity is not being resolved: finio_all should need libxml2"
fi

# AND A COMMENT IS NOT A DEPENDENCY. `rules.bas` names `ldap.bind` in its header
# prose, arguing about reporting failure as a value; reading that as a `load`
# would report a pure-gBASIC library as needing OpenLDAP.
if ! grep -q 'ldap' stdlib/rules.bas; then
    fail "rules.bas no longer mentions ldap in prose, so the comment case is untested -- pick another"
elif grep -qx 'rules' "$work/derived"; then
    pass "a module named only in a comment is not a dependency"
else
    fail "a comment is being read as a dependency: rules needs nothing"
fi

# ---------------------------------------------------------------------------
# EVERY OPTIONAL MODULE MUST APPEAR IN THE MAPPING. Without this, adding a new
# optional module leaves its libraries silently dependency-free -- the exact
# shape of the defect, one level up.
# READ FROM THE AUTHORITATIVE LIST, which is `eval_module_needs_load`'s
# `loadable[]` in src/eval.c -- the native qualifiers a `load` must name. The
# first version of this tier instead scanned for every "support is not
# available" message, and reported five capabilities as unmapped of which THREE
# ARE NOT REACHABLE BY `load` AT ALL (`Password hashing` and `xlsx` are
# builtins, `row-model` is internal to datagrid). A tripwire whose false
# positives look exactly like its true ones is one somebody turns off.
printf 'TIER every module a `load` can name is mapped\n'
modules=$(sed -n '/int eval_module_needs_load/,/NULL$/p' src/eval.c \
          | grep -oE '"[a-z]+"' | tr -d '"' | sort -u)
missing=""
for m in $modules; do
    grep -q "dep == \"$m\"" tools/library-needs.sh || missing="$missing $m"
done
if [ -z "$modules" ]; then
    fail "read no module names out of eval_module_needs_load -- the scan is broken, not the mapping"
elif [ "$(echo "$modules" | wc -w | tr -d ' ')" -lt 8 ]; then
    fail "read only $(echo $modules) out of eval_module_needs_load -- too few to be the real list"
elif [ -n "$missing" ]; then
    fail "these loadable modules are not in tools/library-needs.sh:$missing"
else
    pass "all $(echo "$modules" | wc -w | tr -d ' ') loadable modules mapped: $(echo $modules)"
fi

# THE CONTROL on that tier: an unmapped module must be REPORTED. Without it
# "all mapped" is equally satisfied by a scan that read nothing, which is
# exactly what the first draft did.
if grep -q 'dep == "zzznope"' tools/library-needs.sh; then
    fail "the control name is actually in the mapping -- pick another"
else
    pass "and a module absent from the mapping would be named"
fi

# ---------------------------------------------------------------------------
printf 'TIER the reference table agrees with the derivation, both directions\n'
# The table rows: | `lib` | Caps, Caps |
# THE TABLES ARE MARKER-DELIMITED, which is the third shape of the same bug and
# the reason to stop using prose as a boundary. First the range ran to
# `General-purpose:`, far below, and adding the call-time table between the two
# swept its rows into the load-time comparison. Then it ran to a heading, and
# rewording the paragraph after the table broke it again. `<!--LIBNEEDS:loads-->`
# to `<!--LIBNEEDS:end-->` is a region an edit to the surrounding words cannot
# move -- the same device `tools/sync_xlsx_cookbook.sh` uses on the cookbook
# pages, for the same reason.
sed -n '/^<!--LIBNEEDS:loads-->/,/^<!--LIBNEEDS:end-->/p' "$REF" \
  | awk -F'|' '/^\| `/ { gsub(/[` ]/, "", $2); gsub(/^ +| +$/, "", $3); gsub(/, /, ",", $3); print $2, $3 }' \
  | sort > "$work/declared"
awk 'NF>1' "$work/derived" > "$work/derived_dep"

if [ "$(command grep -c '^<!--LIBNEEDS:' "$REF")" != "4" ]; then
    fail "expected 4 LIBNEEDS markers in $REF, found $(command grep -c '^<!--LIBNEEDS:' "$REF") -- a missing marker makes both comparisons read an empty region"
fi
if [ ! -s "$work/declared" ]; then
    fail "no table rows found in $REF -- the section or its format has moved"
else
    if diff -u "$work/derived_dep" "$work/declared" > "$work/diff"; then
        pass "$(wc -l < "$work/declared" | tr -d ' ') rows, identical to the derivation"
    else
        sed -n '1,20p' "$work/diff" | sed 's/^/     /'
        fail "the reference table and the derivation disagree -- regenerate the table, do not edit the derivation to match"
    fi
fi

# ---------------------------------------------------------------------------
# THE CONTROL. Every tier above is also satisfied by a comparison of two empty
# things, or by a derivation that happens to agree with a table because both
# are wrong in the same way. So the comparison is perturbed: a planted
# disagreement must be caught, in BOTH directions.
printf 'TIER the comparison can fail\n'
cp "$work/derived_dep" "$work/p1"
printf 'zzz_invented libxml2\n' >> "$work/p1"
if diff -q "$work/p1" "$work/declared" >/dev/null 2>&1; then
    fail "an added row did not change the comparison -- it is measuring nothing"
else
    pass "a library the table does not list is caught"
fi
sed '1d' "$work/declared" > "$work/p2"
if diff -q "$work/derived_dep" "$work/p2" >/dev/null 2>&1; then
    fail "a removed row did not change the comparison"
else
    pass "a library the derivation does not produce is caught"
fi
sed 's/ libxml2$/ libsqlite3/' "$work/declared" > "$work/p3"
if diff -q "$work/derived_dep" "$work/p3" >/dev/null 2>&1; then
    fail "a changed capability did not change the comparison"
else
    pass "a row whose dependency is wrong is caught"
fi

# ---------------------------------------------------------------------------
# THE OTHER HALF, AND THE WORSE ONE: a library that LOADS and whose functions
# then fail. Measured on the published lean archive -- `load crypto` and
# `load otp` both succeed, and every function of either answers *HMAC requires
# OpenSSL*. These call a BUILTIN whose availability moves with the build, so no
# amount of reading `load` lines finds them, and the books session's report did
# not reach them either.
printf 'TIER the libraries that load and then fail\n'
./tools/library-needs.sh --calls > "$work/calls" 2>"$work/cerr" || { cat "$work/cerr"; fail "the call-time derivation did not run"; }
awk 'NF>1' "$work/calls" | sort > "$work/calls_dep"
n_calls=$(wc -l < "$work/calls_dep" | tr -d ' ')
if [ "$n_calls" -lt 1 ]; then
    # A FLOOR OF ONE, because `crypto` cannot stop needing libcrypto while
    # remaining `crypto`: a zero here means the scan broke, not that the tree
    # improved.
    fail "no library was found to need a build-dependent builtin -- the scan is broken (crypto at least must)"
elif ! grep -qx 'crypto libcrypto' "$work/calls_dep"; then
    fail "crypto is not reported as needing libcrypto -- the scan is not finding builtin uses"
else
    pass "$n_calls libraries load anywhere and need something at call time"
fi

sed -n '/^<!--LIBNEEDS:calls-->/,/^<!--LIBNEEDS:end-->/p' "$REF" \
  | awk -F'|' '/^\| `/ { gsub(/[` ]/, "", $2); gsub(/^ +| +$/, "", $3); gsub(/, /, ",", $3); print $2, $3 }' \
  | sort > "$work/calls_declared"
if [ ! -s "$work/calls_declared" ]; then
    fail "no call-time table rows found in $REF -- the section or its format has moved"
elif diff -u "$work/calls_dep" "$work/calls_declared" > "$work/cdiff"; then
    pass "$(wc -l < "$work/calls_declared" | tr -d ' ') rows, identical to the derivation"
else
    sed -n '1,12p' "$work/cdiff" | sed 's/^/     /'
    fail "the call-time table and the derivation disagree"
fi

# A COMMENT IS NOT A USE, here too: this file's own argument about `sha256`
# must not make the reference list every library that mentions one.
if grep -qx 'rules' "$work/calls"; then
    pass "a builtin named only in a comment is not a call-time dependency"
else
    fail "rules is reported as needing a build-dependent builtin; it calls none"
fi

# ---------------------------------------------------------------------------
# THE RELEASE SCRIPT MUST USE THIS DERIVATION, or the measured half drifts from
# the declared half and the whole arrangement is two unrelated checks again.
printf 'TIER the release build reads the same derivation\n'
if grep -q 'library-needs.sh' tools/build-release-tarball.sh; then
    pass "tools/build-release-tarball.sh runs tools/library-needs.sh"
else
    fail "the release build does not use the derivation, so nothing measures the claim"
fi

# ---------------------------------------------------------------------------
# EVERY make IN THE RELEASE BUILD MUST CARRY THE SAME SWITCHES, and this tier
# exists because the opposite SHIPPED in a0514d0.
#
# `install` depends on `gbasic`, and since the .buildswitch-stamp landed every
# object depends on the switch signature -- so `make install` without the
# tier's switches recomputes that signature from whatever pkg-config finds,
# rewrites the stamp, REBUILDS THE WHOLE TREE WITH THE WRONG SWITCHES and then
# stages that binary. Measured: a lean artifact at 832K carrying libcrypt.so.1
# and libz.so.1, with no xml, no sqlite and no static zlib, from a script whose
# tier definition named every one of those switches correctly.
#
# THE TWO HALVES WERE EACH VERIFIED AND THE COMBINATION WAS NOT: the stamp and
# the static link went in as ONE COMMIT, the release measurements were taken
# before the stamp existed, and no gate builds a release tarball -- so nothing
# ran them together. This is the cheap structural stand-in for that: it reads
# the source rather than building, because a tier that compiles eval.c twice is
# one nobody runs.
printf 'TIER every make in the release build carries the tier switches
'
rel=tools/build-release-tarball.sh
# MARKER-DELIMITED, not a regex over the file: the dependency builds above run
# their own `make` and `make install` with none of these switches, and the
# first draft of this tier reported libxml2's install as the defect.
nmark=$(grep -c 'GBASIC-MAKE:\(begin\|end\)' "$rel" || true)
region="$(sed -n '/GBASIC-MAKE:begin/,/GBASIC-MAKE:end/p' "$rel")"
mk="$(printf '%s\n' "$region" | grep -nE '^make( |$)' || true)"
bare="$(printf '%s\n' "$mk" | grep -v 'args\[@\]' | grep -v 'make clean' || true)"
if [ "${nmark:-0}" -ne 2 ]; then
    fail "expected both GBASIC-MAKE markers in $rel, found $nmark -- the region would read as empty"
elif [ -z "$(printf '%s' "$mk" | tr -d '[:space:]')" ]; then
    fail "no make invocation inside the marked region -- this tier has stopped reading anything"
elif [ -z "$(printf '%s' "$bare" | tr -d '[:space:]')" ]; then
    pass "every make invocation uses the shared switch list"
else
    fail "a make invocation in $rel does not carry the tier switches:"
    printf '%s\n' "$bare" | sed 's/^/       /'
fi

# THE CONTROL, without which the check above is satisfied by a script that has
# stopped invoking make at all: the build and the install must BOTH be there,
# and both must use the list.
n_shared="$(printf '%s\n' "$region" | grep -cE '^make .*args\[@\]' || true)"
if [ "${n_shared:-0}" -ge 2 ] && printf '%s\n' "$region" | grep -qE '^make install .*args\[@\]'; then
    pass "the build and the install are both present and both use it"
else
    fail "expected a build AND an install sharing the switch list; found $n_shared line(s)"
fi

# ---------------------------------------------------------------------------
# NO UNESCAPED BACKTICK IN THE CONTAINER SCRIPT, which is a tripwire rather
# than a style rule because the release build runs what it finds there.
#
# The container script is a DOUBLE-QUOTED HOST STRING, so a backtick inside it
# is command substitution the HOST performs before podman ever starts. That has
# now happened THREE TIMES: once quoting 'cp -r' (executed on every release
# build, harmless by luck), once in the very comment written to document the
# fix, and once in a comment of mine quoting 'make install' -- which ran
# `make install` ON THE HOST with no DESTDIR, i.e. a full host build followed by
# an attempt to write into /usr/local/bin. It failed only because the release
# is not built as root; as root it would have overwritten the system install as
# a side effect of a COMMENT, and the script would have said nothing.
#
# The existing guard was a paragraph saying "single quotes only, in every
# comment from here to the end of the string". It was correct, it was fifteen
# lines away, and it did not stop the third instance -- which is the argument
# for checking it mechanically instead.
printf 'TIER the container script has no unescaped backtick\n'
cstr="$(awk '/bash -euo pipefail -c "/,/^"$/' "$rel")"
if [ -z "$(printf '%s' "$cstr" | tr -d '[:space:]')" ]; then
    fail "read no container script out of $rel -- this tier is measuring nothing"
else
    loose="$(printf '%s\n' "$cstr" | grep -n '`' | grep -v '\\`' || true)"
    if [ -z "$(printf '%s' "$loose" | tr -d '[:space:]')" ]; then
        pass "every backtick in the container script is escaped"
    else
        fail "an unescaped backtick in the container script -- the HOST will run it:"
        printf '%s\n' "$loose" | sed 's/^/       /'
    fi
fi

# ---------------------------------------------------------------------------
# EVERY TIER DECLARES A PASSWORD BACKEND, which exists because the opposite
# shipped: `password_hash` and `password_verify` were in NEITHER published
# download, and the reason was never a decision about password hashing. Both
# tiers set LIBXCRYPT_AVAILABLE=0 because libxcrypt's soname is `.so.1` on the
# Debian family and `.so.2` on the RHEL family, and a DT_NEEDED on either is a
# binary that does not start on the other half of the range. Correct, and it
# took two builtins with it silently.
#
# The rule is DERIVED rather than a literal: a tier that turns libxcrypt OFF
# must turn the vendored yescrypt ON, or it has no `crypt(3)` at all. A new
# tier then fails here, naming itself, rather than quietly shipping without
# password hashing -- the same argument run_packaging.sh makes about a switch
# nobody decided either way.
printf 'TIER every release tier declares a password backend\n'
# Continuations joined first: TIER_MAKE_ARGS spans three lines.
joined="$(sed -e :a -e '/\\$/N; s/\\\n//; ta' "$rel")"
targs="$(printf '%s\n' "$joined" | grep -E '^[[:space:]]*TIER_MAKE_ARGS=' || true)"
n_tiers="$(printf '%s\n' "$targs" | grep -c . || true)"
if [ "${n_tiers:-0}" -lt 2 ]; then
    fail "read $n_tiers tier argument list(s) out of $rel -- expected one per tier, so this tier is measuring nothing"
else
    # EVERY TIER MUST NAME A BACKEND, and a tier naming NEITHER switch fails
    # too. That third case is not pedantry: an unnamed LIBXCRYPT_AVAILABLE is
    # decided by whatever pkg-config finds on the build machine, which is
    # precisely how libxcrypt got into an earlier measurement WITHOUT BEING
    # ASKED FOR -- the release script says so in its own header. "Whatever was
    # installed" is not a decision anybody made.
    undeclared=""
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        case "$line" in
            *YESCRYPT_VENDORED=1*|*LIBXCRYPT_AVAILABLE=1*) ;;
            *LIBXCRYPT_AVAILABLE=0*) undeclared="$undeclared
       libxcrypt off, no replacement named: $line" ;;
            *) undeclared="$undeclared
       names neither switch, so the backend comes from the build machine: $line" ;;
        esac
    done <<EOF
$targs
EOF
    if [ -z "$(printf '%s' "$undeclared" | tr -d '[:space:]')" ]; then
        pass "every tier names a password backend ($n_tiers tiers read)"
    else
        fail "a tier would ship with no password hashing, or with whatever the build machine had:$undeclared"
    fi
fi

# AND THE BEHAVIOURAL CHECK MUST STILL BE THERE. The declaration above says
# what was asked for; only the probe says the artifact does it -- and the half
# that matters is reading a hash LIBXCRYPT WROTE, since a backend hashing with
# anything at all satisfies "password_hash works" while producing a download
# whose hashes nothing else can read.
if grep -q 'LIBXCRYPT_WROTE' "$rel" && grep -q 'this one is \$6\$' "$rel"; then
    pass "the release build still proves interoperability and names what it cannot read"
else
    fail "the password probe is gone from $rel -- the tier declaration alone does not measure the artifact"
fi

# THE THIRD-PARTY NOTICES ARE BOUNDED, and this guards a defect that SHIPPED
# PAST EVERY OTHER CHECK. Static linking makes a download a redistribution of
# libxml2 (MIT) and yescrypt (BSD-2), both of which require their notice to
# travel with the binary, so the release build generates one and refuses to
# publish without it. The absence guards there ask WHETHER each component was
# carried. Nothing asked whether what was carried was PLAUSIBLE -- and an
# extraction that runs away yields a file that is complete, correct at the top,
# and 9 MB. Measured: an interval expression (`{4,}`) in the SQLite extraction
# is correct in gawk, which this machine has, and matches NOTHING in mawk,
# which the ubuntu:22.04 release image provides, so the whole amalgamation went
# into the notices file and every check in the release script passed.
#
# So the bound is asserted to EXIST, both ends of it, since a bound nobody
# checks is one a later edit removes silently.
if grep -q 'no licence text is that long' "$rel" && grep -q 'too short to carry four licences' "$rel"; then
    pass "the notices file is bounded at both ends, so a runaway extraction cannot ship"
else
    fail "the size bound on THIRD-PARTY-NOTICES.txt is gone from $rel -- an absence guard is not a plausibility guard"
fi

# AND THE RELEASE BUILD MEASURES `has_builtin` ON THE ARTIFACT, which is the one
# check run_capabilities.sh states it cannot make: it runs against the binary
# this suite built, and this suite builds everything, so a build with the
# dependency ABSENT -- the configuration the defect shipped in -- is only
# reachable from a lean compile. The release container is that compile.
#
# THE TABLE MUST BE DERIVED, NOT COPIED. Two representations of one list drift,
# and the direction that drifts silently is a builtin added to
# run_capabilities.sh and not to the release script: the sweep would go on
# passing while saying nothing about the new name. So the release script is
# required to read `HB_TABLE` out of run_capabilities.sh by name.
if grep -q "HB_TABLE" "$rel" && grep -q "run_capabilities.sh" "$rel" \
   && grep -q 'has_builtin agrees with what this artifact' "$rel"; then
    pass "the release build measures has_builtin on the artifact, from run_capabilities.sh's own table"
else
    fail "the artifact's has_builtin sweep is gone from $rel, or no longer derives its table from run_capabilities.sh"
fi
# The table it derives from must still be there to derive, and big enough to mean
# something -- without this the grep above is satisfied by a mention.
hb_n="$(sed -n "/^HB_TABLE='/,/^'\$/p" tests/run_capabilities.sh | sed '1d;$d' | grep -c . || true)"
if [ "${hb_n:-0}" -ge 15 ]; then
    pass "run_capabilities.sh still holds the optional-builtin table ($hb_n rows)"
else
    fail "run_capabilities.sh's HB_TABLE reads as $hb_n rows -- the release build's sweep would measure nothing"
fi

# ---------------------------------------------------------------------------
# EVERY NAME IN BUILD_SWITCH_SIG IS A SWITCH THAT EXISTS.
#
# That signature is what makes changing a switch rebuild the tree, so a name in
# it that is assigned NOWHERE expands to the empty string and tracks nothing --
# while reading exactly like the live names beside it. Measured 2026-10-11: it
# carried $(YESCRYPT_AVAILABLE), assigned nowhere, giving
# `1|1|0|1|1|1|1|1|1|1|1|1|1||0` with an empty field in the middle. Same class
# as the -DSQLITE_ENABLE_JSON1 flag found retired upstream, and the reason
# there is no exemption list in either place: a dead entry answers "is that
# tracked?" with a yes.
#
# run_packaging.sh's switch-list check structurally cannot see this -- it greps
# `^[A-Z0-9_]+_AVAILABLE` from COLUMN ONE, and a name used only inside this one
# variable never appears there. Hence a check here, in a suite that never
# skips, rather than beside it in one that needs dpkg-deb.
printf 'TIER every name in BUILD_SWITCH_SIG is an assigned switch\n'
sig="$(grep -E '^BUILD_SWITCH_SIG[[:space:]]*:?=' Makefile || true)"
# `|| true` is not decoration: with $sig empty this grep matches nothing and
# exits 1, pipefail gives the pipeline that status, and `set -e` ends the suite
# SILENTLY -- printing the tier header and then nothing, with no FAIL line in a
# scrollback that looks green. Measured while perturbing this very tier, which
# is the fourth instance of that trap here (run_ldap's skip probe, run_web_tls's,
# run_rules' scanner) and the first one inside a VACUITY GUARD -- the branch
# whose whole job is to say "this measured nothing", and the one branch nobody
# ever watches work.
names="$(printf '%s\n' "$sig" | grep -oE '\$\([A-Z0-9_]+\)' | tr -d '$()' | sort -u || true)"
n_names="$(printf '%s\n' "$names" | grep -c . || true)"
if [ "${n_names:-0}" -lt 10 ]; then
    fail "read $n_names name(s) out of BUILD_SWITCH_SIG -- expected the full switch list, so this tier is measuring nothing"
else
    dead=""
    for n in $names; do
        grep -qE "^[[:space:]]*$n[[:space:]]*[:?+]?=" Makefile || dead="$dead $n"
    done
    if [ -z "$dead" ]; then
        pass "all $n_names names in the build signature are assigned somewhere in the Makefile"
    else
        fail "BUILD_SWITCH_SIG names a switch the Makefile never assigns, so it tracks nothing:$dead"
    fi
fi

[ "$status" -ne 0 ] && exit 1
printf 'PASS tests/run_library_tiers.sh\n'
