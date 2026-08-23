#include "ast.h"
#include "builtins.h"
#include "eval.h"
#include "gbasic.h"
#include "lexer.h"

#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

int parse_source(const char *source, AstStmtList *out_program);
void parse_set_source_path(const char *path);

static void print_help(const char *argv0) {
    printf("usage:\n");
    printf("  %s FILE [args...]\n", argv0);
    printf("  %s --tokens FILE\n", argv0);
    printf("  %s --ast FILE\n", argv0);
    printf("  %s --add-loads FILE\n", argv0);
    printf("  %s --add-uses FILE\n", argv0);
    printf("  %s --json-diagnostics FILE [args...]\n", argv0);
    printf("  %s --line-buffered FILE [args...]\n", argv0);
    printf("\n");
    printf("flags:\n");
    printf("  --help          show this help\n");
    printf("  --version       show version\n");
    printf("  --tokens FILE   print lexer tokens\n");
    printf("  --ast FILE      parse and print AST\n");
    printf("  --add-loads FILE analyze unresolved calls/modifiers and print source with load statements\n");
    printf("  --add-uses FILE  compatibility alias; emits use statements\n");
    printf("  --json-diagnostics FILE [args...]  run FILE, emitting diagnostics as JSON\n");
    printf("                   lines to stderr. Runs the program, so it takes program\n");
    printf("                   arguments; the inspect-only modes above do not.\n");
    printf("  --line-buffered  flush stdout at every completed line instead of at buffer\n");
    printf("                   capacity; combines with any of the above\n");
}

static void print_tokens(const char *source) {
    Lexer lexer;
    lexer_init(&lexer, source);

    for (;;) {
        Token token = lexer_next(&lexer);
        printf("%4d:%-3d  %-10s", token.line, token.column, token_type_name(token.type));
        if (token.length > 0 && token.type != TOKEN_NEWLINE && token.type != TOKEN_EOF) {
            printf("  %.*s", token.length, token.start);
        }
        printf("\n");

        if (token.type == TOKEN_EOF || token.type == TOKEN_ERROR) {
            break;
        }
    }
}

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        perror(path);
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        perror(path);
        fclose(file);
        return NULL;
    }

    long size = ftell(file);
    if (size < 0) {
        perror(path);
        fclose(file);
        return NULL;
    }

    rewind(file);

    char *source = malloc((size_t)size + 1);
    if (!source) {
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(source, 1, (size_t)size, file);
    if (ferror(file)) {
        perror(path);
        free(source);
        fclose(file);
        return NULL;
    }

    source[bytes_read] = '\0';
    fclose(file);
    return source;
}

typedef struct {
    char **items;
    size_t count;
} StringList;

typedef struct {
    char *path;
    AstStmtList program;
} ParsedFile;

typedef struct {
    char *library;
    char *path;
} Provider;

typedef struct {
    ParsedFile *files;
    size_t file_count;
    Provider *providers;
    size_t provider_count;
    StringList local_functions;
    StringList local_assign_modifiers;
    StringList local_compare_modifiers;
    StringList existing_uses;
    StringList insert_uses;
    const char *root_path;
    AstStmtList root_program;
} AddUsesContext;

static char *copy_string_main(const char *text) {
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (!copy) {
        abort();
    }
    memcpy(copy, text, length + 1);
    return copy;
}

static int string_list_contains(StringList *list, const char *text) {
    for (size_t i = 0; i < list->count; i++) {
        if (strcmp(list->items[i], text) == 0) {
            return 1;
        }
    }
    return 0;
}

static void string_list_add(StringList *list, const char *text) {
    if (string_list_contains(list, text)) {
        return;
    }
    char **next = realloc(list->items, sizeof(char *) * (list->count + 1));
    if (!next) {
        abort();
    }
    list->items = next;
    list->items[list->count++] = copy_string_main(text);
}

static void string_list_free(StringList *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

static char *dirname_copy_main(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash) {
        return copy_string_main(".");
    }
    if (slash == path) {
        return copy_string_main("/");
    }
    size_t len = (size_t)(slash - path);
    char *dir = malloc(len + 1);
    if (!dir) {
        abort();
    }
    memcpy(dir, path, len);
    dir[len] = '\0';
    return dir;
}

