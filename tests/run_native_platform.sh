#!/usr/bin/env bash
# Native Application Platform suite (docs/gbasic_native_app_platform_plan.md).
# Exercises the generalized GI/runtime capabilities that let gBASIC build
# sophisticated native GTK 4 applications. Headless: every case uses
# display-free GObject/Gio/GLib types (or a require-only precondition), so it
# never needs an X/Wayland display.
#
# Skips cleanly when the platform's runtime prerequisites are absent:
#   1. libgirepository-2.0 development files (HAVE_GIR=0)  -> like run_gi.sh
#   2. the GTK 4 / GtkSource 5 introspection typelibs      -> detected at runtime
# GtkSourceView and GTK 4 are driven THROUGH `gi`, not linked, so their only
# footprint is these typelibs; the interpreter builds and runs without them.
#
# Future NAP phases extend the positive_cases / negative_cases arrays below.
set -euo pipefail

cd "$(dirname "$0")/.."

# Make any GLib critical (e.g. a G_IS_OBJECT assertion from a lifetime bug) abort
# the interpreter so it fails the suite loudly rather than printing and passing.
export G_DEBUG="${G_DEBUG:+$G_DEBUG,}fatal-criticals"

if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists girepository-2.0; then
    printf 'SKIP tests/native_platform (libgirepository-2.0 development files not available)\n'
    exit 0
fi

make >/dev/null

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
trap 'rm -f "$stdout_file" "$stderr_file"' EXIT

# --- Dependency gate -------------------------------------------------------
# Probe the required typelibs by actually resolving them through gi.require.
# A "could not load namespace" failure means the GTK4/GtkSource typelibs are
# not installed -> SKIP. Any other failure is a real regression -> FAIL.
if ! ./gbasic tests/native_platform/require_typelibs.bas >"$stdout_file" 2>"$stderr_file"; then
    if grep -q 'gi.require: could not load namespace' "$stderr_file"; then
        printf 'SKIP tests/native_platform (GTK 4 / GtkSource 5 typelibs not available)\n'
        exit 0
    fi
    printf 'FAIL tests/native_platform/require_typelibs.bas (unexpected probe error)\n'
    cat "$stderr_file"
    exit 1
fi

# --- Positive cases (byte-exact stdout vs sibling .out) --------------------
positive_cases=(
    require_typelibs
    boxed_struct
    out_scalar
    out_multi
    out_struct
    loop_timeout
    loop_idle
    loop_source_remove
    loop_mailbox
    loop_responsive
    variant_scalars
    variant_strv
    variant_parse
    array_out
    array_in
)

for name in "${positive_cases[@]}"; do
    source="tests/native_platform/$name.bas"
    expected="tests/native_platform/$name.out"
    : >"$stdout_file"
    : >"$stderr_file"

    # timeout guards the WI-4 main-loop cases: they quit themselves, but a future
    # regression that never quits must fail the suite rather than hang it forever.
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
    negative_boxed_unknown_type
    negative_boxed_not_struct
    negative_boxed_unknown_field
    negative_boxed_bad_value
    negative_boxed_unsupported_field
    negative_boxed_serialize
    negative_out_unsupported
    negative_out_gerror
    negative_out_arity
    negative_inout_unsupported
    negative_loop_badfn
    negative_loop_source_unknown
    negative_variant_strv_badelem
    negative_array_heterogeneous
    negative_array_not_array
    negative_variant_parse_bad
    negative_variant_get_unsupported
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
