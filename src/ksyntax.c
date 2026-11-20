/* ksyntax: KDE's syntax definitions read and applied in plain C
 * (include/ksyntax.h; docs/tui_design.md §3.9).
 *
 * THE RULES ARE KDE'S, NOT OURS. Every choice below that a reader might think
 * arbitrary -- a rule with no attribute taking the attribute of the context it
 * switches to, a keyword rule that never looks behind itself, a regular
 * expression matched once for the whole line and its first match remembered,
 * `#pop` that cannot pop the first context -- is what KSyntaxHighlighting
 * does, and tests/run_ui_tui.sh's `syntax` tier holds this file to that
 * library line by line over every definition it ships. Where this file
 * departs, the departure is listed in §3.9, measured.
 *
 * XML IS READ BY libxml2, so this file is built only with it (HAVE_LIBXML2);
 * a build without it has no `highlighting` group on the terminal. Regular
 * expressions are PCRE2's, which is also what QRegularExpression runs.
 */
#include "ksyntax.h"

#if HAVE_LIBXML2

#define PCRE2_CODE_UNIT_WIDTH 8
#include <dirent.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlreader.h>
#include <pcre2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "ksyntax_themes.h"

/* ---- small things -------------------------------------------------------- */

static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) {
        abort();
    }
    return p;
}

static void *xrealloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p) {
        abort();
    }
    return p;
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s);
    char *d = xmalloc(n + 1);
    memcpy(d, s, n + 1);
    return d;
}

#define GROW(arr, n, cap)                                          \
    do {                                                           \
        if ((n) >= (cap)) {                                        \
            (cap) = (cap) ? (cap) * 2 : 8;                         \
            (arr) = xrealloc((arr), (cap) * sizeof(*(arr)));       \
        }                                                          \
    } while (0)

/* one codepoint from UTF-8; a byte that does not start one is itself, as
 * U+FFFD, so a bad byte costs one position and nothing is skipped */
static uint32_t utf8_next(const unsigned char *s, size_t len, size_t *i) {
    unsigned c = s[*i];
    if (c < 0x80) {
        (*i)++;
        return c;
    }
    int need = c >= 0xF0 && c < 0xF5 ? 3 : c >= 0xE0 && c < 0xF0 ? 2 : c >= 0xC2 && c < 0xE0 ? 1 : -1;
    if (need < 0 || *i + (size_t)need >= len) {
        (*i)++;
        return 0xFFFD;
    }
    uint32_t cp = c & (need == 3 ? 0x07 : need == 2 ? 0x0F : 0x1F);
    for (int k = 1; k <= need; k++) {
        unsigned b = s[*i + (size_t)k];
        if ((b & 0xC0) != 0x80) {
            (*i)++;
            return 0xFFFD;
        }
        cp = (cp << 6) | (b & 0x3F);
    }
    *i += (size_t)need + 1;
    return cp;
}

/* QChar::isSpace */
static int is_space(uint32_t c) {
    return c == ' ' || (c >= 9 && c <= 13) || c == 0x85 || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F ||
           c == 0x205F || c == 0x3000;
}

/* QChar::isLetter and isLetterOrNumber above ASCII, from PCRE2's Unicode
 * tables (Qt's come from the same standard): first asked, then remembered.
 * A rough table of blocks was measured wrong on U+212F, a letter among the
 * letterlike symbols (Julia's corpus). */
static int unicode_class(uint32_t c, int number) {
    static pcre2_code *letter, *alnum;
    static pcre2_match_data *md;
    enum { MEMO = 256 };
    static uint32_t memo_cp[MEMO];
    static signed char memo[MEMO][2];
    static int ready;
    if (!ready) {
        int err;
        PCRE2_SIZE eo;
        letter = pcre2_compile((PCRE2_SPTR) "^\\p{L}", PCRE2_ZERO_TERMINATED, PCRE2_UTF, &err, &eo, NULL);
        alnum = pcre2_compile((PCRE2_SPTR) "^[\\p{L}\\p{N}]", PCRE2_ZERO_TERMINATED, PCRE2_UTF, &err, &eo, NULL);
        md = pcre2_match_data_create(1, NULL);
        memset(memo, -1, sizeof memo);
        ready = 1;
    }
    unsigned slot = c % MEMO;
    if (memo_cp[slot] != c || memo[slot][number] < 0) {
        if (memo_cp[slot] != c) {
            memo_cp[slot] = c;
            memo[slot][0] = memo[slot][1] = -1;
        }
        unsigned char u[4];
        size_t n;
        if (c < 0x800) {
            u[0] = (unsigned char)(0xC0 | (c >> 6));
            u[1] = (unsigned char)(0x80 | (c & 0x3F));
            n = 2;
        } else if (c < 0x10000) {
            u[0] = (unsigned char)(0xE0 | (c >> 12));
            u[1] = (unsigned char)(0x80 | ((c >> 6) & 0x3F));
            u[2] = (unsigned char)(0x80 | (c & 0x3F));
            n = 3;
        } else {
            u[0] = (unsigned char)(0xF0 | (c >> 18));
            u[1] = (unsigned char)(0x80 | ((c >> 12) & 0x3F));
            u[2] = (unsigned char)(0x80 | ((c >> 6) & 0x3F));
            u[3] = (unsigned char)(0x80 | (c & 0x3F));
            n = 4;
        }
        pcre2_code *re = number ? alnum : letter;
        memo[slot][number] = (signed char)(re && pcre2_match(re, u, n, 0, 0, md, NULL) >= 0);
    }
    return memo[slot][number];
}

static int is_letter(uint32_t c) {
    if (c < 0x80) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }
    return unicode_class(c, 0);
}

static int is_letter_or_number(uint32_t c) {
    if (c < 0x80) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    }
    return unicode_class(c, 1);
}

static int is_digit(uint32_t c) {
    return c >= '0' && c <= '9';
}

static int is_hex(uint32_t c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int is_octal(uint32_t c) {
    return c >= '0' && c <= '7';
}

/* case folding for a case-insensitive comparison: ASCII, Latin-1, Greek and
 * Cyrillic's basic letters (§3.9) */
static uint32_t fold(uint32_t c) {
    if (c >= 'A' && c <= 'Z') {
        return c + 32;
    }
    if (c < 0xC0) {
        return c;
    }
    if (c <= 0xDE && c != 0xD7) {
        return c + 32;
    }
    if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) {
        return c + 32;
    }
    if (c >= 0x410 && c <= 0x42F) {
        return c + 32;
    }
    if (c >= 0x400 && c <= 0x40F) {
        return c + 80;
    }
    return c;
}

/* ---- the repository ------------------------------------------------------ */

enum {
    R_ANYCHAR, R_DETECTCHAR, R_DETECT2CHARS, R_DETECTIDENTIFIER, R_DETECTSPACES,
    R_FLOAT, R_HLCCHAR, R_HLCHEX, R_HLCOCT, R_HLCSTRINGCHAR, R_INCLUDERULES,
    R_INT, R_KEYWORD, R_LINECONTINUE, R_RANGEDETECT, R_REGEXPR, R_STRINGDETECT,
    R_WORDDETECT
};

static const char *const rule_names[] = {
    "AnyChar", "DetectChar", "Detect2Chars", "DetectIdentifier", "DetectSpaces",
    "Float", "HlCChar", "HlCHex", "HlCOct", "HlCStringChar", "IncludeRules",
    "Int", "keyword", "LineContinue", "RangeDetect", "RegExpr", "StringDetect",
    "WordDetect"
};

typedef struct KshCtx KshCtx;

/* the characters that end a word: KDE's default, then what the definition's
 * <keywords> and a rule's own attributes add and take away */
typedef struct {
    unsigned char ascii[128];
    uint32_t *hi;
    size_t nhi;
} Delims;

/* a context switch: pops, then pushes; `A!B!C` pushes A, B, then C, so the
 * last named is on top (Mermaid's; measured against KDE, line 68 of its
 * corpus: `title` then a space in C's format) */
typedef struct {
    int pop;
    KshCtx *ctx;             /* the one on top after it, or NULL */
    KshCtx **more;           /* those beneath it, the bottom first */
    size_t nmore;
} Switch;

typedef struct KshList {
    char *name;
    char **items;      /* each as written */
    size_t nitems, cap;
    char **includes;   /* lists named in <include>, maybe `name##Def` */
    size_t nincludes, capinc;
    /* after resolution: every word, and every word folded, sorted */
    char **words, **folded;
    size_t nwords;
    int resolving, resolved;
} KshList;

typedef struct Rule {
    int kind;
    KshDef *def;
    int format;              /* -1: none, so the context's */
    char *ctx_text;
    Switch sw;
    int lookahead, first_non_space, column, insensitive, dynamic, minimal;
    uint32_t c1, c2;
    int capture;             /* a dynamic DetectChar's %N */
    uint32_t *cps;           /* AnyChar's set, StringDetect's and WordDetect's text */
    size_t ncps;
    char *text;              /* StringDetect (dynamic), RegExpr's pattern, keyword's list */
    pcre2_code *re;
    int re_failed;
    KshList *list;
    int list_insensitive;    /* -1: the definition's */
    int include_attrib;
    Delims *delims;          /* its own, when it says so; else the definition's */
} Rule;

struct KshCtx {
    char *name;
    KshDef *def;
    int format;
    char *end_text, *empty_text, *fall_text;
    Switch line_end, line_empty, fall;
    int fallthrough, dynamic, stop_empty_loop;
    Rule **rules;
    size_t nrules, cap;
    int resolve;             /* 0 not yet, 1 under way, 2 done */
};

typedef struct {
    char *name;
    int style;               /* the default style's index */
    int has_fg, has_bg, has_bold, has_italic, has_underline, has_strike;
    uint32_t fg, bg;
    int bold, italic, underline, strike;
    KshDef *def;
} Format;

struct KshDef {
    KshRepo *repo;
    char *name, *file;
    char **alt;
    size_t nalt;
    int version;
    int loaded, failed;
    KshCtx **ctxs;
    size_t nctx, capctx;
    KshList **lists;
    size_t nlists, caplists;
    int *formats;            /* this definition's formats, global indices */
    size_t nformats, capformats;
    int case_sensitive;
    Delims delims;
    Rule **all_rules;        /* every rule this definition owns, to free */
    size_t nall, capall;
};

typedef struct {
    char *name;
    int has_fg, has_bg, has_bold, has_italic, has_underline, has_strike;
    uint32_t fg, bg;
    int bold, italic, underline, strike;
} Style;

typedef struct {
    char *def, *item;
    Style s;
} Custom;

struct KshTheme {
    char *name, *file;
    int revision;
    Style styles[31];
    char **ed_names;
    uint32_t *ed_rgb;
    size_t ned;
    Custom *custom;
    size_t ncustom;
};

struct KshRepo {
    char **paths;
    size_t npaths, cappaths;
    KshDef **defs;
    size_t ndefs, capdefs;
    int scanned;
    Format *formats;
    size_t nformats, capformats;
    KshTheme **themes;
    size_t nthemes, capthemes;
    int themes_loaded;
};

typedef struct {
    KshCtx *ctx;
    char **caps;
    size_t ncaps;
} Frame;

struct KshState {
    KshDef *def;
    Frame *frames;
    size_t n, cap;
};

static const char *const style_names[31] = {
    "Normal", "Keyword", "Function", "Variable", "ControlFlow", "Operator",
    "BuiltIn", "Extension", "Preprocessor", "Attribute", "Char", "SpecialChar",
    "String", "VerbatimString", "SpecialString", "Import", "DataType", "DecVal",
    "BaseN", "Float", "Constant", "Comment", "Documentation", "Annotation",
    "CommentVar", "RegionMarker", "Information", "Warning", "Alert", "Others",
    "Error"
};

