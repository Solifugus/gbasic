#!/usr/bin/env bash
# THE MONITORING MECHANISM (docs/financial_adapters_design.md §13 "The
# monitoring mechanism", and §9's ObservationLog).
#
# §13 said what to RECORD and what to do ONCE A REVISION IS DISCOVERED, and
# never said what a watch source is, what checking one does, what triggers a
# check or what it emits. Without those, "continually maintained" is an
# intention rather than a capability -- and measured before this shipped, all
# five of §13's own maintenance fields were REFUSED BY NAME by
# check_registry_entry, exactly as §9's five had been one section along.
#
# TWO STRUCTURAL TIERS CARRY THE ARCHITECTURE, and neither can be expressed
# behaviourally. IT NEVER UPDATES: a process that modified an adapter would
# silently change how a file written in 2019 is read, which is the one thing
# Axiom 4 forbids and §13 step 7 restates -- so the library must contain no
# route to an adapter at all, which is a fact about the SOURCE. And IT
# PERFORMS NO I/O: `check` is a pure function of what the caller fetched, which
# is what lets a maintenance process be tested with no network -- a gate that
# needs the internet goes red when somebody else's site is down and then gets
# turned off.
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

printf 'TIER semantics\n'
out="$scratch/w.out"
if timeout 120 ./gbasic tests/finio/watch_test.bas >"$out" 2>&1; then
    mism="$(sed -n 's/^mismatches: //p' "$out")"
    checks="$(sed -n 's/^checks: //p' "$out")"
    if [ "$mism" = "0" ] && [ "${checks:-0}" -ge 34 ]; then
        ok "$checks checks, 0 mismatches"
    else
        bad "watch_test: $checks checks, $mism mismatches"
        grep MISMATCH "$out" || true
    fi
else
    bad "watch_test exited nonzero"; cat "$out"
fi

# --- IT NEVER UPDATES AN ADAPTER -------------------------------------------
# The load-bearing architectural claim, and it cannot be tested behaviourally:
# a function that returned a modified adapter would look exactly like one that
# returned a finding until the day it was used. So the tripwire reads the
# source. `finio_watch` has no business constructing an adapter, calling one,
# or naming an adapter's internals.
printf 'TIER never_updates\n'
forbidden=0
for pat in 'finio\.adapter\(' '\.recognise' '\.read_source' '\.write_doc' '\.validate_doc' 'registry_entry ='; do
    if grep -qE "$pat" stdlib/finio_watch.bas; then
        bad "finio_watch.bas contains '$pat' -- this library detects, it does not update (Axiom 4, §13 step 7)"
        forbidden=$((forbidden + 1))
    fi
done
if [ "$forbidden" = "0" ]; then
    ok "no route to an adapter: it produces work, never a patch"
fi
# AND THE TRIPWIRE IS NOT VACUOUS: it must actually match when the thing it
# forbids is present, or it is six greps over a file that could say anything.
cp stdlib/finio_watch.bas "$scratch/probe.bas"
printf '\n%s\n' "' x = finio.adapter({})" >> "$scratch/probe.bas"
if grep -qE 'finio\.adapter\(' "$scratch/probe.bas"; then
    ok "CONTROL: the tripwire matches when the forbidden construct is present"
else
    bad "the tripwire does not match its own probe -- it is asserting nothing"
fi

# --- IT PERFORMS NO I/O ----------------------------------------------------
printf 'TIER no_io\n'
io=0
for pat in '[^_a-z]read\(' '[^_a-z]write\(' 'list_files' 'http\.' 'webclient\.' '\{file\}' '\{dir\}' 'process\.'; do
    if grep -qE "$pat" stdlib/finio_watch.bas; then
        bad "finio_watch.bas contains '$pat' -- checking is a pure function of what the CALLER fetched"
        io=$((io + 1))
    fi
done
if [ "$io" = "0" ]; then
    ok "no I/O: the application decides when and how to fetch"
fi

# --- THE MECHANISM HAS SOMETHING TO WATCH ----------------------------------
# A mechanism with no caller is a mechanism nothing checks. Every registry
# entry must declare a maintenance priority, and every entry that is not
# dormant must name at least one watch source -- otherwise "continually
# maintained" is a library rather than a practice.
printf 'TIER wired_up\n'
cat > "$scratch/wired.bas" <<'BEOF'
load finio
load finio_nacha
load finio_camt
load finio_registry
load finio_watch
program main( args )
    all = finio_registry.all([ finio_nacha.adapter(), finio_camt.adapter() ])
    unpriced = 0
    unwatched = 0
    sources = 0
    dormant = 0
    for each e in all
        if not has(e, "maintenance_priority") then
            unpriced = unpriced + 1
        else
            if e.maintenance_priority = "dormant" then
                dormant = dormant + 1
            else
                n = 0
                if has(e, "watch_sources") then
                    n = count(e.watch_sources)
                end if
                if n = 0 then
                    unwatched = unwatched + 1
                end if
                sources = sources + n
            end if
        end if
        if has(e, "watch_sources") then
            for each w in e.watch_sources
                v = finio_watch.source(w.kind, { reference: w.reference, watching: w.watching })
            end for
        end if
    end for
    print ("entries " + string(count(all)) + " unpriced " + string(unpriced)
           + " unwatched " + string(unwatched) + " sources " + string(sources)
           + " dormant " + string(dormant))
end program
BEOF
w="$(timeout 60 ./gbasic "$scratch/wired.bas" 2>&1 | sed -n 's/^entries //p')"
set -- $w
if [ "${3:-1}" = "0" ] && [ "${5:-1}" = "0" ] && [ "${7:-0}" -ge 9 ] && [ "${9:-0}" -ge 1 ]; then
    ok "all $1 entries priced, $7 watch sources declared, $9 declared dormant rather than left unwatched"
else
    bad "wired_up: got [$w], wanted 0 unpriced, 0 unwatched, at least 9 sources and at least 1 dormant"
fi

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/finio/watch_test.bas >/dev/null 2>&1; then
        ok "no definite leak or invalid access"
    else
        bad "valgrind"
    fi
else
    printf '  SKIP valgrind (unavailable)\n'
fi

if [ "$fails" = "0" ]; then
    printf 'run_finio_watch: all cases passed\n'
else
    printf 'run_finio_watch: %d FAILED\n' "$fails"
    exit 1
fi
