#!/usr/bin/env bash
# GObject-Introspection bridge (gi.*) suite. Exercises headless Gio/GObject types
# so it never needs a display. Skips cleanly when libgirepository-2.0 is absent
# (HAVE_GIR=0), mirroring run_sqlite.sh.
set -euo pipefail

cd "$(dirname "$0")/.."

# Make any GLib critical (e.g. a G_IS_OBJECT assertion from a lifetime bug) abort
# the interpreter so it fails the suite loudly rather than printing and passing.
export G_DEBUG="${G_DEBUG:+$G_DEBUG,}fatal-criticals"

if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists girepository-2.0; then
    printf 'SKIP tests/gi (libgirepository-2.0 development files not available)\n'
    exit 0
fi

make >/dev/null

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
trap 'rm -f "$stdout_file" "$stderr_file"' EXIT

positive_cases=(
    gi_signal_test
    gi_property_test
    reflect_foreign_test
    chained_gobject_method_test
    gi_enum_test
    gi_transfer_test
    gi_inherited_method_test
    gi_handler_error_test
    gi_construct_props_test
    gi_construct_object_prop_test
    gi_invoke_test
    gi_handler_survives_scope_test
)

for name in "${positive_cases[@]}"; do
    source="tests/gi/$name.bas"
    expected="tests/gi/$name.out"
    : >"$stdout_file"
    : >"$stderr_file"

    if ./gbasic "$source" >"$stdout_file" 2>"$stderr_file"; then
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

negative_cases=(
    negative_gi_not_loaded
    negative_gi_require_unknown
    negative_gi_unknown_type
    negative_gi_unknown_property
    negative_gi_unknown_method
    negative_gi_new_arity
    negative_gi_new_unknown_prop
    negative_gi_new_unpaired
    negative_gi_invoke_unknown
    negative_gi_invoke_not_function
)

for name in "${negative_cases[@]}"; do
    source="tests/gi/$name.bas"
    expected="tests/gi/$name.err"
    : >"$stdout_file"
    : >"$stderr_file"

    if ./gbasic "$source" >"$stdout_file" 2>"$stderr_file"; then
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
