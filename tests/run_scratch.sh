#!/usr/bin/env bash
# SCRATCH SPACE: `temp_file`, `temp_dir` and `remove_tree`.
#
# The three shipped together because they are one job -- make somewhere to
# work, and get rid of it afterwards -- and before them a program had NEITHER
# END. There was no way to ask for a unique name, so `persist.bas` writes
# `path + ".tmp"` beside its destination and two writers of one file share one
# temp name; and `remove_dir` removes only an EMPTY directory, so nothing in
# the language could remove a tree it had just built.
#
# `remove_tree` gets its own name rather than an option on `remove_dir`, and
# that was decided before this increment: the comment above `remove_dir` says
# the option it would be offered is a recursive delete, which "belongs to a
# decision nobody should make by passing a flag".
#
# THE LOAD-BEARING TIER IS THE SYMLINK, and it is the reason this runner exists
# rather than a fixture alone: gBASIC cannot create a symlink, so the one
# behaviour that separates a correct recursive delete from a destructive one
# cannot be written in the language it is a property of. A tree containing a
# link to a directory outside it must lose THE LINK and not the target --
# asserted as a difference, since "the tree was removed" is equally satisfied
# by a walk that followed the link and removed somebody else's data on the way.
#
# Tiers:
#   SEMANTICS   tests/scratch_test.bas -- 39 checks: a temp file that EXISTS,
#               two calls giving two names, `in:` and the safe-write pattern
#               end to end, a temp directory removed by `remove_tree`, and 12
#               refusals beside the three controls that must still work.
#   SYMLINK     a link INSIDE the tree is unlinked, its target survives with
#               its content; a link given AS the top path is refused by name.
#   MODE        a temp file arrives at 0600 and a temp directory at 0700, which
#               is what lets a program hold a secret in one BEFORE `chmod`
#               exists in this language. Read with `stat`, from outside.
#   CROSSDEV    the CONTROL for `in:`: a temp file in the SYSTEM directory is
#               refused by `atomic_replace` onto the work directory, which is
#               the measurement the option exists for. Skips with its reason
#               where the two sit on one filesystem, because a tier that cannot
#               establish its own premise must say so rather than pass.
#   DEPTH       the 256-directory bound, with the control that an ordinary tree
#               is still removed -- or "it refuses deep trees" is satisfied by
#               refusing every tree.
#   VALGRIND    a recursive walk holding a DIR handle at every level.
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -euo pipefail

cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh
make >/dev/null

work="$(mktemp -d)"
trap 'chmod -R u+rwX "$work" 2>/dev/null; rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

printf 'TIER semantics: created not named, `in:`, the safe write, the refusals\n'
mkdir -p "$work/sem"
if ! timeout -k 5 60 ./gbasic tests/scratch_test.bas "$work/sem" >"$work/out" 2>"$work/err"; then
    cat "$work/err"
    # WHAT IT HAD ALREADY DISAGREED ABOUT, or the death hides the diagnosis:
    # the perturbation that answers a name WITHOUT creating the file mismatches
    # on the first check and then raises at `file_size`, and the first version
    # of this branch reported only the raise.
    grep 'MISMATCH' "$work/out" || true
    fail "the fixture did not run"
elif grep -q 'MISMATCH' "$work/out"; then
    grep 'MISMATCH' "$work/out"; fail "the fixture disagreed with itself"
elif ! grep -qx 'mismatches: 0' "$work/out"; then
    fail "the fixture did not finish"
else
    n=$(sed -n 's/^checks: //p' "$work/out")
    if [ -z "$n" ] || [ "$n" -lt 36 ]; then
        fail "only ${n:-0} checks ran, wanted at least 36"
    else
        pass "$n checks"
    fi
fi

