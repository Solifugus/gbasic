#!/bin/bash
# G-SQL's TABLE LAYER (include/gsql_table.h, src/gsql/table.c;
# docs/gsql_table_design.md), Phase 1's first increment: a catalog, typed rows,
# primary keys, unique and secondary indexes, NOT NULL and defaults, over the
# storage engine run_gsql_store.sh holds.
#
# IT WORKS ON `serialize` VERSION 3 BYTES, never on interpreter values, so it
# compiles alone; what makes its tests more than the layer agreeing with
# itself is WHERE each expectation comes from:
#   kinds   the type rules (§3.1) over 34 values GBASIC SERIALIZED
#           (tests/gsql/table_values.bas), each with a verdict per column kind
#           written there BY HAND from the design's table -- including the
#           ones a plausible checker gets wrong: 6 / 2 is 3.0 and an integer
#           column refuses it, a year-month date fits neither `date` nor
#           `datetime`, a record holding a duration is refused like the
#           duration. Every truncation of every value is refused too.
#   refuse  each refusal of §3.2 and §7 beside the case that is accepted, so
#           the tier cannot pass on a layer that refuses everything; and the
#           invariant check's own control, a stray rows tree it must report.
#   model   a random workload of creates, inserts, updates, deletes, index
#           creations and drops, ADD COLUMN, table drops, aborts and reopens
#           against an in-memory model, on the simulated disk and the real
#           filesystem, under ASan and UBSan, with the invariant check (§8)
#           after every reopen. The model does NOT ask the layer what is
#           allowed: each pool value carries a class written in the test, a
#           table there says which kinds take which class, and uniqueness is
#           key equality from the key encoder, which run_gsql_key.sh holds to
#           gBASIC's own answers. A floor on every outcome (accepted, each
#           refusal, a key moved by an update, aborts, reopens), because the
#           first draft accepted 1 insert in 400 rounds and agreed perfectly.
#   layout  a fixed database dumped and decoded by tests/gsql/table_layout.py,
#           written from the design's text with tests/serialize/decode_v3.py's
#           reader, so a schema or row layout our writer and reader agree on
#           wrongly is still caught.
#
# TWELVE PERTURBATIONS PROVEN RED (2026-10-29), each by the tier written for
# it: an integer column taking 3.0 (kinds, refuse, model); UNIQUE treating two
# unknowns as equal (refuse, model); an update that moved a row's key leaving
# its index entries pointing at the old one (model, through the invariant
# check); a row predating ADD COLUMN reading unknown instead of the default
# (refuse, model); a bound `nothing` stored as `nothing` (model, layout); an
# update allowed onto ANY row's unique value (model); a DROP leaving its index
# trees (refuse, model); a datetime column taking a date (kinds, model); a
# descending key component skipped without inverting, and a time-only one
# skipped at a date-time's length (run_gsql_key.sh's skip tier, and the model
# here); and a SELF-CONSISTENT one -- the schema's `columns` field renamed in
# the writer and the reader together -- which every tier but `layout` passes.
#
# THE `nothing` ONE FIRST WENT RED ONLY IN `layout`, and the cause was the
# MODEL: it normalised each value in the very buffer it handed the layer, so
# the layer was never sent a `nothing` at all, and the floor on outcomes had
# no line for one. The model keeps what it sends apart from what it expects
# stored now, and requires more than ten bound nothings per run.
. "$(dirname "$0")/portable.sh"
set -uo pipefail
cd "$(dirname "$0")/.."

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

CC=${CC:-cc}
if ! command -v "$CC" >/dev/null 2>&1; then
    printf 'SKIP run_gsql_table (no C compiler -- the table layer was NOT measured)\n'
    exit 0
fi
FLAGS="-std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude -Isrc/gsql"
ENGINE="src/gsql/lsm.c src/gsql/store_common.c src/gsql/store_util.c src/gsql/io.c src/gsql/io_sim.c"
LAYER="src/gsql/table.c src/gsql/key.c"
SAN=""
printf 'int main(void){return 0;}\n' > "$work/probe.c"
if "$CC" -fsanitize=address,undefined -o "$work/probe" "$work/probe.c" 2>/dev/null; then
    SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
fi
bounded() { timeout -k 5 "$1" "${@:2}"; }

