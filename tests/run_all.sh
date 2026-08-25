#!/usr/bin/env bash
# Run every suite in tests/, discovered by GLOB rather than by a list.
#
# The list is the point. Three suites (run_ari, run_nap_fs, run_render) sat
# broken from 0.1.0-rc6 to rc7 because the brace-modifier migration drove off
# `.bas` files and those three embed gBASIC in a shell heredoc -- and nothing
# noticed, because every gate anyone ran was a hand-maintained list that did not
# happen to name them. A gate you have to remember to extend is a gate that
# silently shrinks; docs/project_state.md's hand-maintained document index
# rotted the same way and for the same reason.
#
# So: no list. A new tests/run_*.sh is in the gate the moment it exists.
#
# Usage:
#   tests/run_all.sh                 # every suite
#   tests/run_all.sh web             # only suites whose name contains "web"
#   SUITE_TIMEOUT=1200 tests/run_all.sh
#   RUN_MANUAL=1 tests/run_all.sh    # include the manual/very-slow tiers
#
# Exit 0 only if every suite that ran passed. A suite that SKIPPED is reported
# as such and does NOT count as a pass -- a green line that ran nothing is the
# other way a gate silently shrinks.
set -uo pipefail
cd "$(dirname "$0")/.."

filter="${1:-}"
timeout_s="${SUITE_TIMEOUT:-1800}"
logdir="$(mktemp -d)"

# Excluded unless RUN_MANUAL=1, and NAMED when excluded rather than quietly
# dropped: run_xml_bigfile generates a >=100 MB document and is documented as a
# manual tier. Anything added here must be announced the same way.
manual=(run_xml_bigfile.sh)

is_manual() {
    local s
    for s in "${manual[@]}"; do [ "$1" = "$s" ] && return 0; done
    return 1
}

if ! make >/dev/null; then
    printf 'FAIL: build failed\n'
    exit 1
fi

pass=0; fail=0; skip=0; excluded=0
failed_names=()
skipped_names=()

for path in tests/run_*.sh; do
    name="$(basename "$path")"
    [ "$name" = "run_all.sh" ] && continue
    if [ -n "$filter" ] && [[ "$name" != *"$filter"* ]]; then
        continue
    fi
    if is_manual "$name" && [ "${RUN_MANUAL:-0}" != "1" ]; then
        printf '%-32s MANUAL (excluded; RUN_MANUAL=1 to include)\n' "${name%.sh}"
        excluded=$((excluded + 1))
        continue
    fi

    log="$logdir/$name.log"
    printf '%-32s ' "${name%.sh}"
    if timeout "$timeout_s" bash "$path" >"$log" 2>&1; then
        # A suite whose every reported case SKIPped ran no assertions. Say so.
        if grep -q '^SKIP' "$log" && ! grep -q '^PASS\|^OK\|passed' "$log"; then
            printf 'SKIP  %s\n' "$(grep -m1 '^SKIP' "$log" | cut -c1-70)"
            skip=$((skip + 1))
            skipped_names+=("$name")
        else
            printf 'OK\n'
            pass=$((pass + 1))
        fi
    else
        rc=$?
        if [ "$rc" -eq 124 ]; then
            printf 'TIMEOUT after %ss -> %s\n' "$timeout_s" "$log"
        else
            printf 'FAIL (exit %d) -> %s\n' "$rc" "$log"
        fi
        fail=$((fail + 1))
        failed_names+=("$name")
    fi
done

printf '\n%d passed, %d failed, %d skipped entirely' "$pass" "$fail" "$skip"
[ "$excluded" -gt 0 ] && printf ', %d manual excluded' "$excluded"
printf '\n'

if [ "$skip" -gt 0 ]; then
    printf 'skipped entirely: %s\n' "${skipped_names[*]}"
fi
if [ "$fail" -gt 0 ]; then
    printf 'failed: %s\n' "${failed_names[*]}"
    printf 'logs in %s\n' "$logdir"
    exit 1
fi

rm -rf "$logdir"
exit 0