static int style_of(const char *ds) {
    if (!ds || strncmp(ds, "ds", 2) != 0) {
        return 0;
    }
    for (int i = 0; i < 31; i++) {
        if (strcasecmp(ds + 2, style_names[i]) == 0) {
            return i;
        }
    }
    return 0;
}

KshRepo *ksh_repo_new(void) {
    KshRepo *r = xmalloc(sizeof *r);
    memset(r, 0, sizeof *r);
    return r;
}

void ksh_repo_add_path(KshRepo *repo, const char *dir) {
    if (!dir || !*dir) {
        return;
    }
    for (size_t i = 0; i < repo->npaths; i++) {
        if (strcmp(repo->paths[i], dir) == 0) {
            return;
        }
    }
    GROW(repo->paths, repo->npaths, repo->cappaths);
    repo->paths[repo->npaths++] = xstrdup(dir);
    /* a path added after the first lookup is read at the next one */
    repo->scanned = 0;
    repo->themes_loaded = 0;
}

/* KDE's standard locations, then the custom paths: QStandardPaths's
 * GenericDataLocation is $XDG_DATA_HOME, then each of $XDG_DATA_DIRS */
static char **search_dirs(KshRepo *repo, const char *leaf, size_t *n) {
    char **out = NULL;
    size_t cnt = 0, cap = 0;
    char buf[4096];
    const char *home = getenv("XDG_DATA_HOME");
    if (home && *home) {
        snprintf(buf, sizeof buf, "%s/org.kde.syntax-highlighting/%s", home, leaf);
    } else {
        const char *h = getenv("HOME");
        snprintf(buf, sizeof buf, "%s/.local/share/org.kde.syntax-highlighting/%s", h ? h : "", leaf);
    }
    GROW(out, cnt, cap);
    out[cnt++] = xstrdup(buf);
    const char *dirs = getenv("XDG_DATA_DIRS");
    if (!dirs || !*dirs) {
        dirs = "/usr/local/share:/usr/share";
    }
    const char *p = dirs;
    while (*p) {
        const char *e = strchr(p, ':');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        if (l) {
            snprintf(buf, sizeof buf, "%.*s/org.kde.syntax-highlighting/%s", (int)l, p, leaf);
            GROW(out, cnt, cap);
            out[cnt++] = xstrdup(buf);
        }
        p += l;
        if (*p == ':') {
            p++;
        }
    }
    for (size_t i = 0; i < repo->npaths; i++) {
        snprintf(buf, sizeof buf, "%s/%s", repo->paths[i], leaf);
        GROW(out, cnt, cap);
        out[cnt++] = xstrdup(buf);
    }
    *n = cnt;
    return out;
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* the files in a directory with a suffix, sorted so the order is the
 * same everywhere (KDE's is the directory's own order) */
static char **files_in(const char *dir, const char *suffix, size_t *n) {
    char **out = NULL;
    size_t cnt = 0, cap = 0;
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        size_t sl = strlen(suffix);
        while ((e = readdir(d))) {
            size_t l = strlen(e->d_name);
            if (l > sl && strcmp(e->d_name + l - sl, suffix) == 0) {
                char *full = xmalloc(strlen(dir) + l + 2);
                sprintf(full, "%s/%s", dir, e->d_name);
                GROW(out, cnt, cap);
                out[cnt++] = full;
            }
        }
        closedir(d);
    }
    if (cnt) {
        qsort(out, cnt, sizeof *out, cmp_str);
    }
    *n = cnt;
    return out;
}

static void def_free(KshDef *d);

/* the first element's attributes, without reading the rest */
static KshDef *def_header(KshRepo *repo, const char *file) {
    xmlTextReaderPtr rd = xmlReaderForFile(file, NULL, XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!rd) {
        return NULL;
    }
    KshDef *d = NULL;
    while (xmlTextReaderRead(rd) == 1) {
        if (xmlTextReaderNodeType(rd) != XML_READER_TYPE_ELEMENT) {
            continue;
        }
        if (strcmp((const char *)xmlTextReaderConstName(rd), "language") != 0) {
            break;
        }
        xmlChar *name = xmlTextReaderGetAttribute(rd, (const xmlChar *)"name");
        if (!name || !*name) {
            xmlFree(name);
            break;
        }
        d = xmalloc(sizeof *d);
        memset(d, 0, sizeof *d);
        d->repo = repo;
        d->name = xstrdup((const char *)name);
        d->file = xstrdup(file);
        d->case_sensitive = 1;
        xmlFree(name);
        xmlChar *v = xmlTextReaderGetAttribute(rd, (const xmlChar *)"version");
        d->version = v ? atoi((const char *)v) : 0;
        xmlFree(v);
        xmlChar *alt = xmlTextReaderGetAttribute(rd, (const xmlChar *)"alternativeNames");
        if (alt) {
            char *s = xstrdup((const char *)alt);
            size_t cap = 0;
            for (char *t = s; t;) {
                char *semi = strchr(t, ';');
                if (semi) {
                    *semi = '\0';
                }
                while (*t == ' ') {
                    t++;
                }
                if (*t) {
                    GROW(d->alt, d->nalt, cap);
                    d->alt[d->nalt++] = xstrdup(t);
                }
                t = semi ? semi + 1 : NULL;
            }
            free(s);
            xmlFree(alt);
        }
        break;
    }
    xmlFreeTextReader(rd);
    return d;
}

/* every definition's header, KDE's precedence: a name seen first stays
 * unless a later file carries a higher version */
static void scan(KshRepo *repo) {
    if (repo->scanned) {
        return;
    }
    repo->scanned = 1;
    size_t nd;
    char **dirs = search_dirs(repo, "syntax", &nd);
    for (size_t i = 0; i < nd; i++) {
        size_t nf;
        char **files = files_in(dirs[i], ".xml", &nf);
        for (size_t k = 0; k < nf; k++) {
            int seen = 0;
            for (size_t j = 0; j < repo->ndefs; j++) {
                if (strcmp(repo->defs[j]->file, files[k]) == 0) {
                    seen = 1;
                }
            }
            KshDef *d = seen ? NULL : def_header(repo, files[k]);
            if (d) {
                size_t j;
                for (j = 0; j < repo->ndefs; j++) {
                    if (strcmp(repo->defs[j]->name, d->name) == 0) {
                        break;
                    }
                }
                if (j < repo->ndefs) {
                    if (repo->defs[j]->version >= d->version || repo->defs[j]->loaded) {
                        def_free(d);
                    } else {
                        def_free(repo->defs[j]);
                        repo->defs[j] = d;
                    }
                } else {
                    GROW(repo->defs, repo->ndefs, repo->capdefs);
                    repo->defs[repo->ndefs++] = d;
                }
            }
            free(files[k]);
        }
        free(files);
        free(dirs[i]);
    }
    free(dirs);
}

static int load(KshDef *d);

KshDef *ksh_find(KshRepo *repo, const char *name) {
    if (!name) {
        return NULL;
    }
    scan(repo);
    KshDef *hit = NULL;
    for (size_t j = 0; j < repo->ndefs && !hit; j++) {
        if (strcasecmp(repo->defs[j]->name, name) == 0) {
            hit = repo->defs[j];
        }
    }
    for (size_t j = 0; j < repo->ndefs && !hit; j++) {
        for (size_t a = 0; a < repo->defs[j]->nalt; a++) {
            if (strcasecmp(repo->defs[j]->alt[a], name) == 0) {
                hit = repo->defs[j];
            }
        }
    }
    if (!hit || !load(hit)) {
        return NULL;
    }
    return hit;
}

const char *ksh_def_name(const KshDef *def) {
    return def->name;
}

const char *ksh_def_file(const KshDef *def) {
    return def->file;
}

/* ---- loading a definition ------------------------------------------------- */

static const char *attr(xmlNodePtr n, const char *name, char *buf, size_t size) {
    xmlChar *v = xmlGetProp(n, (const xmlChar *)name);
    if (!v) {
        return NULL;
    }
    snprintf(buf, size, "%s", (const char *)v);
    xmlFree(v);
    return buf;
}

static char *attr_dup(xmlNodePtr n, const char *name) {
    xmlChar *v = xmlGetProp(n, (const xmlChar *)name);
    if (!v) {
        return NULL;
    }
    char *s = xstrdup((const char *)v);
    xmlFree(v);
    return s;
}

/* KDE's Xml::attrToBool: "1" or "true", any case */
static int attr_bool(xmlNodePtr n, const char *name, int dflt) {
    char buf[32];
    const char *v = attr(n, name, buf, sizeof buf);
    if (!v) {
        return dflt;
    }
    return strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0;
}

static int has_attr(xmlNodePtr n, const char *name) {
    return xmlHasProp(n, (const xmlChar *)name) != NULL;
}

static uint32_t *to_cps(const char *s, size_t *n) {
    size_t len = strlen(s), i = 0, cnt = 0;
    uint32_t *out = xmalloc((len + 1) * sizeof *out);
    while (i < len) {
        out[cnt++] = utf8_next((const unsigned char *)s, len, &i);
    }
    *n = cnt;
    return out;
}

static uint32_t first_cp(const char *s) {
    if (!s || !*s) {
        return 0;
    }
    size_t i = 0;
    return utf8_next((const unsigned char *)s, strlen(s), &i);
}

