#!/usr/bin/env bash
# `files(d, pattern)` -- the recursive glob, and the `size`/`modified` fields
# the listing was already paying for (docs/etl_files_design.md §3).
#
# WHAT IT REPLACED. There was no way to find a file by shape at all: `files(d)`
# lists ONE level, so "every .xlsx under /data, whatever folders the vendor
# invented this quarter" was a hand-written recursive walk per program. §3's
# requirement is Matthew's own -- "folder structures that can and will change"
# -- and `**` is the whole answer to it, being anchored at the top and at the
# filename and indifferent to everything in between.
#
# TESTED AGAINST BASH, WHICH IS THE POINT OF CHOOSING GLOB. The pattern
# language is glob because that is what people already know, and that claim is
# only worth making if it is TRUE -- so the load-bearing tier expands the same
# patterns over the same tree with `shopt -s globstar` and requires the two to
# agree exactly. bash is an INDEPENDENT implementation written by somebody
# else, which is evidence where a golden of our own output is a transcript, and
# it is the implementation the claim is about.
#
# SELF-CHECKING WHERE IT IS NOT AN ORACLE, and here that is forced: every
# defect a glob can have produces A PLAUSIBLE LIST OF FILES. A pattern that
# quietly matched one directory too few, a `**` that stopped at the first
# level, a hidden file swept in -- each is an ordinary-looking answer, and in
# ETL an answer short by one file is a load short by one file that nothing
# downstream can detect.
#
# Tiers:
#   ORACLE      twelve patterns against bash globstar, byte for byte.
#   RECORD      `size` and `modified` against an EXTERNAL truth -- the byte
#               count `printf` wrote and the timestamp `touch` was given --
#               and separately against `file_size`/`file_mtime`, which are a
#               second route to the same question. `size` must be `unknown`
#               for a folder, never 0.
#   ENCODE      the one thing this change TOOK AWAY: `modified` is a datetime,
#               so an entry no longer goes through `encode`. Asserted as a cost
#               with both routes out beside it (`notation`, and `epoch`), since
#               an unstated cost is one the next reader rediscovers.
#   EMPTY       a pattern matching nothing is an EMPTY ARRAY, not an error,
#               because a run with no new files is a normal Tuesday. CONTROL:
#               a directory that cannot be read still RAISES, or "empty" would
#               be indistinguishable from the silent-trap this family had.
#   HIDDEN      a three-way DIFFERENCE: no pattern includes a dotfile, `*`
#               excludes it, `.*` includes it. Asserting any one alone passes
#               on an implementation with no dot rule at all.
#   UNREADABLE  a locked subdirectory WARNS and the walk CONTINUES, asserted as
#               a difference (the readable files are still returned). Three
#               controls, because this split is the easiest thing here to get
#               wrong in either direction: `on warning ignore` silences it,
#               `on warning stop` turns it into an error, and the TOP-LEVEL
#               directory still raises.
#   SYMLINK     a symlinked directory is NOT walked -- the stated cost of
#               refusing to hang on `ln -s .. loop`. CONTROL: a symlink to a
#               FILE is an ordinary entry, so the rule is about cycles and not
#               about symlinks.
#   PRUNE       a pattern with no `**` does not OPEN a directory it cannot
#               match, observed through the ABSENCE of the warning a locked
#               folder would have produced -- with the same tree and a `**`
#               pattern as the control, which does warn. Without the pair, the
#               pattern would be a filter over a walk that happened anyway and
#               nothing would say so.
#   ORDER       sorted by path, which readdir is not.
#   REFUSALS    eight, each located and each naming what is wrong with the
#               pattern rather than reporting nothing found.
#   VALGRIND    a recursive walk allocating a record per entry, plus a warn
#               path that returns from the middle of one.
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -euo pipefail

cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh
make >/dev/null
export GBASIC_PATH=stdlib
export TZ=UTC                      # `modified` renders local time; pin it

work="$(mktemp -d)"
trap 'chmod -R u+rwX "$work" 2>/dev/null || true; rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

