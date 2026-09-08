#!/usr/bin/env bash
# `otp` -- one-time passwords as a second factor (stdlib/otp.bas,
# docs/otp_design.md). RFC 4226 (HOTP) and RFC 6238 (TOTP).
#
# WHY THIS IS SELF-CHECKING RATHER THAN GOLDEN, and here it is forced: every
# defect in this library produces a PLAUSIBLE SIX-DIGIT NUMBER. A golden would
# record a wrong code as the expected output and defend it forever, and nobody
# reviewing it could tell 081804 from 081805 by eye.
#
# THE ORACLE IS EXTERNAL AND PUBLISHED. RFC 4226 Appendix D and RFC 6238
# Appendix B give exact codes; they were computed independently (python3
# hmac/hashlib) BEFORE the library existed and agree with the printed tables,
# so the fixture asserts what the RFCs say rather than what we emitted. The
# vectors carry their own trap and it is exercised: the SHA256 and SHA512
# seeds are LONGER than SHA1's (32 and 64 bytes against 20), and reusing the
# SHA1 seed for all three yields wrong answers whose correction then gets
# applied to the code instead of to the seed.
#
# Tiers:
#   VECTORS   the RFC tables, all three algorithms, 6 and 8 digits, plus the
#             t=20000000000 row where the counter outgrows 32 bits.
#   REPLAY    THE LOAD-BEARING ONE. A code stays valid for its whole step, so
#             being arithmetically correct is not enough to be accepted twice.
#             Asserted as a DIFFERENCE with a control: "accepted" alone passes
#             on a library with no replay guard, and "refused" alone passes on
#             one that refuses everything.
#   SKEW      +/-1 accepted and +/-2 not, again as a difference.
#   REFUSALS  each beside its NEAREST LEGAL NEIGHBOUR, or a refusal suite is
#             satisfied by refusing everything.
#   VALGRIND  new C on the crypto path (base32 both ways, hmac_sha1).
set -euo pipefail

cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh
make >/dev/null
export GBASIC_PATH=stdlib

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

run_fixture() { # file label floor
    local file=$1 label=$2 floor=$3 out
    if ! timeout -k 5 120 ./gbasic "$file" >"$work/out" 2>"$work/err"; then
        cat "$work/err"; fail "$label did not run"
        return
    fi
    if grep -q MISMATCH "$work/out"; then
        grep MISMATCH "$work/out"; fail "$label disagreed with the RFC vectors"
        return
    fi
    if ! grep -qx 'mismatches: 0' "$work/out"; then
        fail "$label did not finish"
        return
    fi
    local n
    n=$(sed -n 's/^checks: //p' "$work/out")
    # A coverage floor, because a fixture that stops running its checks
    # otherwise passes by asserting nothing.
    if [ -z "$n" ] || [ "$n" -lt "$floor" ]; then
        fail "$label ran only ${n:-0} checks, wanted at least $floor"
        return
    fi
    if [ -s "$work/err" ]; then
        cat "$work/err"; fail "$label wrote to stderr"
        return
    fi
    pass "$label ($n checks)"
}

printf 'TIER vectors, replay and skew\n'
run_fixture tests/otp_test.bas "otp_test" 60

printf 'TIER refusals, each beside its nearest legal neighbour\n'
run_fixture tests/otp_refusals_test.bas "otp_refusals_test" 24

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/otp_test.bas >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access"
    else
        cat "$work/vg.err"; fail "valgrind"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

[ "$status" -ne 0 ] && exit 1
printf 'PASS tests/run_otp.sh\n'
