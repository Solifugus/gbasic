/* SPDX-License-Identifier: Apache-2.0
 *
 * THE PRIMITIVES AN SSH IMPLEMENTATION NEEDS THAT gBASIC DID NOT HAVE
 * (docs/crypto_design.md §9): X25519 key exchange, RSA signatures over the
 * components SSH carries, and AES in counter mode with the counter handed back.
 * Asked for by Transward, which is writing its own SSH/SFTP client and server
 * in gBASIC on the net library.
 *
 * BINDINGS, NOT IMPLEMENTATIONS: every operation here is OpenSSL's, as the rest
 * of the crypto builtins are (§1), so this file decides only the SHAPE a gBASIC
 * program sees -- which is the part an SSH implementation would otherwise get
 * wrong in gBASIC, since its numbers are doubles and SSH's are 2048-bit.
 *
 * KEYS ARE RECORDS OF BIG-ENDIAN BYTE STRINGS: `{ n, e }` public and
 * `{ n, e, d, p, q, iqmp }` private, the six SSH and `openssh-key-v1` carry, in
 * the encoding SSH's `mpint` uses (without its sign byte). A record rather than
 * a PEM text or a handle because a protocol implementation reads and writes
 * exactly these fields, and a handle would have to be serialized to cross to an
 * actor. dmp1 and dmq1, which OpenSSL also wants, are computed here.
 *
 * #include'd into src/eval.c, like the other modules. */

/* ---- shared ---------------------------------------------------------------*/

#define CRYPTO_SSH_NO_LIBCRYPTO(name) \
    do { \
        char m_[160]; \
        snprintf(m_, sizeof(m_), "%s requires OpenSSL (libcrypto); rebuild with it installed", \
                 name); \
        runtime_error_raise(m_, 1003, "unsupported"); \
    } while (0)

#if HAVE_LIBCRYPTO
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/param_build.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/bio.h>
#include <openssl/decoder.h>
#include <openssl/err.h>
#include <openssl/rand.h>

static void crypto_ssh_raise(const char *name, const char *what) {
    char m[512];
    snprintf(m, sizeof(m), "%s: %s", name, what);
    runtime_error_raise(m, 1003, "crypto");
}

/* Evaluates every argument; refuses a wrong count by name. */
static int crypto_ssh_args(AstExpr *expr, const char *name, size_t lo, size_t hi,
                           Value *out) {
    size_t n = expr->as.call.args.count;
    if (n < lo || n > hi) {
        char m[200];
        if (lo == hi) {
            snprintf(m, sizeof(m), "%s expects %zu argument%s", name, lo, lo == 1 ? "" : "s");
        } else {
            snprintf(m, sizeof(m), "%s expects %zu to %zu arguments", name, lo, hi);
        }
        runtime_error_raise(m, 1003, "invalid function call");
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        out[i] = eval_expr(expr->as.call.args.items[i]);
        if (error_action_pending()) {
            for (size_t j = 0; j <= i; j++) value_free(out[j]);
            return 0;
        }
    }
    return 1;
}

static void crypto_ssh_free(Value *v, size_t n) {
    for (size_t i = 0; i < n; i++) value_free(v[i]);
}

static Value crypto_ssh_bytes(const unsigned char *p, size_t n) {
    return value_string_n((const char *)p, n);
}

/* ---- X25519 ---------------------------------------------------------------*/

static int crypto_x25519_public_of(const unsigned char *priv, unsigned char *pub32) {
    EVP_PKEY *k = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, priv, 32);
    if (!k) return 0;
    size_t len = 32;
    int ok = EVP_PKEY_get_raw_public_key(k, pub32, &len) == 1 && len == 32;
    EVP_PKEY_free(k);
    return ok;
}

/* 1 with the secret, 0 when the derivation is refused. Measured, OpenSSL 3.5
 * itself refuses an all-zero result (RFC 7748 §6.1), so the check below is a
 * backstop for an older libcrypto that answered the zeros: removing it leaves
 * the suite green on this machine, and that is why it is said here. */
