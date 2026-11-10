#!/usr/bin/env bash
# The zip module (src/modules/zip.c; gap G11 from the gBASIC books): listing,
# reading and extracting the members of a ZIP file.
#
# Tiers:
#   1. AGREEMENT -- tests/make_zip_cases.py makes archives with Python's
#      zipfile and writes zipfile's answer for every member (name, size, CRC,
#      method, folder, the bytes as hex); tests/zip/list.bas prints the same
#      from zip.members and zip.read, and the two must be identical. Stored
#      and deflated members, a folder, an empty member, NUL bytes, a UTF-8
#      name, a cp437 name (flag bit 11 clear), a member written through a data
#      descriptor (its local header's sizes zeroed), an archive comment, and
#      40 random archives. The answers are zipfile's, never gBASIC's.
#   2. REFUSALS AND EXTRACT -- tests/zip/zip_test.bas over the defect cases,
#      one defect each: two members of one name, ../ and absolute names, a
#      symbolic link, bzip2, a wrong CRC-32, an encrypted member, a truncated
#      file. An archive with one unsafe name writes nothing; a file already
#      there is refused unless replace: true. Self-checking; every answer is
#      written there by hand.
#   3. WRITING (zip.write, gap G55) -- tests/make_zip_write_cases.py chooses
#      20 archives' members (names in UTF-8 and ASCII, folders, empty, NUL
#      bytes, data that deflates and data that does not); tests/zip/write.bas
#      writes them; tests/zip/check_written.py reads each back with zipfile
#      (names in order, bytes, CRCs, 1980-01-01, no extra fields), and
#      `unzip -t` tests each. tests/zip/write_test.bas: refusals each before a
#      byte is written, the same members giving the same bytes, and a
#      workbook's part edited and put back (when the build has xlsx).
#   4. VALGRIND, over all of them.
#
# Skips without python3, or when the build has no zlib.
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -u
cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"
root=$(pwd)

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_zip: build failed\n'
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    printf 'SKIP run_zip (no python3: the oracle is its zipfile module; nothing was measured)\n'
    exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
status=0

printf 'print zip.members("none.zip")\n' >"$work/probe.bas"
if "$root/gbasic" "$work/probe.bas" 2>&1 | grep -q 'not available in this build'; then
    printf 'SKIP run_zip (built without zlib; nothing was measured)\n'
    exit 0
fi

mkdir -p "$work/cases"
if ! python3 -I "$root/tests/make_zip_cases.py" "$work/cases" 20261105 >"$work/make.out" 2>&1; then
    printf 'FAIL run_zip: the fixture generator failed\n'
    cat "$work/make.out"
    exit 1
fi

printf -- '-- agreement with Python zipfile\n'
mapfile -t archives < <(cd "$work/cases" && ls plain.zip cp437.zip stream.zip random_*.zip | sort -V)
names=$(cd "$work/cases" && ls plain.zip cp437.zip stream.zip random_*.zip | sort -V | tr '\n' ' ')
# shellcheck disable=SC2086
if (cd "$work/cases" && timeout 120 "$root/gbasic" "$root/tests/zip/list.bas" $names) >"$work/got.txt" 2>"$work/err.txt"; then
    # zipfile's lines in the same archive order
    for a in "${archives[@]}"; do grep "^$a|" "$work/cases/expect.txt"; done >"$work/want.txt"
    nmem=$(wc -l <"$work/want.txt" | tr -d ' ')
    if [ "$nmem" -gt 200 ] && cmp -s "$work/want.txt" "$work/got.txt"; then
        printf 'PASS zip.members and zip.read agree with zipfile on all %s members of %s archives\n' "$nmem" \
            "${#archives[@]}"
    else
        printf 'FAIL zip agreement (%s members expected)\n' "$nmem"
        diff "$work/want.txt" "$work/got.txt" | cut -c1-200 | head -20
        status=1
    fi
else
    printf 'FAIL tests/zip/list.bas (exit)\n'
    head -20 "$work/err.txt"
    status=1
fi

printf -- '-- refusals and extract\n'
zt=$(cd "$work/cases" && timeout 60 "$root/gbasic" "$root/tests/zip/zip_test.bas" 2>&1)
if [ "$(printf '%s\n' "$zt" | tail -1)" = "mismatches: 0" ]; then
    printf 'PASS tests/zip/zip_test.bas (%s)\n' "$(printf '%s\n' "$zt" | head -1)"
