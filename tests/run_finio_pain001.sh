#!/usr/bin/env bash
# pain.001 -- the FIFTH adapter, and THE FIRST FOR A FILE YOU SEND
# (docs/financial_adapters_design.md §16, §20).
#
# Every adapter before this reads a report, and all four say some version of
# "re-emission only" for `write_status`. A pain.001 is an INSTRUCTION: the
# message a business sends its bank to say "make these payments". That inverts
# where the risk lives -- reading a statement wrongly gives a wrong number on a
# screen; writing a pain.001 wrongly gives a payment run the bank rejects, or
# executes.
#
# WHICH IS WHY §16 HAD NEVER BEEN IMPLEMENTED. "Writing requires stronger
# guarantees than reading... classify the requested conversion as
# representable, lossy or impossible... The default should favor refusal when
# semantic information would be silently lost." Four read-only adapters put no
# weight on that sentence; this one does, and the framework grew
# `finio.classify_write` and a refusing `write_text` to carry it.
#
# THE LOAD-BEARING TIER IS A THREE-WAY DIFFERENCE, inside the fixture: the same
# reader accepts all three documents without complaint and the WRITER treats
# them differently -- one written, one refused-but-overridable, one refused with
# no override at all. Asserting any single one passes on a writer that always
# does that.
#
# THE LIMITS CHECKED ARE THE SCHEME'S, NOT THE SCHEMA'S, and that is the point:
# an XSD accepts a 200-character creditor name, the SEPA scheme carries 70, and
# a file that VALIDATES is exactly the one that gets sent and comes back
# refused.
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

printf 'TIER build\n'
cat > "$scratch/probe.bas" <<'BEOF'
program main( args )
    load xml
    on error goto next
    d = xml.parse("<a/>")
    if error then
        print "NOXML"
        error.clear()
    else
        print "XML"
    end if
    on error stop
end program
BEOF
if ! timeout 60 ./gbasic "$scratch/probe.bas" 2>&1 | grep -q '^XML'; then
    printf '  SKIP pain001 (this build cannot parse XML)\n'
    printf 'run_finio_pain001: SKIPPED (no libxml2 in this build)\n'
    exit 0
fi
ok "this build can parse XML"

printf 'TIER semantics\n'
out="$scratch/p.out"
if timeout 180 ./gbasic tests/finio/pain001_test.bas >"$out" 2>&1; then
    mism="$(sed -n 's/^mismatches: //p' "$out")"
    checks="$(sed -n 's/^checks: //p' "$out")"
    if [ "$mism" = "0" ] && [ "${checks:-0}" -ge 37 ]; then
        ok "$checks checks, 0 mismatches"
    else
        bad "pain001_test: $checks checks, $mism mismatches"
        grep MISMATCH "$out" || true
    fi
else
    bad "pain001_test exited nonzero"; cat "$out"
fi

# --- EXTERNAL ORACLE -------------------------------------------------------
# Python recomputes both levels of control total from the XML. It has seen
# neither this adapter nor the generator, and both were written here.
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
init = root.find(q("CstmrCdtTrfInitn"))
gh = init.find(q("GrpHdr"))
out = []
total = Decimal("0"); ntx = 0
for i, pi in enumerate(init.findall(q("PmtInf"))):
    s = Decimal("0"); n = 0
    for t in pi.findall(q("CdtTrfTxInf")):
        s += Decimal(t.find(q("Amt") + "/" + q("InstdAmt")).text); n += 1
    out.append("block %d %s %d" % (i, s, n))
    out.append("declared %d %s %s" % (i, pi.findtext(q("CtrlSum")), pi.findtext(q("NbOfTxs"))))
    total += s; ntx += n
