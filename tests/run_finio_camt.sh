#!/usr/bin/env bash
# finio_camt -- ISO 20022 camt.053, the SECOND adapter and the first of a
# different REPRESENTATION (docs/financial_adapters_design.md §20, §21).
#
# WHY THIS FORMAT AND NOT BAI2. §21's Phase 2 asks for "one fixed-width format;
# one hierarchical format; one additional representation", and §20's argument is
# that an abstraction surviving only one representation is a generalized parser
# rather than a description of the problem. BAI2 is record-oriented and
# legacy-heavy -- the SAME FAMILY as NACHA -- so it would exercise the identical
# layout-and-record path and settle nothing. camt pushes back, and did so
# immediately: a hierarchical source has NO BYTE RANGE, which is what turned §4's
# "a universal byte-offset model is too representation-specific" from a sentence
# into `finio.location(kind, detail)`.
#
# THE LOAD-BEARING TIER IS INSIDE THE FIXTURE, not here: `-- §20: DOES THE
# DOCUMENT SHAPE SURVIVE THE REPRESENTATION?` walks a 94-byte fixed-width ACH
# file and a namespaced hierarchical XML statement in ONE PROGRAM and requires
# the same document fields by the same names -- and asserts what did NOT survive
# too, since a NACHA record carries `raw` and a camt record must not pretend to.
#
# WHAT THIS RUNNER ADDS is the thing a fixture cannot be: an ORACLE THAT IS NOT
# OURS. Python's ElementTree reads the same statements and recomputes
# opening + credited - debited, and that must equal both the balance the file
# declares and the figure finio reports. The fixtures and the adapter were both
# written here; ElementTree has seen neither.
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

D=tests/finio/camt

# --- libxml2 --------------------------------------------------------------
# DETECTED BY THE MODULE'S OWN REFUSAL, not by pkg-config: what matters is
# whether THIS binary can parse, and a build flag is a proxy for that.
printf 'TIER build\n'
cat > "$scratch/probe.bas" <<'BEOF'
program main( args )
    load xml
    on error goto next
    d = xml.parse("<a/>")
    if error then
        print "NOXML " + error.message
        error.clear()
    else
        print "XML"
    end if
    on error stop
end program
BEOF
probe="$(timeout 60 ./gbasic "$scratch/probe.bas" 2>&1)"
if ! printf '%s' "$probe" | grep -q '^XML'; then
    printf '  SKIP camt (this build cannot parse XML: %s)\n' "$probe"
    printf 'run_finio_camt: SKIPPED (no libxml2 in this build)\n'
    exit 0
fi
ok "this build can parse XML"

# --- SEMANTICS -------------------------------------------------------------
printf 'TIER semantics\n'
out="$scratch/sem.out"
if timeout 180 ./gbasic tests/finio/camt_test.bas >"$out" 2>&1; then
    mism="$(sed -n 's/^mismatches: //p' "$out")"
    checks="$(sed -n 's/^checks: //p' "$out")"
    if [ "$mism" = "0" ] && [ "${checks:-0}" -ge 89 ]; then
        ok "$checks checks, 0 mismatches"
    else
        bad "camt_test: $checks checks, $mism mismatches"
        grep MISMATCH "$out" || true
    fi
else
    bad "camt_test exited nonzero"; cat "$out"
fi

# --- EXTERNAL ORACLE -------------------------------------------------------
printf 'TIER oracle\n'
if ! command -v python3 >/dev/null 2>&1; then
    printf '  SKIP oracle (no python3)\n'
else
cat > "$scratch/oracle.py" <<'PYEOF'
import sys, re
from decimal import Decimal
import xml.etree.ElementTree as ET

root = ET.parse(sys.argv[1]).getroot()
ns = re.match(r"\{(.*)\}", root.tag).group(1)
def q(*p): return "/".join("{%s}%s" % (ns, x) for x in p)

