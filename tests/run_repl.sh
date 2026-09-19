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
if grep -A4 'command_word(line, "run"' "$root/src/repl.c" | grep -q 'gb_session_close'; then
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
  1  function sq(n)
     return n * n * n
     end function
  2  print sq(3)" "$(repl 'function sq(n)
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
echo "== COMMANDS: what a curious person reaches for =="
check "? is BASIC's shorthand" "3" "$(repl '? 1 + 2
quit
')"
# And the reason `?` is a QUESTION rather than sugar for `print`: it is how you
# ask for a name a COMMAND would otherwise claim.
check "? reaches a name a command would claim" "5" "$(repl 'list = 5
? list
quit
')"
check "list names one declaration" "  1  function sq(n)
     return n*n
     end function" "$(repl 'function sq(n)
return n*n
end function
function dbl(n)
return n*2
end function
list sq
quit
')"
check "delete by the number list shows" "3
  1  x = 1
  2  print 3" "$(repl 'x = 1
y = 2
print 3
delete 2
list
quit
')"
check "delete by declaration name" "  1  x = 1" "$(repl 'x = 1
function sq(n)
return n*n
end function
delete sq
list
quit
')"
# A delete that matched nothing SAYS SO. Silently doing nothing is the
# commonest way a delete command lies, and the listing afterwards looks
# perfectly ordinary.
contains "a delete that matches nothing says so" "nothing to delete: 9" \
    "$(repl_err 'x = 1
delete 9
quit
')"
contains "cls clears the screen" "$(printf '\033[H\033[2J')" "$(repl 'cls
quit
')"
# THE CONTROL ON EVERY COMMAND ABOVE: a command name is only a command as a
# WHOLE WORD. Without the delimiter rule, declaring `function run()` would make
# the author's own function unreachable -- and `list` was reported as a syntax
# error about a missing LPAREN, which names neither the command nor the call.
# A NAME THAT MERELY STARTS WITH A COMMAND WORD IS NOT THAT COMMAND. Without
# the delimiter, a variable called `listing` is read as `list ing` and answers
# "nothing named 'ing' in the program" -- a puzzle about a program the author
# never mentioned. `run()` does NOT catch this, because the parenthesis already
# makes its argument non-empty; only a command that TAKES an argument can be
# fooled, which is why the case is spelled with `list`.
check "a name that starts with a command word is a name" "7" "$(repl 'listing = 7
listing
quit
')"
check "run() is still the author's own function" "called" "$(repl 'function run()
return "called"
end function
print run()
quit
')"

echo
echo "== SESSION CACHE: there is always a file =="
# What you type is written to a cache file as you go. Two things come of it, and
# the second is what motivated it: `spawn` needs a SOURCE FILE to re-exec (the
# child re-parses it) and a session had none, and a killed session used to lose
# everything typed.
cache="$work/state"
rm -rf "$cache"; mkdir -p "$cache"
cached() { printf '%s' "$1" | GBASIC_SESSION_DIR="$cache" "$GB" --repl 2>&1; }
# BOUNDED, because the spawn tier below WAITS FOR A REPLY: if the child never
# starts, `receive()` blocks and the suite does not fail, it HANGS -- the third
# way this gate can go quiet, and the one run_string_nul.sh already had to
# defend against. -k because the interpreter installs a SIGTERM handler for pool
# drain, and a bound that might not fire is not a bound.
cached_bounded() { printf '%s' "$2" | GBASIC_SESSION_DIR="$cache" timeout -k 5 "$1" "$GB" --repl 2>&1; }

# The cache holds the PROGRAM, not the transcript -- the same rule `list` and
# `save` follow. A question is not program text, and the literal transcript is
# what history is for.
cfifo="$work/c.fifo"; rm -f "$cfifo"; mkfifo "$cfifo"
GBASIC_SESSION_DIR="$cache" "$GB" --repl < "$cfifo" >/dev/null 2>&1 &
cpid=$!
exec 4>"$cfifo"
# `dbl(x)` and not `1 + 1`: a bare call PARSES, so it is the recording rule's
# real subject -- `1 + 1` does not parse at all and takes the wrapper path,
# which records nothing either way, so a check using it is vacuous. Measured:
# with the rule removed it stayed green.
printf 'x = 2\nfunction dbl(n)\nreturn n*2\nend function\nprint dbl(x)\ndbl(x)\n' >&4
wait_for_file() { local i=0; while [ "$i" -lt 100 ]; do [ -s "$1" ] && return 0; sleep 0.1; i=$((i+1)); done; return 1; }
wait_for_file "$cache/session-$cpid.bas" || true
sleep 0.5
live="$(cat "$cache"/session-*.bas 2>/dev/null)"
check "the cache holds the program while you type" "x = 2
function dbl(n)
return n*2
end function
print dbl(x)" "$live"
lacks "and not the questions asked along the way" "
dbl(x)" "$live"
check "and only its owner can read it" "-rw-------" \
    "$(ls -l "$cache"/session-*.bas 2>/dev/null | head -1 | awk '{print $1}')"

# SIGKILL: the case the whole thing exists for.
kill -9 "$cpid" 2>/dev/null; wait "$cpid" 2>/dev/null
exec 4>&-; rm -f "$cfifo"
check "a killed session leaves its program behind" "1" \
    "$(ls "$cache" 2>/dev/null | grep -c '^session-')"
contains "the next session says so" "ended without saving" \
    "$(printf 'quit\n' | GBASIC_SESSION_DIR="$cache" GBASIC_REPL_PROMPT=1 "$GB" --repl 2>&1)"

# RECOVER RESTORES, AND DOES NOT RUN. Asserted as a DIFFERENCE, because "the
# program came back" is equally satisfied by a recovery that executed it -- and
# executing yesterday's half-finished work on someone's behalf is the one
# behaviour here that could destroy something.
rec="$(cached 'recover
list
quit
')"
contains "recover brings the program back" "print dbl(x)" "$rec"
# Asserted against the OUTPUT LINES. A `lacks` on the raw text missed it: with
# recovery made to run the program the `4` lands as the FIRST thing printed, so
# a needle written with a leading newline never matched. Third time in this file
# that a contains/lacks wanted a line-exact match instead.
check "and does not run it" "" "$(printf '%s\n' "$rec" | grep -x '4')"
check "and recovering consumes it" "0" \
    "$(ls "$cache" 2>/dev/null | grep -c '^session-')"
# ... with the control that `run` DOES produce the answer, or "does not run" is
# satisfied by a recovery that brought back nothing at all. A fresh orphan is
# made the same way -- by killing a session -- rather than by writing the file
# by hand, which would test the reader and not the thing that writes it.
make_orphan() {
    local f="$work/o.fifo"; rm -f "$f"; mkfifo "$f"
    GBASIC_SESSION_DIR="$cache" "$GB" --repl < "$f" >/dev/null 2>&1 &
    local p=$!
    exec 5>"$f"
    printf '%s' "$1" >&5
    local i=0
    while [ "$i" -lt 100 ] && [ ! -s "$cache/session-$p.bas" ]; do sleep 0.1; i=$((i+1)); done
    kill -9 "$p" 2>/dev/null; wait "$p" 2>/dev/null
    exec 5>&-; rm -f "$f"
}
make_orphan 'x = 2
function dbl(n)
return n*2
end function
print dbl(x)
'
rec2="$(cached 'recover
run
quit
')"
contains "run then produces its answer" "4" "$rec2"

# Leaving deliberately with unsaved work keeps it too: that loses the program
# just as completely as a crash, and is the commoner way to lose it.
rm -rf "$cache"; mkdir -p "$cache"
cached 'y = 9
quit
' >/dev/null
check "quitting with unsaved work keeps it" "1" \
    "$(ls "$cache" 2>/dev/null | grep -c '^session-')"
# THE CONTROL: having saved, there is nothing to warn about, so the next start
# must be silent -- a notice that fires every time is one nobody reads.
rm -rf "$cache"; mkdir -p "$cache"
cached "z = 1
save \"$work/kept.bas\"
quit
" >/dev/null
check "and after save it does not" "0" \
    "$(ls "$cache" 2>/dev/null | grep -c '^session-')"

# `discard` is the other answer to the notice.
rm -rf "$cache"; mkdir -p "$cache"
cached 'w = 1
quit
' >/dev/null
cached 'discard
quit
' >/dev/null
check "discard forgets it" "0" "$(ls "$cache" 2>/dev/null | grep -c '^session-')"

echo
echo "== SPAWN: the cache is the file a child re-execs =="
# A spawned actor is fork+exec and the child re-parses the SOURCE FILE, so a
# session had nothing for a child to run -- docs/multiprocessing_design.md §3
# predicted this would need "a clear error at spawn", and nothing could reach
# the case until the prompt existed. The cache dissolves it: there IS a file.
rm -rf "$cache"; mkdir -p "$cache"
# THE CHILD MUST PROVE IT RAN. A weak version of this check -- `print "spawn
# works"` on the line after the spawn -- passes on the REFUSAL too, because the
# session survives an error and runs the next line either way; measured, it did.
# So the assertion is a REPLY that only a real child process can send.
contains "spawn works at the prompt" "A got: hello" "$(cached_bounded 30 'function worker(parent, name)
msg = receive()
send(parent, name + " got: " + msg)
end function
me = self()
a = spawn worker(me, "A")
send(a, "hello")
print(receive())
quit
')"
# AND THE REFUSAL IS STILL THERE as the fallback, for a machine with nowhere to
# write a cache. Without this the error would have become unreachable and would
# rot; with it, both the ordinary path and the fallback are asserted.
nofile="$(printf 'function worker()
return 1
end function
h = spawn worker()
print "session survives"
quit
' | GBASIC_SESSION_DIR=/proc/nonexistent/nope "$GB" --repl 2>&1)"
contains "and without a cache it refuses, naming the cause" \
    "nothing for the child actor to run" "$nofile"
lacks "rather than leaking the child's own complaint" "No such file or directory" "$nofile"
contains "the session survives either way" "session survives" "$nofile"
# THE CONTROL on all of it: spawn from a real file is untouched.
printf 'function worker()\n  return 1\nend function\nprogram main()\n  h = spawn worker()\n  print "spawned ok"\nend program\n' > "$work/spawn.bas"
contains "and spawn from a file still works" "spawned ok" "$("$GB" "$work/spawn.bas" 2>&1)"

echo
echo "== EDITING: the pty tier =="
# The line editor runs ONLY when stdin and stdout are both terminals, so a pipe
# cannot exercise a single key of it -- tests/repl_pty.py gives it a real
# pseudo-terminal and types one byte at a time, because the editor reads a byte
# at a time and redraws after each.
if command -v python3 >/dev/null 2>&1; then
    # Strip the redraw: every keystroke repaints the row, so what a tier wants
    # is the program's output, not the repainting.
    # Keystrokes are written with ANSI-C quoting ($'...') at the call sites, so
    # an escape byte is a byte here and printf does no interpreting of its own.
    pty() { printf '%s' "$1" | GBASIC_HISTORY="${2:-}" python3 "$root/tests/repl_pty.py" "$GB" --repl 2>/dev/null | tr -d '\r' | sed $'s/\033\\[[0-9]*[A-Za-z]//g'; }

    # HISTORY IS A DIFFERENCE. "the output contains 1" is satisfied by a prompt
    # with no history at all, since the first line already printed it -- so the
    # assertion is that Up makes it happen TWICE, against a control that runs
    # the same keys without the Up and gets it once.
    up_twice="$(pty $'print 1\n\033[A\nquit\n')"
    check "Up recalls the last line" "2" "$(printf '%s\n' "$up_twice" | grep -c '^1$')"
    once="$(pty $'print 1\nquit\n')"
    check "and without it, once" "1" "$(printf '%s\n' "$once" | grep -c '^1$')"

    # EDITING A RECALLED LINE. Recall `print 99`, move left one, type 8: the
    # answer must be 989, which no amount of recalling alone produces.
    edited="$(pty $'print 99\n\033[A\033[D8\nquit\n')"
    contains "a recalled line can be edited" "989" "$edited"

    kills="$(pty $'print "keep"\n\033[A\001\013print "new"\nquit\n')"
    contains "Ctrl-A and Ctrl-K rewrite the line" "new" "$kills"

    # ONE BACKSPACE DELETES ONE CHARACTER, not one byte: `é` is two bytes, and
    # a byte-wise delete leaves half a character behind and a broken string.
    # Type print "café", then TWO backspaces -- the first takes the quote, the
    # second must take the whole `é`, which is two bytes. A byte-wise delete
    # leaves half a character behind and a string that will not lex.
    utf="$(pty $'print "caf\303\251"\177\177"\nquit\n')"
    check "one backspace deletes a whole multi-byte character" "caf" \
        "$(printf '%s\n' "$utf" | grep -x 'caf')"

    # Ctrl-C WHILE EDITING throws the line away; Ctrl-C while a chunk RUNS
    # interrupts it (the tier above). Same key, two meanings, decided by which
    # of the two the prompt is doing -- so the assertion is that the cancelled
    # line did NOT run and the session did.
    cancel="$(pty $'x = 5\nprint 999\003print x\nquit\n')"
    # Asserted against the OUTPUT LINES, not the text: every keystroke is
    # echoed by the redraw, so `999` appears in the transcript however the tier
    # comes out. What must not exist is a line that IS 999 -- the program's
    # answer -- and the first draft of this check did not distinguish them.
    check "Ctrl-C while editing does not run the line" "" \
        "$(printf '%s\n' "$cancel" | grep -x '999')"
    contains "and the session is untouched" "5" "$cancel"

    hist="$work/hist"
    rm -f "$hist"
    pty $'print 42\nquit\n' "$hist" >/dev/null
    contains "history is kept between sessions" "print 42" "$(cat "$hist" 2>/dev/null)"
    # A prompt session can contain a connection string with a password in it, so
    # the file it is written to may not be readable by everyone on the machine.
    check "and only its owner can read it" "-rw-------" \
        "$(ls -l "$hist" 2>/dev/null | awk '{print $1}')"
    back="$(pty $'\033[A\nquit\n' "$hist")"
    contains "and a new session can recall it" "42" "$back"

    # A BARE ESCAPE MUST NOT HANG THE PROMPT. An arrow key is three bytes that
    # arrive together and Escape is one byte that arrives alone, and the only
    # thing telling them apart is whether more is waiting -- so reading the
    # second byte unconditionally leaves the prompt frozen until the user
    # presses something else, which reads as a crash. Typed at a HUMAN pace
    # here, deliberately: at the driver's default the following keystroke
    # arrives inside the 50ms window and is legitimately taken as part of a
    # sequence, which is what Alt+key is and is not a defect.
    esc="$(printf '%s' $'\033print 5\nquit\n' \
        | GBASIC_HISTORY= GBASIC_PTY_DELAY=0.2 python3 "$root/tests/repl_pty.py" \
          "$GB" --repl 2>/dev/null | tr -d '\r' | sed $'s/\033\\[[0-9]*[A-Za-z]//g')"
    check "a bare Escape is swallowed and the line still runs" "5" \
        "$(printf '%s\n' "$esc" | grep -x 5)"

    # A LINE LONGER THAN THE TERMINAL is scrolled horizontally, not wrapped --
    # a wrapped redraw has no idea how many rows it used and paints over the
    # wrong ones. The oracle is the RESULT: 300 characters must survive being
    # typed, recalled from history and run again, which a garbled redraw and a
    # mis-tracked cursor both break.
    longline="print \"$(printf 'y%.0s' $(seq 1 300))\""
    scrolled="$(pty "$longline"$'\n\033[A\n quit\n')"
    check "a line longer than the terminal survives an edit and a recall" "2" \
        "$(printf '%s\n' "$scrolled" | grep -c 'y\{300\}')"

    # AND THE COST OF THAT SCROLLING IS LINEAR, asserted as a RATIO across a 4x
    # size step (gate 8x; linear is ~4x and the quadratic version this replaced
    # measured 8.44s against 1.48s for one 3000-character line). The obvious
    # scroll search recomputes the column count from the start of the line on
    # every step, which is invisible at 80 characters and a stutter at 3000.
    # GBASIC_PTY_DELAY=0 because the driver's own pacing would otherwise be what
    # is being measured.
    shape_ms() {
        local n="$1" t0 t1
        local text; text="print \"$(printf 'z%.0s' $(seq 1 "$n"))\""
        t0=$(date +%s%N)
        printf '%s' "$text"$'\nquit\n' \
            | GBASIC_PTY_DELAY=0 GBASIC_PTY_TOTAL=200 python3 "$root/tests/repl_pty.py" \
              "$GB" --repl >/dev/null 2>&1
        t1=$(date +%s%N)
        echo $(( (t1 - t0) / 1000000 ))
    }
    small=$(shape_ms 1000)
    large=$(shape_ms 4000)
    if [ "$small" -lt 30 ]; then
        small=30      # a floor, so a fast machine does not divide by noise
    fi
    ratio=$(( large * 100 / small ))
    if [ "$ratio" -le 800 ]; then
        printf 'ok   redrawing a long line is linear (%sms -> %sms, %d.%02dx over a 4x step)\n' \
            "$small" "$large" "$((ratio / 100))" "$((ratio % 100))"; pass=$((pass+1))
    else
        printf 'FAIL redrawing a long line is not linear: %sms -> %sms is %d.%02dx over a 4x step\n' \
            "$small" "$large" "$((ratio / 100))" "$((ratio % 100))"; fail=$((fail+1))
    fi

    # THE EDITOR ALSO NEEDS VALGRIND, and the piped tier below cannot reach it:
    # a pipe never enters raw mode, so the history list, the edit buffer and the
    # saved not-yet-submitted line are all allocated on a path nothing else
    # runs. Through the shared policy, in a shell, so the flags stay in one
    # place. The exit status is the child's, so this cannot pass by reading a
    # report that was never produced.
    if vg_available; then
        printf '%s' $'print 12\n\033[A\033[D3\n\033[A\001\013print "x"\nquit\n' \
            | GBASIC_HISTORY="$work/vghist" python3 "$root/tests/repl_pty.py" \
              /bin/bash -c ". \"$root/tests/valgrind_tier.sh\"; vg_run \"$GB\" --repl" \
              > "$work/vgpty.out" 2>&1
        vgst=$?
        if [ "$vgst" = "$VG_EXIT" ]; then
            tr -d '\r' < "$work/vgpty.out" | grep -E "definitely lost|Invalid" | head -3
            printf 'FAIL valgrind over the editor\n'; fail=$((fail+1))
        else
            printf 'ok   the editor leaks nothing and reads nothing invalid\n'; pass=$((pass+1))
        fi
        # AND THE CONTROL THAT THE RUN ACTUALLY HAPPENED. It earned its place
        # immediately: the first draft used `/bin/sh -c "... exec vg_run ..."`,
        # and dash cannot exec a shell function, so the prompt never started --
        # the valgrind check reported `ok` over `exec: vg_run: not found`,
        # which is the silence of a clean run and the silence of no run at all.
        contains "and that run really edited a line" "132" \
            "$(tr -d '\r' < "$work/vgpty.out" | sed $'s/\033\\[[0-9]*[A-Za-z]//g')"
    else
        printf 'ok   SKIP (valgrind unavailable)\n'; pass=$((pass+1))
    fi
else
    echo "SKIP editing tier (no python3 for the pty)"
fi

# LOAD-BEARING FOR EVERY OTHER TIER IN THIS FILE: the editor must be OFF for a
# pipe. It writes escape sequences to repaint the row, and one of those in the
# output would be in every comparison above.
lacks "a pipe gets no escape sequences" "$(printf '\033')" "$(repl 'print 1
quit
')"

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
