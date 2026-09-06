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
  YYSYMBOL_UNTIL = 36,                     /* UNTIL  */
  YYSYMBOL_IN = 37,                        /* IN  */
  YYSYMBOL_EACH = 38,                      /* EACH  */
  YYSYMBOL_WHILE = 39,                     /* WHILE  */
  YYSYMBOL_CONSIDER = 40,                  /* CONSIDER  */
  YYSYMBOL_BREAK = 41,                     /* BREAK  */
  YYSYMBOL_CONTINUE = 42,                  /* CONTINUE  */
  YYSYMBOL_FUNCTION = 43,                  /* FUNCTION  */
  YYSYMBOL_RETURN = 44,                    /* RETURN  */
  YYSYMBOL_GOTO = 45,                      /* GOTO  */
  YYSYMBOL_GOSUB = 46,                     /* GOSUB  */
  YYSYMBOL_WATCH = 47,                     /* WATCH  */
  YYSYMBOL_UNWATCH = 48,                   /* UNWATCH  */
  YYSYMBOL_WITHOUT = 49,                   /* WITHOUT  */
  YYSYMBOL_WATCHERS = 50,                  /* WATCHERS  */
  YYSYMBOL_ON = 51,                        /* ON  */
  YYSYMBOL_NEXT = 52,                      /* NEXT  */
  YYSYMBOL_STOP = 53,                      /* STOP  */
  YYSYMBOL_ERROR_VALUE = 54,               /* ERROR_VALUE  */
  YYSYMBOL_MODIFIER = 55,                  /* MODIFIER  */
  YYSYMBOL_PROGRAM = 56,                   /* PROGRAM  */
  YYSYMBOL_LIBRARY = 57,                   /* LIBRARY  */
  YYSYMBOL_LOAD = 58,                      /* LOAD  */
  YYSYMBOL_USE = 59,                       /* USE  */
  YYSYMBOL_EXPORT = 60,                    /* EXPORT  */
  YYSYMBOL_OP_EQ = 61,                     /* OP_EQ  */
  YYSYMBOL_OP_NE = 62,                     /* OP_NE  */
  YYSYMBOL_OP_GT = 63,                     /* OP_GT  */
  YYSYMBOL_OP_LT = 64,                     /* OP_LT  */
  YYSYMBOL_OP_GE = 65,                     /* OP_GE  */
  YYSYMBOL_OP_LE = 66,                     /* OP_LE  */
  YYSYMBOL_OP_NGT = 67,                    /* OP_NGT  */
  YYSYMBOL_OP_NLT = 68,                    /* OP_NLT  */
  YYSYMBOL_OP_NGE = 69,                    /* OP_NGE  */
  YYSYMBOL_OP_NLE = 70,                    /* OP_NLE  */
  YYSYMBOL_PLUS = 71,                      /* PLUS  */
  YYSYMBOL_MINUS = 72,                     /* MINUS  */
  YYSYMBOL_STAR = 73,                      /* STAR  */
  YYSYMBOL_SLASH = 74,                     /* SLASH  */
  YYSYMBOL_LPAREN = 75,                    /* LPAREN  */
  YYSYMBOL_RPAREN = 76,                    /* RPAREN  */
  YYSYMBOL_LBRACKET = 77,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 78,                  /* RBRACKET  */
  YYSYMBOL_LBRACE = 79,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 80,                    /* RBRACE  */
  YYSYMBOL_COMMA = 81,                     /* COMMA  */
  YYSYMBOL_COLON = 82,                     /* COLON  */
  YYSYMBOL_NEWLINE = 83,                   /* NEWLINE  */
  YYSYMBOL_IF_WITHOUT_ELSE = 84,           /* IF_WITHOUT_ELSE  */
  YYSYMBOL_NO_DOT = 85,                    /* NO_DOT  */
  YYSYMBOL_DOT = 86,                       /* DOT  */
  YYSYMBOL_YYACCEPT = 87,                  /* $accept  */
  YYSYMBOL_program = 88,                   /* program  */
  YYSYMBOL_statement_list = 89,            /* statement_list  */
  YYSYMBOL_statement = 90,                 /* statement  */
  YYSYMBOL_assignment = 91,                /* assignment  */
  YYSYMBOL_compound_op = 92,               /* compound_op  */
  YYSYMBOL_lvalue = 93,                    /* lvalue  */
  YYSYMBOL_variable_name = 94,             /* variable_name  */
  YYSYMBOL_comparison_lens = 95,           /* comparison_lens  */
  YYSYMBOL_96_1 = 96,                      /* $@1  */
  YYSYMBOL_modifier_name = 97,             /* modifier_name  */
  YYSYMBOL_modifier_word = 98,             /* modifier_word  */
  YYSYMBOL_print_statement = 99,           /* print_statement  */
  YYSYMBOL_call_statement = 100,           /* call_statement  */
  YYSYMBOL_with_lock_statement = 101,      /* with_lock_statement  */
  YYSYMBOL_for_end = 102,                  /* for_end  */
  YYSYMBOL_for_each_statement = 103,       /* for_each_statement  */
  YYSYMBOL_do_loop_statement = 104,        /* do_loop_statement  */
  YYSYMBOL_while_statement = 105,          /* while_statement  */
  YYSYMBOL_consider_statement = 106,       /* consider_statement  */
  YYSYMBOL_consider_branch_list = 107,     /* consider_branch_list  */
  YYSYMBOL_consider_else_opt = 108,        /* consider_else_opt  */
  YYSYMBOL_consider_statement_list = 109,  /* consider_statement_list  */
  YYSYMBOL_consider_body_statement = 110,  /* consider_body_statement  */
  YYSYMBOL_function_statement = 111,       /* function_statement  */
  YYSYMBOL_modifier_statement = 112,       /* modifier_statement  */
  YYSYMBOL_program_statement = 113,        /* program_statement  */
  YYSYMBOL_library_statement = 114,        /* library_statement  */
  YYSYMBOL_use_statement = 115,            /* use_statement  */
  YYSYMBOL_modifier_signature = 116,       /* modifier_signature  */
  YYSYMBOL_modifier_context = 117,         /* modifier_context  */
  YYSYMBOL_watch_statement = 118,          /* watch_statement  */
  YYSYMBOL_unwatch_statement = 119,        /* unwatch_statement  */
  YYSYMBOL_watch_target_list = 120,        /* watch_target_list  */
  YYSYMBOL_server_statement = 121,         /* server_statement  */
  YYSYMBOL_server_item_list = 122,         /* server_item_list  */
  YYSYMBOL_server_item = 123,              /* server_item  */
  YYSYMBOL_server_string_list = 124,       /* server_string_list  */
  YYSYMBOL_watch_target_path = 125,        /* watch_target_path  */
  YYSYMBOL_without_watchers_statement = 126, /* without_watchers_statement  */
  YYSYMBOL_on_error_statement = 127,       /* on_error_statement  */
  YYSYMBOL_error_statement = 128,          /* error_statement  */
  YYSYMBOL_return_statement = 129,         /* return_statement  */
  YYSYMBOL_label_statement = 130,          /* label_statement  */
  YYSYMBOL_goto_statement = 131,           /* goto_statement  */
  YYSYMBOL_gosub_statement = 132,          /* gosub_statement  */
  YYSYMBOL_break_statement = 133,          /* break_statement  */
  YYSYMBOL_continue_statement = 134,       /* continue_statement  */
  YYSYMBOL_if_statement = 135,             /* if_statement  */
  YYSYMBOL_if_block_tail = 136,            /* if_block_tail  */
  YYSYMBOL_if_inline_tail = 137,           /* if_inline_tail  */
  YYSYMBOL_inline_statement = 138,         /* inline_statement  */
  YYSYMBOL_expression = 139,               /* expression  */
  YYSYMBOL_or_expression = 140,            /* or_expression  */
  YYSYMBOL_and_expression = 141,           /* and_expression  */
  YYSYMBOL_comparison_expression = 142,    /* comparison_expression  */
  YYSYMBOL_additive_expression = 143,      /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 144, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 145,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 146,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 147,      /* comparison_operator  */
  YYSYMBOL_primary = 148,                  /* primary  */
  YYSYMBOL_record_literal = 149,           /* record_literal  */
  YYSYMBOL_ident_suffix = 150,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 151,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 152,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 153,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 154,            /* argument_list  */
  YYSYMBOL_array_argument_list = 155,      /* array_argument_list  */
  YYSYMBOL_parameter_list_opt = 156,       /* parameter_list_opt  */
  YYSYMBOL_parameter_default = 157,        /* parameter_default  */
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
#define YYLAST   2599

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  87
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  77
/* YYNRULES -- Number of rules.  */
#define YYNRULES  322
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  682

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   341


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
      85,    86
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   501,   501,   505,   506,   507,   511,   512,   513,   514,
     515,   516,   517,   518,   519,   520,   521,   522,   523,   524,
     525,   526,   527,   528,   529,   530,   531,   532,   533,   534,
     535,   536,   542,   553,   558,   559,   571,   583,   584,   585,
     586,   590,   591,   592,   596,   597,   598,   609,   609,   615,
     616,   620,   621,   622,   623,   627,   633,   637,   638,   644,
     649,   658,   671,   700,   701,   702,   706,   710,   717,   721,
     742,   748,   754,   760,   763,   769,   770,   774,   775,   776,
     780,   781,   782,   783,   784,   785,   786,   787,   788,   789,
     790,   791,   792,   793,   794,   795,   796,   797,   798,   799,
     800,   801,   802,   803,   804,   810,   821,   824,   831,   834,
     840,   846,   852,   853,   854,   855,   856,   857,   873,   889,
     910,   911,   915,   919,   922,   930,   936,   940,   941,   960,
     963,   969,   970,   971,   975,   978,   981,   984,   987,   993,
     994,   998,   999,  1003,  1009,  1010,  1011,  1012,  1017,  1030,
    1035,  1040,  1050,  1054,  1055,  1059,  1066,  1070,  1079,  1080,
    1084,  1085,  1089,  1093,  1100,  1103,  1106,  1115,  1125,  1128,
    1131,  1137,  1144,  1154,  1155,  1156,  1157,  1158,  1159,  1160,
    1161,  1162,  1163,  1164,  1168,  1172,  1173,  1177,  1178,  1182,
    1183,  1184,  1190,  1191,  1192,  1196,  1197,  1198,  1202,  1203,
    1204,  1205,  1206,  1207,  1211,  1212,  1213,  1214,  1219,  1233,
    1234,  1235,  1236,  1237,  1238,  1239,  1240,  1241,  1242,  1246,
    1247,  1248,  1249,  1250,  1267,  1273,  1274,  1275,  1276,  1277,
    1278,  1279,  1280,  1281,  1285,  1286,  1290,  1295,  1300,  1306,
    1318,  1323,  1331,  1335,  1341,  1342,  1346,  1347,  1351,  1352,
    1356,  1357,  1371,  1372,  1373,  1374,  1375,  1376,  1377,  1378,
    1382,  1383,  1386,  1387,  1402,  1409,  1418,  1419,  1420,  1421,
    1422,  1423,  1424,  1425,  1426,  1427,  1428,  1429,  1430,  1431,
    1432,  1433,  1434,  1435,  1436,  1437,  1438,  1439,  1440,  1441,
    1442,  1443,  1444,  1445,  1446,  1447,  1448,  1449,  1450,  1451,
    1452,  1453,  1454,  1455,  1456,  1457,  1458,  1459,  1460,  1461,
    1462,  1463,  1464,  1468,  1469,  1470,  1471,  1472,  1473,  1481,
    1508,  1527,  1528
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
  "FOR", "TO", "STEP", "DO", "UNTIL", "IN", "EACH", "WHILE", "CONSIDER",
  "BREAK", "CONTINUE", "FUNCTION", "RETURN", "GOTO", "GOSUB", "WATCH",
  "UNWATCH", "WITHOUT", "WATCHERS", "ON", "NEXT", "STOP", "ERROR_VALUE",
  "MODIFIER", "PROGRAM", "LIBRARY", "LOAD", "USE", "EXPORT", "OP_EQ",
  "OP_NE", "OP_GT", "OP_LT", "OP_GE", "OP_LE", "OP_NGT", "OP_NLT",
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
  "parameter_default", "parameter_list", "field_name", "dot_field_name",
  "record_field_list", "field_policy", "optional_newlines", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-505)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -505,    43,   972,  -505,     8,    18,  -505,  2181,  -505,  2168,
     164,    72,    69,  2181,  2181,   200,   220,   108,  2181,    68,
      68,   142,  2181,    76,    37,  -505,   554,    56,   243,   245,
      75,   175,   197,  -505,  -505,   173,   122,   177,   179,   180,
    -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,   181,
    -505,   182,  -505,  -505,   184,   185,   187,   189,   190,   193,
     194,   196,  -505,   205,  2181,  2181,   256,  -505,  -505,   206,
    -505,  -505,  -505,  -505,  2181,  2245,   279,   209,  -505,  2181,
    2181,  -505,  -505,    13,   270,   262,   265,  -505,   632,   116,
    -505,    74,  -505,  -505,   289,   239,  -505,   219,    41,   291,
    -505,   213,   214,  -505,  -505,   223,   224,  -505,  -505,  -505,
     231,    68,  -505,   131,   221,  -505,   225,   170,   119,   308,
    -505,  -505,  -505,  -505,  -505,   159,  -505,   283,   242,   237,
     114,  -505,   318,  -505,    56,  -505,  -505,  -505,  -505,  -505,
    2181,  2181,  -505,  2425,  2181,   174,  -505,  -505,  -505,  -505,
    -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,  2309,
    -505,   248,   246,   252,  -505,  2181,  -505,    85,   254,   255,
    -505,   257,   594,   736,  2181,  2482,  -505,  2083,  2181,  2181,
    -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,
    2181,  2181,   654,  2181,  2181,  2181,  2181,  2539,   326,  2181,
    2181,  2181,  2181,   297,   858,  -505,   320,   332,   332,    68,
     100,    68,  -505,   336,  -505,  -505,  -505,    16,  -505,    42,
    -505,   266,   332,  -505,   339,   332,  -505,   340,   343,   344,
     321,  -505,   272,   351,   284,   286,  -505,  -505,  -505,  -505,
    -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,
    -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,
    -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,
    -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,
    -505,  -505,  -505,  -505,  2181,  2181,   290,  -505,   287,    46,
    -505,   101,  -505,  2181,  -505,   296,   288,  2181,  -505,  -505,
    -505,  -505,  -505,   285,  -505,   295,   301,  -505,   305,   306,
     309,  -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,
    -505,  -505,  -505,  -505,   302,   265,  -505,   116,   116,  2181,
     125,  -505,  -505,   310,   314,   315,  -505,  -505,  -505,   319,
     311,   369,  2181,  2181,  1029,  2181,   201,   342,   328,   327,
     333,   112,   334,   221,  1086,  -505,  1143,  -505,  -505,  -505,
    -505,  2181,   335,  -505,   338,   346,  1200,   407,  -505,  -505,
     339,  -505,   352,  2181,  2181,  -505,  -505,   412,  -505,  2181,
    2181,   341,  -505,  -505,  -505,  -505,   347,  -505,   117,   156,
    -505,  2181,  2181,  -505,   915,   401,   125,  -505,  2181,  2181,
     354,  -505,  2181,   360,   361,   380,   410,  2181,   363,   429,
     472,   368,   448,   370,   373,  -505,   411,   417,   383,  -505,
    -505,   377,   404,   463,   385,  -505,   398,   402,  2181,   403,
      47,  -505,  -505,  -505,   801,  -505,   660,  -505,  -505,   406,
     408,   199,   466,  -505,  2055,  -505,   409,   413,  -505,  1257,
     -19,  -505,  -505,   415,   416,   471,  -505,   418,  -505,  -505,
    -505,  -505,  -505,  -505,   487,   488,  -505,  -505,   431,  -505,
    -505,  1314,   420,   422,  -505,  1371,  -505,   423,  -505,  -505,
    -505,  -505,  -505,   425,   233,   496,   505,  -505,  -505,    66,
     435,    83,  -505,  -505,  -505,  2181,  -505,   428,   430,  2181,
    -505,   432,  -505,  -505,  1428,   482,    67,  -505,  2181,  -505,
    1257,  -505,  -505,   434,  1485,  -505,  -505,  -505,  1542,   472,
    1599,  1656,   473,  -505,  -505,   467,  1713,  -505,  1770,  2181,
     452,   454,   148,   440,   447,   527,   412,  2181,  2181,   516,
    1827,  -505,  -505,   517,  1884,  -505,   506,   453,  -505,   456,
     458,  1257,  -505,  1485,  -505,  -505,  -505,   462,   464,   465,
    -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,
     469,  -505,   470,  -505,   477,   479,   483,   484,   485,   486,
     491,   492,  -505,   494,  -505,   503,   507,   497,   498,   500,
     495,  -505,  2367,   332,   560,  -505,  -505,  -505,   504,   510,
    -505,  -505,   508,   557,  2111,   569,   509,  -505,  -505,  -505,
    -505,  1485,  -505,  -505,  -505,  -505,  -505,  -505,  -505,  -505,
    -505,  -505,  -505,  -505,  -505,   511,   513,   520,  -505,  -505,
     522,   524,   528,   129,   512,  -505,  1941,  -505,   518,  -505,
     529,  -505,   531,   532,  -505,  1257,  -505,  -505,  -505,  -505,
    -505,  -505,   537,   538,   539,  2181,   915,  -505,   915,   401,
    -505,  -505,    73,  -505,  -505,   540,  -505,  -505,  -505,  -505,
     585,    86,  1998,  -505,   544,   589,   606,  -505,   545,   547,
    -505,  -505
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       3,     0,     2,     1,    44,     0,    32,     0,    45,     0,
       0,     0,     0,     0,     0,   158,   160,     0,   153,     0,
       0,     0,     0,     0,     0,    46,     0,     0,     0,     0,
       0,     0,     0,     4,     5,     0,     0,    41,     0,     0,
       9,    10,    12,    11,    13,    14,    15,    16,    17,     0,
      19,     0,    20,    22,     0,     0,     0,     0,     0,     0,
       0,     0,    31,     0,   244,   244,   219,    44,   222,     0,
     226,   227,   228,   229,     0,     0,     0,     0,   225,     0,
       0,   321,   321,   236,     0,   184,   185,   187,   189,   192,
     195,   198,   204,   233,   221,     0,    55,     0,     0,     0,
       3,     0,     0,   159,   161,     0,     0,   154,   156,   157,
      44,     0,   141,     0,   127,   126,     0,     0,     0,     0,
     152,    51,    53,    52,    54,   120,    49,     0,     0,     0,
     113,   115,   112,   114,     0,     6,    37,    38,    39,    40,
       0,     0,    47,     0,     0,     0,   155,     7,     8,    18,
      21,    23,    24,    25,    26,    27,    28,    29,    30,     0,
     246,     0,   245,     0,   242,   244,   199,   201,     0,     0,
     200,     0,     0,     0,   244,     0,   223,     0,     0,     0,
     209,   210,   211,   212,   213,   214,   215,   216,   217,   218,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     3,     0,   250,   250,     0,
       0,     0,     3,     0,     3,   151,   150,     0,   149,     0,
     146,     0,   250,    50,     0,   250,     3,     0,     0,     0,
       0,    33,     0,     0,   266,     0,   267,   312,   282,   279,
     280,   271,   286,   293,   294,   295,   311,   291,   292,   290,
     277,   275,   300,   281,   272,   309,   284,   285,   273,   276,
     283,   308,   296,   297,   303,   287,   298,   299,   306,   310,
     278,   307,   274,   268,   269,   270,   304,   305,   302,   288,
     289,   301,    43,    34,     0,     0,   266,   265,     0,     0,
     264,     0,    57,     0,    58,     0,     0,   244,   220,   230,
     231,   322,   248,   321,   234,   321,     0,   266,     0,   240,
      44,     3,   173,    41,   174,   175,   176,   177,   178,   179,
     180,   181,   182,   183,     0,   186,   188,   193,   194,     0,
     190,   196,   197,     0,   266,     0,   206,   243,    56,     0,
       0,     0,     0,     0,     0,     0,    75,   260,     0,   251,
       0,     0,     0,   128,     0,   142,     0,   148,   147,   144,
     145,   244,     0,   122,     0,     0,     0,   118,   116,   117,
       0,    42,     0,   244,   244,    36,    35,     0,   131,     0,
       0,     0,   321,   247,   224,   202,     0,   321,     0,     0,
     237,   244,   244,   238,     0,   168,   191,   205,   244,   244,
       0,     3,     0,     0,     0,    45,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     3,    45,    45,     0,   121,
       3,     0,    45,     0,     0,    48,     0,     0,   319,     0,
       0,   313,   314,   131,     0,   203,     0,   232,   235,     0,
       0,     0,    45,   162,     0,   163,     0,     0,     3,     0,
       0,     3,    70,     0,     0,     0,    77,     0,   252,   255,
     256,   257,   258,   259,     0,     0,   261,     3,   262,     3,
       3,     0,     0,     0,    61,     0,     3,     0,   119,     3,
      59,    60,   320,     0,     0,     0,     0,   132,   133,     0,
     266,     0,   249,   239,   241,     0,     3,     0,     0,     0,
       3,     0,   207,   208,     0,    45,    46,    66,     0,     3,
       0,    71,    77,     0,    76,    72,   254,   253,     0,     0,
       0,     0,    45,   124,   143,    45,     0,   111,     0,     0,
       0,   139,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   165,   164,     0,     0,   169,    45,     0,    64,     0,
       0,     0,    67,    73,    77,   105,    78,     0,     0,     0,
      83,    84,    86,    85,    87,    79,    88,    89,    90,    91,
       0,    93,     0,    95,     0,     0,     0,     0,     0,     0,
       0,     0,   104,    45,   263,    45,    45,     0,     0,    45,
      45,   315,     0,   250,     0,   134,   130,     3,     0,     0,
     316,   317,     0,    45,     0,    45,     0,    63,    65,     3,
      68,    74,    80,    81,    82,    92,    94,    96,    97,    98,
      99,   100,   101,   102,   103,     0,     0,     0,   123,   108,
       0,     0,     0,     0,     0,   140,     0,   129,     0,     3,
       0,     3,     0,     0,    62,     0,   106,   107,   125,   110,
     109,   131,     0,     0,    45,     0,     0,   166,     0,   168,
     170,    69,     0,   131,     3,     0,   318,   167,   172,   171,
       0,     0,     0,   138,     0,     0,    45,   137,     0,     0,
     136,   135
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -505,  -505,   -69,  -505,  -175,   490,  -505,    -2,   548,  -505,
    -505,   514,  -170,  -166,  -500,  -504,  -498,  -490,  -488,  -486,
    -505,  -505,  -438,  -505,  -484,  -480,  -472,  -469,  -156,   515,
     267,  -458,  -457,  -106,  -505,  -430,  -505,  -505,   421,  -456,
    -154,  -141,  -140,  -453,  -139,  -128,  -123,  -118,  -452,  -410,
     -21,  -431,    26,  -505,   474,   468,  -189,    64,   -47,   566,
     450,  -505,   349,  -505,  -505,  -505,   -36,  -505,  -505,  -200,
     132,  -505,   216,   -93,  -172,   121,   -73
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    34,    35,   144,    36,    83,   145,   233,
     125,   126,    38,    39,    40,   507,    41,    42,    43,    44,
     346,   409,   514,   565,    45,    46,    47,    48,    49,   127,
     364,    50,    51,   113,    52,   430,   488,   532,   114,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,   443,
     445,   324,   160,    85,    86,    87,    88,    89,    90,    91,
     193,    92,    93,   176,   393,    94,   161,   162,   303,   348,
     466,   349,   289,   290,   291,   429,   172
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      37,   305,   312,   489,   330,   210,   552,   314,   350,   173,
     497,   315,    63,   501,   560,   508,   561,   108,   109,   112,
     357,   316,   362,   317,   562,   365,   563,   166,   564,   163,
     566,   204,   170,    84,   567,    96,   318,   319,   320,   101,
     102,   117,   568,     3,   107,   569,   359,   610,   115,   321,
     282,   484,   120,   560,   322,   561,   571,   572,   573,   323,
     121,   577,   582,   562,   509,   563,   485,   564,   358,   566,
     484,    67,    67,   567,   553,   122,    98,   484,   201,   130,
     131,   568,   309,    64,   569,   535,     8,     8,   174,   123,
     484,   118,   670,    65,   360,   571,   572,   573,   486,   175,
     577,   582,   202,   351,   336,   675,   171,   379,   124,   112,
      99,   560,   105,   561,   296,   106,   611,   486,   227,    25,
      25,   562,   228,   563,   486,   564,   116,   566,   380,   295,
     487,   567,   136,   137,   138,   139,   344,   486,   306,   568,
     396,   661,   569,   354,   537,   356,   110,   331,   332,   487,
     548,   196,   100,   571,   572,   573,   487,   366,   577,   582,
     197,     8,   196,   121,   219,   538,   231,   232,    97,   487,
     283,   197,   220,   642,   215,   313,   352,   381,   122,   132,
     133,   211,   382,   140,   136,   137,   138,   139,   414,   194,
     195,   216,   123,   211,    25,   437,   190,   191,   302,   141,
     301,   142,    37,   310,   103,   652,     5,   112,   143,   112,
     382,   124,   211,   495,   212,   217,   407,   111,     8,   408,
       9,   662,   333,   218,   104,   338,   339,   340,   341,   594,
     388,   595,   389,   671,   222,   284,   438,   530,   531,   301,
      15,    16,   394,    18,    19,    20,   667,   128,   668,   129,
      24,    25,   134,    26,   327,   328,   135,    30,    31,   146,
     164,   386,   147,   148,   149,   150,   312,   151,   152,   312,
     153,   314,   154,   155,   314,   315,   156,   157,   315,   158,
     159,   165,   496,   168,   169,   316,   177,   317,   316,   178,
     317,   179,   198,   199,   200,   203,   205,   206,   207,   208,
     318,   319,   320,   318,   319,   320,   209,   213,   214,   434,
     375,   376,   221,   321,   436,   224,   321,   225,   322,   383,
     226,   322,   229,   323,   292,   418,   323,   293,   294,   297,
     337,   298,   449,   299,   342,   345,   347,   426,   427,   557,
     355,   361,    37,   363,   558,   367,   471,   368,   559,   369,
     371,   475,    37,   370,    37,   439,   440,   372,   570,   373,
     574,   374,   446,   447,    37,   377,   387,    82,   403,   404,
     378,   406,   384,   575,   576,   578,   382,   390,   557,   504,
     391,   392,   510,   558,    64,   395,   579,   559,   397,   398,
     399,   580,    37,   634,   401,   400,   581,   570,   518,   574,
     520,   521,   402,   410,   411,   431,   432,   526,   412,   413,
     528,   419,   575,   576,   578,   423,   428,   415,   444,   453,
     633,   420,   421,   435,   433,   579,   454,   540,   450,   312,
     580,   544,   425,   455,   314,   581,   557,   448,   315,   313,
     551,   558,   313,   451,   452,   559,   456,    37,   316,   457,
     317,   467,   468,   469,   482,   570,   470,   574,   472,   474,
     476,   477,   492,   318,   319,   320,   473,   478,   479,    37,
     575,   576,   578,    37,   480,   458,   321,   459,   481,   483,
     498,   322,   493,   579,   494,   502,   323,   513,   580,   503,
     516,   517,   519,   581,   460,   461,   462,   463,   511,   512,
     533,   515,    37,   523,   549,   524,   527,   529,    37,   534,
     536,   541,    37,   542,   547,   545,    37,   554,    37,    37,
     587,   539,   588,   596,    37,   543,    37,   592,   636,   593,
     597,   598,   602,   604,   550,   606,   607,   625,    37,   608,
     645,   609,    37,   464,   465,   612,   626,   613,   614,    37,
     631,    37,   615,   616,   627,   591,   630,    66,    67,    68,
     617,    69,   618,   600,   601,   635,   619,   620,   621,   622,
     656,   640,   658,     8,   623,   624,    70,    71,    72,    73,
     628,   629,    74,   643,    75,    76,   638,   637,   653,   674,
     665,   639,   644,   678,   646,   672,   647,    66,    67,    68,
     655,    69,   313,   648,    77,   649,    25,   650,    78,    37,
     679,   651,   657,     8,   659,   660,    70,    71,    72,    73,
     663,   664,    74,   673,    75,    76,    79,   677,   680,    80,
     681,    81,   353,    82,    37,   285,   192,   424,   669,   223,
     119,   167,   329,    37,    77,   385,    25,   326,    78,   230,
     491,   584,   325,     0,    37,     0,    37,   599,     0,     0,
       0,     0,     0,    66,    67,    68,    79,    69,     0,    80,
      37,    81,   300,    82,     0,     0,     0,   301,     0,     8,
       0,   666,    70,    71,    72,    73,     0,     0,    74,     0,
      75,    76,     0,   180,   181,   182,   183,   184,   185,   186,
     187,   188,   189,   190,   191,     0,     0,     0,     0,     0,
      77,   142,    25,     0,    78,   180,   181,   182,   183,   184,
     185,   186,   187,   188,   189,     0,     0,     0,     0,     0,
       0,     0,    79,     0,     0,    80,     0,    81,     0,    82,
     286,   287,     0,   301,   236,   237,     0,     0,     0,     0,
     238,     0,   239,   240,     0,   241,     0,   242,   243,   244,
     245,   246,   247,   248,   249,   250,   251,   252,   253,   254,
     255,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,     0,     0,     0,
       0,     0,     0,     0,     0,   490,   287,     0,     0,   236,
     237,     0,     0,     0,     0,   238,   304,   239,   240,   301,
     241,     0,   242,   243,   244,   245,   246,   247,   248,   249,
     250,   251,   252,   253,   254,   255,   256,   257,   258,   259,
     260,   261,   262,   263,   264,   265,   266,   267,   268,   269,
     270,   271,   272,   273,   274,   275,   276,   277,   278,   279,
     280,   281,     4,     0,     0,     5,     0,     6,     0,     0,
       0,     0,     7,     0,     0,     0,     0,     8,     0,     9,
       0,     0,     0,     0,   301,     0,     0,    10,     0,     0,
      11,     0,     0,    12,   343,     0,     0,    13,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,     0,    24,
      25,     0,    26,    27,    28,    29,    30,    31,    32,     4,
       0,     0,     5,     0,     6,     0,     0,     0,     0,     7,
       0,     0,   441,     0,   442,     0,     9,     0,     0,     0,
       0,    33,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     6,     0,     0,     0,     0,     7,     0,     0,     0,
       0,     8,     0,     9,     0,     0,     0,     0,    33,     0,
       0,    10,     0,     0,    11,     0,     0,    12,     0,     0,
       0,    13,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,     0,    24,    25,     0,    26,    27,    28,    29,
      30,    31,    32,     4,     0,     0,     5,     0,     6,     0,
       0,     0,     0,     7,     0,     0,     0,     0,   405,     0,
       9,     0,     0,     0,     0,    33,     0,     0,    10,     0,
       0,    11,     0,     0,    12,     0,     0,     0,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,     0,
      24,    25,     0,    26,    27,    28,    29,    30,    31,    32,
       4,     0,     0,     5,     0,     6,     0,     0,     0,     0,
       7,     0,     0,     0,     0,   416,     0,     9,     0,     0,
       0,     0,    33,     0,     0,    10,     0,     0,    11,     0,
       0,    12,     0,     0,     0,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,     0,    24,    25,     0,
      26,    27,    28,    29,    30,    31,    32,     4,     0,     0,
       5,     0,     6,     0,     0,     0,     0,     7,     0,     0,
       0,     0,   417,     0,     9,     0,     0,     0,     0,    33,
       0,     0,    10,     0,     0,    11,     0,     0,    12,     0,
       0,     0,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,     0,    24,    25,     0,    26,    27,    28,
      29,    30,    31,    32,     4,     0,     0,     5,     0,     6,
       0,     0,     0,     0,     7,     0,     0,     0,     0,   422,
       0,     9,     0,     0,     0,     0,    33,     0,     0,    10,
       0,     0,    11,     0,     0,    12,     0,     0,     0,    13,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
       0,    24,    25,     0,    26,    27,    28,    29,    30,    31,
      32,     4,     0,     0,     5,     0,     6,     0,     0,     0,
       0,     7,     0,     0,     0,     0,   505,     0,     9,     0,
       0,     0,     0,    33,     0,     0,    10,     0,     0,    11,
       0,     0,    12,     0,     0,     0,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,     0,    24,   506,
       0,    26,    27,    28,    29,    30,    31,    32,     4,     0,
       0,     5,     0,     6,     0,     0,     0,     0,     7,     0,
       0,     0,     0,   522,     0,     9,     0,     0,     0,     0,
      33,     0,     0,    10,     0,     0,    11,     0,     0,    12,
       0,     0,     0,    13,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,     0,    24,    25,     0,    26,    27,
      28,    29,    30,    31,    32,     4,     0,     0,     5,     0,
       6,     0,     0,     0,     0,     7,     0,     0,     0,     0,
     525,     0,     9,     0,     0,     0,     0,    33,     0,     0,
      10,     0,     0,    11,     0,     0,    12,     0,     0,     0,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,     0,    24,    25,     0,    26,    27,    28,    29,    30,
      31,    32,     4,     0,     0,     5,     0,     6,     0,     0,
       0,     0,     7,     0,     0,     0,     0,   546,     0,     9,
       0,     0,     0,     0,    33,     0,     0,    10,     0,     0,
      11,     0,     0,    12,     0,     0,     0,    13,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,     0,    24,
      25,     0,    26,    27,    28,    29,    30,    31,    32,   310,
       0,     0,     5,     0,   555,     0,     0,     0,     0,     7,
       0,     0,     0,     0,     8,     0,     9,     0,     0,     0,
       0,    33,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     6,     0,     0,     0,     0,     7,     0,     0,     0,
       0,   583,     0,     9,     0,     0,     0,     0,   556,     0,
       0,    10,     0,     0,    11,     0,     0,    12,     0,     0,
       0,    13,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,     0,    24,    25,     0,    26,    27,    28,    29,
      30,    31,    32,     4,     0,     0,     5,     0,     6,     0,
       0,     0,     0,     7,     0,     0,     0,     0,   585,     0,
       9,     0,     0,     0,     0,    33,     0,     0,    10,     0,
       0,    11,     0,     0,    12,     0,     0,     0,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,     0,
      24,    25,     0,    26,    27,    28,    29,    30,    31,    32,
       4,     0,     0,     5,     0,     6,     0,     0,     0,     0,
       7,     0,     0,     0,     0,   586,     0,     9,     0,     0,
       0,     0,    33,     0,     0,    10,     0,     0,    11,     0,
       0,    12,     0,     0,     0,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,     0,    24,    25,     0,
      26,    27,    28,    29,    30,    31,    32,     4,     0,     0,
       5,     0,     6,     0,     0,     0,     0,     7,     0,     0,
       0,     0,   589,     0,     9,     0,     0,     0,     0,    33,
       0,     0,    10,     0,     0,    11,     0,     0,    12,     0,
       0,     0,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,     0,    24,    25,     0,    26,    27,    28,
      29,    30,    31,    32,     4,     0,     0,     5,     0,     6,
       0,     0,     0,     0,     7,     0,     0,     0,     0,   590,
       0,     9,     0,     0,     0,     0,    33,     0,     0,    10,
       0,     0,    11,     0,     0,    12,     0,     0,     0,    13,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
       0,    24,    25,     0,    26,    27,    28,    29,    30,    31,
      32,     4,     0,     0,     5,     0,     6,     0,     0,     0,
       0,     7,     0,     0,     0,     0,   603,     0,     9,     0,
       0,     0,     0,    33,     0,     0,    10,     0,     0,    11,
       0,     0,    12,     0,     0,     0,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,     0,    24,    25,
       0,    26,    27,    28,    29,    30,    31,    32,     4,     0,
       0,     5,     0,     6,     0,     0,     0,     0,     7,     0,
       0,     0,     0,   605,     0,     9,     0,     0,     0,     0,
      33,     0,     0,    10,     0,     0,    11,     0,     0,    12,
       0,     0,     0,    13,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,     0,    24,    25,     0,    26,    27,
      28,    29,    30,    31,    32,     4,     0,     0,     5,     0,
       6,     0,     0,     0,     0,     7,     0,     0,     0,     0,
     654,     0,     9,     0,     0,     0,     0,    33,     0,     0,
      10,     0,     0,    11,     0,     0,    12,     0,     0,     0,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,     0,    24,    25,     0,    26,    27,    28,    29,    30,
      31,    32,     4,     0,     0,     5,     0,     6,     0,     0,
       0,     0,     7,     0,     0,     0,     0,   676,     0,     9,
       0,     0,     0,     0,    33,     0,     0,    10,     0,     0,
      11,     0,     0,    12,     0,     0,     0,    13,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,     0,    24,
      25,     0,    26,    27,    28,    29,    30,    31,    32,   310,
       0,     0,     5,     0,     0,     0,     0,     0,     0,   499,
       0,     0,     0,     0,     8,     0,     9,     0,     0,     0,
       0,    33,     0,     0,     0,     0,     0,   310,     0,     0,
       5,     0,     0,     0,     0,     0,    15,    16,     0,    18,
      19,    20,     8,     0,     9,     0,    24,    25,     0,    26,
       0,     0,     0,    30,    31,   310,     0,     0,     5,     0,
       0,     0,     0,     0,    15,    16,     0,    18,    19,    20,
       8,     0,     9,     0,    24,    25,     0,    26,   500,     0,
       0,    30,    31,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    15,    16,     0,    18,    19,    20,     0,     0,
       0,     0,    24,    25,     0,    26,   311,     0,     0,    30,
      31,    66,    67,    68,     0,    69,     0,     0,     0,     0,
       0,     0,     0,     0,    66,    67,    68,     8,    69,     0,
      70,    71,    72,    73,   641,     0,    74,     0,    75,    76,
       8,    95,     0,    70,    71,    72,    73,     0,     0,    74,
       0,    75,    76,     0,     0,     0,     0,     0,    77,     0,
      25,     0,    78,     0,     0,     0,     0,     0,     0,     0,
       0,    77,     0,    25,     0,    78,     0,     0,     0,     0,
      79,     0,     0,    80,     0,    81,     0,    82,    66,    67,
      68,     0,    69,    79,     0,     0,    80,     0,    81,     0,
      82,     0,     0,     0,     8,     0,     0,    70,    71,    72,
      73,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    77,     0,    25,     0,    78,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   286,   287,     0,     0,   236,   237,     0,
      80,     0,    81,   238,    82,   239,   240,     0,   241,     0,
     242,   243,   244,   245,   246,   247,   248,   249,   250,   251,
     252,   253,   254,   255,   256,   257,   258,   259,   260,   261,
     262,   263,   264,   265,   266,   267,   268,   269,   270,   271,
     272,   273,   274,   275,   276,   277,   278,   279,   280,   281,
       0,   286,   287,     0,     0,   236,   237,     0,     0,     0,
       0,   238,     0,   239,   240,   288,   241,     0,   242,   243,
     244,   245,   246,   247,   248,   249,   250,   251,   252,   253,
     254,   255,   256,   257,   258,   259,   260,   261,   262,   263,
     264,   265,   266,   267,   268,   269,   270,   271,   272,   273,
     274,   275,   276,   277,   278,   279,   280,   281,     0,   234,
       0,     0,   235,   236,   237,     0,     0,     0,     0,   238,
       0,   239,   240,   632,   241,     0,   242,   243,   244,   245,
     246,   247,   248,   249,   250,   251,   252,   253,   254,   255,
     256,   257,   258,   259,   260,   261,   262,   263,   264,   265,
     266,   267,   268,   269,   270,   271,   272,   273,   274,   275,
     276,   277,   278,   279,   280,   281,   307,     0,     0,   308,
     236,   237,     0,     0,     0,     0,   238,     0,   239,   240,
       0,   241,     0,   242,   243,   244,   245,   246,   247,   248,
     249,   250,   251,   252,   253,   254,   255,   256,   257,   258,
     259,   260,   261,   262,   263,   264,   265,   266,   267,   268,
     269,   270,   271,   272,   273,   274,   275,   276,   277,   278,
     279,   280,   281,   334,     0,     0,   335,   236,   237,     0,
       0,     0,     0,   238,     0,   239,   240,     0,   241,     0,
     242,   243,   244,   245,   246,   247,   248,   249,   250,   251,
     252,   253,   254,   255,   256,   257,   258,   259,   260,   261,
     262,   263,   264,   265,   266,   267,   268,   269,   270,   271,
     272,   273,   274,   275,   276,   277,   278,   279,   280,   281
};

