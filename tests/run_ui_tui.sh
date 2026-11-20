#!/usr/bin/env bash
# GATE-LANE: serial -- drives real ptys and waits on what the program writes back, which a loaded machine turns into a guess
#
# The terminal's `ui` backend (docs/tui_design.md §7 increment 3,
# src/modules/ui_tui.c): a `window` drawn in cells over src/term.c, the
# selection rule that chooses it, `needs:`, and sizes in characters.
#
# It is LEFT OUT OF EVERY OTHER GUI SUITE'S PASSES until it covers the core
# (tests/ui_backend.sh, UI_SUITES_SKIP_BACKENDS), so this suite is where it is
# measured. THE ORACLES ARE OUTSIDE IT: what each key must do, written by hand
# below; a real pty driven by tests/ui_tui/terminal.py, which reads the pty's
# settings itself and, with pyte, the screen; Qt for the sizes.
#
# Tiers:
#   [N] sizes     `40 columns` and `12 rows` read as size records, and a
#                 fraction or a mixture refused at its position.
#   [T] drive     with no terminal (GBASIC_TUI_SCREEN): typing, the caret,
#                 max_length, a secret, read only, Tab order, Space and Enter,
#                 a click, invoke, write, the bound subject, visible, a chord
#                 refused, the close reported once; held to the lines below.
#   [T] layout    the screen as text (GBASIC_TUI_DUMP) against
#                 tests/ui_tui/layout.txt, and what a golden cannot argue
#                 with: box corners, the stretch's buttons at the right edge,
#                 a field 30 columns wide.
#   [T] parts     increment 4 by keys and presses: a table and a tree by
#                 arrows, a choice's list, menus by F10 and Alt, a submenu, a
#                 checkable item, a toolbar item, pages by Ctrl+PgDn; and
#                 the menu bar, toolbar, tab line and status bar drawn.
#   [T] dialogs   increment 5 by keys: a question (No the default, Left,
#                 y, Escape), a statement wrapped, the chooser (folders
#                 first, hidden files and other kinds left out, a folder
#                 entered, the filter stepped, a name typed, a missing one
#                 refused in the box, save and folder kinds), a modal window
#                 as a box over its owner (Enter accepts, Escape rejects);
#                 the screens held to tests/ui_tui/dialogs.txt.
#   [T] editor    increment 6 by keys: a memo wrapping at words, Enter, Up
#                 and Down keeping a column, Shift selecting, Backspace,
#                 undo by steps and redo, Tab typed and Ctrl+Tab leaving; a
#                 source numbered and unwrapped, scrolled to its caret,
#                 marked, white space shown, a margin press, select_text,
#                 replace_text undone in one step, a marked range moving
#                 with its text, read_only; held to tests/ui_tui/editor.txt.
#                 Then the editor's refusals, Qt's sentences word for word.
#   [T] refusals  a `source` needing `highlighting` is refused by group;
#                 no terminal names itself and GBASIC_TUI_SCREEN; a window
#                 needing `docking` is refused naming qt; a `needs:` that is
#                 no group; warning 2124 for an icon with no label.
#   [Q] qtsizes   on Qt, 40 columns is twice 20, from the font, and a height
#                 in columns is refused.
#   [T] live      a real pty: the rule picks tui with no display and Qt with
#                 one; typing, Tab, Space, Enter; ui.stop; Ctrl-Q; the
#                 terminal restored. Skips without python3.
#   [T] live_dialogs  a real pty: a question by Enter and by Left, a
#                 statement by a mouse press on OK, the chooser by arrows
#                 and by a lone Escape.
#   [T] live_editor  a real pty: a memo typed into, a bracketed paste with
#                 its line break, a mouse press placing the caret, a drag
#                 selecting, Backspace taking it, Shift+Tab leaving.
#   [T] needs     a program whose declared windows need `docking`, run where
#                 only the terminal could draw.
#   [T] valgrind  the drive, parts, dialogs and editor fixtures.
. "$(dirname "$0")/portable.sh"
set -u
cd "$(dirname "$0")/.."
source tests/build_has.sh
. "$(dirname "$0")/valgrind_tier.sh"
. "$(dirname "$0")/ui_backend.sh"

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_ui_tui: build failed\n'
    exit 1
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
status=0
GB="$PWD/gbasic"
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

