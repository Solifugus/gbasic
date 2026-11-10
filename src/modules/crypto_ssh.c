/* SPDX-License-Identifier: Apache-2.0
 *
 * THE PRIMITIVES AN SSH IMPLEMENTATION NEEDS THAT gBASIC DID NOT HAVE
 * (docs/crypto_design.md §9): X25519 key exchange, RSA signatures over the
 * components SSH carries, AES in counter mode with the counter handed back,
 * bcrypt_pbkdf, raw ChaCha20 and Poly1305, ECDSA/ECDH on the NIST curves,
 * classic Diffie-Hellman, HKDF, and incremental hashing.
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
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/sha.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/x509.h>

#include "../vendor/bcrypt_pbkdf.c"

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

/* ---- ChaCha20 and Poly1305, the raw parts ----------------------------------
 * `chacha20-poly1305@openssh.com` is NOT RFC 8439's AEAD: OpenSSH builds it from
 * raw ChaCha20 (two keys, the packet length encrypted under one) and a
 * one-time Poly1305, so these are the parts, not the AEAD. */

static int crypto_chacha20(const unsigned char *key, const unsigned char *iv,
                           const unsigned char *in, size_t n, unsigned char *out) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) abort();
    int l1 = 0, l2 = 0;
    int ok = EVP_EncryptInit_ex(ctx, EVP_chacha20(), NULL, key, iv) == 1 &&
             EVP_EncryptUpdate(ctx, out, &l1, in, (int)n) == 1 &&
             EVP_EncryptFinal_ex(ctx, out + l1, &l2) == 1 &&
             (size_t)(l1 + l2) == n;
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

static int crypto_poly1305(const unsigned char *key, const unsigned char *msg, size_t n,
                           unsigned char *tag16) {
    EVP_MAC *mac = EVP_MAC_fetch(NULL, "POLY1305", NULL);
    EVP_MAC_CTX *ctx = mac ? EVP_MAC_CTX_new(mac) : NULL;
    size_t len = 0;
    int ok = ctx && EVP_MAC_init(ctx, key, 32, NULL) == 1 &&
             EVP_MAC_update(ctx, msg, n) == 1 &&
             EVP_MAC_final(ctx, tag16, &len, 16) == 1 && len == 16;
    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);
    return ok;
}

/* ---- ECDSA and ECDH on the NIST curves -------------------------------------
 * A key is a record `{ curve, public }` or `{ curve, public, private }`:
 * `curve` is "p256", "p384" or "p521" (SSH's nistp256/384/521), `public` the
 * SEC1 uncompressed point (04 || X || Y) SSH carries, `private` the scalar,
 * big-endian, padded to the curve's size. */

typedef struct {
    const char *name;     /* gBASIC's */
    const char *group;    /* OpenSSL's */
    int nid;
    size_t bytes;         /* a coordinate or a scalar */
    const EVP_MD *(*md)(void);
} CryptoCurve;

static const CryptoCurve crypto_curves[] = {
    { "p256", "P-256", NID_X9_62_prime256v1, 32, EVP_sha256 },
    { "p384", "P-384", NID_secp384r1, 48, EVP_sha384 },
    { "p521", "P-521", NID_secp521r1, 66, EVP_sha512 },
};

static const CryptoCurve *crypto_curve_named(const char *name) {
    for (size_t i = 0; i < sizeof(crypto_curves) / sizeof(crypto_curves[0]); i++) {
        if (strcmp(crypto_curves[i].name, name) == 0) return &crypto_curves[i];
    }
    return NULL;
}

/* The public point of a scalar, uncompressed. */
static int crypto_ec_public_of(const CryptoCurve *c, const BIGNUM *priv, unsigned char *out,
                               size_t *outlen) {
    EC_GROUP *grp = EC_GROUP_new_by_curve_name(c->nid);
    EC_POINT *pt = grp ? EC_POINT_new(grp) : NULL;
    int ok = pt && EC_POINT_mul(grp, pt, priv, NULL, NULL, NULL) == 1;
    if (ok) {
        *outlen = EC_POINT_point2oct(grp, pt, POINT_CONVERSION_UNCOMPRESSED, out,
                                     1 + 2 * c->bytes, NULL);
        ok = *outlen == 1 + 2 * c->bytes;
    }
    EC_POINT_free(pt);
    EC_GROUP_free(grp);
    return ok;
}

/* An EVP_PKEY from a curve, a public point and optionally a scalar. NULL with
 * `why` set. The point is CHECKED to lie on the curve: an ECDH peer that sends
 * one off it is the invalid-curve attack. Measured, OpenSSL 3.5's import
 * already refuses such a point, so the explicit public check is a backstop
 * for a libcrypto that does not: removing it leaves the suite green on this
 * machine, and that is why it is said here. The pairwise check is not a
 * backstop -- without it a record pairing one key's scalar with another's
 * point is accepted (measured). */
