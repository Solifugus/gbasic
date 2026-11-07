#!/usr/bin/env bash
# PLAT-WEB-3 Gap D — TLS termination in the webserver: a default certificate,
# SNI selection by hostname, refusal of a bad pair at LISTEN time (not at the
# first handshake, hours later), and the pooled shape where a plain TCP socket
# is inherited and TLS is terminated in the worker — which is what makes
# certificate rotation a rolling reload and nothing more. And client
# certificates (client_ca, require_client_cert), on a `server` block through
# web.configure and over the websocket the webserver hands to net.
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null
. tests/build_has.sh
if ! build_has listen; then
    printf 'SKIP tests/run_web_tls.sh (this platform cannot listen: webserver.listen is refused)\n'
    exit 0
fi

for tool in curl openssl; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'SKIP tests/web_tls (%s not installed)\n' "$tool"
        exit 0
    fi
done
# THE PROBE IS CAPTURED, NOT PIPED, AND THAT IS A FIX. This read
#
#     if ! ./gbasic negative_bad_cert.bas 2>&1 | grep -q "could not load..."
#
# and under the `pipefail` above NEITHER BRANCH COULD EVER DECIDE ANYTHING: the
# fixture is a NEGATIVE, so gbasic exits 1 by design, and pipefail takes the
# pipeline's status from the last command to fail -- so the condition was 1
# whether or not grep matched. MEASURED: grep matches (the text is right there)
# and the pipeline still reports 1, so `!` made the outer test always true and
# the inner test always false, and a build WITHOUT libssl FAILED this suite
# instead of skipping it. Same trap as run_ldap.sh's skip probe, found by
# sweeping for the shape after fixing that one -- which is this tree's standing
# lesson: a defect found in one probe is evidence about every probe.
probe="$(./gbasic tests/web_tls/negative_bad_cert.bas 2>&1 || true)"
case "$probe" in
    *"could not load certificate"*) ;;                 # libssl is in: carry on
    *"not available in this build"*)
        printf 'SKIP tests/web_tls (gbasic built without libssl)\n'
        exit 0 ;;
    *)
        # NEITHER answer is a third outcome worth naming rather than guessing
        # at: the fixture is meant to refuse for exactly one of two reasons.
        printf 'FAIL tests/web_tls (the libssl probe said neither; got: %s)\n' "$probe"
        exit 1 ;;
esac

export GBASIC_PATH=stdlib
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
fail() { printf 'FAIL %s\n' "$1"; exit 1; }

openssl req -x509 -newkey rsa:2048 -nodes -keyout "$scratch/main.key" \
    -out "$scratch/main.crt" -days 2 -subj "/CN=localhost" 2>/dev/null
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$scratch/alt.key" \
    -out "$scratch/alt.crt" -days 2 -subj "/CN=alt.example" 2>/dev/null

# --- a TLS server: https round trip, scheme, SNI, plain-HTTP refusal -------
log="$scratch/server.log"
timeout 60 ./gbasic --line-buffered tests/web_tls/tls_server.bas "$scratch" >"$log" 2>/dev/null &
srv=$!
port=""
for _ in $(seq 1 100); do
    port="$(sed -n 's/^PORT //p' "$log" | head -1)"; [ -n "$port" ] && break; sleep 0.05
done
[ -n "$port" ] || fail "tls_basic (no port announced)"

body="$(curl -sk -m 5 "https://127.0.0.1:$port/")"
[ "$body" = "secure hello via https" ] || fail "tls_basic (got '$body')"
printf 'PASS tls_basic (https round trip, req.scheme is https)\n'

subj_default="$(echo | timeout 5 openssl s_client -connect "127.0.0.1:$port" 2>/dev/null | grep '^subject=')"
case "$subj_default" in *CN*=*localhost*) ;; *) fail "tls_sni (default cert was '$subj_default')";; esac
subj_alt="$(echo | timeout 5 openssl s_client -connect "127.0.0.1:$port" -servername alt.example 2>/dev/null | grep '^subject=')"
case "$subj_alt" in *CN*=*alt.example*) ;; *) fail "tls_sni (SNI cert was '$subj_alt')";; esac
printf 'PASS tls_sni (default and per-host certificates selected by name)\n'

if curl -s -m 3 -o /dev/null "http://127.0.0.1:$port/"; then
    fail "tls_plain_refused (plain HTTP to a TLS port was answered)"
fi
printf 'PASS tls_plain_refused (plain HTTP to a TLS port fails fast, no hang)\n'
kill "$srv" 2>/dev/null || true
wait "$srv" 2>/dev/null || true

