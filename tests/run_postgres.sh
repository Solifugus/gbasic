#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

if [[ "${GBASIC_POSTGRES_TEST:-0}" != "1" ]]; then
    printf 'SKIP tests/postgres_integration.bas (set GBASIC_POSTGRES_TEST=1 and standard PG* connection variables)\n'
    exit 0
fi

make

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
trap 'rm -f "$stdout_file" "$stderr_file"' EXIT

if ./gbasic tests/postgres_integration.bas >"$stdout_file" 2>"$stderr_file"; then
    if diff -u tests/postgres_integration.out "$stdout_file"; then
        printf 'PASS tests/postgres_integration.bas\n'
    else
        exit 1
    fi
else
    status=$?
    cat "$stderr_file"
    exit "$status"
fi

# NATIVE POSTGRES ARRAYS, both directions, with POSTGRES AS THE ORACLE: an
# array parameter is read back through array_length/unnest/array_dims, so
# what is asserted is what the SERVER parsed rather than what our reader
# makes of our writer -- a round trip alone passes on a matched pair of bugs.
# Proven red on the pair that matters: with the backslash unescaped,
# `back\slash` arrives as `backslash`, a plausible string only the oracle
# and the round trip can see. Self-checking rather than golden for the same
# reason. The pgvector tier skips itself when the extension is absent from
# $PGDATABASE; the cost tier gates the prepare+describe round trip at 5x a
# bare query (measured 1.3x).
#
# This file was the NEGATIVE CONTROL for the limitation it now tests the
# removal of: until 2026-09-05 an array result raised and an array parameter
# went out as JSON, both undocumented, found checking the AI reference
# proposal against the tree. The control went red the day arrays landed,
# which is how the reference paragraph got rewritten instead of rotting.
if ./gbasic tests/postgres_arrays.bas >"$stdout_file" 2>"$stderr_file"; then
    if grep -q '^mismatches: 0$' "$stdout_file" \
       && [ "$(sed -n 's/^checks: //p' "$stdout_file")" -ge 45 ]; then
        printf 'PASS tests/postgres_arrays.bas (%s checks%s)\n' \
            "$(sed -n 's/^checks: //p' "$stdout_file")" \
            "$(grep -q '^SKIP pgvector' "$stdout_file" && printf ', pgvector tiers skipped')"
    else
        printf 'FAIL tests/postgres_arrays.bas\n'
        grep -E '^MISMATCH|^checks|^mismatches' "$stdout_file"
        exit 1
    fi
else
    status=$?
    printf 'FAIL tests/postgres_arrays.bas (exit %s)\n' "$status"
    cat "$stderr_file"
    exit "$status"
fi

# --- AN INTERIOR NUL IS REFUSED, NOT TRUNCATED ------------------------------
#
# PLAT-NUL's standing lesson reached the database parameter last: binding
# `"a" + chr(0) + "b"` used to store ONE byte, measured by asking the server
# rather than our own reader. `sqlite` was repaired -- it stores the bytes
# happily -- and `pg` cannot be, because PostgreSQL's `text` cannot hold the
# byte at all: `select octet_length(E'a\000b'::text)` answers
# `invalid byte sequence for encoding "UTF8": 0x00`.
#
# libpq's own `paramLengths` is documented as IGNORED for text-format
# parameters, so there is no wire fix either. When a value cannot arrive
# whole by any route, saying so is the only honest answer -- and it is the
# difference between a caller who knows and a row that is quietly wrong.
cat > "$stdout_file.nul.bas" <<'BAS'
load pg
program main()
    on error goto next
    c = pg.connect({ database: "" })
    pg.exec(c, "select $1::text", ["a" + chr(0) + "b"])
    if error then
        print "refused: " + error.message
    else
        print "ACCEPTED -- the NUL was silently lost"
    end if
end program
BAS
out="$(GBASIC_PATH=stdlib ./gbasic "$stdout_file.nul.bas" 2>&1)"
case "$out" in
    *"cannot hold an interior NUL"*)
        printf 'PASS an interior NUL is refused rather than truncated\n' ;;
    *)
        printf 'FAIL an interior NUL is not refused: %s\n' "$out"; exit 1 ;;
esac
# THE CONTROL: an ordinary string still binds. Without it, "refuses a NUL" is
# satisfied by a build that refuses every text parameter there is.
cat > "$stdout_file.ok.bas" <<'BAS'
load pg
program main()
    c = pg.connect({ database: "" })
    r = pg.query(c, "select $1::text as v", ["plain"])
    print "bound: " + r[0].v
end program
BAS
out="$(GBASIC_PATH=stdlib ./gbasic "$stdout_file.ok.bas" 2>&1)"
case "$out" in
    *"bound: plain"*) printf 'PASS and an ordinary text parameter still binds\n' ;;
    *) printf 'FAIL an ordinary text parameter broke: %s\n' "$out"; exit 1 ;;
esac
rm -f "$stdout_file.nul.bas" "$stdout_file.ok.bas"
