#!/usr/bin/env bash
# The native Windows suite (tests/windows/*.bas), run on EVERY platform.
#
# Those files are the gate a Windows machine has -- self-checking gBASIC, the
# same file on both platforms (tests/windows/README.md says why) -- and until
# this runner nothing invoked them: they were run by hand, which is the gate
# CLAUDE.md warns about, the one you have to remember to run and which
# therefore silently shrinks. They are DISCOVERED by glob, so a new file joins
# the gate by existing; `*_child.bas` are the helpers the suites launch, not
# suites. Run from the repository root, as each file expects.
#
# A file passes when its LAST line is exactly `mismatches: 0` -- each prints
# `BROKEN: ...` instead if its own tally counted too few checks, so a suite
# that silently stopped asserting cannot pass here. A last line beginning
# `SKIP` is reported as a skip, with the file's own reason (odbc_unicode needs
# GBASIC_ODBC_CONNECTION). Anything else, or a nonzero exit, is a failure.
set -u
cd "$(dirname "$0")/.."
make >/dev/null || { printf 'FAIL build\n'; exit 1; }

status=0
ran=0
for f in tests/windows/*.bas; do
    case "$f" in *_child.bas) continue ;; esac
    out="$(timeout -k 5 300 ./gbasic "$f" 2>&1)"
    rc=$?
    last="$(printf '%s\n' "$out" | tail -1)"
    if [ "$rc" -eq 0 ] && [ "$last" = "mismatches: 0" ]; then
        printf 'PASS %s (%s)\n' "$f" "$(printf '%s\n' "$out" | grep -c '^ok ')"
        ran=$((ran + 1))
    elif [ "$rc" -eq 0 ] && [ "${last#SKIP}" != "$last" ]; then
        printf '%s\n' "$last"
    elif printf '%s' "$out" | grep -qE 'support is (not available in this build|unavailable)|requires OpenSSL|not available on Windows'; then
        # A module this build lacks (xml_encodings.bas needs libxml2): the
        # binary's own refusal, never a feature under test failing.
        printf 'SKIP %s (%s)\n' "$f" "$(printf '%s' "$out" | grep -m1 -oE '[A-Za-z0-9 -]*(support is [a-z ]*|requires OpenSSL|not available on Windows)')"
    else
        printf 'FAIL %s (exit %s)\n' "$f" "$rc"
        printf '%s\n' "$out" | grep -E 'MISMATCH|BROKEN|error' | head -20
        printf '  last line: %s\n' "$last"
        status=1
    fi
done

# PAST 2 GB. `read_at` and `overwrite` positioned with fseek/ftell, whose
# `long` is 32 bits on Windows, so MEASURED 2026-10-17 both refused EVERY file
# over 2 GB -- even `read_at(f, 0, 4)`, because each asks the file's size
# first and ftell could not answer it. The file is SPARSE (fsutil marks it on
# Windows; truncate makes one natively on Linux), so 3 GB costs no disk and the
# write at 2.5 GB does not zero-fill the gap. Runs on every platform: the
# failure was Windows-only and the property is not.
big="$(mktemp)"
: >"$big"
if command -v fsutil >/dev/null 2>&1; then
    fsutil sparse setflag "$(cygpath -w "$big")" >/dev/null 2>&1 || true
fi
if truncate -s 3G "$big" 2>/dev/null; then
    big_prog="$(mktemp --suffix=.bas)"
    cat >"$big_prog" <<BAS
f {file}= "$(cygpath -m "$big" 2>/dev/null || printf '%s' "$big")"
print "start " + string(len(read_at(f, 0, 4)))
print "write " + string(overwrite(f, "MARKER", 2500000000))
print "past2g " + read_at(f, 2500000000, 6)
print "end [" + read_at(f, 3221225472, 4) + "]"
BAS
    big_out="$(./gbasic "$big_prog" 2>&1)"
    rm -f "$big_prog"
    big_want="$(printf 'start 4\nwrite true\npast2g MARKER\nend []')"
    if [ "$big_out" = "$big_want" ]; then
        printf 'PASS read_at and overwrite reach past 2 GB in a 3 GB file\n'
    else
        printf 'FAIL read_at/overwrite past 2 GB:\n%s\n' "$big_out"
        status=1
    fi
else
    printf 'SKIP the past-2-GB tier: could not make a 3 GB sparse file here, so NOTHING was measured about it\n'
fi
rm -f "$big"

# A LINK TO NOTHING IS NOT THERE. Windows' C-runtime stat does not follow a
# reparse point, so MEASURED 2026-10-17 `exists()` answered true for a dangling
# link and `file_type` and the listing called it a file (§28); gb_stat now
# follows. Tested with a DIRECTORY JUNCTION because a junction needs no
# privilege -- a symlink needs Developer Mode or elevation, so a tier built on
# one would skip on most machines. Windows only: `mklink` is cmd's.
if command -v cmd >/dev/null 2>&1 && command -v cygpath >/dev/null 2>&1; then
    jd="$(mktemp -d)"
    mkdir -p "$jd/target"
    printf 'hello' >"$jd/target/f.txt"
    cmd //c "mklink /J $(cygpath -w "$jd")\\live $(cygpath -w "$jd")\\target" >/dev/null 2>&1
    cmd //c "mklink /J $(cygpath -w "$jd")\\dangling $(cygpath -w "$jd")\\nowhere" >/dev/null 2>&1
    jm="$(cygpath -m "$jd")"
    jprog="$(mktemp --suffix=.bas)"
    cat >"$jprog" <<BAS
w {dir}= "$jm"
dl {dir}= "$jm/dangling"
lv {dir}= "$jm/live"
inner {file}= "$jm/live/f.txt"
print "premise " + string(is_symlink(dl)) + " " + string(is_symlink(lv))
print "dangling " + string(exists(dl)) + " " + string(file_type(dl))
print "live " + string(exists(lv)) + " " + string(file_type(lv)) + " " + read(inner)
for each e in list(w)
    if e.name = "dangling" then
        print "listed " + string(e.type)
    end if
end for
BAS
    jout="$(./gbasic "$jprog" 2>&1)"
    rm -f "$jprog"
    cmd //c "rmdir $(cygpath -w "$jd")\\live & rmdir $(cygpath -w "$jd")\\dangling" >/dev/null 2>&1
    rm -rf "$jd"
    jwant="$(printf 'premise true true\ndangling false unknown\nlive true folder hello\nlisted unknown')"
    if [ "$jout" = "$jwant" ]; then
        printf 'PASS a dangling junction does not exist; a live one is its target\n'
    elif printf '%s\n' "$jout" | grep -qx 'premise true true'; then
        printf 'FAIL links are not followed:\n%s\n' "$jout"
        status=1
    else
        printf 'FAIL the junctions were not created, so NOTHING was measured:\n%s\n' "$jout"
        status=1
    fi
fi

# remove_tree OVER WHAT WINDOWS GUARDS DIFFERENTLY (§28 predictions 1 and 2,
# §29 items 3 and 4). A READ-ONLY FILE and a READ-ONLY DIRECTORY stopped the
# walk with EACCES, where POSIX removes both; a DIRECTORY JUNCTION inside the
# tree was refused, because `unlink` cannot remove a directory reparse point.
# The junction case asserts the TARGET'S CONTENT survives, since descending
# into the link would empty it -- the outcome that must never happen. Windows
# only, for `mklink`.
if command -v cmd >/dev/null 2>&1 && command -v cygpath >/dev/null 2>&1; then
    rd="$(mktemp -d)"
    mkdir -p "$rd/precious" "$rd/jtree" "$rd/dtree/inner"
    printf 'keep' >"$rd/precious/keep.txt"
    cmd //c "mklink /J $(cygpath -w "$rd")\\jtree\\jlink $(cygpath -w "$rd")\\precious" >/dev/null 2>&1
    rm_="$(cygpath -m "$rd")"
    rprog="$(mktemp --suffix=.bas)"
    cat >"$rprog" <<BAS
on warning ignore
ftree {dir}= "$rm_/ftree"
make_dir(ftree)
ro {file}= "$rm_/ftree/readonly.txt"
write(ro, "locked")
chmod(ro, "0400")
print "premise file " + string(not permissions(ro).writable)
print "file " + string(remove_tree(ftree)) + " " + string(not exists(ftree))
inner {dir}= "$rm_/dtree/inner"
chmod(inner, "0400")
dtree {dir}= "$rm_/dtree"
print "dir " + string(remove_tree(dtree)) + " " + string(not exists(dtree))
jl {dir}= "$rm_/jtree/jlink"
print "premise link " + string(is_symlink(jl))
jtree {dir}= "$rm_/jtree"
print "junction " + string(remove_tree(jtree)) + " " + string(not exists(jtree))
keep {file}= "$rm_/precious/keep.txt"
print "target " + read(keep)
BAS
    rout="$(./gbasic "$rprog" 2>&1)"
    rm -f "$rprog"
    attrib -r "$(cygpath -w "$rd")\\*" //s //d >/dev/null 2>&1
    cmd //c "rmdir /s /q $(cygpath -w "$rd")" >/dev/null 2>&1
    rwant="$(printf 'premise file true\nfile true true\ndir true true\npremise link true\njunction true true\ntarget keep')"
    if [ "$rout" = "$rwant" ]; then
        printf 'PASS remove_tree clears read-only files and folders, and removes a junction without following it\n'
    else
        printf 'FAIL remove_tree over read-only items and a junction:\n%s\n' "$rout"
        status=1
    fi
fi

# A TEMP FILE IS OWNER-ONLY ON WINDOWS TOO (§29 item 2). MEASURED 2026-10-17
# (§28): created with the inherited ACL, a temp file under a shared TEMP was
# readable and modifiable by INTERACTIVE, SERVICE and BATCH. The folder here is
# made deliberately LEAKY -- BUILTIN\Users granted Modify, inheritable -- and a
# temp_file and a temp_dir made `in:` it must each carry exactly ONE access
# entry, for the current user. The premise is asserted too: a plain file
# created in that folder must show the Users grant, or the folder was never
# leaky and the check would pass on a build that changed nothing.
if command -v icacls >/dev/null 2>&1 && command -v cygpath >/dev/null 2>&1; then
    ad="$(mktemp -d)"
    adw="$(cygpath -w "$ad")"
    icacls "$adw" //grant '*S-1-5-32-545:(OI)(CI)M' >/dev/null 2>&1
    printf 'x' >"$ad/plain.txt"
    aprog="$(mktemp --suffix=.bas)"
    cat >"$aprog" <<BAS
f = temp_file({ in: "$(cygpath -m "$ad")" })
d = temp_dir({ in: "$(cygpath -m "$ad")" })
print string(f)
print string(d)
BAS
    aout="$(./gbasic "$aprog" 2>&1)"
    rm -f "$aprog"
    me="$(whoami)"
    # icacls prints the path, then one ACE per line; the last two lines are a
    # blank and the "Successfully processed" summary.
    aces() { icacls "$(cygpath -w "$1")" 2>/dev/null | head -n -2 | sed "s|^.*$(basename "$1") *||; s|^ *||" | grep -v '^$'; }
    premise="$(aces "$ad/plain.txt" | grep -ci 'BUILTIN\\Users')"
    tf="$(printf '%s\n' "$aout" | sed -n 1p)"
    td="$(printf '%s\n' "$aout" | sed -n 2p)"
    fa="$(aces "$tf")"; da="$(aces "$td")"
    if [ "${premise:-0}" -lt 1 ]; then
        printf 'FAIL the folder was not leaky (no Users grant on a plain file), so NOTHING was measured\n'
        status=1
    elif [ "$(printf '%s\n' "$fa" | wc -l)" -eq 1 ] && printf '%s' "$fa" | grep -qi "\\\\$me:" &&
         [ "$(printf '%s\n' "$da" | wc -l)" -eq 1 ] && printf '%s' "$da" | grep -qi "\\\\$me:"; then
        printf 'PASS temp_file and temp_dir are owner-only even in a folder that grants Users\n'
    else
        printf 'FAIL temp_file/temp_dir are not owner-only:\n  file: %s\n  dir:  %s\n  (gbasic said: %s)\n' \
            "$(printf '%s' "$fa" | tr '\n' ';')" "$(printf '%s' "$da" | tr '\n' ';')" "$aout"
        status=1
    fi
    rm -rf "$ad"
fi

# THE WINDOWS FILE VERSION MUST BE THE BINARY'S VERSION. src/gbasic.manifest
# carries one by hand (it is what Windows shows in a file's Properties, and the
# MSIX build reads `gbasic --version` instead); it sat at 0.4.0.0 while the
# binary said 0.5.1, found only when master was merged. Asked of the source on
# every platform, since the manifest is a source file.
gb_version="$(grep -o 'printf("gBASIC [0-9][0-9.]*[0-9]' src/main.c | head -1 | sed 's/.* //')"
manifest_version="$(sed -n 's/.*name="gBASIC.gbasic" version="\([0-9.]*\)".*/\1/p' src/gbasic.manifest)"
if [ -n "$gb_version" ] && [ "$manifest_version" = "$gb_version.0" ]; then
    printf 'PASS src/gbasic.manifest version %s matches gbasic %s\n' "$manifest_version" "$gb_version"
else
    printf 'FAIL src/gbasic.manifest says %s but gbasic --version says %s (want %s.0)\n' \
        "${manifest_version:-nothing}" "${gb_version:-nothing}" "$gb_version"
    status=1
fi

# smoke, process_run and process_start need nothing optional; a run that
# passed fewer than those three passed nothing it should have.
if [ "$ran" -lt 3 ]; then
    printf 'FAIL only %d of the always-runnable suites passed\n' "$ran"
    status=1
fi
[ "$status" -eq 0 ] && printf 'run_windows_suite: all passed\n'
exit "$status"