# --- whether TLS is on, on the record: web.serve's answer and listen's ------
# Transward asked for it after a stale web.bas served plain HTTP to a program
# that asked for TLS. The flag is the listener's own state, so the plain run is
# the control: a flag that echoed the request would be true there too only if
# asked, and the two runs differ in exactly that.
for mode in tls plain; do
    want=true; [ "$mode" = plain ] && want=false
    got="$(timeout 20 ./gbasic tests/web_tls/tls_flag.bas "$scratch" "$mode" 2>&1 | tr '\n' ' ')"
    [ "$got" = "serve $want listen $want " ] || fail "tls_flag $mode (got '$got')"
done
printf 'PASS tls_flag (serve and listen answer tls: true over TLS, false without)\n'

# --- the pooled shape ------------------------------------------------------
rm -f "$scratch/stop.flag"
plog="$scratch/pool.log"
timeout 90 ./gbasic --line-buffered tests/web_tls/tls_pool.bas "$scratch" >"$plog" 2>/dev/null &
pool=$!
port=""
for _ in $(seq 1 200); do
    port="$(sed -n 's/^PORT //p' "$plog" | head -1)"; [ -n "$port" ] && break; sleep 0.05
done
[ -n "$port" ] || fail "tls_pool (no port announced)"
grep -q "pool up: true" "$plog" || sleep 1
body="$(curl -sk -m 10 "https://127.0.0.1:$port/")"
[ "$body" = "pooled tls: https" ] || fail "tls_pool (got '$body')"
touch "$scratch/stop.flag"
wait "$pool" 2>/dev/null || true
grep -q "^stopped$" "$plog" || fail "tls_pool (pool did not stop cleanly)"
printf 'PASS tls_pool (workers terminate TLS on the supervisor-held socket)\n'

# --- negatives: refused at LISTEN time, naming the file --------------------
if ./gbasic tests/web_tls/negative_bad_cert.bas >/dev/null 2>"$scratch/err1"; then
    fail "negative_bad_cert (expected nonzero exit)"
fi
grep -q "could not load certificate '/no/such/cert.pem'" "$scratch/err1" \
    || fail "negative_bad_cert (stderr: $(cat "$scratch/err1"))"
printf 'PASS negative_bad_cert\n'

if ./gbasic tests/web_tls/negative_wrong_key.bas "$scratch" >/dev/null 2>"$scratch/err2"; then
    fail "negative_wrong_key (expected nonzero exit)"
fi
grep -q "could not use private key" "$scratch/err2" \
    || fail "negative_wrong_key (stderr: $(cat "$scratch/err2"))"
printf 'PASS negative_wrong_key (a mismatched pair is refused at listen, not at the first handshake)\n'

# --- client certificates (mutual TLS) -------------------------------------
# webserver.listen took client_ca and require_client_cert from the record
# net.listen takes and DROPPED THEM IN SILENCE until 2026-11-07 (DOGFOOD,
# Transward M26): a client with no certificate was served, and one with a
# certificate was reported as having none. So each case below asserts what a
# client WITHOUT the right certificate gets, beside the one with it.
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$scratch/ca.key" -out "$scratch/ca.crt" \
    -days 2 -subj "/CN=gBASIC Test CA" 2>/dev/null
cp "$scratch/main.crt" "$scratch/server.crt"
cp "$scratch/main.key" "$scratch/server.key"
openssl req -newkey rsa:2048 -nodes -keyout "$scratch/client.key" -out "$scratch/client.csr" \
    -subj "/CN=client-one" 2>/dev/null
openssl x509 -req -in "$scratch/client.csr" -CA "$scratch/ca.crt" -CAkey "$scratch/ca.key" \
    -CAcreateserial -out "$scratch/client.crt" -days 2 2>/dev/null
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$scratch/stranger.key" -out "$scratch/stranger.crt" \
    -days 2 -subj "/CN=stranger" 2>/dev/null

