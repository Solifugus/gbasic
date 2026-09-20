#!/usr/bin/env bash
# ari_discover Phase 4 -- the optional LLM advisor (docs/ari_discover_design.md
# §11). SELF-CHECKING, and forced: every defect here is a PLAUSIBLE NAME.
# `transaction_amount` for `amount` reads as an improvement whether or not
# anything justified it, so a golden would record whichever name came back and
# defend it.
#
# THE SPLIT THIS SUITE EXISTS TO ENFORCE, and it is a CORRECTION to §11. That
# section says every LLM suggestion is "translated into a deterministic
# candidate, executed by ARI, and scored". True of a RULE -- coverage and
# collisions judge it. FALSE OF A NAME: `posted` and `posted_date` parse the
# same corpus to the same rows with the same coverage and the same collisions,
# so running ARI cannot prefer either. A uniform "everything is validated"
# promise therefore has a hole exactly where the advisor is most useful, since
# naming is the one thing Phases 0-3 cannot do without a heading to read.
#
# So a NAME is never adopted: it sits beside the field, carrying who proposed
# it, and a person calls `adopt`. The load-bearing tier asserts the field KEEPS
# ITS DETERMINISTIC NAME while the suggestion is present -- a check that only
# looked for the suggestion passes on a library that silently renamed the field.
#
# NO NETWORK AND NO KEY. Every model call replays a recorded response; a gate
# that needs credentials goes quiet the day a key expires, and one that calls a
# live model can only assert something vague, because the same question twice
# does not give the same words. tests/ari_advisor/record.bas re-records against
# a live provider and is run deliberately.
set -u
cd "$(dirname "$0")/.."

make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }
export GBASIC_PATH="$PWD/stdlib"
status=0

printf 'TIER semantics\n'
out="$(timeout 600 ./gbasic tests/ari_advisor/advisor_test.bas 2>&1)"
if printf '%s' "$out" | grep -q '^mismatches: 0$'; then
    n="$(printf '%s' "$out" | sed -n 's/^checks: //p')"
    printf '  ok   %s checks, 0 mismatches\n' "$n"
    [ "${n:-0}" -ge 24 ] || { printf '  FAIL too few checks ran (%s)\n' "$n"; status=1; }
else
    printf '  FAIL advisor_test\n'
    printf '%s\n' "$out" | grep -E '^MISMATCH|^runtime error|^parse error' | head -5
    status=1
fi

# THE FIXTURE IS THE ORACLE, so it has to be the one the library asks for. A
# stale or renamed fixture makes replay raise rather than answer, which the
# tier above would report as a semantics failure and send somebody reading the
# library. Checked separately so the cause is named.
printf 'TIER replay\n'
n_fix=$(ls tests/ari_advisor/replay/*.json 2>/dev/null | wc -l)
if [ "$n_fix" -ge 1 ]; then
    printf '  ok   %s recorded response(s) present\n' "$n_fix"
else
    printf '  FAIL no replay fixtures; run tests/ari_advisor/record.bas with a key\n'
    status=1
fi

# THE PACKAGE MUST NOT CARRY THE REPORT. Asserted against the library source as
# well as behaviourally, because "the text is absent from this package" is a
# fact about one proposal, where "evidence() never reads source text" is the
# property. A future field that happened to include a line would pass the
# behavioural check on a fixture whose lines are short.
printf 'TIER minimization\n'
if grep -nE '\.text|report_text' stdlib/ari_advisor.bas | grep -vE '^\s*[0-9]+:\s*.$|mask\(' | grep -q .; then
    printf '  FAIL ari_advisor reads source text somewhere:\n'
    grep -nE '\.text|report_text' stdlib/ari_advisor.bas | head -3
    status=1
else
    printf '  ok   the advisor never reads the report, only the proposal\n'
fi

# VALGRIND over the replay path: this library allocates per field and per
# suggestion, and the record/adopt path rebuilds arrays in place.
printf 'TIER valgrind\n'
if [ -f tests/valgrind_tier.sh ]; then
    # shellcheck disable=SC1091
    . tests/valgrind_tier.sh
    if vg_run ./gbasic tests/ari_advisor/advisor_test.bas >/dev/null 2>/tmp/ari_adv_vg.txt; then
        printf '  ok   no definite leak or invalid access\n'
    else
        if [ "${VG_EXIT:-0}" = "0" ]; then
            printf '  ok   no definite leak or invalid access (fixture exit %s)\n' "${VG_EXIT}"
        else
            printf '  FAIL valgrind\n'; head -15 /tmp/ari_adv_vg.txt; status=1
        fi
    fi
else
    printf '  SKIP (no shared valgrind policy)\n'
fi

echo
if [ "$status" = 0 ]; then echo "run_ari_advisor: all cases passed"; fi
exit "$status"
