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
  YYSYMBOL_PLUS_EQ = 9,                    /* PLUS_EQ  */
  YYSYMBOL_MINUS_EQ = 10,                  /* MINUS_EQ  */
  YYSYMBOL_STAR_EQ = 11,                   /* STAR_EQ  */
  YYSYMBOL_SLASH_EQ = 12,                  /* SLASH_EQ  */
  YYSYMBOL_IF = 13,                        /* IF  */
  YYSYMBOL_CONSIDER_IF = 14,               /* CONSIDER_IF  */
  YYSYMBOL_THEN = 15,                      /* THEN  */
  YYSYMBOL_ELSE = 16,                      /* ELSE  */
  YYSYMBOL_CONSIDER_ELSE = 17,             /* CONSIDER_ELSE  */
  YYSYMBOL_END = 18,                       /* END  */
  YYSYMBOL_END_CONSIDER = 19,              /* END_CONSIDER  */
  YYSYMBOL_PRINT = 20,                     /* PRINT  */
  YYSYMBOL_TRUE = 21,                      /* TRUE  */
  YYSYMBOL_FALSE = 22,                     /* FALSE  */
  YYSYMBOL_NOTHING = 23,                   /* NOTHING  */
  YYSYMBOL_UNKNOWN_VALUE = 24,             /* UNKNOWN_VALUE  */
  YYSYMBOL_AND = 25,                       /* AND  */
  YYSYMBOL_OR = 26,                        /* OR  */
  YYSYMBOL_NOT = 27,                       /* NOT  */
  YYSYMBOL_WITH = 28,                      /* WITH  */
  YYSYMBOL_NEW = 29,                       /* NEW  */
  YYSYMBOL_SPAWN = 30,                     /* SPAWN  */
  YYSYMBOL_FOR = 31,                       /* FOR  */
  YYSYMBOL_TO = 32,                        /* TO  */
  YYSYMBOL_STEP = 33,                      /* STEP  */
  YYSYMBOL_DO = 34,                        /* DO  */
  YYSYMBOL_LOOP = 35,                      /* LOOP  */
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
#line 434 "src/parser.y"

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
#define YYLAST   2576

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  87
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  76
/* YYNRULES -- Number of rules.  */
#define YYNRULES  308
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  653

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
       0,   499,   499,   503,   504,   505,   509,   510,   511,   512,
     513,   514,   515,   516,   517,   518,   519,   520,   521,   522,
     523,   524,   525,   526,   527,   528,   529,   530,   531,   532,
     533,   534,   538,   543,   544,   556,   568,   569,   570,   571,
     575,   576,   577,   581,   582,   583,   588,   589,   593,   593,
     599,   600,   604,   605,   606,   607,   611,   617,   621,   622,
     628,   633,   642,   655,   678,   679,   680,   684,   688,   695,
     699,   708,   711,   717,   723,   729,   732,   738,   739,   743,
     744,   745,   749,   750,   751,   752,   753,   754,   755,   756,
     757,   758,   759,   760,   761,   762,   763,   764,   765,   766,
     767,   768,   769,   770,   771,   772,   773,   777,   780,   787,
     790,   796,   802,   808,   809,   810,   811,   812,   828,   847,
     848,   852,   856,   859,   867,   873,   877,   878,   897,   900,
     906,   907,   908,   912,   915,   918,   921,   924,   930,   931,
     935,   936,   940,   946,   947,   948,   949,   954,   967,   972,
     977,   987,   991,   992,   996,  1003,  1007,  1016,  1017,  1021,
    1022,  1026,  1030,  1037,  1040,  1043,  1049,  1052,  1055,  1061,
    1062,  1063,  1064,  1065,  1066,  1067,  1068,  1069,  1070,  1071,
    1075,  1079,  1080,  1084,  1085,  1089,  1090,  1091,  1097,  1098,
    1099,  1103,  1104,  1105,  1109,  1110,  1111,  1112,  1113,  1114,
    1118,  1119,  1120,  1121,  1126,  1140,  1141,  1142,  1143,  1144,
    1145,  1146,  1147,  1148,  1149,  1153,  1154,  1155,  1156,  1157,
    1174,  1180,  1181,  1182,  1183,  1184,  1185,  1186,  1187,  1188,
    1192,  1193,  1197,  1202,  1207,  1213,  1225,  1230,  1238,  1242,
    1248,  1249,  1253,  1254,  1258,  1259,  1263,  1264,  1268,  1269,
    1282,  1289,  1298,  1299,  1300,  1301,  1302,  1303,  1304,  1305,
    1306,  1307,  1308,  1309,  1310,  1311,  1312,  1313,  1314,  1315,
    1316,  1317,  1318,  1319,  1320,  1321,  1322,  1323,  1324,  1325,
    1326,  1327,  1328,  1329,  1330,  1331,  1332,  1333,  1334,  1335,
    1336,  1337,  1338,  1339,  1340,  1341,  1342,  1343,  1344,  1348,
    1349,  1350,  1351,  1352,  1353,  1361,  1388,  1407,  1408
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
  "STRING", "LENS_CONTENT", "QUALIFIED_IDENT", "AS", "PLUS_EQ", "MINUS_EQ",
  "STAR_EQ", "SLASH_EQ", "IF", "CONSIDER_IF", "THEN", "ELSE",
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