out = []
for i, st in enumerate(root.findall(q("BkToCstmrStmt") + "/" + q("Stmt"))):
    ccy = st.findtext(q("Acct") + "/" + q("Ccy"))
    opening = closing = None
    for b in st.findall(q("Bal")):
        code = b.findtext(q("Tp") + "/" + q("CdOrPrtry") + "/" + q("Cd"))
        amt = Decimal(b.findtext(q("Amt")))
        if code == "OPBD": opening = amt
        if code == "CLBD": closing = amt
    credits = debits = Decimal("0")
    n = 0
    for e in st.findall(q("Ntry")):
        n += 1
        amt = Decimal(e.findtext(q("Amt")))
        ind = e.findtext(q("CdtDbtInd"))
        if ind == "CRDT": credits += amt
        elif ind == "DBIT": debits += amt
    out.append("computed S%d %s %d %s %s %s" % (i, ccy, n, credits, debits, opening + credits - debits))
    out.append("declared S%d %s %d %s %s %s" % (i, ccy, n, credits, debits, closing))
print("\n".join(out))
PYEOF
for f in statement bad_balance; do
    a="$(python3 "$scratch/oracle.py" "$D/$f.xml" 2>&1)"
    computed="$(printf '%s\n' "$a" | sed -n 's/^computed //p')"
    declared="$(printf '%s\n' "$a" | sed -n 's/^declared //p')"
    got="$(timeout 60 ./gbasic tests/finio/camt_totals.bas "$D/$f.xml" 2>&1)"
    if [ -z "$computed" ] || [ -z "$got" ]; then
        bad "oracle $f: a side produced nothing (python='$computed' finio='$got')"
    elif [ "$f" = "statement" ]; then
        if [ "$computed" = "$declared" ] && [ "$computed" = "$got" ]; then
            ok "$f: ElementTree, the document's own balances and finio all agree"
        else
            bad "$f disagreement"
            printf '    python  : %s\n' "$(printf '%s' "$computed" | tr '\n' ';')"
            printf '    declared: %s\n' "$(printf '%s' "$declared" | tr '\n' ';')"
            printf '    finio   : %s\n' "$(printf '%s' "$got" | tr '\n' ';')"
        fi
    else
        # THE NEGATIVE CONTROL. Without it the tier above is satisfied by three
        # implementations wrong in the same way, and by a comparison that never
        # fails: the corrupted document must make ElementTree and the declared
        # closing balance DIFFER, and finio must side with the entries.
        if [ "$computed" != "$declared" ] && [ "$computed" = "$got" ]; then
            ok "CONTROL: the corrupted document makes python and its own balance differ, and finio sides with the entries"
        else
            bad "control: the corrupted fixture is not corrupted, or finio followed the balance"
            printf '    python [%s] declared [%s] finio [%s]\n' "$computed" "$declared" "$got"
        fi
    fi
done
fi

# --- GENERATOR DRIFT -------------------------------------------------------
# A fixture nothing can reproduce is a fixture that tests itself, and the
# generator is where the balance arithmetic lives.
printf 'TIER generator\n'
if command -v python3 >/dev/null 2>&1; then
    python3 tools/make_camt_fixture.py "$scratch/regen" >/dev/null 2>&1
    if diff -r "$D" "$scratch/regen" >/dev/null 2>&1; then
        ok "the committed fixtures are byte-identical to what the generator writes"
    else
        bad "the committed fixtures have drifted from tools/make_camt_fixture.py"
        diff -rq "$D" "$scratch/regen" || true
    fi
else
    printf '  SKIP generator drift (no python3)\n'
fi

# --- VALGRIND --------------------------------------------------------------
printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/finio/camt_test.bas >/dev/null 2>&1; then
        ok "no definite leak or invalid access over the adapter"
    else
        bad "valgrind"
    fi
else
    printf '  SKIP valgrind (unavailable)\n'
fi

if [ "$fails" = "0" ]; then
    printf 'run_finio_camt: all cases passed\n'
else
    printf 'run_finio_camt: %d FAILED\n' "$fails"
    exit 1
fi
