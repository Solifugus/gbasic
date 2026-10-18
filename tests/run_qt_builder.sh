#!/usr/bin/env bash
# THE BUILDER (§4 of docs/qt_gui_design.md) -- a window a program BUILDS, and
# `qt.check`, the declaration's own rules over a record.
#
# §4 said the block is SUGAR OVER A BUILDER, because a declaration cannot
# contain a loop and so cannot generate a row per invoice, and that "the
# builder's surface is not designed here". MEASURED 2026-10-18 BEFORE ANYTHING
# WAS DESIGNED, AND THE DOOR WAS ALREADY OPEN: `qt.open` reads the record a
# declaration binds and asks only that `kind` is "window", so a record a
# program builds opens, renders, reads back and fires handlers exactly as a
# declared one. The surface IS the record; no verb was needed. Nobody knew,
# because nothing said so and nothing tested it -- which is the argument for
# measuring a gap before filling it, the third time in this design that the
# measurement changed what got built (menus needed no grammar; a named
# container with options cost four conflicts).
#
# WHAT IT LACKED WAS EVERY LOAD-TIME CHECK, and that is not a missing nicety:
# it is exactly the defect `web.configure` was built to close, where the record
# could already be written directly and that path validated NOTHING, so
# `app.options.workesr = 4` was accepted in silence. Measured on the binary
# before this suite existed:
#
#   a part with NO NAME       opened quietly, unaddressable forever
#   TWO PARTS SHARING A NAME  opened quietly, the second shadowing the first
#   word: "feild"             refused by the BACKEND for having no widget
#                             wired, never "did you mean 'field'?"
#   `item` in a `column`      the right refusal with the wrong sentence
#   a handler naming nothing  refused for carrying no function, never "there
#                             is nothing called `nobody` in this window"
#
# The last is the refusal load-time validation exists for, so a builder without
# it is a builder whose mistakes surface as a window that looks fine.
#
# ONE CHECKER, TWO ADAPTERS, and that is the load-bearing structural decision.
# The rules are ~350 lines and a record walker would be a SECOND COPY -- the
# drift this tree tripwires against, and which the window vocabulary had
# already suffered (it was written out THREE times before `win_nearest` was
# derived from the tables, and the newest word was missing from the third). So
# the rules moved to src/window_check.c against an ACCESSOR MODEL;
# src/frontend.c adapts the AST, src/modules/qt.c adapts a record, and neither
# holds a rule. It is `would`/`execute`'s arrangement in `automation`, for the
# reason stated there: if the two could disagree, a check would describe a
# different window from the one that opens.
#
# Tiers:
#   REFUSALS    every case above, asserted on the SENTENCE rather than on
#               something having been refused -- the declaration's whole
#               argument is that the words name the thing to edit. Needs no Qt.
#   AGREEMENT   EVERY `window` DECLARATION IN THE TREE, lifted out of its
#               fixture and put through the RECORD adapter. A declaration
#               passed the AST adapter at load time, so a refusal here means
#               the two have drifted -- the only tier that can see that, and
#               the reason the extraction is mechanical rather than a list
#               somebody maintains.
#   ONE_COPY    a source tripwire: the rules live in exactly one file, and
#               neither adapter may hold a vocabulary table or a near-miss
#               finder of its own. No behavioural check can see a second copy
#               that currently agrees.
#   OPEN        `qt.open` runs the check itself, because a validator nobody is
#               obliged to call is necessary and not sufficient --
#               `rules.validate` refused a malformed stack all along while
#               `rules.evaluate` still answered `ok: true` for a string.
#   BUILT       offscreen: a window no declaration could write. PARITY is the
#               load-bearing part -- the same window written BOTH WAYS, every
#               observable equal, INCLUDING the grid geometry `qt.at` reads
#               back from the layout, because "the builder works" is otherwise
#               satisfied by a builder that renders something plausible.
#   DEGRADING   `qt.check` ANSWERS on a build with no Qt, like `can_display`
#               and for a stronger reason: it is a check on a DECLARATION, and
#               the whole declarative surface was built to be provable with no
#               toolkit.
#   VALGRIND    the record walk allocates nothing, but it indexes a slot per
#               DEPTH and the first version was a RING OF SIXTEEN that recycled
#               the parent's slot at the seventeenth sibling -- a segfault at
#               nineteen generated parts, found by running it and invisible to
#               every value check.
. "$(dirname "$0")/portable.sh"
set -euo pipefail
cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh
make >/dev/null

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

