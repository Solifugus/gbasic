#!/usr/bin/env bash
# `stem` -- the one third of path splitting that was really missing, and the
# interior-NUL door the same function left open for four of its six names.
#
# WHAT WAS ACTUALLY ABSENT. docs/etl_files_design.md §1 listed path splitting --
# "basename/dirname/stem" -- among the gaps for the ETL book. Measured, two
# thirds of it had been shipped for months under other names: `file_name` is
# basename, `directory_name` is dirname, `extension` is the suffix. Only `stem`
# did not exist. That is the SECOND item in that one list to turn out already
# present (`modified`, which is `file_mtime`, was the first), and both were
# found by RUNNING the tree rather than by reading it.
#
# Tiers:
#   SEMANTICS  the self-checking fixture. Self-checking rather than golden and
#              here it is forced: every defect `stem` can have returns an
#              ORDINARY FILENAME. The first implementation answered `stem("..")`
#              = "." and `stem("a.")` = "a", both of which a golden records as
#              expected and then defends.
#   ORACLE     python3's `posixpath.splitext` recomputing the SAME battery --
#              an independent implementation, which is what catches a cut that
#              is self-consistently wrong where the fixture states values that
#              the same hands wrote. 17 of the 18 agree; THE ONE THAT DIFFERS
#              IS ASSERTED BY NAME WITH ITS REASON, so the divergence can
#              neither grow nor quietly vanish.
#   REFUSAL    the NUL refusal UNCAUGHT: located, nonzero, and nothing after it
#              ran. The fixture proves it is catchable; this proves what an
#              ordinary program sees.
#   MIGRATION  `edgar.bas` hand-rolled `directory_name` as a 14-line segment
#              walk. The builtin replaces it and the GOLDEN MUST NOT MOVE,
#              which is the only thing that says the replacement cuts the same
#              way rather than merely compiling.
#   VALGRIND   `stem` is a new allocation path, and the NUL check moved.
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -euo pipefail

cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh
make >/dev/null
export GBASIC_PATH=stdlib

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

printf 'TIER semantics: the invariant, the awkward names, the refusals\n'
if ! timeout -k 5 60 ./gbasic tests/path_split_test.bas >"$work/out" 2>"$work/err"; then
    cat "$work/err"; fail "the fixture did not run"
elif grep -q 'MISMATCH\|BROKEN' "$work/out"; then
    grep 'MISMATCH\|BROKEN' "$work/out"; fail "the fixture disagreed with itself"
elif ! grep -qx 'mismatches: 0' "$work/out"; then
    fail "the fixture did not finish"
else
    n=$(sed -n 's/^checks: //p' "$work/out")
    if [ -z "$n" ] || [ "$n" -lt 30 ]; then
        fail "only ${n:-0} checks ran, wanted at least 30"
    else
        pass "$n checks"
    fi
fi

printf 'TIER oracle: an independent implementation cuts the same battery\n'
# The battery is dumped by the fixture, so there is ONE copy of it and the
# oracle cannot drift onto an easier set of names. python keeps the DOT in its
# extension where gBASIC's `extension` is documented as dotless, so the
# comparison strips python's leading dot -- and that difference of convention
# is exactly what forces our only divergence, below.
if ! command -v python3 >/dev/null 2>&1; then
    pass "SKIP (python3 unavailable -- NOTHING was cross-checked against an outside implementation)"
else
    grep '^BATTERY|' "$work/out" > "$work/battery" || true
    lines=$(wc -l < "$work/battery")
    if [ "$lines" -lt 15 ]; then
        fail "the fixture dumped only $lines battery lines -- the oracle measured nothing"
    else
        python3 - "$work/battery" > "$work/oracle" <<'PY'