static const yytype_int16 yycheck[] =
{
       2,   173,   177,   433,   193,   111,   510,   177,   208,    82,
     441,   177,     4,   444,   514,    34,   514,    19,    20,    21,
       4,   177,   222,   177,   514,   225,   514,    74,   514,    65,
     514,   100,    79,     7,   514,     9,   177,   177,   177,    13,
      14,     4,   514,     0,    18,   514,     4,   551,    22,   177,
     143,     4,    26,   553,   177,   553,   514,   514,   514,   177,
       4,   514,   514,   553,    83,   553,    19,   553,    52,   553,
       4,     4,     4,   553,   512,    19,     4,     4,    37,     4,
       5,   553,   175,    75,   553,    19,    19,    19,    75,    33,
       4,    54,    19,    75,    52,   553,   553,   553,    51,    86,
     553,   553,    61,   209,   197,    19,    80,    61,    52,   111,
      38,   611,     4,   611,    29,     7,   554,    51,     4,    52,
      52,   611,     8,   611,    51,   611,    50,   611,    82,   165,
      83,   611,    10,    11,    12,    13,   205,    51,   174,   611,
     329,   645,   611,   212,    61,   214,     4,   194,   195,    83,
      83,    77,    83,   611,   611,   611,    83,   226,   611,   611,
      86,    19,    77,     4,    45,    82,   140,   141,     4,    83,
     144,    86,    53,   604,     4,   177,    76,    76,    19,     4,
       5,    81,    81,    61,    10,    11,    12,    13,    76,    73,
      74,    21,    33,    81,    52,    78,    71,    72,   172,    77,
      83,    79,   204,     4,     4,    76,     7,   209,    86,   211,
      81,    52,    81,    14,    83,    45,    15,    75,    19,    18,
      21,   651,   196,    53,     4,   199,   200,   201,   202,    81,
     303,    83,   305,   663,    75,    61,    80,     4,     5,    83,
      41,    42,   311,    44,    45,    46,   656,     4,   658,     4,
      51,    52,    55,    54,   190,   191,    83,    58,    59,    82,
       4,   297,    83,    83,    83,    83,   441,    83,    83,   444,
      83,   441,    83,    83,   444,   441,    83,    83,   444,    83,
      75,    75,    83,     4,    75,   441,    16,   441,   444,    27,
     444,    26,     3,    54,    75,     4,    83,    83,    75,    75,
     441,   441,   441,   444,   444,   444,    75,    86,    83,   382,
     284,   285,     4,   441,   387,    32,   444,    75,   441,   293,
      83,   444,     4,   441,    76,   361,   444,    81,    76,    75,
       4,    76,   401,    76,    37,    15,     4,   373,   374,   514,
       4,    75,   344,     4,   514,     5,   415,     4,   514,     5,
      78,   420,   354,    32,   356,   391,   392,     6,   514,    75,
     514,    75,   398,   399,   366,    75,    81,    79,   342,   343,
      83,   345,    76,   514,   514,   514,    81,    76,   553,   448,
      75,    75,   451,   553,    75,    83,   514,   553,    78,    75,
      75,   514,   394,   593,    83,    76,   514,   553,   467,   553,
     469,   470,    33,    61,    76,   379,   380,   476,    81,    76,
     479,    76,   553,   553,   553,     8,     4,    83,    17,    39,
     592,    83,    76,    76,    83,   553,    16,   496,   402,   604,
     553,   500,    80,   407,   604,   553,   611,    83,   604,   441,
     509,   611,   444,    83,    83,   611,    83,   449,   604,    20,
     604,    83,     4,    83,   428,   611,    83,   611,    47,    76,
      83,    57,   436,   604,   604,   604,    49,     4,    83,   471,
     611,   611,   611,   475,    76,     3,   604,     5,    76,    76,
      14,   604,    76,   611,    76,    76,   604,    16,   611,    76,
       3,     3,    61,   611,    22,    23,    24,    25,    83,    83,
       4,    83,   504,    83,   506,    83,    83,    82,   510,     4,
      75,    83,   514,    83,    32,    83,   518,    83,   520,   521,
      47,   495,    55,    83,   526,   499,   528,    75,   597,    75,
      83,     4,    16,    16,   508,    29,    83,    43,   540,    83,
     609,    83,   544,    71,    72,    83,    43,    83,    83,   551,
      55,   553,    83,    83,    47,   529,    56,     3,     4,     5,
      83,     7,    83,   537,   538,     5,    83,    83,    83,    83,
     639,    14,   641,    19,    83,    83,    22,    23,    24,    25,
      83,    83,    28,    14,    30,    31,    76,    83,    76,     4,
      51,    83,    83,     4,    83,   664,    83,     3,     4,     5,
      82,     7,   604,    83,    50,    83,    52,    83,    54,   611,
       4,    83,    83,    19,    83,    83,    22,    23,    24,    25,
      83,    83,    28,    83,    30,    31,    72,    83,    83,    75,
      83,    77,   211,    79,   636,   145,    88,   370,   659,   125,
      86,    75,   192,   645,    50,   296,    52,   179,    54,   134,
     434,   519,   178,    -1,   656,    -1,   658,   536,    -1,    -1,
      -1,    -1,    -1,     3,     4,     5,    72,     7,    -1,    75,
     672,    77,    78,    79,    -1,    -1,    -1,    83,    -1,    19,
      -1,   655,    22,    23,    24,    25,    -1,    -1,    28,    -1,
      30,    31,    -1,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    -1,    -1,    -1,    -1,    -1,
      50,    79,    52,    -1,    54,    61,    62,    63,    64,    65,
      66,    67,    68,    69,    70,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    72,    -1,    -1,    75,    -1,    77,    -1,    79,
       4,     5,    -1,    83,     8,     9,    -1,    -1,    -1,    -1,
      14,    -1,    16,    17,    -1,    19,    -1,    21,    22,    23,
      24,    25,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,     4,     5,    -1,    -1,     8,
       9,    -1,    -1,    -1,    -1,    14,    80,    16,    17,    83,
      19,    -1,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,     4,    -1,    -1,     7,    -1,     9,    -1,    -1,
      -1,    -1,    14,    -1,    -1,    -1,    -1,    19,    -1,    21,
      -1,    -1,    -1,    -1,    83,    -1,    -1,    29,    -1,    -1,
      32,    -1,    -1,    35,    36,    -1,    -1,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    -1,    51,
      52,    -1,    54,    55,    56,    57,    58,    59,    60,     4,
      -1,    -1,     7,    -1,     9,    -1,    -1,    -1,    -1,    14,
      -1,    -1,    17,    -1,    19,    -1,    21,    -1,    -1,    -1,
      -1,    83,    -1,    -1,    29,    -1,    -1,    32,    -1,    -1,
      35,    -1,    -1,    -1,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    -1,    51,    52,    -1,    54,
      55,    56,    57,    58,    59,    60,     4,    -1,    -1,     7,
      -1,     9,    -1,    -1,    -1,    -1,    14,    -1,    -1,    -1,
      -1,    19,    -1,    21,    -1,    -1,    -1,    -1,    83,    -1,
      -1,    29,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      -1,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    -1,    51,    52,    -1,    54,    55,    56,    57,
      58,    59,    60,     4,    -1,    -1,     7,    -1,     9,    -1,
      -1,    -1,    -1,    14,    -1,    -1,    -1,    -1,    19,    -1,
      21,    -1,    -1,    -1,    -1,    83,    -1,    -1,    29,    -1,
      -1,    32,    -1,    -1,    35,    -1,    -1,    -1,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    -1,
      51,    52,    -1,    54,    55,    56,    57,    58,    59,    60,
       4,    -1,    -1,     7,    -1,     9,    -1,    -1,    -1,    -1,
      14,    -1,    -1,    -1,    -1,    19,    -1,    21,    -1,    -1,
      -1,    -1,    83,    -1,    -1,    29,    -1,    -1,    32,    -1,
      -1,    35,    -1,    -1,    -1,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    -1,    51,    52,    -1,
      54,    55,    56,    57,    58,    59,    60,     4,    -1,    -1,
       7,    -1,     9,    -1,    -1,    -1,    -1,    14,    -1,    -1,
      -1,    -1,    19,    -1,    21,    -1,    -1,    -1,    -1,    83,
      -1,    -1,    29,    -1,    -1,    32,    -1,    -1,    35,    -1,
      -1,    -1,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    -1,    51,    52,    -1,    54,    55,    56,
      57,    58,    59,    60,     4,    -1,    -1,     7,    -1,     9,
      -1,    -1,    -1,    -1,    14,    -1,    -1,    -1,    -1,    19,
      -1,    21,    -1,    -1,    -1,    -1,    83,    -1,    -1,    29,
      -1,    -1,    32,    -1,    -1,    35,    -1,    -1,    -1,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      -1,    51,    52,    -1,    54,    55,    56,    57,    58,    59,
      60,     4,    -1,    -1,     7,    -1,     9,    -1,    -1,    -1,
      -1,    14,    -1,    -1,    -1,    -1,    19,    -1,    21,    -1,
      -1,    -1,    -1,    83,    -1,    -1,    29,    -1,    -1,    32,
      -1,    -1,    35,    -1,    -1,    -1,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    -1,    51,    52,
      -1,    54,    55,    56,    57,    58,    59,    60,     4,    -1,
      -1,     7,    -1,     9,    -1,    -1,    -1,    -1,    14,    -1,
      -1,    -1,    -1,    19,    -1,    21,    -1,    -1,    -1,    -1,
      83,    -1,    -1,    29,    -1,    -1,    32,    -1,    -1,    35,
      -1,    -1,    -1,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    -1,    51,    52,    -1,    54,    55,
      56,    57,    58,    59,    60,     4,    -1,    -1,     7,    -1,
       9,    -1,    -1,    -1,    -1,    14,    -1,    -1,    -1,    -1,
      19,    -1,    21,    -1,    -1,    -1,    -1,    83,    -1,    -1,
      29,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,    -1,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    -1,    51,    52,    -1,    54,    55,    56,    57,    58,
      59,    60,     4,    -1,    -1,     7,    -1,     9,    -1,    -1,
      -1,    -1,    14,    -1,    -1,    -1,    -1,    19,    -1,    21,
      -1,    -1,    -1,    -1,    83,    -1,    -1,    29,    -1,    -1,
      32,    -1,    -1,    35,    -1,    -1,    -1,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    -1,    51,
      52,    -1,    54,    55,    56,    57,    58,    59,    60,     4,
      -1,    -1,     7,    -1,     9,    -1,    -1,    -1,    -1,    14,
      -1,    -1,    -1,    -1,    19,    -1,    21,    -1,    -1,    -1,
      -1,    83,    -1,    -1,    29,    -1,    -1,    32,    -1,    -1,
      35,    -1,    -1,    -1,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    -1,    51,    52,    -1,    54,
      55,    56,    57,    58,    59,    60,     4,    -1,    -1,     7,
      -1,     9,    -1,    -1,    -1,    -1,    14,    -1,    -1,    -1,
      -1,    19,    -1,    21,    -1,    -1,    -1,    -1,    83,    -1,
      -1,    29,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      -1,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    -1,    51,    52,    -1,    54,    55,    56,    57,
      58,    59,    60,     4,    -1,    -1,     7,    -1,     9,    -1,
      -1,    -1,    -1,    14,    -1,    -1,    -1,    -1,    19,    -1,
      21,    -1,    -1,    -1,    -1,    83,    -1,    -1,    29,    -1,
      -1,    32,    -1,    -1,    35,    -1,    -1,    -1,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    -1,
      51,    52,    -1,    54,    55,    56,    57,    58,    59,    60,
       4,    -1,    -1,     7,    -1,     9,    -1,    -1,    -1,    -1,
      14,    -1,    -1,    -1,    -1,    19,    -1,    21,    -1,    -1,
      -1,    -1,    83,    -1,    -1,    29,    -1,    -1,    32,    -1,
      -1,    35,    -1,    -1,    -1,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    -1,    51,    52,    -1,
      54,    55,    56,    57,    58,    59,    60,     4,    -1,    -1,
       7,    -1,     9,    -1,    -1,    -1,    -1,    14,    -1,    -1,
      -1,    -1,    19,    -1,    21,    -1,    -1,    -1,    -1,    83,
      -1,    -1,    29,    -1,    -1,    32,    -1,    -1,    35,    -1,
      -1,    -1,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    -1,    51,    52,    -1,    54,    55,    56,
      57,    58,    59,    60,     4,    -1,    -1,     7,    -1,     9,
      -1,    -1,    -1,    -1,    14,    -1,    -1,    -1,    -1,    19,
      -1,    21,    -1,    -1,    -1,    -1,    83,    -1,    -1,    29,
      -1,    -1,    32,    -1,    -1,    35,    -1,    -1,    -1,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      -1,    51,    52,    -1,    54,    55,    56,    57,    58,    59,
      60,     4,    -1,    -1,     7,    -1,     9,    -1,    -1,    -1,
      -1,    14,    -1,    -1,    -1,    -1,    19,    -1,    21,    -1,
      -1,    -1,    -1,    83,    -1,    -1,    29,    -1,    -1,    32,
      -1,    -1,    35,    -1,    -1,    -1,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    -1,    51,    52,
      -1,    54,    55,    56,    57,    58,    59,    60,     4,    -1,
      -1,     7,    -1,     9,    -1,    -1,    -1,    -1,    14,    -1,
      -1,    -1,    -1,    19,    -1,    21,    -1,    -1,    -1,    -1,
      83,    -1,    -1,    29,    -1,    -1,    32,    -1,    -1,    35,
      -1,    -1,    -1,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    -1,    51,    52,    -1,    54,    55,
      56,    57,    58,    59,    60,     4,    -1,    -1,     7,    -1,
       9,    -1,    -1,    -1,    -1,    14,    -1,    -1,    -1,    -1,
      19,    -1,    21,    -1,    -1,    -1,    -1,    83,    -1,    -1,
      29,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,    -1,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    -1,    51,    52,    -1,    54,    55,    56,    57,    58,
      59,    60,     4,    -1,    -1,     7,    -1,     9,    -1,    -1,
      -1,    -1,    14,    -1,    -1,    -1,    -1,    19,    -1,    21,
      -1,    -1,    -1,    -1,    83,    -1,    -1,    29,    -1,    -1,
      32,    -1,    -1,    35,    -1,    -1,    -1,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    -1,    51,
      52,    -1,    54,    55,    56,    57,    58,    59,    60,     4,
      -1,    -1,     7,    -1,    -1,    -1,    -1,    -1,    -1,    14,
      -1,    -1,    -1,    -1,    19,    -1,    21,    -1,    -1,    -1,
      -1,    83,    -1,    -1,    -1,    -1,    -1,     4,    -1,    -1,
       7,    -1,    -1,    -1,    -1,    -1,    41,    42,    -1,    44,
      45,    46,    19,    -1,    21,    -1,    51,    52,    -1,    54,
      -1,    -1,    -1,    58,    59,     4,    -1,    -1,     7,    -1,
      -1,    -1,    -1,    -1,    41,    42,    -1,    44,    45,    46,
      19,    -1,    21,    -1,    51,    52,    -1,    54,    83,    -1,
      -1,    58,    59,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    41,    42,    -1,    44,    45,    46,    -1,    -1,
      -1,    -1,    51,    52,    -1,    54,    83,    -1,    -1,    58,
      59,     3,     4,     5,    -1,     7,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,     3,     4,     5,    19,     7,    -1,
      22,    23,    24,    25,    83,    -1,    28,    -1,    30,    31,
      19,    33,    -1,    22,    23,    24,    25,    -1,    -1,    28,
      -1,    30,    31,    -1,    -1,    -1,    -1,    -1,    50,    -1,
      52,    -1,    54,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    50,    -1,    52,    -1,    54,    -1,    -1,    -1,    -1,
      72,    -1,    -1,    75,    -1,    77,    -1,    79,     3,     4,
       5,    -1,     7,    72,    -1,    -1,    75,    -1,    77,    -1,
      79,    -1,    -1,    -1,    19,    -1,    -1,    22,    23,    24,
      25,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    50,    -1,    52,    -1,    54,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,     4,     5,    -1,    -1,     8,     9,    -1,
      75,    -1,    77,    14,    79,    16,    17,    -1,    19,    -1,
      21,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      -1,     4,     5,    -1,    -1,     8,     9,    -1,    -1,    -1,
      -1,    14,    -1,    16,    17,    76,    19,    -1,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    -1,     4,
      -1,    -1,     7,     8,     9,    -1,    -1,    -1,    -1,    14,
      -1,    16,    17,    76,    19,    -1,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,     4,    -1,    -1,     7,
       8,     9,    -1,    -1,    -1,    -1,    14,    -1,    16,    17,
      -1,    19,    -1,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,     4,    -1,    -1,     7,     8,     9,    -1,
      -1,    -1,    -1,    14,    -1,    16,    17,    -1,    19,    -1,
      21,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    88,    89,     0,     4,     7,     9,    14,    19,    21,
      29,    32,    35,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    51,    52,    54,    55,    56,    57,
      58,    59,    60,    83,    90,    91,    93,    94,    99,   100,
     101,   103,   104,   105,   106,   111,   112,   113,   114,   115,
     118,   119,   121,   126,   127,   128,   129,   130,   131,   132,
     133,   134,   135,     4,    75,    75,     3,     4,     5,     7,
      22,    23,    24,    25,    28,    30,    31,    50,    54,    72,
      75,    77,    79,    94,   139,   140,   141,   142,   143,   144,
     145,   146,   148,   149,   152,    33,   139,     4,     4,    38,
      83,   139,   139,     4,     4,     4,     7,   139,    94,    94,
       4,    75,    94,   120,   125,   139,    50,     4,    54,    86,
     139,     4,    19,    33,    52,    97,    98,   116,     4,     4,
       4,     5,     4,     5,    55,    83,    10,    11,    12,    13,
      61,    77,    79,    86,    92,    95,    82,    83,    83,    83,
      83,    83,    83,    83,    83,    83,    83,    83,    83,    75,
     139,   153,   154,   153,     4,    75,   145,   146,     4,    75,
     145,   139,   163,   163,    75,    86,   150,    16,    27,    26,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    95,   147,    73,    74,    77,    86,     3,    54,
      75,    37,    61,     4,    89,    83,    83,    75,    75,    75,
     120,    81,    83,    86,    83,     4,    21,    45,    53,    45,
      53,     4,    75,    98,    32,    75,    83,     4,     8,     4,
     116,   139,   139,    96,     4,     7,     8,     9,    14,    16,
      17,    19,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,   160,   139,    61,    92,     4,     5,    76,   159,
     160,   161,    76,    81,    76,   153,    29,    75,    76,    76,
      78,    83,   139,   155,    80,   161,   153,     4,     7,   160,
       4,    83,    91,    94,    99,   100,   115,   127,   128,   129,
     131,   132,   133,   134,   138,   141,   142,   144,   144,   147,
     143,   145,   145,   139,     4,     7,   160,     4,   139,   139,
     139,   139,    37,    36,    89,    15,   107,     4,   156,   158,
     156,   120,    76,   125,    89,     4,    89,     4,    52,     4,
      52,    75,   156,     4,   117,   156,    89,     5,     4,     5,
      32,    78,     6,    75,    75,   139,   139,    75,    83,    61,
      82,    76,    81,   139,    76,   149,   153,    81,   163,   163,
      76,    75,    75,   151,    89,    83,   143,    78,    75,    75,
      76,    83,    33,   139,   139,    19,   139,    15,    18,   108,
      61,    76,    81,    76,    76,    83,    19,    19,   153,    76,
      83,    76,    19,     8,   117,    80,   153,   153,     4,   162,
     122,   139,   139,    83,   163,    76,   163,    78,    80,   153,
     153,    17,    19,   136,    17,   137,   153,   153,    83,    89,
     139,    83,    83,    39,    16,   139,    83,    20,     3,     5,
      22,    23,    24,    25,    71,    72,   157,    83,     4,    83,
      83,    89,    47,    49,    76,    89,    83,    57,     4,    83,
      76,    76,   139,    76,     4,    19,    51,    83,   123,   122,
       4,   159,   139,    76,    76,    14,    83,   138,    14,    14,
      83,   138,    76,    76,    89,    19,    52,   102,    34,    83,
      89,    83,    83,    16,   109,    83,     3,     3,    89,    61,
      89,    89,    19,    83,    83,    19,    89,    83,    89,    82,
       4,     5,   124,     4,     4,    19,    75,    61,    82,   139,
      89,    83,    83,   139,    89,    83,    19,    32,    83,    94,
     139,    89,   102,   109,    83,     9,    83,    91,    99,   100,
     101,   103,   104,   105,   106,   110,   111,   112,   113,   114,
     115,   118,   119,   126,   127,   128,   129,   130,   131,   132,
     133,   134,   135,    19,   157,    19,    19,    47,    55,    19,
      19,   139,    75,    75,    81,    83,    83,    83,     4,   162,
     139,   139,    16,    19,    16,    19,    29,    83,    83,    83,
     102,   109,    83,    83,    83,    83,    83,    83,    83,    83,
      83,    83,    83,    83,    83,    43,    43,    47,    83,    83,
      56,    55,    76,   161,   156,     5,    89,    83,    76,    83,
      14,    83,   138,    14,    83,    89,    83,    83,    83,    83,
      83,    83,    76,    76,    19,    82,    89,    83,    89,    83,
      83,   102,   122,    83,    83,    51,   139,   136,   136,   137,
      19,   122,    89,    83,     4,    19,    19,    83,     4,     4,
      83,    83
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    87,    88,    89,    89,    89,    90,    90,    90,    90,
      90,    90,    90,    90,    90,    90,    90,    90,    90,    90,
      90,    90,    90,    90,    90,    90,    90,    90,    90,    90,
      90,    90,    90,    91,    91,    91,    91,    92,    92,    92,
      92,    93,    93,    93,    94,    94,    94,    96,    95,    97,
      97,    98,    98,    98,    98,    99,    99,   100,   100,   100,
     100,   100,   101,   102,   102,   102,   103,   103,   103,   103,
     104,   105,   106,   107,   107,   108,   108,   109,   109,   109,
     110,   110,   110,   110,   110,   110,   110,   110,   110,   110,
     110,   110,   110,   110,   110,   110,   110,   110,   110,   110,
     110,   110,   110,   110,   110,   110,   111,   111,   112,   112,
     113,   114,   115,   115,   115,   115,   115,   115,   115,   115,
     116,   116,   117,   118,   118,   118,   119,   120,   120,   121,
     121,   122,   122,   122,   123,   123,   123,   123,   123,   124,
     124,   125,   125,   126,   127,   127,   127,   127,   127,   127,
     127,   127,   128,   129,   129,   130,   131,   132,   133,   133,
     134,   134,   135,   135,   136,   136,   136,   136,   137,   137,
     137,   137,   137,   138,   138,   138,   138,   138,   138,   138,
     138,   138,   138,   138,   139,   140,   140,   141,   141,   142,
     142,   142,   143,   143,   143,   144,   144,   144,   145,   145,
     145,   145,   145,   145,   146,   146,   146,   146,   146,   147,
     147,   147,   147,   147,   147,   147,   147,   147,   147,   148,
     148,   148,   148,   148,   148,   148,   148,   148,   148,   148,
     148,   148,   148,   148,   149,   149,   150,   150,   150,   150,
     151,   151,   152,   152,   153,   153,   154,   154,   155,   155,
     156,   156,   157,   157,   157,   157,   157,   157,   157,   157,
     158,   158,   158,   158,   159,   159,   160,   160,   160,   160,
     160,   160,   160,   160,   160,   160,   160,   160,   160,   160,
     160,   160,   160,   160,   160,   160,   160,   160,   160,   160,
     160,   160,   160,   160,   160,   160,   160,   160,   160,   160,
     160,   160,   160,   160,   160,   160,   160,   160,   160,   160,
     160,   160,   160,   161,   161,   161,   161,   161,   161,   162,
     162,   163,   163
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     2,     1,
       1,     2,     1,     2,     2,     2,     2,     2,     2,     2,
       2,     1,     1,     3,     3,     4,     4,     1,     1,     1,
       1,     1,     4,     3,     1,     1,     1,     0,     4,     1,
       2,     1,     1,     1,     1,     2,     4,     4,     4,     6,
       6,     6,    10,     3,     2,     3,     7,     8,     9,    11,
       6,     7,     7,     5,     6,     0,     3,     0,     2,     2,
       2,     2,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     2,     1,     2,     1,     2,     2,     2,     2,
       2,     2,     2,     2,     1,     1,    10,    10,     9,    10,
      10,     7,     2,     2,     2,     2,     4,     4,     4,     6,
       1,     4,     1,     9,     7,    10,     2,     1,     3,    10,
       9,     0,     2,     2,     3,    10,    10,     9,     7,     1,
       3,     1,     3,     7,     4,     4,     3,     4,     4,     3,
       3,     3,     2,     1,     2,     2,     2,     2,     1,     2,
       1,     2,     6,     6,     3,     3,     6,     7,     0,     3,
       6,     7,     7,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     3,     1,     3,     1,
       3,     4,     1,     3,     3,     1,     3,     3,     1,     2,
       2,     2,     4,     5,     1,     4,     3,     6,     6,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       3,     1,     1,     2,     4,     1,     1,     1,     1,     1,
       3,     3,     5,     1,     3,     5,     0,     3,     3,     5,
       0,     3,     2,     3,     0,     1,     1,     3,     1,     4,
       0,     1,     1,     2,     2,     1,     1,     1,     1,     1,
       1,     3,     3,     5,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     3,     3,     6,     6,     6,     9,     1,
       2,     0,     2
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
#line 473 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2557 "src/parser.tab.c"
        break;

    case YYSYMBOL_STRING: /* STRING  */
#line 473 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2563 "src/parser.tab.c"
        break;

    case YYSYMBOL_LENS_CONTENT: /* LENS_CONTENT  */
#line 473 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2569 "src/parser.tab.c"
        break;

    case YYSYMBOL_QUALIFIED_IDENT: /* QUALIFIED_IDENT  */
#line 473 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2575 "src/parser.tab.c"
        break;

    case YYSYMBOL_program: /* program  */
#line 496 "src/parser.y"
            { (void) ((*yyvaluep).stmt_list); }
#line 2581 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement_list: /* statement_list  */
#line 476 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2587 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement: /* statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2593 "src/parser.tab.c"
        break;

    case YYSYMBOL_assignment: /* assignment  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2599 "src/parser.tab.c"
        break;

    case YYSYMBOL_lvalue: /* lvalue  */
#line 474 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2605 "src/parser.tab.c"
        break;

    case YYSYMBOL_variable_name: /* variable_name  */
#line 473 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2611 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_lens: /* comparison_lens  */
#line 481 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 2617 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_name: /* modifier_name  */
#line 473 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2623 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_word: /* modifier_word  */
#line 473 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2629 "src/parser.tab.c"
        break;

    case YYSYMBOL_print_statement: /* print_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2635 "src/parser.tab.c"
        break;

    case YYSYMBOL_call_statement: /* call_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2641 "src/parser.tab.c"
        break;

    case YYSYMBOL_with_lock_statement: /* with_lock_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2647 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_end: /* for_end  */
#line 473 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2653 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_each_statement: /* for_each_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2659 "src/parser.tab.c"
        break;

    case YYSYMBOL_do_loop_statement: /* do_loop_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2665 "src/parser.tab.c"
        break;

    case YYSYMBOL_while_statement: /* while_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2671 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement: /* consider_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2677 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_branch_list: /* consider_branch_list  */
#line 479 "src/parser.y"
            { ast_free_consider_branch_list(((*yyvaluep).consider_branch_list)); }
#line 2683 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_else_opt: /* consider_else_opt  */
#line 476 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2689 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement_list: /* consider_statement_list  */
#line 476 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2695 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_body_statement: /* consider_body_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2701 "src/parser.tab.c"
        break;

    case YYSYMBOL_function_statement: /* function_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2707 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_statement: /* modifier_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2713 "src/parser.tab.c"
        break;

    case YYSYMBOL_program_statement: /* program_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2719 "src/parser.tab.c"
        break;

    case YYSYMBOL_library_statement: /* library_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2725 "src/parser.tab.c"
        break;

    case YYSYMBOL_use_statement: /* use_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2731 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_signature: /* modifier_signature  */
#line 482 "src/parser.y"
            { ast_free_modifier_signature(((*yyvaluep).modifier_signature)); }
#line 2737 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_context: /* modifier_context  */
#line 473 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2743 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_statement: /* watch_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2749 "src/parser.tab.c"
        break;

    case YYSYMBOL_unwatch_statement: /* unwatch_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2755 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_list: /* watch_target_list  */
#line 480 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2761 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_statement: /* server_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2767 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item_list: /* server_item_list  */
#line 486 "src/parser.y"
            { ast_free_server_item_list(((*yyvaluep).server_item_list)); }
#line 2773 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item: /* server_item  */
#line 485 "src/parser.y"
            { AstServerItemList one = ast_server_item_list_append(ast_server_item_list_empty(), ((*yyvaluep).server_item)); ast_free_server_item_list(one); }
#line 2779 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_string_list: /* server_string_list  */
#line 480 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2785 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_path: /* watch_target_path  */
#line 473 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2791 "src/parser.tab.c"
        break;

    case YYSYMBOL_without_watchers_statement: /* without_watchers_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2797 "src/parser.tab.c"
        break;

    case YYSYMBOL_on_error_statement: /* on_error_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2803 "src/parser.tab.c"
        break;

    case YYSYMBOL_error_statement: /* error_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2809 "src/parser.tab.c"
        break;

    case YYSYMBOL_return_statement: /* return_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2815 "src/parser.tab.c"
        break;

    case YYSYMBOL_label_statement: /* label_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2821 "src/parser.tab.c"
        break;

    case YYSYMBOL_goto_statement: /* goto_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2827 "src/parser.tab.c"
        break;

    case YYSYMBOL_gosub_statement: /* gosub_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2833 "src/parser.tab.c"
        break;

    case YYSYMBOL_break_statement: /* break_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2839 "src/parser.tab.c"
        break;

    case YYSYMBOL_continue_statement: /* continue_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2845 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_statement: /* if_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2851 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_block_tail: /* if_block_tail  */
#line 476 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2857 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_inline_tail: /* if_inline_tail  */
#line 476 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2863 "src/parser.tab.c"
        break;

    case YYSYMBOL_inline_statement: /* inline_statement  */
#line 475 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2869 "src/parser.tab.c"
        break;

    case YYSYMBOL_expression: /* expression  */
#line 474 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2875 "src/parser.tab.c"
        break;

    case YYSYMBOL_or_expression: /* or_expression  */
#line 474 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2881 "src/parser.tab.c"
        break;

    case YYSYMBOL_and_expression: /* and_expression  */
#line 474 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2887 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_expression: /* comparison_expression  */
#line 474 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2893 "src/parser.tab.c"
        break;

    case YYSYMBOL_additive_expression: /* additive_expression  */
#line 474 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2899 "src/parser.tab.c"
        break;

    case YYSYMBOL_multiplicative_expression: /* multiplicative_expression  */
#line 474 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2905 "src/parser.tab.c"
        break;

    case YYSYMBOL_unary_expression: /* unary_expression  */
#line 474 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2911 "src/parser.tab.c"
        break;

    case YYSYMBOL_postfix_expression: /* postfix_expression  */
#line 474 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2917 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_operator: /* comparison_operator  */
#line 473 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2923 "src/parser.tab.c"
        break;

    case YYSYMBOL_primary: /* primary  */
#line 474 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2929 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_literal: /* record_literal  */
#line 474 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2935 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_suffix: /* ident_suffix  */
#line 483 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 2941 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_dot_suffix: /* ident_dot_suffix  */
#line 483 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 2947 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list_opt: /* argument_list_opt  */
#line 477 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 2953 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list: /* argument_list  */
#line 477 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 2959 "src/parser.tab.c"
        break;

    case YYSYMBOL_array_argument_list: /* array_argument_list  */
#line 477 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 2965 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list_opt: /* parameter_list_opt  */
#line 480 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2971 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_default: /* parameter_default  */
#line 474 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2977 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list: /* parameter_list  */
#line 480 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2983 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_name: /* field_name  */
#line 473 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2989 "src/parser.tab.c"
        break;

    case YYSYMBOL_dot_field_name: /* dot_field_name  */
#line 473 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2995 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_field_list: /* record_field_list  */
#line 478 "src/parser.y"
            { ast_free_record_field_list(((*yyvaluep).record_field_list)); }
#line 3001 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_policy: /* field_policy  */
#line 484 "src/parser.y"
            { ast_free_expr(((*yyvaluep).field_policy).reset_expr); }
#line 3007 "src/parser.tab.c"
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
#line 501 "src/parser.y"
                     { ctx->parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3313 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 505 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3319 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 506 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3325 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 507 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3331 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 511 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3337 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 512 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3343 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 513 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3349 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 514 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3355 "src/parser.tab.c"
    break;

  case 10: /* statement: for_each_statement  */
#line 515 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3361 "src/parser.tab.c"
    break;

  case 11: /* statement: while_statement  */
#line 516 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3367 "src/parser.tab.c"
    break;

  case 12: /* statement: do_loop_statement  */
#line 517 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3373 "src/parser.tab.c"
    break;

  case 13: /* statement: consider_statement  */
#line 518 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3379 "src/parser.tab.c"
    break;

  case 14: /* statement: function_statement  */
#line 519 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3385 "src/parser.tab.c"
    break;

  case 15: /* statement: modifier_statement  */
#line 520 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3391 "src/parser.tab.c"
    break;

  case 16: /* statement: program_statement  */
#line 521 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3397 "src/parser.tab.c"
    break;

  case 17: /* statement: library_statement  */
#line 522 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3403 "src/parser.tab.c"
    break;

  case 18: /* statement: use_statement NEWLINE  */
#line 523 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3409 "src/parser.tab.c"
    break;

  case 19: /* statement: watch_statement  */
#line 524 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3415 "src/parser.tab.c"
    break;

  case 20: /* statement: server_statement  */
#line 525 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3421 "src/parser.tab.c"
    break;

  case 21: /* statement: unwatch_statement NEWLINE  */
#line 526 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3427 "src/parser.tab.c"
    break;

  case 22: /* statement: without_watchers_statement  */
#line 527 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3433 "src/parser.tab.c"
    break;

  case 23: /* statement: on_error_statement NEWLINE  */
#line 528 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3439 "src/parser.tab.c"
    break;

  case 24: /* statement: error_statement NEWLINE  */
#line 529 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3445 "src/parser.tab.c"
    break;

  case 25: /* statement: return_statement NEWLINE  */
#line 530 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3451 "src/parser.tab.c"
    break;

  case 26: /* statement: label_statement NEWLINE  */
#line 531 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3457 "src/parser.tab.c"
    break;

  case 27: /* statement: goto_statement NEWLINE  */
#line 532 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3463 "src/parser.tab.c"
    break;

  case 28: /* statement: gosub_statement NEWLINE  */
#line 533 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3469 "src/parser.tab.c"
    break;

  case 29: /* statement: break_statement NEWLINE  */
#line 534 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3475 "src/parser.tab.c"
    break;

  case 30: /* statement: continue_statement NEWLINE  */
#line 535 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3481 "src/parser.tab.c"
    break;

  case 31: /* statement: if_statement  */
#line 536 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3487 "src/parser.tab.c"
    break;

  case 32: /* statement: DIM  */
#line 542 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 3500 "src/parser.tab.c"
    break;

  case 33: /* assignment: lvalue OP_EQ expression  */
#line 553 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 3506 "src/parser.tab.c"
    break;

  case 34: /* assignment: lvalue compound_op expression  */
#line 558 "src/parser.y"
                                    { (yyval.stmt) = ast_assign_op((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr), (yyvsp[-1].op_char)); }
#line 3512 "src/parser.tab.c"
    break;

  case 35: /* assignment: lvalue comparison_lens compound_op expression  */
#line 559 "src/parser.y"
                                                    {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign_op((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr), (yyvsp[-1].op_char));
      }
#line 3526 "src/parser.tab.c"
    break;

  case 36: /* assignment: lvalue comparison_lens OP_EQ expression  */
#line 571 "src/parser.y"
                                              {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 3540 "src/parser.tab.c"
    break;

  case 37: /* compound_op: PLUS_EQ  */
#line 583 "src/parser.y"
               { (yyval.op_char) = '+'; }
#line 3546 "src/parser.tab.c"
    break;

  case 38: /* compound_op: MINUS_EQ  */
#line 584 "src/parser.y"
               { (yyval.op_char) = '-'; }
#line 3552 "src/parser.tab.c"
    break;

  case 39: /* compound_op: STAR_EQ  */
#line 585 "src/parser.y"
               { (yyval.op_char) = '*'; }
#line 3558 "src/parser.tab.c"
    break;

  case 40: /* compound_op: SLASH_EQ  */
#line 586 "src/parser.y"
               { (yyval.op_char) = '/'; }
#line 3564 "src/parser.tab.c"
    break;

  case 41: /* lvalue: variable_name  */
#line 590 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3570 "src/parser.tab.c"
    break;

  case 42: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 591 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3576 "src/parser.tab.c"
    break;

  case 43: /* lvalue: lvalue DOT dot_field_name  */
#line 592 "src/parser.y"
                                             { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3582 "src/parser.tab.c"
    break;

  case 44: /* variable_name: IDENT  */
#line 596 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 3588 "src/parser.tab.c"
    break;

  case 45: /* variable_name: END  */
#line 597 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 3594 "src/parser.tab.c"
    break;

  case 46: /* variable_name: NEXT  */
#line 598 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 3600 "src/parser.tab.c"
    break;

  case 47: /* $@1: %empty  */
#line 609 "src/parser.y"
             { lexer_begin_lens_content(ctx->active_lexer); }
#line 3606 "src/parser.tab.c"
    break;

  case 48: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 609 "src/parser.y"
                                                                                  {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 3614 "src/parser.tab.c"
    break;

  case 49: /* modifier_name: modifier_word  */
#line 615 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 3620 "src/parser.tab.c"
    break;

  case 50: /* modifier_name: modifier_name modifier_word  */
#line 616 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 3626 "src/parser.tab.c"
    break;

  case 51: /* modifier_word: IDENT  */
#line 620 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 3632 "src/parser.tab.c"
    break;

  case 52: /* modifier_word: TO  */
#line 621 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 3638 "src/parser.tab.c"
    break;

  case 53: /* modifier_word: END  */
#line 622 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 3644 "src/parser.tab.c"
    break;

  case 54: /* modifier_word: NEXT  */
#line 623 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 3650 "src/parser.tab.c"
    break;

  case 55: /* print_statement: PRINT expression  */
#line 627 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 3656 "src/parser.tab.c"
    break;

  case 56: /* print_statement: PRINT TO ERROR_VALUE expression  */
#line 633 "src/parser.y"
                                      { (yyval.stmt) = ast_print_error((yyvsp[0].expr)); }
#line 3662 "src/parser.tab.c"
    break;

  case 57: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 637 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 3668 "src/parser.tab.c"
    break;

  case 58: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 638 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 3679 "src/parser.tab.c"
    break;

  case 59: /* call_statement: lvalue DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 644 "src/parser.y"
                                                       {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 3689 "src/parser.tab.c"
    break;

  case 60: /* call_statement: lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 649 "src/parser.y"
                                                                 {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 3703 "src/parser.tab.c"
    break;

  case 61: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 658 "src/parser.y"
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
#line 3718 "src/parser.tab.c"
    break;

  case 62: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 671 "src/parser.y"
                                                                                  {
        /* The opener word is recognised by POSITION, not reserved -- the same
         * technique the `server` block's verbs use -- so `lock` and
         * `principal` both stay ordinary identifiers. A second accepted word
         * is a semantic check, not a grammar change: 0 new conflicts. */
        int is_lock = strcmp((yyvsp[-8].text), "lock") == 0;
        int is_principal = strcmp((yyvsp[-8].text), "principal") == 0;
        if (!is_lock && !is_principal) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "expected lock or principal in a with block");
            free((yyvsp[-8].text));
            (yyvsp[-8].text) = NULL;
            YYERROR;
        }
        free((yyvsp[-8].text));
        (yyval.stmt) = is_lock ? ast_with_lock((yyvsp[-6].expr), (yyvsp[-3].stmt_list)) : ast_with_principal((yyvsp[-6].expr), (yyvsp[-3].stmt_list));
      }
#line 3741 "src/parser.tab.c"
    break;

  case 63: /* for_end: END FOR NEWLINE  */
#line 700 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 3747 "src/parser.tab.c"
    break;

  case 64: /* for_end: NEXT NEWLINE  */
#line 701 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 3753 "src/parser.tab.c"
    break;

  case 65: /* for_end: NEXT variable_name NEWLINE  */
#line 702 "src/parser.y"
                                 { (yyval.text) = (yyvsp[-1].text); }
#line 3759 "src/parser.tab.c"
    break;

  case 66: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list for_end  */
#line 706 "src/parser.y"
                                                             {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3768 "src/parser.tab.c"
    break;

  case 67: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list for_end  */
#line 710 "src/parser.y"
                                                                  {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3777 "src/parser.tab.c"
    break;

  case 68: /* for_each_statement: FOR IDENT OP_EQ expression TO expression NEWLINE statement_list for_end  */
#line 717 "src/parser.y"
                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].expr), NULL, (yyvsp[-1].stmt_list));
      }
