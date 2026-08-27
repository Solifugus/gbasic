#!/usr/bin/env bash
set -euo pipefail

# PLAT-WEB-1 step 2 -- the route table as data (`stdlib/web.bas`).
#
# This is the library the `server` block would eventually be sugar for
# (docs/plat-web-design-draft.md §2), built the way the lowering study wrote it
# out by hand: routes are ordinary values, so the whole routing scheme is
# testable with no socket. One tier at the end puts it behind a real one.
#
# WHY MORE THAN A GOLDEN. A router's failure mode is not a crash, it is a
# PLAUSIBLE WRONG ANSWER: "/products/new" resolving to the "/products/{id}"
# handler yields a perfectly ordinary page about a product whose id is the word
# "new". A golden records whatever the library does as the expected output, so
# it would enshrine that. The two fixtures therefore state their own expected
# answers and print `ok` or a MISMATCH naming both sides; the goldens pin that
# every line says ok, and a check count guards against a fixture that quietly
# stopped asserting.
#
# THE TWO INVARIANT TIERS ARE COMPLEMENTARY, WHICH WAS MEASURED, NOT ASSUMED.
# Both were proven red before shipping, on deliberately broken copies of the
# library, and each caught a defect the other missed:
#
#   * first-match-wins (specificity ignored): caught by ORDER-INDEPENDENCE,
#     which resolves every probe against the same routes declared four ways.
#     The ORACLE did NOT catch it -- with that route set, declaration order
#     happens to agree with the rule.
#   * inverted specificity (a capture beating a static segment): caught by the
#     ORACLE, which re-derives the winner from the patterns using a matcher
#     written separately in the fixture. ORDER-INDEPENDENCE did NOT catch it --
#     a consistently wrong router is still consistent across orders.
#
# Neither tier alone is enough, and that is the argument for both.
#
# REFUSALS ARE COMPARED BY MESSAGE, NOT BY POSITION. A library raise reports
# `stdlib/web.bas:LINE:COL`, so pinning whole stderr would make every edit to
# the library a rebaseline of thirteen files -- the standing cost run_chart.sh
# records for its .err goldens. The line number asserts nothing here; the text
# is the contract. Each case carries a hand-written `.msg` beside it.
#
# Needs GBASIC_PATH because web.bas is a stdlib library. No python3, no curl:
# the live tier's client is bash's own /dev/tcp, on loopback.

cd "$(dirname "$0")/.."

make

export GBASIC_PATH=stdlib

failures=0
checks=0
pass() { checks=$((checks + 1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks + 1)); failures=$((failures + 1)); printf '  FAIL %s\n' "$1"; }

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
port_file="tests/tmp_web_routes_port.txt"
server_pid=""
cleanup() {
    if [[ -n "$server_pid" ]]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    rm -f "$stdout_file" "$stderr_file" "$port_file"
}
trap cleanup EXIT

# ---------------------------------------------------------------- goldens

printf 'TIER semantics\n'
for name in web_routes_test web_routes_oracle_test; do
    ./gbasic "tests/$name.bas" >"$stdout_file" 2>"$stderr_file" || true
    if diff -u "tests/$name.out" "$stdout_file" >/dev/null; then
        pass "$name matches its golden"
    else
        fail "$name matches its golden"
        diff -u "tests/$name.out" "$stdout_file" | head -40 || true
    fi
    if command grep -q MISMATCH "$stdout_file"; then
        fail "$name reports no mismatch"
        command grep MISMATCH "$stdout_file" | head -10 || true
    else
        pass "$name reports no mismatch"
    fi
    # A fixture that stopped running its checks would otherwise pass by
    # asserting nothing, and its golden would move to match.
    reported="$(command grep '^checks: ' "$stdout_file" | sed 's/^checks: //')"
    case "$name" in
        web_routes_test) floor=25 ;;
        *) floor=6 ;;
    esac
    if [[ -n "$reported" ]] && [[ "$reported" -ge "$floor" ]]; then
        pass "$name ran at least $floor checks (ran $reported)"
    else
        fail "$name ran at least $floor checks (ran '${reported:-none}')"
    fi
