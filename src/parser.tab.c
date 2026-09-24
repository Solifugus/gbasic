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

#include <ctype.h>
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

/* `for each item, item in list` -- the element and the index cannot share a
 * name. Neither binding is wrong on its own, so nothing would raise: the index
 * is assigned second and would simply overwrite the element, giving a loop
 * whose variable is a number and whose body reads nonsense from it. Refused at
 * parse time, where the typo is visible. */
static int for_each_index_distinct(gb_parse_ctx *ctx, const char *value_name,
                                   const char *index_name, int line, int column) {
    if (!value_name || !index_name || strcmp(value_name, index_name) != 0) {
        return 1;
    }
    char message[192];
    snprintf(message, sizeof(message),
             "`for each %s, %s` names the element and the index the same thing; "
             "the index would overwrite the element",
             value_name, index_name);
    report_syntax_error(ctx, line, column, line, column, message);
    return 0;
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

/* The message NAMES the units that exist, because the commonest cause is a
 * unit this language does not have (`fortnight`, `ms`) and the second
 * commonest is a plural nobody is sure about. It also names the one shape a
 * reader is most likely to have meant instead: before 2026-09-23 `1e20` came
 * down this path as the number 1 beside the "unit" e20. */
static void duration_unit_error(gb_parse_ctx *ctx, char *unit,
                                int line, int column, int end_line, int end_column) {
    char message[256];
    snprintf(message, sizeof(message),
             "unknown duration unit '%s' -- the units are year, month, week, day, "
             "hour, minute and second, singular or plural", unit);
    free(unit);
    report_syntax_error(ctx, line, column, end_line, end_column, message);
}

static int unit_is(const char *text, const char *unit) {
    return strcmp(text, unit) == 0;
}

/* An unknown unit is REPORTED BACK rather than printed. It used to
 * `fprintf` an unlocated line to stderr, answer `0 seconds` and carry on with
 * EXIT 0 -- the exact signature run_silent_traps.sh was built for: a bare
 * line, a plausible value and a successful exit. It bypassed the diagnostics
 * sink too, so `--json-diagnostics` emitted a non-JSON line into a JSON
 * stream, which is the defect run_parse_exit.sh exists for. `1 fortnight` was
 * a duration of zero that nothing downstream could detect. It is a LOCATED
 * parse error now, so nothing runs. */
static AstDuration duration_add_unit(AstDuration duration, double amount, char *unit,
                                     char **bad_unit) {
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
        if (bad_unit && !*bad_unit) {
            *bad_unit = unit;   /* ownership moves to the caller, which frees it */
            return duration;
        }
    }
    free(unit);
    return duration;
}



#line 483 "src/parser.tab.c"

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
  YYSYMBOL_MODIFIER_PREFIX = 8,            /* MODIFIER_PREFIX  */
  YYSYMBOL_AS = 9,                         /* AS  */
  YYSYMBOL_DIM = 10,                       /* DIM  */
  YYSYMBOL_PLUS_EQ = 11,                   /* PLUS_EQ  */
  YYSYMBOL_MINUS_EQ = 12,                  /* MINUS_EQ  */
  YYSYMBOL_STAR_EQ = 13,                   /* STAR_EQ  */
  YYSYMBOL_SLASH_EQ = 14,                  /* SLASH_EQ  */
  YYSYMBOL_IF = 15,                        /* IF  */
  YYSYMBOL_CONSIDER_IF = 16,               /* CONSIDER_IF  */
  YYSYMBOL_THEN = 17,                      /* THEN  */
  YYSYMBOL_ELSE = 18,                      /* ELSE  */
  YYSYMBOL_CONSIDER_ELSE = 19,             /* CONSIDER_ELSE  */
  YYSYMBOL_END = 20,                       /* END  */
  YYSYMBOL_END_CONSIDER = 21,              /* END_CONSIDER  */
  YYSYMBOL_PRINT = 22,                     /* PRINT  */
  YYSYMBOL_TRUE = 23,                      /* TRUE  */
  YYSYMBOL_FALSE = 24,                     /* FALSE  */
  YYSYMBOL_NOTHING = 25,                   /* NOTHING  */
  YYSYMBOL_UNKNOWN_VALUE = 26,             /* UNKNOWN_VALUE  */
  YYSYMBOL_AND = 27,                       /* AND  */
  YYSYMBOL_OR = 28,                        /* OR  */
  YYSYMBOL_NOT = 29,                       /* NOT  */
  YYSYMBOL_WITH = 30,                      /* WITH  */
  YYSYMBOL_NEW = 31,                       /* NEW  */
  YYSYMBOL_SPAWN = 32,                     /* SPAWN  */
  YYSYMBOL_FOR = 33,                       /* FOR  */
  YYSYMBOL_TO = 34,                        /* TO  */
  YYSYMBOL_STEP = 35,                      /* STEP  */
  YYSYMBOL_DO = 36,                        /* DO  */
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
  YYSYMBOL_not_expression = 143,           /* not_expression  */
  YYSYMBOL_comparison_expression = 144,    /* comparison_expression  */
  YYSYMBOL_additive_expression = 145,      /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 146, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 147,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 148,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 149,      /* comparison_operator  */
  YYSYMBOL_primary = 150,                  /* primary  */
  YYSYMBOL_record_literal = 151,           /* record_literal  */
  YYSYMBOL_ident_suffix = 152,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 153,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 154,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 155,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 156,            /* argument_list  */
  YYSYMBOL_array_argument_list = 157,      /* array_argument_list  */
  YYSYMBOL_parameter_list_opt = 158,       /* parameter_list_opt  */
  YYSYMBOL_parameter_default = 159,        /* parameter_default  */
  YYSYMBOL_parameter_list = 160,           /* parameter_list  */
  YYSYMBOL_field_name = 161,               /* field_name  */
  YYSYMBOL_dot_field_name = 162,           /* dot_field_name  */
  YYSYMBOL_record_field_list = 163,        /* record_field_list  */
  YYSYMBOL_field_policy = 164,             /* field_policy  */
  YYSYMBOL_optional_newlines = 165         /* optional_newlines  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;



/* Unqualified %code blocks.  */
#line 482 "src/parser.y"

static int yylex(YYSTYPE *lvalp, YYLTYPE *llocp, gb_parse_ctx *ctx);
static void yyerror(YYLTYPE *llocp, gb_parse_ctx *ctx, const char *message);
static void report_syntax_error(gb_parse_ctx *ctx, int line, int column,
                                int end_line, int end_column, const char *message);

#line 690 "src/parser.tab.c"

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
#define YYLAST   2651

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  88
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  78
/* YYNRULES -- Number of rules.  */
#define YYNRULES  327
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  700

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
       0,   566,   566,   570,   571,   572,   576,   577,   578,   579,
     580,   581,   582,   583,   584,   585,   586,   587,   588,   589,
     590,   591,   592,   593,   594,   595,   596,   597,   598,   599,
     600,   601,   607,   618,   623,   624,   636,   648,   649,   650,
     651,   655,   656,   657,   661,   662,   663,   674,   674,   682,
     686,   687,   691,   692,   693,   694,   698,   704,   708,   709,
     715,   720,   729,   742,   771,   772,   773,   777,   781,   797,
     802,   810,   814,   835,   841,   847,   853,   856,   862,   863,
     867,   868,   869,   873,   874,   875,   876,   877,   878,   879,
     880,   881,   882,   883,   884,   885,   886,   887,   888,   889,
     890,   891,   892,   893,   894,   895,   896,   897,   903,   914,
     917,   924,   927,   933,   939,   945,   946,   947,   948,   949,
     950,   966,   982,  1003,  1004,  1008,  1012,  1015,  1023,  1029,
    1033,  1034,  1053,  1056,  1062,  1063,  1064,  1068,  1071,  1074,
    1077,  1080,  1086,  1087,  1091,  1092,  1096,  1102,  1103,  1104,
    1105,  1110,  1123,  1128,  1133,  1143,  1147,  1148,  1152,  1159,
    1163,  1172,  1173,  1177,  1178,  1182,  1186,  1193,  1196,  1199,
    1208,  1218,  1221,  1224,  1230,  1237,  1247,  1248,  1249,  1250,
    1251,  1252,  1253,  1254,  1255,  1256,  1257,  1261,  1265,  1266,
    1270,  1271,  1293,  1294,  1298,  1299,  1300,  1306,  1307,  1308,
    1312,  1313,  1314,  1318,  1319,  1326,  1330,  1331,  1332,  1336,
    1337,  1338,  1339,  1344,  1358,  1359,  1360,  1361,  1362,  1363,
    1364,  1365,  1366,  1367,  1371,  1372,  1373,  1374,  1375,  1392,
    1398,  1399,  1400,  1401,  1402,  1403,  1404,  1405,  1406,  1410,
    1411,  1415,  1420,  1425,  1431,  1443,  1448,  1456,  1466,  1478,
    1479,  1483,  1484,  1488,  1489,  1493,  1494,  1508,  1509,  1510,
    1511,  1512,  1513,  1514,  1515,  1519,  1520,  1523,  1524,  1539,
    1546,  1555,  1556,  1557,  1558,  1559,  1560,  1561,  1562,  1563,
    1564,  1565,  1566,  1567,  1568,  1569,  1570,  1571,  1572,  1573,
    1574,  1575,  1576,  1577,  1578,  1579,  1580,  1581,  1582,  1583,
    1584,  1585,  1586,  1587,  1588,  1589,  1590,  1591,  1592,  1593,
    1594,  1595,  1596,  1597,  1598,  1599,  1600,  1601,  1605,  1606,
    1607,  1608,  1609,  1610,  1618,  1645,  1664,  1665
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
  "STRING", "LENS_CONTENT", "QUALIFIED_IDENT", "MODIFIER_PREFIX", "AS",
  "DIM", "PLUS_EQ", "MINUS_EQ", "STAR_EQ", "SLASH_EQ", "IF", "CONSIDER_IF",
  "THEN", "ELSE", "CONSIDER_ELSE", "END", "END_CONSIDER", "PRINT", "TRUE",
  "FALSE", "NOTHING", "UNKNOWN_VALUE", "AND", "OR", "NOT", "WITH", "NEW",
  "SPAWN", "FOR", "TO", "STEP", "DO", "UNTIL", "IN", "EACH", "WHILE",
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
  "not_expression", "comparison_expression", "additive_expression",
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

