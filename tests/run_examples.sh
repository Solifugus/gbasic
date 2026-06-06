#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

make clean
make

examples=(
    arithmetic_number_modifier_test.bas
    array_append_prepend_test.bas
    array_insert_remove_test.bas
    array_reverse_test.bas
    array_sort_test.bas
    array_take_test.bas
    array_unique_test.bas
    type_builtin_test.bas
    string_concat_test.bas
    parse_test.gb
    record_test.gb
    datetime_test.gb
    datetime_lens_test.bas
    dates_lib_test.bas
    keyword_stability_test.bas
    duration_test.gb
    file_test.gb
    find_test.bas
    function_call_comparison_test.bas
    lock_test.gb
    lock_cleanup_test.gb
    lower_upper_modifier_test.bas
    dir_test.gb
    function_test.gb
    goto_test.gb
    gosub_test.gb
    helper_functions_test.bas
    if_else_test.bas
    watch_test.gb
    watch_path_test.bas
    while_test.bas
    while_break_continue_test.bas
    error_test.gb
    modifier_test.gb
    modifier_library_regression_test.bas
    modifier_string_helpers_test.bas
    negated_comparison_test.bas
    money_test.bas
    multiline_string_test.bas
    nothing_test.bas
    number_string_modifier_test.bas
    consider_test.bas
    dynamic_record_access_test.bas
    nested_lvalue_test.bas
    parser_hardening_test.bas
    print_parens_test.bas
    split_find_join_integration_test.bas
    split_join_test.bas
    serialization_test.bas
    quote_test.bas
    string_modifier_pipeline_test.bas
    string_helpers_test.bas
    string_test.bas
    unknown_test.bas
    unknown_nothing_distinction_integration_test.bas
    builtin_test.bas
    builtin_override_test.bas
    program_test.bas
    library_test.bas
    load_test.bas
    load_from_test.bas
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

help_file="$(mktemp)"
if ./gbasic --help >"$help_file"; then
    printf 'PASS %s\n' "gbasic --help"
else
    status=$?
    printf 'FAIL %s\n' "gbasic --help"
    rm -f "$help_file"
    exit "$status"
fi
rm -f "$help_file"

version_file="$(mktemp)"
if ./gbasic --version >"$version_file"; then
    version_text="$(cat "$version_file")"
    if [[ "$version_text" == "gBASIC 0.1.0-dev" ]]; then
        printf 'PASS %s\n' "gbasic --version"
    else
        printf 'FAIL %s\n' "gbasic --version"
        printf 'expected: gBASIC 0.1.0-dev\n'
        printf 'actual: %s\n' "$version_text"
        rm -f "$version_file"
        exit 1
    fi
else
    status=$?
    printf 'FAIL %s\n' "gbasic --version"
    rm -f "$version_file"
    exit "$status"
fi
rm -f "$version_file"

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
if printf 'Ada\n' | ./gbasic examples/input_test.bas >"$stdout_file" 2>"$stderr_file"; then
    actual_text="$(cat "$stdout_file")"
    expected_text="$(cat examples/input_test.out)"
    if [[ "$actual_text" == "$expected_text" ]]; then
        printf 'PASS %s\n' "examples/input_test.bas"
    else
        printf 'FAIL %s\n' "examples/input_test.bas"
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
    printf 'FAIL %s\n' "examples/input_test.bas"
    if [[ -s "$stderr_file" ]]; then
        cat "$stderr_file"
    fi
    rm -f "$stdout_file" "$stderr_file"
    exit "$status"
fi
rm -f "$stdout_file" "$stderr_file"

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
if printf '  Joe Jones  \n' | ./gbasic examples/input_trimmed_integration_test.bas >"$stdout_file" 2>"$stderr_file"; then
    actual_text="$(cat "$stdout_file")"
    expected_text="$(cat examples/input_trimmed_integration_test.out)"
    if [[ "$actual_text" == "$expected_text" ]]; then
        printf 'PASS %s\n' "examples/input_trimmed_integration_test.bas"
    else
        printf 'FAIL %s\n' "examples/input_trimmed_integration_test.bas"
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
    printf 'FAIL %s\n' "examples/input_trimmed_integration_test.bas"
    if [[ -s "$stderr_file" ]]; then
        cat "$stderr_file"
    fi
    rm -f "$stdout_file" "$stderr_file"
    exit "$status"
fi
rm -f "$stdout_file" "$stderr_file"

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
if printf '41\n' | ./gbasic examples/input_number_integration_test.bas >"$stdout_file" 2>"$stderr_file"; then
    actual_text="$(cat "$stdout_file")"
    expected_text="$(cat examples/input_number_integration_test.out)"
    if [[ "$actual_text" == "$expected_text" ]]; then
        printf 'PASS %s\n' "examples/input_number_integration_test.bas"
    else
        printf 'FAIL %s\n' "examples/input_number_integration_test.bas"
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
    printf 'FAIL %s\n' "examples/input_number_integration_test.bas"
    if [[ -s "$stderr_file" ]]; then
        cat "$stderr_file"
    fi
    rm -f "$stdout_file" "$stderr_file"
    exit "$status"
fi
rm -f "$stdout_file" "$stderr_file"

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
if printf 'Ada\n41\n' | ./gbasic examples/input_birth_year_concat_test.bas >"$stdout_file" 2>"$stderr_file"; then
    actual_text="$(cat "$stdout_file")"
    expected_text="$(cat examples/input_birth_year_concat_test.out)"
    if [[ "$actual_text" == "$expected_text" ]]; then
        printf 'PASS %s\n' "examples/input_birth_year_concat_test.bas"
    else
        printf 'FAIL %s\n' "examples/input_birth_year_concat_test.bas"
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
    printf 'FAIL %s\n' "examples/input_birth_year_concat_test.bas"
    if [[ -s "$stderr_file" ]]; then
        cat "$stderr_file"
    fi
    rm -f "$stdout_file" "$stderr_file"
    exit "$status"
fi
rm -f "$stdout_file" "$stderr_file"

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
if printf 'help\nnorth\nnorth\nlook\nsouth\nsouth\ntake lamp\ngo north\nnorth\nlook\neast\nlook\ntake note\nread note\nwest\nsouth\neast\ntake brass key\nwest\nwest\nnorth\nlook\ninventory\ndrop note\ninventory\nquit\n' | ./gbasic examples/adventure/adventure.bas >"$stdout_file" 2>"$stderr_file"; then
    actual_text="$(cat "$stdout_file")"
    expected_text="$(cat examples/adventure/adventure.out)"
    if [[ "$actual_text" == "$expected_text" ]]; then
        printf 'PASS %s\n' "examples/adventure/adventure.bas"
    else
        printf 'FAIL %s\n' "examples/adventure/adventure.bas"
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
    printf 'FAIL %s\n' "examples/adventure/adventure.bas"
    if [[ -s "$stderr_file" ]]; then
        cat "$stderr_file"
    fi
    rm -f "$stdout_file" "$stderr_file"
    exit "$status"
fi
rm -f "$stdout_file" "$stderr_file"

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
if ./gbasic --add-loads examples/add_uses_test.bas >"$stdout_file" 2>"$stderr_file"; then
    actual_text="$(cat "$stdout_file")"
    expected_text="$(cat examples/add_loads_test.out)"
    if [[ "$actual_text" == "$expected_text" ]]; then
        printf 'PASS %s\n' "examples/add_uses_test.bas --add-loads"
    else
        printf 'FAIL %s\n' "examples/add_uses_test.bas --add-loads"
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
    printf 'FAIL %s\n' "examples/add_uses_test.bas --add-loads"
    if [[ -s "$stderr_file" ]]; then
        cat "$stderr_file"
    fi
    rm -f "$stdout_file" "$stderr_file"
    exit "$status"
fi
rm -f "$stdout_file" "$stderr_file"

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
if ./gbasic --add-uses examples/add_uses_test.bas >"$stdout_file" 2>"$stderr_file"; then
    actual_text="$(cat "$stdout_file")"
    expected_text="$(cat examples/add_uses_insert_test.out)"
    if [[ "$actual_text" == "$expected_text" ]]; then
        printf 'PASS %s\n' "examples/add_uses_test.bas --add-uses"
    else
        printf 'FAIL %s\n' "examples/add_uses_test.bas --add-uses"
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
    printf 'FAIL %s\n' "examples/add_uses_test.bas --add-uses"
    if [[ -s "$stderr_file" ]]; then
        cat "$stderr_file"
    fi
    rm -f "$stdout_file" "$stderr_file"
    exit "$status"
fi
rm -f "$stdout_file" "$stderr_file"

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
