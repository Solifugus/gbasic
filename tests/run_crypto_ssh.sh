#!/bin/bash
# THE PRIMITIVES SSH NEEDS (docs/crypto_design.md §9): X25519, RSA over the six
# components SSH carries, and AES-CTR with the counter handed back. Asked for by
# Transward (an SSH/SFTP client and server written in gBASIC).
#
# EVERY OPERATION IS OpenSSL's, so the oracles are chosen so that agreeing with
# them is not agreeing with ourselves:
#   - published vectors (RFC 7748, NIST SP 800-38A) are in the fixture;
#   - Python's `cryptography` makes keys and PEM files gBASIC must read, and
#     signatures gBASIC must reproduce BYTE FOR BYTE (PKCS#1 v1.5 is
#     deterministic), then USES what gBASIC made: an exchange with its public
#     key, its signatures, its PEM, its chained CTR packets;
#   - ssh-keygen writes a key gBASIC must read, and the openssl command line
#     verifies a signature gBASIC made with it.
. "$(dirname "$0")/portable.sh"
set -uo pipefail
cd "$(dirname "$0")/.."
source tests/build_has.sh
source tests/valgrind_tier.sh
make >/dev/null

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

if ! build_has crypto; then
    printf 'TIER lean: without libcrypto every one refuses by name\n'
    printf 'r = x25519_keypair()\n' > "$work/lean.bas"
    if ./gbasic "$work/lean.bas" 2>&1 | grep -qF "x25519_keypair requires OpenSSL (libcrypto); rebuild with it installed"; then
        pass "x25519_keypair refuses, naming what to install"
    else
        fail "x25519_keypair refuses, naming what to install"
    fi
    printf 'SKIP run_crypto_ssh semantics (no libcrypto in this build -- the primitives were NOT measured)\n'
    [ "$status" = 0 ] && printf 'run_crypto_ssh: all checks passed\n' || printf 'run_crypto_ssh: FAILED\n'
    exit "$status"
fi
if ! python3 -c 'import cryptography' 2>/dev/null; then
    printf 'SKIP run_crypto_ssh (python3 cryptography unavailable -- the primitives were NOT measured)\n'
    printf 'run_crypto_ssh: all checks passed\n'
    exit 0
fi

printf 'TIER semantics: vectors, Python'"'"'s keys and signatures, refusals\n'
python3 -I tests/crypto_ssh_oracle.py make "$work" || fail "the oracle could not make its fixture"
DIR="$work" timeout -k 5 120 ./gbasic tests/crypto_ssh_test.bas >"$work/sem.out" 2>"$work/sem.err"
rc=$?
checks=$(sed -n 's/^checks: //p' "$work/sem.out")
bad=$(sed -n 's/^mismatches: //p' "$work/sem.out")
if [ "$rc" != 0 ]; then
    tail -3 "$work/sem.out"; head -2 "$work/sem.err"
    fail "the fixture exited $rc"
elif [ "${bad:-1}" != 0 ]; then
    grep '^MISMATCH' "$work/sem.out" | head -5
    fail "$bad mismatches"
elif [ "${checks:-0}" -lt 60 ]; then
    fail "only ${checks:-0} checks ran -- the fixture stopped early"
else
    pass "$checks checks, 0 mismatches"
fi
kdf=$(sed -n 's/^kdf vectors: //p' "$work/sem.out")
if python3 -c 'import bcrypt' 2>/dev/null; then
    if [ "${kdf:-0}" -ge 5 ]; then
        pass "bcrypt_pbkdf agreed with Python's bcrypt on $kdf vectors"
    else
        fail "python3 bcrypt is here but only ${kdf:-0} bcrypt_pbkdf vectors were compared"
    fi
else
    printf '  SKIP (python3 bcrypt unavailable -- bcrypt_pbkdf was NOT compared with it)\n'
fi

printf 'TIER peer: Python uses what gBASIC made\n'
if [ "$rc" = 0 ] && python3 -I tests/crypto_ssh_oracle.py check "$work" >"$work/peer.out" 2>&1; then
    pass "$(tail -1 "$work/peer.out")"
else
    grep -E '^FAIL|Error' "$work/peer.out" | head -5
    fail "Python refused something gBASIC made"
fi

printf 'TIER tools: ssh-keygen'"'"'s key, and openssl verifying gBASIC'"'"'s signature\n'
if ! command -v ssh-keygen >/dev/null 2>&1 || ! command -v openssl >/dev/null 2>&1; then
    printf '  SKIP (ssh-keygen or openssl unavailable -- the tools tier was NOT measured)\n'
else
    ssh-keygen -q -t rsa -b 2048 -m PEM -N '' -C '' -f "$work/sk" </dev/null
    ssh-keygen -e -m PKCS8 -f "$work/sk.pub" >"$work/sk_pub.pem"
    printf 'the exchange hash\n' >"$work/msg"
    cat >"$work/tools.bas" <<'EOF'