#define YYPACT_NINF (-520)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -520,    61,   988,  -520,     6,    -4,  -520,  2222,  -520,  2188,
      98,    67,    24,  2222,  2222,   118,   154,   167,  2222,   177,
     177,    87,  2222,   127,    42,  -520,   569,   146,   186,   194,
      85,   238,   153,  -520,  -520,   141,   174,   162,   169,   171,
    -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,   176,
    -520,   178,  -520,  -520,   192,   197,   198,   200,   201,   203,
     206,   207,  -520,   182,  2222,  2222,   271,  -520,  -520,   202,
    2281,  -520,  -520,  -520,  -520,  2222,   661,   276,   217,  -520,
    2281,  2222,  -520,  -520,    86,   277,   267,   269,  -520,  -520,
    2255,   191,  -520,    97,  -520,  -520,   299,   248,  -520,   228,
     121,   305,  -520,   226,   227,  -520,  -520,   236,   254,  -520,
    -520,  -520,   255,   177,  -520,    48,   246,  -520,   250,   123,
      30,   331,  -520,  -520,  -520,  -520,  -520,   100,  -520,   303,
     261,   256,   196,  -520,   334,  -520,   146,  -520,  -520,  -520,
    -520,  -520,  -520,  2222,  2222,  -520,  2474,  2222,   105,  -520,
    -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,
    -520,  -520,  2358,  -520,   264,   268,   266,  -520,  2222,  -520,
    -520,    25,   278,   279,  -520,   280,   579,   749,  2222,  2532,
    -520,  2098,  2222,  2222,  -520,  -520,  -520,  -520,  -520,  -520,
    -520,  -520,  -520,  -520,  2281,  2281,   310,  2281,  2281,  2281,
    2222,  2590,   349,  2222,  2222,  2222,  2222,   351,   -17,   872,
    -520,   342,   358,   358,   177,   124,   177,  -520,   359,  -520,
    -520,  -520,     9,  -520,    96,  -520,   288,   358,  -520,   364,
     358,  -520,   365,   379,   380,   353,  -520,   308,   378,   312,
     315,  -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,
    -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,
    -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,
    -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,
    -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,  2222,
    2222,   316,  -520,   311,   106,  -520,   133,  -520,  2222,  -520,
     320,   325,  2222,  -520,  -520,  -520,  -520,  -520,   318,  -520,
     326,   335,  -520,   337,   339,   341,  -520,  -520,  -520,  -520,
    -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,   327,
     269,  -520,   191,   191,  2281,   195,  -520,  -520,   340,   344,
     345,  -520,  -520,  -520,   347,   338,   384,   387,  2222,   424,
    2222,  1046,  2222,   219,   367,   355,   348,   361,   136,   352,
     246,  1104,  -520,  1162,  -520,  -520,  -520,  -520,  2222,   368,
    -520,   357,   373,  1220,   435,  -520,  -520,   364,  -520,   372,
    2222,  2222,  -520,  -520,   451,  -520,  2222,  2222,   375,  -520,
    -520,  -520,  -520,   383,  -520,   148,   160,  -520,  2222,  2222,
    -520,   930,   446,   195,  -520,  2222,  2222,   385,  -520,  2222,
    2222,   386,   430,   389,   431,   459,  2222,   393,   457,   274,
     397,   481,   403,   405,  -520,   445,   450,   425,  -520,  -520,
     417,   452,   500,   427,  -520,   432,   437,  2222,   439,    47,
    -520,  -520,  -520,   814,  -520,   672,  -520,  -520,   441,   442,
     204,   497,  -520,  2079,  -520,   444,   447,  -520,  1278,   112,
     438,  -520,  2222,  -520,   449,   454,   509,  -520,   456,  -520,
    -520,  -520,  -520,  -520,  -520,   525,   527,  -520,  -520,   472,
    -520,  -520,  1336,   458,   460,  -520,  1394,  -520,   461,  -520,
    -520,  -520,  -520,  -520,   463,   265,   531,   537,  -520,  -520,
      53,   467,   108,  -520,  -520,  -520,  2222,  -520,   464,   465,
    2222,  -520,   466,  -520,  -520,  1452,   514,    60,  -520,  2222,
    -520,  -520,  1278,   469,  -520,  -520,   470,  1510,  -520,  -520,
    -520,  1568,   274,  1626,  1684,   508,  -520,  -520,   501,  1742,
    -520,  1800,  2222,   482,   484,   110,   477,   480,   561,   451,
    2222,  2222,   552,  1858,  -520,  -520,   553,  1916,  -520,   541,
     491,  -520,   494,   495,  1278,  1278,  -520,  -520,  1510,  -520,
    -520,  -520,   496,   504,   506,  -520,  -520,  -520,  -520,  -520,
    -520,  -520,  -520,  -520,  -520,   507,  -520,   512,  -520,   522,
     523,   528,   529,   530,   532,   534,   535,  -520,   571,  -520,
     577,   533,   539,   543,   572,   575,  -520,  2416,   358,   580,
    -520,  -520,  -520,   544,   556,  -520,  -520,   551,   621,  2126,
     622,   554,  -520,  -520,  -520,  -520,  -520,  1278,  1510,  -520,
    -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,  -520,
    -520,  -520,   555,   560,   562,  -520,  -520,   564,   566,   570,
     152,   563,  -520,  1974,  -520,   578,  -520,   585,  -520,   586,
     587,  -520,  1278,  -520,  -520,  -520,  -520,  -520,  -520,  -520,
     589,   594,   601,  2222,   930,  -520,   930,   446,  -520,  -520,
      73,  -520,  -520,   598,  -520,  -520,  -520,  -520,   658,    81,
    2032,  -520,   599,   685,   687,  -520,   609,   610,  -520,  -520
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       3,     0,     2,     1,    44,     0,    32,     0,    45,     0,
       0,     0,     0,     0,     0,   161,   163,     0,   156,     0,
       0,     0,     0,     0,     0,    46,     0,     0,     0,     0,
       0,     0,     0,     4,     5,     0,     0,    41,     0,     0,
       9,    10,    12,    11,    13,    14,    15,    16,    17,     0,
      19,     0,    20,    22,     0,     0,     0,     0,     0,     0,
       0,     0,    31,     0,   249,   249,   224,    44,   227,     0,
       0,   231,   232,   233,   234,     0,     0,     0,     0,   230,
       0,     0,   326,   326,   241,     0,   187,   188,   190,   192,
     194,   197,   200,   203,   209,   238,   226,     0,    56,     0,
       0,     0,     3,     0,     0,   162,   164,     0,     0,   157,
     159,   160,    44,     0,   144,     0,   130,   129,     0,     0,
       0,     0,   155,    52,    54,    53,    55,   123,    50,     0,
       0,     0,   116,   118,   115,   117,     0,     6,    49,    37,
      38,    39,    40,     0,     0,    47,     0,     0,     0,   158,
       7,     8,    18,    21,    23,    24,    25,    26,    27,    28,
      29,    30,     0,   251,     0,   250,     0,   247,   249,   205,
     193,   206,     0,     0,   204,     0,     0,     0,   249,     0,
     228,     0,     0,     0,   214,   215,   216,   217,   218,   219,
     220,   221,   222,   223,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       3,     0,   255,   255,     0,     0,     0,     3,     0,     3,
     154,   153,     0,   152,     0,   149,     0,   255,    51,     0,
     255,     3,     0,     0,     0,     0,    33,     0,     0,   271,
       0,   272,   317,   287,   284,   285,   276,   291,   298,   299,
     300,   316,   296,   297,   295,   282,   280,   305,   286,   277,
     314,   289,   290,   278,   281,   288,   313,   301,   302,   308,
     292,   303,   304,   311,   315,   283,   312,   279,   273,   274,
     275,   309,   310,   307,   293,   294,   306,    43,    34,     0,
       0,   271,   270,     0,     0,   269,     0,    58,     0,    59,
       0,     0,   249,   225,   235,   236,   327,   253,   326,   239,
     326,     0,   271,     0,   245,    44,     3,   176,    41,   177,
     178,   179,   180,   181,   182,   183,   184,   185,   186,     0,
     189,   191,   198,   199,     0,   195,   201,   202,     0,   271,
       0,   211,   248,    57,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    78,   265,     0,   256,     0,     0,     0,
     131,     0,   145,     0,   151,   150,   147,   148,   249,     0,
     125,     0,     0,     0,   121,   119,   120,     0,    42,     0,
     249,   249,    36,    35,     0,   134,     0,     0,     0,   326,
     252,   229,   207,     0,   326,     0,     0,   242,   249,   249,
     243,     0,   171,   196,   210,   249,   249,     0,     3,     0,
       0,     0,     0,     0,    45,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     3,    45,    45,     0,   124,     3,
       0,    45,     0,     0,    48,     0,     0,   324,     0,     0,
     318,   319,   134,     0,   208,     0,   237,   240,     0,     0,
       0,    45,   165,     0,   166,     0,     0,     3,     0,     0,
       0,     3,     0,    73,     0,     0,     0,    80,     0,   257,
     260,   261,   262,   263,   264,     0,     0,   266,     3,   267,
       3,     3,     0,     0,     0,    62,     0,     3,     0,   122,
       3,    60,    61,   325,     0,     0,     0,     0,   135,   136,
       0,   271,     0,   254,   244,   246,     0,     3,     0,     0,
       0,     3,     0,   212,   213,     0,    45,    46,    67,     0,
       3,     3,     0,     0,    74,    80,     0,    79,    75,   259,
     258,     0,     0,     0,     0,    45,   127,   146,    45,     0,
     114,     0,     0,     0,   142,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   168,   167,     0,     0,   172,    45,
       0,    65,     0,     0,     0,     0,    68,     3,    76,    80,
     108,    81,     0,     0,     0,    86,    87,    89,    88,    90,
      82,    91,    92,    93,    94,     0,    96,     0,    98,     0,
       0,     0,     0,     0,     0,     0,     0,   107,    45,   268,
      45,    45,     0,     0,    45,    45,   320,     0,   255,     0,
     137,   133,     3,     0,     0,   321,   322,     0,    45,     0,
      45,     0,    64,    66,     3,    71,    69,     0,    77,    83,
      84,    85,    95,    97,    99,   100,   101,   102,   103,   104,
     105,   106,     0,     0,     0,   126,   111,     0,     0,     0,
       0,     0,   143,     0,   132,     0,     3,     0,     3,     0,
       0,    63,     0,    70,   109,   110,   128,   113,   112,   134,
       0,     0,    45,     0,     0,   169,     0,   171,   173,    72,
       0,   134,     3,     0,   323,   170,   175,   174,     0,     0,
       0,   141,     0,     0,    45,   140,     0,     0,   139,   138
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -520,  -520,   -15,  -520,  -179,   557,  -520,    -2,   612,  -520,
    -520,   573,  -167,  -161,  -519,  -506,  -505,  -502,  -500,  -499,
    -520,  -520,  -433,  -520,  -493,  -490,  -489,  -487,  -145,   574,
     322,  -485,  -482,  -104,  -520,  -441,  -520,  -520,   490,  -480,
    -137,  -133,  -131,  -474,  -129,  -125,  -121,  -111,  -473,  -443,
      31,  -447,    17,  -520,   536,   -68,  -520,  -192,    78,   -47,
     631,   513,  -520,   410,  -520,  -520,  -520,   -54,  -520,  -520,
    -198,   181,  -520,   272,  -105,  -173,   168,   -71
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    34,    35,   147,    36,    84,   148,   238,
     127,   128,    38,    39,    40,   518,    41,    42,    43,    44,
     353,   418,   527,   580,    45,    46,    47,    48,    49,   129,
     371,    50,    51,   115,    52,   439,   499,   545,   116,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,   452,
     454,   329,   163,    86,    87,    88,    89,    90,    91,    92,
      93,   197,    94,    95,   180,   400,    96,   164,   165,   308,
     355,   477,   356,   294,   295,   296,   438,   176
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      37,   500,   317,   508,   310,   335,   512,   170,   575,   215,
      63,   166,   177,   364,   319,   357,   566,   110,   111,   114,
     320,   348,   576,   169,    85,   577,    98,   578,   579,   369,
     103,   104,   372,   174,   581,   109,   321,   582,   583,   117,
     584,   287,   586,   122,   322,   587,   119,   588,   323,   575,
     324,   495,   325,   592,   597,   301,   326,   495,   625,   626,
     327,     3,   365,   576,    67,   349,   577,   496,   578,   579,
     328,   100,    65,   548,   314,   581,   224,   495,   582,   583,
       8,   584,    64,   586,   225,   495,   587,   209,   588,   132,
     133,   112,   568,   688,   592,   597,   341,   120,   175,   497,
     366,   693,    99,   200,   123,   497,   101,     8,   102,   575,
     358,   114,   201,    25,   300,   331,   139,   140,   141,   142,
     124,   663,   105,   576,   311,   497,   577,   220,   578,   579,
     216,   498,   217,   497,   125,   581,   628,   498,   582,   583,
      25,   584,   403,   586,   561,   221,   587,   519,   588,   367,
     123,   336,   337,   126,   592,   597,   679,   498,   106,   205,
     236,   237,   178,   113,   288,   498,   124,   289,   386,   222,
     550,   107,   659,   179,   108,   200,   227,   223,   118,   318,
     125,    67,   138,   206,   201,   139,   140,   141,   142,   387,
     130,   551,   609,   307,   610,   351,   520,     8,   131,   126,
     232,   359,   361,   207,   363,   233,   216,    37,   315,   136,
     388,     5,   114,   423,   114,   389,   373,   338,   216,   506,
     343,   344,   345,   346,     8,   137,     9,   446,   680,   670,
      25,   685,   306,   686,   389,   416,   143,   395,   417,   396,
     689,   447,   134,   135,   306,   149,    15,    16,   393,    18,
      19,    20,   144,   150,   145,   151,    24,    25,   162,    26,
     152,   146,   153,    30,    31,   198,   199,   194,   195,   543,
     544,   317,   332,   333,   317,   167,   154,   469,   168,   470,
     172,   155,   156,   319,   157,   158,   319,   159,   507,   320,
     160,   161,   320,   173,   181,   182,   183,   471,   472,   473,
     474,   401,   202,   203,   204,   321,   382,   383,   321,   208,
     210,   211,   212,   322,   427,   390,   322,   323,   443,   324,
     323,   325,   324,   445,   325,   326,   435,   436,   326,   327,
     213,   214,   327,   218,   219,   226,   229,   230,   234,   328,
     231,   297,   328,   299,   448,   449,   475,   476,   572,    37,
     298,   455,   456,   342,   302,   347,   303,   304,   352,    37,
     573,    37,   354,   362,   368,   411,   574,   413,   370,   415,
     374,    37,   184,   185,   186,   187,   188,   189,   190,   191,
     192,   193,   585,   375,   379,   376,   377,   378,   380,   572,
     589,   381,   384,   458,   590,   385,   591,   391,   593,    37,
     394,   573,   594,   440,   441,    83,   595,   574,   389,   482,
     651,   402,   397,   398,   486,   399,   596,    64,   409,   404,
     405,   406,   408,   585,   407,   410,   459,   460,   412,   419,
     421,   589,   420,   466,   650,   590,   424,   591,   422,   593,
     317,   429,   515,   594,   432,   428,   522,   595,   318,   572,
     430,   318,   319,   434,   493,   437,    37,   596,   320,   442,
     444,   573,   503,   531,   453,   533,   534,   574,   462,   457,
     461,   464,   539,   463,   321,   541,   465,   467,   468,   523,
      37,   478,   322,   585,    37,   479,   323,   480,   324,   481,
     325,   589,   553,   483,   326,   590,   557,   591,   327,   593,
     484,   487,   485,   594,   489,   564,   565,   595,   328,   491,
     488,   490,   509,    37,   492,   562,   494,   596,   504,   505,
      37,   513,   521,   552,   514,    37,   526,   556,   529,    37,
     530,    37,    37,   524,   532,   546,   563,    37,   525,    37,
     528,   547,   536,   549,   537,   540,   542,   560,   554,   555,
     558,    37,   627,   567,   569,    37,   602,   603,   607,   606,
     608,   611,    37,    37,   612,   613,    37,   615,   616,   617,
     619,   621,    66,    67,    68,   622,    69,    70,   623,   624,
     629,   644,    66,    67,    68,   652,    69,    70,   630,     8,
     631,   632,    71,    72,    73,    74,   633,   653,    75,     8,
      76,    77,    71,    72,    73,    74,   634,   635,    75,   662,
      76,    77,   636,   637,   638,   642,   639,   318,   640,   641,
      78,   643,    25,   645,    79,    37,    37,   646,   654,   647,
      78,   648,    25,   655,    79,   656,   657,   660,   661,   664,
     671,   674,    80,   676,   665,    81,   666,    82,   667,    83,
     668,    37,    80,   683,   669,    81,   121,    82,   305,    83,
      37,   673,   692,   306,    66,    67,    68,   690,    69,   675,
     677,   678,    37,   681,    37,    66,    67,    68,   682,    69,
      70,     8,   691,   695,    71,    72,    73,    74,    37,   696,
     684,   697,     8,   698,   699,    71,    72,    73,    74,   433,
     228,    75,   196,    76,    77,   290,   360,   171,   687,   334,
     235,   392,    78,   599,    25,   502,    79,   614,   330,     0,
       0,     0,     0,    78,     0,    25,     0,    79,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    81,     0,    82,
       0,    83,     0,     0,     0,    80,     0,     0,    81,     0,
      82,     0,    83,   291,   292,     0,   306,     0,   241,   242,
       0,     0,     0,     0,   243,     0,   244,   245,     0,   246,
       0,   247,   248,   249,   250,   251,   252,   253,   254,   255,
     256,   257,   258,   259,   260,   261,   262,   263,   264,   265,
     266,   267,   268,   269,   270,   271,   272,   273,   274,   275,
     276,   277,   278,   279,   280,   281,   282,   283,   284,   285,
     286,     0,     0,     0,     0,     0,     0,     0,   501,   292,
       0,     0,     0,   241,   242,     0,     0,     0,     0,   243,
     309,   244,   245,   306,   246,     0,   247,   248,   249,   250,
     251,   252,   253,   254,   255,   256,   257,   258,   259,   260,
     261,   262,   263,   264,   265,   266,   267,   268,   269,   270,
     271,   272,   273,   274,   275,   276,   277,   278,   279,   280,
     281,   282,   283,   284,   285,   286,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     7,     0,     0,
       0,     0,     8,     0,     9,     0,     0,     0,   306,     0,
       0,     0,    10,     0,     0,    11,     0,     0,    12,   350,
       0,     0,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,     0,    24,    25,     0,    26,    27,    28,
      29,    30,    31,    32,     4,     0,     0,     5,     0,     0,
       6,     0,     0,     0,     0,     7,     0,     0,   450,     0,
     451,     0,     9,     0,     0,     0,    33,     0,     0,     0,
      10,     0,     0,    11,     0,     0,    12,     0,     0,     0,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,     0,    24,    25,     0,    26,    27,    28,    29,    30,
      31,    32,     4,     0,     0,     5,     0,     0,     6,     0,
       0,     0,     0,     7,     0,     0,     0,     0,     8,     0,
       9,     0,     0,     0,    33,     0,     0,     0,    10,     0,
       0,    11,     0,     0,    12,     0,     0,     0,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,     0,
      24,    25,     0,    26,    27,    28,    29,    30,    31,    32,
       4,     0,     0,     5,     0,     0,     6,     0,     0,     0,
       0,     7,     0,     0,     0,     0,   414,     0,     9,     0,
       0,     0,    33,     0,     0,     0,    10,     0,     0,    11,
       0,     0,    12,     0,     0,     0,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,     0,    24,    25,
       0,    26,    27,    28,    29,    30,    31,    32,     4,     0,
       0,     5,     0,     0,     6,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   425,     0,     9,     0,     0,     0,
      33,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     7,     0,     0,
       0,     0,   426,     0,     9,     0,     0,     0,    33,     0,
       0,     0,    10,     0,     0,    11,     0,     0,    12,     0,
       0,     0,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,     0,    24,    25,     0,    26,    27,    28,
      29,    30,    31,    32,     4,     0,     0,     5,     0,     0,
       6,     0,     0,     0,     0,     7,     0,     0,     0,     0,
     431,     0,     9,     0,     0,     0,    33,     0,     0,     0,
      10,     0,     0,    11,     0,     0,    12,     0,     0,     0,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,     0,    24,    25,     0,    26,    27,    28,    29,    30,
      31,    32,     4,     0,     0,     5,     0,     0,     6,     0,
       0,     0,     0,     7,     0,     0,     0,     0,   516,     0,
       9,     0,     0,     0,    33,     0,     0,     0,    10,     0,
       0,    11,     0,     0,    12,     0,     0,     0,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,     0,
      24,   517,     0,    26,    27,    28,    29,    30,    31,    32,
       4,     0,     0,     5,     0,     0,     6,     0,     0,     0,
       0,     7,     0,     0,     0,     0,   535,     0,     9,     0,
       0,     0,    33,     0,     0,     0,    10,     0,     0,    11,
       0,     0,    12,     0,     0,     0,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,     0,    24,    25,
       0,    26,    27,    28,    29,    30,    31,    32,     4,     0,
       0,     5,     0,     0,     6,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   538,     0,     9,     0,     0,     0,
      33,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     7,     0,     0,
       0,     0,   559,     0,     9,     0,     0,     0,    33,     0,
       0,     0,    10,     0,     0,    11,     0,     0,    12,     0,
       0,     0,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,     0,    24,    25,     0,    26,    27,    28,
      29,    30,    31,    32,   315,     0,     0,     5,     0,     0,
     570,     0,     0,     0,     0,     7,     0,     0,     0,     0,
       8,     0,     9,     0,     0,     0,    33,     0,     0,     0,
      10,     0,     0,    11,     0,     0,    12,     0,     0,     0,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,     0,    24,    25,     0,    26,    27,    28,    29,    30,
      31,    32,     4,     0,     0,     5,     0,     0,     6,     0,
       0,     0,     0,     7,     0,     0,     0,     0,   598,     0,
       9,     0,     0,     0,   571,     0,     0,     0,    10,     0,
       0,    11,     0,     0,    12,     0,     0,     0,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,     0,
      24,    25,     0,    26,    27,    28,    29,    30,    31,    32,
       4,     0,     0,     5,     0,     0,     6,     0,     0,     0,
       0,     7,     0,     0,     0,     0,   600,     0,     9,     0,
       0,     0,    33,     0,     0,     0,    10,     0,     0,    11,
       0,     0,    12,     0,     0,     0,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,     0,    24,    25,
       0,    26,    27,    28,    29,    30,    31,    32,     4,     0,
       0,     5,     0,     0,     6,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   601,     0,     9,     0,     0,     0,
      33,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     7,     0,     0,
       0,     0,   604,     0,     9,     0,     0,     0,    33,     0,
       0,     0,    10,     0,     0,    11,     0,     0,    12,     0,
       0,     0,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,     0,    24,    25,     0,    26,    27,    28,
      29,    30,    31,    32,     4,     0,     0,     5,     0,     0,
       6,     0,     0,     0,     0,     7,     0,     0,     0,     0,
     605,     0,     9,     0,     0,     0,    33,     0,     0,     0,
      10,     0,     0,    11,     0,     0,    12,     0,     0,     0,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,     0,    24,    25,     0,    26,    27,    28,    29,    30,
      31,    32,     4,     0,     0,     5,     0,     0,     6,     0,
       0,     0,     0,     7,     0,     0,     0,     0,   618,     0,
       9,     0,     0,     0,    33,     0,     0,     0,    10,     0,
       0,    11,     0,     0,    12,     0,     0,     0,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,     0,
      24,    25,     0,    26,    27,    28,    29,    30,    31,    32,
       4,     0,     0,     5,     0,     0,     6,     0,     0,     0,
       0,     7,     0,     0,     0,     0,   620,     0,     9,     0,
       0,     0,    33,     0,     0,     0,    10,     0,     0,    11,
       0,     0,    12,     0,     0,     0,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,     0,    24,    25,
       0,    26,    27,    28,    29,    30,    31,    32,     4,     0,
       0,     5,     0,     0,     6,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   672,     0,     9,     0,     0,     0,
      33,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     7,     0,     0,
       0,     0,   694,     0,     9,     0,     0,     0,    33,     0,
       0,     0,    10,     0,     0,    11,     0,     0,    12,     0,
       0,     0,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,   315,    24,    25,     5,    26,    27,    28,
      29,    30,    31,    32,   510,     0,     0,     0,     0,     8,
       0,     9,   315,     0,     0,     5,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    33,     0,     8,     0,
       9,    15,    16,     0,    18,    19,    20,     0,     0,     0,
     315,    24,    25,     5,    26,     0,     0,     0,    30,    31,
      15,    16,     0,    18,    19,    20,     8,     0,     9,     0,
      24,    25,     0,    26,     0,     0,     0,    30,    31,     0,
       0,     0,     0,   511,     0,     0,     0,     0,    15,    16,
       0,    18,    19,    20,     0,     0,     0,     0,    24,    25,
       0,    26,   316,     0,     0,    30,    31,     0,     0,     0,
       0,    66,    67,    68,     0,    69,    70,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     8,     0,
     658,    71,    72,    73,    74,     0,     0,    75,     0,    76,
      77,     0,    97,     0,     0,    66,    67,    68,     0,    69,
      70,     0,     0,     0,     0,     0,     0,     0,     0,    78,
       0,    25,     8,    79,     0,    71,    72,    73,    74,     0,
       0,    75,     0,    76,    77,     0,     0,     0,     0,     0,
       0,    80,     0,   138,    81,     0,    82,     0,    83,     0,
       0,     0,     0,    78,     0,    25,     0,    79,     0,     0,
       0,     0,     0,     0,    66,    67,    68,     0,    69,    70,
       0,     0,     0,     0,     0,    80,     0,     0,    81,     0,
      82,     8,    83,     0,    71,    72,    73,    74,     0,     0,
       0,     0,    76,    77,     0,     0,     0,   184,   185,   186,
     187,   188,   189,   190,   191,   192,   193,   194,   195,     0,
       0,     0,    78,     0,    25,   145,    79,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    80,     0,     0,    81,     0,    82,
       0,    83,   291,   292,     0,     0,     0,   241,   242,     0,
       0,     0,     0,   243,     0,   244,   245,     0,   246,     0,
     247,   248,   249,   250,   251,   252,   253,   254,   255,   256,
     257,   258,   259,   260,   261,   262,   263,   264,   265,   266,
     267,   268,   269,   270,   271,   272,   273,   274,   275,   276,
     277,   278,   279,   280,   281,   282,   283,   284,   285,   286,
     291,   292,     0,     0,     0,   241,   242,     0,     0,     0,
       0,   243,     0,   244,   245,   293,   246,     0,   247,   248,
     249,   250,   251,   252,   253,   254,   255,   256,   257,   258,
     259,   260,   261,   262,   263,   264,   265,   266,   267,   268,
     269,   270,   271,   272,   273,   274,   275,   276,   277,   278,
     279,   280,   281,   282,   283,   284,   285,   286,   239,     0,
       0,   240,     0,   241,   242,     0,     0,     0,     0,   243,
       0,   244,   245,   649,   246,     0,   247,   248,   249,   250,
     251,   252,   253,   254,   255,   256,   257,   258,   259,   260,
     261,   262,   263,   264,   265,   266,   267,   268,   269,   270,
     271,   272,   273,   274,   275,   276,   277,   278,   279,   280,
     281,   282,   283,   284,   285,   286,   312,     0,     0,   313,
       0,   241,   242,     0,     0,     0,     0,   243,     0,   244,
     245,     0,   246,     0,   247,   248,   249,   250,   251,   252,
     253,   254,   255,   256,   257,   258,   259,   260,   261,   262,
     263,   264,   265,   266,   267,   268,   269,   270,   271,   272,
     273,   274,   275,   276,   277,   278,   279,   280,   281,   282,
     283,   284,   285,   286,   339,     0,     0,   340,     0,   241,
     242,     0,     0,     0,     0,   243,     0,   244,   245,     0,
     246,     0,   247,   248,   249,   250,   251,   252,   253,   254,
     255,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286
};

