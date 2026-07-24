#!/usr/bin/env bash
# Strict JSON serialization (json_encode / json_encodable).
#
# Two tiers:
#
#   * GOLDEN — examples/json_strict_test.bas against its .out (also run by
#     run_examples.sh). Pins the type mapping, the escaping, and the fact that the
#     native encode/decode dialect is UNCHANGED.
#   * STANDARDS PROOF — every JSON document json_encode emits is parsed by an
#     INDEPENDENT, standards-compliant parser (python3's json module). This is the
#     point of the suite: gBASIC's own `decode` deliberately accepts the historical
#     dialect (bare `nothing`/`unknown`), so round-tripping through it proves
#     nothing about standards compliance. The NAP-13 provider payloads are
#     re-validated the same way.
#
# Skips cleanly when python3 is unavailable (same policy as the webclient suite).
set -euo pipefail

cd "$(dirname "$0")/.."

make >/dev/null

stdout_file="$(mktemp)"
trap 'rm -f "$stdout_file"' EXIT

# --- Tier 1: golden ---------------------------------------------------------
if timeout 60 ./gbasic examples/json_strict_test.bas >"$stdout_file" 2>&1; then
    if diff -u examples/json_strict_test.out "$stdout_file"; then
        printf 'PASS examples/json_strict_test.bas\n'
    else
        printf 'FAIL examples/json_strict_test.bas (golden mismatch)\n'
        exit 1
    fi
else
    printf 'FAIL examples/json_strict_test.bas (exit %d)\n' "$?"
    cat "$stdout_file"
    exit 1
fi

# --- Tier 2: independent standards proof ------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
    printf 'SKIP standards proof (python3 not available)\n'
    exit 0
fi

# Only the emitted JSON documents are checked, not the human-readable report
# lines; the golden above pins those. A document is any line the strict encoder
# produced, which in this fixture is every line before the first "encodable ".
python3 - "$stdout_file" <<'PY'
import json, sys

path = sys.argv[1]
checked = 0
with open(path, encoding='utf-8') as fh:
    for lineno, line in enumerate(fh, 1):
        line = line.rstrip('\n')
        # The report tail is prose, not JSON documents.
        if line.startswith('encodable ') or line.startswith('encode ') \
           or line.startswith('decode ') or line.startswith('string '):
            continue
        if line == '':
            continue
        try:
            json.loads(line)
        except Exception as exc:
            print('FAIL line %d is not valid JSON: %s\n  %s' % (lineno, exc, line))
            sys.exit(1)
        checked += 1
print('PASS %d json_encode documents parsed by python3 json' % checked)
PY

# --- Tier 2b: NAP-13 provider payloads --------------------------------------
# Every request body the llm tool path puts on the wire must parse externally.
timeout 60 ./gbasic examples/llm_tools_test.bas 2>/dev/null | grep '^{' | python3 -c "
import json, sys
n = 0
for line in sys.stdin:
    line = line.strip()
    if not line.startswith('{'):
        continue
    n += 1
    try:
        json.loads(line)
    except Exception as exc:
        print('FAIL provider payload %d is not valid JSON: %s' % (n, exc))
        sys.exit(1)
if n == 0:
    print('FAIL no provider payloads were emitted')
    sys.exit(1)
print('PASS %d llm provider payloads parsed by python3 json' % n)
"
