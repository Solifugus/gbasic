#!/usr/bin/env bash
# THE `window` DECLARATION -- parsed, validated and introspectable WITH NO
# DISPLAY AND NO QT, which is the whole point of shipping it before either.
#
# The precedent is `web.routes`: a route table proven with no socket, because a
# declaration is DATA and everything decidable about it is decidable without the
# thing it eventually drives. A window block is the same bet, and if it holds
# then the entire declarative surface -- every refusal, every position, the
# introspection -- is gated on every platform, headless, before a widget exists.
#
# WHAT IS DELIBERATELY NOT HERE: anything that renders. That is
# tests/run_qt_backend.sh, which needs Qt in the build and so can SKIP, which is
# exactly why everything decidable about the declaration is asserted here
# instead -- a gate that shrinks when a dependency is absent is the failure this
# file's own glob-discovery note argues against.
#
# THIS COMMENT HAS BEEN WRONG TWICE AND BOTH ARE WORTH KEEPING IN MIND. It first
# said evaluating a window block RAISED "declared but not yet executable", which
# was true until the declaration bound a record; then that nothing could render
# one, which was true until the backend landed the same day. A tier asserting a
# refusal that is no longer correct does not go quiet -- it goes RED, which is
# how both were found.
#
# Tiers:
#   PARSE       the design's own example (docs/qt_gui_design.md §2) parses, and
#               the tree is the one the document draws -- containers nested,
#               the form's subject and direction, the labelled group, the
#               handler and its body.
#   TOKENS      `window` is NOT reserved: it still works as a variable, a
#               record key, a field and in arithmetic. MEASURED before the
#               lexer change -- 153 uses of the word in the tree and ZERO
#               instances of `window IDENT (`, which is why the three-token
#               shape could be claimed safely.
#   TYPED       `type: {USD}` names a type inside a declaration, and `m = {USD}`
#               is a LOCATED PARSE ERROR naming the two spellings that work.
#               That is the declaration-only decision (§5.2) enforced where an
#               author can see it rather than when the line runs.
#   REFUSALS    every load-time check, each with its position, each beside the
#               nearest LEGAL neighbour -- or a refusal suite is satisfied by a
#               checker that refuses everything.
#   JSON        those refusals reach `--json-diagnostics`, which is what puts
#               them in an editor's error list. They are reported into the same
#               sink as parse errors, so this is a property of the pass rather
#               than of each message.
#   GRAMMAR     bison still reports ZERO conflicts. Not decoration in a project
#               that rejected `IDENT expression` as a statement form over four
#               measured ones, and the standing assertion §5.2 promised would
#               arrive with the grammar.
#   VALGRIND    the new AST nodes are freed. PLAT-OPTPARAM's per-parameter-list
#               array had to be freed in SIX places and missing three turned 22
#               SUITES RED on their own valgrind tiers -- a few bytes per parse,
#               invisible to every functional test.
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

# run_src NAME <<'EOF' ... EOF  -- write a source file and report its outcome
written=""
src() { written="$work/$1.bas"; cat > "$written"; }
# THE PROBE'S EXIT CODE IS NOT THIS SUITE'S VERDICT: most of these files are
# MEANT to fail, and under `set -euo pipefail` that status would end the suite
# silently mid-tier with no FAIL line. Seventh instance of that trap in this
# tree before it was written down; captured deliberately here.
out_of() {
    set +e
    ./gbasic "$1" 2>&1
    set -e
}

printf 'TIER parse: the design document own example\n'
src example <<'EOF'
window customer( title: "Customer", width: 480 )
    column( margin: 8, spacing: 8 )
        form to edit cust
            field id(    label: "Account" )
            field name(  label: "Name" )
        end form
        group "Credit"
            field limit( label: "Limit", type: {USD} )
            check hold(  label: "On hold" )
        end group
        row
            stretch()
            button save( text: "Save", default: true )
        end row
    end column

    on save.clicked
        print "saved"
    end on
