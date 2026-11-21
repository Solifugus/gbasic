#!/usr/bin/env bash
# GATE-LANE: serial -- drives real ptys and waits on what the program writes back, which a loaded machine turns into a guess
#
# The terminal's `ui` backend (docs/tui_design.md §7 increment 3,
# src/modules/ui_tui.c): a `window` drawn in cells over src/term.c, the
# selection rule that chooses it, `needs:`, and sizes in characters.
#
# Every other GUI suite has a pass on this backend too (tests/ui_backend.sh);
# this suite is where what is the terminal's alone is measured. THE ORACLES
# ARE OUTSIDE IT: what each key must do, written by hand below; a real pty
# driven by tests/ui_tui/terminal.py, which reads the pty's settings itself
# and, with pyte, the screen; Qt for the sizes; KDE's own KSyntaxHighlighting
# for the colours.
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
#   [T] highlight increment 7: a `source` coloured from a KDE definition,
#                 each cell's colours read from GBASIC_TUI_STYLES and held
#                 to tests/ui_tui/highlight.txt and to colours worked by hand
#                 from KDE's Breeze themes; a block comment across lines,
#                 its opener removed (the line below turns plain) and put
#                 back by undo, and 3000 lines below it.
#   [T] syntax    src/ksyntax.c against KDE's own library, the oracle: every
#                 definition the library carries (extracted by
#                 tests/ui_tui/ksh_probe.cpp) on text made from its own
#                 tokens, gBASIC's tests under every theme, real files in six
#                 languages; each character's format, colour, background and
#                 flags identical. And src/ksyntax_themes.h equal to the
#                 library's themes. Skips without KF6's headers.
#   [T] refusals  a `source` with a language refused for `highlighting`
#                 when the group is withheld;
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
#   [T] valgrind  the drive, parts, dialogs, editor and highlight fixtures.
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
        "syntax error at "*":1:5: a size in columns or rows is "*) pass "refused at its position: $bad" ;;
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
    # A CHOICE IN A FORM IS CAPTIONED, as a field is: increment 7 found it
    # drawn with no caption (run_ui_find's captions tier, first run here)
    cat >"$work/formchoice.bas" <<'EOF'
person = { who: "", licence: "MIT" }
window w( title: "x" )
    column
        form to edit person
            field who( label: "Name" )
            choice licence( label: "Licence", options: ["Apache-2.0", "MIT"] )
        end form
    end column
end window
ui.open(w)
x = ui.read(w, "who", "width")
EOF
    GBASIC_UI=tui GBASIC_TUI_SCREEN=40x6 GBASIC_TUI_DUMP="$work/fc.txt" "$GB" "$work/formchoice.bas" >/dev/null 2>&1
    if grep -q '^Licence  *MIT' "$work/fc.txt"; then
        pass "a choice in a form has its caption in the form's column"
    else
        fail "a choice in a form: $(tr '\n' '|' <"$work/fc.txt")"
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
    # ESCAPE THEN TAB LEAVES FORWARD, where many terminals send Ctrl+Tab as
    # a plain Tab: each line what the keys must do, written by hand (\t a tab)
    printf '%s\n' 'in: notes' 'Tab types a tab: [\t] notes' 'Escape alone stays: notes, text [\t]' \
        'Escape then Tab leaves forward: after, text [\t]' 'Shift+Tab back in: notes' \
        'Escape, a key, then Tab types it: [\ta\t] notes' 'Alt+Tab, a quick Escape then Tab, leaves: after' \
        'a press between forgets the Escape: [\t\ta\t] notes' 'a modal, Escape in its memo: false' |
        sed 's/\\t/\t/g' >"$work/esc.want"
    # bounded: an Escape swallowed leaves the modal unanswered for ever
    GBASIC_UI=tui GBASIC_TUI_SCREEN=40x16 timeout 30 "$GB" tests/ui_tui/escape.bas </dev/null >"$work/esc.got" 2>&1
    if cmp -s "$work/esc.got" "$work/esc.want"; then
        pass "Escape then Tab leaves forward; Escape alone, or a key or a press between, does not; a modal is still rejected"
    else
        fail "Escape then Tab:"
        diff "$work/esc.want" "$work/esc.got" | head -8 | sed 's/^/       /'
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

