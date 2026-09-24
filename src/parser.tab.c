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

/* THE HEAD IS CHECKED WHERE IT IS WRITTEN (DOGFOOD 31).
 *
 * The server block is `IDENT IDENT ( ... )` with ZERO reserved words, which is
 * the right design -- `server = webserver.listen(...)` appears in
 * stdlib/web.bas and thirty-odd fixtures, so reserving the word would have
 * broken all of it -- and it has one consequence nobody had paid for: ANY two
 * identifiers followed by `()` open a server block. `sub greet()` does.
 * `def greet()`, `procedure greet()` and `foo bar()` do too; measured, all
 * four behave identically.
 *
 * The parser then demanded a body and an `end`, swallowing following lines
 * while it looked, and reported wherever that search failed -- one or more
 * lines BELOW the mistake. At the prompt the head alone reads as "unexpected
 * end of file", which the continuation logic takes for an unfinished
 * statement, so it waits and eats whatever is typed next.
 *
 * A MID-RULE ACTION, so this runs the moment the parser has committed to the
 * production and before a single body line is read. The alternative -- naming
 * the cause from the failure site -- means guessing from bison's expected set,
 * and a diagnostic that guesses is what this whole cluster was about.
 *
 * `sub` IS NOT RESERVED AND MUST NOT BE: it is a variable in `forensics`,
 * `ari` and `mdna`. The fix is a message, not a keyword. */