#define YYPACT_NINF (-492)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -492,    10,   922,  -492,     9,    -3,  2163,  -492,  2128,    75,
      36,     3,  -492,  -492,  2163,  2163,    88,   170,   180,  2163,
     129,   129,    60,  2163,   136,     7,  -492,   525,   174,   203,
     210,   117,   193,   168,  -492,  -492,   149,   143,   196,   188,
     200,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,
     205,  -492,   209,  -492,  -492,   211,   212,   213,   214,   215,
     219,   220,   221,  -492,   206,  2163,  2163,   289,  -492,  -492,
     231,  -492,  -492,  -492,  -492,  2163,  2223,   303,   241,  -492,
    2163,  2163,  -492,  -492,    93,   304,   292,   295,  -492,   189,
     190,  -492,    96,  -492,  -492,   318,   268,  -492,   248,    32,
     320,  -492,   254,   255,  -492,  -492,   256,   264,  -492,  -492,
    -492,   269,   129,  -492,   150,   257,  -492,   262,   103,   122,
     343,  -492,  -492,  -492,  -492,  -492,   159,  -492,   322,   274,
     272,   347,  -492,   352,  -492,   174,  -492,  -492,  -492,  -492,
    -492,  2163,  2163,  -492,  2402,  2163,   160,  -492,  -492,  -492,
    -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,
    2288,  -492,   281,   282,   290,  -492,  2163,  -492,    74,   296,
     301,  -492,   307,   603,   742,  2163,  2459,  -492,   426,  2163,
    2163,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,
    -492,  2163,  2163,   175,  2163,  2163,  2163,  2163,  2516,   361,
    2163,  2163,  2163,  2163,   348,   979,  -492,   372,   383,   383,
     129,   104,   129,  -492,   384,  -492,  -492,  -492,    13,  -492,
      59,  -492,   314,   383,  -492,   386,   383,  -492,   387,   388,
     365,  -492,   323,   392,   329,   330,  -492,  -492,  -492,  -492,
    -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,
    -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,
    -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,
    -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,
    -492,  -492,  -492,  -492,  2163,  2163,   331,  -492,   324,    57,
    -492,   112,  -492,  2163,  -492,   332,   334,  2163,  -492,  -492,
    -492,  -492,  -492,   335,  -492,   337,   339,  -492,   336,   349,
     350,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,
    -492,  -492,  -492,  -492,   344,   295,  -492,   190,   190,  2163,
     195,  -492,  -492,   353,   351,   357,  -492,  -492,  -492,   359,
     356,   409,  2163,   166,  1036,  2163,   198,  -492,   367,   366,
     373,   114,   368,   257,  1093,  -492,  1150,  -492,  -492,  -492,
    -492,  2163,   376,  -492,   370,   378,  1207,  -492,  -492,   386,
    -492,   375,  2163,  2163,  -492,  -492,   444,  -492,  2163,  2163,
     377,  -492,  -492,  -492,  -492,   380,  -492,   116,   145,  -492,
    2163,  2163,  -492,   865,   442,   195,  -492,  2163,  2163,   382,
    -492,  2163,   390,  2163,  2163,   430,   459,  2163,   393,   456,
     396,   477,   399,   400,  -492,   439,   438,   412,  -492,  -492,
     407,   436,   411,  -492,   420,   421,  2163,   422,    34,  -492,
    -492,  -492,   808,  -492,   666,  -492,  -492,   423,   424,  2051,
     493,  -492,  2071,  -492,   431,   432,  -492,  1264,    25,  -492,
     433,   434,   435,   437,   498,  -492,   441,  -492,  -492,  -492,
    -492,  1321,   451,   453,  -492,  1378,  -492,   454,  -492,  -492,
    -492,  -492,   440,   270,   510,   511,  -492,  -492,    55,   446,
     101,  -492,  -492,  -492,  -492,   455,   458,  -492,   461,  -492,
    -492,  1435,   488,    63,  -492,  2163,  -492,  1264,  -492,  -492,
    -492,  -492,   462,  1492,  -492,  1549,  1606,  1663,   478,  -492,
    -492,   472,  1720,  -492,  1777,  2163,   467,   475,   165,   470,
     473,   553,   444,  2163,  2163,  1834,  -492,  -492,  1891,  -492,
     530,   476,  -492,   479,   480,  1264,  -492,  1492,  -492,  -492,
     481,   482,   483,  -492,  -492,  -492,  -492,  -492,  -492,  -492,
    -492,  -492,  -492,   484,  -492,   485,  -492,   486,   487,   489,
     490,   491,   495,   497,   499,  -492,   528,   533,   534,   500,
     501,   529,   531,  -492,  2345,   383,   582,  -492,  -492,  -492,
     505,   514,  -492,  -492,   578,   579,   512,  -492,  -492,  -492,
    -492,  1492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,
    -492,  -492,  -492,  -492,  -492,   513,   515,   516,  -492,  -492,
     518,   520,   522,   120,   517,  -492,  1948,  -492,   527,   532,
     535,  -492,  1264,  -492,  -492,  -492,  -492,  -492,  -492,   536,
     539,   543,  2163,  -492,  -492,  -492,    76,  -492,  -492,   540,
    -492,   608,    83,  2005,  -492,   545,   609,   612,  -492,   546,
     548,  -492,  -492
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       3,     0,     2,     1,    43,     0,     0,    44,     0,     0,
       0,     0,    46,    47,     0,     0,   157,   159,     0,   152,
       0,     0,     0,     0,     0,     0,    45,     0,     0,     0,
       0,     0,     0,     0,     4,     5,     0,     0,    40,     0,
       0,     9,    10,    12,    11,    13,    14,    15,    16,    17,
       0,    19,     0,    20,    22,     0,     0,     0,     0,     0,
       0,     0,     0,    31,     0,   240,   240,   215,    43,   218,
       0,   222,   223,   224,   225,     0,     0,     0,     0,   221,
       0,     0,   307,   307,   232,     0,   180,   181,   183,   185,
     188,   191,   194,   200,   229,   217,     0,    56,     0,     0,
       0,     3,     0,     0,   158,   160,     0,     0,   153,   155,
     156,    43,     0,   140,     0,   126,   125,     0,     0,     0,
       0,   151,    52,    54,    53,    55,   119,    50,     0,     0,
       0,   114,   116,   113,   115,     0,     6,    36,    37,    38,
      39,     0,     0,    48,     0,     0,     0,   154,     7,     8,
      18,    21,    23,    24,    25,    26,    27,    28,    29,    30,
       0,   242,     0,   241,     0,   238,   240,   195,   197,     0,
       0,   196,     0,     0,     0,   240,     0,   219,     0,     0,
       0,   205,   206,   207,   208,   209,   210,   211,   212,   213,
     214,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     3,     0,   246,   246,
       0,     0,     0,     3,     0,     3,   150,   149,     0,   148,
       0,   145,     0,   246,    51,     0,   246,     3,     0,     0,
       0,    32,     0,     0,   252,     0,   253,   268,   265,   266,
     257,   273,   280,   281,   282,   298,   278,   279,   277,   263,
     261,   287,   267,   258,   296,   270,   271,   272,   259,   262,
     269,   295,   283,   284,   290,   274,   285,   286,   293,   297,
     264,   294,   260,   254,   255,   256,   291,   292,   289,   275,
     276,   288,    42,    33,     0,     0,   252,   251,     0,     0,
     250,     0,    58,     0,    59,     0,     0,   240,   216,   226,
     227,   308,   244,   307,   230,   307,     0,   252,     0,   236,
      43,     3,   169,    40,   170,   171,   172,   173,   174,   175,
     176,   177,   178,   179,     0,   182,   184,   189,   190,     0,
     186,   192,   193,     0,   252,     0,   202,   239,    57,     0,
       0,     0,     0,    46,     0,     0,    77,   248,     0,   247,
       0,     0,     0,   127,     0,   141,     0,   147,   146,   143,
     144,   240,     0,   121,     0,     0,     0,   118,   117,     0,
      41,     0,   240,   240,    35,    34,     0,   130,     0,     0,
       0,   307,   243,   220,   198,     0,   307,     0,     0,   233,
     240,   240,   234,     0,   166,   187,   201,   240,   240,     0,
       3,     0,     0,     0,     0,    44,     0,     0,     0,     0,
       0,     0,     0,     0,     3,    44,    44,     0,   120,     3,
       0,    44,     0,    49,     0,     0,   305,     0,     0,   299,
     300,   130,     0,   199,     0,   228,   231,     0,     0,     0,
      44,   161,     0,   162,     0,     0,     3,     0,     0,     3,
       0,     0,     0,     0,     0,    79,     0,     3,   249,     3,
       3,     0,     0,     0,    62,     0,     3,     0,     3,    60,
      61,   306,     0,     0,     0,     0,   131,   132,     0,   252,
       0,   245,   235,   237,     3,     0,     0,     3,     0,   203,
     204,     0,    44,    45,    67,     0,     3,     0,    71,    72,
      73,    79,     0,    78,    74,     0,     0,     0,    44,   123,
     142,    44,     0,   112,     0,     0,     0,   138,     0,     0,
       0,     0,     0,     0,     0,     0,   164,   163,     0,   167,
      44,     0,    65,     0,     0,     0,    68,    75,    79,    80,
       0,     0,     0,    85,    86,    88,    87,    89,    81,    90,
      91,    92,    93,     0,    95,     0,    97,     0,     0,     0,
       0,     0,     0,     0,     0,   106,    44,    44,    44,     0,
       0,    44,    44,   301,     0,   246,     0,   133,   129,     3,
       0,     0,   302,   303,    44,    44,     0,    64,    66,     3,
      69,    76,    82,    83,    84,    94,    96,    98,    99,   100,
     101,   102,   103,   104,   105,     0,     0,     0,   122,   109,
       0,     0,     0,     0,     0,   139,     0,   128,     0,     0,
       0,    63,     0,   107,   108,   124,   111,   110,   130,     0,
       0,    44,     0,   165,   168,    70,     0,   130,     3,     0,
     304,     0,     0,     0,   137,     0,     0,    44,   136,     0,
       0,   135,   134
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -492,  -492,   -87,  -492,  -177,   471,  -492,    -2,   547,  -492,
    -492,   508,  -170,  -169,  -491,  -490,  -477,  -475,  -471,  -467,
    -492,  -492,  -433,  -492,  -466,  -462,  -461,  -455,  -163,   502,
     266,  -454,  -449,  -107,  -492,  -428,  -492,  -492,   428,  -448,
    -162,  -157,  -155,  -447,  -153,  -134,  -127,  -125,  -446,  -492,
    -492,  -212,    16,  -492,   463,   464,  -188,    99,   -46,   567,
     452,  -492,   354,  -492,  -492,  -492,   -62,  -492,  -492,  -176,
    -492,   217,   -67,  -172,   124,   -56
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    35,    36,   145,    37,    84,   146,   233,
     126,   127,    39,    40,    41,   494,    42,    43,    44,    45,
     346,   409,   503,   548,    46,    47,    48,    49,    50,   128,
     364,    51,    52,   114,    53,   428,   477,   518,   115,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,   441,
     443,   324,   161,    86,    87,    88,    89,    90,    91,    92,
     194,    93,    94,   177,   392,    95,   162,   163,   303,   348,
     349,   289,   290,   291,   427,   173
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      38,   312,   305,   478,   164,   211,   330,   536,   314,   315,
       3,   118,   543,    64,   205,   316,   317,   357,   109,   110,
     113,   318,    85,   319,    97,   320,   544,   174,   545,   167,
     102,   103,   546,   350,   171,   108,   547,   549,   473,   116,
      99,   550,   551,   121,   321,   590,   543,   362,   552,   554,
     365,   322,   474,   323,   555,   556,   560,   565,   495,   473,
     544,   119,   545,   359,   111,   358,   546,    68,   537,   202,
     547,   549,    66,   521,   100,   550,   551,   282,     7,    98,
     473,     7,   552,   554,    65,   475,   101,   473,   555,   556,
     560,   565,   104,   203,   641,    12,    13,   172,    12,    13,
     543,   646,   296,   351,   295,   591,   475,   216,   496,   309,
     113,   360,    26,   306,   544,    26,   545,   476,   378,   344,
     546,   131,   132,   217,   547,   549,   354,   475,   356,   550,
     551,   336,   635,    68,   475,   112,   552,   554,   476,   379,
     366,   395,   555,   556,   560,   565,   532,     7,   218,   331,
     332,   197,   137,   138,   139,   140,   219,   231,   232,   476,
     198,   283,   523,   122,    12,    13,   476,   220,   175,   137,
     138,   139,   140,   197,   105,   221,   313,   123,   122,   176,
     352,    26,   198,   524,   106,   212,   117,   107,   380,   302,
     413,   124,   123,   381,   435,   212,   629,   133,   134,   301,
     636,   381,   403,    38,   141,   404,   124,   129,   113,   642,
     113,   125,   407,   333,   130,   408,   338,   339,   340,   341,
     142,   284,   143,   135,   393,   436,   125,   485,   301,   144,
     488,   212,   136,   213,   223,   385,   181,   182,   183,   184,
     185,   186,   187,   188,   189,   190,   576,   387,   577,   388,
     181,   182,   183,   184,   185,   186,   187,   188,   189,   190,
     191,   192,   312,   195,   196,   312,   191,   192,   143,   314,
     315,   148,   314,   315,   516,   517,   316,   317,   147,   316,
     317,   160,   318,   149,   319,   318,   320,   319,   150,   320,
     327,   328,   151,   165,   152,   153,   154,   155,   156,   417,
     374,   375,   157,   158,   159,   321,   166,   169,   321,   382,
     424,   425,   322,   447,   323,   322,   170,   323,   179,   178,
     180,   199,   200,   201,   204,   432,   540,   461,   437,   438,
     434,   208,   465,   541,   542,   444,   445,   206,   207,   209,
     553,   557,    38,   214,   210,   215,   558,   222,   559,   226,
     561,   228,    38,   225,    38,   227,   229,   292,   402,   491,
     540,   406,   497,   293,    38,   337,   294,   541,   542,   562,
     505,   297,   506,   507,   553,   557,   563,   298,   564,   512,
     558,   514,   559,   299,   561,   342,   345,   347,   355,   361,
     363,    38,   367,   368,   429,   430,   369,   525,   371,   614,
     528,   370,   613,   562,   372,   373,   376,   377,   383,   535,
     563,   390,   564,    83,   540,   389,   386,   448,   381,   450,
     451,   541,   542,   454,   391,    65,   397,   394,   553,   557,
     310,   396,   398,     5,   558,   399,   559,   313,   561,   400,
     313,   401,   471,   410,     7,    38,     8,   411,   426,   412,
     481,   414,   418,   419,   420,   423,   433,   562,   442,    38,
     431,    12,    13,    38,   563,   446,   564,    16,    17,   452,
      19,    20,    21,   449,   453,   456,   455,    25,    26,   457,
      27,   458,   459,   460,    31,    32,   462,   463,   464,    38,
     466,   533,   616,   467,   468,    38,   469,   470,   472,   482,
     483,    38,   622,    38,    38,    38,   486,   489,   490,   311,
      38,   534,    38,   502,   519,   520,   498,   499,   500,   531,
     501,   522,   515,    38,   504,   569,    38,   570,    67,    68,
      69,   573,    70,    38,   509,    38,   510,   513,   526,   582,
     583,   527,   574,     7,   529,   538,    71,    72,    73,    74,
     575,   643,    75,   578,    76,    77,   579,   580,   586,   587,
      12,    13,   588,   589,   592,   593,   594,   595,   596,   597,
     598,   605,   599,   600,   601,    78,   606,    26,   602,    79,
     603,   607,   604,   608,   609,   610,   611,   615,   617,    38,
     618,   619,   620,   630,   639,   621,   623,    80,   624,   625,
      81,   626,    82,   627,    83,   628,    67,    68,    69,   632,
      70,   120,   645,   649,    38,   633,   650,   285,   634,   637,
      38,     7,   638,   644,    71,    72,    73,    74,   648,   651,
      75,   652,    76,    77,   224,   422,   193,   230,    12,    13,
     353,    38,   325,   168,   326,   329,   581,     0,   640,   480,
     384,     0,     0,    78,     0,    26,     0,    79,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    67,
      68,    69,     0,    70,     0,    80,     0,     0,    81,     0,
      82,   300,    83,     0,     7,     0,   301,    71,    72,    73,
      74,     0,     0,    75,     0,    76,    77,     0,     0,     0,
       0,    12,    13,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    78,     0,    26,     0,
      79,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    80,     0,
       0,    81,     0,    82,     0,    83,   286,   287,     0,   301,
     236,     0,     0,     0,     0,   237,     0,   238,   239,     0,
     240,     0,   241,   242,   243,   244,   245,   246,   247,   248,
     249,   250,   251,   252,   253,   254,   255,   256,   257,   258,
     259,   260,   261,   262,   263,   264,   265,   266,   267,   268,
     269,   270,   271,   272,   273,   274,   275,   276,   277,   278,
     279,   280,   281,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   479,   287,     0,     0,   236,     0,     0,     0,
       0,   237,   304,   238,   239,   301,   240,     0,   241,   242,
     243,   244,   245,   246,   247,   248,   249,   250,   251,   252,
     253,   254,   255,   256,   257,   258,   259,   260,   261,   262,
     263,   264,   265,   266,   267,   268,   269,   270,   271,   272,
     273,   274,   275,   276,   277,   278,   279,   280,   281,     4,
       0,     0,     5,     0,     0,     0,     0,     0,     6,     0,
       0,   439,     0,   440,     0,     8,     0,     0,     0,     0,
       0,   301,     0,     9,     0,     0,    10,     0,     0,    11,
      12,    13,     0,     0,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,     0,    25,    26,     0,    27,
      28,    29,    30,    31,    32,    33,     4,     0,     0,     5,
       0,     0,     0,     0,     0,     6,     0,     0,     0,     0,
       7,     0,     8,     0,     0,     0,     0,     0,    34,     0,
       9,     0,     0,    10,     0,     0,    11,    12,    13,     0,
       0,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,     0,    25,    26,     0,    27,    28,    29,    30,
      31,    32,    33,     4,     0,     0,     5,     0,     0,     0,
       0,     0,     6,     0,     0,     0,     0,     7,     0,     8,
       0,     0,     0,     0,     0,    34,     0,     9,     0,     0,
      10,     0,     0,    11,   343,    13,     0,     0,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,     0,
      25,    26,     0,    27,    28,    29,    30,    31,    32,    33,
       4,     0,     0,     5,     0,     0,     0,     0,     0,     6,
       0,     0,     0,     0,   405,     0,     8,     0,     0,     0,
       0,     0,    34,     0,     9,     0,     0,    10,     0,     0,
      11,    12,    13,     0,     0,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,     0,    25,    26,     0,
      27,    28,    29,    30,    31,    32,    33,     4,     0,     0,
       5,     0,     0,     0,     0,     0,     6,     0,     0,     0,
       0,   415,     0,     8,     0,     0,     0,     0,     0,    34,
       0,     9,     0,     0,    10,     0,     0,    11,    12,    13,
       0,     0,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,     0,    25,    26,     0,    27,    28,    29,
      30,    31,    32,    33,     4,     0,     0,     5,     0,     0,
       0,     0,     0,     6,     0,     0,     0,     0,   416,     0,
       8,     0,     0,     0,     0,     0,    34,     0,     9,     0,
       0,    10,     0,     0,    11,    12,    13,     0,     0,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
       0,    25,    26,     0,    27,    28,    29,    30,    31,    32,
      33,     4,     0,     0,     5,     0,     0,     0,     0,     0,
       6,     0,     0,     0,     0,   421,     0,     8,     0,     0,
       0,     0,     0,    34,     0,     9,     0,     0,    10,     0,
       0,    11,    12,    13,     0,     0,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,     0,    25,    26,
       0,    27,    28,    29,    30,    31,    32,    33,     4,     0,
       0,     5,     0,     0,     0,     0,     0,     6,     0,     0,
       0,     0,   492,     0,     8,     0,     0,     0,     0,     0,
      34,     0,     9,     0,     0,    10,     0,     0,    11,    12,
      13,     0,     0,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,     0,    25,   493,     0,    27,    28,
      29,    30,    31,    32,    33,     4,     0,     0,     5,     0,
       0,     0,     0,     0,     6,     0,     0,     0,     0,   508,
       0,     8,     0,     0,     0,     0,     0,    34,     0,     9,
       0,     0,    10,     0,     0,    11,    12,    13,     0,     0,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,    25,    26,     0,    27,    28,    29,    30,    31,
      32,    33,     4,     0,     0,     5,     0,     0,     0,     0,
       0,     6,     0,     0,     0,     0,   511,     0,     8,     0,
       0,     0,     0,     0,    34,     0,     9,     0,     0,    10,
       0,     0,    11,    12,    13,     0,     0,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,     0,    25,
      26,     0,    27,    28,    29,    30,    31,    32,    33,     4,
       0,     0,     5,     0,     0,     0,     0,     0,     6,     0,
       0,     0,     0,   530,     0,     8,     0,     0,     0,     0,
       0,    34,     0,     9,     0,     0,    10,     0,     0,    11,
      12,    13,     0,     0,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,     0,    25,    26,     0,    27,
      28,    29,    30,    31,    32,    33,   310,     0,     0,     5,
       0,     0,     0,     0,     0,     6,     0,     0,     0,     0,
       7,     0,     8,     0,     0,     0,     0,     0,    34,     0,
       9,     0,     0,    10,     0,     0,    11,    12,    13,     0,
       0,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,     0,    25,    26,     0,    27,    28,    29,    30,
      31,    32,    33,     4,     0,     0,     5,     0,     0,     0,
       0,     0,     6,     0,     0,     0,     0,   566,     0,     8,
       0,     0,     0,     0,     0,   539,     0,     9,     0,     0,
      10,     0,     0,    11,    12,    13,     0,     0,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,     0,
      25,    26,     0,    27,    28,    29,    30,    31,    32,    33,
       4,     0,     0,     5,     0,     0,     0,     0,     0,     6,
       0,     0,     0,     0,   567,     0,     8,     0,     0,     0,
       0,     0,    34,     0,     9,     0,     0,    10,     0,     0,
      11,    12,    13,     0,     0,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,     0,    25,    26,     0,
      27,    28,    29,    30,    31,    32,    33,     4,     0,     0,
       5,     0,     0,     0,     0,     0,     6,     0,     0,     0,
       0,   568,     0,     8,     0,     0,     0,     0,     0,    34,
       0,     9,     0,     0,    10,     0,     0,    11,    12,    13,
       0,     0,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,     0,    25,    26,     0,    27,    28,    29,
      30,    31,    32,    33,     4,     0,     0,     5,     0,     0,
       0,     0,     0,     6,     0,     0,     0,     0,   571,     0,
       8,     0,     0,     0,     0,     0,    34,     0,     9,     0,
       0,    10,     0,     0,    11,    12,    13,     0,     0,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
       0,    25,    26,     0,    27,    28,    29,    30,    31,    32,
      33,     4,     0,     0,     5,     0,     0,     0,     0,     0,
       6,     0,     0,     0,     0,   572,     0,     8,     0,     0,
       0,     0,     0,    34,     0,     9,     0,     0,    10,     0,
       0,    11,    12,    13,     0,     0,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,     0,    25,    26,
       0,    27,    28,    29,    30,    31,    32,    33,     4,     0,
       0,     5,     0,     0,     0,     0,     0,     6,     0,     0,
       0,     0,   584,     0,     8,     0,     0,     0,     0,     0,
      34,     0,     9,     0,     0,    10,     0,     0,    11,    12,
      13,     0,     0,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,     0,    25,    26,     0,    27,    28,
      29,    30,    31,    32,    33,     4,     0,     0,     5,     0,
       0,     0,     0,     0,     6,     0,     0,     0,     0,   585,
       0,     8,     0,     0,     0,     0,     0,    34,     0,     9,
       0,     0,    10,     0,     0,    11,    12,    13,     0,     0,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,    25,    26,     0,    27,    28,    29,    30,    31,
      32,    33,     4,     0,     0,     5,     0,     0,     0,     0,
       0,     6,     0,     0,     0,     0,   631,     0,     8,     0,
       0,     0,     0,     0,    34,     0,     9,     0,     0,    10,
       0,     0,    11,    12,    13,     0,     0,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,     0,    25,
      26,     0,    27,    28,    29,    30,    31,    32,    33,     4,
       0,     0,     5,     0,     0,     0,     0,     0,     6,     0,
       0,     0,     0,   647,     0,     8,     0,     0,     0,     0,
       0,    34,     0,     9,     0,     0,    10,     0,     0,    11,
      12,    13,     0,     0,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,   310,    25,    26,     5,    27,
      28,    29,    30,    31,    32,    33,     0,     0,     0,     7,
       0,     8,     0,     0,     0,   310,     0,     0,     5,     0,
       0,     0,     0,     0,     0,     0,    12,    13,    34,     7,
       0,     8,    16,    17,     0,    19,    20,    21,     0,     0,
       0,     0,    25,    26,     0,    27,    12,    13,     0,    31,
      32,     0,    16,    17,     0,    19,    20,    21,     0,     0,
       0,     0,    25,    26,     0,    27,     0,     0,     0,    31,
      32,    67,    68,    69,   484,    70,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     7,     0,     0,    71,
      72,    73,    74,     0,   487,    75,     0,    76,    77,     0,
      96,     0,     0,    12,    13,     0,    67,    68,    69,     0,
      70,     0,     0,     0,     0,     0,     0,     0,    78,     0,
      26,     7,    79,     0,    71,    72,    73,    74,     0,     0,
      75,     0,    76,    77,     0,     0,     0,     0,    12,    13,
      80,     0,     0,    81,     0,    82,     0,    83,     0,     0,
       0,     0,     0,    78,     0,    26,     0,    79,     0,     0,
       0,     0,     0,     0,     0,     0,    67,    68,    69,     0,
      70,     0,     0,     0,     0,    80,     0,     0,    81,     0,
      82,     7,    83,     0,    71,    72,    73,    74,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    12,    13,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    78,     0,    26,     0,    79,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   286,   287,     0,     0,   236,     0,    81,     0,
      82,   237,    83,   238,   239,     0,   240,     0,   241,   242,
     243,   244,   245,   246,   247,   248,   249,   250,   251,   252,
     253,   254,   255,   256,   257,   258,   259,   260,   261,   262,
     263,   264,   265,   266,   267,   268,   269,   270,   271,   272,
     273,   274,   275,   276,   277,   278,   279,   280,   281,   286,
     287,     0,     0,   236,     0,     0,     0,     0,   237,     0,
     238,   239,     0,   240,   288,   241,   242,   243,   244,   245,
     246,   247,   248,   249,   250,   251,   252,   253,   254,   255,
     256,   257,   258,   259,   260,   261,   262,   263,   264,   265,
     266,   267,   268,   269,   270,   271,   272,   273,   274,   275,
     276,   277,   278,   279,   280,   281,   234,     0,     0,   235,
     236,     0,     0,     0,     0,   237,     0,   238,   239,     0,
     240,   612,   241,   242,   243,   244,   245,   246,   247,   248,
     249,   250,   251,   252,   253,   254,   255,   256,   257,   258,
     259,   260,   261,   262,   263,   264,   265,   266,   267,   268,
     269,   270,   271,   272,   273,   274,   275,   276,   277,   278,
     279,   280,   281,   307,     0,     0,   308,   236,     0,     0,
       0,     0,   237,     0,   238,   239,     0,   240,     0,   241,
     242,   243,   244,   245,   246,   247,   248,   249,   250,   251,
     252,   253,   254,   255,   256,   257,   258,   259,   260,   261,
     262,   263,   264,   265,   266,   267,   268,   269,   270,   271,
     272,   273,   274,   275,   276,   277,   278,   279,   280,   281,
     334,     0,     0,   335,   236,     0,     0,     0,     0,   237,
       0,   238,   239,     0,   240,     0,   241,   242,   243,   244,
     245,   246,   247,   248,   249,   250,   251,   252,   253,   254,
     255,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281
};