#line 3786 "src/parser.tab.c"
    break;

  case 69: /* for_each_statement: FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list for_end  */
#line 721 "src/parser.y"
                                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-9].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-9].text), (yyvsp[-7].expr), (yyvsp[-5].expr), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3795 "src/parser.tab.c"
    break;

  case 70: /* do_loop_statement: DO NEWLINE statement_list UNTIL expression NEWLINE  */
#line 742 "src/parser.y"
                                                         {
        (yyval.stmt) = ast_do_loop((yyvsp[-3].stmt_list), (yyvsp[-1].expr));
      }
#line 3803 "src/parser.tab.c"
    break;

  case 71: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 748 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 3811 "src/parser.tab.c"
    break;

  case 72: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 754 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 3819 "src/parser.tab.c"
    break;

  case 73: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 760 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 3827 "src/parser.tab.c"
    break;

  case 74: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 763 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 3835 "src/parser.tab.c"
    break;

  case 75: /* consider_else_opt: %empty  */
#line 769 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3841 "src/parser.tab.c"
    break;

  case 76: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 770 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3847 "src/parser.tab.c"
    break;

  case 77: /* consider_statement_list: %empty  */
#line 774 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3853 "src/parser.tab.c"
    break;

  case 78: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 775 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3859 "src/parser.tab.c"
    break;

  case 79: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 776 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3865 "src/parser.tab.c"
    break;

  case 80: /* consider_body_statement: assignment NEWLINE  */
