#!/usr/bin/env bash
# Certificates for tests/run_net.sh's TLS tiers, generated per run (as
# run_web_tls.sh does) so nothing secret or expiring is committed.
#
#   make_certs.sh DIR
#
# A CA, and signed by it: server (localhost, 127.0.0.1), alt (alt.test),
# client (a client certificate), expired (when this openssl can backdate with
# -not_after). Self-signed, so of an unknown issuer: stranger (localhost,
# 127.0.0.1) and stranger_client. P-256 keys: quick to make, and quick under
# valgrind.
set -eu
d=$1
mkdir -p "$d"
key() { openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$d/$1.key" 2>/dev/null; }
key ca
openssl req -x509 -new -key "$d/ca.key" -out "$d/ca.pem" -days 2 -subj "/CN=gBASIC Test CA" \
    -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" 2>/dev/null
signed() {    # signed NAME CN SAN [x509 options...]
    local name=$1 cn=$2 san=$3; shift 3
    key "$name"
    openssl req -new -key "$d/$name.key" -out "$d/$name.csr" -subj "/CN=$cn" 2>/dev/null
    printf 'subjectAltName=%s\nbasicConstraints=CA:FALSE\n' "$san" >"$d/$name.ext"
    openssl x509 -req -in "$d/$name.csr" -CA "$d/ca.pem" -CAkey "$d/ca.key" -CAcreateserial \
        -out "$d/$name.pem" -extfile "$d/$name.ext" "$@" 2>/dev/null
}
selfsigned() {    # selfsigned NAME CN SAN
    key "$1"
    openssl req -x509 -new -key "$d/$1.key" -out "$d/$1.pem" -days 2 -subj "/CN=$2" \
        -addext "subjectAltName=$3" 2>/dev/null
}
signed server localhost "DNS:localhost,IP:127.0.0.1" -days 2
signed alt alt.test "DNS:alt.test" -days 2
signed client "gBASIC Client" "DNS:client.test" -days 2
signed expired localhost "DNS:localhost,IP:127.0.0.1" \
    -not_before 20200101000000Z -not_after 20200102000000Z || rm -f "$d/expired.pem" "$d/expired.key"
selfsigned stranger stranger "DNS:localhost,IP:127.0.0.1"
selfsigned stranger_client "Stranger Client" "DNS:stranger.test"
openssl x509 -in "$d/server.pem" -noout -fingerprint -sha256 | sed 's/.*=//; s/://g' | tr 'A-F' 'a-f' >"$d/server.sha256"
