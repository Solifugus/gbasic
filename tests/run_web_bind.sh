#!/usr/bin/env bash
set -euo pipefail

# PLAT-WEB-1 Gap A -- `webserver.listen` binds where it is told, and by
# DEFAULT that is still loopback.
#
# The gap this closes (docs/plat-web-lowering-study.md §2): `listen` bound
# 127.0.0.1 unconditionally, so the design's `server myapp( port: 8080 )` could
# not produce a server another machine can reach at any port. The fix is one
# option; the risk it carries is the reason for this suite. Making a listener
# reachable is a SECURITY-RELEVANT act, so the property under test is not only
# "the option works" but "the default did not move" -- a change of default
# would expose every existing gBASIC server on the box and no existing golden
# would notice, because nothing prints a bind address.
#
# HOW THE TIERS AVOID PROVING NOTHING. Two ways an assertion here could be
# vacuous, both avoided deliberately:
#
#   1. `server.address` is read back from getsockname(), not remembered from
#      the argument. Asserting it therefore asks the KERNEL what happened
#      rather than asking us to repeat ourselves.
#   2. The reported string is checked against REACHABILITY, using 127.0.0.2 --
#      a second loopback address every Linux has (the whole 127/8 routes to
#      lo) and one that a 127.0.0.1-bound socket does NOT answer on. So each
#      tier probes two addresses and requires OPPOSITE answers. A build that
#      reported the right string while binding the wrong interface fails; so
#      does one that quietly binds the wildcard for everybody.
#
# Dependency-free and offline by construction: the client is bash's own
# /dev/tcp, there is no python3 in the path, and every address used is
# loopback -- nothing here leaves the machine. Never skips except the
# IPv6 tiers (which state why) and valgrind.

cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"

make

port_file="tests/tmp_web_bind.txt"
server_out="$(mktemp)"
server_err="$(mktemp)"
server_pid=""
failures=0
checks=0

cleanup() {
    if [[ -n "$server_pid" ]]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    rm -f "$port_file" "$server_out" "$server_err"
}
trap cleanup EXIT

pass() { checks=$((checks + 1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks + 1)); failures=$((failures + 1)); printf '  FAIL %s\n' "$1"; }

check_eq() { # check_eq <label> <expected> <actual>
    if [[ "$2" == "$3" ]]; then
        pass "$1"
    else
        fail "$1 (expected '$2', got '$3')"
    fi
}

# probe <addr> <port> [path] -- writes the response to stdout, exits nonzero
# when the connection is refused. bash's /dev/tcp keeps the suite free of
# curl, python3 and nc, none of which are guaranteed present.
probe() {
    local addr="$1" port="$2" path="${3:-/probe}"
    timeout 5 bash -c "
        exec 3<>/dev/tcp/$addr/$port || exit 1
        printf 'GET $path HTTP/1.0\r\nHost: probe\r\n\r\n' >&3
        cat <&3
    " 2>/dev/null
}

check_reachable() { # check_reachable <label> <addr> <port>
    local body
    if body="$(probe "$2" "$3")" && [[ "$body" == *"ok "* ]]; then
        pass "$1"
    else
        fail "$1 (no answer from $2:$3)"
    fi
}

check_refused() { # check_refused <label> <addr> <port>
    if probe "$2" "$3" >/dev/null 2>&1; then
        fail "$1 ($2:$3 answered, but nothing should be listening there)"
    else
        pass "$1"
    fi
}

