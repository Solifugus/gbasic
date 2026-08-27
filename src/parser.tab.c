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
static void report_syntax_error(gb_parse_ctx *ctx, int line, int column,
                                int end_line, int end_column, const char *message);

/* PLAT-NEXT: `next NAME` must name the loop it closes.
 *
 * Classic BASIC let `next x` close an inner `y` loop by implicitly closing
 * both, so a one-letter typo silently restructured the program. Refused here:
 * either name this loop, or write `next` with no name at all. Consumes the
 * closer's name either way. */
static int for_end_matches(gb_parse_ctx *ctx, const char *loop_variable,
                           char *closer, int line, int column) {
    int ok;
    if (!closer) {
        return 1;
    }
    ok = loop_variable && strcmp(loop_variable, closer) == 0;
    if (!ok) {
        char message[256];
        snprintf(message, sizeof(message),
                 "next %s does not close this loop: it iterates %s "
                 "(write `next %s`, `next`, or `end for`)",
                 closer, loop_variable ? loop_variable : "another variable",
                 loop_variable ? loop_variable : "");
        report_syntax_error(ctx, line, column, line, column, message);
    }
    free(closer);
    return ok;
}

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



#line 436 "src/parser.tab.c"

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
  YYSYMBOL_DIM = 9,                        /* DIM  */
  YYSYMBOL_PLUS_EQ = 10,                   /* PLUS_EQ  */
  YYSYMBOL_MINUS_EQ = 11,                  /* MINUS_EQ  */
  YYSYMBOL_STAR_EQ = 12,                   /* STAR_EQ  */
  YYSYMBOL_SLASH_EQ = 13,                  /* SLASH_EQ  */
  YYSYMBOL_IF = 14,                        /* IF  */
  YYSYMBOL_CONSIDER_IF = 15,               /* CONSIDER_IF  */
  YYSYMBOL_THEN = 16,                      /* THEN  */
  YYSYMBOL_ELSE = 17,                      /* ELSE  */
  YYSYMBOL_CONSIDER_ELSE = 18,             /* CONSIDER_ELSE  */
  YYSYMBOL_END = 19,                       /* END  */
  YYSYMBOL_END_CONSIDER = 20,              /* END_CONSIDER  */
  YYSYMBOL_PRINT = 21,                     /* PRINT  */
  YYSYMBOL_TRUE = 22,                      /* TRUE  */
  YYSYMBOL_FALSE = 23,                     /* FALSE  */
  YYSYMBOL_NOTHING = 24,                   /* NOTHING  */
  YYSYMBOL_UNKNOWN_VALUE = 25,             /* UNKNOWN_VALUE  */
  YYSYMBOL_AND = 26,                       /* AND  */
  YYSYMBOL_OR = 27,                        /* OR  */
  YYSYMBOL_NOT = 28,                       /* NOT  */
  YYSYMBOL_WITH = 29,                      /* WITH  */
  YYSYMBOL_NEW = 30,                       /* NEW  */
  YYSYMBOL_SPAWN = 31,                     /* SPAWN  */
  YYSYMBOL_FOR = 32,                       /* FOR  */
  YYSYMBOL_TO = 33,                        /* TO  */
  YYSYMBOL_STEP = 34,                      /* STEP  */
  YYSYMBOL_DO = 35,                        /* DO  */
  YYSYMBOL_LOOP = 36,                      /* LOOP  */
  YYSYMBOL_UNTIL = 37,                     /* UNTIL  */
  YYSYMBOL_IN = 38,                        /* IN  */
  YYSYMBOL_EACH = 39,                      /* EACH  */
  YYSYMBOL_WHILE = 40,                     /* WHILE  */
  YYSYMBOL_CONSIDER = 41,                  /* CONSIDER  */
  YYSYMBOL_BREAK = 42,                     /* BREAK  */
  YYSYMBOL_CONTINUE = 43,                  /* CONTINUE  */
  YYSYMBOL_FUNCTION = 44,                  /* FUNCTION  */
  YYSYMBOL_RETURN = 45,                    /* RETURN  */
  YYSYMBOL_GOTO = 46,                      /* GOTO  */
  YYSYMBOL_GOSUB = 47,                     /* GOSUB  */
  YYSYMBOL_WATCH = 48,                     /* WATCH  */
  YYSYMBOL_UNWATCH = 49,                   /* UNWATCH  */
  YYSYMBOL_WITHOUT = 50,                   /* WITHOUT  */
  YYSYMBOL_WATCHERS = 51,                  /* WATCHERS  */
  YYSYMBOL_ON = 52,                        /* ON  */
  YYSYMBOL_NEXT = 53,                      /* NEXT  */
  YYSYMBOL_STOP = 54,                      /* STOP  */
  YYSYMBOL_ERROR_VALUE = 55,               /* ERROR_VALUE  */
  YYSYMBOL_MODIFIER = 56,                  /* MODIFIER  */
  YYSYMBOL_PROGRAM = 57,                   /* PROGRAM  */
  YYSYMBOL_LIBRARY = 58,                   /* LIBRARY  */
  YYSYMBOL_LOAD = 59,                      /* LOAD  */
  YYSYMBOL_USE = 60,                       /* USE  */
  YYSYMBOL_EXPORT = 61,                    /* EXPORT  */
  YYSYMBOL_OP_EQ = 62,                     /* OP_EQ  */
  YYSYMBOL_OP_NE = 63,                     /* OP_NE  */
  YYSYMBOL_OP_GT = 64,                     /* OP_GT  */
  YYSYMBOL_OP_LT = 65,                     /* OP_LT  */
  YYSYMBOL_OP_GE = 66,                     /* OP_GE  */
  YYSYMBOL_OP_LE = 67,                     /* OP_LE  */
  YYSYMBOL_OP_NGT = 68,                    /* OP_NGT  */
  YYSYMBOL_OP_NLT = 69,                    /* OP_NLT  */
  YYSYMBOL_OP_NGE = 70,                    /* OP_NGE  */
  YYSYMBOL_OP_NLE = 71,                    /* OP_NLE  */
  YYSYMBOL_PLUS = 72,                      /* PLUS  */
  YYSYMBOL_MINUS = 73,                     /* MINUS  */
  YYSYMBOL_STAR = 74,                      /* STAR  */
  YYSYMBOL_SLASH = 75,                     /* SLASH  */
  YYSYMBOL_LPAREN = 76,                    /* LPAREN  */
  YYSYMBOL_RPAREN = 77,                    /* RPAREN  */
  YYSYMBOL_LBRACKET = 78,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 79,                  /* RBRACKET  */
  YYSYMBOL_LBRACE = 80,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 81,                    /* RBRACE  */
  YYSYMBOL_COMMA = 82,                     /* COMMA  */
  YYSYMBOL_COLON = 83,                     /* COLON  */
  YYSYMBOL_NEWLINE = 84,                   /* NEWLINE  */
  YYSYMBOL_IF_WITHOUT_ELSE = 85,           /* IF_WITHOUT_ELSE  */
  YYSYMBOL_NO_DOT = 86,                    /* NO_DOT  */
  YYSYMBOL_DOT = 87,                       /* DOT  */
  YYSYMBOL_YYACCEPT = 88,                  /* $accept  */
  YYSYMBOL_program = 89,                   /* program  */
  YYSYMBOL_statement_list = 90,            /* statement_list  */
  YYSYMBOL_statement = 91,                 /* statement  */
  YYSYMBOL_assignment = 92,                /* assignment  */
  YYSYMBOL_compound_op = 93,               /* compound_op  */
  YYSYMBOL_lvalue = 94,                    /* lvalue  */
  YYSYMBOL_variable_name = 95,             /* variable_name  */
  YYSYMBOL_comparison_lens = 96,           /* comparison_lens  */
  YYSYMBOL_97_1 = 97,                      /* $@1  */
  YYSYMBOL_modifier_name = 98,             /* modifier_name  */
  YYSYMBOL_modifier_word = 99,             /* modifier_word  */
  YYSYMBOL_print_statement = 100,          /* print_statement  */
  YYSYMBOL_call_statement = 101,           /* call_statement  */
  YYSYMBOL_with_lock_statement = 102,      /* with_lock_statement  */
  YYSYMBOL_for_end = 103,                  /* for_end  */
  YYSYMBOL_for_each_statement = 104,       /* for_each_statement  */
  YYSYMBOL_do_loop_statement = 105,        /* do_loop_statement  */
  YYSYMBOL_while_statement = 106,          /* while_statement  */
  YYSYMBOL_consider_statement = 107,       /* consider_statement  */
  YYSYMBOL_consider_branch_list = 108,     /* consider_branch_list  */
  YYSYMBOL_consider_else_opt = 109,        /* consider_else_opt  */
  YYSYMBOL_consider_statement_list = 110,  /* consider_statement_list  */
  YYSYMBOL_consider_body_statement = 111,  /* consider_body_statement  */
  YYSYMBOL_function_statement = 112,       /* function_statement  */
  YYSYMBOL_modifier_statement = 113,       /* modifier_statement  */
  YYSYMBOL_program_statement = 114,        /* program_statement  */
  YYSYMBOL_library_statement = 115,        /* library_statement  */
  YYSYMBOL_use_statement = 116,            /* use_statement  */
  YYSYMBOL_modifier_signature = 117,       /* modifier_signature  */
  YYSYMBOL_modifier_context = 118,         /* modifier_context  */
  YYSYMBOL_watch_statement = 119,          /* watch_statement  */
  YYSYMBOL_unwatch_statement = 120,        /* unwatch_statement  */
  YYSYMBOL_watch_target_list = 121,        /* watch_target_list  */
  YYSYMBOL_server_statement = 122,         /* server_statement  */
  YYSYMBOL_server_item_list = 123,         /* server_item_list  */
  YYSYMBOL_server_item = 124,              /* server_item  */
  YYSYMBOL_server_string_list = 125,       /* server_string_list  */
  YYSYMBOL_watch_target_path = 126,        /* watch_target_path  */
  YYSYMBOL_without_watchers_statement = 127, /* without_watchers_statement  */
  YYSYMBOL_on_error_statement = 128,       /* on_error_statement  */
  YYSYMBOL_error_statement = 129,          /* error_statement  */
  YYSYMBOL_return_statement = 130,         /* return_statement  */
  YYSYMBOL_label_statement = 131,          /* label_statement  */
  YYSYMBOL_goto_statement = 132,           /* goto_statement  */
  YYSYMBOL_gosub_statement = 133,          /* gosub_statement  */
  YYSYMBOL_break_statement = 134,          /* break_statement  */
  YYSYMBOL_continue_statement = 135,       /* continue_statement  */
  YYSYMBOL_if_statement = 136,             /* if_statement  */
  YYSYMBOL_if_block_tail = 137,            /* if_block_tail  */
  YYSYMBOL_if_inline_tail = 138,           /* if_inline_tail  */
  YYSYMBOL_inline_statement = 139,         /* inline_statement  */
  YYSYMBOL_expression = 140,               /* expression  */
  YYSYMBOL_or_expression = 141,            /* or_expression  */
  YYSYMBOL_and_expression = 142,           /* and_expression  */
  YYSYMBOL_comparison_expression = 143,    /* comparison_expression  */
  YYSYMBOL_additive_expression = 144,      /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 145, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 146,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 147,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 148,      /* comparison_operator  */
  YYSYMBOL_primary = 149,                  /* primary  */
  YYSYMBOL_record_literal = 150,           /* record_literal  */
  YYSYMBOL_ident_suffix = 151,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 152,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 153,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 154,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 155,            /* argument_list  */
  YYSYMBOL_array_argument_list = 156,      /* array_argument_list  */
  YYSYMBOL_parameter_list_opt = 157,       /* parameter_list_opt  */
  YYSYMBOL_parameter_list = 158,           /* parameter_list  */
  YYSYMBOL_field_name = 159,               /* field_name  */
  YYSYMBOL_dot_field_name = 160,           /* dot_field_name  */
  YYSYMBOL_record_field_list = 161,        /* record_field_list  */
  YYSYMBOL_field_policy = 162,             /* field_policy  */
  YYSYMBOL_optional_newlines = 163         /* optional_newlines  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;



/* Unqualified %code blocks.  */
#line 435 "src/parser.y"

static int yylex(YYSTYPE *lvalp, YYLTYPE *llocp, gb_parse_ctx *ctx);
static void yyerror(YYLTYPE *llocp, gb_parse_ctx *ctx, const char *message);
static void report_syntax_error(gb_parse_ctx *ctx, int line, int column,
                                int end_line, int end_column, const char *message);

