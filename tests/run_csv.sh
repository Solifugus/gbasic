#!/usr/bin/env bash
# The csv module (docs/csv_design.md): reading and writing delimited files.
#
# WHAT IT REPLACED WAS SILENT CORRUPTION. frame.read_csv split lines on `,`,
# so the gBASIC books' three-record catalog (a quoted comma, a quoted line
# break, a doubled quote) came back as FOUR records with prices in the wrong
# rows and no error; market dropped any row with an extra comma without a word.
# So most tiers here are not goldens of our own output but agreement with
# something else:
#
#   RULES        -- every reading rule of §3-§5, one case each (golden), with
#                   where each record started and what each refusal says.
#   WRITING      -- csv.line and csv.write (golden), and csv.write read back.
#   DIFFERENTIAL -- generated files in every delimiter and quoting, with quoted
#                   delimiters, quoted line breaks of all three kinds, doubled
#                   quotes, empty fields and blank lines. THE ORACLE IS THE
#                   GENERATOR'S OWN RECORD of what it wrote, line and byte offset
#                   included; Python's csv module checks the generator before
#                   gBASIC is compared, so neither reader grades itself. The big
#                   files put a quote, a doubled quote and a CRLF pair across the
#                   64 KiB read buffer at random places.
#   ROUNDTRIP    -- csv.line output read by Python's reader, and Python's writer
#                   output read by csv.records.
#   LIMIT        -- a quote that never closes, in a file larger than the record
#                   limit, is refused naming its line instead of read into memory.
#   MEMORY       -- streaming is flat in the file size, beside a control that
#                   keeps every record and grows.
#   CALLERS      -- the books' catalog through frame.read_csv, and a long record
#                   there raising with its line. (market is run_market.sh.)
#   VALGRIND     -- the goldens, the differential, and every exit from the loop
#                   under --track-fds: the loop owns the file.
#
# Needs python3 for the differential, roundtrip and limit tiers, and says which
# claims went unmeasured when it is absent.
. "$(dirname "$0")/portable.sh"
set -u

cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"
. "$(dirname "$0")/premise.sh"

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_csv: build failed\n'
    exit 1
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
checks=0
failures=0
pass() { checks=$((checks + 1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks + 1)); failures=$((failures + 1)); printf '  FAIL %s\n' "$1"; }

golden() { # label fixture expected args...
    local label="$1" fixture="$2" expected="$3"
    shift 3
    if GBASIC_PATH=stdlib timeout 120 ./gbasic "$fixture" "$@" >"$work/out" 2>&1 </dev/null; then
        if diff -u "$expected" "$work/out" >"$work/diff"; then
            pass "$label"
        else
            fail "$label"
            head -40 "$work/diff"
        fi
    else
        fail "$label (exit)"
        tail -5 "$work/out"
    fi
}

echo "TIER rules"
golden "every reading rule, refusal and position (tests/csv/rules.bas)" tests/csv/rules.bas tests/csv/rules.out

echo "TIER writing"
mkdir -p "$work/w"
golden "csv.line and csv.write (tests/csv/writing.bas)" tests/csv/writing.bas tests/csv/writing.out "$work/w"

have_python=0
command -v python3 >/dev/null 2>&1 && have_python=1

echo "TIER differential"
if [ "$have_python" = 1 ]; then
    for spec in "1 400" "2 400" "3 400" "11 6 big" "12 6 big"; do
        set -- $spec
        d="$work/diff_$1"
        mkdir -p "$d"
        if ! made=$(python3 tests/csv/gen_csv_diff.py "$d" "$@" 2>&1); then
            fail "generator seed $1 (the generator and Python disagree, so nothing was compared)"
            printf '%s\n' "$made" | head -10
            continue
        fi
        if timeout 120 ./gbasic tests/csv/diff.bas "$d/manifest" >"$d/got" 2>"$d/err" </dev/null \
           && cmp -s "$d/expected" "$d/got"; then
            pass "seed $1${3:+ ($3)}: $made, every field, line and offset as written"
        else
            fail "seed $1${3:+ ($3)}: $made"
            diff "$d/expected" "$d/got" | head -10
            head -5 "$d/err"
        fi
    done
