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
  YYSYMBOL_parameter_list = 157,           /* parameter_list  */
  YYSYMBOL_field_name = 158,               /* field_name  */
  YYSYMBOL_dot_field_name = 159,           /* dot_field_name  */
  YYSYMBOL_record_field_list = 160,        /* record_field_list  */
  YYSYMBOL_field_policy = 161,             /* field_policy  */
  YYSYMBOL_optional_newlines = 162         /* optional_newlines  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;



/* Unqualified %code blocks.  */
#line 435 "src/parser.y"

static int yylex(YYSTYPE *lvalp, YYLTYPE *llocp, gb_parse_ctx *ctx);
static void yyerror(YYLTYPE *llocp, gb_parse_ctx *ctx, const char *message);
static void report_syntax_error(gb_parse_ctx *ctx, int line, int column,
                                int end_line, int end_column, const char *message);

#line 640 "src/parser.tab.c"

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
#define YYLAST   2470

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  87
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  76
/* YYNRULES -- Number of rules.  */
#define YYNRULES  307
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  649

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
       0,   500,   500,   504,   505,   506,   510,   511,   512,   513,
     514,   515,   516,   517,   518,   519,   520,   521,   522,   523,
     524,   525,   526,   527,   528,   529,   530,   531,   532,   533,
     534,   535,   541,   552,   557,   558,   570,   582,   583,   584,
     585,   589,   590,   591,   595,   596,   597,   608,   608,   614,
     615,   619,   620,   621,   622,   626,   632,   636,   637,   643,
     648,   657,   670,   693,   694,   695,   699,   703,   710,   714,
     735,   741,   747,   753,   756,   762,   763,   767,   768,   769,
     773,   774,   775,   776,   777,   778,   779,   780,   781,   782,
     783,   784,   785,   786,   787,   788,   789,   790,   791,   792,
     793,   794,   795,   796,   797,   803,   814,   817,   824,   827,
     833,   839,   845,   846,   847,   848,   849,   865,   884,   885,
     889,   893,   896,   904,   910,   914,   915,   934,   937,   943,
     944,   945,   949,   952,   955,   958,   961,   967,   968,   972,
     973,   977,   983,   984,   985,   986,   991,  1004,  1009,  1014,
    1024,  1028,  1029,  1033,  1040,  1044,  1053,  1054,  1058,  1059,
    1063,  1067,  1074,  1077,  1080,  1086,  1089,  1092,  1098,  1099,
    1100,  1101,  1102,  1103,  1104,  1105,  1106,  1107,  1108,  1112,
    1116,  1117,  1121,  1122,  1126,  1127,  1128,  1134,  1135,  1136,
    1140,  1141,  1142,  1146,  1147,  1148,  1149,  1150,  1151,  1155,
    1156,  1157,  1158,  1163,  1177,  1178,  1179,  1180,  1181,  1182,
    1183,  1184,  1185,  1186,  1190,  1191,  1192,  1193,  1194,  1211,
    1217,  1218,  1219,  1220,  1221,  1222,  1223,  1224,  1225,  1229,
    1230,  1234,  1239,  1244,  1250,  1262,  1267,  1275,  1279,  1285,
    1286,  1290,  1291,  1295,  1296,  1300,  1301,  1305,  1306,  1319,
    1326,  1335,  1336,  1337,  1338,  1339,  1340,  1341,  1342,  1343,
    1344,  1345,  1346,  1347,  1348,  1349,  1350,  1351,  1352,  1353,
    1354,  1355,  1356,  1357,  1358,  1359,  1360,  1361,  1362,  1363,
    1364,  1365,  1366,  1367,  1368,  1369,  1370,  1371,  1372,  1373,
    1374,  1375,  1376,  1377,  1378,  1379,  1380,  1381,  1385,  1386,
    1387,  1388,  1389,  1390,  1398,  1425,  1444,  1445
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
  "parameter_list", "field_name", "dot_field_name", "record_field_list",
  "field_policy", "optional_newlines", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-490)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -490,    57,   910,  -490,    36,    22,  -490,  2052,  -490,  2039,
      69,   116,    20,  2052,  2052,   145,   165,   197,  2052,    82,
      82,    80,  2052,   137,    31,  -490,   520,   162,   188,   195,
     206,   242,   148,  -490,  -490,   156,   166,   146,   158,   161,
    -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,   174,
    -490,   176,  -490,  -490,   177,   180,   183,   185,   186,   196,
     199,   200,  -490,   160,  2052,  2052,   254,  -490,  -490,   203,
    -490,  -490,  -490,  -490,  2052,  2116,   261,   205,  -490,  2052,
    2052,  -490,  -490,    84,   255,   258,   260,  -490,   592,   175,
    -490,   107,  -490,  -490,   284,   234,  -490,   214,    15,   286,
    -490,   209,   210,  -490,  -490,   220,   221,  -490,  -490,  -490,
     222,    82,  -490,    47,   212,  -490,   218,   129,   108,   301,
    -490,  -490,  -490,  -490,  -490,    73,  -490,   277,   235,   229,
     311,  -490,   313,  -490,   162,  -490,  -490,  -490,  -490,  -490,
    2052,  2052,  -490,  2296,  2052,   152,  -490,  -490,  -490,  -490,
    -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,  2180,
    -490,   243,   239,   245,  -490,  2052,  -490,    41,   249,   250,
    -490,   251,   598,   674,  2052,  2353,  -490,   179,  2052,  2052,
    -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,
    2052,  2052,  1943,  2052,  2052,  2052,  2052,  2410,   325,  2052,
    2052,  2052,  2052,   293,   796,  -490,   316,   328,   328,    82,
      91,    82,  -490,   332,  -490,  -490,  -490,    60,  -490,    94,
    -490,   262,   328,  -490,   335,   328,  -490,   339,   340,   308,
    -490,   268,   341,   273,   274,  -490,  -490,  -490,  -490,  -490,
    -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,
    -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,
    -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,
    -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,
    -490,  -490,  -490,  2052,  2052,   275,  -490,   271,    13,  -490,
      92,  -490,  2052,  -490,   279,   280,  2052,  -490,  -490,  -490,
    -490,  -490,   290,  -490,   294,   285,  -490,   304,   306,   309,
    -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,
    -490,  -490,  -490,   305,   260,  -490,   175,   175,  2052,   182,
    -490,  -490,   314,   312,   322,  -490,  -490,  -490,   324,   321,
     372,  2052,  2052,   967,  2052,   190,  -490,   333,   331,   337,
     109,   342,   212,  1024,  -490,  1081,  -490,  -490,  -490,  -490,
    2052,   338,  -490,   343,   344,  1138,  -490,  -490,   335,  -490,
     348,  2052,  2052,  -490,  -490,   411,  -490,  2052,  2052,   346,
    -490,  -490,  -490,  -490,   356,  -490,   111,   139,  -490,  2052,
    2052,  -490,   853,   401,   182,  -490,  2052,  2052,   350,  -490,
    2052,   351,   353,   398,   429,  2052,   363,   419,   364,   445,
     367,   368,  -490,   405,   406,   381,  -490,  -490,   375,   402,
     379,  -490,   390,   394,  2052,   395,    59,  -490,  -490,  -490,
     739,  -490,   559,  -490,  -490,   396,   397,   423,   462,  -490,
    1982,  -490,   403,   404,  -490,  1195,   -24,  -490,  -490,   400,
     407,   468,  -490,   408,  -490,  -490,  -490,  -490,  1252,   410,
     412,  -490,  1309,  -490,   414,  -490,  -490,  -490,  -490,   420,
     246,   474,   481,  -490,  -490,    64,   428,    32,  -490,  -490,
    -490,  -490,   421,   426,  -490,   427,  -490,  -490,  1366,   455,
      56,  -490,  2052,  -490,  1195,  -490,  -490,   430,  1423,  -490,
    1480,  1537,  1594,   442,  -490,  -490,   439,  1651,  -490,  1708,
    2052,   437,   440,   151,   431,   433,   513,   411,  2052,  2052,
    1765,  -490,  -490,  1822,  -490,   490,   446,  -490,   448,   449,
    1195,  -490,  1423,  -490,  -490,  -490,   450,   453,   454,  -490,
    -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,   457,
    -490,   458,  -490,   463,   464,   466,   469,   470,   471,   472,
     473,  -490,   477,   479,   491,   475,   476,   501,   506,  -490,
    2238,   328,   560,  -490,  -490,  -490,   484,   492,  -490,  -490,
     555,   557,   493,  -490,  -490,  -490,  -490,  1423,  -490,  -490,
    -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,  -490,
    -490,   494,   496,   497,  -490,  -490,   503,   505,   508,   115,
     499,  -490,  1879,  -490,   511,   515,   517,  -490,  1195,  -490,
    -490,  -490,  -490,  -490,  -490,   521,   524,   522,  2052,  -490,
    -490,  -490,    68,  -490,  -490,   525,  -490,   590,    75,  1936,
    -490,   529,   610,   611,  -490,   535,   536,  -490,  -490
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       3,     0,     2,     1,    44,     0,    32,     0,    45,     0,
       0,     0,     0,     0,     0,   156,   158,     0,   151,     0,
       0,     0,     0,     0,     0,    46,     0,     0,     0,     0,
       0,     0,     0,     4,     5,     0,     0,    41,     0,     0,
       9,    10,    12,    11,    13,    14,    15,    16,    17,     0,
      19,     0,    20,    22,     0,     0,     0,     0,     0,     0,
       0,     0,    31,     0,   239,   239,   214,    44,   217,     0,
     221,   222,   223,   224,     0,     0,     0,     0,   220,     0,
       0,   306,   306,   231,     0,   179,   180,   182,   184,   187,
     190,   193,   199,   228,   216,     0,    55,     0,     0,     0,
       3,     0,     0,   157,   159,     0,     0,   152,   154,   155,
      44,     0,   139,     0,   125,   124,     0,     0,     0,     0,
     150,    51,    53,    52,    54,   118,    49,     0,     0,     0,
     113,   115,   112,   114,     0,     6,    37,    38,    39,    40,
       0,     0,    47,     0,     0,     0,   153,     7,     8,    18,
      21,    23,    24,    25,    26,    27,    28,    29,    30,     0,
     241,     0,   240,     0,   237,   239,   194,   196,     0,     0,
     195,     0,     0,     0,   239,     0,   218,     0,     0,     0,
     204,   205,   206,   207,   208,   209,   210,   211,   212,   213,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     3,     0,   245,   245,     0,
       0,     0,     3,     0,     3,   149,   148,     0,   147,     0,
     144,     0,   245,    50,     0,   245,     3,     0,     0,     0,
      33,     0,     0,   251,     0,   252,   297,   267,   264,   265,
     256,   271,   278,   279,   280,   296,   276,   277,   275,   262,
     260,   285,   266,   257,   294,   269,   270,   258,   261,   268,
     293,   281,   282,   288,   272,   283,   284,   291,   295,   263,
     292,   259,   253,   254,   255,   289,   290,   287,   273,   274,
     286,    43,    34,     0,     0,   251,   250,     0,     0,   249,
       0,    57,     0,    58,     0,     0,   239,   215,   225,   226,
     307,   243,   306,   229,   306,     0,   251,     0,   235,    44,
       3,   168,    41,   169,   170,   171,   172,   173,   174,   175,
     176,   177,   178,     0,   181,   183,   188,   189,     0,   185,
     191,   192,     0,   251,     0,   201,   238,    56,     0,     0,
       0,     0,     0,     0,     0,    75,   247,     0,   246,     0,
       0,     0,   126,     0,   140,     0,   146,   145,   142,   143,
     239,     0,   120,     0,     0,     0,   117,   116,     0,    42,
       0,   239,   239,    36,    35,     0,   129,     0,     0,     0,
     306,   242,   219,   197,     0,   306,     0,     0,   232,   239,
     239,   233,     0,   165,   186,   200,   239,   239,     0,     3,
       0,     0,     0,    45,     0,     0,     0,     0,     0,     0,
       0,     0,     3,    45,    45,     0,   119,     3,     0,    45,
       0,    48,     0,     0,   304,     0,     0,   298,   299,   129,
       0,   198,     0,   227,   230,     0,     0,     0,    45,   160,
       0,   161,     0,     0,     3,     0,     0,     3,    70,     0,
       0,     0,    77,     0,     3,   248,     3,     3,     0,     0,
       0,    61,     0,     3,     0,     3,    59,    60,   305,     0,
       0,     0,     0,   130,   131,     0,   251,     0,   244,   234,
     236,     3,     0,     0,     3,     0,   202,   203,     0,    45,
      46,    66,     0,     3,     0,    71,    77,     0,    76,    72,
       0,     0,     0,    45,   122,   141,    45,     0,   111,     0,
       0,     0,   137,     0,     0,     0,     0,     0,     0,     0,
       0,   163,   162,     0,   166,    45,     0,    64,     0,     0,
       0,    67,    73,    77,   105,    78,     0,     0,     0,    83,
      84,    86,    85,    87,    79,    88,    89,    90,    91,     0,
      93,     0,    95,     0,     0,     0,     0,     0,     0,     0,
       0,   104,    45,    45,    45,     0,     0,    45,    45,   300,
       0,   245,     0,   132,   128,     3,     0,     0,   301,   302,
      45,    45,     0,    63,    65,     3,    68,    74,    80,    81,
      82,    92,    94,    96,    97,    98,    99,   100,   101,   102,
     103,     0,     0,     0,   121,   108,     0,     0,     0,     0,
       0,   138,     0,   127,     0,     0,     0,    62,     0,   106,
     107,   123,   110,   109,   129,     0,     0,    45,     0,   164,
     167,    69,     0,   129,     3,     0,   303,     0,     0,     0,
     136,     0,     0,    45,   135,     0,     0,   134,   133
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -490,  -490,   -74,  -490,  -176,   451,  -490,    -2,   537,  -490,
    -490,   502,  -170,  -165,  -487,  -489,  -483,  -482,  -474,  -471,
    -490,  -490,  -431,  -490,  -470,  -466,  -465,  -452,  -164,   498,
     256,  -451,  -450,  -102,  -490,  -427,  -490,  -490,   422,  -444,
    -163,  -156,  -146,  -443,  -134,  -133,  -126,  -124,  -442,  -490,
    -490,  -211,    16,  -490,   452,   456,  -187,    65,   -71,   564,
     459,  -490,   345,  -490,  -490,  -490,     6,  -490,  -490,  -186,
    -490,   211,  -106,  -169,   126,   -62
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    34,    35,   144,    36,    83,   145,   232,
     125,   126,    38,    39,    40,   491,    41,    42,    43,    44,
     345,   407,   498,   544,    45,    46,    47,    48,    49,   127,
     363,    50,    51,   113,    52,   426,   474,   513,   114,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,   439,
     441,   323,   160,    85,    86,    87,    88,    89,    90,    91,
     193,    92,    93,   176,   391,    94,   161,   162,   302,   347,
     348,   288,   289,   290,   425,   172
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      37,   311,   475,   166,   304,   531,   329,   313,   170,   210,
     492,   539,   314,   315,   316,   540,   541,   108,   109,   112,
     173,   317,   349,    84,   542,    96,   204,   543,   545,   101,
     102,   318,   546,   547,   107,   117,   361,   281,   115,   364,
      63,   586,   120,   319,   320,   539,   548,   550,   551,   540,
     541,   321,   201,   322,   552,   556,   561,     3,   542,   493,
      67,   543,   545,   470,   356,   532,   546,   547,   470,   308,
     295,   163,   470,    97,   377,     8,   202,   121,   471,   470,
     548,   550,   551,   516,   110,   118,    67,   637,   552,   556,
     561,   335,   122,   518,   642,   378,   171,    65,   358,     8,
     539,     8,   587,   100,   540,   541,   123,   350,    25,   112,
     472,    64,   357,   542,   519,   472,   543,   545,   196,   472,
      98,   546,   547,   330,   331,   124,   472,   197,   211,   631,
     212,   343,    25,   215,    25,   548,   550,   551,   353,   527,
     355,   394,   473,   552,   556,   561,   359,   473,   222,   103,
     216,   473,   365,   219,    99,   111,   230,   231,   473,   174,
     282,   220,   136,   137,   138,   139,   121,   351,   379,   104,
     175,   294,   211,   380,   217,   312,   136,   137,   138,   139,
     305,   122,   218,   309,   196,   411,     5,   116,   301,   433,
     211,   625,   128,   197,   300,   123,   380,   632,     8,   129,
       9,   105,    37,   134,   106,   405,   638,   112,   406,   112,
     130,   131,   332,   283,   124,   337,   338,   339,   340,   434,
      15,    16,   300,    18,    19,    20,   482,   140,   146,   485,
      24,    25,   572,    26,   573,   159,   392,    30,    31,   135,
     386,   147,   387,   141,   148,   142,   132,   133,   194,   195,
     511,   512,   143,   190,   191,   326,   327,   149,   164,   150,
     151,   311,   310,   152,   311,   168,   153,   313,   154,   155,
     313,   177,   314,   315,   316,   314,   315,   316,   165,   156,
     169,   317,   157,   158,   317,   178,   179,   198,   199,   200,
     203,   318,   205,   206,   318,   207,   208,   209,   213,   373,
     374,   214,   384,   319,   320,   221,   319,   320,   381,   224,
     225,   321,   226,   322,   321,   227,   322,   228,   430,   291,
     292,   293,   536,   432,   296,   445,   297,   298,   537,   336,
     341,   344,   346,   538,   549,   553,   354,   360,   458,   362,
     368,    37,   554,   462,   366,   367,   369,   370,   371,   372,
     375,    37,   555,    37,   376,   382,   536,   401,   402,    82,
     404,   388,   537,    37,   557,   558,   415,   538,   549,   553,
     488,   385,   559,   494,   560,   380,   554,   422,   423,   389,
     500,   390,   501,   502,    64,   610,   555,   396,   393,   507,
      37,   509,   395,   427,   428,   435,   436,   397,   557,   558,
     398,   609,   442,   443,   399,   400,   559,   520,   560,   408,
     523,   536,   409,   410,   416,   424,   446,   537,   440,   530,
     418,   451,   538,   549,   553,   412,   417,   309,   421,   429,
       5,   554,   431,   444,   447,   312,   448,   449,   312,   453,
     468,   555,     8,    37,     9,   450,   452,   454,   478,   455,
     456,   457,   459,   557,   558,   460,    37,   461,   463,   464,
      37,   559,   465,   560,    15,    16,   466,    18,    19,    20,
     467,   469,   479,   480,    24,    25,   483,    26,   514,   486,
     487,    30,    31,   495,   497,   515,    37,   526,   528,   565,
     496,   499,    37,   504,   566,   505,    37,   508,    37,    37,
      37,   612,   510,   517,   521,    37,   481,    37,   529,   522,
     524,   618,   570,   533,   574,   571,   575,   576,    37,   582,
     601,    37,   602,    66,    67,    68,   569,    69,    37,   583,
      37,   584,   585,   588,   578,   579,   589,   590,   603,     8,
     591,   592,    70,    71,    72,    73,   593,   594,    74,   595,
      75,    76,   596,   597,   598,   599,   600,   606,   604,   605,
     639,   607,    66,    67,    68,   611,    69,   613,   614,   615,
      77,   616,    25,   635,    78,   626,   617,   619,     8,   620,
     621,    70,    71,    72,    73,    37,   622,    74,   623,    75,
      76,   624,    79,   628,   641,    80,   284,    81,   629,    82,
     630,    66,    67,    68,   633,    69,   119,   634,   640,    77,
      37,    25,   644,    78,   645,   646,    37,     8,   647,   648,
      70,    71,    72,    73,   420,   192,    74,   223,    75,    76,
     324,    79,   229,   352,    80,   325,    81,    37,    82,   167,
     383,   477,   300,   577,   636,     0,     0,     0,    77,     0,
      25,   328,    78,   180,   181,   182,   183,   184,   185,   186,
     187,   188,   189,   190,   191,     0,     0,     0,     0,     0,
      79,   142,     0,    80,     0,    81,   299,    82,   285,   286,
       0,   300,   235,   236,     0,     0,     0,     0,   237,     0,
     238,   239,     0,   240,     0,   241,   242,   243,   244,   245,
     246,   247,   248,   249,   250,   251,   252,   253,   254,   255,
     256,   257,   258,   259,   260,   261,   262,   263,   264,   265,
     266,   267,   268,   269,   270,   271,   272,   273,   274,   275,
     276,   277,   278,   279,   280,     0,     0,     0,     0,     0,
       0,     0,     0,   476,   286,     0,     0,   235,   236,     0,
       0,     0,     0,   237,   303,   238,   239,   300,   240,     0,
     241,   242,   243,   244,   245,   246,   247,   248,   249,   250,
     251,   252,   253,   254,   255,   256,   257,   258,   259,   260,
     261,   262,   263,   264,   265,   266,   267,   268,   269,   270,
     271,   272,   273,   274,   275,   276,   277,   278,   279,   280,
       4,     0,     0,     5,     0,     6,     0,     0,     0,     0,
       7,     0,     0,     0,     0,     8,     0,     9,     0,     0,
       0,     0,   300,     0,     0,    10,     0,     0,    11,     0,
       0,    12,   342,     0,     0,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,     0,    24,    25,     0,
      26,    27,    28,    29,    30,    31,    32,     4,     0,     0,
       5,     0,     6,     0,     0,     0,     0,     7,     0,     0,
     437,     0,   438,     0,     9,     0,     0,     0,     0,    33,
       0,     0,    10,     0,     0,    11,     0,     0,    12,     0,
       0,     0,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,     0,    24,    25,     0,    26,    27,    28,
      29,    30,    31,    32,     4,     0,     0,     5,     0,     6,
       0,     0,     0,     0,     7,     0,     0,     0,     0,     8,
       0,     9,     0,     0,     0,     0,    33,     0,     0,    10,
       0,     0,    11,     0,     0,    12,     0,     0,     0,    13,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
       0,    24,    25,     0,    26,    27,    28,    29,    30,    31,
      32,     4,     0,     0,     5,     0,     6,     0,     0,     0,
       0,     7,     0,     0,     0,     0,   403,     0,     9,     0,
       0,     0,     0,    33,     0,     0,    10,     0,     0,    11,
       0,     0,    12,     0,     0,     0,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,     0,    24,    25,
       0,    26,    27,    28,    29,    30,    31,    32,     4,     0,
       0,     5,     0,     6,     0,     0,     0,     0,     7,     0,
       0,     0,     0,   413,     0,     9,     0,     0,     0,     0,
      33,     0,     0,    10,     0,     0,    11,     0,     0,    12,
       0,     0,     0,    13,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,     0,    24,    25,     0,    26,    27,
      28,    29,    30,    31,    32,     4,     0,     0,     5,     0,
       6,     0,     0,     0,     0,     7,     0,     0,     0,     0,
     414,     0,     9,     0,     0,     0,     0,    33,     0,     0,
      10,     0,     0,    11,     0,     0,    12,     0,     0,     0,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,     0,    24,    25,     0,    26,    27,    28,    29,    30,
      31,    32,     4,     0,     0,     5,     0,     6,     0,     0,
       0,     0,     7,     0,     0,     0,     0,   419,     0,     9,
       0,     0,     0,     0,    33,     0,     0,    10,     0,     0,
      11,     0,     0,    12,     0,     0,     0,    13,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,     0,    24,
      25,     0,    26,    27,    28,    29,    30,    31,    32,     4,
       0,     0,     5,     0,     6,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   489,     0,     9,     0,     0,     0,
       0,    33,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,   490,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     6,     0,     0,     0,     0,     7,     0,     0,     0,
       0,   503,     0,     9,     0,     0,     0,     0,    33,     0,
       0,    10,     0,     0,    11,     0,     0,    12,     0,     0,
       0,    13,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,     0,    24,    25,     0,    26,    27,    28,    29,
      30,    31,    32,     4,     0,     0,     5,     0,     6,     0,
       0,     0,     0,     7,     0,     0,     0,     0,   506,     0,
       9,     0,     0,     0,     0,    33,     0,     0,    10,     0,
       0,    11,     0,     0,    12,     0,     0,     0,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,     0,
      24,    25,     0,    26,    27,    28,    29,    30,    31,    32,
       4,     0,     0,     5,     0,     6,     0,     0,     0,     0,
       7,     0,     0,     0,     0,   525,     0,     9,     0,     0,
       0,     0,    33,     0,     0,    10,     0,     0,    11,     0,
       0,    12,     0,     0,     0,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,     0,    24,    25,     0,
      26,    27,    28,    29,    30,    31,    32,   309,     0,     0,
       5,     0,   534,     0,     0,     0,     0,     7,     0,     0,
       0,     0,     8,     0,     9,     0,     0,     0,     0,    33,
       0,     0,    10,     0,     0,    11,     0,     0,    12,     0,
       0,     0,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,     0,    24,    25,     0,    26,    27,    28,
      29,    30,    31,    32,     4,     0,     0,     5,     0,     6,
       0,     0,     0,     0,     7,     0,     0,     0,     0,   562,
       0,     9,     0,     0,     0,     0,   535,     0,     0,    10,
       0,     0,    11,     0,     0,    12,     0,     0,     0,    13,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
       0,    24,    25,     0,    26,    27,    28,    29,    30,    31,
      32,     4,     0,     0,     5,     0,     6,     0,     0,     0,
       0,     7,     0,     0,     0,     0,   563,     0,     9,     0,
       0,     0,     0,    33,     0,     0,    10,     0,     0,    11,
       0,     0,    12,     0,     0,     0,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,     0,    24,    25,
       0,    26,    27,    28,    29,    30,    31,    32,     4,     0,
       0,     5,     0,     6,     0,     0,     0,     0,     7,     0,
       0,     0,     0,   564,     0,     9,     0,     0,     0,     0,
      33,     0,     0,    10,     0,     0,    11,     0,     0,    12,
       0,     0,     0,    13,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,     0,    24,    25,     0,    26,    27,
      28,    29,    30,    31,    32,     4,     0,     0,     5,     0,
       6,     0,     0,     0,     0,     7,     0,     0,     0,     0,
     567,     0,     9,     0,     0,     0,     0,    33,     0,     0,
      10,     0,     0,    11,     0,     0,    12,     0,     0,     0,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,     0,    24,    25,     0,    26,    27,    28,    29,    30,
      31,    32,     4,     0,     0,     5,     0,     6,     0,     0,
       0,     0,     7,     0,     0,     0,     0,   568,     0,     9,
       0,     0,     0,     0,    33,     0,     0,    10,     0,     0,
      11,     0,     0,    12,     0,     0,     0,    13,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,     0,    24,
      25,     0,    26,    27,    28,    29,    30,    31,    32,     4,
       0,     0,     5,     0,     6,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   580,     0,     9,     0,     0,     0,
       0,    33,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     6,     0,     0,     0,     0,     7,     0,     0,     0,
       0,   581,     0,     9,     0,     0,     0,     0,    33,     0,
       0,    10,     0,     0,    11,     0,     0,    12,     0,     0,
       0,    13,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,     0,    24,    25,     0,    26,    27,    28,    29,
      30,    31,    32,     4,     0,     0,     5,     0,     6,     0,
       0,     0,     0,     7,     0,     0,     0,     0,   627,     0,
       9,     0,     0,     0,     0,    33,     0,     0,    10,     0,
       0,    11,     0,     0,    12,     0,     0,     0,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,     0,
      24,    25,     0,    26,    27,    28,    29,    30,    31,    32,
       4,     0,     0,     5,     0,     6,     0,     0,     0,     0,
       7,     0,     0,     0,     0,   643,     0,     9,     0,     0,
       0,     0,    33,     0,     0,    10,     0,     0,    11,     0,
       0,    12,     0,     0,     0,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,   309,    24,    25,     5,
      26,    27,    28,    29,    30,    31,    32,     0,     0,     0,
       0,     8,     0,     9,   180,   181,   182,   183,   184,   185,
     186,   187,   188,   189,     0,     0,     0,     0,     0,    33,
       0,     0,     0,    15,    16,     0,    18,    19,    20,     0,
       0,     0,     0,    24,    25,     0,    26,     0,     0,     0,
      30,    31,    66,    67,    68,     0,    69,     0,     0,     0,
       0,     0,     0,     0,     0,    66,    67,    68,     8,    69,
       0,    70,    71,    72,    73,   484,     0,    74,     0,    75,
      76,     8,    95,     0,    70,    71,    72,    73,     0,     0,
      74,     0,    75,    76,     0,     0,     0,     0,     0,    77,
       0,    25,     0,    78,     0,     0,     0,     0,     0,     0,
       0,     0,    77,     0,    25,     0,    78,     0,     0,     0,
       0,    79,     0,     0,    80,     0,    81,     0,    82,    66,
      67,    68,     0,    69,    79,     0,     0,    80,     0,    81,
       0,    82,     0,     0,     0,     8,     0,     0,    70,    71,
      72,    73,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    77,     0,    25,     0,
      78,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   285,   286,     0,     0,   235,   236,
       0,    80,     0,    81,   237,    82,   238,   239,     0,   240,
       0,   241,   242,   243,   244,   245,   246,   247,   248,   249,
     250,   251,   252,   253,   254,   255,   256,   257,   258,   259,
     260,   261,   262,   263,   264,   265,   266,   267,   268,   269,
     270,   271,   272,   273,   274,   275,   276,   277,   278,   279,
     280,     0,   285,   286,     0,     0,   235,   236,     0,     0,
       0,     0,   237,     0,   238,   239,   287,   240,     0,   241,
     242,   243,   244,   245,   246,   247,   248,   249,   250,   251,
     252,   253,   254,   255,   256,   257,   258,   259,   260,   261,
     262,   263,   264,   265,   266,   267,   268,   269,   270,   271,
     272,   273,   274,   275,   276,   277,   278,   279,   280,     0,
     233,     0,     0,   234,   235,   236,     0,     0,     0,     0,
     237,     0,   238,   239,   608,   240,     0,   241,   242,   243,
     244,   245,   246,   247,   248,   249,   250,   251,   252,   253,
     254,   255,   256,   257,   258,   259,   260,   261,   262,   263,
     264,   265,   266,   267,   268,   269,   270,   271,   272,   273,
     274,   275,   276,   277,   278,   279,   280,   306,     0,     0,
     307,   235,   236,     0,     0,     0,     0,   237,     0,   238,
     239,     0,   240,     0,   241,   242,   243,   244,   245,   246,
     247,   248,   249,   250,   251,   252,   253,   254,   255,   256,
     257,   258,   259,   260,   261,   262,   263,   264,   265,   266,
     267,   268,   269,   270,   271,   272,   273,   274,   275,   276,
     277,   278,   279,   280,   333,     0,     0,   334,   235,   236,
       0,     0,     0,     0,   237,     0,   238,   239,     0,   240,
       0,   241,   242,   243,   244,   245,   246,   247,   248,   249,
     250,   251,   252,   253,   254,   255,   256,   257,   258,   259,
     260,   261,   262,   263,   264,   265,   266,   267,   268,   269,
     270,   271,   272,   273,   274,   275,   276,   277,   278,   279,
     280
};