# ------------------------------------------------------------------- SIZES
printf 'TIER [N] sizes: a size in characters is a record, and only one term\n'
cat >"$work/sizes.bas" <<'EOF'
print 40 columns
print 12 rows
print 1 row
print 3 seconds
EOF
got=$("$GB" "$work/sizes.bas" 2>&1)
if [ "$got" = $'{"columns":40}\n{"rows":12}\n{"rows":1}\n3 seconds' ]; then
    pass "40 columns, 12 rows and 1 row are size records; a duration is unchanged"
else
    fail "sizes printed: $got"
fi
for bad in "x = 2 columns 3 rows" "x = 1 hour 2 columns" "x = 1.5 columns" "x = 0 rows"; do
    printf '%s\n' "$bad" >"$work/bad.bas"
    got=$("$GB" "$work/bad.bas" 2>&1)
    case "$got" in
        "parse error at "*":1:5: a size in columns or rows is "*) pass "refused at its position: $bad" ;;
        *) fail "$bad: $got" ;;
    esac
done

# -------------------------------------------------------------------- DRIVE
printf 'TIER [T] drive: the keys a person presses, with no terminal\n'
if ui_tier_tui; then
    cat >"$work/drive.want" <<'EOF'
tui
name changed to [Bo]
focus name
name changed to [BoA]
name changed to [BA]
BA
name changed to [A]
A
name changed to [Az]
name changed to [Azz]
name changed to [Azzz]
name changed to [Azzzz]
name changed to [Azzzzz]
name changed to [Azzzzzz]
name changed to [Azzzzzzz]
max_length 8: Azzzzzzz
name changed to []
cleared: []
name changed to [J]
sé shown ••
pass entered
pass enter handled true
code typed handled false, still X1
after code, Tab: news
after news, Tab: cancel
after ok, Tab: name
before name, Shift+Tab: ok
news pressed, checked true
news changed
news now true
ok pressed
cancel pressed
news pressed, checked false
news changed
disabled check pressed: false
still false
{"name":"J","pass":"sé","code":"Z9"}
news visible false
a chord: nothing
closed, name J
open after close: false
EOF
    GBASIC_UI=tui GBASIC_TUI_SCREEN=50x14 "$GB" tests/ui_tui/drive.bas >"$work/drive.got" 2>"$work/drive.err"
    if cmp -s "$work/drive.got" "$work/drive.want" && [ ! -s "$work/drive.err" ]; then
        pass "42 lines, each what the keys must have done (the form's 'Bo' firing changed as on Qt, Left then Backspace, max_length 8, bullets, Tab skipping the disabled check, Enter on a check pressing OK)"
    else
        fail "drive differs from what the keys must do:"
        diff "$work/drive.want" "$work/drive.got" | head -12 | sed 's/^/       /'
        sed 's/^/       stderr: /' "$work/drive.err" | head -4
    fi
fi

# ------------------------------------------------------------------- LAYOUT
printf 'TIER [T] layout: the screen as text, and what a golden cannot argue with\n'
if ui_tier_tui; then
    GBASIC_UI=tui GBASIC_TUI_SCREEN=50x12 GBASIC_TUI_DUMP="$work/layout.txt" "$GB" tests/ui_tui/layout.bas
    if cmp -s "$work/layout.txt" tests/ui_tui/layout.txt; then
        pass "the screen equals tests/ui_tui/layout.txt"
    else
        fail "the screen differs from tests/ui_tui/layout.txt:"
        diff tests/ui_tui/layout.txt "$work/layout.txt" | head -10 | sed 's/^/       /'
    fi
    # independent of the golden: the box's corners where the group's rows
    # are, the stretch's buttons ending at the last column, the caret
    if sed -n 2p "$work/layout.txt" | grep -q '^┌ Customer ─.*┐$' && sed -n 5p "$work/layout.txt" | grep -q '^└─*┘$' \
       && [ "$(sed -n 2p "$work/layout.txt" | wc -m)" = 51 ]; then
        pass "the group's box spans the 50 columns, its title in the top border"
    else
        fail "the group's box: $(sed -n 2,5p "$work/layout.txt" | tr '\n' '|')"
    fi
    if sed -n 9p "$work/layout.txt" | grep -q '^\[ help \] .*\[ Place order \]$' && [ "$(sed -n 9p "$work/layout.txt" | wc -m)" = 51 ]; then
        pass "an icon-only button shows its name; the stretch pushes the others to the right edge"
    else
        fail "the button row: $(sed -n 9p "$work/layout.txt")"
    fi
    cat >"$work/width.bas" <<'EOF'