static const yytype_int16 yycheck[] =
{
       2,   442,   181,   450,   177,   197,   453,    75,   527,   113,
       4,    65,    83,     4,   181,   213,   522,    19,    20,    21,
     181,    38,   527,    70,     7,   527,     9,   527,   527,   227,
      13,    14,   230,    80,   527,    18,   181,   527,   527,    22,
     527,   146,   527,    26,   181,   527,     4,   527,   181,   568,
     181,     4,   181,   527,   527,    30,   181,     4,   564,   565,
     181,     0,    53,   568,     4,    82,   568,    20,   568,   568,
     181,     4,    76,    20,   179,   568,    46,     4,   568,   568,
      20,   568,    76,   568,    54,     4,   568,   102,   568,     4,
       5,     4,   525,    20,   568,   568,   201,    55,    81,    52,
       4,    20,     4,    78,     4,    52,    39,    20,    84,   628,
     214,   113,    87,    53,   168,   183,    11,    12,    13,    14,
      20,   627,     4,   628,   178,    52,   628,     4,   628,   628,
      82,    84,    84,    52,    34,   628,   569,    84,   628,   628,
      53,   628,   334,   628,    84,    22,   628,    35,   628,    53,
       4,   198,   199,    53,   628,   628,   662,    84,     4,    38,
     143,   144,    76,    76,   147,    84,    20,    62,    62,    46,
      62,     4,   619,    87,     7,    78,    76,    54,    51,   181,
      34,     4,     8,    62,    87,    11,    12,    13,    14,    83,
       4,    83,    82,   176,    84,   210,    84,    20,     4,    53,
       4,    77,   217,    82,   219,     9,    82,   209,     4,    56,
      77,     7,   214,    77,   216,    82,   231,   200,    82,    15,
     203,   204,   205,   206,    20,    84,    22,    79,   669,    77,
      53,   674,    84,   676,    82,    16,    62,   308,    19,   310,
     681,    81,     4,     5,    84,    83,    42,    43,   302,    45,
      46,    47,    78,    84,    80,    84,    52,    53,    76,    55,
      84,    87,    84,    59,    60,    74,    75,    72,    73,     4,
       5,   450,   194,   195,   453,     4,    84,     3,    76,     5,
       4,    84,    84,   450,    84,    84,   453,    84,    84,   450,
      84,    84,   453,    76,    17,    28,    27,    23,    24,    25,
      26,   316,     3,    55,    76,   450,   289,   290,   453,     4,
      84,    84,    76,   450,   368,   298,   453,   450,   389,   450,
     453,   450,   453,   394,   453,   450,   380,   381,   453,   450,
      76,    76,   453,    87,    84,     4,    33,    76,     4,   450,
      84,    77,   453,    77,   398,   399,    72,    73,   527,   351,
      82,   405,   406,     4,    76,     4,    77,    77,    16,   361,
     527,   363,     4,     4,    76,   348,   527,   350,     4,   352,
       5,   373,    62,    63,    64,    65,    66,    67,    68,    69,
      70,    71,   527,     4,     6,     5,    33,    79,    76,   568,
     527,    76,    76,   408,   527,    84,   527,    77,   527,   401,
      82,   568,   527,   386,   387,    80,   527,   568,    82,   424,
     608,    84,    77,    76,   429,    76,   527,    76,    34,    79,
      76,    76,    84,   568,    77,    38,   409,   410,     4,    62,
      82,   568,    77,   416,   607,   568,    84,   568,    77,   568,
     619,    84,   457,   568,     9,    77,   461,   568,   450,   628,
      77,   453,   619,    81,   437,     4,   458,   568,   619,    84,
      77,   628,   445,   478,    18,   480,   481,   628,    38,    84,
      84,    40,   487,    84,   619,   490,    17,    84,    21,   462,
     482,    84,   619,   628,   486,     4,   619,    84,   619,    84,
     619,   628,   507,    48,   619,   628,   511,   628,   619,   628,
      50,    84,    77,   628,     4,   520,   521,   628,   619,    77,
      58,    84,    15,   515,    77,   517,    77,   628,    77,    77,
     522,    77,    84,   506,    77,   527,    17,   510,     3,   531,
       3,   533,   534,    84,    62,     4,   519,   539,    84,   541,
      84,     4,    84,    76,    84,    84,    83,    33,    84,    84,
      84,   553,   567,    84,    84,   557,    48,    56,    76,   542,
      76,    84,   564,   565,    84,     4,   568,   550,   551,    17,
      17,    30,     3,     4,     5,    84,     7,     8,    84,    84,
      84,    48,     3,     4,     5,     5,     7,     8,    84,    20,
      84,    84,    23,    24,    25,    26,    84,   612,    29,    20,
      31,    32,    23,    24,    25,    26,    84,    84,    29,   624,
      31,    32,    84,    84,    84,    44,    84,   619,    84,    84,
      51,    44,    53,    84,    55,   627,   628,    84,    84,    57,
      51,    56,    53,    77,    55,    84,    15,    15,    84,    84,
      77,   656,    73,   658,    84,    76,    84,    78,    84,    80,
      84,   653,    73,    52,    84,    76,    87,    78,    79,    80,
     662,    83,     4,    84,     3,     4,     5,   682,     7,    84,
      84,    84,   674,    84,   676,     3,     4,     5,    84,     7,
       8,    20,    84,    84,    23,    24,    25,    26,   690,     4,
     673,     4,    20,    84,    84,    23,    24,    25,    26,   377,
     127,    29,    90,    31,    32,   148,   216,    76,   677,   196,
     136,   301,    51,   532,    53,   443,    55,   549,   182,    -1,
      -1,    -1,    -1,    51,    -1,    53,    -1,    55,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    76,    -1,    78,
      -1,    80,    -1,    -1,    -1,    73,    -1,    -1,    76,    -1,
      78,    -1,    80,     4,     5,    -1,    84,    -1,     9,    10,
      -1,    -1,    -1,    -1,    15,    -1,    17,    18,    -1,    20,
      -1,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    -1,    -1,    -1,    -1,    -1,    -1,    -1,     4,     5,
      -1,    -1,    -1,     9,    10,    -1,    -1,    -1,    -1,    15,
      81,    17,    18,    84,    20,    -1,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    15,    -1,    -1,
      -1,    -1,    20,    -1,    22,    -1,    -1,    -1,    84,    -1,
      -1,    -1,    30,    -1,    -1,    33,    -1,    -1,    36,    37,
      -1,    -1,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    -1,    52,    53,    -1,    55,    56,    57,
      58,    59,    60,    61,     4,    -1,    -1,     7,    -1,    -1,
      10,    -1,    -1,    -1,    -1,    15,    -1,    -1,    18,    -1,
      20,    -1,    22,    -1,    -1,    -1,    84,    -1,    -1,    -1,
      30,    -1,    -1,    33,    -1,    -1,    36,    -1,    -1,    -1,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    -1,    52,    53,    -1,    55,    56,    57,    58,    59,
      60,    61,     4,    -1,    -1,     7,    -1,    -1,    10,    -1,
      -1,    -1,    -1,    15,    -1,    -1,    -1,    -1,    20,    -1,
      22,    -1,    -1,    -1,    84,    -1,    -1,    -1,    30,    -1,
      -1,    33,    -1,    -1,    36,    -1,    -1,    -1,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    -1,
      52,    53,    -1,    55,    56,    57,    58,    59,    60,    61,
       4,    -1,    -1,     7,    -1,    -1,    10,    -1,    -1,    -1,
      -1,    15,    -1,    -1,    -1,    -1,    20,    -1,    22,    -1,
      -1,    -1,    84,    -1,    -1,    -1,    30,    -1,    -1,    33,
      -1,    -1,    36,    -1,    -1,    -1,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    -1,    52,    53,
      -1,    55,    56,    57,    58,    59,    60,    61,     4,    -1,
      -1,     7,    -1,    -1,    10,    -1,    -1,    -1,    -1,    15,
      -1,    -1,    -1,    -1,    20,    -1,    22,    -1,    -1,    -1,
      84,    -1,    -1,    -1,    30,    -1,    -1,    33,    -1,    -1,
      36,    -1,    -1,    -1,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    -1,    52,    53,    -1,    55,
      56,    57,    58,    59,    60,    61,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    15,    -1,    -1,
      -1,    -1,    20,    -1,    22,    -1,    -1,    -1,    84,    -1,
      -1,    -1,    30,    -1,    -1,    33,    -1,    -1,    36,    -1,
      -1,    -1,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    -1,    52,    53,    -1,    55,    56,    57,
      58,    59,    60,    61,     4,    -1,    -1,     7,    -1,    -1,
      10,    -1,    -1,    -1,    -1,    15,    -1,    -1,    -1,    -1,
      20,    -1,    22,    -1,    -1,    -1,    84,    -1,    -1,    -1,
      30,    -1,    -1,    33,    -1,    -1,    36,    -1,    -1,    -1,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    -1,    52,    53,    -1,    55,    56,    57,    58,    59,
      60,    61,     4,    -1,    -1,     7,    -1,    -1,    10,    -1,
      -1,    -1,    -1,    15,    -1,    -1,    -1,    -1,    20,    -1,
      22,    -1,    -1,    -1,    84,    -1,    -1,    -1,    30,    -1,
      -1,    33,    -1,    -1,    36,    -1,    -1,    -1,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    -1,
      52,    53,    -1,    55,    56,    57,    58,    59,    60,    61,
       4,    -1,    -1,     7,    -1,    -1,    10,    -1,    -1,    -1,
      -1,    15,    -1,    -1,    -1,    -1,    20,    -1,    22,    -1,
      -1,    -1,    84,    -1,    -1,    -1,    30,    -1,    -1,    33,
      -1,    -1,    36,    -1,    -1,    -1,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    -1,    52,    53,
      -1,    55,    56,    57,    58,    59,    60,    61,     4,    -1,
      -1,     7,    -1,    -1,    10,    -1,    -1,    -1,    -1,    15,
      -1,    -1,    -1,    -1,    20,    -1,    22,    -1,    -1,    -1,
      84,    -1,    -1,    -1,    30,    -1,    -1,    33,    -1,    -1,
      36,    -1,    -1,    -1,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    -1,    52,    53,    -1,    55,
      56,    57,    58,    59,    60,    61,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    15,    -1,    -1,
      -1,    -1,    20,    -1,    22,    -1,    -1,    -1,    84,    -1,
      -1,    -1,    30,    -1,    -1,    33,    -1,    -1,    36,    -1,
      -1,    -1,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    -1,    52,    53,    -1,    55,    56,    57,
      58,    59,    60,    61,     4,    -1,    -1,     7,    -1,    -1,
      10,    -1,    -1,    -1,    -1,    15,    -1,    -1,    -1,    -1,
      20,    -1,    22,    -1,    -1,    -1,    84,    -1,    -1,    -1,
      30,    -1,    -1,    33,    -1,    -1,    36,    -1,    -1,    -1,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    -1,    52,    53,    -1,    55,    56,    57,    58,    59,
      60,    61,     4,    -1,    -1,     7,    -1,    -1,    10,    -1,
      -1,    -1,    -1,    15,    -1,    -1,    -1,    -1,    20,    -1,
      22,    -1,    -1,    -1,    84,    -1,    -1,    -1,    30,    -1,
      -1,    33,    -1,    -1,    36,    -1,    -1,    -1,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    -1,
      52,    53,    -1,    55,    56,    57,    58,    59,    60,    61,
       4,    -1,    -1,     7,    -1,    -1,    10,    -1,    -1,    -1,
      -1,    15,    -1,    -1,    -1,    -1,    20,    -1,    22,    -1,
      -1,    -1,    84,    -1,    -1,    -1,    30,    -1,    -1,    33,
      -1,    -1,    36,    -1,    -1,    -1,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    -1,    52,    53,
      -1,    55,    56,    57,    58,    59,    60,    61,     4,    -1,
      -1,     7,    -1,    -1,    10,    -1,    -1,    -1,    -1,    15,
      -1,    -1,    -1,    -1,    20,    -1,    22,    -1,    -1,    -1,
      84,    -1,    -1,    -1,    30,    -1,    -1,    33,    -1,    -1,
      36,    -1,    -1,    -1,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    -1,    52,    53,    -1,    55,
      56,    57,    58,    59,    60,    61,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    15,    -1,    -1,
      -1,    -1,    20,    -1,    22,    -1,    -1,    -1,    84,    -1,
      -1,    -1,    30,    -1,    -1,    33,    -1,    -1,    36,    -1,
      -1,    -1,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    -1,    52,    53,    -1,    55,    56,    57,
      58,    59,    60,    61,     4,    -1,    -1,     7,    -1,    -1,
      10,    -1,    -1,    -1,    -1,    15,    -1,    -1,    -1,    -1,
      20,    -1,    22,    -1,    -1,    -1,    84,    -1,    -1,    -1,
      30,    -1,    -1,    33,    -1,    -1,    36,    -1,    -1,    -1,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    -1,    52,    53,    -1,    55,    56,    57,    58,    59,
      60,    61,     4,    -1,    -1,     7,    -1,    -1,    10,    -1,
      -1,    -1,    -1,    15,    -1,    -1,    -1,    -1,    20,    -1,
      22,    -1,    -1,    -1,    84,    -1,    -1,    -1,    30,    -1,
      -1,    33,    -1,    -1,    36,    -1,    -1,    -1,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    -1,
      52,    53,    -1,    55,    56,    57,    58,    59,    60,    61,
       4,    -1,    -1,     7,    -1,    -1,    10,    -1,    -1,    -1,
      -1,    15,    -1,    -1,    -1,    -1,    20,    -1,    22,    -1,
      -1,    -1,    84,    -1,    -1,    -1,    30,    -1,    -1,    33,
      -1,    -1,    36,    -1,    -1,    -1,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    -1,    52,    53,
      -1,    55,    56,    57,    58,    59,    60,    61,     4,    -1,
      -1,     7,    -1,    -1,    10,    -1,    -1,    -1,    -1,    15,
      -1,    -1,    -1,    -1,    20,    -1,    22,    -1,    -1,    -1,
      84,    -1,    -1,    -1,    30,    -1,    -1,    33,    -1,    -1,
      36,    -1,    -1,    -1,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    -1,    52,    53,    -1,    55,
      56,    57,    58,    59,    60,    61,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    15,    -1,    -1,
      -1,    -1,    20,    -1,    22,    -1,    -1,    -1,    84,    -1,
      -1,    -1,    30,    -1,    -1,    33,    -1,    -1,    36,    -1,
      -1,    -1,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,     4,    52,    53,     7,    55,    56,    57,
      58,    59,    60,    61,    15,    -1,    -1,    -1,    -1,    20,
      -1,    22,     4,    -1,    -1,     7,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    84,    -1,    20,    -1,
      22,    42,    43,    -1,    45,    46,    47,    -1,    -1,    -1,
       4,    52,    53,     7,    55,    -1,    -1,    -1,    59,    60,
      42,    43,    -1,    45,    46,    47,    20,    -1,    22,    -1,
      52,    53,    -1,    55,    -1,    -1,    -1,    59,    60,    -1,
      -1,    -1,    -1,    84,    -1,    -1,    -1,    -1,    42,    43,
      -1,    45,    46,    47,    -1,    -1,    -1,    -1,    52,    53,
      -1,    55,    84,    -1,    -1,    59,    60,    -1,    -1,    -1,
      -1,     3,     4,     5,    -1,     7,     8,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    20,    -1,
      84,    23,    24,    25,    26,    -1,    -1,    29,    -1,    31,
      32,    -1,    34,    -1,    -1,     3,     4,     5,    -1,     7,
       8,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    51,
      -1,    53,    20,    55,    -1,    23,    24,    25,    26,    -1,
      -1,    29,    -1,    31,    32,    -1,    -1,    -1,    -1,    -1,
      -1,    73,    -1,     8,    76,    -1,    78,    -1,    80,    -1,
      -1,    -1,    -1,    51,    -1,    53,    -1,    55,    -1,    -1,
      -1,    -1,    -1,    -1,     3,     4,     5,    -1,     7,     8,
      -1,    -1,    -1,    -1,    -1,    73,    -1,    -1,    76,    -1,
      78,    20,    80,    -1,    23,    24,    25,    26,    -1,    -1,
      -1,    -1,    31,    32,    -1,    -1,    -1,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    -1,
      -1,    -1,    51,    -1,    53,    80,    55,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    73,    -1,    -1,    76,    -1,    78,
      -1,    80,     4,     5,    -1,    -1,    -1,     9,    10,    -1,
      -1,    -1,    -1,    15,    -1,    17,    18,    -1,    20,    -1,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
       4,     5,    -1,    -1,    -1,     9,    10,    -1,    -1,    -1,
      -1,    15,    -1,    17,    18,    77,    20,    -1,    22,    23,
      24,    25,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,     4,    -1,
      -1,     7,    -1,     9,    10,    -1,    -1,    -1,    -1,    15,
      -1,    17,    18,    77,    20,    -1,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,     4,    -1,    -1,     7,
      -1,     9,    10,    -1,    -1,    -1,    -1,    15,    -1,    17,
      18,    -1,    20,    -1,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,     4,    -1,    -1,     7,    -1,     9,
      10,    -1,    -1,    -1,    -1,    15,    -1,    17,    18,    -1,
      20,    -1,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    89,    90,     0,     4,     7,    10,    15,    20,    22,
      30,    33,    36,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    52,    53,    55,    56,    57,    58,
      59,    60,    61,    84,    91,    92,    94,    95,   100,   101,
     102,   104,   105,   106,   107,   112,   113,   114,   115,   116,
     119,   120,   122,   127,   128,   129,   130,   131,   132,   133,
     134,   135,   136,     4,    76,    76,     3,     4,     5,     7,
       8,    23,    24,    25,    26,    29,    31,    32,    51,    55,
      73,    76,    78,    80,    95,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   150,   151,   154,    34,   140,     4,
       4,    39,    84,   140,   140,     4,     4,     4,     7,   140,
      95,    95,     4,    76,    95,   121,   126,   140,    51,     4,
      55,    87,   140,     4,    20,    34,    53,    98,    99,   117,
       4,     4,     4,     5,     4,     5,    56,    84,     8,    11,
      12,    13,    14,    62,    78,    80,    87,    93,    96,    83,
      84,    84,    84,    84,    84,    84,    84,    84,    84,    84,
      84,    84,    76,   140,   155,   156,   155,     4,    76,   147,
     143,   148,     4,    76,   147,   140,   165,   165,    76,    87,
     152,    17,    28,    27,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    96,   149,    74,    75,
      78,    87,     3,    55,    76,    38,    62,    82,     4,    90,
      84,    84,    76,    76,    76,   121,    82,    84,    87,    84,
       4,    22,    46,    54,    46,    54,     4,    76,    99,    33,
      76,    84,     4,     9,     4,   117,   140,   140,    97,     4,
       7,     9,    10,    15,    17,    18,    20,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,   162,   140,    62,
      93,     4,     5,    77,   161,   162,   163,    77,    82,    77,
     155,    30,    76,    77,    77,    79,    84,   140,   157,    81,
     163,   155,     4,     7,   162,     4,    84,    92,    95,   100,
     101,   116,   128,   129,   130,   132,   133,   134,   135,   139,
     142,   143,   146,   146,   149,   145,   147,   147,   140,     4,
       7,   162,     4,   140,   140,   140,   140,     4,    38,    82,
      37,    90,    16,   108,     4,   158,   160,   158,   121,    77,
     126,    90,     4,    90,     4,    53,     4,    53,    76,   158,
       4,   118,   158,    90,     5,     4,     5,    33,    79,     6,
      76,    76,   140,   140,    76,    84,    62,    83,    77,    82,
     140,    77,   151,   155,    82,   165,   165,    77,    76,    76,
     153,    90,    84,   145,    79,    76,    76,    77,    84,    34,
      38,   140,     4,   140,    20,   140,    16,    19,   109,    62,
      77,    82,    77,    77,    84,    20,    20,   155,    77,    84,
      77,    20,     9,   118,    81,   155,   155,     4,   164,   123,
     140,   140,    84,   165,    77,   165,    79,    81,   155,   155,
      18,    20,   137,    18,   138,   155,   155,    84,    90,   140,
     140,    84,    38,    84,    40,    17,   140,    84,    21,     3,
       5,    23,    24,    25,    26,    72,    73,   159,    84,     4,
      84,    84,    90,    48,    50,    77,    90,    84,    58,     4,
      84,    77,    77,   140,    77,     4,    20,    52,    84,   124,
     123,     4,   161,   140,    77,    77,    15,    84,   139,    15,
      15,    84,   139,    77,    77,    90,    20,    53,   103,    35,
      84,    84,    90,   140,    84,    84,    17,   110,    84,     3,
       3,    90,    62,    90,    90,    20,    84,    84,    20,    90,
      84,    90,    83,     4,     5,   125,     4,     4,    20,    76,
      62,    83,   140,    90,    84,    84,   140,    90,    84,    20,
      33,    84,    95,   140,    90,    90,   103,    84,   110,    84,
      10,    84,    92,   100,   101,   102,   104,   105,   106,   107,
     111,   112,   113,   114,   115,   116,   119,   120,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,    20,   159,
      20,    20,    48,    56,    20,    20,   140,    76,    76,    82,
      84,    84,    84,     4,   164,   140,   140,    17,    20,    17,
      20,    30,    84,    84,    84,   103,   103,    90,   110,    84,
      84,    84,    84,    84,    84,    84,    84,    84,    84,    84,
      84,    84,    44,    44,    48,    84,    84,    57,    56,    77,
     163,   158,     5,    90,    84,    77,    84,    15,    84,   139,
      15,    84,    90,   103,    84,    84,    84,    84,    84,    84,
      77,    77,    20,    83,    90,    84,    90,    84,    84,   103,
     123,    84,    84,    52,   140,   137,   137,   138,    20,   123,
      90,    84,     4,    20,    20,    84,     4,     4,    84,    84
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    88,    89,    90,    90,    90,    91,    91,    91,    91,
      91,    91,    91,    91,    91,    91,    91,    91,    91,    91,
      91,    91,    91,    91,    91,    91,    91,    91,    91,    91,
      91,    91,    91,    92,    92,    92,    92,    93,    93,    93,
      93,    94,    94,    94,    95,    95,    95,    97,    96,    96,
      98,    98,    99,    99,    99,    99,   100,   100,   101,   101,
     101,   101,   101,   102,   103,   103,   103,   104,   104,   104,
     104,   104,   104,   105,   106,   107,   108,   108,   109,   109,
     110,   110,   110,   111,   111,   111,   111,   111,   111,   111,
     111,   111,   111,   111,   111,   111,   111,   111,   111,   111,
     111,   111,   111,   111,   111,   111,   111,   111,   111,   112,
     112,   113,   113,   114,   115,   116,   116,   116,   116,   116,
     116,   116,   116,   117,   117,   118,   119,   119,   119,   120,
     121,   121,   122,   122,   123,   123,   123,   124,   124,   124,
     124,   124,   125,   125,   126,   126,   127,   128,   128,   128,
     128,   128,   128,   128,   128,   129,   130,   130,   131,   132,
     133,   134,   134,   135,   135,   136,   136,   137,   137,   137,
     137,   138,   138,   138,   138,   138,   139,   139,   139,   139,
     139,   139,   139,   139,   139,   139,   139,   140,   141,   141,
     142,   142,   143,   143,   144,   144,   144,   145,   145,   145,
     146,   146,   146,   147,   147,   147,   147,   147,   147,   148,
     148,   148,   148,   148,   149,   149,   149,   149,   149,   149,
     149,   149,   149,   149,   150,   150,   150,   150,   150,   150,
     150,   150,   150,   150,   150,   150,   150,   150,   150,   151,
     151,   152,   152,   152,   152,   153,   153,   154,   154,   155,
     155,   156,   156,   157,   157,   158,   158,   159,   159,   159,
     159,   159,   159,   159,   159,   160,   160,   160,   160,   161,
     161,   162,   162,   162,   162,   162,   162,   162,   162,   162,
     162,   162,   162,   162,   162,   162,   162,   162,   162,   162,
     162,   162,   162,   162,   162,   162,   162,   162,   162,   162,
     162,   162,   162,   162,   162,   162,   162,   162,   162,   162,
     162,   162,   162,   162,   162,   162,   162,   162,   163,   163,
     163,   163,   163,   163,   164,   164,   165,   165
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     2,     1,
       1,     2,     1,     2,     2,     2,     2,     2,     2,     2,
       2,     1,     1,     3,     3,     4,     4,     1,     1,     1,
       1,     1,     4,     3,     1,     1,     1,     0,     4,     1,
       1,     2,     1,     1,     1,     1,     2,     4,     4,     4,
       6,     6,     6,    10,     3,     2,     3,     7,     8,     9,
      10,     9,    11,     6,     7,     7,     5,     6,     0,     3,
       0,     2,     2,     2,     2,     2,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     1,     2,     1,     2,
       2,     2,     2,     2,     2,     2,     2,     1,     1,    10,
      10,     9,    10,    10,     7,     2,     2,     2,     2,     4,
       4,     4,     6,     1,     4,     1,     9,     7,    10,     2,
       1,     3,    10,     9,     0,     2,     2,     3,    10,    10,
       9,     7,     1,     3,     1,     3,     7,     4,     4,     3,
       4,     4,     3,     3,     3,     2,     1,     2,     2,     2,
       2,     1,     2,     1,     2,     6,     6,     3,     3,     6,
       7,     0,     3,     6,     7,     7,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     3,
       1,     3,     1,     2,     1,     3,     4,     1,     3,     3,
       1,     3,     3,     1,     2,     2,     2,     4,     5,     1,
       4,     3,     6,     6,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     3,     1,     1,     2,     4,
       1,     1,     1,     1,     1,     3,     3,     5,     1,     3,
       5,     0,     3,     3,     5,     0,     3,     2,     3,     0,
       1,     1,     3,     1,     4,     0,     1,     1,     2,     2,
       1,     1,     1,     1,     1,     1,     3,     3,     5,     1,
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
#line 538 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2621 "src/parser.tab.c"
        break;

    case YYSYMBOL_STRING: /* STRING  */
#line 538 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2627 "src/parser.tab.c"
        break;

    case YYSYMBOL_LENS_CONTENT: /* LENS_CONTENT  */
#line 538 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2633 "src/parser.tab.c"
        break;

    case YYSYMBOL_QUALIFIED_IDENT: /* QUALIFIED_IDENT  */
#line 538 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2639 "src/parser.tab.c"
        break;

    case YYSYMBOL_MODIFIER_PREFIX: /* MODIFIER_PREFIX  */
#line 538 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2645 "src/parser.tab.c"
        break;

    case YYSYMBOL_program: /* program  */
#line 561 "src/parser.y"
            { (void) ((*yyvaluep).stmt_list); }
#line 2651 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement_list: /* statement_list  */
#line 504 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2657 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement: /* statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2663 "src/parser.tab.c"
        break;

    case YYSYMBOL_assignment: /* assignment  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2669 "src/parser.tab.c"
        break;

    case YYSYMBOL_lvalue: /* lvalue  */
#line 539 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2675 "src/parser.tab.c"
        break;

    case YYSYMBOL_variable_name: /* variable_name  */
#line 538 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2681 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_lens: /* comparison_lens  */
#line 546 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 2687 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_name: /* modifier_name  */
#line 538 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2693 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_word: /* modifier_word  */
#line 538 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2699 "src/parser.tab.c"
        break;

    case YYSYMBOL_print_statement: /* print_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2705 "src/parser.tab.c"
        break;

    case YYSYMBOL_call_statement: /* call_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2711 "src/parser.tab.c"
        break;

    case YYSYMBOL_with_lock_statement: /* with_lock_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2717 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_end: /* for_end  */
#line 538 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2723 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_each_statement: /* for_each_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2729 "src/parser.tab.c"
        break;

    case YYSYMBOL_do_loop_statement: /* do_loop_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2735 "src/parser.tab.c"
        break;

    case YYSYMBOL_while_statement: /* while_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2741 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement: /* consider_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2747 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_branch_list: /* consider_branch_list  */
#line 544 "src/parser.y"
            { ast_free_consider_branch_list(((*yyvaluep).consider_branch_list)); }
#line 2753 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_else_opt: /* consider_else_opt  */
#line 504 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2759 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement_list: /* consider_statement_list  */
#line 504 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2765 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_body_statement: /* consider_body_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2771 "src/parser.tab.c"
        break;

    case YYSYMBOL_function_statement: /* function_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2777 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_statement: /* modifier_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2783 "src/parser.tab.c"
        break;

    case YYSYMBOL_program_statement: /* program_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2789 "src/parser.tab.c"
        break;

    case YYSYMBOL_library_statement: /* library_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2795 "src/parser.tab.c"
        break;

    case YYSYMBOL_use_statement: /* use_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2801 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_signature: /* modifier_signature  */
#line 547 "src/parser.y"
            { ast_free_modifier_signature(((*yyvaluep).modifier_signature)); }
#line 2807 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_context: /* modifier_context  */
#line 538 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2813 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_statement: /* watch_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2819 "src/parser.tab.c"
        break;

    case YYSYMBOL_unwatch_statement: /* unwatch_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2825 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_list: /* watch_target_list  */
#line 545 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2831 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_statement: /* server_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2837 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item_list: /* server_item_list  */
#line 551 "src/parser.y"
            { ast_free_server_item_list(((*yyvaluep).server_item_list)); }
#line 2843 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item: /* server_item  */
#line 550 "src/parser.y"
            { AstServerItemList one = ast_server_item_list_append(ast_server_item_list_empty(), ((*yyvaluep).server_item)); ast_free_server_item_list(one); }
#line 2849 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_string_list: /* server_string_list  */
#line 545 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2855 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_path: /* watch_target_path  */
#line 538 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2861 "src/parser.tab.c"
        break;

    case YYSYMBOL_without_watchers_statement: /* without_watchers_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2867 "src/parser.tab.c"
        break;

    case YYSYMBOL_on_error_statement: /* on_error_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2873 "src/parser.tab.c"
        break;

    case YYSYMBOL_error_statement: /* error_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2879 "src/parser.tab.c"
        break;

    case YYSYMBOL_return_statement: /* return_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2885 "src/parser.tab.c"
        break;

    case YYSYMBOL_label_statement: /* label_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2891 "src/parser.tab.c"
        break;

    case YYSYMBOL_goto_statement: /* goto_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2897 "src/parser.tab.c"
        break;

    case YYSYMBOL_gosub_statement: /* gosub_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2903 "src/parser.tab.c"
        break;

    case YYSYMBOL_break_statement: /* break_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2909 "src/parser.tab.c"
        break;

    case YYSYMBOL_continue_statement: /* continue_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2915 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_statement: /* if_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2921 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_block_tail: /* if_block_tail  */
#line 504 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2927 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_inline_tail: /* if_inline_tail  */
#line 504 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2933 "src/parser.tab.c"
        break;

    case YYSYMBOL_inline_statement: /* inline_statement  */
#line 540 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2939 "src/parser.tab.c"
        break;

    case YYSYMBOL_expression: /* expression  */
#line 539 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2945 "src/parser.tab.c"
        break;

    case YYSYMBOL_or_expression: /* or_expression  */
#line 539 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2951 "src/parser.tab.c"
        break;

    case YYSYMBOL_and_expression: /* and_expression  */
#line 539 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2957 "src/parser.tab.c"
        break;

    case YYSYMBOL_not_expression: /* not_expression  */
#line 539 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2963 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_expression: /* comparison_expression  */
#line 539 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2969 "src/parser.tab.c"
        break;

    case YYSYMBOL_additive_expression: /* additive_expression  */
#line 539 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2975 "src/parser.tab.c"
        break;

    case YYSYMBOL_multiplicative_expression: /* multiplicative_expression  */
#line 539 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2981 "src/parser.tab.c"
        break;

    case YYSYMBOL_unary_expression: /* unary_expression  */
#line 539 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2987 "src/parser.tab.c"
        break;

    case YYSYMBOL_postfix_expression: /* postfix_expression  */
#line 539 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2993 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_operator: /* comparison_operator  */
#line 538 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2999 "src/parser.tab.c"
        break;

    case YYSYMBOL_primary: /* primary  */
#line 539 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3005 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_literal: /* record_literal  */
#line 539 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3011 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_suffix: /* ident_suffix  */
#line 548 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3017 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_dot_suffix: /* ident_dot_suffix  */
#line 548 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3023 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list_opt: /* argument_list_opt  */
#line 542 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3029 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list: /* argument_list  */
#line 542 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3035 "src/parser.tab.c"
        break;

    case YYSYMBOL_array_argument_list: /* array_argument_list  */
#line 542 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3041 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list_opt: /* parameter_list_opt  */
#line 545 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3047 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_default: /* parameter_default  */
#line 539 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3053 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list: /* parameter_list  */
#line 545 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3059 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_name: /* field_name  */
#line 538 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3065 "src/parser.tab.c"
        break;

    case YYSYMBOL_dot_field_name: /* dot_field_name  */
#line 538 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3071 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_field_list: /* record_field_list  */
#line 543 "src/parser.y"
            { ast_free_record_field_list(((*yyvaluep).record_field_list)); }
#line 3077 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_policy: /* field_policy  */
#line 549 "src/parser.y"
            { ast_free_expr(((*yyvaluep).field_policy).reset_expr); }
#line 3083 "src/parser.tab.c"
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
#line 566 "src/parser.y"
                     { ctx->parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3389 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 570 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3395 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 571 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3401 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 572 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3407 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 576 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3413 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 577 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3419 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 578 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3425 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 579 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3431 "src/parser.tab.c"
    break;

  case 10: /* statement: for_each_statement  */
#line 580 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3437 "src/parser.tab.c"
    break;

  case 11: /* statement: while_statement  */
#line 581 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3443 "src/parser.tab.c"
    break;

  case 12: /* statement: do_loop_statement  */
#line 582 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3449 "src/parser.tab.c"
    break;

  case 13: /* statement: consider_statement  */
#line 583 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3455 "src/parser.tab.c"
    break;

  case 14: /* statement: function_statement  */
#line 584 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3461 "src/parser.tab.c"
    break;

  case 15: /* statement: modifier_statement  */
#line 585 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3467 "src/parser.tab.c"
    break;

  case 16: /* statement: program_statement  */
#line 586 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3473 "src/parser.tab.c"
    break;

  case 17: /* statement: library_statement  */
#line 587 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3479 "src/parser.tab.c"
    break;

  case 18: /* statement: use_statement NEWLINE  */
#line 588 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3485 "src/parser.tab.c"
    break;

  case 19: /* statement: watch_statement  */
#line 589 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3491 "src/parser.tab.c"
    break;

  case 20: /* statement: server_statement  */
#line 590 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3497 "src/parser.tab.c"
    break;

  case 21: /* statement: unwatch_statement NEWLINE  */
#line 591 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3503 "src/parser.tab.c"
    break;

  case 22: /* statement: without_watchers_statement  */
#line 592 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3509 "src/parser.tab.c"
    break;

  case 23: /* statement: on_error_statement NEWLINE  */
#line 593 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3515 "src/parser.tab.c"
    break;

  case 24: /* statement: error_statement NEWLINE  */
#line 594 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3521 "src/parser.tab.c"
    break;

  case 25: /* statement: return_statement NEWLINE  */
#line 595 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3527 "src/parser.tab.c"
    break;

  case 26: /* statement: label_statement NEWLINE  */
#line 596 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3533 "src/parser.tab.c"
    break;

  case 27: /* statement: goto_statement NEWLINE  */
#line 597 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3539 "src/parser.tab.c"
    break;

  case 28: /* statement: gosub_statement NEWLINE  */
#line 598 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3545 "src/parser.tab.c"
    break;

  case 29: /* statement: break_statement NEWLINE  */
#line 599 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3551 "src/parser.tab.c"
    break;

  case 30: /* statement: continue_statement NEWLINE  */
#line 600 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3557 "src/parser.tab.c"
    break;

  case 31: /* statement: if_statement  */
#line 601 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3563 "src/parser.tab.c"
    break;

  case 32: /* statement: DIM  */
#line 607 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 3576 "src/parser.tab.c"
    break;

  case 33: /* assignment: lvalue OP_EQ expression  */
#line 618 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 3582 "src/parser.tab.c"
    break;

  case 34: /* assignment: lvalue compound_op expression  */
#line 623 "src/parser.y"
                                    { (yyval.stmt) = ast_assign_op((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr), (yyvsp[-1].op_char)); }
#line 3588 "src/parser.tab.c"
    break;

  case 35: /* assignment: lvalue comparison_lens compound_op expression  */
#line 624 "src/parser.y"
                                                    {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign_op((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr), (yyvsp[-1].op_char));
      }
#line 3602 "src/parser.tab.c"
    break;

  case 36: /* assignment: lvalue comparison_lens OP_EQ expression  */
#line 636 "src/parser.y"
                                              {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 3616 "src/parser.tab.c"
    break;

  case 37: /* compound_op: PLUS_EQ  */
#line 648 "src/parser.y"
               { (yyval.op_char) = '+'; }
#line 3622 "src/parser.tab.c"
    break;

  case 38: /* compound_op: MINUS_EQ  */
#line 649 "src/parser.y"
               { (yyval.op_char) = '-'; }
#line 3628 "src/parser.tab.c"
    break;

  case 39: /* compound_op: STAR_EQ  */
#line 650 "src/parser.y"
               { (yyval.op_char) = '*'; }
#line 3634 "src/parser.tab.c"
    break;

  case 40: /* compound_op: SLASH_EQ  */
#line 651 "src/parser.y"
               { (yyval.op_char) = '/'; }
#line 3640 "src/parser.tab.c"
    break;

  case 41: /* lvalue: variable_name  */
#line 655 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3646 "src/parser.tab.c"
    break;

  case 42: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 656 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3652 "src/parser.tab.c"
    break;

  case 43: /* lvalue: lvalue DOT dot_field_name  */
#line 657 "src/parser.y"
                                             { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3658 "src/parser.tab.c"
    break;

  case 44: /* variable_name: IDENT  */
#line 661 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 3664 "src/parser.tab.c"
    break;

  case 45: /* variable_name: END  */
#line 662 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 3670 "src/parser.tab.c"
    break;

  case 46: /* variable_name: NEXT  */
#line 663 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 3676 "src/parser.tab.c"
    break;

  case 47: /* $@1: %empty  */
#line 674 "src/parser.y"
             { lexer_begin_lens_content(ctx->active_lexer); }
#line 3682 "src/parser.tab.c"
    break;

  case 48: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 674 "src/parser.y"
                                                                                  {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 3690 "src/parser.tab.c"
    break;

  case 49: /* comparison_lens: MODIFIER_PREFIX  */
#line 682 "src/parser.y"
                      { (yyval.modifier) = parse_modifier_use((yyvsp[0].text)); }
#line 3696 "src/parser.tab.c"
    break;

  case 50: /* modifier_name: modifier_word  */
#line 686 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 3702 "src/parser.tab.c"
    break;

  case 51: /* modifier_name: modifier_name modifier_word  */
#line 687 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 3708 "src/parser.tab.c"
    break;

  case 52: /* modifier_word: IDENT  */
#line 691 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 3714 "src/parser.tab.c"
    break;

  case 53: /* modifier_word: TO  */
#line 692 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 3720 "src/parser.tab.c"
    break;

  case 54: /* modifier_word: END  */
#line 693 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 3726 "src/parser.tab.c"
    break;

  case 55: /* modifier_word: NEXT  */
#line 694 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 3732 "src/parser.tab.c"
    break;

  case 56: /* print_statement: PRINT expression  */
#line 698 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 3738 "src/parser.tab.c"
    break;

  case 57: /* print_statement: PRINT TO ERROR_VALUE expression  */
#line 704 "src/parser.y"
                                      { (yyval.stmt) = ast_print_error((yyvsp[0].expr)); }
#line 3744 "src/parser.tab.c"
    break;

  case 58: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 708 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 3750 "src/parser.tab.c"
    break;

  case 59: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 709 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 3761 "src/parser.tab.c"
    break;

  case 60: /* call_statement: lvalue DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 715 "src/parser.y"
                                                       {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 3771 "src/parser.tab.c"
    break;

  case 61: /* call_statement: lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 720 "src/parser.y"
                                                                 {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 3785 "src/parser.tab.c"
    break;

  case 62: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 729 "src/parser.y"
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
#line 3800 "src/parser.tab.c"
    break;

  case 63: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 742 "src/parser.y"
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
#line 3823 "src/parser.tab.c"
    break;

  case 64: /* for_end: END FOR NEWLINE  */
#line 771 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 3829 "src/parser.tab.c"
    break;

  case 65: /* for_end: NEXT NEWLINE  */
#line 772 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 3835 "src/parser.tab.c"
    break;

  case 66: /* for_end: NEXT variable_name NEWLINE  */
#line 773 "src/parser.y"
                                 { (yyval.text) = (yyvsp[-1].text); }
#line 3841 "src/parser.tab.c"
    break;

  case 67: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list for_end  */
#line 777 "src/parser.y"
                                                             {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3850 "src/parser.tab.c"
    break;

  case 68: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list for_end  */
#line 781 "src/parser.y"
                                                                  {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3859 "src/parser.tab.c"
    break;

  case 69: /* for_each_statement: FOR IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 797 "src/parser.y"
                                                                         {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { YYERROR; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3869 "src/parser.tab.c"
    break;

  case 70: /* for_each_statement: FOR EACH IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 802 "src/parser.y"
                                                                              {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { YYERROR; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3879 "src/parser.tab.c"
    break;

  case 71: /* for_each_statement: FOR IDENT OP_EQ expression TO expression NEWLINE statement_list for_end  */
#line 810 "src/parser.y"
                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].expr), NULL, (yyvsp[-1].stmt_list));
      }
#line 3888 "src/parser.tab.c"
    break;

  case 72: /* for_each_statement: FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list for_end  */
#line 814 "src/parser.y"
                                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-9].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-9].text), (yyvsp[-7].expr), (yyvsp[-5].expr), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3897 "src/parser.tab.c"
    break;

  case 73: /* do_loop_statement: DO NEWLINE statement_list UNTIL expression NEWLINE  */
