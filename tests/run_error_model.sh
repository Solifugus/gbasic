#!/usr/bin/env bash
# PLAT-ERR — the frame-scoped error model (docs/error_model_design.md).
#
# The old model's defects were proven, not suspected: docs/ai/ERRORS.md shows
# that under process-global `on error resume next` a function could not catch
# a raise and return a fallback, because the caller's statement was abandoned
# by the generation check regardless. These fixtures were written BEFORE the
# implementation, from the design, and the first one -- catch_return -- is
# exactly the case the old model could not pass.
#
# The two anti-silence rules get their own fixtures because they are the
# design's whole argument: rule 1 (a second raise while one is pending
# escapes the frame) and rule 2 (returning -- or ending the program -- with a
# pending unacknowledged error re-raises instead of vanishing).
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
fail() { printf 'FAIL %s\n' "$1"; exit 1; }

positive=(
    catch_return
    check_consumes
    loop_next
    propagation
    rule1_escape
    rule2_exit
    goto_label
    handler_disarmed
    rearm
    snapshot_reraise
    trace
    resume_ident
    clear
)

for name in "${positive[@]}"; do
    ./gbasic "tests/error_model/$name.bas" >"$scratch/got" 2>"$scratch/err" \
        || fail "$name (exited nonzero: $(cat "$scratch/err"))"
    diff -u "tests/error_model/$name.out" "$scratch/got" \
        || fail "$name (output diverged)"
    printf 'PASS %s\n' "$name"
done

# --- rule 2 at the top: the program ends, the pending error does not -------
if ./gbasic tests/error_model/neg_unchecked_exit.bas \
        >"$scratch/got" 2>"$scratch/err"; then
    fail "neg_unchecked_exit (expected nonzero exit)"
fi
diff -u tests/error_model/neg_unchecked_exit.out "$scratch/got" \
    || fail "neg_unchecked_exit (stdout diverged -- the statements after the raise must run)"
grep -qF "division by zero" "$scratch/err" \
    || fail "neg_unchecked_exit (the pending error was never reported: $(cat "$scratch/err"))"
printf 'PASS neg_unchecked_exit\n'

# --- the deleted mode is a parse error, not a silent acceptance ------------
if ./gbasic tests/error_model/neg_resume.bas >/dev/null 2>"$scratch/err"; then
    fail "neg_resume (expected nonzero exit)"
fi
grep -qiE "syntax error|parse error" "$scratch/err" \
    || fail "neg_resume (expected a parse error: $(cat "$scratch/err"))"
printf 'PASS neg_resume\n'

# --- a structured raise without a message is refused -----------------------
if ./gbasic tests/error_model/neg_structured.bas >/dev/null 2>"$scratch/err"; then
    fail "neg_structured (expected nonzero exit)"
fi
grep -qF "error record requires a message field" "$scratch/err" \
    || fail "neg_structured (missing refusal: $(cat "$scratch/err"))"
printf 'PASS neg_structured\n'

# --- the fatal line is byte-identical to the old format --------------------
if ./gbasic tests/error_model/neg_fatal.bas >/dev/null 2>"$scratch/err"; then
    fail "neg_fatal (expected nonzero exit)"
fi
diff -u tests/error_model/neg_fatal.err "$scratch/err" \
    || fail "neg_fatal (the fatal stderr line moved -- it must stay byte-identical)"
printf 'PASS neg_fatal\n'

printf 'run_error_model: %d cases passed\n' "$(( ${#positive[@]} + 4 ))"
