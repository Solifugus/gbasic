#!/usr/bin/env bash
# PLAT-HTTP: `http` -- requests that do not block the program (six verbs over
# libcurl's multi interface) and handle readiness DELIVERED by the event loop.
#
# The suite is built around what a golden cannot see. Every defect this module
# can have produces an ordinary-looking result rather than an error -- a 500
# reported as a failure, a `start` that secretly blocked, a body that arrived
# in one piece instead of several -- so tests/http/http_test.bas states its own
# expected answers and the tiers below assert DIFFERENCES between two runs.
#
# Tiers:
#   SEMANTICS   the self-checking fixture (transport-vs-HTTP, parity with
#               webclient as the oracle, incremental reads, stop, handles)
#   CONCURRENCY four requests started together against the same four fetched
#               one at a time -- a RATIO, never an absolute time. Without it
#               every other tier passes on a `start` that performs the request.
#   DELIVERY    the waited-or-watched rule, asserted as a difference inside one
#               program, in a program with NO SERVER BOUND
#   NO_WATCH    its control: with the watcher removed the loop is not entered
#   IGNORED     a watcher that never reads the body still terminates (bounded,
#               because the defect this catches is a HANG, not a failure)
#   RAISE       a raise inside a watcher reports a LOCATED diagnostic and exits
#               nonzero -- for `http.events` AND `server.requests`, since the
#               silence was pre-existing in the webserver and the fix is shared
#   BUILD       without libcurl the module raises cleanly rather than failing
#               to build (asserted by the runner only when that build is made)
#   VALGRIND    a new refcounted value kind sharing one multi handle
set -euo pipefail

cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh

if ! command -v python3 >/dev/null 2>&1; then
    printf 'SKIP tests/run_http.sh (python3 is unavailable)\n'
    exit 0
fi

make >/dev/null

# Distinguish "no libcurl in this build" from a real failure, the way
# run_odbc_cookbook.sh separates an absent driver from a broken cookbook.
probe="$(mktemp)"; printf 'load http\n' > "$probe"
if ./gbasic "$probe" 2>&1 | grep -q 'not available in this build'; then
    rm -f "$probe"
    printf 'SKIP tests/run_http.sh (this build has no libcurl)\n'
    exit 0
fi
rm -f "$probe"

work="$(mktemp -d)"
server_pid=""
cleanup() {
    if [[ -n "$server_pid" ]]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    rm -rf "$work"
}
trap cleanup EXIT

python3 tests/http/fixture_server.py 0 >"$work/ready" 2>"$work/server_err" &
server_pid=$!
port=""
for _ in {1..60}; do
    if read -r word value < "$work/ready" 2>/dev/null && [[ "$word" == "READY" ]]; then
        port="$value"
        break
    fi
    if ! kill -0 "$server_pid" 2>/dev/null; then
        printf 'SKIP tests/run_http.sh (loopback networking unavailable)\n'
        exit 0
    fi
    sleep 0.05
done
if [[ -z "$port" ]]; then
    printf 'SKIP tests/run_http.sh (fixture server did not start)\n'
    exit 0
fi
export HTTP_FIXTURE_PORT="$port"

fail=0
note() { printf '%s\n' "$1"; }
bad() { printf 'FAIL %s\n' "$1"; fail=1; }

# --- SEMANTICS -------------------------------------------------------------
if ! timeout -k 5 120 ./gbasic tests/http/http_test.bas >"$work/sem.out" 2>"$work/sem.err"; then
    cat "$work/sem.err"
    bad "SEMANTICS: the fixture did not run to completion"
else
    mismatches="$(sed -n 's/^checks \([0-9]*\) mismatches \([0-9]*\)$/\2/p' "$work/sem.out")"
    checks="$(sed -n 's/^checks \([0-9]*\) mismatches \([0-9]*\)$/\1/p' "$work/sem.out")"
    if [[ -z "$checks" ]]; then
        bad "SEMANTICS: the fixture printed no tally"
    elif [[ "$mismatches" != "0" ]]; then
        grep '^MISMATCH' "$work/sem.out" || true
        bad "SEMANTICS: $mismatches of $checks checks disagreed"
    elif (( checks < 25 )); then
        # A coverage floor: a fixture that stopped running its checks also
        # reports zero mismatches.
        bad "SEMANTICS: only $checks checks ran; the file asserts more than that"
    else
        note "PASS SEMANTICS ($checks checks)"
    fi
    if [[ -s "$work/sem.err" ]]; then
        cat "$work/sem.err"
        bad "SEMANTICS: unexpected stderr"
    fi