window w( title: "x" )
    column
        field note( label: "Note", min_width: 30 columns, max_width: 30 columns )
        field wide( label: "Wide" )
    end column
end window
ui.open(w)
print ui.read(w, "note", "width")
print ui.read(w, "wide", "width")
EOF
    got=$(GBASIC_UI=tui GBASIC_TUI_SCREEN=50x12 "$GB" "$work/width.bas" 2>&1)
    if [ "$got" = $'240\n360' ]; then
        pass "30 columns is 30 cells (240 at 8 a cell), the caption beside it not counted; an unsized field takes the rest (45)"
    else
        fail "widths: $got"
    fi
fi

# -------------------------------------------------------------------- PARTS
printf 'TIER [T] parts: increment 4 by the keys a person presses, with no terminal\n'
if ui_tier_tui; then
    cat >"$work/parts.want" <<'EOF'
menus ["File","Edit"]
chosen 0
chosen 1
chosen 2
activated 2
chosen 0
chosen 1
activated 1
pick changed to green
pick activated
pick changed to blue
pick activated
pick is blue
after Escape pick is blue
open pressed
one pressed
wrap now true
undo pressed
save pressed
a toolbar item clicked by name: nothing
book page 1
and the second handler
file src
file a.c
depth of row 2 1
book page 0
and the second handler
status done
EOF
    GBASIC_UI=tui GBASIC_TUI_SCREEN=60x16 GBASIC_TUI_DUMP="$work/parts.txt" "$GB" tests/ui_tui/parts.bas >"$work/parts.got" 2>&1
    if cmp -s "$work/parts.got" "$work/parts.want"; then
        pass "28 lines, each what the keys must do: arrows, End and Home on a table and a tree, a row clicked, a choice stepped and its list picked from and escaped, F10 and Alt into menus past a separator and into a submenu, a checkable item, a toolbar item, Ctrl+PgDn and Ctrl+PgUp between pages, each running both handlers on the page change"
    else
        fail "parts differ from what the keys must do:"
        diff "$work/parts.want" "$work/parts.got" | head -12 | sed 's/^/       /'
    fi
    if sed -n 2p "$work/parts.txt" | grep -q '^ File  Edit$' && sed -n 3p "$work/parts.txt" | grep -q '^\[ Save \]$' \
       && sed -n 4p "$work/parts.txt" | grep -q '^ Orders   Files$' && [ "$(sed -n 16p "$work/parts.txt")" = "ready" ] \
       && sed -n 6,8p "$work/parts.txt" | tr '\n' '|' | grep -q '^  1 Ann|.*22 Bo|333 Cy|$'; then
        pass "the menu bar, the toolbar, the tab line, a right-aligned column and the status bar on the last line"
    else
        fail "the parts screen: $(tr '\n' '|' < "$work/parts.txt")"
    fi
fi

