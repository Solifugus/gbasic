#!/usr/bin/env bash
set -uo pipefail

# The `odbc` module: one driver-manager connection string, one set of verbs,
# every backend unixODBC can reach.
#
# WHY THE FIXTURES ARE SELF-CHECKING RATHER THAN GOLDEN. The failure this
# module is built to prevent is not a crash: it is an ordinary-looking number
# that is wrong in the last digits. A DECIMAL(19,4) narrowed to a double, or a
# BIGINT past 2^53, comes back as a value nobody would look at twice -- and a
# golden would record the damaged value AS the expected output and defend it
# from then on. So `odbc_test.bas` states the answer it expects for every
# check and prints `ok` or a MISMATCH naming both sides; this script asserts
# that it ran a floor number of checks and declared zero mismatches, which a
# fixture that dies halfway cannot fake.
#
# The exactness tier is the point of the whole module: it inserts 2^53+1,
# reads it back as a string, and asserts BOTH that the digits survived AND
# that routing the same value through a double would have changed it. The
# second half is what makes the first half more than a tautology.
#
# THE INJECTION TIER EXECUTES ITS CLAIM AND PROVES ITSELF NON-VACUOUS. The
# obvious version -- bind `'); drop table x;--` and check the table survives
# -- passes on a module that pastes every parameter, because the SQLite3 ODBC
# driver refuses multiple statements anyway ("only one SQL statement
# allowed"); that was measured, not assumed. The tier's load-bearing payload
# is therefore a TAUTOLOGY, which subverts a SINGLE statement on any engine,
# and the fixture runs it BOTH ways: pasted it matches every row, bound it
# matches only the row whose value really is that text. The pasted half is
# what keeps the bound half honest.
#
# CONNECTION. The fixtures take their connection string from
# GBASIC_ODBC_CONNECTION, so the SAME fixtures run against a real SQL Server
# or MySQL by setting it -- the hermetic default is the SQLite3 ODBC driver
# over a scratch file, which is a genuine driver-manager round trip (unixODBC
# loads a real .so and speaks the real protocol) rather than a stub. It skips
# cleanly when the build has no ODBC or no driver is installed.
#
# Headless, no network in the default configuration.

cd "$(dirname "$0")/.."
make >/dev/null || exit 1

probe="$(mktemp)"
printf 'load odbc\n' >"$probe"
./gbasic "$probe" >/dev/null 2>"$probe.err"
if command grep -q 'not available in this build' "$probe.err"; then
    rm -f "$probe" "$probe.err"
    printf 'SKIP tests/run_odbc.sh (this build has no ODBC support)\n'
    exit 0
fi
rm -f "$probe" "$probe.err"

driver="${GBASIC_ODBC_DRIVER:-SQLite3}"
if [[ -z "${GBASIC_ODBC_CONNECTION:-}" ]]; then
    if ! command -v odbcinst >/dev/null 2>&1; then
        printf 'SKIP tests/run_odbc.sh (odbcinst not available to find a driver)\n'
        exit 0
    fi
    if ! odbcinst -q -d 2>/dev/null | command grep -qx "\[$driver\]"; then
        printf 'SKIP tests/run_odbc.sh (no %s ODBC driver installed)\n' "$driver"
        exit 0
    fi
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

export GBASIC_ODBC_CONNECTION="${GBASIC_ODBC_CONNECTION:-Driver=$driver;Database=$work/odbc_test.db}"

failures=0
checks=0
pass() { checks=$((checks + 1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks + 1)); failures=$((failures + 1)); printf '  FAIL %s\n' "$1"; }

run_fixture() {
    local fixture="$1" floor="$2" label="$3"
    ./gbasic "$fixture" >"$work/out" 2>"$work/err"
    local status=$?

    if [[ $status -eq 0 ]]; then
        pass "$label exits 0"
    else
        fail "$label exits 0 (exit $status)"
        head -5 "$work/err"
    fi

    if [[ -s "$work/err" ]]; then
        fail "$label writes nothing to stderr"
        head -5 "$work/err"
    else
        pass "$label writes nothing to stderr"
    fi

    if command grep -q MISMATCH "$work/out"; then
        fail "$label reports no mismatch"
        command grep MISMATCH "$work/out" | head -10
    else
        pass "$label reports no mismatch"
    fi

    # The fixture's OWN summary line, not the absence of MISMATCH: a fixture
    # that dies on line 20 also prints no mismatches.
    if command grep -qx 'mismatches: 0' "$work/out"; then
        pass "$label finished and declared zero mismatches"
    else
        fail "$label finished and declared zero mismatches"
        tail -5 "$work/out"
    fi

    local ran
    ran="$(command grep '^checks: ' "$work/out" | sed 's/^checks: //')"
    if [[ -n "$ran" ]] && [[ "$ran" -ge "$floor" ]]; then
        pass "$label ran at least $floor checks (ran $ran)"
    else
        fail "$label ran at least $floor checks (ran '${ran:-none}')"
    fi
}

printf 'TIER semantics\n'
run_fixture tests/odbc_test.bas 57 'odbc_test'

# Named individually so deleting one shrinks the suite loudly rather than
# quietly. These are the claims that make this more than a smoke test.
for label in \
    'bigint beyond 2\^53 survives exactly' \
    'through a double it would not have' \
    'an exact decimal survives to the last digit' \
    'and is byte-identical, not merely the right length' \
    'money keeps its cents through a bound parameter' \
    'pasted, the payload matches every row' \
    'bound, it matches only the row that holds it' \
    'the payload is genuinely hostile' \
    'a hostile value stores and reads back unchanged' \
    'the table the injection targeted still stands' \
    'SQL NULL becomes nothing' \
    'rollback discards it' \
    'commit keeps it' \
    'a write after commit is durable without an explicit commit'
