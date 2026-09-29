#!/usr/bin/env bash
# The PLATFORM LAYER (include/platform.h, src/platform_posix.c) and the build
# configurations that reach it.
#
# TWO TIERS, AND THE SECOND ONE FOUND A REAL BUG THE DAY IT WAS WRITTEN.
#
# 1. THE LAYER MAY NOT BE BYPASSED. Measured before it existed, the Linux-specific
#    surface of this interpreter is THREE mechanisms -- /proc/self/exe,
#    SOCK_SEQPACKET and PR_SET_PDEATHSIG -- and there were ZERO platform
#    conditionals anywhere, so Linux was an assumption rather than a target. They
#    are in one file now, and a new use outside it is how that stops being true:
#    the next person reaches for `prctl` because `prctl` is what the neighbouring
#    code used to say. A structural check, because no behavioural test on Linux
#    can notice a fourth Linux-only call being added.
#
# 2. EVERY MODULE MUST BUILD WHEN DISABLED ALONE, and this is the tier that
#    matters. CI builds with ALL optional modules and with NONE. The bug lives in
#    the MIDDLE: `make SQLITE3_AVAILABLE=0` DID NOT COMPILE on 2026-09-27 -- three
#    implicit declarations -- because the shared SQL diagnostic formatter sat
#    inside `#if HAVE_SQLITE3` while `pg` and `odbc` called it. All-or-nothing
#    cannot reach that, and "some" is the NORMAL case: a machine with libpq and no
#    SQLite development files could not build gBASIC at all.
#
#    A parallel build is ~3s, so disabling each flag ALONE costs about a minute
#    for the whole matrix -- cheap enough that there is no argument for sampling.
#
# LEAVES A FULL BUILD BEHIND, deliberately and asserted: suites after this one in
# run_all.sh use ./gbasic and would silently SKIP their module tiers against a
# lean binary, which is a green line that tested nothing.
. "$(dirname "$0")/portable.sh"
set -uo pipefail
cd "$(dirname "$0")/.."

pass=0; fail=0
ok()  { printf '  ok   %s\n' "$1"; pass=$((pass+1)); }
bad() { printf '  FAIL %s\n' "$1"; fail=$((fail+1)); }

echo "TIER the platform layer is not bypassed"
# Comments are stripped first: this file and platform.h DISCUSS all three
# mechanisms at length, and a tripwire that fires on prose is one people disable.
# THE COMMENTS ARE STRIPPED TO A FILE, and the file is what gets grepped.
# Two wrong shapes were tried first and BOTH reported ok with a planted `prctl`
# sitting in eval.c -- `strip_comments "$f" | grep -q`, and the same through a
# shell variable. eval.c strips to 1.1 MB and the round trip loses the match.
# A tripwire that cannot fail is indistinguishable from one that passes, and
# only perturbing it showed the difference; this form is grepped from a file,
# where there is nothing left to go wrong.
strip_scratch="$(mktemp)"
trap 'rm -f "$strip_scratch"' EXIT
strip_comments() {
    sed 's|/\*.*\*/||g; s|//.*||; /^[[:space:]]*\*/d; /^[[:space:]]*\/\*/d' "$1" > "$strip_scratch"
}
for mech in 'prctl[[:space:]]*(' '/proc/self/exe' 'SOCK_SEQPACKET'; do
    hits=""
    for f in src/*.c src/*/*.c include/*.h; do
        case "$f" in (src/platform_posix.c|include/platform.h|src/parser.tab.c) continue ;; esac
        strip_comments "$f"
        if grep -q "$mech" "$strip_scratch"; then hits="$hits $f"; fi
    done
    if [ -z "$hits" ]; then ok "no code outside the platform layer uses $mech"
    else bad "$mech is used outside the platform layer:$hits"; fi
done
# ...and the layer must actually be REACHED, or the tripwire above is satisfied
# by a build that deleted the feature.
for want in gb_exe_path gb_channel_socketpair gb_arm_parent_death \
            gb_net_init gb_sock_close gb_sock_set_blocking; do
    n=$(grep -l "$want" src/eval.c src/actor.c 2>/dev/null | wc -l)
    [ "$n" -ge 1 ] && ok "$want is called from the interpreter" \
                   || bad "$want is defined but nothing calls it"
done

echo "TIER the Windows bodies still compile"
# src/platform_win32.c is in NO build -- the Makefile compiles the posix one --
# so without this it would rot silently until somebody tried the port. Checked
# against REAL windows.h, not a shim, which is why it is worth anything.
if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    if x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Iinclude -D_WIN32_WINNT=0x0601 \
           -fsyntax-only src/platform_win32.c 2>/tmp/gb_win32.log; then
        ok "src/platform_win32.c compiles against real windows.h"
    else
        bad "src/platform_win32.c: $(head -1 /tmp/gb_win32.log)"
    fi
    # every function the header declares must HAVE a Windows body, or the port
    # discovers the gap on a machine we are borrowing.
    for want in gb_exe_path gb_channel_socketpair gb_arm_parent_death gb_net_init \
                gb_sock_close gb_sock_set_blocking gb_platform_name; do
        grep -q "$want" src/platform_win32.c && ok "win32: $want" || bad "win32: $want has no body"
    done
else
    printf '  SKIP Windows bodies (no x86_64-w64-mingw32-gcc; apt install gcc-mingw-w64-x86-64)\n'
fi

echo "TIER every optional module builds when disabled ALONE"
flags=$(grep -o '^[A-Z0-9_]*_AVAILABLE' Makefile | sort -u)
for f in $flags; do
    make clean >/dev/null 2>&1
    if make -j4 "$f=0" >/tmp/gb_platform_build.log 2>&1; then
        ok "make $f=0"
    else
        bad "make $f=0 -- $(grep -m1 -oE 'error: .*' /tmp/gb_platform_build.log | cut -c1-70)"
    fi
done
# and with every one of them off at once, which is the configuration the release
# tarball's lean tier is built in.
make clean >/dev/null 2>&1
alloff=""; for f in $flags; do alloff="$alloff $f=0"; done
# shellcheck disable=SC2086
if make -j4 $alloff >/tmp/gb_platform_build.log 2>&1; then ok "make with every module off"
else bad "make with every module off -- $(grep -m1 -oE 'error: .*' /tmp/gb_platform_build.log | cut -c1-70)"; fi

echo "TIER a full build is restored for the suites that follow"
make clean >/dev/null 2>&1
if make -j4 >/tmp/gb_platform_build.log 2>&1 && [ -x ./gbasic ]; then
    ok "full build restored ($(./gbasic --version))"
else
    bad "could not restore a full build -- later suites would test a lean binary"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" = "0" ]