static EVP_PKEY *crypto_ec_key(const CryptoCurve *c, const unsigned char *pub, size_t publen,
                               const BIGNUM *priv, char *why, size_t why_size) {
    if (publen != 1 + 2 * c->bytes || pub[0] != 4) {
        snprintf(why, why_size, "the public key must be an uncompressed %s point (%zu bytes "
                 "beginning 04)", c->name, 1 + 2 * c->bytes);
        return NULL;
    }
    OSSL_PARAM_BLD *bld = OSSL_PARAM_BLD_new();
    if (!bld) abort();
    int ok = OSSL_PARAM_BLD_push_utf8_string(bld, OSSL_PKEY_PARAM_GROUP_NAME, c->group, 0) &&
             OSSL_PARAM_BLD_push_octet_string(bld, OSSL_PKEY_PARAM_PUB_KEY, pub, publen) &&
             (!priv || OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_PRIV_KEY, priv));
    OSSL_PARAM *params = ok ? OSSL_PARAM_BLD_to_param(bld) : NULL;
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
    EVP_PKEY *pkey = NULL;
    ok = params && ctx && EVP_PKEY_fromdata_init(ctx) == 1 &&
         EVP_PKEY_fromdata(ctx, &pkey, priv ? EVP_PKEY_KEYPAIR : EVP_PKEY_PUBLIC_KEY,
                           params) == 1;
    if (ok) {
        EVP_PKEY_CTX *check = EVP_PKEY_CTX_new(pkey, NULL);
        ok = check && EVP_PKEY_public_check(check) == 1 &&
             (!priv || EVP_PKEY_pairwise_check(check) == 1);
        EVP_PKEY_CTX_free(check);
        if (!ok) {
            snprintf(why, why_size, priv ? "the private and public parts are not one %s key"
                                         : "that point is not on %s", c->name);
        }
    } else {
        snprintf(why, why_size, "that point is not on %s", c->name);
    }
    ERR_clear_error();
    if (!ok) {
        EVP_PKEY_free(pkey);
        pkey = NULL;
    }
    EVP_PKEY_CTX_free(ctx);
    OSSL_PARAM_free(params);
    OSSL_PARAM_BLD_free(bld);
    return pkey;
}

/* A big-endian BIGNUM, left-padded to `bytes`. */
static Value crypto_bn_padded(const BIGNUM *b, size_t bytes) {
    unsigned char *buf = malloc(bytes ? bytes : 1);
    if (!buf) abort();
    if (BN_bn2binpad(b, buf, (int)bytes) < 0) {
        free(buf);
        return value_null();
    }
    Value v = crypto_ssh_bytes(buf, bytes);
    OPENSSL_cleanse(buf, bytes);
    free(buf);
    return v;
}

/* Read the record's curve, public point and (if `want_private`) scalar. */
static EVP_PKEY *crypto_ec_from_record(Value *key, int want_private, const CryptoCurve **curve,
                                       char *why, size_t why_size) {
    if (key->kind != VALUE_RECORD) {
        snprintf(why, why_size, "the key must be a record { curve, public%s }",
                 want_private ? ", private" : "");
        return NULL;
    }
    RecordField *fc = record_find(key, "curve");
    RecordField *fp = record_find(key, "public");
    RecordField *fs = record_find(key, "private");
    if (!fc || fc->value->kind != VALUE_STRING ||
        !(*curve = crypto_curve_named(fc->value->as.string))) {
        snprintf(why, why_size, "the key's `curve` must be \"p256\", \"p384\" or \"p521\"");
        return NULL;
    }
    if (!fp || fp->value->kind != VALUE_STRING) {
        snprintf(why, why_size, "the key has no `public` point");
        return NULL;
    }
    if (want_private && (!fs || fs->value->kind != VALUE_STRING)) {
        snprintf(why, why_size, "this is a public key (it has no `private`); it cannot sign or agree");
        return NULL;
    }
    BIGNUM *priv = NULL;
    if (want_private) {
        priv = BN_bin2bn((const unsigned char *)fs->value->as.string,
                         (int)string_length(fs->value->as.string), NULL);
        if (!priv) abort();
    }
    EVP_PKEY *pkey = crypto_ec_key(*curve, (const unsigned char *)fp->value->as.string,
                                   string_length(fp->value->as.string), priv, why, why_size);
    BN_clear_free(priv);
    return pkey;
}

/* ---- classic Diffie-Hellman ------------------------------------------------
 * Big-number arithmetic, which is the whole reason it needs a binding: SSH's
 * groups are 2048 to 8192 bits and gBASIC's numbers are doubles. */

static BIGNUM *crypto_bn_of(Value *v) {
    if (v->kind != VALUE_STRING || string_length(v->as.string) == 0) return NULL;
    BIGNUM *b = BN_bin2bn((const unsigned char *)v->as.string,
                          (int)string_length(v->as.string), NULL);
    if (!b) abort();
    return b;
}

/* ---- incremental hashing ---------------------------------------------------
 * THE STATE IS A VALUE, not a handle: `s = hash_update(s, chunk)` answers the
 * new state, so it copies, crosses to an actor and needs no closing, as every
 * other gBASIC value does. It is OpenSSL's own SHA-2 context, exported as
 * bytes behind a tag. Those contexts are the deprecated low-level API, and
 * that is the price of a state that can be copied; EVP's cannot be
 * serialized.
 *
 * A STATE IS CHECKED BEFORE IT IS USED, because it is bytes a program could
 * have altered: `num` indexes OpenSSL's block buffer, so a forged one past the
 * block would write past it. A state is refused unless its tag, size, length
 * and position are a real context's. */

#define CRYPTO_HASH_TAG "gbhash1"

typedef enum { HASH_SHA256 = 1, HASH_SHA384 = 2, HASH_SHA512 = 3 } CryptoHashAlg;

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

static size_t crypto_hash_ctx_size(int alg) {
    return alg == HASH_SHA256 ? sizeof(SHA256_CTX) : sizeof(SHA512_CTX);
}

