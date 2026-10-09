#!/usr/bin/env bash
# The VS Code extension (editors/vscode/gbasic), run INSIDE a real VS Code:
# @vscode/test-electron downloads one into editors/vscode/gbasic/.vscode-test
# (git-ignored), loads the extension from source with every other extension
# disabled, and test/suite.js requires that opening a file with a syntax error
# produces a diagnostic FROM gbasic-lsp on the right line -- and that fixing the
# unsaved buffer clears it, which is the control: a client that never cleared
# would pass a test that only looked for an error.
#
# The server is this tree's `make gbasic-lsp`, put on PATH, so the PATH lookup
# an installed gBASIC relies on is exercised too. GBASIC_TEST_INSTALLED=1 (run
# by hand after installing the MSIX) uses the installed alias instead.
#
# SKIPS, naming why, without Node.js or without the extension's dev
# dependencies (`npm ci` in editors/vscode/gbasic), since both are downloads.
set -u
cd "$(dirname "$0")/.."
ext=editors/vscode/gbasic

if ! command -v npm >/dev/null 2>&1; then
    printf 'SKIP tests/run_vscode_extension.sh (needs Node.js and npm)\n'
    exit 0
fi
if [ ! -d "$ext/node_modules/@vscode/test-electron" ]; then
    printf 'SKIP tests/run_vscode_extension.sh (run `npm ci` in %s first)\n' "$ext"
    exit 0
fi
make gbasic-lsp >/dev/null 2>&1 || { printf 'FAIL build gbasic-lsp\n'; exit 1; }

out="$(cd "$ext" && npm run typecheck 2>&1)" || {
    printf 'FAIL typecheck\n%s\n' "$out"
    exit 1
}
printf 'PASS typecheck\n'

out="$(cd "$ext" && timeout -k 10 900 npm test 2>&1)"
rc=$?
if [ "$rc" -eq 0 ] && printf '%s' "$out" | grep -q '^diagnostics cleared after the fix'; then
    printf 'PASS in VS Code: %s; cleared after the fix\n' "$(printf '%s\n' "$out" | grep -m1 '^diagnostic:')"
else
    printf 'FAIL in VS Code (exit %s)\n' "$rc"
    printf '%s\n' "$out" | grep -E '^diagnostic|timed out|Error|failed' | head -10
    exit 1
fi
