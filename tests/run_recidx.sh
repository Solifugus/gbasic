#!/usr/bin/env bash
# PLAT-RECIDX: record field access that is not linear in the record's size.
#
# A record was an association list walked with strcmp, and reading a record
# VARIABLE deep-copied the whole field array (env_get -> value_copy). Two costs,
# and either one alone keeps a per-key loop quadratic -- the same pairing
# PLAT-STRIDX documents for strings. Both are gone:
#
#   * a lazily-built hash index from field name to slot, in a header that rides
#     in front of the RecordField array, built only for records big enough to
#     need one (RECORD_INDEX_MIN_FIELDS);
#   * the field array is shared and refcounted, copied only when a write reaches
#     a record someone else can see (record_ensure_unique), which is the
#     ArrayStorage pattern from docs/array_cow_design.md applied one level in.
#
# Measured on this machine, building a record keyed by string: 16 000 fields
# 0.87 s -> 0.02 s, 32 000 fields 3.42 s -> 0.04 s, and 128 000 fields (which
# did not finish in any reasonable time before) 0.13 s. Reading is the bigger
# one: 300 000 lookups of a SINGLE key cost 0.65 s against a 1 000-field record
# and 8.41 s against 8 000 -- a fixed amount of work that grew with the size of
# the record -- and is now flat at ~0.3 s for both.
#
# Tiers:
#   1. SEMANTICS -- tests/recidx_test.bas, self-checking (every line states its
#      own expected value and prints ok or a MISMATCH) and golden-compared.
#   2. PARITY    -- the fixture runs every check twice, once on a record below
#      RECORD_INDEX_MIN_FIELDS and once above, so the linear walk is the oracle
#      for the indexed path. This tier strips the labels and diffs the two
#      blocks against EACH OTHER: it fails if the index ever answers differently
#      from the walk, whatever the golden happens to say.
#   3. SHAPE (linear) -- ops that touch every field must cost about 4x across a
#      4x size step. Ratios, never absolute times.
#   4. SHAPE (flat)   -- ops doing FIXED work against a record that GROWS must
#      cost about the same at both sizes. This is the tier that would have
#      failed before the change, and the one that fails if reading a record ever
#      goes back to copying it.
#   5. CONTROL   -- a negative control. Neither shape tier can be proven red on
#      this interpreter, since there is no defect left to fix, so the same
#      harness is pointed at repeated string concatenation, which IS still
#      quadratic by design, and is REQUIRED to exceed both gates.
#   6. VALGRIND  -- the semantics fixture. Record sharing is refcounted, so a
#      missed detach or an unbalanced release is a use-after-free or a double
#      free that a value comparison alone can miss; that is exactly how the two
#      real defects found while building this were caught.
#
# Headless, GI-independent, no display. Runs everywhere.
set -u

cd "$(dirname "$0")/.."

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_recidx: build failed\n'
    exit 1
fi

out=$(mktemp)
err=$(mktemp)
work=$(mktemp -d)
trap 'rm -f "$out" "$err"; rm -rf "$work"' EXIT

status=0

# --- Tier 1: semantics --------------------------------------------------------
if timeout 300 ./gbasic tests/recidx_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u tests/recidx_test.out "$out"; then
        printf 'PASS tests/recidx_test.bas (semantics)\n'
    else
        printf 'FAIL tests/recidx_test.bas -- record SEMANTICS moved\n'
        status=1
    fi
    if grep -q 'MISMATCH' "$out"; then
        printf 'FAIL tests/recidx_test.bas -- a check reported MISMATCH:\n'
        grep 'MISMATCH' "$out"
        status=1
    fi
else
    printf 'FAIL tests/recidx_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# --- Tier 2: parity between the walked and the indexed record -----------------
# Both batteries emit "<label> <check> ok", so with the label removed the two
# blocks must be byte-identical. A divergence here means the index disagrees
# with the linear walk it is supposed to be an accelerator for.
grep '^linear '  "$out" | sed 's/^linear //'  >"$work/linear.txt"
grep '^indexed ' "$out" | sed 's/^indexed //' >"$work/indexed.txt"
if [ ! -s "$work/linear.txt" ] || [ ! -s "$work/indexed.txt" ]; then
    printf 'FAIL parity: one of the batteries produced no output\n'
    status=1
elif diff -u "$work/linear.txt" "$work/indexed.txt" >"$work/parity.diff"; then
    printf 'PASS parity (%s checks agree below and above the index threshold)\n' \
           "$(wc -l <"$work/linear.txt" | tr -d ' ')"
else
    printf 'FAIL parity -- the indexed record answers differently from the walked one\n'
    cat "$work/parity.diff"
    status=1
fi