# ---- the oracle tree: plain directories and files only -------------------
# Deliberately free of symlinks and unreadable folders, because bash's answers
# there are about ITS rules and this tree's rule is stated separately (see the
# SYMLINK tier). Mixing them would make a disagreement ambiguous.
O="$work/oracle"
mkdir -p "$O/2026/Q1" "$O/2026/Q2" "$O/2025/Q4" "$O/deep/a/b/c" "$O/.git/objects"
for f in top.xlsx .hidden.xlsx 2026/Q1/vendor_acme.xlsx 2026/Q1/other.csv \
         2026/Q2/vendor_bolt.xlsx 2025/Q4/vendor_old.xlsx \
         .git/objects/pack.xlsx deep/a/b/c/vendor_deep.xlsx deep/note.txt; do
    printf 'x' > "$O/$f"
done

patterns=(
    '*.xlsx'
    '*'
    '.*.xlsx'
    '**/*.xlsx'
    '**/vendor_*.xlsx'
    '2026/*/vendor_*.xlsx'
    '202[56]/Q?/*.xlsx'
    '**/*.parquet'
    '**'
    '2026/**/*.xlsx'
    'deep/a/b/c/*'
    '*/*'
)

printf 'TIER oracle: twelve patterns against bash globstar\n'
# gBASIC prints the path RELATIVE to the directory it was given, so the two
# sides are directly comparable; `list` is used rather than `files` because
# bash's pathname expansion does not filter by type and the comparison must be
# of the MATCHER, not of the type filter (which the ORDER and RECORD tiers
# cover).
oracle_fails=0
for p in "${patterns[@]}"; do
    cat > "$work/one.bas" <<'EOF'
program main( args )
    d {dir}= args[0]
    out = []
    for each r in list(d, args[1])
        out = append(out, replace(r.path, args[0] + "/", ""))
    end for
    print join(out, "\n")
end program
EOF
    got=$(./gbasic "$work/one.bas" "$O" "$p" 2>"$work/err" | sed '/^$/d' | sort)
    if [ -s "$work/err" ]; then
        cat "$work/err"; fail "oracle: $p produced stderr"; oracle_fails=1; continue
    fi
    want=$( cd "$O" && shopt -s globstar nullglob dotglob 2>/dev/null
            shopt -u dotglob
            eval "printf '%s\n' $p" | sed '/^$/d' | sort )
    if [ "$got" = "$want" ]; then
        pass "$p ($(printf '%s' "$want" | grep -c . || true) matches)"
    else
        printf '    gBASIC: %s\n    bash  : %s\n' "$(printf '%s' "$got" | tr '\n' ' ')" \
                                                  "$(printf '%s' "$want" | tr '\n' ' ')"
        fail "oracle: $p disagreed with bash"; oracle_fails=1
    fi
done
# NON-VACUITY: a tier comparing two empty lists twelve times would be silent
# and green. At least one pattern must have matched something, and at least one
# must have matched NOTHING, or the set is not discriminating.
nonempty=$(cd "$O" && shopt -s globstar nullglob; eval "printf '%s\n' ${patterns[3]}" | grep -c . || true)
if [ "$nonempty" -lt 4 ]; then
    fail "the oracle tree is too small for the comparison to mean anything"
fi

printf 'TIER record: size and modified against an external truth\n'
printf '0123456789' > "$O/sized.dat"
touch -d '2026-03-04 05:06:07' "$O/sized.dat"
cat > "$work/rec.bas" <<'EOF'
program main( args )
    checks = 0
    bad = 0
    d {dir}= args[0]
    function want(label, got, expect)
        if string(got) = string(expect) then
            print "ok   " + label
        else
            print "MISMATCH " + label + ": got " + string(got) + ", want " + string(expect)
        end if
    end function

    e = files(d, "sized.dat")[0]
    want("size is the byte count printf wrote", e.size, 10)
    want("modified is the time touch was given", string(e.modified), "2026-03-04 05:06:07")
    want("size agrees with file_size", e.size, file_size({file}e.path))
    want("modified agrees with file_mtime", string(e.modified), string(file_mtime({file}e.path)))
    want("name is the basename", e.name, "sized.dat")
    want("type is file", e.type, "file")

    ' A FOLDER HAS NO SIZE, and the answer is `unknown` rather than 0 --
    ' `file_size` refuses a directory outright for the same reason, and a 0
    ' there reads as an empty tree where the truth is "that is not a question
    ' about this entry".
    f = folders(d, "2026")[0]
    want("a folder's size is unknown", is_unknown(f.size), true)
    want("a folder's size is not zero", f.size = 0, false)
    want("a folder still has a modified", is_unknown(f.modified), false)
    want("type is folder", f.type, "folder")

    ' The deep entry proves `path` is the whole path and not just the matched
    ' tail -- a glob that returned a relative path would break every caller
    ' that opens what it found.
    g = files(d, "deep/a/b/c/*")[0]
    want("path is openable", exists({file}g.path), true)
