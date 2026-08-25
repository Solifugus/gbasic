/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 2

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 1 "src/parser.y"

#include "ast.h"
#include "builtins.h"
#include "diagnostics.h"
#include "lexer.h"
#include "parse_ctx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A keyword used as a FIELD NAME arrives as its own token, carrying no text,
 * so the spelling is supplied here. Local rather than reusing eval.c's
 * copy_string: the parser is a separate translation unit and strdup is not in
 * strict C11. */
static char *kw_name(const char *s) {
    size_t n = strlen(s) + 1;
    char *out = malloc(n);
    if (!out) abort();
    memcpy(out, s, n);
    return out;
}

/* Report a diagnostic with an explicit span. Routes through gb_report_to, which
 * pushes to the per-parse sink (ctx->diags) or, when that is NULL, prints in the
 * legacy stderr format. */
static void report_diag(gb_parse_ctx *ctx, gb_diag_code code, int line, int column,
                        int end_line, int end_column, const char *message) {
    gb_span span = { line, column, end_line, end_column };
    gb_report_to(ctx->diags, code, 0, ctx->active_parse_path, span, message);
}


/* PLAT-WARN: `on warning ...` and `warning <expr>` carry NO reserved word --
 * the channel is an ordinary IDENT recognized by POSITION (the technique the
 * server block proved) and validated here. A word that is not "warning" is
 * named in the diagnostic rather than producing a bare syntax error, because
 * `on wanring stop` should say so. */
static int warn_channel_ok(gb_parse_ctx *ctx, const char *word,
                           int line, int column) {
    if (word && strcmp(word, "warning") == 0) {
        return 1;
    }
    char message[192];
    snprintf(message, sizeof(message),
             "unknown diagnostic channel '%s'; `on` takes `error` or `warning`",
             word ? word : "");
    report_diag(ctx, GB_DIAG_PARSE_ERROR, line, column, line, column, message);
    return 0;
}

static int warn_mode_word(gb_parse_ctx *ctx, const char *word,
                          int line, int column) {
    if (word && strcmp(word, "ignore") == 0) {
        return WARN_MODE_IGNORE;
    }
    char message[192];
    snprintf(message, sizeof(message),
             "unknown warning mode '%s'; expected print, ignore, stop, or goto next",
             word ? word : "");
    report_diag(ctx, GB_DIAG_PARSE_ERROR, line, column, line, column, message);
    return -1;
}

/* Same, computing the end position by walking `len` bytes of the lexeme exactly
 * as the lexer's advance() does (byte-based columns, '\n' resets to column 1). */
static void report_diag_lexeme(gb_parse_ctx *ctx, gb_diag_code code, int line, int column,
                               const char *text, int len, const char *message) {
    int end_line = line;
    int end_column = column;
    for (int i = 0; i < len; i++) {
        if (text[i] == '\n') {
            end_line++;
            end_column = 1;
        } else {
            end_column++;
        }
    }
    report_diag(ctx, code, line, column, end_line, end_column, message);
}

static char *copy_text(const char *start, int length) {
    char *text = malloc((size_t)length + 1);
    if (!text) {
        abort();
    }
    memcpy(text, start, (size_t)length);
    text[length] = '\0';
    return text;
}

static int hex_digit_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

/* Encode a Unicode scalar value as UTF-8 into out (up to 4 bytes); returns the
 * byte count. Caller guarantees a valid scalar (0..0x10FFFF, no surrogate). */
static int utf8_encode_literal(unsigned cp, char out[4]) {
    if (cp <= 0x7fu) {
        out[0] = (char)cp;
        return 1;
    } else if (cp <= 0x7ffu) {
        out[0] = (char)(0xc0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3fu));
        return 2;
    } else if (cp <= 0xffffu) {
        out[0] = (char)(0xe0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3fu));
        out[2] = (char)(0x80u | (cp & 0x3fu));
        return 3;
    }
    out[0] = (char)(0xf0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 0x3fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3fu));
    out[3] = (char)(0x80u | (cp & 0x3fu));
    return 4;
}

static char *copy_string_literal(gb_parse_ctx *ctx, const char *start, int length, int line, int column, int *ok) {
    *ok = 1;
    if (length < 2) {
        return copy_text("", 0);
    }

    char *text = malloc((size_t)length - 1);
    if (!text) {
        abort();
    }
    int out = 0;
    for (int i = 1; i < length - 1; i++) {
        if (start[i] == '\\') {
            if (i + 1 >= length - 1) {
                report_diag_lexeme(ctx, GB_DIAG_STRING_LITERAL, line, column, start, length, "unterminated escape sequence");
                *ok = 0;
                free(text);
                return NULL;
            }
            i++;
            if (start[i] == 'n') {
                text[out++] = '\n';
            } else if (start[i] == 't') {
                text[out++] = '\t';
            } else if (start[i] == '"' || start[i] == '\\') {
                text[out++] = start[i];
            } else if (start[i] == 'u') {
                /* \u{HHHH}: decode a Unicode scalar to UTF-8. The lexer already
                 * guaranteed the { hexdigits } shape, so just read it. */
                i++; /* the '{' */
                unsigned cp = 0;
                int digits = 0;
                i++; /* first hex digit */
                while (i < length - 1 && start[i] != '}') {
                    cp = cp * 16u + (unsigned)hex_digit_value(start[i]);
                    digits++;
                    i++;
                }
                /* i now points at '}', which the for-loop's i++ will consume. */
                if (digits > 6 || cp > 0x10FFFFu) {
                    report_diag_lexeme(ctx, GB_DIAG_STRING_LITERAL, line, column, start, length,
                                       "invalid unicode escape: codepoint must be between 0 and 0x10FFFF");
                    *ok = 0;
                    free(text);
                    return NULL;
                }
                if (cp >= 0xD800u && cp <= 0xDFFFu) {
                    report_diag_lexeme(ctx, GB_DIAG_STRING_LITERAL, line, column, start, length,
                                       "invalid unicode escape: surrogate codepoints (0xD800..0xDFFF) are not valid");
                    *ok = 0;
                    free(text);
                    return NULL;
                }
                if (cp == 0) {
                    report_diag_lexeme(ctx, GB_DIAG_STRING_LITERAL, line, column, start, length,
                                       "invalid unicode escape: \\u{0} is not allowed in a literal; use chr(0)");
                    *ok = 0;
                    free(text);
                    return NULL;
                }
                char utf8[4];
                int n = utf8_encode_literal(cp, utf8);
                for (int b = 0; b < n; b++) {
                    text[out++] = utf8[b];
                }
            } else {
                char message[64];
                snprintf(message, sizeof(message), "invalid escape sequence: \\%c", start[i]);
                report_diag_lexeme(ctx, GB_DIAG_STRING_LITERAL, line, column, start, length, message);
                *ok = 0;
                free(text);
                return NULL;
            }
        } else {
            text[out++] = start[i];
        }
    }
    text[out] = '\0';
    return text;
}

static char *copy_const(const char *text) {
    return copy_text(text, (int)strlen(text));
}

static char *join_watch_path(char *left, char *right) {
    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    char *text = malloc(left_len + 1 + right_len + 1);
    if (!text) {
        abort();
    }
    memcpy(text, left, left_len);
    text[left_len] = '.';
    memcpy(text + left_len + 1, right, right_len + 1);
    free(left);
    free(right);
    return text;
}

static void split_qualified_ident(char *text, char **library, char **name) {
    char *dot = strchr(text, '.');
    if (!dot) {
        *library = text;
        *name = copy_const("");
        return;
    }
    *dot = '\0';
    *library = text;
    *name = copy_const(dot + 1);
}

static int ascii_lower(int ch) {
    return ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch;
}

static int is_ident_start_char(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
}

static int is_ident_char(char ch) {
    return is_ident_start_char(ch) || (ch >= '0' && ch <= '9');
}

static int keyword_at(const char *p, const char *keyword) {
    const char *k = keyword;
    while (*k) {
        if (ascii_lower((unsigned char)*p) != *k) {
            return 0;
        }
        p++;
        k++;
    }
    return !is_ident_char(*p);
}

static int source_declares_function(gb_parse_ctx *ctx, const char *name) {
    const char *p = ctx->active_lexer->source;
    size_t name_len = strlen(name);
    int in_string = 0;
    int in_comment = 0;

    while (*p) {
        if (in_comment) {
            if (*p == '\n') {
                in_comment = 0;
            }
            p++;
            continue;
        }
        if (in_string) {
            if (*p == '\\' && p[1]) {
                p += 2;
                continue;
            }
            if (*p == '"') {
                in_string = 0;
            }
            p++;
            continue;
        }
        if (*p == '\'') {
            in_comment = 1;
            p++;
            continue;
        }
        if (*p == '"') {
            in_string = 1;
            p++;
            continue;
        }
        if ((p == ctx->active_lexer->source || !is_ident_char(p[-1])) &&
            keyword_at(p, "function")) {
            p += strlen("function");
            while (*p == ' ' || *p == '\t' || *p == '\r') {
                p++;
            }
            const char *start = p;
            if (!is_ident_start_char(*p)) {
                continue;
            }
            while (is_ident_char(*p)) {
                p++;
            }
            if ((size_t)(p - start) == name_len && strncmp(start, name, name_len) == 0) {
                return 1;
            }
            continue;
        }
        p++;
    }

    return 0;
}

static int is_modifier_target_expr(AstExpr *expr) {
    if (!expr) {
        return 0;
    }
    if (expr->kind == AST_EXPR_IDENT) {
        return 1;
    }
    if (expr->kind == AST_EXPR_FIELD) {
        return is_modifier_target_expr(expr->as.field.object);
    }
    if (expr->kind == AST_EXPR_INDEX) {
        return is_modifier_target_expr(expr->as.index.array);
    }
    return 0;
}

static AstExpr *expr_at(AstExpr *expr, int line, int column) {
    return ast_expr_position(expr, line, column);
}

static char *join_words(char *left, char *right) {
    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    char *joined = malloc(left_len + right_len + 2);
    if (!joined) {
        abort();
    }
    memcpy(joined, left, left_len);
    joined[left_len] = ' ';
    memcpy(joined + left_len + 1, right, right_len + 1);
    free(left);
    free(right);
    return joined;
}

static AstModifierUse parse_modifier_use(char *text) {
    AstModifierUse modifier = ast_modifier_use(text, ast_expr_list_empty());
    char *dot = strchr(modifier.name, '.');
    if (!dot) {
        return modifier;
    }

    char *start = modifier.name;
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    char *library_end = dot;
    while (library_end > start && (library_end[-1] == ' ' || library_end[-1] == '\t')) {
        library_end--;
    }
    char *name_start = dot + 1;
    while (*name_start == ' ' || *name_start == '\t') {
        name_start++;
    }
    if (library_end == start || *name_start == '\0') {
        return modifier;
    }

    size_t library_len = (size_t)(library_end - start);
    char *library = malloc(library_len + 1);
    if (!library) {
        abort();
    }
    memcpy(library, start, library_len);
    library[library_len] = '\0';

    char *name = copy_const(name_start);
    free(modifier.name);
    modifier.library = library;
    modifier.name = name;
    return modifier;
}

static int unit_is(const char *text, const char *unit) {
    return strcmp(text, unit) == 0;
}

static AstDuration duration_add_unit(AstDuration duration, double amount, char *unit) {
    int value = (int)amount;
    if (unit_is(unit, "year") || unit_is(unit, "years")) {
        duration.years += value;
    } else if (unit_is(unit, "month") || unit_is(unit, "months")) {
        duration.months += value;
    } else if (unit_is(unit, "week") || unit_is(unit, "weeks")) {
        duration.weeks += value;
    } else if (unit_is(unit, "day") || unit_is(unit, "days")) {
        duration.days += value;
    } else if (unit_is(unit, "hour") || unit_is(unit, "hours")) {
        duration.hours += value;
    } else if (unit_is(unit, "minute") || unit_is(unit, "minutes")) {
        duration.minutes += value;
    } else if (unit_is(unit, "second") || unit_is(unit, "seconds")) {
        duration.seconds += value;
    } else {
        fprintf(stderr, "unknown duration unit: %s\n", unit);
    }
    free(unit);
    return duration;
}



#line 490 "src/parser.tab.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "parser.tab.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_NUMBER = 3,                     /* NUMBER  */
  YYSYMBOL_IDENT = 4,                      /* IDENT  */
  YYSYMBOL_STRING = 5,                     /* STRING  */
  YYSYMBOL_LENS_CONTENT = 6,               /* LENS_CONTENT  */
  YYSYMBOL_QUALIFIED_IDENT = 7,            /* QUALIFIED_IDENT  */
  YYSYMBOL_AS = 8,                         /* AS  */
  YYSYMBOL_IF = 9,                         /* IF  */
  YYSYMBOL_CONSIDER_IF = 10,               /* CONSIDER_IF  */
  YYSYMBOL_THEN = 11,                      /* THEN  */
  YYSYMBOL_ELSE = 12,                      /* ELSE  */
  YYSYMBOL_CONSIDER_ELSE = 13,             /* CONSIDER_ELSE  */
  YYSYMBOL_END = 14,                       /* END  */
  YYSYMBOL_END_CONSIDER = 15,              /* END_CONSIDER  */
  YYSYMBOL_PRINT = 16,                     /* PRINT  */
  YYSYMBOL_TRUE = 17,                      /* TRUE  */
  YYSYMBOL_FALSE = 18,                     /* FALSE  */
  YYSYMBOL_NOTHING = 19,                   /* NOTHING  */
  YYSYMBOL_UNKNOWN_VALUE = 20,             /* UNKNOWN_VALUE  */
  YYSYMBOL_AND = 21,                       /* AND  */
  YYSYMBOL_OR = 22,                        /* OR  */
  YYSYMBOL_NOT = 23,                       /* NOT  */
  YYSYMBOL_WITH = 24,                      /* WITH  */
  YYSYMBOL_NEW = 25,                       /* NEW  */
  YYSYMBOL_SPAWN = 26,                     /* SPAWN  */
  YYSYMBOL_FOR = 27,                       /* FOR  */
  YYSYMBOL_TO = 28,                        /* TO  */
  YYSYMBOL_STEP = 29,                      /* STEP  */
  YYSYMBOL_DO = 30,                        /* DO  */
  YYSYMBOL_LOOP = 31,                      /* LOOP  */
  YYSYMBOL_UNTIL = 32,                     /* UNTIL  */
  YYSYMBOL_IN = 33,                        /* IN  */
  YYSYMBOL_EACH = 34,                      /* EACH  */
  YYSYMBOL_WHILE = 35,                     /* WHILE  */
  YYSYMBOL_CONSIDER = 36,                  /* CONSIDER  */
  YYSYMBOL_BREAK = 37,                     /* BREAK  */
  YYSYMBOL_CONTINUE = 38,                  /* CONTINUE  */
  YYSYMBOL_FUNCTION = 39,                  /* FUNCTION  */
  YYSYMBOL_RETURN = 40,                    /* RETURN  */
  YYSYMBOL_GOTO = 41,                      /* GOTO  */
  YYSYMBOL_GOSUB = 42,                     /* GOSUB  */
  YYSYMBOL_WATCH = 43,                     /* WATCH  */
  YYSYMBOL_UNWATCH = 44,                   /* UNWATCH  */
  YYSYMBOL_WITHOUT = 45,                   /* WITHOUT  */
  YYSYMBOL_WATCHERS = 46,                  /* WATCHERS  */
  YYSYMBOL_ON = 47,                        /* ON  */
  YYSYMBOL_NEXT = 48,                      /* NEXT  */
  YYSYMBOL_STOP = 49,                      /* STOP  */
  YYSYMBOL_ERROR_VALUE = 50,               /* ERROR_VALUE  */
  YYSYMBOL_MODIFIER = 51,                  /* MODIFIER  */
  YYSYMBOL_PROGRAM = 52,                   /* PROGRAM  */
  YYSYMBOL_LIBRARY = 53,                   /* LIBRARY  */
  YYSYMBOL_LOAD = 54,                      /* LOAD  */
  YYSYMBOL_USE = 55,                       /* USE  */
  YYSYMBOL_EXPORT = 56,                    /* EXPORT  */
  YYSYMBOL_OP_EQ = 57,                     /* OP_EQ  */
  YYSYMBOL_OP_NE = 58,                     /* OP_NE  */
  YYSYMBOL_OP_GT = 59,                     /* OP_GT  */
  YYSYMBOL_OP_LT = 60,                     /* OP_LT  */
  YYSYMBOL_OP_GE = 61,                     /* OP_GE  */
  YYSYMBOL_OP_LE = 62,                     /* OP_LE  */
  YYSYMBOL_OP_NGT = 63,                    /* OP_NGT  */
  YYSYMBOL_OP_NLT = 64,                    /* OP_NLT  */
  YYSYMBOL_OP_NGE = 65,                    /* OP_NGE  */
  YYSYMBOL_OP_NLE = 66,                    /* OP_NLE  */
  YYSYMBOL_PLUS = 67,                      /* PLUS  */
  YYSYMBOL_MINUS = 68,                     /* MINUS  */
  YYSYMBOL_STAR = 69,                      /* STAR  */
  YYSYMBOL_SLASH = 70,                     /* SLASH  */
  YYSYMBOL_LPAREN = 71,                    /* LPAREN  */
  YYSYMBOL_RPAREN = 72,                    /* RPAREN  */
  YYSYMBOL_LBRACKET = 73,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 74,                  /* RBRACKET  */
  YYSYMBOL_LBRACE = 75,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 76,                    /* RBRACE  */
  YYSYMBOL_COMMA = 77,                     /* COMMA  */
  YYSYMBOL_COLON = 78,                     /* COLON  */
  YYSYMBOL_NEWLINE = 79,                   /* NEWLINE  */
  YYSYMBOL_IF_WITHOUT_ELSE = 80,           /* IF_WITHOUT_ELSE  */
  YYSYMBOL_NO_DOT = 81,                    /* NO_DOT  */
  YYSYMBOL_DOT = 82,                       /* DOT  */
  YYSYMBOL_YYACCEPT = 83,                  /* $accept  */
  YYSYMBOL_program = 84,                   /* program  */
  YYSYMBOL_statement_list = 85,            /* statement_list  */
  YYSYMBOL_statement = 86,                 /* statement  */
  YYSYMBOL_assignment = 87,                /* assignment  */
  YYSYMBOL_lvalue = 88,                    /* lvalue  */
  YYSYMBOL_variable_name = 89,             /* variable_name  */
  YYSYMBOL_comparison_lens = 90,           /* comparison_lens  */
  YYSYMBOL_91_1 = 91,                      /* $@1  */
  YYSYMBOL_modifier_name = 92,             /* modifier_name  */
  YYSYMBOL_modifier_word = 93,             /* modifier_word  */
  YYSYMBOL_print_statement = 94,           /* print_statement  */
  YYSYMBOL_call_statement = 95,            /* call_statement  */
  YYSYMBOL_with_lock_statement = 96,       /* with_lock_statement  */
  YYSYMBOL_for_each_statement = 97,        /* for_each_statement  */
  YYSYMBOL_do_loop_statement = 98,         /* do_loop_statement  */
  YYSYMBOL_while_statement = 99,           /* while_statement  */
  YYSYMBOL_consider_statement = 100,       /* consider_statement  */
  YYSYMBOL_consider_branch_list = 101,     /* consider_branch_list  */
  YYSYMBOL_consider_else_opt = 102,        /* consider_else_opt  */
  YYSYMBOL_consider_statement_list = 103,  /* consider_statement_list  */
  YYSYMBOL_consider_body_statement = 104,  /* consider_body_statement  */
  YYSYMBOL_function_statement = 105,       /* function_statement  */
  YYSYMBOL_modifier_statement = 106,       /* modifier_statement  */
  YYSYMBOL_program_statement = 107,        /* program_statement  */
  YYSYMBOL_library_statement = 108,        /* library_statement  */
  YYSYMBOL_use_statement = 109,            /* use_statement  */
  YYSYMBOL_modifier_signature = 110,       /* modifier_signature  */
  YYSYMBOL_modifier_context = 111,         /* modifier_context  */
  YYSYMBOL_watch_statement = 112,          /* watch_statement  */
  YYSYMBOL_unwatch_statement = 113,        /* unwatch_statement  */
  YYSYMBOL_watch_target_list = 114,        /* watch_target_list  */
  YYSYMBOL_server_statement = 115,         /* server_statement  */
  YYSYMBOL_server_item_list = 116,         /* server_item_list  */
  YYSYMBOL_server_item = 117,              /* server_item  */
  YYSYMBOL_server_string_list = 118,       /* server_string_list  */
  YYSYMBOL_watch_target_path = 119,        /* watch_target_path  */
  YYSYMBOL_without_watchers_statement = 120, /* without_watchers_statement  */
  YYSYMBOL_on_error_statement = 121,       /* on_error_statement  */
  YYSYMBOL_error_statement = 122,          /* error_statement  */
  YYSYMBOL_return_statement = 123,         /* return_statement  */
  YYSYMBOL_label_statement = 124,          /* label_statement  */
  YYSYMBOL_goto_statement = 125,           /* goto_statement  */
  YYSYMBOL_gosub_statement = 126,          /* gosub_statement  */
  YYSYMBOL_break_statement = 127,          /* break_statement  */
  YYSYMBOL_continue_statement = 128,       /* continue_statement  */
  YYSYMBOL_if_statement = 129,             /* if_statement  */
  YYSYMBOL_if_block_tail = 130,            /* if_block_tail  */
  YYSYMBOL_if_inline_tail = 131,           /* if_inline_tail  */
  YYSYMBOL_inline_statement = 132,         /* inline_statement  */
  YYSYMBOL_expression = 133,               /* expression  */
  YYSYMBOL_or_expression = 134,            /* or_expression  */
  YYSYMBOL_and_expression = 135,           /* and_expression  */
  YYSYMBOL_comparison_expression = 136,    /* comparison_expression  */
  YYSYMBOL_additive_expression = 137,      /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 138, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 139,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 140,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 141,      /* comparison_operator  */
  YYSYMBOL_primary = 142,                  /* primary  */
  YYSYMBOL_record_literal = 143,           /* record_literal  */
  YYSYMBOL_ident_suffix = 144,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 145,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 146,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 147,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 148,            /* argument_list  */
  YYSYMBOL_array_argument_list = 149,      /* array_argument_list  */
  YYSYMBOL_parameter_list_opt = 150,       /* parameter_list_opt  */
  YYSYMBOL_parameter_list = 151,           /* parameter_list  */
  YYSYMBOL_field_name = 152,               /* field_name  */
  YYSYMBOL_dot_field_name = 153,           /* dot_field_name  */
  YYSYMBOL_record_field_list = 154,        /* record_field_list  */
  YYSYMBOL_field_policy = 155,             /* field_policy  */
  YYSYMBOL_optional_newlines = 156         /* optional_newlines  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;



/* Unqualified %code blocks.  */
#line 486 "src/parser.y"

