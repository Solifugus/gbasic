#!/usr/bin/env bash
set -uo pipefail

# The valgrind policy itself (tests/valgrind_tier.sh), which 35 suites now
# share.
#
# WHY THIS EXISTS. Sharing the policy removed 12 inconsistent invocations, and
# it introduced one new failure mode that did not exist before: if `vg_run`
# ever stops actually running valgrind -- a bad edit, a renamed variable, a
# source line that silently does nothing -- then 35 valgrind tiers go on
# printing "no definite leak or invalid access" while asserting NOTHING. A
# single shared mechanism needs a single control proving it is not a no-op.
#
# It cannot be proved with gBASIC, because the interpreter is (as far as every
# other suite can tell) clean -- so the control is two three-line C programs
# with a KNOWN definite leak and a KNOWN invalid read. If valgrind is really
# running, they fail; if it is not, they pass, and this suite goes red.
#
# It also pins the ONE AXIS on which the two policies differ, which is the
# whole reason there are two: `vg_run` fails on a definite leak and
# `vg_run_access_only` does not, while BOTH fail on an invalid read. Without
# that pair, access-only would be indistinguishable from "valgrind is off" --
# which is exactly what it must not become, since it is what run_odbc,
# run_continuation and run_smtp assert through.
#
# Skips cleanly without valgrind or a C compiler.

cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"

checks=0; failures=0
pass() { checks=$((checks+1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks+1)); failures=$((failures+1)); printf '  FAIL %s\n' "$1"; }

if ! vg_available; then
    printf 'SKIP run_valgrind_policy (valgrind not installed)\n'
    exit 0
fi
CC="${CC:-cc}"
if ! command -v "$CC" >/dev/null 2>&1; then
    printf 'SKIP run_valgrind_policy (no C compiler)\n'
    exit 0
fi

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

cat >"$scratch/leak.c" <<'EOF'
#include <stdlib.h>
#include <string.h>
int main(void) { char *p = malloc(64); strcpy(p, "leaked"); return p[0] == 'l' ? 0 : 1; }
EOF
cat >"$scratch/bad.c" <<'EOF'
#include <stdlib.h>
int main(void) { int *a = malloc(4 * sizeof(int)); int x = a[7]; free(a); return x & 0; }
EOF
"$CC" -g -O0 -o "$scratch/leak" "$scratch/leak.c" 2>/dev/null || { printf 'SKIP run_valgrind_policy (could not build the controls)\n'; exit 0; }
"$CC" -g -O0 -o "$scratch/bad" "$scratch/bad.c" 2>/dev/null || { printf 'SKIP run_valgrind_policy (could not build the controls)\n'; exit 0; }

printf 'TIER the policy is not a no-op\n'

# Both controls exit 0 on their own. If they still exit 0 under vg_run,
# valgrind is not running and every valgrind tier in the tree is decorative.
"$scratch/leak" && "$scratch/bad" \
    && pass "both controls exit 0 when run directly" \
    || fail "both controls exit 0 when run directly"

vg_run "$scratch/leak" >/dev/null 2>&1
[ "$?" = "$VG_EXIT" ] && pass "vg_run fails on a definite leak (exit $VG_EXIT)" \
                      || fail "vg_run fails on a definite leak"

vg_run "$scratch/bad" >/dev/null 2>&1
[ "$?" = "$VG_EXIT" ] && pass "vg_run fails on an invalid read" \
                      || fail "vg_run fails on an invalid read"

vg_run /bin/true >/dev/null 2>&1
[ "$?" = "0" ] && pass "and a clean program still passes" \
               || fail "and a clean program still passes"

printf 'TIER the two policies differ on exactly one axis\n'

# This pair is what makes access-only a distinct claim rather than a way of
# turning valgrind off.
vg_run_access_only "$scratch/leak" >/dev/null 2>&1
[ "$?" = "0" ] && pass "vg_run_access_only makes no leak claim" \
               || fail "vg_run_access_only makes no leak claim"

vg_run_access_only "$scratch/bad" >/dev/null 2>&1
[ "$?" = "$VG_EXIT" ] && pass "but still fails on an invalid read" \
                      || fail "but still fails on an invalid read"

printf 'TIER extras reach valgrind\n'

# run_odbc passes its suppression file this way. If VG_EXTRA were dropped, the
# driver-internal defects it names would surface and that suite would go red --
# noisily rather than silently, but the mechanism should be pinned regardless.
VG_EXTRA=--suppressions="$scratch/nonexistent.supp" vg_run_access_only /bin/true >"$scratch/o" 2>"$scratch/e"
if grep -qi "suppress" "$scratch/e"; then
    pass "VG_EXTRA is passed through (valgrind objected to a missing file)"
else
    fail "VG_EXTRA is passed through"
fi

printf 'TIER every suite goes through the policy\n'

# The tripwire that keeps the consolidation from unravelling: a new suite (or
# an edited old one) calling valgrind directly would run under whatever flags
# its author happened to type, which is the state this replaced.
raw=$(grep -lE '^\s*(if\s+)?([A-Za-z_]+=[^ ]+\s+)*valgrind\s' tests/run_*.sh | grep -v run_valgrind_policy.sh || true)
if [ -z "$raw" ]; then
    pass "no suite invokes valgrind directly"
else
    fail "these suites invoke valgrind directly instead of vg_run: $raw"
fi

users=$(grep -l 'vg_run' tests/run_*.sh | grep -v run_valgrind_policy.sh | wc -l)
if [ "$users" -ge 35 ]; then
    pass "$users suites use the shared policy"
else
    fail "only $users suites use the shared policy (expected at least 35)"
fi

printf '\nrun_valgrind_policy: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