fi

# --- CONCURRENCY -----------------------------------------------------------
# A ratio across two runs of the SAME four requests. Never an absolute time:
# what is being asserted is that the transfers overlap, which is a fact about
# this program, not about this machine.
elapsed_ms() {
    local start finish
    start=$(date +%s%N)
    timeout -k 5 120 ./gbasic "$1" >"$work/$(basename "$1").out" 2>"$work/$(basename "$1").err" || return 1
    finish=$(date +%s%N)
    echo $(( (finish - start) / 1000000 ))
}
if ! seq_ms="$(elapsed_ms tests/http/sequential.bas)"; then
    cat "$work/sequential.bas.err"; bad "CONCURRENCY: the sequential baseline failed"
elif ! con_ms="$(elapsed_ms tests/http/concurrent.bas)"; then
    cat "$work/concurrent.bas.err"; bad "CONCURRENCY: the concurrent run failed"
else
    if ! grep -q '^sequential ok 4$' "$work/sequential.bas.out" ||
       ! grep -q '^concurrent ok 4$' "$work/concurrent.bas.out"; then
        bad "CONCURRENCY: a run did not complete all four requests"
    elif (( con_ms <= 0 )); then
        bad "CONCURRENCY: the concurrent run took no measurable time"
    else
        ratio_x10=$(( seq_ms * 10 / con_ms ))
        # Four 400ms requests: ideal is 4x, the gate is 2x. A `start` that
        # performed the request would land at 1x.
        if (( ratio_x10 < 20 )); then
            bad "CONCURRENCY: sequential ${seq_ms}ms vs concurrent ${con_ms}ms is only ${ratio_x10}/10x; four overlapping requests must beat four sequential ones by at least 2x"
        else
            note "PASS CONCURRENCY (sequential ${seq_ms}ms, concurrent ${con_ms}ms, ${ratio_x10}/10x)"
        fi
    fi
fi

# --- DELIVERY: waited or watched, never both -------------------------------
if ! timeout -k 5 60 ./gbasic tests/http/watched.bas >"$work/watched.out" 2>"$work/watched.err"; then
    cat "$work/watched.err"; bad "DELIVERY: the watched program did not finish"
else
    waited_id="$(sed -n 's/^waited id \([0-9]*\) status .*/\1/p' "$work/watched.out")"
    watched_ids="$(sed -n 's/^watched ids //p' "$work/watched.out")"
    delivered="$(sed -n 's/^delivered //p' "$work/watched.out" | sort)"
    delivered_ids="$(echo "$delivered" | awk '{print $1}' | tr '\n' ' ' | sed 's/ $//')"
    expected_ids="$(echo "$watched_ids" | tr ' ' '\n' | sort | tr '\n' ' ' | sed 's/ $//')"
    statuses="$(echo "$delivered" | awk '{print $2}' | sort | tr '\n' ' ' | sed 's/ $//')"
    if [[ -z "$waited_id" ]]; then
        bad "DELIVERY: the waited request never completed"
    elif [[ "$delivered_ids" != "$expected_ids" ]]; then
        bad "DELIVERY: the loop delivered [$delivered_ids]; the two WATCHED handles are [$expected_ids]"
    elif echo "$delivered_ids" | tr ' ' '\n' | grep -qx "$waited_id"; then
        bad "DELIVERY: handle $waited_id was waited on and the loop reported it as well; a handle is waited OR watched"
    elif [[ "$statuses" != "200 500" ]]; then
        bad "DELIVERY: delivered statuses were [$statuses], want [200 500]"
    else
        note "PASS DELIVERY (waited $waited_id claimed; watched $expected_ids delivered)"
    fi
    if [[ -s "$work/watched.err" ]]; then
        cat "$work/watched.err"; bad "DELIVERY: unexpected stderr"
    fi
fi

# --- NO_WATCH: the control -------------------------------------------------
# Same shape, no watcher, and a request that would take three seconds. The
# program must return at once and deliver nothing, or "the loop runs after
# main" would be true of every program rather than of a watched one.
start=$(date +%s%N)
if ! timeout -k 5 30 ./gbasic tests/http/no_watch.bas >"$work/nowatch.out" 2>"$work/nowatch.err"; then
    cat "$work/nowatch.err"; bad "NO_WATCH: the program failed"