#line 780 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3871 "src/parser.tab.c"
    break;

  case 81: /* consider_body_statement: print_statement NEWLINE  */
#line 781 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3877 "src/parser.tab.c"
    break;

  case 82: /* consider_body_statement: call_statement NEWLINE  */
#line 782 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3883 "src/parser.tab.c"
    break;

  case 83: /* consider_body_statement: with_lock_statement  */
#line 783 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3889 "src/parser.tab.c"
    break;

  case 84: /* consider_body_statement: for_each_statement  */
#line 784 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3895 "src/parser.tab.c"
    break;

  case 85: /* consider_body_statement: while_statement  */
#line 785 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3901 "src/parser.tab.c"
    break;

  case 86: /* consider_body_statement: do_loop_statement  */
#line 786 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3907 "src/parser.tab.c"
    break;

  case 87: /* consider_body_statement: consider_statement  */
#line 787 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3913 "src/parser.tab.c"
    break;

  case 88: /* consider_body_statement: function_statement  */
#line 788 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3919 "src/parser.tab.c"
    break;

  case 89: /* consider_body_statement: modifier_statement  */
#line 789 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3925 "src/parser.tab.c"
    break;

  case 90: /* consider_body_statement: program_statement  */
#line 790 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3931 "src/parser.tab.c"
    break;

  case 91: /* consider_body_statement: library_statement  */
