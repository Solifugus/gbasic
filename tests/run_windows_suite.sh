#!/usr/bin/env bash
# The native Windows suite (tests/windows/*.bas), run on EVERY platform.
#
# Those files are the gate a Windows machine has -- self-checking gBASIC, the
# same file on both platforms (tests/windows/README.md says why) -- and until
# this runner nothing invoked them: they were run by hand, which is the gate
# CLAUDE.md warns about, the one you have to remember to run and which
# therefore silently shrinks. They are DISCOVERED by glob, so a new file joins
# the gate by existing; `*_child.bas` are the helpers the suites launch, not
# suites. Run from the repository root, as each file expects.
#
# A file passes when its LAST line is exactly `mismatches: 0` -- each prints
# `BROKEN: ...` instead if its own tally counted too few checks, so a suite
# that silently stopped asserting cannot pass here. A last line beginning
# `SKIP` is reported as a skip, with the file's own reason (odbc_unicode needs
# GBASIC_ODBC_CONNECTION). Anything else, or a nonzero exit, is a failure.
set -u
cd "$(dirname "$0")/.."
make >/dev/null || { printf 'FAIL build\n'; exit 1; }

status=0
ran=0
for f in tests/windows/*.bas; do
    case "$f" in *_child.bas) continue ;; esac
    out="$(timeout -k 5 300 ./gbasic "$f" 2>&1)"
    rc=$?
    last="$(printf '%s\n' "$out" | tail -1)"
    if [ "$rc" -eq 0 ] && [ "$last" = "mismatches: 0" ]; then
        printf 'PASS %s (%s)\n' "$f" "$(printf '%s\n' "$out" | grep -c '^ok ')"
        ran=$((ran + 1))
    elif [ "$rc" -eq 0 ] && [ "${last#SKIP}" != "$last" ]; then
        printf '%s\n' "$last"
    elif printf '%s' "$out" | grep -qE 'support is (not available in this build|unavailable)|requires OpenSSL|not available on Windows'; then
        # A module this build lacks (xml_encodings.bas needs libxml2): the
        # binary's own refusal, never a feature under test failing.
        printf 'SKIP %s (%s)\n' "$f" "$(printf '%s' "$out" | grep -m1 -oE '[A-Za-z0-9 -]*(support is [a-z ]*|requires OpenSSL|not available on Windows)')"
    else
        printf 'FAIL %s (exit %s)\n' "$f" "$rc"
        printf '%s\n' "$out" | grep -E 'MISMATCH|BROKEN|error' | head -20
        printf '  last line: %s\n' "$last"
        status=1
    fi
done

# THE WINDOWS FILE VERSION MUST BE THE BINARY'S VERSION. src/gbasic.manifest
# carries one by hand (it is what Windows shows in a file's Properties, and the
# MSIX build reads `gbasic --version` instead); it sat at 0.4.0.0 while the
# binary said 0.5.1, found only when master was merged. Asked of the source on
# every platform, since the manifest is a source file.
gb_version="$(grep -o 'printf("gBASIC [0-9][0-9.]*[0-9]' src/main.c | head -1 | sed 's/.* //')"
manifest_version="$(sed -n 's/.*name="gBASIC.gbasic" version="\([0-9.]*\)".*/\1/p' src/gbasic.manifest)"
if [ -n "$gb_version" ] && [ "$manifest_version" = "$gb_version.0" ]; then
    printf 'PASS src/gbasic.manifest version %s matches gbasic %s\n' "$manifest_version" "$gb_version"
else
    printf 'FAIL src/gbasic.manifest says %s but gbasic --version says %s (want %s.0)\n' \
        "${manifest_version:-nothing}" "${gb_version:-nothing}" "$gb_version"
    status=1
fi

# smoke, process_run and process_start need nothing optional; a run that
# passed fewer than those three passed nothing it should have.
if [ "$ran" -lt 3 ]; then
    printf 'FAIL only %d of the always-runnable suites passed\n' "$ran"
    status=1
fi
[ "$status" -eq 0 ] && printf 'run_windows_suite: all passed\n'
exit "$status"