do
    if command grep -Eq "^ok   $label\$" "$work/out"; then
        pass "asserted: $label"
    else
        fail "asserted: $label"
    fi
done

printf 'TIER refusals (live connection)\n'
run_fixture tests/odbc_refusal_test.bas 12 'odbc_refusal_test'

printf 'TIER refusals (pinned messages)\n'
# These need no connection, so their exact wording is pinned as a golden.
# They live here rather than in run_negative.sh because `load odbc` itself
# raises in a build without ODBC, which would fail the whole negative suite
# on a machine that simply lacks unixODBC.
negative_cases=(
    negative_odbc_connect_type
    negative_odbc_connect_arity
    negative_odbc_close_type
    negative_odbc_query_arity
    negative_odbc_exec_arity
    negative_odbc_query_connection
    negative_odbc_unknown_verb
)
for name in "${negative_cases[@]}"; do
    ./gbasic "tests/$name.bas" >"$work/out" 2>"$work/err"
    if [[ $? -eq 0 ]]; then
        fail "$name exits nonzero"
        continue
    fi
    if diff -u "tests/$name.err" "$work/err" >/dev/null; then
        pass "$name matches its pinned message"
    else
        fail "$name matches its pinned message"
        diff -u "tests/$name.err" "$work/err" | head -10
    fi
    if [[ -s "$work/out" ]]; then
        fail "$name writes nothing to stdout"
    fi
done

# A driver that is not installed must be named as such. "Can't open lib" is
# the driver manager's answer and it is a DIFFERENT answer from a server
# refusing a login -- a dashboard has to be able to tell its user which.
./gbasic tests/negative_odbc_connect_failure.bas >"$work/out" 2>"$work/err"
if [[ $? -ne 0 ]] &&
   command grep -q 'odbc connection failed:' "$work/err" &&
   command grep -q 'NoSuchDriverHere' "$work/err"; then
    pass 'an absent driver is refused by name'
else
    fail 'an absent driver is refused by name'
    head -3 "$work/err"
fi

printf 'TIER catalog\n'
# odbc.drivers() has to see the driver this suite is running against, or the
# catalog call is answering from somewhere other than the driver manager the
# connections use.
cat >"$work/catalog.bas" <<'EOF'
load odbc
for each d in odbc.drivers()
    print d.name
next
EOF
if ./gbasic "$work/catalog.bas" 2>/dev/null | command grep -qx "$driver"; then
    pass "odbc.drivers lists $driver"
else
    if [[ -n "${GBASIC_ODBC_DRIVER:-}" ]] || [[ "$GBASIC_ODBC_CONNECTION" == Driver=* ]]; then
        fail "odbc.drivers lists $driver"
    else
        pass "odbc.drivers skipped (connection string names no driver)"
    fi
fi

printf 'TIER binary-safe parameters\n'
# gBASIC strings hold interior NULs. Bound with SQL_NTS -- the obvious
# spelling, and what this module did until it was measured -- the driver gets
# strlen() and SILENTLY SENDS THE PREFIX: "a\0b" arrived as "a", no error, no
# short write, nothing to see. The parameter now carries its real byte length.
#
# THE ASSERTION IS MADE FROM THE DATABASE'S SIDE, not ours: hex() is computed
# by SQLite over the bytes it actually stored, so it cannot be satisfied by a
# reader that guesses. Note the round trip still does NOT recover the value --
# the SQLite3 ODBC driver hands text back strlen-truncated, which the same
# query shows (length() reports 1 over three stored bytes). The write is ours
# to get right and now is; the read is the driver's, and this tier records
# which is which rather than pretending both are solved.
if [[ "$driver" == "SQLite3" ]]; then
    cat >"$work/nul.bas" <<'EOF'
load odbc
db = odbc.connect(env("GBASIC_ODBC_CONNECTION"))
odbc.exec(db, "drop table if exists gb_odbc_nul")
odbc.exec(db, "create table gb_odbc_nul (v varchar(40))")
odbc.exec(db, "insert into gb_odbc_nul values (?)", ["a" + from_bytes([0]) + "b"])
for each r in odbc.query(db, "select hex(v) as h from gb_odbc_nul")
    print r.h
next
odbc.exec(db, "drop table gb_odbc_nul")
odbc.close(db)
EOF
    if [[ "$(./gbasic "$work/nul.bas" 2>/dev/null)" == "610062" ]]; then
        pass 'a parameter with an interior NUL is sent whole'
    else
        fail 'a parameter with an interior NUL is sent whole'
        ./gbasic "$work/nul.bas" 2>&1 | head -3
    fi
fi

printf 'TIER valgrind\n'
if command -v valgrind >/dev/null 2>&1; then
    for fixture in tests/odbc_test.bas tests/odbc_refusal_test.bas; do
        # The driver manager and the driver itself leak by design (dlopen'd
        # libraries, one-time catalogs), so this tier asserts NO INVALID
        # ACCESS rather than no leak: every real defect in this module so far
        # was a use-after-free or a walk over an array that was never
        # allocated, and neither produced a wrong value.
        if valgrind --error-exitcode=99 --errors-for-leak-kinds=none \
                    --leak-check=no -q \
                    ./gbasic "$fixture" >/dev/null 2>"$work/vg"; then
            pass "valgrind clean: $fixture"
        else
            fail "valgrind clean: $fixture"
            head -20 "$work/vg"
        fi
    done
else
    printf '  SKIP valgrind (not installed)\n'
fi

printf '\n%d checks, %d failed\n' "$checks" "$failures"
[[ $failures -eq 0 ]]