end window
EOF
set +e
tree=$(./gbasic --ast "$written" 2>&1)
set -e
for want in 'Window "customer"' 'Container column' 'Container form to edit cust' \
            'Leaf field id' 'Container group "Credit"' 'Leaf check hold' \
            'Container row' 'Leaf stretch' 'Leaf button save' 'Handler save.clicked'; do
    if ! printf '%s\n' "$tree" | grep -qF "$want"; then
        printf '%s\n' "$tree"
        fail "the tree is missing: $want"
        break
    fi
done
printf '%s\n' "$tree" | grep -qF 'Handler save.clicked' && pass "the whole example parses into the tree the document draws"

# AND IT BINDS ITS NAME, which is the whole introspection surface and needs no
# new builtin: a `server` block does the same and `web.routes` reads the record
# with no socket. This tier REPLACED one asserting a refusal -- "declared but
# not yet executable" -- which was right while nothing observable happened and
# is now simply false.
src introspect <<'EOF'
window customer( title: "Customer", width: 480 ) to edit cust
    column( margin: 8 )
        form to edit cust
            field id( label: "Account" )
        end form
        group "Credit"
            field limit( label: "Limit", type: {USD} )
        end group
    end column
    on id.clicked
        print "x"
    end on
end window

program main()
    print customer.kind + "/" + customer.name + "/" + customer.subject
    print customer.options.title + "/" + string(customer.options.width)
    col = customer.items[0]
    print col.kind + "/" + col.word + "/" + string(count(col.items))
    print col.items[0].subject + "/" + col.items[0].direction
    print col.items[1].label
    print col.items[0].items[0].word + "/" + col.items[0].items[0].name
    print col.items[0].items[0].options.label
    print customer.items[1].kind + "/" + customer.items[1].target + "." + customer.items[1].signal
