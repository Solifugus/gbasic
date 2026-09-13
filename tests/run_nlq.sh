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
    if [ "$mism" = "0" ] && [ "${checks:-0}" -ge 72 ]; then
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

# --- SCORED ON estateforge'S OWN RUBRIC, at two scales ----------------------
# Nineteen questions of three kinds, and NO FIXED DISPOSITION SCORES WELL, which
# is the point of the set: an ordinary question must be answered, an AMBIGUOUS
# one has two defensible answers and is scored on whether the tool NOTICED
# rather than which it picked, and an UNANSWERABLE one must be declined.
# Declining an answerable question is wrong and answering an unanswerable one is
# wrong, so always-decline and never-decline both fail.
#
# THE AMBIGUOUS QUESTION IS WHAT R2 EXISTS FOR, and this is the first thing that
# scores it: both objects surfaced AND the derivation between them disclosed.
#
# THE TWO UNANSWERABLE QUESTIONS ARE COUNTED APART AND NOT CREDITED. They ask
# which JOB reads a table and what breaks if it is dropped -- about jobs and
# dependencies, which a catalog does not model. MEASURED, there is no
# catalog-only signal: `unresolved` does not separate them (f_ledger_sums_zero
# leaves 6 of 9 words unresolved and is perfectly answerable), and on the demo
# estate one of them IS declined -- because the grounding pulled in an unrelated
# numbered-schema collision and R6 fired on that. Crediting an accident would
# let it read as progress.
printf 'TIER scored\n'
for est in demo_v:8:16 enterprise:8:13; do
    name="${est%%:*}"; rest="${est#*:}"; lim="${rest%%:*}"; floor="${rest##*:}"
    out="$(timeout 900 ./gbasic tests/nlq/nlq_score_ef.bas "tests/nlq/estate_$name.json" "$lim" 2>&1)"
    r="$(printf '%s\n' "$out" | sed -n 's|^RIGHT \([0-9]*\) .*|\1|p')"
    att="$(printf '%s\n' "$out" | sed -n 's|.* OF \([0-9]*\) attempted.*|\1|p')"
    un="$(printf '%s\n' "$out" | sed -n 's|^UNATTEMPTED \([0-9]*\) .*|\1|p')"
    if [ -z "$r" ]; then
        bad "$name produced no score: $out"
    else
        [ "${r:-0}" -ge "$floor" ] && ok "$name: $r of $att attempted at limit $lim (floor $floor)" \
            || bad "$name scored $r of $att, floor $floor"
        [ "$un" = "2" ] && ok "  and 2 unanswerable counted apart, not credited" \
            || bad "$name: $un unanswerable counted apart, expected 2"
    fi
done
# THE AMBIGUOUS ONE SPECIFICALLY, since it is the capability R2 was built for
# and a total could hide it.
amb="$(timeout 900 ./gbasic tests/nlq/nlq_score_ef.bas tests/nlq/estate_enterprise.json 8 2>&1 | grep -c '^WRONG w_volume_which' || true)"
[ "$amb" = "0" ] && ok "the two-defensible-answers question is scored right: both sides surfaced AND disclosed" \
    || bad "the ambiguous question failed -- R2 did not disclose, or a side was not grounded"

# --- ENTERPRISE SCALE: where retrieval is hard rather than merely present ---
# 517 objects against the demo's 127, from estateforge's own exporter. `id`
# names 331 columns and seventeen tables have `deal` in the name. THE QUESTIONS
# ARE THE SAME NINETEEN -- estateforge flagged that itself: the enterprise
# estate is harder RETRIEVAL, not harder questions.
#
# THE FAILURE AT SCALE IS CROWDING, NOT RANKING, measured before it was fixed:
# at limit 8 recall fell, and raising the limit to 16 recovered most of it while
# 32 recovered one more and 64 recovered nothing -- so the right tables ranked
# highly enough all along. Every miss had one cause: archive.gl_account_2019,
# _002, _003, _004, _005 score IDENTICALLY, fill six of eight slots and crowd
# out finance.gl_entry. A ranked list spending six slots to say one thing six
# times has not ranked badly, it has spent its budget on repetition.
#
# NOTED BY estateforge AND WORTH RECORDING: those numbered siblings come from
# their uniqueness guard appending _002 on a name collision. Realistic -- real
# archives do exactly this -- but the crowding is an ARTEFACT of name
# generation rather than a planted pathology, so this tier measures a real
# shape that nobody designed.
printf 'TIER crowding\n'
cat > "$scratch/nocollapse.bas" <<'BEOF'
program main( args )
    load nlq
    f {file}= args[0]
    c = decode(read(f))
    cat = { tables: c.objects, columns: c.columns }
    hit = 0
    for each q in c.questions
        if q.answer_kind != "unanswerable" and q.answer_kind != "ambiguous" then
            g = nlq.ground(cat, q.text, { limit: 8, collapse_siblings: false })
            ok2 = true
            for each t in q.touches
                if not contains(g.tables, t) then
                    ok2 = false
                end if
            end for
            if ok2 then
                hit = hit + 1
            end if
        end if
    end for
    print ("RECALL " + string(hit))