#line 835 "src/parser.y"
                                                         {
        (yyval.stmt) = ast_do_loop((yyvsp[-3].stmt_list), (yyvsp[-1].expr));
      }
#line 3905 "src/parser.tab.c"
    break;

  case 74: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 841 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 3913 "src/parser.tab.c"
    break;

  case 75: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 847 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 3921 "src/parser.tab.c"
    break;

  case 76: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 853 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 3929 "src/parser.tab.c"
    break;

  case 77: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 856 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 3937 "src/parser.tab.c"
    break;

  case 78: /* consider_else_opt: %empty  */
#line 862 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3943 "src/parser.tab.c"
    break;

  case 79: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 863 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3949 "src/parser.tab.c"
    break;

  case 80: /* consider_statement_list: %empty  */
#line 867 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3955 "src/parser.tab.c"
    break;

  case 81: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 868 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3961 "src/parser.tab.c"
    break;

  case 82: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 869 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3967 "src/parser.tab.c"
    break;

  case 83: /* consider_body_statement: assignment NEWLINE  */
#line 873 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3973 "src/parser.tab.c"
    break;

  case 84: /* consider_body_statement: print_statement NEWLINE  */
#line 874 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3979 "src/parser.tab.c"
    break;

  case 85: /* consider_body_statement: call_statement NEWLINE  */
