/* docindex_gen: docs/reference.md, docs/ai/ERRORS.md and src/builtins.c
 * into the tables `gbasic --doc` and `gbasic --explain` answer from
 * (src/docindex.c). Run by make on the build machine, so the binary's
 * answers are the docs it was built with and cannot drift from them. A
 * tree without the docs (run_regex.sh builds one from src, include and
 * the Makefile) gets empty tables, and --doc says so, rather than no build.
 *
 *   docindex_gen REFERENCE ERRORS BUILTINS OUT.c
 *
 * A reference ENTRY is a line that opens a bullet (`- `), a bold term (`**`)
 * or a table row (`| `) whose head -- the words before its first dash,
 * colon or arrow, or a table row's first cell -- names one or more calls
 * (`name(`): every name in the head is indexed to the entry. The entry runs
 * to a blank line, a heading, a fence or the next entry no deeper than it,
 * and takes the ```basic block that follows it, if one does. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TEXT 1600
#define MAX_EXAMPLE 18

static char **lines;
static size_t line_count;

/* every name src/builtins.c knows, and which of them an entry named */
static char registry[512][64];
static int registered;
static int has_entry[512];

static void entry_named(const char *name) {
    for (int i = 0; i < registered; i++) {
        if (strcmp(registry[i], name) == 0) {
            has_entry[i] = 1;
        }
    }
}

/* the file's lines, or none when it is not there */
static void load(const char *path) {
    size_t cap = 1024;
    lines = realloc(lines, sizeof *lines * cap);
    line_count = 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        return;
    }
    char buf[8192];
    while (fgets(buf, sizeof buf, f)) {
        size_t n = strlen(buf);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
            buf[--n] = '\0';
        }
        if (line_count == cap) {
            cap *= 2;
            lines = realloc(lines, sizeof *lines * cap);
        }
        lines[line_count] = malloc(n + 1);
        memcpy(lines[line_count], buf, n + 1);
        line_count++;
    }
    fclose(f);
}

/* the C string literal for s, escaped */
static void emit_string(FILE *out, const char *s) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '\\': fputs("\\\\", out); break;
        case '"': fputs("\\\"", out); break;
        case '\n': fputs("\\n\"\n    \"", out); break;
        case '\t': fputs("\\t", out); break;
        case '?': fputs("\\?", out); break;
        default:
            if (*p < 0x20) {
                fprintf(out, "\\%03o", *p);
            } else {
                fputc(*p, out);
            }
        }
    }
    fputc('"', out);
}

static size_t indent_of(const char *l) {
    size_t i = 0;
    while (l[i] == ' ') {
        i++;
    }
    return i;
}

static int is_fence(const char *l) {
    return strncmp(l + indent_of(l), "```", 3) == 0;
}

/* the head of an entry line, into `head`, or 0 when the line opens none */
static int entry_head(const char *l, char *head, size_t size) {
    const char *t = l + indent_of(l);
    const char *start;
    const char *end;
    if (strncmp(t, "- **`", 5) == 0) {
        /* `- **`present(array)`** -- the list ...` */
        start = t + 4;
    } else if (strncmp(t, "- **", 4) == 0) {
        /* `- **Writing**: `csv.line(values)` answers ...`: past the label */
        const char *close = strstr(t + 4, "**");
        if (!close) {
            return 0;
        }
        start = close + 2;
        while (*start == ':' || *start == ' ') {
            start++;
        }
        if (*start != '`') {
            return 0;
        }
    } else if (strncmp(t, "- `", 3) == 0) {
        start = t + 2;
    } else if (strncmp(t, "**`", 3) == 0) {
        start = t + 2;
    } else if (strncmp(t, "| `", 3) == 0) {
        start = t + 2;
        end = strchr(start, '|');
        if (!end) {
            return 0;
        }
        goto copy;
    } else {
        return 0;
    }
    end = start + strlen(start);
    {
        static const char *const stops[] = { " \xe2\x80\x94 ", " - ", ": ", " \xe2\x86\x92 ", "** ", NULL };
        for (int i = 0; stops[i]; i++) {
            const char *s = strstr(start, stops[i]);
            if (s && s < end) {
                end = s;
            }
        }
    }
copy:;
    size_t n = (size_t)(end - start);
    if (n >= size) {
        n = size - 1;
    }
    memcpy(head, start, n);
    head[n] = '\0';
    return 1;
}