static int yylex(YYSTYPE *lvalp, YYLTYPE *llocp, gb_parse_ctx *ctx);
static void yyerror(YYLTYPE *llocp, gb_parse_ctx *ctx, const char *message);
static void report_syntax_error(gb_parse_ctx *ctx, int line, int column,
                                int end_line, int end_column, const char *message);

#line 688 "src/parser.tab.c"

#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_int16 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if 1

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* 1 */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL \
             && defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
  YYLTYPE yyls_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE) \
             + YYSIZEOF (YYLTYPE)) \
      + 2 * YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  3
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   2612

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  83
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  74
/* YYNRULES -- Number of rules.  */
#define YYNRULES  297
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  644

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   337


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   549,   549,   553,   554,   555,   559,   560,   561,   562,
     563,   564,   565,   566,   567,   568,   569,   570,   571,   572,
     573,   574,   575,   576,   577,   578,   579,   580,   581,   582,
     583,   584,   588,   592,   604,   605,   606,   610,   611,   612,
     617,   618,   622,   622,   628,   629,   633,   634,   635,   636,
     640,   646,   650,   651,   657,   662,   671,   684,   699,   702,
     708,   711,   719,   722,   728,   734,   740,   743,   749,   750,
     754,   755,   756,   760,   761,   762,   763,   764,   765,   766,
     767,   768,   769,   770,   771,   772,   773,   774,   775,   776,
     777,   778,   779,   780,   781,   782,   783,   784,   788,   791,
     798,   801,   807,   813,   819,   820,   821,   822,   823,   839,
     858,   859,   863,   867,   870,   878,   884,   888,   889,   908,
     911,   917,   918,   919,   923,   926,   929,   932,   935,   941,
     942,   946,   947,   951,   957,   958,   959,   960,   965,   978,
     983,   988,   998,  1002,  1003,  1007,  1014,  1018,  1022,  1026,
    1030,  1034,  1041,  1044,  1047,  1053,  1056,  1059,  1065,  1066,
    1067,  1068,  1069,  1070,  1071,  1072,  1073,  1074,  1075,  1079,
    1083,  1084,  1088,  1089,  1093,  1094,  1095,  1101,  1102,  1103,
    1107,  1108,  1109,  1113,  1114,  1115,  1116,  1117,  1118,  1122,
    1123,  1124,  1125,  1130,  1144,  1145,  1146,  1147,  1148,  1149,
    1150,  1151,  1152,  1153,  1157,  1158,  1159,  1160,  1161,  1178,
    1184,  1185,  1186,  1187,  1188,  1189,  1190,  1191,  1192,  1196,
    1197,  1201,  1206,  1211,  1217,  1229,  1234,  1242,  1246,  1252,
    1253,  1257,  1258,  1262,  1263,  1267,  1268,  1272,  1273,  1286,
    1293,  1302,  1303,  1304,  1305,  1306,  1307,  1308,  1309,  1310,
    1311,  1312,  1313,  1314,  1315,  1316,  1317,  1318,  1319,  1320,
    1321,  1322,  1323,  1324,  1325,  1326,  1327,  1328,  1329,  1330,
    1331,  1332,  1333,  1334,  1335,  1336,  1337,  1338,  1339,  1340,
    1341,  1342,  1343,  1344,  1345,  1346,  1347,  1348,  1352,  1353,
    1354,  1355,  1356,  1357,  1365,  1392,  1411,  1412
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if 1
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "NUMBER", "IDENT",
  "STRING", "LENS_CONTENT", "QUALIFIED_IDENT", "AS", "IF", "CONSIDER_IF",
  "THEN", "ELSE", "CONSIDER_ELSE", "END", "END_CONSIDER", "PRINT", "TRUE",
  "FALSE", "NOTHING", "UNKNOWN_VALUE", "AND", "OR", "NOT", "WITH", "NEW",
  "SPAWN", "FOR", "TO", "STEP", "DO", "LOOP", "UNTIL", "IN", "EACH",
  "WHILE", "CONSIDER", "BREAK", "CONTINUE", "FUNCTION", "RETURN", "GOTO",
  "GOSUB", "WATCH", "UNWATCH", "WITHOUT", "WATCHERS", "ON", "NEXT", "STOP",
  "ERROR_VALUE", "MODIFIER", "PROGRAM", "LIBRARY", "LOAD", "USE", "EXPORT",
  "OP_EQ", "OP_NE", "OP_GT", "OP_LT", "OP_GE", "OP_LE", "OP_NGT", "OP_NLT",
  "OP_NGE", "OP_NLE", "PLUS", "MINUS", "STAR", "SLASH", "LPAREN", "RPAREN",
  "LBRACKET", "RBRACKET", "LBRACE", "RBRACE", "COMMA", "COLON", "NEWLINE",
  "IF_WITHOUT_ELSE", "NO_DOT", "DOT", "$accept", "program",
  "statement_list", "statement", "assignment", "lvalue", "variable_name",
  "comparison_lens", "$@1", "modifier_name", "modifier_word",
  "print_statement", "call_statement", "with_lock_statement",
  "for_each_statement", "do_loop_statement", "while_statement",
  "consider_statement", "consider_branch_list", "consider_else_opt",
  "consider_statement_list", "consider_body_statement",
  "function_statement", "modifier_statement", "program_statement",
  "library_statement", "use_statement", "modifier_signature",
  "modifier_context", "watch_statement", "unwatch_statement",
  "watch_target_list", "server_statement", "server_item_list",
  "server_item", "server_string_list", "watch_target_path",
  "without_watchers_statement", "on_error_statement", "error_statement",
  "return_statement", "label_statement", "goto_statement",
  "gosub_statement", "break_statement", "continue_statement",
  "if_statement", "if_block_tail", "if_inline_tail", "inline_statement",
  "expression", "or_expression", "and_expression", "comparison_expression",
  "additive_expression", "multiplicative_expression", "unary_expression",
  "postfix_expression", "comparison_operator", "primary", "record_literal",
  "ident_suffix", "ident_dot_suffix", "duration_terms",
  "argument_list_opt", "argument_list", "array_argument_list",
  "parameter_list_opt", "parameter_list", "field_name", "dot_field_name",
  "record_field_list", "field_policy", "optional_newlines", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-478)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -478,     7,   848,  -478,    60,   -48,  2226,  -478,  2191,    32,
     116,    -8,  -478,  -478,  2226,  2226,  -478,  -478,   187,  2226,
     139,   139,   188,  2226,    38,    55,  -478,   158,   145,    87,
     109,    73,   162,    97,  -478,  -478,    50,   125,    76,    95,
     126,  -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,
     132,  -478,   155,  -478,  -478,   164,   183,   190,   193,   195,
     200,   202,   203,  -478,   161,  2226,  2226,   280,  -478,  -478,
     215,  -478,  -478,  -478,  -478,  2226,  2264,   283,   219,  -478,
    2226,  2226,  -478,  -478,    35,   282,   269,   273,  -478,   498,
     176,  -478,   106,  -478,  -478,   292,   247,  -478,   227,    40,
     297,  -478,   226,   228,   238,   239,  -478,  -478,  -478,   242,
     139,  -478,    81,   233,  -478,   237,    98,   103,   314,  -478,
    -478,  -478,  -478,  -478,    62,  -478,   293,   250,   244,   320,
    -478,   321,  -478,   145,  -478,  2226,  2226,  -478,  2450,   270,
    -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,
    -478,  -478,  -478,  2336,  -478,   256,   252,   259,  -478,  2226,
    -478,   -15,   263,   264,  -478,   266,   522,   676,  2226,  2503,
    -478,  2067,  2226,  2226,  -478,  -478,  -478,  -478,  -478,  -478,
    -478,  -478,  -478,  -478,  2226,  2226,   191,  2226,  2226,  2226,
    2226,  2556,   331,  2226,  2226,  2226,  2226,   306,   901,  -478,
     330,   337,   337,   139,    74,   139,  -478,   340,  -478,  -478,
    -478,    56,  -478,    64,  -478,   277,   337,  -478,   346,   337,
    -478,   347,   358,   344,  -478,   298,   367,   305,   307,  -478,
    -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,
    -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,
    -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,
    -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,
    -478,  -478,  -478,  -478,  -478,  -478,  2226,   309,  -478,   303,
      18,  -478,   108,  -478,  2226,  -478,   313,   302,  2226,  -478,
    -478,  -478,  -478,  -478,   315,  -478,   318,   319,  -478,   327,
     328,   329,  -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,
    -478,  -478,  -478,  -478,  -478,   311,   273,  -478,   176,   176,
    2226,   160,  -478,  -478,   312,   332,   333,  -478,  -478,  -478,
     334,   322,   377,  2226,   189,   954,  2226,   229,  -478,   335,
     338,   348,   146,   342,   233,  1007,  -478,  1060,  -478,  -478,
    -478,  -478,  2226,   350,  -478,   345,   353,  1113,  -478,  -478,
     346,  -478,   343,  2226,  2226,  -478,   405,  -478,  2226,  2226,
     355,  -478,  -478,  -478,  -478,   354,  -478,   143,   165,  -478,
    2226,  2226,  -478,   795,   398,   160,  -478,  2226,  2226,   359,
    -478,  2226,   360,  2226,  2226,   394,   421,  2226,   361,   426,
     366,   442,   369,   371,  -478,   409,   410,   382,  -478,  -478,
     378,   408,   379,  -478,   390,   391,  2226,   392,    48,  -478,
    -478,  -478,   742,  -478,   604,  -478,  -478,   393,   396,  2087,
     460,  -478,  2136,  -478,   399,   400,  -478,  1166,   -14,  -478,
     395,   397,   401,   402,   459,  -478,   403,  -478,  -478,  -478,
    -478,  1219,   406,   407,  -478,  1272,  -478,   411,  -478,  -478,
    -478,  -478,   416,   261,   469,   473,  -478,  -478,    78,   417,
      41,  -478,  -478,  -478,  -478,   418,   420,  -478,   422,  -478,
    -478,  1325,   451,  2226,  -478,  1378,  -478,  -478,  -478,  -478,
     423,  1431,  -478,  1484,  1537,  1590,   444,  -478,  -478,   445,
    1643,  -478,  1696,  2226,   424,   433,   118,   427,   428,   501,
     405,  2226,  2226,  1749,  -478,  -478,  1802,  -478,   484,   430,
     431,  1855,   485,  1431,  -478,  -478,   434,   436,   437,  -478,
    -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,   438,
    -478,   439,  -478,   441,   443,   449,   454,   455,   456,   464,
     465,  -478,   491,   499,   481,   467,   470,   500,   516,  -478,
    2393,   337,   545,  -478,  -478,  -478,   472,   497,  -478,  -478,
     562,   565,   502,  -478,  -478,   549,   503,  1431,  -478,  -478,
    -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,  -478,
    -478,   504,   505,   506,  -478,  -478,   507,   508,   509,   153,
     517,  -478,  1908,  -478,   513,   515,   519,  -478,  1961,   520,
    -478,  -478,  -478,  -478,  -478,  -478,  -478,   523,   524,   530,
    2226,  -478,  -478,   551,  -478,    85,  -478,  -478,   525,  -478,
     526,   575,    89,  2014,  -478,  -478,   531,   576,   588,  -478,
     533,   534,  -478,  -478
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       3,     0,     2,     1,    37,     0,     0,    38,     0,     0,
       0,     0,    40,    41,     0,     0,   148,   149,     0,   143,
       0,     0,     0,     0,     0,     0,    39,     0,     0,     0,
       0,     0,     0,     0,     4,     5,     0,     0,    34,     0,
       0,     9,    10,    12,    11,    13,    14,    15,    16,    17,
       0,    19,     0,    20,    22,     0,     0,     0,     0,     0,
       0,     0,     0,    31,     0,   229,   229,   204,    37,   207,
       0,   211,   212,   213,   214,     0,     0,     0,     0,   210,
       0,     0,   296,   296,   221,     0,   169,   170,   172,   174,
     177,   180,   183,   189,   218,   206,     0,    50,     0,     0,
       0,     3,     0,     0,     0,     0,   144,   146,   147,    37,
       0,   131,     0,   117,   116,     0,     0,     0,     0,   142,
      46,    48,    47,    49,   110,    44,     0,     0,     0,   105,
     107,   104,   106,     0,     6,     0,     0,    42,     0,     0,
     145,     7,     8,    18,    21,    23,    24,    25,    26,    27,
      28,    29,    30,     0,   231,     0,   230,     0,   227,   229,
     184,   186,     0,     0,   185,     0,     0,     0,   229,     0,
     208,     0,     0,     0,   194,   195,   196,   197,   198,   199,
     200,   201,   202,   203,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     3,
       0,   235,   235,     0,     0,     0,     3,     0,     3,   141,
     140,     0,   139,     0,   136,     0,   235,    45,     0,   235,
       3,     0,     0,     0,    32,     0,     0,   241,     0,   242,
     257,   254,   255,   246,   262,   269,   270,   271,   287,   267,
     268,   266,   252,   250,   276,   256,   247,   285,   259,   260,
     261,   248,   251,   258,   284,   272,   273,   279,   263,   274,
     275,   282,   286,   253,   283,   249,   243,   244,   245,   280,
     281,   278,   264,   265,   277,    36,     0,   241,   240,     0,
       0,   239,     0,    52,     0,    53,     0,     0,   229,   205,
     215,   216,   297,   233,   296,   219,   296,     0,   241,     0,
     225,    37,     3,   158,    34,   159,   160,   161,   162,   163,
     164,   165,   166,   167,   168,     0,   171,   173,   178,   179,
       0,   175,   181,   182,     0,   241,     0,   191,   228,    51,
       0,     0,     0,     0,    40,     0,     0,    68,   237,     0,
     236,     0,     0,     0,   118,     0,   132,     0,   138,   137,
     134,   135,   229,     0,   112,     0,     0,     0,   109,   108,
       0,    35,     0,   229,   229,    33,     0,   121,     0,     0,
       0,   296,   232,   209,   187,     0,   296,     0,     0,   222,
     229,   229,   223,     0,   155,   176,   190,   229,   229,     0,
       3,     0,     0,     0,     0,    38,     0,     0,     0,     0,
       0,     0,     0,     0,     3,    38,    38,     0,   111,     3,
       0,    38,     0,    43,     0,     0,   294,     0,     0,   288,
     289,   121,     0,   188,     0,   217,   220,     0,     0,     0,
      38,   150,     0,   151,     0,     0,     3,     0,     0,     3,
       0,     0,     0,     0,     0,    70,     0,     3,   238,     3,
       3,     0,     0,     0,    56,     0,     3,     0,     3,    54,
      55,   295,     0,     0,     0,     0,   122,   123,     0,   241,
       0,   234,   224,   226,     3,     0,     0,     3,     0,   192,
     193,     0,    38,     0,     3,     0,    62,    63,    64,    70,
       0,    69,    65,     0,     0,     0,    38,   114,   133,    38,
       0,   103,     0,     0,     0,   129,     0,     0,     0,     0,
       0,     0,     0,     0,   153,   152,     0,   156,    38,     0,
       0,     0,    38,    66,    70,    71,     0,     0,     0,    76,
      77,    79,    78,    80,    72,    81,    82,    83,    84,     0,
      86,     0,    88,     0,     0,     0,     0,     0,     0,     0,
       0,    97,    38,    38,    38,     0,     0,    38,    38,   290,
       0,   235,     0,   124,   120,     3,     0,     0,   291,   292,
      38,    38,     0,    58,     3,    38,     0,    67,    73,    74,
      75,    85,    87,    89,    90,    91,    92,    93,    94,    95,
      96,     0,     0,     0,   113,   100,     0,     0,     0,     0,
       0,   130,     0,   119,     0,     0,     0,    57,     0,     0,
      59,    98,    99,   115,   102,   101,   121,     0,     0,    38,
       0,   154,   157,    38,    60,     0,   121,     3,     0,   293,
       0,     0,     0,     0,   128,    61,     0,     0,    38,   127,
       0,     0,   126,   125
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -478,  -478,   -90,  -478,  -169,  -478,    -2,   527,  -478,  -478,
     490,  -165,  -161,  -477,  -470,  -466,  -462,  -454,  -478,  -478,
    -445,  -478,  -453,  -451,  -449,  -443,  -159,   482,   257,  -442,
    -440,  -109,  -478,  -417,  -478,  -478,   414,  -437,  -154,  -149,
    -144,  -436,  -140,  -130,  -126,  -121,  -435,  -478,  -478,  -171,
      20,  -478,   448,   452,  -182,    92,   -67,   550,   446,  -478,
     341,  -478,  -478,  -478,    79,  -478,  -478,  -186,  -478,   211,
    -106,  -164,   124,   -59
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    35,    36,    37,    84,   139,   226,   124,
     125,    39,    40,    41,    42,    43,    44,    45,   337,   399,
     491,   534,    46,    47,    48,    49,    50,   126,   355,    51,
      52,   112,    53,   418,   467,   506,   113,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,   431,   433,   315,
     154,    86,    87,    88,    89,    90,    91,    92,   187,    93,
      94,   170,   382,    95,   155,   156,   294,   339,   340,   280,
     281,   282,   417,   166
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      38,   204,   303,   296,   468,   321,   305,     3,   160,   287,
     306,   198,   307,   164,   529,   483,   341,   308,   107,   108,
     111,   530,   309,    66,   167,   531,    85,   310,    97,   532,
     353,   311,   275,   356,   102,   103,    98,   533,   535,   106,
     536,   312,   537,   114,   523,   313,   529,   119,   538,   540,
     314,   541,   463,   530,   542,   546,   551,   531,   190,   116,
     348,   532,   464,   300,    64,   484,   120,   191,   350,   533,
     535,   101,   536,   195,   537,   368,   121,   129,   130,   577,
     538,   540,   463,   541,   115,   327,   542,   546,   551,   463,
     122,   127,   509,   463,   342,   465,   369,   196,   511,   631,
     529,   165,   209,   637,   349,   117,   168,   530,   111,   335,
     123,   531,   351,   128,   210,   532,   345,   169,   347,   512,
      99,   322,   323,   533,   535,   465,   536,   466,   537,   134,
     357,    65,   465,   216,   538,   540,   465,   541,   385,   211,
     542,   546,   551,    68,   213,   157,   343,   212,   133,   120,
     100,   205,   214,     7,   140,   224,   225,   466,   205,   121,
     206,    67,    68,    69,   466,    70,   131,   132,   466,   304,
      12,    13,     7,   122,   141,    71,    72,    73,    74,   190,
     370,    75,   135,    76,    77,   371,   293,    26,   191,    12,
      13,   104,   109,   123,   105,   562,    38,   563,   136,   625,
     137,   111,     7,   111,    78,   142,    26,   138,    79,   632,
     324,   143,   383,   329,   330,   331,   332,   425,   403,    12,
      13,   393,   292,   205,   394,   617,    80,   184,   185,    81,
     371,    82,   153,    83,   144,   377,    26,   378,   286,   397,
     118,   426,   398,   145,   292,   188,   189,   297,   174,   175,
     176,   177,   178,   179,   180,   181,   182,   183,   475,   110,
     303,   478,   146,   303,   305,   504,   505,   305,   306,   147,
     307,   306,   148,   307,   149,   308,   318,   319,   308,   150,
     309,   151,   152,   309,   158,   310,   159,   162,   310,   311,
     163,   172,   311,   171,   173,   192,   365,   193,   194,   312,
     437,   197,   312,   313,   372,   199,   313,   200,   314,   201,
     202,   314,   422,   203,   451,   207,   208,   424,   215,   455,
     218,   219,   526,   220,   221,   222,   527,   276,   283,   284,
     528,   285,   539,    38,   288,   328,   289,   543,   290,   333,
     336,   338,   544,    38,   346,    38,   481,   545,   352,   485,
     354,   547,   358,   392,   526,    38,   396,   493,   527,   494,
     495,   548,   528,   359,   539,   549,   500,   375,   502,   543,
     550,   360,   361,   362,   544,   600,   363,    83,   364,   545,
     366,    38,   367,   547,   513,   373,   386,   516,   419,   420,
     384,   379,   376,   548,   521,   371,   599,   549,   380,   381,
      65,   390,   550,   387,   388,   391,   389,   400,   526,   416,
     432,   438,   527,   440,   441,   401,   528,   444,   539,   413,
     402,   404,   408,   543,   409,   410,   423,   304,   544,   442,
     304,   407,   443,   545,   421,    38,   461,   547,   436,   439,
     445,   446,   414,   415,   471,   447,   448,   548,   449,    38,
     450,   549,   452,    38,   454,   453,   550,   456,   458,   427,
     428,   457,   459,   460,   462,   472,   434,   435,   473,   476,
     490,   479,   480,   507,   486,   602,   487,   508,   519,    38,
     488,   489,   492,    38,   608,   497,   498,   555,   510,    38,
     501,    38,    38,    38,   503,   560,   556,   514,    38,   515,
      38,   517,   524,   520,   561,   566,   564,   565,   572,   573,
     574,    38,   576,   578,    38,   579,   580,   581,   582,    38,
     583,    38,   584,   559,   593,    67,    68,    69,   585,    70,
     591,   568,   569,   586,   587,   588,     7,   633,   592,    71,
      72,    73,    74,   589,   590,    75,   594,    76,    77,   595,
     601,   603,   596,    12,    13,   174,   175,   176,   177,   178,
     179,   180,   181,   182,   183,   184,   185,   597,    78,   604,
      26,   605,    79,   137,   606,    38,   609,   628,   630,   636,
     640,   607,   610,   611,   612,   613,   614,   615,   616,   618,
      80,   620,   641,    81,   621,    82,   291,    83,   622,   624,
      38,   292,   626,   627,   634,   635,    38,    67,    68,    69,
     639,    70,   642,   643,   217,   223,   186,   412,     7,   344,
     316,    71,    72,    73,    74,   317,   161,    75,   374,    76,
      77,    38,   320,   470,   567,    12,    13,     0,     0,     0,
     629,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      78,     0,    26,     0,    79,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    80,     0,     0,    81,     0,    82,     0,    83,
     277,   278,     0,   292,   229,   230,     0,   231,   232,     0,
     233,     0,   234,   235,   236,   237,   238,   239,   240,   241,
     242,   243,   244,   245,   246,   247,   248,   249,   250,   251,
     252,   253,   254,   255,   256,   257,   258,   259,   260,   261,
     262,   263,   264,   265,   266,   267,   268,   269,   270,   271,
     272,   273,   274,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   469,   278,     0,     0,
     229,   230,   295,   231,   232,   292,   233,     0,   234,   235,
     236,   237,   238,   239,   240,   241,   242,   243,   244,   245,
     246,   247,   248,   249,   250,   251,   252,   253,   254,   255,
     256,   257,   258,   259,   260,   261,   262,   263,   264,   265,
     266,   267,   268,   269,   270,   271,   272,   273,   274,     4,
       0,     0,     5,     0,     6,     0,     0,   429,     0,   430,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,   292,    10,     0,     0,    11,    12,    13,     0,     0,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,    25,    26,     0,    27,    28,    29,    30,    31,
      32,    33,     4,     0,     0,     5,     0,     6,     0,     0,
       0,     0,     7,     0,     8,     0,     0,     0,     0,     0,
       0,     0,     9,     0,    34,    10,     0,     0,    11,    12,
      13,     0,     0,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,     0,    25,    26,     0,    27,    28,
      29,    30,    31,    32,    33,     4,     0,     0,     5,     0,
       6,     0,     0,     0,     0,     7,     0,     8,     0,     0,
       0,     0,     0,     0,     0,     9,     0,    34,    10,     0,
       0,    11,   334,    13,     0,     0,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,     0,    25,    26,
       0,    27,    28,    29,    30,    31,    32,    33,     4,     0,
       0,     5,     0,     6,     0,     0,     0,     0,   395,     0,
       8,     0,     0,     0,     0,     0,     0,     0,     9,     0,
      34,    10,     0,     0,    11,    12,    13,     0,     0,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
       0,    25,    26,     0,    27,    28,    29,    30,    31,    32,
      33,     4,     0,     0,     5,     0,     6,     0,     0,     0,
       0,   405,     0,     8,     0,     0,     0,     0,     0,     0,
       0,     9,     0,    34,    10,     0,     0,    11,    12,    13,
       0,     0,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,     0,    25,    26,     0,    27,    28,    29,
      30,    31,    32,    33,     4,     0,     0,     5,     0,     6,
       0,     0,     0,     0,   406,     0,     8,     0,     0,     0,
       0,     0,     0,     0,     9,     0,    34,    10,     0,     0,
      11,    12,    13,     0,     0,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,     0,    25,    26,     0,
      27,    28,    29,    30,    31,    32,    33,     4,     0,     0,
       5,     0,     6,     0,     0,     0,     0,   411,     0,     8,
       0,     0,     0,     0,     0,     0,     0,     9,     0,    34,
      10,     0,     0,    11,    12,    13,     0,     0,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,     0,
      25,    26,     0,    27,    28,    29,    30,    31,    32,    33,
       4,     0,     0,     5,     0,     6,     0,     0,     0,     0,
     482,     0,     8,     0,     0,     0,     0,     0,     0,     0,
       9,     0,    34,    10,     0,     0,    11,    12,    13,     0,
       0,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,     0,    25,    26,     0,    27,    28,    29,    30,
      31,    32,    33,     4,     0,     0,     5,     0,     6,     0,
       0,     0,     0,   496,     0,     8,     0,     0,     0,     0,
       0,     0,     0,     9,     0,    34,    10,     0,     0,    11,
      12,    13,     0,     0,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,     0,    25,    26,     0,    27,
      28,    29,    30,    31,    32,    33,     4,     0,     0,     5,
       0,     6,     0,     0,     0,     0,   499,     0,     8,     0,
       0,     0,     0,     0,     0,     0,     9,     0,    34,    10,
       0,     0,    11,    12,    13,     0,     0,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,     0,    25,
      26,     0,    27,    28,    29,    30,    31,    32,    33,     4,
       0,     0,     5,     0,     6,     0,     0,     0,     0,   518,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    34,    10,     0,     0,    11,    12,    13,     0,     0,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,    25,    26,     0,    27,    28,    29,    30,    31,
      32,    33,     4,     0,     0,     5,     0,     6,     0,     0,
       0,     0,   522,     0,     8,     0,     0,     0,     0,     0,
       0,     0,     9,     0,    34,    10,     0,     0,    11,    12,
      13,     0,     0,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,     0,    25,    26,     0,    27,    28,
      29,    30,    31,    32,    33,   301,     0,     0,     5,     0,
       6,     0,     0,     0,     0,     7,     0,     8,     0,     0,
       0,     0,     0,     0,     0,     9,     0,    34,    10,     0,
       0,    11,    12,    13,     0,     0,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,     0,    25,    26,
       0,    27,    28,    29,    30,    31,    32,    33,     4,     0,
       0,     5,     0,     6,     0,     0,     0,     0,   552,     0,
       8,     0,     0,     0,     0,     0,     0,     0,     9,     0,
     525,    10,     0,     0,    11,    12,    13,     0,     0,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
       0,    25,    26,     0,    27,    28,    29,    30,    31,    32,
      33,     4,     0,     0,     5,     0,     6,     0,     0,     0,
       0,   553,     0,     8,     0,     0,     0,     0,     0,     0,
       0,     9,     0,    34,    10,     0,     0,    11,    12,    13,
       0,     0,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,     0,    25,    26,     0,    27,    28,    29,
      30,    31,    32,    33,     4,     0,     0,     5,     0,     6,
       0,     0,     0,     0,   554,     0,     8,     0,     0,     0,
       0,     0,     0,     0,     9,     0,    34,    10,     0,     0,
      11,    12,    13,     0,     0,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,     0,    25,    26,     0,
      27,    28,    29,    30,    31,    32,    33,     4,     0,     0,
       5,     0,     6,     0,     0,     0,     0,   557,     0,     8,
       0,     0,     0,     0,     0,     0,     0,     9,     0,    34,
      10,     0,     0,    11,    12,    13,     0,     0,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,     0,
      25,    26,     0,    27,    28,    29,    30,    31,    32,    33,
       4,     0,     0,     5,     0,     6,     0,     0,     0,     0,
     558,     0,     8,     0,     0,     0,     0,     0,     0,     0,
       9,     0,    34,    10,     0,     0,    11,    12,    13,     0,
       0,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,     0,    25,    26,     0,    27,    28,    29,    30,
      31,    32,    33,     4,     0,     0,     5,     0,     6,     0,
       0,     0,     0,   570,     0,     8,     0,     0,     0,     0,
       0,     0,     0,     9,     0,    34,    10,     0,     0,    11,
      12,    13,     0,     0,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,     0,    25,    26,     0,    27,
      28,    29,    30,    31,    32,    33,     4,     0,     0,     5,
       0,     6,     0,     0,     0,     0,   571,     0,     8,     0,
       0,     0,     0,     0,     0,     0,     9,     0,    34,    10,
       0,     0,    11,    12,    13,     0,     0,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,     0,    25,
      26,     0,    27,    28,    29,    30,    31,    32,    33,     4,
       0,     0,     5,     0,     6,     0,     0,     0,     0,   575,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    34,    10,     0,     0,    11,    12,    13,     0,     0,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,    25,    26,     0,    27,    28,    29,    30,    31,
      32,    33,     4,     0,     0,     5,     0,     6,     0,     0,
       0,     0,   619,     0,     8,     0,     0,     0,     0,     0,
       0,     0,     9,     0,    34,    10,     0,     0,    11,    12,
      13,     0,     0,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,     0,    25,    26,     0,    27,    28,
      29,    30,    31,    32,    33,     4,     0,     0,     5,     0,
       6,     0,     0,     0,     0,   623,     0,     8,     0,     0,
       0,     0,     0,     0,     0,     9,     0,    34,    10,     0,
       0,    11,    12,    13,     0,     0,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,     0,    25,    26,
       0,    27,    28,    29,    30,    31,    32,    33,     4,     0,
       0,     5,     0,     6,     0,     0,     0,     0,   638,     0,
       8,     0,     0,     0,     0,     0,     0,     0,     9,     0,
      34,    10,     0,     0,    11,    12,    13,     0,     0,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
       0,    25,    26,     0,    27,    28,    29,    30,    31,    32,
      33,   301,     0,     0,     5,     0,     0,     0,     0,     0,
       0,     7,     0,     8,     0,     0,     0,     0,     0,     0,
       0,   301,     0,    34,     5,     0,     0,     0,    12,    13,
       0,     7,     0,     8,    16,    17,     0,    19,    20,    21,
       0,     0,     0,     0,    25,    26,     0,    27,    12,    13,
       0,    31,    32,     0,    16,    17,     0,    19,    20,    21,
       0,     0,     0,     0,    25,    26,     0,    27,     0,     0,
     301,    31,    32,     5,     0,     0,   302,     0,     0,     0,
       7,     0,     8,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   474,    12,    13,     0,
       0,     0,     0,    16,    17,     0,    19,    20,    21,     0,
       0,     0,     0,    25,    26,     0,    27,     0,     0,     0,
      31,    32,     0,     0,    67,    68,    69,     0,    70,     0,
       0,     0,     0,     0,     0,     7,     0,     0,    71,    72,
      73,    74,     0,     0,    75,   477,    76,    77,     0,    96,
       0,     0,    12,    13,     0,     0,     0,     0,     0,    67,
      68,    69,     0,    70,     0,     0,     0,    78,     0,    26,
       7,    79,     0,    71,    72,    73,    74,     0,     0,    75,
       0,    76,    77,     0,     0,     0,     0,    12,    13,    80,
       0,     0,    81,     0,    82,     0,    83,    67,    68,    69,
       0,    70,    78,     0,    26,     0,    79,     0,     7,     0,
       0,    71,    72,    73,    74,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    80,    12,    13,    81,     0,    82,
       0,    83,     0,     0,     0,     0,     0,     0,     0,     0,
      78,     0,    26,     0,    79,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    81,     0,    82,     0,    83,
     277,   278,     0,     0,   229,   230,     0,   231,   232,     0,
     233,     0,   234,   235,   236,   237,   238,   239,   240,   241,
     242,   243,   244,   245,   246,   247,   248,   249,   250,   251,
     252,   253,   254,   255,   256,   257,   258,   259,   260,   261,
     262,   263,   264,   265,   266,   267,   268,   269,   270,   271,
     272,   273,   274,     0,     0,     0,     0,   277,   278,     0,
       0,   229,   230,     0,   231,   232,     0,   233,   279,   234,
     235,   236,   237,   238,   239,   240,   241,   242,   243,   244,
     245,   246,   247,   248,   249,   250,   251,   252,   253,   254,
     255,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
       0,     0,     0,     0,   227,     0,     0,   228,   229,   230,
       0,   231,   232,     0,   233,   598,   234,   235,   236,   237,
     238,   239,   240,   241,   242,   243,   244,   245,   246,   247,
     248,   249,   250,   251,   252,   253,   254,   255,   256,   257,
     258,   259,   260,   261,   262,   263,   264,   265,   266,   267,
     268,   269,   270,   271,   272,   273,   274,   298,     0,     0,
     299,   229,   230,     0,   231,   232,     0,   233,     0,   234,
     235,   236,   237,   238,   239,   240,   241,   242,   243,   244,
     245,   246,   247,   248,   249,   250,   251,   252,   253,   254,
     255,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     325,     0,     0,   326,   229,   230,     0,   231,   232,     0,
     233,     0,   234,   235,   236,   237,   238,   239,   240,   241,
     242,   243,   244,   245,   246,   247,   248,   249,   250,   251,
     252,   253,   254,   255,   256,   257,   258,   259,   260,   261,
     262,   263,   264,   265,   266,   267,   268,   269,   270,   271,
     272,   273,   274
};

