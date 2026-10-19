#!/usr/bin/env bash
# tests/portable.sh -- the shims that let this gate run on a machine without GNU
# coreutils, and the reason each one is shaped the way it is.
#
# THIS SUITE EXISTS BECAUSE THE SHIMS CANNOT OTHERWISE RUN HERE. On Linux
# portable.sh installs NOTHING by design, so every fallback in it is dead code
# on the machine it was written on -- and a fallback that has never run is a
# fallback that does not work. GB_PORTABLE_FORCE exists for this suite.
#
# THE LOAD-BEARING TIER IS "NEVER UNBOUNDED". 90 suites pass `timeout` a bound
# because a hung fixture otherwise sits until run_all.sh's per-suite cap while
# reporting nothing -- and a shim that quietly ran the command unbounded would
# still print PASS. So the hang tier is itself bounded by the REAL timeout, and
# it fails if the shim lets a hang through.
set -uo pipefail
cd "$(dirname "$0")/.."

pass=0; fail=0
ok()   { printf '  ok   %s\n' "$1"; pass=$((pass+1)); }
bad()  { printf '  FAIL %s\n' "$1"; fail=$((fail+1)); }
is()   { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1: got [$2], want [$3]"; fi }

echo "TIER on this machine portable.sh must change nothing"
# Adding a source line to 90 suites is only safe if it is a no-op here.
out="$(bash -c '. tests/portable.sh; echo "[$GB_PORTABLE_SHIMS]"; type -t timeout')"
is "no shim is installed where the native tool exists" "$(printf '%s' "$out" | head -1)" "[]"
is "timeout is still the real binary, not a function" "$(printf '%s' "$out" | tail -1)" "file"

echo "TIER the shell timeout fallback"
run_forced() { GB_PORTABLE_FORCE="timeout:timeout_shell" bash -c '. tests/portable.sh; '"$1"'; echo "status=$?"'; }

is "a command that finishes keeps its own exit status" \
   "$(run_forced 'timeout 10 true')" "status=0"
is "and a nonzero status is passed through, not swallowed" \
   "$(run_forced 'timeout 10 sh -c "exit 7"')" "status=7"
is "the shim is actually a function when forced" \
   "$(GB_PORTABLE_FORCE=timeout:timeout_shell bash -c '. tests/portable.sh; type -t timeout')" "function"

# A HANG MUST BE KILLED AND REPORTED AS 124, like GNU timeout. Bounded by the
# REAL timeout, because if the shim is broken this is exactly a hang.
got="$(timeout 30 env GB_PORTABLE_FORCE=timeout:timeout_shell \
        bash -c '. tests/portable.sh; timeout 1 sleep 60; echo "status=$?"' 2>/dev/null)"
is "a hang is killed and reported 124" "$got" "status=124"

# -k, for a child that ignores SIGTERM. Without it the bound is advisory, which
# matters here because this interpreter installs a SIGTERM handler.
got="$(timeout 30 env GB_PORTABLE_FORCE=timeout:timeout_shell \
        bash -c '. tests/portable.sh; timeout -k 1 1 sh -c "trap \"\" TERM; sleep 60"; echo "status=$?"' 2>/dev/null)"
is "-k kills a child that ignores SIGTERM" "$got" "status=124"

echo "TIER it refuses rather than running unbounded"
is "no command given is a refusal, not a silent success" \
   "$(run_forced 'timeout 5' 2>/dev/null)" "status=125"

echo "TIER a timeout is reported by what HAPPENED, not guessed from the status"
# THE PRECISION POINT, and the reason this shim uses a marker file. Mapping exit
# 143 to GNU's 124 would call every command a caller deliberately SIGTERMs a
# timeout -- and this tree has suites that SIGTERM a child on purpose
# (run_lock_signal, run_web_pool's drain, run_stream's mid-output kill).
got="$(timeout 30 env GB_PORTABLE_FORCE=timeout:timeout_shell bash -c '
    . tests/portable.sh
    timeout 30 sh -c "kill -TERM \$\$" ; echo "status=$?"' 2>/dev/null)"
is "a command SIGTERMed by someone else is NOT called a timeout" "$got" "status=143"

echo "TIER stat helpers"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
printf '12345' > "$tmp/f"; chmod 640 "$tmp/f"
eval "$(bash -c '. tests/portable.sh; declare -f gb_stat_size gb_stat_mode gb_stat_inode gb_stat_device')"
is "gb_stat_size"  "$(gb_stat_size "$tmp/f")"  "5"
# 640 is the premise, and chmod does not set it everywhere (under MSYS2 the
# file stays 644). Where it did not take, the helper is held to `stat` itself,
# which is what it wraps -- and the skipped premise is said, not passed.
if [ "$(stat -c '%a' "$tmp/f")" = "640" ]; then
    is "gb_stat_mode"  "$(gb_stat_mode "$tmp/f")"  "640"
else
    printf '  SKIP gb_stat_mode = 640 (chmod is not honoured here)\n'
    is "gb_stat_mode agrees with stat" "$(gb_stat_mode "$tmp/f")" "$(stat -c '%a' "$tmp/f")"
fi
is "gb_stat_inode" "$(gb_stat_inode "$tmp/f")" "$(stat -c '%i' "$tmp/f")"
is "gb_stat_device" "$(gb_stat_device "$tmp/f")" "$(stat -c '%d' "$tmp/f")"

echo "TIER sha256sum fallbacks agree with the real thing"
want="$(sha256sum "$tmp/f" | cut -d' ' -f1)"
for forced in sha256sum; do
    if command -v openssl >/dev/null 2>&1; then
        got="$(GB_PORTABLE_FORCE="$forced" bash -c '. tests/portable.sh
                 command -v shasum >/dev/null 2>&1 || true
                 sha256sum "$1" | cut -d" " -f1' _ "$tmp/f")"
        is "the fallback hash equals sha256sum's" "$got" "$want"
    else
        printf '  SKIP sha256 fallback (no openssl/shasum)\n'
    fi
done

echo "TIER /proc is asked about, never shimmed"
# BOTH FUNCTIONS, because `gb_have_proc` CALLS the other one. Extracted alone
# it printed `_gb_portable_forced: command not found` on every run and then
# answered correctly BY LUCK -- the missing call returns 127, the `&&`
# short-circuits, and the real test below it runs. So the answer was right, the
# noise was real, and `GB_PORTABLE_FORCE=noproc` could not be exercised here at
# all, which is the half this tier would have needed to check the Cygwin branch.
eval "$(bash -c '. tests/portable.sh; declare -f gb_have_proc _gb_portable_forced')"
# A real procfs answers yes; Cygwin's emulation (marked by /proc/self/winpid)
# answers no, because it cannot see a native process (tests/portable.sh).
if [ -e /proc/self/winpid ]; then
    if gb_have_proc; then bad "gb_have_proc is true under Cygwin's /proc"; else ok "gb_have_proc is false under Cygwin's /proc, which cannot see a native process"; fi
elif [ -r /proc/self/status ]; then
    if gb_have_proc; then ok "gb_have_proc is true on this machine"; else bad "gb_have_proc false with a real /proc"; fi
else
    if gb_have_proc; then bad "gb_have_proc is true with no /proc"; else ok "gb_have_proc is false with no /proc"; fi
fi
# AND NOW THE FORCE CAN BE EXERCISED, which is what the missing helper cost:
# the Cygwin branch above only runs on Cygwin, so without this the "answers no"
# half of gb_have_proc is unreachable on every machine in the gate.
if ( GB_PORTABLE_FORCE=noproc; gb_have_proc ); then
    bad "GB_PORTABLE_FORCE=noproc did not make gb_have_proc answer no"
else
    ok "GB_PORTABLE_FORCE=noproc makes it answer no, so the no-/proc branch is reachable here"
fi

# --- tests/premise.sh: the OTHER harness file, and the hazard it adds ----
#
# `portable.sh` supplies a missing TOOL; `premise.sh` answers whether this
# machine can DEMONSTRATE a premise (does `chmod` govern reading, is there a
# `mkfifo`, a peak-RSS meter, a real symlink, a settable stack). It exists
# because six suites failed on Windows for reasons that were facts about the
# harness and not about gBASIC (docs/windows_port_status.md §30), and a suite
# that fails for the wrong reason is one people learn to ignore.
#
# THE HAZARD IS THE REASON THESE CHECKS ARE HERE, and it is a SILENT SHRINK
# rather than a failure: a suite that CALLS a probe without SOURCING the file
# gets 127, which `if ! premise_x` reads as "cannot", so every guarded tier SKIPS
# and the suite stays GREEN while measuring nothing. Demonstrated, not reasoned.
. "$(dirname "$0")/premise.sh"

for p in premise_mode_bits premise_rss_meter premise_fifo premise_real_symlink premise_stack_limit; do
    if ! command -v "$p" >/dev/null 2>&1; then
        bad "$cap is not defined by premise.sh"
    fi
done
ok "premise.sh defines all five probes"

# EVERY CALLER MUST SOURCE IT. The grep is for a CALL in command position, so a
# mention in a comment does not count.
#
# IT CAUGHT ITS OWN STALE PATTERN WITHIN THE HOUR, which is the best
# endorsement available: the file was renamed from `capabilities.sh` and the
# sweep that renamed every reference could not match THIS line, the filename
# being spelled `premise\.sh` inside a regex. So the one place the rename
# missed was the check, and the check reported it by name.
missing=""
for f in tests/run_*.sh; do
    if grep -qE '^[[:space:]]*(if ! |if |elif ! |elif )?premise_[a-z_]+' "$f" \
       && ! grep -q 'premise\.sh' "$f"; then
        missing="$missing $f"
    fi
done
if [ -n "$missing" ]; then
    bad "these suites call a probe without sourcing premise.sh:$missing"
else
    ok "every suite that calls a probe sources premise.sh"
fi
# ITS VACUITY CONTROL: a scanner that matched nothing would pass that too.
probe_dir="$(mktemp -d)"
printf 'if ! premise_mode_bits; then
  :
fi
' > "$probe_dir/run_planted.sh"
if grep -qE '^[[:space:]]*(if ! |if |elif ! |elif )?premise_[a-z_]+' "$probe_dir/run_planted.sh"; then
    ok "CONTROL: the scanner really does match a planted call"
else
    bad "the scanner found nothing even in a planted file"
fi
rm -rf "$probe_dir"

# ON A FULL MACHINE EVERY PROBE ANSWERS YES, so nothing here is silently
# skipping. If one of these goes to SKIP on this machine, a suite somewhere has
# quietly stopped measuring something and this is where it shows.
for p in premise_mode_bits premise_rss_meter premise_fifo premise_real_symlink premise_stack_limit; do
    if $p; then ok "$p answers yes here"; else printf '  SKIP %s answers no on this machine -- the tiers resting on it measure NOTHING here\n' "$p"; fi
done

# THE FORCING SEAM, which is how a skip path gets proven at all: on a machine
# where every probe answers yes, every skip is UNREACHABLE and therefore
# untested. Each probe reads its cache from the environment.
if ( _premise_mode_bits=no; premise_mode_bits ); then
    bad "the forcing seam does not work, so no skip path can be proven"
else
    ok "a probe can be forced to no, which is how each skip was checked to say the right thing"
fi

echo "TIER a suite's own messages do not run the shell"
# A BACKTICK INSIDE A DOUBLE-QUOTED STRING IS COMMAND SUBSTITUTION, and this
# tree has produced ELEVEN instances -- a message quoting a name the way this
# project quotes names (`load`, `qt.confirm`, `unreachable`) and the shell
# running it. IT IS INVISIBLE WHERE IT IS WRITTEN: the substitution usually
# fails quietly, so the line prints with the quoted word MISSING and reads as an
# ordinary sentence, and in a `fail` message it only surfaces on the day a tier
# legitimately goes red. FOUR WERE LIVE when this tier was added and none was
# written by the session that found them, which is the argument for a scanner
# over another paragraph -- the release script's own backtick tripwire has
# covered its container string since the third instance and could never see
# these. Sited here because a suite's message is part of the harness.
if python3 tools/scan_shell_backticks.py tests/*.sh tools/*.sh > "$tmp/backticks.txt" 2>&1; then
    ok "no suite or tool message hands a quoted name to the shell"
else
    head -8 "$tmp/backticks.txt"
    bad "a message would run a quoted name as a command"
fi
# ITS OWN VACUITY CONTROL, because "no offenders" is equally satisfied by a
# scanner that reads nothing: the selftest plants one and requires the two safe
# spellings beside it to stay silent.
if python3 tools/scan_shell_backticks.py --selftest >/dev/null 2>&1; then
    ok "CONTROL: and it still flags a planted one while leaving the safe spellings alone"
else
    python3 tools/scan_shell_backticks.py --selftest 2>&1 | head -4
    bad "CONTROL: the scanner's selftest failed, so it is measuring nothing"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" = "0" ]