#line 641 "src/parser.tab.c"

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
#define YYLAST   2609

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  88
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  76
/* YYNRULES -- Number of rules.  */
#define YYNRULES  311
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  656

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   342


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
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   500,   500,   504,   505,   506,   510,   511,   512,   513,
     514,   515,   516,   517,   518,   519,   520,   521,   522,   523,
     524,   525,   526,   527,   528,   529,   530,   531,   532,   533,
     534,   535,   541,   552,   557,   558,   570,   582,   583,   584,
     585,   589,   590,   591,   595,   596,   597,   602,   603,   607,
     607,   613,   614,   618,   619,   620,   621,   625,   631,   635,
     636,   642,   647,   656,   669,   692,   693,   694,   698,   702,
     709,   713,   722,   725,   731,   737,   743,   746,   752,   753,
     757,   758,   759,   763,   764,   765,   766,   767,   768,   769,
     770,   771,   772,   773,   774,   775,   776,   777,   778,   779,
     780,   781,   782,   783,   784,   785,   786,   787,   793,   804,
     807,   814,   817,   823,   829,   835,   836,   837,   838,   839,
     855,   874,   875,   879,   883,   886,   894,   900,   904,   905,
     924,   927,   933,   934,   935,   939,   942,   945,   948,   951,
     957,   958,   962,   963,   967,   973,   974,   975,   976,   981,
     994,   999,  1004,  1014,  1018,  1019,  1023,  1030,  1034,  1043,
    1044,  1048,  1049,  1053,  1057,  1064,  1067,  1070,  1076,  1079,
    1082,  1088,  1089,  1090,  1091,  1092,  1093,  1094,  1095,  1096,
    1097,  1098,  1102,  1106,  1107,  1111,  1112,  1116,  1117,  1118,
    1124,  1125,  1126,  1130,  1131,  1132,  1136,  1137,  1138,  1139,
    1140,  1141,  1145,  1146,  1147,  1148,  1153,  1167,  1168,  1169,
    1170,  1171,  1172,  1173,  1174,  1175,  1176,  1180,  1181,  1182,
    1183,  1184,  1201,  1207,  1208,  1209,  1210,  1211,  1212,  1213,
    1214,  1215,  1219,  1220,  1224,  1229,  1234,  1240,  1252,  1257,
    1265,  1269,  1275,  1276,  1280,  1281,  1285,  1286,  1290,  1291,
    1295,  1296,  1309,  1316,  1325,  1326,  1327,  1328,  1329,  1330,
    1331,  1332,  1333,  1334,  1335,  1336,  1337,  1338,  1339,  1340,
    1341,  1342,  1343,  1344,  1345,  1346,  1347,  1348,  1349,  1350,
    1351,  1352,  1353,  1354,  1355,  1356,  1357,  1358,  1359,  1360,
    1361,  1362,  1363,  1364,  1365,  1366,  1367,  1368,  1369,  1370,
    1371,  1372,  1376,  1377,  1378,  1379,  1380,  1381,  1389,  1416,
    1435,  1436
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
  "STRING", "LENS_CONTENT", "QUALIFIED_IDENT", "AS", "DIM", "PLUS_EQ",
  "MINUS_EQ", "STAR_EQ", "SLASH_EQ", "IF", "CONSIDER_IF", "THEN", "ELSE",
  "CONSIDER_ELSE", "END", "END_CONSIDER", "PRINT", "TRUE", "FALSE",
  "NOTHING", "UNKNOWN_VALUE", "AND", "OR", "NOT", "WITH", "NEW", "SPAWN",
  "FOR", "TO", "STEP", "DO", "LOOP", "UNTIL", "IN", "EACH", "WHILE",
  "CONSIDER", "BREAK", "CONTINUE", "FUNCTION", "RETURN", "GOTO", "GOSUB",
  "WATCH", "UNWATCH", "WITHOUT", "WATCHERS", "ON", "NEXT", "STOP",
  "ERROR_VALUE", "MODIFIER", "PROGRAM", "LIBRARY", "LOAD", "USE", "EXPORT",
  "OP_EQ", "OP_NE", "OP_GT", "OP_LT", "OP_GE", "OP_LE", "OP_NGT", "OP_NLT",
  "OP_NGE", "OP_NLE", "PLUS", "MINUS", "STAR", "SLASH", "LPAREN", "RPAREN",
  "LBRACKET", "RBRACKET", "LBRACE", "RBRACE", "COMMA", "COLON", "NEWLINE",
  "IF_WITHOUT_ELSE", "NO_DOT", "DOT", "$accept", "program",
  "statement_list", "statement", "assignment", "compound_op", "lvalue",
  "variable_name", "comparison_lens", "$@1", "modifier_name",
  "modifier_word", "print_statement", "call_statement",
  "with_lock_statement", "for_end", "for_each_statement",
  "do_loop_statement", "while_statement", "consider_statement",
  "consider_branch_list", "consider_else_opt", "consider_statement_list",
  "consider_body_statement", "function_statement", "modifier_statement",
  "program_statement", "library_statement", "use_statement",
  "modifier_signature", "modifier_context", "watch_statement",
  "unwatch_statement", "watch_target_list", "server_statement",
  "server_item_list", "server_item", "server_string_list",
  "watch_target_path", "without_watchers_statement", "on_error_statement",
  "error_statement", "return_statement", "label_statement",
  "goto_statement", "gosub_statement", "break_statement",
  "continue_statement", "if_statement", "if_block_tail", "if_inline_tail",
  "inline_statement", "expression", "or_expression", "and_expression",
  "comparison_expression", "additive_expression",
  "multiplicative_expression", "unary_expression", "postfix_expression",
  "comparison_operator", "primary", "record_literal", "ident_suffix",
  "ident_dot_suffix", "duration_terms", "argument_list_opt",
  "argument_list", "array_argument_list", "parameter_list_opt",
  "parameter_list", "field_name", "dot_field_name", "record_field_list",
  "field_policy", "optional_newlines", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-495)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -495,    34,   910,  -495,     7,   -60,  -495,  2227,  -495,  2192,
      43,    71,   -24,  -495,  -495,  2227,  2227,    72,    98,   171,
    2227,   203,   203,   116,  2227,    82,    42,  -495,   546,   162,
     150,   170,   230,   232,   149,  -495,  -495,   128,    83,   133,
     144,   148,  -495,  -495,  -495,  -495,  -495,  -495,  -495,  -495,
    -495,   154,  -495,   169,  -495,  -495,   178,   181,   193,   196,
     197,   198,   199,   201,  -495,   210,  2227,  2227,   268,  -495,
    -495,   222,  -495,  -495,  -495,  -495,  2227,  2237,   295,   224,
    -495,  2227,  2227,  -495,  -495,    96,   214,   278,   280,  -495,
     179,   180,  -495,    93,  -495,  -495,   304,   255,  -495,   236,
     126,   311,  -495,   235,   237,  -495,  -495,   240,   247,  -495,
    -495,  -495,   248,   203,  -495,    19,   221,  -495,   241,   152,
     122,   322,  -495,  -495,  -495,  -495,  -495,   132,  -495,   297,
     254,   249,   327,  -495,   332,  -495,   162,  -495,  -495,  -495,
    -495,  -495,  2227,  2227,  -495,  2432,  2227,    55,  -495,  -495,
    -495,  -495,  -495,  -495,  -495,  -495,  -495,  -495,  -495,  -495,
    -495,  2314,  -495,   261,   259,   270,  -495,  2227,  -495,    13,
     273,   274,  -495,   275,   584,   728,  2227,  2490,  -495,  2070,
    2227,  2227,  -495,  -495,  -495,  -495,  -495,  -495,  -495,  -495,
    -495,  -495,  2227,  2227,   226,  2227,  2227,  2227,  2227,  2548,
     346,  2227,  2227,  2227,  2227,   315,   968,  -495,   340,   353,
     353,   203,   -20,   203,  -495,   354,  -495,  -495,  -495,    59,
    -495,    85,  -495,   283,   353,  -495,   366,   353,  -495,   367,
     371,   339,  -495,   300,   377,   309,   312,  -495,  -495,  -495,
    -495,  -495,  -495,  -495,  -495,  -495,  -495,  -495,  -495,  -495,
    -495,  -495,  -495,  -495,  -495,  -495,  -495,  -495,  -495,  -495,
    -495,  -495,  -495,  -495,  -495,  -495,  -495,  -495,  -495,  -495,
    -495,  -495,  -495,  -495,  -495,  -495,  -495,  -495,  -495,  -495,
    -495,  -495,  -495,  -495,  -495,  -495,  2227,  2227,   313,  -495,
     303,    46,  -495,   107,  -495,  2227,  -495,   314,   310,  2227,
    -495,  -495,  -495,  -495,  -495,   316,  -495,   318,   317,  -495,
     319,   325,   328,  -495,  -495,  -495,  -495,  -495,  -495,  -495,
    -495,  -495,  -495,  -495,  -495,  -495,   308,   280,  -495,   180,
     180,  2227,   188,  -495,  -495,   324,   333,   334,  -495,  -495,
    -495,   335,   329,   378,  2227,   156,  1026,  2227,   185,  -495,
     341,   338,   349,   109,   343,   221,  1084,  -495,  1142,  -495,
    -495,  -495,  -495,  2227,   355,  -495,   347,   356,  1200,  -495,
    -495,   366,  -495,   357,  2227,  2227,  -495,  -495,   426,  -495,
    2227,  2227,   350,  -495,  -495,  -495,  -495,   358,  -495,   115,
     140,  -495,  2227,  2227,  -495,   852,   419,   188,  -495,  2227,
    2227,   359,  -495,  2227,   362,  2227,  2227,   408,   433,  2227,
     369,   430,   372,   447,   373,   374,  -495,   407,   409,   383,
    -495,  -495,   379,   404,   380,  -495,   389,   390,  2227,   391,
      66,  -495,  -495,  -495,   794,  -495,   651,  -495,  -495,   392,
     394,  2090,   458,  -495,  2134,  -495,   396,   397,  -495,  1258,
      25,  -495,   393,   395,   398,   399,   459,  -495,   400,  -495,
    -495,  -495,  -495,  1316,   401,   402,  -495,  1374,  -495,   403,
    -495,  -495,  -495,  -495,   405,   253,   472,   474,  -495,  -495,
      73,   413,    84,  -495,  -495,  -495,  -495,   406,   410,  -495,
     415,  -495,  -495,  1432,   448,    37,  -495,  2227,  -495,  1258,
    -495,  -495,  -495,  -495,   416,  1490,  -495,  1548,  1606,  1664,
     444,  -495,  -495,   425,  1722,  -495,  1780,  2227,   428,   434,
     143,   417,   418,   507,   426,  2227,  2227,  1838,  -495,  -495,
    1896,  -495,   486,   432,  -495,   435,   438,  1258,  -495,  1490,
    -495,  -495,  -495,   439,   440,   442,  -495,  -495,  -495,  -495,
    -495,  -495,  -495,  -495,  -495,  -495,   443,  -495,   445,  -495,
     446,   450,   452,   456,   460,   463,   464,   468,  -495,   473,
     487,   470,   471,   475,   497,   476,  -495,  2373,   353,   538,
    -495,  -495,  -495,   477,   479,  -495,  -495,   544,   548,   480,
    -495,  -495,  -495,  -495,  1490,  -495,  -495,  -495,  -495,  -495,
    -495,  -495,  -495,  -495,  -495,  -495,  -495,  -495,   482,   483,
     488,  -495,  -495,   489,   491,   494,   120,   502,  -495,  1954,
    -495,   498,   496,   500,  -495,  1258,  -495,  -495,  -495,  -495,
    -495,  -495,   501,   506,   508,  2227,  -495,  -495,  -495,    95,
    -495,  -495,   509,  -495,   559,   103,  2012,  -495,   510,   582,
     591,  -495,   512,   514,  -495,  -495
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       3,     0,     2,     1,    44,     0,    32,     0,    45,     0,
       0,     0,     0,    47,    48,     0,     0,   159,   161,     0,
     154,     0,     0,     0,     0,     0,     0,    46,     0,     0,
       0,     0,     0,     0,     0,     4,     5,     0,     0,    41,
       0,     0,     9,    10,    12,    11,    13,    14,    15,    16,
      17,     0,    19,     0,    20,    22,     0,     0,     0,     0,
       0,     0,     0,     0,    31,     0,   242,   242,   217,    44,
     220,     0,   224,   225,   226,   227,     0,     0,     0,     0,
     223,     0,     0,   310,   310,   234,     0,   182,   183,   185,
     187,   190,   193,   196,   202,   231,   219,     0,    57,     0,
       0,     0,     3,     0,     0,   160,   162,     0,     0,   155,
     157,   158,    44,     0,   142,     0,   128,   127,     0,     0,
       0,     0,   153,    53,    55,    54,    56,   121,    51,     0,
       0,     0,   116,   118,   115,   117,     0,     6,    37,    38,
      39,    40,     0,     0,    49,     0,     0,     0,   156,     7,
       8,    18,    21,    23,    24,    25,    26,    27,    28,    29,
      30,     0,   244,     0,   243,     0,   240,   242,   197,   199,
       0,     0,   198,     0,     0,     0,   242,     0,   221,     0,
       0,     0,   207,   208,   209,   210,   211,   212,   213,   214,
     215,   216,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     3,     0,   248,
     248,     0,     0,     0,     3,     0,     3,   152,   151,     0,
     150,     0,   147,     0,   248,    52,     0,   248,     3,     0,
       0,     0,    33,     0,     0,   254,     0,   255,   301,   270,
     267,   268,   259,   275,   282,   283,   284,   300,   280,   281,
     279,   265,   263,   289,   269,   260,   298,   272,   273,   274,
     261,   264,   271,   297,   285,   286,   292,   276,   287,   288,
     295,   299,   266,   296,   262,   256,   257,   258,   293,   294,
     291,   277,   278,   290,    43,    34,     0,     0,   254,   253,
       0,     0,   252,     0,    59,     0,    60,     0,     0,   242,
     218,   228,   229,   311,   246,   310,   232,   310,     0,   254,
       0,   238,    44,     3,   171,    41,   172,   173,   174,   175,
     176,   177,   178,   179,   180,   181,     0,   184,   186,   191,
     192,     0,   188,   194,   195,     0,   254,     0,   204,   241,
      58,     0,     0,     0,     0,    47,     0,     0,    78,   250,
       0,   249,     0,     0,     0,   129,     0,   143,     0,   149,
     148,   145,   146,   242,     0,   123,     0,     0,     0,   120,
     119,     0,    42,     0,   242,   242,    36,    35,     0,   132,
       0,     0,     0,   310,   245,   222,   200,     0,   310,     0,
       0,   235,   242,   242,   236,     0,   168,   189,   203,   242,
     242,     0,     3,     0,     0,     0,     0,    45,     0,     0,
       0,     0,     0,     0,     0,     0,     3,    45,    45,     0,
     122,     3,     0,    45,     0,    50,     0,     0,   308,     0,
       0,   302,   303,   132,     0,   201,     0,   230,   233,     0,
       0,     0,    45,   163,     0,   164,     0,     0,     3,     0,
       0,     3,     0,     0,     0,     0,     0,    80,     0,     3,
     251,     3,     3,     0,     0,     0,    63,     0,     3,     0,
       3,    61,    62,   309,     0,     0,     0,     0,   133,   134,
       0,   254,     0,   247,   237,   239,     3,     0,     0,     3,
       0,   205,   206,     0,    45,    46,    68,     0,     3,     0,
      72,    73,    74,    80,     0,    79,    75,     0,     0,     0,
      45,   125,   144,    45,     0,   114,     0,     0,     0,   140,
       0,     0,     0,     0,     0,     0,     0,     0,   166,   165,
       0,   169,    45,     0,    66,     0,     0,     0,    69,    76,
      80,   108,    81,     0,     0,     0,    86,    87,    89,    88,
      90,    82,    91,    92,    93,    94,     0,    96,     0,    98,
       0,     0,     0,     0,     0,     0,     0,     0,   107,    45,
      45,    45,     0,     0,    45,    45,   304,     0,   248,     0,
     135,   131,     3,     0,     0,   305,   306,    45,    45,     0,
      65,    67,     3,    70,    77,    83,    84,    85,    95,    97,
      99,   100,   101,   102,   103,   104,   105,   106,     0,     0,
       0,   124,   111,     0,     0,     0,     0,     0,   141,     0,
     130,     0,     0,     0,    64,     0,   109,   110,   126,   113,
     112,   132,     0,     0,    45,     0,   167,   170,    71,     0,
     132,     3,     0,   307,     0,     0,     0,   139,     0,     0,
      45,   138,     0,     0,   137,   136
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -495,  -495,   -84,  -495,  -178,   453,  -495,    -2,   515,  -495,
    -495,   484,  -177,  -171,  -490,  -494,  -488,  -481,  -478,  -475,
    -495,  -495,  -425,  -495,  -470,  -468,  -467,  -460,  -170,   466,
     233,  -457,  -455,  -106,  -495,  -430,  -495,  -495,   412,  -453,
    -166,  -165,  -157,  -452,  -140,  -124,   -99,   -98,  -451,  -495,
    -495,  -218,    16,  -495,   436,   429,  -191,    76,   -48,   536,
     424,  -495,   330,  -495,  -495,  -495,   146,  -495,  -495,  -198,
    -495,   195,   -17,  -169,   106,   -74
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    36,    37,   146,    38,    85,   147,   234,
     127,   128,    40,    41,    42,   496,    43,    44,    45,    46,
     348,   411,   505,   551,    47,    48,    49,    50,    51,   129,
     366,    52,    53,   115,    54,   430,   479,   520,   116,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,   443,
     445,   326,   162,    87,    88,    89,    90,    91,    92,    93,
     195,    94,    95,   178,   394,    96,   163,   164,   305,   350,
     351,   291,   292,   293,   429,   174
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      39,   314,   316,   480,   332,   538,   307,   212,   317,   318,
     175,    65,   352,   319,   320,   546,    67,   547,   206,   110,
     111,   114,   321,    86,   548,    98,   364,   549,   168,   367,
     550,   103,   104,   172,     3,   552,   109,   553,   554,   322,
     117,    69,   298,   593,   122,   555,   119,    99,   557,   546,
     558,   547,   559,   563,   568,   323,     8,   354,   548,   497,
     102,   549,   213,   359,   550,   138,   139,   140,   141,   552,
     475,   553,   554,    13,    14,   100,   105,   475,   539,   555,
     324,   325,   557,    66,   558,   476,   559,   563,   568,   361,
      27,   198,   523,   138,   139,   140,   141,   120,   173,   475,
     199,   213,   106,   214,   546,   353,   547,   475,   380,   498,
     101,   114,   360,   548,   644,   594,   549,   286,   477,   550,
     112,   534,   649,   346,   552,   477,   553,   554,   284,   381,
     356,   638,   358,   118,   555,     8,   123,   557,   362,   558,
     397,   559,   563,   568,   368,   142,   525,   477,   333,   334,
     478,   124,    13,    14,   130,   477,   217,   478,   232,   233,
     311,   143,   285,   144,   203,   125,   123,   526,   221,    27,
     145,   198,   176,   218,   131,   107,   222,   315,   108,   478,
     199,   124,   338,   177,   382,   126,   415,   478,   204,   383,
     304,   213,   113,   405,   437,   125,   406,   632,   219,   303,
     409,   639,   383,   410,    39,   136,   220,    69,   224,   114,
     645,   114,   137,   165,   335,   126,   148,   340,   341,   342,
     343,   438,     8,   487,   303,   579,   490,   580,   149,   395,
     179,   389,   150,   390,   132,   133,   134,   135,   151,    13,
      14,   182,   183,   184,   185,   186,   187,   188,   189,   190,
     191,   192,   193,   152,   196,   197,    27,   518,   519,   144,
     192,   193,   153,   314,   316,   154,   314,   316,   329,   330,
     317,   318,   166,   317,   318,   319,   320,   155,   319,   320,
     156,   157,   158,   159,   321,   160,   161,   321,   182,   183,
     184,   185,   186,   187,   188,   189,   190,   191,   167,   170,
     171,   322,   376,   377,   322,   180,   181,   200,   215,   434,
     201,   384,   202,   297,   436,   205,   209,   323,   449,   207,
     323,   208,   308,   210,   211,   216,   223,   543,   544,   226,
     227,   229,   463,   228,   545,   556,   230,   467,   294,   560,
     561,   295,   324,   325,    39,   324,   325,   296,   562,   299,
     339,   300,   301,   344,    39,   347,    39,   349,   357,   363,
     404,   543,   544,   408,   493,   564,    39,   499,   545,   556,
     365,   371,   369,   560,   561,   507,   370,   508,   509,   372,
     617,   565,   562,   373,   514,   374,   516,   379,   375,   378,
      84,   385,   396,    39,   391,   392,   431,   432,   388,   564,
     383,   393,   527,   398,    66,   530,   566,   567,   616,   399,
     400,   403,   401,   402,   537,   565,   543,   544,   412,   450,
     413,   452,   453,   545,   556,   456,   414,   416,   560,   561,
     428,   421,   420,   422,   433,   435,   444,   562,   425,   315,
     566,   567,   315,   448,   473,   387,   451,    39,   454,   455,
     458,   460,   483,   457,   564,   464,   459,   461,   462,   465,
     466,    39,   469,   468,   470,    39,   471,   472,   474,   484,
     565,   485,   488,   491,   492,   504,   521,   500,   522,   501,
     533,   573,   502,   503,   506,   511,   512,   515,   517,   524,
     528,    39,   572,   535,   529,   566,   567,    39,   619,   531,
     540,   581,   582,    39,   577,    39,    39,    39,   625,   419,
     578,   583,    39,   536,    39,   589,   590,   608,   610,   591,
     426,   427,   592,   595,   596,    39,   597,   598,    39,   599,
     600,   609,   614,   576,   601,    39,   602,    39,   439,   440,
     603,   585,   586,   618,   604,   446,   447,   605,   606,    68,
      69,    70,   607,    71,   613,   611,   621,   646,   622,   612,
     642,   620,   623,   648,   624,     8,   626,   627,    72,    73,
      74,    75,   628,   629,    76,   630,    77,    78,   631,   633,
     636,   635,    13,    14,   637,   640,   652,    68,    69,    70,
     641,    71,    39,   647,   651,   653,   654,    79,   655,    27,
     287,    80,   231,     8,   424,   194,    72,    73,    74,    75,
     328,   225,    76,   169,    77,    78,   327,    39,   331,    81,
      13,    14,    82,    39,    83,   355,    84,     0,   386,   482,
     584,     0,     0,   121,     0,    79,     0,    27,     0,    80,
       0,     0,     0,     0,    39,     0,     0,     0,     0,     0,
       0,   643,     0,     0,    68,    69,    70,    81,    71,     0,
      82,     0,    83,   302,    84,     0,     0,     0,   303,     0,
       8,     0,     0,    72,    73,    74,    75,     0,     0,    76,
       0,    77,    78,     0,     0,     0,     0,    13,    14,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    79,     0,    27,     0,    80,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    81,     0,     0,    82,     0,    83,
       0,    84,   288,   289,     0,   303,   237,   238,     0,     0,
       0,     0,   239,     0,   240,   241,     0,   242,     0,   243,
     244,   245,   246,   247,   248,   249,   250,   251,   252,   253,
     254,   255,   256,   257,   258,   259,   260,   261,   262,   263,
     264,   265,   266,   267,   268,   269,   270,   271,   272,   273,
     274,   275,   276,   277,   278,   279,   280,   281,   282,   283,
       0,     0,     0,     0,     0,     0,     0,     0,   481,   289,
       0,     0,   237,   238,     0,     0,     0,     0,   239,   306,
     240,   241,   303,   242,     0,   243,   244,   245,   246,   247,
     248,   249,   250,   251,   252,   253,   254,   255,   256,   257,
     258,   259,   260,   261,   262,   263,   264,   265,   266,   267,
     268,   269,   270,   271,   272,   273,   274,   275,   276,   277,
     278,   279,   280,   281,   282,   283,     4,     0,     0,     5,
       0,     6,     0,     0,     0,     0,     7,     0,     0,   441,
       0,   442,     0,     9,     0,     0,     0,     0,   303,     0,
       0,    10,     0,     0,    11,     0,     0,    12,    13,    14,
       0,     0,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,     0,    26,    27,     0,    28,    29,    30,
      31,    32,    33,    34,     4,     0,     0,     5,     0,     6,
       0,     0,     0,     0,     7,     0,     0,     0,     0,     8,
       0,     9,     0,     0,     0,     0,    35,     0,     0,    10,
       0,     0,    11,     0,     0,    12,    13,    14,     0,     0,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,     0,    26,    27,     0,    28,    29,    30,    31,    32,
      33,    34,     4,     0,     0,     5,     0,     6,     0,     0,
       0,     0,     7,     0,     0,     0,     0,     8,     0,     9,
       0,     0,     0,     0,    35,     0,     0,    10,     0,     0,
      11,     0,     0,    12,   345,    14,     0,     0,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,    25,     0,
      26,    27,     0,    28,    29,    30,    31,    32,    33,    34,
       4,     0,     0,     5,     0,     6,     0,     0,     0,     0,
       7,     0,     0,     0,     0,   407,     0,     9,     0,     0,
       0,     0,    35,     0,     0,    10,     0,     0,    11,     0,
       0,    12,    13,    14,     0,     0,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,    25,     0,    26,    27,
       0,    28,    29,    30,    31,    32,    33,    34,     4,     0,
       0,     5,     0,     6,     0,     0,     0,     0,     7,     0,
       0,     0,     0,   417,     0,     9,     0,     0,     0,     0,
      35,     0,     0,    10,     0,     0,    11,     0,     0,    12,
      13,    14,     0,     0,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,     0,    26,    27,     0,    28,
      29,    30,    31,    32,    33,    34,     4,     0,     0,     5,
       0,     6,     0,     0,     0,     0,     7,     0,     0,     0,
       0,   418,     0,     9,     0,     0,     0,     0,    35,     0,
       0,    10,     0,     0,    11,     0,     0,    12,    13,    14,
       0,     0,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,     0,    26,    27,     0,    28,    29,    30,
      31,    32,    33,    34,     4,     0,     0,     5,     0,     6,
       0,     0,     0,     0,     7,     0,     0,     0,     0,   423,
       0,     9,     0,     0,     0,     0,    35,     0,     0,    10,
       0,     0,    11,     0,     0,    12,    13,    14,     0,     0,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,     0,    26,    27,     0,    28,    29,    30,    31,    32,
      33,    34,     4,     0,     0,     5,     0,     6,     0,     0,
       0,     0,     7,     0,     0,     0,     0,   494,     0,     9,
       0,     0,     0,     0,    35,     0,     0,    10,     0,     0,
      11,     0,     0,    12,    13,    14,     0,     0,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,    25,     0,
      26,   495,     0,    28,    29,    30,    31,    32,    33,    34,
       4,     0,     0,     5,     0,     6,     0,     0,     0,     0,
       7,     0,     0,     0,     0,   510,     0,     9,     0,     0,
       0,     0,    35,     0,     0,    10,     0,     0,    11,     0,
       0,    12,    13,    14,     0,     0,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,    25,     0,    26,    27,
       0,    28,    29,    30,    31,    32,    33,    34,     4,     0,
       0,     5,     0,     6,     0,     0,     0,     0,     7,     0,
       0,     0,     0,   513,     0,     9,     0,     0,     0,     0,
      35,     0,     0,    10,     0,     0,    11,     0,     0,    12,
      13,    14,     0,     0,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,     0,    26,    27,     0,    28,
      29,    30,    31,    32,    33,    34,     4,     0,     0,     5,
       0,     6,     0,     0,     0,     0,     7,     0,     0,     0,
       0,   532,     0,     9,     0,     0,     0,     0,    35,     0,
       0,    10,     0,     0,    11,     0,     0,    12,    13,    14,
       0,     0,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,     0,    26,    27,     0,    28,    29,    30,
      31,    32,    33,    34,   312,     0,     0,     5,     0,   541,
       0,     0,     0,     0,     7,     0,     0,     0,     0,     8,
       0,     9,     0,     0,     0,     0,    35,     0,     0,    10,
       0,     0,    11,     0,     0,    12,    13,    14,     0,     0,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,     0,    26,    27,     0,    28,    29,    30,    31,    32,
      33,    34,     4,     0,     0,     5,     0,     6,     0,     0,
       0,     0,     7,     0,     0,     0,     0,   569,     0,     9,
       0,     0,     0,     0,   542,     0,     0,    10,     0,     0,
      11,     0,     0,    12,    13,    14,     0,     0,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,    25,     0,
      26,    27,     0,    28,    29,    30,    31,    32,    33,    34,
       4,     0,     0,     5,     0,     6,     0,     0,     0,     0,
       7,     0,     0,     0,     0,   570,     0,     9,     0,     0,
       0,     0,    35,     0,     0,    10,     0,     0,    11,     0,
       0,    12,    13,    14,     0,     0,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,    25,     0,    26,    27,
       0,    28,    29,    30,    31,    32,    33,    34,     4,     0,
       0,     5,     0,     6,     0,     0,     0,     0,     7,     0,
       0,     0,     0,   571,     0,     9,     0,     0,     0,     0,
      35,     0,     0,    10,     0,     0,    11,     0,     0,    12,
      13,    14,     0,     0,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,     0,    26,    27,     0,    28,
      29,    30,    31,    32,    33,    34,     4,     0,     0,     5,
       0,     6,     0,     0,     0,     0,     7,     0,     0,     0,
       0,   574,     0,     9,     0,     0,     0,     0,    35,     0,
       0,    10,     0,     0,    11,     0,     0,    12,    13,    14,
       0,     0,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,     0,    26,    27,     0,    28,    29,    30,
      31,    32,    33,    34,     4,     0,     0,     5,     0,     6,
       0,     0,     0,     0,     7,     0,     0,     0,     0,   575,
       0,     9,     0,     0,     0,     0,    35,     0,     0,    10,
       0,     0,    11,     0,     0,    12,    13,    14,     0,     0,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,     0,    26,    27,     0,    28,    29,    30,    31,    32,
      33,    34,     4,     0,     0,     5,     0,     6,     0,     0,
       0,     0,     7,     0,     0,     0,     0,   587,     0,     9,
       0,     0,     0,     0,    35,     0,     0,    10,     0,     0,
      11,     0,     0,    12,    13,    14,     0,     0,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,    25,     0,
      26,    27,     0,    28,    29,    30,    31,    32,    33,    34,
       4,     0,     0,     5,     0,     6,     0,     0,     0,     0,
       7,     0,     0,     0,     0,   588,     0,     9,     0,     0,
       0,     0,    35,     0,     0,    10,     0,     0,    11,     0,
       0,    12,    13,    14,     0,     0,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,    25,     0,    26,    27,
       0,    28,    29,    30,    31,    32,    33,    34,     4,     0,
       0,     5,     0,     6,     0,     0,     0,     0,     7,     0,
       0,     0,     0,   634,     0,     9,     0,     0,     0,     0,
      35,     0,     0,    10,     0,     0,    11,     0,     0,    12,
      13,    14,     0,     0,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,     0,    26,    27,     0,    28,
      29,    30,    31,    32,    33,    34,     4,     0,     0,     5,
       0,     6,     0,     0,     0,     0,     7,     0,     0,     0,
       0,   650,     0,     9,     0,     0,     0,     0,    35,     0,
       0,    10,     0,     0,    11,     0,     0,    12,    13,    14,
       0,     0,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,     0,    26,    27,     0,    28,    29,    30,
      31,    32,    33,    34,   312,     0,     0,     5,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     8,
       0,     9,     0,     0,   312,     0,    35,     5,     0,     0,
       0,     0,     0,     0,     0,     0,    13,    14,     0,     8,
       0,     9,    17,    18,     0,    20,    21,    22,     0,     0,
       0,     0,    26,    27,     0,    28,    13,    14,     0,    32,
      33,     0,    17,    18,     0,    20,    21,    22,   312,     0,
       0,     5,    26,    27,     0,    28,     0,     0,     0,    32,
      33,     0,     0,     8,   313,     9,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      13,    14,     0,     0,   486,     0,    17,    18,     0,    20,
      21,    22,     0,     0,     0,     0,    26,    27,     0,    28,
       0,     0,     0,    32,    33,    68,    69,    70,     0,    71,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     8,     0,     0,    72,    73,    74,    75,   489,     0,
      76,     0,    77,    78,     0,    97,     0,     0,    13,    14,
      68,    69,    70,     0,    71,     0,     0,     0,     0,     0,
      68,    69,    70,    79,    71,    27,     8,    80,     0,    72,
      73,    74,    75,     0,     0,    76,     8,    77,    78,    72,
      73,    74,    75,    13,    14,    81,     0,     0,    82,     0,
      83,     0,    84,    13,    14,     0,     0,     0,    79,     0,
      27,     0,    80,     0,     0,     0,     0,     0,    79,     0,
      27,     0,    80,     0,     0,     0,     0,     0,     0,     0,
      81,     0,     0,    82,     0,    83,     0,    84,     0,     0,
       0,     0,     0,    82,     0,    83,     0,    84,   288,   289,
       0,     0,   237,   238,     0,     0,     0,     0,   239,     0,
     240,   241,     0,   242,     0,   243,   244,   245,   246,   247,
     248,   249,   250,   251,   252,   253,   254,   255,   256,   257,
     258,   259,   260,   261,   262,   263,   264,   265,   266,   267,
     268,   269,   270,   271,   272,   273,   274,   275,   276,   277,
     278,   279,   280,   281,   282,   283,     0,   288,   289,     0,
       0,   237,   238,     0,     0,     0,     0,   239,     0,   240,
     241,   290,   242,     0,   243,   244,   245,   246,   247,   248,
     249,   250,   251,   252,   253,   254,   255,   256,   257,   258,
     259,   260,   261,   262,   263,   264,   265,   266,   267,   268,
     269,   270,   271,   272,   273,   274,   275,   276,   277,   278,
     279,   280,   281,   282,   283,     0,   235,     0,     0,   236,
     237,   238,     0,     0,     0,     0,   239,     0,   240,   241,
     615,   242,     0,   243,   244,   245,   246,   247,   248,   249,
     250,   251,   252,   253,   254,   255,   256,   257,   258,   259,
     260,   261,   262,   263,   264,   265,   266,   267,   268,   269,
     270,   271,   272,   273,   274,   275,   276,   277,   278,   279,
     280,   281,   282,   283,   309,     0,     0,   310,   237,   238,
       0,     0,     0,     0,   239,     0,   240,   241,     0,   242,
       0,   243,   244,   245,   246,   247,   248,   249,   250,   251,
     252,   253,   254,   255,   256,   257,   258,   259,   260,   261,
     262,   263,   264,   265,   266,   267,   268,   269,   270,   271,
     272,   273,   274,   275,   276,   277,   278,   279,   280,   281,
     282,   283,   336,     0,     0,   337,   237,   238,     0,     0,
       0,     0,   239,     0,   240,   241,     0,   242,     0,   243,
     244,   245,   246,   247,   248,   249,   250,   251,   252,   253,
     254,   255,   256,   257,   258,   259,   260,   261,   262,   263,
     264,   265,   266,   267,   268,   269,   270,   271,   272,   273,
     274,   275,   276,   277,   278,   279,   280,   281,   282,   283
};

