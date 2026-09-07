#!/usr/bin/env bash
# `llm`: the canonical part-based transcript and keyed replay.
# Step 4 of docs/gbasic_ai_reference_and_primitives.md (its Part 2, items 3-4).
#
# WHY BOTH HALVES. A message is not `{role, content:string}`: after a tool call
# the assistant turn carries text AND tool-call parts, and providers shape
# those differently, so a transcript re-sent in the wrong shape is rejected.
# And `llm.offline` returns ONE fixed file for EVERY request, so it can test a
# single turn and cannot replay a conversation at all -- which is what an agent
# loop needs before it can be tested.
#
# Tiers:
#   TRANSCRIPT one canonical transcript translated for BOTH providers, and the
#              two must DIFFER -- a translator emitting one shape for both
#              satisfies every check that names a single provider, and one of
#              the two providers would reject it. Plus `raw` fidelity, legacy
#              pass-through, the fingerprint's order-independence, and the
#              refusals.
#   REPLAY     the two failures the design names, each as a difference: a
#              volatile system prompt still matches (control: a change outside
#              it does NOT), and a retry replays the SECOND recording. Plus
#              THE CONTROL THAT MAKES THIS A DIFFERENT THING FROM `offline`:
#              the same two turns through `offline` get the same answer.
#   COLLISION  a 32-bit key can collide and a collision would silently serve
#              another request's answer, so the fixture records the canonical
#              text and replay refuses when it does not match.
#   VECTORS    the hash is checked against FNV-1a's OWN published vectors. It
#              lives in the library rather than in `crypto` so that
#              `llm.replay` -- the seam that makes an agent testable -- works
#              in every build, which was measured: with libcrypto compiled out
#              crypto.sha256_hex raises and replay still passes. That is only
#              defensible if the arithmetic is right, and a WRONG hash still
#              works, being deterministic, so nothing else here would notice.
#   EMBED      batch embeddings, with the shuffled-response tier: reading the
#              provider's rows positionally pairs every chunk with another
#              chunk's vector, and nothing raises.
#   PINNED     the canonical rendering is asserted by value, because the
#              committed fixtures are NAMED by it -- otherwise changing the
#              rendering fails every fixture with "no recorded response",
#              which is true and points at the wrong thing.
#   VALGRIND
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

run_fixture() { # file minimum-checks label
    local f="$1" min="$2" label="$3"
    if timeout -k 5 120 ./gbasic "$f" >"$work/out" 2>"$work/err"; then
        local checks
        checks="$(sed -n 's/^checks: //p' "$work/out")"
        if ! grep -q '^mismatches: 0$' "$work/out"; then
            grep '^MISMATCH' "$work/out" || true
            fail "$label: the fixture disagreed with itself"
        elif [ -z "$checks" ] || [ "$checks" -lt "$min" ]; then
            fail "$label: only ${checks:-0} checks ran, wanted at least $min"
        else
            pass "$label ($checks checks)"
        fi
        if [ -s "$work/err" ]; then
            cat "$work/err"; fail "$label: unexpected stderr"
        fi
    else
        cat "$work/err"; fail "$label: did not run to completion"
    fi
}

printf 'TIER the canonical transcript, both providers, and the fingerprint\n'
run_fixture tests/llm_transcript_test.bas 37 "transcript"

printf 'TIER keyed replay, its controls, and the collision guard\n'
run_fixture tests/llm_replay_test.bas 10 "replay"

printf 'TIER batch embeddings, placed by index\n'
# THE ORDER IS WHAT GOES SILENTLY WRONG. The API returns rows carrying an
# `index` and does not promise they arrive sorted; read positionally, every
# chunk is paired with another chunk's vector and NOTHING RAISES -- the store
# fills, retrieval answers, and the documents are wrong forever. The fixture
# feeds a deliberately shuffled response, which is the only way to tell a
# placement by index from a placement by arrival.
run_fixture tests/llm_embed_test.bas 15 "embed"

printf 'TIER every committed fixture is reachable\n'
# A fixture nobody replays is a fixture that rots. Each recorded file must be
# named by the key its own recorded request hashes to -- checked by the library
# refusing a mismatch, so what is asserted here is only that none is orphaned.
missing=0
for f in tests/llm/replay/*.json tests/llm/retry/*.json; do
    [ -e "$f" ] || continue
    if ! grep -q '"request"' "$f"; then
        printf '    %s has no recorded request; a collision could not be caught\n' "$f"
        missing=1
    fi
done
if [ "$missing" != "0" ]; then
    fail "a fixture carries no canonical request"
else
    pass "$(ls tests/llm/replay/*.json tests/llm/retry/*.json 2>/dev/null | wc -l) fixtures, each carrying the request it was recorded for"
fi

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/llm_transcript_test.bas >/dev/null 2>"$work/vg1.err" &&
       vg_run ./gbasic tests/llm_replay_test.bas >/dev/null 2>"$work/vg2.err"; then
        pass "no definite leak or invalid access"
    else
        cat "$work/vg1.err" "$work/vg2.err" 2>/dev/null || true
        fail "valgrind"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

[ "$status" -ne 0 ] && exit 1
printf 'PASS tests/run_llm_transcript.sh\n'