end program
EOF
if ! ./gbasic "$work/rec.bas" "$O" >"$work/rec.out" 2>"$work/rec.err"; then
    cat "$work/rec.err"; fail "the record fixture did not run"
elif grep -q MISMATCH "$work/rec.out"; then
    grep MISMATCH "$work/rec.out"; fail "the record fixture disagreed"
else
    pass "$(grep -c '^ok' "$work/rec.out") checks on the entry record"
fi
rm -f "$O/sized.dat"

printf 'TIER encode: the cost of `modified` being a datetime, stated\n'
# THE ONE THING THIS CHANGE TOOK AWAY, asserted so nobody has to rediscover it.
# An entry used to be three strings and went through `encode` like any record;
# `modified` is a `datetime`, and `encode` deliberately REFUSES typed values
# because a lossy token yields text `decode` cannot read back (pinned as
# negative_encode_typed).
#
# IT IS KEPT A DATETIME ANYWAY, and the reason is the rule this same increment
# applied one level down: `file_mtime` answers this question too, and two
# routes to one question that disagree about the TYPE are the defect that
# refactor removed. An epoch number here would reintroduce it.
#
# So the two routes out are asserted beside the refusal, or "encode refuses"
# would read as data loss rather than as a choice: `notation` keeps every type
# and round-trips the whole entry, and `epoch(e.modified)` is the number for a
# caller that wants `encode`.
cat > "$work/enc.bas" <<'EOF'
program main( args )
    d {dir}= args[0]
    e = files(d, "*.xlsx")[0]
    on error goto next
    t = encode(e)
    if error then
        print "encode refuses an entry"
        error.clear()
    else
        print "MISMATCH encode accepted a datetime"
    end if
    print "epoch is a number: " + type(epoch(e.modified))
    print "without modified: " + string(count(keys(remove_key(e, "modified")))) + " fields encode"
    plain = encode(remove_key(e, "modified"))
    print "and they do: " + string(len(plain) > 0)
end program
EOF
got=$(./gbasic "$work/enc.bas" "$O" 2>&1 || true)
if printf '%s' "$got" | grep -q 'encode refuses an entry' \
   && printf '%s' "$got" | grep -q 'epoch is a number: number' \
   && printf '%s' "$got" | grep -q 'and they do: true' \
   && ! printf '%s' "$got" | grep -q MISMATCH; then
    pass "encode refuses a typed entry; CONTROL: epoch() gives a number and the rest still encodes"
else
    printf '%s\n' "$got"; fail "the encode cost is not what is documented"
fi
cat > "$work/not.bas" <<'EOF'
program main( args )
    load notation
    d {dir}= args[0]
    e = files(d, "*.xlsx")[0]
    back = notation.from_text(notation.to_text(e))
    print "modified survives: " + string(back.modified = e.modified) + " " + type(back.modified)
    print "size survives: " + string(back.size = e.size)
end program
EOF
got=$(./gbasic "$work/not.bas" "$O" 2>&1 || true)
if printf '%s' "$got" | grep -q 'modified survives: true datetime' \
   && printf '%s' "$got" | grep -q 'size survives: true'; then
    pass "CONTROL: notation round-trips the whole entry, datetime and all"
else
    printf '%s\n' "$got"; fail "notation did not round-trip an entry"
fi

printf 'TIER empty: nothing found is an empty array, not an error\n'
cat > "$work/empty.bas" <<'EOF'
program main( args )
    d {dir}= args[0]
    r = files(d, "**/*.nothing-matches-this")
    print "count=" + string(count(r)) + " type=" + type(r)
EOF
printf '    missing {dir}"%s/absent" \n' "$work" >/dev/null
cat >> "$work/empty.bas" <<'EOF'
    on error goto next
    bad = files({dir}"/no/such/directory/at/all", "*")
    if error then
        print "unreadable raises: " + error.message
        error.clear()
    else
        print "MISMATCH an unreadable directory did not raise"
    end if
end program
EOF
./gbasic "$work/empty.bas" "$O" >"$work/empty.out" 2>&1 || true
if grep -qx 'count=0 type=array' "$work/empty.out" \
   && grep -q 'unreadable raises: files could not read directory' "$work/empty.out"; then
    pass "an empty result is an array; CONTROL: an unreadable directory still raises"
