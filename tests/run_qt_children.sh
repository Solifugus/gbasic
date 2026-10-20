#!/bin/bash
# A CONTAINER WHOSE CHILDREN CHANGE (§0.27) -- `qt.refresh(win, name, items)`,
# the third form of a verb that already means "re-read what this shows", and
# `qt.tabs`, which is the only way to observe a caption.
#
# ASKED FOR BY THE CONSUMER, WITH MEASUREMENTS, which is why this one is not a
# gap I found: docs/qt_dynamic_children_proposal.md was written by the gBASIC
# Studio side from converting its shell, and it established the shape of the
# gap rather than describing it -- `qt.read(win, "book", "count")` answers 2 for
# a two-tab `tabs` so the set is OBSERVABLE, `qt.refresh(win, "book")` refuses
# by naming tables, and `qt.invoke` passes NO ARGUMENTS, so `removeTab(int)`
# and `setTabText(int, QString)` are out of reach by construction. Studio's
# notebook is one tab per open document, which is what the application IS.
#
# THE LOAD-BEARING TIER IS KEPT, and the reason it is worth building at all was
# measured in C++ before a line of this: a QPlainTextEdit taken out of a
# QTabWidget and put back keeps its text AND its undo stack, and so does one
# removed from a box layout and re-inserted. Every other check -- the count,
# the order, the captions -- passes on an implementation that REBUILDS
# everything, which is the naive version and the one this has to be
# distinguishable from.
#
# AND "ABSENT" MEANS DELETED. Measured: a child dropped from a box layout and
# NOT deleted stays parented and VISIBLE, floating at its old geometry -- so a
# reconcile that merely unhooked it would leave the window showing a widget the
# record no longer mentions, with nothing raised. That is §0.17's rule for a
# dismissed window arriving one level in, and it is why the name must leave the
# parts registry with the widget.
#
# A TAB CARRIES A NAME IN THE RECORD, which the DECLARATION cannot spell: its
# one slot is the caption (§0.8). That is not a loosening for symmetry -- a
# reconcile needs an identity, and the CAPTION IS THE THING THAT CHANGES, since
# Studio's tab labels carry a `* ` unsaved marker, so keying on it would
# rebuild the editor on the first keystroke. The one-slot rule is a fact about
# the grammar and not about the record, which is what §0.9's builder already
# rests on.
. "$(dirname "$0")/portable.sh"
# NO `set -e`, DELIBERATELY, and the `set +e` lines below are not paired with a
# restore for that reason: this header omits -e, so a bare `set -e` would ARM
# it rather than put it back -- and the cost is paid exactly when a tier is
# going red, where the next unguarded `grep` that finds nothing kills the suite
# with no verdict. run_portable.sh tripwires for it and caught this file within
# minutes of its being written.
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
# NOTHING NEW IS SPELLED, so this is the standing zero-conflict claim rather
# than a cost: the verb takes a third argument and the record carries a name a
# builder could always set.
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
# RUNS WHETHER OR NOT QT IS IN THE BUILD, being a claim about the load-time
# pass: a `tab` may now carry a name, and the containers that cannot be
# reconciled are refused BY NAME rather than silently doing nothing.
printf 'TIER declaration: a named tab is admitted, and the record rules hold\n'
decl() {                        # label, window body, expected fragment ("" = accept)
    cat > "$work/d.bas" <<BAS
window app( title: "D", width: 300, height: 200 )
$2
end window
program main(args)
    print "RAN"
end program
BAS
    out="$(GBASIC_PATH=stdlib ./gbasic "$work/d.bas" 2>&1 || true)"
    if [ -z "$3" ]; then
        if printf '%s' "$out" | grep -q RAN; then
            pass "$1"
        else
            printf '     %s\n' "$(printf '%s' "$out" | head -2)"
            fail "$1 (should have been accepted)"
        fi
    elif printf '%s' "$out" | grep -qF -- "$3"; then
        pass "$1"
    else
        printf '     %s\n' "$(printf '%s' "$out" | head -2)"
        fail "$1"
    fi
}
decl "a captioned tab is unchanged" '    tabs book
        tab "One"
            text a( label: "x" )
        end tab
    end tabs' ""
# AND THE DECLARATION STILL REQUIRES A CAPTION, which is the better pair and
# which the binary taught me rather than the other way round: I expected `tab
# one` to be admitted once the name rule was relaxed, and it is refused by a
# DIFFERENT rule -- a tab needs a caption, because that is what a person reads
# on it. Both rules are right, and together they say exactly what §0.8 says:
# the GRAMMAR has one slot, so a declared tab spends it on the caption and the
# callers that need an identity are the ones BUILDING a record, which has no
# such limit. Nothing was loosened for the declaration at all.
decl "and a named tab with no caption is still refused" '    tabs book
        tab one
            text a( label: "x" )
        end tab
    end tabs' "a tab needs a caption"