/* the names `head` calls, into names[], their count */
static int head_names(const char *head, char names[][64], int max) {
    int count = 0;
    for (const char *p = head; *p && count < max; p++) {
        if (*p != '`') {
            continue;
        }
        const char *q = p + 1;
        const char *n0 = q;
        while (*q && (islower((unsigned char)*q) || isdigit((unsigned char)*q) || *q == '_' || *q == '.')) {
            q++;
        }
        if (*q == '(' && q > n0 && (size_t)(q - n0) < 64 && islower((unsigned char)*n0)) {
            int dup = 0;
            for (int i = 0; i < count; i++) {
                if (strncmp(names[i], n0, (size_t)(q - n0)) == 0 && names[i][q - n0] == '\0') {
                    dup = 1;
                }
            }
            if (!dup) {
                memcpy(names[count], n0, (size_t)(q - n0));
                names[count][q - n0] = '\0';
                count++;
            }
        }
        /* to the closing backtick of this code span */
        while (*q && *q != '`') {
            q++;
        }
        p = *q ? q : q - 1;
    }
    return count;
}

static void append(char *text, size_t *used, const char *s) {
    size_t n = strlen(s);
    while (n > 0 && s[n - 1] == ' ') {
        n--;   /* a Markdown line break, two spaces, is nothing in a terminal */
    }
    if (*used + n + 2 >= MAX_TEXT) {
        return;
    }
    memcpy(text + *used, s, n);
    *used += n;
    text[(*used)++] = '\n';
    text[*used] = '\0';
}

static void heading_text(const char *l, char *out, size_t size) {
    const char *t = l;
    while (*t == '#') {
        t++;
    }
    while (*t == ' ') {
        t++;
    }
    snprintf(out, size, "%s", t);
}