else
    printf '  SKIP no python3: the differential against what was written was NOT measured\n'
fi

echo "TIER roundtrip"
if [ "$have_python" = 1 ]; then
    mkdir -p "$work/rt"
    if out=$(timeout 300 python3 tests/csv/roundtrip.py ./gbasic "$work/rt" 5 2>&1); then
        while IFS= read -r l; do pass "$l"; done <<<"$out"
    else
        fail "roundtrip"
        printf '%s\n' "$out" | head -20
    fi
else
    printf '  SKIP no python3: writing was NOT checked against another reader\n'
fi

echo "TIER limit"
if [ "$have_python" = 1 ]; then
    python3 -c "
import sys
with open(sys.argv[1], 'w') as f:
    f.write('a,b\n1,2\n3,\"never closed\n')
    chunk = 'x' * (1 << 20)
    for _ in range(65):
        f.write(chunk)
" "$work/open.csv"
    printf 'for each r in csv.records({file}"%s")\nend for\n' "$work/open.csv" >"$work/open.bas"
    # run bare: it was under /usr/bin/time writing a figure nothing read, so a
    # machine without GNU time failed this tier for nothing (riscv64, 2026-11-10)
    ./gbasic "$work/open.bas" >/dev/null 2>"$work/open.err" </dev/null
    if grep -q 'the record starting on line 3 is over 64 MiB; a quote that never closes?' "$work/open.err"; then
        pass "a 65 MiB unterminated field is refused naming line 3"
    else
        fail "a 65 MiB unterminated field is refused naming line 3"
        head -3 "$work/open.err"
    fi
else
    printf '  SKIP no python3: the record limit was NOT measured\n'
fi

echo "TIER memory"
if premise_rss_meter && [ "$have_python" = 1 ]; then
    python3 -c "
import sys
for n, name in ((40000, 'small'), (400000, 'large')):
    with open(sys.argv[1] + '/' + name + '.csv', 'w') as f:
        f.write('sku,description,price\n')
        for i in range(n):
            f.write('A-%d,\"Gripper, two-finger\nmodel %d\",%d.50\n' % (i, i % 997, i % 1000))
" "$work"
    peak() { /usr/bin/time -f '%M' ./gbasic "$1" "$2" 2>&1 >/dev/null </dev/null | tail -1; }
    s_small=$(peak tests/csv/count.bas "$work/small.csv")
    s_large=$(peak tests/csv/count.bas "$work/large.csv")
    c_small=$(peak tests/csv/collect.bas "$work/small.csv")
    c_large=$(peak tests/csv/collect.bas "$work/large.csv")
    # Over a 10x range of input: streaming within 1.25x of itself, while the
    # control that keeps every record must grow at least 3x, or the files were
    # too small for the comparison to mean anything.
    if [ "$((c_large * 100 / c_small))" -ge 300 ]; then
        pass "the control grows: ${c_small} kB -> ${c_large} kB keeping every record"
        if [ "$((s_large * 100 / s_small))" -le 125 ]; then
            pass "streaming is flat: ${s_small} kB -> ${s_large} kB over 10x the records"
        else
            fail "streaming is flat: ${s_small} kB -> ${s_large} kB over 10x the records"
        fi
    else
        fail "the control did not grow (${c_small} -> ${c_large} kB), so flatness was not measured"
    fi
else
    printf '  SKIP no /usr/bin/time or python3: bounded memory was NOT measured\n'
fi

