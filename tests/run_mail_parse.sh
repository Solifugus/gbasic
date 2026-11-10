#!/usr/bin/env bash
# mail.parse (book gap G59): a received message read -- headers unfolded and
# decoded, every part with its type and decoded body, the text a person would
# read, and every problem by name -- with mail.unflow (RFC 3676) and
# mail.own_text (the text above a quotation).
#
# THE ORACLE IS PYTHON'S email PACKAGE, reading each message from its bytes
# (email.policy.default); nothing gBASIC wrote decides what a message means.
# The messages are made per run from fixed seeds by tests/mail/make_mail.py,
# half written byte by byte by its own rules and half by Python's
# EmailMessage, and it refuses a corpus missing any form it claims to make.
#
# Python and the RFCs part in three places, and the RFC is followed:
#   - RFC 2047 6.2: white space between adjacent encoded words is dropped.
#     Python's address parser keeps a space there in a display name ("Anna
#     Kowalsk a"), so names from headers holding encoded words are compared
#     with their white space removed, and tier 1 holds the joining by hand.
#   - RFC 2045 6.7: a decoder deletes trailing white space on a quoted-
#     printable line, so "= " ending a line is a soft break. binascii keeps
#     it; check_mail.py applies the RFC's rule before decoding.
#   - RFC 5322 4.3: a two-digit year of 50-99 is 19xx. Python adds 2000 to
#     50-68. The corpus has no such year; tier 1 has one.
#
# Tiers:
#   1. SEMANTICS -- tests/mail/parse_test.bas, by hand from the RFCs: encoded
#      words (RFC 2047 section 8's table), unfolding, addresses and groups,
#      dates and their obsolete forms, charsets, quoted-printable, multipart
#      edges, get_body's choice, RFC 2231 parameters, format=flowed, own_text,
#      what mail.compose writes read back (CRLF too), and every problem.
#      Floor 130 checks, stderr empty.
#   2. ORACLE -- three seeds of 400 messages, every field check_mail.py names.
#   3. VALGRIND, over tier 1.
#
# The oracle tier skips without python3.
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -u
cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"
export GBASIC_PATH=stdlib

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_mail_parse: build failed\n'
    exit 1
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
status=0
fail() { printf 'FAIL %s\n' "$1"; status=1; }

printf -- '-- semantics\n'
out=$(timeout 120 ./gbasic tests/mail/parse_test.bas 2>"$work/err")
n=$(printf '%s\n' "$out" | sed -n 's/^checks: \([0-9]*\)$/\1/p')
if [ "$(printf '%s\n' "$out" | tail -1)" = "mismatches: 0" ] && [ "${n:-0}" -ge 130 ] && [ ! -s "$work/err" ]; then
    printf 'PASS semantics (%s checks)\n' "$n"
else
    printf '%s\n' "$out" | grep -v '^ok ' | head -20
    head -5 "$work/err"
    fail "semantics (floor 130 checks, stderr empty)"
fi

printf -- '-- oracle: Python email reads the same bytes\n'
if command -v python3 >/dev/null 2>&1; then
    for seed in 1 2 3; do
        dir="$work/corpus$seed"
        mkdir -p "$dir"
        if ! made=$(python3 -I tests/mail/make_mail.py "$dir" 400 "$seed" 2>&1); then
            printf '%s\n' "$made"
            fail "corpus $seed could not be made"
            continue
        fi
        if ! timeout 300 ./gbasic tests/mail/mail_emit.bas "$dir" >"$work/got$seed.jsonl" 2>"$work/emit.err" || [ -s "$work/emit.err" ]; then
            head -5 "$work/emit.err"
            fail "mail_emit over corpus $seed"
            continue
        fi
        if o=$(python3 -I tests/mail/check_mail.py "$dir" "$work/got$seed.jsonl" 2>&1); then
            printf 'PASS oracle seed %s (%s; %s, %s checks)\n' "$seed" "$(printf '%s\n' "$made" | sed 's/^make_mail: //')" \
                "$(printf '%s\n' "$o" | sed -n 's/^messages: [0-9]*, //p')" "$(printf '%s\n' "$o" | sed -n 's/^checks: //p')"
        else
            printf '%s\n' "$o" | cut -c1-400 | head -15
            fail "oracle seed $seed"
        fi
    done
else
    printf 'SKIP oracle (no python3: nothing was held to an outside reader)\n'
fi

if vg_available; then
    if timeout 900 bash -c '. tests/valgrind_tier.sh; vg_run ./gbasic tests/mail/parse_test.bas' >/dev/null 2>"$work/vg"; then
        printf 'PASS valgrind tests/mail/parse_test.bas\n'
    else
        printf 'FAIL valgrind\n'; head -20 "$work/vg"; status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit $status