#line 791 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3937 "src/parser.tab.c"
    break;

  case 92: /* consider_body_statement: use_statement NEWLINE  */
#line 792 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3943 "src/parser.tab.c"
    break;

  case 93: /* consider_body_statement: watch_statement  */
#line 793 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3949 "src/parser.tab.c"
    break;

  case 94: /* consider_body_statement: unwatch_statement NEWLINE  */
#line 794 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3955 "src/parser.tab.c"
    break;

  case 95: /* consider_body_statement: without_watchers_statement  */
#line 795 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3961 "src/parser.tab.c"
    break;

  case 96: /* consider_body_statement: on_error_statement NEWLINE  */
#line 796 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3967 "src/parser.tab.c"
    break;

  case 97: /* consider_body_statement: error_statement NEWLINE  */
#line 797 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3973 "src/parser.tab.c"
    break;

  case 98: /* consider_body_statement: return_statement NEWLINE  */
#line 798 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3979 "src/parser.tab.c"
    break;

  case 99: /* consider_body_statement: label_statement NEWLINE  */
#line 799 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3985 "src/parser.tab.c"
    break;

  case 100: /* consider_body_statement: goto_statement NEWLINE  */
#line 800 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3991 "src/parser.tab.c"
    break;

  case 101: /* consider_body_statement: gosub_statement NEWLINE  */
