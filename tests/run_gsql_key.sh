#!/bin/bash
# G-SQL's INDEX KEY ENCODING (src/gsql/key.c, docs/gsql_storage_design.md §3.2):
# a value becomes bytes whose memcmp order is gBASIC's own order, so every
# Phase 0a storage candidate can compare keys without knowing what is in them.
# Phase 0a's first artefact, and needed whichever candidate wins.
#
# THE ORACLE IS gBASIC: tests/gsql/key_values.bas serializes a battery (the
# exactness edge, subnormals, signed zero, infinities, a generated spread,
# strings with NULs and prefixes, booleans, dates/datetimes/times at several
# precisions, money with guard digits in three currencies, absence) and writes
# gBASIC's OWN answer for every pair it will order, deciding absence placement
# with its own `sort`. tests/gsql/key_check.c requires memcmp to agree --
# ascending, descending, as typed money, and over composite (ASC, DESC) keys
# built from those pairs. NaN, which gBASIC gives no order, is held to the
# documented position: after +inf, equal to itself, below absence. Every
# truncation of every value must be refused, and the kinds with no index order
# (a duration, an array, a record, a path) must be refused rather than keyed.
#
# The encoder's input is a value in `serialize` version 3 -- what a G-SQL row
# holds -- so it needs no interpreter and is compiled here on its own.
#
# AND `gsql_key_skip` (2026-10-29), which the table layer needs so a
# non-unique index entry -- its columns, then the row key -- gives the row key
# back (docs/gsql_table_design.md §6): every value's key, ascending and
# descending and followed by bytes shaped like a string's terminator, must
# skip to exactly its own length, and every strict prefix must be refused.
# Proven red by reading a descending component without inverting it (160
# mismatches) and by a time-only datetime given a date-time's length (188).
. "$(dirname "$0")/portable.sh"
set -uo pipefail
cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh
if ! make >/dev/null; then
    printf 'FAIL run_gsql_key (the build failed, so nothing below would test this tree)\n'
    exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

CC=${CC:-cc}
if ! command -v "$CC" >/dev/null 2>&1; then
    printf 'SKIP run_gsql_key (no C compiler -- the key encoding was NOT measured)\n'
    exit 0
fi
if ! "$CC" -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude -g \
        -o "$work/key_check" tests/gsql/key_check.c src/gsql/key.c 2> "$work/cc.err"; then
    head -5 "$work/cc.err"
    printf 'FAIL run_gsql_key (the encoder or its checker did not compile cleanly)\n'
    exit 1
fi

printf 'TIER oracle: memcmp agrees with gBASIC on every pair it orders\n'
if ! ./gbasic tests/gsql/key_values.bas > "$work/values.txt" 2> "$work/values.err"; then
    head -3 "$work/values.err"; fail "the oracle fixture failed"
elif [ -s "$work/values.err" ]; then
    head -3 "$work/values.err"; fail "the oracle fixture wrote to stderr"
else
    pairs=$(grep -c '^C ' "$work/values.txt" || true)
    # BOUNDED, because a hang is not a failure: an encoder that loops on one
    # value would otherwise leave the gate sitting here saying nothing.
    timeout -k 5 120 "$work/key_check" < "$work/values.txt" > "$work/check.out"
    rc=$?
    summary=$(tail -1 "$work/check.out")
    checks=$(printf '%s' "$summary" | sed -n 's/^ok \([0-9]*\) checks.*/\1/p')
    if [ "$rc" = "124" ] || [ "$rc" = "137" ]; then
        fail "the checker HUNG -- the encoder looped on some value"
    elif [ "$rc" != "0" ]; then
        grep '^MISMATCH' "$work/check.out" | head -10
        fail "the keys disagree with gBASIC ($summary)"
    elif [ "${pairs:-0}" -lt 10000 ] || [ "${checks:-0}" -lt 100000 ]; then
        # A battery that shrank, or a checker that stopped early, would agree
        # with gBASIC about nothing and report it as agreement.
        fail "only ${pairs:-0} pairs and ${checks:-0} checks -- the battery or the checker stopped early"
    else
        pass "$(sed -n 1p "$work/check.out" | tr -d '\n'); $summary"
    fi
fi

printf 'TIER valgrind: the encoder over the whole battery\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access was NOT measured)\n'
elif [ "$status" != "0" ]; then
    # Run only over an encoder the bounded tier passed: `timeout` cannot wrap
    # `vg_run` (a shell FUNCTION), so an encoder that loops would hang here.
    printf '  SKIP (the oracle tier failed -- memory access was NOT measured)\n'
elif [ -s "$work/values.txt" ]; then
    vg_run "$work/key_check" < "$work/values.txt" > /dev/null 2> "$work/vg.err"
    vrc=$?
    if [ "$vrc" = "$VG_EXIT" ]; then
        grep -E "Invalid|uninitialised|definitely" "$work/vg.err" | head -4
        fail "valgrind objected"
    elif [ "$vrc" != "0" ]; then
        fail "the checker exited $vrc under valgrind"
    else
        pass "no definite leak or invalid access"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_gsql_key: all checks passed\n'
else
    printf 'run_gsql_key: FAILED\n'
fi
exit "$status"