static const yytype_int16 yycheck[] =
{
       2,   178,   174,   431,    66,   112,   194,   497,   178,   178,
       0,     4,   503,     4,   101,   178,   178,     4,    20,    21,
      22,   178,     6,   178,     8,   178,   503,    83,   503,    75,
      14,    15,   503,   209,    80,    19,   503,   503,     4,    23,
       4,   503,   503,    27,   178,   535,   537,   223,   503,   503,
     226,   178,    18,   178,   503,   503,   503,   503,    33,     4,
     537,    54,   537,     4,     4,    52,   537,     4,   501,    37,
     537,   537,    75,    18,    38,   537,   537,   144,    18,     4,
       4,    18,   537,   537,    75,    51,    83,     4,   537,   537,
     537,   537,     4,    61,    18,    35,    36,    81,    35,    36,
     591,    18,    28,   210,   166,   538,    51,     4,    83,   176,
     112,    52,    52,   175,   591,    52,   591,    83,    61,   206,
     591,     4,     5,    20,   591,   591,   213,    51,   215,   591,
     591,   198,   622,     4,    51,    75,   591,   591,    83,    82,
     227,   329,   591,   591,   591,   591,    83,    18,    45,   195,
     196,    77,     9,    10,    11,    12,    53,   141,   142,    83,
      86,   145,    61,     4,    35,    36,    83,    45,    75,     9,
      10,    11,    12,    77,     4,    53,   178,    18,     4,    86,
      76,    52,    86,    82,     4,    81,    50,     7,    76,   173,
      76,    32,    18,    81,    78,    81,    76,     4,     5,    83,
     628,    81,    36,   205,    61,    39,    32,     4,   210,   637,
     212,    52,    14,   197,     4,    17,   200,   201,   202,   203,
      77,    61,    79,    55,   311,    80,    52,   439,    83,    86,
     442,    81,    83,    83,    75,   297,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    81,   303,    83,   305,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,   439,    73,    74,   442,    71,    72,    79,   439,
     439,    83,   442,   442,     4,     5,   439,   439,    82,   442,
     442,    75,   439,    83,   439,   442,   439,   442,    83,   442,
     191,   192,    83,     4,    83,    83,    83,    83,    83,   361,
     284,   285,    83,    83,    83,   439,    75,     4,   442,   293,
     372,   373,   439,   400,   439,   442,    75,   442,    26,    15,
      25,     3,    54,    75,     4,   381,   503,   414,   390,   391,
     386,    75,   419,   503,   503,   397,   398,    83,    83,    75,
     503,   503,   344,    86,    75,    83,   503,     4,   503,    75,
     503,     4,   354,    31,   356,    83,     4,    76,   342,   446,
     537,   345,   449,    81,   366,     4,    76,   537,   537,   503,
     457,    75,   459,   460,   537,   537,   503,    76,   503,   466,
     537,   468,   537,    76,   537,    37,    14,     4,     4,    75,
       4,   393,     5,     5,   378,   379,    31,   484,     6,   575,
     487,    78,   574,   537,    75,    75,    75,    83,    76,   496,
     537,    75,   537,    79,   591,    76,    81,   401,    81,   403,
     404,   591,   591,   407,    75,    75,    75,    83,   591,   591,
       4,    78,    75,     7,   591,    76,   591,   439,   591,    83,
     442,    32,   426,    76,    18,   447,    20,    81,     4,    76,
     434,    83,    76,    83,    76,    80,    76,   591,    16,   461,
      83,    35,    36,   465,   591,    83,   591,    41,    42,    39,
      44,    45,    46,    83,    15,    19,    83,    51,    52,    83,
      54,     4,    83,    83,    58,    59,    47,    49,    76,   491,
      83,   493,   579,    57,    83,   497,    76,    76,    76,    76,
      76,   503,   589,   505,   506,   507,    13,    76,    76,    83,
     512,   495,   514,    15,     4,     4,    83,    83,    83,    31,
      83,    75,    82,   525,    83,    47,   528,    55,     3,     4,
       5,   515,     7,   535,    83,   537,    83,    83,    83,   523,
     524,    83,    75,    18,    83,    83,    21,    22,    23,    24,
      75,   638,    27,    83,    29,    30,    83,     4,    28,    83,
      35,    36,    83,    83,    83,    83,    83,    83,    83,    83,
      83,    43,    83,    83,    83,    50,    43,    52,    83,    54,
      83,    47,    83,    83,    83,    56,    55,     5,    83,   591,
      76,    13,    13,    76,    51,    83,    83,    72,    83,    83,
      75,    83,    77,    83,    79,    83,     3,     4,     5,    82,
       7,    86,     4,     4,   616,    83,     4,   146,    83,    83,
     622,    18,    83,    83,    21,    22,    23,    24,    83,    83,
      27,    83,    29,    30,   126,   369,    89,   135,    35,    36,
     212,   643,   179,    76,   180,   193,   522,    -1,   632,   432,
     296,    -1,    -1,    50,    -1,    52,    -1,    54,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,     3,
       4,     5,    -1,     7,    -1,    72,    -1,    -1,    75,    -1,
      77,    78,    79,    -1,    18,    -1,    83,    21,    22,    23,
      24,    -1,    -1,    27,    -1,    29,    30,    -1,    -1,    -1,
      -1,    35,    36,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    50,    -1,    52,    -1,
      54,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    72,    -1,
      -1,    75,    -1,    77,    -1,    79,     4,     5,    -1,    83,
       8,    -1,    -1,    -1,    -1,    13,    -1,    15,    16,    -1,
      18,    -1,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,     4,     5,    -1,    -1,     8,    -1,    -1,    -1,
      -1,    13,    80,    15,    16,    83,    18,    -1,    20,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,     4,
      -1,    -1,     7,    -1,    -1,    -1,    -1,    -1,    13,    -1,
      -1,    16,    -1,    18,    -1,    20,    -1,    -1,    -1,    -1,
      -1,    83,    -1,    28,    -1,    -1,    31,    -1,    -1,    34,
      35,    36,    -1,    -1,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    -1,    51,    52,    -1,    54,
      55,    56,    57,    58,    59,    60,     4,    -1,    -1,     7,
      -1,    -1,    -1,    -1,    -1,    13,    -1,    -1,    -1,    -1,
      18,    -1,    20,    -1,    -1,    -1,    -1,    -1,    83,    -1,
      28,    -1,    -1,    31,    -1,    -1,    34,    35,    36,    -1,
      -1,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    -1,    51,    52,    -1,    54,    55,    56,    57,
      58,    59,    60,     4,    -1,    -1,     7,    -1,    -1,    -1,
      -1,    -1,    13,    -1,    -1,    -1,    -1,    18,    -1,    20,
      -1,    -1,    -1,    -1,    -1,    83,    -1,    28,    -1,    -1,
      31,    -1,    -1,    34,    35,    36,    -1,    -1,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    -1,
      51,    52,    -1,    54,    55,    56,    57,    58,    59,    60,
       4,    -1,    -1,     7,    -1,    -1,    -1,    -1,    -1,    13,
      -1,    -1,    -1,    -1,    18,    -1,    20,    -1,    -1,    -1,
      -1,    -1,    83,    -1,    28,    -1,    -1,    31,    -1,    -1,
      34,    35,    36,    -1,    -1,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    -1,    51,    52,    -1,
      54,    55,    56,    57,    58,    59,    60,     4,    -1,    -1,
       7,    -1,    -1,    -1,    -1,    -1,    13,    -1,    -1,    -1,
      -1,    18,    -1,    20,    -1,    -1,    -1,    -1,    -1,    83,
      -1,    28,    -1,    -1,    31,    -1,    -1,    34,    35,    36,
      -1,    -1,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    -1,    51,    52,    -1,    54,    55,    56,
      57,    58,    59,    60,     4,    -1,    -1,     7,    -1,    -1,
      -1,    -1,    -1,    13,    -1,    -1,    -1,    -1,    18,    -1,
      20,    -1,    -1,    -1,    -1,    -1,    83,    -1,    28,    -1,
      -1,    31,    -1,    -1,    34,    35,    36,    -1,    -1,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      -1,    51,    52,    -1,    54,    55,    56,    57,    58,    59,
      60,     4,    -1,    -1,     7,    -1,    -1,    -1,    -1,    -1,
      13,    -1,    -1,    -1,    -1,    18,    -1,    20,    -1,    -1,
      -1,    -1,    -1,    83,    -1,    28,    -1,    -1,    31,    -1,
      -1,    34,    35,    36,    -1,    -1,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    -1,    51,    52,
      -1,    54,    55,    56,    57,    58,    59,    60,     4,    -1,
      -1,     7,    -1,    -1,    -1,    -1,    -1,    13,    -1,    -1,
      -1,    -1,    18,    -1,    20,    -1,    -1,    -1,    -1,    -1,
      83,    -1,    28,    -1,    -1,    31,    -1,    -1,    34,    35,
      36,    -1,    -1,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    -1,    51,    52,    -1,    54,    55,
      56,    57,    58,    59,    60,     4,    -1,    -1,     7,    -1,
      -1,    -1,    -1,    -1,    13,    -1,    -1,    -1,    -1,    18,
      -1,    20,    -1,    -1,    -1,    -1,    -1,    83,    -1,    28,
      -1,    -1,    31,    -1,    -1,    34,    35,    36,    -1,    -1,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    -1,    51,    52,    -1,    54,    55,    56,    57,    58,
      59,    60,     4,    -1,    -1,     7,    -1,    -1,    -1,    -1,
      -1,    13,    -1,    -1,    -1,    -1,    18,    -1,    20,    -1,
      -1,    -1,    -1,    -1,    83,    -1,    28,    -1,    -1,    31,
      -1,    -1,    34,    35,    36,    -1,    -1,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    -1,    51,
      52,    -1,    54,    55,    56,    57,    58,    59,    60,     4,
      -1,    -1,     7,    -1,    -1,    -1,    -1,    -1,    13,    -1,
      -1,    -1,    -1,    18,    -1,    20,    -1,    -1,    -1,    -1,
      -1,    83,    -1,    28,    -1,    -1,    31,    -1,    -1,    34,
      35,    36,    -1,    -1,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    -1,    51,    52,    -1,    54,
      55,    56,    57,    58,    59,    60,     4,    -1,    -1,     7,
      -1,    -1,    -1,    -1,    -1,    13,    -1,    -1,    -1,    -1,
      18,    -1,    20,    -1,    -1,    -1,    -1,    -1,    83,    -1,
      28,    -1,    -1,    31,    -1,    -1,    34,    35,    36,    -1,
      -1,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    -1,    51,    52,    -1,    54,    55,    56,    57,
      58,    59,    60,     4,    -1,    -1,     7,    -1,    -1,    -1,
      -1,    -1,    13,    -1,    -1,    -1,    -1,    18,    -1,    20,
      -1,    -1,    -1,    -1,    -1,    83,    -1,    28,    -1,    -1,
      31,    -1,    -1,    34,    35,    36,    -1,    -1,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    -1,
      51,    52,    -1,    54,    55,    56,    57,    58,    59,    60,
       4,    -1,    -1,     7,    -1,    -1,    -1,    -1,    -1,    13,
      -1,    -1,    -1,    -1,    18,    -1,    20,    -1,    -1,    -1,
      -1,    -1,    83,    -1,    28,    -1,    -1,    31,    -1,    -1,
      34,    35,    36,    -1,    -1,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    -1,    51,    52,    -1,
      54,    55,    56,    57,    58,    59,    60,     4,    -1,    -1,
       7,    -1,    -1,    -1,    -1,    -1,    13,    -1,    -1,    -1,
      -1,    18,    -1,    20,    -1,    -1,    -1,    -1,    -1,    83,
      -1,    28,    -1,    -1,    31,    -1,    -1,    34,    35,    36,
      -1,    -1,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    -1,    51,    52,    -1,    54,    55,    56,
      57,    58,    59,    60,     4,    -1,    -1,     7,    -1,    -1,
      -1,    -1,    -1,    13,    -1,    -1,    -1,    -1,    18,    -1,
      20,    -1,    -1,    -1,    -1,    -1,    83,    -1,    28,    -1,
      -1,    31,    -1,    -1,    34,    35,    36,    -1,    -1,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      -1,    51,    52,    -1,    54,    55,    56,    57,    58,    59,
      60,     4,    -1,    -1,     7,    -1,    -1,    -1,    -1,    -1,
      13,    -1,    -1,    -1,    -1,    18,    -1,    20,    -1,    -1,
      -1,    -1,    -1,    83,    -1,    28,    -1,    -1,    31,    -1,
      -1,    34,    35,    36,    -1,    -1,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    -1,    51,    52,
      -1,    54,    55,    56,    57,    58,    59,    60,     4,    -1,
      -1,     7,    -1,    -1,    -1,    -1,    -1,    13,    -1,    -1,
      -1,    -1,    18,    -1,    20,    -1,    -1,    -1,    -1,    -1,
      83,    -1,    28,    -1,    -1,    31,    -1,    -1,    34,    35,
      36,    -1,    -1,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    -1,    51,    52,    -1,    54,    55,
      56,    57,    58,    59,    60,     4,    -1,    -1,     7,    -1,
      -1,    -1,    -1,    -1,    13,    -1,    -1,    -1,    -1,    18,
      -1,    20,    -1,    -1,    -1,    -1,    -1,    83,    -1,    28,
      -1,    -1,    31,    -1,    -1,    34,    35,    36,    -1,    -1,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    -1,    51,    52,    -1,    54,    55,    56,    57,    58,
      59,    60,     4,    -1,    -1,     7,    -1,    -1,    -1,    -1,
      -1,    13,    -1,    -1,    -1,    -1,    18,    -1,    20,    -1,
      -1,    -1,    -1,    -1,    83,    -1,    28,    -1,    -1,    31,
      -1,    -1,    34,    35,    36,    -1,    -1,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    -1,    51,
      52,    -1,    54,    55,    56,    57,    58,    59,    60,     4,
      -1,    -1,     7,    -1,    -1,    -1,    -1,    -1,    13,    -1,
      -1,    -1,    -1,    18,    -1,    20,    -1,    -1,    -1,    -1,
      -1,    83,    -1,    28,    -1,    -1,    31,    -1,    -1,    34,
      35,    36,    -1,    -1,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,     4,    51,    52,     7,    54,
      55,    56,    57,    58,    59,    60,    -1,    -1,    -1,    18,
      -1,    20,    -1,    -1,    -1,     4,    -1,    -1,     7,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    35,    36,    83,    18,
      -1,    20,    41,    42,    -1,    44,    45,    46,    -1,    -1,
      -1,    -1,    51,    52,    -1,    54,    35,    36,    -1,    58,
      59,    -1,    41,    42,    -1,    44,    45,    46,    -1,    -1,
      -1,    -1,    51,    52,    -1,    54,    -1,    -1,    -1,    58,
      59,     3,     4,     5,    83,     7,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    18,    -1,    -1,    21,
      22,    23,    24,    -1,    83,    27,    -1,    29,    30,    -1,
      32,    -1,    -1,    35,    36,    -1,     3,     4,     5,    -1,
       7,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    50,    -1,
      52,    18,    54,    -1,    21,    22,    23,    24,    -1,    -1,
      27,    -1,    29,    30,    -1,    -1,    -1,    -1,    35,    36,
      72,    -1,    -1,    75,    -1,    77,    -1,    79,    -1,    -1,
      -1,    -1,    -1,    50,    -1,    52,    -1,    54,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,     3,     4,     5,    -1,
       7,    -1,    -1,    -1,    -1,    72,    -1,    -1,    75,    -1,
      77,    18,    79,    -1,    21,    22,    23,    24,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    35,    36,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    50,    -1,    52,    -1,    54,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,     4,     5,    -1,    -1,     8,    -1,    75,    -1,
      77,    13,    79,    15,    16,    -1,    18,    -1,    20,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,     4,
       5,    -1,    -1,     8,    -1,    -1,    -1,    -1,    13,    -1,
      15,    16,    -1,    18,    76,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,     4,    -1,    -1,     7,
       8,    -1,    -1,    -1,    -1,    13,    -1,    15,    16,    -1,
      18,    76,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,     4,    -1,    -1,     7,     8,    -1,    -1,
      -1,    -1,    13,    -1,    15,    16,    -1,    18,    -1,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
       4,    -1,    -1,     7,     8,    -1,    -1,    -1,    -1,    13,
      -1,    15,    16,    -1,    18,    -1,    20,    21,    22,    23,
      24,    25,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    88,    89,     0,     4,     7,    13,    18,    20,    28,
      31,    34,    35,    36,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    51,    52,    54,    55,    56,
      57,    58,    59,    60,    83,    90,    91,    93,    94,    99,
     100,   101,   103,   104,   105,   106,   111,   112,   113,   114,
     115,   118,   119,   121,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,     4,    75,    75,     3,     4,     5,
       7,    21,    22,    23,    24,    27,    29,    30,    50,    54,
      72,    75,    77,    79,    94,   139,   140,   141,   142,   143,
     144,   145,   146,   148,   149,   152,    32,   139,     4,     4,
      38,    83,   139,   139,     4,     4,     4,     7,   139,    94,
      94,     4,    75,    94,   120,   125,   139,    50,     4,    54,
      86,   139,     4,    18,    32,    52,    97,    98,   116,     4,
       4,     4,     5,     4,     5,    55,    83,     9,    10,    11,
      12,    61,    77,    79,    86,    92,    95,    82,    83,    83,
      83,    83,    83,    83,    83,    83,    83,    83,    83,    83,
      75,   139,   153,   154,   153,     4,    75,   145,   146,     4,
      75,   145,   139,   162,   162,    75,    86,   150,    15,    26,
      25,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      70,    71,    72,    95,   147,    73,    74,    77,    86,     3,
      54,    75,    37,    61,     4,    89,    83,    83,    75,    75,
      75,   120,    81,    83,    86,    83,     4,    20,    45,    53,
      45,    53,     4,    75,    98,    31,    75,    83,     4,     4,
     116,   139,   139,    96,     4,     7,     8,    13,    15,    16,
      18,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,   159,   139,    61,    92,     4,     5,    76,   158,
     159,   160,    76,    81,    76,   153,    28,    75,    76,    76,
      78,    83,   139,   155,    80,   160,   153,     4,     7,   159,
       4,    83,    91,    94,    99,   100,   115,   127,   128,   129,
     131,   132,   133,   134,   138,   141,   142,   144,   144,   147,
     143,   145,   145,   139,     4,     7,   159,     4,   139,   139,
     139,   139,    37,    35,    89,    14,   107,     4,   156,   157,
     156,   120,    76,   125,    89,     4,    89,     4,    52,     4,
      52,    75,   156,     4,   117,   156,    89,     5,     5,    31,
      78,     6,    75,    75,   139,   139,    75,    83,    61,    82,
      76,    81,   139,    76,   149,   153,    81,   162,   162,    76,
      75,    75,   151,    89,    83,   143,    78,    75,    75,    76,
      83,    32,   139,    36,    39,    18,   139,    14,    17,   108,
      76,    81,    76,    76,    83,    18,    18,   153,    76,    83,
      76,    18,   117,    80,   153,   153,     4,   161,   122,   139,
     139,    83,   162,    76,   162,    78,    80,   153,   153,    16,
      18,   136,    16,   137,   153,   153,    83,    89,   139,    83,
     139,   139,    39,    15,   139,    83,    19,    83,     4,    83,
      83,    89,    47,    49,    76,    89,    83,    57,    83,    76,
      76,   139,    76,     4,    18,    51,    83,   123,   122,     4,
     158,   139,    76,    76,    83,   138,    13,    83,   138,    76,
      76,    89,    18,    52,   102,    33,    83,    89,    83,    83,
      83,    83,    15,   109,    83,    89,    89,    89,    18,    83,
      83,    18,    89,    83,    89,    82,     4,     5,   124,     4,
       4,    18,    75,    61,    82,    89,    83,    83,    89,    83,
      18,    31,    83,    94,   139,    89,   102,   109,    83,    83,
      91,    99,   100,   101,   103,   104,   105,   106,   110,   111,
     112,   113,   114,   115,   118,   119,   126,   127,   128,   129,
     130,   131,   132,   133,   134,   135,    18,    18,    18,    47,
      55,    18,    18,   139,    75,    75,    81,    83,    83,    83,
       4,   161,   139,   139,    18,    18,    28,    83,    83,    83,
     102,   109,    83,    83,    83,    83,    83,    83,    83,    83,
      83,    83,    83,    83,    83,    43,    43,    47,    83,    83,
      56,    55,    76,   160,   156,     5,    89,    83,    76,    13,
      13,    83,    89,    83,    83,    83,    83,    83,    83,    76,
      76,    18,    82,    83,    83,   102,   122,    83,    83,    51,
     139,    18,   122,    89,    83,     4,    18,    18,    83,     4,
       4,    83,    83
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    87,    88,    89,    89,    89,    90,    90,    90,    90,
      90,    90,    90,    90,    90,    90,    90,    90,    90,    90,
      90,    90,    90,    90,    90,    90,    90,    90,    90,    90,
      90,    90,    91,    91,    91,    91,    92,    92,    92,    92,
      93,    93,    93,    94,    94,    94,    94,    94,    96,    95,
      97,    97,    98,    98,    98,    98,    99,    99,   100,   100,
     100,   100,   100,   101,   102,   102,   102,   103,   103,   103,
     103,   104,   104,   105,   106,   107,   107,   108,   108,   109,
     109,   109,   110,   110,   110,   110,   110,   110,   110,   110,
     110,   110,   110,   110,   110,   110,   110,   110,   110,   110,
     110,   110,   110,   110,   110,   110,   110,   111,   111,   112,
     112,   113,   114,   115,   115,   115,   115,   115,   115,   116,
     116,   117,   118,   118,   118,   119,   120,   120,   121,   121,
     122,   122,   122,   123,   123,   123,   123,   123,   124,   124,
     125,   125,   126,   127,   127,   127,   127,   127,   127,   127,
     127,   128,   129,   129,   130,   131,   132,   133,   133,   134,
     134,   135,   135,   136,   136,   136,   137,   137,   137,   138,
     138,   138,   138,   138,   138,   138,   138,   138,   138,   138,
     139,   140,   140,   141,   141,   142,   142,   142,   143,   143,
     143,   144,   144,   144,   145,   145,   145,   145,   145,   145,
     146,   146,   146,   146,   146,   147,   147,   147,   147,   147,
     147,   147,   147,   147,   147,   148,   148,   148,   148,   148,
     148,   148,   148,   148,   148,   148,   148,   148,   148,   148,
     149,   149,   150,   150,   150,   150,   151,   151,   152,   152,
     153,   153,   154,   154,   155,   155,   156,   156,   157,   157,
     158,   158,   159,   159,   159,   159,   159,   159,   159,   159,
     159,   159,   159,   159,   159,   159,   159,   159,   159,   159,
     159,   159,   159,   159,   159,   159,   159,   159,   159,   159,
     159,   159,   159,   159,   159,   159,   159,   159,   159,   159,
     159,   159,   159,   159,   159,   159,   159,   159,   159,   160,
     160,   160,   160,   160,   160,   161,   161,   162,   162
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     2,     1,
       1,     2,     1,     2,     2,     2,     2,     2,     2,     2,
       2,     1,     3,     3,     4,     4,     1,     1,     1,     1,
       1,     4,     3,     1,     1,     1,     1,     1,     0,     4,
       1,     2,     1,     1,     1,     1,     2,     4,     4,     4,
       6,     6,     6,    10,     3,     2,     3,     7,     8,     9,
      11,     7,     7,     7,     7,     5,     6,     0,     3,     0,
       2,     2,     2,     2,     2,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     2,     1,     2,     1,     2,     2,
       2,     2,     2,     2,     2,     2,     1,    10,    10,     9,
      10,    10,     7,     2,     2,     2,     2,     4,     4,     1,
       4,     1,     9,     7,    10,     2,     1,     3,    10,     9,
       0,     2,     2,     3,    10,    10,     9,     7,     1,     3,
       1,     3,     7,     4,     4,     3,     4,     4,     3,     3,
       3,     2,     1,     2,     2,     2,     2,     1,     2,     1,
       2,     6,     6,     3,     3,     6,     0,     3,     6,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     3,     1,     3,     1,     3,     4,     1,     3,
       3,     1,     3,     3,     1,     2,     2,     2,     4,     5,
       1,     4,     3,     6,     6,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     3,     1,     1,     2,
       4,     1,     1,     1,     1,     1,     3,     3,     5,     1,
       3,     5,     0,     3,     3,     5,     0,     3,     2,     3,
       0,     1,     1,     3,     1,     4,     0,     1,     1,     3,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     3,
       3,     6,     6,     6,     9,     1,     2,     0,     2
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
#line 471 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2537 "src/parser.tab.c"
        break;

    case YYSYMBOL_STRING: /* STRING  */
#line 471 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2543 "src/parser.tab.c"
        break;

    case YYSYMBOL_LENS_CONTENT: /* LENS_CONTENT  */
#line 471 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2549 "src/parser.tab.c"
        break;

    case YYSYMBOL_QUALIFIED_IDENT: /* QUALIFIED_IDENT  */
#line 471 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2555 "src/parser.tab.c"
        break;

    case YYSYMBOL_program: /* program  */
#line 494 "src/parser.y"
            { (void) ((*yyvaluep).stmt_list); }
#line 2561 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement_list: /* statement_list  */
#line 474 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2567 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement: /* statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2573 "src/parser.tab.c"
        break;

    case YYSYMBOL_assignment: /* assignment  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2579 "src/parser.tab.c"
        break;

    case YYSYMBOL_lvalue: /* lvalue  */
#line 472 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2585 "src/parser.tab.c"
        break;

    case YYSYMBOL_variable_name: /* variable_name  */
#line 471 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2591 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_lens: /* comparison_lens  */
#line 479 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 2597 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_name: /* modifier_name  */
#line 471 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2603 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_word: /* modifier_word  */
#line 471 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2609 "src/parser.tab.c"
        break;

    case YYSYMBOL_print_statement: /* print_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2615 "src/parser.tab.c"
        break;

    case YYSYMBOL_call_statement: /* call_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2621 "src/parser.tab.c"
        break;

    case YYSYMBOL_with_lock_statement: /* with_lock_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2627 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_end: /* for_end  */
#line 471 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2633 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_each_statement: /* for_each_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2639 "src/parser.tab.c"
        break;

    case YYSYMBOL_do_loop_statement: /* do_loop_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2645 "src/parser.tab.c"
        break;

    case YYSYMBOL_while_statement: /* while_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2651 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement: /* consider_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2657 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_branch_list: /* consider_branch_list  */
#line 477 "src/parser.y"
            { ast_free_consider_branch_list(((*yyvaluep).consider_branch_list)); }
#line 2663 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_else_opt: /* consider_else_opt  */
#line 474 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2669 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement_list: /* consider_statement_list  */
#line 474 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2675 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_body_statement: /* consider_body_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2681 "src/parser.tab.c"
        break;

    case YYSYMBOL_function_statement: /* function_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2687 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_statement: /* modifier_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2693 "src/parser.tab.c"
        break;

    case YYSYMBOL_program_statement: /* program_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2699 "src/parser.tab.c"
        break;

    case YYSYMBOL_library_statement: /* library_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2705 "src/parser.tab.c"
        break;

    case YYSYMBOL_use_statement: /* use_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2711 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_signature: /* modifier_signature  */
#line 480 "src/parser.y"
            { ast_free_modifier_signature(((*yyvaluep).modifier_signature)); }
#line 2717 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_context: /* modifier_context  */
#line 471 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2723 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_statement: /* watch_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2729 "src/parser.tab.c"
        break;

    case YYSYMBOL_unwatch_statement: /* unwatch_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2735 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_list: /* watch_target_list  */
#line 478 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2741 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_statement: /* server_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2747 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item_list: /* server_item_list  */
#line 484 "src/parser.y"
            { ast_free_server_item_list(((*yyvaluep).server_item_list)); }
#line 2753 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item: /* server_item  */
#line 483 "src/parser.y"
            { AstServerItemList one = ast_server_item_list_append(ast_server_item_list_empty(), ((*yyvaluep).server_item)); ast_free_server_item_list(one); }
#line 2759 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_string_list: /* server_string_list  */
#line 478 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2765 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_path: /* watch_target_path  */
#line 471 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2771 "src/parser.tab.c"
        break;

    case YYSYMBOL_without_watchers_statement: /* without_watchers_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2777 "src/parser.tab.c"
        break;

    case YYSYMBOL_on_error_statement: /* on_error_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2783 "src/parser.tab.c"
        break;

    case YYSYMBOL_error_statement: /* error_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2789 "src/parser.tab.c"
        break;

    case YYSYMBOL_return_statement: /* return_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2795 "src/parser.tab.c"
        break;

    case YYSYMBOL_label_statement: /* label_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2801 "src/parser.tab.c"
        break;

    case YYSYMBOL_goto_statement: /* goto_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2807 "src/parser.tab.c"
        break;

    case YYSYMBOL_gosub_statement: /* gosub_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2813 "src/parser.tab.c"
        break;

    case YYSYMBOL_break_statement: /* break_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2819 "src/parser.tab.c"
        break;

    case YYSYMBOL_continue_statement: /* continue_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2825 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_statement: /* if_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2831 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_block_tail: /* if_block_tail  */
#line 474 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2837 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_inline_tail: /* if_inline_tail  */
#line 474 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2843 "src/parser.tab.c"
        break;

    case YYSYMBOL_inline_statement: /* inline_statement  */
#line 473 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2849 "src/parser.tab.c"
        break;

    case YYSYMBOL_expression: /* expression  */
#line 472 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2855 "src/parser.tab.c"
        break;

    case YYSYMBOL_or_expression: /* or_expression  */
#line 472 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2861 "src/parser.tab.c"
        break;

    case YYSYMBOL_and_expression: /* and_expression  */
#line 472 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2867 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_expression: /* comparison_expression  */
#line 472 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2873 "src/parser.tab.c"
        break;

    case YYSYMBOL_additive_expression: /* additive_expression  */
#line 472 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2879 "src/parser.tab.c"
        break;

    case YYSYMBOL_multiplicative_expression: /* multiplicative_expression  */
#line 472 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2885 "src/parser.tab.c"
        break;

    case YYSYMBOL_unary_expression: /* unary_expression  */
#line 472 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2891 "src/parser.tab.c"
        break;

    case YYSYMBOL_postfix_expression: /* postfix_expression  */
#line 472 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2897 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_operator: /* comparison_operator  */
#line 471 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2903 "src/parser.tab.c"
        break;

    case YYSYMBOL_primary: /* primary  */
#line 472 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2909 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_literal: /* record_literal  */
#line 472 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2915 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_suffix: /* ident_suffix  */
#line 481 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 2921 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_dot_suffix: /* ident_dot_suffix  */
#line 481 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 2927 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list_opt: /* argument_list_opt  */
#line 475 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 2933 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list: /* argument_list  */
#line 475 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 2939 "src/parser.tab.c"
        break;

    case YYSYMBOL_array_argument_list: /* array_argument_list  */
#line 475 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 2945 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list_opt: /* parameter_list_opt  */
#line 478 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2951 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list: /* parameter_list  */
#line 478 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2957 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_name: /* field_name  */
#line 471 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2963 "src/parser.tab.c"
        break;

    case YYSYMBOL_dot_field_name: /* dot_field_name  */
#line 471 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2969 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_field_list: /* record_field_list  */
#line 476 "src/parser.y"
            { ast_free_record_field_list(((*yyvaluep).record_field_list)); }
#line 2975 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_policy: /* field_policy  */
#line 482 "src/parser.y"
            { ast_free_expr(((*yyvaluep).field_policy).reset_expr); }
#line 2981 "src/parser.tab.c"
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
#line 499 "src/parser.y"
                     { ctx->parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3287 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 503 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3293 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 504 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3299 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 505 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3305 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 509 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3311 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 510 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3317 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 511 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3323 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 512 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3329 "src/parser.tab.c"
    break;

  case 10: /* statement: for_each_statement  */
#line 513 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3335 "src/parser.tab.c"
    break;

  case 11: /* statement: while_statement  */
#line 514 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3341 "src/parser.tab.c"
    break;

  case 12: /* statement: do_loop_statement  */
#line 515 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3347 "src/parser.tab.c"
    break;

  case 13: /* statement: consider_statement  */
#line 516 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3353 "src/parser.tab.c"
    break;

  case 14: /* statement: function_statement  */
#line 517 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3359 "src/parser.tab.c"
    break;

  case 15: /* statement: modifier_statement  */
#line 518 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3365 "src/parser.tab.c"
    break;

  case 16: /* statement: program_statement  */
#line 519 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3371 "src/parser.tab.c"
    break;

  case 17: /* statement: library_statement  */
#line 520 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3377 "src/parser.tab.c"
    break;

  case 18: /* statement: use_statement NEWLINE  */
#line 521 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3383 "src/parser.tab.c"
    break;

  case 19: /* statement: watch_statement  */
#line 522 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3389 "src/parser.tab.c"
    break;

  case 20: /* statement: server_statement  */
#line 523 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3395 "src/parser.tab.c"
    break;

  case 21: /* statement: unwatch_statement NEWLINE  */
#line 524 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3401 "src/parser.tab.c"
    break;

  case 22: /* statement: without_watchers_statement  */
#line 525 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3407 "src/parser.tab.c"
    break;

  case 23: /* statement: on_error_statement NEWLINE  */
#line 526 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3413 "src/parser.tab.c"
    break;

  case 24: /* statement: error_statement NEWLINE  */
#line 527 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3419 "src/parser.tab.c"
    break;

  case 25: /* statement: return_statement NEWLINE  */
#line 528 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3425 "src/parser.tab.c"
    break;

  case 26: /* statement: label_statement NEWLINE  */
#line 529 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3431 "src/parser.tab.c"
    break;

  case 27: /* statement: goto_statement NEWLINE  */
#line 530 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3437 "src/parser.tab.c"
    break;

  case 28: /* statement: gosub_statement NEWLINE  */
#line 531 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3443 "src/parser.tab.c"
    break;

  case 29: /* statement: break_statement NEWLINE  */
#line 532 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3449 "src/parser.tab.c"
    break;

  case 30: /* statement: continue_statement NEWLINE  */
#line 533 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3455 "src/parser.tab.c"
    break;

  case 31: /* statement: if_statement  */
#line 534 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3461 "src/parser.tab.c"
    break;

  case 32: /* assignment: lvalue OP_EQ expression  */
#line 538 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 3467 "src/parser.tab.c"
    break;

  case 33: /* assignment: lvalue compound_op expression  */
#line 543 "src/parser.y"
                                    { (yyval.stmt) = ast_assign_op((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr), (yyvsp[-1].op_char)); }
#line 3473 "src/parser.tab.c"
    break;

  case 34: /* assignment: lvalue comparison_lens compound_op expression  */
#line 544 "src/parser.y"
                                                    {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign_op((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr), (yyvsp[-1].op_char));
      }
#line 3487 "src/parser.tab.c"
    break;

  case 35: /* assignment: lvalue comparison_lens OP_EQ expression  */
#line 556 "src/parser.y"
                                              {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 3501 "src/parser.tab.c"
    break;

  case 36: /* compound_op: PLUS_EQ  */
#line 568 "src/parser.y"
               { (yyval.op_char) = '+'; }
#line 3507 "src/parser.tab.c"
    break;

  case 37: /* compound_op: MINUS_EQ  */
#line 569 "src/parser.y"
               { (yyval.op_char) = '-'; }
#line 3513 "src/parser.tab.c"
    break;

  case 38: /* compound_op: STAR_EQ  */
#line 570 "src/parser.y"
               { (yyval.op_char) = '*'; }
#line 3519 "src/parser.tab.c"
    break;

  case 39: /* compound_op: SLASH_EQ  */
#line 571 "src/parser.y"
               { (yyval.op_char) = '/'; }
#line 3525 "src/parser.tab.c"
    break;

  case 40: /* lvalue: variable_name  */
#line 575 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3531 "src/parser.tab.c"
    break;

  case 41: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 576 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3537 "src/parser.tab.c"
    break;

  case 42: /* lvalue: lvalue DOT dot_field_name  */
#line 577 "src/parser.y"
                                             { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3543 "src/parser.tab.c"
    break;

  case 43: /* variable_name: IDENT  */
#line 581 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 3549 "src/parser.tab.c"
    break;

  case 44: /* variable_name: END  */
#line 582 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 3555 "src/parser.tab.c"
    break;

  case 45: /* variable_name: NEXT  */
#line 583 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 3561 "src/parser.tab.c"
    break;

  case 46: /* variable_name: LOOP  */
#line 588 "src/parser.y"
                        { (yyval.text) = copy_const("loop"); }
#line 3567 "src/parser.tab.c"
    break;

  case 47: /* variable_name: UNTIL  */
#line 589 "src/parser.y"
                         { (yyval.text) = copy_const("until"); }
#line 3573 "src/parser.tab.c"
    break;

  case 48: /* $@1: %empty  */
#line 593 "src/parser.y"
             { lexer_begin_lens_content(ctx->active_lexer); }
#line 3579 "src/parser.tab.c"
    break;

  case 49: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 593 "src/parser.y"
                                                                                  {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 3587 "src/parser.tab.c"
    break;

  case 50: /* modifier_name: modifier_word  */
#line 599 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 3593 "src/parser.tab.c"
    break;

  case 51: /* modifier_name: modifier_name modifier_word  */
#line 600 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 3599 "src/parser.tab.c"
    break;

  case 52: /* modifier_word: IDENT  */
#line 604 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 3605 "src/parser.tab.c"
    break;

  case 53: /* modifier_word: TO  */
#line 605 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 3611 "src/parser.tab.c"
    break;

  case 54: /* modifier_word: END  */
#line 606 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 3617 "src/parser.tab.c"
    break;

  case 55: /* modifier_word: NEXT  */
#line 607 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 3623 "src/parser.tab.c"
    break;

  case 56: /* print_statement: PRINT expression  */
#line 611 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 3629 "src/parser.tab.c"
    break;

  case 57: /* print_statement: PRINT TO ERROR_VALUE expression  */
#line 617 "src/parser.y"
                                      { (yyval.stmt) = ast_print_error((yyvsp[0].expr)); }
#line 3635 "src/parser.tab.c"
    break;

  case 58: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 621 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 3641 "src/parser.tab.c"
    break;

  case 59: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 622 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 3652 "src/parser.tab.c"
    break;

  case 60: /* call_statement: lvalue DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 628 "src/parser.y"
                                                       {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 3662 "src/parser.tab.c"
    break;

  case 61: /* call_statement: lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 633 "src/parser.y"
                                                                 {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 3676 "src/parser.tab.c"
    break;

  case 62: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 642 "src/parser.y"
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
#line 3691 "src/parser.tab.c"
    break;

  case 63: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 655 "src/parser.y"
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
#line 3708 "src/parser.tab.c"
    break;

  case 64: /* for_end: END FOR NEWLINE  */
#line 678 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 3714 "src/parser.tab.c"
    break;

  case 65: /* for_end: NEXT NEWLINE  */
#line 679 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 3720 "src/parser.tab.c"
    break;

  case 66: /* for_end: NEXT variable_name NEWLINE  */
#line 680 "src/parser.y"
                                 { (yyval.text) = (yyvsp[-1].text); }
#line 3726 "src/parser.tab.c"
    break;

  case 67: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list for_end  */
#line 684 "src/parser.y"
                                                             {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3735 "src/parser.tab.c"
    break;

  case 68: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list for_end  */
#line 688 "src/parser.y"
                                                                  {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3744 "src/parser.tab.c"
    break;

  case 69: /* for_each_statement: FOR IDENT OP_EQ expression TO expression NEWLINE statement_list for_end  */
#line 695 "src/parser.y"
                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].expr), NULL, (yyvsp[-1].stmt_list));
      }
#line 3753 "src/parser.tab.c"
    break;

  case 70: /* for_each_statement: FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list for_end  */
#line 699 "src/parser.y"
                                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-9].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-9].text), (yyvsp[-7].expr), (yyvsp[-5].expr), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3762 "src/parser.tab.c"
    break;

  case 71: /* do_loop_statement: DO NEWLINE statement_list LOOP UNTIL expression NEWLINE  */