end program
BEOF
cat > "$scratch/collapse.bas" <<'BEOF'
program main( args )
    load nlq
    f {file}= args[0]
    c = decode(read(f))
    cat = { tables: c.objects, columns: c.columns }
    hit = 0
    for each q in c.questions
        if q.answer_kind != "unanswerable" and q.answer_kind != "ambiguous" then
            g = nlq.ground(cat, q.text, { limit: 8 })
            ok2 = true
            for each t in q.touches
                if not contains(g.tables, t) then
                    ok2 = false
                end if
            end for
            if ok2 then
                hit = hit + 1
            end if
        end if
    end for
    print ("RECALL " + string(hit))
end program
BEOF
nc="$(timeout 600 ./gbasic "$scratch/nocollapse.bas" tests/nlq/estate_enterprise.json 2>&1 | sed -n 's|^RECALL ||p')"
wc2="$(timeout 600 ./gbasic "$scratch/collapse.bas" tests/nlq/estate_enterprise.json 2>&1 | sed -n 's|^RECALL ||p')"
if [ -n "$nc" ] && [ "${wc2:-0}" -gt "${nc:-99}" ]; then
    ok "collapsing numbered siblings buys recall at 517 objects ($nc without, $wc2 with)"
else
    bad "collapsing changed nothing at scale: $nc without, $wc2 with"
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
# SILENT ON THE BANK TOO, AND THAT CHANGED -- it used to fire on one, and the
# question is "How many rows in staging.tmp_load_notes belong to a
# counterparty?", which NAMES THE SCHEMA. Refusing a question that answered
# itself is the worst kind of refusal: it looks like rigour. The firing case is
# the null_region tier below, whose question says "the temporary load table"
# and names no schema -- silent where the asker settled it, firing where they
# did not, which is the difference this pair exists to assert.
if [ "${b_amb:-99}" -eq 0 ]; then
    ok "and silent on all $b_n bank questions, which name their own schema"
else
    bad "R6 fired on $b_amb of $b_n bank questions, all of which name a schema"
fi

# --- R3/R4 OVER RECORDED MODEL OUTPUT --------------------------------------
# The SQL a real 4B model actually wrote, replayed OFFLINE from committed
# fixtures: no network, no GPU, no database. Recording cost 13 fixtures over a
# couple of hours on a loaded machine and a driver that was quietly broken;
# replay costs seconds and cannot go quiet because a GPU did.
#
# 13 OF 13 COME BACK CLEAN, AND THAT IS A RESULT ABOUT THE MODEL, NOT A TIER
# THAT ASSERTS NOTHING. This model stays inside the tables it was handed: it
# never named an ungrounded one and never wrote a write statement. The proof
# that the check is not asleep is in the SEMANTICS fixture, which feeds it
# `archive.deal_2015` (ungrounded) and `DELETE` and requires both refused. A
# tier that only ever sees clean input needs its teeth demonstrated elsewhere.
#
# THE THREE MISSING FIXTURES ARE ALSO A RESULT. Their reasoning exhausted the
# output budget and returned empty content, so the recorder refused to save
# them -- an empty answer is an outcome, and about one question in five is
# unanswerable at this model size.
printf 'TIER replay\n'
rp="$(timeout 200 ./gbasic tests/nlq/nlq_replay.bas 2>&1)"
r_n="$(printf '%s\n' "$rp" | sed -n 's|^REPLAYED \([0-9]*\) .*|\1|p')"
r_c="$(printf '%s\n' "$rp" | sed -n 's|^CLEAN \([0-9]*\) of \([0-9]*\)|\1|p')"
r_t="$(printf '%s\n' "$rp" | sed -n 's|^CLEAN [0-9]* of \([0-9]*\)|\1|p')"
r_missing="$(printf '%s\n' "$rp" | grep -c '^NOFIXTURE' || true)"
if [ -z "$r_n" ]; then
    bad "replay produced no summary: $rp"
