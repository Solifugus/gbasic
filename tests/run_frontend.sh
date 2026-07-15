#!/usr/bin/env bash
# Front-end library tests: exercise libgbasic.a directly (structure, not stderr).
#
#   test_diagnostics_sink   sink API unit test             -> must PASS
#   test_parse_diagnostics  3 errors -> 3 diagnostics       -> must PASS (Phase 1)
#   test_two_contexts       concurrent parse (reentrancy)   -> must PASS (Phase 2)
#
# Skips cleanly when no C compiler is available. Exit status is nonzero only if a
# test that is supposed to pass does not.
set -u

cd "$(dirname "$0")/.."
CC="${CC:-cc}"
if ! command -v "$CC" >/dev/null 2>&1; then
    echo "SKIP run_frontend: no C compiler"
    exit 0
fi

TESTDIR=tests/frontend
BUILD="$(mktemp -d)"
trap 'rm -rf "$BUILD"' EXIT

# Optional-dependency libs, mirroring the Makefile, so linking the archive always
# resolves even if a future front-end test pulls an eval.c object. The current
# front-end objects do not reference eval.c, so these are typically unused.
opt_libs() {
    local libs="-lm"
    for pc in libpq sqlite3 libcurl libxcrypt libcrypto libxml-2.0; do
        if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists "$pc" 2>/dev/null; then
            libs="$libs $(pkg-config --libs "$pc" 2>/dev/null)"
        fi
    done
    echo "$libs"
}
LIBS="$(opt_libs)"

echo "=== building libgbasic.a ==="
if ! make libgbasic.a >/dev/null 2>&1; then
    echo "FAIL run_frontend: could not build libgbasic.a"
    exit 1
fi

status=0

build_and_run() { # name  extra_ldflags
    local name="$1" extra="${2:-}"
    local bin="$BUILD/$name"
    if ! "$CC" -std=c11 -Iinclude -g "$TESTDIR/$name.c" libgbasic.a $extra $LIBS -o "$bin" 2>"$BUILD/$name.build"; then
        echo "BUILD-FAIL $name"
        sed 's/^/    /' "$BUILD/$name.build"
        return 2
    fi
    ( "$bin" ) >"$BUILD/$name.out" 2>/dev/null
    local rc=$?
    sed 's/^/    /' "$BUILD/$name.out"
    return $rc
}

echo "=== test_diagnostics_sink (must PASS) ==="
if build_and_run test_diagnostics_sink; then
    echo "OK   test_diagnostics_sink"
else
    echo "FAIL test_diagnostics_sink  <-- sink API regression"
    status=1
fi

echo "=== test_parse_diagnostics (must PASS) ==="
if build_and_run test_parse_diagnostics; then
    echo "OK   test_parse_diagnostics"
else
    echo "FAIL test_parse_diagnostics  <-- Phase 1 sink wiring regression"
    status=1
fi

echo "=== test_two_contexts (must PASS) ==="
if build_and_run test_two_contexts "-pthread"; then
    echo "OK   test_two_contexts"
else
    echo "FAIL test_two_contexts  <-- parser reentrancy regression"
    status=1
fi

echo "=== run_frontend status=$status ==="
exit $status