import posixpath, sys
agree = 0
diffs = []
for raw in open(sys.argv[1], encoding='utf-8'):
    _, path, name, stem, ext = raw.rstrip('\n').split('|', 4)
    # python's basename does not tolerate a trailing slash, which gBASIC's
    # file_name does, so the oracle strips them the way the builtin documents.
    trimmed = path
    while len(trimmed) > 1 and trimmed.endswith('/'):
        trimmed = trimmed[:-1]
    p_name = posixpath.basename(trimmed) if trimmed != '/' else ''
    p_stem, p_ext = posixpath.splitext(p_name)
    p_ext = p_ext[1:] if p_ext.startswith('.') else p_ext
    if (p_name, p_stem, p_ext) == (name, stem, ext):
        agree += 1
    else:
        diffs.append(f"{path}: gbasic({name!r},{stem!r},{ext!r}) python({p_name!r},{p_stem!r},{p_ext!r})")
print(f"agree {agree}")
for d in diffs:
    print("differ " + d)
PY
        agree=$(sed -n 's/^agree //p' "$work/oracle")
        ndiff=$(grep -c '^differ ' "$work/oracle" || true)
        if [ "$agree" -lt 15 ]; then
            cat "$work/oracle"; fail "only $agree of $lines agreed with posixpath"
        else
            pass "$agree of $lines agree with python's posixpath.splitext"
        fi
        # THE ONE DIVERGENCE, PINNED. `extension` reports the suffix WITHOUT
        # the dot, so an extension that is ONLY a dot has no representation and
        # "" is the only honest answer -- at which point `stem` must keep the
        # dot or the two disagree about the boundary. python can answer
        # ext="." because it keeps the dot, and its own answer is what shows
        # the alternative convention. Asserted by name so that a change of
        # convention has to come here and say so.
        if [ "$ndiff" = "1" ] && grep -q "^differ a.: gbasic('a.','a.','') python('a.','a','')" "$work/oracle"; then
            pass "the single divergence is the trailing dot, and it is the dotless-extension rule"
        elif [ "$ndiff" = "0" ]; then
            fail "the trailing-dot divergence is GONE -- either extension now keeps the dot or the oracle stopped asking"
        else
            grep '^differ ' "$work/oracle"
            fail "$ndiff divergences from posixpath, wanted exactly the one"
        fi
    fi
fi

printf 'TIER refusal: what an ordinary program sees when it does not catch it\n'
# The fixture proves the refusal is CATCHABLE. This proves the other half: a
# program that does not arm `on error` is told where, told why, and stopped --
# the three things run_silent_traps.sh established for this family, and the
# reason a bare printed line is not an acceptable answer.
printf 'p = "/a/b" + chr(0) + "x/c.txt"\nprint "before"\nn = stem(p)\nprint "after " + n\n' > "$work/nul.bas"
set +e
out=$(./gbasic "$work/nul.bas" 2>&1); rc=$?
set -e
case "$out" in
    *"runtime error at"*"stem: a path cannot contain an interior NUL byte"*) pass "located and named" ;;
    *) printf '%s\n' "$out"; fail "the refusal was not located or not named" ;;
esac
[ "$rc" != "0" ] && pass "nonzero exit ($rc)" || fail "exited 0 over a refused path"
printf '%s\n' "$out" | grep -q '^after ' && fail "execution continued past the refusal" || pass "nothing after it ran"
printf '%s\n' "$out" | grep -qx 'before' && pass "and what came before it did" || fail "the statement before the refusal did not run"

printf 'TIER migration: edgar stops hand-rolling directory_name\n'
# The hand-rolled walk is gone from the library; its goldens are what say the
# builtin cuts the same way, and they are asserted by run_examples/run_edgar
# rather than restated here. What this tier can see is that the walk has not
# come back: dead code that still works is how a retired construct returns.
if grep -q '_parent_dir' stdlib/edgar.bas; then
    if grep -A3 'function _parent_dir' stdlib/edgar.bas | grep -q 'directory_name'; then
        pass "_parent_dir delegates to the builtin"
    else
        fail "_parent_dir still hand-rolls the split"
    fi
else
    pass "_parent_dir is gone entirely"
fi

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/path_split_test.bas >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access"
    else
        tail -30 "$work/vg.err"; fail "valgrind objected"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

exit $status