static const yytype_int16 yycheck[] =
{
       2,   110,   171,   167,   421,   187,   171,     0,    75,    24,
     171,   101,   171,    80,   491,    29,   202,   171,    20,    21,
      22,   491,   171,    71,    83,   491,     6,   171,     8,   491,
     216,   171,   138,   219,    14,    15,     4,   491,   491,    19,
     491,   171,   491,    23,   489,   171,   523,    27,   491,   491,
     171,   491,     4,   523,   491,   491,   491,   523,    73,     4,
       4,   523,    14,   169,     4,    79,     4,    82,     4,   523,
     523,    79,   523,    33,   523,    57,    14,     4,     5,   524,
     523,   523,     4,   523,    46,   191,   523,   523,   523,     4,
      28,     4,    14,     4,   203,    47,    78,    57,    57,    14,
     577,    81,     4,    14,    48,    50,    71,   577,   110,   199,
      48,   577,    48,     4,    16,   577,   206,    82,   208,    78,
       4,   188,   189,   577,   577,    47,   577,    79,   577,    79,
     220,    71,    47,    71,   577,   577,    47,   577,   320,    41,
     577,   577,   577,     4,    41,    66,    72,    49,    51,     4,
      34,    77,    49,    14,    78,   135,   136,    79,    77,    14,
      79,     3,     4,     5,    79,     7,     4,     5,    79,   171,
      31,    32,    14,    28,    79,    17,    18,    19,    20,    73,
      72,    23,    57,    25,    26,    77,   166,    48,    82,    31,
      32,     4,     4,    48,     7,    77,   198,    79,    73,   616,
      75,   203,    14,   205,    46,    79,    48,    82,    50,   626,
     190,    79,   302,   193,   194,   195,   196,    74,    72,    31,
      32,    32,    79,    77,    35,    72,    68,    67,    68,    71,
      77,    73,    71,    75,    79,   294,    48,   296,   159,    10,
      82,    76,    13,    79,    79,    69,    70,   168,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,   429,    71,
     429,   432,    79,   432,   429,     4,     5,   432,   429,    79,
     429,   432,    79,   432,    79,   429,   184,   185,   432,    79,
     429,    79,    79,   432,     4,   429,    71,     4,   432,   429,
      71,    22,   432,    11,    21,     3,   276,    50,    71,   429,
     390,     4,   432,   429,   284,    79,   432,    79,   429,    71,
      71,   432,   371,    71,   404,    82,    79,   376,     4,   409,
      27,    71,   491,    79,     4,     4,   491,    57,    72,    77,
     491,    72,   491,   335,    71,     4,    72,   491,    72,    33,
      10,     4,   491,   345,     4,   347,   436,   491,    71,   439,
       4,   491,     5,   333,   523,   357,   336,   447,   523,   449,
     450,   491,   523,     5,   523,   491,   456,   288,   458,   523,
     491,    27,    74,     6,   523,   561,    71,    75,    71,   523,
      71,   383,    79,   523,   474,    72,    74,   477,   368,   369,
      79,    72,    77,   523,   484,    77,   560,   523,    71,    71,
      71,    79,   523,    71,    71,    28,    72,    72,   577,     4,
      12,   391,   577,   393,   394,    77,   577,   397,   577,    76,
      72,    79,    72,   577,    79,    72,    72,   429,   577,    35,
     432,   352,    11,   577,    79,   437,   416,   577,    79,    79,
      79,    15,   363,   364,   424,    79,     4,   577,    79,   451,
      79,   577,    43,   455,    72,    45,   577,    79,    79,   380,
     381,    53,    72,    72,    72,    72,   387,   388,    72,     9,
      11,    72,    72,     4,    79,   565,    79,     4,    27,   481,
      79,    79,    79,   485,   574,    79,    79,    43,    71,   491,
      79,   493,   494,   495,    78,    71,    51,    79,   500,    79,
     502,    79,    79,   483,    71,     4,    79,    79,    24,    79,
      79,   513,    27,    79,   516,    79,    79,    79,    79,   521,
      79,   523,    79,   503,    43,     3,     4,     5,    79,     7,
      39,   511,   512,    79,    79,    79,    14,   627,    39,    17,
      18,    19,    20,    79,    79,    23,    79,    25,    26,    79,
       5,    79,    52,    31,    32,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    51,    46,    72,
      48,     9,    50,    75,     9,   577,    27,    47,    27,     4,
       4,    79,    79,    79,    79,    79,    79,    79,    79,    72,
      68,    78,     4,    71,    79,    73,    74,    75,    79,    79,
     602,    79,    79,    79,    79,    79,   608,     3,     4,     5,
      79,     7,    79,    79,   124,   133,    89,   360,    14,   205,
     172,    17,    18,    19,    20,   173,    76,    23,   287,    25,
      26,   633,   186,   422,   510,    31,    32,    -1,    -1,    -1,
     620,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      46,    -1,    48,    -1,    50,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    68,    -1,    -1,    71,    -1,    73,    -1,    75,
       4,     5,    -1,    79,     8,     9,    -1,    11,    12,    -1,
      14,    -1,    16,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,     4,     5,    -1,    -1,
       8,     9,    76,    11,    12,    79,    14,    -1,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,     4,
      -1,    -1,     7,    -1,     9,    -1,    -1,    12,    -1,    14,
      -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,
      -1,    79,    27,    -1,    -1,    30,    31,    32,    -1,    -1,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    -1,    47,    48,    -1,    50,    51,    52,    53,    54,
      55,    56,     4,    -1,    -1,     7,    -1,     9,    -1,    -1,
      -1,    -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    24,    -1,    79,    27,    -1,    -1,    30,    31,
      32,    -1,    -1,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    -1,    47,    48,    -1,    50,    51,
      52,    53,    54,    55,    56,     4,    -1,    -1,     7,    -1,
       9,    -1,    -1,    -1,    -1,    14,    -1,    16,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    24,    -1,    79,    27,    -1,
      -1,    30,    31,    32,    -1,    -1,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    -1,    47,    48,
      -1,    50,    51,    52,    53,    54,    55,    56,     4,    -1,
      -1,     7,    -1,     9,    -1,    -1,    -1,    -1,    14,    -1,
      16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,    -1,
      79,    27,    -1,    -1,    30,    31,    32,    -1,    -1,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      -1,    47,    48,    -1,    50,    51,    52,    53,    54,    55,
      56,     4,    -1,    -1,     7,    -1,     9,    -1,    -1,    -1,
      -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    24,    -1,    79,    27,    -1,    -1,    30,    31,    32,
      -1,    -1,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    -1,    47,    48,    -1,    50,    51,    52,
      53,    54,    55,    56,     4,    -1,    -1,     7,    -1,     9,
      -1,    -1,    -1,    -1,    14,    -1,    16,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    24,    -1,    79,    27,    -1,    -1,
      30,    31,    32,    -1,    -1,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    -1,    47,    48,    -1,
      50,    51,    52,    53,    54,    55,    56,     4,    -1,    -1,
       7,    -1,     9,    -1,    -1,    -1,    -1,    14,    -1,    16,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,    -1,    79,
      27,    -1,    -1,    30,    31,    32,    -1,    -1,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    -1,
      47,    48,    -1,    50,    51,    52,    53,    54,    55,    56,
       4,    -1,    -1,     7,    -1,     9,    -1,    -1,    -1,    -1,
      14,    -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      24,    -1,    79,    27,    -1,    -1,    30,    31,    32,    -1,
      -1,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    -1,    47,    48,    -1,    50,    51,    52,    53,
      54,    55,    56,     4,    -1,    -1,     7,    -1,     9,    -1,
      -1,    -1,    -1,    14,    -1,    16,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    24,    -1,    79,    27,    -1,    -1,    30,
      31,    32,    -1,    -1,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    -1,    47,    48,    -1,    50,
      51,    52,    53,    54,    55,    56,     4,    -1,    -1,     7,
      -1,     9,    -1,    -1,    -1,    -1,    14,    -1,    16,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    24,    -1,    79,    27,
      -1,    -1,    30,    31,    32,    -1,    -1,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    -1,    47,
      48,    -1,    50,    51,    52,    53,    54,    55,    56,     4,
      -1,    -1,     7,    -1,     9,    -1,    -1,    -1,    -1,    14,
      -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,
      -1,    79,    27,    -1,    -1,    30,    31,    32,    -1,    -1,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    -1,    47,    48,    -1,    50,    51,    52,    53,    54,
      55,    56,     4,    -1,    -1,     7,    -1,     9,    -1,    -1,
      -1,    -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    24,    -1,    79,    27,    -1,    -1,    30,    31,
      32,    -1,    -1,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    -1,    47,    48,    -1,    50,    51,
      52,    53,    54,    55,    56,     4,    -1,    -1,     7,    -1,
       9,    -1,    -1,    -1,    -1,    14,    -1,    16,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    24,    -1,    79,    27,    -1,
      -1,    30,    31,    32,    -1,    -1,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    -1,    47,    48,
      -1,    50,    51,    52,    53,    54,    55,    56,     4,    -1,
      -1,     7,    -1,     9,    -1,    -1,    -1,    -1,    14,    -1,
      16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,    -1,
      79,    27,    -1,    -1,    30,    31,    32,    -1,    -1,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      -1,    47,    48,    -1,    50,    51,    52,    53,    54,    55,
      56,     4,    -1,    -1,     7,    -1,     9,    -1,    -1,    -1,
      -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    24,    -1,    79,    27,    -1,    -1,    30,    31,    32,
      -1,    -1,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    -1,    47,    48,    -1,    50,    51,    52,
      53,    54,    55,    56,     4,    -1,    -1,     7,    -1,     9,
      -1,    -1,    -1,    -1,    14,    -1,    16,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    24,    -1,    79,    27,    -1,    -1,
      30,    31,    32,    -1,    -1,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    -1,    47,    48,    -1,
      50,    51,    52,    53,    54,    55,    56,     4,    -1,    -1,
       7,    -1,     9,    -1,    -1,    -1,    -1,    14,    -1,    16,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,    -1,    79,
      27,    -1,    -1,    30,    31,    32,    -1,    -1,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    -1,
      47,    48,    -1,    50,    51,    52,    53,    54,    55,    56,
       4,    -1,    -1,     7,    -1,     9,    -1,    -1,    -1,    -1,
      14,    -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      24,    -1,    79,    27,    -1,    -1,    30,    31,    32,    -1,
      -1,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    -1,    47,    48,    -1,    50,    51,    52,    53,
      54,    55,    56,     4,    -1,    -1,     7,    -1,     9,    -1,
      -1,    -1,    -1,    14,    -1,    16,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    24,    -1,    79,    27,    -1,    -1,    30,
      31,    32,    -1,    -1,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    -1,    47,    48,    -1,    50,
      51,    52,    53,    54,    55,    56,     4,    -1,    -1,     7,
      -1,     9,    -1,    -1,    -1,    -1,    14,    -1,    16,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    24,    -1,    79,    27,
      -1,    -1,    30,    31,    32,    -1,    -1,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    -1,    47,
      48,    -1,    50,    51,    52,    53,    54,    55,    56,     4,
      -1,    -1,     7,    -1,     9,    -1,    -1,    -1,    -1,    14,
      -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,
      -1,    79,    27,    -1,    -1,    30,    31,    32,    -1,    -1,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    -1,    47,    48,    -1,    50,    51,    52,    53,    54,
      55,    56,     4,    -1,    -1,     7,    -1,     9,    -1,    -1,
      -1,    -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    24,    -1,    79,    27,    -1,    -1,    30,    31,
      32,    -1,    -1,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    -1,    47,    48,    -1,    50,    51,
      52,    53,    54,    55,    56,     4,    -1,    -1,     7,    -1,
       9,    -1,    -1,    -1,    -1,    14,    -1,    16,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    24,    -1,    79,    27,    -1,
      -1,    30,    31,    32,    -1,    -1,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    -1,    47,    48,
      -1,    50,    51,    52,    53,    54,    55,    56,     4,    -1,
      -1,     7,    -1,     9,    -1,    -1,    -1,    -1,    14,    -1,
      16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,    -1,
      79,    27,    -1,    -1,    30,    31,    32,    -1,    -1,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      -1,    47,    48,    -1,    50,    51,    52,    53,    54,    55,
      56,     4,    -1,    -1,     7,    -1,    -1,    -1,    -1,    -1,
      -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,     4,    -1,    79,     7,    -1,    -1,    -1,    31,    32,
      -1,    14,    -1,    16,    37,    38,    -1,    40,    41,    42,
      -1,    -1,    -1,    -1,    47,    48,    -1,    50,    31,    32,
      -1,    54,    55,    -1,    37,    38,    -1,    40,    41,    42,
      -1,    -1,    -1,    -1,    47,    48,    -1,    50,    -1,    -1,
       4,    54,    55,     7,    -1,    -1,    79,    -1,    -1,    -1,
      14,    -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    79,    31,    32,    -1,
      -1,    -1,    -1,    37,    38,    -1,    40,    41,    42,    -1,
      -1,    -1,    -1,    47,    48,    -1,    50,    -1,    -1,    -1,
      54,    55,    -1,    -1,     3,     4,     5,    -1,     7,    -1,
      -1,    -1,    -1,    -1,    -1,    14,    -1,    -1,    17,    18,
      19,    20,    -1,    -1,    23,    79,    25,    26,    -1,    28,
      -1,    -1,    31,    32,    -1,    -1,    -1,    -1,    -1,     3,
       4,     5,    -1,     7,    -1,    -1,    -1,    46,    -1,    48,
      14,    50,    -1,    17,    18,    19,    20,    -1,    -1,    23,
      -1,    25,    26,    -1,    -1,    -1,    -1,    31,    32,    68,
      -1,    -1,    71,    -1,    73,    -1,    75,     3,     4,     5,
      -1,     7,    46,    -1,    48,    -1,    50,    -1,    14,    -1,
      -1,    17,    18,    19,    20,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    68,    31,    32,    71,    -1,    73,
      -1,    75,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      46,    -1,    48,    -1,    50,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    71,    -1,    73,    -1,    75,
       4,     5,    -1,    -1,     8,     9,    -1,    11,    12,    -1,
      14,    -1,    16,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    -1,    -1,    -1,    -1,     4,     5,    -1,
      -1,     8,     9,    -1,    11,    12,    -1,    14,    72,    16,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      -1,    -1,    -1,    -1,     4,    -1,    -1,     7,     8,     9,
      -1,    11,    12,    -1,    14,    72,    16,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,     4,    -1,    -1,
       7,     8,     9,    -1,    11,    12,    -1,    14,    -1,    16,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
       4,    -1,    -1,     7,     8,     9,    -1,    11,    12,    -1,
      14,    -1,    16,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    84,    85,     0,     4,     7,     9,    14,    16,    24,
      27,    30,    31,    32,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    47,    48,    50,    51,    52,
      53,    54,    55,    56,    79,    86,    87,    88,    89,    94,
      95,    96,    97,    98,    99,   100,   105,   106,   107,   108,
     109,   112,   113,   115,   120,   121,   122,   123,   124,   125,
     126,   127,   128,   129,     4,    71,    71,     3,     4,     5,
       7,    17,    18,    19,    20,    23,    25,    26,    46,    50,
      68,    71,    73,    75,    89,   133,   134,   135,   136,   137,
     138,   139,   140,   142,   143,   146,    28,   133,     4,     4,
      34,    79,   133,   133,     4,     7,   133,    89,    89,     4,
      71,    89,   114,   119,   133,    46,     4,    50,    82,   133,
       4,    14,    28,    48,    92,    93,   110,     4,     4,     4,
       5,     4,     5,    51,    79,    57,    73,    75,    82,    90,
      78,    79,    79,    79,    79,    79,    79,    79,    79,    79,
      79,    79,    79,    71,   133,   147,   148,   147,     4,    71,
     139,   140,     4,    71,   139,   133,   156,   156,    71,    82,
     144,    11,    22,    21,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    90,   141,    69,    70,
      73,    82,     3,    50,    71,    33,    57,     4,    85,    79,
      79,    71,    71,    71,   114,    77,    79,    82,    79,     4,
      16,    41,    49,    41,    49,     4,    71,    93,    27,    71,
      79,     4,     4,   110,   133,   133,    91,     4,     7,     8,
       9,    11,    12,    14,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,   153,    57,     4,     5,    72,
     152,   153,   154,    72,    77,    72,   147,    24,    71,    72,
      72,    74,    79,   133,   149,    76,   154,   147,     4,     7,
     153,     4,    79,    87,    89,    94,    95,   109,   121,   122,
     123,   125,   126,   127,   128,   132,   135,   136,   138,   138,
     141,   137,   139,   139,   133,     4,     7,   153,     4,   133,
     133,   133,   133,    33,    31,    85,    10,   101,     4,   150,
     151,   150,   114,    72,   119,    85,     4,    85,     4,    48,
       4,    48,    71,   150,     4,   111,   150,    85,     5,     5,
      27,    74,     6,    71,    71,   133,    71,    79,    57,    78,
      72,    77,   133,    72,   143,   147,    77,   156,   156,    72,
      71,    71,   145,    85,    79,   137,    74,    71,    71,    72,
      79,    28,   133,    32,    35,    14,   133,    10,    13,   102,
      72,    77,    72,    72,    79,    14,    14,   147,    72,    79,
      72,    14,   111,    76,   147,   147,     4,   155,   116,   133,
     133,    79,   156,    72,   156,    74,    76,   147,   147,    12,
      14,   130,    12,   131,   147,   147,    79,    85,   133,    79,
     133,   133,    35,    11,   133,    79,    15,    79,     4,    79,
      79,    85,    43,    45,    72,    85,    79,    53,    79,    72,
      72,   133,    72,     4,    14,    47,    79,   117,   116,     4,
     152,   133,    72,    72,    79,   132,     9,    79,   132,    72,
      72,    85,    14,    29,    79,    85,    79,    79,    79,    79,
      11,   103,    79,    85,    85,    85,    14,    79,    79,    14,
      85,    79,    85,    78,     4,     5,   118,     4,     4,    14,
      71,    57,    78,    85,    79,    79,    85,    79,    14,    27,
     133,    85,    14,   103,    79,    79,    87,    94,    95,    96,
      97,    98,    99,   100,   104,   105,   106,   107,   108,   109,
     112,   113,   120,   121,   122,   123,   124,   125,   126,   127,
     128,   129,    14,    14,    14,    43,    51,    14,    14,   133,
      71,    71,    77,    79,    79,    79,     4,   155,   133,   133,
      14,    14,    24,    79,    79,    14,    27,   103,    79,    79,
      79,    79,    79,    79,    79,    79,    79,    79,    79,    79,
      79,    39,    39,    43,    79,    79,    52,    51,    72,   154,
     150,     5,    85,    79,    72,     9,     9,    79,    85,    27,
      79,    79,    79,    79,    79,    79,    79,    72,    72,    14,
      78,    79,    79,    14,    79,   116,    79,    79,    47,   133,
      27,    14,   116,    85,    79,    79,     4,    14,    14,    79,
       4,     4,    79,    79
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    83,    84,    85,    85,    85,    86,    86,    86,    86,
      86,    86,    86,    86,    86,    86,    86,    86,    86,    86,
      86,    86,    86,    86,    86,    86,    86,    86,    86,    86,
      86,    86,    87,    87,    88,    88,    88,    89,    89,    89,
      89,    89,    91,    90,    92,    92,    93,    93,    93,    93,
      94,    94,    95,    95,    95,    95,    95,    96,    97,    97,
      97,    97,    98,    98,    99,   100,   101,   101,   102,   102,
     103,   103,   103,   104,   104,   104,   104,   104,   104,   104,
     104,   104,   104,   104,   104,   104,   104,   104,   104,   104,
     104,   104,   104,   104,   104,   104,   104,   104,   105,   105,
     106,   106,   107,   108,   109,   109,   109,   109,   109,   109,
     110,   110,   111,   112,   112,   112,   113,   114,   114,   115,
     115,   116,   116,   116,   117,   117,   117,   117,   117,   118,
     118,   119,   119,   120,   121,   121,   121,   121,   121,   121,
     121,   121,   122,   123,   123,   124,   125,   126,   127,   128,
     129,   129,   130,   130,   130,   131,   131,   131,   132,   132,
     132,   132,   132,   132,   132,   132,   132,   132,   132,   133,
     134,   134,   135,   135,   136,   136,   136,   137,   137,   137,
     138,   138,   138,   139,   139,   139,   139,   139,   139,   140,
     140,   140,   140,   140,   141,   141,   141,   141,   141,   141,
     141,   141,   141,   141,   142,   142,   142,   142,   142,   142,
     142,   142,   142,   142,   142,   142,   142,   142,   142,   143,
     143,   144,   144,   144,   144,   145,   145,   146,   146,   147,
     147,   148,   148,   149,   149,   150,   150,   151,   151,   152,
     152,   153,   153,   153,   153,   153,   153,   153,   153,   153,
     153,   153,   153,   153,   153,   153,   153,   153,   153,   153,
     153,   153,   153,   153,   153,   153,   153,   153,   153,   153,
     153,   153,   153,   153,   153,   153,   153,   153,   153,   153,
     153,   153,   153,   153,   153,   153,   153,   153,   154,   154,
     154,   154,   154,   154,   155,   155,   156,   156
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     2,     1,
       1,     2,     1,     2,     2,     2,     2,     2,     2,     2,
       2,     1,     3,     4,     1,     4,     3,     1,     1,     1,
       1,     1,     0,     4,     1,     2,     1,     1,     1,     1,
       2,     4,     4,     4,     6,     6,     6,    10,     9,    10,
      11,    13,     7,     7,     7,     7,     5,     6,     0,     3,
       0,     2,     2,     2,     2,     2,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     1,     2,     1,     2,
       2,     2,     2,     2,     2,     2,     2,     1,    10,    10,
       9,    10,    10,     7,     2,     2,     2,     2,     4,     4,
       1,     4,     1,     9,     7,    10,     2,     1,     3,    10,
       9,     0,     2,     2,     3,    10,    10,     9,     7,     1,
       3,     1,     3,     7,     4,     4,     3,     4,     4,     3,
       3,     3,     2,     1,     2,     2,     2,     2,     1,     1,
       6,     6,     3,     3,     6,     0,     3,     6,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     3,     1,     3,     1,     3,     4,     1,     3,     3,
       1,     3,     3,     1,     2,     2,     2,     4,     5,     1,
       4,     3,     6,     6,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     3,     1,     1,     2,     4,
       1,     1,     1,     1,     1,     3,     3,     5,     1,     3,
       5,     0,     3,     3,     5,     0,     3,     2,     3,     0,
       1,     1,     3,     1,     4,     0,     1,     1,     3,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     3,     3,
       6,     6,     6,     9,     1,     2,     0,     2
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (&yylloc, ctx, YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF

/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)                                \
    do                                                                  \
      if (N)                                                            \
        {                                                               \
          (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;        \
          (Current).first_column = YYRHSLOC (Rhs, 1).first_column;      \
          (Current).last_line    = YYRHSLOC (Rhs, N).last_line;         \
          (Current).last_column  = YYRHSLOC (Rhs, N).last_column;       \
        }                                                               \
      else                                                              \
        {                                                               \
          (Current).first_line   = (Current).last_line   =              \
            YYRHSLOC (Rhs, 0).last_line;                                \
          (Current).first_column = (Current).last_column =              \
            YYRHSLOC (Rhs, 0).last_column;                              \
        }                                                               \
    while (0)
#endif

#define YYRHSLOC(Rhs, K) ((Rhs)[K])


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)


/* YYLOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

# ifndef YYLOCATION_PRINT

#  if defined YY_LOCATION_PRINT

   /* Temporary convenience wrapper in case some people defined the
      undocumented and private YY_LOCATION_PRINT macros.  */
#   define YYLOCATION_PRINT(File, Loc)  YY_LOCATION_PRINT(File, *(Loc))

#  elif defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL

/* Print *YYLOCP on YYO.  Private, do not rely on its existence. */

YY_ATTRIBUTE_UNUSED
static int
yy_location_print_ (FILE *yyo, YYLTYPE const * const yylocp)
{
  int res = 0;
  int end_col = 0 != yylocp->last_column ? yylocp->last_column - 1 : 0;
  if (0 <= yylocp->first_line)
    {
      res += YYFPRINTF (yyo, "%d", yylocp->first_line);
      if (0 <= yylocp->first_column)
        res += YYFPRINTF (yyo, ".%d", yylocp->first_column);
    }
  if (0 <= yylocp->last_line)
    {
      if (yylocp->first_line < yylocp->last_line)
        {
          res += YYFPRINTF (yyo, "-%d", yylocp->last_line);
          if (0 <= end_col)
            res += YYFPRINTF (yyo, ".%d", end_col);
        }
      else if (0 <= end_col && yylocp->first_column < end_col)
        res += YYFPRINTF (yyo, "-%d", end_col);
    }
  return res;
}

#   define YYLOCATION_PRINT  yy_location_print_

    /* Temporary convenience wrapper in case some people defined the
       undocumented and private YY_LOCATION_PRINT macros.  */
#   define YY_LOCATION_PRINT(File, Loc)  YYLOCATION_PRINT(File, &(Loc))

#  else

#   define YYLOCATION_PRINT(File, Loc) ((void) 0)
    /* Temporary convenience wrapper in case some people defined the
       undocumented and private YY_LOCATION_PRINT macros.  */
#   define YY_LOCATION_PRINT  YYLOCATION_PRINT

#  endif
# endif /* !defined YYLOCATION_PRINT */


# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value, Location, ctx); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp, gb_parse_ctx *ctx)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  YY_USE (yylocationp);
  YY_USE (ctx);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp, gb_parse_ctx *ctx)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  YYLOCATION_PRINT (yyo, yylocationp);
  YYFPRINTF (yyo, ": ");
  yy_symbol_value_print (yyo, yykind, yyvaluep, yylocationp, ctx);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp, YYLTYPE *yylsp,
                 int yyrule, gb_parse_ctx *ctx)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)],
                       &(yylsp[(yyi + 1) - (yynrhs)]), ctx);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, yylsp, Rule, ctx); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif


