#!/usr/bin/env bash
# The gBASIC prompt (src/repl.c over the session split in src/eval.c).
#
# WHY THIS SUITE IS SELF-CHECKING RATHER THAN A GOLDEN. Every defect a prompt
# can have produces an ORDINARY-LOOKING TRANSCRIPT. A session that quietly
# forgets its variables looks like a user who mistyped a name; a `run` that
# reuses the session prints a number that is merely one too large; a resident
# program that records the questions you asked still lists as a program and
# still runs. A golden would record any of those as expected output and defend
# it. So each tier states the answer it wants and prints `ok` or a MISMATCH
# naming both sides.
#
# THE LOAD-BEARING TIER IS QUESTION_NOT_PROGRAM (below). The tier that is NOT
# load-bearing, and says so, is `run` starting from a fresh session: that is the
# right semantics -- a program must begin the way it begins from a file -- and
# MEASURED, removing it leaves every check in this file green, because a line is
# recorded exactly when it ACTED, so every line that makes state is also in the
# program that remakes it. It is asserted against the SOURCE, with the reason
# written beside it, rather than by a check that would pass either way.
#
# WHY QUESTION_NOT_PROGRAM CARRIES THE LOAD. `sq(3)` at a prompt is an enquiry and
# `setup()` is an instruction, and the only thing that separates them is whether
# an answer came back -- so the recording decision is made AFTER the chunk runs.
# Get it wrong and `list` shows a program that still parses and still runs, one
# whose `run` merely warns about a discarded result. Asserted with its control:
# `print sq(3)`, which acts rather than answers, MUST be in the listing, or
# "questions are not recorded" is satisfied by recording nothing at all.
#
# CONTINUE_MESSAGES IS A TRIPWIRE. Deciding a line is unfinished is asked of the
# parser, not of a keyword table, and the answer is carried in two message
# texts. A rewording would cost the prompt its continuation SILENTLY -- a `for`
# loop would be reported as an error instead of waiting for `next` -- so the two
# wordings are asserted against what the binary actually emits.
set -u

root="$(cd "$(dirname "$0")/.." && pwd)"
# The shared policy, not our own flags: run_valgrind_policy.sh fails any suite
# that types its own, because 35 suites each choosing a strictness by accident
# is the state that replaced.
. "$root/tests/valgrind_tier.sh"
GB="$root/gbasic"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

pass=0; fail=0
check() {
    local label="$1" want="$2" got="$3"
    if [ "$want" = "$got" ]; then
        printf 'ok   %s\n' "$label"; pass=$((pass+1))
    else
        printf 'FAIL %s\n       want: %s\n       got:  %s\n' "$label" "$(printf '%s' "$want" | tr '\n' '|')" "$(printf '%s' "$got" | tr '\n' '|')"
        fail=$((fail+1))
    fi
}
contains() {
    local label="$1" needle="$2" hay="$3"
    case "$hay" in
        *"$needle"*) printf 'ok   %s\n' "$label"; pass=$((pass+1)) ;;
        *) printf 'FAIL %s\n       wanted to contain: %s\n       got:  %s\n' "$label" "$needle" "$(printf '%s' "$hay" | tr '\n' '|')"; fail=$((fail+1)) ;;
    esac
}
lacks() {
    local label="$1" needle="$2" hay="$3"
    case "$hay" in
        *"$needle"*) printf 'FAIL %s\n       must NOT contain: %s\n       got:  %s\n' "$label" "$needle" "$(printf '%s' "$hay" | tr '\n' '|')"; fail=$((fail+1)) ;;
        *) printf 'ok   %s\n' "$label"; pass=$((pass+1)) ;;
    esac
}
# Feed lines to a prompt and capture stdout (stderr separately where a tier
# needs it). Piped, so no banner and no prompts -- which is what makes these
# comparisons exact.
repl() { printf '%s' "$1" | "$GB" --repl 2>/dev/null; }
repl_err() { printf '%s' "$1" | "$GB" --repl 2>&1 >/dev/null; }

if [ ! -x "$GB" ]; then
    echo "SKIP run_repl.sh: no ./gbasic (run make first)"; exit 0
fi

echo "== PERSIST: one environment across lines =="
check "a variable outlives its line" "5" "$(repl 'x = 5
print x
quit
')"
check "a function outlives its line" "49" "$(repl 'function sq(n)
return n * n
end function
print sq(7)
quit
')"
# The reason a session accumulates its ROOT rather than replacing it per line:
# `load util` on one line has to find a library declared on an earlier one.
check "a library declared on an earlier line can be loaded on a later one" "hi from util" "$(repl 'library util
function greet()
return "hi from util"
end function
end library
load util
print util.greet()
quit
')"