static char *join_path_main(const char *dir, const char *name) {
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    int needs_slash = dir_len > 0 && dir[dir_len - 1] != '/';
    char *path = malloc(dir_len + (size_t)needs_slash + name_len + 1);
    if (!path) {
        abort();
    }
    memcpy(path, dir, dir_len);
    if (needs_slash) {
        path[dir_len] = '/';
    }
    memcpy(path + dir_len + (size_t)needs_slash, name, name_len + 1);
    return path;
}

static int has_bas_extension_main(const char *path) {
    size_t len = strlen(path);
    return len >= 4 && strcmp(path + len - 4, ".bas") == 0;
}

static const char *basename_main(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static ParsedFile *parsed_file_get(AddUsesContext *ctx, const char *path) {
    for (size_t i = 0; i < ctx->file_count; i++) {
        if (strcmp(ctx->files[i].path, path) == 0) {
            return &ctx->files[i];
        }
    }

    char *source = read_file(path);
    if (!source) {
        return NULL;
    }
    AstStmtList program = ast_stmt_list_empty();
    parse_set_source_path(path);
    if (parse_source(source, &program) != 0) {
        free(source);
        return NULL;
    }
    free(source);

    ParsedFile *next = realloc(ctx->files, sizeof(ParsedFile) * (ctx->file_count + 1));
    if (!next) {
        abort();
    }
    ctx->files = next;
    ctx->files[ctx->file_count].path = copy_string_main(path);
    ctx->files[ctx->file_count].program = program;
    ctx->file_count++;
    return &ctx->files[ctx->file_count - 1];
}

static int builtin_function(const char *name) {
    return gbasic_builtin_function(name);
}

static int modifier_phrase_matches_main(const char *phrase, const char *name);

static int builtin_modifier(const char *name, const char *context) {
    if (strcmp(context, "compare") == 0) {
        return strcmp(name, "caseless") == 0;
    }
    if (strcmp(context, "assign") == 0) {
        return strcmp(name, "USD") == 0 || strcmp(name, "date") == 0 ||
            strcmp(name, "time") == 0 || strcmp(name, "file") == 0 ||
            strcmp(name, "dir") == 0 ||
            modifier_phrase_matches_main(name, "trimmed") ||
            modifier_phrase_matches_main(name, "lowered") ||
            modifier_phrase_matches_main(name, "uppered") ||
            modifier_phrase_matches_main(name, "split") ||
            modifier_phrase_matches_main(name, "join") ||
            modifier_phrase_matches_main(name, "length") ||
            modifier_phrase_matches_main(name, "number") ||
            modifier_phrase_matches_main(name, "string");
    }
    return 0;
}

static int modifier_phrase_matches_main(const char *phrase, const char *name) {
    size_t name_len = strlen(name);
    while (*phrase == ' ' || *phrase == '\t') {
        phrase++;
    }
    if (strncmp(phrase, name, name_len) != 0) {
        return 0;
    }
    return phrase[name_len] == '\0' || phrase[name_len] == ' ' || phrase[name_len] == '\t';
}

static void provider_add(AddUsesContext *ctx, const char *library, const char *path) {
    Provider *next = realloc(ctx->providers, sizeof(Provider) * (ctx->provider_count + 1));
    if (!next) {
        abort();
    }
    ctx->providers = next;
    ctx->providers[ctx->provider_count].library = copy_string_main(library);
    ctx->providers[ctx->provider_count].path = copy_string_main(path);
    ctx->provider_count++;
}

static int library_provides_function(AstStmt *library, const char *name) {
    for (size_t i = 0; i < library->as.library.body.count; i++) {
        AstStmt *stmt = library->as.library.body.items[i];
        if (stmt->kind == AST_STMT_FUNCTION && stmt->as.function.name &&
            strcmp(stmt->as.function.name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int library_provides_modifier(AstStmt *library, const char *name, const char *context) {
    for (size_t i = 0; i < library->as.library.body.count; i++) {
        AstStmt *stmt = library->as.library.body.items[i];
        if (stmt->kind == AST_STMT_MODIFIER &&
            stmt->as.modifier.exported &&
            strcmp(stmt->as.modifier.context, context) == 0 &&
            modifier_phrase_matches_main(name, stmt->as.modifier.name)) {
            return 1;
        }
    }
    return 0;
}

static int provider_file_matches_library(const char *path, const char *library_name) {
    const char *base = basename_main(path);
    size_t name_len = strlen(library_name);
    return strlen(base) == name_len + strlen(".bas") &&
        strncmp(base, library_name, name_len) == 0 &&
        strcmp(base + name_len, ".bas") == 0;
}

static void scan_program_for_provider(AddUsesContext *ctx,
                                      AstStmtList program,
                                      const char *path,
                                      const char *name,
                                      const char *context,
                                      int is_modifier,
                                      int require_provider_filename) {
    for (size_t i = 0; i < program.count; i++) {
        AstStmt *stmt = program.items[i];
        if (stmt->kind != AST_STMT_LIBRARY) {
            continue;
        }
        if (require_provider_filename && !provider_file_matches_library(path, stmt->as.library.name)) {
            continue;
        }
        int provides = is_modifier
            ? library_provides_modifier(stmt, name, context)
            : library_provides_function(stmt, name);
        if (provides) {
            provider_add(ctx, stmt->as.library.name, path);
        }
    }
}

static void scan_file_for_provider(AddUsesContext *ctx,
                                   const char *path,
                                   const char *name,
                                   const char *context,
                                   int is_modifier,
                                   int require_provider_filename) {
    if (!has_bas_extension_main(path) || strcmp(path, ctx->root_path) == 0) {
        return;
    }
    ParsedFile *file = parsed_file_get(ctx, path);
    if (file) {
        scan_program_for_provider(ctx, file->program, path, name, context, is_modifier, require_provider_filename);
    }
}

static void scan_dir_for_provider(AddUsesContext *ctx,
                                  const char *dir_path,
                                  const char *name,
                                  const char *context,
                                  int is_modifier,
                                  int require_provider_filename) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return;
    }
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char *path = join_path_main(dir_path, entry->d_name);
        struct stat st;
        if (stat(path, &st) == 0) {
            if (S_ISREG(st.st_mode)) {
                scan_file_for_provider(ctx, path, name, context, is_modifier, require_provider_filename);
            } else if (S_ISDIR(st.st_mode)) {
                scan_dir_for_provider(ctx, path, name, context, is_modifier, require_provider_filename);
            }
        }
        free(path);
    }
    closedir(dir);
}

static void scan_gbasic_path_for_provider(AddUsesContext *ctx,
                                          const char *name,
                                          const char *context,
                                          int is_modifier,
                                          int require_provider_filename) {
    const char *gbasic_path = getenv("GBASIC_PATH");
    const char *start = gbasic_path;
    while (start && *start) {
        const char *end = strchr(start, ':');
        size_t len = end ? (size_t)(end - start) : strlen(start);
        if (len > 0) {
            char *dir = malloc(len + 1);
            if (!dir) {
                abort();
            }
            memcpy(dir, start, len);
            dir[len] = '\0';
            scan_dir_for_provider(ctx, dir, name, context, is_modifier, require_provider_filename);
            free(dir);
        }
        if (!end) {
            break;
        }
        start = end + 1;
    }

#ifdef GBASIC_DEFAULT_STDLIB
    /* Fallback so --add-loads suggests stdlib providers without GBASIC_PATH set. */
    if (GBASIC_DEFAULT_STDLIB[0] != '\0') {
        scan_dir_for_provider(ctx, GBASIC_DEFAULT_STDLIB, name, context, is_modifier, require_provider_filename);
    }
#endif
}

static void scan_library_dirs_for_provider(AddUsesContext *ctx,
                                           const char *base_dir,
                                           const char *name,
                                           const char *context,
                                           int is_modifier,
                                           int require_provider_filename) {
    char *libs_dir = join_path_main(base_dir, "libs");
    scan_dir_for_provider(ctx, libs_dir, name, context, is_modifier, require_provider_filename);
    free(libs_dir);
    scan_gbasic_path_for_provider(ctx, name, context, is_modifier, require_provider_filename);
}

static const char *resolve_provider(AddUsesContext *ctx,
                                    const char *name,
                                    const char *context,
                                    int is_modifier) {
    ctx->provider_count = 0;
    scan_program_for_provider(ctx, ctx->root_program, ctx->root_path, name, context, is_modifier, 0);

    char *base_dir = dirname_copy_main(ctx->root_path);
    if (ctx->provider_count == 0) {
        scan_library_dirs_for_provider(ctx, base_dir, name, context, is_modifier, 1);
    }
    if (ctx->provider_count == 0) {
        scan_dir_for_provider(ctx, base_dir, name, context, is_modifier, 1);
    }
    if (ctx->provider_count == 0) {
        scan_library_dirs_for_provider(ctx, base_dir, name, context, is_modifier, 0);
    }
    if (ctx->provider_count == 0) {
        scan_dir_for_provider(ctx, base_dir, name, context, is_modifier, 0);
    }
    free(base_dir);

    if (ctx->provider_count == 0) {
        fprintf(stderr, "warning: unresolved %s: %s\n", is_modifier ? "modifier" : "function", name);
        return NULL;
    }

    for (size_t i = 1; i < ctx->provider_count; i++) {
        fprintf(stderr,
                "warning: additional provider for %s '%s' ignored: library %s in %s\n",
                is_modifier ? "modifier" : "function",
                name,
                ctx->providers[i].library,
                ctx->providers[i].path);
    }
    return ctx->providers[0].library;
}

static void clear_providers(AddUsesContext *ctx) {
    for (size_t i = 0; i < ctx->provider_count; i++) {
        free(ctx->providers[i].library);
        free(ctx->providers[i].path);
    }
    free(ctx->providers);
    ctx->providers = NULL;
    ctx->provider_count = 0;
}

static void maybe_add_use(AddUsesContext *ctx, const char *library) {
    if (!library ||
        string_list_contains(&ctx->existing_uses, library) ||
        string_list_contains(&ctx->insert_uses, library)) {
        return;
    }
    string_list_add(&ctx->insert_uses, library);
}

static int local_modifier_contains(StringList *list, const char *name) {
    for (size_t i = 0; i < list->count; i++) {
        if (modifier_phrase_matches_main(name, list->items[i])) {
            return 1;
        }
    }
    return 0;
}

static void analyze_expr(AddUsesContext *ctx, AstExpr *expr);

static void analyze_modifier(AddUsesContext *ctx, AstModifierUse modifier, const char *context) {
    if (!modifier.name || modifier.library ||
        builtin_modifier(modifier.name, context) ||
        local_modifier_contains(strcmp(context, "assign") == 0
                                ? &ctx->local_assign_modifiers
                                : &ctx->local_compare_modifiers,
                                modifier.name)) {
        return;
    }
    const char *library = resolve_provider(ctx, modifier.name, context, 1);
    maybe_add_use(ctx, library);
    clear_providers(ctx);
}

static void analyze_expr(AddUsesContext *ctx, AstExpr *expr) {
    if (!expr) {
        return;
    }
    switch (expr->kind) {
    case AST_EXPR_ARRAY:
        for (size_t i = 0; i < expr->as.array.count; i++) analyze_expr(ctx, expr->as.array.items[i]);
        break;
    case AST_EXPR_RECORD:
        for (size_t i = 0; i < expr->as.record.count; i++) {
            analyze_expr(ctx, expr->as.record.items[i].value);
            analyze_expr(ctx, expr->as.record.items[i].reset_expr);
        }
        break;
    case AST_EXPR_INDEX:
        analyze_expr(ctx, expr->as.index.array);
        analyze_expr(ctx, expr->as.index.index);
        break;
    case AST_EXPR_FIELD:
        analyze_expr(ctx, expr->as.field.object);
        break;
    case AST_EXPR_CALL:
        for (size_t i = 0; i < expr->as.call.args.count; i++) analyze_expr(ctx, expr->as.call.args.items[i]);
        if (expr->as.call.receiver) {
            /* A method call on an expression receiver — the name is a method, not a
             * bare library function, so it never triggers a `load` suggestion. */
            analyze_expr(ctx, expr->as.call.receiver);
            break;
        }
        if (!expr->as.call.library &&
            !builtin_function(expr->as.call.name) &&
            !string_list_contains(&ctx->local_functions, expr->as.call.name)) {
            const char *library = resolve_provider(ctx, expr->as.call.name, NULL, 0);
            maybe_add_use(ctx, library);
            clear_providers(ctx);
        }
        break;
    case AST_EXPR_BINARY:
        analyze_expr(ctx, expr->as.binary.left);
        analyze_expr(ctx, expr->as.binary.right);
        analyze_modifier(ctx, expr->as.binary.modifier, "compare");
        break;
    case AST_EXPR_UNARY:
        analyze_expr(ctx, expr->as.unary.expr);
        break;
    case AST_EXPR_NEW:
        analyze_expr(ctx, expr->as.derive.proto);
        analyze_expr(ctx, expr->as.derive.with);
        break;
    case AST_EXPR_SPAWN:
        /* The entry is always a local function; only its arguments can reference
         * library-provided helpers, so analyze those (not the entry name). */
        for (size_t i = 0; i < expr->as.call.args.count; i++) {
            analyze_expr(ctx, expr->as.call.args.items[i]);
        }
        break;
    case AST_EXPR_NUMBER:
    case AST_EXPR_STRING:
    case AST_EXPR_IDENT:
    case AST_EXPR_BOOL:
    case AST_EXPR_NULL:
    case AST_EXPR_UNKNOWN:
    case AST_EXPR_DURATION:
        break;
    }
}

static void analyze_stmt_list(AddUsesContext *ctx, AstStmtList list);
static void analyze_server_items(AddUsesContext *ctx, AstServerItemList items);

static void analyze_stmt(AddUsesContext *ctx, AstStmt *stmt) {
    switch (stmt->kind) {
    case AST_STMT_ASSIGN:
        analyze_expr(ctx, stmt->as.assign.target);
        analyze_expr(ctx, stmt->as.assign.value);
        analyze_modifier(ctx, stmt->as.assign.modifier, "assign");
        break;
    case AST_STMT_PRINT:
        analyze_expr(ctx, stmt->as.print.expr);
        break;
    case AST_STMT_EXPR:
        analyze_expr(ctx, stmt->as.expr_stmt);
        break;
    case AST_STMT_WITH_LOCK:
        analyze_expr(ctx, stmt->as.with_lock.file);
        analyze_stmt_list(ctx, stmt->as.with_lock.body);
        break;
    case AST_STMT_FOR_EACH:
        analyze_expr(ctx, stmt->as.for_each.iterable);
        analyze_stmt_list(ctx, stmt->as.for_each.body);
        break;
    case AST_STMT_RETURN:
        analyze_expr(ctx, stmt->as.return_expr);
        break;
    case AST_STMT_WATCH:
        analyze_stmt_list(ctx, stmt->as.watch.body);
        break;
    case AST_STMT_WITHOUT_WATCHERS:
        analyze_stmt_list(ctx, stmt->as.without_watchers);
        break;
    case AST_STMT_SERVER:
        /* PLAT-WEB-5: handler and hook bodies are ordinary statements and may
         * call into libraries; the block itself implies `load web`. */
        analyze_server_items(ctx, stmt->as.server.items);
        break;
    case AST_STMT_ERROR:
        analyze_expr(ctx, stmt->as.error_message);
        break;
    case AST_STMT_IF:
        analyze_expr(ctx, stmt->as.if_stmt.condition);
        analyze_stmt_list(ctx, stmt->as.if_stmt.body);
        analyze_stmt_list(ctx, stmt->as.if_stmt.else_body);
        break;
    case AST_STMT_WHILE:
        analyze_expr(ctx, stmt->as.while_stmt.condition);
        analyze_stmt_list(ctx, stmt->as.while_stmt.body);
        break;
    case AST_STMT_CONSIDER:
        analyze_expr(ctx, stmt->as.consider.subject);
        for (size_t i = 0; i < stmt->as.consider.branches.count; i++) {
            analyze_expr(ctx, stmt->as.consider.branches.items[i].match);
            analyze_stmt_list(ctx, stmt->as.consider.branches.items[i].body);
        }
        analyze_stmt_list(ctx, stmt->as.consider.else_body);
        break;
    case AST_STMT_FUNCTION:
        analyze_stmt_list(ctx, stmt->as.function.body);
        break;
    case AST_STMT_USE:
    case AST_STMT_MODIFIER:
    case AST_STMT_PROGRAM:
    case AST_STMT_LIBRARY:
    case AST_STMT_LABEL:
    case AST_STMT_GOTO:
    case AST_STMT_GOSUB:
    case AST_STMT_BREAK:
    case AST_STMT_CONTINUE:
    case AST_STMT_ON_ERROR_GOTO:
    case AST_STMT_ON_ERROR_RESUME_NEXT:
    case AST_STMT_ON_ERROR_STOP:
        break;
    }
}

static void analyze_server_items(AddUsesContext *ctx, AstServerItemList items) {
    for (size_t i = 0; i < items.count; i++) {
        AstServerItem *item = items.items[i];
        analyze_stmt_list(ctx, item->body);
        analyze_server_items(ctx, item->entries);
    }
}

static void analyze_stmt_list(AddUsesContext *ctx, AstStmtList list) {
    for (size_t i = 0; i < list.count; i++) {
        analyze_stmt(ctx, list.items[i]);
    }
}

static void collect_defs(AddUsesContext *ctx, AstStmtList list) {
    for (size_t i = 0; i < list.count; i++) {
        AstStmt *stmt = list.items[i];
        if (stmt->kind == AST_STMT_FUNCTION && stmt->as.function.name) {
            string_list_add(&ctx->local_functions, stmt->as.function.name);
        } else if (stmt->kind == AST_STMT_MODIFIER) {
            if (strcmp(stmt->as.modifier.context, "assign") == 0) {
                string_list_add(&ctx->local_assign_modifiers, stmt->as.modifier.name);
            } else if (strcmp(stmt->as.modifier.context, "compare") == 0) {
                string_list_add(&ctx->local_compare_modifiers, stmt->as.modifier.name);
            }
        } else if (stmt->kind == AST_STMT_USE) {
            string_list_add(&ctx->existing_uses, stmt->as.use_stmt.name);
        }
    }
}

static AstStmt *find_program_block(AstStmtList program) {
    for (size_t i = 0; i < program.count; i++) {
        if (program.items[i]->kind == AST_STMT_PROGRAM) {
            return program.items[i];
        }
    }
    return NULL;
}

static size_t offset_after_line(const char *source, int line) {
    int current = 1;
    for (size_t i = 0; source[i]; i++) {
        if (current == line && source[i] == '\n') {
            return i + 1;
        }
        if (source[i] == '\n') {
            current++;
        }
    }
    return strlen(source);
}

static void output_with_loads(const char *source,
                              size_t offset,
                              const char *indent,
                              const char *keyword,
                              StringList *uses) {
    fwrite(source, 1, offset, stdout);
    for (size_t i = 0; i < uses->count; i++) {
        printf("%s%s %s\n", indent, keyword, uses->items[i]);
    }
    if (uses->count > 0) {
        printf("\n");
    }
    fputs(source + offset, stdout);
}

static int add_loads_mode(const char *path,
                          const char *source,
                          AstStmtList program,
                          const char *keyword) {
    AddUsesContext ctx = {0};
    ctx.root_path = path;
    ctx.root_program = program;

    AstStmt *program_block = find_program_block(program);
    AstStmtList body = program_block ? program_block->as.program.body : program;
    collect_defs(&ctx, body);
    analyze_stmt_list(&ctx, body);

    size_t offset = program_block ? offset_after_line(source, program_block->line) : 0;
    output_with_loads(source, offset, program_block ? "    " : "", keyword, &ctx.insert_uses);

    string_list_free(&ctx.local_functions);
    string_list_free(&ctx.local_assign_modifiers);
    string_list_free(&ctx.local_compare_modifiers);
    string_list_free(&ctx.existing_uses);
    string_list_free(&ctx.insert_uses);
    clear_providers(&ctx);
    for (size_t i = 0; i < ctx.file_count; i++) {
        free(ctx.files[i].path);
        ast_free_program(ctx.files[i].program);
    }
    free(ctx.files);
    return 0;
}

/* Run as a spawned actor: gbasic --actor ENTRY PROGRAM
 *   --actor-inbox FD --actor-self FD --actor-control FD
 * (set up by the parent's spawn, docs/multiprocessing_design.md §3). */
static int run_actor_mode(int argc, char **argv) {
    const char *entry = argv[2];
    const char *prog_path = argv[3];
    int inbox_fd = -1;
    int self_fd = -1;
    int control_fd = -1;

    for (int i = 4; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "--actor-inbox") == 0) {
            inbox_fd = atoi(argv[i + 1]);
        } else if (strcmp(argv[i], "--actor-self") == 0) {
            self_fd = atoi(argv[i + 1]);
        } else if (strcmp(argv[i], "--actor-control") == 0) {
            control_fd = atoi(argv[i + 1]);
        }
    }
    if (inbox_fd < 0 || self_fd < 0 || control_fd < 0) {
        fprintf(stderr, "actor: missing mailbox descriptors\n");
        return 2;
    }

    char *source = read_file(prog_path);
    if (!source) {
        return 1;
    }
    AstStmtList program = ast_stmt_list_empty();
    parse_set_source_path(prog_path);
    if (parse_source(source, &program) != 0) {
        free(source);
        return 1;
    }
    free(source);

    eval_set_source_path(prog_path);
    int status = eval_run_actor(program, entry, inbox_fd, self_fd, control_fd);
    ast_free_program(program);
    return status;
}