static Value crypto_hash_state(int alg, const void *ctx) {
    size_t tag = sizeof(CRYPTO_HASH_TAG) - 1;
    size_t n = tag + 1 + crypto_hash_ctx_size(alg);
    unsigned char *buf = malloc(n);
    if (!buf) abort();
    memcpy(buf, CRYPTO_HASH_TAG, tag);
    buf[tag] = (unsigned char)alg;
    memcpy(buf + tag + 1, ctx, crypto_hash_ctx_size(alg));
    Value v = crypto_ssh_bytes(buf, n);
    free(buf);
    return v;
}

/* 1 with the algorithm and the context copied out, 0 if `v` is not a state. */
static int crypto_hash_unpack(Value *v, int *alg, SHA256_CTX *c256, SHA512_CTX *c512) {
    size_t tag = sizeof(CRYPTO_HASH_TAG) - 1;
    if (v->kind != VALUE_STRING) return 0;
    const unsigned char *b = (const unsigned char *)v->as.string;
    size_t n = string_length(v->as.string);
    if (n < tag + 1 || memcmp(b, CRYPTO_HASH_TAG, tag) != 0) return 0;
    *alg = b[tag];
    if (*alg != HASH_SHA256 && *alg != HASH_SHA384 && *alg != HASH_SHA512) return 0;
    if (n != tag + 1 + crypto_hash_ctx_size(*alg)) return 0;
    if (*alg == HASH_SHA256) {
        memcpy(c256, b + tag + 1, sizeof(*c256));
        return c256->num < SHA256_CBLOCK && c256->md_len == SHA256_DIGEST_LENGTH;
    }
    memcpy(c512, b + tag + 1, sizeof(*c512));
    return c512->num < SHA512_CBLOCK &&
           c512->md_len == (*alg == HASH_SHA384 ? SHA384_DIGEST_LENGTH : SHA512_DIGEST_LENGTH);
}

static Value crypto_hash_new(int alg) {
    SHA256_CTX c256;
    SHA512_CTX c512;
    Value v;
    if (alg == HASH_SHA256) {
        SHA256_Init(&c256);
        v = crypto_hash_state(alg, &c256);
    } else {
        if (alg == HASH_SHA384) SHA384_Init(&c512); else SHA512_Init(&c512);
        v = crypto_hash_state(alg, &c512);
    }
    return v;
}

/* The state after `d`, or (when `d` is NULL) the digest. 0 if `state` is not
 * one: the context is copied out, advanced and copied back, so the caller's
 * state is never written. */
static int crypto_hash_step(Value *state, const unsigned char *d, size_t n, Value *out) {
    int alg = 0;
    SHA256_CTX c256;
    SHA512_CTX c512;
    memset(&c256, 0, sizeof(c256));
    memset(&c512, 0, sizeof(c512));
    if (!crypto_hash_unpack(state, &alg, &c256, &c512)) return 0;
    if (d) {
        if (alg == HASH_SHA256) {
            SHA256_Update(&c256, d, n);
            *out = crypto_hash_state(alg, &c256);
        } else {
            if (alg == HASH_SHA384) SHA384_Update(&c512, d, n); else SHA512_Update(&c512, d, n);
            *out = crypto_hash_state(alg, &c512);
        }
    } else {
        unsigned char md[SHA512_DIGEST_LENGTH];
        size_t len;
        if (alg == HASH_SHA256) {
            SHA256_Final(md, &c256);
            len = SHA256_DIGEST_LENGTH;
        } else if (alg == HASH_SHA384) {
            SHA384_Final(md, &c512);
            len = SHA384_DIGEST_LENGTH;
        } else {
            SHA512_Final(md, &c512);
            len = SHA512_DIGEST_LENGTH;
        }
        *out = crypto_ssh_bytes(md, len);
    }
    OPENSSL_cleanse(&c256, sizeof(c256));
    OPENSSL_cleanse(&c512, sizeof(c512));
    return 1;
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static int crypto_hash_alg_named(Value *v) {
    if (v->kind != VALUE_STRING) return 0;
    if (strcmp(v->as.string, "sha256") == 0) return HASH_SHA256;
    if (strcmp(v->as.string, "sha384") == 0) return HASH_SHA384;
    if (strcmp(v->as.string, "sha512") == 0) return HASH_SHA512;
    return 0;
}
/* ---- X.509 (x509_parse) ----------------------------------------------------*/

/* A name as RFC 2253 writes it, most specific first: what `openssl x509
 * -nameopt RFC2253` prints, and what an LDAP DN looks like. */
static Value crypto_x509_name(const X509_NAME *n) {
    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio) abort();
    X509_NAME_print_ex(bio, n, 0, XN_FLAG_RFC2253);
    char *p = NULL;
    long len = BIO_get_mem_data(bio, &p);
    Value v = value_string_n(p ? p : "", len > 0 ? (size_t)len : 0);
    BIO_free(bio);
    return v;
}

/* A certificate's time as a datetime in UTC, which is what X.509 writes
 * (UTCTime and GeneralizedTime both end in Z). */
static Value crypto_x509_time(const ASN1_TIME *t) {
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    if (!t || ASN1_TIME_to_tm(t, &tm) != 1) {
        return value_unknown();
    }
    DateTime dt = {0};
    dt.year = tm.tm_year + 1900;
    dt.month = tm.tm_mon + 1;
    dt.day = tm.tm_mday;
    dt.hour = tm.tm_hour;
    dt.minute = tm.tm_min;
    dt.second = tm.tm_sec;
    dt.precision = PREC_SECOND;
    return value_datetime(dt);
}