# ------------------------------------------------------------ DROP IS A DELETE
# THE ONE CLAIM NO BEHAVIOURAL CHECK CAN REACH, and a perturbation proved it
# rather than caution suggesting it: replacing the `delete` with a `hide()`
# leaves EVERY check in this suite green, because the name is purged either way
# and a widget nobody can name is unobservable from the language -- the leak
# that would show it is excluded by the Qt valgrind policy (access-only, since
# Qt does not free its own platform plugin or QApplication statics).
#
# AND THE HALF BESIDE IT **IS** COVERED, measured the same way: skipping the
# NAME PURGE also leaves every check green -- `qt.read` on a dropped name
# happens to refuse -- and valgrind reports 21 invalid reads, which is the
# tier's own stated reason for existing. So the two halves are proven by
# different tiers rather than both resting on this one.
printf 'TIER drop: the dropped child is deleted, not merely unhooked\n'
dropfn="$(sed -n '/^extern "C" int gb_qt_child_drop/,/^}/p' src/modules/qt_backend.cpp)"
if printf '%s' "$dropfn" | grep -q 'delete w;' \
   && printf '%s' "$dropfn" | grep -q 'setParent(nullptr)'; then
    pass "the drop path reparents and deletes the widget"
else
    fail "the drop path no longer deletes -- a child merely unhooked from a box layout stays PARENTED AND VISIBLE, measured"
fi
# THE CONTROL, since "it contains the word delete" is equally satisfied by a
# scanner that read the wrong function or nothing at all.
if printf '%s' "$dropfn" | grep -q 'gb_qt_child_drop' \
   && [ "$(printf '%s' "$dropfn" | wc -l)" -gt 20 ]; then
    pass "and the scanner really read that function"
else
    fail "the scanner did not find gb_qt_child_drop -- it is measuring nothing"
fi

# -------------------------------------------------------------- SEMANTICS
printf 'TIER semantics: kept, built, dropped, recaptioned and reordered\n'
if ! build_has qt; then
    printf '  SKIP (no Qt in this build -- the reconcile was NOT measured)\n'
else
    set +e
    QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 60 \
        ./gbasic --line-buffered tests/qt_children_test.bas \
        > "$work/sem.out" 2> "$work/sem.err"
    rc=$?
    if [ "$rc" = "124" ] || [ "$rc" = "137" ]; then
        fail "the fixture HUNG"
    elif grep -q NODISPLAY "$work/sem.out"; then
        printf '  SKIP (no display and no offscreen platform)\n'
    elif [ "$rc" != "0" ]; then
        tail -3 "$work/sem.out"; printf '     %s\n' "$(head -2 "$work/sem.err")"
        fail "the fixture exited $rc"
    else
        bad=$(grep -c '^MISMATCH' "$work/sem.out" || true)
        checks=$(sed -n 's/^ok  *\([0-9]*\) checks.*/\1/p' "$work/sem.out")
        if [ "${bad:-1}" != "0" ]; then
            grep '^MISMATCH' "$work/sem.out" | head -5
            fail "$bad mismatches"
        elif [ "${checks:-0}" -lt 16 ]; then
            # A FLOOR, because a fixture that died early prints no MISMATCH and
            # would otherwise read as a clean sweep of nothing.
            fail "only ${checks:-0} checks ran -- the fixture stopped early"
        else
            pass "$checks checks, 0 mismatches"
        fi
        if [ -s "$work/sem.err" ] \
           && grep -vqE "This plugin does not support|^$" "$work/sem.err"; then
            head -3 "$work/sem.err"; fail "the fixture wrote to stderr"
        else
            pass "and nothing on stderr"
        fi
    fi
fi

# -------------------------------------------------------------- VALGRIND
printf 'TIER valgrind: no invalid access across a reconcile that deletes\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access was NOT measured)\n'
elif ! build_has qt; then
    printf '  SKIP (no Qt in this build)\n'
else
    # THE PATH WORTH MEASURING: a dropped child is DELETED and its name purged
    # from the registry, so a stale QObject* there is a use-after-free that a
    # correct answer would not reveal -- which is this suite's own reason for
    # the tier rather than a habit.
    set +e
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib \
        vg_run_access_only ./gbasic tests/qt_children_test.bas \
        >/dev/null 2>"$work/vg.err" &
    vg_pid=$!
    vg_killed=0
    vg_deadline=$((SECONDS + 420))
    while kill -0 "$vg_pid" 2>/dev/null; do
        if [ "$SECONDS" -ge "$vg_deadline" ]; then
            for vg_kid in $(pgrep -P "$vg_pid" 2>/dev/null); do
                kill -9 "$vg_kid" 2>/dev/null
            done
            kill -9 "$vg_pid" 2>/dev/null
            pkill -KILL -f "gbasic tests/qt_children_test.bas" 2>/dev/null
            vg_killed=1
            break
        fi
        sleep 1
    done
    wait "$vg_pid"
    vg_rc=$?
    if [ "$vg_killed" = "1" ]; then
        fail "the fixture hung under valgrind, so memory access was NOT measured"
    elif [ "$vg_rc" = "$VG_EXIT" ]; then
        grep -E "Invalid|uninitialised" "$work/vg.err" | head -4
        fail "valgrind objected"
    else
        pass "no invalid access across a grow, a reorder and a delete"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_children: all checks passed\n'
else
    printf 'run_qt_children: FAILED\n'
fi
exit "$status"