echo "TIER callers"
printf 'sku,description,price\nA-100,"Gripper, two-finger",120.50\nA-200,"Servo motor\n24V, 400W",310.00\nA-300,"The ""Pro"" sensor",85.00\n' >"$work/catalog.csv"
cat >"$work/frame.bas" <<EOF
load frame
df = frame.read_csv("$work/catalog.csv")
print(json_encode(df.sku))
print(json_encode(df.description))
print(json_encode(df.price))
p = frame.write_csv(df, "$work/again.csv")
back = frame.read_csv("$work/again.csv")
print(back = df)
EOF
printf '%s\n' '["A-100","A-200","A-300"]' '["Gripper, two-finger","Servo motor\n24V, 400W","The \"Pro\" sensor"]' '[120.5,310,85]' 'true' >"$work/frame.want"
if GBASIC_PATH=stdlib ./gbasic "$work/frame.bas" >"$work/frame.got" 2>&1 </dev/null && cmp -s "$work/frame.want" "$work/frame.got"; then
    pass "frame.read_csv reads the books' catalog as three records, and write_csv round-trips it"
else
    fail "frame.read_csv reads the books' catalog as three records, and write_csv round-trips it"
    diff "$work/frame.want" "$work/frame.got" | head
fi
printf 'a,b\n1,2\n3,4,5\n' >"$work/long.csv"
printf 'load frame\ndf = frame.read_csv("%s")\n' "$work/long.csv" >"$work/long.bas"
if ! GBASIC_PATH=stdlib ./gbasic "$work/long.bas" >/dev/null 2>"$work/long.err" </dev/null \
   && grep -q 'frame.read_csv: line 3 of .*long.csv has 3 fields where the header has 2' "$work/long.err"; then
    pass "frame.read_csv raises on a long record, naming its line, instead of padding or dropping"
else
    fail "frame.read_csv raises on a long record, naming its line"
    head -3 "$work/long.err"
fi

echo "TIER valgrind"
if vg_available; then
    printf 'a,b\n1,2\n3,4\n' >"$work/f.csv"
    printf 'a,b\n1,2\n3,4,5\n' >"$work/g.csv"
    if vg_run ./gbasic tests/csv/exits.bas "$work/f.csv" "$work/g.csv" >"$work/vg.out" 2>"$work/vg.err" </dev/null; then
        if [ "$(grep -c . "$work/vg.out")" = 7 ]; then
            pass "every exit from the loop: no leaked memory and no leaked descriptor"
        else
            fail "the exits fixture did not reach all seven exits"
            cat "$work/vg.out"
        fi
    else
        fail "every exit from the loop under valgrind"
        grep -E 'Invalid|definitely|FILE DESCRIPTORS|Open file' "$work/vg.err" | head
    fi
    if vg_run ./gbasic tests/csv/rules.bas >/dev/null 2>"$work/vg.err" </dev/null; then
        pass "the rules, refusals included, under valgrind"
    else
        fail "the rules under valgrind"
        grep -E 'Invalid|definitely' "$work/vg.err" | head
    fi
    if vg_run ./gbasic tests/csv/writing.bas "$work/w" >/dev/null 2>"$work/vg.err" </dev/null; then
        pass "writing, refusals included, under valgrind"
    else
        fail "writing under valgrind"
        grep -E 'Invalid|definitely' "$work/vg.err" | head
    fi
    if [ -f "$work/diff_1/manifest" ]; then
        if vg_run ./gbasic tests/csv/diff.bas "$work/diff_1/manifest" >/dev/null 2>"$work/vg.err" </dev/null; then
            pass "400 generated files under valgrind"
        else
            fail "the differential under valgrind"
            grep -E 'Invalid|definitely' "$work/vg.err" | head
        fi
    fi
else
    printf '  SKIP no valgrind: leaks and descriptors were NOT measured\n'
fi

if [ "$failures" -eq 0 ]; then
    printf 'PASS tests/run_csv.sh (%d checks)\n' "$checks"
    exit 0
fi
printf 'FAIL tests/run_csv.sh (%d of %d checks failed)\n' "$failures" "$checks"
exit 1