# A FIXTURE THAT DIES REPORTS ONLY ITS DEATH and swallows the MISMATCH it had
# already printed -- run_scratch.sh's recorded trap. So the exit code is
# captured and checked BESIDE the mismatch count rather than instead of it.
run_fixture() {
    set +e
    out=$("$@" 2>&1)
    rc=$?
    set -e
    printf '%s' "$out"
    return $rc
}

report() {
    local label="$1" out="$2" rc="$3"
    local oks mismatches
    oks=$(printf '%s\n' "$out" | grep -cE '^ok ' || true)
    mismatches=$(printf '%s\n' "$out" | grep -cE '^MISMATCH ' || true)
    if [ "$rc" -ne 0 ]; then
        printf '%s\n' "$out" | tail -20
        fail "$label exited $rc"
        return
    fi
    # A DEAD FIXTURE EMITS NO MISMATCH LINES, so zero checks must FAIL rather
    # than print a green `0 checks, 0 mismatches` above the real problem.
    if [ "$oks" -eq 0 ]; then
        printf '%s\n' "$out" | tail -20
        fail "$label ran NO checks"
        return
    fi
    if [ "$mismatches" -ne 0 ]; then
        printf '%s\n' "$out" | grep -E '^MISMATCH ' | head -20
        fail "$label: $mismatches mismatches over $oks checks"
        return
    fi
    pass "$label: $oks checks, 0 mismatches"
}

printf 'TIER refusals: the declaration own rules, over a built record\n'
set +e
out=$(GBASIC_PATH=stdlib ./gbasic tests/qt_check_test.bas 2>&1)
rc=$?
set -e
report "qt_check_test" "$out" "$rc"

printf 'TIER agreement: every declared window in the tree, through the RECORD adapter\n'
# Lifted mechanically rather than listed, so a new fixture is covered without
# anybody remembering to extend this -- the glob-discovery argument run_all.sh
# makes, applied to declarations.
agree="$work/agree.bas"
: > "$agree"
body=""
files=$(grep -rl '^[[:space:]]*window [a-z_][a-z_0-9]*[[:space:]]*(' --include='*.bas' examples tests | sort)
nfiles=0
for f in $files; do
    nfiles=$((nfiles + 1))
    awk '/^[[:space:]]*window [a-z_][a-z_0-9]*[[:space:]]*\(/ {inw=1}
         inw {print}
         /^[[:space:]]*end window[[:space:]]*$/ {inw=0}' "$f" >> "$agree"
    printf '\n' >> "$agree"
    for n in $(grep -oE '^[[:space:]]*window [a-z_][a-z_0-9]*' "$f" | awk '{print $2}'); do
        body="$body"$'\n'"    dummy = probe(\"$(basename "$f"):$n\", $n)"
    done
done
cat >> "$agree" <<EOF
function probe(what, w)
    on error goto handle
    ok = qt.check(w)
    print "ok   " + what
    return 0
handle:
    print "MISMATCH " + what + ": the RECORD adapter refused a window the AST adapter accepted at load time: " + error.message
    error.clear()
end function

program main()$body
end program
EOF
if [ "$nfiles" -lt 5 ]; then
    fail "agreement found only $nfiles files with a window declaration -- the extraction is broken, not the tree"
else
    set +e
    out=$(GBASIC_PATH=stdlib ./gbasic "$agree" 2>/dev/null)
    rc=$?
    set -e
    report "agreement over $nfiles files" "$out" "$rc"
fi

printf 'TIER one_copy: the rules live in exactly one place\n'
# The direction that drifts SILENTLY is a second table that currently agrees,
# which no behavioural check can see.
rules_files=$(grep -rln 'win_container_words' --include='*.c' --include='*.h' src include | sort | tr '\n' ' ')
if [ "$rules_files" = "src/window_check.c " ]; then
    pass "the vocabulary table is only in src/window_check.c"
else
    fail "the vocabulary table appears in: $rules_files"
