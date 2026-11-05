#!/usr/bin/env bash
# `net`: TCP and Unix sockets, and pack/unpack (docs/networking_design.md, N1).
#
# Tiers:
#   1. PACK -- tests/net/pack_test.bas, self-checking; the expected bytes were
#      produced by Python's struct module, not by this interpreter.
#   2. WAITED -- tests/net/waited_test.bas: a listener and a connect in one
#      process, framing, the take family, timeouts against closes, Unix sockets
#      and peer credentials, every named failure.
#   3. AGAINST A PEER THIS MODULE DID NOT WRITE -- tests/net/peer.py drives a
#      watched gBASIC server:
#        split   a line protocol sent one byte per send reassembles exactly as
#                one sent whole (the first version stalled on this for good)
#        frames  length-prefixed frames cut at every 7th byte echo byte for byte
#        many    1000 connections at once, each answered its own line
#        flood   a 10 MiB write reaches a slow reader whole and in order
#        hold    a program that never takes pauses reading at its ceiling, and
#                its peak memory stays flat (Linux /proc only)
#   4. WATCHED CLIENT -- tests/net/watched_client.bas against a Python echo
#      server: a connect by NAME (the resolver thread), connected/data/closed in
#      order, a refused connect as error then closed, an idle timeout.
#   5. UNDER QT -- tests/net/qt_modal.bas: an echo answered while a modal dialog
#      raised from `main` is up (P1). Skips without Qt or a display.
#   6. VALGRIND -- the fixtures and a watched server under memcheck, and
#      --track-fds over the waited fixture: no socket left open at exit.
#
# Tiers 3-5 need python3 and skip, saying so, without it.
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -u

cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_net: build failed\n'
    exit 1
fi

work=$(mktemp -d)
pids=""
trap 'for p in $pids; do kill "$p" 2>/dev/null; done; rm -rf "$work"' EXIT
status=0
fail() { printf 'FAIL %s\n' "$1"; status=1; }

fixture() {
    local file=$1 floor=$2 label=$3; shift 3
    if timeout 120 "$@" ./gbasic "$file" $FIXTURE_ARGS >"$work/f.out" 2>"$work/f.err" </dev/null; then
        local n
        n=$(sed -n 's/^checks: //p' "$work/f.out")
        if grep -q '^mismatches: 0$' "$work/f.out" && [ "${n:-0}" -ge "$floor" ] && [ ! -s "$work/f.err" ]; then
            printf 'PASS %s (%s checks)\n' "$label" "$n"
        else
            fail "$label"
            grep -E '^MISMATCH|^checks|^mismatches' "$work/f.out"
            head -5 "$work/f.err"
        fi
    else
        fail "$label (exit)"
        tail -5 "$work/f.out"
        head -5 "$work/f.err"
    fi
}

# --- Tier 1 and 2 -----------------------------------------------------------
FIXTURE_ARGS="" fixture tests/net/pack_test.bas 29 "pack/unpack against Python's struct"
FIXTURE_ARGS="$work/w.sock $(id -u)" fixture tests/net/waited_test.bas 45 "waited connections, framing, Unix sockets, named failures"

# --- Tier 3: a peer this module did not write --------------------------------
if ! command -v python3 >/dev/null 2>&1; then
    printf 'SKIP peer, watched-client and Qt tiers: no python3, so nothing was checked against another implementation\n'
else
    serve() {     # serve MODE -> sets $port and $spid
        rm -f "$work/ready"
        timeout 120 ./gbasic --line-buffered tests/net/watched_server.bas "$1" "$work/ready" \
            >"$work/srv.out" 2>&1 </dev/null &
        spid=$!
        pids="$pids $spid"
        for _ in $(seq 100); do [ -s "$work/ready" ] && break; sleep 0.05; done
        port=$(cat "$work/ready" 2>/dev/null)
    }
    stop() { kill "$spid" 2>/dev/null; wait "$spid" 2>/dev/null; }
    peer() {
        if out=$(cd "$work" && timeout 90 python3 -I "$OLDPWD/tests/net/peer.py" "$@" 2>&1); then
            printf '%s\n' "$out" | sed 's/^INFO/     /'
        else
            printf '%s\n' "$out" | grep -E '^FAIL|Error' | head -3
            fail "peer $1"
        fi
    }
    serve line;  peer split "$port";  stop
    serve frame; peer frames "$port"; stop
    # A subshell for the raised descriptor limit; it exits with ITS verdict,
    # not with the status of the kill that stops the server.
    ( ulimit -n 4096 2>/dev/null
      serve line; peer many "$port" 1000; stop; exit "$status" ) || status=1
    serve flood; peer flood "$port"; stop
    if [ -r /proc/self/status ]; then
        serve hold; peer hold "$port" "$spid"; stop
        if grep -qx full "$work/srv.out"; then
            printf 'PASS   and the program was told: a full event\n'
        else
            fail "the hold server was never told its buffer was full"
            head -3 "$work/srv.out"
        fi
    else
        printf 'SKIP hold: no /proc, so the memory bound was not measured\n'
    fi

    # --- Tier 4: a watched client ---------------------------------------------
    python3 -I - >"$work/echo.txt" 2>&1 <<'PY' &
