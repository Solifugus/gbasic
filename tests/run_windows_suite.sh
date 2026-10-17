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
