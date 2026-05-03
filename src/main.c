#include "ast.h"
#include "eval.h"
#include "lexer.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int parse_source(const char *source, AstStmtList *out_program);

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

int main(int argc, char **argv) {
    int ast_only = 0;
    int tokens_only = 0;
    const char *path = NULL;

    if (argc == 2) {
        path = argv[1];
    } else if (argc == 3 && strcmp(argv[1], "--ast") == 0) {
        ast_only = 1;
        path = argv[2];
    } else if (argc == 3 && strcmp(argv[1], "--tokens") == 0) {
        tokens_only = 1;
        path = argv[2];
    } else {
        fprintf(stderr, "usage: %s [--ast|--tokens] file.gb\n", argv[0]);
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

    AstStmtList program = ast_stmt_list_empty();
    if (parse_source(source, &program) != 0) {
        free(source);
        return 1;
    }

    if (ast_only) {
        ast_dump(program);
    } else {
        eval_program(program);
    }

    ast_free_program(program);
    free(source);
    return 0;
}