static void server_head_note(gb_parse_ctx *ctx, const char *name,
                             int line, int column) {
    ctx->bad_block_word[0] = '\0';
    if (!name || strcmp(name, "server") == 0) {
        return;
    }
    if (strlen(name) < sizeof(ctx->bad_block_word)) {
        snprintf(ctx->bad_block_word, sizeof(ctx->bad_block_word), "%s", name);
        ctx->bad_block_line = line;
        ctx->bad_block_column = column;
    }
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



#line 520 "src/parser.tab.c"

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
  YYSYMBOL_123_2 = 123,                    /* @2  */
  YYSYMBOL_124_3 = 124,                    /* @3  */
  YYSYMBOL_server_item_list = 125,         /* server_item_list  */
  YYSYMBOL_server_item = 126,              /* server_item  */
  YYSYMBOL_server_string_list = 127,       /* server_string_list  */
  YYSYMBOL_watch_target_path = 128,        /* watch_target_path  */
  YYSYMBOL_without_watchers_statement = 129, /* without_watchers_statement  */
  YYSYMBOL_on_error_statement = 130,       /* on_error_statement  */
  YYSYMBOL_error_statement = 131,          /* error_statement  */
  YYSYMBOL_return_statement = 132,         /* return_statement  */
  YYSYMBOL_label_statement = 133,          /* label_statement  */
  YYSYMBOL_goto_statement = 134,           /* goto_statement  */
  YYSYMBOL_gosub_statement = 135,          /* gosub_statement  */
  YYSYMBOL_break_statement = 136,          /* break_statement  */
  YYSYMBOL_continue_statement = 137,       /* continue_statement  */
  YYSYMBOL_if_statement = 138,             /* if_statement  */
  YYSYMBOL_if_block_tail = 139,            /* if_block_tail  */
  YYSYMBOL_if_inline_tail = 140,           /* if_inline_tail  */
  YYSYMBOL_inline_statement = 141,         /* inline_statement  */
  YYSYMBOL_expression = 142,               /* expression  */
  YYSYMBOL_or_expression = 143,            /* or_expression  */
  YYSYMBOL_and_expression = 144,           /* and_expression  */
  YYSYMBOL_not_expression = 145,           /* not_expression  */
  YYSYMBOL_comparison_expression = 146,    /* comparison_expression  */
  YYSYMBOL_additive_expression = 147,      /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 148, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 149,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 150,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 151,      /* comparison_operator  */
  YYSYMBOL_primary = 152,                  /* primary  */
  YYSYMBOL_record_literal = 153,           /* record_literal  */
  YYSYMBOL_ident_suffix = 154,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 155,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 156,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 157,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 158,            /* argument_list  */
  YYSYMBOL_array_argument_list = 159,      /* array_argument_list  */
  YYSYMBOL_parameter_list_opt = 160,       /* parameter_list_opt  */
  YYSYMBOL_parameter_default = 161,        /* parameter_default  */
  YYSYMBOL_parameter_list = 162,           /* parameter_list  */
  YYSYMBOL_field_name = 163,               /* field_name  */
  YYSYMBOL_dot_field_name = 164,           /* dot_field_name  */
  YYSYMBOL_record_field_list = 165,        /* record_field_list  */
  YYSYMBOL_field_policy = 166,             /* field_policy  */
  YYSYMBOL_optional_newlines = 167         /* optional_newlines  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;



/* Unqualified %code blocks.  */
#line 519 "src/parser.y"

static int yylex(YYSTYPE *lvalp, YYLTYPE *llocp, gb_parse_ctx *ctx);
static void yyerror(YYLTYPE *llocp, gb_parse_ctx *ctx, const char *message);
static void report_syntax_error(gb_parse_ctx *ctx, int line, int column,
                                int end_line, int end_column, const char *message);

#line 729 "src/parser.tab.c"

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
#define YYNNTS  80
/* YYNRULES -- Number of rules.  */
#define YYNRULES  329
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  702

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
       0,   603,   603,   607,   608,   609,   613,   614,   615,   616,
     617,   618,   619,   620,   621,   622,   623,   624,   625,   626,
     627,   628,   629,   630,   631,   632,   633,   634,   635,   636,
     637,   638,   644,   655,   660,   661,   673,   685,   686,   687,
     688,   692,   693,   694,   698,   699,   700,   711,   711,   719,
     723,   724,   728,   729,   730,   731,   735,   741,   745,   746,
     752,   757,   766,   779,   808,   809,   810,   814,   818,   834,
     839,   847,   851,   872,   878,   884,   890,   893,   899,   900,
     904,   905,   906,   910,   911,   912,   913,   914,   915,   916,
     917,   918,   919,   920,   921,   922,   923,   924,   925,   926,
     927,   928,   929,   930,   931,   932,   933,   934,   940,   951,
     954,   961,   964,   970,   976,   982,   983,   984,   985,   986,
     987,  1003,  1019,  1040,  1041,  1045,  1049,  1052,  1060,  1066,
    1070,  1071,  1091,  1090,  1098,  1097,  1107,  1108,  1109,  1113,
    1116,  1119,  1122,  1125,  1131,  1132,  1136,  1137,  1141,  1147,
    1148,  1149,  1150,  1155,  1168,  1173,  1178,  1188,  1192,  1193,
    1197,  1204,  1208,  1217,  1218,  1222,  1223,  1227,  1231,  1238,
    1241,  1244,  1253,  1263,  1266,  1269,  1275,  1282,  1292,  1293,
    1294,  1295,  1296,  1297,  1298,  1299,  1300,  1301,  1302,  1306,
    1310,  1311,  1315,  1316,  1338,  1339,  1343,  1344,  1345,  1351,
    1352,  1353,  1357,  1358,  1359,  1363,  1364,  1371,  1375,  1376,
    1377,  1381,  1382,  1383,  1384,  1389,  1403,  1404,  1405,  1406,
    1407,  1408,  1409,  1410,  1411,  1412,  1416,  1417,  1418,  1419,
    1420,  1437,  1443,  1444,  1445,  1446,  1447,  1448,  1449,  1450,
    1451,  1455,  1456,  1460,  1465,  1470,  1476,  1488,  1493,  1501,
    1511,  1523,  1524,  1528,  1529,  1533,  1534,  1538,  1539,  1553,
    1554,  1555,  1556,  1557,  1558,  1559,  1560,  1564,  1565,  1568,
    1569,  1584,  1591,  1600,  1601,  1602,  1603,  1604,  1605,  1606,
    1607,  1608,  1609,  1610,  1611,  1612,  1613,  1614,  1615,  1616,
    1617,  1618,  1619,  1620,  1621,  1622,  1623,  1624,  1625,  1626,
    1627,  1628,  1629,  1630,  1631,  1632,  1633,  1634,  1635,  1636,
    1637,  1638,  1639,  1640,  1641,  1642,  1643,  1644,  1645,  1646,
    1650,  1651,  1652,  1653,  1654,  1655,  1663,  1690,  1709,  1710
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
  "unwatch_statement", "watch_target_list", "server_statement", "@2", "@3",
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

#define YYPACT_NINF (-510)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -510,    71,   926,  -510,    12,     5,  -510,  2180,  -510,  2146,
      91,    89,    28,  2180,  2180,   139,   148,   241,  2180,    64,
      64,    54,  2180,   137,    42,  -510,   177,   119,   162,   166,
     266,   274,   118,  -510,  -510,   110,   178,   141,   114,   168,
    -510,  -510,  -510,  -510,  -510,  -510,  -510,  -510,  -510,   175,
    -510,   190,  -510,  -510,   227,   236,   242,   243,   245,   248,
     250,   259,  -510,   238,  2180,  2180,   231,  -510,  -510,   268,
    2239,  -510,  -510,  -510,  -510,  2180,  2225,   348,   277,  -510,
    2239,  2180,  -510,  -510,   -15,   337,   327,   330,  -510,  -510,
     223,   224,  -510,    26,  -510,  -510,   355,   305,  -510,   286,
     157,   359,  -510,   280,   282,  -510,  -510,   292,   296,  -510,
    -510,  -510,   297,    64,  -510,   184,   289,  -510,   293,    72,
     117,   374,  -510,  -510,  -510,  -510,  -510,    53,  -510,   346,
     304,   299,   123,  -510,   377,  -510,   119,  -510,  -510,  -510,
    -510,  -510,  -510,  2180,  2180,  -510,  2432,  2180,   164,  -510,
    -510,  -510,  -510,  -510,  -510,  -510,  -510,  -510,  -510,  -510,
    -510,  -510,  2316,  -510,   308,   307,   313,  -510,  2180,  -510,
    -510,    -9,   316,   318,  -510,   319,   563,   687,  2180,  2490,
    -510,  2064,  2180,  2180,  -510,  -510,  -510,  -510,  -510,  -510,
    -510,  -510,  -510,  -510,  2239,  2239,   474,  2239,  2239,  2239,
    2180,  2548,   389,  2180,  2180,  2180,  2180,   393,    27,   810,
    -510,   384,   397,   397,    64,   128,    64,  -510,   398,  -510,
    -510,  -510,   101,  -510,   104,  -510,   333,   397,  -510,   406,
     397,  -510,   408,   414,   415,   386,  -510,   342,   416,   349,
     352,  -510,  -510,  -510,  -510,  -510,  -510,  -510,  -510,  -510,
    -510,  -510,  -510,  -510,  -510,  -510,  -510,  -510,  -510,  -510,
    -510,  -510,  -510,  -510,  -510,  -510,  -510,  -510,  -510,  -510,
    -510,  -510,  -510,  -510,  -510,  -510,  -510,  -510,  -510,  -510,
    -510,  -510,  -510,  -510,  -510,  -510,  -510,  -510,  -510,  2180,
    2180,   353,  -510,   340,   142,  -510,   156,  -510,  2180,  -510,
     354,   360,  2180,  -510,  -510,  -510,  -510,  -510,   350,  -510,
     367,   365,  -510,   376,   379,   381,  -510,  -510,  -510,  -510,
    -510,  -510,  -510,  -510,  -510,  -510,  -510,  -510,  -510,   375,
     330,  -510,   224,   224,  2239,   244,  -510,  -510,   385,   382,
     387,  -510,  -510,  -510,   383,   388,   433,   431,  2180,   467,
    2180,   984,  2180,   230,   419,   399,   395,   401,   159,   402,
     289,  1042,  -510,  1100,  -510,  -510,  -510,  -510,  2180,   410,
    -510,   404,   418,  1158,   482,  -510,  -510,   406,  -510,   411,
    2180,  2180,  -510,  -510,   479,  -510,  2180,  2180,   409,  -510,
    -510,  -510,  -510,   422,  -510,   132,   170,  -510,  2180,  2180,
    -510,   868,   478,   244,  -510,  2180,  2180,   417,  -510,  2180,
    2180,   420,   462,   421,   468,   485,  2180,   423,   489,   258,
     429,   510,   434,   436,  -510,   469,   465,   445,  -510,  -510,
     440,   471,   522,   446,  -510,   454,   457,  2180,   472,  -510,
    -510,  -510,  -510,   752,  -510,   602,  -510,  -510,   473,   475,
    2017,   533,  -510,  2036,  -510,   476,   477,  -510,  1216,   134,
     481,  -510,  2180,  -510,   488,   490,   539,  -510,   491,  -510,
    -510,  -510,  -510,  -510,  -510,   554,   557,  -510,  -510,   499,
    -510,  -510,  1274,   493,   494,  -510,  1332,  -510,   496,  -510,
    -510,  -510,  -510,  -510,   486,    40,  -510,   497,   151,  -510,
    -510,  -510,  2180,  -510,   498,   500,  2180,  -510,   501,  -510,
    -510,  1390,   558,   115,  -510,  2180,  -510,  -510,  1216,   506,
    -510,  -510,   509,  1448,  -510,  -510,  -510,  1506,   258,  1564,
    1622,   548,  -510,  -510,   541,  1680,  -510,  1738,  2180,   314,
     594,   595,  -510,  -510,    47,   479,  2180,  2180,   583,  1796,
    -510,  -510,   584,  1854,  -510,   572,   519,  -510,   520,   524,
    1216,  1216,  -510,  -510,  1448,  -510,  -510,  -510,   527,   529,
     535,  -510,  -510,  -510,  -510,  -510,  -510,  -510,  -510,  -510,
    -510,   540,  -510,   545,  -510,   546,   551,   553,   556,   560,
     561,   562,   564,  -510,   573,  -510,   579,   590,   565,   566,
     575,   596,  -510,   578,   582,   185,   567,   576,   655,   585,
    -510,  -510,   577,   648,  2083,   649,   581,  -510,  -510,  -510,
    -510,  -510,  1216,  1448,  -510,  -510,  -510,  -510,  -510,  -510,
    -510,  -510,  -510,  -510,  -510,  -510,  -510,   587,   589,   592,
    -510,  -510,   593,   597,  2374,   397,   661,  -510,  -510,  -510,
     599,   586,  -510,   600,  -510,   601,   603,  -510,  1216,  -510,
    -510,  -510,  -510,  -510,  -510,   605,   160,   591,  -510,  1912,
    -510,  2180,   868,  -510,   868,   478,  -510,  -510,  -510,   609,
     610,   622,  -510,  -510,  -510,  -510,    81,  -510,  -510,   611,
     675,   163,  1970,  -510,   614,   695,   696,  -510,   617,   619,
    -510,  -510
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       3,     0,     2,     1,    44,     0,    32,     0,    45,     0,
       0,     0,     0,     0,     0,   163,   165,     0,   158,     0,
       0,     0,     0,     0,     0,    46,     0,     0,     0,     0,
       0,     0,     0,     4,     5,     0,     0,    41,     0,     0,
       9,    10,    12,    11,    13,    14,    15,    16,    17,     0,
      19,     0,    20,    22,     0,     0,     0,     0,     0,     0,
       0,     0,    31,     0,   251,   251,   226,    44,   229,     0,
       0,   233,   234,   235,   236,     0,     0,     0,     0,   232,
       0,     0,   328,   328,   243,     0,   189,   190,   192,   194,
     196,   199,   202,   205,   211,   240,   228,     0,    56,     0,
       0,     0,     3,     0,     0,   164,   166,     0,     0,   159,
     161,   162,    44,     0,   146,     0,   130,   129,     0,     0,
       0,     0,   157,    52,    54,    53,    55,   123,    50,     0,
       0,     0,   116,   118,   115,   117,     0,     6,    49,    37,
      38,    39,    40,     0,     0,    47,     0,     0,     0,   160,
       7,     8,    18,    21,    23,    24,    25,    26,    27,    28,
      29,    30,     0,   253,     0,   252,     0,   249,   251,   207,
     195,   208,     0,     0,   206,     0,     0,     0,   251,     0,
     230,     0,     0,     0,   216,   217,   218,   219,   220,   221,
     222,   223,   224,   225,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       3,     0,   257,   257,     0,     0,     0,     3,     0,     3,
     156,   155,     0,   154,     0,   151,     0,   257,    51,     0,
     257,     3,     0,     0,     0,     0,    33,     0,     0,   273,
       0,   274,   319,   289,   286,   287,   278,   293,   300,   301,
     302,   318,   298,   299,   297,   284,   282,   307,   288,   279,
     316,   291,   292,   280,   283,   290,   315,   303,   304,   310,
     294,   305,   306,   313,   317,   285,   314,   281,   275,   276,
     277,   311,   312,   309,   295,   296,   308,    43,    34,     0,
       0,   273,   272,     0,     0,   271,     0,    58,     0,    59,
       0,     0,   251,   227,   237,   238,   329,   255,   328,   241,
     328,     0,   273,     0,   247,    44,     3,   178,    41,   179,
     180,   181,   182,   183,   184,   185,   186,   187,   188,     0,
     191,   193,   200,   201,     0,   197,   203,   204,     0,   273,
       0,   213,   250,    57,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    78,   267,     0,   258,     0,     0,     0,
     131,     0,   147,     0,   153,   152,   149,   150,   251,     0,
     125,     0,     0,     0,   121,   119,   120,     0,    42,     0,
     251,   251,    36,    35,     0,   134,     0,     0,     0,   328,
     254,   231,   209,     0,   328,     0,     0,   244,   251,   251,
     245,     0,   173,   198,   212,   251,   251,     0,     3,     0,
       0,     0,     0,     0,    45,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     3,    45,    45,     0,   124,     3,
       0,    45,     0,     0,    48,     0,     0,   326,     0,   136,
     320,   321,   132,     0,   210,     0,   239,   242,     0,     0,
       0,    45,   167,     0,   168,     0,     0,     3,     0,     0,
       0,     3,     0,    73,     0,     0,     0,    80,     0,   259,
     262,   263,   264,   265,   266,     0,     0,   268,     3,   269,
       3,     3,     0,     0,     0,    62,     0,     3,     0,   122,
       3,    60,    61,   327,     0,     0,   136,   273,     0,   256,
     246,   248,     0,     3,     0,     0,     0,     3,     0,   214,
     215,     0,    45,    46,    67,     0,     3,     3,     0,     0,
      74,    80,     0,    79,    75,   261,   260,     0,     0,     0,
       0,    45,   127,   148,    45,     0,   114,     0,     0,     0,
       0,     0,   137,   138,     0,     0,     0,     0,     0,     0,
     170,   169,     0,     0,   174,    45,     0,    65,     0,     0,
       0,     0,    68,     3,    76,    80,   108,    81,     0,     0,
       0,    86,    87,    89,    88,    90,    82,    91,    92,    93,
      94,     0,    96,     0,    98,     0,     0,     0,     0,     0,
       0,     0,     0,   107,    45,   270,    45,    45,     0,     0,
      45,    45,   322,     0,   144,     0,     0,     0,     0,     0,
     323,   324,     0,    45,     0,    45,     0,    64,    66,     3,
      71,    69,     0,    77,    83,    84,    85,    95,    97,    99,
     100,   101,   102,   103,   104,   105,   106,     0,     0,     0,
     126,   111,     0,     0,     0,   257,     0,   139,   135,     3,
       0,     0,     3,     0,     3,     0,     0,    63,     0,    70,
     109,   110,   128,   113,   112,     0,     0,     0,   145,     0,
     133,     0,     0,   171,     0,   173,   175,    72,   136,     0,
       0,    45,   325,   172,   177,   176,     0,   136,     3,     0,
       0,     0,     0,   143,     0,     0,    45,   142,     0,     0,
     141,   140
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -510,  -510,   -73,  -510,  -178,   604,  -510,    -2,   616,  -510,
    -510,   623,  -177,  -173,  -509,  -507,  -501,  -498,  -489,  -487,
    -510,  -510,  -425,  -510,  -485,  -482,  -481,  -478,  -153,   613,
     331,  -475,  -474,   -98,  -510,  -510,  -510,  -491,  -510,  -510,
     537,  -473,  -149,  -148,  -141,  -464,  -129,  -125,  -117,  -111,
    -461,  -412,    76,  -441,    17,  -510,   633,   -62,  -510,  -187,
     143,   -43,   678,   559,  -510,   458,  -510,  -510,  -510,   -58,
    -510,  -510,  -207,   232,  -510,   315,   -99,  -176,   218,   -81
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    34,    35,   147,    36,    84,   148,   238,
     127,   128,    38,    39,    40,   514,    41,    42,    43,    44,
     353,   418,   523,   576,    45,    46,    47,    48,    49,   129,
     371,    50,    51,   115,    52,   496,   439,   495,   543,   605,
     116,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,   452,   454,   329,   163,    86,    87,    88,    89,    90,
      91,    92,    93,   197,    94,    95,   180,   400,    96,   164,
     165,   308,   355,   477,   356,   294,   295,   296,   438,   176
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      37,   310,   177,   317,   319,   544,   357,   166,   320,   504,
     335,   562,   508,   170,   571,   215,    63,   110,   111,   114,
     369,   301,   572,   372,    85,   573,    98,   169,   321,   209,
     103,   104,   322,   323,   574,   109,   575,   174,   577,   117,
     324,   578,   579,   122,   539,   580,   119,   287,   582,   583,
     584,   539,   325,   620,   621,   571,   326,   123,   112,   588,
     540,   178,   593,   572,   327,   348,   573,   608,    67,   200,
     328,     3,   179,   124,     8,   574,   220,   575,   201,   577,
     314,    65,   578,   579,     8,   539,   580,   125,    64,   582,
     583,   584,   541,   100,   221,    99,   564,   120,   175,   541,
     588,   690,   341,   593,   200,   364,   126,    25,   366,   349,
     300,   114,   102,   201,   571,   659,   358,    25,   222,    67,
     311,   331,   572,   123,   542,   573,   223,   232,   101,   227,
     113,   542,   233,   541,   574,     8,   575,   351,   577,   124,
     623,   578,   579,   105,   361,   580,   363,   403,   582,   583,
     584,   677,   106,   125,   365,   336,   337,   367,   373,   588,
     236,   237,   593,   224,   288,   542,   130,   539,    25,   515,
     131,   225,   126,   655,   136,   139,   140,   141,   142,   318,
      66,    67,    68,   695,    69,    70,   138,   686,   118,   139,
     140,   141,   142,   307,   137,   205,   691,     8,   150,   557,
      71,    72,    73,    74,   386,   359,    75,    37,    76,    77,
     216,   446,   114,   546,   114,   541,   306,   338,   516,   206,
     343,   344,   345,   346,   149,   387,   289,   395,    78,   396,
      25,   138,    79,   388,   547,   167,   423,   679,   389,   207,
     143,   216,   389,   401,   393,   107,   416,   542,   108,   417,
      80,   447,   151,    81,   306,    82,   144,    83,   145,   152,
     683,   469,   684,   470,   121,   146,   216,   646,   217,   647,
     132,   133,   317,   319,   153,   317,   319,   320,   134,   135,
     320,   471,   472,   473,   474,   184,   185,   186,   187,   188,
     189,   190,   191,   192,   193,   194,   195,   321,   198,   199,
     321,   322,   323,   145,   322,   323,   382,   383,   443,   324,
     427,   154,   324,   445,   162,   390,   194,   195,   603,   604,
     155,   325,   435,   436,   325,   326,   156,   157,   326,   158,
     475,   476,   159,   327,   160,   458,   327,   332,   333,   328,
     448,   449,   328,   161,   168,   568,   569,   455,   456,    37,
     570,   482,   172,   173,   181,   182,   486,   183,   202,    37,
     203,    37,   204,   208,   210,   411,   211,   413,   212,   415,
     581,    37,   213,   214,   585,   586,   218,   219,   226,   229,
     230,   234,   587,   231,   511,   297,   568,   569,   518,   298,
     299,   570,   302,   342,   589,   303,   304,   347,   590,    37,
     352,   354,   362,   440,   441,   527,   591,   529,   530,   368,
     370,   581,   592,   374,   535,   585,   586,   537,   375,   377,
     376,   378,   379,   587,   385,   380,   459,   460,   381,   384,
     549,   391,   394,   466,   553,   589,   317,   319,   667,   590,
      83,   320,   397,   560,   561,   568,   569,   591,   318,   389,
     570,   318,   398,   592,   493,   399,    37,    64,   405,   402,
     407,   321,   499,   406,   404,   322,   323,   409,   666,   410,
     581,   412,   408,   324,   585,   586,   420,   421,   422,   519,
      37,   419,   587,   437,    37,   325,   424,   428,   429,   326,
     622,   432,   434,   442,   589,   430,   453,   327,   590,   444,
     462,   457,   465,   328,   461,   463,   591,   467,   464,    37,
     468,   558,   592,   478,   479,   484,    37,   483,   480,   548,
     481,    37,   485,   552,   487,    37,   489,    37,    37,   488,
     490,   491,   559,    37,   492,    37,   184,   185,   186,   187,
     188,   189,   190,   191,   192,   193,   658,    37,   505,   494,
     500,    37,   501,   509,   510,   602,   522,   525,    37,    37,
     526,   528,    37,   610,   611,   517,    66,    67,    68,   538,
      69,    70,   520,   545,   521,   524,   669,   532,   533,   672,
     536,   674,   550,     8,   551,   554,    71,    72,    73,    74,
     563,   556,    75,   565,    76,    77,   598,   599,   606,   607,
     612,   614,   616,   617,   618,    66,    67,    68,   619,    69,
      70,   624,   318,   625,    78,   692,    25,   637,    79,   626,
      37,    37,     8,   638,   627,    71,    72,    73,    74,   628,
     629,    75,   642,    76,    77,   630,    80,   631,   639,    81,
     632,    82,   305,    83,   633,   634,   635,   306,   636,   640,
     641,   648,   643,    78,   644,    25,    37,    79,   645,   650,
     649,   652,   651,   653,   656,   657,   668,    37,   680,   671,
      37,   660,    37,   661,   689,    80,   662,   663,    81,   694,
      82,   664,    83,   670,   673,   675,   306,   676,   682,   678,
      37,   291,   292,   687,   688,   693,   241,   242,   697,   698,
     699,   700,   243,   701,   244,   245,   196,   246,   433,   247,
     248,   249,   250,   251,   252,   253,   254,   255,   256,   257,
     258,   259,   260,   261,   262,   263,   264,   265,   266,   267,
     268,   269,   270,   271,   272,   273,   274,   275,   276,   277,
     278,   279,   280,   281,   282,   283,   284,   285,   286,   235,
     228,   685,   290,   360,   171,   334,   497,   292,   498,   392,
     595,   241,   242,   609,     0,     0,     0,   243,   309,   244,
     245,   306,   246,     0,   247,   248,   249,   250,   251,   252,
     253,   254,   255,   256,   257,   258,   259,   260,   261,   262,
     263,   264,   265,   266,   267,   268,   269,   270,   271,   272,
     273,   274,   275,   276,   277,   278,   279,   280,   281,   282,
     283,   284,   285,   286,     4,   330,     0,     5,     0,     0,
       6,     0,     0,     0,     0,     7,     0,     0,     0,     0,
       8,     0,     9,     0,     0,     0,   306,     0,     0,     0,
      10,     0,     0,    11,     0,     0,    12,   350,     0,     0,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,     0,    24,    25,     0,    26,    27,    28,    29,    30,
      31,    32,     4,     0,     0,     5,     0,     0,     6,     0,
       0,     0,     0,     7,     0,     0,   450,     0,   451,     0,
       9,     0,     0,     0,    33,     0,     0,     0,    10,     0,
       0,    11,     0,     0,    12,     0,     0,     0,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,     0,
      24,    25,     0,    26,    27,    28,    29,    30,    31,    32,
       4,     0,     0,     5,     0,     0,     6,     0,     0,     0,
       0,     7,     0,     0,     0,     0,     8,     0,     9,     0,
       0,     0,    33,     0,     0,     0,    10,     0,     0,    11,
       0,     0,    12,     0,     0,     0,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,     0,    24,    25,
       0,    26,    27,    28,    29,    30,    31,    32,     4,     0,
       0,     5,     0,     0,     6,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   414,     0,     9,     0,     0,     0,
      33,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     7,     0,     0,
       0,     0,   425,     0,     9,     0,     0,     0,    33,     0,
       0,     0,    10,     0,     0,    11,     0,     0,    12,     0,
       0,     0,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,     0,    24,    25,     0,    26,    27,    28,
      29,    30,    31,    32,     4,     0,     0,     5,     0,     0,
       6,     0,     0,     0,     0,     7,     0,     0,     0,     0,
     426,     0,     9,     0,     0,     0,    33,     0,     0,     0,
      10,     0,     0,    11,     0,     0,    12,     0,     0,     0,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,     0,    24,    25,     0,    26,    27,    28,    29,    30,
      31,    32,     4,     0,     0,     5,     0,     0,     6,     0,
       0,     0,     0,     7,     0,     0,     0,     0,   431,     0,
       9,     0,     0,     0,    33,     0,     0,     0,    10,     0,
       0,    11,     0,     0,    12,     0,     0,     0,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,     0,
      24,    25,     0,    26,    27,    28,    29,    30,    31,    32,
       4,     0,     0,     5,     0,     0,     6,     0,     0,     0,
       0,     7,     0,     0,     0,     0,   512,     0,     9,     0,
       0,     0,    33,     0,     0,     0,    10,     0,     0,    11,
       0,     0,    12,     0,     0,     0,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,     0,    24,   513,
       0,    26,    27,    28,    29,    30,    31,    32,     4,     0,
       0,     5,     0,     0,     6,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   531,     0,     9,     0,     0,     0,
      33,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     7,     0,     0,
       0,     0,   534,     0,     9,     0,     0,     0,    33,     0,
       0,     0,    10,     0,     0,    11,     0,     0,    12,     0,
       0,     0,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,     0,    24,    25,     0,    26,    27,    28,
      29,    30,    31,    32,     4,     0,     0,     5,     0,     0,
       6,     0,     0,     0,     0,     7,     0,     0,     0,     0,
     555,     0,     9,     0,     0,     0,    33,     0,     0,     0,
      10,     0,     0,    11,     0,     0,    12,     0,     0,     0,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,     0,    24,    25,     0,    26,    27,    28,    29,    30,
      31,    32,   315,     0,     0,     5,     0,     0,   566,     0,
       0,     0,     0,     7,     0,     0,     0,     0,     8,     0,
       9,     0,     0,     0,    33,     0,     0,     0,    10,     0,
       0,    11,     0,     0,    12,     0,     0,     0,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,     0,
      24,    25,     0,    26,    27,    28,    29,    30,    31,    32,
       4,     0,     0,     5,     0,     0,     6,     0,     0,     0,
       0,     7,     0,     0,     0,     0,   594,     0,     9,     0,
       0,     0,   567,     0,     0,     0,    10,     0,     0,    11,
       0,     0,    12,     0,     0,     0,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,     0,    24,    25,
       0,    26,    27,    28,    29,    30,    31,    32,     4,     0,
       0,     5,     0,     0,     6,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   596,     0,     9,     0,     0,     0,
      33,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     7,     0,     0,
       0,     0,   597,     0,     9,     0,     0,     0,    33,     0,
       0,     0,    10,     0,     0,    11,     0,     0,    12,     0,
       0,     0,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,     0,    24,    25,     0,    26,    27,    28,
      29,    30,    31,    32,     4,     0,     0,     5,     0,     0,
       6,     0,     0,     0,     0,     7,     0,     0,     0,     0,
     600,     0,     9,     0,     0,     0,    33,     0,     0,     0,
      10,     0,     0,    11,     0,     0,    12,     0,     0,     0,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,     0,    24,    25,     0,    26,    27,    28,    29,    30,
      31,    32,     4,     0,     0,     5,     0,     0,     6,     0,
       0,     0,     0,     7,     0,     0,     0,     0,   601,     0,
       9,     0,     0,     0,    33,     0,     0,     0,    10,     0,
       0,    11,     0,     0,    12,     0,     0,     0,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,     0,
      24,    25,     0,    26,    27,    28,    29,    30,    31,    32,
       4,     0,     0,     5,     0,     0,     6,     0,     0,     0,
       0,     7,     0,     0,     0,     0,   613,     0,     9,     0,
       0,     0,    33,     0,     0,     0,    10,     0,     0,    11,
       0,     0,    12,     0,     0,     0,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,     0,    24,    25,
       0,    26,    27,    28,    29,    30,    31,    32,     4,     0,
       0,     5,     0,     0,     6,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   615,     0,     9,     0,     0,     0,
      33,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     7,     0,     0,
       0,     0,   681,     0,     9,     0,     0,     0,    33,     0,
       0,     0,    10,     0,     0,    11,     0,     0,    12,     0,
       0,     0,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,     0,    24,    25,     0,    26,    27,    28,
      29,    30,    31,    32,     4,     0,     0,     5,     0,     0,
       6,     0,     0,     0,     0,     7,     0,     0,     0,     0,
     696,     0,     9,     0,     0,     0,    33,     0,     0,     0,
      10,     0,     0,    11,     0,     0,    12,     0,     0,     0,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,   315,    24,    25,     5,    26,    27,    28,    29,    30,
      31,    32,   502,     0,     0,     0,     0,     8,     0,     9,
     315,     0,     0,     5,     0,     0,     0,     0,     0,     0,
       0,   506,     0,     0,    33,     0,     8,     0,     9,    15,
      16,     0,    18,    19,    20,     0,     0,     0,   315,    24,
      25,     5,    26,     0,     0,     0,    30,    31,    15,    16,
       0,    18,    19,    20,     8,     0,     9,   315,    24,    25,
       5,    26,     0,     0,     0,    30,    31,     0,     0,     0,
       0,   503,     0,     8,     0,     9,    15,    16,     0,    18,
      19,    20,     0,     0,     0,     0,    24,    25,     0,    26,
     507,     0,     0,    30,    31,    15,    16,     0,    18,    19,
      20,     0,     0,     0,     0,    24,    25,     0,    26,     0,
       0,     0,    30,    31,     0,     0,     0,     0,   316,    66,
      67,    68,     0,    69,    70,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     8,   654,     0,    71,
      72,    73,    74,     0,     0,    75,     0,    76,    77,     0,
      97,     0,     0,    66,    67,    68,     0,    69,    70,     0,
       0,     0,     0,     0,     0,     0,     0,    78,     0,    25,
       8,    79,     0,    71,    72,    73,    74,     0,     0,    75,
       0,    76,    77,     0,     0,     0,     0,     0,     0,    80,
       0,     0,    81,     0,    82,     0,    83,     0,    66,    67,
      68,    78,    69,    25,     0,    79,     0,     0,     0,     0,
       0,     0,    66,    67,    68,     8,    69,    70,    71,    72,
      73,    74,     0,    80,     0,     0,    81,     0,    82,     8,
      83,     0,    71,    72,    73,    74,     0,     0,     0,     0,
      76,    77,     0,     0,     0,     0,    78,     0,    25,     0,
      79,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      78,     0,    25,     0,    79,     0,     0,     0,     0,     0,
       0,    81,     0,    82,     0,    83,     0,     0,     0,     0,
       0,     0,    80,     0,     0,    81,     0,    82,     0,    83,
     291,   292,     0,     0,     0,   241,   242,     0,     0,     0,
       0,   243,     0,   244,   245,     0,   246,     0,   247,   248,
     249,   250,   251,   252,   253,   254,   255,   256,   257,   258,
     259,   260,   261,   262,   263,   264,   265,   266,   267,   268,
     269,   270,   271,   272,   273,   274,   275,   276,   277,   278,
     279,   280,   281,   282,   283,   284,   285,   286,   291,   292,
       0,     0,     0,   241,   242,     0,     0,     0,     0,   243,
       0,   244,   245,   293,   246,     0,   247,   248,   249,   250,
     251,   252,   253,   254,   255,   256,   257,   258,   259,   260,
     261,   262,   263,   264,   265,   266,   267,   268,   269,   270,
     271,   272,   273,   274,   275,   276,   277,   278,   279,   280,
     281,   282,   283,   284,   285,   286,   239,     0,     0,   240,
       0,   241,   242,     0,     0,     0,     0,   243,     0,   244,
     245,   665,   246,     0,   247,   248,   249,   250,   251,   252,
     253,   254,   255,   256,   257,   258,   259,   260,   261,   262,
     263,   264,   265,   266,   267,   268,   269,   270,   271,   272,
     273,   274,   275,   276,   277,   278,   279,   280,   281,   282,
     283,   284,   285,   286,   312,     0,     0,   313,     0,   241,
     242,     0,     0,     0,     0,   243,     0,   244,   245,     0,
     246,     0,   247,   248,   249,   250,   251,   252,   253,   254,
     255,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   339,     0,     0,   340,     0,   241,   242,     0,
       0,     0,     0,   243,     0,   244,   245,     0,   246,     0,
     247,   248,   249,   250,   251,   252,   253,   254,   255,   256,
     257,   258,   259,   260,   261,   262,   263,   264,   265,   266,
     267,   268,   269,   270,   271,   272,   273,   274,   275,   276,
     277,   278,   279,   280,   281,   282,   283,   284,   285,   286
};