out.append("message %s %d" % (total, ntx))
out.append("declared_message %s %s" % (gh.findtext(q("CtrlSum")), gh.findtext(q("NbOfTxs"))))
print("\n".join(out))
PYEOF
for f in tests/finio/pain/payments.xml tests/finio/foreign_pain/pain001-batch-sct.xml; do
    py="$(python3 "$scratch/oracle.py" "$f" 2>&1)"
    comp="$(printf '%s\n' "$py" | grep -E '^block|^message' | sed 's/^block /B/; s/^message /M/' | tr '\n' ';')"
    decl="$(printf '%s\n' "$py" | grep -E '^declared' | sed 's/^declared_message /M/; s/^declared /B/' | tr '\n' ';')"
    gb="$(timeout 60 ./gbasic tests/finio/pain001_totals.bas "$f" 2>&1 | tr '\n' ';')"
    if [ -n "$comp" ] && [ "$comp" = "$gb" ] && [ "$comp" = "$decl" ]; then
        ok "$(basename "$f"): python, the document's own control sums and finio all agree"
    else
        bad "$(basename "$f") disagreement"
        printf '    python  : %s\n    declared: %s\n    finio   : %s\n' "$comp" "$decl" "$gb"
    fi
done
# THE NEGATIVE CONTROL: the corrupted document must make python and the
# declared totals DIFFER, and finio must side with the transactions.
py="$(python3 "$scratch/oracle.py" tests/finio/pain/bad_group_sum.xml 2>&1)"
comp="$(printf '%s\n' "$py" | grep -E '^block|^message' | sed 's/^block /B/; s/^message /M/' | tr '\n' ';')"
decl="$(printf '%s\n' "$py" | grep -E '^declared' | sed 's/^declared_message /M/; s/^declared /B/' | tr '\n' ';')"
gb="$(timeout 60 ./gbasic tests/finio/pain001_totals.bas tests/finio/pain/bad_group_sum.xml 2>&1 | tr '\n' ';')"
if [ "$comp" != "$decl" ] && [ "$comp" = "$gb" ]; then
    ok "CONTROL: the corrupted document makes python and its own control sums differ, and finio sides with the transactions"
else
    bad "control: the corrupted fixture is not corrupted, or finio followed the declared sum"
fi
fi

# --- PROVENANCE AND RIGHTS -------------------------------------------------
printf 'TIER provenance\n'
man=tests/finio/foreign_pain/PROVENANCE.txt
if [ ! -f "$man" ] || [ ! -f tests/finio/foreign_pain/LICENSE-synaptica ]; then
    bad "the foreign vectors are redistributed without a manifest or the licence beside them"
else
    miss=0; n=0
    for f in tests/finio/foreign_pain/*.xml; do
        b="$(basename "$f")"; n=$((n + 1))
        line="$(grep "^$b	" "$man" || true)"
        if [ -z "$line" ]; then bad "  $b appears nowhere in PROVENANCE.txt"; miss=$((miss + 1)); continue; fi
        want="$(printf '%s' "$line" | cut -f2)"
        [ "$want" = "$(sha256sum "$f" | cut -d' ' -f1)" ] || { bad "  $b does not match its recorded hash"; miss=$((miss + 1)); }
    done
    [ "$miss" = "0" ] && [ "$n" -ge 3 ] && ok "all $n foreign vectors carry a source, a date, a licence and a matching hash"
    notok="$(awk -F'\t' '/^[^#]/ && NF>=7 && $7 != "yes" { print $1 }' "$man" | tr '\n' ' ')"
    [ -z "$notok" ] && ok "and every one permits redistribution" || bad "committed without redistribution rights: $notok"
    grep -q 'EXCLUDED' "$man" && ok "and what was NOT used says why" || bad "no exclusions recorded"
fi

printf 'TIER generator\n'
if command -v python3 >/dev/null 2>&1; then
    python3 tools/make_pain001_fixture.py "$scratch/regen" >/dev/null 2>&1
    if diff -r tests/finio/pain "$scratch/regen" >/dev/null 2>&1; then
        ok "the committed fixtures are byte-identical to what the generator writes"
    else
        bad "the committed fixtures have drifted from tools/make_pain001_fixture.py"
        diff -rq tests/finio/pain "$scratch/regen" || true
    fi
else
    printf '  SKIP generator drift (no python3)\n'
fi

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/finio/pain001_test.bas >/dev/null 2>&1; then
        ok "no definite leak or invalid access over the adapter"
    else
        bad "valgrind"
    fi
else
    printf '  SKIP valgrind (unavailable)\n'
fi

if [ "$fails" = "0" ]; then
    printf 'run_finio_pain001: all cases passed\n'
else
    printf 'run_finio_pain001: %d FAILED\n' "$fails"
    exit 1
fi