printf 'TIER build: the layer and its test, -Wpedantic -Werror\n'
# shellcheck disable=SC2086
if "$CC" $FLAGS -g -O1 $SAN -o "$work/table_test" tests/gsql/table_test.c $LAYER $ENGINE 2> "$work/build.err"; then
    pass "table_test"
else
    head -8 "$work/build.err"
    fail "table_test did not compile cleanly"
    printf 'run_gsql_table: FAILED (nothing below was run)\n'
    exit 1
fi
[ -n "$SAN" ] || printf '  note the compiler has no ASan/UBSan: the model tier runs without them\n'

# the model runs are independent: start them together, read them in order
mkdir -p "$work/real"
for seed in 1 2 3 4 5; do bounded 300 "$work/table_test" model "$seed" > "$work/model.$seed" 2>&1 & done
bounded 300 "$work/table_test" model 6 "$work/real" > "$work/model.real" 2>&1 &

printf 'TIER kinds: the column kinds over values gBASIC serialized, verdicts by hand\n'
if ! make >/dev/null 2>&1; then
    fail "./gbasic did not build, so gBASIC could not write the battery"
elif ! ./gbasic tests/gsql/table_values.bas > "$work/values.txt" 2> "$work/values.err"; then
    head -5 "$work/values.err"
    fail "tests/gsql/table_values.bas did not run"
else
    out=$(bounded 60 "$work/table_test" kinds "$work/values.txt" 2>&1)
    last=$(printf '%s\n' "$out" | tail -1)
    case "$last" in
        "ok "*" 0 mismatches") pass "$(printf '%s\n' "$out" | grep '^values' | sed 's/^values //') values, ten kinds each, nullable and NOT NULL: $last" ;;
        *) printf '%s\n' "$out" | grep MISMATCH | head -10; fail "the column kinds: $last" ;;
    esac
fi

printf 'TIER refuse: every refusal beside the case that is accepted\n'
out=$(bounded 60 "$work/table_test" refuse 2>&1)
last=$(printf '%s\n' "$out" | tail -1)
case "$last" in
    "ok "*" 0 mismatches") pass "declarations, writes, unique indexes, ADD COLUMN, DROP and the check's own control: $last" ;;
    *) printf '%s\n' "$out" | grep -E 'MISMATCH|runtime error|ERROR' | head -10; fail "the refusals: $last" ;;
esac

printf 'TIER model: a random workload against a model%s, the invariant check after every reopen\n' \
    "${SAN:+, under ASan and UBSan}"
wait
for seed in 1 2 3 4 5 real; do
    where="seed $seed (simulated disk)"; [ "$seed" = real ] && where="real filesystem"
    last=$(tail -1 "$work/model.$seed")
    census=$(grep '^accepted' "$work/model.$seed")
    if grep -q 'runtime error\|ERROR: AddressSanitizer' "$work/model.$seed"; then
        grep -m3 'runtime error\|ERROR: AddressSanitizer' "$work/model.$seed"
        fail "$where: the sanitizers objected"
    fi
    case "$last" in
        "ok "*" 0 mismatches") pass "$where: $census; $last" ;;
        *) grep -E 'MISMATCH' "$work/model.$seed" | head -8; fail "$where: $last" ;;
    esac
done

printf 'TIER layout: an independent reader of the catalog, the rows and an index\n'
if command -v python3 >/dev/null 2>&1; then
    mkdir -p "$work/layout"
    if ! bounded 60 "$work/table_test" layout "$work/layout" > "$work/layout.txt" 2>&1; then
        head -5 "$work/layout.txt"
        fail "the layout database did not build"
    else
        out=$(python3 tests/gsql/table_layout.py < "$work/layout.txt" 2>&1)
        last=$(printf '%s\n' "$out" | tail -1)
        case "$last" in
            ok*) pass "table_layout.py reads the schema, rows (one predating ADD COLUMN) and a descending index from the design's own layout: $last" ;;
            *) printf '%s\n' "$out" | grep MISMATCH | head -8; fail "table_layout.py disagrees: $last" ;;
        esac
    fi
else
    printf '  SKIP the independent reader (no python3): the layout was cross-checked by NOTHING but the layer'"'"'s own reader\n'
fi

if [ "$status" = 0 ]; then
    printf 'run_gsql_table: all checks passed\n'
else
    printf 'run_gsql_table: FAILED\n'
fi
exit "$status"