static int crypto_x25519_shared(const unsigned char *priv, const unsigned char *peer,
                                unsigned char *out32) {
    EVP_PKEY *mine = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, priv, 32);
    EVP_PKEY *theirs = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, NULL, peer, 32);
    EVP_PKEY_CTX *ctx = mine ? EVP_PKEY_CTX_new(mine, NULL) : NULL;
    int ok = mine && theirs && ctx;
    size_t len = 32;
    if (ok && EVP_PKEY_derive_init(ctx) != 1) ok = 0;
    if (ok && EVP_PKEY_derive_set_peer(ctx, theirs) != 1) ok = 0;
    if (ok && (EVP_PKEY_derive(ctx, out32, &len) != 1 || len != 32)) ok = 0;
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(mine);
    EVP_PKEY_free(theirs);
    if (ok) {
        /* RFC 7748 §6.1, asserted here rather than trusted to the library's
         * version: OpenSSL 3 refuses it, older ones answered the zeros. */
        unsigned char acc = 0;
        for (size_t i = 0; i < 32; i++) acc |= out32[i];
        if (acc == 0) ok = 0;
    }
    return ok;
}

/* ---- RSA ------------------------------------------------------------------*/

static const char *const rsa_private_fields[] = { "n", "e", "d", "p", "q", "iqmp", NULL };

/* A field of a key record as a BIGNUM, or NULL having set `why`. */
static BIGNUM *crypto_rsa_field(Value *key, const char *field, char *why, size_t why_size) {
    RecordField *f = record_find(key, field);
    Value *v = f ? f->value : NULL;
    if (!v) {
        snprintf(why, why_size, "the key has no `%s`", field);
        return NULL;
    }
    if (v->kind != VALUE_STRING || string_length(v->as.string) == 0) {
        snprintf(why, why_size, "the key's `%s` must be big-endian bytes", field);
        return NULL;
    }
    BIGNUM *b = BN_bin2bn((const unsigned char *)v->as.string,
                          (int)string_length(v->as.string), NULL);
    if (!b) abort();
    return b;
}

static int crypto_rsa_is_private(Value *key) {
    return record_find(key, "d") != NULL;
}

/* An EVP_PKEY from a key record: private when it carries `d`, public
 * otherwise. NULL with `why` set. */