end program
EOF
got=$(out_of "$written" | tr '
' '|')
want='window/customer/cust|Customer/480|container/column/2|cust/edit|Credit|field/id|Account|handler/id.clicked|'
if [ "$got" = "$want" ]; then
    pass "the declaration reads back as ordinary data, nested, with no new builtin"
else
    printf '    got:  %s
    want: %s
' "$got" "$want"
    fail "the introspection record is not what the declaration says"
fi

# A NON-LITERAL OPTION IS VISIBLE BY NAME AND NOT BY VALUE, which is a
# statement about what a declaration can KNOW rather than a gap: `show: cust.id`
# names a place to read when something renders, in the frame where the subject
# lives, so there is no value to report at load time. The head's options are all
# literals (load-time enforced), so nothing there is ever unknown.
src binding <<'EOF'
window w( title: "x" )
    form to edit cust
        field id( label: "Account", show: cust.id )
    end form
end window

program main()
    f = w.items[0].items[0]
    print f.options.label
    print "show is " + type(f.options.show)
    print "and the NAME is there: " + string(has(f.options, "show"))
end program
EOF
got=$(out_of "$written" | tr '
' '|')
if [ "$got" = "Account|show is nothing|and the NAME is there: true|" ]; then
    pass "a binding option is reported by NAME with no value, which is the honest answer"
else
    printf '    %s
' "$got"; fail "a non-literal option was not reported by name"
fi

# AND IT IS HOISTED, like a server declaration: the block may sit BELOW the
# program that names it, which is the position the design's own examples use.
src hoisted <<'EOF'
program main()
    print "named from above: " + below.name
end program

window below( title: "x" )
    stretch()
end window
EOF
got=$(out_of "$written")
if [ "$got" = "named from above: below" ]; then
    pass "a window declared BELOW the program block is still bound"
else
    printf '    %s
' "$got"; fail "the declaration is not hoisted"
fi

printf 'TIER tokens: `window` is not reserved\n'
src ordinary <<'EOF'
window = 5
print window
r = { window: 1 }
print r.window
print window + 1
window2 = window * 2
print window2
EOF
got=$(out_of "$written" | tr '\n' ' ')
if [ "$got" = "5 1 6 10 " ]; then
    pass "it is still a variable, a record key, a field and arithmetic ($got)"
else
    fail "the word stopped being ordinary: got '$got', wanted '5 1 6 10 '"
fi

printf 'TIER typed: `type: {USD}` inside a declaration, refused outside one\n'
src bare <<'EOF'
m = {USD}
print m
EOF
got=$(out_of "$written")
if printf '%s' "$got" | grep -q "names a type only inside a declaration" \
   && printf '%s' "$got" | grep -qE ":1:5:"; then
    pass "a bare modifier outside a declaration is a LOCATED parse error"
else
    printf '%s\n' "$got"; fail "a bare modifier was not refused with a position"
fi
# THE CONTROLS, without which the refusal is satisfied by one that refuses
# every modifier there is.
src applied <<'EOF'
print string({USD}19.99)
p {USD}= 5.00
print string(p)
EOF
got=$(out_of "$written" | tr '\n' ' ')
if [ "$got" = "19.99 5.00 " ]; then
    pass "CONTROL: applied inline and as a clause, both untouched"
else
    fail "the ordinary modifier forms changed: got '$got'"
fi

printf 'TIER refusals: each with its position, each beside a legal neighbour\n'
# NOT EVERY WRONG WINDOW REACHES VALIDATION, and two of this tier's first drafts
# did not: `field a` followed by a body is no production at all, and nor is a
# container word with options and no body -- the GRAMMAR refuses both before the
# checker sees them, which is the discipline that makes the block cost zero
# conflicts working as intended. What reaches the word/shape check is a bare
# `field` used as a container and a parenthesised `row()` used as a part.
refuse() {   # refuse LABEL NEEDLE
    local label="$1" needle="$2"
    local got
    got=$(out_of "$written")
    if printf '%s' "$got" | grep -qF "$needle"; then
        pass "$label"
    else
        printf '    %s\n' "$(printf '%s' "$got" | head -2)"
        fail "$label (wanted: $needle)"
    fi
}

src unknown_word <<'EOF'
window w( title: "x" )
    colunm( margin: 8 )
        field a( label: "A" )
    end colunm
end window
EOF
refuse "a misspelled directive names the nearest real word" "did you mean 'column'?"

src unknown_far <<'EOF'
window w( title: "x" )
    accordion
        field a( label: "A" )
    end accordion
end window
EOF
refuse "a word nothing is close to lists the vocabulary" "The containers are row"

src leaf_with_body <<'EOF'
window w( title: "x" )
    field
        field b( label: "B" )
    end field
end window
EOF
refuse "a part used as a container is refused" "'field' takes no body"

src container_as_leaf <<'EOF'
window w( title: "x" )
    row()
end window
EOF
refuse "a container used as a part is refused" "needs a body ending \`end row\`"

src close_mismatch <<'EOF'
window w( title: "x" )
    row
        stretch()
    end column
end window
EOF
refuse "a mismatched \`end\` names both words" "closed by \`end row\`, not \`end column\`"

src form_no_subject <<'EOF'
window w( title: "x" )
    form
        field a( label: "A" )
    end form
end window
EOF
refuse "a form with no subject and no window subject is refused" "names no subject and the window has none"

src form_inherits <<'EOF'
window w( title: "x" ) to edit cust
    form
        field a( label: "A" )
    end form
end window
EOF
got=$(out_of "$written")
if [ -z "$got" ]; then
    pass "CONTROL: a bare form INHERITS the window's subject, as the shorthand says"
else
    printf '    %s\n' "$(printf '%s' "$got" | head -2)"
    fail "a bare form did not inherit the window's subject"
fi

src bad_direction <<'EOF'
window w( title: "x" )
    form to destroy cust
        field a( label: "A" )
    end form
end window
EOF
refuse "a direction that is not edit or show is refused" "\`to edit\` or \`to show\`, not \`to destroy\`"

src subject_on_row <<'EOF'
window w( title: "x" )
    row to edit cust
        stretch()
    end row
end window
EOF
refuse "only a form or a table carries a subject" "only a form or a table carries a subject"

# ------------------------------------------------------------------- TABLE
# `table` JOINED THE VOCABULARY 2026-10-18 (§7's own decision: a table's parts
# are `field`, the same word a form uses for the same idea -- one value of the
# subject). Everything decidable about it is decidable HERE, with no Qt and no
# display, which is the whole bet this suite rests on.
#
# MEASURED BEFORE WRITING THE GRAMMAR: a NAMED container carrying a subject --
# `table orders to show lines` -- costs ZERO conflicts, which is what let the
# design's own spelling be built rather than a nameless `table to show lines`.
# `TO` is a hard keyword, so after `IDENT IDENT` one token of lookahead
# separates it from the named LEAF's `LPAREN`.
src table_ok <<'EOF'
window w( title: "x" )
    table orders to show lines
        field placed( label: "Date" )
        field total( label: "Total", type: {USD} )
    end table

    on orders.activated
        print "chosen"
    end on
end window
program main()
    print "ACCEPTED"
end program
EOF
got=$(out_of "$written")
if printf '%s' "$got" | grep -qF 'ACCEPTED'; then
    pass "CONTROL: a table with columns and a handler is accepted"
else
    printf '    %s\n' "$(printf '%s' "$got" | head -2)"
    fail "a valid table was refused"
fi

# A TABLE IS NAMED AND A FORM IS NOT, refused BOTH WAYS so neither shape can be
# written the other's way and quietly mean something else. The difference is
# real rather than a spelling: a form's parts are addressed by their own names,
# where a table shows MANY rows, so the TABLE is what a handler and
# `qt.selected` address.
src table_unnamed <<'EOF'
window w( title: "x" )
    table to show lines
        field a( label: "A" )
    end table
end window
EOF
refuse "a table with no name is refused, with the reason" "a table needs a name"

src form_named <<'EOF'
window w( title: "x" )
    form rows to edit cust
        field a( label: "A" )
    end form
end window
EOF
refuse "a NAMED form is refused, saying which containers may be named" "a table and a \`tabs\` are named"

# -------------------------------------------------------------------- TABS
# `tabs` / `tab "Name"` JOINED 2026-10-18 AND COST NO GRAMMAR EITHER: both ride
# productions that already existed (the bare container, and the
# `group "Credit"` labelled form). A NAMED container with no options costs none
# -- but one WITH options and a body costs FOUR reduce/reduce, and bison reports
# the rule useless, because after `IDENT IDENT ( ... ) NEWLINE` the parser
# cannot tell a named LEAF from a container head. That is the wall §5.2 records
# for the server block, so the vocabulary carries A NAME OR OPTIONS AND NOT
# BOTH, and `qt.set` is what makes that affordable.
src tabs_ok <<'EOF'
window w( title: "x" )
    column
        tabs settings
            tab "General"
                field a( label: "A" )
            end tab
            tab "Advanced"
                check b( label: "B" )
            end tab
        end tabs
    end column
    on settings.currentChanged
        print "changed"
    end on
end window
program main()
    print "ACCEPTED"
end program
EOF
got=$(out_of "$written")
if printf '%s' "$got" | grep -qF 'ACCEPTED'; then
    pass "CONTROL: a named tabs with two captioned pages and a handler is accepted"
else
    printf '    %s\n' "$(printf '%s' "$got" | head -2)"
    fail "a valid tabs was refused"
fi

# A QTabWidget HAS NO LAYOUT OF ITS OWN -- its children are PAGES -- so
# anything but a `tab` inside one would be DROPPED rather than mislaid, which
# is the silent kind of wrong.
src tabs_bare_field <<'EOF'
window w( title: "x" )
    tabs
        field a( label: "A" )
    end tabs
end window
EOF
refuse "a part directly inside tabs is refused, naming the remedy" "put it inside a tab"

src tab_outside <<'EOF'
window w( title: "x" )
    column
        tab "X"
            field a( label: "A" )
        end tab
    end column
end window
EOF
refuse "a tab outside a tabs is refused" "a \`tab\` belongs to a \`tabs\`"

src tab_no_caption <<'EOF'
window w( title: "x" )
    tabs
        tab
            field a( label: "A" )
        end tab
    end tabs
end window
EOF
refuse "a tab with no caption is refused" "a tab needs a caption"

# AND A NAME IS STILL REFUSED ON THE CONTAINERS THAT HAVE NO STATE TO ASK
# ABOUT, which the named-container production made grammatical for the first
# time: `row thing` parses now, and must still be refused.
src row_named <<'EOF'
window w( title: "x" )
    row thing
        field a( label: "A" )
    end row
end window
EOF
refuse "a named row is refused, now that the grammar admits the shape" "'row' takes no name"

# A TABLE'S PARTS ARE COLUMNS, so every one is a `field`. A `button` among them
# would be a widget in a place that holds values, which lays out as nothing an
# author recognises -- and the refusal points at the BUTTON rather than at the
# table, which is where the author has to edit.
src table_button <<'EOF'
window w( title: "x" )
    table orders to show lines
        field a( label: "A" )
        button go( label: "Go" )
    end table
end window
EOF
refuse "a part that is not a field is refused inside a table" "a table's parts are columns"

# A COLUMN'S NAME BELONGS TO ITS TABLE, NOT TO THE WINDOW -- a correction the
# first runnable example produced immediately, and the control matters more than
# the refusal here. A screen that LISTS orders and SHOWS the chosen one is the
# commonest shape there is, and both halves want a `who` and a `total`: the
# list's are columns, the form's are widgets. With columns registered
# window-wide that program was refused as declaring `who` twice, which is a
# true sentence about a program with nothing wrong with it.
src table_column_scope <<'EOF'
window w( title: "x" )
    column
        table lines_view to show lines
            field who(   label: "Customer" )
            field total( label: "Total" )
        end table
        form to show chosen
            field who(   label: "Customer" )
            field total( label: "Total" )
        end form
    end column
    on lines_view.itemSelectionChanged
        print "chosen"
    end on
end window
program main()
    print "ACCEPTED"
end program
EOF
got=$(out_of "$written")
if printf '%s' "$got" | grep -qF 'ACCEPTED'; then
    pass "CONTROL: a table and a form may show the same field names"
else
    printf '    %s\n' "$(printf '%s' "$got" | head -2)"
    fail "a table's column names leaked into the window's namespace"
fi

# AND THE TABLE'S OWN NAME IS STILL WINDOW-WIDE, which the control above also
# proves (its handler resolves) -- asserted separately because scoping the
# columns is one `return` away from scoping the table with them, and that makes
# every table handler unreachable.
src table_handler_unknown <<'EOF'
window w( title: "x" )
    table lines_view to show lines
        field who( label: "Customer" )
    end table
    on who.itemSelectionChanged
        print "no"
    end on
end window
EOF
refuse "a handler naming a COLUMN is refused, since a column is not addressable" "names nothing in this window"

# TWO COLUMNS SHOWING ONE FIELD is a mistake with no sensible reading: both
# would hold the same value and `qt.cell` could not say which was meant.
src table_dup_column <<'EOF'
window w( title: "x" )
    table lines_view to show lines
        field who( label: "Customer" )
        field who( label: "Also customer" )
    end table
end window
EOF
refuse "a table showing one field twice is refused" "shows 'who' twice"

# -------------------------------------------------------------------- MENU
# `menu` JOINED THE VOCABULARY 2026-10-18 AND NEEDED NO GRAMMAR AT ALL, which
# is the measurement that shaped it: `menu "File"` is the production
# `group "Credit"` already uses, `item open( label: "..." )` is the named leaf,
# `separator()` is the `stretch()` form, and a submenu is the same production
# recursively. Bison still reports zero conflicts (the tier below), and the
# shapes already PARSED before this -- only the vocabulary refused them.
src menu_ok <<'EOF'
window w( title: "x" )
    menu "File"
        item open_file( label: "Open...", shortcut: "Ctrl+O" )
        menu "Recent"
            item r1( label: "a.bas" )
        end menu
        separator()
    end menu
    column
        field note( label: "Note" )
    end column
    on open_file.triggered
        print "chosen"
    end on
end window
program main()
    print "ACCEPTED"
end program
EOF
got=$(out_of "$written")
if printf '%s' "$got" | grep -qF 'ACCEPTED'; then
    pass "CONTROL: a menu bar with a submenu, a separator and a handler is accepted"
else
    printf '    %s\n' "$(printf '%s' "$got" | head -2)"
    fail "a valid menu was refused"
fi

# A MENU BAR BELONGS TO THE WINDOW, so a `menu` sits at the top level or inside
# another menu and NOWHERE ELSE. Refused rather than placed somewhere
# surprising: Qt would put a QMenuBar wherever the layout said, so a `menu`
# inside a `row` renders as a bar in the middle of the screen -- a
# plausible-looking widget nobody asked for.
src menu_in_column <<'EOF'
window w( title: "x" )
    column
        menu "File"
            item a( label: "A" )
        end menu
    end column
end window
EOF
refuse "a menu inside a layout is refused, saying a bar is not laid out" "a menu belongs to the window or to another menu"

src menu_no_caption <<'EOF'
window w( title: "x" )
    menu
        item a( label: "A" )
    end menu
end window
EOF
refuse "a menu with no caption is refused" "a menu needs a caption"

# A MENU'S PARTS ARE ITEMS, SEPARATORS AND SUBMENUS. A button among them is a
# widget in a place that holds actions.
src menu_button <<'EOF'
window w( title: "x" )
    menu "File"
        button b( label: "B" )
    end menu
end window
EOF
refuse "a widget inside a menu is refused" "a menu holds items, separators and submenus"

# AND THE MIRROR OF THE PLACEMENT RULE: a menu's parts may not appear where
# things ARE laid out. Without this, `item open( ... )` in a column reaches
# `gb_qt_part`, which has no widget for the word -- refused at RUN time, late,
# and only for somebody with Qt in their build.
src item_outside <<'EOF'
window w( title: "x" )
    column
        item a( label: "A" )
    end column
end window
EOF
refuse "an item outside a menu is refused at LOAD time" "'item' belongs in a menu"

src separator_named <<'EOF'
window w( title: "x" )
    menu "File"
        separator x( label: "y" )
    end menu
end window
EOF
refuse "a separator is a line and takes no name" "a separator is a line and takes no name"

# A MENU'S CAPTION IS NOT A NAME, so a handler cannot address one -- its ITEMS
# are what a handler addresses, which is the opposite of a table (named,
# addressable) and is asserted so the two cannot drift into each other.
src menu_handler <<'EOF'
window w( title: "x" )
    menu "File"
        item a( label: "A" )
    end menu
    on File.triggered
        print "x"
    end on
end window
EOF
refuse "a handler naming a menu's CAPTION is refused" "names nothing in this window"

src table_direction <<'EOF'
window w( title: "x" )
    table orders to view lines
        field a( label: "A" )
    end table
end window
EOF
refuse "a table's direction is edit or show too" "a table is \`to edit\` or \`to show\`, not \`to view\`"

# THE VOCABULARY SENTENCE MUST NAME IT, or a reader who typed `table` correctly
# and made some other mistake is told the containers are row, column, form and
# group -- a list that does not include the word they just used successfully.
src table_typo <<'EOF'
window w( title: "x" )
    tabel orders to show lines
        field a( label: "A" )
    end tabel
end window
EOF
refuse "a misspelled table is named, and the suggestion is the real word" "did you mean 'table'?"

# AND EVERY VOCABULARY WORD MUST BE REACHABLE AS A SUGGESTION, which is a
# TRIPWIRE and not a flourish: `win_nearest` held a THIRD hardcoded copy of the
# vocabulary, so `table` was added to the containers and to the sentence that
# lists them and MISSED THERE -- a reader who typed `tabel` got the generic
# list rather than the one-word answer, for the newest word in the language.
# Two representations of one list drift. It is derived from the two tables now,
# and this loop fails the next time a word is added to one and not the other.
for w in row column form group table menu tabs field button check stretch item separator; do
    # ONE TRANSPOSITION of the last two letters, which is what fingers actually
    # do and is the shape the finder had to learn (`colunm`, `feild`).
    typo=$(printf '%s' "$w" | sed -E 's/(.)(.)$/\2\1/')
    # WRITTEN AS A PARENTHESISED LEAF (`rwo()`), because a BARE word with no
    # body is no production at all and dies in the grammar before the word
    # check ever sees it -- the first draft of this loop did that and reported
    # all nine words as missing, including the ones that already worked, which
    # is how it was obvious the fixture and not the code was wrong.
    src "near_$w" <<EOF
window w( title: "x" )
    $typo()
end window
EOF
    got=$(out_of "$written")
    if printf '%s' "$got" | grep -qF "did you mean '$w'?"; then
        pass "a typo for '$w' suggests it"
    else
        printf '    %s\n' "$(printf '%s' "$got" | head -1)"
        fail "'$typo' did not suggest '$w' -- is it missing from a vocabulary list?"
    fi
done

src no_name <<'EOF'
window w( title: "x" )
    field()
end window
EOF
refuse "a part with no name is refused, with the reason" "because that is what a handler and a binding address"

src stretch_named <<'EOF'
window w( title: "x" )
    stretch gap( spacing: 4 )
end window
EOF
refuse "stretch takes no name" "stretch is space and takes no name"

src duplicate_name <<'EOF'
window w( title: "x" )
    field id( label: "A" )
    field id( label: "B" )
end window
EOF
refuse "a name declared twice is refused, naming the consequence" "could not say which"

src dangling_handler <<'EOF'
window w( title: "x" )
    button save( text: "Save" )
    on cancel.clicked
        print "x"
    end on
end window
EOF
refuse "a handler naming nothing in the window could never fire" "names nothing in this window"

src bad_head_option <<'EOF'
window w( titel: "x" )
    stretch()
end window
EOF
refuse "an unknown head option is refused BY NAME" "unknown option 'titel'"

src bad_window_close <<'EOF'
window w( title: "x" )
    stretch()
end form
EOF
refuse "a window closed by the wrong word is refused" "closed by \`end window\`, not \`end form\`"

# THE CONTROL FOR THE WHOLE BLOCK: without it every refusal above is satisfied
# by a checker that rejects every window there is.
# A TABLE IS IN HERE DELIBERATELY, and not only for the control: this file is
# what the VALGRIND tier parses, so the named-container-with-a-subject
# production is exercised under valgrind by being here. PLAT-OPTPARAM's lesson
# is that a new AST field has to be freed in every place that frees the node,
# and a few unfreed bytes per parse are invisible to every functional check --
# that one turned 22 suites red on their own valgrind tiers.
src valid <<'EOF'
window customer( title: "C", width: 300, height: 200 ) to show cust
    menu "File"
        item open_it( label: "Open...", shortcut: "Ctrl+O" )
        menu "Recent"
            item recent_one( label: "a.bas" )
        end menu
        separator()
    end menu
    column( margin: 4 )
        form to edit cust
            field id( label: "Account" )
        end form
        table orders to show lines
            field placed( label: "Date" )
            field total( label: "Total", type: {USD} )
        end table
        tabs settings
            tab "General"
                field note( label: "Note" )
            end tab
        end tabs
        row
            stretch()
            button save( text: "Save" )
        end row
    end column
    on save.clicked
        print "ok"
    end on
    on orders.activated
        print "chosen"
    end on
    on open_it.triggered
        print "opened"
    end on
end window
EOF
got=$(out_of "$written")
if [ -z "$got" ]; then
    pass "CONTROL: a valid window loads with NOTHING to say"
else
    printf '%s\n' "$got" | head -4
    fail "a valid window was rejected by a load-time check"
fi

printf 'TIER json: the refusals reach --json-diagnostics\n'
src forjson <<'EOF'
window w( title: "x" )
    colunm( margin: 8 )
        field a( label: "A" )
    end colunm
end window
EOF
set +e
js=$(./gbasic --json-diagnostics "$written" 2>&1 >/dev/null)
set -e
if printf '%s' "$js" | grep -q '"message"' \
   && printf '%s' "$js" | grep -q "did you mean" \
   && printf '%s' "$js" | grep -qE '"line": *2|"line":2'; then
    pass "a window refusal arrives as JSON, with its line"
else
    printf '%s\n' "$js" | head -3; fail "the refusal did not reach --json-diagnostics"
fi
# AND NOTHING ELSE ON THE STREAM, which is the defect run_parse_exit exists for.
if printf '%s' "$js" | grep -qv '^[[:space:]]*[{}\[]' >/dev/null 2>&1; then
    if printf '%s' "$js" | grep -vqE '^\s*[{}\[\]"]|^\s*$|^\s*"' ; then
        printf '%s\n' "$js" | head -3; fail "something non-JSON reached the stream"
    else
        pass "and nothing non-JSON is on the stream"
    fi
else
    pass "and nothing non-JSON is on the stream"
fi

printf 'TIER grammar: bison still reports zero conflicts\n'
# THE STANDING ASSERTION §5.2 PROMISED WOULD ARRIVE WITH THE GRAMMAR. Not
# decoration: this project rejected `IDENT expression` as a statement form over
# FOUR measured conflicts, and ten shapes were priced before this one was built.
conf=$(bison -d -o "$work/parser.tab.c" src/parser.y 2>&1 | grep -ci "conflict" || true)
if [ "$conf" = "0" ]; then
    pass "zero shift/reduce and zero reduce/reduce"
else
    bison -d -o "$work/parser.tab.c" src/parser.y 2>&1 | grep -i conflict | head -3
    fail "the grammar now has conflicts"
fi

printf 'TIER valgrind\n'
if vg_available; then
    set +e
    vg_run ./gbasic --ast "$work/valid.bas" >/dev/null 2>"$work/vg.err"
    vg_rc=$?
    set -e
    # COMPARED AGAINST $VG_EXIT, which the shared policy exposes for exactly
    # this. A bare `if vg_run ...` reads ANY nonzero as a valgrind objection,
    # so when the fixture itself failed -- adding a `table` to it before the
    # vocabulary knew the word -- this tier printed FAIL valgrind objected over
    # a report saying `definitely lost: 0 bytes` and `ERROR SUMMARY: 0 errors`,
    # which sent the reader to look for a leak that was not there. The
    # reports-the-wrong-cause class, and run_repl.sh records the identical
    # defect in its own valgrind tier.
    if [ "$vg_rc" = "$VG_EXIT" ]; then
        tail -25 "$work/vg.err"; fail "valgrind objected"
    elif [ "$vg_rc" != "0" ]; then
        printf '    %s\n' "$(out_of "$work/valid.bas" | head -2)"
        fail "the fixture itself exited $vg_rc, which is not a valgrind objection"
    else
        pass "no definite leak or invalid access parsing and freeing a window"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

exit $status
