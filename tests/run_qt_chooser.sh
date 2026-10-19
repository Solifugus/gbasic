#!/usr/bin/env bash
# A FILE CHOOSER (docs/qt_gui_design.md §0.16) -- `qt.choose_file`,
# `qt.choose_save`, `qt.choose_folder`, and `qt.dialog()`.
#
# THREE VERBS RATHER THAN ONE WITH A MODE, because they ask three different
# questions -- "which existing file?", "what shall I call it?", "which folder?"
# -- which is also how Qt splits them, and which lets each refuse the options
# that mean nothing to it instead of ignoring them.
#
# SELF-CHECKING NOT GOLDEN AND FORCED: every defect here is an
# ORDINARY-LOOKING PATH. An option accepted and dropped opens the chooser in
# the wrong folder with the wrong filter and says so nowhere; a cancelled
# chooser that answered a path hands back a file the person declined, which is
# not an error but a wrong answer, and a golden would record it as expected.
#
# FOUR THINGS WERE MEASURED BEFORE ANY OF THIS WAS WRITTEN, and three of them
# changed the design:
#
#   1. THE NATIVE DIALOG CANNOT BE DRIVEN. With the desktop's own chooser (the
#      XDG portal, which is what Qt reaches for on Wayland), `selectFile` does
#      NOTHING -- the selection is empty before exec -- and accepting does
#      nothing either: 41 pump ticks, rc=0, no file. So `DontUseNativeDialog`
#      is FORCED rather than offered, or `qt.answer` would be a silent no-op.
#   2. AND OFFSCREEN CANNOT TELL THE TWO APART, because with no portal to reach
#      Qt falls back to its own widget dialog -- so the native path measures as
#      drivable under the one platform every suite here runs on. That is
#      comfortable test data hiding the difference, so the forcing is held by a
#      SOURCE TRIPWIRE below rather than by a tier that would pass either way.
#   3. `selectFile` IS IGNORED WHILE THE DIALOG IS VISIBLE and its name field
#      has focus -- deliberately, so Qt never overwrites what somebody is
#      typing. The obvious implementation of `qt.answer(path)` therefore left
#      the selection EMPTY and the accept silently refused. What works is what
#      a person does: type into the name field, press the button.
#   4. A REJECTED QFileDialog STILL REPORTS ITS SELECTION, so reading the file
#      list without checking the result code answers a path for a chooser the
#      person cancelled. Only the `cancel` tier can see that.
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

nodisplay() {
    env -u DISPLAY -u WAYLAND_DISPLAY -u QT_QPA_PLATFORM -u XDG_RUNTIME_DIR \
        -u XDG_SESSION_TYPE "$@"
}

# ---------------------------------------------------------- NATIVE TRIPWIRE
# RUNS WHETHER OR NOT QT IS IN THE BUILD, because it reads the source: it is
# the one claim no tier can make offscreen (see 2 above), and a suite that
# skipped it along with everything else would leave the forcing unguarded on
# exactly the machines that ship the chooser.
printf 'TIER native: the desktop chooser is refused in source, since no tier can see it\n'
if grep -q 'setOption(QFileDialog::DontUseNativeDialog, true)' \
        src/modules/qt_backend.cpp; then
    pass "DontUseNativeDialog is set unconditionally"
else
    fail "the chooser no longer forces Qt's own dialog -- the native one cannot be preselected or answered (measured), so qt.answer becomes a silent no-op"
fi
# THE VACUITY CONTROL: "it is present" is equally satisfied by a grep that
# cannot match, so the same pattern must FAIL against a copy with it removed.
sed 's|setOption(QFileDialog::DontUseNativeDialog, true)|setOption(QFileDialog::ShowDirsOnly, false)|' \
    src/modules/qt_backend.cpp > "$work/native_off.cpp"
if grep -q 'setOption(QFileDialog::DontUseNativeDialog, true)' "$work/native_off.cpp"; then
    fail "CONTROL: the scanner still matched a copy with the option removed, so it is measuring nothing"
else
    pass "CONTROL: and the same scan fails on a copy without it"
fi

if ! build_has qt; then
    printf 'SKIP run_qt_chooser (no Qt in this build -- NOTHING about choosing a file was measured)\n'
    exit 0
fi

# ------------------------------------------------------------------ SEMANTICS
printf 'TIER semantics: three verbs, their options, and cancelling\n'
printf 'hello\n' > "$work/have.txt"
set +e
QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 120 ./gbasic \
    --line-buffered tests/qt_chooser_test.bas "$work" \
    > "$work/sem.out" 2> "$work/sem.err"
rc=$?
set -e
if grep -q "SKIP no display" "$work/sem.out"; then
    printf '  SKIP (no display -- the chooser was NOT exercised)\n'
elif [ "$rc" != "0" ]; then
    tail -4 "$work/sem.out"; tail -3 "$work/sem.err"
    fail "the fixture exited $rc"
else
    checks=$(grep -c '^ok' "$work/sem.out" || true)
    bad=$(grep -c 'MISMATCH' "$work/sem.out" || true)
    # A FLOOR, because a fixture that dies early leaves no MISMATCH lines and
    # would otherwise report `ok 0 checks, 0 mismatches`.
    if [ "$bad" = "0" ] && [ "${checks:-0}" -ge 18 ]; then
        pass "$checks checks, 0 mismatches"
    else
        grep MISMATCH "$work/sem.out" | head -6
        fail "$checks checks, $bad mismatches (want >= 18 and none)"
    fi
    if grep -q "^ok   filter: as a list" "$work/sem.out"; then
        pass "and the options were read back off the live chooser, not off the record"
    else
        fail "the oracle tier did not run, so title:/folder:/filter: were accepted unobserved"
    fi
