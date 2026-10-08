#!/usr/bin/env bash
# Failures that reported without a diagnostic, promoted to raises.
#
# Both cases here shared one signature, catalogued in
# docs/warning_model_design.md §7: an UNLOCATED bare line on stderr, a result of
# `nothing`, and EXIT CODE 0. `nothing` is a legitimate value, so a caller could
# not tell the failure from a real one, and CI saw success. Neither is a warning
# — they are raises that were never written:
#
#   goto <unknown label>   printed, then ABANDONED THE REST OF THE FUNCTION, so
#                          a typo'd label silently truncated it
#   a[99] (read)           printed and yielded `nothing` — while out-of-range
#                          ASSIGNMENT called runtime_error_raise properly, two
#                          lines away in the same source file
#
# Being raises, they are now located, fatal by default, and CATCHABLE, which a
# printed line never was.
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
fail() { printf 'FAIL %s\n' "$1"; exit 1; }
# Counted, not hardcoded. The summary carried a literal 12 that happened to be
# right only because whoever last added a case remembered to bump it -- and a
# gate reporting a number it does not measure can shrink without saying so.
cases=0
ok() { cases=$((cases + 1)); printf 'PASS %s\n' "$1"; }

# --- fatal, located, nonzero ----------------------------------------------
fatal() { # file expected-fragment
    if ./gbasic "tests/silent_traps/$1" >"$scratch/out" 2>"$scratch/err"; then
        fail "$1 (expected a NONZERO exit; a silent trap is one that exits 0)"
    fi
    grep -qF "$2" "$scratch/err" \
        || fail "$1 (missing: $2; got: $(cat "$scratch/err"))"
    # Located: the old bare line carried no file:line, which is what made these
    # untraceable in a real program.
    grep -qE "^runtime error at .+:[0-9]+:[0-9]+:" "$scratch/err" \
        || fail "$1 (the diagnostic is not LOCATED: $(cat "$scratch/err"))"
    grep -q "unreachable" "$scratch/out" \
        && fail "$1 (execution continued past the failure)"
    ok "${1%.bas}"
}

fatal label_goto.bas "unknown label in function f: nowhere"
fatal label_gosub.bas "unknown label in function f: nowhere"
fatal index_read.bas "array index out of range"
fatal modifier_date.bas "date modifier expects an ISO-like date string"
fatal modifier_file.bas "file modifier expects a path string"
fatal watch_undefined.bas "undefined variable: nope"
fatal watch_body.bas "undefined variable: undefined_in_body"
# The directory family, found 2026-08-27 by auditing all 176 builtins for this
# exact signature rather than by reading for it. `list`, `files` and `folders`
# each had TWO bare fprintf refusals -- wrong arity, and a non-directory
# argument -- and both returned `nothing` with exit 0. They were the ONLY three
# builtins left with the shape; the sweep is what makes that a measurement
# rather than a hope.
fatal dir_arity.bas "list expects one directory argument"
fatal dir_type.bas  "files expects a string or directory reference"

# --- catchable, which the printed line never was --------------------------
for name in label_caught index_read_caught modifier_caught watch_caught dir_caught; do
    ./gbasic "tests/silent_traps/$name.bas" >"$scratch/got" 2>"$scratch/err" \
        || fail "$name (exited nonzero: $(cat "$scratch/err"))"
    diff -u "tests/silent_traps/$name.out" "$scratch/got" \
        || fail "$name (output diverged)"
    ok "$name"
done

# --- the write path was always right; it must stay that way ---------------
printf 'program main( args )\n    a = [1]\n    a[9] = 2\nend program\n' >"$scratch/w.bas"
if ./gbasic "$scratch/w.bas" >/dev/null 2>"$scratch/err"; then
    fail "write path (out-of-range assignment must still raise)"
fi
grep -qF "array index out of range" "$scratch/err" \
    || fail "write path (message changed: $(cat "$scratch/err"))"
ok write_path_unchanged

