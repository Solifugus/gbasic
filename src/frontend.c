#include "gbasic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Reentrant parse core (src/parser.y). Threads the sink and path straight into a
 * stack-allocated per-parse context, touching no file-scope parser state — so
 * concurrent gb_parse calls in one process share nothing. Declared here rather
 * than in a header because it is not yet part of a public interface. */
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

static void srv_check_options(SrvCheck *chk, const AstRecordFieldList *options,
                              const SrvOption *allowed, size_t allowed_count,
                              const char *where, int line, int column) {
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
                      "%s: option '%s' must be %s -- head options are literals, "
                      "so the block stays statically checkable; computed "
                      "configuration belongs to webserver.listen",
                      where, field->name, want);
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
        srv_error(chk, stmt->line, stmt->column,
                  "unknown declarative block '%s' (only 'server' exists)",
                  stmt->as.server.word);
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
                      sizeof(head_options) / sizeof(head_options[0]),
                      where, stmt->line, stmt->column);

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
                          site_where, site->line, site->column);
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
    int status = parse_source_reentrant(source, path, diags, out_program);
    if (status == 0) {
        if (server_blocks_validate(out_program, path, diags) != 0) {
            status = 1;
        }
    }
    return status;
}
