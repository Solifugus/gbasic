#include "builtins.h"

#include <string.h>

int gbasic_builtin_function(const char *name) {
    static const char *builtins[] = {
        "compare",
        "env",
        "default",
        "has_builtin",
        "sleep",
        "password_hash",
        "password_verify",
        "secure_token",
        "epoch",
        "from_epoch",
        "to_zone",
        "from_zone",
        "zone_offset",
        "zone_resolve",
        "monotonic",
        "base64_encode",
        "base64_decode",
        "base64url_encode",
        "base64url_decode",
        "hex_encode",
        "hex_decode",
        "random_bytes",
        "bytes_equal",
        "sha256",
        "sha512",
        "sha1",
        "md5",
        "hmac_sha256",
        "hmac_sha512",
        "band",
        "bor",
        "bxor",
        "bnot",
        "shl",
        "shr",
        "rotl",
        "rotr",
        "aes_gcm_encrypt",
        "aes_gcm_decrypt",
        "ed25519_keypair",
        "ed25519_sign",
        "ed25519_verify",
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
        "password_hash_cost",
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

