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
    tracelib
    resume_ident
    clear
    arg_raise
    first_raise_wins
)

for name in "${positive[@]}"; do
    ./gbasic "tests/error_model/$name.bas" >"$scratch/got" 2>"$scratch/err" \
        || fail "$name (exited nonzero: $(cat "$scratch/err"))"
    diff -u "tests/error_model/$name.out" "$scratch/got" \
        || fail "$name (output diverged)"
    printf 'PASS %s\n' "$name"
done

# --- THE TRACEBACK BENEATH THE DIAGNOSTIC ------------------------------------
# `error.trace` has carried the frames since PLAT-ERR and the report threw them
# away, so this prints what the runtime already knew.
#
# THE RULE THAT MATTERS IS WHAT IS **NOT** SHOWN. Matthew's ruling: a raise
# inside a library should say so without detailing it. Measured, the first
# implementation of that moved 33 goldens and THIRTY were pure redundancy --
#
#     runtime error at stdlib/chart.bas:541:25: chart: column 'name' holds ...
#       in library chart
#
# where the path names the file and the message already begins `chart:`. So a
# collapsed library line is SUPPRESSED when the error is reported in that very
# library, which took the cost back to 3 goldens and made the feature better.
# A library line earns its place only mid-chain, where the header cannot tell
# you a library was involved.
trace_dir="$scratch/tb"
mkdir -p "$trace_dir"
cat > "$trace_dir/deep.bas" <<'BAS'
library deep
    function a()
        return b()
    end function
    function b()
        error "four frames in"
        return 1
    end function
end library
BAS
cat > "$trace_dir/show.bas" <<'BAS'
load deep
function mine()
    return deep.a()
end function
program main( args )
    print mine()
end program
BAS
tb_out="$( (cd "$trace_dir" && "$OLDPWD/gbasic" show.bas 2>&1 >/dev/null) || true )"
tb_full="$( (cd "$trace_dir" && "$OLDPWD/gbasic" --full-trace show.bas 2>&1 >/dev/null) || true )"
# THE USER'S OWN FRAME IS SHOWN -- that is the information the header lacks.
if printf '%s\n' "$tb_out" | grep -q "in mine  show.bas"; then
    printf 'PASS traceback shows the caller frame\n'
else
    printf '%s\n' "$tb_out" | sed 's/^/  /'
    fail "traceback (the caller frame is missing)"
fi
# AND THE LIBRARY RUN IS NOT, because the header already names that file.
if printf '%s\n' "$tb_out" | grep -q "in library deep"; then
    printf '%s\n' "$tb_out" | sed 's/^/  /'
    fail "traceback (a redundant library line was printed)"
else
    printf 'PASS traceback suppresses the library it is already reported in\n'
fi
# CONTROL: --full-trace shows every frame, or "suppressed" would be
# indistinguishable from "never collected".
if printf '%s\n' "$tb_full" | grep -q "in a  " && printf '%s\n' "$tb_full" | grep -q "in b  "; then
    printf 'PASS --full-trace lists every library frame\n'
else
    printf '%s\n' "$tb_full" | sed 's/^/  /'
    fail "--full-trace (frames missing)"
fi
# AND THE JSON STREAM STAYS ONE SENTENCE PER DIAGNOSTIC: frames in a `message`
# would reach an editor as a multi-line field it never asked for, and the
# no-frames-in-JSON rule is what keeps run_parse_exit's claim true.
tb_json="$( (cd "$trace_dir" && "$OLDPWD/gbasic" --json-diagnostics show.bas 2>&1 >/dev/null) || true )"
if printf '%s\n' "$tb_json" | head -1 | grep -q "in mine"; then
    fail "traceback leaked into --json-diagnostics"
else
    printf 'PASS --json-diagnostics carries no frames\n'
fi
if printf '%s\n' "$tb_json" | head -1 \
     | python3 -c 'import sys,json; json.loads(sys.stdin.read())' 2>/dev/null; then
    printf 'PASS --json-diagnostics is still valid JSON\n'
else
    printf 'SKIP json validity (python3 unavailable)\n'
fi

