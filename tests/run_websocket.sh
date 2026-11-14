#!/usr/bin/env bash
# The websocket library (stdlib/websocket.bas): RFC 6455 over `net`, client and
# server, and the webserver's upgrade hand-off (docs/networking_design.md §6, N4).
#
# Tiers:
#   1. BYTES -- valid_utf8 and bytes_xor against 3000-odd cases Python decided
#      (tests/websocket/bytes_cases.py): strict UTF-8 is Python's decoder's
#      answer, never this interpreter's.
#   2. BOTH ENDS IN ONE PROGRAM -- tests/websocket/ws_pair.bas: a watched
#      client and this program's own server block, text, binary, 200 000
#      bytes, a ping and a clean close, seen the same from both ends. Needs no
#      Python, so it runs everywhere the library loads.
#   3. SERVER, AGAINST PYTHON -- tests/websocket/ws_server.bas driven by
#      ws_peer.py: the `websockets` package for every length form, fragments,
#      ping, both closing handshakes, a subprotocol and 200 connections at
#      once; raw sockets for what a conforming client will not send: a frame a
#      byte at a time, a frame in the handshake's own write, 400/426 and the
#      RFC's accept value, and eleven protocol violations each closed with its
#      code. And an upgrade in a program not watching net.events is a 500.
#   4. CLIENT, AGAINST PYTHON -- tests/websocket/ws_client.bas against
#      ws_peer.py's servers, waited and watched: echoes, fragments from the
#      server, a server's ping answered, its close code, a bad accept and a
#      masked server frame refused, a server that just goes (1006), and wss
#      verified against a test CA.
#   5. wss SERVER -- tests/websocket/ws_server_tls.bas: the TLS session moves
#      with the socket from the webserver to net, and 10 MB queued at once
#      reaches a slow reader (the queue moves under a refused TLS write).
#      Skips without openssl or libssl.
#   6. VALGRIND -- the pair, and the server through an echo, fragments, the
#      violations and a close.
#
# The suite skips when the build has no libcrypto (the handshake's sha1);
# tiers 3-5 need python3 with the `websockets` package and skip, saying so.
. "$(dirname "$0")/portable.sh"
set -u

cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_websocket: build failed\n'
    exit 1
fi
export GBASIC_PATH=stdlib

work=$(mktemp -d)
has() { printf 'print has_builtin("%s")\n' "$1" >"$work/has.bas"; [ "$(./gbasic "$work/has.bas" 2>/dev/null)" = "true" ]; }
if ! has sha1; then
    rm -rf "$work"
    printf 'SKIP run_websocket: this build has no libcrypto, so the handshake (sha1) cannot be made; nothing was measured\n'
    exit 0
fi

pids=""
certs_here=tests/websocket/.certs
trap 'for p in $pids; do kill "$p" 2>/dev/null; done; rm -rf "$work" "$certs_here"' EXIT
status=0
fail() { printf 'FAIL %s\n' "$1"; status=1; }

fixture() {
    local label=$1 floor=$2; shift 2
    if timeout 120 "$@" >"$work/f.out" 2>"$work/f.err" </dev/null; then
        local n
        n=$(sed -n 's/^checks: //p' "$work/f.out")
        if grep -q '^mismatches: 0$' "$work/f.out" && [ "${n:-0}" -ge "$floor" ] && [ ! -s "$work/f.err" ]; then
            printf 'PASS %s (%s checks)\n' "$label" "$n"
        else
            fail "$label"
            grep -E '^MISMATCH|^checks|^mismatches' "$work/f.out" | head -10
            head -5 "$work/f.err"
        fi
    else
        fail "$label (exit)"
        tail -5 "$work/f.out"
        head -5 "$work/f.err"
    fi
}

# --- Tier 1 and 2 -------------------------------------------------------------
if command -v python3 >/dev/null 2>&1; then
    python3 -I tests/websocket/bytes_cases.py "$work/cases.txt"
    fixture "valid_utf8 and bytes_xor agree with Python on every case" 6000 \
        ./gbasic tests/websocket/bytes_test.bas "$work/cases.txt"
else
    printf 'SKIP bytes: no python3, so valid_utf8 was not checked against another decoder\n'
fi
fixture "both ends in one program: a conversation seen the same from each" 5 ./gbasic tests/websocket/ws_pair.bas

have_ws=0
if command -v python3 >/dev/null 2>&1 && python3 -I -c 'import websockets.asyncio.server' 2>/dev/null; then
    have_ws=1
else
    printf 'SKIP server, client and wss tiers: no python3 with the websockets package, so nothing was checked against another implementation\n'
fi

peer() {      # peer CHECK PORT [ARG]
    if out=$(timeout 90 python3 -I tests/websocket/ws_peer.py "$@" 2>&1); then
        printf '%s\n' "$out" | grep -v '^INFO'
    else
        printf '%s\n' "$out" | grep -E '^FAIL|Error' | head -3
        fail "ws_peer $1"
    fi
}

serve_ws() {  # serve_ws FIXTURE [PREFIX...] -> $wport, $wsp
    local file=$1; shift
    rm -f "$work/wready"
    timeout 300 "$@" ./gbasic --line-buffered "$file" "$work/wready" >"$work/ws.out" 2>&1 </dev/null &
    wsp=$!
    pids="$pids $wsp"
    for _ in $(seq 300); do [ -s "$work/wready" ] && break; sleep 0.1; done
    wport=$(cat "$work/wready" 2>/dev/null)
}