static const yytype_int16 yycheck[] =
{
       2,   179,   179,   433,   195,   499,   175,   113,   179,   179,
      84,     4,   210,   179,   179,   505,    76,   505,   102,    21,
      22,    23,   179,     7,   505,     9,   224,   505,    76,   227,
     505,    15,    16,    81,     0,   505,    20,   505,   505,   179,
      24,     4,    29,   537,    28,   505,     4,     4,   505,   539,
     505,   539,   505,   505,   505,   179,    19,    77,   539,    34,
      84,   539,    82,     4,   539,    10,    11,    12,    13,   539,
       4,   539,   539,    36,    37,     4,     4,     4,   503,   539,
     179,   179,   539,    76,   539,    19,   539,   539,   539,     4,
      53,    78,    19,    10,    11,    12,    13,    55,    82,     4,
      87,    82,     4,    84,   594,   211,   594,     4,    62,    84,
      39,   113,    53,   594,    19,   540,   594,    62,    52,   594,
       4,    84,    19,   207,   594,    52,   594,   594,   145,    83,
     214,   625,   216,    51,   594,    19,     4,   594,    53,   594,
     331,   594,   594,   594,   228,    62,    62,    52,   196,   197,
      84,    19,    36,    37,     4,    52,     4,    84,   142,   143,
     177,    78,   146,    80,    38,    33,     4,    83,    46,    53,
      87,    78,    76,    21,     4,     4,    54,   179,     7,    84,
      87,    19,   199,    87,    77,    53,    77,    84,    62,    82,
     174,    82,    76,    37,    79,    33,    40,    77,    46,    84,
      15,   631,    82,    18,   206,    56,    54,     4,    76,   211,
     640,   213,    84,    67,   198,    53,    83,   201,   202,   203,
     204,    81,    19,   441,    84,    82,   444,    84,    84,   313,
      16,   305,    84,   307,     4,     5,     4,     5,    84,    36,
      37,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    84,    74,    75,    53,     4,     5,    80,
      72,    73,    84,   441,   441,    84,   444,   444,   192,   193,
     441,   441,     4,   444,   444,   441,   441,    84,   444,   444,
      84,    84,    84,    84,   441,    84,    76,   444,    62,    63,
      64,    65,    66,    67,    68,    69,    70,    71,    76,     4,
      76,   441,   286,   287,   444,    27,    26,     3,    87,   383,
      55,   295,    76,   167,   388,     4,    76,   441,   402,    84,
     444,    84,   176,    76,    76,    84,     4,   505,   505,    32,
      76,     4,   416,    84,   505,   505,     4,   421,    77,   505,
     505,    82,   441,   441,   346,   444,   444,    77,   505,    76,
       4,    77,    77,    38,   356,    15,   358,     4,     4,    76,
     344,   539,   539,   347,   448,   505,   368,   451,   539,   539,
       4,    32,     5,   539,   539,   459,     5,   461,   462,    79,
     578,   505,   539,     6,   468,    76,   470,    84,    76,    76,
      80,    77,    84,   395,    77,    76,   380,   381,    82,   539,
      82,    76,   486,    79,    76,   489,   505,   505,   577,    76,
      76,    33,    77,    84,   498,   539,   594,   594,    77,   403,
      82,   405,   406,   594,   594,   409,    77,    84,   594,   594,
       4,    84,    77,    77,    84,    77,    17,   594,    81,   441,
     539,   539,   444,    84,   428,   299,    84,   449,    40,    16,
      20,     4,   436,    84,   594,    48,    84,    84,    84,    50,
      77,   463,    58,    84,    84,   467,    77,    77,    77,    77,
     594,    77,    14,    77,    77,    16,     4,    84,     4,    84,
      32,    56,    84,    84,    84,    84,    84,    84,    83,    76,
      84,   493,    48,   495,    84,   594,   594,   499,   582,    84,
      84,    84,    84,   505,    76,   507,   508,   509,   592,   363,
      76,     4,   514,   497,   516,    29,    84,    44,    48,    84,
     374,   375,    84,    84,    84,   527,    84,    84,   530,    84,
      84,    44,    56,   517,    84,   537,    84,   539,   392,   393,
      84,   525,   526,     5,    84,   399,   400,    84,    84,     3,
       4,     5,    84,     7,    57,    84,    77,   641,    14,    84,
      52,    84,    14,     4,    84,    19,    84,    84,    22,    23,
      24,    25,    84,    84,    28,    84,    30,    31,    84,    77,
      84,    83,    36,    37,    84,    84,     4,     3,     4,     5,
      84,     7,   594,    84,    84,     4,    84,    51,    84,    53,
     147,    55,   136,    19,   371,    90,    22,    23,    24,    25,
     181,   127,    28,    77,    30,    31,   180,   619,   194,    73,
      36,    37,    76,   625,    78,   213,    80,    -1,   298,   434,
     524,    -1,    -1,    87,    -1,    51,    -1,    53,    -1,    55,
      -1,    -1,    -1,    -1,   646,    -1,    -1,    -1,    -1,    -1,
      -1,   635,    -1,    -1,     3,     4,     5,    73,     7,    -1,
      76,    -1,    78,    79,    80,    -1,    -1,    -1,    84,    -1,
      19,    -1,    -1,    22,    23,    24,    25,    -1,    -1,    28,
      -1,    30,    31,    -1,    -1,    -1,    -1,    36,    37,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    51,    -1,    53,    -1,    55,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    73,    -1,    -1,    76,    -1,    78,
      -1,    80,     4,     5,    -1,    84,     8,     9,    -1,    -1,
      -1,    -1,    14,    -1,    16,    17,    -1,    19,    -1,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,     4,     5,
      -1,    -1,     8,     9,    -1,    -1,    -1,    -1,    14,    81,
      16,    17,    84,    19,    -1,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,     4,    -1,    -1,     7,
      -1,     9,    -1,    -1,    -1,    -1,    14,    -1,    -1,    17,
      -1,    19,    -1,    21,    -1,    -1,    -1,    -1,    84,    -1,
      -1,    29,    -1,    -1,    32,    -1,    -1,    35,    36,    37,
      -1,    -1,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    -1,    52,    53,    -1,    55,    56,    57,
      58,    59,    60,    61,     4,    -1,    -1,     7,    -1,     9,
      -1,    -1,    -1,    -1,    14,    -1,    -1,    -1,    -1,    19,
      -1,    21,    -1,    -1,    -1,    -1,    84,    -1,    -1,    29,
      -1,    -1,    32,    -1,    -1,    35,    36,    37,    -1,    -1,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    -1,    52,    53,    -1,    55,    56,    57,    58,    59,
      60,    61,     4,    -1,    -1,     7,    -1,     9,    -1,    -1,
      -1,    -1,    14,    -1,    -1,    -1,    -1,    19,    -1,    21,
      -1,    -1,    -1,    -1,    84,    -1,    -1,    29,    -1,    -1,
      32,    -1,    -1,    35,    36,    37,    -1,    -1,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    -1,
      52,    53,    -1,    55,    56,    57,    58,    59,    60,    61,
       4,    -1,    -1,     7,    -1,     9,    -1,    -1,    -1,    -1,
      14,    -1,    -1,    -1,    -1,    19,    -1,    21,    -1,    -1,
      -1,    -1,    84,    -1,    -1,    29,    -1,    -1,    32,    -1,
      -1,    35,    36,    37,    -1,    -1,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    -1,    52,    53,
      -1,    55,    56,    57,    58,    59,    60,    61,     4,    -1,
      -1,     7,    -1,     9,    -1,    -1,    -1,    -1,    14,    -1,
      -1,    -1,    -1,    19,    -1,    21,    -1,    -1,    -1,    -1,
      84,    -1,    -1,    29,    -1,    -1,    32,    -1,    -1,    35,
      36,    37,    -1,    -1,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    -1,    52,    53,    -1,    55,
      56,    57,    58,    59,    60,    61,     4,    -1,    -1,     7,
      -1,     9,    -1,    -1,    -1,    -1,    14,    -1,    -1,    -1,
      -1,    19,    -1,    21,    -1,    -1,    -1,    -1,    84,    -1,
      -1,    29,    -1,    -1,    32,    -1,    -1,    35,    36,    37,
      -1,    -1,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    -1,    52,    53,    -1,    55,    56,    57,
      58,    59,    60,    61,     4,    -1,    -1,     7,    -1,     9,
      -1,    -1,    -1,    -1,    14,    -1,    -1,    -1,    -1,    19,
      -1,    21,    -1,    -1,    -1,    -1,    84,    -1,    -1,    29,
      -1,    -1,    32,    -1,    -1,    35,    36,    37,    -1,    -1,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    -1,    52,    53,    -1,    55,    56,    57,    58,    59,
      60,    61,     4,    -1,    -1,     7,    -1,     9,    -1,    -1,
      -1,    -1,    14,    -1,    -1,    -1,    -1,    19,    -1,    21,
      -1,    -1,    -1,    -1,    84,    -1,    -1,    29,    -1,    -1,
      32,    -1,    -1,    35,    36,    37,    -1,    -1,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    -1,
      52,    53,    -1,    55,    56,    57,    58,    59,    60,    61,
       4,    -1,    -1,     7,    -1,     9,    -1,    -1,    -1,    -1,
      14,    -1,    -1,    -1,    -1,    19,    -1,    21,    -1,    -1,
      -1,    -1,    84,    -1,    -1,    29,    -1,    -1,    32,    -1,
      -1,    35,    36,    37,    -1,    -1,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    -1,    52,    53,
      -1,    55,    56,    57,    58,    59,    60,    61,     4,    -1,
      -1,     7,    -1,     9,    -1,    -1,    -1,    -1,    14,    -1,
      -1,    -1,    -1,    19,    -1,    21,    -1,    -1,    -1,    -1,
      84,    -1,    -1,    29,    -1,    -1,    32,    -1,    -1,    35,
      36,    37,    -1,    -1,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    -1,    52,    53,    -1,    55,
      56,    57,    58,    59,    60,    61,     4,    -1,    -1,     7,
      -1,     9,    -1,    -1,    -1,    -1,    14,    -1,    -1,    -1,
      -1,    19,    -1,    21,    -1,    -1,    -1,    -1,    84,    -1,
      -1,    29,    -1,    -1,    32,    -1,    -1,    35,    36,    37,
      -1,    -1,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    -1,    52,    53,    -1,    55,    56,    57,
      58,    59,    60,    61,     4,    -1,    -1,     7,    -1,     9,
      -1,    -1,    -1,    -1,    14,    -1,    -1,    -1,    -1,    19,
      -1,    21,    -1,    -1,    -1,    -1,    84,    -1,    -1,    29,
      -1,    -1,    32,    -1,    -1,    35,    36,    37,    -1,    -1,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    -1,    52,    53,    -1,    55,    56,    57,    58,    59,
      60,    61,     4,    -1,    -1,     7,    -1,     9,    -1,    -1,
      -1,    -1,    14,    -1,    -1,    -1,    -1,    19,    -1,    21,
      -1,    -1,    -1,    -1,    84,    -1,    -1,    29,    -1,    -1,
      32,    -1,    -1,    35,    36,    37,    -1,    -1,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    -1,
      52,    53,    -1,    55,    56,    57,    58,    59,    60,    61,
       4,    -1,    -1,     7,    -1,     9,    -1,    -1,    -1,    -1,
      14,    -1,    -1,    -1,    -1,    19,    -1,    21,    -1,    -1,
      -1,    -1,    84,    -1,    -1,    29,    -1,    -1,    32,    -1,
      -1,    35,    36,    37,    -1,    -1,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    -1,    52,    53,
      -1,    55,    56,    57,    58,    59,    60,    61,     4,    -1,
      -1,     7,    -1,     9,    -1,    -1,    -1,    -1,    14,    -1,
      -1,    -1,    -1,    19,    -1,    21,    -1,    -1,    -1,    -1,
      84,    -1,    -1,    29,    -1,    -1,    32,    -1,    -1,    35,
      36,    37,    -1,    -1,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    -1,    52,    53,    -1,    55,
      56,    57,    58,    59,    60,    61,     4,    -1,    -1,     7,
      -1,     9,    -1,    -1,    -1,    -1,    14,    -1,    -1,    -1,
      -1,    19,    -1,    21,    -1,    -1,    -1,    -1,    84,    -1,
      -1,    29,    -1,    -1,    32,    -1,    -1,    35,    36,    37,
      -1,    -1,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    -1,    52,    53,    -1,    55,    56,    57,
      58,    59,    60,    61,     4,    -1,    -1,     7,    -1,     9,
      -1,    -1,    -1,    -1,    14,    -1,    -1,    -1,    -1,    19,
      -1,    21,    -1,    -1,    -1,    -1,    84,    -1,    -1,    29,
      -1,    -1,    32,    -1,    -1,    35,    36,    37,    -1,    -1,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    -1,    52,    53,    -1,    55,    56,    57,    58,    59,
      60,    61,     4,    -1,    -1,     7,    -1,     9,    -1,    -1,
      -1,    -1,    14,    -1,    -1,    -1,    -1,    19,    -1,    21,
      -1,    -1,    -1,    -1,    84,    -1,    -1,    29,    -1,    -1,
      32,    -1,    -1,    35,    36,    37,    -1,    -1,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    -1,
      52,    53,    -1,    55,    56,    57,    58,    59,    60,    61,
       4,    -1,    -1,     7,    -1,     9,    -1,    -1,    -1,    -1,
      14,    -1,    -1,    -1,    -1,    19,    -1,    21,    -1,    -1,
      -1,    -1,    84,    -1,    -1,    29,    -1,    -1,    32,    -1,
      -1,    35,    36,    37,    -1,    -1,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    -1,    52,    53,
      -1,    55,    56,    57,    58,    59,    60,    61,     4,    -1,
      -1,     7,    -1,     9,    -1,    -1,    -1,    -1,    14,    -1,
      -1,    -1,    -1,    19,    -1,    21,    -1,    -1,    -1,    -1,
      84,    -1,    -1,    29,    -1,    -1,    32,    -1,    -1,    35,
      36,    37,    -1,    -1,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    -1,    52,    53,    -1,    55,
      56,    57,    58,    59,    60,    61,     4,    -1,    -1,     7,
      -1,     9,    -1,    -1,    -1,    -1,    14,    -1,    -1,    -1,
      -1,    19,    -1,    21,    -1,    -1,    -1,    -1,    84,    -1,
      -1,    29,    -1,    -1,    32,    -1,    -1,    35,    36,    37,
      -1,    -1,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    -1,    52,    53,    -1,    55,    56,    57,
      58,    59,    60,    61,     4,    -1,    -1,     7,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    19,
      -1,    21,    -1,    -1,     4,    -1,    84,     7,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    36,    37,    -1,    19,
      -1,    21,    42,    43,    -1,    45,    46,    47,    -1,    -1,
      -1,    -1,    52,    53,    -1,    55,    36,    37,    -1,    59,
      60,    -1,    42,    43,    -1,    45,    46,    47,     4,    -1,
      -1,     7,    52,    53,    -1,    55,    -1,    -1,    -1,    59,
      60,    -1,    -1,    19,    84,    21,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      36,    37,    -1,    -1,    84,    -1,    42,    43,    -1,    45,
      46,    47,    -1,    -1,    -1,    -1,    52,    53,    -1,    55,
      -1,    -1,    -1,    59,    60,     3,     4,     5,    -1,     7,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    19,    -1,    -1,    22,    23,    24,    25,    84,    -1,
      28,    -1,    30,    31,    -1,    33,    -1,    -1,    36,    37,
       3,     4,     5,    -1,     7,    -1,    -1,    -1,    -1,    -1,
       3,     4,     5,    51,     7,    53,    19,    55,    -1,    22,
      23,    24,    25,    -1,    -1,    28,    19,    30,    31,    22,
      23,    24,    25,    36,    37,    73,    -1,    -1,    76,    -1,
      78,    -1,    80,    36,    37,    -1,    -1,    -1,    51,    -1,
      53,    -1,    55,    -1,    -1,    -1,    -1,    -1,    51,    -1,
      53,    -1,    55,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      73,    -1,    -1,    76,    -1,    78,    -1,    80,    -1,    -1,
      -1,    -1,    -1,    76,    -1,    78,    -1,    80,     4,     5,
      -1,    -1,     8,     9,    -1,    -1,    -1,    -1,    14,    -1,
      16,    17,    -1,    19,    -1,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    -1,     4,     5,    -1,
      -1,     8,     9,    -1,    -1,    -1,    -1,    14,    -1,    16,
      17,    77,    19,    -1,    21,    22,    23,    24,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    -1,     4,    -1,    -1,     7,
       8,     9,    -1,    -1,    -1,    -1,    14,    -1,    16,    17,
      77,    19,    -1,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,     4,    -1,    -1,     7,     8,     9,
      -1,    -1,    -1,    -1,    14,    -1,    16,    17,    -1,    19,
      -1,    21,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,     4,    -1,    -1,     7,     8,     9,    -1,    -1,
      -1,    -1,    14,    -1,    16,    17,    -1,    19,    -1,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    89,    90,     0,     4,     7,     9,    14,    19,    21,
      29,    32,    35,    36,    37,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    52,    53,    55,    56,
      57,    58,    59,    60,    61,    84,    91,    92,    94,    95,
     100,   101,   102,   104,   105,   106,   107,   112,   113,   114,
     115,   116,   119,   120,   122,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,     4,    76,    76,     3,     4,
       5,     7,    22,    23,    24,    25,    28,    30,    31,    51,
      55,    73,    76,    78,    80,    95,   140,   141,   142,   143,
     144,   145,   146,   147,   149,   150,   153,    33,   140,     4,
       4,    39,    84,   140,   140,     4,     4,     4,     7,   140,
      95,    95,     4,    76,    95,   121,   126,   140,    51,     4,
      55,    87,   140,     4,    19,    33,    53,    98,    99,   117,
       4,     4,     4,     5,     4,     5,    56,    84,    10,    11,
      12,    13,    62,    78,    80,    87,    93,    96,    83,    84,
      84,    84,    84,    84,    84,    84,    84,    84,    84,    84,
      84,    76,   140,   154,   155,   154,     4,    76,   146,   147,
       4,    76,   146,   140,   163,   163,    76,    87,   151,    16,
      27,    26,    62,    63,    64,    65,    66,    67,    68,    69,
      70,    71,    72,    73,    96,   148,    74,    75,    78,    87,
       3,    55,    76,    38,    62,     4,    90,    84,    84,    76,
      76,    76,   121,    82,    84,    87,    84,     4,    21,    46,
      54,    46,    54,     4,    76,    99,    32,    76,    84,     4,
       4,   117,   140,   140,    97,     4,     7,     8,     9,    14,
      16,    17,    19,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,   160,   140,    62,    93,     4,     5,
      77,   159,   160,   161,    77,    82,    77,   154,    29,    76,
      77,    77,    79,    84,   140,   156,    81,   161,   154,     4,
       7,   160,     4,    84,    92,    95,   100,   101,   116,   128,
     129,   130,   132,   133,   134,   135,   139,   142,   143,   145,
     145,   148,   144,   146,   146,   140,     4,     7,   160,     4,
     140,   140,   140,   140,    38,    36,    90,    15,   108,     4,
     157,   158,   157,   121,    77,   126,    90,     4,    90,     4,
      53,     4,    53,    76,   157,     4,   118,   157,    90,     5,
       5,    32,    79,     6,    76,    76,   140,   140,    76,    84,
      62,    83,    77,    82,   140,    77,   150,   154,    82,   163,
     163,    77,    76,    76,   152,    90,    84,   144,    79,    76,
      76,    77,    84,    33,   140,    37,    40,    19,   140,    15,
      18,   109,    77,    82,    77,    77,    84,    19,    19,   154,
      77,    84,    77,    19,   118,    81,   154,   154,     4,   162,
     123,   140,   140,    84,   163,    77,   163,    79,    81,   154,
     154,    17,    19,   137,    17,   138,   154,   154,    84,    90,
     140,    84,   140,   140,    40,    16,   140,    84,    20,    84,
       4,    84,    84,    90,    48,    50,    77,    90,    84,    58,
      84,    77,    77,   140,    77,     4,    19,    52,    84,   124,
     123,     4,   159,   140,    77,    77,    84,   139,    14,    84,
     139,    77,    77,    90,    19,    53,   103,    34,    84,    90,
      84,    84,    84,    84,    16,   110,    84,    90,    90,    90,
      19,    84,    84,    19,    90,    84,    90,    83,     4,     5,
     125,     4,     4,    19,    76,    62,    83,    90,    84,    84,
      90,    84,    19,    32,    84,    95,   140,    90,   103,   110,
      84,     9,    84,    92,   100,   101,   102,   104,   105,   106,
     107,   111,   112,   113,   114,   115,   116,   119,   120,   127,
     128,   129,   130,   131,   132,   133,   134,   135,   136,    19,
      19,    19,    48,    56,    19,    19,   140,    76,    76,    82,
      84,    84,    84,     4,   162,   140,   140,    19,    19,    29,
      84,    84,    84,   103,   110,    84,    84,    84,    84,    84,
      84,    84,    84,    84,    84,    84,    84,    84,    44,    44,
      48,    84,    84,    57,    56,    77,   161,   157,     5,    90,
      84,    77,    14,    14,    84,    90,    84,    84,    84,    84,
      84,    84,    77,    77,    19,    83,    84,    84,   103,   123,
      84,    84,    52,   140,    19,   123,    90,    84,     4,    19,
      19,    84,     4,     4,    84,    84
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    88,    89,    90,    90,    90,    91,    91,    91,    91,
      91,    91,    91,    91,    91,    91,    91,    91,    91,    91,
      91,    91,    91,    91,    91,    91,    91,    91,    91,    91,
      91,    91,    91,    92,    92,    92,    92,    93,    93,    93,
      93,    94,    94,    94,    95,    95,    95,    95,    95,    97,
      96,    98,    98,    99,    99,    99,    99,   100,   100,   101,
     101,   101,   101,   101,   102,   103,   103,   103,   104,   104,
     104,   104,   105,   105,   106,   107,   108,   108,   109,   109,
     110,   110,   110,   111,   111,   111,   111,   111,   111,   111,
     111,   111,   111,   111,   111,   111,   111,   111,   111,   111,
     111,   111,   111,   111,   111,   111,   111,   111,   111,   112,
     112,   113,   113,   114,   115,   116,   116,   116,   116,   116,
     116,   117,   117,   118,   119,   119,   119,   120,   121,   121,
     122,   122,   123,   123,   123,   124,   124,   124,   124,   124,
     125,   125,   126,   126,   127,   128,   128,   128,   128,   128,
     128,   128,   128,   129,   130,   130,   131,   132,   133,   134,
     134,   135,   135,   136,   136,   137,   137,   137,   138,   138,
     138,   139,   139,   139,   139,   139,   139,   139,   139,   139,
     139,   139,   140,   141,   141,   142,   142,   143,   143,   143,
     144,   144,   144,   145,   145,   145,   146,   146,   146,   146,
     146,   146,   147,   147,   147,   147,   147,   148,   148,   148,
     148,   148,   148,   148,   148,   148,   148,   149,   149,   149,
     149,   149,   149,   149,   149,   149,   149,   149,   149,   149,
     149,   149,   150,   150,   151,   151,   151,   151,   152,   152,
     153,   153,   154,   154,   155,   155,   156,   156,   157,   157,
     158,   158,   159,   159,   160,   160,   160,   160,   160,   160,
     160,   160,   160,   160,   160,   160,   160,   160,   160,   160,
     160,   160,   160,   160,   160,   160,   160,   160,   160,   160,
     160,   160,   160,   160,   160,   160,   160,   160,   160,   160,
     160,   160,   160,   160,   160,   160,   160,   160,   160,   160,
     160,   160,   161,   161,   161,   161,   161,   161,   162,   162,
     163,   163
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     2,     1,
       1,     2,     1,     2,     2,     2,     2,     2,     2,     2,
       2,     1,     1,     3,     3,     4,     4,     1,     1,     1,
       1,     1,     4,     3,     1,     1,     1,     1,     1,     0,
       4,     1,     2,     1,     1,     1,     1,     2,     4,     4,
       4,     6,     6,     6,    10,     3,     2,     3,     7,     8,
       9,    11,     7,     7,     7,     7,     5,     6,     0,     3,
       0,     2,     2,     2,     2,     2,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     1,     2,     1,     2,
       2,     2,     2,     2,     2,     2,     2,     1,     1,    10,
      10,     9,    10,    10,     7,     2,     2,     2,     2,     4,
       4,     1,     4,     1,     9,     7,    10,     2,     1,     3,
      10,     9,     0,     2,     2,     3,    10,    10,     9,     7,
       1,     3,     1,     3,     7,     4,     4,     3,     4,     4,
       3,     3,     3,     2,     1,     2,     2,     2,     2,     1,
       2,     1,     2,     6,     6,     3,     3,     6,     0,     3,
       6,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     3,     1,     3,     1,     3,     4,
       1,     3,     3,     1,     3,     3,     1,     2,     2,     2,
       4,     5,     1,     4,     3,     6,     6,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     3,     1,
       1,     2,     4,     1,     1,     1,     1,     1,     3,     3,
       5,     1,     3,     5,     0,     3,     3,     5,     0,     3,
       2,     3,     0,     1,     1,     3,     1,     4,     0,     1,
       1,     3,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     3,     3,     6,     6,     6,     9,     1,     2,
       0,     2
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
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2547 "src/parser.tab.c"
        break;

    case YYSYMBOL_STRING: /* STRING  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2553 "src/parser.tab.c"
        break;

    case YYSYMBOL_LENS_CONTENT: /* LENS_CONTENT  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2559 "src/parser.tab.c"
        break;

    case YYSYMBOL_QUALIFIED_IDENT: /* QUALIFIED_IDENT  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2565 "src/parser.tab.c"
        break;

    case YYSYMBOL_program: /* program  */
#line 495 "src/parser.y"
            { (void) ((*yyvaluep).stmt_list); }
#line 2571 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement_list: /* statement_list  */
#line 475 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2577 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement: /* statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2583 "src/parser.tab.c"
        break;

    case YYSYMBOL_assignment: /* assignment  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2589 "src/parser.tab.c"
        break;

    case YYSYMBOL_lvalue: /* lvalue  */
#line 473 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2595 "src/parser.tab.c"
        break;

    case YYSYMBOL_variable_name: /* variable_name  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2601 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_lens: /* comparison_lens  */
#line 480 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 2607 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_name: /* modifier_name  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2613 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_word: /* modifier_word  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2619 "src/parser.tab.c"
        break;

    case YYSYMBOL_print_statement: /* print_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2625 "src/parser.tab.c"
        break;

    case YYSYMBOL_call_statement: /* call_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2631 "src/parser.tab.c"
        break;

    case YYSYMBOL_with_lock_statement: /* with_lock_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2637 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_end: /* for_end  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2643 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_each_statement: /* for_each_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2649 "src/parser.tab.c"
        break;

    case YYSYMBOL_do_loop_statement: /* do_loop_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2655 "src/parser.tab.c"
        break;

    case YYSYMBOL_while_statement: /* while_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2661 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement: /* consider_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2667 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_branch_list: /* consider_branch_list  */
#line 478 "src/parser.y"
            { ast_free_consider_branch_list(((*yyvaluep).consider_branch_list)); }
#line 2673 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_else_opt: /* consider_else_opt  */
#line 475 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2679 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement_list: /* consider_statement_list  */
#line 475 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2685 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_body_statement: /* consider_body_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2691 "src/parser.tab.c"
        break;

    case YYSYMBOL_function_statement: /* function_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2697 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_statement: /* modifier_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2703 "src/parser.tab.c"
        break;

    case YYSYMBOL_program_statement: /* program_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2709 "src/parser.tab.c"
        break;

    case YYSYMBOL_library_statement: /* library_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2715 "src/parser.tab.c"
        break;

    case YYSYMBOL_use_statement: /* use_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2721 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_signature: /* modifier_signature  */
#line 481 "src/parser.y"
            { ast_free_modifier_signature(((*yyvaluep).modifier_signature)); }
#line 2727 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_context: /* modifier_context  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2733 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_statement: /* watch_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2739 "src/parser.tab.c"
        break;

    case YYSYMBOL_unwatch_statement: /* unwatch_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2745 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_list: /* watch_target_list  */
#line 479 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2751 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_statement: /* server_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2757 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item_list: /* server_item_list  */
#line 485 "src/parser.y"
            { ast_free_server_item_list(((*yyvaluep).server_item_list)); }
#line 2763 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item: /* server_item  */
#line 484 "src/parser.y"
            { AstServerItemList one = ast_server_item_list_append(ast_server_item_list_empty(), ((*yyvaluep).server_item)); ast_free_server_item_list(one); }
#line 2769 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_string_list: /* server_string_list  */
#line 479 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2775 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_path: /* watch_target_path  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2781 "src/parser.tab.c"
        break;

    case YYSYMBOL_without_watchers_statement: /* without_watchers_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2787 "src/parser.tab.c"
        break;

    case YYSYMBOL_on_error_statement: /* on_error_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2793 "src/parser.tab.c"
        break;

    case YYSYMBOL_error_statement: /* error_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2799 "src/parser.tab.c"
        break;

    case YYSYMBOL_return_statement: /* return_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2805 "src/parser.tab.c"
        break;

    case YYSYMBOL_label_statement: /* label_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2811 "src/parser.tab.c"
        break;

    case YYSYMBOL_goto_statement: /* goto_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2817 "src/parser.tab.c"
        break;

    case YYSYMBOL_gosub_statement: /* gosub_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2823 "src/parser.tab.c"
        break;

    case YYSYMBOL_break_statement: /* break_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2829 "src/parser.tab.c"
        break;

    case YYSYMBOL_continue_statement: /* continue_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2835 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_statement: /* if_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2841 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_block_tail: /* if_block_tail  */
#line 475 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2847 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_inline_tail: /* if_inline_tail  */
#line 475 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2853 "src/parser.tab.c"
        break;

    case YYSYMBOL_inline_statement: /* inline_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2859 "src/parser.tab.c"
        break;

    case YYSYMBOL_expression: /* expression  */
#line 473 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2865 "src/parser.tab.c"
        break;

    case YYSYMBOL_or_expression: /* or_expression  */
#line 473 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2871 "src/parser.tab.c"
        break;

    case YYSYMBOL_and_expression: /* and_expression  */
#line 473 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2877 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_expression: /* comparison_expression  */
#line 473 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2883 "src/parser.tab.c"
        break;

    case YYSYMBOL_additive_expression: /* additive_expression  */
#line 473 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2889 "src/parser.tab.c"
        break;

    case YYSYMBOL_multiplicative_expression: /* multiplicative_expression  */
#line 473 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2895 "src/parser.tab.c"
        break;

    case YYSYMBOL_unary_expression: /* unary_expression  */
#line 473 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2901 "src/parser.tab.c"
        break;

    case YYSYMBOL_postfix_expression: /* postfix_expression  */
#line 473 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2907 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_operator: /* comparison_operator  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2913 "src/parser.tab.c"
        break;

    case YYSYMBOL_primary: /* primary  */
#line 473 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2919 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_literal: /* record_literal  */
#line 473 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2925 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_suffix: /* ident_suffix  */
#line 482 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 2931 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_dot_suffix: /* ident_dot_suffix  */
#line 482 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 2937 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list_opt: /* argument_list_opt  */
#line 476 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 2943 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list: /* argument_list  */
#line 476 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 2949 "src/parser.tab.c"
        break;

    case YYSYMBOL_array_argument_list: /* array_argument_list  */
#line 476 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 2955 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list_opt: /* parameter_list_opt  */
#line 479 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2961 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list: /* parameter_list  */
#line 479 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2967 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_name: /* field_name  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2973 "src/parser.tab.c"
        break;

    case YYSYMBOL_dot_field_name: /* dot_field_name  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2979 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_field_list: /* record_field_list  */
#line 477 "src/parser.y"
            { ast_free_record_field_list(((*yyvaluep).record_field_list)); }
#line 2985 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_policy: /* field_policy  */
#line 483 "src/parser.y"
            { ast_free_expr(((*yyvaluep).field_policy).reset_expr); }
#line 2991 "src/parser.tab.c"
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
#line 500 "src/parser.y"
                     { ctx->parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3297 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 504 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3303 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 505 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3309 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 506 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3315 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 510 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3321 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 511 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3327 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 512 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3333 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 513 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3339 "src/parser.tab.c"
    break;

  case 10: /* statement: for_each_statement  */
#line 514 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3345 "src/parser.tab.c"
    break;

  case 11: /* statement: while_statement  */
#line 515 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3351 "src/parser.tab.c"
    break;

  case 12: /* statement: do_loop_statement  */
#line 516 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3357 "src/parser.tab.c"
    break;

  case 13: /* statement: consider_statement  */
#line 517 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3363 "src/parser.tab.c"
    break;

  case 14: /* statement: function_statement  */
#line 518 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3369 "src/parser.tab.c"
    break;

  case 15: /* statement: modifier_statement  */
#line 519 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3375 "src/parser.tab.c"
    break;

  case 16: /* statement: program_statement  */
#line 520 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3381 "src/parser.tab.c"
    break;

  case 17: /* statement: library_statement  */
#line 521 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3387 "src/parser.tab.c"
    break;

  case 18: /* statement: use_statement NEWLINE  */
#line 522 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3393 "src/parser.tab.c"
    break;

  case 19: /* statement: watch_statement  */
#line 523 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3399 "src/parser.tab.c"
    break;

  case 20: /* statement: server_statement  */
#line 524 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3405 "src/parser.tab.c"
    break;

  case 21: /* statement: unwatch_statement NEWLINE  */
#line 525 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3411 "src/parser.tab.c"
    break;

  case 22: /* statement: without_watchers_statement  */
#line 526 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3417 "src/parser.tab.c"
    break;

  case 23: /* statement: on_error_statement NEWLINE  */
#line 527 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3423 "src/parser.tab.c"
    break;

  case 24: /* statement: error_statement NEWLINE  */
#line 528 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3429 "src/parser.tab.c"
    break;

  case 25: /* statement: return_statement NEWLINE  */
#line 529 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3435 "src/parser.tab.c"
    break;

  case 26: /* statement: label_statement NEWLINE  */
#line 530 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3441 "src/parser.tab.c"
    break;

  case 27: /* statement: goto_statement NEWLINE  */
#line 531 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3447 "src/parser.tab.c"
    break;

  case 28: /* statement: gosub_statement NEWLINE  */
#line 532 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3453 "src/parser.tab.c"
    break;

  case 29: /* statement: break_statement NEWLINE  */
#line 533 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3459 "src/parser.tab.c"
    break;

  case 30: /* statement: continue_statement NEWLINE  */
#line 534 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3465 "src/parser.tab.c"
    break;

  case 31: /* statement: if_statement  */
#line 535 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3471 "src/parser.tab.c"
    break;

  case 32: /* statement: DIM  */
#line 541 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 3484 "src/parser.tab.c"
    break;

  case 33: /* assignment: lvalue OP_EQ expression  */
#line 552 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 3490 "src/parser.tab.c"
    break;

  case 34: /* assignment: lvalue compound_op expression  */
#line 557 "src/parser.y"
                                    { (yyval.stmt) = ast_assign_op((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr), (yyvsp[-1].op_char)); }
#line 3496 "src/parser.tab.c"
    break;

  case 35: /* assignment: lvalue comparison_lens compound_op expression  */
#line 558 "src/parser.y"
                                                    {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign_op((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr), (yyvsp[-1].op_char));
      }
#line 3510 "src/parser.tab.c"
    break;

  case 36: /* assignment: lvalue comparison_lens OP_EQ expression  */
#line 570 "src/parser.y"
                                              {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 3524 "src/parser.tab.c"
    break;

  case 37: /* compound_op: PLUS_EQ  */
#line 582 "src/parser.y"
               { (yyval.op_char) = '+'; }
#line 3530 "src/parser.tab.c"
    break;

  case 38: /* compound_op: MINUS_EQ  */
#line 583 "src/parser.y"
               { (yyval.op_char) = '-'; }
#line 3536 "src/parser.tab.c"
    break;

  case 39: /* compound_op: STAR_EQ  */
#line 584 "src/parser.y"
               { (yyval.op_char) = '*'; }
#line 3542 "src/parser.tab.c"
    break;

  case 40: /* compound_op: SLASH_EQ  */
#line 585 "src/parser.y"
               { (yyval.op_char) = '/'; }
#line 3548 "src/parser.tab.c"
    break;

  case 41: /* lvalue: variable_name  */
#line 589 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3554 "src/parser.tab.c"
    break;

  case 42: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 590 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3560 "src/parser.tab.c"
    break;

  case 43: /* lvalue: lvalue DOT dot_field_name  */
#line 591 "src/parser.y"
                                             { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3566 "src/parser.tab.c"
    break;

  case 44: /* variable_name: IDENT  */
#line 595 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 3572 "src/parser.tab.c"
    break;

  case 45: /* variable_name: END  */
#line 596 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 3578 "src/parser.tab.c"
    break;

  case 46: /* variable_name: NEXT  */
#line 597 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 3584 "src/parser.tab.c"
    break;

  case 47: /* variable_name: LOOP  */
#line 602 "src/parser.y"
                        { (yyval.text) = copy_const("loop"); }
#line 3590 "src/parser.tab.c"
    break;

  case 48: /* variable_name: UNTIL  */
#line 603 "src/parser.y"
                         { (yyval.text) = copy_const("until"); }
#line 3596 "src/parser.tab.c"
    break;

  case 49: /* $@1: %empty  */
#line 607 "src/parser.y"
             { lexer_begin_lens_content(ctx->active_lexer); }
#line 3602 "src/parser.tab.c"
    break;

  case 50: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 607 "src/parser.y"
                                                                                  {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 3610 "src/parser.tab.c"
    break;

  case 51: /* modifier_name: modifier_word  */
#line 613 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 3616 "src/parser.tab.c"
    break;

  case 52: /* modifier_name: modifier_name modifier_word  */
#line 614 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 3622 "src/parser.tab.c"
    break;

  case 53: /* modifier_word: IDENT  */
#line 618 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 3628 "src/parser.tab.c"
    break;

  case 54: /* modifier_word: TO  */
#line 619 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 3634 "src/parser.tab.c"
    break;

  case 55: /* modifier_word: END  */
#line 620 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 3640 "src/parser.tab.c"
    break;

  case 56: /* modifier_word: NEXT  */
#line 621 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 3646 "src/parser.tab.c"
    break;

  case 57: /* print_statement: PRINT expression  */
#line 625 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 3652 "src/parser.tab.c"
    break;

  case 58: /* print_statement: PRINT TO ERROR_VALUE expression  */
#line 631 "src/parser.y"
                                      { (yyval.stmt) = ast_print_error((yyvsp[0].expr)); }
#line 3658 "src/parser.tab.c"
    break;

  case 59: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 635 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 3664 "src/parser.tab.c"
    break;

  case 60: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 636 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 3675 "src/parser.tab.c"
    break;

  case 61: /* call_statement: lvalue DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 642 "src/parser.y"
                                                       {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 3685 "src/parser.tab.c"
    break;

  case 62: /* call_statement: lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 647 "src/parser.y"
                                                                 {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 3699 "src/parser.tab.c"
    break;

  case 63: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 656 "src/parser.y"
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
#line 3714 "src/parser.tab.c"
    break;

  case 64: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 669 "src/parser.y"
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
#line 3731 "src/parser.tab.c"
    break;

  case 65: /* for_end: END FOR NEWLINE  */
#line 692 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 3737 "src/parser.tab.c"
    break;

  case 66: /* for_end: NEXT NEWLINE  */
#line 693 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 3743 "src/parser.tab.c"
    break;

  case 67: /* for_end: NEXT variable_name NEWLINE  */
#line 694 "src/parser.y"
                                 { (yyval.text) = (yyvsp[-1].text); }
#line 3749 "src/parser.tab.c"
    break;

  case 68: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list for_end  */
#line 698 "src/parser.y"
                                                             {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3758 "src/parser.tab.c"
    break;

  case 69: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list for_end  */
#line 702 "src/parser.y"
                                                                  {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3767 "src/parser.tab.c"
    break;

  case 70: /* for_each_statement: FOR IDENT OP_EQ expression TO expression NEWLINE statement_list for_end  */
#line 709 "src/parser.y"
                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].expr), NULL, (yyvsp[-1].stmt_list));
      }
#line 3776 "src/parser.tab.c"
    break;

  case 71: /* for_each_statement: FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list for_end  */
#line 713 "src/parser.y"
                                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-9].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-9].text), (yyvsp[-7].expr), (yyvsp[-5].expr), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3785 "src/parser.tab.c"
    break;

  case 72: /* do_loop_statement: DO NEWLINE statement_list LOOP UNTIL expression NEWLINE  */