#line 801 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3997 "src/parser.tab.c"
    break;

  case 102: /* consider_body_statement: break_statement NEWLINE  */
#line 802 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4003 "src/parser.tab.c"
    break;

  case 103: /* consider_body_statement: continue_statement NEWLINE  */
#line 803 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4009 "src/parser.tab.c"
    break;

  case 104: /* consider_body_statement: if_statement  */
#line 804 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4015 "src/parser.tab.c"
    break;

  case 105: /* consider_body_statement: DIM  */
#line 810 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 4028 "src/parser.tab.c"
    break;

  case 106: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 821 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4036 "src/parser.tab.c"
    break;

  case 107: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 824 "src/parser.y"
                                                                                                            {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4045 "src/parser.tab.c"
    break;

  case 108: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 831 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 4053 "src/parser.tab.c"
    break;

  case 109: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 834 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 4061 "src/parser.tab.c"
    break;

  case 110: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 840 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4069 "src/parser.tab.c"
    break;

  case 111: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 846 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 4077 "src/parser.tab.c"
    break;

  case 112: /* use_statement: USE IDENT  */
#line 852 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4083 "src/parser.tab.c"
    break;

  case 113: /* use_statement: LOAD IDENT  */
#line 853 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4089 "src/parser.tab.c"
    break;

  case 114: /* use_statement: USE STRING  */
#line 854 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4095 "src/parser.tab.c"
    break;

  case 115: /* use_statement: LOAD STRING  */
#line 855 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4101 "src/parser.tab.c"
    break;

  case 116: /* use_statement: LOAD IDENT AS IDENT  */
#line 856 "src/parser.y"
                          { (yyval.stmt) = ast_use((yyvsp[-2].text), NULL, (yyvsp[0].text)); }
#line 4107 "src/parser.tab.c"
    break;

  case 117: /* use_statement: USE IDENT IDENT STRING  */
#line 857 "src/parser.y"
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
        (yyval.stmt) = ast_use((yyvsp[-2].text), (yyvsp[0].text), NULL);
      }
#line 4128 "src/parser.tab.c"
    break;

  case 118: /* use_statement: LOAD IDENT IDENT STRING  */
#line 873 "src/parser.y"
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
        (yyval.stmt) = ast_use((yyvsp[-2].text), (yyvsp[0].text), NULL);
      }
#line 4149 "src/parser.tab.c"
    break;

  case 119: /* use_statement: LOAD IDENT IDENT STRING AS IDENT  */
#line 889 "src/parser.y"
                                       {
        if (strcmp((yyvsp[-3].text), "from") != 0) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "expected from in load statement");
            free((yyvsp[-4].text));
            free((yyvsp[-3].text));
            free((yyvsp[-2].text));
            free((yyvsp[0].text));
            (yyvsp[-4].text) = NULL;
            (yyvsp[-3].text) = NULL;
            (yyvsp[-2].text) = NULL;
            (yyvsp[0].text) = NULL;
            YYERROR;
        }
        free((yyvsp[-3].text));
        (yyval.stmt) = ast_use((yyvsp[-4].text), (yyvsp[-2].text), (yyvsp[0].text));
      }
#line 4172 "src/parser.tab.c"
    break;

  case 120: /* modifier_signature: modifier_name  */