fi
# COMMENTS ARE STRIPPED FIRST, and that was not caution: both adapters ARGUE
# for this rule at length, so the first draft reported src/modules/qt.c for the
# sentence `never "did you mean 'field'?"` inside the comment explaining why
# the finder lives elsewhere -- the ARGUMENT for a rule read as a breach of it,
# which is the trap run_libcurl_floor.sh records for smtp.c's dot-stuffing
# comment. A tripwire whose false positives look exactly like its true ones is
# one somebody turns off.
# AND IT TRACKS STATE RATHER THAN USING A RANGE, which the third control
# below found by failing: a sed range from `/*` to `*/` whose two ends are on
# ONE LINE searches for the end from the NEXT line, finds none, and deletes to
# END OF FILE -- so a single-line comment anywhere above the code silently
# removed everything the scanner was meant to read, and "no second copy" would
# have been satisfied by reading nothing at all.
code_only() {
    awk '{
        line = $0
        out = ""
        while (1) {
            if (inc) {
                i = index(line, "*/")
                if (i == 0) { line = ""; break }
                inc = 0
                line = substr(line, i + 2)
                continue
            }
            i = index(line, "/*")
            if (i == 0) { out = out line; break }
            out = out substr(line, 1, i - 1)
            line = substr(line, i + 2)
            inc = 1
        }
        sub(/\/\/.*$/, "", out)
        print out
    }' "$1"
}
# COUNTED INTO A VARIABLE, never `| grep -q`. Under `pipefail` grep -q exits
# on the FIRST HIT, closes the pipe, awk dies of SIGPIPE at 141 and the whole
# pipeline reports failure BECAUSE IT MATCHED -- so this loop found nothing in
# a file that has it. Fifth instance of that trap in this tree, and recorded in
# run_rules.sh, which is where this helper was copied from.
near_files=""
for f in src/window_check.c src/frontend.c src/modules/qt.c; do
    hits=$(code_only "$f" | grep -c 'did you mean' || true)
    if [ "$hits" -gt 0 ]; then
        near_files="$near_files$f "
    fi
done
if [ "$near_files" = "src/window_check.c " ]; then
    pass "the near-miss finder is only in src/window_check.c"
else
    fail "a near-miss finder appears in: $near_files"
fi
# AND THE CONTROLS, without which "no second copy" is equally satisfied by a
# scanner that read nothing -- one per scanner, since they read differently.
printf 'static const char *win_container_words[] = {"row", NULL};\n' > "$work/planted.c"
if grep -q 'win_container_words' "$work/planted.c"; then
    pass "CONTROL: the table scanner flags a planted table"
else
    fail "the table scanner found nothing in a file that has one"
fi
printf 'x("did you mean %%s", near);\n' > "$work/planted2.c"
planted_hits=$(code_only "$work/planted2.c" | grep -c 'did you mean' || true)
if [ "$planted_hits" -ge 1 ]; then
    pass "CONTROL: the finder scanner flags a planted message"
else
    fail "the finder scanner found nothing in a file that has one"
fi
# AND THE OTHER DIRECTION: stripping comments must not strip CODE. Without
# this, "no second copy" is satisfied by a code_only that returns nothing.
printf '/* did you mean */\nreal("did you mean %%s");\n' > "$work/planted3.c"
kept=$(code_only "$work/planted3.c" | grep -c 'did you mean' || true)
if [ "$kept" -eq 1 ]; then
    pass "CONTROL: it strips the comment and keeps the code"
else
    fail "code_only kept $kept of one code line and one comment line"
fi
# Both adapters must go THROUGH the shared entry point, or one could quietly
# grow rules of its own beside it.
for f in src/frontend.c src/modules/qt.c; do
    if grep -q 'gb_window_check' "$f"; then
        pass "$(basename "$f") calls the shared checker"
    else
        fail "$(basename "$f") does not call gb_window_check"
    fi
done

printf 'TIER open: the check runs whether or not anybody called it\n'
cat > "$work/at_open.bas" <<'EOF'
program main()
    on error goto handle
    qt.open({ kind: "window", name: "bad", options: {},
              items: [ { kind: "container", word: "column", options: {},
                         items: [ { kind: "part", word: "field",
                                    options: {} } ] } ] })
    print "ACCEPTED"
    return
handle:
    print "REFUSED " + error.message
    error.clear()
end program
EOF
set +e
out=$(QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib ./gbasic "$work/at_open.bas" 2>&1 | grep -v propagateSize)
set -e
if printf '%s\n' "$out" | grep -q 'REFUSED.*needs a name'; then
    pass "qt.open refuses an invalid built window in the same words"
elif printf '%s\n' "$out" | grep -q 'Qt support is not available'; then
    pass "SKIP qt.open (no Qt in this build) -- the refusal tier above still ran"
