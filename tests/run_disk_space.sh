#!/usr/bin/env bash
# disk_space(path) (Transward §11.1): the filesystem holding a path, as
# { total, free, available } in exact integer bytes.
#
# THE ORACLE IS OUTSIDE gBASIC: Python's os.statvfs and `df -B1`, two other
# programs asking the kernel. `total` must equal both exactly. `free` and
# `available` move while anything writes, so they must lie within 64 MiB of
# Python's, read just before and just after; and `available` must be below
# `free` exactly when Python's is (a filesystem keeping a reserve for root).
#
# Tiers:
#   1. AGREEMENT -- the repository's filesystem, /tmp, /dev/shm and / where
#      present, a file (its filesystem's answer), {file} and {dir} references;
#      each answer exact (serialize, against a double's), ordered.
#   2. REFUSALS -- a path that is not there raises naming it, a NUL, a number,
#      no argument; has_builtin answers true.
#   3. VALGRIND.
# Skips the agreement tier without python3, saying nothing was compared.
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -u
cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"
root=$(pwd)

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_disk_space: build failed\n'
    exit 1
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
status=0
printf 'x\n' >"$work/a.txt"
paths=("$root" "$work/a.txt")
for p in /tmp /dev/shm /; do [ -d "$p" ] && paths+=("$p"); done
missing="$work/not/here"

if ! ./gbasic tests/disk_space_test.bas "$work/a.txt" "${paths[@]}" -- "$missing" >"$work/out" 2>"$work/err" || [ -s "$work/err" ]; then
    printf 'FAIL tests/disk_space_test.bas\n'
    head -5 "$work/err"
    exit 1
fi

printf -- '-- agreement with os.statvfs and df\n'
if command -v python3 >/dev/null 2>&1; then
    if python3 -I - "$work/out" <<'PY'
import os, subprocess, sys
bad, n = [], 0
lines = open(sys.argv[1]).read().splitlines()
rows = [l.split("|") for l in lines if "|" in l]
for path, total, free, avail in rows:
    total, free, avail = int(total), int(free), int(avail)
    before, after = os.statvfs(path), None
    after = os.statvfs(path)
    t = before.f_blocks * before.f_frsize
    df = subprocess.run(["df", "-P", "-B1", path], capture_output=True, text=True).stdout.splitlines()[-1].split()
    slack = 64 << 20
    def near(got, a, b):
        return min(a, b) - slack <= got <= max(a, b) + slack
    checks = [
        ("total = statvfs's", total == t),
        ("total = df's", total == int(df[1])),
        ("free near statvfs's", near(free, before.f_bfree * before.f_frsize, after.f_bfree * after.f_frsize)),
        ("available near statvfs's", near(avail, before.f_bavail * before.f_frsize, after.f_bavail * after.f_frsize)),
        ("available near df's", near(avail, int(df[3]), int(df[3]))),
        ("a reserve exactly when statvfs has one", (avail < free) == (before.f_bavail < before.f_bfree)),
    ]
    for label, ok in checks:
        n += 1
        if not ok:
            bad.append(f"{path}: {label} (gBASIC {total} {free} {avail}; statvfs {t} "
                       f"{before.f_bfree * before.f_frsize} {before.f_bavail * before.f_frsize}; df {df[1]} {df[3]})")
for l in lines:
    if l.startswith("  "):
        n += 1
        if not l.endswith(" true"):
            bad.append(l.strip())
for b in bad:
    print("MISMATCH " + b)
print(f"PASS {len(rows)} paths, {n} checks" if not bad and len(rows) >= 3 else f"FAIL {len(bad)} of {n}")
sys.exit(1 if bad or len(rows) < 3 else 0)
PY
    then :; else status=1; fi
else
    printf 'SKIP agreement (no python3; no answer was compared)\n'
fi

printf -- '-- refusals\n'
want() {  # label, line wanted exactly
    if grep -qxF -- "$2" "$work/out"; then printf 'PASS %s\n' "$1"; else
        printf 'FAIL %s\n  want: %s\n' "$1" "$2"; grep -v '|' "$work/out" | sed 's/^/  got:  /'; status=1; fi
}
want "a file reference" "file ref true"
want "a folder reference" "dir ref true"
want "a path not there names it" "missing: disk_space could not read $missing (No such file or directory)"
want "a NUL is refused" "nul: true"
want "a number is refused" "number: disk_space expects a string, file reference, or directory reference"
want "no argument" "none: disk_space expects one path argument, a file or folder on the filesystem asked about"
want "has_builtin" "has_builtin true"

if vg_available; then
    if vg_run ./gbasic tests/disk_space_test.bas "$work/a.txt" "$root" -- "$missing" >/dev/null 2>"$work/vg"; then
        printf 'PASS valgrind\n'
    else
        printf 'FAIL valgrind\n'; head -20 "$work/vg"; status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi
exit $status
