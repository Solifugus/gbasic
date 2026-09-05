#!/usr/bin/env bash
# Executable-docs gate (PLAN.md Phase D3, deliverable 4).
#
# The AI cookbook (docs/ai/COOKBOOK.md) promises that every idiom points at a
# real, suite-verified file. This gate enforces that promise: every file the
# cookbook references must (a) exist and (b) be wired into some test runner —
# either named in a tests/*.sh case list, or covered by a directory glob a runner
# uses (e.g. run_gui_parse.sh globs examples/gi/*.bas). It does not re-run the
# programs; the suites already do that.
set -u

cd "$(dirname "$0")/.."

COOK=docs/ai/COOKBOOK.md
if [ ! -f "$COOK" ]; then
    echo "FAIL run_docs_gate: $COOK not found"
    exit 1
fi

# Runners to search, excluding this gate itself.
runners=$(ls tests/*.sh tests/lsp/*.sh 2>/dev/null | grep -v 'run_docs_gate.sh')

# `packaging/` joined this list when the shipping guide landed: COOKBOOK
# referenced packaging/example-app/app/notesd.bas and the gate could not see
# it, so a cookbook entry pointed at a file nothing checked -- the exact hole
# this gate exists to close.
refs=$(grep -oE '(examples|tests|packaging)/[A-Za-z0-9_./-]+\.(bas|gb)' "$COOK" | sort -u)
if [ -z "$refs" ]; then
    echo "FAIL run_docs_gate: no file references found in $COOK"
    exit 1
fi

status=0
for f in $refs; do
    if [ ! -f "$f" ]; then
        echo "FAIL missing        $f"
        status=1
        continue
    fi
    base=$(basename "$f")
    stem="${base%.*}"
    ext="${f##*.}"
    glob="$(dirname "$f")/*.$ext"
    if grep -qF "$stem" $runners 2>/dev/null || grep -qF "$glob" $runners 2>/dev/null; then
        echo "PASS wired          $f"
    else
        echo "FAIL not wired      $f"
        status=1
    fi
done

if [ "$status" -ne 0 ]; then
    echo "run_docs_gate: some cookbook references are missing or unwired"
fi

# --- Performance claims must be able to expire ---------------------------------
#
# UNLEARN.md told readers that `arr[i]` and `append` were O(n^2) for six weeks
# after a copy-on-write package made them linear. Nothing failed. The claim was
# then repeated into a phase report without measurement and scoped a whole phase
# against a problem that no longer existed.
#
# The rule that prevents a repeat: every performance claim must cite a RUNNER,
# and that runner must assert the claim in its own direction. A runner is
# required rather than a `.bas` golden because a golden asserts VALUES, and no
# arrangement of values can tell you what something costs — only a runner times
# anything. So:
#
#   * a claim that something IS slow is carried by a negative control, which
#     fails when the thing gets fixed (run_arridx.sh requires repeated string
#     concatenation to exceed the quadratic gate);
#   * a claim that something is NOT slow is carried by a shape tier, which fails
#     when it regresses (run_arridx.sh, run_stridx.sh).
#
# Either way the claim has an expiry: change the behaviour and a test goes red,
# so the doc cannot quietly become a lie.
#
# TWO LIMITS, STATED SO NOBODY MISTAKES THIS FOR MORE THAN IT IS:
#
#   1. It checks the CITATION, not the semantics. It cannot read a runner and
#      decide whether its assertions really match the prose. What it guarantees
#      is that every claim points at a live runner, so there is always something
#      that can go red and somewhere to look when it does.
#   2. In COOKBOOK.md it finds claims by KEYWORD, and prose can always make a
#      cost claim without using one. That is not hypothetical: the bullet
#      "remember `append` copies" was stale for months, sat two lines from a
#      cited one, and tripped no trigger until it was found by reading. The word
#      list below was widened to catch that shape; it will not catch every shape.
#
# The UNLEARN.md half has neither weakness, because it checks every bullet in the
# section regardless of wording. Prefer putting a performance claim there.
perf_status=0

# Emit one line per markdown bullet (continuation lines folded in).
fold_bullets() {
    awk '/^- /{if(b!="")print b; b=$0; next}
         {if(b!="")b=b" "$0}
         END{if(b!="")print b}'
}

check_citation() { # label bullet-text
    local label=$1 bullet=$2 runner
    runner=$(printf '%s\n' "$bullet" | grep -oE 'tests/run_[A-Za-z0-9_]+\.sh' | head -1)
    if [ -z "$runner" ]; then
        printf 'FAIL uncited perf   %s\n' "$label"
        printf '     a performance claim must cite a tests/run_*.sh that asserts it,\n'
        printf '     so that changing the behaviour makes a test fail rather than the\n'
        printf '     documentation quietly become wrong.\n'
        perf_status=1
        return
    fi
    if [ ! -f "$runner" ]; then
        printf 'FAIL missing runner %s -> %s\n' "$label" "$runner"
        perf_status=1
        return
    fi
    printf 'PASS perf cited     %-58s -> %s\n' "$label" "$runner"
}

# UNLEARN.md: every bullet in the performance-traps section.
UNLEARN=docs/ai/UNLEARN.md
if [ ! -f "$UNLEARN" ]; then
    echo "FAIL run_docs_gate: $UNLEARN not found"
    exit 1
fi
perf_section=$(awk '/^## Performance traps/{f=1;next} f && (/^---/ || /^## /){exit} f' "$UNLEARN")
if [ -z "$(printf '%s' "$perf_section" | tr -d '[:space:]')" ]; then
    echo "FAIL run_docs_gate: no '## Performance traps' section in $UNLEARN"
    echo "     If the section was renamed, update this gate with it -- silently"
    echo "     checking nothing is exactly the failure this gate exists to stop."
    exit 1
fi
while IFS= read -r bullet; do
    [ -n "$bullet" ] || continue
    label="UNLEARN: $(printf '%s' "$bullet" | sed -e 's/^- //' -e 's/\*\*//g' | cut -c1-52)"
    check_citation "$label" "$bullet"
done <<EOF
$(printf '%s\n' "$perf_section" | fold_bullets)
EOF

# COOKBOOK.md: any bullet that makes a claim about cost. The trigger words are
# deliberately few and blunt; a bullet that trips one and is not really a
# performance claim should be reworded rather than exempted, because an
# exemption list is the thing that rots.
while IFS= read -r bullet; do
    [ -n "$bullet" ] || continue
    printf '%s\n' "$bullet" | grep -qE 'O\(n²\)|O\(n\^2\)|O\(1\)|quadratic|superlinear|linear|amortized|copies' || continue
    label="COOKBOOK: $(printf '%s' "$bullet" | sed -e 's/^- //' -e 's/\*\*//g' | cut -c1-51)"
    check_citation "$label" "$bullet"
done <<EOF
$(fold_bullets <"$COOK")
EOF

if [ "$perf_status" -ne 0 ]; then
    echo "run_docs_gate: a performance claim has no test that can prove it stale"
    status=1
fi

# --- (3) The documentation index must be complete, in both directions ---------
#
# docs/README.md is the only place that lists every document AND says whether it
# describes shipped behaviour or proposes unbuilt work. That distinction is the
# one this project keeps getting hurt by -- a design proposal read as a
# description, and stale status lines claiming unbuilt what shipped months ago
# (four corrected on 2026-08-15, one of which turned a working module into a
# release blocker). An index nobody maintains would reproduce the same failure
# one level up, so it is checked rather than trusted: every doc must appear, and
# every link must resolve.
INDEX=docs/README.md
if [ ! -f "$INDEX" ]; then
    echo "FAIL missing        $INDEX (the documentation index)"
    status=1
else
    for f in docs/*.md docs/ai/*.md; do
        rel="${f#docs/}"
        [ "$rel" = "README.md" ] && continue
        if ! grep -qF "($rel)" "$INDEX"; then
            echo "FAIL not indexed    $f -- add it to $INDEX with a status"
            status=1
        fi
    done
    # Every relative link in the index must point at a real file.
    for link in $(grep -oE '\]\([A-Za-z0-9_./-]+\.md\)' "$INDEX" | sed 's/^](//; s/)$//'); do
        case "$link" in
            ../*) target="${link#../}" ;;
            *)    target="docs/$link" ;;
        esac
        if [ ! -f "$target" ]; then
            echo "FAIL index dangling $link -- $INDEX links to a file that does not exist"
            status=1
        fi
    done
    if [ "$status" -eq 0 ]; then
        echo "PASS index          every doc listed in $INDEX, every link resolves"
    fi
fi

# --- (4) The licence split must stay unambiguous -------------------------------
#
# gBASIC is dual-licensed: Apache-2.0 except for ten stdlib libraries that are
# AGPL, so that they can also be offered commercially. A file whose licence is
# unclear is worse than either choice -- a user cannot tell what they may do, and
# the copyright holder cannot tell what they may sell. So: every stdlib library
# declares an SPDX identifier, LICENSING.md lists every one of them, and the two
# agree. Adding a library without deciding its licence fails here.
LICMAP=LICENSING.md
if [ ! -f "$LICMAP" ]; then
    echo "FAIL missing        $LICMAP (the licence map)"
    status=1
else
    lic_ok=1
    for f in stdlib/*.bas; do
        base=$(basename "$f" .bas)
        spdx=$(grep -m1 -oE 'SPDX-License-Identifier: [A-Za-z0-9.+-]+' "$f" | sed 's/.*: //')
        if [ -z "$spdx" ]; then
            echo "FAIL no SPDX        $f -- add a licence header and list it in $LICMAP"
            lic_ok=0; status=1; continue
        fi
        # The map lists names as `word` inside the section for their licence.
        if ! grep -qF "\`$base\`" "$LICMAP"; then
            echo "FAIL unmapped       $f declares $spdx but is not listed in $LICMAP"
            lic_ok=0; status=1
        fi
    done
    # And the reverse: a name in the AGPL list must actually declare AGPL.
    agpl_listed=$(sed -n '/^### AGPL-3.0-or-later/,/^## /p' "$LICMAP" | grep -oE '`[a-z_]+`' | tr -d '`' | sort -u)
    for n in $agpl_listed; do
        [ -f "stdlib/$n.bas" ] || continue
        if ! grep -q 'SPDX-License-Identifier: AGPL-3.0-or-later' "stdlib/$n.bas"; then
            echo "FAIL licence drift  $LICMAP lists $n as AGPL but stdlib/$n.bas does not declare it"
            lic_ok=0; status=1
        fi
    done
    [ "$lic_ok" = "1" ] && echo "PASS licensing      every stdlib library declares a licence and matches $LICMAP"
fi

# --- PLAT-DEBT 2: claims about the STATE of the product ---------------------
#
# Two tripwires, both added after a documentation sweep found the same rot in
# three places at once, none of which reading had caught across several
# previous sweeps:
#
#   * README.md said the WebServer had "no TLS, routing, middleware, static
#     files, streaming" -- while run_web_tls.sh, run_web_routes.sh and
#     run_web_stream.sh were green in the same tree;
#   * docs/webclient_design.md, marked Shipped in the index, opened with "No
#     webclient runtime implementation exists yet";
#   * docs/project_state.md named two stdlib libraries that do not exist
#     (`sourceview`, `text`) and omitted four that do.
#
# A prose sweep cannot see any of these, because each reads perfectly well on
# its own page. Only comparing the page against the tree can.

# 1. The stdlib roster. Both directions: a library that exists must be named,
#    and a name must exist. The second half is what catches `sourceview`.
STATE=docs/project_state.md
if [ -f "$STATE" ]; then
    roster_ok=1
    # Only the bullet list is the roster. A following prose paragraph may
    # legitimately name a library that does not exist (this file's own note
    # about `sourceview` did), so bullets and their indented continuations are
    # the roster and nothing else is.
    section=$(awk '/^## Standard-Library Toolkits/{f=1} f&&/^## /&&!/Standard-Library/{exit} f' "$STATE" \
              | awk '/^- /{b=1} /^[^ -]/{b=0} b')
    for f in stdlib/*.bas; do
        [ -e "$f" ] || continue
        base=$(basename "$f" .bas)
        if ! printf '%s' "$section" | grep -qF "\`$base\`"; then
            echo "FAIL roster missing $STATE does not name stdlib/$base.bas -- add it to the toolkit list"
            roster_ok=0; status=1
        fi
    done
    for n in $(printf '%s' "$section" | grep -oE '`[a-z_]+`' | tr -d '`' | sort -u); do
        # Prose words inside the section that are not library names would trip
        # this, so only names that LOOK like a library are checked: a name is
        # exempt if it is a documented module rather than a stdlib file.
        case "$n" in
            xlsx|sqlite|pg|odbc|ldap|webclient|webserver|xml|gi|gui|process|reflect) continue ;;
        esac
        if [ ! -f "stdlib/$n.bas" ]; then
            echo "FAIL roster ghost   $STATE names \`$n\` but stdlib/$n.bas does not exist"
            roster_ok=0; status=1
        fi
    done
    [ "$roster_ok" = "1" ] && echo "PASS roster         $STATE names every stdlib library, and no library it does not have"
fi

# 1b. THE SAME CHECK ON THE README, which is the page a stranger actually
#     reads, and which rotted for exactly the reason project_state did: it said
#     "Twenty-eight pure-gBASIC libraries" while the tree held forty. Twelve had
#     been added -- the whole finance and accounting family, and the whole
#     business-automation-reasoning family -- without the list moving, and no
#     check could see it because this tier only ever looked at project_state.
#
#     The stated COUNT is checked too. A list can be complete and still open
#     with a stale number, and the number is the part a reader believes without
#     counting.
if [ -f README.md ]; then
    readme_ok=1
    rsection=$(awk '/^### The standard library/{f=1} f&&/^Two larger example/{exit} f' README.md)
    for f in stdlib/*.bas; do
        [ -e "$f" ] || continue
        base=$(basename "$f" .bas)
        if ! printf '%s' "$rsection" | grep -qF "\`$base\`"; then
            echo "FAIL roster missing README.md does not name stdlib/$base.bas in its standard-library section"
            readme_ok=0; status=1
        fi
    done
    for n in $(printf '%s' "$rsection" | grep -oE '`[a-z_]+`' | tr -d '`' | sort -u); do
        case "$n" in
            xlsx|sqlite|pg|odbc|ldap|webclient|webserver|xml|gi|process|reflect) continue ;;
        esac
        if [ ! -f "stdlib/$n.bas" ]; then
            echo "FAIL roster ghost   README.md names \`$n\` but stdlib/$n.bas does not exist"
            readme_ok=0; status=1
        fi
    done
    want=$(ls stdlib/*.bas 2>/dev/null | wc -l)
    # The count may be a numeral or a word, including a compound like
    # "Twenty-eight" -- which the first version of this parser read as 20. It
    # still failed (20 != 40) and it failed for the wrong reason, and would
    # have PASSED a stale "Twenty-eight" on a tree of twenty.
    said=$(printf '%s' "$rsection" | grep -m1 'pure-gBASIC libraries')
    n=$(printf '%s' "$said" | awk '
        BEGIN{
            split("one two three four five six seven eight nine", u, " ")
            for (i in u) v[u[i]] = i
            v["ten"]=10; v["eleven"]=11; v["twelve"]=12; v["thirteen"]=13
            v["fourteen"]=14; v["fifteen"]=15; v["sixteen"]=16
            v["seventeen"]=17; v["eighteen"]=18; v["nineteen"]=19
            v["twenty"]=20; v["thirty"]=30; v["forty"]=40; v["fifty"]=50
            v["sixty"]=60; v["seventy"]=70; v["eighty"]=80; v["ninety"]=90
        }
        {
            if (match($0, /^[0-9]+/)) { print substr($0, 1, RLENGTH); exit }
            head = tolower($1)
            gsub(/[^a-z-]/, "", head)
            total = 0; got = 0
            k = split(head, parts, "-")
            for (i = 1; i <= k; i++) if (parts[i] in v) { total += v[parts[i]]; got = 1 }
            if (got) print total
        }')
    if [ -z "$n" ]; then
        echo "FAIL roster count   README.md standard-library section states no library count"
        readme_ok=0; status=1
    elif [ "$n" != "$want" ]; then
        echo "FAIL roster count   README.md says $n libraries (\"$(printf '%s' "$said" | cut -d' ' -f1)\"); the tree has $want"
        readme_ok=0; status=1
    fi
    [ "$readme_ok" = "1" ] && echo "PASS roster README  README.md names every stdlib library and states the right count ($want)"
fi

# 1c. THE PLATFORM CLAIM, against the CI configuration that is its only
#     evidence. A README that names the platforms it is tested on is a claim
#     like any other, and this one has two ways to go stale: CI moves to a
#     newer Ubuntu and the README goes on naming the old one, or CI gains a
#     macOS or Windows runner and the README goes on saying neither is tested.
#     Both directions are checked.
CI=.github/workflows/ci.yml
if [ -f "$CI" ] && [ -f README.md ]; then
    plat_ok=1
    # Collapsed to ONE LINE: the claim spans a line break ("macOS and Windows"
    # ends one line and "are **not** tested" begins the next), and grep is
    # line-oriented, so the macOS/Windows half silently matched nothing until
    # this was red-proofed.
    plat=$(grep -m1 -A3 '^\*\*Platform' README.md | tr '\n' ' ')
    for v in $(grep -oE 'ubuntu-[0-9]+\.[0-9]+' "$CI" | sed 's/ubuntu-//' | sort -u); do
        if ! printf '%s' "$plat" | grep -qF "$v"; then
            echo "FAIL platform       $CI builds on Ubuntu $v and README.md's platform note does not say so"
            plat_ok=0; status=1
        fi
    done
    for other in macos windows; do
        if grep -qE "runs-on:.*$other|$other-[0-9a-z]+" "$CI" \
           && printf '%s' "$plat" | grep -qiE "$other.*not|not.*$other"; then
            echo "FAIL platform       $CI has a $other runner but README.md says $other is not tested"
            plat_ok=0; status=1
        fi
    done
    [ "$plat_ok" = "1" ] && echo "PASS platform       README.md names the platforms CI actually builds on"
fi

# 1c-2. THE SUITE COUNT AND THE COOKBOOK COUNT, both of which the README states
#     as a number a reader believes without counting -- and both of which are
#     exactly the shape that rots. MEASURED when this check was written: the
#     README said "all 75 suites" against a tree holding 107, so it was
#     understating the gate by a third, and the roster tripwire beside it could
#     not see it because that one only reads the standard-library section.
#
#     Counted from the tree, never from a list, for the same reason run_all.sh
#     discovers by glob: a number maintained by hand is a number that drifts.
num_word() {
    awk '
        BEGIN{
            split("one two three four five six seven eight nine", u, " ")
            for (i in u) v[u[i]] = i
            v["ten"]=10; v["eleven"]=11; v["twelve"]=12; v["thirteen"]=13
            v["fourteen"]=14; v["fifteen"]=15; v["sixteen"]=16
            v["seventeen"]=17; v["eighteen"]=18; v["nineteen"]=19
            v["twenty"]=20; v["thirty"]=30; v["forty"]=40; v["fifty"]=50
            v["sixty"]=60; v["seventy"]=70; v["eighty"]=80; v["ninety"]=90
        }
        {
            w = tolower($1); gsub(/[^a-z0-9-]/, "", w)
            if (w ~ /^[0-9]+$/) { print w; exit }
            total = 0; got = 0
            k = split(w, parts, "-")
            for (i = 1; i <= k; i++) if (parts[i] in v) { total += v[parts[i]]; got = 1 }
            if (got) print total
        }'
}

if [ -f README.md ]; then
    count_ok=1
    libcount_tmp=$(mktemp)

    #     EVERY numeric suite claim, not just the one in the Tests section: the
    #     README states the count twice (the opening and `run_all.sh`), and a
    #     check that reads one of them leaves the other free to drift, which is
    #     the same shape as the rot it exists to catch.
    want_suites=$(ls tests/run_*.sh 2>/dev/null | grep -vc 'run_all\.sh$')
    said_suites=$(grep -oE '[0-9]+ (test )?suites' README.md | grep -oE '^[0-9]+')
    if [ -z "$said_suites" ]; then
        echo "FAIL suite count    README.md never states how many test suites there are"
        count_ok=0; status=1
    else
        for n in $said_suites; do
            if [ "$n" != "$want_suites" ]; then
                echo "FAIL suite count    README.md says $n suites; tests/ holds $want_suites"
                count_ok=0; status=1
            fi
        done
    fi

    #     BOTH spellings of the library count, for the same reason. The roster
    #     tier above reads only the standard-library section; the opening
    #     paragraph states the number again, where nothing was looking. Scoped
    #     to the two phrasings that mean THIS count, so "48 shared libraries"
    #     (a packaging measurement) and "4 libraries" (a different claim
    #     entirely) are not swept in -- nor is "Ten standard libraries are AGPL",
    #     which is a true statement about a SUBSET and is why the phrase checked
    #     is `pure-gBASIC libraries`, which has exactly one meaning here.
    want_libs=$(ls stdlib/*.bas 2>/dev/null | wc -l)
    grep -oiE '[a-z0-9-]+ pure-gBASIC libraries' README.md | while read -r said _; do
        n=$(printf '%s' "$said" | num_word)
        if [ -z "$n" ]; then
            echo "FAIL library count  README.md says \"$said ... libraries\" and the number cannot be read"
        elif [ "$n" != "$want_libs" ]; then
            echo "FAIL library count  README.md says $n libraries (\"$said\"); stdlib/ holds $want_libs"
        fi
    done > "$libcount_tmp" 2>/dev/null || true
    if [ -s "$libcount_tmp" ]; then
        cat "$libcount_tmp"
        count_ok=0; status=1
    fi
    rm -f "$libcount_tmp"

    # The cookbook list is the README's answer to "where do I actually learn
    # this", so a page missing from it is a page nobody finds.
    want_books=$(ls docs/*cookbook*.md 2>/dev/null | wc -l)
    bsection=$(awk '/^\*\*Cookbooks\*\*/{f=1} f&&/^\*\*Ship it\*\*/{exit} f' README.md)
    said_books=$(printf '%s' "$bsection" | grep -oiE '[a-z0-9-]+ of them' | num_word)
    if [ -z "$said_books" ]; then
        echo "FAIL cookbook count README.md cookbook section states no count"
        count_ok=0; status=1
    elif [ "$said_books" != "$want_books" ]; then
        echo "FAIL cookbook count README.md says $said_books cookbooks; docs/ holds $want_books"
        count_ok=0; status=1
    fi
    for f in docs/*cookbook*.md; do
        [ -e "$f" ] || continue
        if ! printf '%s' "$bsection" | grep -qF "$(basename "$f")"; then
            echo "FAIL cookbook list  README.md cookbook section does not link $f"
            count_ok=0; status=1
        fi
    done

    [ "$count_ok" = "1" ] && echo "PASS README counts  suite count ($want_suites) and cookbook list ($want_books) match the tree"
fi

# 1c-3. THE OPTIONAL-DEPENDENCY TABLE, against the Makefile that is its only
#     evidence. Every `HAVE_*` the build probes is a dependency a reader has to
#     be told about, and the README's table is where they are told. MEASURED
#     when this was written: the `ldap` module had shipped and appeared in
#     NEITHER the platform table nor the dependency table -- so the one page a
#     stranger reads did not know a module existed. Nothing could see it,
#     because the roster tiers read the standard-library section and `ldap` is
#     a native module rather than a stdlib file.
#
#     Names, not macros: the table says "libcurl", the Makefile says
#     HAVE_LIBCURL, so the macro tail is mapped to the spelling a reader would
#     recognise. GIO rides with GIR and is not separately installable.
if [ -f Makefile ] && [ -f README.md ]; then
    dep_ok=1
    deptable=$(awk '/^\| Dependency \| Enables \|/{f=1} f&&/^```/{exit} f' README.md)
    for m in $(grep -oE 'HAVE_[A-Z0-9_]+' Makefile | sort -u); do
        case "$m" in
            HAVE_GIO) continue ;;                       # ships with HAVE_GIR
            HAVE_GIR)      want='girepository' ;;
            HAVE_GTK)      want='GTK 3' ;;
            HAVE_SQLITE3)  want='sqlite3' ;;
            HAVE_LIBPQ)    want='libpq' ;;
            HAVE_ODBC)     want='ODBC' ;;
            HAVE_LDAP)     want='libldap' ;;
            HAVE_LIBCURL)  want='libcurl' ;;
            HAVE_LIBXCRYPT) want='libxcrypt' ;;
            HAVE_LIBCRYPTO) want='libcrypto' ;;
            HAVE_LIBSSL)   want='libssl' ;;
            HAVE_LIBXML2)  want='libxml2' ;;
            HAVE_ZLIB)     want='zlib' ;;
            *) echo "FAIL dependency     Makefile probes $m and this check has no README spelling for it"
               dep_ok=0; status=1; continue ;;
        esac
        if ! printf '%s' "$deptable" | grep -qiF "$want"; then
            echo "FAIL dependency     Makefile probes $m and README.md's dependency table does not name $want"
            dep_ok=0; status=1
        fi
    done
    [ "$dep_ok" = "1" ] && echo "PASS dependency     README.md names every optional dependency the build probes"
fi

# 1d. THE STATS COOKBOOKS, against the rule that a function from another library
#     must be qualified. Both pages taught `ols(...)` after `load stats` -- 78
#     calls between them -- and nothing could see it: they are not in
#     run_doc_examples, and a parse-only check would not help if they were,
#     because an unqualified call PARSES perfectly well. It is only wrong at
#     run time, and only since the cross-library scan was removed.
#
#     Checked by comparing the fenced code against stats' own exports, which is
#     the only way to tell `stats.ols(...)` from a local helper named `ols`.
for page in docs/cookbook_social_behavioral.md docs/cookbook_econometrics_finance.md; do
    [ -f "$page" ] || continue
    # A call NOT preceded by a dot. Identifiers may contain digits --
    # `t_test_2sample` is one, and an earlier pattern that stripped them turned
    # it into `t_test_sample`, matched nothing, and passed while the page was
    # wrong. Found by red-proofing this tier, not by reading it.
    # THREE THINGS THIS GOT WRONG, all found by red-proofing and none by
    # reading it: identifiers may contain digits (`t_test_2sample` became
    # `t_test_sample` and matched nothing), the EXPORTS side needed the same
    # fix, and `comm` wants byte order where `sort` gives locale order -- it
    # said "input is not in sorted order" on stderr and returned nothing, so
    # the tier passed on a page that was wrong.
    bare=$(awk '/^```basic/{f=1;next} f&&/^```/{f=0;next} f' "$page" \
           | grep -oE "(^|[^[:alnum:]_.])[a-z_][a-z0-9_]*[[:space:]]*\(" \
           | sed -E 's/^[^a-z_]//; s/[[:space:]]*\($//' | LC_ALL=C sort -u)
    exports=$(grep -oE '^    function [a-z_][a-z0-9_]*' stdlib/stats.bas \
              | awk '{print $2}' | grep -v '^_' | LC_ALL=C sort -u)
    hits=$(LC_ALL=C comm -12 <(printf '%s\n' "$bare") <(printf '%s\n' "$exports") | tr '\n' ' ')
    if [ -n "$(printf '%s' "$hits" | tr -d ' ')" ]; then
        echo "FAIL cookbook qual  $page calls stats functions unqualified: $hits"
        status=1
    else
        echo "PASS cookbook qual  $page qualifies every stats call"
    fi
done

# 2. Negative capability claims contradicted by a test suite.
#
#    The vocabulary is derived FROM THE SUITE NAMES, not hand-listed, so a new
#    capability with a suite is covered the day it lands -- a hand-maintained
#    list of things-not-to-claim would rot exactly like the claims it guards.
#    A doc saying "no tls" or "does not support streaming" while
#    run_web_tls.sh / run_web_stream.sh exist and pass is the shape of every
#    instance found so far.
claims_ok=1
CAPS="tls routing streaming static pool"
for doc in README.md docs/reference.md docs/project_state.md; do
    [ -f "$doc" ] || continue
    for cap in $CAPS; do
        # Only suites that actually exist define a capability worth guarding.
        case "$cap" in
            tls)       suite=tests/run_web_tls.sh ;;
            routing)   suite=tests/run_web_routes.sh ;;
            streaming) suite=tests/run_web_stream.sh ;;
            static)    suite=tests/run_web_routes.sh ;;
            pool)      suite=tests/run_web_pool.sh ;;
        esac
        [ -f "$suite" ] || continue
        hit=$(grep -inE "(no|without|not support|does not do|lacks) [a-z, ]{0,40}$cap" "$doc" \
              | grep -iE "server|http|web" \
              | grep -viE "no longer|not support (websockets|chunked|multipart)" | head -1)
        if [ -n "$hit" ]; then
            echo "FAIL stale claim    $doc denies '$cap' but $suite exists:"
            echo "                    $(printf '%s' "$hit" | cut -c1-100)"
            claims_ok=0; status=1
        fi
    done
done
[ "$claims_ok" = "1" ] && echo "PASS state claims   no doc denies a capability that has a passing suite"

exit "$status"