#line 875 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3985 "src/parser.tab.c"
    break;

  case 86: /* consider_body_statement: with_lock_statement  */
#line 876 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3991 "src/parser.tab.c"
    break;

  case 87: /* consider_body_statement: for_each_statement  */
#line 877 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3997 "src/parser.tab.c"
    break;

  case 88: /* consider_body_statement: while_statement  */
#line 878 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4003 "src/parser.tab.c"
    break;

  case 89: /* consider_body_statement: do_loop_statement  */
#line 879 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4009 "src/parser.tab.c"
    break;

  case 90: /* consider_body_statement: consider_statement  */
#line 880 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4015 "src/parser.tab.c"
    break;

  case 91: /* consider_body_statement: function_statement  */
#line 881 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4021 "src/parser.tab.c"
    break;

  case 92: /* consider_body_statement: modifier_statement  */
#line 882 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4027 "src/parser.tab.c"
    break;

  case 93: /* consider_body_statement: program_statement  */
#line 883 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4033 "src/parser.tab.c"
    break;

  case 94: /* consider_body_statement: library_statement  */
#line 884 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4039 "src/parser.tab.c"
    break;

  case 95: /* consider_body_statement: use_statement NEWLINE  */
#line 885 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4045 "src/parser.tab.c"
    break;

  case 96: /* consider_body_statement: watch_statement  */