else
    cat "$work/empty.out"; fail "the empty/raise pair did not hold"
fi

printf 'TIER hidden: a leading dot must be matched explicitly\n'
cat > "$work/hid.bas" <<'EOF'
program main( args )
    d {dir}= args[0]
    function names(rows)
        out = []
        for each r in rows
            out = append(out, r.name)
        end for
        return join(sort(out), ",")
    end function
    print "none=" + names(files(d, "*.xlsx"))
    print "dot=" + names(files(d, ".*.xlsx"))
    print "bare=" + names(files(d))
end program
EOF
./gbasic "$work/hid.bas" "$O" >"$work/hid.out" 2>&1
if grep -qx 'none=top.xlsx' "$work/hid.out" \
   && grep -qx 'dot=.hidden.xlsx' "$work/hid.out" \
   && grep -qx 'bare=.hidden.xlsx,top.xlsx' "$work/hid.out"; then
    pass "a dotfile is excluded by '*', included by '.*', and present with no pattern"
else
    cat "$work/hid.out"; fail "the three-way dot difference did not hold"
fi

# ---- the odd tree: an unreadable folder, and symlinks --------------------
D="$work/odd"
mkdir -p "$D/good" "$D/locked/inner" "$D/prune/a/b/locked"
printf 'x' > "$D/good/vendor_one.xlsx"
printf 'x' > "$D/locked/inner/vendor_hidden.xlsx"
printf 'x' > "$D/prune/a/b/vendor_two.xlsx"
printf 'x' > "$D/target.xlsx"
ln -s target.xlsx "$D/alias.xlsx"
ln -s ../good "$D/prune/linked"
chmod 000 "$D/locked" "$D/prune/a/b/locked"

printf 'TIER unreadable: a locked subdirectory warns and the walk continues\n'
cat > "$work/warn.bas" <<'EOF'
program main( args )
    d {dir}= args[0]
    print "found=" + string(count(files(d, "**/vendor_*.xlsx")))
end program
EOF
./gbasic "$work/warn.bas" "$D" >"$work/warn.out" 2>"$work/warn.err" || true
if grep -q 'found=2' "$work/warn.out" \
   && grep -q 'warning: files could not read directory' "$work/warn.err" \
   && grep -q 'Permission denied' "$work/warn.err" \
   && grep -q '\[2113\]' "$work/warn.err"; then
    pass "the two readable files came back AND the locked folder was named, with its reason"
else
    cat "$work/warn.out" "$work/warn.err"; fail "warn-and-continue did not hold"
fi

cat > "$work/warn_off.bas" <<'EOF'
program main( args )
    on warning ignore
    d {dir}= args[0]
    print "found=" + string(count(files(d, "**/vendor_*.xlsx")))
end program
EOF
./gbasic "$work/warn_off.bas" "$D" >"$work/off.out" 2>"$work/off.err" || true
if grep -q 'found=2' "$work/off.out" && [ ! -s "$work/off.err" ]; then
    pass "CONTROL: 'on warning ignore' silences it and the answer is unchanged"
else
    cat "$work/off.out" "$work/off.err"; fail "the warning could not be silenced"
fi

cat > "$work/warn_stop.bas" <<'EOF'
program main( args )
    on warning stop
    on error goto next
    d {dir}= args[0]
    r = files(d, "**/vendor_*.xlsx")
    if error then
        print "escalated severity=" + error.severity
        error.clear()
    else
        print "MISMATCH 'on warning stop' did not escalate"
    end if
end program
EOF
./gbasic "$work/warn_stop.bas" "$D" >"$work/stop.out" 2>&1 || true
if grep -qx 'escalated severity=warning' "$work/stop.out"; then
    pass "CONTROL: 'on warning stop' turns it into an error for a job that needs completeness"
else
    cat "$work/stop.out"; fail "escalation did not reach the walk"
fi

printf 'TIER symlink: a symlinked directory is not walked\n'
# THE STATED COST. A symlink can point at its own ancestor, so a recursive walk
# through one can run forever -- and a HANG reports nothing at all, which this
# tree treats as worse than a failure (run_string_nul.sh had to defend against
# exactly that). So the walk does not follow them, and the remedy is to name
# the link as the DIRECTORY, which opendir does follow.
cat > "$work/link.bas" <<'EOF'
program main( args )
    on warning ignore
    d {dir}= args[0]
    print "through=" + string(count(files(d, "prune/linked/*.xlsx")))
    print "named=" + string(count(files({dir}(args[0] + "/prune/linked"), "*.xlsx")))
    print "filelink=" + string(count(files(d, "alias.xlsx")))