#line 910 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 4178 "src/parser.tab.c"
    break;

  case 121: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 911 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 4184 "src/parser.tab.c"
    break;

  case 122: /* modifier_context: IDENT  */
#line 915 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4190 "src/parser.tab.c"
    break;

  case 123: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 919 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4198 "src/parser.tab.c"
    break;

  case 124: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 922 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 4206 "src/parser.tab.c"
    break;

  case 125: /* watch_statement: WATCH IDENT LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 930 "src/parser.y"
                                                                                           {
        (yyval.stmt) = ast_watch((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4214 "src/parser.tab.c"
    break;

  case 126: /* unwatch_statement: UNWATCH expression  */
#line 936 "src/parser.y"
                         { (yyval.stmt) = ast_unwatch((yyvsp[0].expr)); }
#line 4220 "src/parser.tab.c"
    break;

  case 127: /* watch_target_list: watch_target_path  */
#line 940 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4226 "src/parser.tab.c"
    break;

  case 128: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 941 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4232 "src/parser.tab.c"
    break;

  case 129: /* server_statement: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 960 "src/parser.y"
                                                                                             {
        (yyval.stmt) = ast_server((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4240 "src/parser.tab.c"
    break;

  case 130: /* server_statement: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 963 "src/parser.y"
                                                                           {
        (yyval.stmt) = ast_server((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4248 "src/parser.tab.c"
    break;

  case 131: /* server_item_list: %empty  */
#line 969 "src/parser.y"
             { (yyval.server_item_list) = ast_server_item_list_empty(); }
#line 4254 "src/parser.tab.c"
    break;

  case 132: /* server_item_list: server_item_list NEWLINE  */
#line 970 "src/parser.y"
                               { (yyval.server_item_list) = (yyvsp[-1].server_item_list); }
#line 4260 "src/parser.tab.c"
    break;

  case 133: /* server_item_list: server_item_list server_item  */
#line 971 "src/parser.y"
                                   { (yyval.server_item_list) = ast_server_item_list_append((yyvsp[-1].server_item_list), (yyvsp[0].server_item)); }
#line 4266 "src/parser.tab.c"
    break;

  case 134: /* server_item: IDENT server_string_list NEWLINE  */
