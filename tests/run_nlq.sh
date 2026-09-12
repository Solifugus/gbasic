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
    if [ "$mism" = "0" ] && [ "${checks:-0}" -ge 37 ]; then
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

# --- A SECOND ESTATE, in a different domain --------------------------------
# The gas-trading estate is the one the grounding was developed against, so it
# cannot say whether anything was tuned to it. The bank estate is a different
# vocabulary (accounts, ledgers, ACH, delinquency) that nothing here was written
# for, and NO SYNONYMS ARE DECLARED FOR IT.
#
# IT CARRIES THE `legacy_nightmare` PRESET, which is the whole reason to use it:
# nine planted pathologies, eleven decoys, SIX COLUMNS NAMED `status` across six
# tables, and three `audit_log*` tables that look like a foreign key to
# everything. The clean bank has none of that and is a much weaker benchmark.
#
# BUT RECALL ALONE PROVES LITTLE HERE AND THE TIER SAYS SO: the bank is 21
# objects and the limit is 8, so a selection is over a third of the estate and
# even a poor ranking recalls a lot. The load-bearing number is the RATIO --
# what was dragged in per table actually needed. On the 121-object estate that
# ratio is what stops "select everything" from scoring perfectly; here it is
# what stops a small estate from flattering the result.
printf 'TIER second_estate\n'
bank="$(timeout 300 ./gbasic tests/nlq/nlq_score.bas tests/nlq/estate_bank.json 2>&1)"
b_r="$(printf '%s\n' "$bank" | sed -n 's|^RECALL \([0-9]*\)/.*|\1|p')"
b_n="$(printf '%s\n' "$bank" | sed -n 's|^RECALL [0-9]*/\([0-9]*\)|\1|p')"
b_sel="$(printf '%s\n' "$bank" | sed -n 's|^SELECTED \([0-9]*\) .*|\1|p')"
b_need="$(printf '%s\n' "$bank" | sed -n 's|^SELECTED [0-9]* tables for \([0-9]*\) .*|\1|p')"
if [ -z "$b_r" ]; then
    bad "the bank estate produced no RECALL line: $bank"
else
    [ "$b_r" = "$b_n" ] && ok "a different domain, no synonyms declared: $b_r/$b_n" \
        || bad "bank recall $b_r/$b_n"
    # 40 for 8 measured, against a theoretical maximum of 56 (7 questions at a
    # limit of 8). A ceiling, because on an estate this small recall is cheap
    # and only the ratio distinguishes a search from a sweep.
    if [ "${b_sel:-9999}" -le 48 ]; then
        ok "and $b_sel table-slots for $b_need needed, on a 21-object estate"
    else
        bad "bank selection ran away: $b_sel for $b_need"
    fi
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

# --- R6 AGAINST THE PLANTED NULL REGION ------------------------------------
# The fixture's own pathology, end to end. estateforge plants three identically
# named `tmp_load_notes` tables in staging/staging_2/staging_3 and its truth
# says any lineage reported there is INVENTED. The grounding must find them and
# must NOT pick one -- and asking it for an answer must be refused.
#
# ASSERTED ON THE REAL FIXTURE, not a hand-built catalog, because the hand-built
# one in nlq_test.bas is a shape I chose and this is a shape estateforge planted
# to defeat a tool.
printf 'TIER null_region\n'
cat > "$scratch/nr.bas" <<'BEOF'
program main( args )
    load nlq
    f {file}= args[0]
    c = decode(read(f))
    cat = { tables: c.tables, columns: c.columns }
    g = nlq.ground(cat, "notes in the temporary load table", { limit: 8 })
    print ("AMBIG " + string(count(g.ambiguous)))
    for each a in g.ambiguous
        print ("KIND " + a.kind + " " + join(a.candidates, " "))
    end for
    on error goto next
    ok = nlq.check_answerable(g)
    if error then
        print "REFUSED"
        error.clear()
    else
        print "ANSWERED"
    end if
    on error stop
end program
BEOF
nr="$(timeout 60 ./gbasic "$scratch/nr.bas" tests/nlq/estate_bank.json 2>&1)"
if printf '%s\n' "$nr" | grep -q 'KIND same_name_different_schema .*staging_2.tmp_load_notes'; then
    ok "the planted null region is reported, all three candidates named"
else
    bad "null region not reported: $nr"
fi
if printf '%s\n' "$nr" | grep -q '^REFUSED'; then
    ok "and producing an ANSWER from it is refused"
else
    bad "an answer was produced from the null region: $nr"
fi

# --- THE FALSE-POSITIVE RATE, which is what keeps R6 honest ----------------
# MEASURED BEFORE THE DISCRIMINATOR EXISTED: 15 of the 16 demo questions carried
# an "ambiguity" and answering was refused for nearly all of them. A refusal
# that fires on everything is indistinguishable from having no tool -- the same
# failure the blind-shadow warning had at 287 false positives before it was
# reverted, and the same one `insight`'s first threshold had when it cleared
# half of all pure-noise populations.
#
# Two causes, both fixed: `trading.deal` / `trading_apac.deal` are regional
# partitions whose SCHEMA NAMES SAY SO, and a tie at the cut does not block an
# answer (true of 13 of 16 questions, so gating on it refuses almost everything
# while the needed table sits inside the cut anyway -- it is disclosure in
# `search.cut_tied` now).
#
# ASSERTED AS A DIFFERENCE: silent where nothing is planted, firing where
# something is. Either half alone is satisfied by a check that always answers or
# always refuses.
printf 'TIER refusal_rate\n'
cat > "$scratch/rate.bas" <<'BEOF'
program main( args )
    load nlq
    f {file}= args[0]
    c = decode(read(f))
    cat = { tables: c.tables, columns: c.columns }
    n = 0
    amb = 0
    for each q in c.questions
        g = nlq.ground(cat, q.text, { limit: 8 })
        n = n + 1
        if count(g.ambiguous) > 0 then
            amb = amb + 1
        end if
    end for
    print ("RATE " + string(amb) + " " + string(n))
end program
BEOF
d_rate="$(timeout 120 ./gbasic "$scratch/rate.bas" tests/nlq/estate_demo.json 2>&1 | sed -n 's/^RATE //p')"
b_rate="$(timeout 120 ./gbasic "$scratch/rate.bas" tests/nlq/estate_bank.json 2>&1 | sed -n 's/^RATE //p')"
d_amb="${d_rate%% *}"; d_n="${d_rate##* }"
b_amb="${b_rate%% *}"; b_n="${b_rate##* }"
if [ "${d_amb:-99}" -eq 0 ]; then
    ok "silent on all $d_n questions of the estate with nothing planted for it"
else
    bad "R6 fired on $d_amb of $d_n demo questions -- it is noise"
fi
if [ "${b_amb:-0}" -ge 1 ] && [ "${b_amb:-99}" -lt "$b_n" ]; then
    ok "and fires on $b_amb of $b_n where the null region IS planted"
else
    bad "R6 fired on $b_amb of $b_n bank questions -- wanted some but not all"
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