/* Context of a parse error.  */
typedef struct
{
  yy_state_t *yyssp;
  yysymbol_kind_t yytoken;
  YYLTYPE *yylloc;
} yypcontext_t;

/* Put in YYARG at most YYARGN of the expected tokens given the
   current YYCTX, and return the number of tokens stored in YYARG.  If
   YYARG is null, return the number of expected tokens (guaranteed to
   be less than YYNTOKENS).  Return YYENOMEM on memory exhaustion.
   Return 0 if there are more than YYARGN expected tokens, yet fill
   YYARG up to YYARGN. */
static int
yypcontext_expected_tokens (const yypcontext_t *yyctx,
                            yysymbol_kind_t yyarg[], int yyargn)
{
  /* Actual size of YYARG. */
  int yycount = 0;
  int yyn = yypact[+*yyctx->yyssp];
  if (!yypact_value_is_default (yyn))
    {
      /* Start YYX at -YYN if negative to avoid negative indexes in
         YYCHECK.  In other words, skip the first -YYN actions for
         this state because they are default actions.  */
      int yyxbegin = yyn < 0 ? -yyn : 0;
      /* Stay within bounds of both yycheck and yytname.  */
      int yychecklim = YYLAST - yyn + 1;
      int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
      int yyx;
      for (yyx = yyxbegin; yyx < yyxend; ++yyx)
        if (yycheck[yyx + yyn] == yyx && yyx != YYSYMBOL_YYerror
            && !yytable_value_is_error (yytable[yyx + yyn]))
          {
            if (!yyarg)
              ++yycount;
            else if (yycount == yyargn)
              return 0;
            else
              yyarg[yycount++] = YY_CAST (yysymbol_kind_t, yyx);
          }
    }
  if (yyarg && yycount == 0 && 0 < yyargn)
    yyarg[0] = YYSYMBOL_YYEMPTY;
  return yycount;
}




