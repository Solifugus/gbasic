#include "builtins.h"

#include <string.h>

int gbasic_builtin_function(const char *name) {
    static const char *builtins[] = {
        "compare",
        "string",
        "number",
        "boolean",
        "array",
        "record",
        "replace",
        "starts_with",
        "ends_with",
        "repeat",
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
        "quote",
        "round",
        "len",
        "find",
        "contains",
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
        "max"
    };

    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
        if (strcmp(name, builtins[i]) == 0) {
            return 1;
        }
    }
    return 0;
}