# --- the modifier that was ALWAYS right must stay that way ----------------
# `USD` raised from the start, four lines from `date` in the same dispatch
# function. That neighbouring inconsistency is what made this a bug rather
# than a policy.
#
# THE MESSAGE MOVED IN rc9 AND THE MOVE IS THE POINT. PLAT-MONEY phase 0 made
# `USD` reflective: it now accepts decimal TEXT as well as a number, so "nope"
# is no longer a type error -- it reaches the parser and fails there, with a
# message that says what is actually wrong. Both spellings are still a raise,
# which is what this tier exists to protect, so the two cases below assert the
# type refusal and the parse refusal SEPARATELY rather than loosening the
# match to whatever comes out.
printf 'program main( args )\n    m{USD} = "nope"\nend program\n' >"$scratch/m.bas"
if ./gbasic "$scratch/m.bas" >/dev/null 2>"$scratch/err"; then
    fail "USD (unparseable text must still raise)"
fi
grep -qF "money text is not a number" "$scratch/err" \
    || fail "USD (message changed: $(cat "$scratch/err"))"

# A value that is not text and not a number is still a TYPE refusal -- the
# reflective modifier widened what it accepts, it did not stop refusing.
printf 'program main( args )\n    m{USD} = [1, 2]\nend program\n' >"$scratch/m2.bas"
if ./gbasic "$scratch/m2.bas" >/dev/null 2>"$scratch/err"; then
    fail "USD (a wrong type must still raise)"
fi
grep -qF "USD modifier expects a number or decimal text" "$scratch/err" \
    || fail "USD type refusal (message changed: $(cat "$scratch/err"))"
ok usd_unchanged

# A REFUSAL THAT WAS REPORTED AND THEN IGNORED -- the same family as the two
# above with one extra failure: this one HAD its diagnostic and still had no
# consequence. Six value kinds refuse to be a condition (`unknown`, and the five
# live connection handles), and refusing is right: silently reading an absent
# value as false makes a missing answer indistinguishable from a negative one.
# But `value_truthy` raised and returned 0, and every caller checked for a raise
# BEFORE calling it and never after -- so `if unknown then` printed the error
# and RAN THE ELSE BRANCH, `while unknown` printed it and carried on past the
# loop, and none of it could be caught.
#
# `do ... until unknown` did not merely continue: it HUNG, because a condition
# that can never become true is an infinite loop. Measured before the fix at
# 5.18 million iterations in three seconds. A hang is not a failure, which is
# why that case is bounded here.
#
# Found by planning Chapter 1 of the book against the language (DOGFOOD 14).
for form in if while until; do
    case "$form" in
        if)    body='if x then\n        print("BRANCH")\n    else\n        print("BRANCH")\n    end if' ;;
        while) body='while x\n        print("BRANCH")\n    end while' ;;
        until) body='do\n        print("BODY")\n    until x' ;;
    esac
    printf "program main( args )\n    x = unknown\n    $body\n    print(\"CONTINUED\")\nend program\n" >"$scratch/c.bas"
    if timeout -k 5 20 ./gbasic "$scratch/c.bas" >"$scratch/out" 2>"$scratch/err"; then
        fail "condition/$form (a refused condition must not exit 0)"
    fi
    grep -qF "cannot be used as a condition" "$scratch/err" \
        || fail "condition/$form (no diagnostic: $(cat "$scratch/err"))"
    # THE LOAD-BEARING HALF. The diagnostic was always there; what was missing
    # was the consequence, so what must be asserted is that nothing ran after
    # it -- and for `if`, that the ELSE branch did not stand in for a condition
    # nobody could evaluate.
    grep -qF "CONTINUED" "$scratch/out" \
        && fail "condition/$form (execution continued past the refusal)"
    if [ "$form" = "if" ]; then
        grep -qF "BRANCH" "$scratch/out" && fail "condition/if (a branch ran anyway)"
    fi
done
ok condition_refusal_stops

# AND IT IS CATCHABLE, which is what a printed line never was -- the property
# every case in this file exists to establish.
printf 'program main( args )\n    on error goto next\n    x = unknown\n    if x then\n        print("THEN")\n    end if\n    if error then\n        print("CAUGHT")\n    end if\nend program\n' >"$scratch/c2.bas"
timeout -k 5 20 ./gbasic "$scratch/c2.bas" >"$scratch/out" 2>"$scratch/err" \
    || fail "condition (catching it should leave exit 0)"
grep -qF "CAUGHT" "$scratch/out" || fail "condition (on error goto next did not see it)"
ok condition_refusal_catchable

