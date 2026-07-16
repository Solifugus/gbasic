#!/usr/bin/env bash
# gbasic-lsp test suite (PLAN.md Phase L).
#
#   test_position     byte-column -> LSP-position transcode unit test  -> must PASS
#   handshake harness  frame a full JSON-RPC session into gbasic-lsp and
#                      byte-compare stdout against tests/lsp/handshake.golden
#
# The harness drives a real session: initialize (utf-16) -> initialized ->
# didOpen (multi-byte source with a lex error, exercising the UTF-16 transcode
# end-to-end) -> didOpen (clean, empty diagnostics) -> didChange (now has an
# error) -> didClose (clears diagnostics) -> shutdown -> exit. cJSON's compact
# output is deterministic, so the whole stdout stream is a stable golden.
#
# Regenerate the golden intentionally with:  REGEN_GOLDEN=1 tests/lsp/run_lsp.sh
#
# Skips cleanly when no C compiler is available. Exit status is nonzero only if a
# test that is supposed to pass does not.
set -u

cd "$(dirname "$0")/.."/..
CC="${CC:-cc}"
if ! command -v "$CC" >/dev/null 2>&1; then
    echo "SKIP run_lsp: no C compiler"
    exit 0
fi

TESTDIR=tests/lsp
GOLDEN="$TESTDIR/handshake.golden"
BUILD="$(mktemp -d)"
trap 'rm -rf "$BUILD"' EXIT

status=0

# ---- unit: position transcode -------------------------------------------------
echo "=== test_position (must PASS) ==="
if "$CC" -std=c11 -Isrc/lsp -g \
        "$TESTDIR/test_position.c" src/lsp/lsp_position.c \
        -o "$BUILD/test_position" 2>"$BUILD/test_position.build"; then
    if ( "$BUILD/test_position" ); then
        echo "OK   test_position"
    else
        echo "FAIL test_position  <-- transcode regression"
        status=1
    fi
else
    echo "BUILD-FAIL test_position"
    sed 's/^/    /' "$BUILD/test_position.build"
    status=1
fi

# ---- integration: framed JSON-RPC session ------------------------------------
echo "=== handshake harness (must PASS) ==="
if ! make gbasic-lsp >"$BUILD/make.log" 2>&1; then
    echo "FAIL run_lsp: could not build gbasic-lsp"
    sed 's/^/    /' "$BUILD/make.log"
    exit 1
fi

# Emit one Content-Length-framed message. Byte length (not char length) is what
# the header must carry, so wc -c is correct even with multi-byte payloads.
frame() {
    local body="$1"
    printf 'Content-Length: %d\r\n\r\n%s' "$(printf '%s' "$body" | wc -c)" "$body"
}

session() {
    frame '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{"general":{"positionEncodings":["utf-16"]}}}}'
    frame '{"jsonrpc":"2.0","method":"initialized","params":{}}'
    # doc A: multi-byte é before an unexpected token '@' — proves the UTF-16
    # transcode reaches the published range (character 8, not byte 9).
    frame '{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///a.bas","languageId":"gbasic","version":1,"text":"x = \"é\" @\n"}}}'
    # doc B: clean source -> empty diagnostics.
    frame '{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///b.bas","languageId":"gbasic","version":1,"text":"a = 1\n"}}}'
    # doc B changes to something with a parse error -> one diagnostic.
    frame '{"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///b.bas","version":2},"contentChanges":[{"text":"b = )\n"}]}}'
    # closing doc B clears its diagnostics.
    frame '{"jsonrpc":"2.0","method":"textDocument/didClose","params":{"textDocument":{"uri":"file:///b.bas"}}}'
    frame '{"jsonrpc":"2.0","id":2,"method":"shutdown"}'
    frame '{"jsonrpc":"2.0","method":"exit"}'
}

session | ./gbasic-lsp >"$BUILD/out" 2>/dev/null
lsp_exit=$?

if [ "${REGEN_GOLDEN:-0}" = "1" ]; then
    cp "$BUILD/out" "$GOLDEN"
    echo "REGEN wrote $GOLDEN"
fi

if [ "$lsp_exit" -ne 0 ]; then
    echo "FAIL handshake harness  <-- gbasic-lsp exited $lsp_exit (expected 0 after shutdown/exit)"
    status=1
elif [ ! -f "$GOLDEN" ]; then
    echo "FAIL handshake harness  <-- no golden ($GOLDEN); run REGEN_GOLDEN=1 to create it"
    status=1
elif diff -u "$GOLDEN" "$BUILD/out" >"$BUILD/diff" 2>&1; then
    echo "OK   handshake harness"
else
    echo "FAIL handshake harness  <-- stdout differs from golden"
    sed 's/^/    /' "$BUILD/diff" | head -40
    status=1
fi

echo "=== run_lsp status=$status ==="
exit $status