#endif /* HAVE_LIBCRYPTO */

/* ---- dispatch -------------------------------------------------------------*/

static int crypto_ssh_is(const char *name) {
    static const char *const names[] = {
        "x25519_keypair", "x25519_public", "x25519_shared",
        "rsa_generate", "rsa_public", "rsa_sign", "rsa_verify", "rsa_from_pem", "rsa_to_pem",
        "aes_ctr", "bcrypt_pbkdf",
        "chacha20", "poly1305",
        "ec_keypair", "ec_public", "ecdsa_sign", "ecdsa_verify", "ecdh_shared",
        "dh_keypair", "dh_shared", "hkdf", "hash_start", "hash_update", "hash_finish", "x509_parse", NULL
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
    Value a[5];

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
        /* a certificate is the commonest thing handed here that is not a key
         * (a SAML IdP publishes one), so it is named, with the verb that reads it */
        int is_cert = !pkey && memmem(a[0].as.string, string_length(a[0].as.string),
                                      "-----BEGIN CERTIFICATE-----", 27) != NULL;
        crypto_ssh_free(a, argc);
        if (is_cert) {
            crypto_ssh_raise(name, "that text is an X.509 certificate, not a key; x509_parse(text).public_key "
                                   "is the key it holds");
            return 1;
        }
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

    /* x509_parse(der_or_pem): what a certificate says (Transward M31 §10.2,
     * a SAML identity provider's signing key). It READS a certificate and
     * checks nothing: not its signature, its dates or its chain, which are
     * the caller's decisions (a SAML IdP's certificate is often self-signed
     * and pinned by metadata). The key comes back in the record shape
     * rsa_verify and ecdsa_verify already take, so a certificate's key is
     * used as any other. */
    if (strcmp(name, "x509_parse") == 0) {
        if (!crypto_ssh_args(expr, name, 1, 1, a)) return 1;
        if (a[0].kind != VALUE_STRING || string_length(a[0].as.string) == 0) {
            crypto_ssh_free(a, 1);
            crypto_ssh_raise(name, "expects a certificate as DER bytes or PEM text");
            return 1;
        }
        const unsigned char *in = (const unsigned char *)a[0].as.string;
        size_t inlen = string_length(a[0].as.string);
        X509 *cert = NULL;
        if (inlen >= 11 && memmem(in, inlen, "-----BEGIN ", 11)) {
            BIO *bio = BIO_new_mem_buf(in, (int)inlen);
            if (!bio) abort();
            cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
            BIO_free(bio);
        } else {
            const unsigned char *p = in;
            cert = d2i_X509(NULL, &p, (long)inlen);
            /* DER is the whole input or it is not this certificate */
            if (cert && p != in + inlen) {
                X509_free(cert);
                cert = NULL;
            }
        }
        ERR_clear_error();
        crypto_ssh_free(a, 1);
        if (!cert) {
            crypto_ssh_raise(name, "no X.509 certificate could be read from that (a PEM's BEGIN line must be "
                                   "CERTIFICATE; DER must be the certificate's bytes and nothing after)");
            return 1;
        }
        Value rec = value_record(NULL, 0);
        record_set(&rec, "subject", crypto_x509_name(X509_get_subject_name(cert)));
        record_set(&rec, "issuer", crypto_x509_name(X509_get_issuer_name(cert)));
        record_set(&rec, "not_before", crypto_x509_time(X509_get0_notBefore(cert)));
        record_set(&rec, "not_after", crypto_x509_time(X509_get0_notAfter(cert)));
        BIGNUM *serial = ASN1_INTEGER_to_BN(X509_get0_serialNumber(cert), NULL);
        record_set(&rec, "serial", serial ? crypto_bn_value(serial) : value_unknown());
        BN_free(serial);
        unsigned char *der = NULL;
        int derlen = i2d_X509(cert, &der);
        if (derlen > 0) {
            unsigned char md[EVP_MAX_MD_SIZE];
            unsigned int mdlen = 0;
            EVP_Digest(der, (size_t)derlen, md, &mdlen, EVP_sha256(), NULL);
            record_set(&rec, "fingerprint_sha256", crypto_ssh_bytes(md, mdlen));
            record_set(&rec, "der", crypto_ssh_bytes(der, (size_t)derlen));
        }
        OPENSSL_free(der);
        EVP_PKEY *pkey = X509_get0_pubkey(cert);
        int base = pkey ? EVP_PKEY_get_base_id(pkey) : 0;
        Value key = value_unknown();
        const char *kind = "other";
        if (base == EVP_PKEY_RSA) {
            kind = "rsa";
            key = crypto_rsa_to_record(pkey);
        } else if (base == EVP_PKEY_EC) {
            char group[64] = "";
            size_t glen = 0;
            const CryptoCurve *c = NULL;
            if (EVP_PKEY_get_utf8_string_param(pkey, OSSL_PKEY_PARAM_GROUP_NAME, group, sizeof group, &glen) == 1) {
                int nid = OBJ_txt2nid(group);
                for (size_t i = 0; i < sizeof crypto_curves / sizeof crypto_curves[0]; i++)
                    if (crypto_curves[i].nid == nid) c = &crypto_curves[i];
            }
            unsigned char pub[1 + 2 * 66];
            size_t publen = 0;
            if (c && EVP_PKEY_get_octet_string_param(pkey, OSSL_PKEY_PARAM_ENCODED_PUBLIC_KEY, pub, sizeof pub,
                                                     &publen) == 1 && publen == 1 + 2 * c->bytes) {
                kind = "ec";
                key = value_record(NULL, 0);
                record_set(&key, "curve", value_string(c->name));
                record_set(&key, "public", crypto_ssh_bytes(pub, publen));
            } else {
                kind = "ec_other";  /* a curve or point form ecdsa_verify does not take */
            }
        } else if (base == EVP_PKEY_ED25519) {
            unsigned char pub[32];
            size_t publen = sizeof pub;
            kind = "ed25519";
            if (EVP_PKEY_get_raw_public_key(pkey, pub, &publen) == 1 && publen == 32) {
                key = crypto_ssh_bytes(pub, 32);  /* as ed25519_verify takes it */
            }
        }
        ERR_clear_error();
        record_set(&rec, "key_type", value_string(kind));
        record_set(&rec, "public_key", key);
        X509_free(cert);
        *out = rec;
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

    /* THE KEY DERIVATION OF A PASSPHRASE-PROTECTED `openssh-key-v1` FILE
     * (Transward §2.4): `bcrypt_pbkdf(passphrase, salt, rounds, length)`, the
     * salt and rounds read from the file's kdfoptions. OpenSSH's own code,
     * vendored in src/vendor/bcrypt_pbkdf.c because OpenSSL has none. Its own
     * limits are checked here first so a refusal says which argument. */
    if (strcmp(name, "bcrypt_pbkdf") == 0) {
        if (!crypto_ssh_args(expr, name, 4, 4, a)) return 1;
        double rounds = a[2].kind == VALUE_NUMBER ? a[2].as.num.value : 0;
        double length = a[3].kind == VALUE_NUMBER ? a[3].as.num.value : 0;
        const char *why = NULL;
        if (a[0].kind != VALUE_STRING || string_length(a[0].as.string) == 0) {
            why = "expects the passphrase as non-empty text";
        } else if (a[1].kind != VALUE_STRING || string_length(a[1].as.string) == 0 ||
                   string_length(a[1].as.string) > (1u << 20)) {
            why = "expects the salt as bytes, at least 1 and at most 1048576";
        } else if (!(rounds >= 1 && rounds <= 4294967295.0) || rounds != (double)(unsigned long)rounds) {
            why = "expects the rounds as a whole number of at least 1";
        } else if (!(length >= 1 && length <= 1024) || length != (double)(int)length) {
            why = "expects the length in bytes, a whole number from 1 to 1024";
        }
        if (why) {
            crypto_ssh_free(a, 4);
            crypto_ssh_raise(name, why);
            return 1;
        }
        unsigned char key[1024];
        int rc = gb_bcrypt_pbkdf(a[0].as.string, string_length(a[0].as.string),
                                 (const uint8_t *)a[1].as.string, string_length(a[1].as.string),
                                 key, (size_t)length, (unsigned int)rounds);
        crypto_ssh_free(a, 4);
        if (rc != 0) {
            crypto_ssh_raise(name, "the derivation was refused");
            return 1;
        }
        *out = crypto_ssh_bytes(key, (size_t)length);
        OPENSSL_cleanse(key, sizeof(key));
        return 1;
    }

    if (strcmp(name, "chacha20") == 0) {
        if (!crypto_ssh_args(expr, name, 3, 3, a)) return 1;
        if (a[0].kind != VALUE_STRING || string_length(a[0].as.string) != 32 ||
            a[1].kind != VALUE_STRING || string_length(a[1].as.string) != 16 ||
            a[2].kind != VALUE_STRING) {
            crypto_ssh_free(a, 3);
            crypto_ssh_raise(name, "expects a 32-byte key, a 16-byte IV (the block counter, "
                                   "4 bytes little-endian, then the 12-byte nonce) and the data");
            return 1;
        }
        size_t n = string_length(a[2].as.string);
        unsigned char *buf = malloc(n + 1);
        if (!buf) abort();
        int ok = crypto_chacha20((const unsigned char *)a[0].as.string,
                                 (const unsigned char *)a[1].as.string,
                                 (const unsigned char *)a[2].as.string, n, buf);
        crypto_ssh_free(a, 3);
        if (ok) {
            *out = crypto_ssh_bytes(buf, n);
        } else {
            crypto_ssh_raise(name, "OpenSSL could not run the cipher");
        }
        free(buf);
        return 1;
    }

    if (strcmp(name, "poly1305") == 0) {
        if (!crypto_ssh_args(expr, name, 2, 2, a)) return 1;
        if (a[0].kind != VALUE_STRING || string_length(a[0].as.string) != 32 ||
            a[1].kind != VALUE_STRING) {
            crypto_ssh_free(a, 2);
            crypto_ssh_raise(name, "expects a 32-byte one-time key and the message");
            return 1;
        }
        unsigned char tag[16];
        int ok = crypto_poly1305((const unsigned char *)a[0].as.string,
                                 (const unsigned char *)a[1].as.string,
                                 string_length(a[1].as.string), tag);
        crypto_ssh_free(a, 2);
        if (ok) {
            *out = crypto_ssh_bytes(tag, 16);
        } else {
            crypto_ssh_raise(name, "OpenSSL could not compute the tag");
        }
        return 1;
    }

    if (strcmp(name, "ec_keypair") == 0) {
        if (!crypto_ssh_args(expr, name, 1, 1, a)) return 1;
        const CryptoCurve *c = a[0].kind == VALUE_STRING ? crypto_curve_named(a[0].as.string) : NULL;
        value_free(a[0]);
        if (!c) {
            crypto_ssh_raise(name, "expects the curve as \"p256\", \"p384\" or \"p521\"");
            return 1;
        }
        EVP_PKEY *pkey = EVP_EC_gen(c->group);
        BIGNUM *priv = NULL;
        unsigned char pub[1 + 2 * 66];
        size_t publen = 0;
        int ok = pkey && EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_PRIV_KEY, &priv) == 1 &&
                 crypto_ec_public_of(c, priv, pub, &publen);
        if (ok) {
            Value rec = value_record(NULL, 0);
            record_set(&rec, "curve", value_string(c->name));
            record_set(&rec, "public", crypto_ssh_bytes(pub, publen));
            record_set(&rec, "private", crypto_bn_padded(priv, c->bytes));
            *out = rec;
        } else {
            crypto_ssh_raise(name, "could not generate a key pair");
        }
        BN_clear_free(priv);
        EVP_PKEY_free(pkey);
        return 1;
    }

    if (strcmp(name, "ec_public") == 0) {
        if (!crypto_ssh_args(expr, name, 2, 2, a)) return 1;
        const CryptoCurve *c = a[0].kind == VALUE_STRING ? crypto_curve_named(a[0].as.string) : NULL;
        if (!c || a[1].kind != VALUE_STRING || string_length(a[1].as.string) == 0 ||
            string_length(a[1].as.string) > c->bytes) {
            crypto_ssh_free(a, 2);
            crypto_ssh_raise(name, "expects the curve (\"p256\", \"p384\" or \"p521\") and the "
                                   "private scalar, big-endian, at most the curve's size");
            return 1;
        }
        BIGNUM *priv = crypto_bn_of(&a[1]);
        unsigned char pub[1 + 2 * 66];
        size_t publen = 0;
        int ok = !BN_is_zero(priv) && crypto_ec_public_of(c, priv, pub, &publen);
        BN_clear_free(priv);
        crypto_ssh_free(a, 2);
        if (ok) {
            Value rec = value_record(NULL, 0);
            record_set(&rec, "curve", value_string(c->name));
            record_set(&rec, "public", crypto_ssh_bytes(pub, publen));
            *out = rec;
        } else {
            crypto_ssh_raise(name, "that scalar is not a private key on the curve");
        }
        return 1;
    }

    if (strcmp(name, "ecdsa_sign") == 0 || strcmp(name, "ecdsa_verify") == 0) {
        int signing = strcmp(name, "ecdsa_sign") == 0;
        size_t argc = signing ? 2 : 3;
        if (!crypto_ssh_args(expr, name, argc, argc, a)) return 1;
        char why[200];
        const CryptoCurve *c = NULL;
        EVP_PKEY *pkey = crypto_ec_from_record(&a[0], signing, &c, why, sizeof(why));
        if (!pkey) {
            crypto_ssh_free(a, argc);
            crypto_ssh_raise(name, why);
            return 1;
        }
        if (a[1].kind != VALUE_STRING) {
            EVP_PKEY_free(pkey);
            crypto_ssh_free(a, argc);
            crypto_ssh_raise(name, "expects the message as bytes");
            return 1;
        }
        /* THE HASH IS THE CURVE'S, as SSH fixes it (RFC 5656 §6.2.1):
         * SHA-256 for p256, SHA-384 for p384, SHA-512 for p521. An argument
         * for it would be a way to make an SSH signature no peer checks. */
        EVP_MD_CTX *mctx = EVP_MD_CTX_new();
        if (!mctx) abort();
        const unsigned char *msg = (const unsigned char *)a[1].as.string;
        size_t msglen = string_length(a[1].as.string);
        if (signing) {
            size_t derlen = 0;
            unsigned char *der = NULL;
            int ok = EVP_DigestSignInit(mctx, NULL, c->md(), NULL, pkey) == 1 &&
                     EVP_DigestSign(mctx, NULL, &derlen, msg, msglen) == 1;
            if (ok) {
                der = malloc(derlen);
                if (!der) abort();
                ok = EVP_DigestSign(mctx, der, &derlen, msg, msglen) == 1;
            }
            /* (r, s) AS BYTES, NOT DER: SSH sends them as two mpints. */
            const unsigned char *p = der;
            ECDSA_SIG *sig = ok ? d2i_ECDSA_SIG(NULL, &p, (long)derlen) : NULL;
            if (sig) {
                Value rec = value_record(NULL, 0);
                record_set(&rec, "r", crypto_bn_value(ECDSA_SIG_get0_r(sig)));
                record_set(&rec, "s", crypto_bn_value(ECDSA_SIG_get0_s(sig)));
                *out = rec;
            } else {
                crypto_ssh_raise(name, "OpenSSL could not sign with that key");
            }
            ECDSA_SIG_free(sig);
            free(der);
        } else {
            RecordField *fr = a[2].kind == VALUE_RECORD ? record_find(&a[2], "r") : NULL;
            RecordField *fs = a[2].kind == VALUE_RECORD ? record_find(&a[2], "s") : NULL;
            if (!fr || !fs || fr->value->kind != VALUE_STRING || fs->value->kind != VALUE_STRING) {
                EVP_MD_CTX_free(mctx);
                EVP_PKEY_free(pkey);
                crypto_ssh_free(a, argc);
                crypto_ssh_raise(name, "expects the signature as a record { r, s } of big-endian bytes");
                return 1;
            }
            ECDSA_SIG *sig = ECDSA_SIG_new();
            BIGNUM *r = crypto_bn_of(fr->value);
            BIGNUM *s = crypto_bn_of(fs->value);
            int ok = 0;
            if (sig && r && s && ECDSA_SIG_set0(sig, r, s) == 1) {
                r = s = NULL;
                unsigned char *der = NULL;
                int derlen = i2d_ECDSA_SIG(sig, &der);
                ok = derlen > 0 && EVP_DigestVerifyInit(mctx, NULL, c->md(), NULL, pkey) == 1 &&
                     EVP_DigestVerify(mctx, der, (size_t)derlen, msg, msglen) == 1;
                OPENSSL_free(der);
            }
            BN_free(r);
            BN_free(s);
            ECDSA_SIG_free(sig);
            ERR_clear_error();
            *out = value_bool(ok);
        }
        EVP_MD_CTX_free(mctx);
        EVP_PKEY_free(pkey);
        crypto_ssh_free(a, argc);
        return 1;
    }

    if (strcmp(name, "ecdh_shared") == 0) {
        if (!crypto_ssh_args(expr, name, 2, 2, a)) return 1;
        char why[200];
        const CryptoCurve *c = NULL;
        EVP_PKEY *mine = crypto_ec_from_record(&a[0], 1, &c, why, sizeof(why));
        EVP_PKEY *theirs = NULL;
        if (mine) {
            if (a[1].kind != VALUE_STRING) {
                snprintf(why, sizeof(why), "expects the peer's public point as bytes");
            } else {
                theirs = crypto_ec_key(c, (const unsigned char *)a[1].as.string,
                                       string_length(a[1].as.string), NULL, why, sizeof(why));
            }
        }
        crypto_ssh_free(a, 2);
        if (!mine || !theirs) {
            EVP_PKEY_free(mine);
            EVP_PKEY_free(theirs);
            crypto_ssh_raise(name, why);
            return 1;
        }
        EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(mine, NULL);
        unsigned char secret[66];
        size_t len = sizeof(secret);
        int ok = ctx && EVP_PKEY_derive_init(ctx) == 1 &&
                 EVP_PKEY_derive_set_peer(ctx, theirs) == 1 &&
                 EVP_PKEY_derive(ctx, secret, &len) == 1 && len == c->bytes;
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(mine);
        EVP_PKEY_free(theirs);
        if (ok) {
            *out = crypto_ssh_bytes(secret, len);
        } else {
            crypto_ssh_raise(name, "the key agreement was refused");
        }
        OPENSSL_cleanse(secret, sizeof(secret));
        return 1;
    }

    if (strcmp(name, "dh_keypair") == 0) {
        if (!crypto_ssh_args(expr, name, 2, 2, a)) return 1;
        BIGNUM *p = crypto_bn_of(&a[0]);
        BIGNUM *gen = crypto_bn_of(&a[1]);
        crypto_ssh_free(a, 2);
        /* 2048 BITS IS THE FLOOR (group14, RFC 8268's smallest). A smaller
         * prime is a mistake or an attack, and either way not SSH. */
        if (!p || !gen || BN_num_bits(p) < 2048 || !BN_is_odd(p) || BN_cmp(gen, BN_value_one()) <= 0) {
            BN_free(p);
            BN_free(gen);
            crypto_ssh_raise(name, "expects the group's prime p (at least 2048 bits) and its "
                                   "generator g, as big-endian bytes");
            return 1;
        }
        BN_CTX *bctx = BN_CTX_new();
        BIGNUM *range = BN_new(), *x = BN_secure_new(), *y = BN_new();
        if (!bctx || !range || !x || !y) abort();
        /* x uniform in [2, p-2], and y = g^x mod p in constant time. */
        int ok = BN_sub(range, p, BN_value_one()) && BN_sub_word(range, 2) &&
                 BN_priv_rand_range(x, range) && BN_add_word(x, 2) &&
                 BN_mod_exp_mont_consttime(y, gen, x, p, bctx, NULL);
        if (ok) {
            Value rec = value_record(NULL, 0);
            record_set(&rec, "private", crypto_bn_value(x));
            record_set(&rec, "public", crypto_bn_value(y));
            *out = rec;
        } else {
            crypto_ssh_raise(name, "could not generate a key pair");
        }
        BN_free(range);
        BN_clear_free(x);
        BN_free(y);
        BN_free(p);
        BN_free(gen);
        BN_CTX_free(bctx);
        return 1;
    }

    if (strcmp(name, "dh_shared") == 0) {
        if (!crypto_ssh_args(expr, name, 3, 3, a)) return 1;
        BIGNUM *p = crypto_bn_of(&a[0]);
        BIGNUM *x = crypto_bn_of(&a[1]);
        BIGNUM *peer = crypto_bn_of(&a[2]);
        crypto_ssh_free(a, 3);
        const char *why = NULL;
        BIGNUM *pm1 = BN_new();
        if (!pm1) abort();
        if (!p || !x || !peer) {
            why = "expects the prime p, our private exponent and the peer's public value, as big-endian bytes";
        } else if (!BN_sub(pm1, p, BN_value_one())) {
            why = "the prime could not be read";
        } else if (BN_cmp(peer, BN_value_one()) <= 0 || BN_cmp(peer, pm1) >= 0) {
            /* RFC 4253 §8: e and f outside [1, p-1] are not accepted -- 1 and
             * p-1 confine the secret to a subgroup of order 2 at most. */
            why = "the peer's public value must be greater than 1 and less than p - 1";
        }
        if (why) {
            BN_free(p); BN_clear_free(x); BN_free(peer); BN_free(pm1);
            crypto_ssh_raise(name, why);
            return 1;
        }
        BN_CTX *bctx = BN_CTX_new();
        BIGNUM *k = BN_secure_new();
        if (!bctx || !k) abort();
        int ok = BN_mod_exp_mont_consttime(k, peer, x, p, bctx, NULL);
        if (ok) {
            *out = crypto_bn_value(k);
        } else {
            crypto_ssh_raise(name, "the key agreement failed");
        }
        BN_clear_free(k);
        BN_free(p); BN_clear_free(x); BN_free(peer); BN_free(pm1);
        BN_CTX_free(bctx);
        return 1;
    }

    if (strcmp(name, "hkdf") == 0) {
        if (!crypto_ssh_args(expr, name, 5, 5, a)) return 1;
        static const char *const digests[] = { NULL, "SHA256", "SHA384", "SHA512" };
        static const int sizes[] = { 0, 32, 48, 64 };
        int alg = crypto_hash_alg_named(&a[0]);
        const char *digest = digests[alg];
        double length = a[4].kind == VALUE_NUMBER ? a[4].as.num.value : 0;
        const char *why = NULL;
        char whybuf[160];
        if (!digest) {
            why = "expects the hash as \"sha256\", \"sha384\" or \"sha512\"";
        } else if (a[1].kind != VALUE_STRING || string_length(a[1].as.string) == 0 ||
                   a[2].kind != VALUE_STRING || a[3].kind != VALUE_STRING) {
            why = "expects the key material (not empty), the salt and the info as bytes";
        } else if (!(length >= 1 && length <= 255 * sizes[alg]) || length != (double)(int)length) {
            /* RFC 5869 §2.3: L <= 255*HashLen. */
            snprintf(whybuf, sizeof(whybuf), "expects the length in bytes, a whole number from 1 "
                     "to %d (255 times %s's size)", 255 * sizes[alg], a[0].as.string);
            why = whybuf;
        }
        if (why) {
            crypto_ssh_free(a, 5);
            crypto_ssh_raise(name, why);
            return 1;
        }
        /* OpenSSL's HKDF (RFC 5869), through EVP_KDF, so a FIPS provider
         * covers it -- composing it from HMAC in gBASIC would not be. */
        EVP_KDF *kdf = EVP_KDF_fetch(NULL, "HKDF", NULL);
        EVP_KDF_CTX *kctx = kdf ? EVP_KDF_CTX_new(kdf) : NULL;
        size_t n = (size_t)length;
        unsigned char *okm = malloc(n);
        if (!okm) abort();
        OSSL_PARAM params[5];
        size_t pi = 0;
        params[pi++] = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, (char *)digest, 0);
        params[pi++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, (void *)a[1].as.string,
                                                         string_length(a[1].as.string));
        params[pi++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, (void *)a[2].as.string,
                                                         string_length(a[2].as.string));
        params[pi++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, (void *)a[3].as.string,
                                                         string_length(a[3].as.string));
        params[pi] = OSSL_PARAM_construct_end();
        int ok = kctx && EVP_KDF_derive(kctx, okm, n, params) == 1;
        EVP_KDF_CTX_free(kctx);
        EVP_KDF_free(kdf);
        crypto_ssh_free(a, 5);
        if (ok) {
            *out = crypto_ssh_bytes(okm, n);
        } else {
            ERR_clear_error();
            crypto_ssh_raise(name, "OpenSSL refused the derivation");
        }
        OPENSSL_cleanse(okm, n);
        free(okm);
        return 1;
    }

    /* THE STATE IS A VALUE (see crypto_hash_state): `s = hash_update(s, data)`. */
    if (strcmp(name, "hash_start") == 0) {
        if (!crypto_ssh_args(expr, name, 1, 1, a)) return 1;
        int alg = crypto_hash_alg_named(&a[0]);
        value_free(a[0]);
        if (!alg) {
            crypto_ssh_raise(name, "expects the hash as \"sha256\", \"sha384\" or \"sha512\"");
            return 1;
        }
        *out = crypto_hash_new(alg);
        return 1;
    }

    if (strcmp(name, "hash_update") == 0 || strcmp(name, "hash_finish") == 0) {
        int updating = strcmp(name, "hash_update") == 0;
        size_t argc = updating ? 2 : 1;
        if (!crypto_ssh_args(expr, name, argc, argc, a)) return 1;
        if (updating && a[1].kind != VALUE_STRING) {
            crypto_ssh_free(a, argc);
            crypto_ssh_raise(name, "expects the data as bytes");
            return 1;
        }
        const unsigned char *data = updating ? (const unsigned char *)a[1].as.string
                                             : NULL;
        size_t n = updating ? string_length(a[1].as.string) : 0;
        int ok = crypto_hash_step(&a[0], data, n, out);
        crypto_ssh_free(a, argc);
        if (!ok) {
            crypto_ssh_raise(name, "expects a hash state from hash_start or hash_update");
        }
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
