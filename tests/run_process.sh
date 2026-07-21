#!/usr/bin/env bash
# General process API suite (docs/gbasic_native_app_platform_plan.md, NAP-6).
# Exercises `process.run`, a shell-injection-safe synchronous process runner.
#
# Unlike run_native_platform.sh / run_gi.sh, this suite has NO GObject-
# Introspection dependency: `process.run` is an unconditional built-in (fork +
# execvp + pipes, no GI), so it must run and be verified even on hosts without
# libgirepository or the GTK4/GtkSource typelibs. Keeping these cases here rather
# than in run_native_platform.sh is what prevents them from being falsely skipped
# behind that runner's GI gate.
#
# Fixtures that spawn helper scripts reference them by repo-root-relative path
# (e.g. tests/native_platform/helpers/sleep_long.sh), so the child's working
# directory must be the repo root — hence the cd below and running ./gbasic from
# there, exactly as run_native_platform.sh does.
set -euo pipefail

cd "$(dirname "$0")/.."

make >/dev/null

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
trap 'rm -f "$stdout_file" "$stderr_file"' EXIT

# --- Positive cases (byte-exact stdout vs sibling .out) --------------------
positive_cases=(
    nap6_basic
    nap6_streams
    nap6_argv
    nap6_bigstreams
    nap6_binary
    nap6_cwd
    nap6_timeout
    nap6_churn
)

for name in "${positive_cases[@]}"; do
    source="tests/native_platform/$name.bas"
    expected="tests/native_platform/$name.out"
    : >"$stdout_file"
    : >"$stderr_file"

    # timeout guards a future regression where a child (or the timeout path
    # itself) hangs: the suite must fail loudly rather than hang forever.
    if timeout 60 ./gbasic "$source" >"$stdout_file" 2>"$stderr_file"; then
        if diff -u "$expected" "$stdout_file"; then
            printf 'PASS %s\n' "$source"
        else
            printf 'FAIL %s\n' "$source"
            exit 1
        fi
    else
        status=$?
        printf 'FAIL %s (exit %d)\n' "$source" "$status"
        cat "$stderr_file"
        exit 1
    fi
done

# --- Negative cases (byte-exact stderr vs sibling .err, nonzero exit) ------
negative_cases=(
    negative_nap6_missing
    negative_nap6_not_record
    negative_nap6_no_command
    negative_nap6_command_type
    negative_nap6_arg_type
)

for name in "${negative_cases[@]}"; do
    source="tests/native_platform/$name.bas"
    expected="tests/native_platform/$name.err"
    : >"$stdout_file"
    : >"$stderr_file"

    if timeout 60 ./gbasic "$source" >"$stdout_file" 2>"$stderr_file"; then
        printf 'FAIL %s\n' "$source"
        printf 'expected nonzero exit\n'
        exit 1
    fi

    actual_text="$(cat "$stderr_file")"
    expected_text="$(cat "$expected")"
    if [[ "$actual_text" == "$expected_text" ]]; then
        printf 'PASS %s\n' "$source"
    else
        printf 'FAIL %s\n' "$source"
        actual_norm="$(mktemp)"
        expected_norm="$(mktemp)"
        printf '%s\n' "$actual_text" >"$actual_norm"
        printf '%s\n' "$expected_text" >"$expected_norm"
        diff -u "$expected_norm" "$actual_norm" || true
        rm -f "$actual_norm" "$expected_norm"
        exit 1
    fi

    if [[ -s "$stdout_file" ]]; then
        printf 'FAIL %s\n' "$source"
        printf 'expected empty stdout\n'
        cat "$stdout_file"
        exit 1
    fi
done