/* the colour names QColor knows (SVG's), which a few definitions use */
static const struct {
    const char *name;
    uint32_t rgb;
} colour_names[] = {
    {"aliceblue", 0xf0f8ff}, {"antiquewhite", 0xfaebd7}, {"aqua", 0x00ffff}, {"aquamarine", 0x7fffd4},
    {"azure", 0xf0ffff}, {"beige", 0xf5f5dc}, {"bisque", 0xffe4c4}, {"black", 0x000000},
    {"blanchedalmond", 0xffebcd}, {"blue", 0x0000ff}, {"blueviolet", 0x8a2be2}, {"brown", 0xa52a2a},
    {"burlywood", 0xdeb887}, {"cadetblue", 0x5f9ea0}, {"chartreuse", 0x7fff00}, {"chocolate", 0xd2691e},
    {"coral", 0xff7f50}, {"cornflowerblue", 0x6495ed}, {"cornsilk", 0xfff8dc}, {"crimson", 0xdc143c},
    {"cyan", 0x00ffff}, {"darkblue", 0x00008b}, {"darkcyan", 0x008b8b}, {"darkgoldenrod", 0xb8860b},
    {"darkgray", 0xa9a9a9}, {"darkgreen", 0x006400}, {"darkgrey", 0xa9a9a9}, {"darkkhaki", 0xbdb76b},
    {"darkmagenta", 0x8b008b}, {"darkolivegreen", 0x556b2f}, {"darkorange", 0xff8c00},
    {"darkorchid", 0x9932cc}, {"darkred", 0x8b0000}, {"darksalmon", 0xe9967a}, {"darkseagreen", 0x8fbc8f},
    {"darkslateblue", 0x483d8b}, {"darkslategray", 0x2f4f4f}, {"darkslategrey", 0x2f4f4f},
    {"darkturquoise", 0x00ced1}, {"darkviolet", 0x9400d3}, {"deeppink", 0xff1493},
    {"deepskyblue", 0x00bfff}, {"dimgray", 0x696969}, {"dimgrey", 0x696969}, {"dodgerblue", 0x1e90ff},
    {"firebrick", 0xb22222}, {"floralwhite", 0xfffaf0}, {"forestgreen", 0x228b22}, {"fuchsia", 0xff00ff},
    {"gainsboro", 0xdcdcdc}, {"ghostwhite", 0xf8f8ff}, {"gold", 0xffd700}, {"goldenrod", 0xdaa520},
    {"gray", 0x808080}, {"grey", 0x808080}, {"green", 0x008000}, {"greenyellow", 0xadff2f},
    {"honeydew", 0xf0fff0}, {"hotpink", 0xff69b4}, {"indianred", 0xcd5c5c}, {"indigo", 0x4b0082},
    {"ivory", 0xfffff0}, {"khaki", 0xf0e68c}, {"lavender", 0xe6e6fa}, {"lavenderblush", 0xfff0f5},
    {"lawngreen", 0x7cfc00}, {"lemonchiffon", 0xfffacd}, {"lightblue", 0xadd8e6}, {"lightcoral", 0xf08080},
    {"lightcyan", 0xe0ffff}, {"lightgoldenrodyellow", 0xfafad2}, {"lightgray", 0xd3d3d3},
    {"lightgreen", 0x90ee90}, {"lightgrey", 0xd3d3d3}, {"lightpink", 0xffb6c1}, {"lightsalmon", 0xffa07a},
    {"lightseagreen", 0x20b2aa}, {"lightskyblue", 0x87cefa}, {"lightslategray", 0x778899},
    {"lightslategrey", 0x778899}, {"lightsteelblue", 0xb0c4de}, {"lightyellow", 0xffffe0},
    {"lime", 0x00ff00}, {"limegreen", 0x32cd32}, {"linen", 0xfaf0e6}, {"magenta", 0xff00ff},
    {"maroon", 0x800000}, {"mediumaquamarine", 0x66cdaa}, {"mediumblue", 0x0000cd},
    {"mediumorchid", 0xba55d3}, {"mediumpurple", 0x9370db}, {"mediumseagreen", 0x3cb371},
    {"mediumslateblue", 0x7b68ee}, {"mediumspringgreen", 0x00fa9a}, {"mediumturquoise", 0x48d1cc},
    {"mediumvioletred", 0xc71585}, {"midnightblue", 0x191970}, {"mintcream", 0xf5fffa},
    {"mistyrose", 0xffe4e1}, {"moccasin", 0xffe4b5}, {"navajowhite", 0xffdead}, {"navy", 0x000080},
    {"oldlace", 0xfdf5e6}, {"olive", 0x808000}, {"olivedrab", 0x6b8e23}, {"orange", 0xffa500},
    {"orangered", 0xff4500}, {"orchid", 0xda70d6}, {"palegoldenrod", 0xeee8aa}, {"palegreen", 0x98fb98},
    {"paleturquoise", 0xafeeee}, {"palevioletred", 0xdb7093}, {"papayawhip", 0xffefd5},
    {"peachpuff", 0xffdab9}, {"peru", 0xcd853f}, {"pink", 0xffc0cb}, {"plum", 0xdda0dd},
    {"powderblue", 0xb0e0e6}, {"purple", 0x800080}, {"red", 0xff0000}, {"rosybrown", 0xbc8f8f},
    {"royalblue", 0x4169e1}, {"saddlebrown", 0x8b4513}, {"salmon", 0xfa8072}, {"sandybrown", 0xf4a460},
    {"seagreen", 0x2e8b57}, {"seashell", 0xfff5ee}, {"sienna", 0xa0522d}, {"silver", 0xc0c0c0},
    {"skyblue", 0x87ceeb}, {"slateblue", 0x6a5acd}, {"slategray", 0x708090}, {"slategrey", 0x708090},
    {"snow", 0xfffafa}, {"springgreen", 0x00ff7f}, {"steelblue", 0x4682b4}, {"tan", 0xd2b48c},
    {"teal", 0x008080}, {"thistle", 0xd8bfd8}, {"tomato", 0xff6347}, {"turquoise", 0x40e0d0},
    {"violet", 0xee82ee}, {"wheat", 0xf5deb3}, {"white", 0xffffff}, {"whitesmoke", 0xf5f5f5},
    {"yellow", 0xffff00}, {"yellowgreen", 0x9acd32},
};

/* a colour as the XML writes it: #rgb, #rrggbb, #aarrggbb or a name */
static int parse_colour(const char *s, uint32_t *rgb) {
    if (!s) {
        return 0;
    }
    if (*s != '#') {
        for (size_t i = 0; i < sizeof colour_names / sizeof *colour_names; i++) {
            if (strcasecmp(s, colour_names[i].name) == 0) {
                *rgb = colour_names[i].rgb;
                return 1;
            }
        }
        return 0;
    }
    size_t l = strlen(s + 1);
    char *end;
    unsigned long v = strtoul(s + 1, &end, 16);
    if (*end) {
        return 0;
    }
    if (l == 3) {
        unsigned r = (v >> 8) & 15, g = (v >> 4) & 15, b = v & 15;
        *rgb = (r * 17) << 16 | (g * 17) << 8 | (b * 17);
        return 1;
    }
    if (l == 6) {
        *rgb = (uint32_t)v;
        return 1;
    }
    if (l == 8) {   /* #aarrggbb */
        *rgb = (uint32_t)(v & 0xFFFFFF);
        return 1;
    }
    return 0;
}

static int format_named(KshDef *d, const char *name) {
    if (!name) {
        return -1;
    }
    for (size_t i = 0; i < d->nformats; i++) {
        if (strcmp(d->repo->formats[d->formats[i]].name, name) == 0) {
            return d->formats[i];
        }
    }
    return -1;
}

static KshCtx *ctx_named(KshDef *d, const char *name) {
    for (size_t i = 0; i < d->nctx; i++) {
        if (strcmp(d->ctxs[i]->name, name) == 0) {
            return d->ctxs[i];
        }
    }
    return NULL;
}

static KshList *list_named(KshDef *d, const char *name) {
    for (size_t i = 0; i < d->nlists; i++) {
        if (strcmp(d->lists[i]->name, name) == 0) {
            return d->lists[i];
        }
    }
    return NULL;
}

static void delims_apply(Delims *d, xmlNodePtr n);

static Rule *rule_load(KshDef *d, xmlNodePtr n) {
    const char *el = (const char *)n->name;
    int kind = -1;
    for (int i = 0; i < (int)(sizeof rule_names / sizeof *rule_names); i++) {
        if (strcmp(el, rule_names[i]) == 0) {
            kind = i;
        }
    }
    if (kind < 0) {
        return NULL;
    }
    Rule *r = xmalloc(sizeof *r);
    memset(r, 0, sizeof *r);
    r->kind = kind;
    r->def = d;
    r->column = -1;
    char buf[1024];
    r->format = format_named(d, attr(n, "attribute", buf, sizeof buf));
    r->ctx_text = attr_dup(n, "context");
    r->lookahead = attr_bool(n, "lookAhead", 0);
    r->first_non_space = attr_bool(n, "firstNonSpace", 0);
    const char *col = attr(n, "column", buf, sizeof buf);
    if (col) {
        char *end;
        long c = strtol(col, &end, 10);
        if (!*end) {
            r->column = (int)c;
        }
    }
    r->insensitive = attr_bool(n, "insensitive", 0);
    if (has_attr(n, "additionalDeliminator") || has_attr(n, "weakDeliminator")) {
        r->delims = xmalloc(sizeof *r->delims);
        *r->delims = d->delims;
        r->delims->hi = NULL;
        r->delims->nhi = 0;
        for (size_t i = 0; i < d->delims.nhi; i++) {
            r->delims->hi = xrealloc(r->delims->hi, (r->delims->nhi + 1) * sizeof *r->delims->hi);
            r->delims->hi[r->delims->nhi++] = d->delims.hi[i];
        }
        delims_apply(r->delims, n);
    }
    r->dynamic = attr_bool(n, "dynamic", 0);
    r->minimal = attr_bool(n, "minimal", 0);
    switch (kind) {
    case R_ANYCHAR: {
        char *s = attr_dup(n, "String");
        r->cps = to_cps(s ? s : "", &r->ncps);
        free(s);
        break;
    }
    case R_DETECTCHAR: {
        char *s = attr_dup(n, "char");
        if (r->dynamic && s && is_digit((unsigned char)s[0])) {
            r->capture = atoi(s);
        } else {
            r->c1 = first_cp(s);
            r->dynamic = 0;
        }
        free(s);
        break;
    }
    case R_DETECT2CHARS:
    case R_RANGEDETECT: {
        char *a = attr_dup(n, "char"), *b = attr_dup(n, "char1");
        r->c1 = first_cp(a);
        r->c2 = first_cp(b);
        free(a);
        free(b);
        break;
    }
    case R_LINECONTINUE: {
        char *a = attr_dup(n, "char");
        r->c1 = a && *a ? first_cp(a) : '\\';
        free(a);
        break;
    }
    case R_STRINGDETECT:
    case R_WORDDETECT: {
        char *s = attr_dup(n, "String");
        r->text = s ? s : xstrdup("");
        r->cps = to_cps(r->text, &r->ncps);
        if (kind == R_WORDDETECT) {
            r->dynamic = 0;
        }
        break;
    }
    case R_REGEXPR: {
        char *s = attr_dup(n, "String");
        r->text = s ? s : xstrdup("");
        break;
    }
    case R_KEYWORD: {
        char *s = attr_dup(n, "String");
        r->text = s ? s : xstrdup("");
        r->list_insensitive = has_attr(n, "insensitive") ? r->insensitive : -1;
        r->dynamic = 0;
        break;
    }
    case R_INCLUDERULES:
        r->text = attr_dup(n, "context");
        r->include_attrib = attr_bool(n, "includeAttrib", 0);
        break;
    default:
        break;
    }
    if (r->kind != R_DETECTCHAR && r->kind != R_STRINGDETECT && r->kind != R_REGEXPR) {
        r->dynamic = 0;
    }
    GROW(d->all_rules, d->nall, d->capall);
    d->all_rules[d->nall++] = r;
    /* child rules: KDE treats a rule nested in a rule as the next rule of the
     * context, with the parent's position (the old "child rules" form) */
    return r;
}

static void ctx_add_rules(KshDef *d, KshCtx *c, xmlNodePtr n) {
    for (xmlNodePtr k = n->children; k; k = k->next) {
        if (k->type != XML_ELEMENT_NODE) {
            continue;
        }
        Rule *r = rule_load(d, k);
        if (r) {
            GROW(c->rules, c->nrules, c->cap);
            c->rules[c->nrules++] = r;
        }
    }
}

static void delims_default(Delims *d) {
    memset(d, 0, sizeof *d);
    const char *def = "\t !%&()*+,-./:;<=>?[\\]^{|}~";
    for (const char *p = def; *p; p++) {
        d->ascii[(unsigned char)*p] = 1;
    }
}

static void delims_change(Delims *d, const char *s, int on) {
    size_t n;
    uint32_t *cps = to_cps(s, &n);
    for (size_t i = 0; i < n; i++) {
        uint32_t c = cps[i];
        if (c < 128) {
            d->ascii[c] = (unsigned char)on;
            continue;
        }
        size_t j;
        for (j = 0; j < d->nhi && d->hi[j] != c; j++) {
        }
        if (on && j == d->nhi) {
            d->hi = xrealloc(d->hi, (d->nhi + 1) * sizeof *d->hi);
            d->hi[d->nhi++] = c;
        } else if (!on && j < d->nhi) {
            d->hi[j] = d->hi[--d->nhi];
        }
    }
    free(cps);
}

