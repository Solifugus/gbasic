#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

cases=(
    negative_append_type
    negative_insert_type
    negative_insert_bounds
    negative_remove_bounds
    negative_insert_fractional
    negative_take_first_type
    negative_take_first_empty
    negative_take_last_empty
    negative_reverse_type
    negative_unique_type
    negative_unique_nested_array
    negative_sort_type
    negative_sort_mixed
    negative_sort_nested_array
    negative_invalid_escape
    negative_unterminated_string
    negative_unknown_order
    negative_find_type
    negative_function_assignment
    negative_len_assignment
    negative_foo_assignment
    negative_getname_assignment
    negative_function_result_modifier
    negative_while_missing_end
    negative_break_outside_loop
    negative_continue_outside_loop
    negative_left_type
    negative_mid_arity
    negative_trim_type
    negative_split_empty_separator
    negative_join_type
    negative_join_element_type
    negative_modifier_split_empty
    negative_modifier_join_element_type
    negative_lowered_modifier_type
    negative_uppered_modifier_type
    negative_number_modifier_invalid
)

for name in "${cases[@]}"; do
    source="tests/$name.bas"
    expected="tests/$name.err"
    stdout_file="$(mktemp)"
    stderr_file="$(mktemp)"

    if ./gbasic "$source" >"$stdout_file" 2>"$stderr_file"; then
        printf 'FAIL %s\n' "$source"
        printf 'expected nonzero exit\n'
        rm -f "$stdout_file" "$stderr_file"
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
        rm -f "$actual_norm" "$expected_norm" "$stdout_file" "$stderr_file"
        exit 1
    fi

    if [[ -s "$stdout_file" ]]; then
        printf 'FAIL %s\n' "$source"
        printf 'expected empty stdout\n'
        cat "$stdout_file"
        rm -f "$stdout_file" "$stderr_file"
        exit 1
    fi

    rm -f "$stdout_file" "$stderr_file"
done
