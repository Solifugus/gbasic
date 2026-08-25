#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists libcrypto; then
    printf 'SKIP examples/crypto_test.bas (OpenSSL libcrypto not available)\n'
    exit 0
fi

make

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
trap 'rm -f "$stdout_file" "$stderr_file"' EXIT

positive_cases=(
    crypto_test
    crypto_cipher_test
    crypto_compose_test
    crypto_json_hostile_test
    crypto_kdf_test
)

for name in "${positive_cases[@]}"; do
    : >"$stdout_file"
    : >"$stderr_file"
    if ./gbasic "examples/$name.bas" >"$stdout_file" 2>"$stderr_file"; then
        if diff -u "examples/$name.out" "$stdout_file"; then
            printf 'PASS examples/%s.bas\n' "$name"
        else
            exit 1
        fi
    else
        status=$?
        cat "$stderr_file"
        exit "$status"
    fi
done

negative_cases=(
    negative_crypto_sha256_arity
    negative_kdf_empty_salt
    negative_kdf_zero_iterations
    negative_kdf_fractional_iterations
    negative_kdf_zero_length
    negative_kdf_huge_length
    negative_kdf_type
    negative_kdf_arity
    negative_scrypt_empty_salt
    negative_scrypt_not_power_of_two
    negative_scrypt_n_one
    negative_scrypt_over_memory
    negative_scrypt_arity
    negative_crypto_base64_type
    negative_crypto_random_bytes_range
    negative_crypto_bytes_equal_arity
    negative_crypto_aes_arity
)

for name in "${negative_cases[@]}"; do
    source="tests/$name.bas"
    expected="tests/$name.err"
    : >"$stdout_file"
    : >"$stderr_file"

    if ./gbasic "$source" >"$stdout_file" 2>"$stderr_file"; then
        printf 'FAIL %s\n' "$source"
        printf 'expected nonzero exit\n'
        exit 1
    fi

    if diff -u "$expected" "$stderr_file"; then
        printf 'PASS %s\n' "$source"
    else
        exit 1
    fi

    if [[ -s "$stdout_file" ]]; then
        printf 'FAIL %s\n' "$source"
        printf 'expected empty stdout\n'
        cat "$stdout_file"
        exit 1
    fi
done