/* Print every collected diagnostic to stderr. In the default (legacy) format the
 * shared gb_diag_format guarantees these bytes match what the no-sink fallback
 * would have emitted immediately; with `as_json` set (--json-diagnostics) each is
 * emitted as one JSON line instead. */
static void drain_diagnostics(gb_diagnostics *diags, int as_json) {
    for (size_t i = 0; i < gb_diagnostics_count(diags); i++) {
        const gb_diag *d = gb_diagnostics_at(diags, i);
        if (as_json) {
            gb_diag_write_json(stderr, d);
        } else {
            gb_diag_format(stderr, d);
        }
    }
}

/* PLAT-STREAM: pull a standalone, mode-independent flag out of argv so the mode
 * dispatch below keeps seeing exactly the argument shapes it always has.
 *
 * Scanning stops at the FIRST argument that does not begin with '-' -- that is
 * the FILE, and in run mode everything after it belongs to the program, not to
 * the interpreter. `gbasic prog.bas --line-buffered` therefore passes the flag
 * through to the program untouched, as any other program argument would be.
 *
 * argc/argv are rewritten in place with the flag removed. */
static int extract_flag(int *argc, char **argv, const char *flag) {
    int found = 0;
    int write_index = 1;
    for (int i = 1; i < *argc; i++) {
        if (argv[i][0] != '-') {
            while (i < *argc) {
                argv[write_index++] = argv[i++];
            }
            break;
        }
        if (strcmp(argv[i], flag) == 0) {
            found = 1;
            continue;
        }
        argv[write_index++] = argv[i];
    }
    argv[write_index] = NULL;
    *argc = write_index;
    return found;
}

