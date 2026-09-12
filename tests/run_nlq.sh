#!/usr/bin/env bash
# `nlq` FIRST INCREMENT -- retrieval and grounding, with NO MODEL AND NO SQL
# (docs/nlq_design.md).
#
# WHY THIS PART FIRST: discovery_design §2 says schema retrieval must precede
# prompting, because 500 tables do not fit in a context window. That makes
# retrieval a SEARCH, and Recipe 1's finding applies unchanged -- a search
# ALWAYS RETURNS A WINNER. Hand a model twenty tables chosen badly and it writes
# flawless SQL about the wrong ones, and THE SQL RUNS, because the tables it was
# given are real. The number that comes back has the right units and nobody
# downstream can tell.
#
# THE ORACLE IS NOT OURS AND IT NEEDS NO MODEL: estateforge's
# `questions_for(demo_plan())` records, for every question, the `touches[]` it
# actually needs -- computed from the plan rather than by running any SQL. The
# catalog and the questions are COMMITTED as tests/nlq/estate_demo.json so this
# gate is hermetic; a gate you can turn off by not having a sister repository
# checked out is a gate that shrinks.
#
# BOTH HALVES OF RETRIEVAL ARE ASSERTED. Recall alone is maximised by selecting
# every table, which is exactly the failure retrieval exists to prevent;
# precision alone is maximised by selecting nothing. So the suite requires a
# recall floor AND a ceiling on how much was dragged in with it.
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

fixture=tests/nlq/estate_demo.json

# --- SEMANTICS -------------------------------------------------------------
printf 'TIER semantics\n'
out="$scratch/sem.out"
if timeout 120 ./gbasic tests/nlq/nlq_test.bas >"$out" 2>&1; then
    mism="$(sed -n 's/^mismatches: //p' "$out")"
    checks="$(sed -n 's/^checks: //p' "$out")"
    if [ "$mism" = "0" ] && [ "${checks:-0}" -ge 20 ]; then
        ok "$checks checks, 0 mismatches"
    else
        bad "nlq_test: $checks checks, $mism mismatches"; grep MISMATCH "$out" || true
    fi
else
    bad "nlq_test exited nonzero"; cat "$out"
fi

# --- RECALL against estateforge's answer key -------------------------------
printf 'TIER recall\n'
plain="$(timeout 300 ./gbasic tests/nlq/nlq_score.bas "$fixture" 2>&1)"
withs="$(timeout 300 ./gbasic tests/nlq/nlq_score.bas "$fixture" synonyms 2>&1)"
r_plain="$(printf '%s\n' "$plain" | sed -n 's|^RECALL \([0-9]*\)/.*|\1|p')"
r_syn="$(printf '%s\n' "$withs" | sed -n 's|^RECALL \([0-9]*\)/.*|\1|p')"
n_q="$(printf '%s\n' "$plain" | sed -n 's|^RECALL [0-9]*/\([0-9]*\)|\1|p')"
sel="$(printf '%s\n' "$withs" | sed -n 's|^SELECTED \([0-9]*\) .*|\1|p')"

if [ -z "$r_plain" ] || [ -z "$r_syn" ]; then
    bad "scoring produced no RECALL line"
else
    # A FLOOR, not an exact figure: this is a retrieval score over real
    # questions, and pinning it exactly would make every scoring improvement a
    # rebaseline. Measured 13/16 lexical, 14/16 with declared synonyms.
    [ "$r_plain" -ge 12 ] && ok "lexical grounding recalls $r_plain/$n_q (floor 12)" \
        || bad "lexical recall fell to $r_plain/$n_q"
    [ "$r_syn" -ge 14 ] && ok "with declared synonyms, $r_syn/$n_q (floor 14)" \
        || bad "synonym recall fell to $r_syn/$n_q"
    # THE DIFFERENCE, not the level: declared synonyms must HELP, or the option
    # is decoration and the `unresolved` field it exists to close is pointless.
    [ "$r_syn" -gt "$r_plain" ] && ok "declaring a synonym improves recall ($r_plain -> $r_syn)" \
        || bad "synonyms changed nothing ($r_plain -> $r_syn)"
    # AND THE OTHER HALF. 16 questions at a limit of 8 can select at most 128
    # tables out of 121 objects; selecting everything every time would score
    # perfect recall and be useless, so the ceiling is what says the search is
    # a search.
    [ "${sel:-9999}" -le 128 ] && ok "and selected $sel table-slots, never the whole estate" \
        || bad "selection ran away: $sel"
fi

# --- CAPABILITY: a single percentage cannot say what to fix ----------------
printf 'TIER capability\n'
for cap in aggregate join single_table column_disambiguation; do
    line="$(printf '%s\n' "$withs" | sed -n "s|^CAP $cap ||p")"
    got="${line%%/*}"; want="${line##*/}"
    if [ -n "$line" ] && [ "$got" = "$want" ]; then
        ok "$cap $line"
    else
        bad "$cap $line"
    fi
done
# HONEST, AND ASSERTED AS SUCH: lineage_aware cannot be complete in this
# increment. `w_filtered_out` asks which staged rows never reached the fact
# table, and the answer is `trading.ctp` -- reachable only through the ETL's
# own `where k.is_active = 1`, which is lineage and not vocabulary. No lexical
# method can find it, and a suite that demanded 4/4 here would be demanding the
# next increment.
lin="$(printf '%s\n' "$withs" | sed -n 's|^CAP lineage_aware ||p')"
if [ "${lin%%/*}" -ge 3 ]; then
    ok "lineage_aware ${lin} -- the miss needs discovery.lineage, which is not this increment"
else
    bad "lineage_aware ${lin}"
fi

# --- FIXTURE PROVENANCE ----------------------------------------------------
# The fixture must contain VIEWS, not only tables. The first one did not, and
# every `touches` naming warehouse.rpt_volume_gross was therefore unrecallable
# -- lineage_aware scored 0/4 for a defect in the ANSWER KEY'S INPUT that read
# exactly like a weakness in the library. A tripwire, because nothing else
# would say so.
printf 'TIER fixture\n'
if grep -q '"kind": "view"' "$fixture" && grep -q 'rpt_volume_gross' "$fixture"; then
    ok "the catalog includes views, so a question about a report is answerable"
else
    bad "the fixture has no views -- lineage questions cannot be recalled"
fi

# --- VALGRIND --------------------------------------------------------------
printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/nlq/nlq_test.bas >/dev/null 2>&1; then
        ok "no definite leak or invalid access"
    else
        bad "valgrind"
    fi
else
    printf '  SKIP valgrind (unavailable)\n'
fi

if [ "$fails" = "0" ]; then
    printf 'run_nlq: all cases passed\n'
else
    printf 'run_nlq: %d FAILED\n' "$fails"
    exit 1
fi