# --- Tier 3: the server --------------------------------------------------------
if [ "$have_ws" = 1 ]; then
    serve_ws tests/websocket/ws_server.bas
    for c in echo fragments ping close_client close_server plain refusals bytewise early violations; do
        peer "$c" "$wport"
    done
    peer many "$wport" 200
    peer stats "$wport" 1000,4000,1002,1007,1009,1006
    peer quit "$wport"
    if wait "$wsp" && [ ! -s "$work/ws.out" ]; then
        printf 'PASS   and the server ended when its listener and WebSockets had closed, with nothing unexpected\n'
    else
        fail "the WebSocket server did not end cleanly"
        head -5 "$work/ws.out"
    fi
    serve_ws tests/websocket/ws_unwatched.bas
    peer unwatched "$wport"
    peer quit "$wport"
    if wait "$wsp" && grep -q 'nothing is watching net.events' "$work/ws.out"; then
        printf 'PASS   and the refusal is named on stderr\n'
    else
        fail "the unwatched upgrade was not refused by name"
        head -5 "$work/ws.out"
    fi
fi

# --- Tier 4 and 5: the client, and wss ------------------------------------------
tls=0
if [ "$have_ws" = 1 ]; then
    if command -v openssl >/dev/null 2>&1 &&
       ./gbasic tests/net/tls_probe.bas 2>&1 | grep -q 'could not load certificate'; then
        tls=1
        rm -rf "$certs_here"
        bash tests/net/make_certs.sh "$certs_here" >/dev/null 2>&1 || tls=0
    fi
    rm -f "$work/ports.json"
    if [ "$tls" = 1 ]; then
        timeout 300 python3 -I tests/websocket/ws_peer.py serve "$work/ports.json" "$certs_here" >"$work/peer.out" 2>&1 &
    else
        timeout 300 python3 -I tests/websocket/ws_peer.py serve "$work/ports.json" >"$work/peer.out" 2>&1 &
    fi
    ppid=$!
    pids="$pids $ppid"
    for _ in $(seq 100); do [ -s "$work/ports.json" ] && break; sleep 0.05; done
    # tr: a native Windows python3 ends the line with CRLF, and read leaves
    # the CR on the last field -- the wss port reached ws_client.bas as
    # "NNNNN\r", which number() rightly refused
    read -r p_ws p_raw p_wss < <(python3 -I -c "import json,sys;p=json.load(open(sys.argv[1]));print(p['ws'],p['raw'],p.get('wss',''))" "$work/ports.json" | tr -d '\r')
    if [ "$tls" = 1 ]; then
        fixture "the client, waited and watched, against Python's servers, and wss" 35 \
            ./gbasic tests/websocket/ws_client.bas "$p_ws" "$p_raw" "$p_wss" "$certs_here/ca.pem"
    else
        printf 'SKIP wss client: no openssl or no libssl, so TLS was NOT measured\n'
        fixture "the client, waited and watched, against Python's servers" 31 \
            ./gbasic tests/websocket/ws_client.bas "$p_ws" "$p_raw"
    fi
    if [ "$tls" = 1 ]; then
        serve_ws tests/websocket/ws_server_tls.bas
        peer tls "$wport" "$certs_here"
        python3 -I -c "
import ssl, sys, urllib.request
ctx = ssl.create_default_context(cafile=sys.argv[2])
urllib.request.urlopen('https://localhost:%s/quit' % sys.argv[1], context=ctx, timeout=5).read()" "$wport" "$certs_here/ca.pem"
        if wait "$wsp" && [ ! -s "$work/ws.out" ]; then
            printf 'PASS   and the wss server ended cleanly\n'
        else
            fail "the wss server did not end cleanly"
            head -5 "$work/ws.out"
        fi
    fi
fi

# --- Tier 6: valgrind -------------------------------------------------------------
if vg_available; then
    if VG_EXTRA="--log-file=$work/vgp.log" timeout 300 bash tests/net/vg_wrap.sh ./gbasic tests/websocket/ws_pair.bas \
           >"$work/vgp.out" 2>&1 </dev/null && grep -q '^mismatches: 0$' "$work/vgp.out" &&
       ! grep -q 'Invalid \(read\|write\)\|definitely lost: [1-9]' "$work/vgp.log"; then
        printf 'PASS valgrind both ends in one program\n'
    else
        fail "valgrind ws_pair"
        grep -B2 -A12 'Invalid\|definitely' "$work/vgp.log" | head -30
    fi
    if [ "$have_ws" = 1 ]; then
        serve_ws tests/websocket/ws_server.bas env "VG_EXTRA=--log-file=$work/vgs.log" bash tests/net/vg_wrap.sh
        for c in echo fragments violations close_client; do
            peer "$c" "$wport" >/dev/null
        done
        peer quit "$wport"
        if wait "$wsp" && ! grep -q 'Invalid \(read\|write\)\|definitely lost: [1-9]' "$work/vgs.log"; then
            printf 'PASS valgrind the server through an echo, fragments, the violations and a close\n'
        else
            fail "valgrind ws_server"
            grep -B2 -A12 'Invalid\|definitely' "$work/vgs.log" | head -30
        fi
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit $status
