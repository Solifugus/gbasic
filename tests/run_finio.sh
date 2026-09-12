#!/usr/bin/env bash
# finio PHASE 0 -- the financial adapter framework's VALUE MODEL, and the ONE
# MEASUREMENT that chose its architecture (docs/financial_adapters_design.md
# §21).
#
# NO ADAPTER EXISTS YET AND THAT IS THE PHASE. What Phase 0 fixes is the shape
# everything above it is built on, and the design says that shape is decided by
# a measurement rather than by preference: Axiom 2 makes every interpreted value
# traceable, a 100,000-record file at eleven fields is 1.1 MILLION source
# locations, and whether gBASIC carries that is not knowable by reading.
#
# SELF-CHECKING RATHER THAN GOLDEN, and forced: every defect this layer can have
# is a PLAUSIBLE RECORD -- a location one byte off yields an ordinary-looking
# raw field from the neighbouring column, a registry entry claiming
# `implemented` without a spec reads exactly like one that has it, and a blank
# field reported as invalid reads exactly like a real defect.
#
# THE LAB TIER IS THE LOAD-BEARING ONE, and its assertion is an ORACLE rather
# than a timing: the three architectures must answer an identical query workload
# with an IDENTICAL CHECKSUM. Without it a mode could look cheap by being wrong,
# and the whole measurement would be comparing the cost of three different
# answers. The timings themselves are NOT asserted here -- they are a property
# of the machine, they are recorded in §21, and the suite that needs them is the
# one a reader runs deliberately (RUN_FINIO_COST=1).
set -u
cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh

make >/dev/null || { printf 'FAIL build\n'; exit 1; }
export GBASIC_PATH=stdlib

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
fails=0
ok()  { printf '  ok   %s\n' "$1"; }
bad() { printf '  FAIL %s\n' "$1"; fails=$((fails + 1)); }

# --- SEMANTICS: the value model -------------------------------------------
printf 'TIER semantics\n'
out="$scratch/sem.out"
if timeout 120 ./gbasic tests/finio/finio_test.bas >"$out" 2>&1; then
    mism="$(sed -n 's/^mismatches: //p' "$out")"
    checks="$(sed -n 's/^checks: //p' "$out")"
    if [ "$mism" = "0" ] && [ "${checks:-0}" -ge 35 ]; then
        ok "$checks checks, 0 mismatches"
    else
        bad "finio_test: $checks checks, $mism mismatches"
        grep MISMATCH "$out" || true
    fi
else
    bad "finio_test exited nonzero"; cat "$out"
fi

# --- LAB: the three architectures must AGREE ------------------------------
# The oracle for §21's measurement. A mode that is fast because it answers a
# different question would otherwise be indistinguishable from a cheap one.
printf 'TIER lab_oracle\n'
if ! timeout 120 ./gbasic examples/finio_lab/make_nacha.bas 2000 "$scratch/n.txt" >/dev/null 2>&1; then
    bad "could not generate the NACHA fixture"
else
    sums=""
    for m in per_value per_record on_demand; do
        got="$(timeout 300 ./gbasic examples/finio_lab/provenance_cost.bas "$m" "$scratch/n.txt" 500 2>&1)"
        c="$(printf '%s\n' "$got" | sed -n 's/^CHECKSUM //p')"
        r="$(printf '%s\n' "$got" | sed -n 's/^RECORDS //p')"
        if [ -z "$c" ]; then
            bad "$m produced no checksum: $got"
        fi
        [ "$r" = "2000" ] || bad "$m read $r records, wanted 2000"
        sums="$sums $c"
    done
    uniq_n="$(printf '%s\n' $sums | sort -u | wc -l)"
    if [ "$uniq_n" = "1" ] && [ -n "$(printf '%s' "$sums" | tr -d ' ')" ]; then
        ok "all three architectures answer identically (checksum$(printf '%s' "$sums" | awk '{print " "$1}'))"
    else
        bad "the three architectures disagree:$sums"
    fi
fi

# --- LOCATION ORACLE: the bytes a location NAMES are the bytes it RETURNED --
# Checked against the FILE with an outside tool, not against ourselves: the
# fixture's own version of this compares finio to finio, which is satisfied by
# a reader that is self-consistently one byte out. (gBASIC's `mid` is 0-BASED,
# and the first draft of this library was one byte late everywhere because of
# it -- an ordinary-looking raw field from the neighbouring column.)
printf 'TIER location\n'
if [ -s "$scratch/n.txt" ]; then
    cat > "$scratch/loc.bas" <<'BEOF'
program main( args )
    load finio
    f {file}= args[0]
    src = finio.open_text(read(f), { format: "nacha", revision: "2021" })
    lay = finio.layout([ { concept: "amount", offset: 29, length: 10 },
                         { concept: "trace",  offset: 79, length: 15 } ])
    ' the first entry detail record is line 2
    for each c in [ "amount", "trace" ]
        sv = finio.source_value(src, lay, 2, c)
        print (c + " " + string(sv.location.byte_offset + 1) + " " + string(sv.location.byte_length) + " " + sv.raw)
    end for
end program
BEOF
    loc_out="$(timeout 60 ./gbasic "$scratch/loc.bas" "$scratch/n.txt" 2>&1)"
    # REPORT THE RIGHT CAUSE: without this, a program that failed to parse
    # arrives in the loop below as a byte mismatch, which sends a reader
    # looking at the location arithmetic for a problem that is a syntax error.
    if [ "$(printf '%s\n' "$loc_out" | grep -c '^\(amount\|trace\) ')" != "2" ]; then
        bad "location: the probe did not run -- $loc_out"
    else
        printf '%s\n' "$loc_out" | while read -r concept start length raw; do
            want="$(head -c $((start + length - 1)) "$scratch/n.txt" | tail -c "$length")"
            if [ "$want" = "$raw" ]; then
                printf '  ok   %s: the byte range finio names holds the bytes it returned\n' "$concept"
            else
                printf '  FAIL %s: finio said %s at %s+%s, the file has %s\n' "$concept" "$raw" "$start" "$length" "$want"
                exit 1
            fi
        done || bad "location: a byte range did not match the file"
    fi
fi

# --- COST: opt-in, because it is a measurement and not a gate ---------------
printf 'TIER cost\n'
if [ "${RUN_FINIO_COST:-0}" = "1" ]; then
    timeout 300 ./gbasic examples/finio_lab/make_nacha.bas 100000 "$scratch/big.txt" >/dev/null
    for m in on_demand per_record per_value; do
        printf '  %-11s ' "$m"
        /usr/bin/time -f "build %%e s   peak %%M KB" timeout 900 \
            ./gbasic examples/finio_lab/provenance_cost.bas "$m" "$scratch/big.txt" 0 2>&1 >/dev/null | tail -1
    done
    ok "cost measured (figures are the machine's; §21 records this machine's)"
else
    printf '  SKIP cost (RUN_FINIO_COST=1 to measure; it needs ~3 GB and minutes)\n'
fi

# --- VALGRIND --------------------------------------------------------------
printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/finio/finio_test.bas >/dev/null 2>&1; then
        ok "no definite leak or invalid access over the value model"
    else
        bad "valgrind"
    fi
else
    printf '  SKIP valgrind (unavailable)\n'
fi

if [ "$fails" = "0" ]; then
    printf 'run_finio: all cases passed\n'
else
    printf 'run_finio: %d FAILED\n' "$fails"
    exit 1
fi
