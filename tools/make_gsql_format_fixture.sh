#!/bin/bash
# Writes tests/gsql/format1/, the committed format-1 database every later
# build must open and read exactly (tests/run_gsql_store.sh's format tier;
# docs/gsql_storage_design.md §3.1.5). Run it ONCE, when a format is fixed:
# rewriting the fixture with a later build would test that build against
# itself, which is the thing the fixture exists not to do. A new major gets a
# new directory (format2/) beside this one, which stays.
set -euo pipefail
cd "$(dirname "$0")/.."
out=tests/gsql/format1
if [ -e "$out" ] && [ "${1:-}" != --replace ]; then
    echo "refusing: $out exists, and a committed format fixture is not regenerated (--replace to override)" >&2
    exit 2
fi
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cc -std=c11 -O1 -Iinclude -Isrc/gsql -o "$work/format_test" tests/gsql/format_test.c \
    src/gsql/lsm.c src/gsql/store_common.c src/gsql/store_util.c src/gsql/io.c src/gsql/io_sim.c
"$work/format_test" write "$work/db"
rm -f "$work/db/db.lock" # the open makes it; it holds nothing
rm -rf "$out"
cp -r "$work/db" "$out"
python3 tests/gsql/format_check.py "$out"
ls -l "$out"
