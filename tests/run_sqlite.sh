#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists sqlite3; then
    printf 'SKIP tests/sqlite_integration.bas (sqlite3 development files not available)\n'
    exit 0
fi

make

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
trap 'rm -f "$stdout_file" "$stderr_file"' EXIT

if ./gbasic tests/sqlite_integration.bas >"$stdout_file" 2>"$stderr_file"; then
    if diff -u tests/sqlite_integration.out "$stdout_file"; then
        printf 'PASS tests/sqlite_integration.bas\n'
    else
        exit 1
    fi
else
    status=$?
    cat "$stderr_file"
    exit "$status"
fi

negative_cases=(
    negative_sqlite_connect_type
    negative_sqlite_close_type
    negative_sqlite_query_arity
    negative_sqlite_exec_arity
    negative_sqlite_query_connection
    negative_sqlite_params_type
    negative_sqlite_param_count
    negative_sqlite_exec_rows
    negative_sqlite_blob_result
    negative_sqlite_last_insert_rowid_arity
)

for name in "${negative_cases[@]}"; do
    source="tests/$name.bas"
    expected="tests/$name.err"
    : >"$stdout_file"
    : >"$stderr_file"

    if ./gbasic "$source" >"$stdout_file" 2>"$stderr_file"; then
        printf 'FAIL %s\n' "$source"
        printf 'expected nonzero exit\n'
        exit 1
    fi

    if diff -u "$expected" "$stderr_file"; then
        printf 'PASS %s\n' "$source"
    else
        exit 1
    fi

    if [[ -s "$stdout_file" ]]; then
        printf 'FAIL %s\n' "$source"
        printf 'expected empty stdout\n'
        cat "$stdout_file"
        exit 1
    fi
done

# --- THE STATEMENT THAT FAILED is named in the message (DOGFOOD 41) --------
#
# A database's own error names a column or a table and says nothing about WHICH
# query was looking at it, and the position gBASIC reports is the call site --
# which in any program with a helper around `query` is the helper and not the
# query. Five statements through one four-line `one(db, sql)` helper left the
# word `player` as the only handle and grep as the only tool.
#
# HERMETIC AND HERE, because sqlite is the module that needs no server; `pg`
# and `odbc` share the one formatter and are exercised by their own suites.
work="$(mktemp -d)"
trap 'rm -f "$stdout_file" "$stderr_file"; rm -rf "$work"' EXIT
sqlfail() {   # label, program body -> the stderr line
    printf 'load sqlite\nprogram main()\n  db = sqlite.connect("%s/t.db")\n%s\nend program\n' \
        "$work" "$2" > "$work/p.bas"
    ./gbasic "$work/p.bas" 2>"$work/err" >/dev/null || true
    cat "$work/err"
}
sqlok() { printf 'ok   %s\n' "$1"; }
sqlbad() { printf 'FAIL %s\n       %s\n' "$1" "$2"; exit 1; }

out="$(sqlfail names '  r = sqlite.query(db, "select oops from sqlite_master")')"
case "$out" in
    *'-- in "select oops from sqlite_master"'*) sqlok "the failing statement is named" ;;
    *) sqlbad "the failing statement is named" "$out" ;;
esac

# ONE LINE. SQL is written across several and a diagnostic is one line by
# construction -- a raw newline here breaks the `file:line:col: message` shape
# every reader and every tool in this tree parses, `--json-diagnostics`
# included. Counted, not eyeballed.
out="$(sqlfail oneline '  r = sqlite.query(db, "select oops
     from
     sqlite_master")')"
[ "$(printf '%s\n' "$out" | wc -l)" = "1" ] \
    || sqlbad "a multi-line statement is collapsed to one line" "$(printf '%s' "$out" | tr '\n' '|')"
sqlok "a multi-line statement is collapsed to one line"

# TRUNCATED, and the truncation must not GLUE TWO WORDS. Written as "emit the
# pending space only if it fits", the space was dropped at the edge while the
# character after it still fitted, so `... from games where ...` came out as
# `from gamesw...` -- two words joined into one that looks like a real
# identifier. Found by running it, not by reading it.
long_sql='select name, type, rootpage, missing_column from sqlite_master where type = 1'
out="$(sqlfail truncate "  r = sqlite.query(db, \"$long_sql\")")"
case "$out" in
    *'..."'*) sqlok "a long statement is truncated" ;;
    *) sqlbad "a long statement is truncated" "$out" ;;
esac
# ASSERTED AS A PROPERTY, NOT A NEEDLE, and that is a correction: the first
# version of this check looked for two specific words joined (`masterwhere`),
# and the bug actually produces `masterw` -- one character past the lost space
# -- so the check passed against a binary carrying the defect. Proven by
# perturbation, which is the only thing that could have found it.
#
# What must hold is exact and needs no guess about which words collide: the
# excerpt, minus any `...`, is a PREFIX of the statement with its whitespace
# collapsed. A glued word breaks it, a dropped character breaks it, and a
# reordering breaks it.
excerpt="$(printf '%s' "$out" | sed -n 's/.* -- in "\(.*\)"$/\1/p')"
excerpt="${excerpt%...}"
collapsed="$(printf '%s' "$long_sql" | tr -s ' \t\n' ' ')"
[ -n "$excerpt" ] || sqlbad "the excerpt is a prefix of the statement" "no excerpt in: $out"
case "$collapsed" in
    "$excerpt"*) sqlok "and the excerpt is an exact prefix of the statement" ;;
    *) sqlbad "the excerpt is a prefix of the statement" "excerpt [$excerpt] is not a prefix of [$collapsed]" ;;
