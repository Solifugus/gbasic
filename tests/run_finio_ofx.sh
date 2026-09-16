#!/usr/bin/env bash
# OFX -- the FOURTH adapter, and §20's SCHEMA-DRIFT CASE OCCURRING INSIDE ONE
# FORMAT (docs/financial_adapters_design.md §20).
#
# OFX 1.x is SGML-like and NOT well-formed XML -- a leaf is `<CODE>0` with no
# closing tag -- while 2.x is proper XML. Same element tree, two
# serializations, and a file of each kind is ordinary. Until now schema drift
# had only arrived BETWEEN formats.
#
# ONE READER SERVES BOTH, AND CONVERSION WAS REJECTED. Inserting the missing
# closing tags and handing the result to `xml.parse` is what more than one
# public tool does, and it would put every LOCATION into text the bank never
# sent -- Axiom 2 says a value is traceable to its source, and a byte offset
# into a document this library invented is not provenance.
#
# THE LOAD-BEARING TIER IS `dialects`, inside the fixture: the same logical
# statement written both ways must give ONE answer. The foreign corpus cannot
# supply that pair, because every real file is one dialect or the other -- so
# it is generated, and without it a reader handling only one would pass.
#
# OFX STATES NO CONTROL TOTAL, which is worth naming because the three formats
# before it all did. Its ledger balance is a BALANCE, not a sum of anything in
# the file. So validation checks what consumers actually depend on instead: that
# the response is not an ERROR, and that FITIDs are UNIQUE -- the latter being
# the whole of OFX deduplication, where a duplicate silently drops or doubles a
# transaction.
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
out="$scratch/o.out"
if timeout 180 ./gbasic tests/finio/ofx_test.bas >"$out" 2>&1; then
    mism="$(sed -n 's/^mismatches: //p' "$out")"
    checks="$(sed -n 's/^checks: //p' "$out")"
    if [ "$mism" = "0" ] && [ "${checks:-0}" -ge 37 ]; then
        ok "$checks checks, 0 mismatches"
    else
        bad "ofx_test: $checks checks, $mism mismatches"
        grep MISMATCH "$out" || true
    fi
else
    bad "ofx_test exited nonzero"; cat "$out"
fi

# --- THE CORPUS REALLY CARRIES BOTH DIALECTS -------------------------------
# The fixture asserts that a reader handles both. That is only a claim about
# dialects if the corpus contains both, which the fixture cannot establish
# about the files on disk and this can.
printf 'TIER corpus_premise\n'
sg=0; xm=0
for f in tests/finio/foreign_ofx/*.ofx; do
    if head -3 "$f" | grep -q '<?OFX'; then xm=$((xm + 1)); else sg=$((sg + 1)); fi
done
if [ "$sg" -ge 3 ] && [ "$xm" -ge 3 ]; then
    ok "$sg files are 1.x SGML and $xm are 2.x XML"
else
    bad "the corpus is not split across dialects: $sg sgml, $xm xml"
fi
# AND THE DIFFERENCE IS ABOUT LEAVES, NOT TAGS IN GENERAL. Both dialects close
# AGGREGATES -- </STATUS>, </BANKACCTFROM>, </OFX> appear in a 1.x file too --
# and only 2.x closes a LEAF. A tripwire that just looked for closing tags
# would pass on two 1.x files, which is how this one was written first and why
# it failed: it named a tag the 2.x file does not carry. `BALAMT` is a leaf both
# files have.
lv1=tests/finio/foreign_ofx/checking.ofx
lv2=tests/finio/foreign_ofx/suncorp.ofx
if grep -q '<BALAMT>' "$lv1" && grep -q '<BALAMT>' "$lv2" \
   && ! grep -q '</BALAMT>' "$lv1" && grep -q '</BALAMT>' "$lv2" \
   && grep -q '</STATUS>' "$lv1"; then
    ok "a 1.x LEAF has no closing tag where a 2.x one does, and both close aggregates"
else
    bad "the dialect difference is not present in the corpus as described"
fi

# --- PROVENANCE AND RIGHTS -------------------------------------------------
printf 'TIER provenance\n'
man=tests/finio/foreign_ofx/PROVENANCE.txt
if [ ! -f "$man" ] || [ ! -f tests/finio/foreign_ofx/LICENSE-ofxparse ]; then
    bad "the foreign vectors are redistributed without a manifest or the licence beside them"
else
    miss=0; n=0
    for f in tests/finio/foreign_ofx/*.ofx; do
        b="$(basename "$f")"; n=$((n + 1))
        line="$(grep "^$b	" "$man" || true)"
        if [ -z "$line" ]; then bad "  $b appears nowhere in PROVENANCE.txt"; miss=$((miss + 1)); continue; fi
        want="$(printf '%s' "$line" | cut -f2)"
        got="$(sha256sum "$f" | cut -d' ' -f1)"
        [ "$want" = "$got" ] || { bad "  $b does not match its recorded hash"; miss=$((miss + 1)); }
    done
    [ "$miss" = "0" ] && [ "$n" -ge 10 ] && ok "all $n foreign vectors carry a source, a date, a licence and a matching hash"
    notok="$(awk -F'\t' '/^[^#]/ && NF>=7 && $7 != "yes" { print $1 }' "$man" | tr '\n' ' ')"
    [ -z "$notok" ] && ok "and every one permits redistribution" || bad "committed without redistribution rights: $notok"
    grep -q 'EXCLUDED' "$man" && ok "and what was NOT used says why" || bad "no exclusions recorded"
fi

printf 'TIER generator\n'
if command -v python3 >/dev/null 2>&1; then
    python3 tools/make_ofx_fixture.py "$scratch/regen" >/dev/null 2>&1
    if diff -r tests/finio/ofx "$scratch/regen" >/dev/null 2>&1; then
        ok "the committed fixtures are byte-identical to what the generator writes"
    else
        bad "the committed fixtures have drifted from tools/make_ofx_fixture.py"
        diff -rq tests/finio/ofx "$scratch/regen" || true
    fi
else
    printf '  SKIP generator drift (no python3)\n'
fi

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/finio/ofx_test.bas >/dev/null 2>&1; then
        ok "no definite leak or invalid access over the adapter"
    else
        bad "valgrind"
    fi
else
    printf '  SKIP valgrind (unavailable)\n'
fi

if [ "$fails" = "0" ]; then
    printf 'run_finio_ofx: all cases passed\n'
else
    printf 'run_finio_ofx: %d FAILED\n' "$fails"
    exit 1
fi