fi

# ------------------------------------------------------------------ REFUSALS
# EACH BESIDE ITS NEAREST LEGAL NEIGHBOUR, or a refusal suite is satisfied by a
# chooser that refuses everything. The control is the semantics tier above,
# which passes four real options through.
printf 'TIER refusals: the option set is closed, and each refusal names its own thing\n'
refuse() {                      # name, program body, expected fragment
    cat > "$work/r.bas" <<BAS
window app( title: "R", width: 200, height: 100 )
    column( margin: 6 )
        text t( label: "x" )
    end column
end window
program main(args)
    if not qt.can_display() then
        print "NO DISPLAY"
        return
    end if
    qt.open(app)
    on error goto refused
$2
    print "ACCEPTED"
    qt.stop()
    return
refused:
    print "REFUSED " + error.message
    error.clear()
    qt.stop()
end program
BAS
    set +e
    QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 40 ./gbasic \
        --line-buffered "$work/r.bas" > "$work/r.out" 2>&1
    set -e
    if grep -q "NO DISPLAY" "$work/r.out"; then
        printf '  SKIP %s (no display)\n' "$1"
    elif grep -q "REFUSED" "$work/r.out" && grep -qF "$3" "$work/r.out"; then
        pass "$1"
    else
        head -3 "$work/r.out"
        fail "$1 (wanted a refusal containing \"$3\")"
    fi
}

refuse 'a misspelled option is refused BY NAME, with the near miss' \
    '    p = qt.choose_file(app, { fodler: "/tmp" })' \
    'did you mean `folder`?'
refuse "and one nothing is close to gets the whole vocabulary" \
    '    p = qt.choose_file(app, { wibble: "/tmp" })' \
    'takes title, folder, name and filter'
refuse 'a folder chooser has no name filter' \
    '    p = qt.choose_folder(app, { filter: "Text (*.txt)" })' \
    'has no use for `filter`'
refuse 'nor a suggested name, `folder:` being where it starts' \
    '    p = qt.choose_folder(app, { name: "x" })' \
    'has no use for `name`'
refuse "options that are not a record" \
    '    p = qt.choose_file(app, "/tmp")' \
    'options as a record'
refuse "a filter list holding something that is not text" \
    '    p = qt.choose_file(app, { filter: ["ok (*)", 7] })' \
    'every filter to be text'
refuse "a path answers a chooser, so a message box refuses one" \
    '    beat = timer.after(0.05)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            on error goto inner
            z = qt.answer("/tmp/x")
            return nothing
inner:
            print "REFUSED " + error.message
            error.clear()
            z = qt.answer(true)
            return nothing
        end while
    end watch
    y = qt.confirm(app, "?")' \
    'answered with true or false rather than with a path'

# ----------------------------------------------------------------- NO DISPLAY
# THE CORRECTION §0.15 RECORDS, APPLIED RATHER THAN REDISCOVERED: "window `app`
# is not open" is TRUE with no display and names the wrong cause -- the window
# is shut BECAUSE there is nowhere to put it.
printf 'TIER no display: the refusal names the display, not the shut window\n'
cat > "$work/nd.bas" <<'BAS'
window app( title: "ND", width: 200, height: 100 )
    column( margin: 6 )
        text t( label: "x" )
    end column
end window
program main(args)
    on error goto refused
    p = qt.choose_file(app)
    print "ACCEPTED"
    return
refused:
    print "REFUSED " + error.message
    error.clear()
end program
BAS
set +e
nodisplay timeout -k 5 60 ./gbasic "$work/nd.bas" > "$work/nd.out" 2>&1
nd_rc=$?
set -e
if [ "$nd_rc" = "0" ] && grep -q "no display is available" "$work/nd.out"; then
    pass "with no display it names the display, and is catchable"
else
    cat "$work/nd.out"; fail "the no-display refusal did not name the display (exit $nd_rc)"
fi
# AND THE CONTROL: with a display, an unopened window gets the OTHER sentence,
# or naming the display would just be what this verb always says.
set +e
QT_QPA_PLATFORM=offscreen timeout -k 5 60 ./gbasic "$work/nd.bas" > "$work/nd2.out" 2>&1
set -e
if grep -q "is not open" "$work/nd2.out"; then
    pass "CONTROL: and an unopened window still gets the other sentence"
else
    cat "$work/nd2.out"; fail "CONTROL: an unopened window did not report itself"
fi

# ------------------------------------------------------------------ VALGRIND
# ACCESS-ONLY, for the reason run_qt_backend.sh records: Qt does not free its
# platform plugin, font configuration or QApplication statics, so a LEAK claim
# would be a suppression file describing Qt. The JIT is off because PCRE2's
# regex JIT over-reads its subject by design (§0.14).
printf 'TIER valgrind: no invalid access across construction, exec and teardown\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access in a chooser was NOT measured)\n'
else
    set +e
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib \
        vg_run_access_only ./gbasic tests/qt_chooser_test.bas "$work" \
        >/dev/null 2>"$work/vg.err"
    vg_rc=$?
    set -e
    if [ "$vg_rc" = "$VG_EXIT" ]; then
        grep -E "Invalid|uninitialised" "$work/vg.err" | head -4
        fail "valgrind objected"
    else
        pass "no invalid access across three choosers and a cancel"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_chooser: all checks passed\n'
else
    printf 'run_qt_chooser: FAILED\n'
fi
exit "$status"