static const yytype_int16 yycheck[] =
{
       2,   177,    83,   181,   181,   496,   213,    65,   181,   450,
     197,   518,   453,    75,   523,   113,     4,    19,    20,    21,
     227,    30,   523,   230,     7,   523,     9,    70,   181,   102,
      13,    14,   181,   181,   523,    18,   523,    80,   523,    22,
     181,   523,   523,    26,     4,   523,     4,   146,   523,   523,
     523,     4,   181,   560,   561,   564,   181,     4,     4,   523,
      20,    76,   523,   564,   181,    38,   564,    20,     4,    78,
     181,     0,    87,    20,    20,   564,     4,   564,    87,   564,
     179,    76,   564,   564,    20,     4,   564,    34,    76,   564,
     564,   564,    52,     4,    22,     4,   521,    55,    81,    52,
     564,    20,   201,   564,    78,     4,    53,    53,     4,    82,
     168,   113,    84,    87,   623,   622,   214,    53,    46,     4,
     178,   183,   623,     4,    84,   623,    54,     4,    39,    76,
      76,    84,     9,    52,   623,    20,   623,   210,   623,    20,
     565,   623,   623,     4,   217,   623,   219,   334,   623,   623,
     623,   658,     4,    34,    53,   198,   199,    53,   231,   623,
     143,   144,   623,    46,   147,    84,     4,     4,    53,    35,
       4,    54,    53,   614,    56,    11,    12,    13,    14,   181,
       3,     4,     5,    20,     7,     8,     8,   678,    51,    11,
      12,    13,    14,   176,    84,    38,   687,    20,    84,    84,
      23,    24,    25,    26,    62,    77,    29,   209,    31,    32,
      82,    79,   214,    62,   216,    52,    84,   200,    84,    62,
     203,   204,   205,   206,    83,    83,    62,   308,    51,   310,
      53,     8,    55,    77,    83,     4,    77,    77,    82,    82,
      62,    82,    82,   316,   302,     4,    16,    84,     7,    19,
      73,    81,    84,    76,    84,    78,    78,    80,    80,    84,
     672,     3,   674,     5,    87,    87,    82,    82,    84,    84,
       4,     5,   450,   450,    84,   453,   453,   450,     4,     5,
     453,    23,    24,    25,    26,    62,    63,    64,    65,    66,
      67,    68,    69,    70,    71,    72,    73,   450,    74,    75,
     453,   450,   450,    80,   453,   453,   289,   290,   389,   450,
     368,    84,   453,   394,    76,   298,    72,    73,     4,     5,
      84,   450,   380,   381,   453,   450,    84,    84,   453,    84,
      72,    73,    84,   450,    84,   408,   453,   194,   195,   450,
     398,   399,   453,    84,    76,   523,   523,   405,   406,   351,
     523,   424,     4,    76,    17,    28,   429,    27,     3,   361,
      55,   363,    76,     4,    84,   348,    84,   350,    76,   352,
     523,   373,    76,    76,   523,   523,    87,    84,     4,    33,
      76,     4,   523,    84,   457,    77,   564,   564,   461,    82,
      77,   564,    76,     4,   523,    77,    77,     4,   523,   401,
      16,     4,     4,   386,   387,   478,   523,   480,   481,    76,
       4,   564,   523,     5,   487,   564,   564,   490,     4,    33,
       5,    79,     6,   564,    84,    76,   409,   410,    76,    76,
     503,    77,    82,   416,   507,   564,   614,   614,   645,   564,
      80,   614,    77,   516,   517,   623,   623,   564,   450,    82,
     623,   453,    76,   564,   437,    76,   458,    76,    76,    84,
      77,   614,   445,    76,    79,   614,   614,    34,   644,    38,
     623,     4,    84,   614,   623,   623,    77,    82,    77,   462,
     482,    62,   623,     4,   486,   614,    84,    77,    84,   614,
     563,     9,    81,    84,   623,    77,    18,   614,   623,    77,
      38,    84,    17,   614,    84,    84,   623,    84,    40,   511,
      21,   513,   623,    84,     4,    50,   518,    48,    84,   502,
      84,   523,    77,   506,    84,   527,     4,   529,   530,    58,
      84,    77,   515,   535,    77,   537,    62,    63,    64,    65,
      66,    67,    68,    69,    70,    71,   619,   549,    15,    77,
      77,   553,    77,    77,    77,   538,    17,     3,   560,   561,
       3,    62,   564,   546,   547,    84,     3,     4,     5,    83,
       7,     8,    84,    76,    84,    84,   649,    84,    84,   652,
      84,   654,    84,    20,    84,    84,    23,    24,    25,    26,
      84,    33,    29,    84,    31,    32,    48,    56,     4,     4,
      17,    17,    30,    84,    84,     3,     4,     5,    84,     7,
       8,    84,   614,    84,    51,   688,    53,    44,    55,    84,
     622,   623,    20,    44,    84,    23,    24,    25,    26,    84,
      84,    29,    57,    31,    32,    84,    73,    84,    48,    76,
      84,    78,    79,    80,    84,    84,    84,    84,    84,    84,
      84,    84,    56,    51,    76,    53,   658,    55,    76,     4,
      84,    84,    77,    15,    15,    84,     5,   669,    77,    83,
     672,    84,   674,    84,    52,    73,    84,    84,    76,     4,
      78,    84,    80,    84,    84,    84,    84,    84,   671,    84,
     692,     4,     5,    84,    84,    84,     9,    10,    84,     4,
       4,    84,    15,    84,    17,    18,    90,    20,   377,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,   136,
     127,   675,   148,   216,    76,   196,     4,     5,   443,   301,
     528,     9,    10,   545,    -1,    -1,    -1,    15,    81,    17,
      18,    84,    20,    -1,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,     4,   182,    -1,     7,    -1,    -1,
      10,    -1,    -1,    -1,    -1,    15,    -1,    -1,    -1,    -1,
      20,    -1,    22,    -1,    -1,    -1,    84,    -1,    -1,    -1,
      30,    -1,    -1,    33,    -1,    -1,    36,    37,    -1,    -1,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    -1,    52,    53,    -1,    55,    56,    57,    58,    59,
      60,    61,     4,    -1,    -1,     7,    -1,    -1,    10,    -1,
      -1,    -1,    -1,    15,    -1,    -1,    18,    -1,    20,    -1,
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
      48,    49,    50,    -1,    52,    53,    -1,    55,    56,    57,
      58,    59,    60,    61,     4,    -1,    -1,     7,    -1,    -1,
      10,    -1,    -1,    -1,    -1,    15,    -1,    -1,    -1,    -1,
      20,    -1,    22,    -1,    -1,    -1,    84,    -1,    -1,    -1,
      30,    -1,    -1,    33,    -1,    -1,    36,    -1,    -1,    -1,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,     4,    52,    53,     7,    55,    56,    57,    58,    59,
      60,    61,    15,    -1,    -1,    -1,    -1,    20,    -1,    22,
       4,    -1,    -1,     7,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    15,    -1,    -1,    84,    -1,    20,    -1,    22,    42,
      43,    -1,    45,    46,    47,    -1,    -1,    -1,     4,    52,
      53,     7,    55,    -1,    -1,    -1,    59,    60,    42,    43,
      -1,    45,    46,    47,    20,    -1,    22,     4,    52,    53,
       7,    55,    -1,    -1,    -1,    59,    60,    -1,    -1,    -1,
      -1,    84,    -1,    20,    -1,    22,    42,    43,    -1,    45,
      46,    47,    -1,    -1,    -1,    -1,    52,    53,    -1,    55,
      84,    -1,    -1,    59,    60,    42,    43,    -1,    45,    46,
      47,    -1,    -1,    -1,    -1,    52,    53,    -1,    55,    -1,
      -1,    -1,    59,    60,    -1,    -1,    -1,    -1,    84,     3,
       4,     5,    -1,     7,     8,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    20,    84,    -1,    23,
      24,    25,    26,    -1,    -1,    29,    -1,    31,    32,    -1,
      34,    -1,    -1,     3,     4,     5,    -1,     7,     8,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    51,    -1,    53,
      20,    55,    -1,    23,    24,    25,    26,    -1,    -1,    29,
      -1,    31,    32,    -1,    -1,    -1,    -1,    -1,    -1,    73,
      -1,    -1,    76,    -1,    78,    -1,    80,    -1,     3,     4,
       5,    51,     7,    53,    -1,    55,    -1,    -1,    -1,    -1,
      -1,    -1,     3,     4,     5,    20,     7,     8,    23,    24,
      25,    26,    -1,    73,    -1,    -1,    76,    -1,    78,    20,
      80,    -1,    23,    24,    25,    26,    -1,    -1,    -1,    -1,
      31,    32,    -1,    -1,    -1,    -1,    51,    -1,    53,    -1,
      55,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      51,    -1,    53,    -1,    55,    -1,    -1,    -1,    -1,    -1,
      -1,    76,    -1,    78,    -1,    80,    -1,    -1,    -1,    -1,
      -1,    -1,    73,    -1,    -1,    76,    -1,    78,    -1,    80,
       4,     5,    -1,    -1,    -1,     9,    10,    -1,    -1,    -1,
      -1,    15,    -1,    17,    18,    -1,    20,    -1,    22,    23,
      24,    25,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,     4,     5,
      -1,    -1,    -1,     9,    10,    -1,    -1,    -1,    -1,    15,
      -1,    17,    18,    77,    20,    -1,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,     4,    -1,    -1,     7,
      -1,     9,    10,    -1,    -1,    -1,    -1,    15,    -1,    17,
      18,    77,    20,    -1,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,     4,    -1,    -1,     7,    -1,     9,
      10,    -1,    -1,    -1,    -1,    15,    -1,    17,    18,    -1,
      20,    -1,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,     4,    -1,    -1,     7,    -1,     9,    10,    -1,
      -1,    -1,    -1,    15,    -1,    17,    18,    -1,    20,    -1,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61
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
     119,   120,   122,   129,   130,   131,   132,   133,   134,   135,
     136,   137,   138,     4,    76,    76,     3,     4,     5,     7,
       8,    23,    24,    25,    26,    29,    31,    32,    51,    55,
      73,    76,    78,    80,    95,   142,   143,   144,   145,   146,
     147,   148,   149,   150,   152,   153,   156,    34,   142,     4,
       4,    39,    84,   142,   142,     4,     4,     4,     7,   142,
      95,    95,     4,    76,    95,   121,   128,   142,    51,     4,
      55,    87,   142,     4,    20,    34,    53,    98,    99,   117,
       4,     4,     4,     5,     4,     5,    56,    84,     8,    11,
      12,    13,    14,    62,    78,    80,    87,    93,    96,    83,
      84,    84,    84,    84,    84,    84,    84,    84,    84,    84,
      84,    84,    76,   142,   157,   158,   157,     4,    76,   149,
     145,   150,     4,    76,   149,   142,   167,   167,    76,    87,
     154,    17,    28,    27,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    96,   151,    74,    75,
      78,    87,     3,    55,    76,    38,    62,    82,     4,    90,
      84,    84,    76,    76,    76,   121,    82,    84,    87,    84,
       4,    22,    46,    54,    46,    54,     4,    76,    99,    33,
      76,    84,     4,     9,     4,   117,   142,   142,    97,     4,
       7,     9,    10,    15,    17,    18,    20,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,   164,   142,    62,
      93,     4,     5,    77,   163,   164,   165,    77,    82,    77,
     157,    30,    76,    77,    77,    79,    84,   142,   159,    81,
     165,   157,     4,     7,   164,     4,    84,    92,    95,   100,
     101,   116,   130,   131,   132,   134,   135,   136,   137,   141,
     144,   145,   148,   148,   151,   147,   149,   149,   142,     4,
       7,   164,     4,   142,   142,   142,   142,     4,    38,    82,
      37,    90,    16,   108,     4,   160,   162,   160,   121,    77,
     128,    90,     4,    90,     4,    53,     4,    53,    76,   160,
       4,   118,   160,    90,     5,     4,     5,    33,    79,     6,
      76,    76,   142,   142,    76,    84,    62,    83,    77,    82,
     142,    77,   153,   157,    82,   167,   167,    77,    76,    76,
     155,    90,    84,   147,    79,    76,    76,    77,    84,    34,
      38,   142,     4,   142,    20,   142,    16,    19,   109,    62,
      77,    82,    77,    77,    84,    20,    20,   157,    77,    84,
      77,    20,     9,   118,    81,   157,   157,     4,   166,   124,
     142,   142,    84,   167,    77,   167,    79,    81,   157,   157,
      18,    20,   139,    18,   140,   157,   157,    84,    90,   142,
     142,    84,    38,    84,    40,    17,   142,    84,    21,     3,
       5,    23,    24,    25,    26,    72,    73,   161,    84,     4,
      84,    84,    90,    48,    50,    77,    90,    84,    58,     4,
      84,    77,    77,   142,    77,   125,   123,     4,   163,   142,
      77,    77,    15,    84,   141,    15,    15,    84,   141,    77,
      77,    90,    20,    53,   103,    35,    84,    84,    90,   142,
      84,    84,    17,   110,    84,     3,     3,    90,    62,    90,
      90,    20,    84,    84,    20,    90,    84,    90,    83,     4,
      20,    52,    84,   126,   125,    76,    62,    83,   142,    90,
      84,    84,   142,    90,    84,    20,    33,    84,    95,   142,
      90,    90,   103,    84,   110,    84,    10,    84,    92,   100,
     101,   102,   104,   105,   106,   107,   111,   112,   113,   114,
     115,   116,   119,   120,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,    20,   161,    20,    20,    48,    56,
      20,    20,   142,     4,     5,   127,     4,     4,    20,   166,
     142,   142,    17,    20,    17,    20,    30,    84,    84,    84,
     103,   103,    90,   110,    84,    84,    84,    84,    84,    84,
      84,    84,    84,    84,    84,    84,    84,    44,    44,    48,
      84,    84,    57,    56,    76,    76,    82,    84,    84,    84,
       4,    77,    84,    15,    84,   141,    15,    84,    90,   103,
      84,    84,    84,    84,    84,    77,   165,   160,     5,    90,
      84,    83,    90,    84,    90,    84,    84,   103,    84,    77,
      77,    20,   142,   139,   139,   140,   125,    84,    84,    52,
      20,   125,    90,    84,     4,    20,    20,    84,     4,     4,
      84,    84
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
     121,   121,   123,   122,   124,   122,   125,   125,   125,   126,
     126,   126,   126,   126,   127,   127,   128,   128,   129,   130,
     130,   130,   130,   130,   130,   130,   130,   131,   132,   132,
     133,   134,   135,   136,   136,   137,   137,   138,   138,   139,
     139,   139,   139,   140,   140,   140,   140,   140,   141,   141,
     141,   141,   141,   141,   141,   141,   141,   141,   141,   142,
     143,   143,   144,   144,   145,   145,   146,   146,   146,   147,
     147,   147,   148,   148,   148,   149,   149,   149,   149,   149,
     149,   150,   150,   150,   150,   150,   151,   151,   151,   151,
     151,   151,   151,   151,   151,   151,   152,   152,   152,   152,
     152,   152,   152,   152,   152,   152,   152,   152,   152,   152,
     152,   153,   153,   154,   154,   154,   154,   155,   155,   156,
     156,   157,   157,   158,   158,   159,   159,   160,   160,   161,
     161,   161,   161,   161,   161,   161,   161,   162,   162,   162,
     162,   163,   163,   164,   164,   164,   164,   164,   164,   164,
     164,   164,   164,   164,   164,   164,   164,   164,   164,   164,
     164,   164,   164,   164,   164,   164,   164,   164,   164,   164,
     164,   164,   164,   164,   164,   164,   164,   164,   164,   164,
     164,   164,   164,   164,   164,   164,   164,   164,   164,   164,
     165,   165,   165,   165,   165,   165,   166,   166,   167,   167
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
       1,     3,     0,    11,     0,    10,     0,     2,     2,     3,
      10,    10,     9,     7,     1,     3,     1,     3,     7,     4,
       4,     3,     4,     4,     3,     3,     3,     2,     1,     2,
       2,     2,     2,     1,     2,     1,     2,     6,     6,     3,
       3,     6,     7,     0,     3,     6,     7,     7,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     3,     1,     3,     1,     2,     1,     3,     4,     1,
       3,     3,     1,     3,     3,     1,     2,     2,     2,     4,
       5,     1,     4,     3,     6,     6,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     3,     1,     1,
       2,     4,     1,     1,     1,     1,     1,     3,     3,     5,
       1,     3,     5,     0,     3,     3,     5,     0,     3,     2,
       3,     0,     1,     1,     3,     1,     4,     0,     1,     1,
       2,     2,     1,     1,     1,     1,     1,     1,     3,     3,
       5,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       3,     3,     6,     6,     6,     9,     1,     2,     0,     2
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
#line 575 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2653 "src/parser.tab.c"
        break;

    case YYSYMBOL_STRING: /* STRING  */
#line 575 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2659 "src/parser.tab.c"
        break;

    case YYSYMBOL_LENS_CONTENT: /* LENS_CONTENT  */
#line 575 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2665 "src/parser.tab.c"
        break;

    case YYSYMBOL_QUALIFIED_IDENT: /* QUALIFIED_IDENT  */
#line 575 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2671 "src/parser.tab.c"
        break;

    case YYSYMBOL_MODIFIER_PREFIX: /* MODIFIER_PREFIX  */
#line 575 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2677 "src/parser.tab.c"
        break;

    case YYSYMBOL_program: /* program  */
#line 598 "src/parser.y"
            { (void) ((*yyvaluep).stmt_list); }
#line 2683 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement_list: /* statement_list  */
#line 541 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2689 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement: /* statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2695 "src/parser.tab.c"
        break;

    case YYSYMBOL_assignment: /* assignment  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2701 "src/parser.tab.c"
        break;

    case YYSYMBOL_lvalue: /* lvalue  */
#line 576 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2707 "src/parser.tab.c"
        break;

    case YYSYMBOL_variable_name: /* variable_name  */
#line 575 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2713 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_lens: /* comparison_lens  */
#line 583 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 2719 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_name: /* modifier_name  */
#line 575 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2725 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_word: /* modifier_word  */
#line 575 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2731 "src/parser.tab.c"
        break;

    case YYSYMBOL_print_statement: /* print_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2737 "src/parser.tab.c"
        break;

    case YYSYMBOL_call_statement: /* call_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2743 "src/parser.tab.c"
        break;

    case YYSYMBOL_with_lock_statement: /* with_lock_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2749 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_end: /* for_end  */
#line 575 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2755 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_each_statement: /* for_each_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2761 "src/parser.tab.c"
        break;

    case YYSYMBOL_do_loop_statement: /* do_loop_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2767 "src/parser.tab.c"
        break;

    case YYSYMBOL_while_statement: /* while_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2773 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement: /* consider_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2779 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_branch_list: /* consider_branch_list  */
#line 581 "src/parser.y"
            { ast_free_consider_branch_list(((*yyvaluep).consider_branch_list)); }
#line 2785 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_else_opt: /* consider_else_opt  */
#line 541 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2791 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement_list: /* consider_statement_list  */
#line 541 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2797 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_body_statement: /* consider_body_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2803 "src/parser.tab.c"
        break;

    case YYSYMBOL_function_statement: /* function_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2809 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_statement: /* modifier_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2815 "src/parser.tab.c"
        break;

    case YYSYMBOL_program_statement: /* program_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2821 "src/parser.tab.c"
        break;

    case YYSYMBOL_library_statement: /* library_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2827 "src/parser.tab.c"
        break;

    case YYSYMBOL_use_statement: /* use_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2833 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_signature: /* modifier_signature  */
#line 584 "src/parser.y"
            { ast_free_modifier_signature(((*yyvaluep).modifier_signature)); }
#line 2839 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_context: /* modifier_context  */
#line 575 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2845 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_statement: /* watch_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2851 "src/parser.tab.c"
        break;

    case YYSYMBOL_unwatch_statement: /* unwatch_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2857 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_list: /* watch_target_list  */
#line 582 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2863 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_statement: /* server_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2869 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item_list: /* server_item_list  */
#line 588 "src/parser.y"
            { ast_free_server_item_list(((*yyvaluep).server_item_list)); }
#line 2875 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item: /* server_item  */
#line 587 "src/parser.y"
            { AstServerItemList one = ast_server_item_list_append(ast_server_item_list_empty(), ((*yyvaluep).server_item)); ast_free_server_item_list(one); }
#line 2881 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_string_list: /* server_string_list  */
#line 582 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2887 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_path: /* watch_target_path  */
#line 575 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2893 "src/parser.tab.c"
        break;

    case YYSYMBOL_without_watchers_statement: /* without_watchers_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2899 "src/parser.tab.c"
        break;

    case YYSYMBOL_on_error_statement: /* on_error_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2905 "src/parser.tab.c"
        break;

    case YYSYMBOL_error_statement: /* error_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2911 "src/parser.tab.c"
        break;

    case YYSYMBOL_return_statement: /* return_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2917 "src/parser.tab.c"
        break;

    case YYSYMBOL_label_statement: /* label_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2923 "src/parser.tab.c"
        break;

    case YYSYMBOL_goto_statement: /* goto_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2929 "src/parser.tab.c"
        break;

    case YYSYMBOL_gosub_statement: /* gosub_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2935 "src/parser.tab.c"
        break;

    case YYSYMBOL_break_statement: /* break_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2941 "src/parser.tab.c"
        break;

    case YYSYMBOL_continue_statement: /* continue_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2947 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_statement: /* if_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2953 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_block_tail: /* if_block_tail  */
#line 541 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2959 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_inline_tail: /* if_inline_tail  */
#line 541 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2965 "src/parser.tab.c"
        break;

    case YYSYMBOL_inline_statement: /* inline_statement  */
#line 577 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2971 "src/parser.tab.c"
        break;

    case YYSYMBOL_expression: /* expression  */
#line 576 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2977 "src/parser.tab.c"
        break;

    case YYSYMBOL_or_expression: /* or_expression  */
#line 576 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2983 "src/parser.tab.c"
        break;

    case YYSYMBOL_and_expression: /* and_expression  */
#line 576 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2989 "src/parser.tab.c"
        break;

    case YYSYMBOL_not_expression: /* not_expression  */
#line 576 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2995 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_expression: /* comparison_expression  */
#line 576 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3001 "src/parser.tab.c"
        break;

    case YYSYMBOL_additive_expression: /* additive_expression  */
#line 576 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3007 "src/parser.tab.c"
        break;

    case YYSYMBOL_multiplicative_expression: /* multiplicative_expression  */
#line 576 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3013 "src/parser.tab.c"
        break;

    case YYSYMBOL_unary_expression: /* unary_expression  */
#line 576 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3019 "src/parser.tab.c"
        break;

    case YYSYMBOL_postfix_expression: /* postfix_expression  */
#line 576 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3025 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_operator: /* comparison_operator  */
#line 575 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3031 "src/parser.tab.c"
        break;

    case YYSYMBOL_primary: /* primary  */
#line 576 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3037 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_literal: /* record_literal  */
#line 576 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3043 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_suffix: /* ident_suffix  */
#line 585 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3049 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_dot_suffix: /* ident_dot_suffix  */
#line 585 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3055 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list_opt: /* argument_list_opt  */
#line 579 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3061 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list: /* argument_list  */
#line 579 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3067 "src/parser.tab.c"
        break;

    case YYSYMBOL_array_argument_list: /* array_argument_list  */
#line 579 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3073 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list_opt: /* parameter_list_opt  */
#line 582 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3079 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_default: /* parameter_default  */
#line 576 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3085 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list: /* parameter_list  */
#line 582 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3091 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_name: /* field_name  */
#line 575 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3097 "src/parser.tab.c"
        break;

    case YYSYMBOL_dot_field_name: /* dot_field_name  */
#line 575 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3103 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_field_list: /* record_field_list  */
#line 580 "src/parser.y"
            { ast_free_record_field_list(((*yyvaluep).record_field_list)); }
#line 3109 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_policy: /* field_policy  */
#line 586 "src/parser.y"
            { ast_free_expr(((*yyvaluep).field_policy).reset_expr); }
#line 3115 "src/parser.tab.c"
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
#line 603 "src/parser.y"
                     { ctx->parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3421 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 607 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3427 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 608 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3433 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 609 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3439 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 613 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3445 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 614 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3451 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 615 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3457 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 616 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3463 "src/parser.tab.c"
    break;

  case 10: /* statement: for_each_statement  */
#line 617 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3469 "src/parser.tab.c"
    break;

  case 11: /* statement: while_statement  */
#line 618 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3475 "src/parser.tab.c"
    break;

  case 12: /* statement: do_loop_statement  */
#line 619 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3481 "src/parser.tab.c"
    break;

  case 13: /* statement: consider_statement  */
#line 620 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3487 "src/parser.tab.c"
    break;

  case 14: /* statement: function_statement  */
#line 621 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3493 "src/parser.tab.c"
    break;

  case 15: /* statement: modifier_statement  */
#line 622 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3499 "src/parser.tab.c"
    break;

  case 16: /* statement: program_statement  */
#line 623 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3505 "src/parser.tab.c"
    break;

  case 17: /* statement: library_statement  */
#line 624 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3511 "src/parser.tab.c"
    break;

  case 18: /* statement: use_statement NEWLINE  */
#line 625 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3517 "src/parser.tab.c"
    break;

  case 19: /* statement: watch_statement  */
#line 626 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3523 "src/parser.tab.c"
    break;

  case 20: /* statement: server_statement  */
#line 627 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3529 "src/parser.tab.c"
    break;

  case 21: /* statement: unwatch_statement NEWLINE  */
#line 628 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3535 "src/parser.tab.c"
    break;

  case 22: /* statement: without_watchers_statement  */
#line 629 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3541 "src/parser.tab.c"
    break;

  case 23: /* statement: on_error_statement NEWLINE  */
#line 630 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3547 "src/parser.tab.c"
    break;

  case 24: /* statement: error_statement NEWLINE  */
#line 631 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3553 "src/parser.tab.c"
    break;

  case 25: /* statement: return_statement NEWLINE  */
#line 632 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3559 "src/parser.tab.c"
    break;

  case 26: /* statement: label_statement NEWLINE  */
#line 633 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3565 "src/parser.tab.c"
    break;

  case 27: /* statement: goto_statement NEWLINE  */
#line 634 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3571 "src/parser.tab.c"
    break;

  case 28: /* statement: gosub_statement NEWLINE  */
#line 635 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3577 "src/parser.tab.c"
    break;

  case 29: /* statement: break_statement NEWLINE  */
#line 636 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3583 "src/parser.tab.c"
    break;

  case 30: /* statement: continue_statement NEWLINE  */
#line 637 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3589 "src/parser.tab.c"
    break;

  case 31: /* statement: if_statement  */
#line 638 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3595 "src/parser.tab.c"
    break;

  case 32: /* statement: DIM  */
#line 644 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 3608 "src/parser.tab.c"
    break;

  case 33: /* assignment: lvalue OP_EQ expression  */
#line 655 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 3614 "src/parser.tab.c"
    break;

  case 34: /* assignment: lvalue compound_op expression  */
#line 660 "src/parser.y"
                                    { (yyval.stmt) = ast_assign_op((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr), (yyvsp[-1].op_char)); }
#line 3620 "src/parser.tab.c"
    break;

  case 35: /* assignment: lvalue comparison_lens compound_op expression  */
#line 661 "src/parser.y"
                                                    {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign_op((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr), (yyvsp[-1].op_char));
      }
#line 3634 "src/parser.tab.c"
    break;

  case 36: /* assignment: lvalue comparison_lens OP_EQ expression  */
#line 673 "src/parser.y"
                                              {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 3648 "src/parser.tab.c"
    break;

  case 37: /* compound_op: PLUS_EQ  */
#line 685 "src/parser.y"
               { (yyval.op_char) = '+'; }
#line 3654 "src/parser.tab.c"
    break;

  case 38: /* compound_op: MINUS_EQ  */
#line 686 "src/parser.y"
               { (yyval.op_char) = '-'; }
#line 3660 "src/parser.tab.c"
    break;

  case 39: /* compound_op: STAR_EQ  */
#line 687 "src/parser.y"
               { (yyval.op_char) = '*'; }
#line 3666 "src/parser.tab.c"
    break;

  case 40: /* compound_op: SLASH_EQ  */
#line 688 "src/parser.y"
               { (yyval.op_char) = '/'; }
#line 3672 "src/parser.tab.c"
    break;

  case 41: /* lvalue: variable_name  */
#line 692 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3678 "src/parser.tab.c"
    break;

  case 42: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 693 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3684 "src/parser.tab.c"
    break;

  case 43: /* lvalue: lvalue DOT dot_field_name  */
#line 694 "src/parser.y"
                                             { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3690 "src/parser.tab.c"
    break;

  case 44: /* variable_name: IDENT  */
#line 698 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 3696 "src/parser.tab.c"
    break;

  case 45: /* variable_name: END  */
#line 699 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 3702 "src/parser.tab.c"
    break;

  case 46: /* variable_name: NEXT  */
#line 700 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 3708 "src/parser.tab.c"
    break;

  case 47: /* $@1: %empty  */
#line 711 "src/parser.y"
             { lexer_begin_lens_content(ctx->active_lexer); }
#line 3714 "src/parser.tab.c"
    break;

  case 48: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 711 "src/parser.y"
                                                                                  {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 3722 "src/parser.tab.c"
    break;

  case 49: /* comparison_lens: MODIFIER_PREFIX  */
#line 719 "src/parser.y"
                      { (yyval.modifier) = parse_modifier_use((yyvsp[0].text)); }
#line 3728 "src/parser.tab.c"
    break;

  case 50: /* modifier_name: modifier_word  */
#line 723 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 3734 "src/parser.tab.c"
    break;

  case 51: /* modifier_name: modifier_name modifier_word  */
#line 724 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 3740 "src/parser.tab.c"
    break;

  case 52: /* modifier_word: IDENT  */
#line 728 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 3746 "src/parser.tab.c"
    break;

  case 53: /* modifier_word: TO  */
#line 729 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 3752 "src/parser.tab.c"
    break;

  case 54: /* modifier_word: END  */
#line 730 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 3758 "src/parser.tab.c"
    break;

  case 55: /* modifier_word: NEXT  */
#line 731 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 3764 "src/parser.tab.c"
    break;

  case 56: /* print_statement: PRINT expression  */
#line 735 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 3770 "src/parser.tab.c"
    break;

  case 57: /* print_statement: PRINT TO ERROR_VALUE expression  */
#line 741 "src/parser.y"
                                      { (yyval.stmt) = ast_print_error((yyvsp[0].expr)); }
#line 3776 "src/parser.tab.c"
    break;

  case 58: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 745 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 3782 "src/parser.tab.c"
    break;

  case 59: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 746 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 3793 "src/parser.tab.c"
    break;

  case 60: /* call_statement: lvalue DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 752 "src/parser.y"
                                                       {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 3803 "src/parser.tab.c"
    break;

  case 61: /* call_statement: lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 757 "src/parser.y"
                                                                 {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 3817 "src/parser.tab.c"
    break;

  case 62: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 766 "src/parser.y"
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
#line 3832 "src/parser.tab.c"
    break;

  case 63: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 779 "src/parser.y"
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
#line 3855 "src/parser.tab.c"
    break;

  case 64: /* for_end: END FOR NEWLINE  */
#line 808 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 3861 "src/parser.tab.c"
    break;

  case 65: /* for_end: NEXT NEWLINE  */
#line 809 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 3867 "src/parser.tab.c"
    break;

  case 66: /* for_end: NEXT variable_name NEWLINE  */
#line 810 "src/parser.y"
                                 { (yyval.text) = (yyvsp[-1].text); }
#line 3873 "src/parser.tab.c"
    break;

  case 67: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list for_end  */
#line 814 "src/parser.y"
                                                             {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3882 "src/parser.tab.c"
    break;

  case 68: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list for_end  */
#line 818 "src/parser.y"
                                                                  {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3891 "src/parser.tab.c"
    break;

  case 69: /* for_each_statement: FOR IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 834 "src/parser.y"
                                                                         {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { YYERROR; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3901 "src/parser.tab.c"
    break;

  case 70: /* for_each_statement: FOR EACH IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 839 "src/parser.y"
                                                                              {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { YYERROR; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3911 "src/parser.tab.c"
    break;

  case 71: /* for_each_statement: FOR IDENT OP_EQ expression TO expression NEWLINE statement_list for_end  */
#line 847 "src/parser.y"
                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].expr), NULL, (yyvsp[-1].stmt_list));
      }
#line 3920 "src/parser.tab.c"
    break;

  case 72: /* for_each_statement: FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list for_end  */
#line 851 "src/parser.y"
                                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-9].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-9].text), (yyvsp[-7].expr), (yyvsp[-5].expr), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3929 "src/parser.tab.c"
    break;

  case 73: /* do_loop_statement: DO NEWLINE statement_list UNTIL expression NEWLINE  */
