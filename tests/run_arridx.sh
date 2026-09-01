#!/usr/bin/env bash
# PLAT-ARRIDX: array access that is not quadratic in the array's length.
#
# Arrays are value types over a shared, refcounted store with copy-on-write
# (docs/array_cow_design.md, landed 2026-07-23). `value_copy` bumps a refcount
# rather than deep-copying, so reading a variable, passing an array to a
# function and indexing are all O(1); `append` reserves by doubling, so a build
# loop is amortized O(n); and every mutating operation detaches first.
#
# NOTE ON WHAT THIS SUITE IS. Unlike tests/run_stridx.sh it did not accompany a
# fix -- the fix predates it. It exists because the complexity class had no
# regression guard at all: examples/array_cow_test.bas pins the SEMANTICS, and
# nothing pinned the COST, so a return to O(n^2) would have been silent. It also
# pins the aliasing cases that golden does not reach.
#
# Tiers:
#   1. ALIASING    -- tests/arridx_test.bas: an element assigned from the same
#      array (the right-hand side is read from the store the write is about to
#      free), mutation during iteration, capture-then-mutate in both directions,
#      and the whole in-place mutator family against a live alias.
#   2. SHAPE       -- cost across a 4x size step, for indexed read, bare append,
#      append-as-expression, count() in the loop condition, for-each, indexed
#      write, and an array of records. Ratios, never absolute times.
#   3. CONTROL     -- a negative control. The shape tiers above cannot be proven
#      red on this interpreter, because there is no defect left to fix, so a
#      green result would otherwise say nothing about whether the gate can fail.
#      Repeated string concatenation IS still quadratic by design, so the same
#      harness is pointed at it and REQUIRED to exceed the gate. If this passes,
#      the gate is live; if it stops failing, the measurement is broken.
#   4. VALGRIND    -- the aliasing golden, where a missed detach is a
#      use-after-free that a value comparison alone can miss.
#
# Headless, GI-independent, no display. Runs everywhere.
set -u

cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_arridx: build failed\n'
    exit 1
fi

out=$(mktemp)
err=$(mktemp)
work=$(mktemp -d)
trap 'rm -f "$out" "$err"; rm -rf "$work"' EXIT

status=0

# --- Tier 1: aliasing ---------------------------------------------------------
if timeout 300 ./gbasic tests/arridx_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u tests/arridx_test.out "$out"; then
        printf 'PASS tests/arridx_test.bas (aliasing semantics)\n'
    else
        printf 'FAIL tests/arridx_test.bas -- array ALIASING moved; a write is not detaching\n'
        status=1
    fi
else
    printf 'FAIL tests/arridx_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# --- timing helper ------------------------------------------------------------
# Echoes elapsed seconds. Fails loudly if the fixture did not actually run: a
# crashed or no-op run is fast, and must never read as a pass.
timed() { # elements op [file] -> seconds
    local file=${3:-tests/arridx_perf.bas} t0 t1
    t0=$(date +%s.%N)
    if ! timeout 900 ./gbasic "$file" "$1" "$2" >"$out" 2>"$err" </dev/null; then
        printf 'FAIL %s %s %s (exit)\n' "$file" "$1" "$2" >&2
        cat "$err" >&2
        echo "-1"
        return
    fi
    t1=$(date +%s.%N)
    if [ ! -s "$out" ]; then
        printf 'FAIL %s %s %s (no output -- did not run)\n' "$file" "$1" "$2" >&2
        echo "-1"
        return
    fi
    echo "$t1 $t0" | awk '{ printf "%.3f", $1 - $2 }'
}

# ratio of t(4n) to t(n). A floor keeps a sub-quarter-second t(n) from making the
# ratio meaningless on a fast machine.
ratio_of() { # small large
    echo "$1 $2" | awk '{ f = ($1 < 0.25) ? 0.25 : $1; printf "%.2f", $2 / f }'
}

shape() { # label small large op factor
    local label=$1 small=$2 large=$3 op=$4 factor=$5 ts tl r
    ts=$(timed "$small" "$op")
    tl=$(timed "$large" "$op")
    if [ "$ts" = "-1" ] || [ "$tl" = "-1" ]; then
        status=1
        return
    fi
    r=$(ratio_of "$ts" "$tl")
    if awk -v r="$r" -v f="$factor" 'BEGIN { exit !(r <= f) }'; then
        printf 'PASS shape %-24s %ss -> %ss  ratio %sx (gate %sx, quadratic ~16x)\n' \
               "$label" "$ts" "$tl" "$r" "$factor"
    else
        printf 'FAIL shape %-24s %ss -> %ss  ratio %sx exceeds %sx -- gone superlinear\n' \
               "$label" "$ts" "$tl" "$r" "$factor"
        status=1
    fi
}

# --- Tier 2: shape ------------------------------------------------------------
printf -- '-- shape: cost across a 4x size step\n'
shape "indexed read a[i]"     50000 200000 index       8
shape "append (bare)"         50000 200000 append      8
shape "append (expression)"   50000 200000 append_expr 8
shape "count() in condition"  50000 200000 count       8
shape "for each"              50000 200000 foreach     8
shape "indexed write a[i]=x"  50000 200000 write       8
shape "array of records"      50000 200000 records     8

# --- Tier 3: negative control -------------------------------------------------
# Proves the gate above can actually fail. Repeated `s = s + x` allocates and
# copies both sides every iteration and is genuinely O(n^2); measured 0.33 s at
# 25 000 against 8.11 s at 100 000, a 24.6x step. If the harness ever reports
# this as linear, the shape tiers are not measuring anything.
printf -- '-- control: a known-quadratic loop must FAIL the same gate\n'
cat >"$work/quadratic.bas" <<'EOF'
program main(args)
  n = number(args[0])
  s = ""
  i = 0
  while i < n
    s = s + "xxxxxxxx"
    i = i + 1
  end while
  print "len=" + len(s)
end program
EOF
cts=$(timed 25000 unused "$work/quadratic.bas")
ctl=$(timed 100000 unused "$work/quadratic.bas")
if [ "$cts" = "-1" ] || [ "$ctl" = "-1" ]; then
    status=1
else
    cr=$(ratio_of "$cts" "$ctl")
    if awk -v r="$cr" 'BEGIN { exit !(r > 8) }'; then
        printf 'PASS control string-concat %ss -> %ss  ratio %sx exceeds 8x, as it must\n' \
               "$cts" "$ctl" "$cr"
    else
        printf 'FAIL control string-concat %ss -> %ss  ratio %sx did NOT exceed 8x -- the gate is dead\n' \
               "$cts" "$ctl" "$cr"
        status=1
    fi
fi

# --- Tier 4: valgrind ---------------------------------------------------------
if vg_available; then
    if vg_run ./gbasic tests/arridx_test.bas >"$out" 2>"$err" </dev/null; then
        if diff -q tests/arridx_test.out "$out" >/dev/null; then
            printf 'PASS valgrind tests/arridx_test.bas\n'
        else
            printf 'FAIL valgrind tests/arridx_test.bas (output differs under valgrind)\n'
            status=1
        fi
    else
        printf 'FAIL valgrind tests/arridx_test.bas\n'
        cat "$err"
        status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit "$status"
