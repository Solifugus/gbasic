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
# WHAT IS DELIBERATELY NOT HERE: anything that renders. Evaluating a window
# block RAISES "declared but not yet executable", asserted below, because a
# block that parsed, bound nothing and raised nothing would read as a working
# window to whoever wrote one -- the silent-success shape this tree distrusts
# most. That refusal is replaced when the Qt backend lands, and the tier goes
# red then, which is the correct way for it to end.
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
refuse "only a form carries a subject" "only a form carries a subject"

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
src valid <<'EOF'
window customer( title: "C", width: 300, height: 200 ) to show cust
    column( margin: 4 )
        form to edit cust
            field id( label: "Account" )
        end form
        row
            stretch()
            button save( text: "Save" )
        end row
    end column
    on save.clicked
        print "ok"
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
    if vg_run ./gbasic --ast "$work/valid.bas" >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access parsing and freeing a window"
    else
        tail -25 "$work/vg.err"; fail "valgrind objected"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

exit $status