#line 872 "src/parser.y"
                                                         {
        (yyval.stmt) = ast_do_loop((yyvsp[-3].stmt_list), (yyvsp[-1].expr));
      }
#line 3937 "src/parser.tab.c"
    break;

  case 74: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 878 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 3945 "src/parser.tab.c"
    break;

  case 75: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 884 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 3953 "src/parser.tab.c"
    break;

  case 76: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 890 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 3961 "src/parser.tab.c"
    break;

  case 77: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 893 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 3969 "src/parser.tab.c"
    break;

  case 78: /* consider_else_opt: %empty  */
#line 899 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3975 "src/parser.tab.c"
    break;

  case 79: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 900 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3981 "src/parser.tab.c"
    break;

  case 80: /* consider_statement_list: %empty  */
#line 904 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3987 "src/parser.tab.c"
    break;

  case 81: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 905 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3993 "src/parser.tab.c"
    break;

  case 82: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 906 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3999 "src/parser.tab.c"
    break;

  case 83: /* consider_body_statement: assignment NEWLINE  */
#line 910 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4005 "src/parser.tab.c"
    break;

  case 84: /* consider_body_statement: print_statement NEWLINE  */
#line 911 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4011 "src/parser.tab.c"
    break;

  case 85: /* consider_body_statement: call_statement NEWLINE  */
