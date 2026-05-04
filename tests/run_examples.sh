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
    if ./gbasic "$path" >/dev/null; then
        printf 'PASS %s\n' "$path"
    else
        status=$?
        printf 'FAIL %s\n' "$path"
        exit "$status"
    fi
done