#line 722 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_do_loop((yyvsp[-4].stmt_list), (yyvsp[-1].expr), 1);
      }
#line 3793 "src/parser.tab.c"
    break;

  case 73: /* do_loop_statement: DO NEWLINE statement_list LOOP WHILE expression NEWLINE  */
#line 725 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_do_loop((yyvsp[-4].stmt_list), (yyvsp[-1].expr), 0);
      }
#line 3801 "src/parser.tab.c"
    break;

  case 74: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 731 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 3809 "src/parser.tab.c"
    break;

  case 75: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 737 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 3817 "src/parser.tab.c"
    break;

  case 76: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 743 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 3825 "src/parser.tab.c"
    break;

  case 77: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 746 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 3833 "src/parser.tab.c"
    break;

  case 78: /* consider_else_opt: %empty  */
#line 752 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3839 "src/parser.tab.c"
    break;

  case 79: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 753 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3845 "src/parser.tab.c"
    break;

  case 80: /* consider_statement_list: %empty  */
#line 757 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3851 "src/parser.tab.c"
    break;

  case 81: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 758 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3857 "src/parser.tab.c"
    break;

  case 82: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 759 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3863 "src/parser.tab.c"
    break;

  case 83: /* consider_body_statement: assignment NEWLINE  */
#line 763 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3869 "src/parser.tab.c"
    break;

  case 84: /* consider_body_statement: print_statement NEWLINE  */