# ------------------------------------------------------------------ DIALOGS
printf 'TIER [T] dialogs: increment 5 by the keys a person presses, with no terminal\n'
if ui_tier_tui; then
    mkdir -p "$work/pick/docs"
    : >"$work/pick/notes.txt"; : >"$work/pick/c.md"; : >"$work/pick/.hidden"
    : >"$work/pick/docs/a.txt"; : >"$work/pick/docs/b.md"
    GBASIC_UI=tui GBASIC_TUI_SCREEN=60x16 GBASIC_TUI_DUMP="$work/dlg.txt" timeout 60 "$GB" --line-buffered \
        tests/ui_tui/dialogs.bas "$work/pick" </dev/null >"$work/dlg.raw" 2>&1
    # THE FOLDER LINE is the one place the scratch path is drawn; its
    # padding follows the path's length, so both go
    sed -E "s#$work/pick([^ ]*) *#WORK\1 #g" "$work/dlg.raw" >"$work/dlg.got"
    if cmp -s "$work/dlg.got" tests/ui_tui/dialogs.txt; then
        pass "a question four ways, a statement wrapped, the chooser five ways and a modal window two: every answer and every screen as tests/ui_tui/dialogs.txt holds them"
    else
        fail "dialogs differ from tests/ui_tui/dialogs.txt:"
        diff tests/ui_tui/dialogs.txt "$work/dlg.got" | head -14 | sed 's/^/       /'
    fi
    # AND WHAT A GOLDEN CANNOT ARGUE WITH: the answers themselves
    for want in "Enter on the default: false" "Left, then Enter: true" "y: true" "Escape: false" \
                "chosen PICK/docs/a.txt" "chosen by the list PICK/notes.txt" "then Escape: unknown true" \
                "saved as PICK/out.txt" "folder PICK/docs" "modal, Enter on the default button: true" \
                "modal, Escape: false"; do
        grep -qxF "$want" "$work/dlg.raw" || fail "no line \`$want\`"
    done
    if ! grep -q '^|.*\.hidden' "$work/dlg.got" && ! grep -A12 'folders first' "$work/dlg.got" | grep -q 'c\.md'; then
        pass "the hidden file never drawn, and c.md left out by the Text filter"
    else
        fail "the chooser drew a hidden file, or c.md under the Text filter"
    fi
fi

# ------------------------------------------------------------------- EDITOR
printf 'TIER [T] editor: increment 6, a memo and a source by the keys a person presses\n'
if ui_tier_tui; then
    GBASIC_UI=tui GBASIC_TUI_SCREEN=40x20 GBASIC_TUI_DUMP="$work/ed.dump" "$GB" tests/ui_tui/editor.bas \
        </dev/null >"$work/ed.got" 2>&1
    if cmp -s "$work/ed.got" tests/ui_tui/editor.txt; then
        pass "every answer and every screen as tests/ui_tui/editor.txt holds them"
    else
        fail "the editor differs from tests/ui_tui/editor.txt:"
        diff tests/ui_tui/editor.txt "$work/ed.got" | head -14 | sed 's/^/       /'
    fi
    # AND WHAT A GOLDEN CANNOT ARGUE WITH, each worked by hand: 64 letters
    # typed, one change each; Up from column 7 of line 2 onto the wrapped
    # row that starts at column 36 (36 + 6 = 42); the selected line; the
    # margin press; the marked range a line lower after Enter above it
    for want in "caret 1:65, changed 64 times" \
                "Up from 2:7 lands on the wrapped row's column 7: caret 1:42" \
                "Home, Shift+End selects [second] 2:1-2:7" \
                "Ctrl+Z again takes the typing as one step: [The quick brown fox jumps over the lazy dog and keeps on running|]" \
                "Ctrl+Tab moves on to code" "gutter pressed at line 9" \
                "Up onto the short line 8: 8:7" "Down keeps the column it came from: 9:50" \
                "select_text 2:1-3:5 holds [line 2|line], caret 3:5 selecting" \
                "a marked range moves with its text, a line down: 5:1-5:5" \
                "read_only takes no typing: true" "ok pressed"; do
        grep -qxF "$want" "$work/ed.got" || fail "no line \`$want\`"
    done
    # the memo wraps at a word: no row ends inside one
    if grep -qF '|│The quick brown fox jumps over the    │' "$work/ed.got" &&
       grep -qF '|│lazy dog and keeps on running         │' "$work/ed.got"; then
        pass "a memo wraps after the last space that fits, not inside a word"
    else
        fail "the memo did not wrap at the word"
    fi
    cat >"$work/ref.want" <<'EOF'
notes
ui.mark: only a `source` has a margin, and `m` is not one
ui.mark: `source s` does not carry marks; declare it `source s( marks: true )` so the margin reserves its space before the window is shown
ui.caret_to: that line is past the end -- this editor holds 1
ui.select_text: a range ends before it starts -- give the earlier position first
ui.replace_text: `m` is read-only, so its text cannot be replaced as an edit
ui.replace_text: edits 1 and 2 overlap, so the result would depend on their order -- nothing was changed
ab
ui.caret: there is nothing called `notthere` in this window
{"line":1,"column":1,"selecting":false}
ui.gutter_press: `source s` has no such line on screen
EOF
    GBASIC_UI=tui GBASIC_TUI_SCREEN=40x20 "$GB" tests/ui_tui/editor_refusals.bas </dev/null >"$work/ref.got" 2>&1
    if cmp -s "$work/ref.got" "$work/ref.want"; then
        pass "a memo's label as its text outside a form, and the editor's refusals, each Qt's sentence"
    else
        fail "the editor's refusals differ:"
        diff "$work/ref.want" "$work/ref.got" | head -10 | sed 's/^/       /'
    fi