# ---------------------------------------------------------------- HIGHLIGHT
printf 'TIER [T] highlight: increment 7, a source coloured from a KDE definition\n'
if ui_tier_tui && GBASIC_UI=tui ui_tier_needs highlighting; then
    # the repo's own definitions only: an empty user data directory, and the
    # suite's test definition beside the tree's gBASIC
    mkdir -p "$work/empty-home"
    XDG_DATA_HOME="$work/empty-home" GBASIC_SYNTAX_PATH=tests/ui_tui/syntax GBASIC_UI=tui \
        GBASIC_TUI_SCREEN=40x16 GBASIC_TUI_DUMP="$work/hl.dump" GBASIC_TUI_STYLES="$work/hl.styles" \
        "$GB" tests/ui_tui/highlight.bas </dev/null >"$work/hl.got" 2>&1
    if cmp -s "$work/hl.got" tests/ui_tui/highlight.txt; then
        pass "every colour and every screen as tests/ui_tui/highlight.txt holds them"
    else
        fail "the highlighting differs from tests/ui_tui/highlight.txt:"
        diff tests/ui_tui/highlight.txt "$work/hl.got" | head -14 | sed 's/^/       /'
    fi
    # AND WHAT A GOLDEN CANNOT ARGUE WITH, each colour read by hand from
    # KDE's theme files (fg:bg:attrs, attrs 1 bold): Breeze Light's Keyword
    # #1f1c1b bold, String #bf0303, Comment #898887, DecVal #b08000,
    # Operator #ca60ca on its BackgroundColor #ffffff; Breeze Dark's Comment
    # #7a7c7d and Normal #cfcfc2 on #232629
    for want in "keyword print: 1f1c1b:ffffff:1" 'string "hi": bf0303:ffffff:0' \
                "comment: 898887:ffffff:0" "number 42: b08000:ffffff:0" "operator =: ca60ca:ffffff:0" \
                "colors: #ffffff #1f1c1b" \
                "the comment runs on: two -> 7a7c7d:232629:0" \
                "opener removed: two -> cfcfc2:232629:0" \
                "undone: two -> 7a7c7d:232629:0" \
                "3000 lines down, still in the comment: end -> 7a7c7d:232629:0"; do
        grep -qxF "$want" "$work/hl.got" || fail "no line \`$want\`"
    done
fi

# ------------------------------------------------------------------- SYNTAX
# THE ORACLE IS KDE'S OWN LIBRARY. A C++ probe linked to it extracts every
# definition and theme it carries compiled in and prints, for each line of a
# text, each character's format, colour, background and flags; the engine's
# dump driver prints the same from src/ksyntax.c. Text made from each
# definition's own tokens (tests/ui_tui/ksh_corpus.py, Python's XML parser)
# enters contexts real files rarely do; real files have the shapes text does.
printf 'TIER [T] syntax: the engine against KDE'"'"'s KSyntaxHighlighting, character by character\n'
if ui_tier_tui && GBASIC_UI=tui ui_tier_needs highlighting; then
    cxx=${CXX:-c++}
    ksh_inc=/usr/include/KF6/KSyntaxHighlighting
    if ! command -v python3 >/dev/null 2>&1 || ! command -v "$cxx" >/dev/null 2>&1 ||
       [ ! -d "$ksh_inc" ] || ! pkg-config --exists Qt6Gui libxml-2.0 libpcre2-8 2>/dev/null; then
        printf '  SKIP (needs python3, a C++ compiler, KF6 SyntaxHighlighting headers and Qt6Gui -- the engine was NOT held to KDE'"'"'s)\n'
    elif ! "$cxx" -std=c++17 -fPIC -O1 tests/ui_tui/ksh_probe.cpp -o "$work/ksh_probe" -I"$ksh_inc" \
            $(pkg-config --cflags Qt6Core Qt6Gui) -lKF6SyntaxHighlighting $(pkg-config --libs Qt6Core Qt6Gui) \
            2>"$work/probe.cc"; then
        fail "the KDE probe did not build: $(head -3 "$work/probe.cc")"
    elif ! ${CC:-cc} -std=c11 -O2 -DHAVE_LIBXML2=1 $(pkg-config --cflags libxml-2.0 libpcre2-8) -Iinclude -Isrc \
            src/ksyntax.c tests/ui_tui/ksyntax_dump.c -o "$work/ksyntax_dump" \
            $(pkg-config --libs libxml-2.0 libpcre2-8) 2>"$work/dump.cc"; then
        fail "the engine's dump driver did not build: $(head -3 "$work/dump.cc")"
    else
        kde="$work/kde"
        mkdir -p "$work/empty-home"
        export XDG_DATA_HOME="$work/empty-home"
        "$work/ksh_probe" extract "$kde" >"$work/kde.list"
        grep -vP '^(theme|nofile)\t' "$work/kde.list" >"$work/defs.tsv"
        ndefs=$(wc -l <"$work/defs.tsv")
        # one definition: its own text, both engines, compared
        cat >"$work/one.sh" <<'ONE'