mtls_up() { # mode -> sets $mport, $msrv
    mlog="$scratch/mtls-$1.log"
    timeout 60 ./gbasic --line-buffered tests/web_tls/mtls_block.bas "$scratch" "$1" >"$mlog" 2>"$scratch/mtls-$1.err" &
    msrv=$!
    mport=""
    for _ in $(seq 1 100); do
        mport="$(sed -n 's/^PORT //p' "$mlog" | head -1)"; [ -n "$mport" ] && break; sleep 0.05
    done
    [ -n "$mport" ] || fail "mtls_$1 (no port announced: $(cat "$scratch/mtls-$1.err"))"
}
who() { # [cert key] -> the body, or "REFUSED"
    if [ $# -eq 2 ]; then
        curl -sk -m 5 --cert "$1" --key "$2" "https://127.0.0.1:$mport/who" 2>/dev/null || printf 'REFUSED'
    else
        curl -sk -m 5 "https://127.0.0.1:$mport/who" 2>/dev/null || printf 'REFUSED'
    fi
}

mtls_up require
got="$(who "$scratch/client.crt" "$scratch/client.key")"
[ "$got" = "CN=client-one" ] || fail "mtls_require (a client the CA signed: got '$got')"
got="$(who)"
[ "$got" = "REFUSED" ] || fail "mtls_require (A CLIENT WITH NO CERTIFICATE WAS SERVED: '$got')"
got="$(who "$scratch/stranger.crt" "$scratch/stranger.key")"
[ "$got" = "REFUSED" ] || fail "mtls_require (A CERTIFICATE NO TRUSTED CA SIGNED WAS SERVED: '$got')"
printf 'PASS mtls_require (the CA'"'"'s client served and named in req.tls; none, and a stranger, refused in the handshake)\n'

# a resumed session: a verifying context needs a session id context, or
# OpenSSL ends the resumption with "internal error" (measured: alert 80 with
# the line that sets it removed, every other case here still passing)
resumed="$(echo | timeout 10 openssl s_client -connect "127.0.0.1:$mport" -cert "$scratch/client.crt" \
    -key "$scratch/client.key" -tls1_2 -reconnect 2>&1 || true)"
case "$resumed" in
    *"alert internal error"*) fail "mtls_resume (a resumed session was refused: internal error)" ;;
    *Reused,*) ;;
    *) fail "mtls_resume (no session was reused: $(printf '%s' "$resumed" | grep -m2 -E 'New|alert|error'))" ;;
esac
printf 'PASS mtls_resume (a client-verified session resumes)\n'

# over the websocket the webserver hands to net: c.tls is the same session
if python3 -c 'import websockets.sync.client' 2>/dev/null; then
    ws_who() { # cert key -> what "who" answers, or REFUSED
        python3 -I - "$mport" "$@" <<'PY' 2>/dev/null || printf 'REFUSED'
import ssl, sys
from websockets.sync.client import connect
ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE
if len(sys.argv) > 3:
    ctx.load_cert_chain(sys.argv[2], sys.argv[3])
with connect("wss://127.0.0.1:%s/ws" % sys.argv[1], ssl=ctx, open_timeout=5) as ws:
    ws.send("who")
    sys.stdout.write(ws.recv(timeout=5))
PY
    }
    got="$(ws_who "$scratch/client.crt" "$scratch/client.key")"
    [ "$got" = "CN=client-one" ] || fail "mtls_wss (c.tls after the upgrade: got '$got')"
    got="$(ws_who)"
    [ "$got" = "REFUSED" ] || fail "mtls_wss (A WSS CLIENT WITH NO CERTIFICATE WAS UPGRADED: '$got')"
    printf 'PASS mtls_wss (the upgraded connection'"'"'s c.tls names the client; none is refused before the 101)\n'
else
    printf 'SKIP mtls_wss (python3 websockets not installed): the upgraded connection'"'"'s c.tls was NOT checked\n'
fi
kill "$msrv" 2>/dev/null || true
wait "$msrv" 2>/dev/null || true

mtls_up ask
got="$(who)"
[ "$got" = "none" ] || fail "mtls_ask (no certificate, asked not required: got '$got')"
got="$(who "$scratch/client.crt" "$scratch/client.key")"
[ "$got" = "CN=client-one" ] || fail "mtls_ask (a client the CA signed: got '$got')"
got="$(who "$scratch/stranger.crt" "$scratch/stranger.key")"
[ "$got" = "REFUSED" ] || fail "mtls_ask (a certificate offered must still verify: got '$got')"
printf 'PASS mtls_ask (client_ca alone: none is served as none, an offered certificate must verify)\n'
kill "$msrv" 2>/dev/null || true
wait "$msrv" 2>/dev/null || true

if ./gbasic tests/web_tls/negative_tls_option.bas "$scratch" >/dev/null 2>"$scratch/err3"; then
    fail "negative_tls_option (A MISSPELLED TLS OPTION WAS ACCEPTED)"
fi
grep -q "webserver tls: unknown option 'client_cert'" "$scratch/err3" \
    || fail "negative_tls_option (stderr: $(cat "$scratch/err3"))"
printf 'PASS negative_tls_option (a misspelled option is refused by name, not dropped)\n'
if ./gbasic tests/web_tls/negative_require_no_ca.bas "$scratch" >/dev/null 2>"$scratch/err4"; then
    fail "negative_require_no_ca (expected nonzero exit)"
fi
grep -q "require_client_cert needs client_ca" "$scratch/err4" \
    || fail "negative_require_no_ca (stderr: $(cat "$scratch/err4"))"
printf 'PASS negative_require_no_ca\n'

printf 'run_web_tls: all cases passed\n'