#line 764 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3875 "src/parser.tab.c"
    break;

  case 85: /* consider_body_statement: call_statement NEWLINE  */
#line 765 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3881 "src/parser.tab.c"
    break;

  case 86: /* consider_body_statement: with_lock_statement  */
#line 766 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3887 "src/parser.tab.c"
    break;

  case 87: /* consider_body_statement: for_each_statement  */
#line 767 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3893 "src/parser.tab.c"
    break;

  case 88: /* consider_body_statement: while_statement  */
#line 768 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3899 "src/parser.tab.c"
    break;

  case 89: /* consider_body_statement: do_loop_statement  */
#line 769 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3905 "src/parser.tab.c"
    break;

  case 90: /* consider_body_statement: consider_statement  */
#line 770 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3911 "src/parser.tab.c"
    break;

  case 91: /* consider_body_statement: function_statement  */
#line 771 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3917 "src/parser.tab.c"
    break;

  case 92: /* consider_body_statement: modifier_statement  */
#line 772 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3923 "src/parser.tab.c"
    break;

  case 93: /* consider_body_statement: program_statement  */
#line 773 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3929 "src/parser.tab.c"
    break;

  case 94: /* consider_body_statement: library_statement  */
#line 774 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3935 "src/parser.tab.c"
    break;

  case 95: /* consider_body_statement: use_statement NEWLINE  */
