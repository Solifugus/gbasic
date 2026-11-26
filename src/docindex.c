/* `gbasic --doc NAME` and `gbasic --explain CODE`: the binary answering
 * questions about itself, so an author -- an AI writing gBASIC most of all --
 * asks the build it is using rather than guessing a signature or reading the
 * docs of another version. The tables are src/doc_index.h, which make
 * generates from docs/reference.md, docs/ai/ERRORS.md and src/builtins.c
 * (src/docindex_gen.c); a library's function is read from the library file
 * a `load` would find. */
#include "docindex.h"

#include "builtins.h"
#include "eval.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    const char *section;
    const char *text;
} GbDocEntry;

typedef struct {
    const char *code;
    const char *table;
    const char *text;
} GbCodeEntry;

#include "doc_index.h"

#define DOC_MAX_SHOWN 4

static int doc_in_registry(const char *name) {
    for (size_t i = 0; i < gb_doc_registry_count; i++) {
        if (strcmp(gb_doc_registry[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

/* edit distance, for the names a misspelling is closest to */
static size_t doc_distance(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    if (la > 40 || lb > 40) {
        return 99;
    }
    size_t row[41];
    for (size_t j = 0; j <= lb; j++) {
        row[j] = j;
    }
    for (size_t i = 1; i <= la; i++) {
        size_t prev = row[0];
        row[0] = i;
        for (size_t j = 1; j <= lb; j++) {
            size_t cur = row[j];
            size_t best = prev + (a[i - 1] != b[j - 1]);
            if (row[j] + 1 < best) {
                best = row[j] + 1;
            }
            if (row[j - 1] + 1 < best) {
                best = row[j - 1] + 1;
            }
            row[j] = best;
            prev = cur;
        }
    }
    return row[lb];
}

static void doc_suggest(const char *name, FILE *out) {
    const char *seen[8];
    size_t count = 0;
    for (size_t limit = 1; limit <= 2 && count == 0; limit++) {
        for (size_t i = 0; i < gb_doc_entry_count && count < 8; i++) {
            const char *n = gb_doc_entries[i].name;
            int dup = 0;
            for (size_t k = 0; k < count; k++) {
                dup |= strcmp(seen[k], n) == 0;
            }
            if (!dup && doc_distance(name, n) <= limit) {
                seen[count++] = n;
            }
        }
    }
    if (count) {
        fprintf(out, "did you mean:");
        for (size_t k = 0; k < count; k++) {
            fprintf(out, " %s", seen[k]);
        }
        fprintf(out, "\n");
    }
}

static char *doc_copy(const char *s) {
    size_t n = strlen(s) + 1;
    char *c = malloc(n);
    if (!c) {
        abort();
    }
    memcpy(c, s, n);
    return c;
}

/* the file a `load` of `library` would find: GBASIC_PATH, then this
 * binary's own stdlib, then the compiled-in one; by file name, as stdlib's
 * libraries are named */
static char *doc_library_file(const char *library) {
    const char *dirs[3] = { NULL, gb_exe_relative_stdlib(), NULL };
#ifdef GBASIC_DEFAULT_STDLIB
    dirs[2] = GBASIC_DEFAULT_STDLIB[0] ? GBASIC_DEFAULT_STDLIB : NULL;
#endif
    char path[4096];
    const char *gp = getenv("GBASIC_PATH");
    while (gp && *gp) {
        const char *end = strchr(gp, ':');
        size_t len = end ? (size_t)(end - gp) : strlen(gp);
        if (len > 0 && len < 3000) {
            snprintf(path, sizeof path, "%.*s/%s.bas", (int)len, gp, library);
            FILE *f = fopen(path, "rb");
            if (f) {
                fclose(f);
                return doc_copy(path);
            }
        }
        gp = end ? end + 1 : NULL;
    }
    for (int i = 1; i < 3; i++) {
        if (dirs[i]) {
            snprintf(path, sizeof path, "%s/%s.bas", dirs[i], library);
            FILE *f = fopen(path, "rb");
            if (f) {
                fclose(f);
                return doc_copy(path);
            }
        }
    }
    return NULL;
}

/* `library.function`: its signature and the comment above it, read from
 * the library's own file. 1 when found. */
static int doc_library_function(const char *library, const char *fn, FILE *out, int *library_found) {
    char *path = doc_library_file(library);
    *library_found = path != NULL;
    if (!path) {
        return 0;
    }
    char near[8][64];
    size_t nears = 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        free(path);
        return 0;
    }
    char want[160];
    snprintf(want, sizeof want, "function %s(", fn);
    char line[4096];
    char comment[24][4096];
    size_t comments = 0;
    size_t number = 0;
    int found = 0;
    while (fgets(line, sizeof line, f)) {
        number++;
        const char *t = line;
        while (*t == ' ' || *t == '\t') {
            t++;
        }
        if (*t == '\'') {
            if (comments == 24) {
                memmove(comment[0], comment[1], sizeof comment[0] * 23);
                comments = 23;
            }
            snprintf(comment[comments++], sizeof comment[0], "%s", t + 1 + (t[1] == ' '));
            continue;
        }
        if (strncmp(t, "function ", 9) == 0 && nears < 8) {
            const char *n0 = t + 9;
            const char *p = strchr(n0, '(');
            if (p && (size_t)(p - n0) < sizeof near[0] && !memchr(n0, '.', (size_t)(p - n0))) {
                char cand[64];
                memcpy(cand, n0, (size_t)(p - n0));
                cand[p - n0] = '\0';
                if (doc_distance(fn, cand) <= 2) {
                    snprintf(near[nears++], sizeof near[0], "%s", cand);
                }
            }
        }
        if (strncmp(t, want, strlen(want)) == 0) {
            size_t n = strlen(t);
            while (n > 0 && (t[n - 1] == '\n' || t[n - 1] == '\r')) {
                n--;
            }
            fprintf(out, "%s.%.*s\n", library, (int)(n - strlen("function ")), t + strlen("function "));
            fprintf(out, "  (library %s, %s:%zu)\n", library, path, number);
            if (comments) {
                fprintf(out, "\n");
            }
            for (size_t i = 0; i < comments; i++) {
                fprintf(out, "%s", comment[i]);
            }
            found = 1;
            break;
        }
        comments = 0;
    }
    fclose(f);
    if (!found) {
        fprintf(out, "library %s (%s) defines no function %s\n", library, path, fn);
        if (nears) {
            fprintf(out, "did you mean:");
            for (size_t i = 0; i < nears; i++) {
                fprintf(out, " %s.%s", library, near[i]);
            }
            fprintf(out, "\n");
        }
    }
    free(path);
    return found;
}

int gb_doc_main(const char *name, FILE *out) {
    if (gb_doc_entry_count == 0) {
        fprintf(out, "this build carries no documentation index (it was built without docs/)\n");
        return 1;
    }
    size_t shown = 0;
    for (size_t i = 0; i < gb_doc_entry_count && shown < DOC_MAX_SHOWN; i++) {
        if (strcmp(gb_doc_entries[i].name, name) != 0) {
            continue;
        }
        int dup = 0;
        for (size_t k = 0; k < i; k++) {
            dup |= strcmp(gb_doc_entries[k].name, name) == 0 &&
                   strcmp(gb_doc_entries[k].text, gb_doc_entries[i].text) == 0;
        }
        if (dup) {
            continue;
        }
        if (shown) {
            fprintf(out, "\n");
        }
        fprintf(out, "%s  (docs/reference.md: %s)\n%s\n", name, gb_doc_entries[i].section,
                gb_doc_entries[i].text);
        shown++;
    }
    const char *dot = strchr(name, '.');
    if (!shown && dot) {
        char library[128];
        size_t n = (size_t)(dot - name);
        if (n < sizeof library) {
            memcpy(library, name, n);
            library[n] = '\0';
            int library_found = 0;
            if (doc_library_function(library, dot + 1, out, &library_found)) {
                return 0;
            }
            if (library_found) {
                for (size_t i = 0; i < gb_doc_entry_count; i++) {
                    if (strcmp(gb_doc_entries[i].name, dot + 1) == 0) {
                        fprintf(out, "%s is a builtin, called without a qualifier: gbasic --doc %s\n",
                                dot + 1, dot + 1);
                        break;
                    }
                }
                return 1;
            }
        }
    }
    if (shown && !dot && doc_in_registry(name) && !gbasic_has_builtin(name)) {
        fprintf(out, "\nnot in this build: has_builtin(\"%s\") is false here (gbasic --modules says what each part needs)\n",
                name);
    }
    if (!shown) {
        fprintf(out, "no entry for %s%s\n", name,
                doc_in_registry(name) ? " (a builtin the reference does not describe yet)" : "");
        doc_suggest(name, out);
        return 1;
    }
    return 0;
}

int gb_explain_main(const char *code, FILE *out) {
    long want = strtol(code, NULL, 10);
    size_t shown = 0;
    for (size_t i = 0; i < gb_code_entry_count; i++) {
        const char *c = gb_code_entries[i].code;
        long lo = strtol(c, NULL, 10);
        const char *dash = strchr(c, '-');
        long hi = dash ? strtol(dash + 1, NULL, 10) : lo;
        if (want < lo || want > hi) {
            continue;
        }
        if (shown) {
            fprintf(out, "\n");
        }
        fprintf(out, "%s as a %s:\n%s\n", c, gb_code_entries[i].table, gb_code_entries[i].text);
        if (strstr(gb_code_entries[i].table, "warning")) {
            fprintf(out, "(`on warning ignore` silences warnings; `on warning stop` makes them errors)\n");
        }
        shown++;
    }
    if (!shown) {
        fprintf(out, "no code %s in docs/ai/ERRORS.md\n", code);
        return 1;
    }
    return 0;
}
