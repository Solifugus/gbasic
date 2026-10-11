#include "builtins.h"

#include <string.h>

int gbasic_builtin_function(const char *name) {
    static const char *builtins[] = {
        "compare",
        "env",
        "default",
        "has_builtin",
        "sleep",
        /* THE REGISTRY ANSWERS FOR THIS BUILD, which it did not until 0.7.1.
         * `has_builtin` is documented as the way to degrade gracefully across
         * releases, and on two of the three published 0.7.0 artifacts it could
         * not answer the question it exists for: it affirmed `password_hash`,
         * `sha256` and `compress` on builds that cannot run any of them, so a
         * program that CHECKED FIRST then called crashed on the branch it had
         * checked for. Reported by the books session, and the inverse was
         * already open as DOGFOOD 46. A probe that can be wrong is worse than
         * none -- which is this file's own comment, thirty lines down, about a
         * narrower case. */
#if HAVE_LIBXCRYPT || HAVE_YESCRYPT
        "password_hash",
        "password_verify",
#endif
        "secure_token",
        "epoch",
        "from_epoch",
        "to_zone",
        "from_zone",
        "zone_offset",
        "zone_resolve",
        "monotonic",
        "base32_decode",
        "base32_encode",
        "base64_encode",
        "base64_decode",
        "base64url_encode",
        "base64url_decode",
        "hex_encode",
        "hex_decode",
#if HAVE_ZLIB
        "compress",
        "uncompress",
#endif
        "byte_slice",
        "byte_find",
        "to_bytes",
        "random_bytes",
        "bytes_equal",
#if HAVE_LIBCRYPTO
        "sha256",
        "sha512",
        "sha1",
        "md5",
        "hmac_sha256",
        "hmac_sha1",
        "hmac_sha512",
        /* THREE THE REGISTRY HAD NEVER HELD, which is the DOGFOOD 46 direction
         * of the same defect: the dispatch knows all three and `has_builtin`
         * answered FALSE for them, so a probing program took its fallback path
         * for a key-derivation function the build could run. Found by probing
         * every registry name against the published lean archive and then
         * asking what the archive could do that the registry denied. */
        "pbkdf2_sha256",
        "pbkdf2_sha512",
        "scrypt",
#endif
        "band",
        "bor",
        "bxor",
        "bnot",
        "shl",
        "shr",
        "rotl",
        "rotr",
#if HAVE_LIBCRYPTO
                "aes_gcm_encrypt",
        "aes_gcm_decrypt",
        "ed25519_keypair",
        "ed25519_sign",
        "ed25519_verify",
#endif
        "exit",
        "now",
        "string",
        "number",
        "boolean",
        "array",
        "record",
        "replace",
        "starts_with",
        "ends_with",
        "repeat",
        "chr",
        "code",
        "byte_count",
        "byte_at",
        "from_bytes",
        "keys",
        "values",
        "has",
        "remove_key",
        "count",
        "type",
        "is_string",
        "is_number",
        "is_boolean",
        "is_array",
        "is_record",
        "is_nothing",
        "is_unknown",
        "lower",
        "upper",
        "input",
        "encode",
        "decode",
        "try_decode",
        "json_encode",
        "json_encodable",
        "source_outline",
        "watchers",
        "serialize",
        "deserialize",
        "self",
        "send",
        "receive",
        "monitor",
        "demonitor",
        "quote",
        "round",
        "len",
        "find",
        "contains",
        /* Regex (docs/text_design.md §3). `contains`, `replace` and `split`
         * above are OVERLOADED on a regex argument rather than duplicated under
         * an re_* prefix; only the two verbs whose return shape has no literal
         * counterpart need names of their own. */
        "regex",
        "match",
        "match_all",
        "remove_value",
        "find_by",
        "join_from",
        "first",
        "last",
        "slice",
        "present",
        "rest",
        "left",
        "right",
        "mid",
        "trim",
        "split",
        "join",
        "append",
        "delete",
        "copy",
        "move",
        "list_files",
        "make_dir",
        "remove_dir",
        "file_size",
        "file_mtime",
        "atomic_replace",
        "overwrite",
        "read_lines",
        "join_path",
        "library_collisions",
        "principal",
#if HAVE_LIBXCRYPT || HAVE_YESCRYPT
        "password_hash_cost",
#endif
        "real_path",
        "file_type",
        "file_name",
        "directory_name",
        "extension",
        "prepend",
        "insert",
        "remove",
        "take_first",
        "take_last",
        "reverse",
        "unique",
        "sort",
        "sum",
        "mean",
        "median",
        "mode",
        "min",
        "max",
        "variance",
        "stdev",
        "pvariance",
        "pstdev",
        "skewness",
        "kurtosis",
        "range",
        "iqr",
        "quantile",
        "percentile",
        "correlation",
        "covariance",
        "sqrt",
        "abs",
        "exp",
        "log",
        "log10",
        "floor",
        "ceil",
        "erf",
        "erfc",
        "lgamma",
        "sign",
        "pow",
        "seed",
        "random",
        "random_int"
    };

    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
        if (strcmp(name, builtins[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* The feature-probe answer, read by has_builtin().
 *
 * The registry above is what the PARSER consults, and it is deliberately not
 * the whole callable surface: the file/dir call families (exists, read, write,
 * list, ...) are dispatched by name inside eval.c and were never registered
 * here. A probe that consulted only the registry answered false for `exists`
 * -- a builtin that has worked since the beginning -- and a probe that can be
 * wrong is worse than none.
 *
 * MAINTENANCE RULE: when adding a builtin to eval.c's top-level dispatch
 * without registering it above, add its name here. Forgetting is the SAFE
 * failure -- has_builtin answers false and a probing program takes its
 * fallback path -- but it is still a wrong answer, so tests/has_builtin.bas
 * pins every name in both lists.
 *
 * Module-scoped names (process.run, webserver.listen, gui.window ...) are
 * deliberately absent: they are not callable unqualified, and has_builtin
 * refuses dotted names rather than answering for them. */
int gbasic_has_builtin(const char *name) {
    static const char *dispatch_only[] = {
        /* file calls (eval_file_call) */
        "exists",
        "read",
        "write",
        "bytes",
        "lines",
        "chars",
        "lock",
        "unlock",
        /* directory calls (eval_dir_call) */
        "list",
        "files",
        "folders",
        /* value calls dispatched in eval_call, never registered above.
           Found 2026-09-30 by deriving the list from docs/reference.md's own
           "Core Builtin Functions" section and probing each of its 117 names
           -- these three were the only misses, and `mod` is the one a
           diagnostic now points people at, so a probe answering false for it
           would have made that diagnostic a lie. run_docs_gate.sh derives the
           same list every run, which is what stops this rotting again: the
           MAINTENANCE RULE above was already written, and was still forgotten
           three times. */
        "mod",
        "concat",
        "key",
        "bound",
        "any",
        "all",
        "merge",
    };
    if (gbasic_builtin_function(name)) {
        return 1;
    }
    for (size_t i = 0; i < sizeof(dispatch_only) / sizeof(dispatch_only[0]); i++) {
        if (strcmp(name, dispatch_only[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