#line 775 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3941 "src/parser.tab.c"
    break;

  case 96: /* consider_body_statement: watch_statement  */
#line 776 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3947 "src/parser.tab.c"
    break;

  case 97: /* consider_body_statement: unwatch_statement NEWLINE  */
#line 777 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3953 "src/parser.tab.c"
    break;

  case 98: /* consider_body_statement: without_watchers_statement  */
#line 778 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3959 "src/parser.tab.c"
    break;

  case 99: /* consider_body_statement: on_error_statement NEWLINE  */
#line 779 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3965 "src/parser.tab.c"
    break;

  case 100: /* consider_body_statement: error_statement NEWLINE  */
#line 780 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3971 "src/parser.tab.c"
    break;

  case 101: /* consider_body_statement: return_statement NEWLINE  */
#line 781 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3977 "src/parser.tab.c"
    break;

  case 102: /* consider_body_statement: label_statement NEWLINE  */
#line 782 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3983 "src/parser.tab.c"
    break;

  case 103: /* consider_body_statement: goto_statement NEWLINE  */
#line 783 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3989 "src/parser.tab.c"
    break;

  case 104: /* consider_body_statement: gosub_statement NEWLINE  */
#line 784 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3995 "src/parser.tab.c"
    break;

  case 105: /* consider_body_statement: break_statement NEWLINE  */
