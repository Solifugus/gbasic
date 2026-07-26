#!/usr/bin/env bash
# gBASIC Studio — STU-0 backbone suite (docs/gbasic_studio_plan.md, STU-0).
#
# Exercises the persistent BACKBONE only (project/session/settings model +
# versioned atomic persistence + startup/shutdown lifecycle). It is entirely
# headless and GI-independent: the studio_* stdlib libraries are pure gBASIC over
# the filesystem/JSON builtins, so this suite must run and be verified even on
# hosts without a display or GTK typelibs. The GTK shell (display-only) is covered
# by run_gui_parse.sh (parse) and a manual checklist in examples/studio/README.md.
#
# Determinism: the app's summary output is PATH-FREE (ids are counter-minted, no
# timestamps, temp home never printed), so stdout is byte-stable against goldens
# regardless of the throwaway home directory each case runs in.
set -euo pipefail

cd "$(dirname "$0")/.."

make >/dev/null

export GBASIC_PATH=stdlib
APP=examples/studio/studio.bas

tmproot="$(mktemp -d)"
stdout_file="$(mktemp)"
trap 'rm -rf "$tmproot" "$stdout_file"' EXIT

fail() { printf 'FAIL %s\n' "$1"; exit 1; }

run_golden() { # name  mode  home  golden
    local name="$1" mode="$2" home="$3" golden="$4"
    : >"$stdout_file"
    if ! timeout 60 ./gbasic "$APP" "$mode" "$home" >"$stdout_file" 2>&1; then
        cat "$stdout_file"; fail "$name (nonzero exit)"
    fi
    if diff -u "$golden" "$stdout_file"; then
        printf 'PASS %s\n' "$name"
    else
        fail "$name (output diff)"
    fi
}

# 1. Empty startup — no prior session; defaults constructed.
run_golden "empty_startup" startup "$tmproot/empty" tests/studio/empty_startup.out

# 2. Save / restore — build+persist in one launch, restore in a SECOND launch on
#    the same home; restored state must equal the golden (window state included).
home_sr="$tmproot/sr"
timeout 60 ./gbasic "$APP" build "$home_sr" >"$stdout_file" 2>&1 || { cat "$stdout_file"; fail "save_restore (build)"; }
grep -q '^saved=settings,session,workspace:ws-1$' "$stdout_file" || { cat "$stdout_file"; fail "save_restore (saved line)"; }
run_golden "save_restore" startup "$home_sr" tests/studio/save_restore.out

# 3. Corrupt session — invalid JSON in session.json; startup recovers, no crash.
home_cor="$tmproot/cor"
timeout 60 ./gbasic "$APP" build "$home_cor" >/dev/null 2>&1 || fail "corrupt_session (setup)"
printf '%s' '{ not valid json ]' > "$home_cor/session.json"
run_golden "corrupt_session" startup "$home_cor" tests/studio/corrupt_session.out

# 4. Version mismatch — a future schema_version is rejected cleanly.
home_fut="$tmproot/fut"
timeout 60 ./gbasic "$APP" build "$home_fut" >/dev/null 2>&1 || fail "future_version (setup)"
printf '%s' '{"schema_version":999,"active_workspace":"ws-1","next_ws":2,"window":{"width":1,"height":1,"maximized":false},"recent_files":[]}' > "$home_fut/session.json"
run_golden "future_version" startup "$home_fut" tests/studio/future_version.out

# 5. Atomic persistence — 30 save/reload cycles; every reload must load cleanly
#    (a truncated store from a non-atomic write would surface as a failed reload).
: >"$stdout_file"
timeout 60 ./gbasic "$APP" stress "$tmproot/stress" >"$stdout_file" 2>&1 || { cat "$stdout_file"; fail "stress (exit)"; }
if grep -q '^stress_ok=true$' "$stdout_file"; then
    printf 'PASS stress\n'
else
    cat "$stdout_file"; fail "stress (not ok)"
fi

# 6. Memory — 50 startup/shutdown cycles under valgrind: 0 errors, 0 leaks.
#    (Studio is pure gBASIC — no new C — so this exercises the interpreter running
#    the backbone; it must be clean end-to-end.) Skips cleanly if valgrind absent.
if command -v valgrind >/dev/null 2>&1; then
    vg_log="$(mktemp)"
    : >"$stdout_file"
    if timeout 300 valgrind --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=definite \
            ./gbasic "$APP" cycles "$tmproot/cycles" >"$stdout_file" 2>"$vg_log"; then
        if grep -q '^cycles_done=50$' "$stdout_file"; then
            printf 'PASS memory_cycles (valgrind clean)\n'
        else
            cat "$stdout_file"; rm -f "$vg_log"; fail "memory_cycles (bad output)"
        fi
    else
        status=$?
        printf 'FAIL memory_cycles (valgrind exit %d)\n' "$status"
        grep -E 'definitely lost|ERROR SUMMARY|Invalid ' "$vg_log" || tail -20 "$vg_log"
        rm -f "$vg_log"
        exit 1
    fi
    rm -f "$vg_log"
else
    printf 'SKIP memory_cycles (valgrind not installed)\n'
fi

printf 'run_studio: all cases passed\n'
