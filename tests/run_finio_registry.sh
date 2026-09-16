#!/usr/bin/env bash
# THE FORMAT REGISTRY (docs/financial_adapters_design.md §9, §10, §14).
#
# §9 says the registry "may ultimately be as important as any individual
# adapter" and that it "should include formats even when implementation is
# presently impossible -- such entries become a research and acquisition
# queue". Until 2026-09-15 it held TWO entries and both lived inside the
# adapter that implemented them, so it recorded only what had already been
# done, which is the one thing a registry is not for. Worse, and measured:
# five of the fields §9 itself specifies were REFUSED BY NAME by our own
# validator, including `source_url_or_reference` and `date_retrieved` -- so a
# discovery pass could not have written down what it found.
#
# THE LOAD-BEARING TIER IS `classification`, and it is a DIFFERENCE. Every
# other check passes on a registry in which every format is OPEN and nothing is
# blocked -- a comfortable and false picture of this industry, and one that
# would make `acquisition_class` a constant with a type. What must hold is that
# the classification SEPARATES: several classes present, some formats
# implementable without asking anyone, and some blocked with a named reason.
#
# WHAT THIS IS NOT. §14 names twenty-two candidate domains and this tranche
# touches six families. That gap is reported as a VALUE by `coverage()` and
# asserted here, because a registry that knows what it does not know is worth
# something and one that merely looks short is not.
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
out="$scratch/reg.out"
if timeout 120 ./gbasic tests/finio/registry_test.bas >"$out" 2>&1; then
    mism="$(sed -n 's/^mismatches: //p' "$out")"
    checks="$(sed -n 's/^checks: //p' "$out")"
    if [ "$mism" = "0" ] && [ "${checks:-0}" -ge 27 ]; then
        ok "$checks checks, 0 mismatches"
    else
        bad "registry_test: $checks checks, $mism mismatches"
        grep MISMATCH "$out" || true
    fi
else
    bad "registry_test exited nonzero"; cat "$out"
fi

# --- THE CITATIONS ARE REAL URLS -------------------------------------------
# A registry whose evidence is a sentence rather than a reference is a registry
# that cannot be re-checked, which is the whole of §13's maintenance story.
# This does NOT fetch them -- a gate that needs the network is a gate that goes
# red when someone else's site is down -- it asserts they are addresses.
printf 'TIER citations\n'
cat > "$scratch/cite.bas" <<'BEOF'
load finio
load finio_nacha
load finio_camt
load finio_registry
program main( args )
    all = finio_registry.all([ finio_nacha.adapter(), finio_camt.adapter() ])
    urls = 0
    dated = 0
    srcs = 0
    for each e in all
        if has(e, "specification_sources") then
            for each s in e.specification_sources
                srcs = srcs + 1
                if contains(s.source_url_or_reference, "https://") then
                    urls = urls + 1
                end if
                if byte_count(string(s.date_retrieved)) = 10 then
                    dated = dated + 1
                end if
            end for
        end if
    end for
    print "sources " + string(srcs) + " urls " + string(urls) + " dated " + string(dated)
end program
BEOF
cite="$(timeout 60 ./gbasic "$scratch/cite.bas" 2>&1 | sed -n 's/^sources //p')"
set -- $cite
if [ "${1:-0}" -ge 8 ] && [ "${3:-0}" = "$1" ] && [ "${5:-0}" -ge 7 ]; then
    ok "$1 sources, all dated, $5 carrying a retrievable https reference"
else
    bad "citations: got [$cite], wanted at least 8 sources, every one dated, at least 7 with an https reference"
fi

# --- THE SURVEY IS REPORTED, NOT JUST STORED -------------------------------
# The answer to "what could we build next" has to be readable, or the registry
# is a data structure nobody consults.
printf 'TIER report\n'
cat > "$scratch/rep.bas" <<'BEOF'
load finio
load finio_nacha
load finio_camt
load finio_registry
program main( args )
    all = finio_registry.all([ finio_nacha.adapter(), finio_camt.adapter() ])
    byc = finio_registry.by_acquisition_class(all)
    for each c in finio.acquisition_classes()
        if count(byc[c]) > 0 then
            print (c + " " + string(count(byc[c])) + ": " + join(byc[c], ", "))
        end if
    end for
    print "IMPLEMENTABLE " + join(finio_registry.implementable(all), ", ")
    for each b in finio_registry.blocked(all)
        print ("BLOCKED " + b.id + " -- " + b.blocked_by)
    end for
    print finio_registry.coverage(all).note
end program
BEOF
rep="$(timeout 60 ./gbasic "$scratch/rep.bas" 2>&1)"
if printf '%s' "$rep" | grep -q '^IMPLEMENTABLE ' && printf '%s' "$rep" | grep -q '^BLOCKED ' \
   && printf '%s' "$rep" | grep -q 'OPEN ' && printf '%s' "$rep" | grep -q 'CONTROLLED '; then
    ok "the survey reports both what is reachable and what is blocked"
    printf '%s\n' "$rep" | sed 's/^/       /'
else
    bad "the survey did not report both halves"; printf '%s\n' "$rep"
fi

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/finio/registry_test.bas >/dev/null 2>&1; then
        ok "no definite leak or invalid access"
    else
        bad "valgrind"
    fi
else
    printf '  SKIP valgrind (unavailable)\n'
fi

if [ "$fails" = "0" ]; then
    printf 'run_finio_registry: all cases passed\n'
else
    printf 'run_finio_registry: %d FAILED\n' "$fails"
    exit 1
fi
