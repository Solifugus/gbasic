/* G-SQL's SQL parser (docs/gsql_sql_design.md §3): text into a statement tree
 * with a position on every node. Needs nothing but the text.
 *
 * Hand-written recursive descent, because SQL's grammar is small and its error
 * messages are better written by hand than reported by a generator: every
 * refusal names the token it met and where. Names are folded to lower case
 * unless quoted, PostgreSQL's rule. */

#include "sql.h"
#include "v3.h"
#include "gsql_wire.h"
#include "serialize_tags.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the arena ------------------------------------------------------- */

struct SqlArenaBlock {
    SqlArenaBlock *next;
    size_t used, cap;
    unsigned char data[];
};

void *sql_alloc(SqlArena *a, size_t n) {
    n = (n + 15) & ~(size_t)15;
    SqlArenaBlock *b = a->head;
    if (!b || b->cap - b->used < n) {
        size_t cap = n > 8192 ? n : 8192;
        b = malloc(sizeof *b + cap);
        if (!b) {
            abort();
        }
        b->next = a->head;
        b->used = 0;
        b->cap = cap;
        a->head = b;
    }
    void *p = b->data + b->used;
    b->used += n;
    memset(p, 0, n);
    return p;
}

char *sql_strndup(SqlArena *a, const char *s, size_t n) {
    char *p = sql_alloc(a, n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

void sql_arena_free(SqlArena *a) {
    SqlArenaBlock *b = a->head;
    while (b) {
        SqlArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    a->head = NULL;
}

void sql_parsed_free(SqlParsed *p) {
    sql_arena_free(&p->arena);
    p->stmt = NULL;
}

/* ---- tokens ---------------------------------------------------------- */

typedef enum {
    T_EOF = 0,
    T_IDENT,      /* unquoted, folded: may be a keyword */
    T_QIDENT,     /* "quoted": never a keyword */
    T_STRING,
    T_NUMBER,
    T_PNAME,      /* :name */
    T_PNUM,       /* $n */
    T_SYM         /* punctuation and operators: text holds it */
} TokKind;

typedef struct {
    TokKind kind;
    char *text;          /* folded name, unescaped string, or the symbol */
    size_t len;          /* of text (a string may hold NUL) */
    uint32_t pos;        /* 1-based character offset */
} Tok;

typedef struct {
    const char *src;
    size_t n, i;
    uint32_t chars;      /* characters before src[i] */
    SqlArena *arena;
    Tok tok;             /* the current token */
    GsqlError *err;
    int failed;
    SqlStmt *stmt;       /* for parameter bookkeeping */
} P;

static void advance_byte(P *p) {
    unsigned char c = (unsigned char)p->src[p->i++];
    if ((c & 0xC0) != 0x80) {
        p->chars++;
    }
}

static int fail_at(P *p, uint32_t pos, int code, const char *fmt, ...) {
    if (p->failed) {
        return 0;
    }
    p->failed = 1;
    p->err->code = code;
    p->err->position = pos;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p->err->message, sizeof p->err->message, fmt, ap);
    va_end(ap);
    return 0;
}

/* how the current token reads in a message */
static const char *tok_desc(P *p, char *buf, size_t cap) {
    switch (p->tok.kind) {
    case T_EOF:
        return "the end of the statement";
    case T_STRING:
        snprintf(buf, cap, "the string '%.40s'", p->tok.text);
        return buf;
    case T_NUMBER:
        snprintf(buf, cap, "the number %.40s", p->tok.text);
        return buf;
    case T_PNAME:
        snprintf(buf, cap, "the parameter :%.40s", p->tok.text);
        return buf;
    case T_PNUM:
        snprintf(buf, cap, "the parameter $%.40s", p->tok.text);
        return buf;
    case T_QIDENT:
        snprintf(buf, cap, "\"%.40s\"", p->tok.text);
        return buf;
    default:
        snprintf(buf, cap, "`%.40s`", p->tok.text);
        return buf;
    }
}

static int expected(P *p, const char *what) {
    char b[96];
    return fail_at(p, p->tok.pos, GSQL_SYNTAX, "expected %s, found %s", what, tok_desc(p, b, sizeof b));
}

static void skip_space(P *p) {
    for (;;) {
        while (p->i < p->n && isspace((unsigned char)p->src[p->i])) {
            advance_byte(p);
        }
        if (p->i + 1 < p->n && p->src[p->i] == '-' && p->src[p->i + 1] == '-') {
            while (p->i < p->n && p->src[p->i] != '\n') {
                advance_byte(p);
            }
            continue;
        }
        if (p->i + 1 < p->n && p->src[p->i] == '/' && p->src[p->i + 1] == '*') {
            uint32_t at = p->chars + 1;
            advance_byte(p);
            advance_byte(p);
            while (p->i + 1 < p->n && !(p->src[p->i] == '*' && p->src[p->i + 1] == '/')) {
                advance_byte(p);
            }
            if (p->i + 1 >= p->n) {
                p->i = p->n;
                fail_at(p, at, GSQL_SYNTAX, "a /* comment is never closed");
                return;
            }
            advance_byte(p);
            advance_byte(p);
            continue;
        }
        return;
    }
}

static int ident_start(unsigned char c) {
    return isalpha(c) || c == '_' || c >= 0x80;
}

static int ident_char(unsigned char c) {
    return isalnum(c) || c == '_' || c >= 0x80;
}

static void next(P *p) {
    skip_space(p);
    Tok *t = &p->tok;
    t->pos = p->chars + 1;
    if (p->failed || p->i >= p->n) {
        t->kind = T_EOF;
        t->text = "";
        t->len = 0;
        return;
    }
    size_t start = p->i;
    unsigned char c = (unsigned char)p->src[p->i];
    if (ident_start(c)) {
        while (p->i < p->n && ident_char((unsigned char)p->src[p->i])) {
            advance_byte(p);
        }
        t->kind = T_IDENT;
        t->len = p->i - start;
        t->text = sql_strndup(p->arena, p->src + start, t->len);
        for (size_t k = 0; k < t->len; k++) {
            t->text[k] = (char)tolower((unsigned char)t->text[k]);
        }
        return;
    }
    if (c == '"' || c == '\'') {
        char q = (char)c;
        advance_byte(p);
        GsqlBuf b = {0};
        for (;;) {
            if (p->i >= p->n) {
                gsql_buf_free(&b);
                t->kind = T_EOF;
                t->text = "";
                fail_at(p, t->pos, GSQL_SYNTAX, q == '\'' ? "a string is never closed" : "a quoted name is never closed");
                return;
            }
            if (p->src[p->i] == q) {
                if (p->i + 1 < p->n && p->src[p->i + 1] == q) {
                    v3_put(&b, &q, 1);
                    advance_byte(p);
                    advance_byte(p);
                    continue;
                }
                advance_byte(p);
                break;
            }
            v3_put(&b, p->src + p->i, 1);
            advance_byte(p);
        }
        t->kind = q == '\'' ? T_STRING : T_QIDENT;
        t->len = b.length;
        t->text = sql_strndup(p->arena, b.bytes ? (const char *)b.bytes : "", b.length);
        gsql_buf_free(&b);
        if (t->kind == T_QIDENT && (t->len == 0 || memchr(t->text, 0, t->len))) {
            fail_at(p, t->pos, GSQL_SYNTAX, "a quoted name may not be empty or hold a NUL");
        }
        return;
    }
    if (isdigit(c) || (c == '.' && p->i + 1 < p->n && isdigit((unsigned char)p->src[p->i + 1]))) {
        while (p->i < p->n && isdigit((unsigned char)p->src[p->i])) {
            advance_byte(p);
        }
        if (p->i < p->n && p->src[p->i] == '.') {
            advance_byte(p);
            while (p->i < p->n && isdigit((unsigned char)p->src[p->i])) {
                advance_byte(p);
            }
        }
        if (p->i < p->n && (p->src[p->i] == 'e' || p->src[p->i] == 'E')) {
            size_t save = p->i;
            uint32_t savec = p->chars;
            advance_byte(p);
            if (p->i < p->n && (p->src[p->i] == '+' || p->src[p->i] == '-')) {
                advance_byte(p);
            }
            if (p->i < p->n && isdigit((unsigned char)p->src[p->i])) {
                while (p->i < p->n && isdigit((unsigned char)p->src[p->i])) {
                    advance_byte(p);
                }
            } else {
                p->i = save;
                p->chars = savec;
            }
        }
        if (p->i < p->n && ident_start((unsigned char)p->src[p->i])) {
            t->kind = T_EOF;
            t->text = "";
            fail_at(p, t->pos, GSQL_SYNTAX, "a number runs into a name: `%.*s`",
                    (int)(p->i - start + 1), p->src + start);
            return;
        }
        t->kind = T_NUMBER;
        t->len = p->i - start;
        t->text = sql_strndup(p->arena, p->src + start, t->len);
        return;
    }
    if (c == ':' && p->i + 1 < p->n && ident_start((unsigned char)p->src[p->i + 1])) {
        advance_byte(p);
        size_t s = p->i;
        while (p->i < p->n && ident_char((unsigned char)p->src[p->i])) {
            advance_byte(p);
        }
        t->kind = T_PNAME;
        t->len = p->i - s;
        t->text = sql_strndup(p->arena, p->src + s, t->len);
        return;
    }
    if (c == '$' && p->i + 1 < p->n && isdigit((unsigned char)p->src[p->i + 1])) {
        advance_byte(p);
        size_t s = p->i;
        while (p->i < p->n && isdigit((unsigned char)p->src[p->i])) {
            advance_byte(p);
        }
        t->kind = T_PNUM;
        t->len = p->i - s;
        t->text = sql_strndup(p->arena, p->src + s, t->len);
        return;
    }
    static const char *const two[] = { "<=", ">=", "<>", "!=", "||", NULL };
    for (int k = 0; two[k]; k++) {
        if (p->i + 1 < p->n && p->src[p->i] == two[k][0] && p->src[p->i + 1] == two[k][1]) {
            advance_byte(p);
            advance_byte(p);
            t->kind = T_SYM;
            t->len = 2;
            t->text = (char *)two[k];
            return;
        }
    }
    if (strchr("(),.;*+-/%=<>", c)) {
        advance_byte(p);
        t->kind = T_SYM;
        t->len = 1;
        t->text = sql_strndup(p->arena, p->src + start, 1);
        return;
    }
    t->kind = T_EOF;
    t->text = "";
    if (c == '?') {
        fail_at(p, t->pos, GSQL_SYNTAX, "`?` parameters are not supported: name one as :name, or number it as $1");
    } else if (c == '`') {
        fail_at(p, t->pos, GSQL_SYNTAX, "a backquoted name is MySQL's spelling: quote a name with \"double quotes\"");
    } else if (c == '[') {
        fail_at(p, t->pos, GSQL_SYNTAX, "a bracketed name is SQL Server's spelling: quote a name with \"double quotes\"");
    } else {
        fail_at(p, t->pos, GSQL_SYNTAX, "unexpected character `%c`", isprint(c) ? c : '?');
    }
}

/* Keywords are recognized only in unquoted names, and only these are
 * reserved: a word here cannot name a table or a column without quotes. */
static const char *const reserved[] = {
    "add", "all", "alter", "and", "as", "asc", "between", "by", "case", "cast", "column", "create",
    "default", "delete", "desc", "distinct", "drop", "else", "end", "except", "exists", "explain",
    "false", "fetch", "from", "group", "having", "if", "in", "index", "insert", "intersect", "into",
    "is", "join", "key", "like", "limit", "not", "null", "offset", "on", "or", "order", "primary",
    "select", "set", "table", "then", "true", "union", "unique", "unknown", "update", "values", "when",
    "where", "with", NULL
};

static int is_reserved(const char *s) {
    for (int k = 0; reserved[k]; k++) {
        if (strcmp(reserved[k], s) == 0) {
            return 1;
        }
    }
    return 0;
}

static int at_kw(P *p, const char *kw) {
    return p->tok.kind == T_IDENT && strcmp(p->tok.text, kw) == 0;
}

static int at_sym(P *p, const char *s) {
    return p->tok.kind == T_SYM && strcmp(p->tok.text, s) == 0;
}

static int accept_kw(P *p, const char *kw) {
    if (at_kw(p, kw)) {
        next(p);
        return 1;
    }
    return 0;
}

static int accept_sym(P *p, const char *s) {
    if (at_sym(p, s)) {
        next(p);
        return 1;
    }
    return 0;
}

static int expect_kw(P *p, const char *kw) {
    if (accept_kw(p, kw)) {
        return 1;
    }
    char want[48];
    snprintf(want, sizeof want, "%s", kw);
    for (char *q = want; *q; q++) {
        *q = (char)toupper((unsigned char)*q);
    }
    return expected(p, want);
}

static int expect_sym(P *p, const char *s) {
    if (accept_sym(p, s)) {
        return 1;
    }
    char want[16];
    snprintf(want, sizeof want, "`%s`", s);
    return expected(p, want);
}

/* A table, column or index name: an unquoted name that is not reserved, or a
 * quoted one. */
static char *name(P *p, const char *what) {
    if (p->tok.kind == T_QIDENT || (p->tok.kind == T_IDENT && !is_reserved(p->tok.text))) {
        char *s = p->tok.text;
        if (strlen(s) > 255) {
            fail_at(p, p->tok.pos, GSQL_SYNTAX, "a name may be at most 255 bytes");
            return NULL;
        }
        next(p);
        return s;
    }
    if (p->tok.kind == T_IDENT) {
        fail_at(p, p->tok.pos, GSQL_SYNTAX, "expected %s, found the reserved word `%s` (quote it as \"%s\" to use it as a name)",
                what, p->tok.text, p->tok.text);
        return NULL;
    }
    expected(p, what);
    return NULL;
}

/* Phase 2's words, refused by name rather than as a puzzle. */
static int phase2(P *p) {
    static const struct { const char *kw; const char *what; } later[] = {
        { "join", "joins" }, { "union", "UNION" },
        { "intersect", "INTERSECT" }, { "except", "EXCEPT" }, { "distinct", "DISTINCT" },
        { "with", "WITH" }, { NULL, NULL }
    };
    for (int k = 0; later[k].kw; k++) {
        if (at_kw(p, later[k].kw)) {
            return fail_at(p, p->tok.pos, GSQL_UNSUPPORTED, "%s are not in this release of G-SQL (Phase 2)",
                           later[k].what);
        }
    }
    if (p->tok.kind == T_IDENT && (strcmp(p->tok.text, "inner") == 0 || strcmp(p->tok.text, "left") == 0 ||
                                   strcmp(p->tok.text, "right") == 0 || strcmp(p->tok.text, "cross") == 0 ||
                                   strcmp(p->tok.text, "full") == 0)) {
        return fail_at(p, p->tok.pos, GSQL_UNSUPPORTED, "joins are not in this release of G-SQL (Phase 2)");
    }
    return 1;
}

/* ---- expressions ----------------------------------------------------- */

static SqlExpr *node(P *p, SqlExprKind k, uint32_t pos) {
    SqlExpr *e = sql_alloc(p->arena, sizeof *e);
    e->kind = k;
    e->pos = pos;
    return e;
}

static void kids(P *p, SqlExpr *e, size_t n) {
    e->a = sql_alloc(p->arena, sizeof(SqlExpr *) * (n ? n : 1));
    e->na = n;
}

static SqlExpr *lit_bytes(P *p, uint32_t pos, const GsqlBuf *b) {
    SqlExpr *e = node(p, SX_LIT, pos);
    e->lit = sql_alloc(p->arena, b->length);
    memcpy(e->lit, b->bytes, b->length);
    e->litlen = b->length;
    return e;
}

static SqlExpr *lit_unknown(P *p, uint32_t pos) {
    GsqlBuf b = {0};
    v3_w_unknown(&b);
    SqlExpr *e = lit_bytes(p, pos, &b);
    gsql_buf_free(&b);
    return e;
}

/* gBASIC's literal rule: digits alone are EXACT while they fit int64; a
 * fraction or an exponent is inexact (exact_number_design.md). */
static SqlExpr *lit_number(P *p) {
    const char *s = p->tok.text;
    uint32_t pos = p->tok.pos;
    GsqlBuf b = {0};
    int exact = strspn(s, "0123456789") == strlen(s);
    if (exact) {
        errno = 0;
        unsigned long long v = strtoull(s, NULL, 10);
        if (errno == 0 && v <= (unsigned long long)INT64_MAX) {
            v3_w_int(&b, (uint64_t)v);
        } else {
            exact = 0;
        }
    }
    if (!exact) {
        double d = strtod(s, NULL);
        uint64_t bits;
        memcpy(&bits, &d, sizeof bits);
        v3_u8(&b, SER_NUMBER);
        v3_u8(&b, 0);
        v3_le(&b, bits, 8);
    }
    next(p);
    SqlExpr *e = lit_bytes(p, pos, &b);
    gsql_buf_free(&b);
    return e;
}

static SqlExpr *lit_string(P *p) {
    GsqlBuf b = {0};
    v3_u8(&b, SER_STRING);
    v3_uleb(&b, p->tok.len);
    v3_put(&b, p->tok.text, p->tok.len);
    SqlExpr *e = lit_bytes(p, p->tok.pos, &b);
    gsql_buf_free(&b);
    next(p);
    return e;
}

static int parse_type(P *p, SqlType *t);
static SqlExpr *expr(P *p);

static void note_param(P *p, SqlExpr *e) {
    SqlStmt *s = p->stmt;
    if (e->name) {
        for (size_t k = 0; k < s->nparam_names; k++) {
            if (strcmp(s->param_names[k], e->name) == 0) {
                return;
            }
        }
        char **grown = sql_alloc(p->arena, sizeof(char *) * (s->nparam_names + 1));
        if (s->nparam_names) {
            memcpy(grown, s->param_names, sizeof(char *) * s->nparam_names);
        }
        grown[s->nparam_names++] = e->name;
        s->param_names = grown;
    } else if (e->index > s->max_param_index) {
        s->max_param_index = e->index;
    }
}

/* a growable list of expressions, in the arena */
typedef struct {
    SqlExpr **v;
    size_t n, cap;
} EList;

static void elist_push(P *p, EList *l, SqlExpr *e) {
    if (l->n == l->cap) {
        size_t cap = l->cap ? l->cap * 2 : 4;
        SqlExpr **v = sql_alloc(p->arena, sizeof(SqlExpr *) * cap);
        if (l->n) {
            memcpy(v, l->v, sizeof(SqlExpr *) * l->n);
        }
        l->v = v;
        l->cap = cap;
    }
    l->v[l->n++] = e;
}

static void take(SqlExpr *e, EList *l) {
    e->a = l->v;
    e->na = l->n;
}

static const char *const typed_literal_words[] = { "date", "time", "timestamp", "datetime", NULL };

static SqlExpr *primary(P *p) {
    if (p->failed) {
        return NULL;
    }
    uint32_t pos = p->tok.pos;
    switch (p->tok.kind) {
    case T_NUMBER:
        return lit_number(p);
    case T_STRING:
        return lit_string(p);
    case T_PNAME: {
        SqlExpr *e = node(p, SX_PARAM, pos);
        e->name = p->tok.text;
        note_param(p, e);
        next(p);
        return e;
    }
    case T_PNUM: {
        SqlExpr *e = node(p, SX_PARAM, pos);
        unsigned long v = strtoul(p->tok.text, NULL, 10);
        if (v == 0 || v > 65535) {
            fail_at(p, pos, GSQL_SYNTAX, "a numbered parameter runs from $1 to $65535");
            return NULL;
        }
        e->index = (unsigned)v;
        note_param(p, e);
        next(p);
        return e;
    }
    case T_SYM:
        if (accept_sym(p, "(")) {
            if (at_kw(p, "select")) {
                fail_at(p, p->tok.pos, GSQL_UNSUPPORTED, "subqueries are not in this release of G-SQL (Phase 2)");
                return NULL;
            }
            SqlExpr *e = expr(p);
            if (!expect_sym(p, ")")) {
                return NULL;
            }
            return e;
        }
        expected(p, "a value");
        return NULL;
    case T_IDENT:
        if (accept_kw(p, "null") || accept_kw(p, "unknown")) {
            return lit_unknown(p, pos);
        }
        if (accept_kw(p, "case")) {
            SqlExpr *e = node(p, SX_CASE, pos);
            if (!at_kw(p, "when")) {
                e->operand = expr(p);
            }
            EList l = {0};
            while (accept_kw(p, "when")) {
                elist_push(p, &l, expr(p));
                if (!expect_kw(p, "then")) {
                    return NULL;
                }
                elist_push(p, &l, expr(p));
            }
            if (p->failed) {
                return NULL;
            }
            if (l.n == 0) {
                expected(p, "WHEN");
                return NULL;
            }
            if (accept_kw(p, "else")) {
                elist_push(p, &l, expr(p));
                e->has_else = 1;
            }
            if (!expect_kw(p, "end")) {
                return NULL;
            }
            take(e, &l);
            return e;
        }
        if (accept_kw(p, "cast")) {
            SqlExpr *e = node(p, SX_CAST, pos);
            if (!expect_sym(p, "(")) {
                return NULL;
            }
            kids(p, e, 1);
            e->a[0] = expr(p);
            if (!expect_kw(p, "as") || !parse_type(p, &e->type) || !expect_sym(p, ")")) {
                return NULL;
            }
            return e;
        }
        if (at_kw(p, "exists")) {
            fail_at(p, pos, GSQL_UNSUPPORTED, "subqueries are not in this release of G-SQL (Phase 2)");
            return NULL;
        }
        for (int k = 0; typed_literal_words[k]; k++) {
            if (at_kw(p, typed_literal_words[k])) {
                /* DATE '2026-10-30': a typed literal, which is CAST of a string */
                size_t si = p->i;
                uint32_t sc = p->chars;
                Tok save = p->tok;
                next(p);
                if (p->tok.kind == T_STRING) {
                    SqlExpr *e = node(p, SX_CAST, pos);
                    e->type.kind = k == 0 ? GSQL_COL_DATE : k == 1 ? GSQL_COL_TIME : GSQL_COL_DATETIME;
                    kids(p, e, 1);
                    e->a[0] = lit_string(p);
                    return e;
                }
                p->i = si;
                p->chars = sc;
                p->tok = save;
            }
        }
        /* fall through: a name or a call */
        /* FALLTHROUGH */
    case T_QIDENT: {
        int quoted = p->tok.kind == T_QIDENT;
        if (!quoted && is_reserved(p->tok.text)) {
            char b[96];
            fail_at(p, pos, GSQL_SYNTAX, "expected a value, found the reserved word %s", tok_desc(p, b, sizeof b));
            return NULL;
        }
        char *first = p->tok.text;
        next(p);
        if (!quoted && at_sym(p, "(")) {
            next(p);
            SqlExpr *e = node(p, SX_CALL, pos);
            e->name = first;
            EList l = {0};
            /* count(*) and an aggregate's DISTINCT; the binder decides whether
               the name is an aggregate at all */
            if (at_sym(p, "*")) {
                if (strcmp(first, "count") != 0) {
                    fail_at(p, p->tok.pos, GSQL_SYNTAX, "only count takes *, as count(*)");
                    return NULL;
                }
                next(p);
                e->star = 1;
                if (!expect_sym(p, ")")) {
                    return NULL;
                }
                return e;
            }
            if (accept_kw(p, "distinct")) {
                e->distinct = 1;
            }
            if (!at_sym(p, ")")) {
                do {
                    elist_push(p, &l, expr(p));
                } while (!p->failed && accept_sym(p, ","));
            }
            if (!expect_sym(p, ")")) {
                return NULL;
            }
            take(e, &l);
            return e;
        }
        SqlExpr *e = node(p, SX_NAME, pos);
        char *parts[64];
        size_t n = 0;
        parts[n++] = first;
        while (at_sym(p, ".")) {
            next(p);
            if (p->tok.kind == T_IDENT || p->tok.kind == T_QIDENT) {
                /* after a dot a word can only be a field: reserved or not */
                if (n == 64) {
                    fail_at(p, p->tok.pos, GSQL_SYNTAX, "a name may have at most 64 parts");
                    return NULL;
                }
                parts[n++] = p->tok.text;
                next(p);
            } else if (at_sym(p, "*")) {
                fail_at(p, p->tok.pos, GSQL_SYNTAX, "`%s.*` stands only in a select list", first);
                return NULL;
            } else {
                expected(p, "a name after `.`");
                return NULL;
            }
        }
        e->parts = sql_alloc(p->arena, sizeof(char *) * n);
        memcpy(e->parts, parts, sizeof(char *) * n);
        e->nparts = n;
        return e;
    }
    default:
        expected(p, "a value");
        return NULL;
    }
}

static SqlExpr *unary(P *p) {
    uint32_t pos = p->tok.pos;
    if (accept_sym(p, "-")) {
        SqlExpr *e = node(p, SX_NEG, pos);
        kids(p, e, 1);
        e->a[0] = unary(p);
        return p->failed ? NULL : e;
    }
    if (accept_sym(p, "+")) {
        return unary(p);
    }
    if (at_kw(p, "true") || at_kw(p, "false")) {
        int v = at_kw(p, "true");
        next(p);
        GsqlBuf b = {0};
        v3_w_bool(&b, v);
        SqlExpr *e = lit_bytes(p, pos, &b);
        gsql_buf_free(&b);
        return e;
    }
    return primary(p);
}

static SqlExpr *binary(P *p, int op, SqlExpr *l, SqlExpr *r, uint32_t pos) {
    if (p->failed || !l || !r) {
        return NULL;
    }
    SqlExpr *e = node(p, SX_BIN, pos);
    e->op = op;
    kids(p, e, 2);
    e->a[0] = l;
    e->a[1] = r;
    return e;
}

static SqlExpr *mul(P *p) {
    SqlExpr *l = unary(p);
    for (;;) {
        uint32_t pos = p->tok.pos;
        int op = at_sym(p, "*") ? SQL_OP_MUL : at_sym(p, "/") ? SQL_OP_DIV : at_sym(p, "%") ? SQL_OP_MOD : 0;
        if (!op || p->failed) {
            return l;
        }
        next(p);
        l = binary(p, op, l, unary(p), pos);
    }
}

static SqlExpr *add(P *p) {
    SqlExpr *l = mul(p);
    for (;;) {
        uint32_t pos = p->tok.pos;
        int op = at_sym(p, "+") ? SQL_OP_ADD : at_sym(p, "-") ? SQL_OP_SUB : 0;
        if (!op || p->failed) {
            return l;
        }
        next(p);
        l = binary(p, op, l, mul(p), pos);
    }
}

static SqlExpr *concat(P *p) {
    SqlExpr *l = add(p);
    while (!p->failed && at_sym(p, "||")) {
        uint32_t pos = p->tok.pos;
        next(p);
        l = binary(p, SQL_OP_CONCAT, l, add(p), pos);
    }
    return l;
}

static int cmp_op(P *p) {
    if (p->tok.kind != T_SYM) {
        return 0;
    }
    const char *s = p->tok.text;
    if (!strcmp(s, "=")) return SQL_OP_EQ;
    if (!strcmp(s, "<>") || !strcmp(s, "!=")) return SQL_OP_NE;
    if (!strcmp(s, "<")) return SQL_OP_LT;
    if (!strcmp(s, "<=")) return SQL_OP_LE;
    if (!strcmp(s, ">")) return SQL_OP_GT;
    if (!strcmp(s, ">=")) return SQL_OP_GE;
    return 0;
}

static SqlExpr *comparison(P *p) {
    SqlExpr *l = concat(p);
    if (p->failed) {
        return NULL;
    }
    uint32_t pos = p->tok.pos;
    int op = cmp_op(p);
    SqlExpr *e = NULL;
    if (op) {
        next(p);
        e = binary(p, op, l, concat(p), pos);
    } else if (accept_kw(p, "is")) {
        int neg = accept_kw(p, "not");
        if (!accept_kw(p, "null") && !accept_kw(p, "unknown")) {
            if (at_kw(p, "distinct")) {
                fail_at(p, p->tok.pos, GSQL_UNSUPPORTED, "IS DISTINCT FROM is not in this release of G-SQL");
                return NULL;
            }
            expected(p, "NULL or UNKNOWN after IS");
            return NULL;
        }
        e = node(p, SX_ISNULL, pos);
        e->neg = neg;
        kids(p, e, 1);
        e->a[0] = l;
    } else {
        int neg = 0;
        if (at_kw(p, "not")) {
            neg = 1;
            next(p);
            if (!at_kw(p, "in") && !at_kw(p, "between") && !at_kw(p, "like")) {
                expected(p, "IN, BETWEEN or LIKE after NOT");
                return NULL;
            }
        }
        if (accept_kw(p, "in")) {
            e = node(p, SX_IN, pos);
            e->neg = neg;
            if (!expect_sym(p, "(")) {
                return NULL;
            }
            if (at_kw(p, "select")) {
                fail_at(p, p->tok.pos, GSQL_UNSUPPORTED, "subqueries are not in this release of G-SQL (Phase 2)");
                return NULL;
            }
            EList list = {0};
            elist_push(p, &list, l);
            do {
                elist_push(p, &list, expr(p));
            } while (!p->failed && accept_sym(p, ","));
            if (!expect_sym(p, ")")) {
                return NULL;
            }
            take(e, &list);
        } else if (accept_kw(p, "between")) {
            e = node(p, SX_BETWEEN, pos);
            e->neg = neg;
            kids(p, e, 3);
            e->a[0] = l;
            e->a[1] = concat(p);
            if (!expect_kw(p, "and")) {
                return NULL;
            }
            e->a[2] = concat(p);
        } else if (accept_kw(p, "like")) {
            e = node(p, SX_LIKE, pos);
            e->neg = neg;
            kids(p, e, 2);
            e->a[0] = l;
            e->a[1] = concat(p);
        } else {
            return l;
        }
    }
    if (p->failed) {
        return NULL;
    }
    if (cmp_op(p) || at_kw(p, "is") || at_kw(p, "in") || at_kw(p, "between") || at_kw(p, "like")) {
        fail_at(p, p->tok.pos, GSQL_SYNTAX, "comparisons do not chain: join two with AND, or parenthesise one");
        return NULL;
    }
    return e;
}

static SqlExpr *negation(P *p) {
    uint32_t pos = p->tok.pos;
    if (accept_kw(p, "not")) {
        SqlExpr *e = node(p, SX_NOT, pos);
        kids(p, e, 1);
        e->a[0] = negation(p);
        return p->failed ? NULL : e;
    }
    return comparison(p);
}

static SqlExpr *conjunction(P *p) {
    SqlExpr *l = negation(p);
    while (!p->failed && at_kw(p, "and")) {
        uint32_t pos = p->tok.pos;
        next(p);
        l = binary(p, SQL_OP_AND, l, negation(p), pos);
    }
    return l;
}

static SqlExpr *expr(P *p) {
    if (p->failed) {
        return NULL;
    }
    SqlExpr *l = conjunction(p);
    while (!p->failed && at_kw(p, "or")) {
        uint32_t pos = p->tok.pos;
        next(p);
        l = binary(p, SQL_OP_OR, l, conjunction(p), pos);
    }
    return p->failed ? NULL : l;
}

/* ---- types ----------------------------------------------------------- */

static int parse_type(P *p, SqlType *t) {
    memset(t, 0, sizeof *t);
    uint32_t pos = p->tok.pos;
    if (p->tok.kind != T_IDENT) {
        return expected(p, "a type");
    }
    char w[32];
    snprintf(w, sizeof w, "%s", p->tok.text);
    next(p);
    static const struct { const char *word; GsqlColKind kind; } words[] = {
        { "number", GSQL_COL_NUMBER }, { "numeric", GSQL_COL_NUMBER }, { "real", GSQL_COL_NUMBER },
        { "float", GSQL_COL_NUMBER }, { "integer", GSQL_COL_INTEGER }, { "int", GSQL_COL_INTEGER },
        { "int64", GSQL_COL_INTEGER }, { "bigint", GSQL_COL_INTEGER }, { "text", GSQL_COL_STRING },
        { "string", GSQL_COL_STRING }, { "varchar", GSQL_COL_STRING }, { "boolean", GSQL_COL_BOOLEAN },
        { "bool", GSQL_COL_BOOLEAN }, { "money", GSQL_COL_MONEY }, { "date", GSQL_COL_DATE },
        { "time", GSQL_COL_TIME }, { "datetime", GSQL_COL_DATETIME }, { "timestamp", GSQL_COL_DATETIME },
        { "record", GSQL_COL_RECORD }, { NULL, 0 }
    };
    if (strcmp(w, "double") == 0) {
        if (!accept_kw(p, "precision")) {
            return expected(p, "PRECISION after DOUBLE");
        }
        t->kind = GSQL_COL_NUMBER;
        return 1;
    }
    for (int k = 0; words[k].word; k++) {
        if (strcmp(words[k].word, w) == 0) {
            t->kind = words[k].kind;
        }
    }
    if (!t->kind) {
        return fail_at(p, pos, GSQL_SYNTAX,
                       "`%s` is not a G-SQL type: the types are NUMBER, INTEGER, TEXT, BOOLEAN, MONEY, MONEY(CUR), "
                       "DATE, TIME, DATETIME and RECORD", w);
    }
    if (at_sym(p, "(")) {
        uint32_t at = p->tok.pos;
        if (t->kind == GSQL_COL_MONEY) {
            next(p);
            if (p->tok.kind != T_IDENT || p->tok.len != 3) {
                return expected(p, "a three-letter currency code");
            }
            for (int k = 0; k < 3; k++) {
                t->currency[k] = (char)toupper((unsigned char)p->tok.text[k]);
            }
            next(p);
            return expect_sym(p, ")");
        }
        if (t->kind == GSQL_COL_STRING || t->kind == GSQL_COL_NUMBER) {
            return fail_at(p, at, GSQL_UNSUPPORTED,
                           "a %s length or precision is not enforced by G-SQL, so it is refused rather than ignored: "
                           "write %s alone", w, t->kind == GSQL_COL_STRING ? "TEXT" : "NUMBER");
        }
        return fail_at(p, at, GSQL_SYNTAX, "%s takes no arguments", w);
    }
    if (t->kind == GSQL_COL_DATETIME && strcmp(w, "timestamp") == 0 && at_kw(p, "with")) {
        return fail_at(p, p->tok.pos, GSQL_UNSUPPORTED, "a time zone in a column type is not in this release of G-SQL");
    }
    return 1;
}

const char *sql_type_name(const SqlType *t, char *buf, size_t cap) {
    static const char *const names[] = {
        "?", "NUMBER", "INTEGER", "TEXT", "BOOLEAN", "MONEY", "DATE", "TIME", "DATETIME", "RECORD"
    };
    const char *n = t->kind >= 1 && t->kind <= GSQL_COL_RECORD ? names[t->kind] : "?";
    if (t->kind == GSQL_COL_MONEY && t->currency[0]) {
        snprintf(buf, cap, "MONEY(%s)", t->currency);
    } else {
        snprintf(buf, cap, "%s", n);
    }
    return buf;
}

/* ---- statements ------------------------------------------------------ */

static int name_list(P *p, SqlNameList *l, int allow_order) {
    l->pos = p->tok.pos;
    if (!expect_sym(p, "(")) {
        return 0;
    }
    char *cols[256];
    int desc[256];
    size_t n = 0;
    do {
        if (n == 256) {
            return fail_at(p, p->tok.pos, GSQL_SYNTAX, "a column list may have at most 256 columns");
        }
        cols[n] = name(p, "a column name");
        if (!cols[n]) {
            return 0;
        }
        desc[n] = 0;
        if (allow_order) {
            if (accept_kw(p, "desc")) {
                desc[n] = 1;
            } else {
                accept_kw(p, "asc");
            }
        }
        n++;
    } while (accept_sym(p, ","));
    if (!expect_sym(p, ")")) {
        return 0;
    }
    l->cols = sql_alloc(p->arena, sizeof(char *) * n);
    l->desc = sql_alloc(p->arena, sizeof(int) * n);
    memcpy(l->cols, cols, sizeof(char *) * n);
    memcpy(l->desc, desc, sizeof(int) * n);
    l->n = n;
    return 1;
}

static int coldef(P *p, SqlColDef *c) {
    c->pos = p->tok.pos;
    c->name = name(p, "a column name");
    if (!c->name || !parse_type(p, &c->type)) {
        return 0;
    }
    for (;;) {
        uint32_t pos = p->tok.pos;
        if (accept_kw(p, "not")) {
            if (!expect_kw(p, "null")) {
                return 0;
            }
            c->not_null = 1;
        } else if (accept_kw(p, "null")) {
            /* the default, said aloud */
        } else if (accept_kw(p, "default")) {
            c->def = concat(p);
            if (!c->def) {
                return 0;
            }
        } else if (accept_kw(p, "primary")) {
            if (!expect_kw(p, "key")) {
                return 0;
            }
            c->primary = 1;
        } else if (accept_kw(p, "unique")) {
            c->unique = 1;
        } else if (at_kw(p, "references") || at_kw(p, "check")) {
            return fail_at(p, pos, GSQL_UNSUPPORTED, "%s constraints are in the next release of G-SQL",
                           at_kw(p, "check") ? "CHECK" : "FOREIGN KEY");
        } else {
            return 1;
        }
    }
}

static int select_stmt(P *p, SqlStmt *s) {
    if (!phase2(p)) {
        return 0;
    }
    SqlItem items[1024];
    size_t n = 0;
    do {
        if (n == 1024) {
            return fail_at(p, p->tok.pos, GSQL_SYNTAX, "a select list may have at most 1024 items");
        }
        SqlItem *it = &items[n];
        memset(it, 0, sizeof *it);
        it->pos = p->tok.pos;
        if (accept_sym(p, "*")) {
            n++;
            continue;
        }
        /* t.* */
        if ((p->tok.kind == T_IDENT || p->tok.kind == T_QIDENT) && p->i < p->n) {
            size_t si = p->i;
            uint32_t sc = p->chars;
            Tok save = p->tok;
            int reserved_word = p->tok.kind == T_IDENT && is_reserved(p->tok.text);
            next(p);
            if (!reserved_word && at_sym(p, ".")) {
                next(p);
                if (at_sym(p, "*")) {
                    next(p);
                    it->star_table = save.text;
                    n++;
                    continue;
                }
            }
            p->i = si;
            p->chars = sc;
            p->tok = save;
        }
        it->e = expr(p);
        if (!it->e) {
            return 0;
        }
        if (accept_kw(p, "as")) {
            it->alias = name(p, "a column alias");
            if (!it->alias) {
                return 0;
            }
        } else if (p->tok.kind == T_QIDENT || (p->tok.kind == T_IDENT && !is_reserved(p->tok.text))) {
            it->alias = name(p, "a column alias");
        }
        n++;
    } while (accept_sym(p, ","));
    if (p->failed) {
        return 0;
    }
    s->items = sql_alloc(p->arena, sizeof(SqlItem) * n);
    memcpy(s->items, items, sizeof(SqlItem) * n);
    s->nitems = n;
    if (!phase2(p)) {
        return 0;
    }
    if (accept_kw(p, "from")) {
        if (at_sym(p, "(")) {
            return fail_at(p, p->tok.pos, GSQL_UNSUPPORTED, "subqueries are not in this release of G-SQL (Phase 2)");
        }
        s->table = name(p, "a table name");
        if (!s->table) {
            return 0;
        }
        if (accept_kw(p, "as")) {
            s->alias = name(p, "a table alias");
            if (!s->alias) {
                return 0;
            }
        } else if (p->tok.kind == T_QIDENT || (p->tok.kind == T_IDENT && !is_reserved(p->tok.text) &&
                                                strcmp(p->tok.text, "inner") && strcmp(p->tok.text, "left") &&
                                                strcmp(p->tok.text, "right") && strcmp(p->tok.text, "cross") &&
                                                strcmp(p->tok.text, "full"))) {
            s->alias = name(p, "a table alias");
        }
        if (at_sym(p, ",")) {
            return fail_at(p, p->tok.pos, GSQL_UNSUPPORTED, "joins are not in this release of G-SQL (Phase 2)");
        }
        if (!phase2(p)) {
            return 0;
        }
    }
    if (accept_kw(p, "where")) {
        s->where = expr(p);
        if (!s->where) {
            return 0;
        }
    }
    if (accept_kw(p, "group")) {
        if (!expect_kw(p, "by")) {
            return 0;
        }
        SqlExpr *g[256];
        size_t ng = 0;
        do {
            if (ng == 256) {
                return fail_at(p, p->tok.pos, GSQL_SYNTAX, "GROUP BY may have at most 256 terms");
            }
            if (!(g[ng++] = expr(p))) {
                return 0;
            }
        } while (accept_sym(p, ","));
        s->group = sql_alloc(p->arena, sizeof(SqlExpr *) * ng);
        memcpy(s->group, g, sizeof(SqlExpr *) * ng);
        s->ngroup = ng;
    }
    if (accept_kw(p, "having")) {
        if (!(s->having = expr(p))) {
            return 0;
        }
    }
    if (!phase2(p)) {
        return 0;
    }
    if (accept_kw(p, "order")) {
        if (!expect_kw(p, "by")) {
            return 0;
        }
        SqlOrder ord[256];
        size_t no = 0;
        do {
            if (no == 256) {
                return fail_at(p, p->tok.pos, GSQL_SYNTAX, "ORDER BY may have at most 256 terms");
            }
            ord[no].pos = p->tok.pos;
            ord[no].e = expr(p);
            if (!ord[no].e) {
                return 0;
            }
            ord[no].desc = 0;
            if (accept_kw(p, "desc")) {
                ord[no].desc = 1;
            } else {
                accept_kw(p, "asc");
            }
            if (p->tok.kind == T_IDENT && strcmp(p->tok.text, "nulls") == 0) {
                return fail_at(p, p->tok.pos, GSQL_UNSUPPORTED,
                               "NULLS FIRST and NULLS LAST are not in this release: NULL sorts last ascending "
                               "and first descending, as an index stores it");
            }
            no++;
        } while (accept_sym(p, ","));
        s->order = sql_alloc(p->arena, sizeof(SqlOrder) * no);
        memcpy(s->order, ord, sizeof(SqlOrder) * no);
        s->norder = no;
    }
    if (accept_kw(p, "limit")) {
        if (!(s->limit = concat(p))) {
            return 0;
        }
        if (accept_kw(p, "offset")) {
            if (!(s->offset = concat(p))) {
                return 0;
            }
        }
    } else {
        if (accept_kw(p, "offset")) {
            if (!(s->offset = concat(p))) {
                return 0;
            }
            if (p->tok.kind == T_IDENT && (strcmp(p->tok.text, "row") == 0 || strcmp(p->tok.text, "rows") == 0)) {
                next(p);
            }
        }
        if (accept_kw(p, "fetch")) {
            if (!(p->tok.kind == T_IDENT && (strcmp(p->tok.text, "first") == 0 || strcmp(p->tok.text, "next") == 0))) {
                return expected(p, "FIRST or NEXT after FETCH");
            }
            next(p);
            uint32_t at = p->tok.pos;
            if (p->tok.kind == T_IDENT && (strcmp(p->tok.text, "row") == 0 || strcmp(p->tok.text, "rows") == 0)) {
                GsqlBuf b = {0};
                v3_w_int(&b, 1);
                s->limit = lit_bytes(p, at, &b);
                gsql_buf_free(&b);
            } else if (!(s->limit = concat(p))) {
                return 0;
            }
            if (!(p->tok.kind == T_IDENT && (strcmp(p->tok.text, "row") == 0 || strcmp(p->tok.text, "rows") == 0))) {
                return expected(p, "ROW or ROWS");
            }
            next(p);
            if (!(p->tok.kind == T_IDENT && strcmp(p->tok.text, "only") == 0)) {
                return expected(p, "ONLY");
            }
            next(p);
        }
    }
    return 1;
}

/* VALUES (...) or SET x = ...: DEFAULT stands for the column's default */
static SqlExpr *value_or_default(P *p) {
    if (accept_kw(p, "default")) {
        return NULL;   /* NULL with no failure: the default */
    }
    return expr(p);
}

static int insert_stmt(P *p, SqlStmt *s) {
    if (!expect_kw(p, "into")) {
        return 0;
    }
    s->table = name(p, "a table name");
    if (!s->table) {
        return 0;
    }
    if (at_sym(p, "(") && !name_list(p, &s->cols, 0)) {
        return 0;
    }
    if (at_kw(p, "select")) {
        return fail_at(p, p->tok.pos, GSQL_UNSUPPORTED, "INSERT ... SELECT is not in this release of G-SQL (Phase 2)");
    }
    if (!expect_kw(p, "values")) {
        return 0;
    }
    SqlExpr ***rows = NULL;
    size_t nrows = 0, cap = 0, width = 0;
    do {
        uint32_t rpos = p->tok.pos;
        if (!expect_sym(p, "(")) {
            return 0;
        }
        EList l = {0};
        do {
            SqlExpr *e = value_or_default(p);
            if (p->failed) {
                return 0;
            }
            /* a NULL slot is DEFAULT: an EList cannot hold one, so wrap */
            elist_push(p, &l, e);
        } while (accept_sym(p, ","));
        if (!expect_sym(p, ")")) {
            return 0;
        }
        if (nrows == 0) {
            width = l.n;
        } else if (l.n != width) {
            return fail_at(p, rpos, GSQL_SYNTAX, "row %zu of VALUES has %zu values and row 1 has %zu", nrows + 1, l.n,
                           width);
        }
        if (nrows == cap) {
            cap = cap ? cap * 2 : 4;
            SqlExpr ***grown = sql_alloc(p->arena, sizeof(SqlExpr **) * cap);
            if (nrows) {
                memcpy(grown, rows, sizeof(SqlExpr **) * nrows);
            }
            rows = grown;
        }
        rows[nrows++] = l.v;
    } while (accept_sym(p, ","));
    s->rows = rows;
    s->nrows = nrows;
    s->rowlen = width;
    if (at_kw(p, "on") || (p->tok.kind == T_IDENT && strcmp(p->tok.text, "returning") == 0)) {
        return fail_at(p, p->tok.pos, GSQL_UNSUPPORTED, "%s is not in this release of G-SQL",
                       at_kw(p, "on") ? "ON CONFLICT" : "RETURNING");
    }
    return 1;
}

static int table_and_alias(P *p, SqlStmt *s) {
    s->table = name(p, "a table name");
    if (!s->table) {
        return 0;
    }
    if (accept_kw(p, "as")) {
        s->alias = name(p, "a table alias");
        return s->alias != NULL;
    }
    if (p->tok.kind == T_QIDENT || (p->tok.kind == T_IDENT && !is_reserved(p->tok.text))) {
        s->alias = name(p, "a table alias");
    }
    return 1;
}

static int update_stmt(P *p, SqlStmt *s) {
    if (!table_and_alias(p, s) || !expect_kw(p, "set")) {
        return 0;
    }
    char *cols[1024];
    SqlExpr *vals[1024];
    size_t n = 0;
    s->cols.pos = p->tok.pos;
    do {
        if (n == 1024) {
            return fail_at(p, p->tok.pos, GSQL_SYNTAX, "SET may assign at most 1024 columns");
        }
        cols[n] = name(p, "a column name");
        if (!cols[n] || !expect_sym(p, "=")) {
            return 0;
        }
        vals[n] = value_or_default(p);
        if (p->failed) {
            return 0;
        }
        n++;
    } while (accept_sym(p, ","));
    s->cols.cols = sql_alloc(p->arena, sizeof(char *) * n);
    memcpy(s->cols.cols, cols, sizeof(char *) * n);
    s->cols.n = n;
    s->sets = sql_alloc(p->arena, sizeof(SqlExpr *) * n);
    memcpy(s->sets, vals, sizeof(SqlExpr *) * n);
    if (at_kw(p, "from")) {
        return fail_at(p, p->tok.pos, GSQL_UNSUPPORTED, "UPDATE ... FROM is a join, not in this release of G-SQL");
    }
    if (accept_kw(p, "where")) {
        s->where = expr(p);
        if (!s->where) {
            return 0;
        }
    }
    return 1;
}

static int delete_stmt(P *p, SqlStmt *s) {
    if (!expect_kw(p, "from") || !table_and_alias(p, s)) {
        return 0;
    }
    if (accept_kw(p, "where")) {
        s->where = expr(p);
        if (!s->where) {
            return 0;
        }
    }
    return 1;
}

static int create_stmt(P *p, SqlStmt *s) {
    if (accept_kw(p, "table")) {
        s->kind = SQ_CREATE_TABLE;
        if (accept_kw(p, "if")) {
            if (!expect_kw(p, "not") || !expect_kw(p, "exists")) {
                return 0;
            }
            s->if_exists = 1;
        }
        s->table = name(p, "a table name");
        if (!s->table || !expect_sym(p, "(")) {
            return 0;
        }
        SqlColDef defs[1024];
        size_t n = 0;
        SqlNameList uniq[64];
        size_t nu = 0;
        do {
            uint32_t pos = p->tok.pos;
            if (accept_kw(p, "primary")) {
                if (!expect_kw(p, "key")) {
                    return 0;
                }
                if (s->primary.n) {
                    return fail_at(p, pos, GSQL_SYNTAX, "a table has one PRIMARY KEY");
                }
                if (!name_list(p, &s->primary, 0)) {
                    return 0;
                }
                s->primary.pos = pos;
            } else if (accept_kw(p, "unique")) {
                if (nu == 64) {
                    return fail_at(p, pos, GSQL_SYNTAX, "a table may declare at most 64 UNIQUE constraints");
                }
                if (!name_list(p, &uniq[nu], 0)) {
                    return 0;
                }
                uniq[nu++].pos = pos;
            } else if (p->tok.kind == T_IDENT && (strcmp(p->tok.text, "foreign") == 0 ||
                                                  strcmp(p->tok.text, "check") == 0 ||
                                                  strcmp(p->tok.text, "constraint") == 0)) {
                return fail_at(p, pos, GSQL_UNSUPPORTED, "%s is in the next release of G-SQL",
                               strcmp(p->tok.text, "foreign") == 0 ? "FOREIGN KEY" :
                               strcmp(p->tok.text, "check") == 0 ? "CHECK" : "a named CONSTRAINT");
            } else {
                if (n == 1024) {
                    return fail_at(p, pos, GSQL_SYNTAX, "a table may have at most 1024 columns");
                }
                memset(&defs[n], 0, sizeof defs[n]);
                if (!coldef(p, &defs[n])) {
                    return 0;
                }
                n++;
            }
        } while (accept_sym(p, ","));
        if (!expect_sym(p, ")")) {
            return 0;
        }
        s->coldefs = sql_alloc(p->arena, sizeof(SqlColDef) * (n ? n : 1));
        memcpy(s->coldefs, defs, sizeof(SqlColDef) * n);
        s->ncoldefs = n;
        s->uniques = sql_alloc(p->arena, sizeof(SqlNameList) * (nu ? nu : 1));
        memcpy(s->uniques, uniq, sizeof(SqlNameList) * nu);
        s->nuniques = nu;
        return 1;
    }
    int unique = accept_kw(p, "unique");
    if (accept_kw(p, "index")) {
        s->kind = SQ_CREATE_INDEX;
        s->unique = unique;
        if (accept_kw(p, "if")) {
            if (!expect_kw(p, "not") || !expect_kw(p, "exists")) {
                return 0;
            }
            s->if_exists = 1;
        }
        s->index = name(p, "an index name");
        if (!s->index || !expect_kw(p, "on")) {
            return 0;
        }
        s->table = name(p, "a table name");
        if (!s->table) {
            return 0;
        }
        return name_list(p, &s->index_cols, 1);
    }
    if (unique) {
        return expected(p, "INDEX after CREATE UNIQUE");
    }
    if (p->tok.kind == T_IDENT && strcmp(p->tok.text, "view") == 0) {
        return fail_at(p, p->tok.pos, GSQL_UNSUPPORTED, "views are not in this release of G-SQL (Phase 2)");
    }
    return expected(p, "TABLE or INDEX after CREATE");
}

static int drop_stmt(P *p, SqlStmt *s) {
    int table = accept_kw(p, "table");
    if (!table && !accept_kw(p, "index")) {
        return expected(p, "TABLE or INDEX after DROP");
    }
    s->kind = table ? SQ_DROP_TABLE : SQ_DROP_INDEX;
    if (accept_kw(p, "if")) {
        if (!expect_kw(p, "exists")) {
            return 0;
        }
        s->if_exists = 1;
    }
    if (table) {
        s->table = name(p, "a table name");
        return s->table != NULL;
    }
    s->index = name(p, "an index name");
    return s->index != NULL;
}

static int alter_stmt(P *p, SqlStmt *s) {
    if (!expect_kw(p, "table")) {
        return 0;
    }
    s->table = name(p, "a table name");
    if (!s->table) {
        return 0;
    }
    if (!accept_kw(p, "add")) {
        if (at_kw(p, "drop") || (p->tok.kind == T_IDENT && (strcmp(p->tok.text, "rename") == 0 ||
                                                                 strcmp(p->tok.text, "alter") == 0))) {
            return fail_at(p, p->tok.pos, GSQL_UNSUPPORTED, "ALTER TABLE ... ADD COLUMN is the only change in this "
                           "release of G-SQL");
        }
        return expected(p, "ADD after ALTER TABLE name");
    }
    s->kind = SQ_ADD_COLUMN;
    accept_kw(p, "column");
    s->coldefs = sql_alloc(p->arena, sizeof(SqlColDef));
    s->ncoldefs = 1;
    if (!coldef(p, &s->coldefs[0])) {
        return 0;
    }
    if (s->coldefs[0].primary || s->coldefs[0].unique) {
        return fail_at(p, s->coldefs[0].pos, GSQL_UNSUPPORTED,
                       "ADD COLUMN cannot declare PRIMARY KEY or UNIQUE: create the index afterwards");
    }
    return 1;
}

int sql_parse(const char *text, size_t length, SqlParsed *out, GsqlError *err) {
    memset(out, 0, sizeof *out);
    P p;
    memset(&p, 0, sizeof p);
    p.src = text;
    p.n = length;
    p.arena = &out->arena;
    p.err = err;
    SqlStmt *s = sql_alloc(&out->arena, sizeof *s);
    p.stmt = s;
    if (memchr(text, 0, length)) {
        fail_at(&p, 0, GSQL_SYNTAX, "SQL text may not hold a NUL byte");
    }
    next(&p);
    s->pos = p.tok.pos;
    if (accept_kw(&p, "explain")) {
        s->explain = 1;
    }
    int ok = 0;
    if (p.failed) {
        ok = 0;
    } else if (accept_kw(&p, "select")) {
        s->kind = SQ_SELECT;
        ok = select_stmt(&p, s);
    } else if (s->explain && !at_kw(&p, "update") && !at_kw(&p, "delete")) {
        ok = expected(&p, "SELECT, UPDATE or DELETE after EXPLAIN");
    } else if (accept_kw(&p, "insert")) {
        s->kind = SQ_INSERT;
        ok = insert_stmt(&p, s);
    } else if (accept_kw(&p, "update")) {
        s->kind = SQ_UPDATE;
        ok = update_stmt(&p, s);
    } else if (accept_kw(&p, "delete")) {
        s->kind = SQ_DELETE;
        ok = delete_stmt(&p, s);
    } else if (accept_kw(&p, "create")) {
        ok = create_stmt(&p, s);
    } else if (accept_kw(&p, "drop")) {
        ok = drop_stmt(&p, s);
    } else if (accept_kw(&p, "alter")) {
        ok = alter_stmt(&p, s);
    } else if (p.tok.kind == T_EOF) {
        ok = fail_at(&p, p.tok.pos, GSQL_SYNTAX, "the statement is empty");
    } else if (at_kw(&p, "with")) {
        ok = phase2(&p);
    } else if (p.tok.kind == T_IDENT && (strcmp(p.tok.text, "begin") == 0 || strcmp(p.tok.text, "commit") == 0 ||
                                         strcmp(p.tok.text, "rollback") == 0 || strcmp(p.tok.text, "start") == 0)) {
        ok = fail_at(&p, p.tok.pos, GSQL_UNSUPPORTED,
                     "transactions are begun and ended by the protocol's BEGIN, COMMIT and ROLLBACK, not by SQL text");
    } else {
        ok = expected(&p, "a statement (SELECT, INSERT, UPDATE, DELETE, CREATE, DROP, ALTER or EXPLAIN)");
    }
    if (ok && !p.failed) {
        accept_sym(&p, ";");
        if (p.tok.kind != T_EOF) {
            if (!p.failed) {
                char b[96];
                static const char *const starts[] = { "select", "insert", "update", "delete", "create",
                                                      "drop", "alter", "explain", NULL };
                int another = at_sym(&p, ";");
                for (int k = 0; starts[k]; k++) {
                    another |= at_kw(&p, starts[k]);
                }
                fail_at(&p, p.tok.pos, GSQL_SYNTAX, another
                        ? "one statement per request: found %s after the statement"
                        : "unexpected %s after the statement", tok_desc(&p, b, sizeof b));
            }
        }
    }
    if (!ok || p.failed) {
        if (!p.failed) {
            fail_at(&p, p.tok.pos, GSQL_SYNTAX, "the statement does not parse");
        }
        sql_arena_free(&out->arena);
        out->stmt = NULL;
        return err->code;
    }
    out->stmt = s;
    err->code = GSQL_OK;
    err->message[0] = 0;
    err->position = 0;
    return GSQL_OK;
}

/* ---- printing -------------------------------------------------------- */

const char *sql_op_text(int op) {
    static const char *const t[] = { "?", "OR", "AND", "=", "<>", "<", "<=", ">", ">=", "||", "+", "-", "*", "/", "%" };
    return op >= 1 && op <= SQL_OP_MOD ? t[op] : "?";
}

static void puts_(GsqlBuf *o, const char *s) {
    v3_put(o, s, strlen(s));
}

static void print_name(GsqlBuf *o, const char *n) {
    /* quoted where the unquoted form would read differently */
    int plain = ident_start((unsigned char)n[0]) && !is_reserved(n);
    for (const char *q = n; *q && plain; q++) {
        if (!ident_char((unsigned char)*q) || isupper((unsigned char)*q)) {
            plain = 0;
        }
    }
    if (plain) {
        puts_(o, n);
        return;
    }
    v3_put(o, "\"", 1);
    for (const char *q = n; *q; q++) {
        v3_put(o, q, 1);
        if (*q == '"') {
            v3_put(o, "\"", 1);
        }
    }
    v3_put(o, "\"", 1);
}

static void print_lit(GsqlBuf *o, const unsigned char *v, size_t n) {
    char b[64];
    switch (v[0]) {
    case SER_UNKNOWN:
        puts_(o, "NULL");
        return;
    case SER_BOOL:
        puts_(o, v[1] ? "TRUE" : "FALSE");
        return;
    case SER_NUMBER: {
        uint64_t bits = 0;
        for (int k = 0; k < 8; k++) {
            bits |= (uint64_t)v[2 + k] << (8 * k);
        }
        if (v[1]) {
            snprintf(b, sizeof b, "%lld", (long long)bits);
        } else {
            double d;
            memcpy(&d, &bits, sizeof d);
            snprintf(b, sizeof b, "%.17g", d);
            if (!strpbrk(b, ".eEni")) {
                strcat(b, ".0");
            }
        }
        puts_(o, b);
        return;
    }
    case SER_STRING: {
        V3In in = { v, n, 1, 1 };
        uint64_t len = v3_in_uleb(&in);
        v3_put(o, "'", 1);
        for (uint64_t k = 0; k < len; k++) {
            char c = (char)v[in.pos + k];
            v3_put(o, &c, 1);
            if (c == '\'') {
                v3_put(o, "'", 1);
            }
        }
        v3_put(o, "'", 1);
        return;
    }
    default:
        puts_(o, "<value>");
    }
}

/* How a bound column is named: by its table's column name, and where a
 * statement reads several tables, qualified by the table's alias or name. */
typedef struct {
    const GsqlTable *tb;
    const SqlScan *scans;
    size_t nscans;
} PrintCtx;

static void print_expr_tb(GsqlBuf *o, const SqlExpr *e, const PrintCtx *pc);

void sql_print_expr(GsqlBuf *o, const SqlExpr *e) {
    PrintCtx pc = { NULL, NULL, 0 };
    print_expr_tb(o, e, &pc);
}

void sql_print_expr_tb(GsqlBuf *o, const SqlExpr *e, const GsqlTable *tb) {
    PrintCtx pc = { tb, NULL, 0 };
    print_expr_tb(o, e, &pc);
}

void sql_print_expr_scans(GsqlBuf *o, const SqlExpr *e, const SqlScan *scans, size_t nscans) {
    PrintCtx pc = { nscans ? scans[0].tb : NULL, scans, nscans };
    print_expr_tb(o, e, &pc);
}

static void print_expr_tb(GsqlBuf *o, const SqlExpr *e, const PrintCtx *pc) {
    char b[64];
    if (!e) {
        puts_(o, "DEFAULT");
        return;
    }
    switch (e->kind) {
    case SX_LIT:
        print_lit(o, e->lit, e->litlen);
        return;
    case SX_PARAM:
        if (e->name) {
            v3_put(o, ":", 1);
            puts_(o, e->name);
        } else {
            snprintf(b, sizeof b, "$%u", e->index);
            puts_(o, b);
        }
        return;
    case SX_NAME:
        for (size_t k = 0; k < e->nparts; k++) {
            if (k) {
                v3_put(o, ".", 1);
            }
            print_name(o, e->parts[k]);
        }
        return;
    case SX_COL: {
        const GsqlTable *tb = pc->scans && e->tab < pc->nscans ? pc->scans[e->tab].tb : pc->tb;
        if (pc->nscans > 1 && e->tab < pc->nscans) {
            print_name(o, pc->scans[e->tab].qualifier);
            v3_put(o, ".", 1);
        }
        if (tb && e->col < gsql_table_ncols(tb)) {
            print_name(o, gsql_table_col(tb, e->col)->name);
        } else {
            snprintf(b, sizeof b, "#%u", e->col);
            puts_(o, b);
        }
        for (size_t k = 0; k < e->nfields; k++) {
            v3_put(o, ".", 1);
            print_name(o, e->fields[k]);
        }
        return;
    }
    case SX_BIN:
        v3_put(o, "(", 1);
        print_expr_tb(o, e->a[0], pc);
        v3_put(o, " ", 1);
        puts_(o, sql_op_text(e->op));
        v3_put(o, " ", 1);
        print_expr_tb(o, e->a[1], pc);
        v3_put(o, ")", 1);
        return;
    case SX_NOT:
        puts_(o, "(NOT ");
        print_expr_tb(o, e->a[0], pc);
        v3_put(o, ")", 1);
        return;
    case SX_NEG:
        puts_(o, "(- ");
        print_expr_tb(o, e->a[0], pc);
        v3_put(o, ")", 1);
        return;
    case SX_ISNULL:
        v3_put(o, "(", 1);
        print_expr_tb(o, e->a[0], pc);
        puts_(o, e->neg ? " IS NOT NULL)" : " IS NULL)");
        return;
    case SX_IN:
        v3_put(o, "(", 1);
        print_expr_tb(o, e->a[0], pc);
        puts_(o, e->neg ? " NOT IN (" : " IN (");
        for (size_t k = 1; k < e->na; k++) {
            if (k > 1) {
                puts_(o, ", ");
            }
            print_expr_tb(o, e->a[k], pc);
        }
        puts_(o, "))");
        return;
    case SX_BETWEEN:
        v3_put(o, "(", 1);
        print_expr_tb(o, e->a[0], pc);
        puts_(o, e->neg ? " NOT BETWEEN " : " BETWEEN ");
        print_expr_tb(o, e->a[1], pc);
        puts_(o, " AND ");
        print_expr_tb(o, e->a[2], pc);
        v3_put(o, ")", 1);
        return;
    case SX_LIKE:
        v3_put(o, "(", 1);
        print_expr_tb(o, e->a[0], pc);
        puts_(o, e->neg ? " NOT LIKE " : " LIKE ");
        print_expr_tb(o, e->a[1], pc);
        v3_put(o, ")", 1);
        return;
    case SX_CASE: {
        puts_(o, "CASE");
        if (e->operand) {
            v3_put(o, " ", 1);
            print_expr_tb(o, e->operand, pc);
        }
        size_t pairs = (e->na - (e->has_else ? 1 : 0)) / 2;
        for (size_t k = 0; k < pairs; k++) {
            puts_(o, " WHEN ");
            print_expr_tb(o, e->a[2 * k], pc);
            puts_(o, " THEN ");
            print_expr_tb(o, e->a[2 * k + 1], pc);
        }
        if (e->has_else) {
            puts_(o, " ELSE ");
            print_expr_tb(o, e->a[e->na - 1], pc);
        }
        puts_(o, " END");
        return;
    }
    case SX_CALL:
    case SX_AGG:
        puts_(o, e->name);
        v3_put(o, "(", 1);
        if (e->star) {
            v3_put(o, "*", 1);
        }
        if (e->distinct) {
            puts_(o, "DISTINCT ");
        }
        for (size_t k = 0; k < e->na; k++) {
            if (k) {
                puts_(o, ", ");
            }
            print_expr_tb(o, e->a[k], pc);
        }
        v3_put(o, ")", 1);
        return;
    case SX_CAST:
        puts_(o, "CAST(");
        print_expr_tb(o, e->a[0], pc);
        puts_(o, " AS ");
        puts_(o, sql_type_name(&e->type, b, sizeof b));
        v3_put(o, ")", 1);
        return;
    }
}

static void print_names(GsqlBuf *o, const SqlNameList *l, int order) {
    v3_put(o, "(", 1);
    for (size_t k = 0; k < l->n; k++) {
        if (k) {
            puts_(o, ", ");
        }
        print_name(o, l->cols[k]);
        if (order && l->desc[k]) {
            puts_(o, " DESC");
        }
    }
    v3_put(o, ")", 1);
}

static void print_coldef(GsqlBuf *o, const SqlColDef *c) {
    char b[32];
    print_name(o, c->name);
    v3_put(o, " ", 1);
    puts_(o, sql_type_name(&c->type, b, sizeof b));
    if (c->not_null) {
        puts_(o, " NOT NULL");
    }
    if (c->def) {
        puts_(o, " DEFAULT ");
        sql_print_expr(o, c->def);
    }
    if (c->primary) {
        puts_(o, " PRIMARY KEY");
    }
    if (c->unique) {
        puts_(o, " UNIQUE");
    }
}

void sql_print_stmt(GsqlBuf *o, const SqlStmt *s) {
    if (s->explain) {
        puts_(o, "EXPLAIN ");
    }
    switch (s->kind) {
    case SQ_SELECT:
        puts_(o, "SELECT ");
        for (size_t k = 0; k < s->nitems; k++) {
            const SqlItem *it = &s->items[k];
            if (k) {
                puts_(o, ", ");
            }
            if (!it->e) {
                if (it->star_table) {
                    print_name(o, it->star_table);
                    v3_put(o, ".", 1);
                }
                v3_put(o, "*", 1);
                continue;
            }
            sql_print_expr(o, it->e);
            if (it->alias) {
                puts_(o, " AS ");
                print_name(o, it->alias);
            }
        }
        if (s->table) {
            puts_(o, " FROM ");
            print_name(o, s->table);
            if (s->alias) {
                puts_(o, " AS ");
                print_name(o, s->alias);
            }
        }
        if (s->where) {
            puts_(o, " WHERE ");
            sql_print_expr(o, s->where);
        }
        for (size_t k = 0; k < s->ngroup; k++) {
            puts_(o, k ? ", " : " GROUP BY ");
            sql_print_expr(o, s->group[k]);
        }
        if (s->having) {
            puts_(o, " HAVING ");
            sql_print_expr(o, s->having);
        }
        for (size_t k = 0; k < s->norder; k++) {
            puts_(o, k ? ", " : " ORDER BY ");
            sql_print_expr(o, s->order[k].e);
            if (s->order[k].desc) {
                puts_(o, " DESC");
            }
        }
        if (s->limit) {
            puts_(o, " LIMIT ");
            sql_print_expr(o, s->limit);
        }
        if (s->offset) {
            puts_(o, " OFFSET ");
            sql_print_expr(o, s->offset);
        }
        return;
    case SQ_INSERT:
        puts_(o, "INSERT INTO ");
        print_name(o, s->table);
        if (s->cols.n) {
            v3_put(o, " ", 1);
            print_names(o, &s->cols, 0);
        }
        puts_(o, " VALUES ");
        for (size_t r = 0; r < s->nrows; r++) {
            puts_(o, r ? ", (" : "(");
            for (size_t c = 0; c < s->rowlen; c++) {
                if (c) {
                    puts_(o, ", ");
                }
                sql_print_expr(o, s->rows[r][c]);
            }
            v3_put(o, ")", 1);
        }
        return;
    case SQ_UPDATE:
        puts_(o, "UPDATE ");
        print_name(o, s->table);
        if (s->alias) {
            puts_(o, " AS ");
            print_name(o, s->alias);
        }
        for (size_t k = 0; k < s->cols.n; k++) {
            puts_(o, k ? ", " : " SET ");
            print_name(o, s->cols.cols[k]);
            puts_(o, " = ");
            sql_print_expr(o, s->sets[k]);
        }
        if (s->where) {
            puts_(o, " WHERE ");
            sql_print_expr(o, s->where);
        }
        return;
    case SQ_DELETE:
        puts_(o, "DELETE FROM ");
        print_name(o, s->table);
        if (s->alias) {
            puts_(o, " AS ");
            print_name(o, s->alias);
        }
        if (s->where) {
            puts_(o, " WHERE ");
            sql_print_expr(o, s->where);
        }
        return;
    case SQ_CREATE_TABLE:
        puts_(o, s->if_exists ? "CREATE TABLE IF NOT EXISTS " : "CREATE TABLE ");
        print_name(o, s->table);
        puts_(o, " (");
        for (size_t k = 0; k < s->ncoldefs; k++) {
            if (k) {
                puts_(o, ", ");
            }
            print_coldef(o, &s->coldefs[k]);
        }
        if (s->primary.n) {
            puts_(o, ", PRIMARY KEY ");
            print_names(o, &s->primary, 0);
        }
        for (size_t k = 0; k < s->nuniques; k++) {
            puts_(o, ", UNIQUE ");
            print_names(o, &s->uniques[k], 0);
        }
        v3_put(o, ")", 1);
        return;
    case SQ_DROP_TABLE:
        puts_(o, s->if_exists ? "DROP TABLE IF EXISTS " : "DROP TABLE ");
        print_name(o, s->table);
        return;
    case SQ_ADD_COLUMN:
        puts_(o, "ALTER TABLE ");
        print_name(o, s->table);
        puts_(o, " ADD COLUMN ");
        print_coldef(o, &s->coldefs[0]);
        return;
    case SQ_CREATE_INDEX:
        puts_(o, s->unique ? "CREATE UNIQUE INDEX " : "CREATE INDEX ");
        if (s->if_exists) {
            puts_(o, "IF NOT EXISTS ");
        }
        print_name(o, s->index);
        puts_(o, " ON ");
        print_name(o, s->table);
        v3_put(o, " ", 1);
        print_names(o, &s->index_cols, 1);
        return;
    case SQ_DROP_INDEX:
        puts_(o, s->if_exists ? "DROP INDEX IF EXISTS " : "DROP INDEX ");
        print_name(o, s->index);
        return;
    }
}