else
    ms=$(( ($(date +%s%N) - start) / 1000000 ))
    if (( ms > 1500 )); then
        bad "NO_WATCH: took ${ms}ms; with no watcher an outstanding request must not hold the program open"
    else
        note "PASS NO_WATCH (${ms}ms, the 3s request abandoned)"
    fi
fi

# --- IGNORED: a watcher that never reads the body still terminates ----------
if timeout -k 5 30 ./gbasic tests/http/ignored_body.bas >"$work/ignored.out" 2>"$work/ignored.err"; then
    note "PASS IGNORED (a body nobody reads does not keep the loop alive)"
else
    status=$?
    if (( status == 124 || status == 137 )); then
        bad "IGNORED: the program HUNG; undrained bytes are being re-reported every iteration"
    else
        cat "$work/ignored.err"; bad "IGNORED: exit $status"
    fi
fi

# --- RAISE: a watcher that raises must say so ------------------------------
set +e
timeout -k 5 30 ./gbasic tests/http/watcher_raise.bas >"$work/raise.out" 2>"$work/raise.err"
raise_status=$?
set -e
if (( raise_status == 0 )); then
    bad "RAISE: a raising http watcher exited 0"
elif ! grep -q 'runtime error at .*watcher_raise\.bas:[0-9]*:[0-9]*: the watcher raised' "$work/raise.err"; then
    cat "$work/raise.err"
    bad "RAISE: no LOCATED diagnostic; this is the silent case -- exit nonzero with nothing on stderr"
else
    note "PASS RAISE/http (exit $raise_status, located)"
fi

# The same claim for the queue the defect came from. python3 drives it rather
# than /dev/tcp: the readiness probe there opens and closes a connection
# without sending anything, which is itself a request the server sees.
server_port="$(python3 - <<'PORT'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PORT
)"
set +e
HTTP_SERVER_PORT="$server_port" timeout -k 5 30 ./gbasic tests/http/server_watcher_raise.bas \
    >"$work/sraise.out" 2>"$work/sraise.err" &
srv=$!
python3 - "$server_port" <<'DRIVE'
import socket, sys, time
port = int(sys.argv[1])
deadline = time.time() + 10
while time.time() < deadline:
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=2) as c:
            # A Host header, because the server answers 400 without one and
            # the request never reaches the queue this tier is about.
            c.sendall(b"GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
            c.recv(64)
        break
    except OSError:
        time.sleep(0.05)
DRIVE
wait $srv
sraise_status=$?
set -e
if (( sraise_status == 0 )); then
    bad "RAISE/server: a raising request watcher exited 0"
elif ! grep -q 'runtime error at .*server_watcher_raise\.bas:[0-9]*:[0-9]*: the request watcher raised' "$work/sraise.err"; then
    cat "$work/sraise.err"
    bad "RAISE/server: no LOCATED diagnostic -- the pre-existing silence is back"
else
    note "PASS RAISE/server (exit $sraise_status, located)"
fi

# --- DEFERRED: a handler that starts a call and does not answer ------------
#
# The shape everything downstream of this phase needs: a handler runs on the
# event loop's thread, so a request that needs a model call must start it and
# RETURN, with the answer appended later from a different watcher. Nothing in
# the tree had ever done that.
#
# THE ASSERTION IS AN ORDERING, NOT A CLOCK. Two requests are sent at once and
# each upstream call takes 800ms; both handlers must have STARTED before either
# ANSWER appears. A test that only checked both clients got their bodies would
# pass on a handler that blocked the loop and served them one at a time.
srv_port="$(python3 - <<'PORT'
import socket
s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()
PORT
)"
# --line-buffered because the tier reads the fixture's stdout after killing it:
# block-buffered, the lines that prove the ordering may never reach the file,
# and the tier would report "0 started" for a run that worked.
PORT="$srv_port" UPSTREAM="$port" timeout -k 5 60 ./gbasic --line-buffered tests/http/deferred_server.bas \
    >"$work/def.out" 2>"$work/def.err" &
defsrv=$!
# `|| true`: a lost transfer means a client that never gets an answer, and the
# driver failing must produce a REPORTED failure below rather than aborting the
# suite under `set -e` and taking the remaining tiers with it.
python3 - "$srv_port" "$work" <<'DRIVE' || true
import socket, sys, threading, time
port, work = int(sys.argv[1]), sys.argv[2]
deadline = time.time() + 10
while time.time() < deadline:
    try:
        socket.create_connection(("127.0.0.1", port), timeout=1).close()
        break
    except OSError:
        time.sleep(0.05)