# start_server [address] -- sets PORT and ADDR from what the fixture reports.
start_server() {
    rm -f "$port_file"
    : >"$server_err"
    if [[ $# -ge 1 ]]; then
        GBASIC_WEB_BIND="$1" ./gbasic tests/web_bind_fixture.bas \
            >"$server_out" 2>"$server_err" &
    else
        env -u GBASIC_WEB_BIND ./gbasic tests/web_bind_fixture.bas \
            >"$server_out" 2>"$server_err" &
    fi
    server_pid=$!
    local waited=0
    while [[ $waited -lt 100 ]]; do
        [[ -s "$port_file" ]] && break
        kill -0 "$server_pid" 2>/dev/null || break
        sleep 0.05
        waited=$((waited + 1))
    done
    if [[ ! -s "$port_file" ]]; then
        PORT=""
        ADDR=""
        return 1
    fi
    # `|| true` because the fixture writes no trailing newline, so `read`
    # assigns both fields and STILL reports EOF -- which under `set -e` ends
    # the run silently outside an `if` condition.
    read -r PORT ADDR <"$port_file" || true
    return 0
}

stop_server() {
    if [[ -n "$server_pid" ]]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
        server_pid=""
    fi
}

start_or_fail() { # start_or_fail <label> [address]
    local label="$1"
    shift
    if start_server "$@"; then
        return 0
    fi
    fail "$label (server never published a port)"
    cat "$server_err" >&2
    stop_server
    return 1
}

have_ipv6=0
if timeout 5 bash -c 'exec 3<>/dev/tcp/::1/1' 2>&1 | command grep -qi 'refused\|connect'; then
    have_ipv6=1
elif timeout 5 bash -c 'exec 3<>/dev/tcp/::1/1' 2>/dev/null; then
    have_ipv6=1
fi
# The probe above answers "is ::1 routable", which is what the tiers need; a
# refused connection proves the stack is there. Fall back to the interface
# list when bash reports nothing useful.
if [[ $have_ipv6 -eq 0 ]] && [[ -e /proc/net/if_inet6 ]]; then
    have_ipv6=1
fi

printf 'TIER default-loopback\n'
if start_or_fail 'default listener starts'; then
    check_eq 'default bind address is loopback' '127.0.0.1' "$ADDR"
    check_reachable 'loopback answers' 127.0.0.1 "$PORT"
    check_refused 'a second loopback address does NOT answer' 127.0.0.2 "$PORT"
    stop_server
fi

printf 'TIER explicit-wildcard\n'
if start_or_fail 'wildcard listener starts' '0.0.0.0'; then
    check_eq 'wildcard bind address is reported' '0.0.0.0' "$ADDR"
    check_reachable 'wildcard answers on 127.0.0.1' 127.0.0.1 "$PORT"
    check_reachable 'wildcard answers on 127.0.0.2' 127.0.0.2 "$PORT"
    stop_server
fi

printf 'TIER explicit-specific\n'
# The mirror image of the default tier: binding a NAMED address must exclude
# the one the default would have chosen, which no wildcard-vs-loopback test
# can distinguish on its own.
if start_or_fail 'specific listener starts' '127.0.0.2'; then
    check_eq 'specific bind address is reported' '127.0.0.2' "$ADDR"
    check_reachable 'the requested address answers' 127.0.0.2 "$PORT"
    check_refused 'the default address does NOT answer' 127.0.0.1 "$PORT"
    stop_server
fi

printf 'TIER ipv6\n'
if [[ $have_ipv6 -eq 0 ]]; then
    printf '  SKIP (no IPv6 stack on this host)\n'
else
    if start_or_fail 'ipv6 listener starts' '::1'; then
        check_eq 'ipv6 bind address is reported' '::1' "$ADDR"
        check_reachable 'ipv6 loopback answers' '::1' "$PORT"
        check_refused 'ipv4 loopback does NOT answer' 127.0.0.1 "$PORT"
        stop_server
    fi
fi

printf 'TIER v4-mapped-peer\n'
# A dual-stack listener (bound `::`) receives IPv4 peers as ::ffff:127.0.0.1.
# Reporting that spelling in req.remote_ip would silently break every literal
# comparison a program makes against it -- `trust_proxy "127.0.0.1"` in the
# design draft is exactly such a comparison -- so a v4-mapped peer is
# normalised back to its dotted quad.
bindv6only="$(cat /proc/sys/net/ipv6/bindv6only 2>/dev/null || echo 1)"
if [[ $have_ipv6 -eq 0 || "$bindv6only" != "0" ]]; then
    printf '  SKIP (no dual-stack sockets on this host)\n'
else
    if start_or_fail 'dual-stack listener starts' '::'; then
        check_eq 'dual-stack bind address is reported' '::' "$ADDR"
        body="$(probe 127.0.0.1 "$PORT" || true)"
        if [[ "$body" == *"ok 127.0.0.1"* ]]; then
            pass 'an ipv4 peer is reported as a dotted quad'
        elif [[ "$body" == *"::ffff:"* ]]; then
            fail 'an ipv4 peer is reported as a dotted quad (got a v4-mapped address)'
        else
            fail 'an ipv4 peer is reported as a dotted quad (no answer)'
        fi
        stop_server
    fi
fi

printf 'TIER graceful-shutdown\n'
# Unchanged behaviour, re-asserted here because the bind rewrite touches the
# same fd: the listener still closes on request and the process still exits.
if start_or_fail 'listener starts for shutdown'; then
    if probe 127.0.0.1 "$PORT" /quit | command grep -q bye; then
        pass 'the shutdown request is answered'
    else
        fail 'the shutdown request is answered'
    fi
    waited=0
    while [[ $waited -lt 100 ]] && kill -0 "$server_pid" 2>/dev/null; do
        sleep 0.05
        waited=$((waited + 1))
    done
    if kill -0 "$server_pid" 2>/dev/null; then
        fail 'the process exits after close'
        stop_server
    else
        wait "$server_pid" 2>/dev/null || true
        server_pid=""
        pass 'the process exits after close'
    fi
    if [[ -s "$server_err" ]]; then
        fail 'the server wrote nothing to stderr'
        cat "$server_err" >&2
    else
        pass 'the server wrote nothing to stderr'
    fi
fi

printf 'TIER refusal\n'
# Also wired into run_negative.sh. Kept here as well because these messages
# ARE the option's contract: an address gBASIC cannot honour must be refused
# by name, never quietly downgraded to the default -- a program that asked to
# be reachable and was silently left on loopback fails as a mystery later.
for name in \
    negative_webserver_listen_options_type \
    negative_webserver_listen_option_unknown \
    negative_webserver_listen_address_type \
    negative_webserver_listen_address_invalid \
    negative_webserver_listen_arity
do
    actual="$(./gbasic "tests/$name.bas" 2>&1 >/dev/null || true)"
    expected="$(cat "tests/$name.err")"
    check_eq "$name" "$expected" "$actual"
done

printf 'TIER valgrind\n'
if ! vg_available; then
    printf '  SKIP (valgrind is unavailable)\n'
else
    rm -f "$port_file"
    vg_run ./gbasic tests/web_bind_fixture.bas >"$server_out" 2>"$server_err" &
    server_pid=$!
    waited=0
    while [[ $waited -lt 300 ]]; do
        [[ -s "$port_file" ]] && break
        kill -0 "$server_pid" 2>/dev/null || break
        sleep 0.1
        waited=$((waited + 1))
    done
    if [[ ! -s "$port_file" ]]; then
        fail 'valgrind run published a port'
        cat "$server_err" >&2
        stop_server
    else
        read -r PORT ADDR <"$port_file" || true
        probe 127.0.0.1 "$PORT" >/dev/null || true
        probe 127.0.0.1 "$PORT" /quit >/dev/null || true
        if wait "$server_pid"; then
            pass 'valgrind reports no definite leak or error'
        else
            fail 'valgrind reports no definite leak or error'
            cat "$server_err" >&2
        fi
        server_pid=""
    fi
fi

# A tier that stops running its checks otherwise passes by asserting nothing.
if [[ $checks -lt 18 ]]; then
    printf 'FAIL coverage floor: only %d checks ran\n' "$checks"
    exit 1
fi

if [[ $failures -gt 0 ]]; then
    printf 'FAIL tests/run_web_bind.sh (%d of %d checks failed)\n' "$failures" "$checks"
    exit 1
fi

printf 'PASS tests/run_web_bind.sh (%d checks)\n' "$checks"
