#!/usr/bin/env bash
set -uo pipefail

# `req.form` -- the request body decoded as application/x-www-form-urlencoded.
#
# Every web application needs it and, until now, every one wrote it: `req.body`
# was raw, so a login form was thirty lines of hand-rolled percent-decoding per
# application, independently wrong in a different place each time. Reported by
# the gdash session while surveying what auth would need (2026-08-30).
#
# IT SHARES THE QUERY PARSER, because a form body and a query string are the
# same grammar -- pairs joined by `&`, percent-decoded, `+` meaning space. A
# second decoder would be a second set of bugs.
#
# THE CONTENT-TYPE TIER IS THE ONE THAT MATTERS. A JSON body split on `&` and
# `=` yields a field named `{"a"` holding `1}` -- a perfectly plausible record,
# and the wrong one. So only a form content type is decoded and everything else
# gives an EMPTY record; the tier asserts both halves, since a decoder that
# returned empty for everything would pass the negative cases alone.
#
# Client is curl. Loopback only; nothing leaves the machine.

cd "$(dirname "$0")/.."
command -v curl >/dev/null 2>&1 || { echo "SKIP run_web_form (no curl)"; exit 0; }
make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }

scratch="$(mktemp -d)"
server_pid=""
cleanup() { [ -n "$server_pid" ] && kill "$server_pid" 2>/dev/null; rm -rf "$scratch"; }
trap cleanup EXIT

checks=0; failures=0
pass() { checks=$((checks+1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks+1)); failures=$((failures+1)); printf '  FAIL %s\n' "$1"; }

cat >"$scratch/srv.bas" <<'EOF'
load webserver
server = webserver.listen(0)
print "PORT " + string(server.port)
watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)
        out = "fields:"
        for each k in sort(keys(req.form))
            out = out + " " + k + "=[" + req.form[k] + "]"
        next
        append(server.responses, { id: req.id, status: 200, body: out })
    end while
end watch
EOF

GBASIC_PATH=stdlib ./gbasic --line-buffered "$scratch/srv.bas" >"$scratch/srv.log" 2>"$scratch/srv.err" &
server_pid=$!
port=""
for _ in $(seq 1 100); do
    port=$(sed -n 's/^PORT //p' "$scratch/srv.log" 2>/dev/null | head -1)
    [ -n "$port" ] && break
    sleep 0.05
done
[ -n "$port" ] || { fail "the server published a port"; printf '\nrun_web_form: %d checks, %d failed\n' "$checks" "$((failures+1))"; exit 1; }

probe() { # label content-type body expected
    local got
    got=$(curl -s -m 5 -X POST "http://127.0.0.1:$port/x" \
                -H "Content-Type: $2" --data "$3" 2>/dev/null)
    if [ "$got" = "$4" ]; then
        pass "$1"
    else
        fail "$1 (got '$got', want '$4')"
    fi
}

printf 'TIER decoding\n'
# `+` is a space, %XX decodes, and an encoded `&` does NOT split the pair --
# which is the case a naive split-then-decode gets wrong.
probe "plus, percent, and an encoded separator" \
      "application/x-www-form-urlencoded" \
      'user=ada+lovelace&pass=p%40ss%26word' \
      'fields: pass=[p@ss&word] user=[ada lovelace]'
probe "a present-but-empty field survives" \
      "application/x-www-form-urlencoded" 'a=&b=2' 'fields: a=[] b=[2]'
probe "a charset parameter is tolerated" \
      "application/x-www-form-urlencoded; charset=utf-8" 'a=1' 'fields: a=[1]'
probe "an empty body gives no fields" \
      "application/x-www-form-urlencoded" '' 'fields:'

printf 'TIER only a form body is decoded\n'
# Without these, a decoder that split everything on & would pass the tier above.
probe "JSON is NOT parsed as a form" "application/json" '{"a":1}' 'fields:'
probe "text/plain is not either" "text/plain" 'a=1&b=2' 'fields:'
probe "a longer type that merely starts the same way is not" \
      "application/x-www-form-urlencoded-ish" 'a=1' 'fields:'

printf 'TIER the server survived\n'
probe "and still serves after all of that" \
      "application/x-www-form-urlencoded" 'z=9' 'fields: z=[9]'
if [ -s "$scratch/srv.err" ]; then
    fail "the server wrote nothing to stderr ($(head -1 "$scratch/srv.err"))"
else
    pass "the server wrote nothing to stderr"
fi

printf '\nrun_web_form: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