#line 912 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4017 "src/parser.tab.c"
    break;

  case 86: /* consider_body_statement: with_lock_statement  */
#line 913 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4023 "src/parser.tab.c"
    break;

  case 87: /* consider_body_statement: for_each_statement  */
#line 914 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4029 "src/parser.tab.c"
    break;

  case 88: /* consider_body_statement: while_statement  */
#line 915 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4035 "src/parser.tab.c"
    break;

  case 89: /* consider_body_statement: do_loop_statement  */
#line 916 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4041 "src/parser.tab.c"
    break;

  case 90: /* consider_body_statement: consider_statement  */
#line 917 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4047 "src/parser.tab.c"
    break;

  case 91: /* consider_body_statement: function_statement  */
#line 918 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4053 "src/parser.tab.c"
    break;

  case 92: /* consider_body_statement: modifier_statement  */
#line 919 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4059 "src/parser.tab.c"
    break;

  case 93: /* consider_body_statement: program_statement  */
#line 920 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4065 "src/parser.tab.c"
    break;

  case 94: /* consider_body_statement: library_statement  */
#line 921 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4071 "src/parser.tab.c"
    break;

  case 95: /* consider_body_statement: use_statement NEWLINE  */
#line 922 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4077 "src/parser.tab.c"
    break;

  case 96: /* consider_body_statement: watch_statement  */
