#!/usr/bin/env bash
# METHODS ON HANDLES (docs/handle_methods_design.md, Matthew's ruling): a
# handle's methods are its module's verbs, so `h.verb(args)` is
# `module.verb(h, args)` -- derived from the module, not listed, so a verb a
# module gains is a method at once. Started with G-SQL connections and blocks
# and Qt windows.
#
# Self-checking fixtures, every method asked beside its module form:
#   gsql        a block's exec/query, a connection's begin/rollback/commit/
#               exec/on/close; a receiver in an array, and a CALL receiver
#               evaluated once (passing the receiver's expression on instead
#               of its value would run the call twice); a verb that does not
#               take the handle (`db.connect()`) refused in its own words;
#               a record's own function field and a variable named like the
#               module still winning
#   unused      a block used only as `add.exec()` is a use, so warning 2121
#               stays quiet; and 2121 names the method form when it does fire
#   qt          a window opened, written, read, set, invoked and closed through
#               methods, offscreen
#   valgrind    the gsql fixture, no invalid access, no definite leak
. "$(dirname "$0")/portable.sh"
. "$(dirname "$0")/valgrind_tier.sh"
set -uo pipefail
cd "$(dirname "$0")/.."

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

if ! make -s gbasic >"$work/make.out" 2>&1; then
    tail -20 "$work/make.out"
    printf 'run_handle_methods: FAILED (./gbasic did not build)\n'
    exit 1
fi
root=$PWD
GSQL_FIX=$root/tests/handle_methods/gsql_methods.bas
QT_FIX=$root/tests/handle_methods/qt_methods.bas

# a fixture's verdict: no MISMATCH, its last line `done`, and a floor
verdict() {
    local name=$1 file=$2 rc=$3 floor=$4
    grep MISMATCH "$file" | head -12
    local n
    n=$(grep -c '^ok ' "$file")
    if [ "$rc" != 0 ]; then
        grep -m3 'runtime error' "$file"
        fail "$name: the fixture exited $rc"
    elif grep -q MISMATCH "$file"; then
        fail "$name: $(grep -c MISMATCH "$file") mismatches"
    elif [ "$n" -lt "$floor" ]; then
        fail "$name: only $n checks ran, under the floor of $floor"
    else
        pass "$name: $n checks"
    fi
}

printf 'load gsql\nprint "loaded"\n' > "$work/probe.bas"
if ! probe=$(./gbasic "$work/probe.bas" 2>&1) || [ "$probe" != loaded ]; then
    printf '  SKIP gsql, unused, valgrind (this build has no G-SQL: %s -- G-SQL methods were NOT measured)\n' "$probe"
else
    printf 'TIER gsql: a connection and a block take gsql verbs as methods\n'
    mkdir -p "$work/g"
    (cd "$work/g" && "$root/gbasic" "$GSQL_FIX") >"$work/g.out" 2>"$work/g.err"
    rc=$?
    cat "$work/g.err" >>"$work/g.out"
    verdict "gsql" "$work/g.out" "$rc" 14
    if [ -s "$work/g.err" ]; then
        head -3 "$work/g.err"
        fail "gsql: the fixture wrote to stderr"
    fi

    printf 'TIER unused: a method on a block is a use\n'
    mkdir -p "$work/u"
    cat >"$work/u/used.bas" <<'GB'
load gsql
database db( file: "u.gsql", create: true, linger: 0 )
sql make on db
    create table t (id integer primary key)
end sql
r = make.exec()
c = db.close()
GB
    cat >"$work/u/unused.bas" <<'GB'
load gsql
database db( file: "v.gsql", create: true, linger: 0 )
sql make on db
    create table t (id integer primary key)
end sql
GB
    err=$(cd "$work/u" && "$root/gbasic" used.bas 2>&1)
    if printf '%s' "$err" | grep -q 2121; then
        fail "unused: make.exec() was not counted as a use: $err"
    else
        pass "unused: make.exec() counts as a use"
    fi
    err=$(cd "$work/u" && "$root/gbasic" unused.bas 2>&1)
    if printf '%s' "$err" | grep -qF "only when make.exec(), make.query() or"; then
        pass "unused: 2121 names the method form"
    else
        fail "unused: $err"
    fi

    printf 'TIER valgrind: the gsql fixture\n'
    if vg_available; then
        mkdir -p "$work/vg"
        (cd "$work/vg" && vg_run "$root/gbasic" "$GSQL_FIX") >"$work/vg.out" 2>"$work/vg.err"
        vgst=$?
        if [ "$vgst" = 0 ] && ! grep -q MISMATCH "$work/vg.out" && tail -1 "$work/vg.out" | grep -q '^done$'; then
            pass "valgrind: clean"
        elif [ "$vgst" = "$VG_EXIT" ]; then
            grep -m8 'Invalid\|definitely lost\|uninitialised' "$work/vg.err"
            fail "valgrind objected"
        else
            tail -3 "$work/vg.out"
            fail "valgrind: the fixture did not pass under valgrind (exit $vgst)"
        fi
    else
        printf '  SKIP valgrind (unavailable)\n'
    fi
fi

printf 'TIER qt: a window takes qt verbs as methods\n'
printf 'print qt.can_display()\n' > "$work/qtprobe.bas"
probe=$(QT_QPA_PLATFORM=offscreen ./gbasic "$work/qtprobe.bas" 2>/dev/null)
if [ "$probe" != true ]; then
    printf '  SKIP qt (no Qt in this build, or no display: Qt methods were NOT measured)\n'
else
    QT_QPA_PLATFORM=offscreen ./gbasic "$QT_FIX" >"$work/q.out" 2>"$work/q.err"
    verdict "qt" "$work/q.out" "$?" 6
fi

if [ "$status" = 0 ]; then
    printf 'run_handle_methods: PASS\n'
else
    printf 'run_handle_methods: FAILED\n'
fi
exit $status