static const yytype_int16 yycheck[] =
{
       2,   177,   429,    74,   173,   494,   193,   177,    79,   111,
      34,   498,   177,   177,   177,   498,   498,    19,    20,    21,
      82,   177,   208,     7,   498,     9,   100,   498,   498,    13,
      14,   177,   498,   498,    18,     4,   222,   143,    22,   225,
       4,   530,    26,   177,   177,   532,   498,   498,   498,   532,
     532,   177,    37,   177,   498,   498,   498,     0,   532,    83,
       4,   532,   532,     4,     4,   496,   532,   532,     4,   175,
      29,    65,     4,     4,    61,    19,    61,     4,    19,     4,
     532,   532,   532,    19,     4,    54,     4,    19,   532,   532,
     532,   197,    19,    61,    19,    82,    80,    75,     4,    19,
     587,    19,   533,    83,   587,   587,    33,   209,    52,   111,
      51,    75,    52,   587,    82,    51,   587,   587,    77,    51,
       4,   587,   587,   194,   195,    52,    51,    86,    81,   618,
      83,   205,    52,     4,    52,   587,   587,   587,   212,    83,
     214,   328,    83,   587,   587,   587,    52,    83,    75,     4,
      21,    83,   226,    45,    38,    75,   140,   141,    83,    75,
     144,    53,    10,    11,    12,    13,     4,    76,    76,     4,
      86,   165,    81,    81,    45,   177,    10,    11,    12,    13,
     174,    19,    53,     4,    77,    76,     7,    50,   172,    78,
      81,    76,     4,    86,    83,    33,    81,   624,    19,     4,
      21,     4,   204,    55,     7,    15,   633,   209,    18,   211,
       4,     5,   196,    61,    52,   199,   200,   201,   202,    80,
      41,    42,    83,    44,    45,    46,   437,    61,    82,   440,
      51,    52,    81,    54,    83,    75,   310,    58,    59,    83,
     302,    83,   304,    77,    83,    79,     4,     5,    73,    74,
       4,     5,    86,    71,    72,   190,   191,    83,     4,    83,
      83,   437,    83,    83,   440,     4,    83,   437,    83,    83,
     440,    16,   437,   437,   437,   440,   440,   440,    75,    83,
      75,   437,    83,    83,   440,    27,    26,     3,    54,    75,
       4,   437,    83,    83,   440,    75,    75,    75,    86,   283,
     284,    83,   296,   437,   437,     4,   440,   440,   292,    32,
      75,   437,    83,   437,   440,     4,   440,     4,   380,    76,
      81,    76,   498,   385,    75,   399,    76,    76,   498,     4,
      37,    15,     4,   498,   498,   498,     4,    75,   412,     4,
      32,   343,   498,   417,     5,     5,    78,     6,    75,    75,
      75,   353,   498,   355,    83,    76,   532,   341,   342,    79,
     344,    76,   532,   365,   498,   498,   360,   532,   532,   532,
     444,    81,   498,   447,   498,    81,   532,   371,   372,    75,
     454,    75,   456,   457,    75,   571,   532,    75,    83,   463,
     392,   465,    78,   377,   378,   389,   390,    75,   532,   532,
      76,   570,   396,   397,    83,    33,   532,   481,   532,    76,
     484,   587,    81,    76,    76,     4,   400,   587,    17,   493,
      76,   405,   587,   587,   587,    83,    83,     4,    80,    83,
       7,   587,    76,    83,    83,   437,    83,    39,   440,    20,
     424,   587,    19,   445,    21,    16,    83,    83,   432,     4,
      83,    83,    47,   587,   587,    49,   458,    76,    83,    57,
     462,   587,    83,   587,    41,    42,    76,    44,    45,    46,
      76,    76,    76,    76,    51,    52,    14,    54,     4,    76,
      76,    58,    59,    83,    16,     4,   488,    32,   490,    47,
      83,    83,   494,    83,    55,    83,   498,    83,   500,   501,
     502,   575,    82,    75,    83,   507,    83,   509,   492,    83,
      83,   585,    75,    83,    83,    75,    83,     4,   520,    29,
      43,   523,    43,     3,     4,     5,   510,     7,   530,    83,
     532,    83,    83,    83,   518,   519,    83,    83,    47,    19,
      83,    83,    22,    23,    24,    25,    83,    83,    28,    83,
      30,    31,    83,    83,    83,    83,    83,    56,    83,    83,
     634,    55,     3,     4,     5,     5,     7,    83,    76,    14,
      50,    14,    52,    51,    54,    76,    83,    83,    19,    83,
      83,    22,    23,    24,    25,   587,    83,    28,    83,    30,
      31,    83,    72,    82,     4,    75,   145,    77,    83,    79,
      83,     3,     4,     5,    83,     7,    86,    83,    83,    50,
     612,    52,    83,    54,     4,     4,   618,    19,    83,    83,
      22,    23,    24,    25,   368,    88,    28,   125,    30,    31,
     178,    72,   134,   211,    75,   179,    77,   639,    79,    75,
     295,   430,    83,   517,   628,    -1,    -1,    -1,    50,    -1,
      52,   192,    54,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    -1,    -1,    -1,    -1,    -1,
      72,    79,    -1,    75,    -1,    77,    78,    79,     4,     5,
      -1,    83,     8,     9,    -1,    -1,    -1,    -1,    14,    -1,
      16,    17,    -1,    19,    -1,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,     4,     5,    -1,    -1,     8,     9,    -1,
      -1,    -1,    -1,    14,    80,    16,    17,    83,    19,    -1,
      21,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
       4,    -1,    -1,     7,    -1,     9,    -1,    -1,    -1,    -1,
      14,    -1,    -1,    -1,    -1,    19,    -1,    21,    -1,    -1,
      -1,    -1,    83,    -1,    -1,    29,    -1,    -1,    32,    -1,
      -1,    35,    36,    -1,    -1,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    -1,    51,    52,    -1,
      54,    55,    56,    57,    58,    59,    60,     4,    -1,    -1,
       7,    -1,     9,    -1,    -1,    -1,    -1,    14,    -1,    -1,
      17,    -1,    19,    -1,    21,    -1,    -1,    -1,    -1,    83,
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
      44,    45,    46,    47,    48,    49,     4,    51,    52,     7,
      54,    55,    56,    57,    58,    59,    60,    -1,    -1,    -1,
      -1,    19,    -1,    21,    61,    62,    63,    64,    65,    66,
      67,    68,    69,    70,    -1,    -1,    -1,    -1,    -1,    83,
      -1,    -1,    -1,    41,    42,    -1,    44,    45,    46,    -1,
      -1,    -1,    -1,    51,    52,    -1,    54,    -1,    -1,    -1,
      58,    59,     3,     4,     5,    -1,     7,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,     3,     4,     5,    19,     7,
      -1,    22,    23,    24,    25,    83,    -1,    28,    -1,    30,
      31,    19,    33,    -1,    22,    23,    24,    25,    -1,    -1,
      28,    -1,    30,    31,    -1,    -1,    -1,    -1,    -1,    50,
      -1,    52,    -1,    54,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    50,    -1,    52,    -1,    54,    -1,    -1,    -1,
      -1,    72,    -1,    -1,    75,    -1,    77,    -1,    79,     3,
       4,     5,    -1,     7,    72,    -1,    -1,    75,    -1,    77,
      -1,    79,    -1,    -1,    -1,    19,    -1,    -1,    22,    23,
      24,    25,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    50,    -1,    52,    -1,
      54,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,     4,     5,    -1,    -1,     8,     9,
      -1,    75,    -1,    77,    14,    79,    16,    17,    -1,    19,
      -1,    21,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    -1,     4,     5,    -1,    -1,     8,     9,    -1,    -1,
      -1,    -1,    14,    -1,    16,    17,    76,    19,    -1,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    -1,
       4,    -1,    -1,     7,     8,     9,    -1,    -1,    -1,    -1,
      14,    -1,    16,    17,    76,    19,    -1,    21,    22,    23,
      24,    25,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,     4,    -1,    -1,
       7,     8,     9,    -1,    -1,    -1,    -1,    14,    -1,    16,
      17,    -1,    19,    -1,    21,    22,    23,    24,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,     4,    -1,    -1,     7,     8,     9,
      -1,    -1,    -1,    -1,    14,    -1,    16,    17,    -1,    19,
      -1,    21,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60
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
     145,   139,   162,   162,    75,    86,   150,    16,    27,    26,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    95,   147,    73,    74,    77,    86,     3,    54,
      75,    37,    61,     4,    89,    83,    83,    75,    75,    75,
     120,    81,    83,    86,    83,     4,    21,    45,    53,    45,
      53,     4,    75,    98,    32,    75,    83,     4,     4,   116,
     139,   139,    96,     4,     7,     8,     9,    14,    16,    17,
      19,    21,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,   159,   139,    61,    92,     4,     5,    76,   158,   159,
     160,    76,    81,    76,   153,    29,    75,    76,    76,    78,
      83,   139,   155,    80,   160,   153,     4,     7,   159,     4,
      83,    91,    94,    99,   100,   115,   127,   128,   129,   131,
     132,   133,   134,   138,   141,   142,   144,   144,   147,   143,
     145,   145,   139,     4,     7,   159,     4,   139,   139,   139,
     139,    37,    36,    89,    15,   107,     4,   156,   157,   156,
     120,    76,   125,    89,     4,    89,     4,    52,     4,    52,
      75,   156,     4,   117,   156,    89,     5,     5,    32,    78,
       6,    75,    75,   139,   139,    75,    83,    61,    82,    76,
      81,   139,    76,   149,   153,    81,   162,   162,    76,    75,
      75,   151,    89,    83,   143,    78,    75,    75,    76,    83,
      33,   139,   139,    19,   139,    15,    18,   108,    76,    81,
      76,    76,    83,    19,    19,   153,    76,    83,    76,    19,
     117,    80,   153,   153,     4,   161,   122,   139,   139,    83,
     162,    76,   162,    78,    80,   153,   153,    17,    19,   136,
      17,   137,   153,   153,    83,    89,   139,    83,    83,    39,
      16,   139,    83,    20,    83,     4,    83,    83,    89,    47,
      49,    76,    89,    83,    57,    83,    76,    76,   139,    76,
       4,    19,    51,    83,   123,   122,     4,   158,   139,    76,
      76,    83,   138,    14,    83,   138,    76,    76,    89,    19,
      52,   102,    34,    83,    89,    83,    83,    16,   109,    83,
      89,    89,    89,    19,    83,    83,    19,    89,    83,    89,
      82,     4,     5,   124,     4,     4,    19,    75,    61,    82,
      89,    83,    83,    89,    83,    19,    32,    83,    94,   139,
      89,   102,   109,    83,     9,    83,    91,    99,   100,   101,
     103,   104,   105,   106,   110,   111,   112,   113,   114,   115,
     118,   119,   126,   127,   128,   129,   130,   131,   132,   133,
     134,   135,    19,    19,    19,    47,    55,    19,    19,   139,
      75,    75,    81,    83,    83,    83,     4,   161,   139,   139,
      19,    19,    29,    83,    83,    83,   102,   109,    83,    83,
      83,    83,    83,    83,    83,    83,    83,    83,    83,    83,
      83,    43,    43,    47,    83,    83,    56,    55,    76,   160,
     156,     5,    89,    83,    76,    14,    14,    83,    89,    83,
      83,    83,    83,    83,    83,    76,    76,    19,    82,    83,
      83,   102,   122,    83,    83,    51,   139,    19,   122,    89,
      83,     4,    19,    19,    83,     4,     4,    83,    83
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
     113,   114,   115,   115,   115,   115,   115,   115,   116,   116,
     117,   118,   118,   118,   119,   120,   120,   121,   121,   122,
     122,   122,   123,   123,   123,   123,   123,   124,   124,   125,
     125,   126,   127,   127,   127,   127,   127,   127,   127,   127,
     128,   129,   129,   130,   131,   132,   133,   133,   134,   134,
     135,   135,   136,   136,   136,   137,   137,   137,   138,   138,
     138,   138,   138,   138,   138,   138,   138,   138,   138,   139,
     140,   140,   141,   141,   142,   142,   142,   143,   143,   143,
     144,   144,   144,   145,   145,   145,   145,   145,   145,   146,
     146,   146,   146,   146,   147,   147,   147,   147,   147,   147,
     147,   147,   147,   147,   148,   148,   148,   148,   148,   148,
     148,   148,   148,   148,   148,   148,   148,   148,   148,   149,
     149,   150,   150,   150,   150,   151,   151,   152,   152,   153,
     153,   154,   154,   155,   155,   156,   156,   157,   157,   158,
     158,   159,   159,   159,   159,   159,   159,   159,   159,   159,
     159,   159,   159,   159,   159,   159,   159,   159,   159,   159,
     159,   159,   159,   159,   159,   159,   159,   159,   159,   159,
     159,   159,   159,   159,   159,   159,   159,   159,   159,   159,
     159,   159,   159,   159,   159,   159,   159,   159,   160,   160,
     160,   160,   160,   160,   161,   161,   162,   162
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
      10,     7,     2,     2,     2,     2,     4,     4,     1,     4,
       1,     9,     7,    10,     2,     1,     3,    10,     9,     0,
       2,     2,     3,    10,    10,     9,     7,     1,     3,     1,
       3,     7,     4,     4,     3,     4,     4,     3,     3,     3,
       2,     1,     2,     2,     2,     2,     1,     2,     1,     2,
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
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2514 "src/parser.tab.c"
        break;

    case YYSYMBOL_STRING: /* STRING  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2520 "src/parser.tab.c"
        break;

    case YYSYMBOL_LENS_CONTENT: /* LENS_CONTENT  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2526 "src/parser.tab.c"
        break;

    case YYSYMBOL_QUALIFIED_IDENT: /* QUALIFIED_IDENT  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2532 "src/parser.tab.c"
        break;

    case YYSYMBOL_program: /* program  */
#line 495 "src/parser.y"
            { (void) ((*yyvaluep).stmt_list); }
#line 2538 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement_list: /* statement_list  */
#line 475 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2544 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement: /* statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2550 "src/parser.tab.c"
        break;

    case YYSYMBOL_assignment: /* assignment  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2556 "src/parser.tab.c"
        break;

    case YYSYMBOL_lvalue: /* lvalue  */
#line 473 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2562 "src/parser.tab.c"
        break;

    case YYSYMBOL_variable_name: /* variable_name  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2568 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_lens: /* comparison_lens  */
#line 480 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 2574 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_name: /* modifier_name  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2580 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_word: /* modifier_word  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2586 "src/parser.tab.c"
        break;

    case YYSYMBOL_print_statement: /* print_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2592 "src/parser.tab.c"
        break;

    case YYSYMBOL_call_statement: /* call_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2598 "src/parser.tab.c"
        break;

    case YYSYMBOL_with_lock_statement: /* with_lock_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2604 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_end: /* for_end  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2610 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_each_statement: /* for_each_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2616 "src/parser.tab.c"
        break;

    case YYSYMBOL_do_loop_statement: /* do_loop_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2622 "src/parser.tab.c"
        break;

    case YYSYMBOL_while_statement: /* while_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2628 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement: /* consider_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2634 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_branch_list: /* consider_branch_list  */
#line 478 "src/parser.y"
            { ast_free_consider_branch_list(((*yyvaluep).consider_branch_list)); }
#line 2640 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_else_opt: /* consider_else_opt  */
#line 475 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2646 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement_list: /* consider_statement_list  */
#line 475 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2652 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_body_statement: /* consider_body_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2658 "src/parser.tab.c"
        break;

    case YYSYMBOL_function_statement: /* function_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2664 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_statement: /* modifier_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2670 "src/parser.tab.c"
        break;

    case YYSYMBOL_program_statement: /* program_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2676 "src/parser.tab.c"
        break;

    case YYSYMBOL_library_statement: /* library_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2682 "src/parser.tab.c"
        break;

    case YYSYMBOL_use_statement: /* use_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2688 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_signature: /* modifier_signature  */
#line 481 "src/parser.y"
            { ast_free_modifier_signature(((*yyvaluep).modifier_signature)); }
#line 2694 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_context: /* modifier_context  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2700 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_statement: /* watch_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2706 "src/parser.tab.c"
        break;

    case YYSYMBOL_unwatch_statement: /* unwatch_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2712 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_list: /* watch_target_list  */
#line 479 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2718 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_statement: /* server_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2724 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item_list: /* server_item_list  */
#line 485 "src/parser.y"
            { ast_free_server_item_list(((*yyvaluep).server_item_list)); }
#line 2730 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item: /* server_item  */
#line 484 "src/parser.y"
            { AstServerItemList one = ast_server_item_list_append(ast_server_item_list_empty(), ((*yyvaluep).server_item)); ast_free_server_item_list(one); }
#line 2736 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_string_list: /* server_string_list  */
#line 479 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2742 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_path: /* watch_target_path  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2748 "src/parser.tab.c"
        break;

    case YYSYMBOL_without_watchers_statement: /* without_watchers_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2754 "src/parser.tab.c"
        break;

    case YYSYMBOL_on_error_statement: /* on_error_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2760 "src/parser.tab.c"
        break;

    case YYSYMBOL_error_statement: /* error_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2766 "src/parser.tab.c"
        break;

    case YYSYMBOL_return_statement: /* return_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2772 "src/parser.tab.c"
        break;

    case YYSYMBOL_label_statement: /* label_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2778 "src/parser.tab.c"
        break;

    case YYSYMBOL_goto_statement: /* goto_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2784 "src/parser.tab.c"
        break;

    case YYSYMBOL_gosub_statement: /* gosub_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2790 "src/parser.tab.c"
        break;

    case YYSYMBOL_break_statement: /* break_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2796 "src/parser.tab.c"
        break;

    case YYSYMBOL_continue_statement: /* continue_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2802 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_statement: /* if_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2808 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_block_tail: /* if_block_tail  */
#line 475 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2814 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_inline_tail: /* if_inline_tail  */
#line 475 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2820 "src/parser.tab.c"
        break;

    case YYSYMBOL_inline_statement: /* inline_statement  */
#line 474 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2826 "src/parser.tab.c"
        break;

    case YYSYMBOL_expression: /* expression  */
#line 473 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2832 "src/parser.tab.c"
        break;

    case YYSYMBOL_or_expression: /* or_expression  */
#line 473 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2838 "src/parser.tab.c"
        break;

    case YYSYMBOL_and_expression: /* and_expression  */
#line 473 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2844 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_expression: /* comparison_expression  */
#line 473 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2850 "src/parser.tab.c"
        break;

    case YYSYMBOL_additive_expression: /* additive_expression  */
#line 473 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2856 "src/parser.tab.c"
        break;

    case YYSYMBOL_multiplicative_expression: /* multiplicative_expression  */
#line 473 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2862 "src/parser.tab.c"
        break;

    case YYSYMBOL_unary_expression: /* unary_expression  */
#line 473 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2868 "src/parser.tab.c"
        break;

    case YYSYMBOL_postfix_expression: /* postfix_expression  */
#line 473 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2874 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_operator: /* comparison_operator  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2880 "src/parser.tab.c"
        break;

    case YYSYMBOL_primary: /* primary  */
#line 473 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2886 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_literal: /* record_literal  */
#line 473 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2892 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_suffix: /* ident_suffix  */
#line 482 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 2898 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_dot_suffix: /* ident_dot_suffix  */
#line 482 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 2904 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list_opt: /* argument_list_opt  */
#line 476 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 2910 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list: /* argument_list  */
#line 476 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 2916 "src/parser.tab.c"
        break;

    case YYSYMBOL_array_argument_list: /* array_argument_list  */
#line 476 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 2922 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list_opt: /* parameter_list_opt  */
#line 479 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2928 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list: /* parameter_list  */
#line 479 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2934 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_name: /* field_name  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2940 "src/parser.tab.c"
        break;

    case YYSYMBOL_dot_field_name: /* dot_field_name  */
#line 472 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2946 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_field_list: /* record_field_list  */
#line 477 "src/parser.y"
            { ast_free_record_field_list(((*yyvaluep).record_field_list)); }
#line 2952 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_policy: /* field_policy  */
#line 483 "src/parser.y"
            { ast_free_expr(((*yyvaluep).field_policy).reset_expr); }
#line 2958 "src/parser.tab.c"
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
#line 3264 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 504 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3270 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 505 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3276 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 506 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3282 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 510 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3288 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 511 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3294 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 512 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3300 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 513 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3306 "src/parser.tab.c"
    break;

  case 10: /* statement: for_each_statement  */
#line 514 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3312 "src/parser.tab.c"
    break;

  case 11: /* statement: while_statement  */
#line 515 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3318 "src/parser.tab.c"
    break;

  case 12: /* statement: do_loop_statement  */
#line 516 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3324 "src/parser.tab.c"
    break;

  case 13: /* statement: consider_statement  */
#line 517 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3330 "src/parser.tab.c"
    break;

  case 14: /* statement: function_statement  */
#line 518 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3336 "src/parser.tab.c"
    break;

  case 15: /* statement: modifier_statement  */
#line 519 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3342 "src/parser.tab.c"
    break;

  case 16: /* statement: program_statement  */
#line 520 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3348 "src/parser.tab.c"
    break;

  case 17: /* statement: library_statement  */
#line 521 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3354 "src/parser.tab.c"
    break;

  case 18: /* statement: use_statement NEWLINE  */
#line 522 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3360 "src/parser.tab.c"
    break;

  case 19: /* statement: watch_statement  */
#line 523 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3366 "src/parser.tab.c"
    break;

  case 20: /* statement: server_statement  */
#line 524 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3372 "src/parser.tab.c"
    break;

  case 21: /* statement: unwatch_statement NEWLINE  */
#line 525 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3378 "src/parser.tab.c"
    break;

  case 22: /* statement: without_watchers_statement  */
#line 526 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3384 "src/parser.tab.c"
    break;

  case 23: /* statement: on_error_statement NEWLINE  */
#line 527 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3390 "src/parser.tab.c"
    break;

  case 24: /* statement: error_statement NEWLINE  */
#line 528 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3396 "src/parser.tab.c"
    break;

  case 25: /* statement: return_statement NEWLINE  */
#line 529 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3402 "src/parser.tab.c"
    break;

  case 26: /* statement: label_statement NEWLINE  */
#line 530 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3408 "src/parser.tab.c"
    break;

  case 27: /* statement: goto_statement NEWLINE  */
#line 531 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3414 "src/parser.tab.c"
    break;

  case 28: /* statement: gosub_statement NEWLINE  */
#line 532 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3420 "src/parser.tab.c"
    break;

  case 29: /* statement: break_statement NEWLINE  */
#line 533 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3426 "src/parser.tab.c"
    break;

  case 30: /* statement: continue_statement NEWLINE  */
#line 534 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3432 "src/parser.tab.c"
    break;

  case 31: /* statement: if_statement  */
#line 535 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3438 "src/parser.tab.c"
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
#line 3451 "src/parser.tab.c"
    break;

  case 33: /* assignment: lvalue OP_EQ expression  */
#line 552 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 3457 "src/parser.tab.c"
    break;

  case 34: /* assignment: lvalue compound_op expression  */
#line 557 "src/parser.y"
                                    { (yyval.stmt) = ast_assign_op((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr), (yyvsp[-1].op_char)); }
#line 3463 "src/parser.tab.c"
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
#line 3477 "src/parser.tab.c"
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
#line 3491 "src/parser.tab.c"
    break;

  case 37: /* compound_op: PLUS_EQ  */
#line 582 "src/parser.y"
               { (yyval.op_char) = '+'; }
#line 3497 "src/parser.tab.c"
    break;

  case 38: /* compound_op: MINUS_EQ  */
#line 583 "src/parser.y"
               { (yyval.op_char) = '-'; }
#line 3503 "src/parser.tab.c"
    break;

  case 39: /* compound_op: STAR_EQ  */
#line 584 "src/parser.y"
               { (yyval.op_char) = '*'; }
#line 3509 "src/parser.tab.c"
    break;

  case 40: /* compound_op: SLASH_EQ  */
#line 585 "src/parser.y"
               { (yyval.op_char) = '/'; }
#line 3515 "src/parser.tab.c"
    break;

  case 41: /* lvalue: variable_name  */
#line 589 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3521 "src/parser.tab.c"
    break;

  case 42: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 590 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3527 "src/parser.tab.c"
    break;

  case 43: /* lvalue: lvalue DOT dot_field_name  */
#line 591 "src/parser.y"
                                             { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3533 "src/parser.tab.c"
    break;

  case 44: /* variable_name: IDENT  */
#line 595 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 3539 "src/parser.tab.c"
    break;

  case 45: /* variable_name: END  */
#line 596 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 3545 "src/parser.tab.c"
    break;

  case 46: /* variable_name: NEXT  */
#line 597 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 3551 "src/parser.tab.c"
    break;

  case 47: /* $@1: %empty  */
#line 608 "src/parser.y"
             { lexer_begin_lens_content(ctx->active_lexer); }
#line 3557 "src/parser.tab.c"
    break;

  case 48: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 608 "src/parser.y"
                                                                                  {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 3565 "src/parser.tab.c"
    break;

  case 49: /* modifier_name: modifier_word  */
#line 614 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 3571 "src/parser.tab.c"
    break;

  case 50: /* modifier_name: modifier_name modifier_word  */
#line 615 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 3577 "src/parser.tab.c"
    break;

  case 51: /* modifier_word: IDENT  */
#line 619 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 3583 "src/parser.tab.c"
    break;

  case 52: /* modifier_word: TO  */
#line 620 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 3589 "src/parser.tab.c"
    break;

  case 53: /* modifier_word: END  */
#line 621 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 3595 "src/parser.tab.c"
    break;

  case 54: /* modifier_word: NEXT  */
#line 622 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 3601 "src/parser.tab.c"
    break;

  case 55: /* print_statement: PRINT expression  */
#line 626 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 3607 "src/parser.tab.c"
    break;

  case 56: /* print_statement: PRINT TO ERROR_VALUE expression  */
#line 632 "src/parser.y"
                                      { (yyval.stmt) = ast_print_error((yyvsp[0].expr)); }
#line 3613 "src/parser.tab.c"
    break;

  case 57: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 636 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 3619 "src/parser.tab.c"
    break;

  case 58: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 637 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 3630 "src/parser.tab.c"
    break;

  case 59: /* call_statement: lvalue DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 643 "src/parser.y"
                                                       {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 3640 "src/parser.tab.c"
    break;

  case 60: /* call_statement: lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 648 "src/parser.y"
                                                                 {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 3654 "src/parser.tab.c"
    break;

  case 61: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 657 "src/parser.y"
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
#line 3669 "src/parser.tab.c"
    break;

  case 62: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 670 "src/parser.y"
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
#line 3686 "src/parser.tab.c"
    break;

  case 63: /* for_end: END FOR NEWLINE  */
#line 693 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 3692 "src/parser.tab.c"
    break;

  case 64: /* for_end: NEXT NEWLINE  */
#line 694 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 3698 "src/parser.tab.c"
    break;

  case 65: /* for_end: NEXT variable_name NEWLINE  */
#line 695 "src/parser.y"
                                 { (yyval.text) = (yyvsp[-1].text); }
#line 3704 "src/parser.tab.c"
    break;

  case 66: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list for_end  */
#line 699 "src/parser.y"
                                                             {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3713 "src/parser.tab.c"
    break;

  case 67: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list for_end  */
#line 703 "src/parser.y"
                                                                  {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3722 "src/parser.tab.c"
    break;

  case 68: /* for_each_statement: FOR IDENT OP_EQ expression TO expression NEWLINE statement_list for_end  */
#line 710 "src/parser.y"
                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].expr), NULL, (yyvsp[-1].stmt_list));
      }
#line 3731 "src/parser.tab.c"
    break;

  case 69: /* for_each_statement: FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list for_end  */
#line 714 "src/parser.y"
                                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-9].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-9].text), (yyvsp[-7].expr), (yyvsp[-5].expr), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3740 "src/parser.tab.c"
    break;

  case 70: /* do_loop_statement: DO NEWLINE statement_list UNTIL expression NEWLINE  */
