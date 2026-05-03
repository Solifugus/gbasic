#include "ast.h"
#include "eval.h"
#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>

extern int yyparse(void);
extern StmtList parsed_program;

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
        abort();
    }
    size_t read = fread(source, 1, (size_t)size, file);
    if (read != (size_t)size && ferror(file)) {
        perror(path);
        free(source);
        fclose(file);
        return NULL;
    }
    source[read] = '\0';
    fclose(file);
    return source;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s program.gb\n", argv[0]);
        return 2;
    }

    char *source = read_file(argv[1]);
    if (!source) {
        return 1;
    }

    lexer_init(source);
    if (yyparse() != 0) {
        free(source);
        return 1;
    }

    int status = eval_program(parsed_program);
    ast_free_statements(parsed_program);
    free(source);
    return status;
}