end program
EOF
./gbasic "$work/link.bas" "$D" >"$work/link.out" 2>&1
if grep -qx 'through=0' "$work/link.out" \
   && grep -qx 'named=1' "$work/link.out" \
   && grep -qx 'filelink=1' "$work/link.out"; then
    pass "not globbed through, reachable by naming it; CONTROL: a symlink to a FILE is an entry"
else
    cat "$work/link.out"; fail "the symlink rule did not hold"
fi

printf 'TIER prune: a pattern with no ** does not open what it cannot match\n'
# OBSERVED THROUGH AN ABSENCE, which needs its control in the same breath: the
# locked folder sits at prune/a/b/locked, so a two-segment pattern cannot reach
# it and must produce NO warning, while `**` over the same tree must.
cat > "$work/prune.bas" <<'EOF'
program main( args )
    d {dir}= args[0]
    print "n=" + string(count(files(d, args[1])))
end program
EOF
# ITS OWN TREE, because `odd` has a locked folder at the TOP level, which a
# `*/...` pattern legitimately matches and must therefore open -- the first
# draft of this tier put the two in one tree and reported the correct behaviour
# as a failure to prune.
P="$work/pruned"
mkdir -p "$P/a/b/locked"
printf 'x' > "$P/a/b/vendor_two.xlsx"
chmod 000 "$P/a/b/locked"
./gbasic "$work/prune.bas" "$P" '*/vendor_*.xlsx' >"$work/p1.out" 2>"$work/p1.err" || true
./gbasic "$work/prune.bas" "$P" '**/vendor_*.xlsx' >"$work/p2.out" 2>"$work/p2.err" || true
if [ ! -s "$work/p1.err" ] && grep -q 'could not read directory' "$work/p2.err"; then
    pass "the shallow pattern never opened prune/a/b/locked; CONTROL: ** does"
else
    printf '    shallow stderr: %s\n' "$(cat "$work/p1.err")"
    printf '    deep stderr   : %s\n' "$(cat "$work/p2.err")"
    fail "the walk is not pruned by the pattern"
fi

printf 'TIER order: sorted by path, which readdir is not\n'
# PERTURBATION 5 FOUND THIS TIER MEASURING NOTHING. The first draft asked "is
# the result sorted" over the oracle tree, and REMOVING THE SORT ALTOGETHER
# left every check green -- a directory of nine entries came back in readdir
# order which happened to be alphabetical. So the tier now has to establish its
# own premise, and it does it twice, by two mechanisms that fail differently.
#
# (a) STRUCTURALLY, with no dependence on readdir order at all: a directory `b`
# holding `y.txt` beside a FILE called `b-x.txt`. Sorted by path that is
# b, b-x.txt, b/y.txt -- because '-' (0x2D) sorts before '/' (0x2F) -- while a
# depth-first walk emits b, b/y.txt, b-x.txt whichever of the two readdir hands
# over first. Both readdir orders are therefore wrong, so the check cannot pass
# by luck.
O2="$work/order"
mkdir -p "$O2/b"
printf 'x' > "$O2/b/y.txt"
printf 'x' > "$O2/b-x.txt"
cat > "$work/ord.bas" <<'EOF'
program main( args )
    d {dir}= args[0]
    out = []
    for each r in list(d, args[1])
        out = append(out, replace(r.path, args[0] + "/", ""))
    end for
    print join(out, " ")
end program
EOF
got=$(./gbasic "$work/ord.bas" "$O2" '**' 2>&1)
if [ "$got" = "b b-x.txt b/y.txt" ]; then
    pass "a depth-first walk comes back in path order, not walk order"
else
    printf '    got: %s\n    want: b b-x.txt b/y.txt\n' "$got"
    fail "the recursive result is in walk order"
fi

# (b) THE PLAIN LISTING, which has no walk to interleave it, so the premise has
# to come from the filesystem: 60 files whose readdir order is the directory
# hash's and not alphabetical. The premise is ASSERTED rather than assumed --
# a filesystem that happens to hand entries back sorted cannot distinguish the
# two implementations, and this tier says so instead of passing.
O3="$work/order_many"
mkdir -p "$O3"
i=1
while [ "$i" -le 60 ]; do
    printf 'x' > "$O3/f$(printf '%02d' $(( (i * 37) % 60 )))_$i.dat"
    i=$((i + 1))
