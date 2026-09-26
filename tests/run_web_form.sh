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

printf 'TIER %%00 is a byte, not a terminator\n'
# THE REALISTIC NUL DOOR, and the one the sweep reached last (PLAT-NUL,
# 2026-09-25). `%00` is the RFC-correct way a NUL travels in a form field or a
# query string, and the decoder -- which counted its own output internally and
# then handed back a bare pointer for the caller to re-measure with `strlen` --
# lost it: `a=x%00y` arrived as one byte, `x`.
#
# THAT IS THE WORST DIRECTION AVAILABLE FOR A WEB INPUT. A handler validating
# `req.form.name` was checking a string the client had not sent, which is the
# NUL-injection shape exactly, and it is the same argument the LDAP report that
# started PLAT-NUL made one layer up.
#
# REPORTED AS HEX, because a NUL cannot survive a shell command substitution --
# a tier comparing the raw bytes would silently compare the truncations.
cat >"$scratch/nul.bas" <<'EOF'
load webserver
server = webserver.listen(0)
print "PORT " + string(server.port)
watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)
        src = req.form
        if count(keys(req.form)) = 0 then
            src = req.query
        end if
        out = "fields=" + string(count(keys(src))) + ":"
        for each k in sort(keys(src))
            out = out + " " + hex_encode(k) + "=" + hex_encode(src[k])
        next
        append(server.responses, { id: req.id, status: 200, body: out })
    end while
end watch
EOF
GBASIC_PATH=stdlib ./gbasic --line-buffered "$scratch/nul.bas" >"$scratch/nul.log" 2>"$scratch/nul.err" &
nul_pid=$!
nul_port=""
for _ in $(seq 1 100); do
    nul_port=$(sed -n 's/^PORT //p' "$scratch/nul.log" 2>/dev/null | head -1)
    [ -n "$nul_port" ] && break
    sleep 0.05
done
if [ -z "$nul_port" ]; then
    fail "the hex server published a port"
else
    nul_probe() { # label method payload expected
        local got
        if [ "$2" = "POST" ]; then
            got=$(curl -s -m 5 -X POST "http://127.0.0.1:$nul_port/x" \
                        -H "Content-Type: application/x-www-form-urlencoded" \
                        --data-raw "$3" 2>/dev/null)
        else
            got=$(curl -s -m 5 "http://127.0.0.1:$nul_port/x?$3" 2>/dev/null)
        fi
        if [ "$got" = "$4" ]; then
            pass "$1"
        else
            fail "$1 (got '$got', want '$4')"
        fi
    }
    # 61 = a, 78 = x, 79 = y. A value of three bytes with the NUL in the middle.
    nul_probe "a form value keeps an encoded NUL" POST 'a=x%00y' \
              'fields=1: 61=780079'
    nul_probe "so does a query value" GET 'a=x%00y' 'fields=1: 61=780079'
    # THE CONTROL that the decoder did not merely start appending something:
    # the same field without the escape is two bytes.
    nul_probe "and a value without one is unchanged" POST 'a=xy' \
              'fields=1: 61=7879'
    # THE NAME HALF, which is the collapse rather than the truncation: two
    # fields differing only AFTER a NUL must stay two fields. This is the
    # record-field-name defect the record tier already fixed at the storage
    # level, and it would have been reintroduced here by fixing values alone.
    nul_probe "two names differing after a NUL stay two fields" POST \
              'x%00y=1&x%00z=2' 'fields=2: 780079=31 78007a=32'
    kill "$nul_pid" 2>/dev/null
fi

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
