#!/usr/bin/env bash
# `a if c else b`, the conditional expression (Matthew, 2026-11-20: Python's
# spelling, over `if c then a else b` as an expression, which would start
# with `if` like two statements do and bring back the dangling else).
#
# TIERS
#   semantics   tests/conditional/conditional_test.bas, self-checking: the
#               chosen value only (the other would raise), right-grouping
#               chains, looser than `or` and tighter than `try`, `if`'s own
#               truth with unknown refused (code 1003, nothing assigned),
#               every place an expression goes, a row's field in plain-data
#               brackets, and a record parameter read in one is no 2125.
#   refusals    a missing `else` and `c ? a : b`, each naming the form.
#   grammar     bison reports zero conflicts -- the measurement that let the
#               form be taken (docs/ai/UNLEARN.md's `?` entry).
#   ast         --ast shows the node, condition first.
#   valgrind    the fixture, bounded.
. "$(dirname "$0")/portable.sh"
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null
status=0
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT

echo "--- TIER semantics ---"
out="$({ timeout -k 5 120 ./gbasic tests/conditional/conditional_test.bas 2>&1 </dev/null || true; })"
bad="$(printf '%s\n' "$out" | grep -vc '^ok  ' || true)"
ok="$(printf '%s\n' "$out" | grep -c '^ok  ' || true)"
if [ "${bad:-0}" -ne 0 ]; then
    printf '%s\n' "$out" | grep -v '^ok  ' | sed 's/^/  /'
    printf 'FAIL semantics (anything but ok is a failure, stderr included)\n'; status=1
elif [ "${ok:-0}" -lt 30 ]; then
    printf 'FAIL semantics (only %s checks ran; at least 30 expected)\n' "$ok"; status=1
else
    printf 'PASS semantics (%s checks, nothing on stderr)\n' "$ok"
fi

echo "--- TIER refusals ---"
say() {
    printf '%s\n' "$1" >"$tmp/p.bas"
    { ./gbasic "$tmp/p.bas" 2>&1 </dev/null || true; } | head -1 | sed 's/^[^:]*:[0-9]*:[0-9]*: //'
}
want() {
    local got; got="$(say "$2")"
    if [ "$got" = "$3" ]; then
        printf 'ok   %s\n' "$1"
    else
        printf 'MISMATCH %s\n  want: %s\n  got:  %s\n' "$1" "$3" "$got"; status=1
    fi
}
want 'a missing else'    'print 1 if true'  'unexpected end of line, expecting `else` or `or`'
want 'C'"'"'s ?: names it' 'x = c ? 1 : 2'    'gBASIC has no `c ? a : b`; write `a if c else b`'
# the advice runs as written
printf 'c = true\nx = 1 if c else 2\nprint x\n' >"$tmp/advice.bas"
if [ "$(./gbasic "$tmp/advice.bas" 2>&1)" = "1" ]; then
    printf 'ok   the advice runs as written\n'
else
    printf 'MISMATCH the advice does not run: %s\n' "$(./gbasic "$tmp/advice.bas" 2>&1 | head -1)"; status=1
fi

echo "--- TIER grammar ---"
if ! command -v bison >/dev/null 2>&1; then
    printf 'SKIP grammar (bison unavailable; the conflict count was NOT measured)\n'
else
    conf="$({ bison -d -o "$tmp/p.c" src/parser.y 2>&1 || true; } | grep -ci conflict || true)"
    if [ "${conf:-0}" -eq 0 ]; then
        printf 'PASS grammar (bison reports zero conflicts with the conditional)\n'
    else
        bison -d -o "$tmp/p.c" src/parser.y 2>&1 | grep -i conflict | sed 's/^/  /'
        printf 'FAIL grammar (the conditional now costs conflicts)\n'; status=1
    fi
fi

echo "--- TIER ast ---"
printf 'x = "a" if c else "b"\n' >"$tmp/ast.bas"
ast="$(./gbasic --ast "$tmp/ast.bas" 2>&1 | tr -s ' ' | tr '\n' '|')"
case "$ast" in
    *Conditional*Identifier\ c*String\ \"a\"*String\ \"b\"*) printf 'PASS --ast shows the condition, then each value\n' ;;
    *) printf 'FAIL --ast: %s\n' "$ast"; status=1 ;;
esac

echo "--- TIER gsql: a path's brackets compile to SQL ---"
# Without a column the conditional is a value, evaluated in gBASIC and sent as
# a parameter; with one, the filter compiler has no CASE for it and refuses
# with its own sentence -- which needs path_has_column to look inside, or the
# `.credit` would be evaluated in gBASIC and refused as a field outside brackets.
printf 'load gsql\nprint "loaded"\n' >"$tmp/probe.bas"
if [ "$({ GBASIC_PATH=stdlib ./gbasic "$tmp/probe.bas" 2>&1 || true; })" != loaded ]; then
    printf 'SKIP gsql (this build has no G-SQL; the path case was NOT measured)\n'
else
    mkdir -p "$tmp/db"
    cat >"$tmp/g.bas" <<'BAS'
load gsql
shop = gsql.connect(env("GSQL_DIR") + "/p", { create: true, linger: 0, wait: 600 })
u = shop.exec("create table customer (id integer primary key, credit integer)")
u = shop.exec("insert into customer values (1, 5), (2, 50)")
lim = 10
print "rows " + string(len(shop.customer[.credit > (100 if false else lim)].query()))
on error goto next
n = shop.customer[(.credit if true else 0) > 10].query()
if error then
    print "refused: " + error.message
end if
BAS
    out="$({ GSQL_DIR="$tmp/db" GBASIC_PATH=stdlib timeout -k 5 120 ./gbasic "$tmp/g.bas" 2>/dev/null </dev/null || true; })"
    case "$out" in
        "rows 1"*) printf 'ok   no column: a value, sent as a parameter\n' ;;
        *) printf 'MISMATCH no column: %s\n' "$out"; status=1 ;;
    esac
    case "$out" in
        *"refused: in a path's filter a column may be compared"*) printf 'ok   a column inside: the filter'"'"'s own refusal\n' ;;
        *) printf 'MISMATCH a column inside: %s\n' "$out"; status=1 ;;
    esac
fi

echo "--- TIER valgrind ---"
. "$(dirname "$0")/valgrind_tier.sh"
if vg_available; then
    rc=0
    timeout -k 5 600 bash -c '. tests/valgrind_tier.sh; vg_run ./gbasic tests/conditional/conditional_test.bas' \
        >/dev/null 2>"$tmp/vg.err" </dev/null || rc=$?
    if [ "$rc" = "$VG_EXIT" ] || [ "$rc" = 124 ]; then
        tail -30 "$tmp/vg.err"; printf 'FAIL valgrind (rc %s)\n' "$rc"; status=1
    else
        printf 'PASS valgrind (no definite leak or invalid access)\n'
    fi
else
    printf 'SKIP valgrind (unavailable)\n'
fi

exit $status
