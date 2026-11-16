#!/usr/bin/env bash
# ONE VOCABULARY (docs/ui_design.md §3, Matthew 2026-11-16: "I don't want to
# end up with two vocabularies"). The words a part's options, ui.read, ui.set,
# ui.invoke and a handler's event may use are listed once, in
# src/window_check.c; each backend maps them to its own means (Qt's map is in
# src/modules/qt_backend.cpp); docs/reference.md's *Words* documents them.
# Three lists that must agree, so this holds them to each other in both
# directions, with a planted word as the control. Then the refusals a program
# meets, at load time, which need no Qt and no display.
cd "$(dirname "$0")/.."
export LC_ALL=C   # sort and comm must agree on one order
status=0
pass() { printf 'ok   %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; status=1; }
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# The words of one kind, read from the C array that lists them.
words_of() {   # $1 = file, $2 = array name
    sed -n "/static const char \*$2\[\] = {/,/};/p" "$1" \
        | grep -oE '"[a-z_]+"' | tr -d '"' | sort -u
}
# What the Qt backend can answer for a word: a row of its name tables, or a
# converter that tests the word by name.
qt_answers() {   # $1 = backend file
    { grep -oE '^\s*\{"[a-z_]+", "[A-Za-z*]+", "[A-Za-z]+"\}' "$1" | grep -oE '^\s*\{"[a-z_]+"' | grep -oE '[a-z_]+'
      grep -oE 'strcmp\((word|prop|signal), "[a-z_]+"\) == 0' "$1" | grep -oE '"[a-z_]+"' | tr -d '"'
    } | sort -u
}

printf 'TIER tables: every word has a Qt mapping, and the map holds only words\n'
check_tables() {   # $1 = window_check.c, $2 = qt_backend.cpp; prints problems
    words_of "$1" ui_property_words > "$work/p"
    words_of "$1" ui_method_words > "$work/m"
    words_of "$1" ui_event_words > "$work/e"
    cat "$work/p" "$work/m" "$work/e" | sort -u > "$work/all"
    qt_answers "$2" > "$work/qt"
    comm -23 "$work/all" "$work/qt" | sed 's/^/unmapped: /'
    # A table row naming something that is not a word would be a word nobody
    # can say: the map's own first column, checked against the list.
    grep -oE '^\s*\{"[a-z_]+", "[A-Za-z*]+", "[A-Za-z]+"\}' "$2" \
        | grep -oE '^\s*\{"[a-z_]+"' | grep -oE '[a-z_]+' | sort -u \
        | comm -23 - "$work/all" | sed 's/^/not a word: /'
}
n=$(words_of src/window_check.c ui_property_words | wc -l)
if [ "$n" -lt 30 ]; then
    fail "read only $n property words -- the extraction is broken, not the vocabulary"
fi
problems=$(check_tables src/window_check.c src/modules/qt_backend.cpp)
if [ -z "$problems" ]; then
    pass "all $(wc -l < "$work/all") words map to Qt, and Qt's map names only words"
else
    printf '%s\n' "$problems" | sed 's/^/     /'
    fail "the vocabulary and the Qt map disagree"
fi
# THE CONTROL: a word added to the list with no mapping must be reported.
sed 's/    "pressed", "changed",/    "pressed", "squeezed", "changed",/' src/window_check.c > "$work/planted.c"
if check_tables "$work/planted.c" src/modules/qt_backend.cpp | grep -qx 'unmapped: squeezed'; then
    pass "CONTROL: a planted word with no mapping is reported"
else
    fail "CONTROL: a planted unmapped word went unseen"
fi

# The real lists again: the control above rewrote the scratch copies.
check_tables src/window_check.c src/modules/qt_backend.cpp > /dev/null

printf 'TIER former names: every "write X instead" names a real word\n'
bad=""
while read -r qtname word; do
    if ! grep -qx "$word" "$work/all"; then
        bad="$bad $qtname->$word"
    fi
done < <(sed -n '/ui_former_names\[\]\[2\] = {/,/};/p' src/window_check.c \
             | grep -oE '\{"[A-Za-z]+", "[a-z_]+"\}' | tr -d '{}",' )
if [ -z "$bad" ]; then
    pass "each Qt name's refusal teaches a word that exists"
else
    fail "former names pointing at no word:$bad"
fi

printf 'TIER reference: docs/reference.md documents every word\n'
words_section=$(sed -n '/^### Words$/,/^## /p' docs/reference.md)
missing=""
for w in $(cat "$work/all"); do
    printf '%s' "$words_section" | grep -q "\`$w\`" || missing="$missing $w"
done
if [ -n "$words_section" ] && [ -z "$missing" ]; then
    pass "the reference's *Words* names every word"
else
    fail "the reference's *Words* does not name:${missing:- (no Words section)}"
fi

printf 'TIER refusals: at load time, with no Qt and no display\n'
refused() {   # $1 = name, $2 = window body lines, $3 = expected text
    printf 'window w( title: "W" )\n    column\n%s\n    end column\n%s\nend window\nprogram main()\n    print "RAN"\nend program\n' \
        "$2" "${4:-}" > "$work/$1.bas"
    out=$(./gbasic "$work/$1.bas" 2>&1)
    rc=$?
    if [ "$rc" -ne 0 ] && printf '%s' "$out" | grep -qF -- "$3" && ! printf '%s' "$out" | grep -q RAN; then
        pass "$1"
    else
        fail "$1: exit $rc, $(printf '%s' "$out" | head -2 | tr '\n' '|')"
    fi
}
refused "Qt's name for an option is refused, naming the word" \
    '        field a( placeholderText: "x" )' \
    'window: on `a`, `placeholderText` is Qt'"'"'s name, not ui'"'"'s -- write `hint`'
refused "a misspelled option is refused with the nearest word" \
    '        field a( hnit: "x" )' \
    '`hnit` is not a property ui knows -- did you mean `hint`?'
refused "Qt's name for an event is refused, naming the word" \
    '        button go( text: "Go" )' \
    '`on go.clicked`: `clicked` is Qt'"'"'s name, not ui'"'"'s -- write `pressed`' \
    '    on go.clicked
        print "x"
    end on'
# AND THE NEIGHBOUR: the same window in words loads and runs, or every refusal
# above is satisfied by refusing windows.
printf 'window w( title: "W" )\n    column\n        field a( hint: "x", max_length: 4, tip: "t" )\n        button go( text: "Go", default: true )\n    end column\n    on go.pressed\n        print "x"\n    end on\nend window\nprogram main()\n    print "RAN"\nend program\n' > "$work/ok.bas"
if [ "$(./gbasic "$work/ok.bas" 2>&1)" = "RAN" ]; then
    pass "CONTROL: the same window written in words loads and runs"
else
    fail "CONTROL: a window in words did not run: $(./gbasic "$work/ok.bas" 2>&1 | head -2 | tr '\n' '|')"
fi

printf 'TIER seam: the front reaches a backend only through its table\n'
# docs/ui_design.md §8 step 4. include/ui_backend.h lists the members; each
# backend's table (src/modules/ui_<name>.c) must set every one, since a member
# left out is a NULL the front calls. The compiler checks each entry's TYPE;
# this checks that none is missing.
members_of() {   # the GbUiBackend members, from the header
    sed -n '/^typedef struct GbUiBackend {/,/^} GbUiBackend;/p' include/ui_backend.h \
        | grep -oE '\(\*[a-z_0-9]+\)\(' | tr -d '(*)' | sort -u
}
table_of() {     # $1 = a table file: the members it sets
    grep -oE '^\s*\.[a-z_0-9]+\s*=' "$1" | tr -d ' .=' | grep -vx name | sort -u
}
members_of > "$work/members"
n=$(wc -l < "$work/members")
if [ "$n" -lt 100 ]; then
    fail "read only $n members from include/ui_backend.h -- the extraction is broken"
fi
tables=$(grep -l '^static const GbUiBackend ' src/modules/ui_*.c)
for t in $tables; do
    gaps=$(table_of "$t" | comm -3 "$work/members" - | tr -d '\t' | tr '\n' ' ')
    if [ -z "$gaps" ]; then
        pass "$t sets all $n members, and nothing else"
    else
        fail "$t and include/ui_backend.h disagree on: $gaps"
    fi
done
[ -n "$tables" ] || fail "no backend table found under src/modules/ui_*.c"
# THE CONTROL: a table with one entry taken out must be reported.
grep -v '^\s*\.tab_caption\s*=' src/modules/ui_qt.c > "$work/ui_short.c"
if table_of "$work/ui_short.c" | comm -3 "$work/members" - | grep -q tab_caption; then
    pass "CONTROL: a table missing one member is reported"
else
    fail "CONTROL: a table missing tab_caption went unseen"
fi
# AND THE FRONT CALLS NO BACKEND FUNCTION BY ITS OWN NAME: one that did would
# work on Qt and fail to link, or call the wrong toolkit, beside any other.
# Comment lines (` * ...`) may still name gb_qt_ functions, as documentation.
direct() { grep -nE '(^|[^_a-z])gb_qt_[a-z_0-9]+\s*\(' "$1" | grep -vE '^[0-9]+:\s*(\*|/\*)'; }
found=$(direct src/modules/ui.c)
if [ -z "$found" ]; then
    pass "src/modules/ui.c calls no gb_qt_ function directly"
else
    printf '%s\n' "$found" | head -5 | sed 's/^/     /'
    fail "src/modules/ui.c calls the Qt backend by name, around its table"
fi
sed 's/ui->retire_drain();/gb_qt_retire_drain();/' src/modules/ui.c > "$work/front.c"
if [ -n "$(direct "$work/front.c")" ]; then
    pass "CONTROL: a planted direct call is found"
else
    fail "CONTROL: a planted gb_qt_retire_drain() call went unseen"
fi
# GBASIC_UI NAMING A BACKEND THIS BUILD LACKS IS REFUSED, never quietly
# replaced by another (ruling Q6). With no backend at all the build's own
# sentence comes first. Either way nothing is constructed, so no display is
# needed. `ui.check` answers regardless: it is about the declaration.
printf 'window w( title: "W" )\n    column\n        field a( hint: "x" )\n    end column\nend window\nprogram main()\n    print ui.check(w)\n    print ui.can_display()\n    ui.open(w)\n    print "OPENED"\nend program\n' > "$work/be.bas"
out=$(GBASIC_UI=nonesuch ./gbasic "$work/be.bas" 2> "$work/be.err")
err=$(cat "$work/be.err")
if printf '%s' "$err" | grep -q 'GBASIC_UI names `nonesuch`, and this build has no such backend -- it has: ' \
   || printf '%s' "$err" | grep -q 'Qt support is not available in this build'; then
    if [ "$out" = "$(printf 'true\nfalse')" ]; then
        pass "GBASIC_UI=nonesuch: check and can_display answer, open is refused naming it"
    else
        fail "GBASIC_UI=nonesuch answered wrongly: $(printf '%s' "$out" | tr '\n' '|')"
    fi
else
    fail "GBASIC_UI=nonesuch was not refused: $(printf '%s' "$err" | tr '\n' '|')"
fi

[ "$status" = 0 ] && printf 'PASS tests/run_ui_vocabulary.sh\n'
exit "$status"