esac

# THE CONTROL, without which "the statement is named" is satisfied by a build
# that appends `...` to everything: a short statement must NOT claim it was cut.
out="$(sqlfail short '  r = sqlite.query(db, "select oops from t")')"
case "$out" in
    *'..."'*) sqlbad "a short statement does not claim truncation" "$out" ;;
    *) sqlok "a short statement does not claim truncation" ;;
esac

# THE SECOND CONTROL: a failure that is not ABOUT a statement adds nothing. A
# note on a connection error would name SQL that was never the problem, which
# is the reports-the-wrong-cause class this tree keeps finding.
printf 'load sqlite\nprogram main()\n  db = sqlite.connect("/nonexistent-dir-xyz/t.db")\nend program\n' > "$work/c.bas"
./gbasic "$work/c.bas" 2>"$work/err" >/dev/null || true
case "$(cat "$work/err")" in
    *' -- in "'*) sqlbad "a connection failure names no statement" "$(cat "$work/err")" ;;
    *) sqlok "a connection failure names no statement" ;;
esac

# THE TRIPWIRE. sqlite, pg and odbc now say this the same way, and three copies
# of a format are three things that drift -- so the suffix is built in exactly
# one place and this fails if a module starts writing its own.
built="$(grep -c -- ' -- in \\"' src/eval.c || true)"
[ "$built" = "1" ] \
    || sqlbad "the statement note is built in exactly one place" "found $built sites in src/eval.c"
sqlok "the statement note is built in exactly one place"

# --- rows_affected is about THIS statement (DOGFOOD 36) --------------------
#
# `sqlite3_changes()` is defined for INSERT, UPDATE and DELETE and holds its
# PREVIOUS value for anything else, so a `create table` run after an update
# that touched three rows answered rows_affected 3. Not a message that reads
# badly: a NUMBER that is wrong, stale from a statement two lines up, and a
# program logging it records three rows created.
#
# `nothing`, not 0, and that is not a preference: `pg` has always answered
# `nothing` here and it is the module that was right. A plausible zero is the
# answer this tree refuses everywhere else it appears.
rm -f "$work/c.db"
cat > "$work/rows.bas" <<BAS
load sqlite
program main()
  db = sqlite.connect("$work/c.db")
  print sqlite.exec(db, "create table t (a integer)").rows_affected
  print sqlite.exec(db, "insert into t values (1), (2), (3)").rows_affected
  print sqlite.exec(db, "update t set a = a + 1").rows_affected
  print sqlite.exec(db, "create table u (b integer)").rows_affected
  print sqlite.exec(db, "delete from t where a > 99").rows_affected
end program
BAS
rows="$(./gbasic "$work/rows.bas" 2>&1 | tr '\n' ' ')"
# THE LOAD-BEARING ONE IS THE FOURTH: a CREATE after an UPDATE. The first
# CREATE would answer 0 on the broken build too (nothing had run yet), so a
# check on it alone passes on the defect.
[ "$rows" = "nothing 3 3 nothing 0 " ] \
    || sqlbad "rows_affected is about this statement" "got [$rows], want [nothing 3 3 nothing 0 ]"
sqlok "rows_affected is nothing where a count means nothing"
# AND THE CONTROL: a DELETE that matched no rows is a real zero, not an
# absence. Without it, "answer nothing for anything uncounted" is satisfied by
# a build that never reports a count at all -- it is the last field above.
sqlok "and a genuine zero is still zero"

# --- a refusal names the function that was CALLED (DOGFOOD 37) -------------
# One prepare serves `sqlite.query` and `sqlite.exec`, and its messages said
# "SQLite query ..." for both -- so `sqlite.exec(db, "a; b")` was refused in
# the name of a function the program had not called, sending the author to
# look at the wrong line. Asserted BOTH WAYS, or "it says exec" is satisfied
# by a build that now names the wrong one in the other direction.
out="$(sqlfail multi_exec '  sqlite.exec(db, "create table z (a integer); create table y (b integer)")')"
case "$out" in
    *"sqlite.exec expects exactly one statement"*) sqlok "exec is refused in exec's name" ;;
    *) sqlbad "exec is refused in exec's name" "$out" ;;
esac
out="$(sqlfail multi_query '  r = sqlite.query(db, "select 1; select 2")')"
case "$out" in
    *"sqlite.query expects exactly one statement"*) sqlok "and query in query's" ;;
    *) sqlbad "and query in query's" "$out" ;;
esac
# The remedy is named as well, because a refusal that only says no leaves the
# author guessing whether the module can be talked into it.
case "$out" in
    *"run them one at a time"*) sqlok "and the refusal names the remedy" ;;
    *) sqlbad "and the refusal names the remedy" "$out" ;;
esac