printf 'TIER symlink: the link goes, the target stays\n'
# The target is OUTSIDE the tree, which is the whole point: a walk that
# descended the link would remove `precious/` while reporting success.
rm -rf "$work/sl"; mkdir -p "$work/sl/tree/a" "$work/sl/precious"
echo 'do not lose me' > "$work/sl/precious/keep.txt"
echo 'ordinary'       > "$work/sl/tree/a/plain.txt"
ln -s "$work/sl/precious"           "$work/sl/tree/a/dir_link"
ln -s "$work/sl/precious/keep.txt"  "$work/sl/tree/file_link"
ln -s "$work/sl/precious"           "$work/sl/top_link"
cat > "$work/sl.bas" <<'BAS'
program main(args)
    on error goto next
    t {dir}= args[0] + "/tree"
    print "tree: " + string(remove_tree(t))
    if error then
        print "RAISED: " + error.message
        error.clear()
    end if
    l {dir}= args[0] + "/top_link"
    r = remove_tree(l)
    if error then
        print "top refused: " + error.message
        error.clear()
    else
        print "top ACCEPTED: " + string(r)
    end if
end program
BAS
set +e
./gbasic "$work/sl.bas" "$work/sl" >"$work/sl.out" 2>&1
sl_rc=$?
set -e
if [ "$sl_rc" -ne 0 ]; then
    cat "$work/sl.out"; fail "the symlink fixture did not run"
else
    if [ -d "$work/sl/tree" ]; then
        fail "the tree was not removed"
    else
        pass "the tree is gone"
    fi
    # THE ASSERTION THAT MATTERS. Content, not just presence: a walk that
    # truncated the target would leave the file there.
    if [ "$(cat "$work/sl/precious/keep.txt" 2>/dev/null)" = 'do not lose me' ]; then
        pass "the target directory and its content survived"
    else
        fail "the symlink was FOLLOWED: content outside the tree was destroyed"
    fi
    if grep -q 'top refused: .*symlink' "$work/sl.out"; then
        pass "a symlink given as the top path is refused, naming it"
    else
        cat "$work/sl.out"; fail "a symlink as the top path was not refused by name"
    fi
    if [ -L "$work/sl/top_link" ]; then
        pass "and the link itself is left alone (delete() removes a link)"
    else
        fail "the refused call removed the link anyway"
    fi
fi

printf 'TIER mode: a temp file is 0600 before any chmod exists\n'
cat > "$work/mode.bas" <<'BAS'
program main(args)
    d {dir}= args[0]
    print string(temp_file({ in: d, prefix: "mode-" }))
    print string(temp_dir({ in: d, prefix: "moded-" }))
end program
BAS
rm -rf "$work/md"; mkdir -p "$work/md"
./gbasic "$work/mode.bas" "$work/md" > "$work/mode.out"
mf=$(sed -n 1p "$work/mode.out")
md=$(sed -n 2p "$work/mode.out")
set +e
fmode=$(stat -c %a "$mf" 2>/dev/null); fmode=${fmode:-absent}
dmode=$(stat -c %a "$md" 2>/dev/null); dmode=${dmode:-absent}
set -e
if [ "$fmode" = "600" ]; then
    pass "the file is 0600 ($mf)"
else
    fail "the file's mode is $fmode, wanted 600 -- a secret written to it is readable (absent = it was never created)"
fi
if [ "$dmode" = "700" ]; then
    pass "the directory is 0700"
else
    fail "the directory's mode is $dmode, wanted 700"
fi

printf 'TIER crossdev: the measurement `in:` exists for\n'
sys_tmp="${TMPDIR:-/tmp}"
# THE WORK DIRECTORY IS NO USE HERE and that is the trap this tier shares with
# run_read_at's directory tier: `mktemp -d` lands in the system temp directory,
# so comparing the two compares one filesystem with itself and the difference
# this tier exists for cannot be produced. It needs a directory on the
# REPOSITORY's filesystem, which is where the program's own data would live.
xdwork=$(mktemp -d "$PWD/.scratch_xdev_XXXXXX")
trap 'chmod -R u+rwX "$work" 2>/dev/null; rm -rf "$work" "$xdwork"' EXIT
dev_tmp=$(stat -c %d "$sys_tmp")
dev_work=$(stat -c %d "$xdwork")
if [ "$dev_tmp" = "$dev_work" ]; then
    pass "SKIP: $sys_tmp and the repository are on one filesystem here, so the cross-device case CANNOT be produced and NOTHING was measured about it"
else
    cat > "$work/xdev.bas" <<'BAS'
