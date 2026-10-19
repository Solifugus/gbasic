#!/usr/bin/env bash
# A TABLE THAT ASKS FOR ITS CELLS (docs/qt_gui_design.md §0.25) -- `table x to
# ask fn`, `qt.rows`, and a model behind a QTableView.
#
# `set +e` BELOW WITHOUT A MATCHING `set -e`, DELIBERATELY: this file's mode is
# `set -uo pipefail` -- no `-e` -- so `set +e` is a no-op and a `set -e` after
# it would TURN THE MODE ON, which costs nothing until a suite goes red and
# then kills it mid-tier with no verdict. run_portable.sh tripwires it.
#
# WHY IT EXISTS. `table x to show <list>` materialises every row into a
# QTableWidget, so §0.6 deferred the large-result path to a MODEL -- and gBASIC
# Studio is the consumer rather than a hypothesis: its results grid switches to
# `datagrid.create_virtual(count_fn, cell_fn)` above 500 rows, and `datagrid`
# is GTK-only, so this was the ONE thing its Qt conversion could not express.
#
# ONE MEASUREMENT DECIDED WHETHER IT IS POSSIBLE AT ALL, because a model's
# `data()` has to call back into the interpreter and per cell of the whole
# table that would be unusable:
#
#        100 rows, 300px tall  ->  27 data() calls, highest row 8
#     10,000 rows, 300px tall  ->  27 data() calls, highest row 8
#  1,000,000 rows, 300px tall  ->  27 data() calls, highest row 8
#  1,000,000 rows, 900px tall  ->  87 data() calls, highest row 28
#  scrollToBottom on 1,000,000 ->  54 calls IN TOTAL, straight to row 999,999
#
# THE COST IS THE VIEWPORT, NOT THE TABLE. And it needed NO new seam, which is
# a correction to §0.25's own first assessment: `invoke_function(stmt, args,
# argc, receiver)` already takes arguments and returns a Value -- the handler
# path simply passes `NULL, 0, NULL` because handlers take none.
#
# `ask` COSTS ZERO GRAMMAR: `IDENT IDENT TO IDENT IDENT` is the production a
# named container with a subject already uses, and the direction is recognised
# by POSITION rather than by a token, so `ask` joins `show` and `edit` in the
# validator. A form is still refused one, because a form shows or edits ONE
# record whose fields it can read and there is nothing for it to ask FOR.
#
# THE TIER TO READ FIRST IS COST, and the oracle was FREE for once: the
# callback is gBASIC code, so it counts its own calls. Asserted as a DIFFERENCE
# across four orders of magnitude, plus the other side of the same claim (far
# fewer calls than rows) -- a model that walked the table would be correct and
# unusable, and nothing else in the file would notice.
. "$(dirname "$0")/portable.sh"
set -uo pipefail
cd "$(dirname "$0")/.."
source tests/build_has.sh
source tests/valgrind_tier.sh
make >/dev/null

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

# ------------------------------------------------------------------ GRAMMAR
printf 'TIER grammar: bison still reports zero conflicts\n'
if ! command -v bison >/dev/null 2>&1; then
    printf '  SKIP (no bison -- the grammar cost was NOT measured)\n'
else
    bison -d src/parser.y -o "$work/p.c" 2>"$work/bison.err" || true
    sr=$(grep -cE "shift/reduce" "$work/bison.err" || true)
    rr=$(grep -cE "reduce/reduce" "$work/bison.err" || true)
    if [ "${sr:-0}" = "0" ] && [ "${rr:-0}" = "0" ]; then
        pass "zero shift/reduce and zero reduce/reduce"
    else
        head -4 "$work/bison.err"
        fail "the grammar now reports conflicts"
    fi
fi

# -------------------------------------------------------------- DECLARATION
# RUNS WHETHER OR NOT QT IS IN THE BUILD: claims about the DECLARATION.
printf 'TIER declaration: `ask` is a table direction and not a form one\n'
decl() {                        # name, window body, expected fragment ("" = accept)
    cat > "$work/d.bas" <<BAS
window app( title: "D", width: 300, height: 200 )
$2
end window
program main(args)
    print "RAN"
end program
BAS
    set +e
    ./gbasic "$work/d.bas" > "$work/d.out" 2>&1
    if [ -n "$3" ]; then
        if grep -qF "$3" "$work/d.out" && ! grep -q RAN "$work/d.out"; then
            pass "$1"
        else
            head -2 "$work/d.out"
            fail "$1 (wanted a refusal containing \"$3\")"
        fi
    else
        if grep -q RAN "$work/d.out"; then
            pass "$1"
        else
            head -2 "$work/d.out"
            fail "$1 (expected it to be accepted)"
        fi
    fi
}
decl 'a table may ask' \
'    column
        table r to ask cell_of
            field a( label: "A" )
        end table
    end column' \
    ''
# THE CONTROL PAIR. A form shows or edits ONE record whose fields it can read,
# so there is nothing for it to ask FOR -- and `form x to ask f` would be a
# declaration whose subject could never be bound. Without this, "ask is a
# direction" is satisfied by letting every word that carries a subject take it.
decl 'a form may not ask' \
'    column
        form to ask cell_of
            field a( label: "A" )
        end form
    end column' \
    'a form is `to edit` or `to show`'
