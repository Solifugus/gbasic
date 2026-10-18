#include "gbasic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Reentrant parse core (src/parser.y). Threads the sink and path straight into a
 * stack-allocated per-parse context, touching no file-scope parser state — so
 * concurrent gb_parse calls in one process share nothing. Declared here rather
 * than in a header because it is not yet part of a public interface. */
extern int parse_source_reentrant_at(const char *source, const char *path,
                                     int first_line,
                                     gb_diagnostics *diags, AstStmtList *out_program);
extern int parse_source_reentrant(const char *source, const char *path,
                                  gb_diagnostics *diags, AstStmtList *out_program);

/* ---- PLAT-WEB-5: load-time validation of `server` declarative blocks --------
 *
 * Everything in the design draft's §8 that is diagnosable WITHOUT I/O happens
 * here, on the AST, reported into the same sink as parse errors — which is what
 * puts the whole list into --json-diagnostics and Studio's error attribution
 * for free. Deliberately runtime instead: certificate file existence, port
 * availability, static directory existence.
 *
 * The head-option literal rule is load-bearing: option VALUES must be number /
 * string / true / false literals, and that restriction is what makes duplicate
 * hosts, worker counts and ports statically decidable at all. Computed
 * configuration belongs to the library layer underneath (webserver.listen), not
 * to the block. */

typedef struct {
    gb_diagnostics *diags;
    const char *path;
    int errors;
} SrvCheck;

static void srv_error(SrvCheck *chk, int line, int column, const char *fmt, ...);