#line 708 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_do_loop((yyvsp[-4].stmt_list), (yyvsp[-1].expr), 1);
      }
#line 3770 "src/parser.tab.c"
    break;

  case 72: /* do_loop_statement: DO NEWLINE statement_list LOOP WHILE expression NEWLINE  */
#line 711 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_do_loop((yyvsp[-4].stmt_list), (yyvsp[-1].expr), 0);
      }
#line 3778 "src/parser.tab.c"
    break;

  case 73: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 717 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 3786 "src/parser.tab.c"
    break;

  case 74: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 723 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 3794 "src/parser.tab.c"
    break;

  case 75: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 729 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 3802 "src/parser.tab.c"
    break;

  case 76: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 732 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 3810 "src/parser.tab.c"
    break;

  case 77: /* consider_else_opt: %empty  */
#line 738 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3816 "src/parser.tab.c"
    break;

  case 78: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 739 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3822 "src/parser.tab.c"
    break;

  case 79: /* consider_statement_list: %empty  */
#line 743 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3828 "src/parser.tab.c"
    break;

  case 80: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 744 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3834 "src/parser.tab.c"
    break;

  case 81: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 745 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3840 "src/parser.tab.c"
    break;

  case 82: /* consider_body_statement: assignment NEWLINE  */
#line 749 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3846 "src/parser.tab.c"
    break;

  case 83: /* consider_body_statement: print_statement NEWLINE  */
