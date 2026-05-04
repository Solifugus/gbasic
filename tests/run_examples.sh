#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

make clean
make

examples=(
    parse_test.gb
    record_test.gb
    datetime_test.gb
    duration_test.gb
    file_test.gb
    lock_test.gb
    lock_cleanup_test.gb
    dir_test.gb
    function_test.gb
    goto_test.gb
    gosub_test.gb
    watch_test.gb
    error_test.gb
    modifier_test.gb
    builtin_test.bas
    builtin_override_test.bas
    program_test.bas
    library_test.bas
    use_test.bas
    use_from_test.bas
    use_search_test.bas
    qualified_modifier_test.bas
    qualified_function_test.bas
)

for example in "${examples[@]}"; do
    path="examples/$example"
    expected="${path%.*}.out"
    stdout_file="$(mktemp)"
    stderr_file="$(mktemp)"

    if ./gbasic "$path" >"$stdout_file" 2>"$stderr_file"; then
        if [[ -f "$expected" ]]; then
            actual_text="$(cat "$stdout_file")"
            expected_text="$(cat "$expected")"
            if [[ "$actual_text" == "$expected_text" ]]; then
                printf 'PASS %s\n' "$path"
            else
                printf 'FAIL %s\n' "$path"
                printf 'stdout mismatch against %s\n' "$expected"
                actual_norm="$(mktemp)"
                expected_norm="$(mktemp)"
                printf '%s\n' "$actual_text" >"$actual_norm"
                printf '%s\n' "$expected_text" >"$expected_norm"
                diff -u "$expected_norm" "$actual_norm" || true
                rm -f "$actual_norm" "$expected_norm" "$stdout_file" "$stderr_file"
                exit 1
            fi
        else
            printf 'PASS %s\n' "$path"
        fi
        rm -f "$stdout_file" "$stderr_file"
    else
        status=$?
        printf 'FAIL %s\n' "$path"
        if [[ -s "$stderr_file" ]]; then
            cat "$stderr_file"
        fi
        rm -f "$stdout_file" "$stderr_file"
        exit "$status"
    fi
done

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
if ./gbasic --add-uses examples/add_uses_builtins_test.bas >"$stdout_file" 2>"$stderr_file"; then
    actual_text="$(cat "$stdout_file")"
    expected_text="$(cat examples/add_uses_builtins_test.out)"
    if [[ "$actual_text" == "$expected_text" ]]; then
        printf 'PASS %s\n' "examples/add_uses_builtins_test.bas --add-uses"
    else
        printf 'FAIL %s\n' "examples/add_uses_builtins_test.bas --add-uses"
        actual_norm="$(mktemp)"
        expected_norm="$(mktemp)"
        printf '%s\n' "$actual_text" >"$actual_norm"
        printf '%s\n' "$expected_text" >"$expected_norm"
        diff -u "$expected_norm" "$actual_norm" || true
        rm -f "$actual_norm" "$expected_norm" "$stdout_file" "$stderr_file"
        exit 1
    fi
else
    status=$?
    printf 'FAIL %s\n' "examples/add_uses_builtins_test.bas --add-uses"
    if [[ -s "$stderr_file" ]]; then
        cat "$stderr_file"
    fi
    rm -f "$stdout_file" "$stderr_file"
    exit "$status"
fi
rm -f "$stdout_file" "$stderr_file"