else
    printf 'FAIL tests/zip/zip_test.bas\n%s\n' "$zt"
    status=1
fi
# the extract wrote the bytes zipfile reads, checked from outside
if python3 -I - "$work/cases" <<'EOF'
import os, sys, zipfile
base = sys.argv[1]
with zipfile.ZipFile(os.path.join(base, "plain.zip")) as z:
    for i in z.infolist():
        p = os.path.join(base, "out", i.filename)
        if i.is_dir():
            assert os.path.isdir(p), p
        else:
            assert open(p, "rb").read() == z.read(i), p
assert not os.path.exists(os.path.join(base, "evil.txt"))
assert not os.path.exists(os.path.join(base, "slip", "fine.txt"))
EOF
then
    printf 'PASS the extracted files hold the bytes zipfile reads, and nothing escaped the folder\n'
else
    printf 'FAIL the extracted files differ from zipfile, or something escaped\n'
    status=1
fi

printf -- '-- writing\n'
mkdir -p "$work/w" "$work/wt"
ks=$(seq 0 19 | tr '\n' ' ')
xl=""
printf 'x = xlsx.new()\n' >"$work/xprobe.bas"
"$root/gbasic" "$work/xprobe.bas" >/dev/null 2>&1 && xl=xlsx
# shellcheck disable=SC2086
if python3 -I "$root/tests/make_zip_write_cases.py" "$work/w" 20261109 &&
   timeout 120 "$root/gbasic" "$root/tests/zip/write.bas" "$work/w" $ks 2>"$work/err.txt" && [ ! -s "$work/err.txt" ]; then
    # shellcheck disable=SC2086
    cw=$(python3 -I "$root/tests/zip/check_written.py" "$work/w" $ks 2>&1)
    ut=0
    for k in $ks; do unzip -tq "$work/w/case_$k.zip" >/dev/null 2>&1 || ut=1; done
    if [ "$(printf '%s\n' "$cw" | tail -1)" = "mismatches: 0" ] && [ "$(printf '%s\n' "$cw" | sed -n 's/^members: //p')" -ge 100 ] &&
       [ $ut = 0 ]; then
        printf 'PASS zipfile reads every written member as chosen (%s), and unzip -t passes all 20\n' \
            "$(printf '%s\n' "$cw" | head -1)"
    else
        printf 'FAIL the written archives differ from what was chosen (unzip -t failed: %s)\n%s\n' "$ut" "$cw"
        status=1
    fi
else
    printf 'FAIL tests/zip/write.bas\n'
    head -5 "$work/err.txt"
    status=1
fi
wt=$(timeout 60 "$root/gbasic" "$root/tests/zip/write_test.bas" "$work/wt" $xl 2>&1)
if [ "$(printf '%s\n' "$wt" | tail -1)" = "mismatches: 0" ] &&
   [ "$(printf '%s\n' "$wt" | sed -n 's/^checks: //p')" -ge "$([ -n "$xl" ] && echo 19 || echo 17)" ]; then
    printf 'PASS tests/zip/write_test.bas (%s%s)\n' "$(printf '%s\n' "$wt" | head -1)" \
        "$([ -n "$xl" ] || echo '; no xlsx in this build, so no workbook was put back')"
else
    printf 'FAIL tests/zip/write_test.bas\n%s\n' "$wt"
    status=1
fi

if vg_available; then
    rm -rf "$work/cases/out" "$work/cases/slip"
    rm -rf "$work/wt" && mkdir -p "$work/wt"
    if (cd "$work/cases" && vg_run "$root/gbasic" "$root/tests/zip/zip_test.bas" >/dev/null 2>"$work/vg.txt") &&
       (cd "$work/cases" && vg_run "$root/gbasic" "$root/tests/zip/list.bas" plain.zip cp437.zip stream.zip \
            random_0.zip >/dev/null 2>>"$work/vg.txt") &&
       vg_run "$root/gbasic" "$root/tests/zip/write_test.bas" "$work/wt" $xl >/dev/null 2>>"$work/vg.txt" &&
       vg_run "$root/gbasic" "$root/tests/zip/write.bas" "$work/w" 0 1 2 >/dev/null 2>>"$work/vg.txt"; then
        printf 'PASS valgrind\n'
    else
        printf 'FAIL valgrind\n'
        head -30 "$work/vg.txt"
        status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit $status