#line 785 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4001 "src/parser.tab.c"
    break;

  case 106: /* consider_body_statement: continue_statement NEWLINE  */
#line 786 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4007 "src/parser.tab.c"
    break;

  case 107: /* consider_body_statement: if_statement  */
#line 787 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4013 "src/parser.tab.c"
    break;

  case 108: /* consider_body_statement: DIM  */
#line 793 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 4026 "src/parser.tab.c"
    break;

  case 109: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 804 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4034 "src/parser.tab.c"
    break;

  case 110: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 807 "src/parser.y"
                                                                                                            {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4043 "src/parser.tab.c"
    break;

  case 111: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 814 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 4051 "src/parser.tab.c"
    break;

  case 112: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 817 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 4059 "src/parser.tab.c"
    break;

  case 113: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 823 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4067 "src/parser.tab.c"
    break;

  case 114: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 829 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 4075 "src/parser.tab.c"
    break;

  case 115: /* use_statement: USE IDENT  */
#line 835 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4081 "src/parser.tab.c"
    break;

  case 116: /* use_statement: LOAD IDENT  */
#line 836 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4087 "src/parser.tab.c"
    break;

  case 117: /* use_statement: USE STRING  */
#line 837 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4093 "src/parser.tab.c"
    break;

  case 118: /* use_statement: LOAD STRING  */
#line 838 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4099 "src/parser.tab.c"
    break;

  case 119: /* use_statement: USE IDENT IDENT STRING  */
#line 839 "src/parser.y"
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
#line 4120 "src/parser.tab.c"
    break;

  case 120: /* use_statement: LOAD IDENT IDENT STRING  */
#line 855 "src/parser.y"
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
#line 4141 "src/parser.tab.c"
    break;

  case 121: /* modifier_signature: modifier_name  */
#line 874 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 4147 "src/parser.tab.c"
    break;

  case 122: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 875 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 4153 "src/parser.tab.c"
    break;

  case 123: /* modifier_context: IDENT  */
#line 879 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4159 "src/parser.tab.c"
    break;

  case 124: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 883 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4167 "src/parser.tab.c"
    break;

  case 125: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 886 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 4175 "src/parser.tab.c"
    break;

  case 126: /* watch_statement: WATCH IDENT LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 894 "src/parser.y"
                                                                                           {
        (yyval.stmt) = ast_watch((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4183 "src/parser.tab.c"
    break;

  case 127: /* unwatch_statement: UNWATCH expression  */
#line 900 "src/parser.y"
                         { (yyval.stmt) = ast_unwatch((yyvsp[0].expr)); }
#line 4189 "src/parser.tab.c"
    break;

  case 128: /* watch_target_list: watch_target_path  */
#line 904 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4195 "src/parser.tab.c"
    break;

  case 129: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 905 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4201 "src/parser.tab.c"
    break;

  case 130: /* server_statement: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 924 "src/parser.y"
                                                                                             {
        (yyval.stmt) = ast_server((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4209 "src/parser.tab.c"
    break;

  case 131: /* server_statement: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 927 "src/parser.y"
                                                                           {
        (yyval.stmt) = ast_server((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4217 "src/parser.tab.c"
    break;

  case 132: /* server_item_list: %empty  */
#line 933 "src/parser.y"
             { (yyval.server_item_list) = ast_server_item_list_empty(); }
#line 4223 "src/parser.tab.c"
    break;

  case 133: /* server_item_list: server_item_list NEWLINE  */
#line 934 "src/parser.y"
                               { (yyval.server_item_list) = (yyvsp[-1].server_item_list); }
#line 4229 "src/parser.tab.c"
    break;

  case 134: /* server_item_list: server_item_list server_item  */
#line 935 "src/parser.y"
                                   { (yyval.server_item_list) = ast_server_item_list_append((yyvsp[-1].server_item_list), (yyvsp[0].server_item)); }
#line 4235 "src/parser.tab.c"
    break;

  case 135: /* server_item: IDENT server_string_list NEWLINE  */
#line 939 "src/parser.y"
                                       {
        (yyval.server_item) = ast_server_directive((yyvsp[-2].text), (yyvsp[-1].name_list), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 4243 "src/parser.tab.c"
    break;

  case 136: /* server_item: IDENT STRING LPAREN parameter_list_opt RPAREN NEWLINE statement_list END IDENT NEWLINE  */
#line 942 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_handler((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4251 "src/parser.tab.c"
    break;

  case 137: /* server_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 945 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_site((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4259 "src/parser.tab.c"
    break;

  case 138: /* server_item: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 948 "src/parser.y"
                                                                           {
        (yyval.server_item) = ast_server_site((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 4267 "src/parser.tab.c"
    break;

  case 139: /* server_item: ON IDENT NEWLINE statement_list END ON NEWLINE  */
#line 951 "src/parser.y"
                                                     {
        (yyval.server_item) = ast_server_hook((yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 4275 "src/parser.tab.c"
    break;

  case 140: /* server_string_list: STRING  */
#line 957 "src/parser.y"
             { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4281 "src/parser.tab.c"
    break;

  case 141: /* server_string_list: server_string_list COMMA STRING  */
#line 958 "src/parser.y"
                                      { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4287 "src/parser.tab.c"
    break;

  case 142: /* watch_target_path: variable_name  */
#line 962 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 4293 "src/parser.tab.c"
    break;

  case 143: /* watch_target_path: watch_target_path DOT IDENT  */
#line 963 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 4299 "src/parser.tab.c"
    break;

  case 144: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 967 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 4307 "src/parser.tab.c"
    break;

  case 145: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 973 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 4313 "src/parser.tab.c"
    break;

  case 146: /* on_error_statement: ON ERROR_VALUE GOTO NEXT  */
#line 974 "src/parser.y"
                               { (yyval.stmt) = ast_on_error_goto_next(); }
#line 4319 "src/parser.tab.c"
    break;

  case 147: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 975 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 4325 "src/parser.tab.c"
    break;

  case 148: /* on_error_statement: ON IDENT GOTO NEXT  */
#line 976 "src/parser.y"
                         {
        if (!warn_channel_ok(ctx, (yyvsp[-2].text), (yylsp[-2]).first_line, (yylsp[-2]).first_column)) { YYERROR; }
        free((yyvsp[-2].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_NEXT);
      }
#line 4335 "src/parser.tab.c"
    break;

  case 149: /* on_error_statement: ON IDENT GOTO IDENT  */
#line 981 "src/parser.y"
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
#line 4353 "src/parser.tab.c"
    break;

  case 150: /* on_error_statement: ON IDENT STOP  */
#line 994 "src/parser.y"
                    {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_STOP);
      }
#line 4363 "src/parser.tab.c"
    break;

  case 151: /* on_error_statement: ON IDENT PRINT  */
#line 999 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_PRINT);
      }
#line 4373 "src/parser.tab.c"
    break;

  case 152: /* on_error_statement: ON IDENT IDENT  */
#line 1004 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        int mode = warn_mode_word(ctx, (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column);
        if (mode < 0) { free((yyvsp[-1].text)); free((yyvsp[0].text)); YYERROR; }
        free((yyvsp[-1].text)); free((yyvsp[0].text));
        (yyval.stmt) = ast_on_warning(mode);
      }
#line 4385 "src/parser.tab.c"
    break;

  case 153: /* error_statement: ERROR_VALUE expression  */
#line 1014 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 4391 "src/parser.tab.c"
    break;

  case 154: /* return_statement: RETURN  */
#line 1018 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 4397 "src/parser.tab.c"
    break;

  case 155: /* return_statement: RETURN expression  */
#line 1019 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 4403 "src/parser.tab.c"
    break;

  case 156: /* label_statement: variable_name COLON  */
#line 1023 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 4409 "src/parser.tab.c"
    break;

  case 157: /* goto_statement: GOTO variable_name  */
#line 1030 "src/parser.y"
                         { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 4415 "src/parser.tab.c"
    break;

  case 158: /* gosub_statement: GOSUB variable_name  */
#line 1034 "src/parser.y"
                          { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 4421 "src/parser.tab.c"
    break;

  case 159: /* break_statement: BREAK  */
#line 1043 "src/parser.y"
            { (yyval.stmt) = ast_break(NULL); }
#line 4427 "src/parser.tab.c"
    break;

  case 160: /* break_statement: BREAK IDENT  */
#line 1044 "src/parser.y"
                  { (yyval.stmt) = ast_break((yyvsp[0].text)); }
#line 4433 "src/parser.tab.c"
    break;

  case 161: /* continue_statement: CONTINUE  */
#line 1048 "src/parser.y"
               { (yyval.stmt) = ast_continue(NULL); }
#line 4439 "src/parser.tab.c"
    break;

  case 162: /* continue_statement: CONTINUE IDENT  */
#line 1049 "src/parser.y"
                     { (yyval.stmt) = ast_continue((yyvsp[0].text)); }
#line 4445 "src/parser.tab.c"
    break;

  case 163: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1053 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4454 "src/parser.tab.c"
    break;

  case 164: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1057 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4463 "src/parser.tab.c"
    break;

  case 165: /* if_block_tail: END IF NEWLINE  */
#line 1064 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4471 "src/parser.tab.c"
    break;

  case 166: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 1067 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4479 "src/parser.tab.c"
    break;

  case 167: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1070 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4487 "src/parser.tab.c"
    break;

  case 168: /* if_inline_tail: %empty  */
#line 1076 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4495 "src/parser.tab.c"
    break;

  case 169: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 1079 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4503 "src/parser.tab.c"
    break;

  case 170: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1082 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4511 "src/parser.tab.c"
    break;

  case 171: /* inline_statement: assignment  */
#line 1088 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4517 "src/parser.tab.c"
    break;

  case 172: /* inline_statement: print_statement  */
#line 1089 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4523 "src/parser.tab.c"
    break;

  case 173: /* inline_statement: call_statement  */
#line 1090 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4529 "src/parser.tab.c"
    break;

  case 174: /* inline_statement: use_statement  */
#line 1091 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4535 "src/parser.tab.c"
    break;

  case 175: /* inline_statement: on_error_statement  */
#line 1092 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4541 "src/parser.tab.c"
    break;

  case 176: /* inline_statement: error_statement  */
#line 1093 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4547 "src/parser.tab.c"
    break;

  case 177: /* inline_statement: return_statement  */
#line 1094 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4553 "src/parser.tab.c"
    break;

  case 178: /* inline_statement: goto_statement  */
#line 1095 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4559 "src/parser.tab.c"
    break;

  case 179: /* inline_statement: gosub_statement  */
#line 1096 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4565 "src/parser.tab.c"
    break;

  case 180: /* inline_statement: break_statement  */
#line 1097 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4571 "src/parser.tab.c"
    break;

  case 181: /* inline_statement: continue_statement  */
#line 1098 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4577 "src/parser.tab.c"
    break;

  case 182: /* expression: or_expression  */
#line 1102 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 4583 "src/parser.tab.c"
    break;

  case 183: /* or_expression: and_expression  */
#line 1106 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 4589 "src/parser.tab.c"
    break;

  case 184: /* or_expression: or_expression OR and_expression  */
#line 1107 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4595 "src/parser.tab.c"
    break;

  case 185: /* and_expression: comparison_expression  */
#line 1111 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 4601 "src/parser.tab.c"
    break;

  case 186: /* and_expression: and_expression AND comparison_expression  */
#line 1112 "src/parser.y"
                                               { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4607 "src/parser.tab.c"
    break;

  case 187: /* comparison_expression: additive_expression  */
#line 1116 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 4613 "src/parser.tab.c"
    break;

  case 188: /* comparison_expression: additive_expression comparison_operator additive_expression  */
#line 1117 "src/parser.y"
                                                                  { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4619 "src/parser.tab.c"
    break;

  case 189: /* comparison_expression: additive_expression comparison_lens comparison_operator additive_expression  */
#line 1118 "src/parser.y"
                                                                                  {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 4627 "src/parser.tab.c"
    break;

  case 190: /* additive_expression: multiplicative_expression  */
#line 1124 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 4633 "src/parser.tab.c"
    break;

  case 191: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 1125 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4639 "src/parser.tab.c"
    break;

  case 192: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 1126 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4645 "src/parser.tab.c"
    break;

  case 193: /* multiplicative_expression: unary_expression  */
#line 1130 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 4651 "src/parser.tab.c"
    break;

  case 194: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 1131 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4657 "src/parser.tab.c"
    break;

  case 195: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 1132 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4663 "src/parser.tab.c"
    break;

  case 196: /* unary_expression: postfix_expression  */
#line 1136 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 4669 "src/parser.tab.c"
    break;

  case 197: /* unary_expression: NOT unary_expression  */
#line 1137 "src/parser.y"
                           { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4675 "src/parser.tab.c"
    break;

  case 198: /* unary_expression: MINUS unary_expression  */
#line 1138 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4681 "src/parser.tab.c"
    break;

  case 199: /* unary_expression: NEW postfix_expression  */
#line 1139 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4687 "src/parser.tab.c"
    break;

  case 200: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 1140 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 4693 "src/parser.tab.c"
    break;

  case 201: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 1141 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4699 "src/parser.tab.c"
    break;

  case 202: /* postfix_expression: primary  */
#line 1145 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 4705 "src/parser.tab.c"
    break;

  case 203: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 1146 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4711 "src/parser.tab.c"
    break;

  case 204: /* postfix_expression: postfix_expression DOT dot_field_name  */
#line 1147 "src/parser.y"
                                            { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4717 "src/parser.tab.c"
    break;

  case 205: /* postfix_expression: postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1148 "src/parser.y"
                                                                   {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        (yyval.expr) = expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 4727 "src/parser.tab.c"
    break;

  case 206: /* postfix_expression: postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1153 "src/parser.y"
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
#line 4743 "src/parser.tab.c"
    break;

  case 207: /* comparison_operator: OP_EQ  */
#line 1167 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 4749 "src/parser.tab.c"
    break;

  case 208: /* comparison_operator: OP_NE  */
#line 1168 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 4755 "src/parser.tab.c"
    break;

  case 209: /* comparison_operator: OP_GT  */
#line 1169 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 4761 "src/parser.tab.c"
    break;

  case 210: /* comparison_operator: OP_LT  */
#line 1170 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 4767 "src/parser.tab.c"
    break;

  case 211: /* comparison_operator: OP_GE  */
#line 1171 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 4773 "src/parser.tab.c"
    break;

  case 212: /* comparison_operator: OP_LE  */
#line 1172 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 4779 "src/parser.tab.c"
    break;

  case 213: /* comparison_operator: OP_NGT  */
#line 1173 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 4785 "src/parser.tab.c"
    break;

  case 214: /* comparison_operator: OP_NLT  */
#line 1174 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 4791 "src/parser.tab.c"
    break;

  case 215: /* comparison_operator: OP_NGE  */
#line 1175 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 4797 "src/parser.tab.c"
    break;

  case 216: /* comparison_operator: OP_NLE  */
#line 1176 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 4803 "src/parser.tab.c"
    break;

  case 217: /* primary: NUMBER  */
#line 1180 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4809 "src/parser.tab.c"
    break;

  case 218: /* primary: WATCHERS LPAREN RPAREN  */
#line 1181 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_call(copy_const("watchers"), ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4815 "src/parser.tab.c"
    break;

  case 219: /* primary: duration_terms  */
#line 1182 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4821 "src/parser.tab.c"
    break;

  case 220: /* primary: STRING  */
#line 1183 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4827 "src/parser.tab.c"
    break;

  case 221: /* primary: variable_name ident_suffix  */
#line 1184 "src/parser.y"
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
#line 4849 "src/parser.tab.c"
    break;

  case 222: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1201 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 4860 "src/parser.tab.c"
    break;

  case 223: /* primary: ERROR_VALUE  */
#line 1207 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4866 "src/parser.tab.c"
    break;

  case 224: /* primary: TRUE  */
#line 1208 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4872 "src/parser.tab.c"
    break;

  case 225: /* primary: FALSE  */
#line 1209 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4878 "src/parser.tab.c"
    break;

  case 226: /* primary: NOTHING  */
#line 1210 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4884 "src/parser.tab.c"
    break;

  case 227: /* primary: UNKNOWN_VALUE  */
#line 1211 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4890 "src/parser.tab.c"
    break;

  case 228: /* primary: LPAREN expression RPAREN  */
#line 1212 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 4896 "src/parser.tab.c"
    break;

  case 229: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 1213 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4902 "src/parser.tab.c"
    break;

  case 230: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 1214 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4908 "src/parser.tab.c"
    break;

  case 231: /* primary: record_literal  */
#line 1215 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 4914 "src/parser.tab.c"
    break;

  case 232: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 1219 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4920 "src/parser.tab.c"
    break;

  case 233: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 1220 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4926 "src/parser.tab.c"
    break;

  case 234: /* ident_suffix: %empty  */
#line 1224 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 4936 "src/parser.tab.c"
    break;

  case 235: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 1229 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 4946 "src/parser.tab.c"
    break;

  case 236: /* ident_suffix: DOT dot_field_name ident_dot_suffix  */
#line 1234 "src/parser.y"
                                          {
        /* dot_field_name, not IDENT: a keyword is a legal FIELD name after a
         * dot, because nothing but a name can appear there. */
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 4957 "src/parser.tab.c"
    break;

  case 237: /* ident_suffix: DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1240 "src/parser.y"
                                                          {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        (yyval.ident_suffix).kind = IDENT_SUFFIX_METHOD;
        (yyval.ident_suffix).name = (yyvsp[-3].text);
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 4971 "src/parser.tab.c"
    break;

  case 238: /* ident_dot_suffix: %empty  */
#line 1252 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 4981 "src/parser.tab.c"
    break;

  case 239: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 1257 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 4991 "src/parser.tab.c"
    break;

  case 240: /* duration_terms: NUMBER IDENT  */
#line 1265 "src/parser.y"
                   {
        AstDuration duration = {0};
        (yyval.duration) = duration_add_unit(duration, (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 5000 "src/parser.tab.c"
    break;

  case 241: /* duration_terms: duration_terms NUMBER IDENT  */
#line 1269 "src/parser.y"
                                  {
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 5008 "src/parser.tab.c"
    break;

  case 242: /* argument_list_opt: %empty  */
#line 1275 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 5014 "src/parser.tab.c"
    break;

  case 243: /* argument_list_opt: argument_list  */
#line 1276 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 5020 "src/parser.tab.c"
    break;

  case 244: /* argument_list: expression  */
#line 1280 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 5026 "src/parser.tab.c"
    break;

  case 245: /* argument_list: argument_list COMMA expression  */
#line 1281 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 5032 "src/parser.tab.c"
    break;

  case 246: /* array_argument_list: expression  */
#line 1285 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 5038 "src/parser.tab.c"
    break;

  case 247: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 1286 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 5044 "src/parser.tab.c"
    break;

  case 248: /* parameter_list_opt: %empty  */
#line 1290 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 5050 "src/parser.tab.c"
    break;

  case 249: /* parameter_list_opt: parameter_list  */
#line 1291 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 5056 "src/parser.tab.c"
    break;

  case 250: /* parameter_list: IDENT  */
#line 1295 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5062 "src/parser.tab.c"
    break;

  case 251: /* parameter_list: parameter_list COMMA IDENT  */
#line 1296 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5068 "src/parser.tab.c"
    break;

  case 252: /* field_name: dot_field_name  */
#line 1309 "src/parser.y"
                     { (yyval.text) = (yyvsp[0].text); }
#line 5074 "src/parser.tab.c"
    break;

  case 253: /* field_name: STRING  */
#line 1316 "src/parser.y"
             { (yyval.text) = (yyvsp[0].text); }
#line 5080 "src/parser.tab.c"
    break;

  case 254: /* dot_field_name: IDENT  */
#line 1325 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 5086 "src/parser.tab.c"
    break;

  case 255: /* dot_field_name: AS  */
#line 1326 "src/parser.y"
                     { (yyval.text) = kw_name("as"); }
#line 5092 "src/parser.tab.c"
    break;

  case 256: /* dot_field_name: NEXT  */
#line 1327 "src/parser.y"
                     { (yyval.text) = kw_name("next"); }
#line 5098 "src/parser.tab.c"
    break;

  case 257: /* dot_field_name: STOP  */
#line 1328 "src/parser.y"
                     { (yyval.text) = kw_name("stop"); }
#line 5104 "src/parser.tab.c"
    break;

  case 258: /* dot_field_name: ERROR_VALUE  */
#line 1329 "src/parser.y"
                     { (yyval.text) = kw_name("error"); }
#line 5110 "src/parser.tab.c"
    break;

  case 259: /* dot_field_name: END  */
#line 1330 "src/parser.y"
                     { (yyval.text) = kw_name("end"); }
#line 5116 "src/parser.tab.c"
    break;

  case 260: /* dot_field_name: TO  */
#line 1331 "src/parser.y"
                     { (yyval.text) = kw_name("to"); }
#line 5122 "src/parser.tab.c"
    break;

  case 261: /* dot_field_name: IN  */
#line 1332 "src/parser.y"
                     { (yyval.text) = kw_name("in"); }
#line 5128 "src/parser.tab.c"
    break;

  case 262: /* dot_field_name: ON  */
#line 1333 "src/parser.y"
                     { (yyval.text) = kw_name("on"); }
#line 5134 "src/parser.tab.c"
    break;

  case 263: /* dot_field_name: NEW  */
#line 1334 "src/parser.y"
                     { (yyval.text) = kw_name("new"); }
#line 5140 "src/parser.tab.c"
    break;

  case 264: /* dot_field_name: EACH  */
#line 1335 "src/parser.y"
                     { (yyval.text) = kw_name("each"); }
#line 5146 "src/parser.tab.c"
    break;

  case 265: /* dot_field_name: WITH  */
#line 1336 "src/parser.y"
                     { (yyval.text) = kw_name("with"); }
#line 5152 "src/parser.tab.c"
    break;

  case 266: /* dot_field_name: WITHOUT  */
#line 1337 "src/parser.y"
                     { (yyval.text) = kw_name("without"); }
#line 5158 "src/parser.tab.c"
    break;

  case 267: /* dot_field_name: THEN  */
#line 1338 "src/parser.y"
                     { (yyval.text) = kw_name("then"); }
#line 5164 "src/parser.tab.c"
    break;

  case 268: /* dot_field_name: ELSE  */
#line 1339 "src/parser.y"
                     { (yyval.text) = kw_name("else"); }
#line 5170 "src/parser.tab.c"
    break;

  case 269: /* dot_field_name: FOR  */
#line 1340 "src/parser.y"
                     { (yyval.text) = kw_name("for"); }
#line 5176 "src/parser.tab.c"
    break;

  case 270: /* dot_field_name: IF  */
#line 1341 "src/parser.y"
                     { (yyval.text) = kw_name("if"); }
#line 5182 "src/parser.tab.c"
    break;

  case 271: /* dot_field_name: WHILE  */
#line 1342 "src/parser.y"
                     { (yyval.text) = kw_name("while"); }
#line 5188 "src/parser.tab.c"
    break;

  case 272: /* dot_field_name: DO  */
#line 1343 "src/parser.y"
                     { (yyval.text) = kw_name("do"); }
#line 5194 "src/parser.tab.c"
    break;

  case 273: /* dot_field_name: LOOP  */
#line 1344 "src/parser.y"
                     { (yyval.text) = kw_name("loop"); }
#line 5200 "src/parser.tab.c"
    break;

  case 274: /* dot_field_name: UNTIL  */
#line 1345 "src/parser.y"
                     { (yyval.text) = kw_name("until"); }
#line 5206 "src/parser.tab.c"
    break;

  case 275: /* dot_field_name: PRINT  */
#line 1346 "src/parser.y"
                     { (yyval.text) = kw_name("print"); }
#line 5212 "src/parser.tab.c"
    break;

  case 276: /* dot_field_name: RETURN  */
#line 1347 "src/parser.y"
                     { (yyval.text) = kw_name("return"); }
#line 5218 "src/parser.tab.c"
    break;

  case 277: /* dot_field_name: LOAD  */
#line 1348 "src/parser.y"
                     { (yyval.text) = kw_name("load"); }
#line 5224 "src/parser.tab.c"
    break;

  case 278: /* dot_field_name: USE  */
#line 1349 "src/parser.y"
                     { (yyval.text) = kw_name("use"); }
#line 5230 "src/parser.tab.c"
    break;

  case 279: /* dot_field_name: NOT  */
#line 1350 "src/parser.y"
                     { (yyval.text) = kw_name("not"); }
#line 5236 "src/parser.tab.c"
    break;

  case 280: /* dot_field_name: AND  */
#line 1351 "src/parser.y"
                     { (yyval.text) = kw_name("and"); }
#line 5242 "src/parser.tab.c"
    break;

  case 281: /* dot_field_name: OR  */
#line 1352 "src/parser.y"
                     { (yyval.text) = kw_name("or"); }
#line 5248 "src/parser.tab.c"
    break;

  case 282: /* dot_field_name: TRUE  */
#line 1353 "src/parser.y"
                     { (yyval.text) = kw_name("true"); }
#line 5254 "src/parser.tab.c"
    break;

  case 283: /* dot_field_name: FALSE  */
#line 1354 "src/parser.y"
                     { (yyval.text) = kw_name("false"); }
#line 5260 "src/parser.tab.c"
    break;

  case 284: /* dot_field_name: NOTHING  */
#line 1355 "src/parser.y"
                     { (yyval.text) = kw_name("nothing"); }
#line 5266 "src/parser.tab.c"
    break;

  case 285: /* dot_field_name: BREAK  */
#line 1356 "src/parser.y"
                     { (yyval.text) = kw_name("break"); }
#line 5272 "src/parser.tab.c"
    break;

  case 286: /* dot_field_name: CONTINUE  */
#line 1357 "src/parser.y"
                     { (yyval.text) = kw_name("continue"); }
#line 5278 "src/parser.tab.c"
    break;

  case 287: /* dot_field_name: GOTO  */
#line 1358 "src/parser.y"
                     { (yyval.text) = kw_name("goto"); }
#line 5284 "src/parser.tab.c"
    break;

  case 288: /* dot_field_name: GOSUB  */
#line 1359 "src/parser.y"
                     { (yyval.text) = kw_name("gosub"); }
#line 5290 "src/parser.tab.c"
    break;

  case 289: /* dot_field_name: SPAWN  */
#line 1360 "src/parser.y"
                     { (yyval.text) = kw_name("spawn"); }
#line 5296 "src/parser.tab.c"
    break;

  case 290: /* dot_field_name: EXPORT  */
#line 1361 "src/parser.y"
                     { (yyval.text) = kw_name("export"); }
#line 5302 "src/parser.tab.c"
    break;

  case 291: /* dot_field_name: LIBRARY  */
#line 1362 "src/parser.y"
                     { (yyval.text) = kw_name("library"); }
#line 5308 "src/parser.tab.c"
    break;

  case 292: /* dot_field_name: FUNCTION  */
#line 1363 "src/parser.y"
                     { (yyval.text) = kw_name("function"); }
#line 5314 "src/parser.tab.c"
    break;

  case 293: /* dot_field_name: MODIFIER  */
#line 1364 "src/parser.y"
                     { (yyval.text) = kw_name("modifier"); }
#line 5320 "src/parser.tab.c"
    break;

  case 294: /* dot_field_name: PROGRAM  */
#line 1365 "src/parser.y"
                     { (yyval.text) = kw_name("program"); }
#line 5326 "src/parser.tab.c"
    break;

  case 295: /* dot_field_name: WATCH  */
#line 1366 "src/parser.y"
                     { (yyval.text) = kw_name("watch"); }
#line 5332 "src/parser.tab.c"
    break;

  case 296: /* dot_field_name: WATCHERS  */
#line 1367 "src/parser.y"
                     { (yyval.text) = kw_name("watchers"); }
#line 5338 "src/parser.tab.c"
    break;

  case 297: /* dot_field_name: CONSIDER  */
#line 1368 "src/parser.y"
                     { (yyval.text) = kw_name("consider"); }
#line 5344 "src/parser.tab.c"
    break;

  case 298: /* dot_field_name: STEP  */
#line 1369 "src/parser.y"
                     { (yyval.text) = kw_name("step"); }
#line 5350 "src/parser.tab.c"
    break;

  case 299: /* dot_field_name: UNWATCH  */
#line 1370 "src/parser.y"
                     { (yyval.text) = kw_name("unwatch"); }
#line 5356 "src/parser.tab.c"
    break;

  case 300: /* dot_field_name: UNKNOWN_VALUE  */
#line 1371 "src/parser.y"
                     { (yyval.text) = kw_name("unknown"); }
#line 5362 "src/parser.tab.c"
    break;

  case 301: /* dot_field_name: DIM  */
#line 1372 "src/parser.y"
                     { (yyval.text) = kw_name("dim"); }
#line 5368 "src/parser.tab.c"
    break;

  case 302: /* record_field_list: field_name OP_EQ expression  */
#line 1376 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5374 "src/parser.tab.c"
    break;

  case 303: /* record_field_list: field_name COLON expression  */
#line 1377 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5380 "src/parser.tab.c"
    break;

  case 304: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1378 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5386 "src/parser.tab.c"
    break;

  case 305: /* record_field_list: record_field_list COMMA optional_newlines field_name OP_EQ expression  */
#line 1379 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5392 "src/parser.tab.c"
    break;

  case 306: /* record_field_list: record_field_list COMMA optional_newlines field_name COLON expression  */
#line 1380 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5398 "src/parser.tab.c"
    break;

  case 307: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1381 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5404 "src/parser.tab.c"
    break;

  case 308: /* field_policy: IDENT  */
#line 1389 "src/parser.y"
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
#line 5436 "src/parser.tab.c"
    break;

  case 309: /* field_policy: IDENT expression  */
#line 1416 "src/parser.y"
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
#line 5457 "src/parser.tab.c"
    break;


#line 5461 "src/parser.tab.c"

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

#line 1439 "src/parser.y"


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
    case TOKEN_PLUS_EQ: return PLUS_EQ;
    case TOKEN_MINUS_EQ: return MINUS_EQ;
    case TOKEN_STAR_EQ: return STAR_EQ;
    case TOKEN_SLASH_EQ: return SLASH_EQ;
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
        /* `dim` is lexed as a keyword for ONE reason: to be refused with advice
         * where someone arriving from QBasic would type it. There is no dim
         * statement -- assignment creates a variable -- and reserving the word
         * to say so is worth more than freeing it, because as an ordinary
         * identifier `dim x` would still fail, just less usefully.
         *
         * THE REFUSAL USED TO HAPPEN HERE, at token delivery, which fired it in
         * every position rather than the one it was written for: `{ dim: 7 }`
         * and `r.dim` were both rejected as "not a gBASIC statement" at a
         * position where no statement is possible. Every other keyword is a
         * legal field name (see dot_field_name) and `dim` was the sole
         * exception -- nothing chose that. The token is delivered now and the
         * grammar decides, which is the difference between asking WHAT the
         * word was and asking WHERE it appeared. */
        return DIM;
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