#!/usr/bin/env bash
w=$1; name=$2; file=$3
t=$(mktemp -d "$w/one.XXXXXX")
python3 tests/ui_tui/ksh_corpus.py "$w/kde/syntax/$file" 7 200 >"$t/in.txt"
timeout 60 "$w/ksh_probe" hl "$name" "Breeze Light" "$t/in.txt" >"$t/k" 2>/dev/null
timeout 60 "$w/ksyntax_dump" "$w/kde" "$name" "Breeze Light" "$t/in.txt" >"$t/o" 2>/dev/null
if cmp -s "$t/k" "$t/o"; then echo "SAME	$name"; else echo "DIFF	$name	$(diff "$t/k" "$t/o" | grep '^<' | head -1 | cut -c1-120)"; fi
rm -r "$t"
ONE
        jobs=$(nproc 2>/dev/null || echo 4)
        [ "$jobs" -gt 8 ] && jobs=8
        awk -F'\t' -v w="$work" '{ printf "%s\0%s\0%s\0", w, $1, $2 }' "$work/defs.tsv" |
            xargs -0 -n3 -P"$jobs" bash "$work/one.sh" >"$work/corpus.txt"
        # AND THE TEXTS THAT FOUND THIS ENGINE'S DEFECTS, each (definition,
        # seed, lines) one that differed before its fix (§3.9): the skip cache
        # under captures (Elixir), a dynamic StringDetect clearing nothing
        # (MIB, Elvish), an empty line after a continued one (Gnuplot), a
        # letter among the letterlike symbols (Julia)
        cat >"$work/again.sh" <<'AGAIN'
