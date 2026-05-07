#include "builtins.h"

#include <string.h>

int gbasic_builtin_function(const char *name) {
    static const char *builtins[] = {
        "compare",
        "lower",
        "upper",
        "input",
        "round",
        "len",
        "find",
        "left",
        "right",
        "mid",
        "trim",
        "split",
        "join",
        "append",
        "prepend",
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