fi

# ----------------------------------------------------------------- REFUSALS
printf 'TIER [T] refusals: each by name\n'
if ui_tier_tui; then
    cat >"$work/later.bas" <<'EOF'
window w( title: "x" )
    column
        source c( language: "gbasic" )
    end column
end window
ui.open(w)
EOF
    got=$(GBASIC_UI=tui GBASIC_TUI_SCREEN=50x12 "$GB" "$work/later.bas" 2>&1)
    case "$got" in
        *"\`highlighting\`"*)
            pass "a source with a language needs \`highlighting\`, refused by its group" ;;
        *) fail "highlighting: $got" ;;
    esac
    printf 'print ui.backend()\n' >"$work/be.bas"
    if [ "$("$GB" --ui=tui "$work/be.bas" 2>&1)" = tui ] && [ "$(GBASIC_UI=tui "$GB" "$work/be.bas" 2>&1)" = tui ]; then
        pass "--ui=tui and GBASIC_UI=tui each choose the terminal by name"
    else
        fail "--ui=tui: $("$GB" --ui=tui "$work/be.bas" 2>&1)"
    fi
    printf 'window w( title: "x" )\n    column\n        text t( label: "hi" )\n    end column\nend window\nui.open(w)\n' >"$work/nt.bas"
    got=$(GBASIC_UI=tui "$GB" "$work/nt.bas" </dev/null 2>&1)
    case "$got" in
        *"no display is available, so window \`w\` cannot be opened: the tui backend needs a terminal, and standard input is not a terminal. Set GBASIC_TUI_SCREEN=80x24 to draw without one"*)
            pass "no terminal: the sentence names what is missing and how to draw without one" ;;
        *) fail "no terminal: $got" ;;
    esac
    printf 'window w( title: "x", needs: ["docking"] )\n    column\n        text t( label: "hi" )\n    end column\nend window\nui.open(w)\n' >"$work/nd.bas"
    got=$(GBASIC_UI=tui GBASIC_TUI_SCREEN=50x12 "$GB" "$work/nd.bas" 2>&1)
    case "$got" in
        *"window \`w\` needs \`docking\`, which the tui backend does not have -- this build's qt has it"*)
            pass "needs: on a backend chosen by name, refused at ui.open naming who has the group" ;;
        *"window \`w\` needs \`docking\`, which the tui backend does not have"*)
            pass "needs: refused at ui.open (no backend in this build has the group)" ;;
        *) fail "needs: $got" ;;
    esac
    printf 'window w( title: "x", needs: ["dockign"] )\n    column\n        text t( label: "hi" )\n    end column\nend window\nprint ui.check(w)\n' >"$work/ng.bas"
    got=$("$GB" "$work/ng.bas" 2>&1)
    case "$got" in
        *"\`needs:\` names \`dockign\`, which is not a capability group -- did you mean \`docking\`?"*)
            pass "needs: a misspelt group refused by ui.check with no display, naming the nearest" ;;
        *) fail "needs misspelt: $got" ;;
    esac
    printf 'window w( title: "x" )\n    row\n        button save( icon: "tests/gpdf/images/alpha.png" )\n        button open( label: "Open", icon: "tests/gpdf/images/alpha.png" )\n    end row\nend window\nprint ui.check(w)\n' >"$work/ic.bas"
    got=$("$GB" "$work/ic.bas" 2>&1)
    if printf '%s' "$got" | grep -q "\`save\` has an icon and no label, so where icons are not drawn (the terminal) it shows only its name -- give it a label.*\[2124\]" \
       && ! printf '%s' "$got" | grep -q "\`open\` has an icon"; then
        pass "warning 2124 for the icon with no label, and not for its labelled neighbour"
    else
        fail "2124: $got"
    fi
fi

# ------------------------------------------------------------------ QTSIZES
printf 'TIER [Q] qtsizes: a size in characters, from Qt'"'"'s font\n'
if ! build_has qt; then
    printf '  SKIP (no Qt in this build -- NOT measured)\n'