#!/usr/bin/env bash
w=$1; name=$2; seed=$3; n=$4
file=$(awk -F'\t' -v d="$name" '$1 == d { print $2 }' "$w/defs.tsv")
t=$(mktemp -d "$w/again.XXXXXX")
python3 tests/ui_tui/ksh_corpus.py "$w/kde/syntax/$file" "$seed" "$n" >"$t/in.txt"
timeout 60 "$w/ksh_probe" hl "$name" "Breeze Light" "$t/in.txt" >"$t/k" 2>/dev/null
timeout 60 "$w/ksyntax_dump" "$w/kde" "$name" "Breeze Light" "$t/in.txt" >"$t/o" 2>/dev/null
if [ -s "$t/k" ] && cmp -s "$t/k" "$t/o"; then echo "SAME	$name $seed"; else echo "DIFF	$name $seed"; fi
rm -r "$t"
AGAIN
        printf '%s\0' "$work" Elixir 14 500 "$work" MIB 7 500 "$work" Elvish 7 500 \
                     "$work" Gnuplot 13 400 "$work" Julia 21 500 |
            xargs -0 -n4 -P"$jobs" bash "$work/again.sh" >>"$work/corpus.txt"
        ndefs=$((ndefs + 5))
        same=$(grep -c '^SAME' "$work/corpus.txt")
        if [ "$ndefs" -ge 300 ] && [ "$same" = "$ndefs" ]; then
            pass "every one of KDE's definitions on text made from its own tokens, and the five texts that found defects ($ndefs runs): identical"
        else
            fail "$same of $ndefs definitions identical (want all, and at least 300):"
            grep -v '^SAME' "$work/corpus.txt" | head -6 | sed 's/^/       /'
        fi
        # gBASIC, the definition this repo ships, under every theme KDE has
        mkdir -p "$work/gb/syntax"
        cp editors/kate/gbasic.xml "$work/gb/syntax/"
        bad=""
        nthemes=0
        while IFS=$'\t' read -r _ theme _; do
            nthemes=$((nthemes + 1))
            KSH_PATH="$work/gb" "$work/ksh_probe" hl gBASIC "$theme" tests/*.bas >"$work/gk" 2>/dev/null
            "$work/ksyntax_dump" "$work/gb" gBASIC "$theme" tests/*.bas >"$work/go" 2>/dev/null
            cmp -s "$work/gk" "$work/go" || bad="$bad $theme;"
        done < <(grep -P '^theme\t' "$work/kde.list")
        lines=$(grep -c '^L' "$work/go")
        if [ -z "$bad" ] && [ "$nthemes" -ge 20 ] && [ "$lines" -ge 10000 ]; then
            pass "gBASIC's own tests ($lines lines) under each of KDE's $nthemes themes: identical"
        else
            fail "gBASIC under $nthemes themes differs for:$bad"
        fi
        # real files, whose shapes made text does not have: long lines among
        # them (Markdown's 12,000-character ones needed Qt's JIT stack)
        for spec in "C:src/ksyntax.c src/term.c src/modules/ui_tui.c" "Bash:tests/run_ui_tui.sh tests/ui_backend.sh" \
                    "Python:tests/ui_tui/terminal.py tools/make_ksyntax_themes.py" "Markdown:docs/tui_design.md docs/test_suite_history.md" \
                    "XML:editors/kate/gbasic.xml" "Makefile:Makefile"; do
            lang=${spec%%:*}
            # shellcheck disable=SC2086
            "$work/ksh_probe" hl "$lang" "Breeze Dark" ${spec#*:} >"$work/rk" 2>/dev/null
            # shellcheck disable=SC2086
            "$work/ksyntax_dump" "$kde" "$lang" "Breeze Dark" ${spec#*:} >"$work/ro" 2>/dev/null
            if [ -s "$work/rk" ] && cmp -s "$work/rk" "$work/ro"; then
                pass "$lang, real files ($(grep -c '^L' "$work/rk") lines): identical"
            else
                fail "$lang, real files: $(diff "$work/rk" "$work/ro" | grep -c '^<') lines differ"
            fi
        done
        # the themes this binary carries, held to the library's
        python3 tools/make_ksyntax_themes.py "$kde" >"$work/themes.h"
        if cmp -s "$work/themes.h" src/ksyntax_themes.h; then
            pass "src/ksyntax_themes.h is the library's $nthemes themes, packed"
        else
            fail "src/ksyntax_themes.h is not this library's themes: regenerate it (tools/make_ksyntax_themes.py, from ksh_probe extract)"
        fi
        unset XDG_DATA_HOME
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
    # the group withheld, as on a build without libxml2
    got=$(GBASIC_UI_WITHHOLD=highlighting GBASIC_UI=tui GBASIC_TUI_SCREEN=50x12 "$GB" "$work/later.bas" 2>&1)
    case "$got" in
        *"\`highlighting\`"*)
            pass "a source with a language needs \`highlighting\`, refused by its group when it is withheld" ;;
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
printf 'TIER [T] valgrind: the drive, parts, dialogs, editor and highlight fixtures\n'
if ui_tier_tui; then
    if ! vg_available; then
        printf '  SKIP (no valgrind -- NOT measured)\n'
    elif GBASIC_UI=tui GBASIC_TUI_SCREEN=50x14 vg_run "$GB" tests/ui_tui/drive.bas >/dev/null 2>"$work/vg.log" \
         && GBASIC_UI=tui GBASIC_TUI_SCREEN=60x16 vg_run "$GB" tests/ui_tui/parts.bas >/dev/null 2>>"$work/vg.log" \
         && GBASIC_UI=tui GBASIC_TUI_SCREEN=60x16 GBASIC_TUI_DUMP="$work/vgd.txt" vg_run "$GB" tests/ui_tui/dialogs.bas "$work/pick" </dev/null >/dev/null 2>>"$work/vg.log" \
         && GBASIC_UI=tui GBASIC_TUI_SCREEN=40x20 GBASIC_TUI_DUMP="$work/vge.txt" vg_run "$GB" tests/ui_tui/editor.bas </dev/null >/dev/null 2>>"$work/vg.log" \
         && GBASIC_UI=tui GBASIC_TUI_SCREEN=40x20 vg_run "$GB" tests/ui_tui/editor_refusals.bas </dev/null >/dev/null 2>>"$work/vg.log" \
         && QT_ENABLE_REGEXP_JIT=0 XDG_DATA_HOME="$work/empty-home" GBASIC_SYNTAX_PATH=tests/ui_tui/syntax GBASIC_UI=tui GBASIC_TUI_SCREEN=40x16 \
            GBASIC_TUI_DUMP="$work/vgh.dump" GBASIC_TUI_STYLES="$work/vgh.styles" \
            vg_run "$GB" tests/ui_tui/highlight.bas </dev/null >/dev/null 2>>"$work/vg.log"; then
        pass "the drive, parts, dialogs, editor and highlight fixtures: no invalid access, no definite leak, no descriptor left"
    else
        fail "valgrind:"
        grep -E "Invalid|definitely|ERROR SUMMARY|Open file" "$work/vg.log" | head -8 | sed 's/^/       /'
    fi
fi

if [ $status = 0 ]; then
    printf 'run_ui_tui: all checks passed\n'
fi
exit $status