#line 886 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4051 "src/parser.tab.c"
    break;

  case 97: /* consider_body_statement: unwatch_statement NEWLINE  */
#line 887 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4057 "src/parser.tab.c"
    break;

  case 98: /* consider_body_statement: without_watchers_statement  */
#line 888 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4063 "src/parser.tab.c"
    break;

  case 99: /* consider_body_statement: on_error_statement NEWLINE  */
#line 889 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4069 "src/parser.tab.c"
    break;

  case 100: /* consider_body_statement: error_statement NEWLINE  */
#line 890 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4075 "src/parser.tab.c"
    break;

  case 101: /* consider_body_statement: return_statement NEWLINE  */
#line 891 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4081 "src/parser.tab.c"
    break;

  case 102: /* consider_body_statement: label_statement NEWLINE  */
#line 892 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4087 "src/parser.tab.c"
    break;

  case 103: /* consider_body_statement: goto_statement NEWLINE  */
#line 893 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4093 "src/parser.tab.c"
    break;

  case 104: /* consider_body_statement: gosub_statement NEWLINE  */
#line 894 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4099 "src/parser.tab.c"
    break;

  case 105: /* consider_body_statement: break_statement NEWLINE  */
#line 895 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4105 "src/parser.tab.c"
    break;

  case 106: /* consider_body_statement: continue_statement NEWLINE  */
#line 896 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4111 "src/parser.tab.c"
    break;

  case 107: /* consider_body_statement: if_statement  */
#line 897 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4117 "src/parser.tab.c"
    break;

  case 108: /* consider_body_statement: DIM  */