static void emit_reference(FILE *out) {
    char section[256] = "";
    size_t count = 0;
    int in_fence = 0;
    fputs("static const GbDocEntry gb_doc_entries[] = {\n", out);
    for (size_t i = 0; i < line_count; i++) {
        const char *l = lines[i];
        if (is_fence(l)) {
            in_fence = !in_fence;
            continue;
        }
        if (in_fence) {
            continue;
        }
        if (l[0] == '#') {
            heading_text(l, section, sizeof section);
            continue;
        }
        char head[512];
        if (!entry_head(l, head, sizeof head)) {
            continue;
        }
        char names[16][64];
        int n = head_names(head, names, 16);
        if (n == 0) {
            continue;
        }
        char text[MAX_TEXT];
        size_t used = 0;
        text[0] = '\0';
        size_t depth = indent_of(l);
        int table = strncmp(l + depth, "| ", 2) == 0;
        append(text, &used, l + depth);
        size_t j = i + 1;
        while (!table && j < line_count) {
            const char *m = lines[j];
            char h2[512];
            if (m[indent_of(m)] == '\0' || m[0] == '#' || is_fence(m) ||
                (indent_of(m) <= depth && entry_head(m, h2, sizeof h2)) ||
                (indent_of(m) <= depth && strncmp(m + indent_of(m), "- ", 2) == 0)) {
                break;
            }
            append(text, &used, m + (indent_of(m) > depth ? depth : indent_of(m)));
            j++;
        }
        /* an example right after it: a ```basic fence, past one blank line
           and an HTML marker comment */
        size_t k = j;
        while (k < line_count && k < j + 3 &&
               (lines[k][indent_of(lines[k])] == '\0' || strncmp(lines[k] + indent_of(lines[k]), "<!--", 4) == 0)) {
            k++;
        }
        if (!table && k < line_count && strncmp(lines[k] + indent_of(lines[k]), "```basic", 8) == 0) {
            append(text, &used, "");
            size_t base = indent_of(lines[k]);
            for (size_t e = k + 1; e < line_count && e < k + 1 + MAX_EXAMPLE && !is_fence(lines[e]); e++) {
                const char *m = lines[e];
                append(text, &used, m + (indent_of(m) > base ? base : indent_of(m)));
            }
        }
        while (used > 0 && text[used - 1] == '\n') {
            text[--used] = '\0';
        }
        for (int a = 0; a < n; a++) {
            entry_named(names[a]);
            fputs("    { ", out);
            emit_string(out, names[a]);
            fputs(", ", out);
            emit_string(out, section);
            fputs(",\n    ", out);
            emit_string(out, text);
            fputs(" },\n", out);
            count++;
        }
    }
    /* A BUILTIN WITH NO ENTRY LINE, described only in running prose
       (`**`from_epoch(number)`** converts ...`): the paragraph it is first
       written in bold in. Not where it is merely written in code: that is
       as often a passing mention (`floor` beside exact integers), which
       would read as its documentation and hide that it has none. */
    for (int r = 0; r < registered; r++) {
        if (has_entry[r]) {
            continue;
        }
        char bold[80];
        snprintf(bold, sizeof bold, "**`%.60s(", registry[r]);
        {
            const char *needle = bold;
            char sect[256] = "";
            int fence = 0;
            for (size_t i = 0; i < line_count; i++) {
                if (is_fence(lines[i])) {
                    fence = !fence;
                    continue;
                }
                if (fence) {
                    continue;
                }
                if (lines[i][0] == '#') {
                    heading_text(lines[i], sect, sizeof sect);
                    continue;
                }
                if (!strstr(lines[i], needle)) {
                    continue;
                }
                size_t a = i, b = i;
                while (a > 0 && a > i - 6 && lines[a - 1][indent_of(lines[a - 1])] != '\0' &&
                       lines[a - 1][0] != '#' && !is_fence(lines[a - 1])) {
                    a--;
                }
                while (b + 1 < line_count && b < i + 8 && lines[b + 1][indent_of(lines[b + 1])] != '\0' &&
                       lines[b + 1][0] != '#' && !is_fence(lines[b + 1])) {
                    b++;
                }
                char text[MAX_TEXT];
                size_t used = 0;
                text[0] = '\0';
                for (size_t k = a; k <= b; k++) {
                    append(text, &used, lines[k] + indent_of(lines[k]));
                }
                while (used > 0 && text[used - 1] == '\n') {
                    text[--used] = '\0';
                }
                fputs("    { ", out);
                emit_string(out, registry[r]);
                fputs(", ", out);
                emit_string(out, sect);
                fputs(",\n    ", out);
                emit_string(out, text);
                fputs(" },\n", out);
                count++;
                has_entry[r] = 1;
                break;
            }
        }
    }
    fprintf(out, "};\nstatic const size_t gb_doc_entry_count = %zu;\n\n", count);
}