/* KDE's order: what is added, then what is taken away */
static void delims_apply(Delims *d, xmlNodePtr n) {
    char *add = attr_dup(n, "additionalDeliminator");
    char *weak = attr_dup(n, "weakDeliminator");
    if (add) {
        delims_change(d, add, 1);
    }
    if (weak) {
        delims_change(d, weak, 0);
    }
    free(add);
    free(weak);
}

static int is_delim(const Rule *r, uint32_t c) {
    const Delims *d = r->delims ? r->delims : &r->def->delims;
    if (c < 128) {
        return d->ascii[c];
    }
    for (size_t j = 0; j < d->nhi; j++) {
        if (d->hi[j] == c) {
            return 1;
        }
    }
    return 0;
}

/* the text of an element, white space at either end removed, as KDE's
 * keyword items are */
static char *element_text(xmlNodePtr n) {
    xmlChar *t = xmlNodeGetContent(n);
    const char *s = t ? (const char *)t : "";
    while (*s && is_space((unsigned char)*s)) {
        s++;
    }
    size_t l = strlen(s);
    while (l && is_space((unsigned char)s[l - 1])) {
        l--;
    }
    char *out = xmalloc(l + 1);
    memcpy(out, s, l);
    out[l] = '\0';
    xmlFree(t);
    return out;
}

static void switch_resolve(KshDef *d, const char *text, Switch *out);
static void ctx_resolve(KshCtx *c);

static int load(KshDef *d) {
    if (d->loaded) {
        return 1;
    }
    if (d->failed) {
        return 0;
    }
    xmlDocPtr doc = xmlReadFile(d->file, NULL,
                                XML_PARSE_NOENT | XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING |
                                    XML_PARSE_NOCDATA);
    xmlNodePtr root = doc ? xmlDocGetRootElement(doc) : NULL;
    if (!root || strcmp((const char *)root->name, "language") != 0) {
        if (doc) {
            xmlFreeDoc(doc);
        }
        d->failed = 1;
        return 0;
    }
    delims_default(&d->delims);
    KshRepo *repo = d->repo;
    xmlNodePtr hl = NULL, general = NULL;
    for (xmlNodePtr k = root->children; k; k = k->next) {
        if (k->type == XML_ELEMENT_NODE && strcmp((const char *)k->name, "highlighting") == 0) {
            hl = k;
        }
        if (k->type == XML_ELEMENT_NODE && strcmp((const char *)k->name, "general") == 0) {
            general = k;
        }
    }
    if (general) {
        for (xmlNodePtr k = general->children; k; k = k->next) {
            if (k->type == XML_ELEMENT_NODE && strcmp((const char *)k->name, "keywords") == 0) {
                d->case_sensitive = attr_bool(k, "casesensitive", 1);
                delims_apply(&d->delims, k);
            }
        }
    }
    if (hl) {
        /* formats first, so rules can name them */
        for (xmlNodePtr k = hl->children; k; k = k->next) {
            if (k->type != XML_ELEMENT_NODE || strcmp((const char *)k->name, "itemDatas") != 0) {
                continue;
            }
            for (xmlNodePtr it = k->children; it; it = it->next) {
                if (it->type != XML_ELEMENT_NODE || strcmp((const char *)it->name, "itemData") != 0) {
                    continue;
                }
                char *name = attr_dup(it, "name");
                if (!name) {
                    continue;
                }
                GROW(repo->formats, repo->nformats, repo->capformats);
                Format *f = &repo->formats[repo->nformats];
                memset(f, 0, sizeof *f);
                f->name = name;
                f->def = d;
                char buf[64];
                f->style = style_of(attr(it, "defStyleNum", buf, sizeof buf));
                f->has_fg = parse_colour(attr(it, "color", buf, sizeof buf), &f->fg);
                f->has_bg = parse_colour(attr(it, "backgroundColor", buf, sizeof buf), &f->bg);
                if ((f->has_bold = has_attr(it, "bold"))) {
                    f->bold = attr_bool(it, "bold", 0);
                }
                if ((f->has_italic = has_attr(it, "italic"))) {
                    f->italic = attr_bool(it, "italic", 0);
                }
                if ((f->has_underline = has_attr(it, "underline"))) {
                    f->underline = attr_bool(it, "underline", 0);
                }
                if ((f->has_strike = has_attr(it, "strikeOut"))) {
                    f->strike = attr_bool(it, "strikeOut", 0);
                }
                GROW(d->formats, d->nformats, d->capformats);
                d->formats[d->nformats++] = (int)repo->nformats;
                repo->nformats++;
            }
        }
        for (xmlNodePtr k = hl->children; k; k = k->next) {
            if (k->type != XML_ELEMENT_NODE) {
                continue;
            }
            if (strcmp((const char *)k->name, "list") == 0) {
                char *name = attr_dup(k, "name");
                if (!name) {
                    continue;
                }
                KshList *l = xmalloc(sizeof *l);
                memset(l, 0, sizeof *l);
                l->name = name;
                for (xmlNodePtr it = k->children; it; it = it->next) {
                    if (it->type != XML_ELEMENT_NODE) {
                        continue;
                    }
                    if (strcmp((const char *)it->name, "item") == 0) {
                        char *w = element_text(it);
                        if (*w) {
                            GROW(l->items, l->nitems, l->cap);
                            l->items[l->nitems++] = w;
                        } else {
                            free(w);
                        }
                    } else if (strcmp((const char *)it->name, "include") == 0) {
                        GROW(l->includes, l->nincludes, l->capinc);
                        l->includes[l->nincludes++] = element_text(it);
                    }
                }
                GROW(d->lists, d->nlists, d->caplists);
                d->lists[d->nlists++] = l;
            } else if (strcmp((const char *)k->name, "contexts") == 0) {
                for (xmlNodePtr c = k->children; c; c = c->next) {
                    if (c->type != XML_ELEMENT_NODE || strcmp((const char *)c->name, "context") != 0) {
                        continue;
                    }
                    char *name = attr_dup(c, "name");
                    if (!name) {
                        continue;
                    }
                    KshCtx *x = xmalloc(sizeof *x);
                    memset(x, 0, sizeof *x);
                    x->name = name;
                    x->def = d;
                    char buf[1024];
                    x->format = format_named(d, attr(c, "attribute", buf, sizeof buf));
                    x->end_text = attr_dup(c, "lineEndContext");
                    x->empty_text = attr_dup(c, "lineEmptyContext");
                    x->fall_text = attr_dup(c, "fallthroughContext");
                    x->fallthrough = attr_bool(c, "fallthrough", 0);
                    x->dynamic = attr_bool(c, "dynamic", 0);
                    x->stop_empty_loop = attr_bool(c, "stopEmptyLineContextSwitchLoop", 0);
                    ctx_add_rules(d, x, c);
                    GROW(d->ctxs, d->nctx, d->capctx);
                    d->ctxs[d->nctx++] = x;
                }
            }
        }
    }
    xmlFreeDoc(doc);
    if (d->nctx == 0) {
        d->failed = 1;
        return 0;
    }
    /* LOADED BEFORE IT IS RESOLVED, so two definitions naming each other
     * (HTML's JavaScript, JavaScript's HTML) each find the other's contexts:
     * the switches and includes below may load another definition, which
     * may come back here and find this one parsed */
    d->loaded = 1;
    for (size_t i = 0; i < d->nctx; i++) {
        KshCtx *x = d->ctxs[i];
        switch_resolve(d, x->end_text, &x->line_end);
        switch_resolve(d, x->empty_text, &x->line_empty);
        switch_resolve(d, x->fall_text, &x->fall);
        /* KDE 6: a fallthrough context alone turns fallthrough on */
        if (x->fall_text && (x->fall.pop || x->fall.ctx)) {
            x->fallthrough = 1;
        }
        if (!x->fall.pop && !x->fall.ctx) {
            x->fallthrough = 0;
        }
    }
    for (size_t i = 0; i < d->nall; i++) {
        Rule *r = d->all_rules[i];
        switch_resolve(d, r->ctx_text, &r->sw);
    }
    for (size_t i = 0; i < d->nctx; i++) {
        ctx_resolve(d->ctxs[i]);
    }
    return 1;
}

/* one context by its name: "Name", "Name##Def", "##Def" */
static KshCtx *ctx_lookup(KshDef *d, const char *p, size_t len) {
    char name[1024];
    snprintf(name, sizeof name, "%.*s", (int)(len < sizeof name ? len : sizeof name - 1), p);
    char *hash = strstr(name, "##");
    if (!hash) {
        return ctx_named(d, name);
    }
    KshDef *other = ksh_find(d->repo, hash + 2);
    if (!other) {
        return NULL;
    }
    if (hash == name) {
        return other->ctxs[0];
    }
    *hash = '\0';
    return ctx_named(other, name);
}

/* "#stay", "#pop#pop", "#pop!Name", "Name", "Name##Def", "##Def", "A!B" */
static void switch_resolve(KshDef *d, const char *text, Switch *out) {
    out->pop = 0;
    out->ctx = NULL;
    out->more = NULL;
    out->nmore = 0;
    if (!text || !*text || strcmp(text, "#stay") == 0) {
        return;
    }
    const char *p = text;
    while (strncmp(p, "#pop", 4) == 0) {
        out->pop++;
        p += 4;
    }
    if (*p == '!') {
        p++;
    }
    KshCtx *found[64];
    size_t n = 0;
    while (*p && n < 64) {
        const char *bang = strchr(p, '!');
        size_t len = bang ? (size_t)(bang - p) : strlen(p);
        KshCtx *c = ctx_lookup(d, p, len);
        if (c) {
            found[n++] = c;
        }
        p += len;
        if (*p == '!') {
            p++;
        }
    }
    if (!n) {
        return;
    }
    out->ctx = found[n - 1];
    if (n > 1) {
        out->nmore = n - 1;
        out->more = xmalloc(out->nmore * sizeof *out->more);
        memcpy(out->more, found, out->nmore * sizeof *out->more);
    }
}

static void ctx_resolve(KshCtx *c) {
    if (c->resolve) {
        return;
    }
    c->resolve = 1;
    for (size_t i = 0; i < c->nrules;) {
        Rule *r = c->rules[i];
        if (r->kind != R_INCLUDERULES) {
            i++;
            continue;
        }
        KshCtx *inc = NULL;
        const char *name = r->text ? r->text : "";
        const char *hash = strstr(name, "##");
        if (!hash) {
            inc = ctx_named(c->def, name);
        } else {
            KshDef *other = ksh_find(c->def->repo, hash + 2);
            if (other) {
                if (hash == name) {
                    inc = other->ctxs[0];
                } else {
                    char buf[1024];
                    snprintf(buf, sizeof buf, "%.*s", (int)(hash - name), name);
                    inc = ctx_named(other, buf);
                }
            }
        }
        if (!inc || inc == c) {
            memmove(&c->rules[i], &c->rules[i + 1], (c->nrules - i - 1) * sizeof *c->rules);
            c->nrules--;
            continue;
        }
        ctx_resolve(inc);
        if (r->include_attrib) {
            c->format = inc->format;
        }
        /* replace the include by the included context's rules */
        size_t add = inc->nrules;
        size_t need = c->nrules - 1 + add;
        if (need > c->cap) {
            c->cap = need;
            c->rules = xrealloc(c->rules, c->cap * sizeof *c->rules);
        }
        memmove(&c->rules[i + add], &c->rules[i + 1], (c->nrules - i - 1) * sizeof *c->rules);
        if (add) {
            memcpy(&c->rules[i], inc->rules, add * sizeof *c->rules);
        }
        c->nrules = need;
        i += add;
    }
    c->resolve = 2;
}

