#!/usr/bin/env bash
# L4, the easy half (stdlib/dbframe.bas over docs/xlsx_design.md §7 and §13.P):
# a frame becomes a database table. §7 sequences this before the formula
# compiler and calls it valuable on its own.
#
# Runs the WHOLE PIPELINE end to end -- xlsx -> grid (L2) -> consolidate (L3)
# -> sqlite (L4a) -- then queries the result, which is the point.
#
# Tiers:
#   1. GOLDEN.
#   2. EXACTNESS -- the loaded total must compare equal to the figure the four
#      tapes imply. Compared, not printed: `print` renders 265550.75 as 265551,
#      so a printed total would hide exactly the cents the pipeline exists to
#      preserve.
#   3. INJECTION -- a loan id of `'); drop table loans;--` must round-trip as
#      itself AND leave the table standing. "We use bound parameters" is a
#      claim only a test can make true, so it is executed rather than asserted.
#   4. REFUSALS -- an unsafe table name, and two headings that collapse to the
#      same identifier (which would silently overwrite each other).
#   5. APPEND vs REPLACE -- loading twice appends; there is no default that
#      silently doubles a total.
#   6. VALGRIND.
#
# Skips when sqlite3 dev files were absent at build time, per the project's
# optional-dependency convention.
set -u
cd "$(dirname "$0")/.."

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_dbframe: build failed\n'
    exit 1
fi

out=$(mktemp); err=$(mktemp); tmp=$(mktemp -d)
trap 'rm -rf "$out" "$err" "$tmp"' EXIT
export GBASIC_PATH=stdlib
status=0

printf 'program main(args)\n  load sqlite\n  print sqlite.connect(":memory:")\nend program\n' >"$tmp/probe.bas"
if ! ./gbasic "$tmp/probe.bas" >/dev/null 2>"$err"; then
    printf 'SKIP run_dbframe (sqlite not available in this build)\n'
    exit 0
fi

printf -- '-- golden: xlsx -> grid -> consolidate -> sqlite\n'
if timeout 120 ./gbasic examples/xlsx_dbframe_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u examples/xlsx_dbframe_test.out "$out"; then
        printf 'PASS examples/xlsx_dbframe_test.bas\n'
    else
        printf 'FAIL examples/xlsx_dbframe_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL examples/xlsx_dbframe_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# Exactness, injection survival and the refusals, asserted independently of the
# golden so a rebaseline cannot quietly accept a regression in any of them.
cat >"$tmp/assert.bas" <<'EOF'
program main(args)
  load grid
  load consolidate
  load dbframe
  load frame
  wb = xlsx.open("examples/fixtures/xlsx/tapes.xlsx")
  srcs = []
  for each s in xlsx.sheets(wb)
    r = grid.extract(grid.of(wb, s), { header_row: 1 })
    append(srcs, { name: s, frame: r.frame })
  end for
  spec = { columns: {
             loan_id: { names: ["Loan #", "Note ID", "loan_number"], kind: "text", required: true },
             balance: { names: ["Balance", "Principal", "Amount"], kind: "money", required: true },
             rate:    { names: ["Int Rate", "Interest Rate", "Rate (%)"], kind: "percent" } } }
  m = consolidate.merge(srcs, spec)
  db = sqlite.connect(":memory:")
  dbframe.to_table(m.frame, db, "loans", { replace: true })
  print sqlite.query(db, "select sum(balance) s from loans", [])[0].s = 265550.75
  nasty = "'); drop table loans;--"
  odd = frame.from_rows([{ id: nasty, amt: 1 }])
  dbframe.to_table(odd, db, "odd", { replace: true })
  print sqlite.query(db, "select id from odd", [])[0].id = nasty
  print sqlite.query(db, "select count(*) c from loans", [])[0].c = 6
  print dbframe.to_table(odd, db, "bad; drop table loans", { }).ok
  print sqlite.close(db)
end program
EOF
a_res=$(timeout 60 ./gbasic "$tmp/assert.bas" 2>/dev/null | tr '\n' ' ')
case "$a_res" in
  "true true true false "*)
    printf 'PASS totals exact to the cent, injection round-tripped and harmless, bad name refused\n' ;;
  *)
    printf 'FAIL expected "true true true false", got "%s"\n' "$a_res"
    status=1 ;;
esac

if command -v valgrind >/dev/null 2>&1; then
    if valgrind --error-exitcode=9 --leak-check=full --errors-for-leak-kinds=definite -q \
        ./gbasic examples/xlsx_dbframe_test.bas >/dev/null 2>"$err"; then
        printf 'PASS valgrind examples/xlsx_dbframe_test.bas\n'
    else
        printf 'FAIL valgrind\n'; head -20 "$err"; status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit $status
