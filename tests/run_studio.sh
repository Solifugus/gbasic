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

mkproj() { # dir — a deterministic project tree for the browser cases
    local d="$1"
    mkdir -p "$d/src" "$d/docs"
    printf x > "$d/main.bas"; printf x > "$d/README.md"
    printf x > "$d/src/a.bas"; printf x > "$d/src/b.bas"; printf x > "$d/docs/guide.md"
}

mkproj2() { # dir — deterministic source files for the document cases
    local d="$1"
    mkdir -p "$d/sub"
    printf 'aaa\n' > "$d/a.bas"; printf 'bbb\n' > "$d/b.bas"; printf 'ccc\n' > "$d/c.bas"
}

run_golden3() { # name mode home arg2 golden
    local name="$1" mode="$2" home="$3" arg2="$4" golden="$5"
    : >"$stdout_file"
    if ! timeout 60 ./gbasic "$APP" "$mode" "$home" "$arg2" >"$stdout_file" 2>&1; then
        cat "$stdout_file"; fail "$name (nonzero exit)"
    fi
    if diff -u "$golden" "$stdout_file"; then printf 'PASS %s\n' "$name"; else fail "$name (output diff)"; fi
}

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

# ==========================================================================
# STU-1 — workspace navigation, project browser, registry.
# ==========================================================================

# 7. Workspace lifecycle + multiple projects + navigation persistence:
#    build a workspace (2 projects incl. one missing dir, an expanded folder, a
#    selection) over a real tree, persist, and restore it in a SECOND launch.
#    Restored nav summary + browser tree must equal the golden (path-free).
proj_sr="$tmproot/proj_sr"; mkproj "$proj_sr"
home_s1="$tmproot/s1"
: >"$stdout_file"
timeout 60 ./gbasic "$APP" stu1_build "$home_s1" "$proj_sr" >"$stdout_file" 2>&1 || { cat "$stdout_file"; fail "stu1_build (exit)"; }
grep -q '^saved=settings,session,workspace:ws-1,registry$' "$stdout_file" || { cat "$stdout_file"; fail "stu1_build (saved line)"; }
run_golden "stu1_restore" stu1_restore "$home_s1" tests/studio/stu1_restore.out

# 8. Missing project — scanning a non-existent project directory yields no rows
#    and does not crash.
: >"$stdout_file"
timeout 60 ./gbasic "$APP" stu1_missing "$proj_sr" >"$stdout_file" 2>&1 || { cat "$stdout_file"; fail "stu1_missing (exit)"; }
if grep -q '^rows=0$' "$stdout_file"; then printf 'PASS stu1_missing\n'; else cat "$stdout_file"; fail "stu1_missing (rows)"; fi

# 9. Browser correctness — deterministic folders-first, sorted, lazy-expanded tree.
run_golden "stu1_browse" stu1_browse "$proj_sr" tests/studio/stu1_browse.out

# 10. Tree refresh — a new file on disk appears after a re-scan.
proj_rf="$tmproot/proj_rf"; mkproj "$proj_rf"
before="$(mktemp)"; after="$(mktemp)"
timeout 60 ./gbasic "$APP" stu1_browse "$proj_rf" >"$before" 2>&1 || { cat "$before"; fail "stu1_refresh (before)"; }
printf x > "$proj_rf/newfile.bas"
timeout 60 ./gbasic "$APP" stu1_browse "$proj_rf" >"$after" 2>&1 || { cat "$after"; fail "stu1_refresh (after)"; }
if ! grep -q 'newfile.bas' "$before" && grep -q 'newfile.bas' "$after" && [ "$(wc -l <"$after")" -gt "$(wc -l <"$before")" ]; then
    printf 'PASS stu1_refresh\n'
else
    echo "before:"; cat "$before"; echo "after:"; cat "$after"; rm -f "$before" "$after"; fail "stu1_refresh"
fi
rm -f "$before" "$after"

# 11. Workspace registry + recent — multiple workspaces are remembered and ordered.
run_golden "stu1_registry" stu1_registry "$tmproot/reg" tests/studio/stu1_registry.out

# 12. Memory — 50 STU-1 launch/persist cycles (registry included) under valgrind.
if command -v valgrind >/dev/null 2>&1; then
    vg_log="$(mktemp)"; : >"$stdout_file"
    if timeout 300 valgrind --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=definite \
            ./gbasic "$APP" stu1_cycles "$tmproot/s1cycles" >"$stdout_file" 2>"$vg_log"; then
        if grep -q '^cycles_done=50$' "$stdout_file"; then
            printf 'PASS stu1_memory_cycles (valgrind clean)\n'
        else
            cat "$stdout_file"; rm -f "$vg_log"; fail "stu1_memory_cycles (bad output)"
        fi
    else
        status=$?
        printf 'FAIL stu1_memory_cycles (valgrind exit %d)\n' "$status"
        grep -E 'definitely lost|ERROR SUMMARY|Invalid ' "$vg_log" || tail -20 "$vg_log"
        rm -f "$vg_log"; exit 1
    fi
    rm -f "$vg_log"
else
    printf 'SKIP stu1_memory_cycles (valgrind not installed)\n'
fi

# ==========================================================================
# STU-2 — documents & editor lifecycle.
# ==========================================================================

