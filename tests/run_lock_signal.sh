#!/usr/bin/env bash
# A program that has taken a `lock` must still DIE BY THE SIGNAL that killed it.
#
# The lock cleanup handler released the locks and then ended the process with
# `_exit(128 + signo)` -- which is the SHELL'S RENDERING of death by a signal,
# not the thing itself. A supervisor reading the real wait status saw
# `WIFSIGNALED=false, exit_code=143` where a program that had never taken a lock
# gives `WIFSIGNALED=true, signal=15`, so it could not tell a child that was
# KILLED from one that CHOSE to exit 143. And the handler is installed for the
# life of the process, so releasing the lock did not restore it: one `lock`
# anywhere in a program's history changed how every later death was reported.
#
# WHY IT SURVIVED: bash reports `143` in `$?` for both, so no shell-level check
# in this tree could see it. The probe uses waitpid directly.
#
# THE FIX IS THREE LINES AND ONE OF THEM IS LOAD-BEARING: a signal is BLOCKED
# while its own handler runs, so restoring SIG_DFL and calling `raise` is not
# enough -- the signal would merely be marked pending and the following `_exit`
# would still win. It must be UNBLOCKED first, or the fix looks right and
# changes nothing. That is what the perturbation below proves.
#
# Tiers:
#   PARITY      a locked child and an unlocked one must die the SAME way, for
#               SIGTERM, SIGINT and SIGHUP -- all three the handler claims.
#               Asserted as an EQUALITY, because the claim is that holding a
#               lock changes nothing about how a process dies.
#   DISTINCT    and a program that really exits 143 must STILL report an exit.
#               Without this, "report every death as a signal" passes PARITY.
#   RELEASED    the lock is actually gone afterwards. THIS IS AN OUTCOME CHECK
#               AND NOT A CONTROL, and the distinction was MEASURED rather than
#               assumed: with the three `signal()` installs deleted outright
#               this tier still passes, because `flock` is released by the
#               KERNEL when the process dies and the handler's cleanup is
#               belt-and-braces. So the handler did nothing the kernel does not
#               already do, and its only observable effect was the wrong exit
#               status this suite exists to fix. The tier stays because the
#               OUTCOME is what a caller depends on; the redundancy is filed in
#               DOGFOOD rather than acted on here.
#   VALGRIND    not run: the process under test dies by a signal on purpose, so
#               there is no clean exit for a leak report to be written at. Said
#               here rather than omitted silently.
set -euo pipefail

cd "$(dirname "$0")/.."
make >/dev/null
export GBASIC_PATH=stdlib

if ! command -v python3 >/dev/null 2>&1; then
    echo "SKIP run_lock_signal (no python3 for the waitpid probe -- a shell"
    echo "     cannot tell a signal death from exit 143, which is the point)"
    exit 0
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

probe() { python3 tests/lock_signal_probe.py "$1" "$2" "$work/lockfile"; }

printf 'TIER parity: a lock must not change how a process dies\n'
for pair in "15 SIGTERM" "2 SIGINT" "1 SIGHUP"; do
    set -- $pair
    signo="$1"; name="$2"
    locked="$(probe tests/lock_signal/holder.bas "$signo")"
    plain="$(probe tests/lock_signal/plain.bas "$signo")"
    if [ "$locked" = "$plain" ] && [ "$locked" = "signal $signo" ]; then
        pass "$name: both die by the signal ($locked)"
    else
        fail "$name: locked reports '$locked', unlocked reports '$plain'"
    fi
done

printf 'TIER distinct: a chosen exit code is still a chosen exit code\n'
# The control that stops the fix from being "call everything a signal". 143 is
# 128+15 on purpose: it is the number the old handler produced, so this is the
# exact collision the parity tier exists to remove.
#
# NO SIGNAL IS SENT (the 0), and that was a correction rather than a choice: a
# fixture that exits by itself RACES the signal, and with one it reported
# `signal 15` under a perturbation -- passing on timing rather than on the
# property it names.
chosen="$(probe tests/lock_signal/chose_143.bas 0)"
if [ "$chosen" = "exit 143" ]; then
    pass "a program that exits 143 reports an exit, not a signal"
else
    fail "a deliberate exit(143) reported '$chosen'"
fi

printf 'TIER released: the lock is gone (an outcome check, not a control)\n'
rm -f "$work/lockfile"
killed="$(probe tests/lock_signal/holder.bas 15)"
if [ "$killed" != "signal 15" ]; then
    fail "the holder did not die by the signal ($killed) -- the tier below would be meaningless"
else
    out="$(timeout 20 ./gbasic tests/lock_signal/released.bas "$work/lockfile" 2>&1 || true)"
    if [ "$out" = "second holder got it: true" ]; then
        pass "a second process takes the lock the killed one held"
    else
        fail "the lock outlived the process that held it (got: $out)"
    fi
fi

printf 'TIER valgrind: deliberately not run\n'
pass "SKIP (the process under test dies by a signal on purpose; there is no clean exit to report at)"

exit $status