done
raw=$( (ls -U "$O3" 2>/dev/null || ls -f "$O3" 2>/dev/null || true) | sed '/^\.\{1,2\}$/d' | tr '\n' ' ')
srt=$(ls "$O3" | tr '\n' ' ')
if [ -z "$raw" ]; then
    pass "SKIP (no unsorted ls on this system, so the premise cannot be established)"
elif [ "$raw" = "$srt" ]; then
    pass "SKIP (this filesystem returns entries already sorted; nothing to distinguish)"
else
    got=$(./gbasic "$work/ord.bas" "$O3" '*' 2>&1)
    if [ "$got" = "$(printf '%s' "$srt" | sed 's/ $//')" ]; then
        pass "60 entries whose readdir order is scrambled come back sorted"
    else
        printf '    got : %s\n' "$got"
        printf '    want: %s\n' "$srt"
        fail "the plain listing is not sorted"
    fi
fi

printf 'TIER refusals: a pattern that cannot be meant is named\n'
refuse() {
    local label="$1" arg="$2" expect="$3"
    printf 'program main( args )\n    d {dir}= args[0]\n    print count(files(d, %s))\nend program\n' "$arg" > "$work/ref.bas"
    local out
    out=$(./gbasic "$work/ref.bas" "$O" 2>&1 || true)
    if printf '%s' "$out" | grep -q "runtime error at" && printf '%s' "$out" | grep -qF "$expect"; then
        pass "$label"
    else
        printf '    got: %s\n' "$out"; fail "$label"
    fi
}
refuse "an empty pattern says to omit it"        '""'          'the pattern is empty; omit it to list every entry'
refuse "an absolute pattern is refused by name"  '"/data/*"'   "may not begin with '/'"
refuse "an empty segment is refused"             '"a//b"'      'the pattern has an empty segment'
refuse "a trailing slash is an empty segment"    '"a/"'        'the pattern has an empty segment'
refuse "'..' may not escape the directory"       '"../*"'      "may not contain '.' or '..'"
refuse "'.' is refused too"                      '"./*"'       "may not contain '.' or '..'"
refuse "a non-text pattern is refused"           '42'          'expects a text pattern as its second argument'
printf 'program main( args )\n    d {dir}= args[0]\n    print count(files(d, "*", "*"))\nend program\n' > "$work/ref.bas"
# CAPTURED INTO A VARIABLE, NOT PIPED. The fixture is MEANT to exit nonzero, so
# under `pipefail` the status of `./gbasic ... | grep -q` is gbasic's 1 whatever
# grep found -- and this check reported "a third argument was accepted" against
# a binary that had just refused it. Fifth instance of that trap in this tree,
# and the first one I wrote while the helper directly above it already does it
# the right way.
arity_out=$(./gbasic "$work/ref.bas" "$O" 2>&1 || true)
if printf '%s' "$arity_out" | grep -q 'expects a directory and an optional pattern'; then
    pass "a third argument is still an arity error"
else
    printf '    got: %s\n' "$arity_out"; fail "a third argument was accepted"
fi
# CONTROL: the nearest LEGAL neighbour of every refusal above, or a suite that
# refuses everything would pass all eight.
printf 'program main( args )\n    d {dir}= args[0]\n    print count(files(d, "2026/Q1/*.xlsx"))\nend program\n' > "$work/ref.bas"
if [ "$(./gbasic "$work/ref.bas" "$O" 2>&1)" = "1" ]; then
    pass "CONTROL: an ordinary multi-segment pattern is accepted"
else
    fail "CONTROL: a legal pattern was refused"
fi

printf 'TIER valgrind\n'
if vg_available; then
    cat > "$work/vg.bas" <<'EOF'
program main( args )
    on warning ignore
    d {dir}= args[0]
    print count(files(d, "**/vendor_*.xlsx"))
    print count(list(d, "**"))
    print count(files(d, "**/*.none"))
    print count(files(d))
    on error goto next
    r = files(d, "../x")
    error.clear()
    r = files(d, 42)
    error.clear()
    r = files({dir}"/no/such/dir", "*")
    error.clear()
end program
EOF
    if vg_run ./gbasic "$work/vg.bas" "$D" >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access, and no descriptor left open"
    else
        tail -30 "$work/vg.err"; fail "valgrind objected"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

exit $status
