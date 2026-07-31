#!/usr/bin/env bash
# persist — crash-safe, versioned persistence (stdlib/persist.bas).
#
# These cases came from Studio's STU-STORE tier and stayed behind when Studio
# moved to its own project: they exercise `persist`, which is a gBASIC library,
# not anything about Studio.
#
# What they pin: the three read states are unchanged (missing / corrupt /
# loaded); a corrupt file reports WHY, with the parser's reason and position;
# every value shape round-trips through write_atomic byte-identically; and a
# realistic worst-case index (115 KB, 240 records) opens in under 5 s. That last
# one is the reason the library exists in this form -- the pure-gBASIC validator
# it replaced took 92 s on the same input, because `mid` was quadratic then.
set -u

cd "$(dirname "$0")/.."

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_persist: build failed\n'
    exit 1
fi

stdout_file="$(mktemp)"
tmproot="$(mktemp -d)"
trap 'rm -f "$stdout_file"; rm -rf "$tmproot"' EXIT

status=0
DRIVER=examples/persist_store_test.bas

for m in status dialect roundtrip speed; do
    d="$tmproot/$m"
    rm -rf "$d"; mkdir -p "$d"
    : >"$stdout_file"
    if ! timeout 120 ./gbasic "$DRIVER" "$m" "$d" >"$stdout_file" 2>&1; then
        cat "$stdout_file"
        printf 'FAIL persist_%s (nonzero exit)\n' "$m"
        status=1
        continue
    fi
    if diff -u "tests/persist/$m.out" "$stdout_file"; then
        printf 'PASS persist_%s\n' "$m"
    else
        printf 'FAIL persist_%s (output diff)\n' "$m"
        status=1
    fi
done

exit "$status"