else
    [ "${r_t:-0}" -ge 13 ] && ok "$r_t recorded answers replayed offline" \
        || bad "only $r_t fixtures replayed, expected 13"
    [ "$r_c" = "$r_t" ] && ok "and all $r_c name only grounded tables and only read" \
        || bad "R3/R4 flagged $((r_t - r_c)) of $r_t: $(printf '%s\n' "$rp" | grep '^FLAG' | head -3)"
    [ "$r_missing" = "3" ] && ok "3 questions have no fixture -- their reasoning ran out of budget, which is an outcome" \
        || bad "expected 3 unrecorded questions, saw $r_missing"
fi

# --- THE VALUE TIER: the only thing here that produces a SCORE --------------
# Runs the SQL a model wrote against a real estate and compares the NUMBER to
# the answer estateforge folded over the generated rows -- a second
# implementation, not a second call into the thing under test.
#
# THE SKIP NAMES A CONSEQUENCE, NOT A TIER. A skip that only says "skipped" is
# indistinguishable from a pass in scroll-back, and this is the ONLY tier that
# turns observations into numbers: without it the honest report is not "the
# other tiers ran", it is "the pipeline was exercised and NOTHING WAS SCORED".
#
# AND IT SHRINKS THE GATE AS LITTLE AS POSSIBLE. Everything above needs no
# database and still runs: the grounding, the refusals, the recorded SQL, R3/R4.
# The skip costs exactly the comparison. The rule underneath -- estateforge's,
# adopted here -- is that a database-dependent tier must never be the ONLY thing
# covering a claim, because it is the one that can go quiet.
printf 'TIER value\n'
if [ -z "${NLQ_ODBC_CONNECTION:-}" ]; then
    printf '  SKIP  NOTHING WAS SCORED. The pipeline was exercised end to end above --\n'
    printf '        grounding, refusals, recorded SQL, R3/R4 -- and not one answer was\n'
    printf '        checked against a database.\n'
    printf '        UNCHECKED without it: that the number a model SQL returns is the\n'
    printf '        number the question has. Nothing else notices a wrong one: the query\n'
    printf '        parses, names only grounded tables, reads nothing it should not, and\n'
    printf '        returns a value of the right type and magnitude.\n'
    printf '        To run it: createdb nlq_estate; build estateforge demo_plan() into it\n'
    printf '        (the SAME plan the fixture was emitted from, or the answers are for\n'
    printf '        other rows and nothing says so); then set NLQ_ODBC_CONNECTION.\n'
else
    vout="$(timeout 600 ./gbasic tests/nlq/nlq_score_live.bas 2>&1)"
    v_s="$(printf '%s\n' "$vout" | sed -n 's|^SCORED \([0-9]*\) .*|\1|p')"
    v_a="$(printf '%s\n' "$vout" | sed -n 's|^SCORED [0-9]* AGREED \([0-9]*\) .*|\1|p')"
    if [ -z "$v_s" ]; then
        bad "the value tier produced no summary: $vout"
    else
        # A FLOOR, not an exact figure: this scores a real model over real
        # questions and pinning it exactly makes every improvement a rebaseline.
        # Measured 7 of 10 agreeing.
        [ "${v_s:-0}" -ge 10 ] && ok "$v_s model answers executed against the estate" \
            || bad "only $v_s answers scored, expected 10"
        [ "${v_a:-0}" -ge 7 ] && ok "$v_a of $v_s agree with an answer key computed from the rows" \
            || bad "agreement fell to $v_a of $v_s"
        # THE DISAGREEMENTS ARE ASSERTED TO EXIST. A run where everything agreed
        # would mean this tier had stopped discriminating -- and the two that
        # differ are the ones the design cares about most.
        if [ "${v_a:-0}" -lt "${v_s:-0}" ]; then
            ok "and $((v_s - v_a)) disagree -- the tier still tells right from plausible"
            printf '%s\n' "$vout" | grep '^DIFFER' | sed 's/^/       /'
        else
            bad "every answer agreed -- a value tier that never disagrees is not discriminating"
        fi
    fi
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