#line 735 "src/parser.y"
                                                         {
        (yyval.stmt) = ast_do_loop((yyvsp[-3].stmt_list), (yyvsp[-1].expr));
      }
#line 3748 "src/parser.tab.c"
    break;

  case 71: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 741 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 3756 "src/parser.tab.c"
    break;

  case 72: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 747 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 3764 "src/parser.tab.c"
    break;

  case 73: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 753 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 3772 "src/parser.tab.c"
    break;

  case 74: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 756 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 3780 "src/parser.tab.c"
    break;

  case 75: /* consider_else_opt: %empty  */
#line 762 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3786 "src/parser.tab.c"
    break;

  case 76: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 763 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3792 "src/parser.tab.c"
    break;

  case 77: /* consider_statement_list: %empty  */
#line 767 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3798 "src/parser.tab.c"
    break;

  case 78: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 768 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3804 "src/parser.tab.c"
    break;

  case 79: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 769 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3810 "src/parser.tab.c"
    break;

  case 80: /* consider_body_statement: assignment NEWLINE  */
#line 773 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3816 "src/parser.tab.c"
    break;

  case 81: /* consider_body_statement: print_statement NEWLINE  */
#line 774 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3822 "src/parser.tab.c"
    break;

  case 82: /* consider_body_statement: call_statement NEWLINE  */