static EVP_PKEY *crypto_rsa_from_record(Value *key, int want_private, char *why,
                                        size_t why_size) {
    if (key->kind != VALUE_RECORD) {
        snprintf(why, why_size, "the key must be a record { n, e%s }",
                 want_private ? ", d, p, q, iqmp" : "");
        return NULL;
    }
    int priv = crypto_rsa_is_private(key);
    if (want_private && !priv) {
        snprintf(why, why_size, "this is a public key (it has no `d`); signing needs the private key");
        return NULL;
    }
    BIGNUM *b[6] = { NULL };
    size_t count = priv ? 6 : 2;
    for (size_t i = 0; i < count; i++) {
        b[i] = crypto_rsa_field(key, rsa_private_fields[i], why, why_size);
        if (!b[i]) {
            for (size_t j = 0; j < i; j++) BN_clear_free(b[j]);
            return NULL;
        }
    }
    BIGNUM *dmp1 = NULL, *dmq1 = NULL;
    BN_CTX *bctx = NULL;
    OSSL_PARAM_BLD *bld = OSSL_PARAM_BLD_new();
    OSSL_PARAM *params = NULL;
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *ctx = NULL;
    int ok = bld != NULL;
    if (ok && priv) {
        /* dmp1 = d mod (p-1), dmq1 = d mod (q-1): OpenSSL wants all eight, SSH
         * carries six. */
        bctx = BN_CTX_new();
        dmp1 = BN_new();
        dmq1 = BN_new();
        BIGNUM *t = BN_new();
        ok = bctx && dmp1 && dmq1 && t &&
             BN_sub(t, b[3], BN_value_one()) && BN_mod(dmp1, b[2], t, bctx) &&
             BN_sub(t, b[4], BN_value_one()) && BN_mod(dmq1, b[2], t, bctx);
        BN_free(t);
    }
    if (ok) {
        ok = OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_N, b[0]) &&
             OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_E, b[1]);
    }
    if (ok && priv) {
        ok = OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_D, b[2]) &&
             OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_FACTOR1, b[3]) &&
             OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_FACTOR2, b[4]) &&
             OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_EXPONENT1, dmp1) &&
             OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_EXPONENT2, dmq1) &&
             OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_COEFFICIENT1, b[5]);
    }
    if (ok) {
        params = OSSL_PARAM_BLD_to_param(bld);
        ctx = EVP_PKEY_CTX_new_from_name(NULL, "RSA", NULL);
        ok = params && ctx && EVP_PKEY_fromdata_init(ctx) == 1 &&
             EVP_PKEY_fromdata(ctx, &pkey, priv ? EVP_PKEY_KEYPAIR : EVP_PKEY_PUBLIC_KEY,
                               params) == 1;
    }
    if (ok && priv) {
        /* A KEY WHOSE PARTS DISAGREE SIGNS GARBAGE WITHOUT COMPLAINT -- the
         * CRT path uses p, q and iqmp, so a wrong `d` or a swapped p and q
         * gives a signature no verifier accepts and nothing here says why.
         * Checked once, where the record becomes a key. */
        EVP_PKEY_CTX *check = EVP_PKEY_CTX_new(pkey, NULL);
        ok = check && EVP_PKEY_private_check(check) == 1 && EVP_PKEY_pairwise_check(check) == 1;
        EVP_PKEY_CTX_free(check);
        if (!ok) {
            snprintf(why, why_size, "the key's parts do not belong together (n, e, d, p, q and iqmp must be one RSA key)");
        }
    } else if (!ok) {
        snprintf(why, why_size, "OpenSSL would not build a key from those parts");
    }
    if (!ok) {
        EVP_PKEY_free(pkey);
        pkey = NULL;
    }
    EVP_PKEY_CTX_free(ctx);
    OSSL_PARAM_free(params);
    OSSL_PARAM_BLD_free(bld);
    BN_CTX_free(bctx);
    BN_clear_free(dmp1);
    BN_clear_free(dmq1);
    for (size_t i = 0; i < count; i++) BN_clear_free(b[i]);
    return pkey;
}

static Value crypto_bn_value(const BIGNUM *b) {
    int n = BN_num_bytes(b);
    unsigned char *buf = malloc(n > 0 ? (size_t)n : 1);
    if (!buf) abort();
    BN_bn2bin(b, buf);
    Value v = crypto_ssh_bytes(buf, (size_t)n);
    OPENSSL_cleanse(buf, (size_t)n);
    free(buf);
    return v;
}

/* The record form of a key: all six fields when it holds a private key. */
static Value crypto_rsa_to_record(EVP_PKEY *pkey) {
    static const char *const params[] = {
        OSSL_PKEY_PARAM_RSA_N, OSSL_PKEY_PARAM_RSA_E, OSSL_PKEY_PARAM_RSA_D,
        OSSL_PKEY_PARAM_RSA_FACTOR1, OSSL_PKEY_PARAM_RSA_FACTOR2,
        OSSL_PKEY_PARAM_RSA_COEFFICIENT1
    };
    Value rec = value_record(NULL, 0);
    BIGNUM *d = NULL;
    int priv = EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_RSA_D, &d) == 1;
    BN_clear_free(d);
    for (size_t i = 0; i < (priv ? 6u : 2u); i++) {
        BIGNUM *b = NULL;
        if (EVP_PKEY_get_bn_param(pkey, params[i], &b) != 1) {
            value_free(rec);
            return value_null();
        }
        record_set(&rec, rsa_private_fields[i], crypto_bn_value(b));
        BN_clear_free(b);
    }
    return rec;
}

static const EVP_MD *crypto_rsa_md(const char *name) {
    if (strcmp(name, "sha256") == 0) return EVP_sha256();
    if (strcmp(name, "sha512") == 0) return EVP_sha512();
    if (strcmp(name, "sha1") == 0) return EVP_sha1();
    return NULL;
}