#line 750 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3852 "src/parser.tab.c"
    break;

  case 84: /* consider_body_statement: call_statement NEWLINE  */
#line 751 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3858 "src/parser.tab.c"
    break;

  case 85: /* consider_body_statement: with_lock_statement  */
#line 752 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3864 "src/parser.tab.c"
    break;

  case 86: /* consider_body_statement: for_each_statement  */
#line 753 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3870 "src/parser.tab.c"
    break;

  case 87: /* consider_body_statement: while_statement  */
#line 754 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3876 "src/parser.tab.c"
    break;

  case 88: /* consider_body_statement: do_loop_statement  */
#line 755 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3882 "src/parser.tab.c"
    break;

  case 89: /* consider_body_statement: consider_statement  */
#line 756 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3888 "src/parser.tab.c"
    break;

  case 90: /* consider_body_statement: function_statement  */
#line 757 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3894 "src/parser.tab.c"
    break;

  case 91: /* consider_body_statement: modifier_statement  */
#line 758 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3900 "src/parser.tab.c"
    break;

  case 92: /* consider_body_statement: program_statement  */
#line 759 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3906 "src/parser.tab.c"
    break;

  case 93: /* consider_body_statement: library_statement  */
#line 760 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3912 "src/parser.tab.c"
    break;

  case 94: /* consider_body_statement: use_statement NEWLINE  */