#line 903 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 4130 "src/parser.tab.c"
    break;

  case 109: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 914 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4138 "src/parser.tab.c"
    break;

  case 110: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 917 "src/parser.y"
                                                                                                            {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4147 "src/parser.tab.c"
    break;

  case 111: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 924 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 4155 "src/parser.tab.c"
    break;

  case 112: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 927 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 4163 "src/parser.tab.c"
    break;

  case 113: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 933 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4171 "src/parser.tab.c"
    break;

  case 114: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 939 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 4179 "src/parser.tab.c"
    break;

  case 115: /* use_statement: USE IDENT  */
#line 945 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4185 "src/parser.tab.c"
    break;

  case 116: /* use_statement: LOAD IDENT  */
#line 946 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4191 "src/parser.tab.c"
    break;

  case 117: /* use_statement: USE STRING  */
#line 947 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4197 "src/parser.tab.c"
    break;

  case 118: /* use_statement: LOAD STRING  */
#line 948 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4203 "src/parser.tab.c"
    break;

  case 119: /* use_statement: LOAD IDENT AS IDENT  */
#line 949 "src/parser.y"
                          { (yyval.stmt) = ast_use((yyvsp[-2].text), NULL, (yyvsp[0].text)); }
#line 4209 "src/parser.tab.c"
    break;

  case 120: /* use_statement: USE IDENT IDENT STRING  */
#line 950 "src/parser.y"
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
#line 4230 "src/parser.tab.c"
    break;

  case 121: /* use_statement: LOAD IDENT IDENT STRING  */
#line 966 "src/parser.y"
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
#line 4251 "src/parser.tab.c"
    break;

  case 122: /* use_statement: LOAD IDENT IDENT STRING AS IDENT  */
#line 982 "src/parser.y"
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
#line 4274 "src/parser.tab.c"
    break;

  case 123: /* modifier_signature: modifier_name  */
#line 1003 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 4280 "src/parser.tab.c"
    break;

  case 124: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 1004 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 4286 "src/parser.tab.c"
    break;

  case 125: /* modifier_context: IDENT  */
#line 1008 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4292 "src/parser.tab.c"
    break;

  case 126: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1012 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4300 "src/parser.tab.c"
    break;

  case 127: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 1015 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 4308 "src/parser.tab.c"
    break;

  case 128: /* watch_statement: WATCH IDENT LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1023 "src/parser.y"
                                                                                           {
        (yyval.stmt) = ast_watch((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4316 "src/parser.tab.c"
    break;

  case 129: /* unwatch_statement: UNWATCH expression  */
#line 1029 "src/parser.y"
                         { (yyval.stmt) = ast_unwatch((yyvsp[0].expr)); }
#line 4322 "src/parser.tab.c"
    break;

  case 130: /* watch_target_list: watch_target_path  */
#line 1033 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4328 "src/parser.tab.c"
    break;

  case 131: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 1034 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4334 "src/parser.tab.c"
    break;

  case 132: /* server_statement: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1053 "src/parser.y"
                                                                                             {
        (yyval.stmt) = ast_server((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4342 "src/parser.tab.c"
    break;

  case 133: /* server_statement: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1056 "src/parser.y"
                                                                           {
        (yyval.stmt) = ast_server((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4350 "src/parser.tab.c"
    break;

  case 134: /* server_item_list: %empty  */
#line 1062 "src/parser.y"
             { (yyval.server_item_list) = ast_server_item_list_empty(); }
#line 4356 "src/parser.tab.c"
    break;

  case 135: /* server_item_list: server_item_list NEWLINE  */
#line 1063 "src/parser.y"
                               { (yyval.server_item_list) = (yyvsp[-1].server_item_list); }
#line 4362 "src/parser.tab.c"
    break;

  case 136: /* server_item_list: server_item_list server_item  */
#line 1064 "src/parser.y"
                                   { (yyval.server_item_list) = ast_server_item_list_append((yyvsp[-1].server_item_list), (yyvsp[0].server_item)); }
#line 4368 "src/parser.tab.c"
    break;

  case 137: /* server_item: IDENT server_string_list NEWLINE  */
#line 1068 "src/parser.y"
                                       {
        (yyval.server_item) = ast_server_directive((yyvsp[-2].text), (yyvsp[-1].name_list), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 4376 "src/parser.tab.c"
    break;

  case 138: /* server_item: IDENT STRING LPAREN parameter_list_opt RPAREN NEWLINE statement_list END IDENT NEWLINE  */
#line 1071 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_handler((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4384 "src/parser.tab.c"
    break;

  case 139: /* server_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1074 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_site((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4392 "src/parser.tab.c"
    break;

  case 140: /* server_item: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1077 "src/parser.y"
                                                                           {
        (yyval.server_item) = ast_server_site((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 4400 "src/parser.tab.c"
    break;

  case 141: /* server_item: ON IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1080 "src/parser.y"
                                                     {
        (yyval.server_item) = ast_server_hook((yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 4408 "src/parser.tab.c"
    break;

  case 142: /* server_string_list: STRING  */
#line 1086 "src/parser.y"
             { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4414 "src/parser.tab.c"
    break;

  case 143: /* server_string_list: server_string_list COMMA STRING  */
#line 1087 "src/parser.y"
                                      { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4420 "src/parser.tab.c"
    break;

  case 144: /* watch_target_path: variable_name  */
#line 1091 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 4426 "src/parser.tab.c"
    break;

  case 145: /* watch_target_path: watch_target_path DOT IDENT  */
#line 1092 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 4432 "src/parser.tab.c"
    break;

  case 146: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 1096 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 4440 "src/parser.tab.c"
    break;

  case 147: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 1102 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 4446 "src/parser.tab.c"
    break;

  case 148: /* on_error_statement: ON ERROR_VALUE GOTO NEXT  */
#line 1103 "src/parser.y"
                               { (yyval.stmt) = ast_on_error_goto_next(); }
#line 4452 "src/parser.tab.c"
    break;

  case 149: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 1104 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 4458 "src/parser.tab.c"
    break;

  case 150: /* on_error_statement: ON IDENT GOTO NEXT  */
#line 1105 "src/parser.y"
                         {
        if (!warn_channel_ok(ctx, (yyvsp[-2].text), (yylsp[-2]).first_line, (yylsp[-2]).first_column)) { YYERROR; }
        free((yyvsp[-2].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_NEXT);
      }
#line 4468 "src/parser.tab.c"
    break;

  case 151: /* on_error_statement: ON IDENT GOTO IDENT  */
#line 1110 "src/parser.y"
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
#line 4486 "src/parser.tab.c"
    break;

  case 152: /* on_error_statement: ON IDENT STOP  */
#line 1123 "src/parser.y"
                    {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_STOP);
      }
#line 4496 "src/parser.tab.c"
    break;

  case 153: /* on_error_statement: ON IDENT PRINT  */
#line 1128 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_PRINT);
      }
#line 4506 "src/parser.tab.c"
    break;

  case 154: /* on_error_statement: ON IDENT IDENT  */
#line 1133 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        int mode = warn_mode_word(ctx, (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column);
        if (mode < 0) { free((yyvsp[-1].text)); free((yyvsp[0].text)); YYERROR; }
        free((yyvsp[-1].text)); free((yyvsp[0].text));
        (yyval.stmt) = ast_on_warning(mode);
      }
#line 4518 "src/parser.tab.c"
    break;

  case 155: /* error_statement: ERROR_VALUE expression  */
#line 1143 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 4524 "src/parser.tab.c"
    break;

  case 156: /* return_statement: RETURN  */
#line 1147 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 4530 "src/parser.tab.c"
    break;

  case 157: /* return_statement: RETURN expression  */
#line 1148 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 4536 "src/parser.tab.c"
    break;

  case 158: /* label_statement: variable_name COLON  */
#line 1152 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 4542 "src/parser.tab.c"
    break;

  case 159: /* goto_statement: GOTO variable_name  */
#line 1159 "src/parser.y"
                         { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 4548 "src/parser.tab.c"
    break;

  case 160: /* gosub_statement: GOSUB variable_name  */
#line 1163 "src/parser.y"
                          { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 4554 "src/parser.tab.c"
    break;

  case 161: /* break_statement: BREAK  */
#line 1172 "src/parser.y"
            { (yyval.stmt) = ast_break(NULL); }
#line 4560 "src/parser.tab.c"
    break;

  case 162: /* break_statement: BREAK IDENT  */
#line 1173 "src/parser.y"
                  { (yyval.stmt) = ast_break((yyvsp[0].text)); }
#line 4566 "src/parser.tab.c"
    break;

  case 163: /* continue_statement: CONTINUE  */
#line 1177 "src/parser.y"
               { (yyval.stmt) = ast_continue(NULL); }
#line 4572 "src/parser.tab.c"
    break;

  case 164: /* continue_statement: CONTINUE IDENT  */
#line 1178 "src/parser.y"
                     { (yyval.stmt) = ast_continue((yyvsp[0].text)); }
#line 4578 "src/parser.tab.c"
    break;

  case 165: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1182 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4587 "src/parser.tab.c"
    break;

  case 166: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1186 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4596 "src/parser.tab.c"
    break;

  case 167: /* if_block_tail: END IF NEWLINE  */
#line 1193 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4604 "src/parser.tab.c"
    break;

  case 168: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 1196 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4612 "src/parser.tab.c"
    break;

  case 169: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1199 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4620 "src/parser.tab.c"
    break;

  case 170: /* if_block_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1208 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 4632 "src/parser.tab.c"
    break;

  case 171: /* if_inline_tail: %empty  */
#line 1218 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4640 "src/parser.tab.c"
    break;

  case 172: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 1221 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4648 "src/parser.tab.c"
    break;

  case 173: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1224 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4656 "src/parser.tab.c"
    break;

  case 174: /* if_inline_tail: ELSE IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1230 "src/parser.y"
                                                                      {
        AstStmt *inner = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 4668 "src/parser.tab.c"
    break;

  case 175: /* if_inline_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1237 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 4680 "src/parser.tab.c"
    break;

  case 176: /* inline_statement: assignment  */
#line 1247 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4686 "src/parser.tab.c"
    break;

  case 177: /* inline_statement: print_statement  */
#line 1248 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4692 "src/parser.tab.c"
    break;

  case 178: /* inline_statement: call_statement  */
#line 1249 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4698 "src/parser.tab.c"
    break;

  case 179: /* inline_statement: use_statement  */
#line 1250 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4704 "src/parser.tab.c"
    break;

  case 180: /* inline_statement: on_error_statement  */
#line 1251 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4710 "src/parser.tab.c"
    break;

  case 181: /* inline_statement: error_statement  */
#line 1252 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4716 "src/parser.tab.c"
    break;

  case 182: /* inline_statement: return_statement  */
#line 1253 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4722 "src/parser.tab.c"
    break;

  case 183: /* inline_statement: goto_statement  */
#line 1254 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4728 "src/parser.tab.c"
    break;

  case 184: /* inline_statement: gosub_statement  */
#line 1255 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4734 "src/parser.tab.c"
    break;

  case 185: /* inline_statement: break_statement  */
#line 1256 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4740 "src/parser.tab.c"
    break;

  case 186: /* inline_statement: continue_statement  */
#line 1257 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4746 "src/parser.tab.c"
    break;

  case 187: /* expression: or_expression  */
#line 1261 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 4752 "src/parser.tab.c"
    break;

  case 188: /* or_expression: and_expression  */
#line 1265 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 4758 "src/parser.tab.c"
    break;

  case 189: /* or_expression: or_expression OR and_expression  */
#line 1266 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4764 "src/parser.tab.c"
    break;

  case 190: /* and_expression: not_expression  */
#line 1270 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 4770 "src/parser.tab.c"
    break;

  case 191: /* and_expression: and_expression AND not_expression  */
#line 1271 "src/parser.y"
                                        { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4776 "src/parser.tab.c"
    break;

  case 192: /* not_expression: comparison_expression  */
#line 1293 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 4782 "src/parser.tab.c"
    break;

  case 193: /* not_expression: NOT not_expression  */
#line 1294 "src/parser.y"
                         { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4788 "src/parser.tab.c"
    break;

  case 194: /* comparison_expression: additive_expression  */
#line 1298 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 4794 "src/parser.tab.c"
    break;

  case 195: /* comparison_expression: additive_expression comparison_operator additive_expression  */
#line 1299 "src/parser.y"
                                                                  { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4800 "src/parser.tab.c"
    break;

  case 196: /* comparison_expression: additive_expression comparison_lens comparison_operator additive_expression  */
#line 1300 "src/parser.y"
                                                                                  {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 4808 "src/parser.tab.c"
    break;

  case 197: /* additive_expression: multiplicative_expression  */
#line 1306 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 4814 "src/parser.tab.c"
    break;

  case 198: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 1307 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4820 "src/parser.tab.c"
    break;

  case 199: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 1308 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4826 "src/parser.tab.c"
    break;

  case 200: /* multiplicative_expression: unary_expression  */
#line 1312 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 4832 "src/parser.tab.c"
    break;

  case 201: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 1313 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4838 "src/parser.tab.c"
    break;

  case 202: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 1314 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4844 "src/parser.tab.c"
    break;

  case 203: /* unary_expression: postfix_expression  */
#line 1318 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 4850 "src/parser.tab.c"
    break;

  case 204: /* unary_expression: MINUS unary_expression  */
#line 1319 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4856 "src/parser.tab.c"
    break;

  case 205: /* unary_expression: MODIFIER_PREFIX unary_expression  */
#line 1326 "src/parser.y"
                                       {
        (yyval.expr) = expr_at(ast_modifier_apply(parse_modifier_use((yyvsp[-1].text)), (yyvsp[0].expr)),
                     (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 4865 "src/parser.tab.c"
    break;

  case 206: /* unary_expression: NEW postfix_expression  */
#line 1330 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4871 "src/parser.tab.c"
    break;

  case 207: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 1331 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 4877 "src/parser.tab.c"
    break;

  case 208: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 1332 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4883 "src/parser.tab.c"
    break;

  case 209: /* postfix_expression: primary  */
#line 1336 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 4889 "src/parser.tab.c"
    break;

  case 210: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 1337 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4895 "src/parser.tab.c"
    break;

  case 211: /* postfix_expression: postfix_expression DOT dot_field_name  */
#line 1338 "src/parser.y"
                                            { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4901 "src/parser.tab.c"
    break;

  case 212: /* postfix_expression: postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1339 "src/parser.y"
                                                                   {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        (yyval.expr) = expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 4911 "src/parser.tab.c"
    break;

  case 213: /* postfix_expression: postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1344 "src/parser.y"
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
#line 4927 "src/parser.tab.c"
    break;

  case 214: /* comparison_operator: OP_EQ  */
#line 1358 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 4933 "src/parser.tab.c"
    break;

  case 215: /* comparison_operator: OP_NE  */
#line 1359 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 4939 "src/parser.tab.c"
    break;

  case 216: /* comparison_operator: OP_GT  */
#line 1360 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 4945 "src/parser.tab.c"
    break;

  case 217: /* comparison_operator: OP_LT  */
#line 1361 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 4951 "src/parser.tab.c"
    break;

  case 218: /* comparison_operator: OP_GE  */
#line 1362 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 4957 "src/parser.tab.c"
    break;

  case 219: /* comparison_operator: OP_LE  */
#line 1363 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 4963 "src/parser.tab.c"
    break;

  case 220: /* comparison_operator: OP_NGT  */
#line 1364 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 4969 "src/parser.tab.c"
    break;

  case 221: /* comparison_operator: OP_NLT  */
#line 1365 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 4975 "src/parser.tab.c"
    break;

  case 222: /* comparison_operator: OP_NGE  */
#line 1366 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 4981 "src/parser.tab.c"
    break;

  case 223: /* comparison_operator: OP_NLE  */
#line 1367 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 4987 "src/parser.tab.c"
    break;

  case 224: /* primary: NUMBER  */
#line 1371 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4993 "src/parser.tab.c"
    break;

  case 225: /* primary: WATCHERS LPAREN RPAREN  */
#line 1372 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_call(copy_const("watchers"), ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4999 "src/parser.tab.c"
    break;

  case 226: /* primary: duration_terms  */
#line 1373 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5005 "src/parser.tab.c"
    break;

  case 227: /* primary: STRING  */
#line 1374 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5011 "src/parser.tab.c"
    break;

  case 228: /* primary: variable_name ident_suffix  */
#line 1375 "src/parser.y"
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
#line 5033 "src/parser.tab.c"
    break;

  case 229: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1392 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 5044 "src/parser.tab.c"
    break;

  case 230: /* primary: ERROR_VALUE  */
#line 1398 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5050 "src/parser.tab.c"
    break;

  case 231: /* primary: TRUE  */
#line 1399 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5056 "src/parser.tab.c"
    break;

  case 232: /* primary: FALSE  */
#line 1400 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5062 "src/parser.tab.c"
    break;

  case 233: /* primary: NOTHING  */
#line 1401 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5068 "src/parser.tab.c"
    break;

  case 234: /* primary: UNKNOWN_VALUE  */
#line 1402 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5074 "src/parser.tab.c"
    break;

  case 235: /* primary: LPAREN expression RPAREN  */
#line 1403 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 5080 "src/parser.tab.c"
    break;

  case 236: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 1404 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5086 "src/parser.tab.c"
    break;

  case 237: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 1405 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5092 "src/parser.tab.c"
    break;

  case 238: /* primary: record_literal  */
#line 1406 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5098 "src/parser.tab.c"
    break;

  case 239: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 1410 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5104 "src/parser.tab.c"
    break;

  case 240: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 1411 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5110 "src/parser.tab.c"
    break;

  case 241: /* ident_suffix: %empty  */
#line 1415 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 5120 "src/parser.tab.c"
    break;

  case 242: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 1420 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5130 "src/parser.tab.c"
    break;

  case 243: /* ident_suffix: DOT dot_field_name ident_dot_suffix  */
#line 1425 "src/parser.y"
                                          {
        /* dot_field_name, not IDENT: a keyword is a legal FIELD name after a
         * dot, because nothing but a name can appear there. */
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 5141 "src/parser.tab.c"
    break;

  case 244: /* ident_suffix: DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1431 "src/parser.y"
                                                          {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        (yyval.ident_suffix).kind = IDENT_SUFFIX_METHOD;
        (yyval.ident_suffix).name = (yyvsp[-3].text);
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5155 "src/parser.tab.c"
    break;

  case 245: /* ident_dot_suffix: %empty  */
#line 1443 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 5165 "src/parser.tab.c"
    break;

  case 246: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 1448 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5175 "src/parser.tab.c"
    break;

  case 247: /* duration_terms: NUMBER IDENT  */
#line 1456 "src/parser.y"
                   {
        AstDuration duration = {0};
        char *bad = NULL;
        (yyval.duration) = duration_add_unit(duration, (yyvsp[-1].number), (yyvsp[0].text), &bad);
        if (bad) {
            duration_unit_error(ctx, bad, (yylsp[0]).first_line, (yylsp[0]).first_column,
                                (yylsp[0]).last_line, (yylsp[0]).last_column);
            YYERROR;
        }
      }
#line 5190 "src/parser.tab.c"
    break;

  case 248: /* duration_terms: duration_terms NUMBER IDENT  */
#line 1466 "src/parser.y"
                                  {
        char *bad = NULL;
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].number), (yyvsp[0].text), &bad);
        if (bad) {
            duration_unit_error(ctx, bad, (yylsp[0]).first_line, (yylsp[0]).first_column,
                                (yylsp[0]).last_line, (yylsp[0]).last_column);
            YYERROR;
        }
      }
#line 5204 "src/parser.tab.c"
    break;

  case 249: /* argument_list_opt: %empty  */
#line 1478 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 5210 "src/parser.tab.c"
    break;

  case 250: /* argument_list_opt: argument_list  */
#line 1479 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 5216 "src/parser.tab.c"
    break;

  case 251: /* argument_list: expression  */
#line 1483 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 5222 "src/parser.tab.c"
    break;

  case 252: /* argument_list: argument_list COMMA expression  */
#line 1484 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 5228 "src/parser.tab.c"
    break;

  case 253: /* array_argument_list: expression  */
#line 1488 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 5234 "src/parser.tab.c"
    break;

  case 254: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 1489 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 5240 "src/parser.tab.c"
    break;

  case 255: /* parameter_list_opt: %empty  */
#line 1493 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 5246 "src/parser.tab.c"
    break;

  case 256: /* parameter_list_opt: parameter_list  */
#line 1494 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 5252 "src/parser.tab.c"
    break;

  case 257: /* parameter_default: NUMBER  */
#line 1508 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5258 "src/parser.tab.c"
    break;

  case 258: /* parameter_default: MINUS NUMBER  */
#line 1509 "src/parser.y"
                   { (yyval.expr) = expr_at(ast_number(-(yyvsp[0].number)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5264 "src/parser.tab.c"
    break;

  case 259: /* parameter_default: PLUS NUMBER  */
#line 1510 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5270 "src/parser.tab.c"
    break;

  case 260: /* parameter_default: STRING  */
#line 1511 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5276 "src/parser.tab.c"
    break;

  case 261: /* parameter_default: TRUE  */
#line 1512 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5282 "src/parser.tab.c"
    break;

  case 262: /* parameter_default: FALSE  */
#line 1513 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5288 "src/parser.tab.c"
    break;

  case 263: /* parameter_default: NOTHING  */
#line 1514 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5294 "src/parser.tab.c"
    break;

  case 264: /* parameter_default: UNKNOWN_VALUE  */
#line 1515 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5300 "src/parser.tab.c"
    break;

  case 265: /* parameter_list: IDENT  */
#line 1519 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5306 "src/parser.tab.c"
    break;

  case 266: /* parameter_list: IDENT OP_EQ parameter_default  */
#line 1520 "src/parser.y"
                                    {
        (yyval.name_list) = ast_name_list_append_default(ast_name_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 5314 "src/parser.tab.c"
    break;

  case 267: /* parameter_list: parameter_list COMMA IDENT  */
#line 1523 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5320 "src/parser.tab.c"
    break;

  case 268: /* parameter_list: parameter_list COMMA IDENT OP_EQ parameter_default  */
#line 1524 "src/parser.y"
                                                         {
        (yyval.name_list) = ast_name_list_append_default((yyvsp[-4].name_list), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 5328 "src/parser.tab.c"
    break;

  case 269: /* field_name: dot_field_name  */
#line 1539 "src/parser.y"
                     { (yyval.text) = (yyvsp[0].text); }
#line 5334 "src/parser.tab.c"
    break;

  case 270: /* field_name: STRING  */
#line 1546 "src/parser.y"
             { (yyval.text) = (yyvsp[0].text); }
#line 5340 "src/parser.tab.c"
    break;

  case 271: /* dot_field_name: IDENT  */
#line 1555 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 5346 "src/parser.tab.c"
    break;

  case 272: /* dot_field_name: AS  */
#line 1556 "src/parser.y"
                     { (yyval.text) = kw_name("as"); }
#line 5352 "src/parser.tab.c"
    break;

  case 273: /* dot_field_name: NEXT  */
#line 1557 "src/parser.y"
                     { (yyval.text) = kw_name("next"); }
#line 5358 "src/parser.tab.c"
    break;

  case 274: /* dot_field_name: STOP  */
#line 1558 "src/parser.y"
                     { (yyval.text) = kw_name("stop"); }
#line 5364 "src/parser.tab.c"
    break;

  case 275: /* dot_field_name: ERROR_VALUE  */
#line 1559 "src/parser.y"
                     { (yyval.text) = kw_name("error"); }
#line 5370 "src/parser.tab.c"
    break;

  case 276: /* dot_field_name: END  */
#line 1560 "src/parser.y"
                     { (yyval.text) = kw_name("end"); }
#line 5376 "src/parser.tab.c"
    break;

  case 277: /* dot_field_name: TO  */
#line 1561 "src/parser.y"
                     { (yyval.text) = kw_name("to"); }
#line 5382 "src/parser.tab.c"
    break;

  case 278: /* dot_field_name: IN  */
#line 1562 "src/parser.y"
                     { (yyval.text) = kw_name("in"); }
#line 5388 "src/parser.tab.c"
    break;

  case 279: /* dot_field_name: ON  */
#line 1563 "src/parser.y"
                     { (yyval.text) = kw_name("on"); }
#line 5394 "src/parser.tab.c"
    break;

  case 280: /* dot_field_name: NEW  */
#line 1564 "src/parser.y"
                     { (yyval.text) = kw_name("new"); }
#line 5400 "src/parser.tab.c"
    break;

  case 281: /* dot_field_name: EACH  */
#line 1565 "src/parser.y"
                     { (yyval.text) = kw_name("each"); }
#line 5406 "src/parser.tab.c"
    break;

  case 282: /* dot_field_name: WITH  */
#line 1566 "src/parser.y"
                     { (yyval.text) = kw_name("with"); }
#line 5412 "src/parser.tab.c"
    break;

  case 283: /* dot_field_name: WITHOUT  */
#line 1567 "src/parser.y"
                     { (yyval.text) = kw_name("without"); }
#line 5418 "src/parser.tab.c"
    break;

  case 284: /* dot_field_name: THEN  */
#line 1568 "src/parser.y"
                     { (yyval.text) = kw_name("then"); }
#line 5424 "src/parser.tab.c"
    break;

  case 285: /* dot_field_name: ELSE  */
#line 1569 "src/parser.y"
                     { (yyval.text) = kw_name("else"); }
#line 5430 "src/parser.tab.c"
    break;

  case 286: /* dot_field_name: FOR  */
#line 1570 "src/parser.y"
                     { (yyval.text) = kw_name("for"); }
#line 5436 "src/parser.tab.c"
    break;

  case 287: /* dot_field_name: IF  */
#line 1571 "src/parser.y"
                     { (yyval.text) = kw_name("if"); }
#line 5442 "src/parser.tab.c"
    break;

  case 288: /* dot_field_name: WHILE  */
#line 1572 "src/parser.y"
                     { (yyval.text) = kw_name("while"); }
#line 5448 "src/parser.tab.c"
    break;

  case 289: /* dot_field_name: DO  */
#line 1573 "src/parser.y"
                     { (yyval.text) = kw_name("do"); }
#line 5454 "src/parser.tab.c"
    break;

  case 290: /* dot_field_name: UNTIL  */
#line 1574 "src/parser.y"
                     { (yyval.text) = kw_name("until"); }
#line 5460 "src/parser.tab.c"
    break;

  case 291: /* dot_field_name: PRINT  */
#line 1575 "src/parser.y"
                     { (yyval.text) = kw_name("print"); }
#line 5466 "src/parser.tab.c"
    break;

  case 292: /* dot_field_name: RETURN  */
#line 1576 "src/parser.y"
                     { (yyval.text) = kw_name("return"); }
#line 5472 "src/parser.tab.c"
    break;

  case 293: /* dot_field_name: LOAD  */
#line 1577 "src/parser.y"
                     { (yyval.text) = kw_name("load"); }
#line 5478 "src/parser.tab.c"
    break;

  case 294: /* dot_field_name: USE  */
#line 1578 "src/parser.y"
                     { (yyval.text) = kw_name("use"); }
#line 5484 "src/parser.tab.c"
    break;

  case 295: /* dot_field_name: NOT  */
#line 1579 "src/parser.y"
                     { (yyval.text) = kw_name("not"); }
#line 5490 "src/parser.tab.c"
    break;

  case 296: /* dot_field_name: AND  */
#line 1580 "src/parser.y"
                     { (yyval.text) = kw_name("and"); }
#line 5496 "src/parser.tab.c"
    break;

  case 297: /* dot_field_name: OR  */
#line 1581 "src/parser.y"
                     { (yyval.text) = kw_name("or"); }
#line 5502 "src/parser.tab.c"
    break;

  case 298: /* dot_field_name: TRUE  */
#line 1582 "src/parser.y"
                     { (yyval.text) = kw_name("true"); }
#line 5508 "src/parser.tab.c"
    break;

  case 299: /* dot_field_name: FALSE  */
#line 1583 "src/parser.y"
                     { (yyval.text) = kw_name("false"); }
#line 5514 "src/parser.tab.c"
    break;

  case 300: /* dot_field_name: NOTHING  */
#line 1584 "src/parser.y"
                     { (yyval.text) = kw_name("nothing"); }
#line 5520 "src/parser.tab.c"
    break;

  case 301: /* dot_field_name: BREAK  */
#line 1585 "src/parser.y"
                     { (yyval.text) = kw_name("break"); }
#line 5526 "src/parser.tab.c"
    break;

  case 302: /* dot_field_name: CONTINUE  */
#line 1586 "src/parser.y"
                     { (yyval.text) = kw_name("continue"); }
#line 5532 "src/parser.tab.c"
    break;

  case 303: /* dot_field_name: GOTO  */
#line 1587 "src/parser.y"
                     { (yyval.text) = kw_name("goto"); }
#line 5538 "src/parser.tab.c"
    break;

  case 304: /* dot_field_name: GOSUB  */
#line 1588 "src/parser.y"
                     { (yyval.text) = kw_name("gosub"); }
#line 5544 "src/parser.tab.c"
    break;

  case 305: /* dot_field_name: SPAWN  */
#line 1589 "src/parser.y"
                     { (yyval.text) = kw_name("spawn"); }
#line 5550 "src/parser.tab.c"
    break;

  case 306: /* dot_field_name: EXPORT  */
#line 1590 "src/parser.y"
                     { (yyval.text) = kw_name("export"); }
#line 5556 "src/parser.tab.c"
    break;

  case 307: /* dot_field_name: LIBRARY  */
#line 1591 "src/parser.y"
                     { (yyval.text) = kw_name("library"); }
#line 5562 "src/parser.tab.c"
    break;

  case 308: /* dot_field_name: FUNCTION  */
#line 1592 "src/parser.y"
                     { (yyval.text) = kw_name("function"); }
#line 5568 "src/parser.tab.c"
    break;

  case 309: /* dot_field_name: MODIFIER  */
#line 1593 "src/parser.y"
                     { (yyval.text) = kw_name("modifier"); }
#line 5574 "src/parser.tab.c"
    break;

  case 310: /* dot_field_name: PROGRAM  */
#line 1594 "src/parser.y"
                     { (yyval.text) = kw_name("program"); }
#line 5580 "src/parser.tab.c"
    break;

  case 311: /* dot_field_name: WATCH  */
#line 1595 "src/parser.y"
                     { (yyval.text) = kw_name("watch"); }
#line 5586 "src/parser.tab.c"
    break;

  case 312: /* dot_field_name: WATCHERS  */
#line 1596 "src/parser.y"
                     { (yyval.text) = kw_name("watchers"); }
#line 5592 "src/parser.tab.c"
    break;

  case 313: /* dot_field_name: CONSIDER  */
#line 1597 "src/parser.y"
                     { (yyval.text) = kw_name("consider"); }
#line 5598 "src/parser.tab.c"
    break;

  case 314: /* dot_field_name: STEP  */
#line 1598 "src/parser.y"
                     { (yyval.text) = kw_name("step"); }
#line 5604 "src/parser.tab.c"
    break;

  case 315: /* dot_field_name: UNWATCH  */
#line 1599 "src/parser.y"
                     { (yyval.text) = kw_name("unwatch"); }
#line 5610 "src/parser.tab.c"
    break;

  case 316: /* dot_field_name: UNKNOWN_VALUE  */
#line 1600 "src/parser.y"
                     { (yyval.text) = kw_name("unknown"); }
#line 5616 "src/parser.tab.c"
    break;

  case 317: /* dot_field_name: DIM  */
#line 1601 "src/parser.y"
                     { (yyval.text) = kw_name("dim"); }
#line 5622 "src/parser.tab.c"
    break;

  case 318: /* record_field_list: field_name OP_EQ expression  */
#line 1605 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5628 "src/parser.tab.c"
    break;

  case 319: /* record_field_list: field_name COLON expression  */
#line 1606 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5634 "src/parser.tab.c"
    break;

  case 320: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1607 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5640 "src/parser.tab.c"
    break;

  case 321: /* record_field_list: record_field_list COMMA optional_newlines field_name OP_EQ expression  */
#line 1608 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5646 "src/parser.tab.c"
    break;

  case 322: /* record_field_list: record_field_list COMMA optional_newlines field_name COLON expression  */
#line 1609 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5652 "src/parser.tab.c"
    break;

  case 323: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1610 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5658 "src/parser.tab.c"
    break;

  case 324: /* field_policy: IDENT  */
#line 1618 "src/parser.y"
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
#line 5690 "src/parser.tab.c"
    break;

  case 325: /* field_policy: IDENT expression  */
#line 1645 "src/parser.y"
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
#line 5711 "src/parser.tab.c"
    break;


#line 5715 "src/parser.tab.c"

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

#line 1668 "src/parser.y"


/* Reentrant parse core: all mutable parser state lives in a stack-allocated
 * gb_parse_ctx, so concurrent parses in one process share nothing. `path` labels
 * diagnostic locations (may be NULL) and `diags` is the sink (NULL => immediate
 * stderr via gb_report_to). This is the entry point gb_parse (frontend.c) uses. */
int parse_source_reentrant_at(const char *source, const char *path,
                              int first_line,
                              gb_diagnostics *diags, AstStmtList *out_program);

int parse_source_reentrant(const char *source, const char *path,
                           gb_diagnostics *diags, AstStmtList *out_program) {
    return parse_source_reentrant_at(source, path, 1, diags, out_program);
}

/* As above, for a buffer that is an excerpt: `first_line` is the line its first
 * character really has (see lexer_init_at). */
int parse_source_reentrant_at(const char *source, const char *path,
                              int first_line,
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
    ctx.tok_type = TOKEN_EOF;
    ctx.tok_prev_type = TOKEN_EOF;
    ctx.tok_word[0] = '\0';
    ctx.tok_prev_word[0] = '\0';
    ctx.tok_after = NULL;

    Lexer lexer;
    lexer_init_at(&lexer, source, first_line);
    ctx.active_lexer = &lexer;

    int result = yyparse(&ctx);
    if (result != 0) {
        /* Empty unless `program` itself reduced, which a syntax error prevents;
         * non-empty on the YYABORT paths, where the root is built and then
         * refused. Freed either way rather than reasoned about per path. */
        ast_free_program(ctx.parsed_program);
        return result;
    }
    /* A diagnostic reported from yylex must fail the parse even when bison
     * ACCEPTED. yylex signals such a token by returning 0 -- end of file -- and
     * bison cannot tell that from a real one, so wherever the grammar allows a
     * program to end (top level, notably) it reduces the truncated prefix and
     * reports success. The file then ran up to the bad token and exited 0.
     * lexer_error_reported is the only evidence that the EOF was synthetic. */
    if (ctx.lexer_error_reported) {
        /* Accepted by the grammar and rejected by us: the root IS built, and
         * nobody is going to be handed it. */
        ast_free_program(ctx.parsed_program);
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
/* Is this span a word the grammar has taken? `lexer_identifier_type` is the
 * authoritative classifier and already exported -- the keyword list lives in
 * exactly one place and this asks it rather than keeping a copy. */
static int word_is_reserved(const char *word) {
    return word && word[0] &&
           lexer_identifier_type(word, (int)strlen(word)) != TOKEN_IDENT;
}

/* Name the reserved word, when a reserved word is what went wrong.
 *
 * TWO SHAPES AND NO MORE, deliberately narrow: a note that fires on every
 * syntax error near a keyword would be wrong more often than right, and a
 * diagnostic that is sometimes a lie is worse than a terse one.
 *
 *   `function f(a, each)`  -- the reserved word IS the unexpected token, and
 *                             an identifier is what the grammar wanted there.
 *   `on = 0`               -- the unexpected token is the `=`, and the word
 *                             before it is reserved. An assignment is the one
 *                             context where that pairing cannot mean anything
 *                             else.
 *
 * The second rule is restricted to `=` on purpose. Taken generally -- "the
 * previous token was a keyword" -- it fires on `for i = 1 to` with the line
 * unfinished, where `to` is perfectly correct and the author never tried to
 * use it as a name. */
static const char *syntax_error_reserved_word(gb_parse_ctx *ctx, const char *message) {
    if (!message) {
        return NULL;
    }
    if (word_is_reserved(ctx->tok_word) && strstr(message, "IDENT")) {
        return ctx->tok_word;
    }
    if (ctx->tok_type == TOKEN_OP_EQ && word_is_reserved(ctx->tok_prev_word)) {
        return ctx->tok_prev_word;
    }
    /* THE THIRD SHAPE, and the one the first two miss: a reserved word at the
     * START of a statement. `each = 5`, `to = 1` and `step = 3` are exactly
     * the ordinary English words a beginner reaches for, and bison offers no
     * expected list at all there -- just `unexpected EACH` -- so neither rule
     * above fires. The `=` has not been lexed yet (the reserved word IS the
     * lookahead), so this asks the SOURCE whether one follows: read-only, one
     * character, and it cannot disturb a parse that is already over.
     *
     * Requiring the `=` is what keeps it honest. Without it the note would
     * fire on every misplaced keyword -- `then` with no `if` -- where the
     * author never tried to use the word as a name and the note would be a
     * true sentence about the wrong mistake. */
    if (word_is_reserved(ctx->tok_word) && ctx->tok_after) {
        const char *p = ctx->tok_after;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '=') {
            return ctx->tok_word;
        }
    }
    return NULL;
}

static void report_syntax_error(gb_parse_ctx *ctx, int line, int column,
                                int end_line, int end_column, const char *message) {
    if (ctx->lexer_error_reported) {
        return;
    }
    char reworded[512];
    const char *reserved = syntax_error_reserved_word(ctx, message);
    if (reserved) {
        snprintf(reworded, sizeof(reworded),
                 "%s -- '%s' is a reserved word and cannot be used as a name",
                 message, reserved);
        message = reworded;
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

    /* Roll the last-two-tokens window (see gb_parse_ctx). The SPELLING is the
     * author's own bytes, so a message can quote what they typed. */
    ctx->tok_prev_type = ctx->tok_type;
    memcpy(ctx->tok_prev_word, ctx->tok_word, sizeof(ctx->tok_word));
    ctx->tok_type = token.type;
    ctx->tok_after = token.start + token.length;
    ctx->tok_word[0] = '\0';
    if (token.length > 0 && (size_t)token.length < sizeof(ctx->tok_word) &&
        (isalpha((unsigned char)token.start[0]) || token.start[0] == '_')) {
        memcpy(ctx->tok_word, token.start, (size_t)token.length);
        ctx->tok_word[token.length] = '\0';
    }

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
    case TOKEN_MODIFIER_PREFIX:
        lvalp->text = copy_text(token.start, token.length);
        return MODIFIER_PREFIX;
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