#line 923 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4083 "src/parser.tab.c"
    break;

  case 97: /* consider_body_statement: unwatch_statement NEWLINE  */
#line 924 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4089 "src/parser.tab.c"
    break;

  case 98: /* consider_body_statement: without_watchers_statement  */
#line 925 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4095 "src/parser.tab.c"
    break;

  case 99: /* consider_body_statement: on_error_statement NEWLINE  */
#line 926 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4101 "src/parser.tab.c"
    break;

  case 100: /* consider_body_statement: error_statement NEWLINE  */
#line 927 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4107 "src/parser.tab.c"
    break;

  case 101: /* consider_body_statement: return_statement NEWLINE  */
#line 928 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4113 "src/parser.tab.c"
    break;

  case 102: /* consider_body_statement: label_statement NEWLINE  */
#line 929 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4119 "src/parser.tab.c"
    break;

  case 103: /* consider_body_statement: goto_statement NEWLINE  */
#line 930 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4125 "src/parser.tab.c"
    break;

  case 104: /* consider_body_statement: gosub_statement NEWLINE  */
#line 931 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4131 "src/parser.tab.c"
    break;

  case 105: /* consider_body_statement: break_statement NEWLINE  */
#line 932 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4137 "src/parser.tab.c"
    break;

  case 106: /* consider_body_statement: continue_statement NEWLINE  */
#line 933 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4143 "src/parser.tab.c"
    break;

  case 107: /* consider_body_statement: if_statement  */
#line 934 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4149 "src/parser.tab.c"
    break;

  case 108: /* consider_body_statement: DIM  */