echo
echo "== ECHO: a line that is not a statement is a question =="
check "arithmetic" "3" "$(repl '1 + 2
quit
')"
check "a string" "hi" "$(repl '"hi"
quit
')"
check "a bare name" "5" "$(repl 'x = 5
x
quit
')"
check "a builtin call" "3" "$(repl 'len("abc")
quit
')"
check "an expression with a trailing comment" "3" "$(repl "1 + 2 ' three
quit
")"
# THE CONTROL. Suppressing `nothing` is only meaningful beside a call that DOES
# answer -- without this line, a prompt that echoed nothing at all would pass.
check "a call that answers is echoed" "42" "$(repl 'function ret()
return 42
end function
ret()
quit
')"
check "a call that answers nothing is silent" "after" "$(repl 'function noop()
return nothing
end function
noop()
print "after"
quit
')"

echo
echo "== CONTINUE: an unfinished line asks for the rest =="
check "a block waits for its end" "1
2
3" "$(repl 'for i = 1 to 3
print i
next
quit
')"
check "a bracket waits for its close" "3" "$(repl 'print (1 +
2)
quit
')"
check "a nested block waits" "inner" "$(repl 'if true then
if true then
print "inner"
end if
end if
quit
')"
# A blank line SUBMITS an unfinished chunk. Without it a typo that merely looks
# unfinished would hold the prompt open with no way out but end-of-input, and
# the author would never see the diagnostic naming it.
out="$(repl_err 'print (1 +

print "after"
quit
')"
contains "a blank line forces the diagnostic out" "unclosed" "$out"
check "and the prompt carries on" "after" "$(repl 'print (1 +

print "after"
quit
')"

echo
echo "== CONTINUE_MESSAGES: the tripwire under that decision =="
# src/repl.c decides a chunk is unfinished by matching these two texts. Asserted
# against the BINARY, not against a copy of the string, so a rewording fails
# here rather than silently costing the prompt its continuation.
printf 'for i = 1 to 3\n' > "$work/block.bas"
contains "an unfinished block still says 'unexpected end of file'" \
    "unexpected end of file" "$("$GB" "$work/block.bas" 2>&1)"
printf 'print (1 +\n' > "$work/bracket.bas"
contains "an unfinished bracket still says \"unclosed '\"" \
    "unclosed '" "$("$GB" "$work/bracket.bas" 2>&1)"