int main(int argc, char **argv) {
    int ast_only = 0;
    int tokens_only = 0;
    int json_diagnostics = 0;
    const char *add_loads_keyword = NULL;
    const char *path = NULL;
    char *const *program_args = NULL;
    size_t program_arg_count = 0;

    if (argc >= 4 && strcmp(argv[1], "--actor") == 0) {
        return run_actor_mode(argc, argv);
    }

    /* When stdout is a pipe, stdio buffers it in ~4 KB blocks, so a program that
     * prints slowly appears silent to whatever is reading it until it exits. Line
     * buffering flushes at every completed line instead. Every `print` this
     * runtime emits ends in a newline, and the one partial line it can produce (an
     * `input` prompt) is already fflushed explicitly, so this makes the whole
     * output surface prompt. Must run before any output. */
    if (extract_flag(&argc, argv, "--line-buffered")) {
        setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
    }

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_help(argv[0]);
        return 0;
    } else if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("gBASIC 0.1.0-rc3\n");
        return 0;
    } else if (argc == 3 && strcmp(argv[1], "--ast") == 0) {
        ast_only = 1;
        path = argv[2];
    } else if (argc == 3 && strcmp(argv[1], "--tokens") == 0) {
        tokens_only = 1;
        path = argv[2];
    } else if (argc == 3 && strcmp(argv[1], "--add-loads") == 0) {
        add_loads_keyword = "load";
        path = argv[2];
    } else if (argc == 3 && strcmp(argv[1], "--add-uses") == 0) {
        add_loads_keyword = "use";
        path = argv[2];
    } else if (argc >= 3 && strcmp(argv[1], "--json-diagnostics") == 0) {
        /* This mode RUNS the program (it falls through to eval_program below,
         * unlike --ast/--tokens/--add-loads which only inspect it), so program
         * arguments are meaningful here and are bound exactly as in run mode.
         * They were rejected until PLAT-DEBT 2 purely because the dispatch
         * matched argc == 3; the plumbing was already in place. */
        json_diagnostics = 1;
        path = argv[2];
        program_args = &argv[3];
        program_arg_count = (size_t)(argc - 3);
    } else if (argc >= 2 && argv[1][0] != '-') {
        /* Run mode: FILE followed by zero or more program arguments. The args
         * after the script path bind to a `program NAME(param)` block's
         * parameter (see eval_set_program_args). */
        path = argv[1];
        program_args = &argv[2];
        program_arg_count = (size_t)(argc - 2);
    } else {
        /* Two shapes, because only the modes that RUN the program can use
         * program arguments. The inspect-only modes reject them rather than
         * accepting and ignoring them, which would be a silent wrong answer. */
        fprintf(stderr, "usage: %s [--line-buffered] [--json-diagnostics] FILE [args...]\n", argv[0]);
        fprintf(stderr, "       %s [--line-buffered] [--ast|--tokens|--add-loads|--add-uses] FILE\n", argv[0]);
        fprintf(stderr, "try '%s --help'\n", argv[0]);
        return 2;
    }

    char *source = read_file(path);
    if (!source) {
        return 1;
    }

    if (tokens_only) {
        print_tokens(source);
        free(source);
        return 0;
    }

    gb_diagnostics diags;
    gb_diagnostics_init(&diags);

    AstStmtList program = ast_stmt_list_empty();
    if (gb_parse(source, path, &program, &diags) != 0) {
        /* A failed LOAD-TIME VALIDATION (PLAT-WEB-5 server blocks) still hands
         * back the parsed AST; a failed parse hands back an empty list. Free
         * either, so a refused program is not also a leaked one. */
        ast_free_program(program);
        drain_diagnostics(&diags, json_diagnostics);
        gb_diagnostics_free(&diags);
        free(source);
        return 1;
    }

    int exit_status = 0;
    if (ast_only) {
        ast_dump(program);
    } else if (add_loads_keyword) {
        exit_status = add_loads_mode(path, source, program, add_loads_keyword);
    } else {
        eval_set_source_path(path);
        eval_set_program_args(program_args, program_arg_count);
        /* Install the sink so a STOP-mode runtime error is collected rather than
         * printed mid-run, then drained below in the exact legacy format. The
         * error is terminal, so it stays the last line on stderr. */
        gb_set_active_sink(&diags);
        exit_status = eval_program(program);
        gb_set_active_sink(NULL);
        drain_diagnostics(&diags, json_diagnostics);
    }

    gb_diagnostics_free(&diags);
    ast_free_program(program);
    free(source);
    return exit_status;
}