# --- timing helper ------------------------------------------------------------
# Echoes elapsed seconds. Fails loudly if the fixture did not actually run: a
# crashed or no-op run is fast, and must never read as a pass.
timed() { # fields op [file] -> seconds
    local file=${3:-tests/recidx_perf.bas} t0 t1
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

# ratio of t(4n) to t(n). The floor keeps a very fast t(n) from making the ratio
# meaningless on a fast machine; it is lower here than in the sibling suites
# because these ops are individually cheaper.
ratio_of() { # small large [floor]
    echo "$1 $2 ${3:-0.10}" | awk '{ f = ($1 < $3) ? $3 : $1; printf "%.2f", $2 / f }'
}

shape() { # label small large op gate note
    local label=$1 small=$2 large=$3 op=$4 gate=$5 note=$6 ts tl r
    ts=$(timed "$small" "$op")
    tl=$(timed "$large" "$op")
    if [ "$ts" = "-1" ] || [ "$tl" = "-1" ]; then
        status=1
        return
    fi
    r=$(ratio_of "$ts" "$tl")
    if awk -v r="$r" -v g="$gate" 'BEGIN { exit !(r <= g) }'; then
        printf 'PASS shape %-22s %ss -> %ss  ratio %sx (gate %sx, %s)\n' \
               "$label" "$ts" "$tl" "$r" "$gate" "$note"
    else
        printf 'FAIL shape %-22s %ss -> %ss  ratio %sx exceeds %sx -- %s\n' \
               "$label" "$ts" "$tl" "$r" "$gate" "$note"
        status=1
    fi
}

# --- Tier 3: shape, ops proportional to the field count -----------------------
printf -- '-- shape: work over every field, across a 4x size step (linear is ~4x)\n'
shape "build by key"      50000 200000 build      8 "quadratic ~16x"
shape "read every key"    50000 200000 lookup_all 8 "quadratic ~16x"
shape "has every key"     50000 200000 has_all    8 "quadratic ~16x"
shape "keys()"            50000 200000 keys       8 "quadratic ~16x"
shape "for each over keys" 50000 200000 foreach   8 "quadratic ~16x"
shape "overwrite every key" 50000 200000 overwrite 8 "quadratic ~16x"
# Assignment to a name that already holds an equal record compares the two field
# by field to decide whether a watcher fires, so this is linear BY DESIGN rather
# than flat. It is here because it used to be quadratic on top of that: 5 000
# iterations against a 2 000-field record measured 26.72 s before and 0.43 s
# after, and against 8 000 fields 502.97 s before and 1.17 s after.
shape "assign equal record" 2000 8000 copy       8 "was 18.8x, quadratic"

# --- Tier 4: shape, FIXED work against a record that GROWS --------------------
# The heart of the suite. Each of these does the same number of operations at
# both sizes, so the cost must not move. A gate of 2.5x leaves room for noise
# while still failing anything that scales with the record: the pre-change
# binary reported 5x here across the same step, and simply reinstating the
# per-read copy would bring that straight back.
printf -- '-- shape: FIXED work, growing record (must be flat, ~1x)\n'
shape "300k lookups, 1 key" 2000 8000 lookup_one 2.5 "must not scale with the record"
shape "300k passes to fn"   2000 8000 pass       2.5 "must not scale with the record"
shape "300k dot reads"      2000 8000 field_dot  2.5 "must not scale with the record"

# --- Tier 5: negative control -------------------------------------------------
# Proves the gates above can actually fail. Repeated `s = s + x` allocates and
# copies both sides every iteration and is genuinely O(n^2). If the harness ever
# reports this as flat or linear, the shape tiers are not measuring anything.
printf -- '-- control: a known-quadratic loop must FAIL both gates\n'
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
    cr=$(ratio_of "$cts" "$ctl" 0.25)
    if awk -v r="$cr" 'BEGIN { exit !(r > 8) }'; then
        printf 'PASS control string-concat %ss -> %ss  ratio %sx exceeds both 2.5x and 8x, as it must\n' \
               "$cts" "$ctl" "$cr"
    else
        printf 'FAIL control string-concat %ss -> %ss  ratio %sx did NOT exceed 8x -- the gates are dead\n' \
               "$cts" "$ctl" "$cr"
        status=1
    fi
fi

# --- Tier 6: valgrind ---------------------------------------------------------
if command -v valgrind >/dev/null 2>&1; then
    if valgrind --error-exitcode=9 --leak-check=full --track-fds=yes \
                --errors-for-leak-kinds=definite \
                ./gbasic tests/recidx_test.bas >"$out" 2>"$err" </dev/null; then
        if diff -q tests/recidx_test.out "$out" >/dev/null; then
            printf 'PASS valgrind tests/recidx_test.bas\n'
        else
            printf 'FAIL valgrind tests/recidx_test.bas (output differs under valgrind)\n'
            status=1
        fi
    else
        printf 'FAIL valgrind tests/recidx_test.bas\n'
        cat "$err"
        status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit "$status"
