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
. "$(dirname "$0")/build_has.sh"
make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }

scratch="$(mktemp -d)"
cleanup() { [ -n "${P:-}" ] && kill "$P" 2>/dev/null; [ -n "${S:-}" ] && kill "$S" 2>/dev/null; rm -rf "$scratch"; }
trap cleanup EXIT

checks=0; failures=0
pass() { checks=$((checks+1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks+1)); failures=$((failures+1)); printf '  FAIL %s\n' "$1"; }

# ASKED THROUGH tests/build_has.sh, WHICH IS A FIX RATHER THAN TIDYING: this
# suite carried its own probe, `./gbasic probe.bas 2>&1 | grep -q "not available
# in this build"`, and under the `pipefail` on line 2 THAT CONDITION COULD NEVER
# BE TRUE -- a build refusal makes gbasic exit 1, pipefail takes the pipeline's
# status from the last command to fail, so the `if` saw 1 EVEN WHEN grep MATCHED.
# Measured on a build made without libldap: the skip did not fire and the suite
# reported 10 of 11 checks FAILED, a red gate describing a correct build. It is
# the exact trap run_http.sh records from the other direction, and a skip probe
# is the worst place for it, being the one branch nobody watches work.
# build_has captures into a variable, so there is no pipeline to mislead it.
if ! build_has ldap; then
    echo "SKIP run_ldap (LDAP not in this build)"
    exit 0
fi
command -v python3 >/dev/null || { echo "SKIP run_ldap (no python3)"; exit 0; }
command -v openssl >/dev/null || { echo "SKIP run_ldap (no openssl)"; exit 0; }

# Ports are OS-assigned nowhere here, so pick high ones and fail loudly if busy.
# PORTS ARE OS-ASSIGNED, and each mock reports the one it got.
PLAIN=0; TLS=0

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
# BOTH MOCKS, AND NAMED SEPARATELY. The loop waited for both and the guard
# below checked only the PLAIN one, so an LDAPS mock that never came up let the
# suite run anyway -- and then the two checks that need a live TLS listener
# failed as `got unreachable, want tls_failed`, which reads as a defect in the
# reason MAPPING and is really "there was nothing to connect to". `unreachable`
# is the CORRECT answer to a refused connection; the fixture's premise is what
# was false.
#
# CAUGHT IN CI, on 2026-10-09, the first run in which this suite had ever
# executed there (its skip probe could not fire until the same day, and CI had
# never installed libldap). It passes on this machine and failed on the runner,
# which is the shape a fixed port and a slow start give you.
#
# The STDERR of whichever mock failed is printed, because the cause is in it and
# nowhere else -- a port already bound, or `load_cert_chain` refusing the
# certificate, both of which kill the mock before it writes `ready`. That is
# also where the comment above becomes TRUE: "fail loudly if busy" was a claim
# nothing implemented, and a busy 13912 was silent.
for pair in "plain:$scratch/p.err" "ldaps:$scratch/s.err"; do
    which="${pair%%:*}"; log="${pair##*:}"
    if ! grep -q '^ready ' "$log" 2>/dev/null; then
        fail "the $which mock directory started"
        printf '    its stderr:\n'; sed 's/^/      /' "$log" 2>/dev/null | head -20
        printf '\nrun_ldap: %d checks, %d failed\n' "$checks" "$failures"; exit 1
    fi
done

PLAIN="$(sed -n 's/^ready //p' "$scratch/p.err" | head -1)"
TLS="$(sed -n 's/^ready //p' "$scratch/s.err" | head -1)"

# `DEAD` MUST NOT BE LISTENING -- it is the whole `unreachable` case -- so it is
# chosen AFTER the mocks have bound, by binding a port and letting go. Chosen
# BEFORE them the kernel could hand that very port to one of the mocks, and the
# tier asserting a refused connection would quietly CONNECT: the one failure
# mode here that looks like a pass. Asserted rather than trusted, since the
# ordering makes it unlikely and not impossible.
DEAD="$(python3 -c 'import socket
s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')"
if [ "$DEAD" = "$PLAIN" ] || [ "$TLS" = "$DEAD" ] || [ -z "$DEAD" ]; then
    fail "the dead port ($DEAD) collides with a live mock (plain $PLAIN, ldaps $TLS)"
    printf '\nrun_ldap: %d checks, %d failed\n' "$checks" "$failures"; exit 1
fi
# And it really refuses, which is the premise the `unreachable` tier rests on.
if (exec 3<>/dev/tcp/127.0.0.1/"$DEAD") 2>/dev/null; then
    fail "the dead port $DEAD accepted a connection; `unreachable` cannot be tested against it"
    printf '\nrun_ldap: %d checks, %d failed\n' "$checks" "$failures"; exit 1
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