elif ui_tier_qt; then
    cat >"$work/qsz.bas" <<'EOF'
window w( title: "Sizes", width: 40 columns )
    column
        field a( min_width: 20 columns )
        field b( min_width: 40 columns )
    end column
end window
ui.open(w)
print ui.read(w, "a", "min_width")
print ui.read(w, "b", "min_width")
on error goto next
ui.set(w, "a", "min_height", 3 columns)
if error then
    print error.message
end if
EOF
    got=$(GBASIC_UI=qt QT_QPA_PLATFORM=offscreen "$GB" "$work/qsz.bas" 2>/dev/null)
    a=$(printf '%s\n' "$got" | sed -n 1p); b=$(printf '%s\n' "$got" | sed -n 2p)
    if [ -n "$a" ] && [ "$a" -gt 0 ] 2>/dev/null && [ "$b" = $((a * 2)) ]; then
        pass "20 columns is $a pixels and 40 columns $b, twice it: a per-character width"
    else
        fail "Qt sizes: $got"
    fi
    if printf '%s' "$got" | grep -q "ui.set: \`min_height:\` is measured down, so it takes rows, not columns"; then
        pass "a height in columns is refused, naming rows"
    else
        fail "wrong axis: $got"
    fi
fi

# --------------------------------------------------------------- LIVE, NEEDS
mkdir -p "$work/livepick"
: >"$work/livepick/a.txt"; : >"$work/livepick/b.txt"
export TUI_PICK="$work/livepick"
for t in live live_parts live_dialogs live_editor needs; do
    printf 'TIER [T] %s: a real terminal\n' "$t"
    if ! ui_tier_tui; then
        continue
    fi
    if ! command -v python3 >/dev/null 2>&1; then
        printf '  SKIP (no python3: nothing drove a pty -- NOT measured)\n'
        continue
    fi
    if [ "$t" = live ] && ! build_has qt; then
        printf '  note: no Qt in this build, so the display half of the rule is not measured\n'
    fi
    out=$(timeout 120 "${TERM_PYTHON:-python3}" -B tests/ui_tui/terminal.py "$t" "$GB" "$PWD/tests/ui_tui" 2>&1); rc=$?
    printf '%s\n' "$out" | sed 's/^PASS /  ok   /; s/^FAIL /  FAIL /; s/^SKIP /  SKIP /'
    if [ $rc != 0 ]; then
        status=1
    fi
done

# ----------------------------------------------------------------- VALGRIND
printf 'TIER [T] valgrind: the drive, parts, dialogs and editor fixtures\n'
if ui_tier_tui; then
    if ! vg_available; then
        printf '  SKIP (no valgrind -- NOT measured)\n'
    elif GBASIC_UI=tui GBASIC_TUI_SCREEN=50x14 vg_run "$GB" tests/ui_tui/drive.bas >/dev/null 2>"$work/vg.log" \
         && GBASIC_UI=tui GBASIC_TUI_SCREEN=60x16 vg_run "$GB" tests/ui_tui/parts.bas >/dev/null 2>>"$work/vg.log" \
         && GBASIC_UI=tui GBASIC_TUI_SCREEN=60x16 GBASIC_TUI_DUMP="$work/vgd.txt" vg_run "$GB" tests/ui_tui/dialogs.bas "$work/pick" </dev/null >/dev/null 2>>"$work/vg.log" \
         && GBASIC_UI=tui GBASIC_TUI_SCREEN=40x20 GBASIC_TUI_DUMP="$work/vge.txt" vg_run "$GB" tests/ui_tui/editor.bas </dev/null >/dev/null 2>>"$work/vg.log" \
         && GBASIC_UI=tui GBASIC_TUI_SCREEN=40x20 vg_run "$GB" tests/ui_tui/editor_refusals.bas </dev/null >/dev/null 2>>"$work/vg.log"; then
        pass "the drive, parts, dialogs and editor fixtures: no invalid access, no definite leak, no descriptor left"
    else
        fail "valgrind:"
        grep -E "Invalid|definitely|ERROR SUMMARY|Open file" "$work/vg.log" | head -8 | sed 's/^/       /'
    fi
fi

if [ $status = 0 ]; then
    printf 'run_ui_tui: all checks passed\n'
fi
exit $status