bodies = {}
def ask(n):
    # Read exactly Content-Length rather than to EOF: the server may hold the
    # connection open, and a reader waiting for EOF then blocks until its own
    # timeout and looks like a server that never answered.
    with socket.create_connection(("127.0.0.1", port), timeout=8) as c:
        c.sendall(b"GET /ask HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")
        data = b""
        while b"\r\n\r\n" not in data:
            chunk = c.recv(4096)
            if not chunk:
                break
            data += chunk
        head, _, body = data.partition(b"\r\n\r\n")
        length = 0
        for line in head.split(b"\r\n"):
            if line.lower().startswith(b"content-length:"):
                length = int(line.split(b":", 1)[1].strip())
        while len(body) < length:
            chunk = c.recv(4096)
            if not chunk:
                break
            body += chunk
    bodies[n] = (head + b"\r\n\r\n" + body).decode("utf-8", "replace")
def guarded(n):
    # One client that never gets its answer must not stop the other from
    # recording what it DID get: the tier's message names how many of the two
    # arrived, and that count is the diagnosis.
    try:
        ask(n)
    except Exception as exc:
        bodies[n] = "CLIENT FAILED: %s" % exc
ts = [threading.Thread(target=guarded, args=(i,)) for i in (1, 2)]
for t in ts: t.start()
for t in ts: t.join()
with open(work + "/def.client", "w") as f:
    for n in sorted(bodies):
        f.write(bodies[n].replace("\r\n", "|") + "\n")
DRIVE
kill $defsrv 2>/dev/null || true
wait $defsrv 2>/dev/null || true
answered=$(grep -c '^answered ' "$work/def.out" || true)
started=$(grep -c '^started ' "$work/def.out" || true)
# `|| true` on the pipeline too: under `pipefail` a grep that matches nothing
# fails the assignment and `set -e` ends the suite silently -- which is how a
# RED tier reports nothing at all instead of reporting red.
order=$(grep -E '^(started|answered) ' "$work/def.out" | awk '{print $1}' | tr '\n' ' ' || true)
got=$(grep -c 'deferred:waited 800' "$work/def.client" 2>/dev/null || true)
if [[ "$started" != "2" || "$answered" != "2" ]]; then
    cat "$work/def.out" "$work/def.err"
    bad "DEFERRED: $started started, $answered answered; both requests must be handled"
elif [[ "$order" != "started started answered answered " ]]; then
    bad "DEFERRED: order was [$order]; both handlers must RETURN before either answer arrives, or the loop was blocked"
elif [[ "$got" != "2" ]]; then
    cat "$work/def.client"
    bad "DEFERRED: $got of 2 clients received the deferred body"
else
    note "PASS DEFERRED (two handlers returned unanswered; both answered later from the http watcher)"
fi

# --- WARN: a blocking wait inside the loop, and its control ----------------
if ! timeout -k 5 60 ./gbasic tests/http/wait_in_watcher.bas \
        >"$work/win.out" 2>"$work/win.err"; then
    cat "$work/win.err"; bad "WARN: the program failed"
elif ! grep -q 'warning: http.wait blocks the event loop' "$work/win.err"; then
    cat "$work/win.err"; bad "WARN: waiting inside a watcher did not warn"
elif ! grep -q 'wait_in_watcher\.bas:[0-9]*:[0-9]*' "$work/win.err"; then
    cat "$work/win.err"; bad "WARN: the warning carries no location"
elif ! grep -q '^waited inside the watcher: 200$' "$work/win.out"; then
    bad "WARN: it warned and CHANGED THE ANSWER; this is a warning, not a refusal"
elif ! timeout -k 5 60 ./gbasic tests/http/wait_outside.bas \
        >"$work/wout.out" 2>"$work/wout.err"; then
    cat "$work/wout.err"; bad "WARN control: a top-level wait failed"
elif [[ -s "$work/wout.err" ]]; then
    cat "$work/wout.err"
    bad "WARN control: an ordinary wait warned too; a warning that fires on every wait is noise"
else
    note "PASS WARN (inside the loop it warns and still answers; outside it is silent)"
fi

# --- VALGRIND --------------------------------------------------------------
# A new refcounted value kind, a shared multi handle torn down before
# curl_global_cleanup, and an event that holds a reference to a handle. None of
# that shows up as a wrong answer.
if vg_run ./gbasic tests/http/http_test.bas; then
    note "PASS VALGRIND (no definite leak or invalid access)"
else
    bad "VALGRIND"
fi

if (( fail )); then
    exit 1
fi
printf 'PASS tests/run_http.sh\n'
