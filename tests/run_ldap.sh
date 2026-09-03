#!/usr/bin/env bash
set -uo pipefail

# the `ldap` module (docs/ldap_design.md) -- bind and search, and nothing else.
# Raised as a cross-repo ask by gdash, whose identity tier 2 is LDAP(S) bind
# against AD and which had no way to reach a directory at all.
#
# THIS IS AN AUTHENTICATION PATH, and the failure that matters is not a crash.
# It is a bind that says the wrong thing: a directory that is DOWN reported as
# a bad password locks every user out while telling the operator nothing, and a
# bad password reported as unreachable is worse. So the load-bearing tier is
# DISTINGUISHABILITY -- invalid_credentials, unreachable and tls_failed must be
# three different answers, asserted as differences rather than as values, since
# any one of them alone is satisfied by a module that always says it.
#
# That requirement is why `bind` returns a VALUE rather than raising: a caller
# has to read `reason` to learn anything at all, so the two cannot be conflated
# by accident.
#
# TESTED AGAINST A MOCK, AND THE LIMIT IS REAL. tests/ldap/mock_ldap.py speaks
# genuine BER over a genuine socket -- libldap encodes and decodes against it
# exactly as against a directory, and the mock must parse real BindRequest and
# SearchRequest messages to answer. It is NOT a directory: no referrals, no
# aliases, no controls, no size limits. THIS MODULE HAS NEVER MET ACTIVE
# DIRECTORY OR OpenLDAP, no such server is reachable from here, and
# docs/ldap_design.md §8 says so rather than leaving it to be discovered.
#
# The mock honours the requested attribute list, which is not decoration:
# without it, asking for two attributes and asking for none look identical from
# the server side and the whole feature would go untested.
#
# Skips cleanly when LDAP is compiled out, or without python3 or openssl.

cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"
make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }

scratch="$(mktemp -d)"
cleanup() { [ -n "${P:-}" ] && kill "$P" 2>/dev/null; [ -n "${S:-}" ] && kill "$S" 2>/dev/null; rm -rf "$scratch"; }
trap cleanup EXIT

checks=0; failures=0
pass() { checks=$((checks+1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks+1)); failures=$((failures+1)); printf '  FAIL %s\n' "$1"; }

printf 'load ldap\nprint 1\n' > "$scratch/probe.bas"
if ./gbasic "$scratch/probe.bas" 2>&1 | grep -q "not available in this build"; then
    echo "SKIP run_ldap (LDAP not in this build)"
    exit 0
fi
command -v python3 >/dev/null || { echo "SKIP run_ldap (no python3)"; exit 0; }
command -v openssl >/dev/null || { echo "SKIP run_ldap (no openssl)"; exit 0; }

# Ports are OS-assigned nowhere here, so pick high ones and fail loudly if busy.
PLAIN=13911; TLS=13912; DEAD=13999

openssl req -x509 -newkey rsa:2048 -keyout "$scratch/server.key" \
    -out "$scratch/server.crt" -days 2 -nodes -subj "/CN=127.0.0.1" \
    -addext "subjectAltName=IP:127.0.0.1" >/dev/null 2>&1 \
    || { echo "SKIP run_ldap (openssl could not make a certificate)"; exit 0; }

python3 tests/ldap/mock_ldap.py "$PLAIN" plain 2>"$scratch/p.err" & P=$!
python3 tests/ldap/mock_ldap.py "$TLS" ldaps "$scratch" 2>"$scratch/s.err" & S=$!
for _ in $(seq 1 100); do
    grep -q ready "$scratch/p.err" 2>/dev/null && grep -q ready "$scratch/s.err" 2>/dev/null && break
    sleep 0.1
done
if ! grep -q ready "$scratch/p.err" 2>/dev/null; then
    fail "the mock directory started"; printf '\nrun_ldap: %d checks, %d failed\n' "$checks" "$failures"; exit 1
fi

export LDAP_PLAIN_PORT="$PLAIN" LDAP_TLS_PORT="$TLS" \
       LDAP_CA_FILE="$scratch/server.crt" LDAP_DEAD_PORT="$DEAD"

printf 'TIER semantics\n'
if ./gbasic tests/ldap_test.bas >"$scratch/out" 2>"$scratch/err"; then
    pass "ldap_test exits 0"
else
    fail "ldap_test exits 0 ($(head -1 "$scratch/err"))"
fi
if grep -q "^mismatches: 0$" "$scratch/out"; then
    pass "no mismatches"
else
    fail "no mismatches"
    grep "^MISMATCH" "$scratch/out" | head -10
fi
n=$(sed -n 's/^checks: //p' "$scratch/out")
if [ -n "$n" ] && [ "$n" -ge 32 ]; then
    pass "check count floor ($n checks)"
else
    fail "check count floor (got '${n:-none}', want >= 32)"
fi

printf 'TIER the load-bearing tiers ran\n'
for needle in \
    "so the two failures are distinguishable" \
    "so a certificate problem is distinguishable from a network one" \
    "an empty password is refused rather than sent" \
    "a single-valued attribute is still an array" \
    "naming the CA makes the same certificate acceptable" \
    "a connection with no declared security is refused"
do
    if grep -qF "ok   $needle" "$scratch/out"; then
        pass "ran: $needle"
    else
        fail "ran: $needle"
    fi
done

# THE PASSWORD MUST NOT APPEAR ANYWHERE. Not in a message, not in a
# diagnostic, not on stderr. Cheap to check and catastrophic to get wrong.
printf 'TIER the password never leaves\n'
if ! grep -q "correct horse" "$scratch/err" && \
   ! grep -q "correct horse" <(grep -v "^ok\|^MISMATCH" "$scratch/out"); then
    pass "no password text on stderr or in any reported message"
else
    fail "no password text on stderr or in any reported message"
fi

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/ldap_test.bas >/dev/null 2>"$scratch/vg"; then
        pass "no definite leak or invalid access"
    else
        fail "no definite leak or invalid access"
        grep -E "definitely lost|Invalid" "$scratch/vg" | head -5
    fi
else
    pass "valgrind (SKIP: not installed)"
fi

printf '\nrun_ldap: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