/* ---- AES-CTR --------------------------------------------------------------*/

static const EVP_CIPHER *crypto_aes_ctr_cipher(size_t keylen) {
    switch (keylen) {
    case 16: return EVP_aes_128_ctr();
    case 24: return EVP_aes_192_ctr();
    case 32: return EVP_aes_256_ctr();
    default: return NULL;
    }
}

/* The counter `blocks` blocks on: a 128-bit big-endian add, which is what
 * OpenSSL's CTR does between blocks (SP 800-38A's standard incrementing
 * function over the whole block, as SSH specifies it in RFC 4344). */
static void crypto_ctr_advance(unsigned char *ctr, size_t blocks) {
    unsigned long long add = (unsigned long long)blocks;
    for (int i = 15; i >= 0 && add; i--) {
        unsigned long long sum = (unsigned long long)ctr[i] + (add & 0xff);
        ctr[i] = (unsigned char)(sum & 0xff);
        add = (add >> 8) + (sum >> 8);
    }
}
#endif /* HAVE_LIBCRYPTO */

/* ---- dispatch -------------------------------------------------------------*/

static int crypto_ssh_is(const char *name) {
    static const char *const names[] = {
        "x25519_keypair", "x25519_public", "x25519_shared",
        "rsa_generate", "rsa_public", "rsa_sign", "rsa_verify", "rsa_from_pem", "rsa_to_pem",
        "aes_ctr", NULL
    };
    for (size_t i = 0; names[i]; i++) {
        if (strcmp(names[i], name) == 0) return 1;
    }
    return 0;
}