# 13. Document lifecycle: open, reuse (dup), directory guard, missing file, edit ->
#     dirty, revert -> clean, save. Plus a disk check that save wrote the file.
proj_life="$tmproot/life"; mkproj2 "$proj_life"
run_golden3 "stu2_lifecycle" stu2_lifecycle "$tmproot/lifehome" "$proj_life" tests/studio/stu2_lifecycle.out
if grep -qx 'saved by studio' "$proj_life/a.bas"; then printf 'PASS stu2_save_disk\n'; else echo "a.bas:"; cat "$proj_life/a.bas"; fail "stu2_save_disk (content)"; fi

# 14. Save failure — writing into a missing directory leaves the document dirty and
#     preserves the buffer (no crash, not marked clean).
: >"$stdout_file"
timeout 60 ./gbasic "$APP" stu2_savefail "$tmproot/failhome" >"$stdout_file" 2>&1 || { cat "$stdout_file"; fail "stu2_savefail (exit)"; }
if grep -qx 'savefail=error dirty=true' "$stdout_file"; then printf 'PASS stu2_savefail\n'; else cat "$stdout_file"; fail "stu2_savefail"; fi

# 15. Close a dirty document three ways (fresh dir each so prior saves can't bleed):
#     save -> closed, discard -> closed, cancel -> kept open.
for spec in "save:closed:0" "discard:closed:0" "cancel:cancelled:1"; do
    dec="${spec%%:*}"; rest="${spec#*:}"; want_status="${rest%%:*}"; want_open="${rest##*:}"
    cd_dir="$tmproot/close_$dec"; mkproj2 "$cd_dir"
    : >"$stdout_file"
    timeout 60 ./gbasic "$APP" stu2_close "$tmproot/closehome_$dec" "$cd_dir" "$dec" >"$stdout_file" 2>&1 || { cat "$stdout_file"; fail "stu2_close_$dec (exit)"; }
    if grep -qx "close($dec)=$want_status open=$want_open" "$stdout_file"; then
        printf 'PASS stu2_close_%s\n' "$dec"
    else
        cat "$stdout_file"; fail "stu2_close_$dec"
    fi
done

# 16. External changes — clean file changed on disk auto-reloads; a dirty file
#     changed on disk becomes a preserved conflict; a deleted file becomes missing.
proj_ext="$tmproot/ext"; mkproj2 "$proj_ext"
run_golden3 "stu2_external" stu2_external "$tmproot/exthome" "$proj_ext" tests/studio/stu2_external.out

# 17. Restore — open two documents, set cursors + active, persist, relaunch: the open
#     set, tab order, active document, and cursor positions are restored.
proj_res="$tmproot/res"; mkproj2 "$proj_res"
run_golden3 "stu2_restore" stu2_restore "$tmproot/reshome" "$proj_res" tests/studio/stu2_restore.out

# 18. Missing restored file — persist an open file, delete it, relaunch: the document
#     restores in a missing state, not a crash.
proj_mr="$tmproot/mr"; mkproj2 "$proj_mr"; home_mr="$tmproot/mrhome"
timeout 60 ./gbasic "$APP" stu2_open_persist "$home_mr" "$proj_mr" >/dev/null 2>&1 || fail "stu2_missing (setup)"
rm -f "$proj_mr/a.bas"
: >"$stdout_file"
timeout 60 ./gbasic "$APP" stu2_missing_restore "$home_mr" "$proj_mr" >"$stdout_file" 2>&1 || { cat "$stdout_file"; fail "stu2_missing (exit)"; }
if grep -q 'a.bas clean missing' "$stdout_file"; then printf 'PASS stu2_missing_restore\n'; else cat "$stdout_file"; fail "stu2_missing_restore"; fi

# 19. Browser integration — opening a file the browser points at activates its tab.
proj_br="$tmproot/br"; mkproj2 "$proj_br"
: >"$stdout_file"
timeout 60 ./gbasic "$APP" stu2_browser "$tmproot/brhome" "$proj_br" >"$stdout_file" 2>&1 || { cat "$stdout_file"; fail "stu2_browser (exit)"; }
if grep -qx 'browser_opened=a.bas active=doc-1' "$stdout_file"; then printf 'PASS stu2_browser\n'; else cat "$stdout_file"; fail "stu2_browser"; fi

# 20. Memory + callbacks — 40 open/edit/save/close/persist cycles under valgrind.
if command -v valgrind >/dev/null 2>&1; then
    proj_cy="$tmproot/cy"; mkproj2 "$proj_cy"
    vg_log="$(mktemp)"; : >"$stdout_file"
    if timeout 300 valgrind --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=definite \
            ./gbasic "$APP" stu2_cycles "$tmproot/cyhome" "$proj_cy" >"$stdout_file" 2>"$vg_log"; then
        if grep -qx 'cycles_done=40' "$stdout_file"; then
            printf 'PASS stu2_memory_cycles (valgrind clean)\n'
        else
            cat "$stdout_file"; rm -f "$vg_log"; fail "stu2_memory_cycles (bad output)"
        fi
    else
        status=$?
        printf 'FAIL stu2_memory_cycles (valgrind exit %d)\n' "$status"
        grep -E 'definitely lost|ERROR SUMMARY|Invalid ' "$vg_log" || tail -20 "$vg_log"
        rm -f "$vg_log"; exit 1
    fi
    rm -f "$vg_log"
else
    printf 'SKIP stu2_memory_cycles (valgrind not installed)\n'
fi

printf 'run_studio: all cases passed\n'