#line 775 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3828 "src/parser.tab.c"
    break;

  case 83: /* consider_body_statement: with_lock_statement  */
#line 776 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3834 "src/parser.tab.c"
    break;

  case 84: /* consider_body_statement: for_each_statement  */
#line 777 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3840 "src/parser.tab.c"
    break;

  case 85: /* consider_body_statement: while_statement  */
#line 778 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3846 "src/parser.tab.c"
    break;

  case 86: /* consider_body_statement: do_loop_statement  */
#line 779 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3852 "src/parser.tab.c"
    break;

  case 87: /* consider_body_statement: consider_statement  */
#line 780 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3858 "src/parser.tab.c"
    break;

  case 88: /* consider_body_statement: function_statement  */
#line 781 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3864 "src/parser.tab.c"
    break;

  case 89: /* consider_body_statement: modifier_statement  */
#line 782 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3870 "src/parser.tab.c"
    break;

  case 90: /* consider_body_statement: program_statement  */
#line 783 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3876 "src/parser.tab.c"
    break;

  case 91: /* consider_body_statement: library_statement  */
#line 784 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3882 "src/parser.tab.c"
    break;

  case 92: /* consider_body_statement: use_statement NEWLINE  */
#line 785 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3888 "src/parser.tab.c"
    break;

  case 93: /* consider_body_statement: watch_statement  */