# THE CONTROL, and without it every assertion above is satisfied by a build that
# refuses every condition there is. An ordinary condition still works, and
# `nothing` -- which is a value rather than an absence of one -- is still false.
printf 'program main( args )\n    if 1 = 1 then\n        print("YES")\n    end if\n    if nothing then\n        print("NO")\n    else\n        print("NOTHING IS FALSE")\n    end if\n    n = 0\n    do\n        n = n + 1\n    until n = 3\n    print("LOOPED " + string(n))\nend program\n' >"$scratch/c3.bas"
timeout -k 5 20 ./gbasic "$scratch/c3.bas" >"$scratch/out" 2>"$scratch/err" \
    || fail "condition control (ordinary conditions must still work: $(cat "$scratch/err"))"
grep -qF "YES" "$scratch/out" || fail "condition control (a true condition did not run)"
grep -qF "NOTHING IS FALSE" "$scratch/out" || fail "condition control (nothing should be false)"
grep -qF "LOOPED 3" "$scratch/out" || fail "condition control (do-until stopped working)"
ok condition_ordinary_unchanged

# --- a write that never reached the file ------------------------------------
# `write` and `append` returned TRUE with exit 0 when the bytes never arrived:
# fwrite only fills stdio's buffer, the real write is in fclose, and fclose's
# result was ignored. Found by the Windows port, where a write inside
# `with lock(f)` vanished this way, and then MEASURED HERE on Linux: /dev/full
# accepts the open and fails the flush with ENOSPC, which is a full disk or a
# quota in miniature.
printf 'program main( args )\n    f {file}= "/dev/full"\n    ok = write(f, "hello")\n    print("CONTINUED " + string(ok))\nend program\n' >"$scratch/w.bas"
flush_reachable=0
if [ -w /dev/full ]; then
    if ./gbasic "$scratch/w.bas" >"$scratch/out" 2>"$scratch/err"; then
        fail "write_flush (a write that never happened exited 0: $(cat "$scratch/out"))"
    fi
    # THE REASON DECIDES WHETHER THIS TIER MEASURED ANYTHING. The shell having
    # /dev/full does not mean the BINARY does: under MSYS2 driving a native
    # gbasic.exe, "/dev/full" is a missing file, the OPEN fails, and the tier
    # would pass for a cause it is not about (measured there). Only a flush
    # failure says "No space left on device".
    grep -qF "No space left on device" "$scratch/err" && flush_reachable=1
fi
if [ "$flush_reachable" = 1 ]; then
    grep -qF "could not write file" "$scratch/err" \
        || fail "write_flush (no diagnostic: $(cat "$scratch/err"))"
    grep -qF "CONTINUED" "$scratch/out" && fail "write_flush (execution continued past it)"
    ok write_flush_failure_raises

    printf 'program main( args )\n    on error goto next\n    f {file}= "/dev/full"\n    append(f, "more")\n    if error then\n        print("CAUGHT")\n    end if\nend program\n' >"$scratch/w2.bas"
    ./gbasic "$scratch/w2.bas" >"$scratch/out" 2>"$scratch/err" \
        || fail "write_flush (catching it should leave exit 0: $(cat "$scratch/err"))"
    grep -qF "CAUGHT" "$scratch/out" || fail "write_flush (append's failure was not catchable)"
    ok append_flush_failure_catchable
else
    printf 'SKIP write_flush (the binary cannot reach a real /dev/full here; the flush failure is NOT measured: %s)\n' \
        "$(cat "$scratch/err" 2>/dev/null)"
fi

# THE CONTROL: an ordinary write still returns true and still writes, or the
# two checks above are satisfied by a build that refuses every write.
# The path is written INTO the program, so it must be one the binary can open:
# under MSYS2 driving a native gbasic.exe, /tmp/... means nothing to Windows,
# and `cygpath -m` gives the C:/... form. Elsewhere cygpath does not exist and
# the path is used as it is.
scratch_native="$(cygpath -m "$scratch" 2>/dev/null || printf '%s' "$scratch")"
printf 'program main( args )\n    f {file}= "%s/ok.txt"\n    r = write(f, "abc")\n    r2 = append(f, "def")\n    print(string(r) + " " + string(r2) + " " + read(f))\nend program\n' "$scratch_native" >"$scratch/w3.bas"
./gbasic "$scratch/w3.bas" >"$scratch/out" 2>"$scratch/err" \
    || fail "write control (an ordinary write failed: $(cat "$scratch/err"))"
grep -qxF "true true abcdef" "$scratch/out" || fail "write control (got: $(cat "$scratch/out"))"
ok ordinary_write_unchanged

printf 'run_silent_traps: %d cases passed\n' "$cases"

