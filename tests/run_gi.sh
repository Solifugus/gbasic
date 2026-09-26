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
    gi_static_test
    gi_handler_survives_scope_test
    gi_string_nul_test
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

# --- warning 2107 through a HANDLE, which is this suite's to assert ---------
#
# Reported from the gBASIC Studio bench (2026-09-25): a `for each` over widget
# rows writing `e.entry.text = "..."` printed "the write is discarded" -- and
# the entry really was filled in, so the diagnostic's own sentence was false.
# The element is a copy, but a handle inside it refers to the same object
# either way.
#
# IT IS ASSERTED HERE BECAUSE IT CANNOT BE ASSERTED THERE. run_for_each_index
# pins the same two-hop shape over a nested RECORD, where the write really IS
# discarded and the silence is a COST; only a reference-kinded value shows the
# silence is CORRECT, and every reference kind sits behind an optional
# dependency. So the cost lives with the rule and the reason lives with `gi`.
#
# BOTH HALVES, or either alone proves nothing: the ids must CHANGE (the write
# reached the objects) and stderr must be EMPTY (nothing warned). A fixture in
# positive_cases above would only check the first, since that loop does not
# look at stderr.
: >"$stdout_file"
: >"$stderr_file"
if ./gbasic tests/gi/gi_loop_handle_write_test.bas >"$stdout_file" 2>"$stderr_file" \
   && diff -u tests/gi/gi_loop_handle_write_test.out "$stdout_file" >/dev/null; then
    if [[ -s "$stderr_file" ]]; then
        printf 'FAIL tests/gi/gi_loop_handle_write_test.bas (wrote to stderr)\n'
        cat "$stderr_file"
        exit 1
    fi
    printf 'PASS tests/gi/gi_loop_handle_write_test.bas (the write landed, nothing warned)\n'
else
    printf 'FAIL tests/gi/gi_loop_handle_write_test.bas\n'
    diff -u tests/gi/gi_loop_handle_write_test.out "$stdout_file" || true
    cat "$stderr_file"
    exit 1
fi

# --- a Gtk widget before gtk_init: A SENTENCE, NOT A CORE DUMP -------------
#
# Reported from the gBASIC Studio bench (2026-09-25): `gi.new("Gtk.DropDown")`
# after a bare `gi.require("Gtk", "4.0")` exited 139, core dumped. A crash is
# the worst diagnostic available -- it takes buffered stdout with it, so a
# program with `print` tracing produced NO OUTPUT AT ALL and the failure read as
# happening at line 1, and under a pipe the exit status is the pipe's, so it
# read as a clean exit 0.
#
# THE PREDICATE WAS MEASURED, thirteen types one process each: six non-widget
# Gtk types construct happily and all seven widgets crashed, so the test is
# ancestry from GtkWidget and NOT "the namespace is Gtk" -- which is what the
# CONTROL here pins, or the refusal would have quietly cost six working kinds
# of object.
#
# Gated on the Gtk 4 typelib by the same probe run_native_platform.sh uses.
: >"$stderr_file"
gtk_typelib=1
if ! ./gbasic tests/gi/gtk_probe.bas >/dev/null 2>"$stderr_file"; then
    if grep -q "could not load namespace" "$stderr_file"; then
        printf 'SKIP tests/gi widget-initialization tier (Gtk 4 typelib not available)\n'
        gtk_typelib=0
    else
        printf 'FAIL tests/gi/gtk_probe.bas (unexpected probe error)\n'
        cat "$stderr_file"
        exit 1
    fi
fi

if [[ "$gtk_typelib" == 1 ]]; then
    : >"$stdout_file"; : >"$stderr_file"
    if ./gbasic tests/gi/negative_gi_widget_before_init.bas \
            >"$stdout_file" 2>"$stderr_file"; then
        printf 'FAIL tests/gi/negative_gi_widget_before_init.bas (expected nonzero exit)\n'
        exit 1
    fi
    if ! diff -u tests/gi/negative_gi_widget_before_init.err "$stderr_file"; then
        printf 'FAIL tests/gi/negative_gi_widget_before_init.bas (message moved)\n'
        exit 1
    fi
    if [[ -s "$stdout_file" ]]; then
        printf 'FAIL tests/gi/negative_gi_widget_before_init.bas (expected empty stdout)\n'
        exit 1
    fi
    printf 'PASS tests/gi/negative_gi_widget_before_init.bas (a located raise, exit 1)\n'

    # THE CONTROL: a non-widget Gtk type still constructs with no init at all.
    : >"$stdout_file"; : >"$stderr_file"
    if ./gbasic tests/gi/gtk_nonwidget_test.bas >"$stdout_file" 2>"$stderr_file" \
       && diff -u tests/gi/gtk_nonwidget_test.out "$stdout_file" >/dev/null; then
        printf 'PASS tests/gi/gtk_nonwidget_test.bas (six measured kinds still construct)\n'
    else
        printf 'FAIL tests/gi/gtk_nonwidget_test.bas\n'
        diff -u tests/gi/gtk_nonwidget_test.out "$stdout_file" || true
        cat "$stderr_file"
        exit 1
    fi

    # AND THE OTHER HALF, which needs a display: after initialization a widget
    # constructs as before -- including inside a Gtk.Application's `activate`,
    # where GTK initialized ITSELF and this bridge never saw it. Without this
    # the refusal is indistinguishable from "gi.new can no longer make a widget",
    # and a check that tracked its own flag instead of asking GTK would refuse
    # the construction every Studio run performs.
    if [[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
        : >"$stdout_file"; : >"$stderr_file"
        if timeout 30 ./gbasic tests/gi/gtk_widget_after_init_test.bas \
                >"$stdout_file" 2>"$stderr_file" \
           && diff -u tests/gi/gtk_widget_after_init_test.out "$stdout_file" >/dev/null; then
            printf 'PASS tests/gi/gtk_widget_after_init_test.bas (explicit init and activate)\n'
        else
            printf 'FAIL tests/gi/gtk_widget_after_init_test.bas\n'
            diff -u tests/gi/gtk_widget_after_init_test.out "$stdout_file" || true
            cat "$stderr_file"
            exit 1
        fi
    else
        printf 'SKIP tests/gi/gtk_widget_after_init_test.bas (no display)\n'
    fi
fi

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
    negative_gi_invoke_static_method
    negative_gi_invoke_static_constructor
    negative_gi_invoke_static_unknown_type
    negative_gi_invoke_static_unknown_function
    negative_gi_invoke_static_wrong_kind
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