#line 786 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3894 "src/parser.tab.c"
    break;

  case 94: /* consider_body_statement: unwatch_statement NEWLINE  */
#line 787 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3900 "src/parser.tab.c"
    break;

  case 95: /* consider_body_statement: without_watchers_statement  */
#line 788 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3906 "src/parser.tab.c"
    break;

  case 96: /* consider_body_statement: on_error_statement NEWLINE  */
#line 789 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3912 "src/parser.tab.c"
    break;

  case 97: /* consider_body_statement: error_statement NEWLINE  */
#line 790 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3918 "src/parser.tab.c"
    break;

  case 98: /* consider_body_statement: return_statement NEWLINE  */
#line 791 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3924 "src/parser.tab.c"
    break;

  case 99: /* consider_body_statement: label_statement NEWLINE  */
#line 792 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3930 "src/parser.tab.c"
    break;

  case 100: /* consider_body_statement: goto_statement NEWLINE  */
#line 793 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3936 "src/parser.tab.c"
    break;

  case 101: /* consider_body_statement: gosub_statement NEWLINE  */
#line 794 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3942 "src/parser.tab.c"
    break;

  case 102: /* consider_body_statement: break_statement NEWLINE  */
#line 795 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3948 "src/parser.tab.c"
    break;

  case 103: /* consider_body_statement: continue_statement NEWLINE  */