import socket, threading
srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(('127.0.0.1', 0)); srv.listen(16)
dead = socket.socket(); dead.bind(('127.0.0.1', 0)); dp = dead.getsockname()[1]; dead.close()
print(srv.getsockname()[1], dp, flush=True)
def serve(c):
    while True:
        d = c.recv(65536)
        if not d:
            break
        c.sendall(d)
    c.close()
srv.settimeout(30)
try:
    while True:
        c, _ = srv.accept()
        threading.Thread(target=serve, args=(c,), daemon=True).start()
except Exception:
    pass
PY
    epid=$!
    pids="$pids $epid"
    for _ in $(seq 100); do [ -s "$work/echo.txt" ] && break; sleep 0.05; done
    read -r eport dport <"$work/echo.txt"
    FIXTURE_ARGS="$eport $dport" fixture tests/net/watched_client.bas 8 "a watched client: by name, refused, idle"
    kill "$epid" 2>/dev/null

    # --- Tier 5: under Qt, with a modal dialog up -----------------------------
    rm -f "$work/ready" "$work/go"
    QT_QPA_PLATFORM=offscreen timeout 30 ./gbasic --line-buffered tests/net/qt_modal.bas \
        "$work/ready" "$work/go" >"$work/qm.out" 2>&1 </dev/null &
    qpid=$!
    pids="$pids $qpid"
    for _ in $(seq 100); do
        grep -qE 'DIALOG UP|NO DISPLAY|not available' "$work/qm.out" 2>/dev/null && break
        sleep 0.05
    done
    if grep -qE 'NO DISPLAY|not available' "$work/qm.out"; then
        printf 'SKIP under Qt: no Qt or no display, so service during a dialog was NOT measured\n'
        wait "$qpid" 2>/dev/null
    elif grep -q 'DIALOG UP' "$work/qm.out"; then
        qport=$(cat "$work/ready")
        if answer=$(timeout 5 python3 -I -c "
import socket
s = socket.create_connection(('127.0.0.1', $qport)); s.settimeout(3)
s.sendall(b'during\n'); print(s.recv(100))" 2>&1) && [ "$answer" = "b'echo:during\\n'" ] \
           && ! grep -q 'DIALOG DOWN' "$work/qm.out"; then
            printf 'PASS a socket is answered while a modal dialog is up\n'
        else
            fail "no echo while the dialog was up: $answer"
        fi
        touch "$work/go"
        if wait "$qpid" && grep -q 'DIALOG DOWN true' "$work/qm.out"; then
            printf 'PASS   and the dialog was still up when it was, answered only afterwards\n'
        else
            fail "the dialog fixture did not finish cleanly"
            grep -v propagate "$work/qm.out" | head -5
        fi
    else
        fail "the Qt fixture never raised its dialog"
        head -5 "$work/qm.out"
    fi
fi

# --- Tier 6: valgrind --------------------------------------------------------
if vg_available; then
    for f in tests/net/pack_test.bas tests/net/waited_test.bas; do
        rm -f "$work/w.sock"
        if vg_run ./gbasic "$f" "$work/w.sock" "$(id -u)" >"$work/vg.out" 2>"$work/vg.err" </dev/null \
           && grep -q '^mismatches: 0$' "$work/vg.out"; then
            printf 'PASS valgrind %s\n' "$f"
        else
            fail "valgrind $f"
            tail -20 "$work/vg.err"
        fi
    done
    rm -f "$work/w.sock"
    VG_EXTRA="--log-file=$work/fds.log" vg_run ./gbasic tests/net/waited_test.bas \
        "$work/w.sock" "$(id -u)" >/dev/null 2>&1 </dev/null
    open_sockets=$(grep -c 'Open AF_\(INET\|INET6\|UNIX\) socket' "$work/fds.log")
    if [ "$open_sockets" = 0 ] && grep -q 'FILE DESCRIPTORS' "$work/fds.log"; then
        printf 'PASS --track-fds: no socket left open at exit\n'
    else
        fail "--track-fds found $open_sockets socket(s) open at exit"
        grep -A3 'Open AF_' "$work/fds.log" | head -12
    fi
    if command -v python3 >/dev/null 2>&1; then
        rm -f "$work/ready"
        VG_EXTRA="--log-file=$work/vgs.log" timeout 120 bash -c '. tests/valgrind_tier.sh; vg_run ./gbasic tests/net/watched_server.bas line "$0"' "$work/ready" \
            >/dev/null 2>&1 </dev/null &
        vpid=$!
        pids="$pids $vpid"
        for _ in $(seq 300); do [ -s "$work/ready" ] && break; sleep 0.1; done
        vport=$(cat "$work/ready" 2>/dev/null)
        (cd "$work" && timeout 60 python3 -I "$OLDPWD/tests/net/peer.py" split "$vport" >/dev/null 2>&1)
        (cd "$work" && timeout 30 python3 -I "$OLDPWD/tests/net/peer.py" quit "$vport" >/dev/null 2>&1)
        wait "$vpid"
        vrc=$?
        if [ "$vrc" = 0 ] && ! grep -q 'Invalid \(read\|write\)\|definitely lost: [1-9]' "$work/vgs.log"; then
            printf 'PASS valgrind a watched server through accept, split frames and close\n'
        else
            fail "valgrind watched server (exit $vrc)"
            grep -B2 -A12 'Invalid\|definitely' "$work/vgs.log" | head -30
        fi
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit $status