/* each row of ERRORS.md's three code tables */
static void emit_errors(FILE *out) {
    const char *table = NULL;
    size_t count = 0;
    /* the 1003 row says "see the domain list below": the list, which is the
       paragraph after the line introducing it */
    char domains[2048] = "";
    for (size_t i = 0; i + 1 < line_count; i++) {
        if (strstr(lines[i], "domains seen with `1003`")) {
            for (size_t j = i + 1; j < line_count && j < i + 12; j++) {
                if (lines[j][0] == '\0') {
                    if (domains[0]) {
                        break;
                    }
                    continue;
                }
                if (domains[0]) {
                    strncat(domains, " ", sizeof domains - strlen(domains) - 1);
                }
                strncat(domains, lines[j], sizeof domains - strlen(domains) - 1);
            }
            break;
        }
    }
    fputs("static const GbCodeEntry gb_code_entries[] = {\n", out);
    for (size_t i = 0; i < line_count; i++) {
        const char *l = lines[i];
        if (l[0] == '#') {
            if (strstr(l, "Diagnostic kind")) {
                table = "diagnostic code (--json-diagnostics, the LSP)";
            } else if (strstr(l, "error.code")) {
                table = "runtime error.code";
            } else if (strstr(l, "Warning codes")) {
                table = "warning code";
            } else if (l[1] == ' ' || (l[1] == '#' && l[2] == ' ')) {
                table = NULL;
            }
            continue;
        }
        if (!table || l[0] != '|' || !isdigit((unsigned char)l[2])) {
            continue;
        }
        char row[4096];
        snprintf(row, sizeof row, "%s", l + 1);
        char *cells[8];
        int nc = 0;
        for (char *c = strtok(row, "|"); c && nc < 8; c = strtok(NULL, "|")) {
            while (*c == ' ') {
                c++;
            }
            size_t len = strlen(c);
            while (len > 0 && c[len - 1] == ' ') {
                c[--len] = '\0';
            }
            cells[nc++] = c;
        }
        if (nc < 2) {
            continue;
        }
        char what[4096] = "";
        for (int c = 1; c < nc; c++) {
            if (c > 1) {
                strncat(what, "\n", sizeof what - strlen(what) - 1);
                strncat(what, c == nc - 1 && strstr(table, "warning") ? "source: " :
                              c == nc - 1 && strstr(table, "runtime") ? "error.source: " : "", sizeof what - strlen(what) - 1);
            }
            strncat(what, domains[0] && strcmp(cells[c], "see the domain list below") == 0 ? domains : cells[c],
                    sizeof what - strlen(what) - 1);
        }
        fputs("    { ", out);
        emit_string(out, cells[0]);
        fputs(", ", out);
        emit_string(out, table);
        fputs(",\n    ", out);
        emit_string(out, what);
        fputs(" },\n", out);
        count++;
    }
    fprintf(out, "};\nstatic const size_t gb_code_entry_count = %zu;\n\n", count);
}

/* every name src/builtins.c knows in some build, whichever #if it is under */
static void emit_builtins(FILE *out) {
    size_t count = 0;
    int in_registry = 0;
    fputs("static const char *const gb_doc_registry[] = {\n", out);
    for (size_t i = 0; i < line_count; i++) {
        const char *t = lines[i] + indent_of(lines[i]);
        /* the registry and the names dispatched without it (has_builtin's
           dispatch_only) */
        if (strstr(t, "builtins[] = {") || strstr(t, "dispatch_only[] = {")) {
            in_registry = 1;
            continue;
        }
        if (in_registry && strncmp(t, "};", 2) == 0) {
            in_registry = 0;
            continue;
        }
        if (in_registry && t[0] == '"') {
            const char *e = strchr(t + 1, '"');
            if (e) {
                char name[128];
                size_t n = (size_t)(e - t - 1);
                if (n < sizeof name) {
                    memcpy(name, t + 1, n);
                    name[n] = '\0';
                    fputs("    ", out);
                    emit_string(out, name);
                    fputs(",\n", out);
                    count++;
                    if (registered < 512 && n < sizeof registry[0]) {
                        snprintf(registry[registered++], sizeof registry[0], "%.63s", name);
                    }
                }
            }
        }
    }
    fprintf(out, "};\nstatic const size_t gb_doc_registry_count = %zu;\n", count);
}

/* the rules card (docs/ai/RULES.md), whole, for `gbasic --rules` */
static void emit_rules(FILE *out) {
    fputs("static const char *const gb_doc_rules[] = {\n", out);
    for (size_t i = 0; i < line_count; i++) {
        fputs("    ", out);
        emit_string(out, lines[i]);
        fputs(",\n", out);
    }
    fprintf(out, "    NULL\n};\nstatic const size_t gb_doc_rules_count = %zu;\n", line_count);
}

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr, "usage: docindex_gen REFERENCE ERRORS BUILTINS RULES OUT.c\n");
        return 2;
    }
    FILE *out = fopen(argv[5], "w");
    if (!out) {
        fprintf(stderr, "docindex_gen: cannot write %s\n", argv[5]);
        return 1;
    }
    fprintf(out, "/* GENERATED by tools/docindex_gen.c from %s, %s, %s and %s: do not edit. */\n\n",
            argv[1], argv[2], argv[3], argv[4]);
    load(argv[3]);
    emit_builtins(out);
    load(argv[1]);
    emit_reference(out);
    load(argv[2]);
    emit_errors(out);
    load(argv[4]);
    emit_rules(out);
    fclose(out);
    return 0;
}