done

# The 500 path must SAY which route misbehaved. A silent 500 is the failure
# this branch exists to avoid.
./gbasic tests/web_routes_test.bas >"$stdout_file" 2>"$stderr_file" || true
if command grep -q 'handler for GET /oops returned a string' "$stderr_file"; then
    pass 'a handler that returns the wrong thing names itself on stderr'
else
    fail 'a handler that returns the wrong thing names itself on stderr'
    cat "$stderr_file"
fi

# ---------------------------------------------------------------- refusals

printf 'TIER refusal\n'
found_cases=0
for source in tests/web_bad_*.bas; do
    name="$(basename "$source" .bas)"
    expected_file="tests/$name.msg"
    if [[ ! -f "$expected_file" ]]; then
        fail "$name has an expected message"
        continue
    fi
    found_cases=$((found_cases + 1))
    if ./gbasic "$source" >/dev/null 2>"$stderr_file"; then
        fail "$name is refused (it was accepted)"
        continue
    fi
    actual="$(sed 's/^runtime error at [^ ]*: //' "$stderr_file")"
    expected="$(cat "$expected_file")"
    if [[ "$actual" == "$expected" ]]; then
        pass "$name"
    else
        fail "$name"
        printf '    expected: %s\n    got:      %s\n' "$expected" "$actual"
    fi
done
if [[ $found_cases -ge 13 ]]; then
    pass "all $found_cases refusal cases ran"
else
    fail "at least 13 refusal cases ran (found $found_cases)"
fi

# ---------------------------------------------------------------- static

printf 'TIER static\n'
# The tree is built here rather than committed because it needs SYMLINKS --
# one pointing inside the root and one pointing out of it -- which are the
# whole reason canonicalize-then-check exists and which gBASIC cannot create.
static_root="$(mktemp -d)"
mkdir -p "$static_root/pub/sub" "$static_root/secret" "$static_root/pub-secret"
printf '<h1>ok</h1>' > "$static_root/pub/index.html"
printf 'deep' > "$static_root/pub/sub/deep.txt"
printf 'opaque!' > "$static_root/pub/data.bin"
printf '\211PNG\r\n\032\n' > "$static_root/pub/img.png"
printf 'SECRET' > "$static_root/secret/key.txt"
printf 'SIBLING' > "$static_root/pub-secret/x.txt"
ln -s sub/deep.txt "$static_root/pub/inside"
ln -s ../secret/key.txt "$static_root/pub/escape"

if [[ "$(stat -c %s "$static_root/pub/img.png")" != "8" ]]; then
    fail 'the png fixture is the 8 bytes the fixture asserts'
else
    pass 'the png fixture is the 8 bytes the fixture asserts'
fi

GBASIC_WEB_STATIC_ROOT="$static_root/pub" ./gbasic tests/web_static_test.bas \
    >"$stdout_file" 2>"$stderr_file" || true
if diff -u tests/web_static_test.out "$stdout_file" >/dev/null; then
    pass 'web_static_test matches its golden'
else
    fail 'web_static_test matches its golden'
    diff -u tests/web_static_test.out "$stdout_file" | head -40 || true
fi
if command grep -q MISMATCH "$stdout_file"; then
    fail 'web_static_test reports no mismatch'
    command grep MISMATCH "$stdout_file" | head -10 || true
else
    pass 'web_static_test reports no mismatch'
fi
reported="$(command grep '^checks: ' "$stdout_file" | sed 's/^checks: //')"
if [[ -n "$reported" ]] && [[ "$reported" -ge 21 ]]; then
    pass "web_static_test ran at least 21 checks (ran $reported)"
else
    fail "web_static_test ran at least 21 checks (ran '${reported:-none}')"
fi

# The 500 must say why on stderr; a misconfigured root that reported nothing
# would look exactly like a missing file to whoever is reading the logs.
if command grep -q "web.static: the root" "$stderr_file"; then
    pass 'a bad root names itself on stderr'
else
    fail 'a bad root names itself on stderr'
fi

