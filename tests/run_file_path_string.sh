#!/usr/bin/env bash
# Whole-file verbs take a path string as well as a `{file}` reference, and
# `append` does not, because it is also the array verb.
#
# Asked for twice: DOGFOOD 2026-09-09 (`read(path)` and `write(path, text)`
# refuse a plain string) and again by the book's G30 line,
# `read(join_path(directory_name(program_file()), "spec.txt"))`, which raised
# because `join_path` answers text. Every neighbour -- `join_path`, `list`,
# `csv.records`, `zip.read`, `xml.parse_file`, `copy`'s destination -- already
# took either.
#
# Tiers:
#   SEMANTICS  the self-checking fixture: each verb through a string against
#              the same verb through `{file}` of the same path. `{file}` is the
#              unchanged route, so it is the oracle.
#   APPEND     the exception, from outside: `append(string, text)` must refuse
#              and must create NO FILE, read off the disk by the shell. A
#              refusal message alone would pass a build that wrote the file
#              and then raised.
#   VALGRIND   the conversion is a new allocation on every file verb.
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -euo pipefail

cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh
make >/dev/null
export GBASIC_PATH=stdlib

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

printf 'TIER semantics: a string is taken as {file} would take it\n'
if ! timeout -k 5 60 ./gbasic tests/file_path_string_test.bas >"$work/out" 2>"$work/err"; then
    cat "$work/err"; fail "the fixture did not run"
elif grep -q 'MISMATCH' "$work/out"; then
    grep 'MISMATCH' "$work/out"; fail "the fixture disagreed with itself"
elif ! grep -qx 'mismatches: 0' "$work/out"; then
    fail "the fixture did not finish"
else
    n=$(sed -n 's/^checks: //p' "$work/out")
    if [ -z "$n" ] || [ "$n" -lt 35 ]; then
        fail "only ${n:-0} checks ran, wanted at least 35"
    else
        pass "$n checks"
    fi
fi

printf 'TIER append: a string is refused and nothing reaches the disk\n'
mkdir "$work/ap"
printf 'name = "report"\nappend(name, "-2026")\nprint "ran on"\n' > "$work/ap/slip.bas"
rc=0
out=$(cd "$work/ap" && "$OLDPWD/gbasic" slip.bas 2>&1) || rc=$?
[ "$rc" -ne 0 ] && pass "nonzero exit ($rc)" || fail "append(string) exited 0"
case "$out" in *"ran on"*) fail "the program ran past the refusal" ;; *) pass "stopped at the refusal" ;; esac
case "$out" in *"write {file}path"*) pass "the message says how to ask" ;; *) printf '%s\n' "$out"; fail "the message does not name {file}" ;; esac
[ ! -e "$work/ap/report" ] && pass "no file named report was created" || fail "append(string) created a file"

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/file_path_string_test.bas >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access"
    else
        tail -30 "$work/vg.err"; fail "valgrind objected"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

exit $status