#line 761 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3918 "src/parser.tab.c"
    break;

  case 95: /* consider_body_statement: watch_statement  */
#line 762 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3924 "src/parser.tab.c"
    break;

  case 96: /* consider_body_statement: unwatch_statement NEWLINE  */
#line 763 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3930 "src/parser.tab.c"
    break;

  case 97: /* consider_body_statement: without_watchers_statement  */
#line 764 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3936 "src/parser.tab.c"
    break;

  case 98: /* consider_body_statement: on_error_statement NEWLINE  */
#line 765 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3942 "src/parser.tab.c"
    break;

  case 99: /* consider_body_statement: error_statement NEWLINE  */
#line 766 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3948 "src/parser.tab.c"
    break;

  case 100: /* consider_body_statement: return_statement NEWLINE  */
#line 767 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3954 "src/parser.tab.c"
    break;

  case 101: /* consider_body_statement: label_statement NEWLINE  */
#line 768 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3960 "src/parser.tab.c"
    break;

  case 102: /* consider_body_statement: goto_statement NEWLINE  */
#line 769 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3966 "src/parser.tab.c"
    break;

  case 103: /* consider_body_statement: gosub_statement NEWLINE  */
#line 770 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3972 "src/parser.tab.c"
    break;

  case 104: /* consider_body_statement: break_statement NEWLINE  */
#line 771 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3978 "src/parser.tab.c"
    break;

  case 105: /* consider_body_statement: continue_statement NEWLINE  */
#line 772 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3984 "src/parser.tab.c"
    break;

  case 106: /* consider_body_statement: if_statement  */
