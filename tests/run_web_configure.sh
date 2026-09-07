#!/usr/bin/env bash
# `web.configure(sv, settings)` -- deployment settings for a declared server.
#
# WHY IT EXISTS. A `server` block's head options are literals, so a deployment
# could not set the worker count from configuration at all. The gap was
# reported from a real application; checking it found something worse than the
# gap, which the MESSAGE tier below now guards: the refusal named
# `webserver.listen` as the remedy, which is true for port/address/timeout/
# cert/key and FALSE for `workers` -- webserver.listen binds one socket and has
# no worker count, so following the advice led nowhere.
#
# THE HEAD STAYS LITERAL, and for the real reason rather than the stated one.
# Measured: of the seven head options NONE has its value read by a parse-time
# check (only the site option `host` does, to refuse two sites claiming one
# name). The reason is that `server_register` is in the pre-registration set,
# so a block is materialised BEFORE ANYTHING RUNS -- `workers: n` would read an
# unassigned `n`. That is why the fix is a second door and not a looser head.
#
# Tiers:
#   SEMANTICS the self-checking fixture: the merge, that the result is still a
#             declaration, composition, all seven kinds, and the refusals each
#             beside a legal neighbour
#   TABLES    THE TRIPWIRE. The admitted options live in TWO places --
#             head_options[] in src/frontend.c (parse time, C) and
#             web._head_options() in stdlib/web.bas (serve time, gBASIC) -- and
#             two representations of one declaration drift. This reads both and
#             requires them identical, names and types.
#   LIVE      the same source served twice under different configuration, which
#             must take DIFFERENT PATHS. Everything else is a fact about a
#             record; this is the tier that says the configured value is acted
#             on rather than merged and ignored.
#   MESSAGE   the head refusal names a remedy that EXISTS, and the site refusal
#             gives its own reason instead -- asserted as a difference, because
#             one message serving both tables is how the old one came to be
#             wrong for exactly one option.
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

printf 'TIER semantics\n'
if timeout -k 5 60 ./gbasic tests/web_configure/configure_test.bas \
        >"$work/s.out" 2>"$work/s.err"; then
    checks="$(sed -n 's/^checks: //p' "$work/s.out")"
    if ! grep -q '^mismatches: 0$' "$work/s.out"; then
        grep '^MISMATCH' "$work/s.out" || true
        fail "the fixture disagreed with itself"
    elif [ -z "$checks" ] || [ "$checks" -lt 27 ]; then
        fail "only ${checks:-0} checks ran"
    else
        pass "$checks checks"
    fi
    [ -s "$work/s.err" ] && { cat "$work/s.err"; fail "unexpected stderr"; } || true
else
    cat "$work/s.err"; fail "the fixture did not run to completion"
fi

printf 'TIER the two option tables agree\n'
c_table="$(sed -n '/static const SrvOption head_options\[\] = {/,/};/p' src/frontend.c \
  | grep -o '{"[a-z]*", *AST_EXPR_[A-Z]*}' \
  | sed 's/{"\([a-z]*\)", *AST_EXPR_NUMBER}/\1:number/;
         s/{"\([a-z]*\)", *AST_EXPR_STRING}/\1:string/;
         s/{"\([a-z]*\)", *AST_EXPR_BOOL}/\1:boolean/' | sort)"
b_table="$(sed -n '/function _head_options()/,/end function/p' stdlib/web.bas \
  | grep -o 'name: *"[a-z]*", *type: *"[a-z]*"' \
  | sed 's/name: *"\([a-z]*\)", *type: *"\([a-z]*\)"/\1:\2/' | sort)"
if [ -z "$c_table" ]; then
    fail "could not read head_options[] out of src/frontend.c -- the table moved"
elif [ -z "$b_table" ]; then
    fail "could not read web._head_options() out of stdlib/web.bas -- the table moved"