#line 796 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3954 "src/parser.tab.c"
    break;

  case 104: /* consider_body_statement: if_statement  */
#line 797 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3960 "src/parser.tab.c"
    break;

  case 105: /* consider_body_statement: DIM  */
#line 803 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 3973 "src/parser.tab.c"
    break;

  case 106: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 814 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 3981 "src/parser.tab.c"
    break;

  case 107: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 817 "src/parser.y"
                                                                                                            {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 3990 "src/parser.tab.c"
    break;

  case 108: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 824 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 3998 "src/parser.tab.c"
    break;

  case 109: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 827 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 4006 "src/parser.tab.c"
    break;

  case 110: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 833 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4014 "src/parser.tab.c"
    break;

  case 111: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 839 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 4022 "src/parser.tab.c"
    break;

  case 112: /* use_statement: USE IDENT  */
#line 845 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4028 "src/parser.tab.c"
    break;

  case 113: /* use_statement: LOAD IDENT  */
#line 846 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4034 "src/parser.tab.c"
    break;

  case 114: /* use_statement: USE STRING  */
#line 847 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4040 "src/parser.tab.c"
    break;

  case 115: /* use_statement: LOAD STRING  */
#line 848 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4046 "src/parser.tab.c"
    break;

  case 116: /* use_statement: USE IDENT IDENT STRING  */
#line 849 "src/parser.y"
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
#line 4067 "src/parser.tab.c"
    break;

  case 117: /* use_statement: LOAD IDENT IDENT STRING  */
#line 865 "src/parser.y"
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
#line 4088 "src/parser.tab.c"
    break;

  case 118: /* modifier_signature: modifier_name  */
#line 884 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 4094 "src/parser.tab.c"
    break;

  case 119: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 885 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 4100 "src/parser.tab.c"
    break;

  case 120: /* modifier_context: IDENT  */
#line 889 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4106 "src/parser.tab.c"
    break;

  case 121: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 893 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4114 "src/parser.tab.c"
    break;

  case 122: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 896 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 4122 "src/parser.tab.c"
    break;

  case 123: /* watch_statement: WATCH IDENT LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 904 "src/parser.y"
                                                                                           {
        (yyval.stmt) = ast_watch((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4130 "src/parser.tab.c"
    break;

  case 124: /* unwatch_statement: UNWATCH expression  */
#line 910 "src/parser.y"
                         { (yyval.stmt) = ast_unwatch((yyvsp[0].expr)); }
#line 4136 "src/parser.tab.c"
    break;

  case 125: /* watch_target_list: watch_target_path  */
#line 914 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4142 "src/parser.tab.c"
    break;

  case 126: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 915 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4148 "src/parser.tab.c"
    break;

  case 127: /* server_statement: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 934 "src/parser.y"
                                                                                             {
        (yyval.stmt) = ast_server((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4156 "src/parser.tab.c"
    break;

  case 128: /* server_statement: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 937 "src/parser.y"
                                                                           {
        (yyval.stmt) = ast_server((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4164 "src/parser.tab.c"
    break;

  case 129: /* server_item_list: %empty  */
#line 943 "src/parser.y"
             { (yyval.server_item_list) = ast_server_item_list_empty(); }
#line 4170 "src/parser.tab.c"
    break;

  case 130: /* server_item_list: server_item_list NEWLINE  */
#line 944 "src/parser.y"
                               { (yyval.server_item_list) = (yyvsp[-1].server_item_list); }
#line 4176 "src/parser.tab.c"
    break;

  case 131: /* server_item_list: server_item_list server_item  */
#line 945 "src/parser.y"
                                   { (yyval.server_item_list) = ast_server_item_list_append((yyvsp[-1].server_item_list), (yyvsp[0].server_item)); }
#line 4182 "src/parser.tab.c"
    break;

  case 132: /* server_item: IDENT server_string_list NEWLINE  */
#line 949 "src/parser.y"
                                       {
        (yyval.server_item) = ast_server_directive((yyvsp[-2].text), (yyvsp[-1].name_list), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 4190 "src/parser.tab.c"
    break;

  case 133: /* server_item: IDENT STRING LPAREN parameter_list_opt RPAREN NEWLINE statement_list END IDENT NEWLINE  */
#line 952 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_handler((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4198 "src/parser.tab.c"
    break;

  case 134: /* server_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 955 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_site((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4206 "src/parser.tab.c"
    break;

  case 135: /* server_item: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 958 "src/parser.y"
                                                                           {
        (yyval.server_item) = ast_server_site((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 4214 "src/parser.tab.c"
    break;

  case 136: /* server_item: ON IDENT NEWLINE statement_list END ON NEWLINE  */
#line 961 "src/parser.y"
                                                     {
        (yyval.server_item) = ast_server_hook((yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 4222 "src/parser.tab.c"
    break;

  case 137: /* server_string_list: STRING  */
#line 967 "src/parser.y"
             { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4228 "src/parser.tab.c"
    break;

  case 138: /* server_string_list: server_string_list COMMA STRING  */
#line 968 "src/parser.y"
                                      { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4234 "src/parser.tab.c"
    break;

  case 139: /* watch_target_path: variable_name  */
#line 972 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 4240 "src/parser.tab.c"
    break;

  case 140: /* watch_target_path: watch_target_path DOT IDENT  */
#line 973 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 4246 "src/parser.tab.c"
    break;

  case 141: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 977 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 4254 "src/parser.tab.c"
    break;

  case 142: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 983 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 4260 "src/parser.tab.c"
    break;

  case 143: /* on_error_statement: ON ERROR_VALUE GOTO NEXT  */
#line 984 "src/parser.y"
                               { (yyval.stmt) = ast_on_error_goto_next(); }
#line 4266 "src/parser.tab.c"
    break;

  case 144: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 985 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 4272 "src/parser.tab.c"
    break;

  case 145: /* on_error_statement: ON IDENT GOTO NEXT  */
#line 986 "src/parser.y"
                         {
        if (!warn_channel_ok(ctx, (yyvsp[-2].text), (yylsp[-2]).first_line, (yylsp[-2]).first_column)) { YYERROR; }
        free((yyvsp[-2].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_NEXT);
      }
#line 4282 "src/parser.tab.c"
    break;

  case 146: /* on_error_statement: ON IDENT GOTO IDENT  */
#line 991 "src/parser.y"
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
#line 4300 "src/parser.tab.c"
    break;

  case 147: /* on_error_statement: ON IDENT STOP  */
#line 1004 "src/parser.y"
                    {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_STOP);
      }
#line 4310 "src/parser.tab.c"
    break;

  case 148: /* on_error_statement: ON IDENT PRINT  */
#line 1009 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_PRINT);
      }
#line 4320 "src/parser.tab.c"
    break;

  case 149: /* on_error_statement: ON IDENT IDENT  */
#line 1014 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        int mode = warn_mode_word(ctx, (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column);
        if (mode < 0) { free((yyvsp[-1].text)); free((yyvsp[0].text)); YYERROR; }
        free((yyvsp[-1].text)); free((yyvsp[0].text));
        (yyval.stmt) = ast_on_warning(mode);
      }
#line 4332 "src/parser.tab.c"
    break;

  case 150: /* error_statement: ERROR_VALUE expression  */
#line 1024 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 4338 "src/parser.tab.c"
    break;

  case 151: /* return_statement: RETURN  */
#line 1028 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 4344 "src/parser.tab.c"
    break;

  case 152: /* return_statement: RETURN expression  */
#line 1029 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 4350 "src/parser.tab.c"
    break;

  case 153: /* label_statement: variable_name COLON  */
#line 1033 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 4356 "src/parser.tab.c"
    break;

  case 154: /* goto_statement: GOTO variable_name  */
#line 1040 "src/parser.y"
                         { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 4362 "src/parser.tab.c"
    break;

  case 155: /* gosub_statement: GOSUB variable_name  */
#line 1044 "src/parser.y"
                          { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 4368 "src/parser.tab.c"
    break;

  case 156: /* break_statement: BREAK  */
#line 1053 "src/parser.y"
            { (yyval.stmt) = ast_break(NULL); }
#line 4374 "src/parser.tab.c"
    break;

  case 157: /* break_statement: BREAK IDENT  */
#line 1054 "src/parser.y"
                  { (yyval.stmt) = ast_break((yyvsp[0].text)); }
#line 4380 "src/parser.tab.c"
    break;

  case 158: /* continue_statement: CONTINUE  */
#line 1058 "src/parser.y"
               { (yyval.stmt) = ast_continue(NULL); }
#line 4386 "src/parser.tab.c"
    break;

  case 159: /* continue_statement: CONTINUE IDENT  */
#line 1059 "src/parser.y"
                     { (yyval.stmt) = ast_continue((yyvsp[0].text)); }
#line 4392 "src/parser.tab.c"
    break;

  case 160: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1063 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4401 "src/parser.tab.c"
    break;

  case 161: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1067 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4410 "src/parser.tab.c"
    break;

  case 162: /* if_block_tail: END IF NEWLINE  */
#line 1074 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4418 "src/parser.tab.c"
    break;

  case 163: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 1077 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4426 "src/parser.tab.c"
    break;

  case 164: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1080 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4434 "src/parser.tab.c"
    break;

  case 165: /* if_inline_tail: %empty  */
#line 1086 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4442 "src/parser.tab.c"
    break;

  case 166: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 1089 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4450 "src/parser.tab.c"
    break;

  case 167: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1092 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4458 "src/parser.tab.c"
    break;

  case 168: /* inline_statement: assignment  */
#line 1098 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4464 "src/parser.tab.c"
    break;

  case 169: /* inline_statement: print_statement  */
#line 1099 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4470 "src/parser.tab.c"
    break;

  case 170: /* inline_statement: call_statement  */
#line 1100 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4476 "src/parser.tab.c"
    break;

  case 171: /* inline_statement: use_statement  */
#line 1101 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4482 "src/parser.tab.c"
    break;

  case 172: /* inline_statement: on_error_statement  */
#line 1102 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4488 "src/parser.tab.c"
    break;

  case 173: /* inline_statement: error_statement  */
#line 1103 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4494 "src/parser.tab.c"
    break;

  case 174: /* inline_statement: return_statement  */
#line 1104 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4500 "src/parser.tab.c"
    break;

  case 175: /* inline_statement: goto_statement  */
#line 1105 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4506 "src/parser.tab.c"
    break;

  case 176: /* inline_statement: gosub_statement  */
#line 1106 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4512 "src/parser.tab.c"
    break;

  case 177: /* inline_statement: break_statement  */
#line 1107 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4518 "src/parser.tab.c"
    break;

  case 178: /* inline_statement: continue_statement  */
#line 1108 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4524 "src/parser.tab.c"
    break;

  case 179: /* expression: or_expression  */
#line 1112 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 4530 "src/parser.tab.c"
    break;

  case 180: /* or_expression: and_expression  */
#line 1116 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 4536 "src/parser.tab.c"
    break;

  case 181: /* or_expression: or_expression OR and_expression  */
#line 1117 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4542 "src/parser.tab.c"
    break;

  case 182: /* and_expression: comparison_expression  */
#line 1121 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 4548 "src/parser.tab.c"
    break;

  case 183: /* and_expression: and_expression AND comparison_expression  */
#line 1122 "src/parser.y"
                                               { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4554 "src/parser.tab.c"
    break;

  case 184: /* comparison_expression: additive_expression  */
#line 1126 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 4560 "src/parser.tab.c"
    break;

  case 185: /* comparison_expression: additive_expression comparison_operator additive_expression  */
#line 1127 "src/parser.y"
                                                                  { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4566 "src/parser.tab.c"
    break;

  case 186: /* comparison_expression: additive_expression comparison_lens comparison_operator additive_expression  */
#line 1128 "src/parser.y"
                                                                                  {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 4574 "src/parser.tab.c"
    break;

  case 187: /* additive_expression: multiplicative_expression  */
#line 1134 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 4580 "src/parser.tab.c"
    break;

  case 188: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 1135 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4586 "src/parser.tab.c"
    break;

  case 189: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 1136 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4592 "src/parser.tab.c"
    break;

  case 190: /* multiplicative_expression: unary_expression  */
#line 1140 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 4598 "src/parser.tab.c"
    break;

  case 191: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 1141 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4604 "src/parser.tab.c"
    break;

  case 192: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 1142 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4610 "src/parser.tab.c"
    break;

  case 193: /* unary_expression: postfix_expression  */
#line 1146 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 4616 "src/parser.tab.c"
    break;

  case 194: /* unary_expression: NOT unary_expression  */
#line 1147 "src/parser.y"
                           { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4622 "src/parser.tab.c"
    break;

  case 195: /* unary_expression: MINUS unary_expression  */
#line 1148 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4628 "src/parser.tab.c"
    break;

  case 196: /* unary_expression: NEW postfix_expression  */
#line 1149 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4634 "src/parser.tab.c"
    break;

  case 197: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 1150 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 4640 "src/parser.tab.c"
    break;

  case 198: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 1151 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4646 "src/parser.tab.c"
    break;

  case 199: /* postfix_expression: primary  */
#line 1155 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 4652 "src/parser.tab.c"
    break;

  case 200: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 1156 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4658 "src/parser.tab.c"
    break;

  case 201: /* postfix_expression: postfix_expression DOT dot_field_name  */
#line 1157 "src/parser.y"
                                            { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4664 "src/parser.tab.c"
    break;

  case 202: /* postfix_expression: postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1158 "src/parser.y"
                                                                   {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        (yyval.expr) = expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 4674 "src/parser.tab.c"
    break;

  case 203: /* postfix_expression: postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1163 "src/parser.y"
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
#line 4690 "src/parser.tab.c"
    break;

  case 204: /* comparison_operator: OP_EQ  */
#line 1177 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 4696 "src/parser.tab.c"
    break;

  case 205: /* comparison_operator: OP_NE  */
#line 1178 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 4702 "src/parser.tab.c"
    break;

  case 206: /* comparison_operator: OP_GT  */
#line 1179 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 4708 "src/parser.tab.c"
    break;

  case 207: /* comparison_operator: OP_LT  */
#line 1180 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 4714 "src/parser.tab.c"
    break;

  case 208: /* comparison_operator: OP_GE  */
#line 1181 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 4720 "src/parser.tab.c"
    break;

  case 209: /* comparison_operator: OP_LE  */
#line 1182 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 4726 "src/parser.tab.c"
    break;

  case 210: /* comparison_operator: OP_NGT  */
#line 1183 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 4732 "src/parser.tab.c"
    break;

  case 211: /* comparison_operator: OP_NLT  */
#line 1184 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 4738 "src/parser.tab.c"
    break;

  case 212: /* comparison_operator: OP_NGE  */
#line 1185 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 4744 "src/parser.tab.c"
    break;

  case 213: /* comparison_operator: OP_NLE  */
#line 1186 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 4750 "src/parser.tab.c"
    break;

  case 214: /* primary: NUMBER  */
#line 1190 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4756 "src/parser.tab.c"
    break;

  case 215: /* primary: WATCHERS LPAREN RPAREN  */
#line 1191 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_call(copy_const("watchers"), ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4762 "src/parser.tab.c"
    break;

  case 216: /* primary: duration_terms  */
#line 1192 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4768 "src/parser.tab.c"
    break;

  case 217: /* primary: STRING  */
#line 1193 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4774 "src/parser.tab.c"
    break;

  case 218: /* primary: variable_name ident_suffix  */
#line 1194 "src/parser.y"
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
#line 4796 "src/parser.tab.c"
    break;

  case 219: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1211 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 4807 "src/parser.tab.c"
    break;

  case 220: /* primary: ERROR_VALUE  */
#line 1217 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4813 "src/parser.tab.c"
    break;

  case 221: /* primary: TRUE  */
#line 1218 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4819 "src/parser.tab.c"
    break;

  case 222: /* primary: FALSE  */
#line 1219 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4825 "src/parser.tab.c"
    break;

  case 223: /* primary: NOTHING  */
#line 1220 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4831 "src/parser.tab.c"
    break;

  case 224: /* primary: UNKNOWN_VALUE  */
#line 1221 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4837 "src/parser.tab.c"
    break;

  case 225: /* primary: LPAREN expression RPAREN  */
#line 1222 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 4843 "src/parser.tab.c"
    break;

  case 226: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 1223 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4849 "src/parser.tab.c"
    break;

  case 227: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 1224 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4855 "src/parser.tab.c"
    break;

  case 228: /* primary: record_literal  */
#line 1225 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 4861 "src/parser.tab.c"
    break;

  case 229: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 1229 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4867 "src/parser.tab.c"
    break;

  case 230: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 1230 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4873 "src/parser.tab.c"
    break;

  case 231: /* ident_suffix: %empty  */
#line 1234 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 4883 "src/parser.tab.c"
    break;

  case 232: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 1239 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 4893 "src/parser.tab.c"
    break;

  case 233: /* ident_suffix: DOT dot_field_name ident_dot_suffix  */
#line 1244 "src/parser.y"
                                          {
        /* dot_field_name, not IDENT: a keyword is a legal FIELD name after a
         * dot, because nothing but a name can appear there. */
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 4904 "src/parser.tab.c"
    break;

  case 234: /* ident_suffix: DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1250 "src/parser.y"
                                                          {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        (yyval.ident_suffix).kind = IDENT_SUFFIX_METHOD;
        (yyval.ident_suffix).name = (yyvsp[-3].text);
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 4918 "src/parser.tab.c"
    break;

  case 235: /* ident_dot_suffix: %empty  */
#line 1262 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 4928 "src/parser.tab.c"
    break;

  case 236: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 1267 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 4938 "src/parser.tab.c"
    break;

  case 237: /* duration_terms: NUMBER IDENT  */
#line 1275 "src/parser.y"
                   {
        AstDuration duration = {0};
        (yyval.duration) = duration_add_unit(duration, (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 4947 "src/parser.tab.c"
    break;

  case 238: /* duration_terms: duration_terms NUMBER IDENT  */
#line 1279 "src/parser.y"
                                  {
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 4955 "src/parser.tab.c"
    break;

  case 239: /* argument_list_opt: %empty  */
#line 1285 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 4961 "src/parser.tab.c"
    break;

  case 240: /* argument_list_opt: argument_list  */
#line 1286 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 4967 "src/parser.tab.c"
    break;

  case 241: /* argument_list: expression  */
#line 1290 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 4973 "src/parser.tab.c"
    break;

  case 242: /* argument_list: argument_list COMMA expression  */
#line 1291 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 4979 "src/parser.tab.c"
    break;

  case 243: /* array_argument_list: expression  */
#line 1295 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 4985 "src/parser.tab.c"
    break;

  case 244: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 1296 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 4991 "src/parser.tab.c"
    break;

  case 245: /* parameter_list_opt: %empty  */
#line 1300 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 4997 "src/parser.tab.c"
    break;

  case 246: /* parameter_list_opt: parameter_list  */
#line 1301 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 5003 "src/parser.tab.c"
    break;

  case 247: /* parameter_list: IDENT  */
#line 1305 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5009 "src/parser.tab.c"
    break;

  case 248: /* parameter_list: parameter_list COMMA IDENT  */
#line 1306 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5015 "src/parser.tab.c"
    break;

  case 249: /* field_name: dot_field_name  */
#line 1319 "src/parser.y"
                     { (yyval.text) = (yyvsp[0].text); }
#line 5021 "src/parser.tab.c"
    break;

  case 250: /* field_name: STRING  */
#line 1326 "src/parser.y"
             { (yyval.text) = (yyvsp[0].text); }
#line 5027 "src/parser.tab.c"
    break;

  case 251: /* dot_field_name: IDENT  */
#line 1335 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 5033 "src/parser.tab.c"
    break;

  case 252: /* dot_field_name: AS  */
#line 1336 "src/parser.y"
                     { (yyval.text) = kw_name("as"); }
#line 5039 "src/parser.tab.c"
    break;

  case 253: /* dot_field_name: NEXT  */
#line 1337 "src/parser.y"
                     { (yyval.text) = kw_name("next"); }
#line 5045 "src/parser.tab.c"
    break;

  case 254: /* dot_field_name: STOP  */
#line 1338 "src/parser.y"
                     { (yyval.text) = kw_name("stop"); }
#line 5051 "src/parser.tab.c"
    break;

  case 255: /* dot_field_name: ERROR_VALUE  */
#line 1339 "src/parser.y"
                     { (yyval.text) = kw_name("error"); }
#line 5057 "src/parser.tab.c"
    break;

  case 256: /* dot_field_name: END  */
#line 1340 "src/parser.y"
                     { (yyval.text) = kw_name("end"); }
#line 5063 "src/parser.tab.c"
    break;

  case 257: /* dot_field_name: TO  */
#line 1341 "src/parser.y"
                     { (yyval.text) = kw_name("to"); }
#line 5069 "src/parser.tab.c"
    break;

  case 258: /* dot_field_name: IN  */
#line 1342 "src/parser.y"
                     { (yyval.text) = kw_name("in"); }
#line 5075 "src/parser.tab.c"
    break;

  case 259: /* dot_field_name: ON  */
#line 1343 "src/parser.y"
                     { (yyval.text) = kw_name("on"); }
#line 5081 "src/parser.tab.c"
    break;

  case 260: /* dot_field_name: NEW  */
#line 1344 "src/parser.y"
                     { (yyval.text) = kw_name("new"); }
#line 5087 "src/parser.tab.c"
    break;

  case 261: /* dot_field_name: EACH  */
#line 1345 "src/parser.y"
                     { (yyval.text) = kw_name("each"); }
#line 5093 "src/parser.tab.c"
    break;

  case 262: /* dot_field_name: WITH  */
#line 1346 "src/parser.y"
                     { (yyval.text) = kw_name("with"); }
#line 5099 "src/parser.tab.c"
    break;

  case 263: /* dot_field_name: WITHOUT  */
#line 1347 "src/parser.y"
                     { (yyval.text) = kw_name("without"); }
#line 5105 "src/parser.tab.c"
    break;

  case 264: /* dot_field_name: THEN  */
#line 1348 "src/parser.y"
                     { (yyval.text) = kw_name("then"); }
#line 5111 "src/parser.tab.c"
    break;

  case 265: /* dot_field_name: ELSE  */
#line 1349 "src/parser.y"
                     { (yyval.text) = kw_name("else"); }
#line 5117 "src/parser.tab.c"
    break;

  case 266: /* dot_field_name: FOR  */
#line 1350 "src/parser.y"
                     { (yyval.text) = kw_name("for"); }
#line 5123 "src/parser.tab.c"
    break;

  case 267: /* dot_field_name: IF  */
#line 1351 "src/parser.y"
                     { (yyval.text) = kw_name("if"); }
#line 5129 "src/parser.tab.c"
    break;

  case 268: /* dot_field_name: WHILE  */
#line 1352 "src/parser.y"
                     { (yyval.text) = kw_name("while"); }
#line 5135 "src/parser.tab.c"
    break;

  case 269: /* dot_field_name: DO  */
#line 1353 "src/parser.y"
                     { (yyval.text) = kw_name("do"); }
#line 5141 "src/parser.tab.c"
    break;

  case 270: /* dot_field_name: UNTIL  */
#line 1354 "src/parser.y"
                     { (yyval.text) = kw_name("until"); }
#line 5147 "src/parser.tab.c"
    break;

  case 271: /* dot_field_name: PRINT  */
#line 1355 "src/parser.y"
                     { (yyval.text) = kw_name("print"); }
#line 5153 "src/parser.tab.c"
    break;

  case 272: /* dot_field_name: RETURN  */
#line 1356 "src/parser.y"
                     { (yyval.text) = kw_name("return"); }
#line 5159 "src/parser.tab.c"
    break;

  case 273: /* dot_field_name: LOAD  */
#line 1357 "src/parser.y"
                     { (yyval.text) = kw_name("load"); }
#line 5165 "src/parser.tab.c"
    break;

  case 274: /* dot_field_name: USE  */
#line 1358 "src/parser.y"
                     { (yyval.text) = kw_name("use"); }
#line 5171 "src/parser.tab.c"
    break;

  case 275: /* dot_field_name: NOT  */
#line 1359 "src/parser.y"
                     { (yyval.text) = kw_name("not"); }
#line 5177 "src/parser.tab.c"
    break;

  case 276: /* dot_field_name: AND  */
#line 1360 "src/parser.y"
                     { (yyval.text) = kw_name("and"); }
#line 5183 "src/parser.tab.c"
    break;

  case 277: /* dot_field_name: OR  */
#line 1361 "src/parser.y"
                     { (yyval.text) = kw_name("or"); }
#line 5189 "src/parser.tab.c"
    break;

  case 278: /* dot_field_name: TRUE  */
#line 1362 "src/parser.y"
                     { (yyval.text) = kw_name("true"); }
#line 5195 "src/parser.tab.c"
    break;

  case 279: /* dot_field_name: FALSE  */
#line 1363 "src/parser.y"
                     { (yyval.text) = kw_name("false"); }
#line 5201 "src/parser.tab.c"
    break;

  case 280: /* dot_field_name: NOTHING  */
#line 1364 "src/parser.y"
                     { (yyval.text) = kw_name("nothing"); }
#line 5207 "src/parser.tab.c"
    break;

  case 281: /* dot_field_name: BREAK  */
#line 1365 "src/parser.y"
                     { (yyval.text) = kw_name("break"); }
#line 5213 "src/parser.tab.c"
    break;

  case 282: /* dot_field_name: CONTINUE  */
#line 1366 "src/parser.y"
                     { (yyval.text) = kw_name("continue"); }
#line 5219 "src/parser.tab.c"
    break;

  case 283: /* dot_field_name: GOTO  */
#line 1367 "src/parser.y"
                     { (yyval.text) = kw_name("goto"); }
#line 5225 "src/parser.tab.c"
    break;

  case 284: /* dot_field_name: GOSUB  */
#line 1368 "src/parser.y"
                     { (yyval.text) = kw_name("gosub"); }
#line 5231 "src/parser.tab.c"
    break;

  case 285: /* dot_field_name: SPAWN  */
#line 1369 "src/parser.y"
                     { (yyval.text) = kw_name("spawn"); }
#line 5237 "src/parser.tab.c"
    break;

  case 286: /* dot_field_name: EXPORT  */
#line 1370 "src/parser.y"
                     { (yyval.text) = kw_name("export"); }
#line 5243 "src/parser.tab.c"
    break;

  case 287: /* dot_field_name: LIBRARY  */
#line 1371 "src/parser.y"
                     { (yyval.text) = kw_name("library"); }
#line 5249 "src/parser.tab.c"
    break;

  case 288: /* dot_field_name: FUNCTION  */
#line 1372 "src/parser.y"
                     { (yyval.text) = kw_name("function"); }
#line 5255 "src/parser.tab.c"
    break;

  case 289: /* dot_field_name: MODIFIER  */
#line 1373 "src/parser.y"
                     { (yyval.text) = kw_name("modifier"); }
#line 5261 "src/parser.tab.c"
    break;

  case 290: /* dot_field_name: PROGRAM  */
#line 1374 "src/parser.y"
                     { (yyval.text) = kw_name("program"); }
#line 5267 "src/parser.tab.c"
    break;

  case 291: /* dot_field_name: WATCH  */
#line 1375 "src/parser.y"
                     { (yyval.text) = kw_name("watch"); }
#line 5273 "src/parser.tab.c"
    break;

  case 292: /* dot_field_name: WATCHERS  */
#line 1376 "src/parser.y"
                     { (yyval.text) = kw_name("watchers"); }
#line 5279 "src/parser.tab.c"
    break;

  case 293: /* dot_field_name: CONSIDER  */
#line 1377 "src/parser.y"
                     { (yyval.text) = kw_name("consider"); }
#line 5285 "src/parser.tab.c"
    break;

  case 294: /* dot_field_name: STEP  */
#line 1378 "src/parser.y"
                     { (yyval.text) = kw_name("step"); }
#line 5291 "src/parser.tab.c"
    break;

  case 295: /* dot_field_name: UNWATCH  */
#line 1379 "src/parser.y"
                     { (yyval.text) = kw_name("unwatch"); }
#line 5297 "src/parser.tab.c"
    break;

  case 296: /* dot_field_name: UNKNOWN_VALUE  */
#line 1380 "src/parser.y"
                     { (yyval.text) = kw_name("unknown"); }
#line 5303 "src/parser.tab.c"
    break;

  case 297: /* dot_field_name: DIM  */
#line 1381 "src/parser.y"
                     { (yyval.text) = kw_name("dim"); }
#line 5309 "src/parser.tab.c"
    break;

  case 298: /* record_field_list: field_name OP_EQ expression  */
#line 1385 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5315 "src/parser.tab.c"
    break;

  case 299: /* record_field_list: field_name COLON expression  */
#line 1386 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5321 "src/parser.tab.c"
    break;

  case 300: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1387 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5327 "src/parser.tab.c"
    break;

  case 301: /* record_field_list: record_field_list COMMA optional_newlines field_name OP_EQ expression  */
#line 1388 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5333 "src/parser.tab.c"
    break;

  case 302: /* record_field_list: record_field_list COMMA optional_newlines field_name COLON expression  */
#line 1389 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5339 "src/parser.tab.c"
    break;

  case 303: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1390 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5345 "src/parser.tab.c"
    break;

  case 304: /* field_policy: IDENT  */
#line 1398 "src/parser.y"
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
#line 5377 "src/parser.tab.c"
    break;

  case 305: /* field_policy: IDENT expression  */
#line 1425 "src/parser.y"
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
#line 5398 "src/parser.tab.c"
    break;


#line 5402 "src/parser.tab.c"

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

#line 1448 "src/parser.y"


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