#line 773 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3990 "src/parser.tab.c"
    break;

  case 107: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 777 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 3998 "src/parser.tab.c"
    break;

  case 108: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 780 "src/parser.y"
                                                                                                            {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4007 "src/parser.tab.c"
    break;

  case 109: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 787 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 4015 "src/parser.tab.c"
    break;

  case 110: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 790 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 4023 "src/parser.tab.c"
    break;

  case 111: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 796 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4031 "src/parser.tab.c"
    break;

  case 112: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 802 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 4039 "src/parser.tab.c"
    break;

  case 113: /* use_statement: USE IDENT  */
#line 808 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4045 "src/parser.tab.c"
    break;

  case 114: /* use_statement: LOAD IDENT  */
#line 809 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4051 "src/parser.tab.c"
    break;

  case 115: /* use_statement: USE STRING  */
#line 810 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4057 "src/parser.tab.c"
    break;

  case 116: /* use_statement: LOAD STRING  */
#line 811 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4063 "src/parser.tab.c"
    break;

  case 117: /* use_statement: USE IDENT IDENT STRING  */
#line 812 "src/parser.y"
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
#line 4084 "src/parser.tab.c"
    break;

  case 118: /* use_statement: LOAD IDENT IDENT STRING  */
#line 828 "src/parser.y"
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
#line 4105 "src/parser.tab.c"
    break;

  case 119: /* modifier_signature: modifier_name  */
#line 847 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 4111 "src/parser.tab.c"
    break;

  case 120: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 848 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 4117 "src/parser.tab.c"
    break;

  case 121: /* modifier_context: IDENT  */
#line 852 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4123 "src/parser.tab.c"
    break;

  case 122: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 856 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4131 "src/parser.tab.c"
    break;

  case 123: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 859 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 4139 "src/parser.tab.c"
    break;

  case 124: /* watch_statement: WATCH IDENT LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 867 "src/parser.y"
                                                                                           {
        (yyval.stmt) = ast_watch((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4147 "src/parser.tab.c"
    break;

  case 125: /* unwatch_statement: UNWATCH expression  */
#line 873 "src/parser.y"
                         { (yyval.stmt) = ast_unwatch((yyvsp[0].expr)); }
#line 4153 "src/parser.tab.c"
    break;

  case 126: /* watch_target_list: watch_target_path  */
#line 877 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4159 "src/parser.tab.c"
    break;

  case 127: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 878 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4165 "src/parser.tab.c"
    break;

  case 128: /* server_statement: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 897 "src/parser.y"
                                                                                             {
        (yyval.stmt) = ast_server((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4173 "src/parser.tab.c"
    break;

  case 129: /* server_statement: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 900 "src/parser.y"
                                                                           {
        (yyval.stmt) = ast_server((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4181 "src/parser.tab.c"
    break;

  case 130: /* server_item_list: %empty  */
#line 906 "src/parser.y"
             { (yyval.server_item_list) = ast_server_item_list_empty(); }
#line 4187 "src/parser.tab.c"
    break;

  case 131: /* server_item_list: server_item_list NEWLINE  */
#line 907 "src/parser.y"
                               { (yyval.server_item_list) = (yyvsp[-1].server_item_list); }
#line 4193 "src/parser.tab.c"
    break;

  case 132: /* server_item_list: server_item_list server_item  */
#line 908 "src/parser.y"
                                   { (yyval.server_item_list) = ast_server_item_list_append((yyvsp[-1].server_item_list), (yyvsp[0].server_item)); }
#line 4199 "src/parser.tab.c"
    break;

  case 133: /* server_item: IDENT server_string_list NEWLINE  */
#line 912 "src/parser.y"
                                       {
        (yyval.server_item) = ast_server_directive((yyvsp[-2].text), (yyvsp[-1].name_list), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 4207 "src/parser.tab.c"
    break;

  case 134: /* server_item: IDENT STRING LPAREN parameter_list_opt RPAREN NEWLINE statement_list END IDENT NEWLINE  */
#line 915 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_handler((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4215 "src/parser.tab.c"
    break;

  case 135: /* server_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 918 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_site((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4223 "src/parser.tab.c"
    break;

  case 136: /* server_item: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 921 "src/parser.y"
                                                                           {
        (yyval.server_item) = ast_server_site((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 4231 "src/parser.tab.c"
    break;

  case 137: /* server_item: ON IDENT NEWLINE statement_list END ON NEWLINE  */
#line 924 "src/parser.y"
                                                     {
        (yyval.server_item) = ast_server_hook((yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 4239 "src/parser.tab.c"
    break;

  case 138: /* server_string_list: STRING  */
#line 930 "src/parser.y"
             { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4245 "src/parser.tab.c"
    break;

  case 139: /* server_string_list: server_string_list COMMA STRING  */
#line 931 "src/parser.y"
                                      { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4251 "src/parser.tab.c"
    break;

  case 140: /* watch_target_path: variable_name  */
#line 935 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 4257 "src/parser.tab.c"
    break;

  case 141: /* watch_target_path: watch_target_path DOT IDENT  */
#line 936 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 4263 "src/parser.tab.c"
    break;

  case 142: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 940 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 4271 "src/parser.tab.c"
    break;

  case 143: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 946 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 4277 "src/parser.tab.c"
    break;

  case 144: /* on_error_statement: ON ERROR_VALUE GOTO NEXT  */
#line 947 "src/parser.y"
                               { (yyval.stmt) = ast_on_error_goto_next(); }
#line 4283 "src/parser.tab.c"
    break;

  case 145: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 948 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 4289 "src/parser.tab.c"
    break;

  case 146: /* on_error_statement: ON IDENT GOTO NEXT  */
#line 949 "src/parser.y"
                         {
        if (!warn_channel_ok(ctx, (yyvsp[-2].text), (yylsp[-2]).first_line, (yylsp[-2]).first_column)) { YYERROR; }
        free((yyvsp[-2].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_NEXT);
      }
#line 4299 "src/parser.tab.c"
    break;

  case 147: /* on_error_statement: ON IDENT GOTO IDENT  */
#line 954 "src/parser.y"
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
#line 4317 "src/parser.tab.c"
    break;

  case 148: /* on_error_statement: ON IDENT STOP  */
#line 967 "src/parser.y"
                    {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_STOP);
      }
#line 4327 "src/parser.tab.c"
    break;

  case 149: /* on_error_statement: ON IDENT PRINT  */
#line 972 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_PRINT);
      }
#line 4337 "src/parser.tab.c"
    break;

  case 150: /* on_error_statement: ON IDENT IDENT  */
#line 977 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        int mode = warn_mode_word(ctx, (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column);
        if (mode < 0) { free((yyvsp[-1].text)); free((yyvsp[0].text)); YYERROR; }
        free((yyvsp[-1].text)); free((yyvsp[0].text));
        (yyval.stmt) = ast_on_warning(mode);
      }
#line 4349 "src/parser.tab.c"
    break;

  case 151: /* error_statement: ERROR_VALUE expression  */
#line 987 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 4355 "src/parser.tab.c"
    break;

  case 152: /* return_statement: RETURN  */
#line 991 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 4361 "src/parser.tab.c"
    break;

  case 153: /* return_statement: RETURN expression  */
#line 992 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 4367 "src/parser.tab.c"
    break;

  case 154: /* label_statement: variable_name COLON  */
#line 996 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 4373 "src/parser.tab.c"
    break;

  case 155: /* goto_statement: GOTO variable_name  */
#line 1003 "src/parser.y"
                         { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 4379 "src/parser.tab.c"
    break;

  case 156: /* gosub_statement: GOSUB variable_name  */
#line 1007 "src/parser.y"
                          { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 4385 "src/parser.tab.c"
    break;

  case 157: /* break_statement: BREAK  */
#line 1016 "src/parser.y"
            { (yyval.stmt) = ast_break(NULL); }
#line 4391 "src/parser.tab.c"
    break;

  case 158: /* break_statement: BREAK IDENT  */
#line 1017 "src/parser.y"
                  { (yyval.stmt) = ast_break((yyvsp[0].text)); }
#line 4397 "src/parser.tab.c"
    break;

  case 159: /* continue_statement: CONTINUE  */
#line 1021 "src/parser.y"
               { (yyval.stmt) = ast_continue(NULL); }
#line 4403 "src/parser.tab.c"
    break;

  case 160: /* continue_statement: CONTINUE IDENT  */
#line 1022 "src/parser.y"
                     { (yyval.stmt) = ast_continue((yyvsp[0].text)); }
#line 4409 "src/parser.tab.c"
    break;

  case 161: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1026 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4418 "src/parser.tab.c"
    break;

  case 162: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1030 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4427 "src/parser.tab.c"
    break;

  case 163: /* if_block_tail: END IF NEWLINE  */
#line 1037 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4435 "src/parser.tab.c"
    break;

  case 164: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 1040 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4443 "src/parser.tab.c"
    break;

  case 165: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1043 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4451 "src/parser.tab.c"
    break;

  case 166: /* if_inline_tail: %empty  */
#line 1049 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4459 "src/parser.tab.c"
    break;

  case 167: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 1052 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4467 "src/parser.tab.c"
    break;

  case 168: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1055 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4475 "src/parser.tab.c"
    break;

  case 169: /* inline_statement: assignment  */
#line 1061 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4481 "src/parser.tab.c"
    break;

  case 170: /* inline_statement: print_statement  */
#line 1062 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4487 "src/parser.tab.c"
    break;

  case 171: /* inline_statement: call_statement  */
#line 1063 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4493 "src/parser.tab.c"
    break;

  case 172: /* inline_statement: use_statement  */
#line 1064 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4499 "src/parser.tab.c"
    break;

  case 173: /* inline_statement: on_error_statement  */
#line 1065 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4505 "src/parser.tab.c"
    break;

  case 174: /* inline_statement: error_statement  */
#line 1066 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4511 "src/parser.tab.c"
    break;

  case 175: /* inline_statement: return_statement  */
#line 1067 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4517 "src/parser.tab.c"
    break;

  case 176: /* inline_statement: goto_statement  */
#line 1068 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4523 "src/parser.tab.c"
    break;

  case 177: /* inline_statement: gosub_statement  */
#line 1069 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4529 "src/parser.tab.c"
    break;

  case 178: /* inline_statement: break_statement  */
#line 1070 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4535 "src/parser.tab.c"
    break;

  case 179: /* inline_statement: continue_statement  */
#line 1071 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4541 "src/parser.tab.c"
    break;

  case 180: /* expression: or_expression  */
#line 1075 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 4547 "src/parser.tab.c"
    break;

  case 181: /* or_expression: and_expression  */
#line 1079 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 4553 "src/parser.tab.c"
    break;

  case 182: /* or_expression: or_expression OR and_expression  */
#line 1080 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4559 "src/parser.tab.c"
    break;

  case 183: /* and_expression: comparison_expression  */
#line 1084 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 4565 "src/parser.tab.c"
    break;

  case 184: /* and_expression: and_expression AND comparison_expression  */
#line 1085 "src/parser.y"
                                               { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4571 "src/parser.tab.c"
    break;

  case 185: /* comparison_expression: additive_expression  */
#line 1089 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 4577 "src/parser.tab.c"
    break;

  case 186: /* comparison_expression: additive_expression comparison_operator additive_expression  */
#line 1090 "src/parser.y"
                                                                  { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4583 "src/parser.tab.c"
    break;

  case 187: /* comparison_expression: additive_expression comparison_lens comparison_operator additive_expression  */
#line 1091 "src/parser.y"
                                                                                  {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 4591 "src/parser.tab.c"
    break;

  case 188: /* additive_expression: multiplicative_expression  */
#line 1097 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 4597 "src/parser.tab.c"
    break;

  case 189: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 1098 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4603 "src/parser.tab.c"
    break;

  case 190: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 1099 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4609 "src/parser.tab.c"
    break;

  case 191: /* multiplicative_expression: unary_expression  */
#line 1103 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 4615 "src/parser.tab.c"
    break;

  case 192: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 1104 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4621 "src/parser.tab.c"
    break;

  case 193: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 1105 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4627 "src/parser.tab.c"
    break;

  case 194: /* unary_expression: postfix_expression  */
#line 1109 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 4633 "src/parser.tab.c"
    break;

  case 195: /* unary_expression: NOT unary_expression  */
#line 1110 "src/parser.y"
                           { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4639 "src/parser.tab.c"
    break;

  case 196: /* unary_expression: MINUS unary_expression  */
#line 1111 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4645 "src/parser.tab.c"
    break;

  case 197: /* unary_expression: NEW postfix_expression  */
#line 1112 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4651 "src/parser.tab.c"
    break;

  case 198: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 1113 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 4657 "src/parser.tab.c"
    break;

  case 199: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 1114 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4663 "src/parser.tab.c"
    break;

  case 200: /* postfix_expression: primary  */
#line 1118 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 4669 "src/parser.tab.c"
    break;

  case 201: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 1119 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4675 "src/parser.tab.c"
    break;

  case 202: /* postfix_expression: postfix_expression DOT dot_field_name  */
#line 1120 "src/parser.y"
                                            { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4681 "src/parser.tab.c"
    break;

  case 203: /* postfix_expression: postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1121 "src/parser.y"
                                                                   {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        (yyval.expr) = expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 4691 "src/parser.tab.c"
    break;

  case 204: /* postfix_expression: postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1126 "src/parser.y"
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
#line 4707 "src/parser.tab.c"
    break;

  case 205: /* comparison_operator: OP_EQ  */
#line 1140 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 4713 "src/parser.tab.c"
    break;

  case 206: /* comparison_operator: OP_NE  */
#line 1141 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 4719 "src/parser.tab.c"
    break;

  case 207: /* comparison_operator: OP_GT  */
#line 1142 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 4725 "src/parser.tab.c"
    break;

  case 208: /* comparison_operator: OP_LT  */
#line 1143 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 4731 "src/parser.tab.c"
    break;

  case 209: /* comparison_operator: OP_GE  */
#line 1144 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 4737 "src/parser.tab.c"
    break;

  case 210: /* comparison_operator: OP_LE  */
#line 1145 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 4743 "src/parser.tab.c"
    break;

  case 211: /* comparison_operator: OP_NGT  */
#line 1146 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 4749 "src/parser.tab.c"
    break;

  case 212: /* comparison_operator: OP_NLT  */
#line 1147 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 4755 "src/parser.tab.c"
    break;

  case 213: /* comparison_operator: OP_NGE  */
#line 1148 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 4761 "src/parser.tab.c"
    break;

  case 214: /* comparison_operator: OP_NLE  */
#line 1149 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 4767 "src/parser.tab.c"
    break;

  case 215: /* primary: NUMBER  */
#line 1153 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4773 "src/parser.tab.c"
    break;

  case 216: /* primary: WATCHERS LPAREN RPAREN  */
#line 1154 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_call(copy_const("watchers"), ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4779 "src/parser.tab.c"
    break;

  case 217: /* primary: duration_terms  */
#line 1155 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4785 "src/parser.tab.c"
    break;

  case 218: /* primary: STRING  */
#line 1156 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4791 "src/parser.tab.c"
    break;

  case 219: /* primary: variable_name ident_suffix  */
#line 1157 "src/parser.y"
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
#line 4813 "src/parser.tab.c"
    break;

  case 220: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1174 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 4824 "src/parser.tab.c"
    break;

  case 221: /* primary: ERROR_VALUE  */
#line 1180 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4830 "src/parser.tab.c"
    break;

  case 222: /* primary: TRUE  */
#line 1181 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4836 "src/parser.tab.c"
    break;

  case 223: /* primary: FALSE  */
#line 1182 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4842 "src/parser.tab.c"
    break;

  case 224: /* primary: NOTHING  */
#line 1183 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4848 "src/parser.tab.c"
    break;

  case 225: /* primary: UNKNOWN_VALUE  */
#line 1184 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4854 "src/parser.tab.c"
    break;

  case 226: /* primary: LPAREN expression RPAREN  */
#line 1185 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 4860 "src/parser.tab.c"
    break;

  case 227: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 1186 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4866 "src/parser.tab.c"
    break;

  case 228: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 1187 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4872 "src/parser.tab.c"
    break;

  case 229: /* primary: record_literal  */
#line 1188 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 4878 "src/parser.tab.c"
    break;

  case 230: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 1192 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4884 "src/parser.tab.c"
    break;

  case 231: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 1193 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4890 "src/parser.tab.c"
    break;

  case 232: /* ident_suffix: %empty  */
#line 1197 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 4900 "src/parser.tab.c"
    break;

  case 233: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 1202 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 4910 "src/parser.tab.c"
    break;

  case 234: /* ident_suffix: DOT dot_field_name ident_dot_suffix  */
#line 1207 "src/parser.y"
                                          {
        /* dot_field_name, not IDENT: a keyword is a legal FIELD name after a
         * dot, because nothing but a name can appear there. */
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 4921 "src/parser.tab.c"
    break;

  case 235: /* ident_suffix: DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1213 "src/parser.y"
                                                          {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        (yyval.ident_suffix).kind = IDENT_SUFFIX_METHOD;
        (yyval.ident_suffix).name = (yyvsp[-3].text);
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 4935 "src/parser.tab.c"
    break;

  case 236: /* ident_dot_suffix: %empty  */
#line 1225 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 4945 "src/parser.tab.c"
    break;

  case 237: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 1230 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 4955 "src/parser.tab.c"
    break;

  case 238: /* duration_terms: NUMBER IDENT  */
#line 1238 "src/parser.y"
                   {
        AstDuration duration = {0};
        (yyval.duration) = duration_add_unit(duration, (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 4964 "src/parser.tab.c"
    break;

  case 239: /* duration_terms: duration_terms NUMBER IDENT  */
#line 1242 "src/parser.y"
                                  {
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 4972 "src/parser.tab.c"
    break;

  case 240: /* argument_list_opt: %empty  */
#line 1248 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 4978 "src/parser.tab.c"
    break;

  case 241: /* argument_list_opt: argument_list  */
#line 1249 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 4984 "src/parser.tab.c"
    break;

  case 242: /* argument_list: expression  */
#line 1253 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 4990 "src/parser.tab.c"
    break;

  case 243: /* argument_list: argument_list COMMA expression  */
#line 1254 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 4996 "src/parser.tab.c"
    break;

  case 244: /* array_argument_list: expression  */
#line 1258 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 5002 "src/parser.tab.c"
    break;

  case 245: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 1259 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 5008 "src/parser.tab.c"
    break;

  case 246: /* parameter_list_opt: %empty  */
#line 1263 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 5014 "src/parser.tab.c"
    break;

  case 247: /* parameter_list_opt: parameter_list  */
#line 1264 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 5020 "src/parser.tab.c"
    break;

  case 248: /* parameter_list: IDENT  */
#line 1268 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5026 "src/parser.tab.c"
    break;

  case 249: /* parameter_list: parameter_list COMMA IDENT  */
#line 1269 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5032 "src/parser.tab.c"
    break;

  case 250: /* field_name: dot_field_name  */
#line 1282 "src/parser.y"
                     { (yyval.text) = (yyvsp[0].text); }
#line 5038 "src/parser.tab.c"
    break;

  case 251: /* field_name: STRING  */
#line 1289 "src/parser.y"
             { (yyval.text) = (yyvsp[0].text); }
#line 5044 "src/parser.tab.c"
    break;

  case 252: /* dot_field_name: IDENT  */
#line 1298 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 5050 "src/parser.tab.c"
    break;

  case 253: /* dot_field_name: AS  */
#line 1299 "src/parser.y"
                     { (yyval.text) = kw_name("as"); }
#line 5056 "src/parser.tab.c"
    break;

  case 254: /* dot_field_name: NEXT  */
#line 1300 "src/parser.y"
                     { (yyval.text) = kw_name("next"); }
#line 5062 "src/parser.tab.c"
    break;

  case 255: /* dot_field_name: STOP  */
#line 1301 "src/parser.y"
                     { (yyval.text) = kw_name("stop"); }
#line 5068 "src/parser.tab.c"
    break;

  case 256: /* dot_field_name: ERROR_VALUE  */
#line 1302 "src/parser.y"
                     { (yyval.text) = kw_name("error"); }
#line 5074 "src/parser.tab.c"
    break;

  case 257: /* dot_field_name: END  */
#line 1303 "src/parser.y"
                     { (yyval.text) = kw_name("end"); }
#line 5080 "src/parser.tab.c"
    break;

  case 258: /* dot_field_name: TO  */
#line 1304 "src/parser.y"
                     { (yyval.text) = kw_name("to"); }
#line 5086 "src/parser.tab.c"
    break;

  case 259: /* dot_field_name: IN  */
#line 1305 "src/parser.y"
                     { (yyval.text) = kw_name("in"); }
#line 5092 "src/parser.tab.c"
    break;

  case 260: /* dot_field_name: ON  */
#line 1306 "src/parser.y"
                     { (yyval.text) = kw_name("on"); }
#line 5098 "src/parser.tab.c"
    break;

  case 261: /* dot_field_name: NEW  */
#line 1307 "src/parser.y"
                     { (yyval.text) = kw_name("new"); }
#line 5104 "src/parser.tab.c"
    break;

  case 262: /* dot_field_name: EACH  */
#line 1308 "src/parser.y"
                     { (yyval.text) = kw_name("each"); }
#line 5110 "src/parser.tab.c"
    break;

  case 263: /* dot_field_name: WITH  */
#line 1309 "src/parser.y"
                     { (yyval.text) = kw_name("with"); }
#line 5116 "src/parser.tab.c"
    break;

  case 264: /* dot_field_name: WITHOUT  */
#line 1310 "src/parser.y"
                     { (yyval.text) = kw_name("without"); }
#line 5122 "src/parser.tab.c"
    break;

  case 265: /* dot_field_name: THEN  */
#line 1311 "src/parser.y"
                     { (yyval.text) = kw_name("then"); }
#line 5128 "src/parser.tab.c"
    break;

  case 266: /* dot_field_name: ELSE  */
#line 1312 "src/parser.y"
                     { (yyval.text) = kw_name("else"); }
#line 5134 "src/parser.tab.c"
    break;

  case 267: /* dot_field_name: FOR  */
#line 1313 "src/parser.y"
                     { (yyval.text) = kw_name("for"); }
#line 5140 "src/parser.tab.c"
    break;

  case 268: /* dot_field_name: IF  */
#line 1314 "src/parser.y"
                     { (yyval.text) = kw_name("if"); }
#line 5146 "src/parser.tab.c"
    break;

  case 269: /* dot_field_name: WHILE  */
#line 1315 "src/parser.y"
                     { (yyval.text) = kw_name("while"); }
#line 5152 "src/parser.tab.c"
    break;

  case 270: /* dot_field_name: DO  */
#line 1316 "src/parser.y"
                     { (yyval.text) = kw_name("do"); }
#line 5158 "src/parser.tab.c"
    break;

  case 271: /* dot_field_name: LOOP  */
#line 1317 "src/parser.y"
                     { (yyval.text) = kw_name("loop"); }
#line 5164 "src/parser.tab.c"
    break;

  case 272: /* dot_field_name: UNTIL  */
#line 1318 "src/parser.y"
                     { (yyval.text) = kw_name("until"); }
#line 5170 "src/parser.tab.c"
    break;

  case 273: /* dot_field_name: PRINT  */
#line 1319 "src/parser.y"
                     { (yyval.text) = kw_name("print"); }
#line 5176 "src/parser.tab.c"
    break;

  case 274: /* dot_field_name: RETURN  */
#line 1320 "src/parser.y"
                     { (yyval.text) = kw_name("return"); }
#line 5182 "src/parser.tab.c"
    break;

  case 275: /* dot_field_name: LOAD  */
#line 1321 "src/parser.y"
                     { (yyval.text) = kw_name("load"); }
#line 5188 "src/parser.tab.c"
    break;

  case 276: /* dot_field_name: USE  */
#line 1322 "src/parser.y"
                     { (yyval.text) = kw_name("use"); }
#line 5194 "src/parser.tab.c"
    break;

  case 277: /* dot_field_name: NOT  */
#line 1323 "src/parser.y"
                     { (yyval.text) = kw_name("not"); }
#line 5200 "src/parser.tab.c"
    break;

  case 278: /* dot_field_name: AND  */
#line 1324 "src/parser.y"
                     { (yyval.text) = kw_name("and"); }
#line 5206 "src/parser.tab.c"
    break;

  case 279: /* dot_field_name: OR  */
#line 1325 "src/parser.y"
                     { (yyval.text) = kw_name("or"); }
#line 5212 "src/parser.tab.c"
    break;

  case 280: /* dot_field_name: TRUE  */
#line 1326 "src/parser.y"
                     { (yyval.text) = kw_name("true"); }
#line 5218 "src/parser.tab.c"
    break;

  case 281: /* dot_field_name: FALSE  */
#line 1327 "src/parser.y"
                     { (yyval.text) = kw_name("false"); }
#line 5224 "src/parser.tab.c"
    break;

  case 282: /* dot_field_name: NOTHING  */
#line 1328 "src/parser.y"
                     { (yyval.text) = kw_name("nothing"); }
#line 5230 "src/parser.tab.c"
    break;

  case 283: /* dot_field_name: BREAK  */
#line 1329 "src/parser.y"
                     { (yyval.text) = kw_name("break"); }
#line 5236 "src/parser.tab.c"
    break;

  case 284: /* dot_field_name: CONTINUE  */
#line 1330 "src/parser.y"
                     { (yyval.text) = kw_name("continue"); }
#line 5242 "src/parser.tab.c"
    break;

  case 285: /* dot_field_name: GOTO  */
#line 1331 "src/parser.y"
                     { (yyval.text) = kw_name("goto"); }
#line 5248 "src/parser.tab.c"
    break;

  case 286: /* dot_field_name: GOSUB  */
#line 1332 "src/parser.y"
                     { (yyval.text) = kw_name("gosub"); }
#line 5254 "src/parser.tab.c"
    break;

  case 287: /* dot_field_name: SPAWN  */
#line 1333 "src/parser.y"
                     { (yyval.text) = kw_name("spawn"); }
#line 5260 "src/parser.tab.c"
    break;

  case 288: /* dot_field_name: EXPORT  */
#line 1334 "src/parser.y"
                     { (yyval.text) = kw_name("export"); }
#line 5266 "src/parser.tab.c"
    break;

  case 289: /* dot_field_name: LIBRARY  */
#line 1335 "src/parser.y"
                     { (yyval.text) = kw_name("library"); }
#line 5272 "src/parser.tab.c"
    break;

  case 290: /* dot_field_name: FUNCTION  */
#line 1336 "src/parser.y"
                     { (yyval.text) = kw_name("function"); }
#line 5278 "src/parser.tab.c"
    break;

  case 291: /* dot_field_name: MODIFIER  */
#line 1337 "src/parser.y"
                     { (yyval.text) = kw_name("modifier"); }
#line 5284 "src/parser.tab.c"
    break;

  case 292: /* dot_field_name: PROGRAM  */
#line 1338 "src/parser.y"
                     { (yyval.text) = kw_name("program"); }
#line 5290 "src/parser.tab.c"
    break;

  case 293: /* dot_field_name: WATCH  */
#line 1339 "src/parser.y"
                     { (yyval.text) = kw_name("watch"); }
#line 5296 "src/parser.tab.c"
    break;

  case 294: /* dot_field_name: WATCHERS  */
#line 1340 "src/parser.y"
                     { (yyval.text) = kw_name("watchers"); }
#line 5302 "src/parser.tab.c"
    break;

  case 295: /* dot_field_name: CONSIDER  */
#line 1341 "src/parser.y"
                     { (yyval.text) = kw_name("consider"); }
#line 5308 "src/parser.tab.c"
    break;

  case 296: /* dot_field_name: STEP  */
#line 1342 "src/parser.y"
                     { (yyval.text) = kw_name("step"); }
#line 5314 "src/parser.tab.c"
    break;

  case 297: /* dot_field_name: UNWATCH  */
#line 1343 "src/parser.y"
                     { (yyval.text) = kw_name("unwatch"); }
#line 5320 "src/parser.tab.c"
    break;

  case 298: /* dot_field_name: UNKNOWN_VALUE  */
#line 1344 "src/parser.y"
                     { (yyval.text) = kw_name("unknown"); }
#line 5326 "src/parser.tab.c"
    break;

  case 299: /* record_field_list: field_name OP_EQ expression  */
#line 1348 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5332 "src/parser.tab.c"
    break;

  case 300: /* record_field_list: field_name COLON expression  */
#line 1349 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5338 "src/parser.tab.c"
    break;

  case 301: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1350 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5344 "src/parser.tab.c"
    break;

  case 302: /* record_field_list: record_field_list COMMA optional_newlines field_name OP_EQ expression  */
#line 1351 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5350 "src/parser.tab.c"
    break;

  case 303: /* record_field_list: record_field_list COMMA optional_newlines field_name COLON expression  */
#line 1352 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5356 "src/parser.tab.c"
    break;

  case 304: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1353 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5362 "src/parser.tab.c"
    break;

  case 305: /* field_policy: IDENT  */
#line 1361 "src/parser.y"
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
#line 5394 "src/parser.tab.c"
    break;

  case 306: /* field_policy: IDENT expression  */
#line 1388 "src/parser.y"
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
#line 5415 "src/parser.tab.c"
    break;


#line 5419 "src/parser.tab.c"

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

#line 1411 "src/parser.y"


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