echo
echo "== RUN / NEW =="
check "run replays the program" "1
1
1" "$(repl 'n = 0
n = n + 1
print n
run
run
quit
')"
check "new forgets the session" "" "$(repl 'a = 1
new
vars
quit
')"
check "new forgets the program" "" "$(repl 'a = 1
new
list
quit
')"
# The control: without `new`, both are still there.
check "and without new they are not forgotten" "a = 1" "$(repl 'a = 1
vars
quit
')"

# STRUCTURAL, AND THE REASON IS THE HONEST PART -- see the header. A behavioural
# check here would pass on a `run` that simply continued the session, so what is
# asserted is that the close is still there.
if grep -A4 'if (word_is(line, "run"))' "$root/src/repl.c" | grep -q 'gb_session_close'; then
    printf 'ok   run closes the session before replaying (structural)\n'; pass=$((pass+1))
else
    printf 'FAIL run no longer closes the session before replaying -- the resident\n'
    printf '     program would run on top of whatever the prompt had already made\n'
    fail=$((fail+1))
fi

echo
echo "== QUESTION_NOT_PROGRAM: an answer is not source =="
prog="$(repl 'function sq(n)
return n * n
end function
sq(3)
print sq(4)
list
quit
')"
contains "a question was answered" "9" "$prog"
lacks "the question is not in the listing" "
sq(3)" "$prog"
# THE CONTROL: a line that ACTS is recorded. Without it, recording nothing at
# all would satisfy the assertion above.
contains "a statement is in the listing" "print sq(4)" "$prog"
# And the consequence the recording rule exists for: replaying the program must
# not produce a discarded-result warning about a question the user asked.
lacks "run does not warn about a recorded question" "is discarded" \
    "$(repl_err 'function sq(n)
return n * n
end function
sq(3)
run
quit
')"

echo
echo "== REDEFINE: at a prompt, typing it again is an edit =="
check "the new definition wins" "9
27" "$(repl 'function sq(n)
return n * n
end function
print sq(3)
function sq(n)
return n * n * n
end function
print sq(3)
quit
')"
# The listing keeps the declaration WHERE IT WAS: dropping and appending would
# move a function you just fixed to the bottom of a program you are reading.
check "and it is replaced in place" "9
function sq(n)
return n * n * n
end function
print sq(3)" "$(repl 'function sq(n)
return n * n
end function
print sq(3)
function sq(n)
return n * n * n
end function
list
quit
')"
# THE CONTROL. The relaxation is scoped to a session: in a FILE two definitions
# of one name are still refused, which is the rule run_scope.sh established and
# which this must not have weakened.
printf 'function sq(n)\nreturn 1\nend function\nfunction sq(n)\nreturn 2\nend function\nprint sq(1)\n' > "$work/twice.bas"
contains "a file with two definitions is still refused" "defined twice" \
    "$("$GB" "$work/twice.bas" 2>&1)"

echo
echo "== ERRORS: a failed line does not end the session =="
check "after a runtime error" "alive" "$(repl 'print undefined_thing
print "alive"
quit
')"
check "after a parse error" "alive" "$(repl 'pritn "typo"
print "alive"
quit
')"
contains "the runtime error is reported" "undefined variable: undefined_thing" \
    "$(repl_err 'print undefined_thing
quit
')"
# A typo must be described as a typo. The prompt retries an unparseable line
# wrapped as `print (...)`; if that also fails, what gets reported is the
# ORIGINAL complaint and never the wrapper's.
# THE DISCRIMINATOR IS THE POSITION, not the words. The wrapper puts the typed
# text on its SECOND line and inside brackets, so it complains at 2:7 about a
# missing `)`; what was typed is at 1:7. Asserting only "unexpected STRING"
# would pass on either, since both say it -- measured, and the first draft of
# this tier did exactly that.
err="$(repl_err 'pritn "typo"
quit
')"
contains "a typo is reported where it was typed" "<prompt>:1:7" "$err"
lacks "and not where the prompt moved it to" "<prompt>:2:7" "$err"
lacks "nor as a malformed print" "expecting RPAREN" "$err"

echo
echo "== SAVE / LOAD =="
check "a saved program reloads and runs" "4
4" "$(cd "$work" && repl 'x = 2
function dbl(n)
return n * 2
end function
print dbl(x)
save "saved.bas"
new
load "saved.bas"
quit
')"
check "the saved file is the listing" "x = 2
function dbl(n)
return n * 2
end function
print dbl(x)" "$(cat "$work/saved.bas")"
# THE CONTROL that keeps `load "file"` from eating the language: a QUOTED
# argument is a file the prompt reads, a bare word is still gBASIC's own
# statement. Nothing else distinguishes them.
check "load with a bare name is still the gBASIC statement" "sqlite is loaded" \
    "$(repl 'load sqlite
print "sqlite is loaded"
quit
')"

echo
echo "== EXIT / QUIT =="
printf 'print "before"\nexit(3)\nprint "never"\n' | "$GB" --repl >/dev/null 2>&1
check "exit(n) leaves with n" "3" "$?"
printf 'print "a"\n' | "$GB" --repl >/dev/null 2>&1
check "end of input leaves with 0" "0" "$?"
# A session that saw a failure exits nonzero. Interactively nobody reads $?;
# piped, this is the difference between a script that worked and one that
# reported three errors and was taken for success.
printf 'print nosuchname\nprint "after"\nquit\n' | "$GB" --repl >/dev/null 2>&1
check "a session that failed leaves with 1" "1" "$?"
printf 'print nosuchname\nexit(0)\n' | "$GB" --repl >/dev/null 2>&1
check "and an explicit exit(n) still wins" "0" "$?"
check "quit stops reading" "a" "$(repl 'print "a"
quit
print "b"
')"

echo
echo "== PIPED: no prompt, no banner =="
# Load-bearing for every check above: a banner or a prompt on stdout would be in
# each of those comparisons. Asserted directly so the reason is visible.
check "piped output is exactly the program's" "5" "$(repl 'print 5
quit
')"
out="$(printf 'print 5\nquit\n' | GBASIC_REPL_PROMPT=1 "$GB" --repl 2>/dev/null)"
contains "and a prompt appears when asked for" "> " "$out"
contains "with a banner" "help" "$out"
check "no arguments is the prompt" "7" "$(printf 'print 7\nquit\n' | "$GB" 2>/dev/null)"

echo
echo "== INTERRUPT: Ctrl-C ends the chunk, not the session =="
# A beginner's first `while true` must not take the whole prompt with it, along
# with a resident program they have not saved. Driven deterministically: the
# loop PRINTS once it is running, so the signal is sent when the loop is
# provably executing rather than after a guessed sleep.
# JOB CONTROL ON, and this is not a detail: a NON-INTERACTIVE shell sets SIGINT
# to IGNORED for every background job it starts (POSIX), so without `set -m` the
# control below would send a signal to a process that cannot receive it. The
# prompt itself is unaffected -- it installs a handler, which replaces the
# inherited ignore -- so the tier would have passed while its control measured
# nothing, which is exactly the failure this file keeps guarding against.
set -m

wait_for() {   # wait_for FILE PATTERN SECONDS
    local i=0
    while [ "$i" -lt "$(( $3 * 10 ))" ]; do
        grep -q "$2" "$1" 2>/dev/null && return 0
        sleep 0.1; i=$((i+1))
    done
    return 1
}
int_fifo="$work/int.fifo"; rm -f "$int_fifo"; mkfifo "$int_fifo"
int_out="$work/int.out"; : > "$int_out"
"$GB" --repl < "$int_fifo" > "$int_out" 2>&1 &
gb_pid=$!
exec 3>"$int_fifo"          # opening the write end releases the reader
printf 'x = 7\n' >&3
printf 'i = 0\nwhile true\ni = i + 1\nif i = 1 then print "LOOPING"\nend while\n' >&3
if wait_for "$int_out" LOOPING 15; then
    kill -INT "$gb_pid" 2>/dev/null
    wait_for "$int_out" interrupted 15 || true
fi
printf 'print "SURVIVED"\nprint x\nquit\n' >&3
exec 3>&-
( sleep 10; kill -KILL "$gb_pid" 2>/dev/null ) & guard=$!
wait "$gb_pid" 2>/dev/null
kill "$guard" 2>/dev/null; wait "$guard" 2>/dev/null
int_result="$(cat "$int_out")"
contains "the runaway loop was reached" "LOOPING" "$int_result"
contains "and interrupted" "interrupted" "$int_result"
contains "the session survived" "SURVIVED" "$int_result"
# THE LOAD-BEARING HALF: surviving is not enough -- the session must still hold
# what it held. A prompt that recovered by restarting itself would print
# SURVIVED and have forgotten everything.
contains "with its variables intact" "7" "$int_result"

# THE CONTROL. Interruption is the PROMPT's behaviour; a script's Ctrl-C still
# ends the process, which is what it has always meant and what anything running
# gbasic in a pipeline expects. Without this, "Ctrl-C is handled" would be
# satisfied by a change that made every gBASIC program ignore it.
printf 'print "LOOPING"\nwhile true\nend while\n' > "$work/loop.bas"
: > "$work/loop.out"
# --line-buffered so the marker reaches the file while the loop is still
# running; block-buffered, nothing arrives and the wait below measures a
# timeout rather than a running program.
"$GB" --line-buffered "$work/loop.bas" > "$work/loop.out" 2>&1 &
loop_pid=$!
wait_for "$work/loop.out" LOOPING 15 || true
kill -INT "$loop_pid" 2>/dev/null
wait "$loop_pid" 2>/dev/null
check "a script is still ended by the same signal" "130" "$?"
set +m

echo
echo "== VALGRIND =="
# A session HOLDS every chunk's AST for its lifetime (a function declared on one
# line is called on the next), so the prompt is the one caller that must free
# ASTs by hand. It also meets failed parses constantly -- every question goes
# through one -- which is why the parser gained a %destructor.
if vg_available; then
    vg_script="$work/vg.in"
    printf 'x = 5\nfunction sq(n)\nreturn n*n\nend function\nprint sq(x)\nsq(3)\n1 + 2\nbadname\npritn "typo"\nfunction sq(n)\nreturn n\nend function\nlist\nrun\nvars\nsave "%s/vg.bas"\nnew\nload "%s/vg.bas"\nquit\n' "$work" "$work" > "$vg_script"
    # Compared against $VG_EXIT rather than "nonzero", because this fixture
    # DELIBERATELY fails: it feeds an undefined name and a typo, so the session
    # exits 1 by design. A bare `if vg_run ...` reads that as a valgrind
    # objection and reports the wrong cause -- which is what the first draft of
    # this tier did, naming valgrind over a report saying 0 errors.
    vg_run "$GB" --repl < "$vg_script" >/dev/null 2>"$work/vg.err"
    if [ "$?" = "$VG_EXIT" ]; then
        grep -E "definitely lost|Invalid|Open file descriptor" "$work/vg.err" | head -5
        printf 'FAIL valgrind\n'; fail=$((fail+1))
    else
        printf 'ok   no definite leak or invalid access\n'; pass=$((pass+1))
    fi
else
    printf 'ok   SKIP (valgrind unavailable)\n'; pass=$((pass+1))
fi

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