#ifndef yystrlen
# if defined __GLIBC__ && defined _STRING_H
#  define yystrlen(S) (YY_CAST (YYPTRDIFF_T, strlen (S)))
# else
/* Return the length of YYSTR.  */
static YYPTRDIFF_T
yystrlen (const char *yystr)
{
  YYPTRDIFF_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
# endif
#endif

#ifndef yystpcpy
# if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#  define yystpcpy stpcpy
# else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
yystpcpy (char *yydest, const char *yysrc)
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
# endif
#endif

#ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYPTRDIFF_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYPTRDIFF_T yyn = 0;
      char const *yyp = yystr;
      for (;;)
        switch (*++yyp)
          {
          case '\'':
          case ',':
            goto do_not_strip_quotes;

          case '\\':
            if (*++yyp != '\\')
              goto do_not_strip_quotes;
            else
              goto append;

          append:
          default:
            if (yyres)
              yyres[yyn] = *yyp;
            yyn++;
            break;

          case '"':
            if (yyres)
              yyres[yyn] = '\0';
            return yyn;
          }
    do_not_strip_quotes: ;
    }

  if (yyres)
    return yystpcpy (yyres, yystr) - yyres;
  else
    return yystrlen (yystr);
}
#endif


static int
yy_syntax_error_arguments (const yypcontext_t *yyctx,
                           yysymbol_kind_t yyarg[], int yyargn)
{
  /* Actual size of YYARG. */
  int yycount = 0;
  /* There are many possibilities here to consider:
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
     - Of course, the expected token list depends on states to have
       correct lookahead information, and it depends on the parser not
       to perform extra reductions after fetching a lookahead from the
       scanner and before detecting a syntax error.  Thus, state merging
       (from LALR or IELR) and default reductions corrupt the expected
       token list.  However, the list is correct for canonical LR with
       one exception: it will still contain any token that will not be
       accepted due to an error action in a later state.
  */
  if (yyctx->yytoken != YYSYMBOL_YYEMPTY)
    {
      int yyn;
      if (yyarg)
        yyarg[yycount] = yyctx->yytoken;
      ++yycount;
      yyn = yypcontext_expected_tokens (yyctx,
                                        yyarg ? yyarg + 1 : yyarg, yyargn - 1);
      if (yyn == YYENOMEM)
        return YYENOMEM;
      else
        yycount += yyn;
    }
  return yycount;
}

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return -1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return YYENOMEM if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYPTRDIFF_T *yymsg_alloc, char **yymsg,
                const yypcontext_t *yyctx)
{
  enum { YYARGS_MAX = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat: reported tokens (one for the "unexpected",
     one per "expected"). */
  yysymbol_kind_t yyarg[YYARGS_MAX];
  /* Cumulated lengths of YYARG.  */
  YYPTRDIFF_T yysize = 0;

  /* Actual size of YYARG. */
  int yycount = yy_syntax_error_arguments (yyctx, yyarg, YYARGS_MAX);
  if (yycount == YYENOMEM)
    return YYENOMEM;

  switch (yycount)
    {
#define YYCASE_(N, S)                       \
      case N:                               \
        yyformat = S;                       \
        break
    default: /* Avoid compiler warnings. */
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
#undef YYCASE_
    }

  /* Compute error message size.  Don't count the "%s"s, but reserve
     room for the terminator.  */
  yysize = yystrlen (yyformat) - 2 * yycount + 1;
  {
    int yyi;
    for (yyi = 0; yyi < yycount; ++yyi)
      {
        YYPTRDIFF_T yysize1
          = yysize + yytnamerr (YY_NULLPTR, yytname[yyarg[yyi]]);
        if (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM)
          yysize = yysize1;
        else
          return YYENOMEM;
      }
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return -1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp += yytnamerr (yyp, yytname[yyarg[yyi++]]);
          yyformat += 2;
        }
      else
        {
          ++yyp;
          ++yyformat;
        }
  }
  return 0;
}


/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep, YYLTYPE *yylocationp, gb_parse_ctx *ctx)
{
  YY_USE (yyvaluep);
  YY_USE (yylocationp);
  YY_USE (ctx);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  switch (yykind)
    {
    case YYSYMBOL_IDENT: /* IDENT  */
#line 521 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2584 "src/parser.tab.c"
        break;

    case YYSYMBOL_STRING: /* STRING  */
#line 521 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2590 "src/parser.tab.c"
        break;

    case YYSYMBOL_LENS_CONTENT: /* LENS_CONTENT  */
#line 521 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2596 "src/parser.tab.c"
        break;

    case YYSYMBOL_QUALIFIED_IDENT: /* QUALIFIED_IDENT  */
#line 521 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2602 "src/parser.tab.c"
        break;

    case YYSYMBOL_program: /* program  */
#line 544 "src/parser.y"
            { (void) ((*yyvaluep).stmt_list); }
#line 2608 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement_list: /* statement_list  */
#line 524 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2614 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement: /* statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2620 "src/parser.tab.c"
        break;

    case YYSYMBOL_assignment: /* assignment  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2626 "src/parser.tab.c"
        break;

    case YYSYMBOL_lvalue: /* lvalue  */
#line 522 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2632 "src/parser.tab.c"
        break;

    case YYSYMBOL_variable_name: /* variable_name  */
#line 521 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2638 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_lens: /* comparison_lens  */
#line 529 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 2644 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_name: /* modifier_name  */
#line 521 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2650 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_word: /* modifier_word  */
#line 521 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2656 "src/parser.tab.c"
        break;

    case YYSYMBOL_print_statement: /* print_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2662 "src/parser.tab.c"
        break;

    case YYSYMBOL_call_statement: /* call_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2668 "src/parser.tab.c"
        break;

    case YYSYMBOL_with_lock_statement: /* with_lock_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2674 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_each_statement: /* for_each_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2680 "src/parser.tab.c"
        break;

    case YYSYMBOL_do_loop_statement: /* do_loop_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2686 "src/parser.tab.c"
        break;

    case YYSYMBOL_while_statement: /* while_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2692 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement: /* consider_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2698 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_branch_list: /* consider_branch_list  */
#line 527 "src/parser.y"
            { ast_free_consider_branch_list(((*yyvaluep).consider_branch_list)); }
#line 2704 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_else_opt: /* consider_else_opt  */
#line 524 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2710 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement_list: /* consider_statement_list  */
#line 524 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2716 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_body_statement: /* consider_body_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2722 "src/parser.tab.c"
        break;

    case YYSYMBOL_function_statement: /* function_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2728 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_statement: /* modifier_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2734 "src/parser.tab.c"
        break;

    case YYSYMBOL_program_statement: /* program_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2740 "src/parser.tab.c"
        break;

    case YYSYMBOL_library_statement: /* library_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2746 "src/parser.tab.c"
        break;

    case YYSYMBOL_use_statement: /* use_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2752 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_signature: /* modifier_signature  */
#line 530 "src/parser.y"
            { ast_free_modifier_signature(((*yyvaluep).modifier_signature)); }
#line 2758 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_context: /* modifier_context  */
#line 521 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2764 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_statement: /* watch_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2770 "src/parser.tab.c"
        break;

    case YYSYMBOL_unwatch_statement: /* unwatch_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2776 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_list: /* watch_target_list  */
#line 528 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2782 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_statement: /* server_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2788 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item_list: /* server_item_list  */
#line 534 "src/parser.y"
            { ast_free_server_item_list(((*yyvaluep).server_item_list)); }
#line 2794 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item: /* server_item  */
#line 533 "src/parser.y"
            { AstServerItemList one = ast_server_item_list_append(ast_server_item_list_empty(), ((*yyvaluep).server_item)); ast_free_server_item_list(one); }
#line 2800 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_string_list: /* server_string_list  */
#line 528 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2806 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_path: /* watch_target_path  */
#line 521 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2812 "src/parser.tab.c"
        break;

    case YYSYMBOL_without_watchers_statement: /* without_watchers_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2818 "src/parser.tab.c"
        break;

    case YYSYMBOL_on_error_statement: /* on_error_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2824 "src/parser.tab.c"
        break;

    case YYSYMBOL_error_statement: /* error_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2830 "src/parser.tab.c"
        break;

    case YYSYMBOL_return_statement: /* return_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2836 "src/parser.tab.c"
        break;

    case YYSYMBOL_label_statement: /* label_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2842 "src/parser.tab.c"
        break;

    case YYSYMBOL_goto_statement: /* goto_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2848 "src/parser.tab.c"
        break;

    case YYSYMBOL_gosub_statement: /* gosub_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2854 "src/parser.tab.c"
        break;

    case YYSYMBOL_break_statement: /* break_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2860 "src/parser.tab.c"
        break;

    case YYSYMBOL_continue_statement: /* continue_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2866 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_statement: /* if_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2872 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_block_tail: /* if_block_tail  */
#line 524 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2878 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_inline_tail: /* if_inline_tail  */
#line 524 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2884 "src/parser.tab.c"
        break;

    case YYSYMBOL_inline_statement: /* inline_statement  */
#line 523 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2890 "src/parser.tab.c"
        break;

    case YYSYMBOL_expression: /* expression  */
#line 522 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2896 "src/parser.tab.c"
        break;

    case YYSYMBOL_or_expression: /* or_expression  */
#line 522 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2902 "src/parser.tab.c"
        break;

    case YYSYMBOL_and_expression: /* and_expression  */
#line 522 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2908 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_expression: /* comparison_expression  */
#line 522 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2914 "src/parser.tab.c"
        break;

    case YYSYMBOL_additive_expression: /* additive_expression  */
#line 522 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2920 "src/parser.tab.c"
        break;

    case YYSYMBOL_multiplicative_expression: /* multiplicative_expression  */
#line 522 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2926 "src/parser.tab.c"
        break;

    case YYSYMBOL_unary_expression: /* unary_expression  */
#line 522 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2932 "src/parser.tab.c"
        break;

    case YYSYMBOL_postfix_expression: /* postfix_expression  */
#line 522 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2938 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_operator: /* comparison_operator  */
#line 521 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2944 "src/parser.tab.c"
        break;

    case YYSYMBOL_primary: /* primary  */
#line 522 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2950 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_literal: /* record_literal  */
#line 522 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2956 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_suffix: /* ident_suffix  */
#line 531 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 2962 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_dot_suffix: /* ident_dot_suffix  */
#line 531 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 2968 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list_opt: /* argument_list_opt  */
#line 525 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 2974 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list: /* argument_list  */
#line 525 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 2980 "src/parser.tab.c"
        break;

    case YYSYMBOL_array_argument_list: /* array_argument_list  */
#line 525 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 2986 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list_opt: /* parameter_list_opt  */
#line 528 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2992 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list: /* parameter_list  */
#line 528 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2998 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_name: /* field_name  */
#line 521 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3004 "src/parser.tab.c"
        break;

    case YYSYMBOL_dot_field_name: /* dot_field_name  */
#line 521 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3010 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_field_list: /* record_field_list  */
#line 526 "src/parser.y"
            { ast_free_record_field_list(((*yyvaluep).record_field_list)); }
#line 3016 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_policy: /* field_policy  */
#line 532 "src/parser.y"
            { ast_free_expr(((*yyvaluep).field_policy).reset_expr); }
#line 3022 "src/parser.tab.c"
        break;

      default:
        break;
    }
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}






/*----------.
| yyparse.  |
`----------*/

int
yyparse (gb_parse_ctx *ctx)
{
/* Lookahead token kind.  */
int yychar;


/* The semantic value of the lookahead symbol.  */
/* Default value used for initialization, for pacifying older GCCs
   or non-GCC compilers.  */
YY_INITIAL_VALUE (static YYSTYPE yyval_default;)
YYSTYPE yylval YY_INITIAL_VALUE (= yyval_default);

/* Location data for the lookahead symbol.  */
static YYLTYPE yyloc_default
# if defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL
  = { 1, 1, 1, 1 }
# endif
;
YYLTYPE yylloc = yyloc_default;

    /* Number of syntax errors so far.  */
    int yynerrs = 0;

    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

    /* The location stack: array, bottom, top.  */
    YYLTYPE yylsa[YYINITDEPTH];
    YYLTYPE *yyls = yylsa;
    YYLTYPE *yylsp = yyls;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;
  YYLTYPE yyloc;

  /* The locations where the error started and ended.  */
  YYLTYPE yyerror_range[3];

  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYPTRDIFF_T yymsg_alloc = sizeof yymsgbuf;

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N), yylsp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  yylsp[0] = yylloc;
  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;
        YYLTYPE *yyls1 = yyls;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yyls1, yysize * YYSIZEOF (*yylsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
        yyls = yyls1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
        YYSTACK_RELOCATE (yyls_alloc, yyls);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;
      yylsp = yyls + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex (&yylval, &yylloc, ctx);
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      yyerror_range[1] = yylloc;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END
  *++yylsp = yylloc;

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];

  /* Default location. */
  YYLLOC_DEFAULT (yyloc, (yylsp - yylen), yylen);
  yyerror_range[1] = yyloc;
  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* program: statement_list  */
