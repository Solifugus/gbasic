#!/usr/bin/env bash
# `key(a, b, ...)` -- ONE STRING THAT CANNOT COLLIDE, for a composite record key.
#
# WHY IT EXISTS: a gBASIC record is keyed by a STRING, so a composite key gets
# built by concatenation with a chosen separator -- and that is wrong in two
# ways nothing reports. MEASURED across stdlib before this shipped:
#
#   stdlib/insight.bas      key = key + string(r[d]) + "|"
#   stdlib/fundamentals.bas k = r["start"] + "|" + r["end"] + "|" + r["fp"]
#   stdlib/ari.bas          k = gen + " :: " + d.reason
#   stdlib/finio.bas        key = key + "/" + string(c.revision)
#
# ONE OF THE FOUR WAS A LIVE DEFECT and it was demonstrated, not reasoned
# about: in `insight` the cells ("North|East", "A") and ("North", "East|A")
# produced the IDENTICAL key, and a decomposition over eight distinct cells
# reported SEVEN. That is worse than a wrong grouping, because `search.cells`
# feeds the Bonferroni threshold -- so the merge corrupted the statistic AND
# the width it was judged against, in the library whose whole job is deciding
# whether a deviation is real. The other three were checked for reachability
# rather than migrated reflexively: ISO dates and adapter names contain no
# separator, so only `fundamentals` (which had the history, plus a residual
# where an absent `fp` and a genuinely empty one became one key) was moved too.
#
# THE ENCODING IS INJECTIVE BY CONSTRUCTION, not by picking a rarer separator
# -- a rarer separator is the same defect with a longer fuse. Each component is
# `<kind><bytelen>:<bytes>`, so decoding is "read the kind, read digits to the
# colon, take exactly that many bytes". THE KIND TAG IS NOT DECORATION:
# without it `key(true)` and `key("true")` both encode as `4:true`.
#
# SELF-CHECKING, NOT GOLDEN, and forced: every defect here is TWO KEYS THAT
# LOOK FINE AND ARE EQUAL, so a golden would pin a collision as expected --
# which is exactly how this survived in two libraries.
. "$(dirname "$0")/portable.sh"
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null
status=0
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT

run() {   # run <fixture> <min-ok> <label>
    local out
    out="$({ GBASIC_PATH=stdlib timeout -k 5 120 ./gbasic "$1" 2>&1 </dev/null || true; })"
    local bad ok
    bad="$(printf '%s\n' "$out" | grep -c '^MISMATCH' || true)"
    ok="$(printf '%s\n' "$out" | grep -c '^ok  ' || true)"
    if [ "${bad:-0}" -ne 0 ]; then
        printf '%s\n' "$out" | grep '^MISMATCH' | sed 's/^/  /'
        printf 'FAIL %s\n' "$3"; status=1
        return
    fi
    # A COVERAGE FLOOR, because a fixture whose checks stopped running reports
    # no MISMATCH either -- the failure this tree has recorded more than once.
    if [ "${ok:-0}" -lt "$2" ]; then
        printf 'FAIL %s (only %s checks ran; at least %s expected)\n' "$3" "$ok" "$2"
        status=1
        return
    fi
    printf 'PASS %s (%s checks)\n' "$3" "$ok"
}

echo "--- TIER 1: distinct inputs, distinct keys ---"
run tests/key/key_test.bas 12 "injectivity"

echo "--- TIER 2: the insight regression, through the real library ---"
run tests/key/collision_test.bas 2 "cell identity"

echo "--- TIER 3: refusals, each beside its nearest legal neighbour ---"
say() {
    printf '%s\n' "$1" >"$tmp/p.bas"
    { ./gbasic "$tmp/p.bas" 2>&1 || true; } | head -1 | sed 's/^[^:]*:[0-9]*:[0-9]*: //'
}
want() {
    local got; got="$(say "$2")"
    case "$got" in
        *"$3"*) printf 'ok   %s\n' "$1" ;;
        *) printf 'MISMATCH %s\n  want: %s\n  got:  %s\n' "$1" "$3" "$got"; status=1 ;;
    esac
}
want 'array refused'   'print key([1, 2])'        'key cannot use an array'
want 'record refused'  'print key({ a: 1 })'      'key cannot use a record'
want 'no arguments'    'print key()'              'key expects at least one value'
# CONTROLS: the scalars it must accept, or "it refuses compounds" is satisfied
# by a builtin that refuses everything. ASSERTED AS ACCEPTANCE, NOT AS BYTES.
# These three used to pin `s1:a` / `n1:7` / `z:`, which contradicted this
# file's own principle -- the encoding is an internal detail and freezing it
# would rebaseline the suite for a change that broke nothing. They were also
# redundant: once Tier 1 gained the case that a component may CONTAIN the tag
# syntax, the injectivity tier catches every perturbation these did.
want 'string ok'   'print "accepted " + string(len(key("a")) > 0)'       'accepted true'
want 'number ok'   'print "accepted " + string(len(key(7)) > 0)'         'accepted true'
want 'absence ok'  'print "accepted " + string(len(key(nothing)) > 0)'   'accepted true'
# AND A NAME CONTROL: `key` is used as an ORDINARY VARIABLE at 53 sites in this
# tree, so adding the builtin must not have reserved the word.
want 'key is still a name' 'key = 5
print key'                                        '5'

echo "--- TIER 4: valgrind ---"
. "$(dirname "$0")/valgrind_tier.sh"
if vg_available; then
    vg_run ./gbasic tests/key/key_test.bas >/dev/null 2>"$tmp/vg.err" </dev/null
    rc=$?
    if [ "$rc" = "$VG_EXIT" ]; then
        cat "$tmp/vg.err"; printf 'FAIL valgrind\n'; status=1
    else
        printf 'PASS valgrind (no definite leak or invalid access)\n'
    fi
else
    printf 'SKIP valgrind (unavailable)\n'
fi

exit $status