decl 'and a table still refuses a word nobody defined' \
'    column
        table r to fetch cell_of
            field a( label: "A" )
        end table
    end column' \
    '`to show`, `to edit` or `to ask`'
decl 'CONTROL: a held table is untouched' \
'    column
        table r to show rows
            field a( label: "A" )
        end table
    end column' \
    ''

# ------------------------------------------------------------------ SEMANTICS
printf 'TIER semantics: the cost, the coordinates, the columns and the control\n'
set +e
QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 120 ./gbasic \
    --line-buffered tests/qt_ask_test.bas > "$work/sem.out" 2> "$work/sem.err"
rc=$?
# A HANG IS NOT A FAILURE, and this fixture is the one most able to hang: it
# hands the thread to `qt.run()` twice and takes it back with a timer, so a
# tick that never arrives waits for ever. `timeout -k` above is the bound, and
# 124 is named rather than reported as an ordinary failure.
if [ "$rc" = "124" ] || [ "$rc" = "137" ]; then
    fail "the fixture HUNG -- a turn of Qt's loop never handed the thread back"
elif grep -q "SKIP no display" "$work/sem.out"; then
    printf '  SKIP (no display -- the asking table was NOT exercised)\n'
elif [ "$rc" != "0" ]; then
    grep MISMATCH "$work/sem.out" | head -4
    tail -3 "$work/sem.out"; tail -3 "$work/sem.err"
    fail "the fixture exited $rc"
else
    checks=$(grep -c '^ok' "$work/sem.out" || true)
    bad=$(grep -c 'MISMATCH' "$work/sem.out" || true)
    if [ "$bad" = "0" ] && [ "${checks:-0}" -ge 23 ]; then
        pass "$checks checks, 0 mismatches"
    else
        grep MISMATCH "$work/sem.out" | head -6
        fail "$checks checks, $bad mismatches (want >= 23 and none)"
    fi
    grep -v 'This plugin does not support' "$work/sem.err" > "$work/sem.err2" || true
    if [ -s "$work/sem.err2" ]; then
        head -3 "$work/sem.err2"; fail "the fixture wrote to stderr"
    else
        pass "and nothing of its own on stderr"
    fi
    # THE HEADLINE CLAIM, NAMED. Both sides of it: four orders of magnitude of
    # rows cost the same handful of calls, AND that handful is nowhere near the
    # table's size. A model that asked for every cell would be correct and
    # unusable.
    if grep -q "^ok   a million rows costs no more than a hundred" "$work/sem.out" &&
       grep -q "^ok   and far fewer than the rows themselves" "$work/sem.out"; then
        pass "COST: the viewport, not the table -- asserted both ways"
    else
        fail "the cost claim was not measured -- a model that walked the rows would pass everything else"
    fi
    # THE COLUMN ORACLE. A cell's data comes from the CALLBACK and its heading
    # from the DECLARATION, so a column mapped wrongly shows the right values
    # under the wrong headings -- which only the captions can say, and only by
    # POSITION, since by name shares the resolution being tested.
    if grep -q "^ok   column 2 is the date" "$work/sem.out" &&
       grep -q "^ok   asking for the third column asks the callback for 2" "$work/sem.out"; then
        pass "COLUMNS: captions by position, and the callback asked for the match"
    else
        fail "the column mapping was not measured -- right values under wrong headings is the plausible defect here"
    fi
    if grep -q "^ok   a cell knows its row" "$work/sem.out" &&
       grep -q "^ok   the callback saw that row" "$work/sem.out"; then
        pass "ROWS: a cell encodes its own coordinates, so an offset is visible"
    else
        fail "the row coordinates were not measured -- an offset shows real data about the wrong record"
    fi
    if grep -q "^ok   a held table still shows its list" "$work/sem.out" &&
       grep -q "^ok   and a held table is not an asking one" "$work/sem.out"; then
        pass "CONTROL: the ordinary table is untouched, and the two are distinct"
    else
        fail "the held table was not asserted beside the asking one -- the same word builds two widgets"
    fi
    if grep -q "^ok   a number cell" "$work/sem.out" &&
       grep -q "^ok   a date cell" "$work/sem.out"; then
        pass "KINDS: a number and a date cross the seam through the one renderer"
    else
        fail "the value kinds were not measured -- a cell that only ever returned text would pass the rest"
    fi
fi

# ------------------------------------------------------------------ VALGRIND
# ACCESS-ONLY, for the reason run_qt_backend.sh records. What is NEW here is a
# callback that crosses the C/gBASIC seam PER CELL, with heap-allocated
# arguments whose ownership passes to the callee and a malloc'd string coming
# back the other way -- and it runs inside a Qt PAINT, which is the one place
# nothing can unwind.
printf 'TIER valgrind: no invalid access across the per-cell callback\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access was NOT measured)\n'
else
    set +e
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib \
        vg_run_access_only ./gbasic tests/qt_ask_test.bas \
        >/dev/null 2>"$work/vg.err"
    vg_rc=$?
    if [ "$vg_rc" = "$VG_EXIT" ]; then
        grep -E "Invalid|uninitialised" "$work/vg.err" | head -4
        fail "valgrind objected"
    else
        pass "no invalid access across the per-cell callback and the model"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_ask: all checks passed\n'
else
    printf 'run_qt_ask: FAILED\n'
fi
exit "$status"
