#!/usr/bin/env bash
# NAP-10 filesystem metadata + atomic replacement — cases that need shell setup
# the golden example suite can't express: an exact controlled mtime, an observed
# mtime change, atomic_replace failure leaving the destination intact, and the
# cross-device (EXDEV) error path. GI-independent, so it never skips wholesale;
# only the cross-device and opt-in stress cases skip when their environment is
# unavailable. The always-on structural coverage lives in examples/nap_fs_test.gb.
set -euo pipefail

cd "$(dirname "$0")/.."

make >/dev/null

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

fail() { printf 'FAIL %s\n' "$1"; exit 1; }

# 1. Exact mtime: stamp a known time, read it back byte-for-byte. TZ=UTC makes
#    the local-time rendering deterministic regardless of the host timezone.
f="$work/stamped.txt"
: >"$f"
touch -d "2020-01-01 00:00:00 UTC" "$f"
cat >"$work/mtime_exact.bas" <<EOF
target{file}= "$f"
print file_mtime(target)
EOF
got="$(TZ=UTC ./gbasic "$work/mtime_exact.bas")"
if [[ "$got" == "2020-01-01 00:00:00" ]]; then
    printf 'PASS mtime-exact (%s)\n' "$got"
else
    printf 'expected 2020-01-01 00:00:00, got %s\n' "$got"
    fail mtime-exact
fi

# 2. mtime tracks the on-disk timestamp: re-stamp to a later time, value changes.
touch -d "2021-06-15 12:34:56 UTC" "$f"
got2="$(TZ=UTC ./gbasic "$work/mtime_exact.bas")"
if [[ "$got2" == "2021-06-15 12:34:56" ]]; then
    printf 'PASS mtime-changes (%s)\n' "$got2"
else
    printf 'expected 2021-06-15 12:34:56, got %s\n' "$got2"
    fail mtime-changes
fi

# 3. Failure safety: a failing atomic_replace must not touch the destination.
dest="$work/dest.txt"
printf 'ORIGINAL' >"$dest"
cat >"$work/replace_fail.bas" <<EOF
src{file}= "$work/does-not-exist.tmp"
atomic_replace(src, "$dest")
EOF
if ./gbasic "$work/replace_fail.bas" >/dev/null 2>"$work/err.txt"; then
    fail failure-safety-expected-error
fi
if ! grep -q "could not atomically replace file" "$work/err.txt"; then
    printf 'unexpected error text:\n'; cat "$work/err.txt"
    fail failure-safety-error-text
fi
if [[ "$(cat "$dest")" == "ORIGINAL" ]]; then
    printf 'PASS failure-safety (destination intact)\n'
else
    printf 'destination was modified by a failed replace: %s\n' "$(cat "$dest")"
    fail failure-safety-dest-intact
fi

# 4. Cross-device: only meaningful when a second filesystem exists. /dev/shm is
#    tmpfs on Linux; run the case only if it is genuinely a different device.
shm_dev=""
work_dev="$(stat -c %d "$work" 2>/dev/null || echo A)"
if [[ -d /dev/shm && -w /dev/shm ]]; then
    shm_dev="$(stat -c %d /dev/shm 2>/dev/null || echo A)"
fi
if [[ -n "$shm_dev" && "$shm_dev" != "$work_dev" ]]; then
    xtmp="$(mktemp /dev/shm/nap_fs_xdev.XXXXXX)"
    printf 'NEWDATA' >"$xtmp"
    xdest="$work/xdev_dest.txt"
    printf 'KEEP' >"$xdest"
    cat >"$work/replace_xdev.bas" <<EOF
src{file}= "$xtmp"
atomic_replace(src, "$xdest")
EOF
    if ./gbasic "$work/replace_xdev.bas" >/dev/null 2>"$work/xerr.txt"; then
        rm -f "$xtmp"
        fail xdev-expected-error
    fi
    ok=1
    grep -q "same filesystem" "$work/xerr.txt" || ok=0
    [[ "$(cat "$xdest")" == "KEEP" ]] || ok=0         # destination untouched
    [[ -f "$xtmp" && "$(cat "$xtmp")" == "NEWDATA" ]] || ok=0  # source untouched
    rm -f "$xtmp"
    if [[ "$ok" == 1 ]]; then
        printf 'PASS xdev-error (EXDEV, both sides intact)\n'
    else
        printf 'cross-device error text:\n'; cat "$work/xerr.txt"
        fail xdev-error
    fi
else
    printf 'SKIP xdev-error (no distinct second filesystem available for a deterministic fixture)\n'
fi

# 5. Concurrent-atomicity stress (opt-in; kept out of the mandatory path because
#    concurrency timing, not correctness, makes it environment-sensitive). A
#    writer atomically replaces the destination between two complete payloads of
#    different lengths while a reader repeatedly reads it; every read must be one
#    whole payload or the other, never partial or empty. rename(2) guarantees
#    this on one filesystem, so a violation is a real regression.
if [[ "${NAP_FS_STRESS:-0}" == "1" ]]; then
    sdest="$work/stress_dest.txt"
    payload_a="$(printf 'A%.0s' {1..4096})"
    payload_b="B"
    printf '%s' "$payload_a" >"$sdest"
    cat >"$work/stress_writer.bas" <<EOF
sa{file}= "$work/sa.tmp"
sb{file}= "$work/sb.tmp"
i = 0
while i < 2000
    write(sa, "$payload_a")
    atomic_replace(sa, "$sdest")
    write(sb, "$payload_b")
    atomic_replace(sb, "$sdest")
    i = i + 1
end while
done_flag{file}= "$work/done.flag"
write(done_flag, "done")
EOF
    ./gbasic "$work/stress_writer.bas" >/dev/null 2>&1 &
    writer_pid=$!
    reads=0
    while [[ ! -f "$work/done.flag" ]]; do
        content="$(cat "$sdest" 2>/dev/null || true)"
        if [[ "$content" != "$payload_a" && "$content" != "$payload_b" ]]; then
            kill "$writer_pid" 2>/dev/null || true
            printf 'reader observed a partial/empty payload (len=%s)\n' "${#content}"
            fail atomicity-stress
        fi
        reads=$((reads + 1))
    done
    wait "$writer_pid" 2>/dev/null || true
    printf 'PASS atomicity-stress (%s reads, always a whole payload)\n' "$reads"
else
    printf 'SKIP atomicity-stress (set NAP_FS_STRESS=1 to run the concurrent reader/writer stress)\n'
fi
