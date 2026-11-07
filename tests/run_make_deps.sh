#!/usr/bin/env bash
# Every object's hand-written header list in the Makefile holds the headers its
# source includes (tests/make_deps_check.py says why: a stale src/frontend.o
# after a lexer.h change broke two suites in the gate, 2026-11-07). Static, no
# build. The negative control removes include/lexer.h from frontend.o's rule in
# a copy and requires the check to report it, so the check cannot pass by
# checking nothing.
cd "$(dirname "$0")/.."
status=0
if out=$(python3 tests/make_deps_check.py Makefile); then
    printf 'ok   %s\n' "$(printf '%s\n' "$out" | tail -1)"
else
    printf '%s\n' "$out" | sed 's/^/FAIL /'
    status=1
fi
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT
sed '/^src\/frontend\.o:/ s| include/lexer\.h||' Makefile > "$tmp"
if grep -q '^src/frontend\.o:.*include/lexer\.h' "$tmp"; then
    printf 'FAIL the control could not remove lexer.h from the copy\n'; status=1
elif python3 tests/make_deps_check.py "$tmp" | grep -q 'src/frontend.o: src/frontend.c includes include/lexer.h'; then
    printf 'ok   control: a rule missing a header its source includes is reported\n'
else
    printf 'FAIL control: removing lexer.h from frontend.o went unreported\n'; status=1
fi
[ "$status" = 0 ] && printf 'PASS tests/run_make_deps.sh\n'
exit "$status"
