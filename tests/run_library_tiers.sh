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
# THE RANGE ENDS AT THE NEXT HEADING, not at `General-purpose:`. It used to run
# to the latter, and adding the call-time table BETWEEN the two swept its three
# rows into the load-time comparison -- which failed loudly rather than quietly,
# but a range bounded by something far away is a range that will do it again.
sed -n '/^### Which libraries need something from the build/,/^### And three that load/p' "$REF" \
  | awk -F'|' '/^\| `/ { gsub(/[` ]/, "", $2); gsub(/^ +| +$/, "", $3); gsub(/, /, ",", $3); print $2, $3 }' \
  | sort > "$work/declared"
awk 'NF>1' "$work/derived" > "$work/derived_dep"

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

sed -n '/^### And three that load, then fail/,/^\*\*What the published downloads carry/p' "$REF" \
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

[ "$status" -ne 0 ] && exit 1
printf 'PASS tests/run_library_tiers.sh\n'