/* ---- keyword lists --------------------------------------------------------- */

static char *fold_text(const char *s) {
    size_t n;
    uint32_t *cps = to_cps(s, &n);
    char *out = xmalloc(n * 4 + 1);
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t c = fold(cps[i]);
        if (c < 0x80) {
            out[o++] = (char)c;
        } else if (c < 0x800) {
            out[o++] = (char)(0xC0 | (c >> 6));
            out[o++] = (char)(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out[o++] = (char)(0xE0 | (c >> 12));
            out[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (c & 0x3F));
        } else {
            out[o++] = (char)(0xF0 | (c >> 18));
            out[o++] = (char)(0x80 | ((c >> 12) & 0x3F));
            out[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[o] = '\0';
    free(cps);
    return out;
}

static void list_collect(KshDef *d, KshList *l, char ***words, size_t *n, size_t *cap);

static void list_collect(KshDef *d, KshList *l, char ***words, size_t *n, size_t *cap) {
    if (l->resolving) {
        return;
    }
    l->resolving = 1;
    for (size_t i = 0; i < l->nitems; i++) {
        GROW(*words, *n, *cap);
        (*words)[(*n)++] = l->items[i];
    }
    for (size_t i = 0; i < l->nincludes; i++) {
        const char *name = l->includes[i];
        const char *hash = strstr(name, "##");
        KshDef *od = d;
        char buf[1024];
        snprintf(buf, sizeof buf, "%s", name);
        if (hash) {
            od = ksh_find(d->repo, hash + 2);
            buf[hash - name] = '\0';
        }
        KshList *other = od ? list_named(od, buf) : NULL;
        if (other) {
            list_collect(od, other, words, n, cap);
        }
    }
    l->resolving = 0;
}

static void list_resolve(KshDef *d, KshList *l) {
    if (l->resolved) {
        return;
    }
    char **words = NULL;
    size_t n = 0, cap = 0;
    list_collect(d, l, &words, &n, &cap);
    l->words = xmalloc((n ? n : 1) * sizeof *l->words);
    l->folded = xmalloc((n ? n : 1) * sizeof *l->folded);
    for (size_t i = 0; i < n; i++) {
        l->words[i] = xstrdup(words[i]);
        l->folded[i] = fold_text(words[i]);
    }
    l->nwords = n;
    free(words);
    qsort(l->words, n, sizeof *l->words, cmp_str);
    qsort(l->folded, n, sizeof *l->folded, cmp_str);
    l->resolved = 1;
}

static int sorted_has(char **arr, size_t n, const char *key) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp(arr[mid], key);
        if (c == 0) {
            return 1;
        }
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return 0;
}

/* ---- the state ----------------------------------------------------------- */

static void frame_free(Frame *f) {
    for (size_t i = 0; i < f->ncaps; i++) {
        free(f->caps[i]);
    }
    free(f->caps);
}

void ksh_state_free(KshState *s) {
    if (!s) {
        return;
    }
    for (size_t i = 0; i < s->n; i++) {
        frame_free(&s->frames[i]);
    }
    free(s->frames);
    free(s);
}

KshState *ksh_state_copy(const KshState *s) {
    KshState *o = xmalloc(sizeof *o);
    o->def = s->def;
    o->n = o->cap = s->n;
    o->frames = xmalloc((s->n ? s->n : 1) * sizeof *o->frames);
    for (size_t i = 0; i < s->n; i++) {
        o->frames[i].ctx = s->frames[i].ctx;
        o->frames[i].ncaps = s->frames[i].ncaps;
        o->frames[i].caps = NULL;
        if (s->frames[i].ncaps) {
            o->frames[i].caps = xmalloc(s->frames[i].ncaps * sizeof(char *));
            for (size_t k = 0; k < s->frames[i].ncaps; k++) {
                o->frames[i].caps[k] = xstrdup(s->frames[i].caps[k]);
            }
        }
    }
    return o;
}

int ksh_state_same(const KshState *a, const KshState *b) {
    if (!a || !b) {
        return a == b;
    }
    if (a->def != b->def || a->n != b->n) {
        return 0;
    }
    for (size_t i = 0; i < a->n; i++) {
        if (a->frames[i].ctx != b->frames[i].ctx || a->frames[i].ncaps != b->frames[i].ncaps) {
            return 0;
        }
        for (size_t k = 0; k < a->frames[i].ncaps; k++) {
            if (strcmp(a->frames[i].caps[k], b->frames[i].caps[k]) != 0) {
                return 0;
            }
        }
    }
    return 1;
}

static void push(KshState *s, KshCtx *c, char **caps, size_t ncaps) {
    GROW(s->frames, s->n, s->cap);
    s->frames[s->n].ctx = c;
    s->frames[s->n].caps = caps;
    s->frames[s->n].ncaps = ncaps;
    s->n++;
}

/* KDE's switchContext: pop as asked, never the first context; then push.
 * Answers 0 when it tried to pop the first context and pushed nothing. */
static int do_switch(KshState *s, const Switch *sw, char **caps, size_t ncaps) {
    if (sw->pop <= 0 && !sw->ctx) {
        for (size_t i = 0; i < ncaps; i++) {
            free(caps[i]);
        }
        free(caps);
        return 1;
    }
    int survived = 1;
    if (sw->pop > 0) {
        survived = (int)s->n > sw->pop;
        size_t keep = (int)s->n - sw->pop < 1 ? 1 : s->n - (size_t)sw->pop;
        while (s->n > keep) {
            frame_free(&s->frames[--s->n]);
        }
    }
    if (sw->ctx) {
        for (size_t i = 0; i < sw->nmore; i++) {
            push(s, sw->more[i], NULL, 0);
        }
        push(s, sw->ctx, caps, ncaps);
        return 1;
    }
    for (size_t i = 0; i < ncaps; i++) {
        free(caps[i]);
    }
    free(caps);
    return survived;
}

/* ---- matching ------------------------------------------------------------ */

typedef struct {
    const char *text;        /* the line's bytes */
    size_t len;
    uint32_t *cp;            /* its codepoints */
    size_t *at;              /* each codepoint's byte offset; at[n] = len */
    size_t n;
} Line;

static size_t cp_index(const Line *l, size_t byte) {
    size_t lo = 0, hi = l->n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (l->at[mid] < byte) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

/* KDE's matchEscapedChar */
static size_t escaped_char(const Line *l, size_t o) {
    if (l->cp[o] != '\\' || l->n < o + 2) {
        return o;
    }
    uint32_t c = l->cp[o + 1];
    switch (c) {
    case 'a': case 'b': case 'e': case 'f': case 'n': case 'r': case 't': case 'v':
    case '"': case '\'': case '?': case '\\':
        return o + 2;
    case 'x':
        if (o + 2 < l->n && is_hex(l->cp[o + 2])) {
            if (o + 3 < l->n && is_hex(l->cp[o + 3])) {
                return o + 4;
            }
            return o + 3;
        }
        return o;
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
        if (o + 2 < l->n && is_octal(l->cp[o + 2])) {
            if (o + 3 < l->n && is_octal(l->cp[o + 3])) {
                return o + 4;
            }
            return o + 3;
        }
        return o + 2;
    }
    return o;
}

static int cps_equal(const uint32_t *a, const uint32_t *b, size_t n, int insensitive) {
    for (size_t i = 0; i < n; i++) {
        if (insensitive ? fold(a[i]) != fold(b[i]) : a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

/* %1..%9 in a dynamic rule's text, each capture escaped for a regular
 * expression when `quote` */
static char *replace_captures(const char *pat, char **caps, size_t ncaps, int quote) {
    size_t cap = strlen(pat) * 2 + 16, o = 0;
    char *out = xmalloc(cap);
    for (const char *p = pat; *p; p++) {
        if (*p == '%' && is_digit((unsigned char)p[1])) {
            int k = p[1] - '0';
            p++;
            const char *c = (k >= 1 && (size_t)k <= ncaps) ? caps[k - 1] : "";
            for (const char *q = c; *q; q++) {
                if (o + 4 >= cap) {
                    cap *= 2;
                    out = xrealloc(out, cap);
                }
                if (quote && !((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9') ||
                               *q == '_' || (unsigned char)*q >= 0x80)) {
                    out[o++] = '\\';
                }
                out[o++] = *q;
            }
            continue;
        }
        if (o + 4 >= cap) {
            cap *= 2;
            out = xrealloc(out, cap);
        }
        out[o++] = *p;
    }
    out[o] = '\0';
    return out;
}

/* QRegularExpression's JIT stack, 32 KB growing to 512 KB: with PCRE2's
 * default of 32 KB a long line's match gave up where KDE's succeeded
 * (measured: Markdown's emphasis over a 12,000-character line) */
static pcre2_match_context *match_context(void) {
    static pcre2_match_context *mc;
    if (!mc) {
        mc = pcre2_match_context_create(NULL);
        pcre2_jit_stack *js = pcre2_jit_stack_create(32 * 1024, 512 * 1024, NULL);
        if (mc && js) {
            pcre2_jit_stack_assign(mc, NULL, js);
        }
    }
    return mc;
}

static pcre2_code *compile_re(const char *pat, const Rule *r) {
    uint32_t opts = PCRE2_UTF | PCRE2_UCP | PCRE2_MATCH_INVALID_UTF;
    if (r->insensitive) {
        opts |= PCRE2_CASELESS;
    }
    if (r->minimal) {
        opts |= PCRE2_UNGREEDY;
    }
    int err;
    PCRE2_SIZE eo;
    pcre2_code *re = pcre2_compile((PCRE2_SPTR)pat, PCRE2_ZERO_TERMINATED, opts, &err, &eo, NULL);
    /* QT'S OWN SWITCH, read here too, so a valgrind tier turns off both
     * engines' JIT with one variable: the JIT's code reads sixteen bytes at
     * a time past a string's end, which valgrind reports (measured) though
     * it is not wrong */
    static int jit = -1;
    if (jit < 0) {
        const char *v = getenv("QT_ENABLE_REGEXP_JIT");
        jit = !(v && strcmp(v, "0") == 0);
    }
    if (re && jit) {
        pcre2_jit_compile(re, PCRE2_JIT_COMPLETE);
    }
    return re;
}

/* one rule at codepoint `o`: the end of its match, or `o` for none, and
 * where it could next match (`*skip`: -1 never on this line, 0 unknown) */
static size_t match_rule(Rule *r, const Line *l, size_t o, char **caps, size_t ncaps, char ***out_caps,
                         size_t *out_ncaps, long *skip, pcre2_match_data *md) {
    const KshDef *d = r->def;
    *skip = 0;
    switch (r->kind) {
    case R_ANYCHAR:
        for (size_t i = 0; i < r->ncps; i++) {
            if (r->cps[i] == l->cp[o]) {
                return o + 1;
            }
        }
        return o;
    case R_DETECTCHAR: {
        uint32_t c = r->c1;
        if (r->dynamic) {
            if (r->capture <= 0 || (size_t)r->capture > ncaps || !caps[r->capture - 1][0]) {
                return o;
            }
            c = first_cp(caps[r->capture - 1]);
        }
        return l->cp[o] == c ? o + 1 : o;
    }
    case R_DETECT2CHARS:
        if (l->n - o < 2) {
            return o;
        }
        return l->cp[o] == r->c1 && l->cp[o + 1] == r->c2 ? o + 2 : o;
    case R_DETECTIDENTIFIER: {
        if (!is_letter(l->cp[o]) && l->cp[o] != '_') {
            return o;
        }
        size_t i = o + 1;
        while (i < l->n && (is_letter_or_number(l->cp[i]) || l->cp[i] == '_')) {
            i++;
        }
        return i;
    }
    case R_DETECTSPACES: {
        size_t i = o;
        while (i < l->n && is_space(l->cp[i])) {
            i++;
        }
        return i;
    }
    case R_FLOAT: {
        if (o > 0 && !is_delim(r, l->cp[o - 1])) {
            return o;
        }
        size_t i = o;
        while (i < l->n && is_digit(l->cp[i])) {
            i++;
        }
        if (i >= l->n || l->cp[i] != '.') {
            return o;
        }
        i++;
        while (i < l->n && is_digit(l->cp[i])) {
            i++;
        }
        if (i == o + 1) {
            return o;
        }
        size_t e = i;
        if (e >= l->n || (l->cp[e] != 'e' && l->cp[e] != 'E')) {
            return i;
        }
        e++;
        if (e < l->n && (l->cp[e] == '+' || l->cp[e] == '-')) {
            e++;
        }
        int found = 0;
        while (e < l->n && is_digit(l->cp[e])) {
            e++;
            found = 1;
        }
        return found ? e : i;
    }
    case R_INT: {
        if (o > 0 && !is_delim(r, l->cp[o - 1])) {
            return o;
        }
        size_t i = o;
        while (i < l->n && is_digit(l->cp[i])) {
            i++;
        }
        return i;
    }
    case R_HLCCHAR: {
        if (l->n < o + 3) {
            return o;
        }
        if (l->cp[o] != '\'' || l->cp[o + 1] == '\'') {
            return o;
        }
        size_t i = escaped_char(l, o + 1);
        if (i == o + 1) {
            if (l->cp[i] == '\\') {
                return o;
            }
            i++;
        }
        if (i >= l->n) {
            return o;
        }
        return l->cp[i] == '\'' ? i + 1 : o;
    }
    case R_HLCHEX: {
        if (o > 0 && !is_delim(r, l->cp[o - 1])) {
            return o;
        }
        if (l->n < o + 3) {
            return o;
        }
        if (l->cp[o] != '0' || (l->cp[o + 1] != 'x' && l->cp[o + 1] != 'X') || !is_hex(l->cp[o + 2])) {
            return o;
        }
        size_t i = o + 3;
        while (i < l->n && is_hex(l->cp[i])) {
            i++;
        }
        return i;
    }
    case R_HLCOCT: {
        if (o > 0 && !is_delim(r, l->cp[o - 1])) {
            return o;
        }
        if (l->n < o + 2) {
            return o;
        }
        if (l->cp[o] != '0' || !is_octal(l->cp[o + 1])) {
            return o;
        }
        size_t i = o + 2;
        while (i < l->n && is_octal(l->cp[i])) {
            i++;
        }
        return i;
    }
    case R_HLCSTRINGCHAR:
        return escaped_char(l, o);
    case R_KEYWORD: {
        size_t i = o;
        while (i < l->n && !is_delim(r, l->cp[i])) {
            i++;
        }
        if (i == o) {
            return o;
        }
        if (!r->list) {
            r->list = list_named(r->def, r->text);
            if (!r->list) {
                *skip = -1;
                return o;
            }
        }
        list_resolve(r->def, r->list);
        int insensitive = r->list_insensitive >= 0 ? r->list_insensitive : !d->case_sensitive;
        size_t bl = l->at[i] - l->at[o];
        char *w = xmalloc(bl + 1);
        memcpy(w, l->text + l->at[o], bl);
        w[bl] = '\0';
        int hit;
        if (insensitive) {
            char *f = fold_text(w);
            hit = sorted_has(r->list->folded, r->list->nwords, f);
            free(f);
        } else {
            hit = sorted_has(r->list->words, r->list->nwords, w);
        }
        free(w);
        if (hit) {
            return i;
        }
        *skip = (long)i;
        return o;
    }
    case R_LINECONTINUE:
        return o == l->n - 1 && l->cp[o] == r->c1 ? o + 1 : o;
    case R_RANGEDETECT: {
        if (l->n - o < 2 || l->cp[o] != r->c1) {
            return o;
        }
        for (size_t i = o + 1; i < l->n; i++) {
            if (l->cp[i] == r->c2) {
                return i + 1;
            }
        }
        return o;
    }
    case R_STRINGDETECT: {
        if (r->dynamic) {
            char *s = replace_captures(r->text, caps, ncaps, 0);
            size_t n;
            uint32_t *cps = to_cps(s, &n);
            free(s);
            size_t res = o;
            if (n && l->n - o >= n && cps_equal(&l->cp[o], cps, n, r->insensitive)) {
                res = o + n;
            }
            free(cps);
            return res;
        }
        if (!r->ncps || l->n - o < r->ncps) {
            return o;
        }
        return cps_equal(&l->cp[o], r->cps, r->ncps, r->insensitive) ? o + r->ncps : o;
    }
    case R_WORDDETECT: {
        size_t n = r->ncps;
        if (!n || l->n - o < n) {
            return o;
        }
        if (o > 0 && !is_delim(r, l->cp[o - 1]) && !is_delim(r, l->cp[o])) {
            return o;
        }
        if (!cps_equal(&l->cp[o], r->cps, n, r->insensitive)) {
            return o;
        }
        if (l->n == o + n || is_delim(r, l->cp[o + n]) || is_delim(r, l->cp[o + n - 1])) {
            return o + n;
        }
        return o;
    }
    case R_REGEXPR: {
        pcre2_code *re = NULL;
        int own = 0;
        if (r->dynamic) {
            char *pat = replace_captures(r->text, caps, ncaps, 1);
            re = compile_re(pat, r);
            free(pat);
            own = 1;
        } else {
            if (!r->re && !r->re_failed) {
                r->re = compile_re(r->text, r);
                r->re_failed = r->re == NULL;
            }
            re = r->re;
        }
        if (!re) {
            *skip = -1;
            return o;
        }
        int rc = pcre2_match(re, (PCRE2_SPTR)l->text, l->len, l->at[o], 0, md, match_context());
        size_t res = o;
        if (rc < 0) {
            *skip = -1;
        } else {
            PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
            size_t start = cp_index(l, ov[0]);
            if (ov[0] == l->at[o]) {
                size_t end = cp_index(l, ov[1]);
                res = end;
                /* captures 1..last that matched, as capturedTexts minus 0 */
                if (rc > 1) {
                    size_t nc = (size_t)rc - 1;
                    char **cs = xmalloc(nc * sizeof *cs);
                    for (size_t k = 0; k < nc; k++) {
                        PCRE2_SIZE a = ov[2 * (k + 1)], b = ov[2 * (k + 1) + 1];
                        if (a == PCRE2_UNSET) {
                            cs[k] = xstrdup("");
                        } else {
                            cs[k] = xmalloc(b - a + 1);
                            memcpy(cs[k], l->text + a, b - a);
                            cs[k][b - a] = '\0';
                        }
                    }
                    *out_caps = cs;
                    *out_ncaps = nc;
                }
                if (end == o) {
                    /* an empty match is no match, and KDE does not remember it */
                    res = o;
                }
            } else {
                *skip = (long)start;
            }
        }
        if (own) {
            pcre2_code_free(re);
        }
        return res;
    }
    }
    return o;
}

/* ---- a line --------------------------------------------------------------- */

typedef struct {
    KshSpan *s;
    size_t n, cap;
} Spans;

static void span_add(Spans *sp, const Line *l, size_t from, size_t to, int format) {
    if (to <= from) {
        return;
    }
    size_t bs = l->at[from], be = l->at[to];
    if (sp->n && sp->s[sp->n - 1].format == format && sp->s[sp->n - 1].start + sp->s[sp->n - 1].len == bs) {
        sp->s[sp->n - 1].len += be - bs;
        return;
    }
    GROW(sp->s, sp->n, sp->cap);
    sp->s[sp->n].start = bs;
    sp->s[sp->n].len = be - bs;
    sp->s[sp->n].format = format;
    sp->n++;
}

typedef struct {
    Rule *rule;
    long skip;
} Skip;

KshState *ksh_highlight(KshDef *def, const KshState *before, const char *text, size_t len, KshSpan **spans,
                        size_t *nspans) {
    KshState *st;
    if (before && before->def == def && before->n) {
        st = ksh_state_copy(before);
    } else {
        st = xmalloc(sizeof *st);
        memset(st, 0, sizeof *st);
        st->def = def;
        push(st, def->ctxs[0], NULL, 0);
    }
    Spans sp = {0};
    *spans = NULL;
    *nspans = 0;
    if (len == 0) {
        /* KDE's: the empty-line switch where there is one, else the line
         * end's, so an empty line after a continued one ends what the
         * continuation kept open (measured: Gnuplot, `dgrid3d\` then an
         * empty line) */
        int guard = 0;
        for (;;) {
            KshCtx *top = st->frames[st->n - 1].ctx;
            int empty = top->line_empty.pop || top->line_empty.ctx;
            int end = top->line_end.pop || top->line_end.ctx;
            if (!empty && !end) {
                break;
            }
            if (empty) {
                if (!do_switch(st, &top->line_empty, NULL, 0)) {
                    break;
                }
                if (st->frames[st->n - 1].ctx->stop_empty_loop) {
                    break;
                }
            } else if (!do_switch(st, &top->line_end, NULL, 0)) {
                break;
            }
            if (++guard > 1024) {
                break;
            }
        }
        return st;
    }
    Line l;
    l.text = text;
    l.len = len;
    l.cp = xmalloc((len + 1) * sizeof *l.cp);
    l.at = xmalloc((len + 1) * sizeof *l.at);
    l.n = 0;
    for (size_t i = 0; i < len;) {
        l.at[l.n] = i;
        l.cp[l.n] = utf8_next((const unsigned char *)text, len, &i);
        l.n++;
    }
    l.at[l.n] = len;
    size_t first_non_space = 0;
    while (first_non_space < l.n && is_space(l.cp[first_non_space])) {
        first_non_space++;
    }
    pcre2_match_data *md = pcre2_match_data_create(64, NULL);
    Skip *skips = NULL;
    size_t nskips = 0, capskips = 0;
    /* the captures the skips were last taken under by a dynamic regular
     * expression; one meeting others clears every skip, and at the start of
     * a line that is "none" (measured: Elixir, `7file:raise`). A dynamic
     * StringDetect or DetectChar records no skip and clears nothing
     * (measured: MIB) */
    char **dyn_caps = NULL;
    size_t ndyn = 0;

    size_t offset = 0, begin = 0;
    int line_continue = 0;
    int cur_format = -2;        /* nothing yet */
    size_t last_offset = 0;
    int stuck = 0;
    do {
        /* KDE'S GUARD, measured on Elixir's `~`: a lookahead into a context
         * that falls through back out of it makes no progress forever. After
         * 1024 steps at one offset the line is given up, the rest of it in
         * the format of the moment, and the stack left as it stands. */
        if (last_offset == offset) {
            if (++stuck > 1024) {
                break;
            }
        } else {
            last_offset = offset;
            stuck = 0;
        }
        KshCtx *ctx = st->frames[st->n - 1].ctx;
        size_t new_offset = offset;
        int new_format = -2;
        int lookahead = 0;
        for (size_t ri = 0; ri < ctx->nrules; ri++) {
            Rule *r = ctx->rules[ri];
            if (r->column >= 0 && (size_t)r->column != offset) {
                continue;
            }
            if (r->first_non_space && offset > first_non_space) {
                continue;
            }
            Frame *top = &st->frames[st->n - 1];
            if (r->dynamic && r->kind == R_REGEXPR) {
                int same = ndyn == top->ncaps;
                for (size_t k = 0; same && k < ndyn; k++) {
                    same = strcmp(dyn_caps[k], top->caps[k]) == 0;
                }
                if (!same) {
                    nskips = 0;
                }
            }
            long cur_skip = 0;
            size_t si;
            for (si = 0; si < nskips; si++) {
                if (skips[si].rule == r) {
                    cur_skip = skips[si].skip;
                    break;
                }
            }
            if (cur_skip < 0 || cur_skip > (long)offset) {
                continue;
            }
            char **caps = NULL;
            size_t ncaps = 0;
            long skip = 0;
            new_offset = match_rule(r, &l, offset, top->caps, top->ncaps, &caps, &ncaps, &skip, md);
            if (skip < 0 || skip > (long)offset) {
                if (si < nskips) {
                    skips[si].skip = skip;
                } else {
                    GROW(skips, nskips, capskips);
                    skips[nskips].rule = r;
                    skips[nskips].skip = skip;
                    nskips++;
                }
                if (r->dynamic) {
                    for (size_t k = 0; k < ndyn; k++) {
                        free(dyn_caps[k]);
                    }
                    free(dyn_caps);
                    ndyn = top->ncaps;
                    dyn_caps = xmalloc((ndyn ? ndyn : 1) * sizeof *dyn_caps);
                    for (size_t k = 0; k < ndyn; k++) {
                        dyn_caps[k] = xstrdup(top->caps[k]);
                    }
                }
            }
            if (new_offset <= offset) {
                for (size_t k = 0; k < ncaps; k++) {
                    free(caps[k]);
                }
                free(caps);
                new_offset = offset;
                continue;
            }
            if (r->lookahead) {
                do_switch(st, &r->sw, caps, ncaps);
                lookahead = 1;
                break;
            }
            do_switch(st, &r->sw, caps, ncaps);
            new_format = r->format >= 0 ? r->format : st->frames[st->n - 1].ctx->format;
            if (new_offset == l.n && r->kind == R_LINECONTINUE) {
                line_continue = 1;
            }
            break;
        }
        if (lookahead) {
            continue;
        }
        if (new_offset <= offset) {
            if (ctx->fallthrough) {
                do_switch(st, &ctx->fall, NULL, 0);
                continue;
            }
            new_offset = offset + 1;
            new_format = ctx->format;
        }
        if (new_format != cur_format) {
            if (offset > 0 && cur_format != -2) {
                span_add(&sp, &l, begin, offset, cur_format);
            }
            begin = offset;
            cur_format = new_format;
        }
        offset = new_offset;
    } while (offset < l.n);
    if (begin < offset) {
        span_add(&sp, &l, begin, l.n, cur_format);
    }
    int guard = 0;
    while (!line_continue) {
        KshCtx *top = st->frames[st->n - 1].ctx;
        if (!top->line_end.pop && !top->line_end.ctx) {
            break;
        }
        if (!do_switch(st, &top->line_end, NULL, 0)) {
            break;
        }
        if (++guard > 1024) {
            break;
        }
    }
    for (size_t k = 0; k < ndyn; k++) {
        free(dyn_caps[k]);
    }
    free(dyn_caps);
    free(skips);
    pcre2_match_data_free(md);
    free(l.cp);
    free(l.at);
    *spans = sp.s;
    *nspans = sp.n;
    return st;
}

const char *ksh_format_name(const KshRepo *repo, int format) {
    if (format < 0 || (size_t)format >= repo->nformats) {
        return "";
    }
    return repo->formats[format].name;
}

/* ---- themes: KDE's JSON ---------------------------------------------------- */

/* a JSON reader for the theme files' shape, nothing more general */
typedef struct J {
    int kind;                 /* 'o' object, 'a' array, 's' string, 'n' number, 'b' bool, 'z' null */
    char *s;
    double num;
    int b;
    char **keys;
    struct J **vals;
    size_t n, cap;
} J;

static void j_free(J *j) {
    if (!j) {
        return;
    }
    free(j->s);
    for (size_t i = 0; i < j->n; i++) {
        if (j->keys) {
            free(j->keys[i]);
        }
        j_free(j->vals[i]);
    }
    free(j->keys);
    free(j->vals);
    free(j);
}

static void j_ws(const char **p) {
    while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') {
        (*p)++;
    }
}

static char *j_string(const char **p) {
    if (**p != '"') {
        return NULL;
    }
    (*p)++;
    size_t cap = 32, o = 0;
    char *out = xmalloc(cap);
    while (**p && **p != '"') {
        char c = **p;
        if (c == '\\') {
            (*p)++;
            c = **p;
            if (!c) {
                break;
            }
            if (c == 'n') {
                c = '\n';
            } else if (c == 't') {
                c = '\t';
            } else if (c == 'u') {
                unsigned v = 0;
                for (int k = 1; k <= 4 && (*p)[k]; k++) {
                    char h = (*p)[k];
                    v = v * 16 + (unsigned)(h >= 'a' ? h - 'a' + 10 : h >= 'A' ? h - 'A' + 10 : h - '0');
                }
                *p += 4;
                if (o + 4 >= cap) {
                    cap *= 2;
                    out = xrealloc(out, cap);
                }
                if (v < 0x80) {
                    out[o++] = (char)v;
                } else if (v < 0x800) {
                    out[o++] = (char)(0xC0 | (v >> 6));
                    out[o++] = (char)(0x80 | (v & 0x3F));
                } else {
                    out[o++] = (char)(0xE0 | (v >> 12));
                    out[o++] = (char)(0x80 | ((v >> 6) & 0x3F));
                    out[o++] = (char)(0x80 | (v & 0x3F));
                }
                (*p)++;
                continue;
            }
        }
        if (o + 2 >= cap) {
            cap *= 2;
            out = xrealloc(out, cap);
        }
        out[o++] = c;
        (*p)++;
    }
    if (**p == '"') {
        (*p)++;
    }
    out[o] = '\0';
    return out;
}

static J *j_value(const char **p, int depth) {
    j_ws(p);
    if (depth > 32) {
        return NULL;
    }
    J *j = xmalloc(sizeof *j);
    memset(j, 0, sizeof *j);
    char c = **p;
    if (c == '{' || c == '[') {
        j->kind = c == '{' ? 'o' : 'a';
        (*p)++;
        j_ws(p);
        char close = c == '{' ? '}' : ']';
        while (**p && **p != close) {
            char *key = NULL;
            if (j->kind == 'o') {
                key = j_string(p);
                if (!key) {
                    j_free(j);
                    return NULL;
                }
                j_ws(p);
                if (**p != ':') {
                    free(key);
                    j_free(j);
                    return NULL;
                }
                (*p)++;
            }
            J *v = j_value(p, depth + 1);
            if (!v) {
                free(key);
                j_free(j);
                return NULL;
            }
            if (j->n >= j->cap) {
                j->cap = j->cap ? j->cap * 2 : 8;
                j->vals = xrealloc(j->vals, j->cap * sizeof *j->vals);
                if (j->kind == 'o') {
                    j->keys = xrealloc(j->keys, j->cap * sizeof *j->keys);
                }
            }
            if (j->kind == 'o') {
                j->keys[j->n] = key;
            }
            j->vals[j->n++] = v;
            j_ws(p);
            if (**p == ',') {
                (*p)++;
                j_ws(p);
            }
        }
        if (**p != close) {
            j_free(j);
            return NULL;
        }
        (*p)++;
        return j;
    }
    if (c == '"') {
        j->kind = 's';
        j->s = j_string(p);
        return j;
    }
    if (strncmp(*p, "true", 4) == 0 || strncmp(*p, "false", 5) == 0) {
        j->kind = 'b';
        j->b = **p == 't';
        *p += j->b ? 4 : 5;
        return j;
    }
    if (strncmp(*p, "null", 4) == 0) {
        j->kind = 'z';
        *p += 4;
        return j;
    }
    char *end;
    j->num = strtod(*p, &end);
    if (end == *p) {
        free(j);
        return NULL;
    }
    j->kind = 'n';
    *p = end;
    return j;
}

static J *j_get(const J *o, const char *key) {
    if (!o || o->kind != 'o') {
        return NULL;
    }
    for (size_t i = 0; i < o->n; i++) {
        if (strcmp(o->keys[i], key) == 0) {
            return o->vals[i];
        }
    }
    return NULL;
}

static void style_read(const J *o, Style *s) {
    J *v;
    if ((v = j_get(o, "text-color")) && v->kind == 's') {
        s->has_fg = parse_colour(v->s, &s->fg);
    }
    if ((v = j_get(o, "background-color")) && v->kind == 's') {
        s->has_bg = parse_colour(v->s, &s->bg);
    }
    if ((v = j_get(o, "bold")) && v->kind == 'b') {
        s->has_bold = 1;
        s->bold = v->b;
    }
    if ((v = j_get(o, "italic")) && v->kind == 'b') {
        s->has_italic = 1;
        s->italic = v->b;
    }
    if ((v = j_get(o, "underline")) && v->kind == 'b') {
        s->has_underline = 1;
        s->underline = v->b;
    }
    if ((v = j_get(o, "strike-through")) && v->kind == 'b') {
        s->has_strike = 1;
        s->strike = v->b;
    }
}

static KshTheme *theme_parse(const char *text, const char *file) {
    const char *p = text;
    J *root = j_value(&p, 0);
    J *meta = j_get(root, "metadata");
    J *name = j_get(meta, "name");
    if (!name || name->kind != 's' || !*name->s) {
        j_free(root);
        return NULL;
    }
    KshTheme *t = xmalloc(sizeof *t);
    memset(t, 0, sizeof *t);
    t->name = xstrdup(name->s);
    t->file = xstrdup(file);
    J *rev = j_get(meta, "revision");
    t->revision = rev && rev->kind == 'n' ? (int)rev->num : 0;
    J *ts = j_get(root, "text-styles");
    for (int i = 0; i < 31; i++) {
        J *o = j_get(ts, style_names[i]);
        if (o) {
            style_read(o, &t->styles[i]);
        }
    }
    J *ed = j_get(root, "editor-colors");
    if (ed && ed->kind == 'o') {
        t->ed_names = xmalloc((ed->n ? ed->n : 1) * sizeof *t->ed_names);
        t->ed_rgb = xmalloc((ed->n ? ed->n : 1) * sizeof *t->ed_rgb);
        for (size_t i = 0; i < ed->n; i++) {
            uint32_t rgb;
            if (ed->vals[i]->kind == 's' && parse_colour(ed->vals[i]->s, &rgb)) {
                t->ed_names[t->ned] = xstrdup(ed->keys[i]);
                t->ed_rgb[t->ned] = rgb;
                t->ned++;
            }
        }
    }
    J *cs = j_get(root, "custom-styles");
    if (cs && cs->kind == 'o') {
        size_t cap = 0;
        for (size_t i = 0; i < cs->n; i++) {
            J *items = cs->vals[i];
            if (items->kind != 'o') {
                continue;
            }
            for (size_t k = 0; k < items->n; k++) {
                GROW(t->custom, t->ncustom, cap);
                Custom *c = &t->custom[t->ncustom++];
                memset(c, 0, sizeof *c);
                c->def = xstrdup(cs->keys[i]);
                c->item = xstrdup(items->keys[k]);
                style_read(items->vals[k], &c->s);
            }
        }
    }
    j_free(root);
    return t;
}

static void theme_free(KshTheme *t) {
    free(t->name);
    free(t->file);
    for (size_t i = 0; i < t->ned; i++) {
        free(t->ed_names[i]);
    }
    free(t->ed_names);
    free(t->ed_rgb);
    for (size_t i = 0; i < t->ncustom; i++) {
        free(t->custom[i].def);
        free(t->custom[i].item);
    }
    free(t->custom);
    free(t);
}

static void theme_add(KshRepo *repo, KshTheme *t) {
    for (size_t i = 0; i < repo->nthemes; i++) {
        if (strcmp(repo->themes[i]->name, t->name) == 0) {
            if (repo->themes[i]->revision >= t->revision) {
                theme_free(t);
            } else {
                theme_free(repo->themes[i]);
                repo->themes[i] = t;
            }
            return;
        }
    }
    GROW(repo->themes, repo->nthemes, repo->capthemes);
    repo->themes[repo->nthemes++] = t;
}

static char *slurp(const char *file) {
    FILE *f = fopen(file, "rb");
    if (!f) {
        return NULL;
    }
    size_t cap = 4096, n = 0;
    char *buf = xmalloc(cap);
    size_t got;
    while ((got = fread(buf + n, 1, cap - n - 1, f)) > 0) {
        n += got;
        if (cap - n < 2) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
    }
    fclose(f);
    buf[n] = '\0';
    return buf;
}

/* one packed style of src/ksyntax_themes.h (tools/make_ksyntax_themes.py) */
static void style_unpack(const unsigned char *p, Style *s) {
    memset(s, 0, sizeof *s);
    s->has_fg = p[0] & 1;
    s->has_bg = (p[0] & 2) != 0;
    s->fg = (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
    s->bg = (uint32_t)p[4] << 16 | (uint32_t)p[5] << 8 | p[6];
    int *flag[4] = {&s->bold, &s->italic, &s->underline, &s->strike};
    int *has[4] = {&s->has_bold, &s->has_italic, &s->has_underline, &s->has_strike};
    for (int i = 0; i < 4; i++) {
        int v = (p[7] >> (2 * i)) & 3;
        *has[i] = v != 0;
        *flag[i] = v == 2;
    }
}

/* KDE's own themes, which its library carries compiled in and this file
 * carries in src/ksyntax_themes.h, named by the path KDE gives them */
static void builtin_themes(KshRepo *repo) {
    for (size_t b = 0; b < sizeof ksh_builtin_themes / sizeof *ksh_builtin_themes; b++) {
        KshTheme *t = xmalloc(sizeof *t);
        memset(t, 0, sizeof *t);
        char file[256];
        snprintf(file, sizeof file, ":/org.kde.syntax-highlighting/themes/%s", ksh_builtin_themes[b].file);
        t->name = xstrdup(ksh_builtin_themes[b].name);
        t->file = xstrdup(file);
        t->revision = ksh_builtin_themes[b].revision;
        for (int i = 0; i < 31; i++) {
            style_unpack(ksh_builtin_themes[b].styles + 8 * i, &t->styles[i]);
        }
        unsigned ne = ksh_builtin_themes[b].neditor;
        t->ed_names = xmalloc((ne ? ne : 1) * sizeof *t->ed_names);
        t->ed_rgb = xmalloc((ne ? ne : 1) * sizeof *t->ed_rgb);
        for (unsigned i = 0; i < ne; i++) {
            const unsigned char *p = ksh_builtin_themes[b].editor + 4 * i;
            t->ed_names[i] = xstrdup(ksh_roles[p[0]]);
            t->ed_rgb[i] = (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
        }
        t->ned = ne;
        unsigned nc = ksh_builtin_themes[b].ncustom;
        t->custom = xmalloc((nc ? nc : 1) * sizeof *t->custom);
        for (unsigned i = 0; i < nc; i++) {
            const unsigned char *p = ksh_builtin_themes[b].custom + 10 * i;
            unsigned k = (unsigned)p[0] << 8 | p[1];
            t->custom[i].def = xstrdup(ksh_custom_def[k]);
            t->custom[i].item = xstrdup(ksh_custom_item[k]);
            style_unpack(p + 2, &t->custom[i].s);
        }
        t->ncustom = nc;
        theme_add(repo, t);
    }
}

/* KDE's order: the standard locations, then its own (here: the themes this
 * file carries, KDE's own files), then the custom paths */
static void themes_load(KshRepo *repo) {
    if (repo->themes_loaded) {
        return;
    }
    repo->themes_loaded = 1;
    for (size_t i = 0; i < repo->nthemes; i++) {
        theme_free(repo->themes[i]);
    }
    repo->nthemes = 0;
    size_t nd;
    char **dirs = search_dirs(repo, "themes", &nd);
    size_t nstd = nd - repo->npaths;
    for (size_t i = 0; i < nd; i++) {
        if (i == nstd) {
            builtin_themes(repo);
        }
        size_t nf;
        char **files = files_in(dirs[i], ".theme", &nf);
        for (size_t k = 0; k < nf; k++) {
            char *text = slurp(files[k]);
            KshTheme *t = text ? theme_parse(text, files[k]) : NULL;
            if (t) {
                theme_add(repo, t);
            }
            free(text);
            free(files[k]);
        }
        free(files);
        free(dirs[i]);
    }
    if (nstd == nd) {
        builtin_themes(repo);
    }
    free(dirs);
}

const KshTheme *ksh_theme(KshRepo *repo, const char *name) {
    themes_load(repo);
    for (size_t i = 0; i < repo->nthemes; i++) {
        if (strcmp(repo->themes[i]->name, name) == 0) {
            return repo->themes[i];
        }
    }
    return NULL;
}

char *ksh_theme_names(KshRepo *repo) {
    themes_load(repo);
    char **names = xmalloc((repo->nthemes ? repo->nthemes : 1) * sizeof *names);
    size_t total = 1;
    for (size_t i = 0; i < repo->nthemes; i++) {
        names[i] = repo->themes[i]->name;
        total += strlen(names[i]) + 1;
    }
    qsort(names, repo->nthemes, sizeof *names, cmp_str);
    char *out = xmalloc(total);
    out[0] = '\0';
    for (size_t i = 0; i < repo->nthemes; i++) {
        if (i) {
            strcat(out, "\n");
        }
        strcat(out, names[i]);
    }
    free(names);
    return out;
}

const char *ksh_theme_name(const KshTheme *theme) {
    return theme->name;
}

int ksh_theme_editor(const KshTheme *theme, const char *role, uint32_t *rgb) {
    for (size_t i = 0; i < theme->ned; i++) {
        if (strcmp(theme->ed_names[i], role) == 0) {
            *rgb = theme->ed_rgb[i];
            return 1;
        }
    }
    return 0;
}

const KshTheme *ksh_theme_for_background(KshRepo *repo, uint32_t rgb) {
    themes_load(repo);
    const KshTheme *best = NULL;
    for (size_t i = 0; i < repo->nthemes; i++) {
        uint32_t bg;
        if (ksh_theme_editor(repo->themes[i], "BackgroundColor", &bg) && bg == rgb) {
            if (!best || strcmp(repo->themes[i]->name, best->name) < 0) {
                best = repo->themes[i];
            }
        }
    }
    if (best) {
        return best;
    }
    unsigned r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    unsigned mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    unsigned mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    return ksh_theme(repo, (mx + mn) / 2 < 128 ? "Breeze Dark" : "Breeze Light");
}

/* KDE's Format under a theme: the theme's style for this definition's item
 * first, then what the item itself says, then the theme's default style */
void ksh_look(const KshRepo *repo, int format, const KshTheme *theme, KshLook *out) {
    memset(out, 0, sizeof *out);
    const Style *normal = theme ? &theme->styles[0] : NULL;
    if (normal && normal->has_fg) {
        out->fg = normal->fg;
    }
    if (format < 0 || (size_t)format >= repo->nformats || !theme) {
        return;
    }
    const Format *f = &repo->formats[format];
    const Style *ds = &theme->styles[f->style];
    const Style *ov = NULL;
    for (size_t i = 0; i < theme->ncustom; i++) {
        if (strcmp(theme->custom[i].def, f->def->name) == 0 && strcmp(theme->custom[i].item, f->name) == 0) {
            ov = &theme->custom[i].s;
            break;
        }
    }
#define PICK(has, field, dflt_has, dflt)                     \
    if (ov && ov->has) {                                     \
        out->field = ov->field;                              \
    } else if (f->has) {                                     \
        out->field = f->field;                               \
    } else if (ds->has) {                                    \
        out->field = ds->field;                              \
    } else if (dflt_has) {                                   \
        out->field = dflt;                                   \
    }
    PICK(has_fg, fg, normal->has_fg, normal->fg)
    out->has_bg = (ov && ov->has_bg) || f->has_bg || ds->has_bg;
    PICK(has_bg, bg, 0, 0)
    PICK(has_bold, bold, 0, 0)
    PICK(has_italic, italic, 0, 0)
    PICK(has_underline, underline, 0, 0)
    PICK(has_strike, strike, 0, 0)
#undef PICK
}

/* ---- freeing --------------------------------------------------------------- */

static void list_free(KshList *l) {
    free(l->name);
    for (size_t i = 0; i < l->nitems; i++) {
        free(l->items[i]);
    }
    free(l->items);
    for (size_t i = 0; i < l->nincludes; i++) {
        free(l->includes[i]);
    }
    free(l->includes);
    for (size_t i = 0; i < l->nwords; i++) {
        free(l->words[i]);
        free(l->folded[i]);
    }
    free(l->words);
    free(l->folded);
    free(l);
}

static void def_free(KshDef *d) {
    if (!d) {
        return;
    }
    free(d->name);
    free(d->file);
    for (size_t i = 0; i < d->nalt; i++) {
        free(d->alt[i]);
    }
    free(d->alt);
    for (size_t i = 0; i < d->nctx; i++) {
        KshCtx *c = d->ctxs[i];
        free(c->name);
        free(c->end_text);
        free(c->empty_text);
        free(c->fall_text);
        free(c->line_end.more);
        free(c->line_empty.more);
        free(c->fall.more);
        free(c->rules);
        free(c);
    }
    free(d->ctxs);
    for (size_t i = 0; i < d->nall; i++) {
        Rule *r = d->all_rules[i];
        free(r->ctx_text);
        free(r->sw.more);
        free(r->cps);
        free(r->text);
        if (r->delims) {
            free(r->delims->hi);
            free(r->delims);
        }
        if (r->re) {
            pcre2_code_free(r->re);
        }
        free(r);
    }
    free(d->all_rules);
    for (size_t i = 0; i < d->nlists; i++) {
        list_free(d->lists[i]);
    }
    free(d->lists);
    free(d->formats);
    free(d->delims.hi);
    free(d);
}

void ksh_repo_free(KshRepo *repo) {
    if (!repo) {
        return;
    }
    for (size_t i = 0; i < repo->ndefs; i++) {
        def_free(repo->defs[i]);
    }
    free(repo->defs);
    for (size_t i = 0; i < repo->nformats; i++) {
        free(repo->formats[i].name);
    }
    free(repo->formats);
    for (size_t i = 0; i < repo->nthemes; i++) {
        theme_free(repo->themes[i]);
    }
    free(repo->themes);
    for (size_t i = 0; i < repo->npaths; i++) {
        free(repo->paths[i]);
    }
    free(repo->paths);
    free(repo);
}

#else
/* nothing without libxml2; ISO C wants a declaration all the same */
typedef int ksh_built_without_libxml2;
#endif /* HAVE_LIBXML2 */
