/* tests/run_ui_tui.sh's `syntax` tier: our engine's formats for each line of
 * each file, in tests/ui_tui/ksh_probe.cpp's form, so the two are compared
 * as text. ksyntax_dump PATH LANG THEME FILE... */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ksyntax.h"

static size_t cps(const char *s, size_t n) {
    size_t c = 0;
    for (size_t i = 0; i < n; i++) {
        if (((unsigned char)s[i] & 0xC0) != 0x80) {
            c++;
        }
    }
    return c;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        return 2;
    }
    KshRepo *repo = ksh_repo_new();
    ksh_repo_add_path(repo, argv[1]);
    KshDef *def = ksh_find(repo, argv[2]);
    if (!def) {
        fprintf(stderr, "no definition %s\n", argv[2]);
        return 1;
    }
    const KshTheme *th = ksh_theme(repo, argv[3]);
    if (!th) {
        fprintf(stderr, "no theme %s\n", argv[3]);
        return 1;
    }
    for (int a = 4; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb");
        if (!f) {
            return 1;
        }
        size_t cap = 1 << 16, n = 0, got;
        char *buf = malloc(cap);
        while ((got = fread(buf + n, 1, cap - n, f)) > 0) {
            n += got;
            if (n == cap) {
                cap *= 2;
                buf = realloc(buf, cap);
            }
        }
        fclose(f);
        printf("F %s\n", argv[a]);
        KshState *st = NULL;
        size_t line = 0, at = 0;
        while (at < n) {
            char *nl = memchr(buf + at, '\n', n - at);
            size_t len = nl ? (size_t)(nl - (buf + at)) : n - at;
            KshSpan *sp;
            size_t nsp;
            KshState *next = ksh_highlight(def, st, buf + at, len, &sp, &nsp);
            ksh_state_free(st);
            st = next;
            printf("L%zu", ++line);
            /* runs of the same look, by codepoints */
            char prev[512] = "";
            size_t count = 0;
            /* a gap no span covers is unformatted, as KDE's invalid format */
            size_t pos = 0;
            for (size_t i = 0; i <= nsp; i++) {
                size_t from = i < nsp ? sp[i].start : len;
                if (from > pos) {
                    size_t c = cps(buf + at + pos, from - pos);
                    if (count && prev[0] == '\0') {
                        count += c;
                    } else {
                        if (count) {
                            printf(" %zu|%s", count, prev);
                        }
                        prev[0] = '\0';
                        count = c;
                    }
                }
                if (i == nsp) {
                    break;
                }
                pos = sp[i].start + sp[i].len;
                char cur[512] = "";
                if (sp[i].format >= 0) {
                    KshLook lk;
                    ksh_look(repo, sp[i].format, th, &lk);
                    char bg[8] = "-";
                    if (lk.has_bg) {
                        snprintf(bg, sizeof bg, "%06x", lk.bg);
                    }
                    snprintf(cur, sizeof cur, "%s:%06x:%s:%c%c%c%c", ksh_format_name(repo, sp[i].format), lk.fg, bg,
                             lk.bold ? 'b' : '.', lk.italic ? 'i' : '.', lk.underline ? 'u' : '.',
                             lk.strike ? 's' : '.');
                }
                size_t c = cps(buf + at + sp[i].start, sp[i].len);
                if (count && strcmp(cur, prev) == 0) {
                    count += c;
                } else {
                    if (count) {
                        printf(" %zu|%s", count, prev);
                    }
                    strcpy(prev, cur);
                    count = c;
                }
            }
            if (count) {
                printf(" %zu|%s", count, prev);
            }
            printf("\n");
            free(sp);
            at += len + (nl ? 1 : 0);
        }
        ksh_state_free(st);
        free(buf);
    }
    ksh_repo_free(repo);
    return 0;
}