elif [ "$c_table" != "$b_table" ]; then
    printf '    parse time (src/frontend.c):  %s\n' "$(echo "$c_table" | tr '\n' ' ')"
    printf '    serve time (stdlib/web.bas):  %s\n' "$(echo "$b_table" | tr '\n' ' ')"
    diff <(echo "$c_table") <(echo "$b_table") || true
    fail "the tables disagree: an option the head accepts that configure refuses (or the reverse) is a gap nobody will find by reading"
else
    pass "$(echo "$c_table" | wc -l) options, identical in both ($(echo "$c_table" | tr '\n' ' '))"
fi

printf 'TIER the configured value is acted on\n'
if ! command -v curl >/dev/null 2>&1 || ! command -v python3 >/dev/null 2>&1; then
    printf '  SKIP live tier (needs curl and python3)\n'
else
    free_port() { python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1]);s.close()'; }
    run_one() { # workers -> writes $work/w$1.out, echoes the body
        local n="$1" port
        port="$(free_port)"
        WEB_PORT="$port" WEB_WORKERS="$n" timeout -k 5 40 ./gbasic --line-buffered \
            tests/web_configure/pool_from_config.bas >"$work/w$n.out" 2>"$work/w$n.err" &
        local pid=$!
        local body=""
        for _ in $(seq 1 120); do
            body="$(curl -s -m 3 "http://127.0.0.1:$port/" 2>/dev/null || true)"
            [ -n "$body" ] && break
            sleep 0.1
        done
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
        printf '%s' "$body"
    }
    body1="$(run_one 1)"
    body2="$(run_one 2)"
    # Only _serve_pool announces the bound port, so a PORT line IS the
    # observable signal that the supervisor path ran.
    p1=$(grep -c '^PORT ' "$work/w1.out" || true)
    p2=$(grep -c '^PORT ' "$work/w2.out" || true)
    if [ "$body1" != "pooled answer" ] || [ "$body2" != "pooled answer" ]; then
        printf '    workers=1 body <%s>, workers=2 body <%s>\n' "$body1" "$body2"
        cat "$work/w1.err" "$work/w2.err" 2>/dev/null || true
        fail "both configurations must serve -- the block declares port 0, so answering at all proves the PORT override reached the runtime"
    elif [ "$p1" != "0" ]; then
        fail "workers=1 took the supervisor path ($p1 PORT lines); the configured count was not honoured"
    elif [ "$p2" != "1" ]; then
        cat "$work/w2.out"
        fail "workers=2 did NOT take the supervisor path; the configured count was merged and then ignored"
    else
        pass "same source: workers=1 serves single-process, workers=2 supervises -- and both answer on a port the source never names"
    fi
fi

printf 'TIER the refusal names a remedy that exists\n'
printf 'server app( port: 0, workers: n )\n  get "/"( req )\n    return { body: "x" }\n  end get\nend server\n' >"$work/head.bas"
printf 'server s( port: 1 )\n  web m( host: h )\n    get "/"( req )\n      return { body: "x" }\n    end get\n  end web\nend server\n' >"$work/site.bas"
./gbasic --ast "$work/head.bas" >/dev/null 2>"$work/head.err" || true
./gbasic --ast "$work/site.bas" >/dev/null 2>"$work/site.err" || true
if ! grep -q "web.configure" "$work/head.err"; then
    cat "$work/head.err"
    fail "a head option's refusal must name web.configure -- it used to name webserver.listen, which has no worker count at all"
elif grep -q "webserver.listen" "$work/head.err"; then
    cat "$work/head.err"
    fail "the head refusal still names webserver.listen"
elif grep -q "web.configure" "$work/site.err"; then
    cat "$work/site.err"
    fail "a SITE option's refusal must not name web.configure: host cannot be deferred, because its value is what the parse-time uniqueness check reads"
elif ! grep -q "two sites claiming one name" "$work/site.err"; then
    cat "$work/site.err"
    fail "the site refusal gives no reason of its own"
else
    pass "head names web.configure; site gives its own reason and does not"
fi

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/web_configure/configure_test.bas >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access"
    else
        cat "$work/vg.err"; fail "valgrind"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

[ "$status" -ne 0 ] && exit 1
printf 'PASS tests/run_web_configure.sh\n'
