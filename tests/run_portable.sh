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
eval "$(bash -c '. tests/portable.sh; declare -f gb_have_proc')"
# A real procfs answers yes; Cygwin's emulation (marked by /proc/self/winpid)
# answers no, because it cannot see a native process (tests/portable.sh).
if [ -e /proc/self/winpid ]; then
    if gb_have_proc; then bad "gb_have_proc is true under Cygwin's /proc"; else ok "gb_have_proc is false under Cygwin's /proc, which cannot see a native process"; fi
elif [ -r /proc/self/status ]; then
    if gb_have_proc; then ok "gb_have_proc is true on this machine"; else bad "gb_have_proc false with a real /proc"; fi
else
    if gb_have_proc; then bad "gb_have_proc is true with no /proc"; else ok "gb_have_proc is false with no /proc"; fi
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" = "0" ]