# --- WHERE a raise reports: the failing subexpression, not the statement ---
# DOGFOOD 27. A raise reports `current_line`/`current_column`, which `eval_stmt`
# stamps from the STATEMENT -- so the same fault spelled four ways reported four
# different columns and only one of them was the failing site. `x = a[5]` and
# `print(a[5])` both blamed column 1; `print("x " + string(a[5]))` blamed the
# `+`. A reader taught that `:5:1` means line 5 column 1 looks at column 1,
# finds `print`, and concludes `print` is broken. A `:1` reads as a caret and is
# not one -- and the one place it was right was the PROMPT, where the
# subexpression happens to be the whole statement, so the reader learned the
# right lesson at the bench and had to unlearn it in a file.
#
# THE EXPECTED COLUMN IS COMPUTED FROM THE SOURCE, not written down. A list of
# four numbers is a transcript of whatever the binary said; asking where the
# `[` actually is makes this an oracle, and it fails both on a build that
# reports the statement's column and on one that reports a constant.
col_of() { awk -v ch="$2" 'NR==1{print index($0, ch); exit}' <<<"$1"; }
check_at() {   # label, source line, character the raise must point at
    local label="$1" line="$2" ch="$3"
    printf 'a = [1, 2]\nr = { a: 1 }\n%s\n' "$line" > "$scratch/pos.bas"
    local want got raw
    want="$(col_of "$line" "$ch")"
    # `|| true`, and it is not decoration: the run under test is SUPPOSED to
    # fail, and with `pipefail` the pipeline takes gbasic's status -- so the
    # assignment fails, `set -e` ends the suite, and it ends SILENTLY, with no
    # FAIL line and a green-looking scrollback above it. run_http.sh records
    # the same trap from the other direction (a grep that matches nothing).
    raw="$(./gbasic "$scratch/pos.bas" 2>&1 >/dev/null || true)"
    got="$(printf '%s\n' "$raw" | sed -n 's/.*pos\.bas:3:\([0-9]*\):.*/\1/p')"
    [ -n "$got" ] || fail "position/$label (no located diagnostic at all)"
    [ "$got" = "$want" ] \
        || fail "position/$label (points at column $got, the '$ch' is at $want)"
    printf 'PASS position_%s (column %s, the %s)\n' "$label" "$got" "$ch"
}
# Four spellings of ONE fault. Asserting any single one proves little; what the
# defect was is that they DISAGREED.
check_at assign        'x = a[5]'                    '['
check_at bare_call     'print(a[5])'                 '['
check_at nested_concat 'print("x " + string(a[5]))'  '['
check_at deep          'print(string(string(a[5])))' '['
# The field family, same rule, different node.
check_at field_assign  'x = r.b'                     '.'
check_at field_nested  'print("v " + string(r.b))'   '.'
# TWO CONTROLS, because "not column 1" is satisfied by a build that points
# anywhere at all. The first uses a DIFFERENT character, so the tier cannot be
# passing by hunting for brackets -- a binary operator has always carried its
# own position, which is why `1/0` was the one shape DOGFOOD 27 found already
# correct, and it must stay that way.
check_at divide 'x = 1 / 0' '/'
# And the second says column 1 is still REACHABLE: a fault whose site really is
# the whole statement must still report it, or the fix would have traded one
# wrong column for another.
printf 'a = [1, 2]\nr = { a: 1 }\nprint undefined_name_here\n' > "$scratch/pos.bas"
raw="$(./gbasic "$scratch/pos.bas" 2>&1 >/dev/null || true)"
got="$(printf '%s\n' "$raw" | sed -n 's/.*pos\.bas:3:\([0-9]*\):.*/\1/p')"
[ "$got" = "1" ] || fail "position/control (a statement-wide fault moved to column $got)"
printf 'PASS position_control (a statement-wide fault still reports column 1)\n'

# --- THE FIRST RAISE WINS, at a site a golden cannot reach -----------------
# The fixture above proves the MESSAGE and that an array is not appended to.
# This is the case where being wrong would destroy something rather than
# mislead: `write` truncates its file before it has anything to put in it, so
# a failed argument that got through would empty a file the program never
# meant to touch. Done here because it needs a scratch path, and asserted on
# the FILE'S CONTENTS -- the return value proves nothing about the disk.
printf 'original contents\n' > "$scratch/victim.txt"
cat > "$scratch/victim.bas" <<EOF
v {file}= "$scratch/victim.txt"
m {file}= "$scratch/no_such_source.txt"
write(v, read(m))
EOF
if ./gbasic "$scratch/victim.bas" >"$scratch/got" 2>"$scratch/err"; then
    fail "victim (expected nonzero exit)"
fi
grep -qF "could not read file" "$scratch/err" \
    || fail "victim (reported the wrong cause: $(cat "$scratch/err"))"
grep -qxF "original contents" "$scratch/victim.txt" \
    || fail "victim (the file was written or truncated on a failed argument)"
printf 'PASS victim_file_untouched\n'

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