#line 549 "src/parser.y"
                     { ctx->parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3328 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 553 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3334 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 554 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3340 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 555 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3346 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 559 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3352 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 560 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3358 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 561 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3364 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 562 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3370 "src/parser.tab.c"
    break;

  case 10: /* statement: for_each_statement  */
#line 563 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3376 "src/parser.tab.c"
    break;

  case 11: /* statement: while_statement  */
#line 564 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3382 "src/parser.tab.c"
    break;

  case 12: /* statement: do_loop_statement  */
#line 565 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3388 "src/parser.tab.c"
    break;

  case 13: /* statement: consider_statement  */
#line 566 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3394 "src/parser.tab.c"
    break;

  case 14: /* statement: function_statement  */
#line 567 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3400 "src/parser.tab.c"
    break;

  case 15: /* statement: modifier_statement  */
#line 568 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3406 "src/parser.tab.c"
    break;

  case 16: /* statement: program_statement  */
#line 569 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3412 "src/parser.tab.c"
    break;

  case 17: /* statement: library_statement  */
#line 570 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3418 "src/parser.tab.c"
    break;

  case 18: /* statement: use_statement NEWLINE  */
#line 571 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3424 "src/parser.tab.c"
    break;

  case 19: /* statement: watch_statement  */
#line 572 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3430 "src/parser.tab.c"
    break;

  case 20: /* statement: server_statement  */
#line 573 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3436 "src/parser.tab.c"
    break;

  case 21: /* statement: unwatch_statement NEWLINE  */
#line 574 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3442 "src/parser.tab.c"
    break;

  case 22: /* statement: without_watchers_statement  */
#line 575 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3448 "src/parser.tab.c"
    break;

  case 23: /* statement: on_error_statement NEWLINE  */
#line 576 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3454 "src/parser.tab.c"
    break;

  case 24: /* statement: error_statement NEWLINE  */
#line 577 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3460 "src/parser.tab.c"
    break;

  case 25: /* statement: return_statement NEWLINE  */
#line 578 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3466 "src/parser.tab.c"
    break;

  case 26: /* statement: label_statement NEWLINE  */
#line 579 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3472 "src/parser.tab.c"
    break;

  case 27: /* statement: goto_statement NEWLINE  */
#line 580 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3478 "src/parser.tab.c"
    break;

  case 28: /* statement: gosub_statement NEWLINE  */
#line 581 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3484 "src/parser.tab.c"
    break;

  case 29: /* statement: break_statement NEWLINE  */
#line 582 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3490 "src/parser.tab.c"
    break;

  case 30: /* statement: continue_statement NEWLINE  */
#line 583 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3496 "src/parser.tab.c"
    break;

  case 31: /* statement: if_statement  */
#line 584 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3502 "src/parser.tab.c"
    break;

  case 32: /* assignment: lvalue OP_EQ expression  */
#line 588 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 3508 "src/parser.tab.c"
    break;

  case 33: /* assignment: lvalue comparison_lens OP_EQ expression  */
#line 592 "src/parser.y"
                                              {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 3522 "src/parser.tab.c"
    break;

  case 34: /* lvalue: variable_name  */
#line 604 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3528 "src/parser.tab.c"
    break;

  case 35: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 605 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3534 "src/parser.tab.c"
    break;

  case 36: /* lvalue: lvalue DOT dot_field_name  */
#line 606 "src/parser.y"
                                             { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3540 "src/parser.tab.c"
    break;

  case 37: /* variable_name: IDENT  */
#line 610 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 3546 "src/parser.tab.c"
    break;

  case 38: /* variable_name: END  */
#line 611 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 3552 "src/parser.tab.c"
    break;

  case 39: /* variable_name: NEXT  */
#line 612 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 3558 "src/parser.tab.c"
    break;

  case 40: /* variable_name: LOOP  */
#line 617 "src/parser.y"
                        { (yyval.text) = copy_const("loop"); }
#line 3564 "src/parser.tab.c"
    break;

  case 41: /* variable_name: UNTIL  */
#line 618 "src/parser.y"
                         { (yyval.text) = copy_const("until"); }
#line 3570 "src/parser.tab.c"
    break;

  case 42: /* $@1: %empty  */
#line 622 "src/parser.y"
             { lexer_begin_lens_content(ctx->active_lexer); }
#line 3576 "src/parser.tab.c"
    break;

  case 43: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 622 "src/parser.y"
                                                                                  {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 3584 "src/parser.tab.c"
    break;

  case 44: /* modifier_name: modifier_word  */
#line 628 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 3590 "src/parser.tab.c"
    break;

  case 45: /* modifier_name: modifier_name modifier_word  */
#line 629 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 3596 "src/parser.tab.c"
    break;

  case 46: /* modifier_word: IDENT  */
#line 633 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 3602 "src/parser.tab.c"
    break;

  case 47: /* modifier_word: TO  */
#line 634 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 3608 "src/parser.tab.c"
    break;

  case 48: /* modifier_word: END  */
#line 635 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 3614 "src/parser.tab.c"
    break;

  case 49: /* modifier_word: NEXT  */
#line 636 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 3620 "src/parser.tab.c"
    break;

  case 50: /* print_statement: PRINT expression  */
#line 640 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 3626 "src/parser.tab.c"
    break;

  case 51: /* print_statement: PRINT TO ERROR_VALUE expression  */
#line 646 "src/parser.y"
                                      { (yyval.stmt) = ast_print_error((yyvsp[0].expr)); }
#line 3632 "src/parser.tab.c"
    break;

  case 52: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 650 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 3638 "src/parser.tab.c"
    break;

  case 53: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 651 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 3649 "src/parser.tab.c"
    break;

  case 54: /* call_statement: lvalue DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 657 "src/parser.y"
                                                       {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 3659 "src/parser.tab.c"
    break;

  case 55: /* call_statement: lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 662 "src/parser.y"
                                                                 {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 3673 "src/parser.tab.c"
    break;

  case 56: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 671 "src/parser.y"
                                                            {
        size_t length = strlen("error.") + strlen((yyvsp[-3].text));
        char *name = malloc(length + 1);
        if (!name) {
            abort();
        }
        snprintf(name, length + 1, "error.%s", (yyvsp[-3].text));
        free((yyvsp[-3].text));
        (yyval.stmt) = ast_expr_stmt(ast_call(name, (yyvsp[-1].expr_list)));
      }
#line 3688 "src/parser.tab.c"
    break;

  case 57: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 684 "src/parser.y"
                                                                                  {
        if (strcmp((yyvsp[-8].text), "lock") != 0) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "expected lock in with lock block");
            free((yyvsp[-8].text));
            (yyvsp[-8].text) = NULL;
            YYERROR;
        }
        free((yyvsp[-8].text));
        (yyval.stmt) = ast_with_lock((yyvsp[-6].expr), (yyvsp[-3].stmt_list));
      }
#line 3705 "src/parser.tab.c"
    break;

  case 58: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list END FOR NEWLINE  */
#line 699 "src/parser.y"
                                                                     {
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 3713 "src/parser.tab.c"
    break;

  case 59: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list END FOR NEWLINE  */
#line 702 "src/parser.y"
                                                                          {
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 3721 "src/parser.tab.c"
    break;

  case 60: /* for_each_statement: FOR IDENT OP_EQ expression TO expression NEWLINE statement_list END FOR NEWLINE  */
#line 708 "src/parser.y"
                                                                                      {
        (yyval.stmt) = ast_for_range((yyvsp[-9].text), (yyvsp[-7].expr), (yyvsp[-5].expr), NULL, (yyvsp[-3].stmt_list));
      }
#line 3729 "src/parser.tab.c"
    break;

  case 61: /* for_each_statement: FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list END FOR NEWLINE  */
#line 711 "src/parser.y"
                                                                                                      {
        (yyval.stmt) = ast_for_range((yyvsp[-11].text), (yyvsp[-9].expr), (yyvsp[-7].expr), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 3737 "src/parser.tab.c"
    break;

  case 62: /* do_loop_statement: DO NEWLINE statement_list LOOP UNTIL expression NEWLINE  */
#line 719 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_do_loop((yyvsp[-4].stmt_list), (yyvsp[-1].expr), 1);
      }
#line 3745 "src/parser.tab.c"
    break;

  case 63: /* do_loop_statement: DO NEWLINE statement_list LOOP WHILE expression NEWLINE  */
#line 722 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_do_loop((yyvsp[-4].stmt_list), (yyvsp[-1].expr), 0);
      }
#line 3753 "src/parser.tab.c"
    break;

  case 64: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 728 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 3761 "src/parser.tab.c"
    break;

  case 65: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 734 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 3769 "src/parser.tab.c"
    break;

  case 66: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 740 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 3777 "src/parser.tab.c"
    break;

  case 67: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 743 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 3785 "src/parser.tab.c"
    break;

  case 68: /* consider_else_opt: %empty  */
#line 749 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3791 "src/parser.tab.c"
    break;

  case 69: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 750 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3797 "src/parser.tab.c"
    break;

  case 70: /* consider_statement_list: %empty  */
#line 754 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3803 "src/parser.tab.c"
    break;

  case 71: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 755 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3809 "src/parser.tab.c"
    break;

  case 72: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 756 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3815 "src/parser.tab.c"
    break;

  case 73: /* consider_body_statement: assignment NEWLINE  */
#line 760 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3821 "src/parser.tab.c"
    break;

  case 74: /* consider_body_statement: print_statement NEWLINE  */
#line 761 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3827 "src/parser.tab.c"
    break;

  case 75: /* consider_body_statement: call_statement NEWLINE  */
#line 762 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3833 "src/parser.tab.c"
    break;

  case 76: /* consider_body_statement: with_lock_statement  */
#line 763 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3839 "src/parser.tab.c"
    break;

  case 77: /* consider_body_statement: for_each_statement  */
#line 764 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3845 "src/parser.tab.c"
    break;

  case 78: /* consider_body_statement: while_statement  */
#line 765 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3851 "src/parser.tab.c"
    break;

  case 79: /* consider_body_statement: do_loop_statement  */
#line 766 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3857 "src/parser.tab.c"
    break;

  case 80: /* consider_body_statement: consider_statement  */
#line 767 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3863 "src/parser.tab.c"
    break;

  case 81: /* consider_body_statement: function_statement  */
#line 768 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3869 "src/parser.tab.c"
    break;

  case 82: /* consider_body_statement: modifier_statement  */
#line 769 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3875 "src/parser.tab.c"
    break;

  case 83: /* consider_body_statement: program_statement  */
#line 770 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3881 "src/parser.tab.c"
    break;

  case 84: /* consider_body_statement: library_statement  */
#line 771 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3887 "src/parser.tab.c"
    break;

  case 85: /* consider_body_statement: use_statement NEWLINE  */
#line 772 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3893 "src/parser.tab.c"
    break;

  case 86: /* consider_body_statement: watch_statement  */
#line 773 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3899 "src/parser.tab.c"
    break;

  case 87: /* consider_body_statement: unwatch_statement NEWLINE  */
#line 774 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3905 "src/parser.tab.c"
    break;

  case 88: /* consider_body_statement: without_watchers_statement  */
#line 775 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3911 "src/parser.tab.c"
    break;

  case 89: /* consider_body_statement: on_error_statement NEWLINE  */
#line 776 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3917 "src/parser.tab.c"
    break;

  case 90: /* consider_body_statement: error_statement NEWLINE  */
#line 777 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3923 "src/parser.tab.c"
    break;

  case 91: /* consider_body_statement: return_statement NEWLINE  */
#line 778 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3929 "src/parser.tab.c"
    break;

  case 92: /* consider_body_statement: label_statement NEWLINE  */
#line 779 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3935 "src/parser.tab.c"
    break;

  case 93: /* consider_body_statement: goto_statement NEWLINE  */
#line 780 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3941 "src/parser.tab.c"
    break;

  case 94: /* consider_body_statement: gosub_statement NEWLINE  */
#line 781 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3947 "src/parser.tab.c"
    break;

  case 95: /* consider_body_statement: break_statement NEWLINE  */
#line 782 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3953 "src/parser.tab.c"
    break;

  case 96: /* consider_body_statement: continue_statement NEWLINE  */
#line 783 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3959 "src/parser.tab.c"
    break;

  case 97: /* consider_body_statement: if_statement  */
#line 784 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3965 "src/parser.tab.c"
    break;

  case 98: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 788 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 3973 "src/parser.tab.c"
    break;

  case 99: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 791 "src/parser.y"
                                                                                                            {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 3982 "src/parser.tab.c"
    break;

  case 100: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 798 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 3990 "src/parser.tab.c"
    break;

  case 101: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 801 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 3998 "src/parser.tab.c"
    break;

  case 102: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 807 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4006 "src/parser.tab.c"
    break;

  case 103: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 813 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 4014 "src/parser.tab.c"
    break;

  case 104: /* use_statement: USE IDENT  */
#line 819 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4020 "src/parser.tab.c"
    break;

  case 105: /* use_statement: LOAD IDENT  */
#line 820 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4026 "src/parser.tab.c"
    break;

  case 106: /* use_statement: USE STRING  */
#line 821 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4032 "src/parser.tab.c"
    break;

  case 107: /* use_statement: LOAD STRING  */
#line 822 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4038 "src/parser.tab.c"
    break;

  case 108: /* use_statement: USE IDENT IDENT STRING  */
#line 823 "src/parser.y"
                             {
        if (strcmp((yyvsp[-1].text), "from") != 0) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "expected from in use statement");
            free((yyvsp[-2].text));
            free((yyvsp[-1].text));
            free((yyvsp[0].text));
            (yyvsp[-2].text) = NULL;
            (yyvsp[-1].text) = NULL;
            (yyvsp[0].text) = NULL;
            YYERROR;
        }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_use((yyvsp[-2].text), (yyvsp[0].text));
      }
#line 4059 "src/parser.tab.c"
    break;

  case 109: /* use_statement: LOAD IDENT IDENT STRING  */
#line 839 "src/parser.y"
                              {
        if (strcmp((yyvsp[-1].text), "from") != 0) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "expected from in load statement");
            free((yyvsp[-2].text));
            free((yyvsp[-1].text));
            free((yyvsp[0].text));
            (yyvsp[-2].text) = NULL;
            (yyvsp[-1].text) = NULL;
            (yyvsp[0].text) = NULL;
            YYERROR;
        }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_use((yyvsp[-2].text), (yyvsp[0].text));
      }
#line 4080 "src/parser.tab.c"
    break;

  case 110: /* modifier_signature: modifier_name  */
#line 858 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 4086 "src/parser.tab.c"
    break;

  case 111: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 859 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 4092 "src/parser.tab.c"
    break;

  case 112: /* modifier_context: IDENT  */
#line 863 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4098 "src/parser.tab.c"
    break;

  case 113: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 867 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4106 "src/parser.tab.c"
    break;

  case 114: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 870 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 4114 "src/parser.tab.c"
    break;

  case 115: /* watch_statement: WATCH IDENT LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 878 "src/parser.y"
                                                                                           {
        (yyval.stmt) = ast_watch((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4122 "src/parser.tab.c"
    break;

  case 116: /* unwatch_statement: UNWATCH expression  */
#line 884 "src/parser.y"
                         { (yyval.stmt) = ast_unwatch((yyvsp[0].expr)); }
#line 4128 "src/parser.tab.c"
    break;

  case 117: /* watch_target_list: watch_target_path  */
#line 888 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4134 "src/parser.tab.c"
    break;

  case 118: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 889 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4140 "src/parser.tab.c"
    break;

  case 119: /* server_statement: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 908 "src/parser.y"
                                                                                             {
        (yyval.stmt) = ast_server((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4148 "src/parser.tab.c"
    break;

  case 120: /* server_statement: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 911 "src/parser.y"
                                                                           {
        (yyval.stmt) = ast_server((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4156 "src/parser.tab.c"
    break;

  case 121: /* server_item_list: %empty  */
#line 917 "src/parser.y"
             { (yyval.server_item_list) = ast_server_item_list_empty(); }
#line 4162 "src/parser.tab.c"
    break;

  case 122: /* server_item_list: server_item_list NEWLINE  */
#line 918 "src/parser.y"
                               { (yyval.server_item_list) = (yyvsp[-1].server_item_list); }
#line 4168 "src/parser.tab.c"
    break;

  case 123: /* server_item_list: server_item_list server_item  */
#line 919 "src/parser.y"
                                   { (yyval.server_item_list) = ast_server_item_list_append((yyvsp[-1].server_item_list), (yyvsp[0].server_item)); }
#line 4174 "src/parser.tab.c"
    break;

  case 124: /* server_item: IDENT server_string_list NEWLINE  */
#line 923 "src/parser.y"
                                       {
        (yyval.server_item) = ast_server_directive((yyvsp[-2].text), (yyvsp[-1].name_list), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 4182 "src/parser.tab.c"
    break;

  case 125: /* server_item: IDENT STRING LPAREN parameter_list_opt RPAREN NEWLINE statement_list END IDENT NEWLINE  */
#line 926 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_handler((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4190 "src/parser.tab.c"
    break;

  case 126: /* server_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 929 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_site((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4198 "src/parser.tab.c"
    break;

  case 127: /* server_item: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 932 "src/parser.y"
                                                                           {
        (yyval.server_item) = ast_server_site((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 4206 "src/parser.tab.c"
    break;

  case 128: /* server_item: ON IDENT NEWLINE statement_list END ON NEWLINE  */
#line 935 "src/parser.y"
                                                     {
        (yyval.server_item) = ast_server_hook((yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 4214 "src/parser.tab.c"
    break;

  case 129: /* server_string_list: STRING  */
#line 941 "src/parser.y"
             { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4220 "src/parser.tab.c"
    break;

  case 130: /* server_string_list: server_string_list COMMA STRING  */
#line 942 "src/parser.y"
                                      { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4226 "src/parser.tab.c"
    break;

  case 131: /* watch_target_path: variable_name  */
#line 946 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 4232 "src/parser.tab.c"
    break;

  case 132: /* watch_target_path: watch_target_path DOT IDENT  */
#line 947 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 4238 "src/parser.tab.c"
    break;

  case 133: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 951 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 4246 "src/parser.tab.c"
    break;

  case 134: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 957 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 4252 "src/parser.tab.c"
    break;

  case 135: /* on_error_statement: ON ERROR_VALUE GOTO NEXT  */
#line 958 "src/parser.y"
                               { (yyval.stmt) = ast_on_error_goto_next(); }
#line 4258 "src/parser.tab.c"
    break;

  case 136: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 959 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 4264 "src/parser.tab.c"
    break;

  case 137: /* on_error_statement: ON IDENT GOTO NEXT  */
#line 960 "src/parser.y"
                         {
        if (!warn_channel_ok(ctx, (yyvsp[-2].text), (yylsp[-2]).first_line, (yylsp[-2]).first_column)) { YYERROR; }
        free((yyvsp[-2].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_NEXT);
      }
#line 4274 "src/parser.tab.c"
    break;

  case 138: /* on_error_statement: ON IDENT GOTO IDENT  */
#line 965 "src/parser.y"
                          {
        /* A warning fires from a statement that SUCCEEDED, so a label jump
         * would mean leaving successful code on an advisory signal. Refused
         * BY NAME rather than as a bare syntax error. */
        if (!warn_channel_ok(ctx, (yyvsp[-2].text), (yylsp[-2]).first_line, (yylsp[-2]).first_column)) { YYERROR; }
        report_diag(ctx, GB_DIAG_PARSE_ERROR, (yylsp[-1]).first_line, (yylsp[-1]).first_column,
                    (yylsp[-1]).first_line, (yylsp[-1]).first_column,
                    "on warning has no goto-label form: a warning does not abandon "
                    "its statement, so there is nothing to jump away from "
                    "(use goto next, stop, ignore or print)");
        free((yyvsp[-2].text)); free((yyvsp[0].text));
        YYERROR;
      }
#line 4292 "src/parser.tab.c"
    break;

  case 139: /* on_error_statement: ON IDENT STOP  */
#line 978 "src/parser.y"
                    {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_STOP);
      }
#line 4302 "src/parser.tab.c"
    break;

  case 140: /* on_error_statement: ON IDENT PRINT  */
#line 983 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_PRINT);
      }
#line 4312 "src/parser.tab.c"
    break;

  case 141: /* on_error_statement: ON IDENT IDENT  */
#line 988 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        int mode = warn_mode_word(ctx, (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column);
        if (mode < 0) { free((yyvsp[-1].text)); free((yyvsp[0].text)); YYERROR; }
        free((yyvsp[-1].text)); free((yyvsp[0].text));
        (yyval.stmt) = ast_on_warning(mode);
      }
#line 4324 "src/parser.tab.c"
    break;

  case 142: /* error_statement: ERROR_VALUE expression  */
#line 998 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 4330 "src/parser.tab.c"
    break;

  case 143: /* return_statement: RETURN  */
#line 1002 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 4336 "src/parser.tab.c"
    break;

  case 144: /* return_statement: RETURN expression  */
#line 1003 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 4342 "src/parser.tab.c"
    break;

  case 145: /* label_statement: variable_name COLON  */
#line 1007 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 4348 "src/parser.tab.c"
    break;

  case 146: /* goto_statement: GOTO variable_name  */
#line 1014 "src/parser.y"
                         { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 4354 "src/parser.tab.c"
    break;

  case 147: /* gosub_statement: GOSUB variable_name  */
#line 1018 "src/parser.y"
                          { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 4360 "src/parser.tab.c"
    break;

  case 148: /* break_statement: BREAK  */
#line 1022 "src/parser.y"
            { (yyval.stmt) = ast_break(); }
#line 4366 "src/parser.tab.c"
    break;

  case 149: /* continue_statement: CONTINUE  */
#line 1026 "src/parser.y"
               { (yyval.stmt) = ast_continue(); }
#line 4372 "src/parser.tab.c"
    break;

  case 150: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1030 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4381 "src/parser.tab.c"
    break;

  case 151: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1034 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4390 "src/parser.tab.c"
    break;

  case 152: /* if_block_tail: END IF NEWLINE  */
#line 1041 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4398 "src/parser.tab.c"
    break;

  case 153: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 1044 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4406 "src/parser.tab.c"
    break;

  case 154: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1047 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4414 "src/parser.tab.c"
    break;

  case 155: /* if_inline_tail: %empty  */
#line 1053 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4422 "src/parser.tab.c"
    break;

  case 156: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 1056 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4430 "src/parser.tab.c"
    break;

  case 157: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1059 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4438 "src/parser.tab.c"
    break;

  case 158: /* inline_statement: assignment  */
#line 1065 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4444 "src/parser.tab.c"
    break;

  case 159: /* inline_statement: print_statement  */
#line 1066 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4450 "src/parser.tab.c"
    break;

  case 160: /* inline_statement: call_statement  */
#line 1067 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4456 "src/parser.tab.c"
    break;

  case 161: /* inline_statement: use_statement  */
#line 1068 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4462 "src/parser.tab.c"
    break;

  case 162: /* inline_statement: on_error_statement  */
#line 1069 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4468 "src/parser.tab.c"
    break;

  case 163: /* inline_statement: error_statement  */
#line 1070 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4474 "src/parser.tab.c"
    break;

  case 164: /* inline_statement: return_statement  */
#line 1071 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4480 "src/parser.tab.c"
    break;

  case 165: /* inline_statement: goto_statement  */
#line 1072 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4486 "src/parser.tab.c"
    break;

  case 166: /* inline_statement: gosub_statement  */
#line 1073 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4492 "src/parser.tab.c"
    break;

  case 167: /* inline_statement: break_statement  */
#line 1074 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4498 "src/parser.tab.c"
    break;

  case 168: /* inline_statement: continue_statement  */
#line 1075 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4504 "src/parser.tab.c"
    break;

  case 169: /* expression: or_expression  */
#line 1079 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 4510 "src/parser.tab.c"
    break;

  case 170: /* or_expression: and_expression  */
#line 1083 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 4516 "src/parser.tab.c"
    break;

  case 171: /* or_expression: or_expression OR and_expression  */
#line 1084 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4522 "src/parser.tab.c"
    break;

  case 172: /* and_expression: comparison_expression  */
#line 1088 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 4528 "src/parser.tab.c"
    break;

  case 173: /* and_expression: and_expression AND comparison_expression  */
#line 1089 "src/parser.y"
                                               { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4534 "src/parser.tab.c"
    break;

  case 174: /* comparison_expression: additive_expression  */
#line 1093 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 4540 "src/parser.tab.c"
    break;

  case 175: /* comparison_expression: additive_expression comparison_operator additive_expression  */
#line 1094 "src/parser.y"
                                                                  { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4546 "src/parser.tab.c"
    break;

  case 176: /* comparison_expression: additive_expression comparison_lens comparison_operator additive_expression  */
#line 1095 "src/parser.y"
                                                                                  {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 4554 "src/parser.tab.c"
    break;

  case 177: /* additive_expression: multiplicative_expression  */
#line 1101 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 4560 "src/parser.tab.c"
    break;

  case 178: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 1102 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4566 "src/parser.tab.c"
    break;

  case 179: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 1103 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4572 "src/parser.tab.c"
    break;

  case 180: /* multiplicative_expression: unary_expression  */
#line 1107 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 4578 "src/parser.tab.c"
    break;

  case 181: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 1108 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4584 "src/parser.tab.c"
    break;

  case 182: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 1109 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4590 "src/parser.tab.c"
    break;

  case 183: /* unary_expression: postfix_expression  */
#line 1113 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 4596 "src/parser.tab.c"
    break;

  case 184: /* unary_expression: NOT unary_expression  */
#line 1114 "src/parser.y"
                           { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4602 "src/parser.tab.c"
    break;

  case 185: /* unary_expression: MINUS unary_expression  */
#line 1115 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4608 "src/parser.tab.c"
    break;

  case 186: /* unary_expression: NEW postfix_expression  */
#line 1116 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4614 "src/parser.tab.c"
    break;

  case 187: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 1117 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 4620 "src/parser.tab.c"
    break;

  case 188: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 1118 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4626 "src/parser.tab.c"
    break;

  case 189: /* postfix_expression: primary  */
#line 1122 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 4632 "src/parser.tab.c"
    break;

  case 190: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 1123 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4638 "src/parser.tab.c"
    break;

  case 191: /* postfix_expression: postfix_expression DOT dot_field_name  */
#line 1124 "src/parser.y"
                                            { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4644 "src/parser.tab.c"
    break;

  case 192: /* postfix_expression: postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1125 "src/parser.y"
                                                                   {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        (yyval.expr) = expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 4654 "src/parser.tab.c"
    break;

  case 193: /* postfix_expression: postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1130 "src/parser.y"
                                                                             {
        /* Method call on an expression receiver where the lexer folded the final
         * `field.method(` into one QUALIFIED_IDENT (e.g. a.b.method(): the
         * `b.method` is a QUALIFIED_IDENT following `a DOT`). Split it: the field
         * extends the receiver, the tail is the method name. */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.expr) = expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 4670 "src/parser.tab.c"
    break;

  case 194: /* comparison_operator: OP_EQ  */
#line 1144 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 4676 "src/parser.tab.c"
    break;

  case 195: /* comparison_operator: OP_NE  */
#line 1145 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 4682 "src/parser.tab.c"
    break;

  case 196: /* comparison_operator: OP_GT  */
#line 1146 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 4688 "src/parser.tab.c"
    break;

  case 197: /* comparison_operator: OP_LT  */
#line 1147 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 4694 "src/parser.tab.c"
    break;

  case 198: /* comparison_operator: OP_GE  */
#line 1148 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 4700 "src/parser.tab.c"
    break;

  case 199: /* comparison_operator: OP_LE  */
#line 1149 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 4706 "src/parser.tab.c"
    break;

  case 200: /* comparison_operator: OP_NGT  */
#line 1150 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 4712 "src/parser.tab.c"
    break;

  case 201: /* comparison_operator: OP_NLT  */
#line 1151 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 4718 "src/parser.tab.c"
    break;

  case 202: /* comparison_operator: OP_NGE  */
#line 1152 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 4724 "src/parser.tab.c"
    break;

  case 203: /* comparison_operator: OP_NLE  */
#line 1153 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 4730 "src/parser.tab.c"
    break;

  case 204: /* primary: NUMBER  */
#line 1157 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4736 "src/parser.tab.c"
    break;

  case 205: /* primary: WATCHERS LPAREN RPAREN  */
#line 1158 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_call(copy_const("watchers"), ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4742 "src/parser.tab.c"
    break;

  case 206: /* primary: duration_terms  */
#line 1159 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4748 "src/parser.tab.c"
    break;

  case 207: /* primary: STRING  */
#line 1160 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4754 "src/parser.tab.c"
    break;

  case 208: /* primary: variable_name ident_suffix  */
#line 1161 "src/parser.y"
                                 {
        if ((yyvsp[0].ident_suffix).kind == IDENT_SUFFIX_CALL) {
            (yyval.expr) = expr_at(ast_call((yyvsp[-1].text), (yyvsp[0].ident_suffix).args), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
        } else if ((yyvsp[0].ident_suffix).kind == IDENT_SUFFIX_FIELD) {
            (yyval.expr) = expr_at(ast_field(expr_at(ast_ident((yyvsp[-1].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column), (yyvsp[0].ident_suffix).name), (yylsp[0]).first_line, (yylsp[0]).first_column);
        } else if ((yyvsp[0].ident_suffix).kind == IDENT_SUFFIX_QUALIFIED_CALL) {
            (yyval.expr) = expr_at(ast_qualified_call((yyvsp[-1].text), (yyvsp[0].ident_suffix).name, (yyvsp[0].ident_suffix).args), (yylsp[0]).first_line, (yylsp[0]).first_column);
        } else if ((yyvsp[0].ident_suffix).kind == IDENT_SUFFIX_METHOD) {
            char *field = NULL;
            char *method = NULL;
            split_qualified_ident((yyvsp[0].ident_suffix).name, &field, &method);
            AstExpr *recv = expr_at(ast_field(expr_at(ast_ident((yyvsp[-1].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column), field), (yylsp[0]).first_line, (yylsp[0]).first_column);
            (yyval.expr) = expr_at(ast_method_call(recv, method, (yyvsp[0].ident_suffix).args), (yylsp[0]).first_line, (yylsp[0]).first_column);
        } else {
            (yyval.expr) = expr_at(ast_ident((yyvsp[-1].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
        }
      }
#line 4776 "src/parser.tab.c"
    break;

  case 209: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1178 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 4787 "src/parser.tab.c"
    break;

  case 210: /* primary: ERROR_VALUE  */
#line 1184 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4793 "src/parser.tab.c"
    break;

  case 211: /* primary: TRUE  */
#line 1185 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4799 "src/parser.tab.c"
    break;

  case 212: /* primary: FALSE  */
#line 1186 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4805 "src/parser.tab.c"
    break;

  case 213: /* primary: NOTHING  */
#line 1187 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4811 "src/parser.tab.c"
    break;

  case 214: /* primary: UNKNOWN_VALUE  */
#line 1188 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4817 "src/parser.tab.c"
    break;

  case 215: /* primary: LPAREN expression RPAREN  */
#line 1189 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 4823 "src/parser.tab.c"
    break;

  case 216: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 1190 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4829 "src/parser.tab.c"
    break;

  case 217: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 1191 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4835 "src/parser.tab.c"
    break;

  case 218: /* primary: record_literal  */
#line 1192 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 4841 "src/parser.tab.c"
    break;

  case 219: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 1196 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4847 "src/parser.tab.c"
    break;

  case 220: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 1197 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4853 "src/parser.tab.c"
    break;

  case 221: /* ident_suffix: %empty  */
#line 1201 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 4863 "src/parser.tab.c"
    break;

  case 222: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 1206 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 4873 "src/parser.tab.c"
    break;

  case 223: /* ident_suffix: DOT dot_field_name ident_dot_suffix  */
#line 1211 "src/parser.y"
                                          {
        /* dot_field_name, not IDENT: a keyword is a legal FIELD name after a
         * dot, because nothing but a name can appear there. */
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 4884 "src/parser.tab.c"
    break;

  case 224: /* ident_suffix: DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1217 "src/parser.y"
                                                          {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        (yyval.ident_suffix).kind = IDENT_SUFFIX_METHOD;
        (yyval.ident_suffix).name = (yyvsp[-3].text);
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 4898 "src/parser.tab.c"
    break;

  case 225: /* ident_dot_suffix: %empty  */
#line 1229 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 4908 "src/parser.tab.c"
    break;

  case 226: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 1234 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 4918 "src/parser.tab.c"
    break;

  case 227: /* duration_terms: NUMBER IDENT  */
#line 1242 "src/parser.y"
                   {
        AstDuration duration = {0};
        (yyval.duration) = duration_add_unit(duration, (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 4927 "src/parser.tab.c"
    break;

  case 228: /* duration_terms: duration_terms NUMBER IDENT  */
#line 1246 "src/parser.y"
                                  {
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 4935 "src/parser.tab.c"
    break;

  case 229: /* argument_list_opt: %empty  */
#line 1252 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 4941 "src/parser.tab.c"
    break;

  case 230: /* argument_list_opt: argument_list  */
#line 1253 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 4947 "src/parser.tab.c"
    break;

  case 231: /* argument_list: expression  */
#line 1257 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 4953 "src/parser.tab.c"
    break;

  case 232: /* argument_list: argument_list COMMA expression  */
#line 1258 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 4959 "src/parser.tab.c"
    break;

  case 233: /* array_argument_list: expression  */
#line 1262 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 4965 "src/parser.tab.c"
    break;

  case 234: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 1263 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 4971 "src/parser.tab.c"
    break;

  case 235: /* parameter_list_opt: %empty  */
#line 1267 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 4977 "src/parser.tab.c"
    break;

  case 236: /* parameter_list_opt: parameter_list  */
#line 1268 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 4983 "src/parser.tab.c"
    break;

  case 237: /* parameter_list: IDENT  */
#line 1272 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4989 "src/parser.tab.c"
    break;

  case 238: /* parameter_list: parameter_list COMMA IDENT  */
#line 1273 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4995 "src/parser.tab.c"
    break;

  case 239: /* field_name: dot_field_name  */
#line 1286 "src/parser.y"
                     { (yyval.text) = (yyvsp[0].text); }
#line 5001 "src/parser.tab.c"
    break;

  case 240: /* field_name: STRING  */
#line 1293 "src/parser.y"
             { (yyval.text) = (yyvsp[0].text); }
#line 5007 "src/parser.tab.c"
    break;

  case 241: /* dot_field_name: IDENT  */
#line 1302 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 5013 "src/parser.tab.c"
    break;

  case 242: /* dot_field_name: AS  */
#line 1303 "src/parser.y"
                     { (yyval.text) = kw_name("as"); }
#line 5019 "src/parser.tab.c"
    break;

  case 243: /* dot_field_name: NEXT  */
#line 1304 "src/parser.y"
                     { (yyval.text) = kw_name("next"); }
#line 5025 "src/parser.tab.c"
    break;

  case 244: /* dot_field_name: STOP  */
#line 1305 "src/parser.y"
                     { (yyval.text) = kw_name("stop"); }
#line 5031 "src/parser.tab.c"
    break;

  case 245: /* dot_field_name: ERROR_VALUE  */
#line 1306 "src/parser.y"
                     { (yyval.text) = kw_name("error"); }
#line 5037 "src/parser.tab.c"
    break;

  case 246: /* dot_field_name: END  */
#line 1307 "src/parser.y"
                     { (yyval.text) = kw_name("end"); }
#line 5043 "src/parser.tab.c"
    break;

  case 247: /* dot_field_name: TO  */
#line 1308 "src/parser.y"
                     { (yyval.text) = kw_name("to"); }
#line 5049 "src/parser.tab.c"
    break;

  case 248: /* dot_field_name: IN  */
#line 1309 "src/parser.y"
                     { (yyval.text) = kw_name("in"); }
#line 5055 "src/parser.tab.c"
    break;

  case 249: /* dot_field_name: ON  */
#line 1310 "src/parser.y"
                     { (yyval.text) = kw_name("on"); }
#line 5061 "src/parser.tab.c"
    break;

  case 250: /* dot_field_name: NEW  */
#line 1311 "src/parser.y"
                     { (yyval.text) = kw_name("new"); }
#line 5067 "src/parser.tab.c"
    break;

  case 251: /* dot_field_name: EACH  */
#line 1312 "src/parser.y"
                     { (yyval.text) = kw_name("each"); }
#line 5073 "src/parser.tab.c"
    break;

  case 252: /* dot_field_name: WITH  */
#line 1313 "src/parser.y"
                     { (yyval.text) = kw_name("with"); }
#line 5079 "src/parser.tab.c"
    break;

  case 253: /* dot_field_name: WITHOUT  */
#line 1314 "src/parser.y"
                     { (yyval.text) = kw_name("without"); }
#line 5085 "src/parser.tab.c"
    break;

  case 254: /* dot_field_name: THEN  */
#line 1315 "src/parser.y"
                     { (yyval.text) = kw_name("then"); }
#line 5091 "src/parser.tab.c"
    break;

  case 255: /* dot_field_name: ELSE  */
#line 1316 "src/parser.y"
                     { (yyval.text) = kw_name("else"); }
#line 5097 "src/parser.tab.c"
    break;

  case 256: /* dot_field_name: FOR  */
#line 1317 "src/parser.y"
                     { (yyval.text) = kw_name("for"); }
#line 5103 "src/parser.tab.c"
    break;

  case 257: /* dot_field_name: IF  */
#line 1318 "src/parser.y"
                     { (yyval.text) = kw_name("if"); }
#line 5109 "src/parser.tab.c"
    break;

  case 258: /* dot_field_name: WHILE  */
#line 1319 "src/parser.y"
                     { (yyval.text) = kw_name("while"); }
#line 5115 "src/parser.tab.c"
    break;

  case 259: /* dot_field_name: DO  */
#line 1320 "src/parser.y"
                     { (yyval.text) = kw_name("do"); }
#line 5121 "src/parser.tab.c"
    break;

  case 260: /* dot_field_name: LOOP  */
#line 1321 "src/parser.y"
                     { (yyval.text) = kw_name("loop"); }
#line 5127 "src/parser.tab.c"
    break;

  case 261: /* dot_field_name: UNTIL  */
#line 1322 "src/parser.y"
                     { (yyval.text) = kw_name("until"); }
#line 5133 "src/parser.tab.c"
    break;

  case 262: /* dot_field_name: PRINT  */
#line 1323 "src/parser.y"
                     { (yyval.text) = kw_name("print"); }
#line 5139 "src/parser.tab.c"
    break;

  case 263: /* dot_field_name: RETURN  */
#line 1324 "src/parser.y"
                     { (yyval.text) = kw_name("return"); }
#line 5145 "src/parser.tab.c"
    break;

  case 264: /* dot_field_name: LOAD  */
#line 1325 "src/parser.y"
                     { (yyval.text) = kw_name("load"); }
#line 5151 "src/parser.tab.c"
    break;

  case 265: /* dot_field_name: USE  */
#line 1326 "src/parser.y"
                     { (yyval.text) = kw_name("use"); }
#line 5157 "src/parser.tab.c"
    break;

  case 266: /* dot_field_name: NOT  */
#line 1327 "src/parser.y"
                     { (yyval.text) = kw_name("not"); }
#line 5163 "src/parser.tab.c"
    break;

  case 267: /* dot_field_name: AND  */
#line 1328 "src/parser.y"
                     { (yyval.text) = kw_name("and"); }
#line 5169 "src/parser.tab.c"
    break;

  case 268: /* dot_field_name: OR  */
#line 1329 "src/parser.y"
                     { (yyval.text) = kw_name("or"); }
#line 5175 "src/parser.tab.c"
    break;

  case 269: /* dot_field_name: TRUE  */
#line 1330 "src/parser.y"
                     { (yyval.text) = kw_name("true"); }
#line 5181 "src/parser.tab.c"
    break;

  case 270: /* dot_field_name: FALSE  */
#line 1331 "src/parser.y"
                     { (yyval.text) = kw_name("false"); }
#line 5187 "src/parser.tab.c"
    break;

  case 271: /* dot_field_name: NOTHING  */
#line 1332 "src/parser.y"
                     { (yyval.text) = kw_name("nothing"); }
#line 5193 "src/parser.tab.c"
    break;

  case 272: /* dot_field_name: BREAK  */
#line 1333 "src/parser.y"
                     { (yyval.text) = kw_name("break"); }
#line 5199 "src/parser.tab.c"
    break;

  case 273: /* dot_field_name: CONTINUE  */
#line 1334 "src/parser.y"
                     { (yyval.text) = kw_name("continue"); }
#line 5205 "src/parser.tab.c"
    break;

  case 274: /* dot_field_name: GOTO  */
#line 1335 "src/parser.y"
                     { (yyval.text) = kw_name("goto"); }
#line 5211 "src/parser.tab.c"
    break;

  case 275: /* dot_field_name: GOSUB  */
#line 1336 "src/parser.y"
                     { (yyval.text) = kw_name("gosub"); }
#line 5217 "src/parser.tab.c"
    break;

  case 276: /* dot_field_name: SPAWN  */
#line 1337 "src/parser.y"
                     { (yyval.text) = kw_name("spawn"); }
#line 5223 "src/parser.tab.c"
    break;

  case 277: /* dot_field_name: EXPORT  */
#line 1338 "src/parser.y"
                     { (yyval.text) = kw_name("export"); }
#line 5229 "src/parser.tab.c"
    break;

  case 278: /* dot_field_name: LIBRARY  */
#line 1339 "src/parser.y"
                     { (yyval.text) = kw_name("library"); }
#line 5235 "src/parser.tab.c"
    break;

  case 279: /* dot_field_name: FUNCTION  */
#line 1340 "src/parser.y"
                     { (yyval.text) = kw_name("function"); }
#line 5241 "src/parser.tab.c"
    break;

  case 280: /* dot_field_name: MODIFIER  */
#line 1341 "src/parser.y"
                     { (yyval.text) = kw_name("modifier"); }
#line 5247 "src/parser.tab.c"
    break;

  case 281: /* dot_field_name: PROGRAM  */
#line 1342 "src/parser.y"
                     { (yyval.text) = kw_name("program"); }
#line 5253 "src/parser.tab.c"
    break;

  case 282: /* dot_field_name: WATCH  */
#line 1343 "src/parser.y"
                     { (yyval.text) = kw_name("watch"); }
#line 5259 "src/parser.tab.c"
    break;

  case 283: /* dot_field_name: WATCHERS  */
#line 1344 "src/parser.y"
                     { (yyval.text) = kw_name("watchers"); }
#line 5265 "src/parser.tab.c"
    break;

  case 284: /* dot_field_name: CONSIDER  */
#line 1345 "src/parser.y"
                     { (yyval.text) = kw_name("consider"); }
#line 5271 "src/parser.tab.c"
    break;

  case 285: /* dot_field_name: STEP  */
#line 1346 "src/parser.y"
                     { (yyval.text) = kw_name("step"); }
#line 5277 "src/parser.tab.c"
    break;

  case 286: /* dot_field_name: UNWATCH  */
#line 1347 "src/parser.y"
                     { (yyval.text) = kw_name("unwatch"); }
#line 5283 "src/parser.tab.c"
    break;

  case 287: /* dot_field_name: UNKNOWN_VALUE  */
#line 1348 "src/parser.y"
                     { (yyval.text) = kw_name("unknown"); }
#line 5289 "src/parser.tab.c"
    break;

  case 288: /* record_field_list: field_name OP_EQ expression  */
#line 1352 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5295 "src/parser.tab.c"
    break;

  case 289: /* record_field_list: field_name COLON expression  */
#line 1353 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5301 "src/parser.tab.c"
    break;

  case 290: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1354 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5307 "src/parser.tab.c"
    break;

  case 291: /* record_field_list: record_field_list COMMA optional_newlines field_name OP_EQ expression  */
#line 1355 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5313 "src/parser.tab.c"
    break;

  case 292: /* record_field_list: record_field_list COMMA optional_newlines field_name COLON expression  */
#line 1356 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5319 "src/parser.tab.c"
    break;

  case 293: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1357 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5325 "src/parser.tab.c"
    break;

  case 294: /* field_policy: IDENT  */
#line 1365 "src/parser.y"
            {
        FieldPolicySpec spec;
        spec.reset_expr = NULL;
        if (strcmp((yyvsp[0].text), "copy") == 0) {
            spec.policy = AST_FIELD_POLICY_COPY;
        } else if (strcmp((yyvsp[0].text), "link") == 0) {
            spec.policy = AST_FIELD_POLICY_LINK;
        } else if (strcmp((yyvsp[0].text), "exclude") == 0) {
            spec.policy = AST_FIELD_POLICY_EXCLUDE;
        } else if (strcmp((yyvsp[0].text), "reset") == 0) {
            free((yyvsp[0].text));
            (yyvsp[0].text) = NULL;
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "reset policy requires a value, e.g. (reset 0)");
            YYERROR;
        } else {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "unknown field policy (expected copy, link, reset, or exclude)");
            free((yyvsp[0].text));
            (yyvsp[0].text) = NULL;
            YYERROR;
        }
        free((yyvsp[0].text));
        (yyval.field_policy) = spec;
      }
#line 5357 "src/parser.tab.c"
    break;

  case 295: /* field_policy: IDENT expression  */
#line 1392 "src/parser.y"
                       {
        FieldPolicySpec spec;
        if (strcmp((yyvsp[-1].text), "reset") == 0) {
            spec.policy = AST_FIELD_POLICY_RESET;
            spec.reset_expr = (yyvsp[0].expr);
        } else {
            free((yyvsp[-1].text));
            (yyvsp[-1].text) = NULL;
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "only the reset policy takes a value");
            YYERROR;
        }
        free((yyvsp[-1].text));
        (yyval.field_policy) = spec;
      }
#line 5378 "src/parser.tab.c"
    break;


#line 5382 "src/parser.tab.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;
  *++yylsp = yyloc;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      {
        yypcontext_t yyctx
          = {yyssp, yytoken, &yylloc};
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = yysyntax_error (&yymsg_alloc, &yymsg, &yyctx);
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == -1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = YY_CAST (char *,
                             YYSTACK_ALLOC (YY_CAST (YYSIZE_T, yymsg_alloc)));
            if (yymsg)
              {
                yysyntax_error_status
                  = yysyntax_error (&yymsg_alloc, &yymsg, &yyctx);
                yymsgp = yymsg;
              }
            else
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = YYENOMEM;
              }
          }
        yyerror (&yylloc, ctx, yymsgp);
        if (yysyntax_error_status == YYENOMEM)
          YYNOMEM;
      }
    }

  yyerror_range[1] = yylloc;
  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval, &yylloc, ctx);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;

      yyerror_range[1] = *yylsp;
      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp, yylsp, ctx);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  yyerror_range[2] = yylloc;
  ++yylsp;
  YYLLOC_DEFAULT (*yylsp, yyerror_range, 2);

  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (&yylloc, ctx, YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval, &yylloc, ctx);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp, yylsp, ctx);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
  return yyresult;
}

#line 1415 "src/parser.y"


/* Reentrant parse core: all mutable parser state lives in a stack-allocated
 * gb_parse_ctx, so concurrent parses in one process share nothing. `path` labels
 * diagnostic locations (may be NULL) and `diags` is the sink (NULL => immediate
 * stderr via gb_report_to). This is the entry point gb_parse (frontend.c) uses. */
int parse_source_reentrant(const char *source, const char *path,
                           gb_diagnostics *diags, AstStmtList *out_program) {
    gb_parse_ctx ctx;
    ctx.active_lexer = NULL;
    ctx.lexer_error_reported = 0;
    ctx.active_parse_path = path;
    ctx.parsed_program = ast_stmt_list_empty();
    ctx.diags = diags;
    ctx.la_line = 0;
    ctx.la_column = 0;
    ctx.la_end_line = 0;
    ctx.la_end_column = 0;

    Lexer lexer;
    lexer_init(&lexer, source);
    ctx.active_lexer = &lexer;

    int result = yyparse(&ctx);
    if (result != 0) {
        return result;
    }
    /* A diagnostic reported from yylex must fail the parse even when bison
     * ACCEPTED. yylex signals such a token by returning 0 -- end of file -- and
     * bison cannot tell that from a real one, so wherever the grammar allows a
     * program to end (top level, notably) it reduces the truncated prefix and
     * reports success. The file then ran up to the bad token and exited 0.
     * lexer_error_reported is the only evidence that the EOF was synthetic. */
    if (ctx.lexer_error_reported) {
        return 1;
    }

    *out_program = ctx.parsed_program;
    return 0;
}

/* Legacy global-backed shims for the single-threaded CLI paths that still use
 * parse_set_source_path + parse_source: --add-loads (main.c), actor mode
 * (main.c), and eval.c's import loader. The sink comes from the process-global
 * active sink (main.c sets it around eval, so import parse errors are collected
 * and drained); the path from parse_set_source_path. gb_parse bypasses both. */
static const char *legacy_parse_path = NULL;

int parse_source(const char *source, AstStmtList *out_program) {
    return parse_source_reentrant(source, legacy_parse_path,
                                  gb_get_active_sink(), out_program);
}

void parse_set_source_path(const char *path) {
    legacy_parse_path = path;
}

/* Mirror of the former global yyerror location logic, sourced from the per-parse
 * ctx. Both Bison's syntax-error yyerror and the grammar's action-level error
 * reports funnel through here so their output stays byte-identical to the
 * pre-Phase-2 global reporter. */
static void report_syntax_error(gb_parse_ctx *ctx, int line, int column,
                                int end_line, int end_column, const char *message) {
    if (ctx->lexer_error_reported) {
        return;
    }
    if (line <= 0 && ctx->active_lexer) {
        line = ctx->active_lexer->line;
        column = ctx->active_lexer->column;
    }
    if (line <= 0) {
        line = 1;
    }
    if (column <= 0) {
        column = 1;
    }
    /* End of the offending token; fall back to the start if it looks unset or
     * inverted. */
    if (end_line < line || (end_line == line && end_column < column)) {
        end_line = line;
        end_column = column;
    }
    report_diag(ctx, GB_DIAG_PARSE_ERROR, line, column, end_line, end_column, message);
}

static int yylex(YYSTYPE *lvalp, YYLTYPE *llocp, gb_parse_ctx *ctx) {
    Token token = lexer_next(ctx->active_lexer);
    llocp->first_line = token.line;
    llocp->first_column = token.column;
    llocp->last_line = token.line;
    llocp->last_column = token.column + token.length;
    /* Record the lookahead location so action-level error reporting reproduces
     * exactly what the former global yyerror read from the global yylloc. */
    ctx->la_line = token.line;
    ctx->la_column = token.column;
    ctx->la_end_line = token.line;
    ctx->la_end_column = token.column + token.length;

    switch (token.type) {
    case TOKEN_EOF: return 0;
    case TOKEN_IDENT:
        lvalp->text = copy_text(token.start, token.length);
        return IDENT;
    case TOKEN_QUALIFIED_IDENT:
        lvalp->text = copy_text(token.start, token.length);
        return QUALIFIED_IDENT;
    case TOKEN_AS: return AS;
    case TOKEN_NUMBER:
    {
        /* Convert exactly the token's bytes (handles decimal and 0x hex), so a
         * following character can never extend what strtod reads. */
        char numbuf[64];
        size_t nlen = token.length < sizeof(numbuf) - 1 ? token.length : sizeof(numbuf) - 1;
        memcpy(numbuf, token.start, nlen);
        numbuf[nlen] = '\0';
        lvalp->number = strtod(numbuf, NULL);
        return NUMBER;
    }
    case TOKEN_STRING:
    {
        int ok = 0;
        lvalp->text = copy_string_literal(ctx, token.start, token.length, token.line, token.column, &ok);
        if (!ok) {
            ctx->lexer_error_reported = 1;
            return 0;
        }
        return STRING;
    }
    case TOKEN_LENS_CONTENT:
        lvalp->text = copy_text(token.start, token.length);
        return LENS_CONTENT;
    case TOKEN_IF: return IF;
    case TOKEN_CONSIDER_IF: return CONSIDER_IF;
    case TOKEN_THEN: return THEN;
    case TOKEN_ELSE: return ELSE;
    case TOKEN_CONSIDER_ELSE: return CONSIDER_ELSE;
    case TOKEN_END: return END;
    case TOKEN_END_CONSIDER: return END_CONSIDER;
    case TOKEN_PRINT: return PRINT;
    case TOKEN_TRUE: return TRUE;
    case TOKEN_FALSE: return FALSE;
    case TOKEN_NOTHING: return NOTHING;
    case TOKEN_UNKNOWN: return UNKNOWN_VALUE;
    case TOKEN_AND: return AND;
    case TOKEN_OR: return OR;
    case TOKEN_NOT: return NOT;
    case TOKEN_WITH: return WITH;
    case TOKEN_NEW: return NEW;
    case TOKEN_SPAWN: return SPAWN;
    case TOKEN_FOR: return FOR;
    case TOKEN_TO: return TO;
    case TOKEN_STEP: return STEP;
    case TOKEN_DO: return DO;
    case TOKEN_LOOP: return LOOP;
    case TOKEN_UNTIL: return UNTIL;
    case TOKEN_IN: return IN;
    case TOKEN_EACH: return EACH;
    case TOKEN_WHILE: return WHILE;
    case TOKEN_CONSIDER: return CONSIDER;
    case TOKEN_BREAK: return BREAK;
    case TOKEN_CONTINUE: return CONTINUE;
    case TOKEN_FUNCTION: return FUNCTION;
    case TOKEN_RETURN: return RETURN;
    case TOKEN_GOTO: return GOTO;
    case TOKEN_GOSUB: return GOSUB;
    case TOKEN_WATCH: return WATCH;
    case TOKEN_UNWATCH: return UNWATCH;
    case TOKEN_WITHOUT: return WITHOUT;
    case TOKEN_WATCHERS: return WATCHERS;
    case TOKEN_ON: return ON;
    case TOKEN_NEXT: return NEXT;
    case TOKEN_STOP: return STOP;
    case TOKEN_ERROR_VALUE: return ERROR_VALUE;
    case TOKEN_MODIFIER: return MODIFIER;
    case TOKEN_PROGRAM: return PROGRAM;
    case TOKEN_LIBRARY: return LIBRARY;
    case TOKEN_LOAD: return LOAD;
    case TOKEN_USE: return USE;
    case TOKEN_EXPORT: return EXPORT;
    case TOKEN_OP_EQ: return OP_EQ;
    case TOKEN_OP_NE: return OP_NE;
    case TOKEN_OP_GT: return OP_GT;
    case TOKEN_OP_LT: return OP_LT;
    case TOKEN_OP_GE: return OP_GE;
    case TOKEN_OP_LE: return OP_LE;
    case TOKEN_OP_NGT: return OP_NGT;
    case TOKEN_OP_NLT: return OP_NLT;
    case TOKEN_OP_NGE: return OP_NGE;
    case TOKEN_OP_NLE: return OP_NLE;
    case TOKEN_PLUS: return PLUS;
    case TOKEN_MINUS: return MINUS;
    case TOKEN_STAR: return STAR;
    case TOKEN_SLASH: return SLASH;
    case TOKEN_LPAREN:
        /* PLAT-BRACE: `(` means a call or grouping and NOTHING else. The
         * ninety-line lookahead that used to decide between a call and a
         * modifier clause is gone with the paren clause spelling, and with it
         * the residual it could not close (docs/brace_modifier_design.md). */
        return LPAREN;
    case TOKEN_RPAREN: return RPAREN;
    case TOKEN_LBRACKET: return LBRACKET;
    case TOKEN_RBRACKET: return RBRACKET;
    case TOKEN_COMMA: return COMMA;
    case TOKEN_LBRACE: return LBRACE;
    case TOKEN_RBRACE: return RBRACE;
    case TOKEN_DOT: return DOT;
    case TOKEN_COLON: return COLON;
    case TOKEN_NEWLINE: return NEWLINE;
    case TOKEN_ERROR:
        if (ctx->active_lexer->error_message[0]) {
            report_diag_lexeme(ctx, GB_DIAG_LEX_DETAIL, token.line, token.column,
                               token.start, token.length, ctx->active_lexer->error_message);
        } else {
            report_diag_lexeme(ctx, GB_DIAG_LEX_ERROR, token.line, token.column,
                               token.start, token.length, "unexpected token");
        }
        ctx->lexer_error_reported = 1;
        return 0;
    case TOKEN_DIM:
        /* `dim` is lexed as a keyword for ONE reason: to be refused here with
         * advice. There is no dim statement -- assignment creates a variable --
         * and someone arriving from QBasic types it in their first ten minutes.
         * Reserving the word to say so is worth more than freeing it, because
         * as an ordinary identifier `dim x` would still fail, just less
         * usefully. */
        report_diag_lexeme(ctx, GB_DIAG_PARSE_ERROR, token.line, token.column,
                           token.start, token.length,
                           "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        ctx->lexer_error_reported = 1;
        return 0;
    default:
        /* Backstop for a token added to the lexer and not to the grammar. It
         * used to fprintf straight to stderr: unlocated, absent from the
         * diagnostics sink, and so under --json-diagnostics a bare line in the
         * middle of a JSON stream. Every diagnostic goes through the sink. */
        report_diag_lexeme(ctx, GB_DIAG_PARSE_ERROR, token.line, token.column,
                           token.start, token.length,
                           "token has no place in the grammar");
        ctx->lexer_error_reported = 1;
        return 0;
    }
}

/* Bison's syntax-error entry point. In the pure parser llocp points at the
 * offending lookahead token's location (what the former global yyerror read from
 * the global yylloc); report_syntax_error applies the shared fallback logic. */
static void yyerror(YYLTYPE *llocp, gb_parse_ctx *ctx, const char *message) {
    report_syntax_error(ctx, llocp->first_line, llocp->first_column,
                        llocp->last_line, llocp->last_column, message);
}