program main(args)
    on error goto next
    here {dir}= args[0]
    ' A temp file in the SYSTEM directory, swapped onto a destination beside
    ' the program's own data -- the safe-write pattern, written the obvious way.
    t = temp_file()
    write(t, "payload")
    far {file}= join_path(args[0], "from_system.txt")
    r = atomic_replace(t, far)
    if error then
        print "system temp REFUSED: " + error.message
        error.clear()
    else
        print "system temp accepted: " + string(r)
    end if
    delete(t)
    ' And the same pattern with `in:`, which is the difference.
    s = temp_file({ in: here })
    write(s, "payload")
    near {file}= join_path(args[0], "from_beside.txt")
    r = atomic_replace(s, near)
    if error then
        print "beside REFUSED: " + error.message
        error.clear()
    else
        print "beside accepted: " + string(r)
    end if
end program
BAS
    ./gbasic "$work/xdev.bas" "$xdwork" > "$work/xdev.out" 2>&1
    if grep -q 'system temp REFUSED: .*same filesystem' "$work/xdev.out" \
       && grep -q 'beside accepted: true' "$work/xdev.out"; then
        pass "a system-directory temp file cannot be swapped in; one made with \`in:\` can"
    else
        cat "$work/xdev.out"
        fail "the cross-device difference did not hold"
    fi
fi

printf 'TIER depth: the 256-directory bound, and the tree that is still removed\n'
rm -rf "$work/dp"; mkdir -p "$work/dp"
deep="$work/dp/deep"; p="$deep"
for _ in $(seq 1 300); do p="$p/x"; done
mkdir -p "$p"; echo leaf > "$p/leaf.txt"
shallow="$work/dp/shallow"; mkdir -p "$shallow/a/b/c"; echo f > "$shallow/a/b/c/f.txt"
cat > "$work/depth.bas" <<'BAS'
program main(args)
    on error goto next
    d {dir}= args[0] + "/deep"
    r = remove_tree(d)
    if error then
        print "deep refused: " + error.message
        error.clear()
    else
        print "deep ACCEPTED: " + string(r)
    end if
    print "deep still there: " + string(exists(d))
    s {dir}= args[0] + "/shallow"
    print "shallow: " + string(remove_tree(s))
    if error then
        print "shallow RAISED: " + error.message
        error.clear()
    end if
end program
BAS
./gbasic "$work/depth.bas" "$work/dp" > "$work/depth.out" 2>&1
if grep -q 'deep refused: .*256 directories deep' "$work/depth.out"; then
    pass "a tree deeper than 256 is refused, naming the TOP rather than a 512-byte path"
else
    cat "$work/depth.out"; fail "the depth bound did not fire"
fi
if grep -qx 'deep still there: true' "$work/depth.out"; then
    pass "and it removed nothing below the bound"
else
    fail "the refusal removed part of the tree"
fi
# THE CONTROL: without it the tier above is satisfied by refusing every tree.
if grep -qx 'shallow: true' "$work/depth.out" && [ ! -d "$shallow" ]; then
    pass "CONTROL: an ordinary four-level tree is still removed"
else
    cat "$work/depth.out"; fail "an ordinary tree was not removed"
fi

printf 'TIER valgrind\n'
if vg_available; then
    rm -rf "$work/vg"; mkdir -p "$work/vg/tree/a/b"
    echo x > "$work/vg/tree/a/b/f.txt"
    ln -s "$work/vg" "$work/vg/tree/a/loop_link"
    cat > "$work/vg.bas" <<'BAS'
program main(args)
    on error goto next
    t {dir}= args[0] + "/tree"
    print string(remove_tree(t))
    f = temp_file({ in: {dir}args[0], prefix: "vg-" })
    d = temp_dir({ in: {dir}args[0] })
    print string(remove_tree(d))
    delete(f)
    r = temp_file({ prefx: "typo" })
    if error then
        error.clear()
    end if
    g = temp_file()
    delete(g)
end program
BAS
    if vg_run ./gbasic "$work/vg.bas" "$work/vg" >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access"
    else
        tail -30 "$work/vg.err"; fail "valgrind objected"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

exit $status
