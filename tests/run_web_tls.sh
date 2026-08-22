#!/usr/bin/env bash
# PLAT-WEB-3 Gap D — TLS termination in the webserver: a default certificate,
# SNI selection by hostname, refusal of a bad pair at LISTEN time (not at the
# first handshake, hours later), and the pooled shape where a plain TCP socket
# is inherited and TLS is terminated in the worker — which is what makes
# certificate rotation a rolling reload and nothing more.
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

for tool in curl openssl; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'SKIP tests/web_tls (%s not installed)\n' "$tool"
        exit 0
    fi
done
if ! ./gbasic tests/web_tls/negative_bad_cert.bas 2>&1 | grep -q "could not load certificate"; then
    # a build without libssl raises "not available in this build" instead
    if ./gbasic tests/web_tls/negative_bad_cert.bas 2>&1 | grep -q "not available in this build"; then
        printf 'SKIP tests/web_tls (gbasic built without libssl)\n'
        exit 0
    fi
fi

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

printf 'run_web_tls: all cases passed\n'