else
    printf '%s\n' "$out"
    fail "qt.open did not refuse a part with no name"
fi

printf 'TIER built: a window no declaration could write, and PARITY with one\n'
if ./gbasic -e 'print 1' >/dev/null 2>&1 || true; then :; fi
set +e
probe=$(QT_QPA_PLATFORM=offscreen ./gbasic tests/qt_nodisplay_test.bas 2>&1)
set -e
if printf '%s\n' "$probe" | grep -q 'Qt support is not available'; then
    printf '  SKIP built (no Qt in this build) -- NOTHING was measured about rendering a built window\n'
else
    set +e
    out=$(QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib ./gbasic tests/qt_builder_test.bas 2>&1 | grep -v propagateSize)
    rc=$?
    set -e
    report "qt_builder_test" "$out" "$rc"
fi

printf 'TIER degrading: qt.check ANSWERS where everything else refuses\n'
# It needs no Qt BY DESIGN, so a build without Qt must still validate a built
# window -- the `web.routes` property, which is what let the declaration ship
# before any Qt existed. Asserted against THIS build's behaviour when Qt is
# present, and named when it is not.
cat > "$work/degrade.bas" <<'EOF'
program main()
    good = { kind: "window", name: "w", options: { title: "T" },
             items: [ { kind: "container", word: "column", options: {},
                        items: [ { kind: "part", word: "field", name: "a",
                                   options: {} } ] } ] }
    print "check good: " + string(qt.check(good))
    on error goto handle
    bad = { kind: "window", name: "w", options: {},
            items: [ { kind: "container", word: "column", options: {},
                       items: [ { kind: "part", word: "feild", name: "a",
                                  options: {} } ] } ] }
    ok = qt.check(bad)
    print "ACCEPTED the typo"
    return
handle:
    print "refused: " + error.message
    error.clear()
end program
EOF
set +e
out=$(GBASIC_PATH=stdlib ./gbasic "$work/degrade.bas" 2>&1)
set -e
if printf '%s\n' "$out" | grep -q 'check good: true' && \
   printf '%s\n' "$out" | grep -q "did you mean 'field'?"; then
    pass "qt.check validates with no display at all"
else
    printf '%s\n' "$out"
    fail "qt.check did not answer without a display"
fi

printf 'TIER valgrind\n'
if vg_available; then
    # NO `timeout` IN FRONT OF IT: `vg_run_access_only` is a shell FUNCTION and
    # timeout execs a program, which reports 127 -- run_odbc.sh's trap, and
    # run_qt_backend.sh's first draft. The hang guard is run_all.sh's per-suite
    # cap, as everywhere else.
    set +e
    vg_run_access_only ./gbasic tests/qt_check_test.bas \
        >"$work/vg.out" 2>"$work/vg.err"
    vg_check_rc=$?
    QT_QPA_PLATFORM=offscreen vg_run_access_only ./gbasic \
        tests/qt_builder_test.bas >"$work/vgb.out" 2>"$work/vgb.err"
    vg_built_rc=$?
    set -e
    # THE WORSE OF THE TWO, so a clean walk of the refusal fixture cannot hide
    # a defect in the one that actually opens windows.
    vg_rc=$vg_check_rc
    vg_err="$work/vg.err"
    if [ "$vg_built_rc" != "0" ]; then
        vg_rc=$vg_built_rc
        vg_err="$work/vgb.err"
    fi
    # THREE OUTCOMES, because a bare `if vg_run ...` reads the FIXTURE's own
    # nonzero as a valgrind objection -- which printed "FAIL valgrind objected"
    # over a report saying 0 errors in run_repl.sh and again in run_window.sh.
    # $VG_EXIT is the code valgrind itself is told to use, so comparing against
    # it is what separates the two.
    if [ "$vg_rc" -eq 0 ]; then
        pass "no invalid access walking and opening a built window"
    elif [ "$vg_rc" != "$VG_EXIT" ]; then
        pass "no invalid access (a fixture itself exited $vg_rc)"
    else
        grep -E 'Invalid|uninitialis|ERROR SUMMARY' "$vg_err" | head -10
        fail "valgrind objected while walking a built window"
    fi
else
    printf '  SKIP valgrind (unavailable)\n'
fi

if [ "$status" -eq 0 ]; then
    printf 'run_qt_builder: all checks passed\n'
else
    printf 'run_qt_builder: FAILURES\n'
fi
exit $status