#line 975 "src/parser.y"
                                       {
        (yyval.server_item) = ast_server_directive((yyvsp[-2].text), (yyvsp[-1].name_list), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 4274 "src/parser.tab.c"
    break;

  case 135: /* server_item: IDENT STRING LPAREN parameter_list_opt RPAREN NEWLINE statement_list END IDENT NEWLINE  */
#line 978 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_handler((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4282 "src/parser.tab.c"
    break;

  case 136: /* server_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 981 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_site((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4290 "src/parser.tab.c"
    break;

  case 137: /* server_item: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 984 "src/parser.y"
                                                                           {
        (yyval.server_item) = ast_server_site((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 4298 "src/parser.tab.c"
    break;

  case 138: /* server_item: ON IDENT NEWLINE statement_list END ON NEWLINE  */
#line 987 "src/parser.y"
                                                     {
        (yyval.server_item) = ast_server_hook((yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 4306 "src/parser.tab.c"
    break;

  case 139: /* server_string_list: STRING  */
#line 993 "src/parser.y"
             { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4312 "src/parser.tab.c"
    break;

  case 140: /* server_string_list: server_string_list COMMA STRING  */
#line 994 "src/parser.y"
                                      { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4318 "src/parser.tab.c"
    break;

  case 141: /* watch_target_path: variable_name  */
#line 998 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 4324 "src/parser.tab.c"
    break;

  case 142: /* watch_target_path: watch_target_path DOT IDENT  */
#line 999 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 4330 "src/parser.tab.c"
    break;

  case 143: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 1003 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 4338 "src/parser.tab.c"
    break;

  case 144: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 1009 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 4344 "src/parser.tab.c"
    break;

  case 145: /* on_error_statement: ON ERROR_VALUE GOTO NEXT  */
#line 1010 "src/parser.y"
                               { (yyval.stmt) = ast_on_error_goto_next(); }
#line 4350 "src/parser.tab.c"
    break;

  case 146: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 1011 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 4356 "src/parser.tab.c"
    break;

  case 147: /* on_error_statement: ON IDENT GOTO NEXT  */
#line 1012 "src/parser.y"
                         {
        if (!warn_channel_ok(ctx, (yyvsp[-2].text), (yylsp[-2]).first_line, (yylsp[-2]).first_column)) { YYERROR; }
        free((yyvsp[-2].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_NEXT);
      }
#line 4366 "src/parser.tab.c"
    break;

  case 148: /* on_error_statement: ON IDENT GOTO IDENT  */
#line 1017 "src/parser.y"
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
#line 4384 "src/parser.tab.c"
    break;

  case 149: /* on_error_statement: ON IDENT STOP  */
#line 1030 "src/parser.y"
                    {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_STOP);
      }
#line 4394 "src/parser.tab.c"
    break;

  case 150: /* on_error_statement: ON IDENT PRINT  */
#line 1035 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_PRINT);
      }
#line 4404 "src/parser.tab.c"
    break;

  case 151: /* on_error_statement: ON IDENT IDENT  */
#line 1040 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        int mode = warn_mode_word(ctx, (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column);
        if (mode < 0) { free((yyvsp[-1].text)); free((yyvsp[0].text)); YYERROR; }
        free((yyvsp[-1].text)); free((yyvsp[0].text));
        (yyval.stmt) = ast_on_warning(mode);
      }
#line 4416 "src/parser.tab.c"
    break;

  case 152: /* error_statement: ERROR_VALUE expression  */
#line 1050 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 4422 "src/parser.tab.c"
    break;

  case 153: /* return_statement: RETURN  */
#line 1054 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 4428 "src/parser.tab.c"
    break;

  case 154: /* return_statement: RETURN expression  */
#line 1055 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 4434 "src/parser.tab.c"
    break;

  case 155: /* label_statement: variable_name COLON  */
#line 1059 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 4440 "src/parser.tab.c"
    break;

  case 156: /* goto_statement: GOTO variable_name  */
#line 1066 "src/parser.y"
                         { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 4446 "src/parser.tab.c"
    break;

  case 157: /* gosub_statement: GOSUB variable_name  */
#line 1070 "src/parser.y"
                          { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 4452 "src/parser.tab.c"
    break;

  case 158: /* break_statement: BREAK  */
#line 1079 "src/parser.y"
            { (yyval.stmt) = ast_break(NULL); }
#line 4458 "src/parser.tab.c"
    break;

  case 159: /* break_statement: BREAK IDENT  */
#line 1080 "src/parser.y"
                  { (yyval.stmt) = ast_break((yyvsp[0].text)); }
#line 4464 "src/parser.tab.c"
    break;

  case 160: /* continue_statement: CONTINUE  */
#line 1084 "src/parser.y"
               { (yyval.stmt) = ast_continue(NULL); }
#line 4470 "src/parser.tab.c"
    break;

  case 161: /* continue_statement: CONTINUE IDENT  */
#line 1085 "src/parser.y"
                     { (yyval.stmt) = ast_continue((yyvsp[0].text)); }
#line 4476 "src/parser.tab.c"
    break;

  case 162: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1089 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4485 "src/parser.tab.c"
    break;

  case 163: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1093 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4494 "src/parser.tab.c"
    break;

  case 164: /* if_block_tail: END IF NEWLINE  */
#line 1100 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4502 "src/parser.tab.c"
    break;

  case 165: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 1103 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4510 "src/parser.tab.c"
    break;

  case 166: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1106 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4518 "src/parser.tab.c"
    break;

  case 167: /* if_block_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1115 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 4530 "src/parser.tab.c"
    break;

  case 168: /* if_inline_tail: %empty  */
#line 1125 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4538 "src/parser.tab.c"
    break;

  case 169: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 1128 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4546 "src/parser.tab.c"
    break;

  case 170: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1131 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4554 "src/parser.tab.c"
    break;

  case 171: /* if_inline_tail: ELSE IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1137 "src/parser.y"
                                                                      {
        AstStmt *inner = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 4566 "src/parser.tab.c"
    break;

  case 172: /* if_inline_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1144 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 4578 "src/parser.tab.c"
    break;

  case 173: /* inline_statement: assignment  */
#line 1154 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4584 "src/parser.tab.c"
    break;

  case 174: /* inline_statement: print_statement  */
#line 1155 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4590 "src/parser.tab.c"
    break;

  case 175: /* inline_statement: call_statement  */
#line 1156 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4596 "src/parser.tab.c"
    break;

  case 176: /* inline_statement: use_statement  */
#line 1157 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4602 "src/parser.tab.c"
    break;

  case 177: /* inline_statement: on_error_statement  */
#line 1158 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4608 "src/parser.tab.c"
    break;

  case 178: /* inline_statement: error_statement  */
#line 1159 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4614 "src/parser.tab.c"
    break;

  case 179: /* inline_statement: return_statement  */
#line 1160 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4620 "src/parser.tab.c"
    break;

  case 180: /* inline_statement: goto_statement  */
#line 1161 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4626 "src/parser.tab.c"
    break;

  case 181: /* inline_statement: gosub_statement  */
#line 1162 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4632 "src/parser.tab.c"
    break;

  case 182: /* inline_statement: break_statement  */
#line 1163 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4638 "src/parser.tab.c"
    break;

  case 183: /* inline_statement: continue_statement  */
#line 1164 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4644 "src/parser.tab.c"
    break;

  case 184: /* expression: or_expression  */
#line 1168 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 4650 "src/parser.tab.c"
    break;

  case 185: /* or_expression: and_expression  */
#line 1172 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 4656 "src/parser.tab.c"
    break;

  case 186: /* or_expression: or_expression OR and_expression  */
#line 1173 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4662 "src/parser.tab.c"
    break;

  case 187: /* and_expression: comparison_expression  */
#line 1177 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 4668 "src/parser.tab.c"
    break;

  case 188: /* and_expression: and_expression AND comparison_expression  */
#line 1178 "src/parser.y"
                                               { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4674 "src/parser.tab.c"
    break;

  case 189: /* comparison_expression: additive_expression  */
#line 1182 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 4680 "src/parser.tab.c"
    break;

  case 190: /* comparison_expression: additive_expression comparison_operator additive_expression  */
#line 1183 "src/parser.y"
                                                                  { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4686 "src/parser.tab.c"
    break;

  case 191: /* comparison_expression: additive_expression comparison_lens comparison_operator additive_expression  */
#line 1184 "src/parser.y"
                                                                                  {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 4694 "src/parser.tab.c"
    break;

  case 192: /* additive_expression: multiplicative_expression  */
#line 1190 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 4700 "src/parser.tab.c"
    break;

  case 193: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 1191 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4706 "src/parser.tab.c"
    break;

  case 194: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 1192 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4712 "src/parser.tab.c"
    break;

  case 195: /* multiplicative_expression: unary_expression  */
#line 1196 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 4718 "src/parser.tab.c"
    break;

  case 196: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 1197 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4724 "src/parser.tab.c"
    break;

  case 197: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 1198 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4730 "src/parser.tab.c"
    break;

  case 198: /* unary_expression: postfix_expression  */
#line 1202 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 4736 "src/parser.tab.c"
    break;

  case 199: /* unary_expression: NOT unary_expression  */
#line 1203 "src/parser.y"
                           { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4742 "src/parser.tab.c"
    break;

  case 200: /* unary_expression: MINUS unary_expression  */
#line 1204 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4748 "src/parser.tab.c"
    break;

  case 201: /* unary_expression: NEW postfix_expression  */
#line 1205 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4754 "src/parser.tab.c"
    break;

  case 202: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 1206 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 4760 "src/parser.tab.c"
    break;

  case 203: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 1207 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4766 "src/parser.tab.c"
    break;

  case 204: /* postfix_expression: primary  */
#line 1211 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 4772 "src/parser.tab.c"
    break;

  case 205: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 1212 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4778 "src/parser.tab.c"
    break;

  case 206: /* postfix_expression: postfix_expression DOT dot_field_name  */
#line 1213 "src/parser.y"
                                            { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4784 "src/parser.tab.c"
    break;

  case 207: /* postfix_expression: postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1214 "src/parser.y"
                                                                   {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        (yyval.expr) = expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 4794 "src/parser.tab.c"
    break;

  case 208: /* postfix_expression: postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1219 "src/parser.y"
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
#line 4810 "src/parser.tab.c"
    break;

  case 209: /* comparison_operator: OP_EQ  */
#line 1233 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 4816 "src/parser.tab.c"
    break;

  case 210: /* comparison_operator: OP_NE  */
#line 1234 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 4822 "src/parser.tab.c"
    break;

  case 211: /* comparison_operator: OP_GT  */
#line 1235 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 4828 "src/parser.tab.c"
    break;

  case 212: /* comparison_operator: OP_LT  */
#line 1236 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 4834 "src/parser.tab.c"
    break;

  case 213: /* comparison_operator: OP_GE  */
#line 1237 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 4840 "src/parser.tab.c"
    break;

  case 214: /* comparison_operator: OP_LE  */
#line 1238 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 4846 "src/parser.tab.c"
    break;

  case 215: /* comparison_operator: OP_NGT  */
#line 1239 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 4852 "src/parser.tab.c"
    break;

  case 216: /* comparison_operator: OP_NLT  */
#line 1240 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 4858 "src/parser.tab.c"
    break;

  case 217: /* comparison_operator: OP_NGE  */
#line 1241 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 4864 "src/parser.tab.c"
    break;

  case 218: /* comparison_operator: OP_NLE  */
#line 1242 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 4870 "src/parser.tab.c"
    break;

  case 219: /* primary: NUMBER  */
#line 1246 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4876 "src/parser.tab.c"
    break;

  case 220: /* primary: WATCHERS LPAREN RPAREN  */
#line 1247 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_call(copy_const("watchers"), ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4882 "src/parser.tab.c"
    break;

  case 221: /* primary: duration_terms  */
#line 1248 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4888 "src/parser.tab.c"
    break;

  case 222: /* primary: STRING  */
#line 1249 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4894 "src/parser.tab.c"
    break;

  case 223: /* primary: variable_name ident_suffix  */
#line 1250 "src/parser.y"
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
#line 4916 "src/parser.tab.c"
    break;

  case 224: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1267 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 4927 "src/parser.tab.c"
    break;

  case 225: /* primary: ERROR_VALUE  */
#line 1273 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4933 "src/parser.tab.c"
    break;

  case 226: /* primary: TRUE  */
#line 1274 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4939 "src/parser.tab.c"
    break;

  case 227: /* primary: FALSE  */
#line 1275 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4945 "src/parser.tab.c"
    break;

  case 228: /* primary: NOTHING  */
#line 1276 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4951 "src/parser.tab.c"
    break;

  case 229: /* primary: UNKNOWN_VALUE  */
#line 1277 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4957 "src/parser.tab.c"
    break;

  case 230: /* primary: LPAREN expression RPAREN  */
#line 1278 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 4963 "src/parser.tab.c"
    break;

  case 231: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 1279 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4969 "src/parser.tab.c"
    break;

  case 232: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 1280 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4975 "src/parser.tab.c"
    break;

  case 233: /* primary: record_literal  */
#line 1281 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 4981 "src/parser.tab.c"
    break;

  case 234: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 1285 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4987 "src/parser.tab.c"
    break;

  case 235: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 1286 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4993 "src/parser.tab.c"
    break;

  case 236: /* ident_suffix: %empty  */
#line 1290 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 5003 "src/parser.tab.c"
    break;

  case 237: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 1295 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5013 "src/parser.tab.c"
    break;

  case 238: /* ident_suffix: DOT dot_field_name ident_dot_suffix  */
#line 1300 "src/parser.y"
                                          {
        /* dot_field_name, not IDENT: a keyword is a legal FIELD name after a
         * dot, because nothing but a name can appear there. */
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 5024 "src/parser.tab.c"
    break;

  case 239: /* ident_suffix: DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1306 "src/parser.y"
                                                          {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        (yyval.ident_suffix).kind = IDENT_SUFFIX_METHOD;
        (yyval.ident_suffix).name = (yyvsp[-3].text);
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5038 "src/parser.tab.c"
    break;

  case 240: /* ident_dot_suffix: %empty  */
#line 1318 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 5048 "src/parser.tab.c"
    break;

  case 241: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 1323 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5058 "src/parser.tab.c"
    break;

  case 242: /* duration_terms: NUMBER IDENT  */
#line 1331 "src/parser.y"
                   {
        AstDuration duration = {0};
        (yyval.duration) = duration_add_unit(duration, (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 5067 "src/parser.tab.c"
    break;

  case 243: /* duration_terms: duration_terms NUMBER IDENT  */
#line 1335 "src/parser.y"
                                  {
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 5075 "src/parser.tab.c"
    break;

  case 244: /* argument_list_opt: %empty  */
#line 1341 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 5081 "src/parser.tab.c"
    break;

  case 245: /* argument_list_opt: argument_list  */
#line 1342 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 5087 "src/parser.tab.c"
    break;

  case 246: /* argument_list: expression  */
#line 1346 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 5093 "src/parser.tab.c"
    break;

  case 247: /* argument_list: argument_list COMMA expression  */
#line 1347 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 5099 "src/parser.tab.c"
    break;

  case 248: /* array_argument_list: expression  */
#line 1351 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 5105 "src/parser.tab.c"
    break;

  case 249: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 1352 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 5111 "src/parser.tab.c"
    break;

  case 250: /* parameter_list_opt: %empty  */
#line 1356 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 5117 "src/parser.tab.c"
    break;

  case 251: /* parameter_list_opt: parameter_list  */
#line 1357 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 5123 "src/parser.tab.c"
    break;

  case 252: /* parameter_default: NUMBER  */
#line 1371 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5129 "src/parser.tab.c"
    break;

  case 253: /* parameter_default: MINUS NUMBER  */
#line 1372 "src/parser.y"
                   { (yyval.expr) = expr_at(ast_number(-(yyvsp[0].number)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5135 "src/parser.tab.c"
    break;

  case 254: /* parameter_default: PLUS NUMBER  */
#line 1373 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5141 "src/parser.tab.c"
    break;

  case 255: /* parameter_default: STRING  */
#line 1374 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5147 "src/parser.tab.c"
    break;

  case 256: /* parameter_default: TRUE  */
#line 1375 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5153 "src/parser.tab.c"
    break;

  case 257: /* parameter_default: FALSE  */
#line 1376 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5159 "src/parser.tab.c"
    break;

  case 258: /* parameter_default: NOTHING  */
#line 1377 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5165 "src/parser.tab.c"
    break;

  case 259: /* parameter_default: UNKNOWN_VALUE  */
#line 1378 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5171 "src/parser.tab.c"
    break;

  case 260: /* parameter_list: IDENT  */
#line 1382 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5177 "src/parser.tab.c"
    break;

  case 261: /* parameter_list: IDENT OP_EQ parameter_default  */
#line 1383 "src/parser.y"
                                    {
        (yyval.name_list) = ast_name_list_append_default(ast_name_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 5185 "src/parser.tab.c"
    break;

  case 262: /* parameter_list: parameter_list COMMA IDENT  */
#line 1386 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5191 "src/parser.tab.c"
    break;

  case 263: /* parameter_list: parameter_list COMMA IDENT OP_EQ parameter_default  */
#line 1387 "src/parser.y"
                                                         {
        (yyval.name_list) = ast_name_list_append_default((yyvsp[-4].name_list), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 5199 "src/parser.tab.c"
    break;

  case 264: /* field_name: dot_field_name  */
#line 1402 "src/parser.y"
                     { (yyval.text) = (yyvsp[0].text); }
#line 5205 "src/parser.tab.c"
    break;

  case 265: /* field_name: STRING  */
#line 1409 "src/parser.y"
             { (yyval.text) = (yyvsp[0].text); }
#line 5211 "src/parser.tab.c"
    break;

  case 266: /* dot_field_name: IDENT  */
#line 1418 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 5217 "src/parser.tab.c"
    break;

  case 267: /* dot_field_name: AS  */
#line 1419 "src/parser.y"
                     { (yyval.text) = kw_name("as"); }
#line 5223 "src/parser.tab.c"
    break;

  case 268: /* dot_field_name: NEXT  */
#line 1420 "src/parser.y"
                     { (yyval.text) = kw_name("next"); }
#line 5229 "src/parser.tab.c"
    break;

  case 269: /* dot_field_name: STOP  */
#line 1421 "src/parser.y"
                     { (yyval.text) = kw_name("stop"); }
#line 5235 "src/parser.tab.c"
    break;

  case 270: /* dot_field_name: ERROR_VALUE  */
#line 1422 "src/parser.y"
                     { (yyval.text) = kw_name("error"); }
#line 5241 "src/parser.tab.c"
    break;

  case 271: /* dot_field_name: END  */
#line 1423 "src/parser.y"
                     { (yyval.text) = kw_name("end"); }
#line 5247 "src/parser.tab.c"
    break;

  case 272: /* dot_field_name: TO  */
#line 1424 "src/parser.y"
                     { (yyval.text) = kw_name("to"); }
#line 5253 "src/parser.tab.c"
    break;

  case 273: /* dot_field_name: IN  */
#line 1425 "src/parser.y"
                     { (yyval.text) = kw_name("in"); }
#line 5259 "src/parser.tab.c"
    break;

  case 274: /* dot_field_name: ON  */
#line 1426 "src/parser.y"
                     { (yyval.text) = kw_name("on"); }
#line 5265 "src/parser.tab.c"
    break;

  case 275: /* dot_field_name: NEW  */
#line 1427 "src/parser.y"
                     { (yyval.text) = kw_name("new"); }
#line 5271 "src/parser.tab.c"
    break;

  case 276: /* dot_field_name: EACH  */
#line 1428 "src/parser.y"
                     { (yyval.text) = kw_name("each"); }
#line 5277 "src/parser.tab.c"
    break;

  case 277: /* dot_field_name: WITH  */
#line 1429 "src/parser.y"
                     { (yyval.text) = kw_name("with"); }
#line 5283 "src/parser.tab.c"
    break;

  case 278: /* dot_field_name: WITHOUT  */
#line 1430 "src/parser.y"
                     { (yyval.text) = kw_name("without"); }
#line 5289 "src/parser.tab.c"
    break;

  case 279: /* dot_field_name: THEN  */
#line 1431 "src/parser.y"
                     { (yyval.text) = kw_name("then"); }
#line 5295 "src/parser.tab.c"
    break;

  case 280: /* dot_field_name: ELSE  */
#line 1432 "src/parser.y"
                     { (yyval.text) = kw_name("else"); }
#line 5301 "src/parser.tab.c"
    break;

  case 281: /* dot_field_name: FOR  */
#line 1433 "src/parser.y"
                     { (yyval.text) = kw_name("for"); }
#line 5307 "src/parser.tab.c"
    break;

  case 282: /* dot_field_name: IF  */
#line 1434 "src/parser.y"
                     { (yyval.text) = kw_name("if"); }
#line 5313 "src/parser.tab.c"
    break;

  case 283: /* dot_field_name: WHILE  */
#line 1435 "src/parser.y"
                     { (yyval.text) = kw_name("while"); }
#line 5319 "src/parser.tab.c"
    break;

  case 284: /* dot_field_name: DO  */
#line 1436 "src/parser.y"
                     { (yyval.text) = kw_name("do"); }
#line 5325 "src/parser.tab.c"
    break;

  case 285: /* dot_field_name: UNTIL  */
#line 1437 "src/parser.y"
                     { (yyval.text) = kw_name("until"); }
#line 5331 "src/parser.tab.c"
    break;

  case 286: /* dot_field_name: PRINT  */
#line 1438 "src/parser.y"
                     { (yyval.text) = kw_name("print"); }
#line 5337 "src/parser.tab.c"
    break;

  case 287: /* dot_field_name: RETURN  */
#line 1439 "src/parser.y"
                     { (yyval.text) = kw_name("return"); }
#line 5343 "src/parser.tab.c"
    break;

  case 288: /* dot_field_name: LOAD  */
#line 1440 "src/parser.y"
                     { (yyval.text) = kw_name("load"); }
#line 5349 "src/parser.tab.c"
    break;

  case 289: /* dot_field_name: USE  */
#line 1441 "src/parser.y"
                     { (yyval.text) = kw_name("use"); }
#line 5355 "src/parser.tab.c"
    break;

  case 290: /* dot_field_name: NOT  */
#line 1442 "src/parser.y"
                     { (yyval.text) = kw_name("not"); }
#line 5361 "src/parser.tab.c"
    break;

  case 291: /* dot_field_name: AND  */
#line 1443 "src/parser.y"
                     { (yyval.text) = kw_name("and"); }
#line 5367 "src/parser.tab.c"
    break;

  case 292: /* dot_field_name: OR  */
#line 1444 "src/parser.y"
                     { (yyval.text) = kw_name("or"); }
#line 5373 "src/parser.tab.c"
    break;

  case 293: /* dot_field_name: TRUE  */
#line 1445 "src/parser.y"
                     { (yyval.text) = kw_name("true"); }
#line 5379 "src/parser.tab.c"
    break;

  case 294: /* dot_field_name: FALSE  */
#line 1446 "src/parser.y"
                     { (yyval.text) = kw_name("false"); }
#line 5385 "src/parser.tab.c"
    break;

  case 295: /* dot_field_name: NOTHING  */
#line 1447 "src/parser.y"
                     { (yyval.text) = kw_name("nothing"); }
#line 5391 "src/parser.tab.c"
    break;

  case 296: /* dot_field_name: BREAK  */
#line 1448 "src/parser.y"
                     { (yyval.text) = kw_name("break"); }
#line 5397 "src/parser.tab.c"
    break;

  case 297: /* dot_field_name: CONTINUE  */
#line 1449 "src/parser.y"
                     { (yyval.text) = kw_name("continue"); }
#line 5403 "src/parser.tab.c"
    break;

  case 298: /* dot_field_name: GOTO  */
#line 1450 "src/parser.y"
                     { (yyval.text) = kw_name("goto"); }
#line 5409 "src/parser.tab.c"
    break;

  case 299: /* dot_field_name: GOSUB  */
#line 1451 "src/parser.y"
                     { (yyval.text) = kw_name("gosub"); }
#line 5415 "src/parser.tab.c"
    break;

  case 300: /* dot_field_name: SPAWN  */
#line 1452 "src/parser.y"
                     { (yyval.text) = kw_name("spawn"); }
#line 5421 "src/parser.tab.c"
    break;

  case 301: /* dot_field_name: EXPORT  */
#line 1453 "src/parser.y"
                     { (yyval.text) = kw_name("export"); }
#line 5427 "src/parser.tab.c"
    break;

  case 302: /* dot_field_name: LIBRARY  */
#line 1454 "src/parser.y"
                     { (yyval.text) = kw_name("library"); }
#line 5433 "src/parser.tab.c"
    break;

  case 303: /* dot_field_name: FUNCTION  */
#line 1455 "src/parser.y"
                     { (yyval.text) = kw_name("function"); }
#line 5439 "src/parser.tab.c"
    break;

  case 304: /* dot_field_name: MODIFIER  */
#line 1456 "src/parser.y"
                     { (yyval.text) = kw_name("modifier"); }
#line 5445 "src/parser.tab.c"
    break;

  case 305: /* dot_field_name: PROGRAM  */
#line 1457 "src/parser.y"
                     { (yyval.text) = kw_name("program"); }
#line 5451 "src/parser.tab.c"
    break;

  case 306: /* dot_field_name: WATCH  */
#line 1458 "src/parser.y"
                     { (yyval.text) = kw_name("watch"); }
#line 5457 "src/parser.tab.c"
    break;

  case 307: /* dot_field_name: WATCHERS  */
#line 1459 "src/parser.y"
                     { (yyval.text) = kw_name("watchers"); }
#line 5463 "src/parser.tab.c"
    break;

  case 308: /* dot_field_name: CONSIDER  */
#line 1460 "src/parser.y"
                     { (yyval.text) = kw_name("consider"); }
#line 5469 "src/parser.tab.c"
    break;

  case 309: /* dot_field_name: STEP  */
#line 1461 "src/parser.y"
                     { (yyval.text) = kw_name("step"); }
#line 5475 "src/parser.tab.c"
    break;

  case 310: /* dot_field_name: UNWATCH  */
#line 1462 "src/parser.y"
                     { (yyval.text) = kw_name("unwatch"); }
#line 5481 "src/parser.tab.c"
    break;

  case 311: /* dot_field_name: UNKNOWN_VALUE  */
#line 1463 "src/parser.y"
                     { (yyval.text) = kw_name("unknown"); }
#line 5487 "src/parser.tab.c"
    break;

  case 312: /* dot_field_name: DIM  */
#line 1464 "src/parser.y"
                     { (yyval.text) = kw_name("dim"); }
#line 5493 "src/parser.tab.c"
    break;

  case 313: /* record_field_list: field_name OP_EQ expression  */
#line 1468 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5499 "src/parser.tab.c"
    break;

  case 314: /* record_field_list: field_name COLON expression  */
#line 1469 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5505 "src/parser.tab.c"
    break;

  case 315: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1470 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5511 "src/parser.tab.c"
    break;

  case 316: /* record_field_list: record_field_list COMMA optional_newlines field_name OP_EQ expression  */
#line 1471 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5517 "src/parser.tab.c"
    break;

  case 317: /* record_field_list: record_field_list COMMA optional_newlines field_name COLON expression  */
#line 1472 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5523 "src/parser.tab.c"
    break;

  case 318: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1473 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5529 "src/parser.tab.c"
    break;

  case 319: /* field_policy: IDENT  */
#line 1481 "src/parser.y"
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
#line 5561 "src/parser.tab.c"
    break;

  case 320: /* field_policy: IDENT expression  */
#line 1508 "src/parser.y"
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
#line 5582 "src/parser.tab.c"
    break;


#line 5586 "src/parser.tab.c"

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

#line 1531 "src/parser.y"


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