dir = env("DIR")
key = rsa_from_pem(read(join_path(dir, "sk")))
msg = read(join_path(dir, "msg"))
write(join_path(dir, "sig.hex"), hex_encode(rsa_sign(key, msg, "sha512")))
pub = rsa_from_pem(read(join_path(dir, "sk_pub.pem")))
print string(pub.n = key.n)
EOF
    out="$(DIR="$work" ./gbasic "$work/tools.bas" 2>&1)"
    if [ "$out" = true ]; then
        pass "ssh-keygen's private key and its exported public key are one key to gBASIC"
    else
        fail "ssh-keygen's key read by gBASIC: $out"
    fi
    xxd -r -p "$work/sig.hex" >"$work/sig.bin" 2>/dev/null || \
        python3 -c 'import sys; open(sys.argv[2],"wb").write(bytes.fromhex(open(sys.argv[1]).read()))' \
            "$work/sig.hex" "$work/sig.bin"
    if openssl dgst -sha512 -verify "$work/sk_pub.pem" -signature "$work/sig.bin" "$work/msg" >/dev/null 2>&1; then
        pass "openssl dgst verifies gBASIC's rsa-sha2-512 signature"
    else
        fail "openssl dgst verifies gBASIC's rsa-sha2-512 signature"
    fi
    printf 'the exchange hasH\n' >"$work/msg2"
    if openssl dgst -sha512 -verify "$work/sk_pub.pem" -signature "$work/sig.bin" "$work/msg2" >/dev/null 2>&1; then
        fail "the control: openssl accepted the signature over a different message"
    else
        pass "and refuses it over a message one letter different (the control)"
    fi
fi

# THE END-TO-END TRANSWARD ASKED FOR: keys ssh-keygen encrypted with a
# passphrase (`-a N`, bcrypt over aes256-ctr), opened by a gBASIC program
# with bcrypt_pbkdf and aes_ctr, and found to be the keys in the .pub files
# ssh-keygen wrote beside them.
printf 'TIER keyfile: openssh-key-v1 files ssh-keygen encrypted, opened in gBASIC\n'
if ! command -v ssh-keygen >/dev/null 2>&1; then
    printf '  SKIP (ssh-keygen unavailable -- encrypted key files were NOT opened)\n'
else
    mkdir -p "$work/kf"
    ssh-keygen -q -t ed25519 -a 16 -N 'correct horse' -C '' -f "$work/kf/ed" </dev/null
    ssh-keygen -q -t rsa -b 2048 -a 8 -N 'correct horse' -C '' -f "$work/kf/rsa" </dev/null
    DIR="$work/kf" timeout -k 5 120 ./gbasic tests/crypto_ssh_keyfile_test.bas >"$work/kf.out" 2>"$work/kf.err"
    kchecks=$(sed -n 's/^checks: //p' "$work/kf.out")
    if grep -q '^mismatches: 0$' "$work/kf.out" && [ "${kchecks:-0}" -ge 19 ]; then
        pass "$kchecks checks: both keys opened and are the .pub files' keys"
    else
        grep -E '^MISMATCH|error' "$work/kf.out" "$work/kf.err" | head -4
        fail "the encrypted key files did not open to ssh-keygen's keys"
    fi
    if command -v openssl >/dev/null 2>&1 && [ -s "$work/kf/rsa_sig.hex" ]; then
        ssh-keygen -e -m PKCS8 -f "$work/kf/rsa.pub" >"$work/kf/rsa_pub.pem"
        python3 -c 'import sys; open(sys.argv[2],"wb").write(bytes.fromhex(open(sys.argv[1]).read()))' \
            "$work/kf/rsa_sig.hex" "$work/kf/rsa_sig.bin"
        printf 'a message' >"$work/kf/m"
        if openssl dgst -sha512 -verify "$work/kf/rsa_pub.pem" -signature "$work/kf/rsa_sig.bin" "$work/kf/m" >/dev/null 2>&1; then
            pass "openssl verifies a signature made with the RSA key gBASIC recovered"
        else
            fail "openssl verifies a signature made with the RSA key gBASIC recovered"
        fi
    fi
fi

printf 'TIER valgrind: no invalid access or leak across the primitives\n'
if [ "$rc" != 0 ]; then
    printf '  SKIP (the fixture did not run)\n'
elif ! vg_available; then
    printf '  SKIP (valgrind unavailable -- memory was NOT measured)\n'
else
    mkdir -p "$work/vg"
    cp "$work/py.json" "$work/vg/"
    if DIR="$work/vg" timeout -k 5 600 valgrind $VG_FLAGS ${VG_EXTRA:-} ./gbasic tests/crypto_ssh_test.bas >"$work/vg.out" 2>"$work/vg.err"; then
        if grep -q '^mismatches: 0$' "$work/vg.out"; then
            pass "clean under valgrind, and the fixture still passes"
        else
            fail "under valgrind the fixture did not pass"
        fi
    else
        grep -E "Invalid|uninitialised|definitely lost" "$work/vg.err" | head -4
        fail "valgrind objected"
    fi
fi

if [ "$status" = 0 ]; then
    printf 'run_crypto_ssh: all checks passed\n'
else
    printf 'run_crypto_ssh: FAILED\n'
fi
exit "$status"