#include <stdarg.h>
static void srv_error(SrvCheck *chk, int line, int column, const char *fmt, ...) {
    char message[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    gb_span span = {line, column, line, column};
    gb_report_to(chk->diags, GB_DIAG_SERVER_BLOCK, 0, chk->path, span, message);
    chk->errors++;
}

static int srv_is_verb(const char *word) {
    static const char *verbs[] = {"get", "post", "put", "delete", "patch",
                                  "head", "options", "stream", NULL};
    for (size_t i = 0; verbs[i]; i++) {
        if (strcmp(word, verbs[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* The effective method a handler answers: `stream` endpoints are GETs on the
 * wire (an SSE client GETs the path), so a stream and a get on the same path
 * collide and the duplicate check must see them as one method. */
static const char *srv_method_of(const AstServerItem *item) {
    return strcmp(item->word, "stream") == 0 ? "get" : item->word;
}

/* Path pattern shape, mirrored from stdlib/web.bas `_segment_of`: static
 * segments rank 2, `{name}` rank 1, `{name...}` rank 0 and last-only. Two
 * routes tie (can never be told apart) when every position has the same rank
 * and equal static text — web.routes' `_same_shape`, restated in C so the
 * refusal happens at load time with a file:line. */
#define SRV_MAX_SEGMENTS 32

typedef struct {
    int count;
    int rank[SRV_MAX_SEGMENTS];              /* 2 static, 1 param, 0 rest */
    const char *text[SRV_MAX_SEGMENTS];      /* start of segment text */
    int length[SRV_MAX_SEGMENTS];
} SrvShape;

static int srv_ident_span(const char *s, int len) {
    if (len <= 0) {
        return 0;
    }
    if (!((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z') || s[0] == '_')) {
        return 0;
    }
    for (int i = 1; i < len; i++) {
        if (!((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z') ||
              (s[i] >= '0' && s[i] <= '9') || s[i] == '_')) {
            return 0;
        }
    }
    return 1;
}

/* Parse one handler path into its shape, reporting every malformation. Returns
 * 0 when the path is too broken to shape-check further. */
static int srv_shape_path(SrvCheck *chk, const AstServerItem *item, SrvShape *shape) {
    const char *path = item->path;
    shape->count = 0;
    if (!path || path[0] != '/') {
        srv_error(chk, item->line, item->column,
                  "%s \"%s\": a route path must start with /", item->word,
                  path ? path : "");
        return 0;
    }
    size_t len = strlen(path);
    if (len > 1 && path[len - 1] == '/') {
        srv_error(chk, item->line, item->column,
                  "%s \"%s\": a route path must not end with / "
                  "('%s' and '%.*s' are different paths and this layer rewrites neither)",
                  item->word, path, path, (int)(len - 1), path);
        return 0;
    }
    if (len == 1) {
        return 1;                              /* "/" -- zero segments */
    }

    const char *seen_name[SRV_MAX_SEGMENTS];
    int seen_len[SRV_MAX_SEGMENTS];
    int seen_count = 0;

    const char *p = path + 1;
    while (1) {
        const char *end = strchr(p, '/');
        int seg_len = end ? (int)(end - p) : (int)strlen(p);
        if (shape->count >= SRV_MAX_SEGMENTS) {
            srv_error(chk, item->line, item->column,
                      "%s \"%s\": more than %d path segments", item->word, path,
                      SRV_MAX_SEGMENTS);
            return 0;
        }
        int idx = shape->count++;
        shape->text[idx] = p;
        shape->length[idx] = seg_len;
        if (seg_len > 0 && p[0] == '{') {
            if (p[seg_len - 1] != '}') {
                srv_error(chk, item->line, item->column,
                          "%s \"%s\": malformed pattern segment '%.*s' (no closing })",
                          item->word, path, seg_len, p);
                return 0;
            }
            const char *name = p + 1;
            int name_len = seg_len - 2;
            int rest = 0;
            if (name_len > 3 && strncmp(name + name_len - 3, "...", 3) == 0) {
                rest = 1;
                name_len -= 3;
            }
            if (!srv_ident_span(name, name_len)) {
                srv_error(chk, item->line, item->column,
                          "%s \"%s\": malformed pattern segment '%.*s' "
                          "(the capture name must be an identifier)",
                          item->word, path, seg_len, p);
                return 0;
            }
            if (rest && end) {
                srv_error(chk, item->line, item->column,
                          "%s \"%s\": puts {%.*s...} before the end; "
                          "a greedy capture may only be the LAST segment",
                          item->word, path, name_len, name);
                return 0;
            }
            for (int i = 0; i < seen_count; i++) {
                if (seen_len[i] == name_len &&
                    strncmp(seen_name[i], name, (size_t)name_len) == 0) {
                    srv_error(chk, item->line, item->column,
                              "%s \"%s\": repeats the capture name '%.*s'",
                              item->word, path, name_len, name);
                    return 0;
                }
            }
            seen_name[seen_count] = name;
            seen_len[seen_count] = name_len;
            seen_count++;
            shape->rank[idx] = rest ? 0 : 1;
        } else if (seg_len == 0) {
            srv_error(chk, item->line, item->column,
                      "%s \"%s\": empty path segment", item->word, path);
            return 0;
        } else {
            if (memchr(p, '{', (size_t)seg_len) || memchr(p, '}', (size_t)seg_len)) {
                srv_error(chk, item->line, item->column,
                          "%s \"%s\": malformed pattern segment '%.*s' "
                          "(a capture is a whole segment: /{name}/)",
                          item->word, path, seg_len, p);
                return 0;
            }
            shape->rank[idx] = 2;
        }
        if (!end) {
            break;
        }
        p = end + 1;
    }
    return 1;
}

static int srv_same_shape(const SrvShape *a, const SrvShape *b) {
    if (a->count != b->count) {
        return 0;
    }
    for (int i = 0; i < a->count; i++) {
        if (a->rank[i] != b->rank[i]) {
            return 0;
        }
        if (a->rank[i] == 2 &&
            (a->length[i] != b->length[i] ||
             strncmp(a->text[i], b->text[i], (size_t)a->length[i]) != 0)) {
            return 0;
        }
    }
    return 1;
}

/* One option in a head: name in `allowed`, value the literal kind the name
 * demands. `where` names the head for the message ("server 'edge'" / "web
 * 'store'"). */
typedef struct {
    const char *name;
    AstExprKind kind;                          /* required literal kind */
} SrvOption;

/* `remedy` is the sentence that follows the refusal. It differs by table and
 * that is the point: for a HEAD option the answer is `web.configure` at serve
 * time, and for a SITE option there is none -- `host` is the one option whose
 * literal value a parse-time check actually reads (two sites may not claim one
 * host), so it cannot be deferred without invalidating that check.
 *
 * The message used to say "computed configuration belongs to webserver.listen"
 * for both. True for port/address/timeout/cert/key, and FALSE for `workers`,
 * which is the one a real deployment hit first: webserver.listen binds one
 * socket and has no worker count at all, so following the advice led nowhere. */
static void srv_check_options(SrvCheck *chk, const AstRecordFieldList *options,
                              const SrvOption *allowed, size_t allowed_count,
                              const char *where, const char *remedy,
                              int line, int column) {
    for (size_t i = 0; i < options->count; i++) {
        const AstRecordField *field = &options->items[i];
        const SrvOption *spec = NULL;
        for (size_t j = 0; j < allowed_count; j++) {
            if (strcmp(field->name, allowed[j].name) == 0) {
                spec = &allowed[j];
                break;
            }
        }
        if (!spec) {
            srv_error(chk, line, column, "%s: unknown option '%s'", where, field->name);
            continue;
        }
        for (size_t j = 0; j < i; j++) {
            if (strcmp(options->items[j].name, field->name) == 0) {
                srv_error(chk, line, column, "%s: option '%s' given twice", where, field->name);
                break;
            }
        }
        AstExprKind kind = field->value ? field->value->kind : AST_EXPR_NULL;
        if (kind != spec->kind) {
            const char *want = spec->kind == AST_EXPR_NUMBER ? "a number literal"
                             : spec->kind == AST_EXPR_STRING ? "a string literal"
                             : "true or false";
            srv_error(chk, line, column,
                      "%s: option '%s' must be %s -- the block is registered "
                      "before anything runs, so an option here has nowhere "
                      "to evaluate an expression; %s",
                      where, field->name, want, remedy);
        }
    }
}

static const char *srv_option_string(const AstRecordFieldList *options, const char *name) {
    for (size_t i = 0; i < options->count; i++) {
        if (strcmp(options->items[i].name, name) == 0 &&
            options->items[i].value && options->items[i].value->kind == AST_EXPR_STRING) {
            return options->items[i].value->as.string;
        }
    }
    return NULL;
}

/* Validate the items of one site scope (the default site is the server's own
 * bare items). Fills `shapes`/`methods` so the caller cannot be tempted to
 * re-walk. */
static void srv_check_scope(SrvCheck *chk, const char *server_name,
                            const char *scope_name, const AstServerItemList *items,
                            int is_default_scope) {
    int root_seen = 0;
    SrvShape *shapes = calloc(items->count ? items->count : 1, sizeof(SrvShape));
    const AstServerItem **routed = calloc(items->count ? items->count : 1,
                                          sizeof(AstServerItem *));
    int *ok = calloc(items->count ? items->count : 1, sizeof(int));
    size_t route_count = 0;
    if (!shapes || !routed || !ok) {
        abort();
    }

    for (size_t i = 0; i < items->count; i++) {
        const AstServerItem *item = items->items[i];
        switch (item->kind) {
        case AST_SERVER_DIRECTIVE:
            if (strcmp(item->word, "root") == 0) {
                if (item->strings.count != 1) {
                    srv_error(chk, item->line, item->column,
                              "%s: root takes exactly one directory", scope_name);
                }
                if (root_seen) {
                    srv_error(chk, item->line, item->column,
                              "%s: root declared twice", scope_name);
                }
                root_seen = 1;
            } else if (strcmp(item->word, "trust_proxy") == 0) {
                if (!is_default_scope) {
                    srv_error(chk, item->line, item->column,
                              "trust_proxy belongs to the server, not to a site: "
                              "the direct peer is a fact of the listener");
                } else if (item->strings.count < 1) {
                    srv_error(chk, item->line, item->column,
                              "trust_proxy needs at least one proxy address");
                }
            } else {
                srv_error(chk, item->line, item->column,
                          "%s: unknown directive '%s' (root, trust_proxy)",
                          scope_name, item->word);
            }
            break;
        case AST_SERVER_HANDLER: {
            if (!srv_is_verb(item->word)) {
                srv_error(chk, item->line, item->column,
                          "unknown verb '%s' (get, post, put, delete, patch, "
                          "head, options, stream)", item->word);
                break;
            }
            if (item->close_word && strcmp(item->close_word, item->word) != 0) {
                srv_error(chk, item->line, item->column,
                          "%s \"%s\" is closed by 'end %s'", item->word,
                          item->path, item->close_word);
            }
            if (item->params.count != 1) {
                srv_error(chk, item->line, item->column,
                          "%s \"%s\": a handler takes exactly (req)",
                          item->word, item->path);
            }
            size_t idx = route_count++;
            routed[idx] = item;
            ok[idx] = srv_shape_path(chk, item, &shapes[idx]);
            if (ok[idx]) {
                for (size_t j = 0; j < idx; j++) {
                    if (!ok[j]) {
                        continue;
                    }
                    if (strcmp(srv_method_of(routed[j]), srv_method_of(item)) != 0) {
                        continue;
                    }
                    if (strcmp(routed[j]->path, item->path) == 0) {
                        srv_error(chk, item->line, item->column,
                                  "%s: duplicate route %s \"%s\" (first declared at line %d)",
                                  scope_name, item->word, item->path, routed[j]->line);
                    } else if (srv_same_shape(&shapes[j], &shapes[idx])) {
                        srv_error(chk, item->line, item->column,
                                  "%s: '%s %s' and '%s %s' can never be told apart; "
                                  "dispatch would have to guess",
                                  scope_name, routed[j]->word, routed[j]->path,
                                  item->word, item->path);
                    }
                }
            }
            break;
        }
        case AST_SERVER_SITE:
            if (is_default_scope) {
                /* handled by the caller (srv_check_server) */
            } else {
                srv_error(chk, item->line, item->column,
                          "a web block cannot contain another web block");
            }
            break;
        case AST_SERVER_HOOK:
            if (!is_default_scope) {
                srv_error(chk, item->line, item->column,
                          "an 'on %s' hook belongs to the server, not to a site",
                          item->word);
            } else if (strcmp(item->word, "drain") != 0) {
                srv_error(chk, item->line, item->column,
                          "unknown hook 'on %s' (only 'on drain' exists)", item->word);
            }
            break;
        }
    }
    (void)server_name;
    free(shapes);
    free(routed);
    free(ok);
}

static void srv_check_server(SrvCheck *chk, const AstStmt *stmt) {
    static const SrvOption head_options[] = {
        {"port", AST_EXPR_NUMBER},   {"address", AST_EXPR_STRING},
        {"inherit", AST_EXPR_BOOL},  {"workers", AST_EXPR_NUMBER},
        {"timeout", AST_EXPR_NUMBER},{"cert", AST_EXPR_STRING},
        {"key", AST_EXPR_STRING},
    };
    static const SrvOption site_options[] = {
        {"host", AST_EXPR_STRING}, {"cert", AST_EXPR_STRING}, {"key", AST_EXPR_STRING},
    };

    if (strcmp(stmt->as.server.word, "server") != 0) {
        /* ONE WORDING, shared with the parser's head check -- see ast.h. The
         * parser catches a block whose BODY does not parse; this catches one
         * that does. Same rule, two moments. */
        char unknown[320];
        gb_format_unknown_block(unknown, sizeof(unknown), stmt->as.server.word);
        srv_error(chk, stmt->line, stmt->column, "%s", unknown);
        return;
    }
    if (stmt->as.server.close_word &&
        strcmp(stmt->as.server.close_word, "server") != 0) {
        srv_error(chk, stmt->line, stmt->column,
                  "server '%s' is closed by 'end %s'", stmt->as.server.name,
                  stmt->as.server.close_word);
    }

    char where[192];
    snprintf(where, sizeof(where), "server '%s'", stmt->as.server.name);
    srv_check_options(chk, &stmt->as.server.options, head_options,
                      sizeof(head_options) / sizeof(head_options[0]), where,
                      "set it from configuration at serve time with "
                      "web.configure(name, { option: value })",
                      stmt->line, stmt->column);

    /* Bare entries form the implicit default site; `web` blocks are the named
     * ones. Hosts must be unique across the named ones. */
    srv_check_scope(chk, stmt->as.server.name, where, &stmt->as.server.items, 1);

    const AstServerItemList *items = &stmt->as.server.items;
    for (size_t i = 0; i < items->count; i++) {
        const AstServerItem *site = items->items[i];
        if (site->kind != AST_SERVER_SITE) {
            continue;
        }
        if (strcmp(site->word, "web") != 0) {
            srv_error(chk, site->line, site->column,
                      "unknown block '%s' inside server '%s' (only 'web' exists)",
                      site->word, stmt->as.server.name);
            continue;
        }
        if (site->close_word && strcmp(site->close_word, "web") != 0) {
            srv_error(chk, site->line, site->column,
                      "web '%s' is closed by 'end %s'", site->name, site->close_word);
        }
        char site_where[192];
        snprintf(site_where, sizeof(site_where), "web '%s'", site->name);
        srv_check_options(chk, &site->options, site_options,
                          sizeof(site_options) / sizeof(site_options[0]),
                          site_where,
                          "a site option stays in the source: `host` is read "
                          "here to refuse two sites claiming one name",
                          site->line, site->column);
        const char *host = srv_option_string(&site->options, "host");
        if (!host) {
            srv_error(chk, site->line, site->column,
                      "web '%s' needs a host: a named site exists to be told "
                      "apart from its siblings by name", site->name);
        } else {
            for (size_t j = 0; j < i; j++) {
                const AstServerItem *prior = items->items[j];
                if (prior->kind != AST_SERVER_SITE) {
                    continue;
                }
                const char *prior_host = srv_option_string(&prior->options, "host");
                if (prior_host && strcmp(prior_host, host) == 0) {
                    srv_error(chk, site->line, site->column,
                              "web '%s' repeats host \"%s\" (web '%s' already "
                              "answers it)", site->name, host, prior->name);
                }
            }
        }
        for (size_t j = 0; j < i; j++) {
            const AstServerItem *prior = items->items[j];
            if (prior->kind == AST_SERVER_SITE &&
                strcmp(prior->name, site->name) == 0) {
                srv_error(chk, site->line, site->column,
                          "web '%s' declared twice", site->name);
            }
        }
        srv_check_scope(chk, stmt->as.server.name, site_where, &site->entries, 0);
    }
}

/* Walk every statement; server blocks are legal at top level and directly in a
 * program block's body (both run in the global frame), and nowhere that has its
 * own frame or registration semantics. */
static void srv_walk(SrvCheck *chk, const AstStmtList *list, int top_level,
                     const char ***names, size_t *name_count);

static void srv_refuse_nested(SrvCheck *chk, const AstStmtList *list, const char *inside) {
    for (size_t i = 0; i < list->count; i++) {
        const AstStmt *stmt = list->items[i];
        switch (stmt->kind) {
        case AST_STMT_SERVER:
            srv_error(chk, stmt->line, stmt->column,
                      "a server block cannot be declared inside %s", inside);
            break;
        case AST_STMT_FUNCTION:
            srv_refuse_nested(chk, &stmt->as.function.body, "a function");
            break;
        case AST_STMT_MODIFIER:
            srv_refuse_nested(chk, &stmt->as.modifier.body, "a modifier");
            break;
        case AST_STMT_LIBRARY:
            srv_refuse_nested(chk, &stmt->as.library.body, "a library");
            break;
        case AST_STMT_IF:
            srv_refuse_nested(chk, &stmt->as.if_stmt.body, inside);
            srv_refuse_nested(chk, &stmt->as.if_stmt.else_body, inside);
            break;
        case AST_STMT_WHILE:
            srv_refuse_nested(chk, &stmt->as.while_stmt.body, inside);
            break;
        case AST_STMT_DO_LOOP:
            srv_refuse_nested(chk, &stmt->as.do_loop.body, inside);
            break;
        case AST_STMT_FOR_EACH:
            srv_refuse_nested(chk, &stmt->as.for_each.body, inside);
            break;
        case AST_STMT_FOR_RANGE:
            srv_refuse_nested(chk, &stmt->as.for_range.body, inside);
            break;
        case AST_STMT_CONSIDER:
            for (size_t j = 0; j < stmt->as.consider.branches.count; j++) {
                srv_refuse_nested(chk, &stmt->as.consider.branches.items[j].body, inside);
            }
            srv_refuse_nested(chk, &stmt->as.consider.else_body, inside);
            break;
        case AST_STMT_WATCH:
            srv_refuse_nested(chk, &stmt->as.watch.body, inside);
            break;
        case AST_STMT_WITH_LOCK:
            srv_refuse_nested(chk, &stmt->as.with_lock.body, inside);
            break;
        case AST_STMT_WITHOUT_WATCHERS:
            srv_refuse_nested(chk, &stmt->as.without_watchers, inside);
            break;
        case AST_STMT_PROGRAM:
            srv_refuse_nested(chk, &stmt->as.program.body, inside);
            break;
        default:
            break;
        }
    }
}

/* ---- load-time validation of `window` declarative blocks --------------------
 *
 * The same treatment the `server` block gets, in the same pass and the same
 * sink, which is what puts every refusal into --json-diagnostics and an
 * editor's error list for nothing. It is also what makes the whole declarative
 * surface testable WITH NO DISPLAY -- the property that let `web.routes` be
 * proven with no socket, and the reason this increment ships before any Qt.
 *
 * THE VOCABULARY IS CLOSED AND THAT IS THE POINT. A misspelled directive is a
 * widget nobody sees: it would lay out, draw nothing an author recognises, and
 * raise nothing. Closed, it is a located refusal naming the nearest real word.
 *
 * WHAT IS DELIBERATELY NOT CHECKED, said here rather than discovered: a LEAF's
 * options. Those are widget properties and Qt exposes 75 on a button alone, so
 * the set cannot be closed without Qt in the build -- the head's options can
 * and are. */
typedef struct {
    gb_diagnostics *diags;
    const char *path;
    int errors;
} WinCheck;

static void win_error(WinCheck *chk, int line, int column, const char *fmt, ...) {
    char message[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    gb_span span = {line, column, line, column};
    gb_report_to(chk->diags, GB_DIAG_WINDOW_BLOCK, 0, chk->path, span, message);
    chk->errors++;
}

/* CONTAINERS TAKE A BODY AND LEAVES DO NOT, which is not a taxonomy but the
 * grammar's own discipline: every head shape decides its own body-ness, which
 * is what makes the block cost zero bison conflicts. `table` is deliberately
 * ABSENT until it exists, so a `table` written today is refused as an unknown
 * word rather than silently accepted and ignored. */
static const char *win_container_words[] = {"row", "column", "form", "group",
                                            "table", "menu", "tabs", "tab",
                                            "cells", NULL};
static const char *win_leaf_words[] = {"field", "button", "check", "stretch",
                                       "item", "separator", NULL};

static int win_in(const char *const *set, const char *word) {
    for (size_t i = 0; set[i]; i++) {
        if (strcmp(set[i], word) == 0) {
            return 1;
        }
    }
    return 0;
}

/* The nearest real word: one substitution, one insertion, one deletion -- OR
 * ONE TRANSPOSITION, which is the correction that matters.
 *
 * `colunm` for `column` is a transposition, and so are `feild`, `recieve` and
 * most of what fingers actually do; under a plain one-edit rule it needs TWO
 * edits and no suggestion is offered. Measured by writing the test first and
 * watching it miss the exact example the comment claimed to catch -- the
 * commonest typo there is was the one case the first version could not see.
 *
 * Still short of a Levenshtein table nobody would maintain, and it answers
 * NULL when nothing is close, because a suggestion that is sometimes nonsense
 * is worse than none. */
static const char *win_nearest(const char *word) {
    /* DERIVED FROM THE TWO VOCABULARY TABLES, not a third copy of them. It WAS
     * a third copy, and `table` was added to the containers and to the
     * sentence that lists them and missed HERE -- so a reader who typed
     * `tabel` got the generic list instead of the one-word answer, which is
     * the defect the near-miss finder exists to prevent, for the newest word
     * in the language. Two representations of one list drift;
     * run_web_configure.sh's TABLES tripwire makes that argument for the
     * server block's own options, and run_window.sh now asserts every word
     * here is reachable. */
    const char *all[32];
    size_t n = 0;
    for (size_t i = 0; win_container_words[i] && n + 1 < 32; i++) {
        all[n++] = win_container_words[i];
    }
    for (size_t i = 0; win_leaf_words[i] && n + 1 < 32; i++) {
        all[n++] = win_leaf_words[i];
    }
    all[n] = NULL;
    size_t len = strlen(word);
    for (size_t i = 0; all[i]; i++) {
        size_t other = strlen(all[i]);
        if (other + 1 < len || len + 1 < other) {
            continue;
        }
        /* ONE TRANSPOSITION: equal length, and exactly two adjacent positions
         * swapped. Checked first because it is the common case. */
        if (other == len) {
            size_t diff = 0, first = 0;
            for (size_t k = 0; k < len; k++) {
                if (word[k] != all[i][k]) {
                    if (diff == 0) { first = k; }
                    diff++;
                }
            }
            if (diff == 0) {
                return all[i];
            }
            if (diff == 2 && first + 1 < len &&
                word[first] == all[i][first + 1] &&
                word[first + 1] == all[i][first] &&
                word[first + 1] != all[i][first + 1]) {
                return all[i];
            }
        }
        size_t a = 0, b = 0, edits = 0;
        while (a < len && b < other && edits <= 1) {
            if (word[a] == all[i][b]) {
                a++; b++;
                continue;
            }
            edits++;
            if (len > other)       { a++; }
            else if (other > len)  { b++; }
            else                   { a++; b++; }
        }
        edits += (len - a) + (other - b);
        if (edits <= 1) {
            return all[i];
        }
    }
    return NULL;
}

static void win_check_items(WinCheck *chk, const AstWindowItemList *items,
                            const char *window_subject, const char *parent_word,
                            const char **names, size_t *name_count,
                            size_t name_cap);

/* One item, the window's subject so a bare `form` can inherit it, and THE
 * PARENT'S WORD, which `menu` is the first directive to need: a menu bar
 * belongs to the WINDOW rather than to a layout, so where a `menu` may appear
 * is a fact about its surroundings and not about itself. */
static void win_check_item(WinCheck *chk, const AstWindowItem *item,
                           const char *window_subject, const char *parent_word,
                           const char **names, size_t *name_count,
                           size_t name_cap) {
    if (item->kind == AST_WINDOW_HANDLER) {
        return;   /* its target is checked once the whole window is known */
    }
    const char *word = item->word ? item->word : "";
    int is_container = win_in(win_container_words, word);
    int is_leaf = win_in(win_leaf_words, word);

    if (!is_container && !is_leaf) {
        const char *near = win_nearest(word);
        if (near) {
            win_error(chk, item->line, item->column,
                      "window: unknown directive '%s' -- did you mean '%s'?",
                      word, near);
        } else {
            win_error(chk, item->line, item->column,
                      "window: unknown directive '%s'. The containers are row, "
                      "column, form, group, table, menu, tabs, tab and "
                      "cells; the parts are field, button, check, stretch, "
                      "item and separator", word);
        }
        return;
    }

    /* THE WORD AND THE SHAPE MUST AGREE. The grammar decides shape from the
     * head alone, so `field x` with a body parses perfectly as a container --
     * and would lay out a field containing other things, which is not a thing. */
    if (is_leaf && item->kind == AST_WINDOW_CONTAINER) {
        win_error(chk, item->line, item->column,
                  "window: '%s' takes no body -- write `%s name( ... )` on one line",
                  word, word);
        return;
    }
    if (is_container && item->kind == AST_WINDOW_LEAF) {
        win_error(chk, item->line, item->column,
                  "window: '%s' is a container and needs a body ending `end %s`",
                  word, word);
        return;
    }

    if (item->kind == AST_WINDOW_CONTAINER) {
        if (item->close_word && strcmp(item->close_word, word) != 0) {
            win_error(chk, item->line, item->column,
                      "window: '%s' is closed by `end %s`, not `end %s`",
                      word, word, item->close_word);
        }
        /* A `form` NAMES ITS SUBJECT OR INHERITS THE WINDOW'S, and a window
         * with neither is a form about nothing. The window's subject is a
         * DEFAULT a form may shadow -- the rule a nested `with principal(p)`
         * already follows. */
        int carries_subject = (strcmp(word, "form") == 0 ||
                               strcmp(word, "table") == 0);
        if (carries_subject) {
            if (item->subject_direction &&
                strcmp(item->subject_direction, "edit") != 0 &&
                strcmp(item->subject_direction, "show") != 0) {
                win_error(chk, item->line, item->column,
                          "window: a %s is `to edit` or `to show`, not `to %s`",
                          word, item->subject_direction);
            }
            if (!item->subject && !window_subject) {
                win_error(chk, item->line, item->column,
                          "window: this %s names no subject and the window has "
                          "none to inherit -- write `%s to edit <record>`",
                          word, word);
            }
        } else if (item->subject) {
            win_error(chk, item->line, item->column,
                      "window: only a form or a table carries a subject, not '%s'",
                      word);
        }
        /* A MENU BAR BELONGS TO THE WINDOW, so a `menu` sits at the window's
         * top level or inside another menu, and nowhere else. Refused rather
         * than placed somewhere surprising: Qt would put a QMenuBar wherever
         * the layout said, so a `menu` inside a `row` would render as a bar in
         * the middle of the screen -- a plausible-looking widget nobody asked
         * for, which is the class the closed vocabulary exists to prevent. */
        if (strcmp(word, "menu") == 0) {
            if (!parent_word ||
                (strcmp(parent_word, "window") != 0 &&
                 strcmp(parent_word, "menu") != 0)) {
                win_error(chk, item->line, item->column,
                          "window: a menu belongs to the window or to another "
                          "menu, not inside '%s' -- a menu bar is not laid out",
                          parent_word ? parent_word : "?");
            }
            if (!item->label) {
                win_error(chk, item->line, item->column,
                          "window: a menu needs a caption -- `menu \"File\"` "
                          "-- because that is what a person reads on the bar");
            }
            if (item->name) {
                win_error(chk, item->line, item->column,
                          "window: a menu takes no name; its ITEMS are what a "
                          "handler addresses");
            }
            for (size_t i = 0; i < item->entries.count; i++) {
                const AstWindowItem *part = item->entries.items[i];
                if (part->kind == AST_WINDOW_HANDLER || !part->word) {
                    continue;
                }
                if (strcmp(part->word, "item") != 0 &&
                    strcmp(part->word, "separator") != 0 &&
                    strcmp(part->word, "menu") != 0) {
                    win_error(chk, part->line, part->column,
                              "window: a menu holds items, separators and "
                              "submenus -- '%s' is none of those", part->word);
                }
            }
        }

        /* A TABLE IS NAMED AND A FORM IS NOT, which is a real difference and
         * not a spelling: a form's parts are addressed by their own names,
         * where a table shows MANY rows, so the table itself is what a handler
         * and `qt.selected` address. Refused both ways, so neither shape can
         * be written the other's way and quietly mean something else. */
        if (strcmp(word, "table") == 0 && !item->name) {
            win_error(chk, item->line, item->column,
                      "window: a table needs a name -- `table orders to show "
                      "<list>` -- because that is what a handler and "
                      "`qt.selected` address");
        }
        /* WHICH CONTAINERS MAY BE NAMED, and it is a short list on purpose: a
         * name is what a handler and a property read address, so it belongs to
         * a container that IS a widget with state of its own. A `table` shows
         * many rows and a `tabs` has a current page; a `row` is a box and a
         * `tab` is a PAGE whose CAPTION is what a person reads, the rule a
         * `menu` follows. */
        int may_be_named = (strcmp(word, "table") == 0 ||
                            strcmp(word, "tabs") == 0);
        if (!may_be_named && item->name) {
            win_error(chk, item->line, item->column,
                      "window: '%s' takes no name; a table and a `tabs` are "
                      "named, because they are the containers with state a "
                      "handler can ask about", word);
        }

        /* A `tab` BELONGS TO A `tabs` and needs a caption; a `tabs` holds
         * nothing else. Refused both ways, because a QTabWidget has NO LAYOUT
         * of its own -- its children are pages -- so anything else inside one
         * would be dropped silently rather than mislaid. */
        if (strcmp(word, "tabs") == 0) {
            for (size_t i = 0; i < item->entries.count; i++) {
                const AstWindowItem *part = item->entries.items[i];
                if (part->kind == AST_WINDOW_HANDLER || !part->word) {
                    continue;
                }
                if (strcmp(part->word, "tab") != 0) {
                    win_error(chk, part->line, part->column,
                              "window: `tabs` holds `tab` pages -- '%s' is not "
                              "one; put it inside a tab", part->word);
                }
            }
        }
        /* `cells` HOLDS ROWS OF CELLS, which is also why it needed no new
         * word: a grid IS rows, so `row` is reused -- refined inside `cells`
         * to mean a row whose columns ALIGN with the rows above, where a `row`
         * elsewhere is a box that does not. Anything else directly inside
         * `cells` has no row to go in, so it is refused rather than placed at
         * whatever position happened to be current. */
        if (strcmp(word, "cells") == 0) {
            for (size_t i = 0; i < item->entries.count; i++) {
                const AstWindowItem *part = item->entries.items[i];
                if (part->kind == AST_WINDOW_HANDLER || !part->word) {
                    continue;
                }
                if (strcmp(part->word, "row") != 0) {
                    win_error(chk, part->line, part->column,
                              "window: `cells` holds `row`s of cells -- '%s' "
                              "is not one; put it inside a row", part->word);
                }
            }
        }
        if (strcmp(word, "tab") == 0) {
            if (!parent_word || strcmp(parent_word, "tabs") != 0) {
                win_error(chk, item->line, item->column,
                          "window: a `tab` belongs to a `tabs`, not to '%s'",
                          parent_word ? parent_word : "?");
            }
            if (!item->label) {
                win_error(chk, item->line, item->column,
                          "window: a tab needs a caption -- `tab \"General\"` "
                          "-- because that is what a person reads on it");
            }
        }
        /* A NAMED CONTAINER REGISTERS ITS NAME TOO. Only leaves did, so
         * `on orders.activated` on a perfectly real table would have been
         * refused as naming nothing in the window -- the refusal that exists
         * to catch a handler that can never fire, firing on one that can. */
        if (item->name) {
            for (size_t i = 0; i < *name_count; i++) {
                if (strcmp(names[i], item->name) == 0) {
                    win_error(chk, item->line, item->column,
                              "window: '%s' is declared twice, so `on "
                              "%s.<signal>` could not say which",
                              item->name, item->name);
                    break;
                }
            }
            if (*name_count < name_cap) {
                names[*name_count] = item->name;
                *name_count = *name_count + 1;
            }
        }
        /* A TABLE'S PARTS ARE COLUMNS, so every one must be a `field` -- §7's
         * own decision, and the saving is that `field` already means "one value
         * of the subject". A `button` among them would be a widget in a place
         * that holds values, which lays out as nothing an author recognises.
         *
         * AND A COLUMN'S NAME BELONGS TO ITS TABLE, NOT TO THE WINDOW, which
         * is a correction the first runnable example produced immediately: a
         * screen listing orders and showing the chosen one is the commonest
         * shape there is, and BOTH want a `who` and a `total` -- the list's
         * are columns, the form's are widgets. With the columns registered
         * window-wide that example was refused as declaring `who` twice, which
         * is a true sentence about a program with nothing wrong with it.
         *
         * So the columns are checked HERE (named, unique within their own
         * table) and the recursion that would register them window-wide is
         * skipped. `qt.cell(win, "orders", r, "who")` addresses one, which is
         * a column of a named table rather than a thing in the window. */
        if (strcmp(word, "table") == 0) {
            const char *seen[64];
            size_t seen_count = 0;
            for (size_t i = 0; i < item->entries.count; i++) {
                const AstWindowItem *part = item->entries.items[i];
                if (part->kind == AST_WINDOW_HANDLER) {
                    continue;
                }
                if (!part->word || strcmp(part->word, "field") != 0) {
                    win_error(chk, part->line, part->column,
                              "window: a table's parts are columns, so they are "
                              "all `field` -- '%s' is not one",
                              part->word ? part->word : "?");
                    continue;
                }
                if (!part->name) {
                    win_error(chk, part->line, part->column,
                              "window: a table's column needs a name -- it is "
                              "the field of the subject this column shows");
                    continue;
                }
                for (size_t j = 0; j < seen_count; j++) {
                    if (strcmp(seen[j], part->name) == 0) {
                        win_error(chk, part->line, part->column,
                                  "window: table '%s' shows '%s' twice, so two "
                                  "columns would hold one field", item->name,
                                  part->name);
                        break;
                    }
                }
                if (seen_count < 64) {
                    seen[seen_count++] = part->name;
                }
            }
            return;   /* columns are not window-wide names */
        }
        win_check_items(chk, &item->entries, window_subject, word, names,
                        name_count, name_cap);
        return;
    }

    /* AN ITEM AND A SEPARATOR ARE MENU WORDS, and the mirror of the rule above:
     * a menu may not be laid out, and a menu's parts may not appear where
     * things ARE laid out. `item open( ... )` in a `column` would otherwise
     * reach `gb_qt_part`, which has no widget for the word and would refuse at
     * RUN time -- late, and only for somebody with Qt in their build. */
    if (strcmp(word, "item") == 0 || strcmp(word, "separator") == 0) {
        if (!parent_word || strcmp(parent_word, "menu") != 0) {
            win_error(chk, item->line, item->column,
                      "window: '%s' belongs in a menu, not in '%s'", word,
                      parent_word ? parent_word : "?");
            return;
        }
    }
    if (strcmp(word, "separator") == 0) {
        /* A SEPARATOR IS A LINE and names nothing, the rule `stretch` follows
         * for being pure space. */
        if (item->name) {
            win_error(chk, item->line, item->column,
                      "window: a separator is a line and takes no name");
        }
        return;
    }

    /* A LEAF'S NAME IS WHAT A HANDLER AND A BINDING ADDRESS, so `field()` with
     * none is refused -- except `stretch`, which is pure space and names
     * nothing. */
    if (strcmp(word, "stretch") == 0) {
        if (item->name) {
            win_error(chk, item->line, item->column,
                      "window: stretch is space and takes no name");
        }
        return;
    }
    if (!item->name) {
        win_error(chk, item->line, item->column,
                  "window: '%s' needs a name -- `%s id( ... )` -- because that is "
                  "what a handler and a binding address", word, word);
        return;
    }
    for (size_t i = 0; i < *name_count; i++) {
        if (strcmp(names[i], item->name) == 0) {
            win_error(chk, item->line, item->column,
                      "window: '%s' is declared twice, so `on %s.<signal>` could "
                      "not say which", item->name, item->name);
            return;
        }
    }
    if (*name_count < name_cap) {
        names[*name_count] = item->name;
        *name_count = *name_count + 1;
    }
}

static void win_check_items(WinCheck *chk, const AstWindowItemList *items,
                            const char *window_subject, const char *parent_word,
                            const char **names, size_t *name_count,
                            size_t name_cap) {
    for (size_t i = 0; i < items->count; i++) {
        win_check_item(chk, items->items[i], window_subject, parent_word, names,
                       name_count, name_cap);
    }
}

/* Every handler in the tree, checked against the names the window declares.
 * THIS IS THE REFUSAL LOAD-TIME VALIDATION EXISTS FOR: `on save.clicked` with
 * no `button save` is a handler that can never fire, and nothing at run time
 * would ever say so -- the button is simply not there to connect. */
static void win_check_handlers(WinCheck *chk, const AstWindowItemList *items,
                               const char **names, size_t name_count) {
    for (size_t i = 0; i < items->count; i++) {
        const AstWindowItem *item = items->items[i];
        if (item->kind == AST_WINDOW_HANDLER) {
            int found = 0;
            for (size_t j = 0; j < name_count; j++) {
                if (item->event_target && strcmp(names[j], item->event_target) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                win_error(chk, item->line, item->column,
                          "window: `on %s.%s` names nothing in this window, so it "
                          "could never fire",
                          item->event_target ? item->event_target : "?",
                          item->event_signal ? item->event_signal : "?");
            }
        } else if (item->kind == AST_WINDOW_CONTAINER) {
            win_check_handlers(chk, &item->entries, names, name_count);
        }
    }
}

static void win_check_window(WinCheck *chk, const AstStmt *stmt) {
    /* THE HEAD'S OPTIONS ARE CLOSED, unlike a leaf's: these are the window's
     * own, not a widget's, so the set is knowable without Qt. A misspelling
     * that was ignored would leave an author believing they had asked for
     * something -- the rule `make_dir` and `webserver.listen` already follow. */
    static const char *head[] = {"title", "width", "height", NULL};
    for (size_t i = 0; i < stmt->as.window.options.count; i++) {
        const AstRecordField *field = &stmt->as.window.options.items[i];
        if (!win_in(head, field->name)) {
            win_error(chk, stmt->line, stmt->column,
                      "window '%s': unknown option '%s' (the options are title, "
                      "width and height)", stmt->as.window.name, field->name);
        }
    }
    if (stmt->as.window.subject_direction &&
        strcmp(stmt->as.window.subject_direction, "edit") != 0 &&
        strcmp(stmt->as.window.subject_direction, "show") != 0) {
        win_error(chk, stmt->line, stmt->column,
                  "window '%s': the shorthand is `to edit` or `to show`, not `to %s`",
                  stmt->as.window.name, stmt->as.window.subject_direction);
    }
    if (stmt->as.window.close_word &&
        strcmp(stmt->as.window.close_word, "window") != 0) {
        win_error(chk, stmt->line, stmt->column,
                  "window '%s' is closed by `end window`, not `end %s`",
                  stmt->as.window.name, stmt->as.window.close_word);
    }

    const char *names[256];
    size_t name_count = 0;
    /* "window" IS THE PARENT AT THE TOP LEVEL, which is the one place a `menu`
     * may appear outside another menu -- a menu bar belongs to the window. */
    win_check_items(chk, &stmt->as.window.items, stmt->as.window.subject,
                    "window", names, &name_count, 256);
    win_check_handlers(chk, &stmt->as.window.items, names, name_count);
}

static void srv_walk(SrvCheck *chk, const AstStmtList *list, int top_level,
                     const char ***names, size_t *name_count) {
    for (size_t i = 0; i < list->count; i++) {
        const AstStmt *stmt = list->items[i];
        switch (stmt->kind) {
        case AST_STMT_SERVER: {
            for (size_t j = 0; j < *name_count; j++) {
                if (strcmp((*names)[j], stmt->as.server.name) == 0) {
                    srv_error(chk, stmt->line, stmt->column,
                              "server '%s' declared twice", stmt->as.server.name);
                }
            }
            const char **grown = realloc((void *)*names,
                                         sizeof(char *) * (*name_count + 1));
            if (!grown) {
                abort();
            }
            grown[*name_count] = stmt->as.server.name;
            *names = grown;
            *name_count = *name_count + 1;
            srv_check_server(chk, stmt);
            /* handler bodies may not declare servers either */
            for (size_t j = 0; j < stmt->as.server.items.count; j++) {
                AstServerItem *item = stmt->as.server.items.items[j];
                if (item->kind == AST_SERVER_HANDLER || item->kind == AST_SERVER_HOOK) {
                    srv_refuse_nested(chk, &item->body, "a handler");
                } else if (item->kind == AST_SERVER_SITE) {
                    for (size_t k = 0; k < item->entries.count; k++) {
                        AstServerItem *entry = item->entries.items[k];
                        if (entry->kind == AST_SERVER_HANDLER || entry->kind == AST_SERVER_HOOK) {
                            srv_refuse_nested(chk, &entry->body, "a handler");
                        }
                    }
                }
            }
            break;
        }
        case AST_STMT_WINDOW: {
            /* Checked in the SAME walk, so one file reports every problem in
             * both kinds of block at once rather than stopping at the first. */
            WinCheck wchk = {chk->diags, chk->path, 0};
            win_check_window(&wchk, stmt);
            chk->errors += wchk.errors;
            break;
        }
        case AST_STMT_PROGRAM:
            /* a program block's body runs in the global frame; a server here
             * binds on reach, so it is legal */
            srv_walk(chk, &stmt->as.program.body, 0, names, name_count);
            break;
        case AST_STMT_FUNCTION:
            srv_refuse_nested(chk, &stmt->as.function.body, "a function");
            break;
        case AST_STMT_MODIFIER:
            srv_refuse_nested(chk, &stmt->as.modifier.body, "a modifier");
            break;
        case AST_STMT_LIBRARY:
            srv_refuse_nested(chk, &stmt->as.library.body, "a library");
            break;
        case AST_STMT_IF:
            srv_refuse_nested(chk, &stmt->as.if_stmt.body, "a branch");
            srv_refuse_nested(chk, &stmt->as.if_stmt.else_body, "a branch");
            break;
        case AST_STMT_WHILE:
            srv_refuse_nested(chk, &stmt->as.while_stmt.body, "a loop");
            break;
        case AST_STMT_DO_LOOP:
            srv_refuse_nested(chk, &stmt->as.do_loop.body, "a loop");
            break;
        case AST_STMT_FOR_EACH:
            srv_refuse_nested(chk, &stmt->as.for_each.body, "a loop");
            break;
        case AST_STMT_FOR_RANGE:
            srv_refuse_nested(chk, &stmt->as.for_range.body, "a loop");
            break;
        case AST_STMT_CONSIDER:
            for (size_t j = 0; j < stmt->as.consider.branches.count; j++) {
                srv_refuse_nested(chk, &stmt->as.consider.branches.items[j].body, "a branch");
            }
            srv_refuse_nested(chk, &stmt->as.consider.else_body, "a branch");
            break;
        case AST_STMT_WATCH:
            srv_refuse_nested(chk, &stmt->as.watch.body, "a watch body");
            break;
        case AST_STMT_WITH_LOCK:
            srv_refuse_nested(chk, &stmt->as.with_lock.body, "a lock body");
            break;
        case AST_STMT_WITHOUT_WATCHERS:
            srv_refuse_nested(chk, &stmt->as.without_watchers, "a watch body");
            break;
        default:
            break;
        }
    }
    (void)top_level;
}

/* Returns the number of errors reported (0 = clean). */
static int server_blocks_validate(const AstStmtList *program, const char *path,
                                  gb_diagnostics *diags) {
    SrvCheck chk = {diags, path, 0};
    const char **names = NULL;
    size_t name_count = 0;
    srv_walk(&chk, program, 1, &names, &name_count);
    free((void *)names);
    return chk.errors;
}

int gb_parse(const char *source, const char *path,
             AstStmtList *out_program, gb_diagnostics *diags) {
    return gb_parse_at(source, path, 1, out_program, diags);
}

int gb_parse_at(const char *source, const char *path, int first_line,
                AstStmtList *out_program, gb_diagnostics *diags) {
    int status = parse_source_reentrant_at(source, path, first_line,
                                           diags, out_program);
    if (status == 0) {
        if (server_blocks_validate(out_program, path, diags) != 0) {
            status = 1;
        }
    }
    return status;
}