/* 1 when `expr` is one of this file's builtins, with the answer in *out. */
static int crypto_ssh_call(AstExpr *expr, Value *out) {
    const char *name = expr->as.call.name;
    if (!crypto_ssh_is(name)) {
        return 0;
    }
    *out = value_null();
#if !HAVE_LIBCRYPTO
    CRYPTO_SSH_NO_LIBCRYPTO(name);
    return 1;
#else
    Value a[4];

    if (strcmp(name, "x25519_keypair") == 0) {
        if (!crypto_ssh_args(expr, name, 0, 0, a)) return 1;
        unsigned char priv[32], pub[32];
        if (RAND_bytes(priv, 32) != 1 || !crypto_x25519_public_of(priv, pub)) {
            crypto_ssh_raise(name, "could not generate a key pair");
            return 1;
        }
        Value rec = value_record(NULL, 0);
        record_set(&rec, "public", crypto_ssh_bytes(pub, 32));
        record_set(&rec, "private", crypto_ssh_bytes(priv, 32));
        OPENSSL_cleanse(priv, 32);
        *out = rec;
        return 1;
    }

    if (strcmp(name, "x25519_public") == 0) {
        if (!crypto_ssh_args(expr, name, 1, 1, a)) return 1;
        if (a[0].kind != VALUE_STRING || string_length(a[0].as.string) != 32) {
            crypto_ssh_free(a, 1);
            crypto_ssh_raise(name, "the private key must be 32 bytes");
            return 1;
        }
        unsigned char pub[32];
        int ok = crypto_x25519_public_of((const unsigned char *)a[0].as.string, pub);
        crypto_ssh_free(a, 1);
        if (!ok) {
            crypto_ssh_raise(name, "OpenSSL refused the key");
            return 1;
        }
        *out = crypto_ssh_bytes(pub, 32);
        return 1;
    }

    if (strcmp(name, "x25519_shared") == 0) {
        if (!crypto_ssh_args(expr, name, 2, 2, a)) return 1;
        if (a[0].kind != VALUE_STRING || a[1].kind != VALUE_STRING ||
            string_length(a[0].as.string) != 32 || string_length(a[1].as.string) != 32) {
            crypto_ssh_free(a, 2);
            crypto_ssh_raise(name, "expects our 32-byte private key and the peer's 32-byte public key");
            return 1;
        }
        unsigned char secret[32];
        int ok = crypto_x25519_shared((const unsigned char *)a[0].as.string,
                                      (const unsigned char *)a[1].as.string, secret);
        crypto_ssh_free(a, 2);
        /* A RAISE, NOT `unknown` (reference.md, "these builtins pre-validate
         * rather than degrade"): Transward has already been burned once by a
         * crypto builtin answering `unknown`, which `string()` turns into the
         * word "unknown" and a caller then uses as a secret. An all-zero
         * secret is the peer's doing -- a low-order point -- so the sentence
         * says so, and `on error` is how a server drops that connection. */
        if (ok) {
            *out = crypto_ssh_bytes(secret, 32);
        } else {
            crypto_ssh_raise(name, "the peer's public key gives an all-zero secret "
                                   "(a low-order point, refused by RFC 7748 section 6.1)");
        }
        OPENSSL_cleanse(secret, 32);
        return 1;
    }

    if (strcmp(name, "rsa_generate") == 0) {
        if (!crypto_ssh_args(expr, name, 1, 1, a)) return 1;
        double bits = a[0].kind == VALUE_NUMBER ? a[0].as.num.value : 0;
        value_free(a[0]);
        /* 2048 IS THE FLOOR SSH'S OWN TOOLS SET (ssh-keygen refuses less since
         * 8.8 for new keys); 16384 a ceiling that is still minutes, not hours. */
        if (bits < 2048 || bits > 16384 || bits != (double)(int)bits || ((int)bits % 8) != 0) {
            crypto_ssh_raise(name, "expects the size in bits, a multiple of 8 from 2048 to 16384 (2048, 3072 or 4096)");
            return 1;
        }
        EVP_PKEY *pkey = EVP_RSA_gen((unsigned int)bits);
        if (!pkey) {
            crypto_ssh_raise(name, "could not generate a key");
            return 1;
        }
        *out = crypto_rsa_to_record(pkey);
        EVP_PKEY_free(pkey);
        return 1;
    }

    if (strcmp(name, "rsa_public") == 0) {
        if (!crypto_ssh_args(expr, name, 1, 1, a)) return 1;
        char why[200];
        EVP_PKEY *pkey = crypto_rsa_from_record(&a[0], 0, why, sizeof(why));
        value_free(a[0]);
        if (!pkey) {
            crypto_ssh_raise(name, why);
            return 1;
        }
        Value rec = value_record(NULL, 0);
        BIGNUM *n = NULL, *e = NULL;
        EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_RSA_N, &n);
        EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_RSA_E, &e);
        record_set(&rec, "n", crypto_bn_value(n));
        record_set(&rec, "e", crypto_bn_value(e));
        BN_free(n);
        BN_free(e);
        EVP_PKEY_free(pkey);
        *out = rec;
        return 1;
    }

    if (strcmp(name, "rsa_sign") == 0 || strcmp(name, "rsa_verify") == 0) {
        int signing = strcmp(name, "rsa_sign") == 0;
        size_t argc = signing ? 3 : 4;
        if (!crypto_ssh_args(expr, name, argc, argc, a)) return 1;
        Value *hash = &a[argc - 1];
        const EVP_MD *md = hash->kind == VALUE_STRING ? crypto_rsa_md(hash->as.string) : NULL;
        if (!md) {
            crypto_ssh_free(a, argc);
            crypto_ssh_raise(name, signing ? "expects the hash as \"sha256\" or \"sha512\""
                                           : "expects the hash as \"sha256\", \"sha512\" or \"sha1\"");
            return 1;
        }
        /* SHA-1 VERIFIES AND DOES NOT SIGN. `ssh-rsa` is a legacy server's
         * host key, accepted under an explicit policy; nothing new should be
         * made with it, and refusing here is what makes that a rule. */
        if (signing && md == EVP_sha1()) {
            crypto_ssh_free(a, argc);
            crypto_ssh_raise(name, "will not sign with SHA-1 (ssh-rsa is verify-only); use \"sha256\" or \"sha512\"");
            return 1;
        }
        if (a[1].kind != VALUE_STRING || (!signing && a[2].kind != VALUE_STRING)) {
            crypto_ssh_free(a, argc);
            crypto_ssh_raise(name, signing ? "expects the message as bytes"
                                           : "expects the message and the signature as bytes");
            return 1;
        }
        char why[200];
        EVP_PKEY *pkey = crypto_rsa_from_record(&a[0], signing, why, sizeof(why));
        if (!pkey) {
            crypto_ssh_free(a, argc);
            crypto_ssh_raise(name, why);
            return 1;
        }
        EVP_MD_CTX *mctx = EVP_MD_CTX_new();
        EVP_PKEY_CTX *pctx = NULL;
        if (!mctx) abort();
        if (signing) {
            size_t siglen = 0;
            unsigned char *sig = NULL;
            int ok = EVP_DigestSignInit(mctx, &pctx, md, NULL, pkey) == 1 &&
                     EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PADDING) == 1 &&
                     EVP_DigestSign(mctx, NULL, &siglen,
                                    (const unsigned char *)a[1].as.string,
                                    string_length(a[1].as.string)) == 1;
            if (ok) {
                sig = malloc(siglen);
                if (!sig) abort();
                ok = EVP_DigestSign(mctx, sig, &siglen, (const unsigned char *)a[1].as.string,
                                    string_length(a[1].as.string)) == 1;
            }
            if (ok) {
                *out = crypto_ssh_bytes(sig, siglen);
            } else {
                crypto_ssh_raise(name, "OpenSSL could not sign with that key");
            }
            free(sig);
        } else {
            int ok = EVP_DigestVerifyInit(mctx, &pctx, md, NULL, pkey) == 1 &&
                     EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PADDING) == 1 &&
                     EVP_DigestVerify(mctx, (const unsigned char *)a[2].as.string,
                                      string_length(a[2].as.string),
                                      (const unsigned char *)a[1].as.string,
                                      string_length(a[1].as.string)) == 1;
            ERR_clear_error();
            *out = value_bool(ok);
        }
        EVP_MD_CTX_free(mctx);
        EVP_PKEY_free(pkey);
        crypto_ssh_free(a, argc);
        return 1;
    }

    if (strcmp(name, "rsa_from_pem") == 0) {
        if (!crypto_ssh_args(expr, name, 1, 2, a)) return 1;
        size_t argc = expr->as.call.args.count;
        if (a[0].kind != VALUE_STRING || (argc == 2 && a[1].kind != VALUE_STRING)) {
            crypto_ssh_free(a, argc);
            crypto_ssh_raise(name, "expects the PEM text, and optionally its passphrase, as text");
            return 1;
        }
        /* EVERY PEM SHAPE AN RSA KEY ARRIVES IN, through one decoder: PKCS#1
         * ("BEGIN RSA PRIVATE KEY"), PKCS#8 plain and encrypted, and a public
         * key as PKCS#1 or SubjectPublicKeyInfo. The passphrase is passed
         * rather than prompted for -- no callback reaches the terminal. */
        const char *pass = argc == 2 ? a[1].as.string : "";
        BIO *bio = BIO_new_mem_buf(a[0].as.string, (int)string_length(a[0].as.string));
        if (!bio) abort();
        EVP_PKEY *pkey = NULL;
        OSSL_DECODER_CTX *dctx = OSSL_DECODER_CTX_new_for_pkey(&pkey, "PEM", NULL, "RSA", 0,
                                                               NULL, NULL);
        if (dctx) {
            OSSL_DECODER_CTX_set_passphrase(dctx, (const unsigned char *)pass, strlen(pass));
            if (OSSL_DECODER_from_bio(dctx, bio) != 1) {
                EVP_PKEY_free(pkey);
                pkey = NULL;
            }
            OSSL_DECODER_CTX_free(dctx);
        }
        ERR_clear_error();
        BIO_free(bio);
        crypto_ssh_free(a, argc);
        if (!pkey) {
            crypto_ssh_raise(name, argc == 2
                ? "no RSA key could be read from that text with that passphrase"
                : "no RSA key could be read from that text (an encrypted key needs its passphrase)");
            return 1;
        }
        if (EVP_PKEY_get_base_id(pkey) != EVP_PKEY_RSA) {
            EVP_PKEY_free(pkey);
            crypto_ssh_raise(name, "that PEM holds a key, but not an RSA key");
            return 1;
        }
        *out = crypto_rsa_to_record(pkey);
        EVP_PKEY_free(pkey);
        return 1;
    }

    if (strcmp(name, "rsa_to_pem") == 0) {
        if (!crypto_ssh_args(expr, name, 1, 1, a)) return 1;
        char why[200];
        int priv = a[0].kind == VALUE_RECORD && crypto_rsa_is_private(&a[0]);
        EVP_PKEY *pkey = crypto_rsa_from_record(&a[0], priv, why, sizeof(why));
        value_free(a[0]);
        if (!pkey) {
            crypto_ssh_raise(name, why);
            return 1;
        }
        /* PKCS#8 FOR A PRIVATE KEY and SubjectPublicKeyInfo for a public one:
         * the two forms every current tool reads. Unencrypted -- a file's
         * protection is its mode, and encrypting is a different verb. */
        BIO *bio = BIO_new(BIO_s_mem());
        if (!bio) abort();
        int ok = priv ? PEM_write_bio_PKCS8PrivateKey(bio, pkey, NULL, NULL, 0, NULL, NULL) == 1
                      : PEM_write_bio_PUBKEY(bio, pkey) == 1;
        EVP_PKEY_free(pkey);
        if (!ok) {
            BIO_free(bio);
            crypto_ssh_raise(name, "OpenSSL could not write the key");
            return 1;
        }
        char *text = NULL;
        long len = BIO_get_mem_data(bio, &text);
        *out = value_string_n(text, (size_t)len);
        BIO_free(bio);
        return 1;
    }

    if (strcmp(name, "aes_ctr") == 0) {
        if (!crypto_ssh_args(expr, name, 3, 3, a)) return 1;
        const EVP_CIPHER *cipher = a[0].kind == VALUE_STRING
                                       ? crypto_aes_ctr_cipher(string_length(a[0].as.string))
                                       : NULL;
        if (!cipher || a[1].kind != VALUE_STRING || string_length(a[1].as.string) != 16 ||
            a[2].kind != VALUE_STRING) {
            crypto_ssh_free(a, 3);
            crypto_ssh_raise(name, "expects a 16, 24 or 32-byte key, a 16-byte counter block and the data");
            return 1;
        }
        size_t len = string_length(a[2].as.string);
        unsigned char *buf = malloc(len + 1);
        if (!buf) abort();
        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        if (!ctx) abort();
        int outl = 0, finl = 0;
        int ok = EVP_EncryptInit_ex(ctx, cipher, NULL, (const unsigned char *)a[0].as.string,
                                    (const unsigned char *)a[1].as.string) == 1 &&
                 EVP_EncryptUpdate(ctx, buf, &outl, (const unsigned char *)a[2].as.string,
                                   (int)len) == 1 &&
                 EVP_EncryptFinal_ex(ctx, buf + outl, &finl) == 1;
        EVP_CIPHER_CTX_free(ctx);
        if (!ok) {
            free(buf);
            crypto_ssh_free(a, 3);
            crypto_ssh_raise(name, "OpenSSL could not run the cipher");
            return 1;
        }
        /* THE NEXT COUNTER, so a stream of packets is one keystream without
         * the program doing 128-bit arithmetic. A partial last block uses up
         * the whole block, as every CTR stream does when it is resumed rather
         * than continued -- SSH's packets are whole blocks, so it never sees
         * the difference. */
        unsigned char next[16];
        memcpy(next, a[1].as.string, 16);
        crypto_ctr_advance(next, (len + 15) / 16);
        Value rec = value_record(NULL, 0);
        record_set(&rec, "data", crypto_ssh_bytes(buf, (size_t)(outl + finl)));
        record_set(&rec, "counter", crypto_ssh_bytes(next, 16));
        free(buf);
        crypto_ssh_free(a, 3);
        *out = rec;
        return 1;
    }
    return 0;
#endif
}