# Not a duplicate of the fixture's own check: this one proves the SECRET was
# reachable all along, so the 403 came from the containment rule rather than
# from the file not being there. Without it, a web.static that always returned
# 403 would pass every refusal case above.
if [[ "$(cat "$static_root/secret/key.txt")" == "SECRET" ]] && \
   [[ -r "$static_root/secret/key.txt" ]]; then
    pass 'the refused file was readable all along'
else
    fail 'the refused file was readable all along'
fi

rm -rf "$static_root"

# ------------------------------------------------------------- live server

printf 'TIER live\n'
probe() { # probe <port> <method> <path>
    local port="$1" method="$2" path="$3"
    timeout 5 bash -c "
        exec 3<>/dev/tcp/127.0.0.1/$port || exit 1
        printf '$method $path HTTP/1.0\r\nHost: probe\r\n\r\n' >&3
        cat <&3
    " 2>/dev/null
}

check_contains() { # check_contains <label> <haystack> <needle>
    if [[ "$2" == *"$3"* ]]; then
        pass "$1"
    else
        fail "$1 (no '$3' in the response)"
    fi
}

rm -f "$port_file"
./gbasic tests/web_routes_server.bas >"$stdout_file" 2>"$stderr_file" &
server_pid=$!
waited=0
while [[ $waited -lt 100 ]]; do
    [[ -s "$port_file" ]] && break
    kill -0 "$server_pid" 2>/dev/null || break
    sleep 0.05
    waited=$((waited + 1))
done

if [[ ! -s "$port_file" ]]; then
    fail 'the routed server published a port'
    cat "$stderr_file" >&2
else
    read -r PORT <"$port_file" || true

    answer="$(probe "$PORT" GET /)"
    check_contains 'a routed GET / is answered' "$answer" 'home'
    check_contains 'and carries 200' "$answer" '200 OK'

    answer="$(probe "$PORT" GET /products/77)"
    check_contains 'a capture taken off the wire reaches the handler' "$answer" 'product 77'
    check_contains 'the handler status reaches the client' "$answer" '201'
    check_contains 'and so do its headers' "$answer" 'x-route: product'

    answer="$(probe "$PORT" GET /nothing)"
    check_contains 'an unrouted path is 404 over HTTP' "$answer" '404'

    answer="$(probe "$PORT" DELETE /products)"
    check_contains 'a wrong verb is 405 over HTTP' "$answer" '405'
    check_contains 'and the Allow header survives the trip' "$answer" 'allow: POST'

    answer="$(probe "$PORT" GET /quit)"
    check_contains 'the shutdown route is answered' "$answer" 'bye'

    waited=0
    while [[ $waited -lt 100 ]] && kill -0 "$server_pid" 2>/dev/null; do
        sleep 0.05
        waited=$((waited + 1))
    done
    if kill -0 "$server_pid" 2>/dev/null; then
        fail 'the routed server exits after close'
        kill "$server_pid" 2>/dev/null || true
    else
        wait "$server_pid" 2>/dev/null || true
        pass 'the routed server exits after close'
    fi
    server_pid=""
    if [[ -s "$stderr_file" ]]; then
        fail 'the routed server wrote nothing to stderr'
        cat "$stderr_file" >&2
    else
        pass 'the routed server wrote nothing to stderr'
    fi
fi

# ---------------------------------------------------------------- valgrind

printf 'TIER valgrind\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind is unavailable)\n'
else
    if valgrind --error-exitcode=9 --leak-check=full --errors-for-leak-kinds=definite \
        ./gbasic tests/web_routes_oracle_test.bas >/dev/null 2>"$stderr_file"; then
        pass 'valgrind reports no definite leak or error'
    else
        fail 'valgrind reports no definite leak or error'
        tail -30 "$stderr_file" >&2
    fi
fi

if [[ $checks -lt 38 ]]; then
    printf 'FAIL coverage floor: only %d checks ran\n' "$checks"
    exit 1
fi

if [[ $failures -gt 0 ]]; then
    printf 'FAIL tests/run_web_routes.sh (%d of %d checks failed)\n' "$failures" "$checks"
    exit 1
fi

printf 'PASS tests/run_web_routes.sh (%d checks)\n' "$checks"