#line 940 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 4162 "src/parser.tab.c"
    break;

  case 109: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 951 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4170 "src/parser.tab.c"
    break;

  case 110: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 954 "src/parser.y"
                                                                                                            {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4179 "src/parser.tab.c"
    break;

  case 111: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 961 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 4187 "src/parser.tab.c"
    break;

  case 112: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 964 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 4195 "src/parser.tab.c"
    break;

  case 113: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 970 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4203 "src/parser.tab.c"
    break;

  case 114: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 976 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 4211 "src/parser.tab.c"
    break;

  case 115: /* use_statement: USE IDENT  */
#line 982 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4217 "src/parser.tab.c"
    break;

  case 116: /* use_statement: LOAD IDENT  */
#line 983 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4223 "src/parser.tab.c"
    break;

  case 117: /* use_statement: USE STRING  */
#line 984 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4229 "src/parser.tab.c"
    break;

  case 118: /* use_statement: LOAD STRING  */
#line 985 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4235 "src/parser.tab.c"
    break;

  case 119: /* use_statement: LOAD IDENT AS IDENT  */
#line 986 "src/parser.y"
                          { (yyval.stmt) = ast_use((yyvsp[-2].text), NULL, (yyvsp[0].text)); }
#line 4241 "src/parser.tab.c"
    break;

  case 120: /* use_statement: USE IDENT IDENT STRING  */
#line 987 "src/parser.y"
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
#line 4262 "src/parser.tab.c"
    break;

  case 121: /* use_statement: LOAD IDENT IDENT STRING  */
#line 1003 "src/parser.y"
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
#line 4283 "src/parser.tab.c"
    break;

  case 122: /* use_statement: LOAD IDENT IDENT STRING AS IDENT  */
#line 1019 "src/parser.y"
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
#line 4306 "src/parser.tab.c"
    break;

  case 123: /* modifier_signature: modifier_name  */
#line 1040 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 4312 "src/parser.tab.c"
    break;

  case 124: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 1041 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 4318 "src/parser.tab.c"
    break;

  case 125: /* modifier_context: IDENT  */
#line 1045 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4324 "src/parser.tab.c"
    break;

  case 126: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1049 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4332 "src/parser.tab.c"
    break;

  case 127: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 1052 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 4340 "src/parser.tab.c"
    break;

  case 128: /* watch_statement: WATCH IDENT LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1060 "src/parser.y"
                                                                                           {
        (yyval.stmt) = ast_watch((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4348 "src/parser.tab.c"
    break;

  case 129: /* unwatch_statement: UNWATCH expression  */
#line 1066 "src/parser.y"
                         { (yyval.stmt) = ast_unwatch((yyvsp[0].expr)); }
#line 4354 "src/parser.tab.c"
    break;

  case 130: /* watch_target_list: watch_target_path  */
#line 1070 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4360 "src/parser.tab.c"
    break;

  case 131: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 1071 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4366 "src/parser.tab.c"
    break;

  case 132: /* @2: %empty  */
#line 1091 "src/parser.y"
      { (yyval.stmt) = NULL;   /* the mid-rule carries no value; typed so bison stays quiet */
        server_head_note(ctx, (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column); }
#line 4373 "src/parser.tab.c"
    break;

  case 133: /* server_statement: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE @2 server_item_list END IDENT NEWLINE  */
#line 1093 "src/parser.y"
                                         {
        ctx->bad_block_word[0] = '\0';
        (yyval.stmt) = ast_server((yyvsp[-10].text), (yyvsp[-9].text), (yyvsp[-7].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4382 "src/parser.tab.c"
    break;

  case 134: /* @3: %empty  */
#line 1098 "src/parser.y"
      { (yyval.stmt) = NULL;   /* the mid-rule carries no value; typed so bison stays quiet */
        server_head_note(ctx, (yyvsp[-4].text), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4389 "src/parser.tab.c"
    break;

  case 135: /* server_statement: IDENT IDENT LPAREN RPAREN NEWLINE @3 server_item_list END IDENT NEWLINE  */
#line 1100 "src/parser.y"
                                         {
        ctx->bad_block_word[0] = '\0';
        (yyval.stmt) = ast_server((yyvsp[-9].text), (yyvsp[-8].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4398 "src/parser.tab.c"
    break;

  case 136: /* server_item_list: %empty  */
#line 1107 "src/parser.y"
             { (yyval.server_item_list) = ast_server_item_list_empty(); }
#line 4404 "src/parser.tab.c"
    break;

  case 137: /* server_item_list: server_item_list NEWLINE  */
#line 1108 "src/parser.y"
                               { (yyval.server_item_list) = (yyvsp[-1].server_item_list); }
#line 4410 "src/parser.tab.c"
    break;

  case 138: /* server_item_list: server_item_list server_item  */
#line 1109 "src/parser.y"
                                   { (yyval.server_item_list) = ast_server_item_list_append((yyvsp[-1].server_item_list), (yyvsp[0].server_item)); }
#line 4416 "src/parser.tab.c"
    break;

  case 139: /* server_item: IDENT server_string_list NEWLINE  */
#line 1113 "src/parser.y"
                                       {
        (yyval.server_item) = ast_server_directive((yyvsp[-2].text), (yyvsp[-1].name_list), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 4424 "src/parser.tab.c"
    break;

  case 140: /* server_item: IDENT STRING LPAREN parameter_list_opt RPAREN NEWLINE statement_list END IDENT NEWLINE  */
#line 1116 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_handler((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4432 "src/parser.tab.c"
    break;

  case 141: /* server_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1119 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_site((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4440 "src/parser.tab.c"
    break;

  case 142: /* server_item: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1122 "src/parser.y"
                                                                           {
        (yyval.server_item) = ast_server_site((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 4448 "src/parser.tab.c"
    break;

  case 143: /* server_item: ON IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1125 "src/parser.y"
                                                     {
        (yyval.server_item) = ast_server_hook((yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 4456 "src/parser.tab.c"
    break;

  case 144: /* server_string_list: STRING  */
#line 1131 "src/parser.y"
             { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4462 "src/parser.tab.c"
    break;

  case 145: /* server_string_list: server_string_list COMMA STRING  */
#line 1132 "src/parser.y"
                                      { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4468 "src/parser.tab.c"
    break;

  case 146: /* watch_target_path: variable_name  */
#line 1136 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 4474 "src/parser.tab.c"
    break;

  case 147: /* watch_target_path: watch_target_path DOT IDENT  */
#line 1137 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 4480 "src/parser.tab.c"
    break;

  case 148: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 1141 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 4488 "src/parser.tab.c"
    break;

  case 149: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 1147 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 4494 "src/parser.tab.c"
    break;

  case 150: /* on_error_statement: ON ERROR_VALUE GOTO NEXT  */
#line 1148 "src/parser.y"
                               { (yyval.stmt) = ast_on_error_goto_next(); }
#line 4500 "src/parser.tab.c"
    break;

  case 151: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 1149 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 4506 "src/parser.tab.c"
    break;

  case 152: /* on_error_statement: ON IDENT GOTO NEXT  */
#line 1150 "src/parser.y"
                         {
        if (!warn_channel_ok(ctx, (yyvsp[-2].text), (yylsp[-2]).first_line, (yylsp[-2]).first_column)) { YYERROR; }
        free((yyvsp[-2].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_NEXT);
      }
#line 4516 "src/parser.tab.c"
    break;

  case 153: /* on_error_statement: ON IDENT GOTO IDENT  */
#line 1155 "src/parser.y"
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
#line 4534 "src/parser.tab.c"
    break;

  case 154: /* on_error_statement: ON IDENT STOP  */
#line 1168 "src/parser.y"
                    {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_STOP);
      }
#line 4544 "src/parser.tab.c"
    break;

  case 155: /* on_error_statement: ON IDENT PRINT  */
#line 1173 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_PRINT);
      }
#line 4554 "src/parser.tab.c"
    break;

  case 156: /* on_error_statement: ON IDENT IDENT  */
#line 1178 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        int mode = warn_mode_word(ctx, (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column);
        if (mode < 0) { free((yyvsp[-1].text)); free((yyvsp[0].text)); YYERROR; }
        free((yyvsp[-1].text)); free((yyvsp[0].text));
        (yyval.stmt) = ast_on_warning(mode);
      }
#line 4566 "src/parser.tab.c"
    break;

  case 157: /* error_statement: ERROR_VALUE expression  */
#line 1188 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 4572 "src/parser.tab.c"
    break;

  case 158: /* return_statement: RETURN  */
#line 1192 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 4578 "src/parser.tab.c"
    break;

  case 159: /* return_statement: RETURN expression  */
#line 1193 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 4584 "src/parser.tab.c"
    break;

  case 160: /* label_statement: variable_name COLON  */
#line 1197 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 4590 "src/parser.tab.c"
    break;

  case 161: /* goto_statement: GOTO variable_name  */
#line 1204 "src/parser.y"
                         { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 4596 "src/parser.tab.c"
    break;

  case 162: /* gosub_statement: GOSUB variable_name  */
#line 1208 "src/parser.y"
                          { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 4602 "src/parser.tab.c"
    break;

  case 163: /* break_statement: BREAK  */
#line 1217 "src/parser.y"
            { (yyval.stmt) = ast_break(NULL); }
#line 4608 "src/parser.tab.c"
    break;

  case 164: /* break_statement: BREAK IDENT  */
#line 1218 "src/parser.y"
                  { (yyval.stmt) = ast_break((yyvsp[0].text)); }
#line 4614 "src/parser.tab.c"
    break;

  case 165: /* continue_statement: CONTINUE  */
#line 1222 "src/parser.y"
               { (yyval.stmt) = ast_continue(NULL); }
#line 4620 "src/parser.tab.c"
    break;

  case 166: /* continue_statement: CONTINUE IDENT  */
#line 1223 "src/parser.y"
                     { (yyval.stmt) = ast_continue((yyvsp[0].text)); }
#line 4626 "src/parser.tab.c"
    break;

  case 167: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1227 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4635 "src/parser.tab.c"
    break;

  case 168: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1231 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4644 "src/parser.tab.c"
    break;

  case 169: /* if_block_tail: END IF NEWLINE  */
#line 1238 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4652 "src/parser.tab.c"
    break;

  case 170: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 1241 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4660 "src/parser.tab.c"
    break;

  case 171: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1244 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4668 "src/parser.tab.c"
    break;

  case 172: /* if_block_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1253 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 4680 "src/parser.tab.c"
    break;

  case 173: /* if_inline_tail: %empty  */
#line 1263 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4688 "src/parser.tab.c"
    break;

  case 174: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 1266 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4696 "src/parser.tab.c"
    break;

  case 175: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1269 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4704 "src/parser.tab.c"
    break;

  case 176: /* if_inline_tail: ELSE IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1275 "src/parser.y"
                                                                      {
        AstStmt *inner = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 4716 "src/parser.tab.c"
    break;

  case 177: /* if_inline_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1282 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 4728 "src/parser.tab.c"
    break;

  case 178: /* inline_statement: assignment  */
#line 1292 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4734 "src/parser.tab.c"
    break;

  case 179: /* inline_statement: print_statement  */
#line 1293 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4740 "src/parser.tab.c"
    break;

  case 180: /* inline_statement: call_statement  */
#line 1294 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4746 "src/parser.tab.c"
    break;

  case 181: /* inline_statement: use_statement  */
#line 1295 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4752 "src/parser.tab.c"
    break;

  case 182: /* inline_statement: on_error_statement  */
#line 1296 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4758 "src/parser.tab.c"
    break;

  case 183: /* inline_statement: error_statement  */
#line 1297 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4764 "src/parser.tab.c"
    break;

  case 184: /* inline_statement: return_statement  */
#line 1298 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4770 "src/parser.tab.c"
    break;

  case 185: /* inline_statement: goto_statement  */
#line 1299 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4776 "src/parser.tab.c"
    break;

  case 186: /* inline_statement: gosub_statement  */
#line 1300 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4782 "src/parser.tab.c"
    break;

  case 187: /* inline_statement: break_statement  */
#line 1301 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4788 "src/parser.tab.c"
    break;

  case 188: /* inline_statement: continue_statement  */
#line 1302 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4794 "src/parser.tab.c"
    break;

  case 189: /* expression: or_expression  */
#line 1306 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 4800 "src/parser.tab.c"
    break;

  case 190: /* or_expression: and_expression  */
#line 1310 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 4806 "src/parser.tab.c"
    break;

  case 191: /* or_expression: or_expression OR and_expression  */
#line 1311 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4812 "src/parser.tab.c"
    break;

  case 192: /* and_expression: not_expression  */
#line 1315 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 4818 "src/parser.tab.c"
    break;

  case 193: /* and_expression: and_expression AND not_expression  */
#line 1316 "src/parser.y"
                                        { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4824 "src/parser.tab.c"
    break;

  case 194: /* not_expression: comparison_expression  */
#line 1338 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 4830 "src/parser.tab.c"
    break;

  case 195: /* not_expression: NOT not_expression  */
#line 1339 "src/parser.y"
                         { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4836 "src/parser.tab.c"
    break;

  case 196: /* comparison_expression: additive_expression  */
#line 1343 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 4842 "src/parser.tab.c"
    break;

  case 197: /* comparison_expression: additive_expression comparison_operator additive_expression  */
#line 1344 "src/parser.y"
                                                                  { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4848 "src/parser.tab.c"
    break;

  case 198: /* comparison_expression: additive_expression comparison_lens comparison_operator additive_expression  */
#line 1345 "src/parser.y"
                                                                                  {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 4856 "src/parser.tab.c"
    break;

  case 199: /* additive_expression: multiplicative_expression  */
#line 1351 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 4862 "src/parser.tab.c"
    break;

  case 200: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 1352 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4868 "src/parser.tab.c"
    break;

  case 201: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 1353 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4874 "src/parser.tab.c"
    break;

  case 202: /* multiplicative_expression: unary_expression  */
#line 1357 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 4880 "src/parser.tab.c"
    break;

  case 203: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 1358 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4886 "src/parser.tab.c"
    break;

  case 204: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 1359 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4892 "src/parser.tab.c"
    break;

  case 205: /* unary_expression: postfix_expression  */
#line 1363 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 4898 "src/parser.tab.c"
    break;

  case 206: /* unary_expression: MINUS unary_expression  */
#line 1364 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4904 "src/parser.tab.c"
    break;

  case 207: /* unary_expression: MODIFIER_PREFIX unary_expression  */
#line 1371 "src/parser.y"
                                       {
        (yyval.expr) = expr_at(ast_modifier_apply(parse_modifier_use((yyvsp[-1].text)), (yyvsp[0].expr)),
                     (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 4913 "src/parser.tab.c"
    break;

  case 208: /* unary_expression: NEW postfix_expression  */
#line 1375 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4919 "src/parser.tab.c"
    break;

  case 209: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 1376 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 4925 "src/parser.tab.c"
    break;

  case 210: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 1377 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4931 "src/parser.tab.c"
    break;

  case 211: /* postfix_expression: primary  */
#line 1381 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 4937 "src/parser.tab.c"
    break;

  case 212: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 1382 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4943 "src/parser.tab.c"
    break;

  case 213: /* postfix_expression: postfix_expression DOT dot_field_name  */
#line 1383 "src/parser.y"
                                            { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4949 "src/parser.tab.c"
    break;

  case 214: /* postfix_expression: postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1384 "src/parser.y"
                                                                   {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        (yyval.expr) = expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 4959 "src/parser.tab.c"
    break;

  case 215: /* postfix_expression: postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1389 "src/parser.y"
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
#line 4975 "src/parser.tab.c"
    break;

  case 216: /* comparison_operator: OP_EQ  */
#line 1403 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 4981 "src/parser.tab.c"
    break;

  case 217: /* comparison_operator: OP_NE  */
#line 1404 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 4987 "src/parser.tab.c"
    break;

  case 218: /* comparison_operator: OP_GT  */
#line 1405 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 4993 "src/parser.tab.c"
    break;

  case 219: /* comparison_operator: OP_LT  */
#line 1406 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 4999 "src/parser.tab.c"
    break;

  case 220: /* comparison_operator: OP_GE  */
#line 1407 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 5005 "src/parser.tab.c"
    break;

  case 221: /* comparison_operator: OP_LE  */
#line 1408 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 5011 "src/parser.tab.c"
    break;

  case 222: /* comparison_operator: OP_NGT  */
#line 1409 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 5017 "src/parser.tab.c"
    break;

  case 223: /* comparison_operator: OP_NLT  */
#line 1410 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 5023 "src/parser.tab.c"
    break;

  case 224: /* comparison_operator: OP_NGE  */
#line 1411 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 5029 "src/parser.tab.c"
    break;

  case 225: /* comparison_operator: OP_NLE  */
#line 1412 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 5035 "src/parser.tab.c"
    break;

  case 226: /* primary: NUMBER  */
#line 1416 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5041 "src/parser.tab.c"
    break;

  case 227: /* primary: WATCHERS LPAREN RPAREN  */
#line 1417 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_call(copy_const("watchers"), ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5047 "src/parser.tab.c"
    break;

  case 228: /* primary: duration_terms  */
#line 1418 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5053 "src/parser.tab.c"
    break;

  case 229: /* primary: STRING  */
#line 1419 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5059 "src/parser.tab.c"
    break;

  case 230: /* primary: variable_name ident_suffix  */
#line 1420 "src/parser.y"
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
#line 5081 "src/parser.tab.c"
    break;

  case 231: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1437 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 5092 "src/parser.tab.c"
    break;

  case 232: /* primary: ERROR_VALUE  */
#line 1443 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5098 "src/parser.tab.c"
    break;

  case 233: /* primary: TRUE  */
#line 1444 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5104 "src/parser.tab.c"
    break;

  case 234: /* primary: FALSE  */
#line 1445 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5110 "src/parser.tab.c"
    break;

  case 235: /* primary: NOTHING  */
#line 1446 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5116 "src/parser.tab.c"
    break;

  case 236: /* primary: UNKNOWN_VALUE  */
#line 1447 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5122 "src/parser.tab.c"
    break;

  case 237: /* primary: LPAREN expression RPAREN  */
#line 1448 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 5128 "src/parser.tab.c"
    break;

  case 238: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 1449 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5134 "src/parser.tab.c"
    break;

  case 239: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 1450 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5140 "src/parser.tab.c"
    break;

  case 240: /* primary: record_literal  */
#line 1451 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5146 "src/parser.tab.c"
    break;

  case 241: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 1455 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5152 "src/parser.tab.c"
    break;

  case 242: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 1456 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5158 "src/parser.tab.c"
    break;

  case 243: /* ident_suffix: %empty  */
#line 1460 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 5168 "src/parser.tab.c"
    break;

  case 244: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 1465 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5178 "src/parser.tab.c"
    break;

  case 245: /* ident_suffix: DOT dot_field_name ident_dot_suffix  */
#line 1470 "src/parser.y"
                                          {
        /* dot_field_name, not IDENT: a keyword is a legal FIELD name after a
         * dot, because nothing but a name can appear there. */
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 5189 "src/parser.tab.c"
    break;

  case 246: /* ident_suffix: DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1476 "src/parser.y"
                                                          {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        (yyval.ident_suffix).kind = IDENT_SUFFIX_METHOD;
        (yyval.ident_suffix).name = (yyvsp[-3].text);
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5203 "src/parser.tab.c"
    break;

  case 247: /* ident_dot_suffix: %empty  */
#line 1488 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 5213 "src/parser.tab.c"
    break;

  case 248: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 1493 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5223 "src/parser.tab.c"
    break;

  case 249: /* duration_terms: NUMBER IDENT  */
#line 1501 "src/parser.y"
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
#line 5238 "src/parser.tab.c"
    break;

  case 250: /* duration_terms: duration_terms NUMBER IDENT  */
#line 1511 "src/parser.y"
                                  {
        char *bad = NULL;
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].number), (yyvsp[0].text), &bad);
        if (bad) {
            duration_unit_error(ctx, bad, (yylsp[0]).first_line, (yylsp[0]).first_column,
                                (yylsp[0]).last_line, (yylsp[0]).last_column);
            YYERROR;
        }
      }
#line 5252 "src/parser.tab.c"
    break;

  case 251: /* argument_list_opt: %empty  */
#line 1523 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 5258 "src/parser.tab.c"
    break;

  case 252: /* argument_list_opt: argument_list  */
#line 1524 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 5264 "src/parser.tab.c"
    break;

  case 253: /* argument_list: expression  */
#line 1528 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 5270 "src/parser.tab.c"
    break;

  case 254: /* argument_list: argument_list COMMA expression  */
#line 1529 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 5276 "src/parser.tab.c"
    break;

  case 255: /* array_argument_list: expression  */
#line 1533 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 5282 "src/parser.tab.c"
    break;

  case 256: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 1534 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 5288 "src/parser.tab.c"
    break;

  case 257: /* parameter_list_opt: %empty  */
#line 1538 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 5294 "src/parser.tab.c"
    break;

  case 258: /* parameter_list_opt: parameter_list  */
#line 1539 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 5300 "src/parser.tab.c"
    break;

  case 259: /* parameter_default: NUMBER  */
#line 1553 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5306 "src/parser.tab.c"
    break;

  case 260: /* parameter_default: MINUS NUMBER  */
#line 1554 "src/parser.y"
                   { (yyval.expr) = expr_at(ast_number(-(yyvsp[0].number)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5312 "src/parser.tab.c"
    break;

  case 261: /* parameter_default: PLUS NUMBER  */
#line 1555 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5318 "src/parser.tab.c"
    break;

  case 262: /* parameter_default: STRING  */
#line 1556 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5324 "src/parser.tab.c"
    break;

  case 263: /* parameter_default: TRUE  */
#line 1557 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5330 "src/parser.tab.c"
    break;

  case 264: /* parameter_default: FALSE  */
#line 1558 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5336 "src/parser.tab.c"
    break;

  case 265: /* parameter_default: NOTHING  */
#line 1559 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5342 "src/parser.tab.c"
    break;

  case 266: /* parameter_default: UNKNOWN_VALUE  */
#line 1560 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5348 "src/parser.tab.c"
    break;

  case 267: /* parameter_list: IDENT  */
#line 1564 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5354 "src/parser.tab.c"
    break;

  case 268: /* parameter_list: IDENT OP_EQ parameter_default  */
#line 1565 "src/parser.y"
                                    {
        (yyval.name_list) = ast_name_list_append_default(ast_name_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 5362 "src/parser.tab.c"
    break;

  case 269: /* parameter_list: parameter_list COMMA IDENT  */
#line 1568 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5368 "src/parser.tab.c"
    break;

  case 270: /* parameter_list: parameter_list COMMA IDENT OP_EQ parameter_default  */
#line 1569 "src/parser.y"
                                                         {
        (yyval.name_list) = ast_name_list_append_default((yyvsp[-4].name_list), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 5376 "src/parser.tab.c"
    break;

  case 271: /* field_name: dot_field_name  */
#line 1584 "src/parser.y"
                     { (yyval.text) = (yyvsp[0].text); }
#line 5382 "src/parser.tab.c"
    break;

  case 272: /* field_name: STRING  */
#line 1591 "src/parser.y"
             { (yyval.text) = (yyvsp[0].text); }
#line 5388 "src/parser.tab.c"
    break;

  case 273: /* dot_field_name: IDENT  */
#line 1600 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 5394 "src/parser.tab.c"
    break;

  case 274: /* dot_field_name: AS  */
#line 1601 "src/parser.y"
                     { (yyval.text) = kw_name("as"); }
#line 5400 "src/parser.tab.c"
    break;

  case 275: /* dot_field_name: NEXT  */
#line 1602 "src/parser.y"
                     { (yyval.text) = kw_name("next"); }
#line 5406 "src/parser.tab.c"
    break;

  case 276: /* dot_field_name: STOP  */
#line 1603 "src/parser.y"
                     { (yyval.text) = kw_name("stop"); }
#line 5412 "src/parser.tab.c"
    break;

  case 277: /* dot_field_name: ERROR_VALUE  */
#line 1604 "src/parser.y"
                     { (yyval.text) = kw_name("error"); }
#line 5418 "src/parser.tab.c"
    break;

  case 278: /* dot_field_name: END  */
#line 1605 "src/parser.y"
                     { (yyval.text) = kw_name("end"); }
#line 5424 "src/parser.tab.c"
    break;

  case 279: /* dot_field_name: TO  */
#line 1606 "src/parser.y"
                     { (yyval.text) = kw_name("to"); }
#line 5430 "src/parser.tab.c"
    break;

  case 280: /* dot_field_name: IN  */
#line 1607 "src/parser.y"
                     { (yyval.text) = kw_name("in"); }
#line 5436 "src/parser.tab.c"
    break;

  case 281: /* dot_field_name: ON  */
#line 1608 "src/parser.y"
                     { (yyval.text) = kw_name("on"); }
#line 5442 "src/parser.tab.c"
    break;

  case 282: /* dot_field_name: NEW  */
#line 1609 "src/parser.y"
                     { (yyval.text) = kw_name("new"); }
#line 5448 "src/parser.tab.c"
    break;

  case 283: /* dot_field_name: EACH  */
#line 1610 "src/parser.y"
                     { (yyval.text) = kw_name("each"); }
#line 5454 "src/parser.tab.c"
    break;

  case 284: /* dot_field_name: WITH  */
#line 1611 "src/parser.y"
                     { (yyval.text) = kw_name("with"); }
#line 5460 "src/parser.tab.c"
    break;

  case 285: /* dot_field_name: WITHOUT  */
#line 1612 "src/parser.y"
                     { (yyval.text) = kw_name("without"); }
#line 5466 "src/parser.tab.c"
    break;

  case 286: /* dot_field_name: THEN  */
#line 1613 "src/parser.y"
                     { (yyval.text) = kw_name("then"); }
#line 5472 "src/parser.tab.c"
    break;

  case 287: /* dot_field_name: ELSE  */
#line 1614 "src/parser.y"
                     { (yyval.text) = kw_name("else"); }
#line 5478 "src/parser.tab.c"
    break;

  case 288: /* dot_field_name: FOR  */
#line 1615 "src/parser.y"
                     { (yyval.text) = kw_name("for"); }
#line 5484 "src/parser.tab.c"
    break;

  case 289: /* dot_field_name: IF  */
#line 1616 "src/parser.y"
                     { (yyval.text) = kw_name("if"); }
#line 5490 "src/parser.tab.c"
    break;

  case 290: /* dot_field_name: WHILE  */
#line 1617 "src/parser.y"
                     { (yyval.text) = kw_name("while"); }
#line 5496 "src/parser.tab.c"
    break;

  case 291: /* dot_field_name: DO  */
#line 1618 "src/parser.y"
                     { (yyval.text) = kw_name("do"); }
#line 5502 "src/parser.tab.c"
    break;

  case 292: /* dot_field_name: UNTIL  */
#line 1619 "src/parser.y"
                     { (yyval.text) = kw_name("until"); }
#line 5508 "src/parser.tab.c"
    break;

  case 293: /* dot_field_name: PRINT  */
#line 1620 "src/parser.y"
                     { (yyval.text) = kw_name("print"); }
#line 5514 "src/parser.tab.c"
    break;

  case 294: /* dot_field_name: RETURN  */
#line 1621 "src/parser.y"
                     { (yyval.text) = kw_name("return"); }
#line 5520 "src/parser.tab.c"
    break;

  case 295: /* dot_field_name: LOAD  */
#line 1622 "src/parser.y"
                     { (yyval.text) = kw_name("load"); }
#line 5526 "src/parser.tab.c"
    break;

  case 296: /* dot_field_name: USE  */
#line 1623 "src/parser.y"
                     { (yyval.text) = kw_name("use"); }
#line 5532 "src/parser.tab.c"
    break;

  case 297: /* dot_field_name: NOT  */
#line 1624 "src/parser.y"
                     { (yyval.text) = kw_name("not"); }
#line 5538 "src/parser.tab.c"
    break;

  case 298: /* dot_field_name: AND  */
#line 1625 "src/parser.y"
                     { (yyval.text) = kw_name("and"); }
#line 5544 "src/parser.tab.c"
    break;

  case 299: /* dot_field_name: OR  */
#line 1626 "src/parser.y"
                     { (yyval.text) = kw_name("or"); }
#line 5550 "src/parser.tab.c"
    break;

  case 300: /* dot_field_name: TRUE  */
#line 1627 "src/parser.y"
                     { (yyval.text) = kw_name("true"); }
#line 5556 "src/parser.tab.c"
    break;

  case 301: /* dot_field_name: FALSE  */
#line 1628 "src/parser.y"
                     { (yyval.text) = kw_name("false"); }
#line 5562 "src/parser.tab.c"
    break;

  case 302: /* dot_field_name: NOTHING  */
#line 1629 "src/parser.y"
                     { (yyval.text) = kw_name("nothing"); }
#line 5568 "src/parser.tab.c"
    break;

  case 303: /* dot_field_name: BREAK  */
#line 1630 "src/parser.y"
                     { (yyval.text) = kw_name("break"); }
#line 5574 "src/parser.tab.c"
    break;

  case 304: /* dot_field_name: CONTINUE  */
#line 1631 "src/parser.y"
                     { (yyval.text) = kw_name("continue"); }
#line 5580 "src/parser.tab.c"
    break;

  case 305: /* dot_field_name: GOTO  */
#line 1632 "src/parser.y"
                     { (yyval.text) = kw_name("goto"); }
#line 5586 "src/parser.tab.c"
    break;

  case 306: /* dot_field_name: GOSUB  */
#line 1633 "src/parser.y"
                     { (yyval.text) = kw_name("gosub"); }
#line 5592 "src/parser.tab.c"
    break;

  case 307: /* dot_field_name: SPAWN  */
#line 1634 "src/parser.y"
                     { (yyval.text) = kw_name("spawn"); }
#line 5598 "src/parser.tab.c"
    break;

  case 308: /* dot_field_name: EXPORT  */
#line 1635 "src/parser.y"
                     { (yyval.text) = kw_name("export"); }
#line 5604 "src/parser.tab.c"
    break;

  case 309: /* dot_field_name: LIBRARY  */
#line 1636 "src/parser.y"
                     { (yyval.text) = kw_name("library"); }
#line 5610 "src/parser.tab.c"
    break;

  case 310: /* dot_field_name: FUNCTION  */
#line 1637 "src/parser.y"
                     { (yyval.text) = kw_name("function"); }
#line 5616 "src/parser.tab.c"
    break;

  case 311: /* dot_field_name: MODIFIER  */
#line 1638 "src/parser.y"
                     { (yyval.text) = kw_name("modifier"); }
#line 5622 "src/parser.tab.c"
    break;

  case 312: /* dot_field_name: PROGRAM  */
#line 1639 "src/parser.y"
                     { (yyval.text) = kw_name("program"); }
#line 5628 "src/parser.tab.c"
    break;

  case 313: /* dot_field_name: WATCH  */
#line 1640 "src/parser.y"
                     { (yyval.text) = kw_name("watch"); }
#line 5634 "src/parser.tab.c"
    break;

  case 314: /* dot_field_name: WATCHERS  */
#line 1641 "src/parser.y"
                     { (yyval.text) = kw_name("watchers"); }
#line 5640 "src/parser.tab.c"
    break;

  case 315: /* dot_field_name: CONSIDER  */
#line 1642 "src/parser.y"
                     { (yyval.text) = kw_name("consider"); }
#line 5646 "src/parser.tab.c"
    break;

  case 316: /* dot_field_name: STEP  */
#line 1643 "src/parser.y"
                     { (yyval.text) = kw_name("step"); }
#line 5652 "src/parser.tab.c"
    break;

  case 317: /* dot_field_name: UNWATCH  */
#line 1644 "src/parser.y"
                     { (yyval.text) = kw_name("unwatch"); }
#line 5658 "src/parser.tab.c"
    break;

  case 318: /* dot_field_name: UNKNOWN_VALUE  */
#line 1645 "src/parser.y"
                     { (yyval.text) = kw_name("unknown"); }
#line 5664 "src/parser.tab.c"
    break;

  case 319: /* dot_field_name: DIM  */
#line 1646 "src/parser.y"
                     { (yyval.text) = kw_name("dim"); }
#line 5670 "src/parser.tab.c"
    break;

  case 320: /* record_field_list: field_name OP_EQ expression  */
#line 1650 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5676 "src/parser.tab.c"
    break;

  case 321: /* record_field_list: field_name COLON expression  */
#line 1651 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5682 "src/parser.tab.c"
    break;

  case 322: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1652 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5688 "src/parser.tab.c"
    break;

  case 323: /* record_field_list: record_field_list COMMA optional_newlines field_name OP_EQ expression  */
#line 1653 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5694 "src/parser.tab.c"
    break;

  case 324: /* record_field_list: record_field_list COMMA optional_newlines field_name COLON expression  */
#line 1654 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5700 "src/parser.tab.c"
    break;

  case 325: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1655 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5706 "src/parser.tab.c"
    break;

  case 326: /* field_policy: IDENT  */
#line 1663 "src/parser.y"
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
#line 5738 "src/parser.tab.c"
    break;

  case 327: /* field_policy: IDENT expression  */
#line 1690 "src/parser.y"
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
#line 5759 "src/parser.tab.c"
    break;


#line 5763 "src/parser.tab.c"

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

#line 1713 "src/parser.y"


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
    ctx.bad_block_word[0] = '\0';
    ctx.bad_block_line = 0;
    ctx.bad_block_column = 0;

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
    /* A BAD BLOCK HEAD OUTRANKS WHATEVER THE BODY TRIPPED ON, because the body
     * is only being read at all on the strength of that head (DOGFOOD 31). */
    char block_message[320];
    if (ctx->bad_block_word[0]) {
        gb_format_unknown_block(block_message, sizeof(block_message),
                                ctx->bad_block_word);
        line = ctx->bad_block_line;
        column = ctx->bad_block_column;
        end_line = line;
        end_column = column + (int)strlen(ctx->bad_block_word);
        message = block_message;
        ctx->bad_block_word[0] = '\0';
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
