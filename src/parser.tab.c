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



#line 455 "src/parser.tab.c"

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
  YYSYMBOL_not_expression = 142,           /* not_expression  */
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
  YYSYMBOL_parameter_default = 158,        /* parameter_default  */
  YYSYMBOL_parameter_list = 159,           /* parameter_list  */
  YYSYMBOL_field_name = 160,               /* field_name  */
  YYSYMBOL_dot_field_name = 161,           /* dot_field_name  */
  YYSYMBOL_record_field_list = 162,        /* record_field_list  */
  YYSYMBOL_field_policy = 163,             /* field_policy  */
  YYSYMBOL_optional_newlines = 164         /* optional_newlines  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;



/* Unqualified %code blocks.  */
#line 454 "src/parser.y"

static int yylex(YYSTYPE *lvalp, YYLTYPE *llocp, gb_parse_ctx *ctx);
static void yyerror(YYLTYPE *llocp, gb_parse_ctx *ctx, const char *message);
static void report_syntax_error(gb_parse_ctx *ctx, int line, int column,
                                int end_line, int end_column, const char *message);

#line 661 "src/parser.tab.c"

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
#define YYLAST   2567

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  87
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  78
/* YYNRULES -- Number of rules.  */
#define YYNRULES  325
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  697

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
       0,   538,   538,   542,   543,   544,   548,   549,   550,   551,
     552,   553,   554,   555,   556,   557,   558,   559,   560,   561,
     562,   563,   564,   565,   566,   567,   568,   569,   570,   571,
     572,   573,   579,   590,   595,   596,   608,   620,   621,   622,
     623,   627,   628,   629,   633,   634,   635,   646,   646,   652,
     653,   657,   658,   659,   660,   664,   670,   674,   675,   681,
     686,   695,   708,   737,   738,   739,   743,   747,   763,   768,
     776,   780,   801,   807,   813,   819,   822,   828,   829,   833,
     834,   835,   839,   840,   841,   842,   843,   844,   845,   846,
     847,   848,   849,   850,   851,   852,   853,   854,   855,   856,
     857,   858,   859,   860,   861,   862,   863,   869,   880,   883,
     890,   893,   899,   905,   911,   912,   913,   914,   915,   916,
     932,   948,   969,   970,   974,   978,   981,   989,   995,   999,
    1000,  1019,  1022,  1028,  1029,  1030,  1034,  1037,  1040,  1043,
    1046,  1052,  1053,  1057,  1058,  1062,  1068,  1069,  1070,  1071,
    1076,  1089,  1094,  1099,  1109,  1113,  1114,  1118,  1125,  1129,
    1138,  1139,  1143,  1144,  1148,  1152,  1159,  1162,  1165,  1174,
    1184,  1187,  1190,  1196,  1203,  1213,  1214,  1215,  1216,  1217,
    1218,  1219,  1220,  1221,  1222,  1223,  1227,  1231,  1232,  1236,
    1237,  1259,  1260,  1264,  1265,  1266,  1272,  1273,  1274,  1278,
    1279,  1280,  1284,  1285,  1286,  1287,  1288,  1292,  1293,  1294,
    1295,  1300,  1314,  1315,  1316,  1317,  1318,  1319,  1320,  1321,
    1322,  1323,  1327,  1328,  1329,  1330,  1331,  1348,  1354,  1355,
    1356,  1357,  1358,  1359,  1360,  1361,  1362,  1366,  1367,  1371,
    1376,  1381,  1387,  1399,  1404,  1412,  1416,  1422,  1423,  1427,
    1428,  1432,  1433,  1437,  1438,  1452,  1453,  1454,  1455,  1456,
    1457,  1458,  1459,  1463,  1464,  1467,  1468,  1483,  1490,  1499,
    1500,  1501,  1502,  1503,  1504,  1505,  1506,  1507,  1508,  1509,
    1510,  1511,  1512,  1513,  1514,  1515,  1516,  1517,  1518,  1519,
    1520,  1521,  1522,  1523,  1524,  1525,  1526,  1527,  1528,  1529,
    1530,  1531,  1532,  1533,  1534,  1535,  1536,  1537,  1538,  1539,
    1540,  1541,  1542,  1543,  1544,  1545,  1549,  1550,  1551,  1552,
    1553,  1554,  1562,  1589,  1608,  1609
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

#define YYPACT_NINF (-574)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -574,   160,   970,  -574,    44,   111,  -574,  2136,  -574,  2123,
      90,    21,    86,  2136,  2136,   189,   206,   157,  2136,   129,
     129,   105,  2136,   165,     9,  -574,   567,   146,   225,   233,
     201,   242,   186,  -574,  -574,   161,   532,   180,   178,   184,
    -574,  -574,  -574,  -574,  -574,  -574,  -574,  -574,  -574,   190,
    -574,   196,  -574,  -574,   198,   202,   203,   204,   205,   207,
     208,   210,  -574,   188,  2136,  2136,   268,  -574,  -574,   209,
    -574,  -574,  -574,  -574,  2136,  2213,   285,   219,  -574,  2200,
    2136,  -574,  -574,   -10,   290,   281,   283,  -574,  -574,   630,
     175,  -574,    97,  -574,  -574,   308,   264,  -574,   246,   135,
     319,  -574,   241,   244,  -574,  -574,   250,   255,  -574,  -574,
    -574,   256,   129,  -574,    73,   247,  -574,   245,   125,    37,
     328,  -574,  -574,  -574,  -574,  -574,    92,  -574,   305,   263,
     257,    60,  -574,   337,  -574,   146,  -574,  -574,  -574,  -574,
    -574,  2136,  2136,  -574,  2393,  2136,   240,  -574,  -574,  -574,
    -574,  -574,  -574,  -574,  -574,  -574,  -574,  -574,  -574,  -574,
    2277,  -574,   266,   262,   269,  -574,  2136,  -574,    31,   272,
     273,  -574,   274,   577,   734,  2136,  2450,  -574,  2042,  2136,
    2136,  -574,  -574,  -574,  -574,  -574,  -574,  -574,  -574,  -574,
    -574,  2200,  2200,   652,  2200,  2200,  2200,  2136,  2507,   340,
    2136,  2136,  2136,  2136,   347,   -23,   856,  -574,   342,   351,
     351,   129,   106,   129,  -574,   357,  -574,  -574,  -574,    51,
    -574,   101,  -574,   288,   351,  -574,   361,   351,  -574,   362,
     366,   367,   339,  -574,   297,   370,   302,   303,  -574,  -574,
    -574,  -574,  -574,  -574,  -574,  -574,  -574,  -574,  -574,  -574,
    -574,  -574,  -574,  -574,  -574,  -574,  -574,  -574,  -574,  -574,
    -574,  -574,  -574,  -574,  -574,  -574,  -574,  -574,  -574,  -574,
    -574,  -574,  -574,  -574,  -574,  -574,  -574,  -574,  -574,  -574,
    -574,  -574,  -574,  -574,  -574,  -574,  2136,  2136,   306,  -574,
     299,    11,  -574,   108,  -574,  2136,  -574,   304,   307,  2136,
    -574,  -574,  -574,  -574,  -574,   311,  -574,   313,   309,  -574,
     322,   323,   324,  -574,  -574,  -574,  -574,  -574,  -574,  -574,
    -574,  -574,  -574,  -574,  -574,  -574,   300,   283,  -574,   175,
     175,  2200,   183,  -574,  -574,   310,   327,   329,  -574,  -574,
    -574,   331,   301,   372,   371,  2136,   405,  2136,  1027,  2136,
     179,   355,   336,   338,   341,   127,   335,   247,  1084,  -574,
    1141,  -574,  -574,  -574,  -574,  2136,   345,  -574,   343,   346,
    1198,   417,  -574,  -574,   361,  -574,   349,  2136,  2136,  -574,
    -574,   429,  -574,  2136,  2136,   352,  -574,  -574,  -574,  -574,
     360,  -574,   -12,   141,  -574,  2136,  2136,  -574,   913,   420,
     183,  -574,  2136,  2136,   358,  -574,  2136,  2136,   359,   401,
     364,   411,   436,  2136,   373,   437,   516,   375,   460,   384,
     385,  -574,   426,   433,   402,  -574,  -574,   403,   427,   483,
     407,  -574,   415,   421,  2136,   422,    69,  -574,  -574,  -574,
     799,  -574,   658,  -574,  -574,   423,   424,   181,   482,  -574,
     224,  -574,   425,   430,  -574,  1255,    35,   428,  -574,  2136,
    -574,   432,   435,   491,  -574,   440,  -574,  -574,  -574,  -574,
    -574,  -574,   505,   506,  -574,  -574,   452,  -574,  -574,  1312,
     442,   444,  -574,  1369,  -574,   447,  -574,  -574,  -574,  -574,
    -574,   434,   253,   527,   528,  -574,  -574,    80,   462,    19,
    -574,  -574,  -574,  2136,  -574,   463,   464,  2136,  -574,   467,
    -574,  -574,  1426,   503,    88,  -574,  2136,  -574,  -574,  1255,
     468,  -574,  -574,   470,  1483,  -574,  -574,  -574,  1540,   516,
    1597,  1654,   507,  -574,  -574,   500,  1711,  -574,  1768,  2136,
     486,   487,    94,   474,   475,   562,   429,  2136,  2136,   551,
    1825,  -574,  -574,   552,  1882,  -574,   540,   490,  -574,   492,
     493,  1255,  1255,  -574,  -574,  1483,  -574,  -574,  -574,   494,
     495,   496,  -574,  -574,  -574,  -574,  -574,  -574,  -574,  -574,
    -574,  -574,   502,  -574,   520,  -574,   521,   529,   530,   533,
     537,   541,   542,   543,  -574,   572,  -574,   585,   536,   547,
     549,   554,   578,  -574,  2335,   351,   629,  -574,  -574,  -574,
     553,   559,  -574,  -574,   558,   623,  2061,   631,   560,  -574,
    -574,  -574,  -574,  -574,  1255,  1483,  -574,  -574,  -574,  -574,
    -574,  -574,  -574,  -574,  -574,  -574,  -574,  -574,  -574,   564,
     568,   575,  -574,  -574,   576,   583,   584,   131,   574,  -574,
    1939,  -574,   586,  -574,   587,  -574,   589,   590,  -574,  1255,
    -574,  -574,  -574,  -574,  -574,  -574,  -574,   591,   592,   625,
    2136,   913,  -574,   913,   420,  -574,  -574,    83,  -574,  -574,
     595,  -574,  -574,  -574,  -574,   675,    85,  1996,  -574,   601,
     686,   699,  -574,   621,   622,  -574,  -574
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       3,     0,     2,     1,    44,     0,    32,     0,    45,     0,
       0,     0,     0,     0,     0,   160,   162,     0,   155,     0,
       0,     0,     0,     0,     0,    46,     0,     0,     0,     0,
       0,     0,     0,     4,     5,     0,     0,    41,     0,     0,
       9,    10,    12,    11,    13,    14,    15,    16,    17,     0,
      19,     0,    20,    22,     0,     0,     0,     0,     0,     0,
       0,     0,    31,     0,   247,   247,   222,    44,   225,     0,
     229,   230,   231,   232,     0,     0,     0,     0,   228,     0,
       0,   324,   324,   239,     0,   186,   187,   189,   191,   193,
     196,   199,   202,   207,   236,   224,     0,    55,     0,     0,
       0,     3,     0,     0,   161,   163,     0,     0,   156,   158,
     159,    44,     0,   143,     0,   129,   128,     0,     0,     0,
       0,   154,    51,    53,    52,    54,   122,    49,     0,     0,
       0,   115,   117,   114,   116,     0,     6,    37,    38,    39,
      40,     0,     0,    47,     0,     0,     0,   157,     7,     8,
      18,    21,    23,    24,    25,    26,    27,    28,    29,    30,
       0,   249,     0,   248,     0,   245,   247,   192,   204,     0,
       0,   203,     0,     0,     0,   247,     0,   226,     0,     0,
       0,   212,   213,   214,   215,   216,   217,   218,   219,   220,
     221,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     3,     0,   253,
     253,     0,     0,     0,     3,     0,     3,   153,   152,     0,
     151,     0,   148,     0,   253,    50,     0,   253,     3,     0,
       0,     0,     0,    33,     0,     0,   269,     0,   270,   315,
     285,   282,   283,   274,   289,   296,   297,   298,   314,   294,
     295,   293,   280,   278,   303,   284,   275,   312,   287,   288,
     276,   279,   286,   311,   299,   300,   306,   290,   301,   302,
     309,   313,   281,   310,   277,   271,   272,   273,   307,   308,
     305,   291,   292,   304,    43,    34,     0,     0,   269,   268,
       0,     0,   267,     0,    57,     0,    58,     0,     0,   247,
     223,   233,   234,   325,   251,   324,   237,   324,     0,   269,
       0,   243,    44,     3,   175,    41,   176,   177,   178,   179,
     180,   181,   182,   183,   184,   185,     0,   188,   190,   197,
     198,     0,   194,   200,   201,     0,   269,     0,   209,   246,
      56,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      77,   263,     0,   254,     0,     0,     0,   130,     0,   144,
       0,   150,   149,   146,   147,   247,     0,   124,     0,     0,
       0,   120,   118,   119,     0,    42,     0,   247,   247,    36,
      35,     0,   133,     0,     0,     0,   324,   250,   227,   205,
       0,   324,     0,     0,   240,   247,   247,   241,     0,   170,
     195,   208,   247,   247,     0,     3,     0,     0,     0,     0,
       0,    45,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     3,    45,    45,     0,   123,     3,     0,    45,     0,
       0,    48,     0,     0,   322,     0,     0,   316,   317,   133,
       0,   206,     0,   235,   238,     0,     0,     0,    45,   164,
       0,   165,     0,     0,     3,     0,     0,     0,     3,     0,
      72,     0,     0,     0,    79,     0,   255,   258,   259,   260,
     261,   262,     0,     0,   264,     3,   265,     3,     3,     0,
       0,     0,    61,     0,     3,     0,   121,     3,    59,    60,
     323,     0,     0,     0,     0,   134,   135,     0,   269,     0,
     252,   242,   244,     0,     3,     0,     0,     0,     3,     0,
     210,   211,     0,    45,    46,    66,     0,     3,     3,     0,
       0,    73,    79,     0,    78,    74,   257,   256,     0,     0,
       0,     0,    45,   126,   145,    45,     0,   113,     0,     0,
       0,   141,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   167,   166,     0,     0,   171,    45,     0,    64,     0,
       0,     0,     0,    67,     3,    75,    79,   107,    80,     0,
       0,     0,    85,    86,    88,    87,    89,    81,    90,    91,
      92,    93,     0,    95,     0,    97,     0,     0,     0,     0,
       0,     0,     0,     0,   106,    45,   266,    45,    45,     0,
       0,    45,    45,   318,     0,   253,     0,   136,   132,     3,
       0,     0,   319,   320,     0,    45,     0,    45,     0,    63,
      65,     3,    70,    68,     0,    76,    82,    83,    84,    94,
      96,    98,    99,   100,   101,   102,   103,   104,   105,     0,
       0,     0,   125,   110,     0,     0,     0,     0,     0,   142,
       0,   131,     0,     3,     0,     3,     0,     0,    62,     0,
      69,   108,   109,   127,   112,   111,   133,     0,     0,    45,
       0,     0,   168,     0,   170,   172,    71,     0,   133,     3,
       0,   321,   169,   174,   173,     0,     0,     0,   140,     0,
       0,    45,   139,     0,     0,   138,   137
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -574,  -574,   -15,  -574,  -176,   561,  -574,    -2,   617,  -574,
    -574,   597,  -170,  -155,  -512,  -510,  -509,  -504,  -503,  -495,
    -574,  -574,  -375,  -574,  -490,  -488,  -487,  -486,  -151,   593,
     350,  -484,  -482,   -96,  -574,  -436,  -574,  -574,   498,  -480,
    -150,  -145,  -137,  -474,  -133,  -131,  -121,  -111,  -470,  -573,
      52,  -443,    17,  -574,   546,   -68,  -574,  -189,    68,   -69,
     654,   534,  -574,   438,  -574,  -574,  -574,   -43,  -574,  -574,
    -178,   211,  -574,   291,   -70,  -173,   199,   -71
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    34,    35,   145,    36,    83,   146,   235,
     126,   127,    38,    39,    40,   515,    41,    42,    43,    44,
     350,   415,   524,   577,    45,    46,    47,    48,    49,   128,
     368,    50,    51,   114,    52,   436,   496,   542,   115,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,   449,
     451,   326,   161,    85,    86,    87,    88,    89,    90,    91,
      92,   194,    93,    94,   177,   397,    95,   162,   163,   305,
     352,   474,   353,   291,   292,   293,   435,   173
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      37,   307,   314,   497,   505,   332,   167,   509,   316,   563,
     171,   174,   572,   118,   345,   573,   212,   109,   110,   113,
     574,   575,   164,   317,    84,    99,    97,   318,   319,   576,
     102,   103,   354,   320,   578,   108,   579,   580,   581,   116,
     583,   321,   584,   121,   585,   322,   366,   323,    63,   369,
     589,   622,   623,   572,   594,   361,   573,   324,   346,   100,
     298,   574,   575,   119,   229,   175,   443,   325,   230,   516,
     576,   303,   383,   492,   284,   578,   176,   579,   580,   581,
     547,   583,   221,   584,   492,   585,   206,   492,   493,   492,
     222,   589,    67,   384,    98,   594,   122,   172,   682,   545,
     683,   548,   685,   362,   690,   363,   311,     8,   197,   111,
     113,   123,   328,   572,   660,   355,   573,   198,   517,    64,
     494,   574,   575,   297,     8,   124,   333,   334,   338,   217,
     576,   494,   308,    67,   494,   578,   494,   579,   580,   581,
      25,   583,   400,   584,   125,   585,   218,   565,     8,   676,
     122,   589,   495,   364,   213,   594,   214,    25,   233,   234,
       3,   106,   285,   495,   107,   123,   495,   224,   495,   101,
     219,   558,   202,   656,   197,   606,   315,   607,   220,   124,
     112,    25,   356,   198,   385,   312,    65,   213,     5,   386,
     304,   625,   348,   104,   413,   503,   203,   414,   125,   358,
       8,   360,     9,   420,    37,   131,   132,   667,   213,   113,
     105,   113,   386,   370,   335,   117,   204,   340,   341,   342,
     343,   444,    15,    16,   303,    18,    19,    20,   312,   129,
     677,     5,    24,    25,   392,    26,   393,   130,   507,    30,
      31,   135,   686,     8,   136,     9,   133,   134,   195,   196,
     137,   138,   139,   140,   191,   192,   390,   540,   541,   329,
     330,   148,   147,   160,   504,    15,    16,   149,    18,    19,
      20,   314,   165,   150,   314,    24,    25,   316,    26,   151,
     316,   152,    30,    31,   166,   153,   154,   155,   156,   169,
     157,   158,   317,   159,   170,   317,   318,   319,   398,   318,
     319,   286,   320,   379,   380,   320,   178,   508,   179,   180,
     321,   199,   387,   321,   322,   440,   323,   322,   200,   323,
     442,   201,   424,   205,   207,   209,   324,   208,   216,   324,
     210,   211,   223,   215,   432,   433,   325,   226,   227,   325,
     228,   231,   294,   295,   339,   296,    37,   299,   569,   300,
     301,   344,   445,   446,   570,   351,    37,   349,    37,   452,
     453,   359,   408,   365,   410,   367,   412,   371,    37,   571,
     372,   374,   373,   582,   586,   375,   376,   377,   378,   587,
     388,   381,   382,   399,   405,   394,    82,   588,   401,   569,
     455,   590,   391,   591,   386,   570,    37,   395,   396,    64,
     437,   438,   402,   592,   403,   406,   479,   404,   407,   409,
     571,   483,   417,   593,   582,   586,   416,   419,   421,   418,
     587,   425,   427,   456,   457,   429,   426,   648,   588,   431,
     463,   647,   590,   434,   591,   439,   441,   450,   459,   512,
     314,   454,   458,   519,   592,   315,   316,   460,   315,   569,
     461,   490,   462,    37,   593,   570,   464,   465,   475,   500,
     528,   317,   530,   531,   476,   318,   319,   477,   478,   536,
     571,   320,   538,   480,   582,   586,   520,    37,   482,   321,
     587,    37,   481,   322,   485,   323,   484,   486,   588,   550,
     487,   488,   590,   554,   591,   324,   506,   489,   491,   501,
     502,   510,   561,   562,   592,   325,   511,   523,   526,   527,
      37,   518,   559,   529,   593,   521,   539,    37,   522,   466,
     549,   467,    37,   525,   553,   533,    37,   534,    37,    37,
     537,   543,   544,   560,    37,   557,    37,   546,   468,   469,
     470,   471,   137,   138,   139,   140,   551,   552,    37,   624,
     555,   564,    37,   566,   599,   600,   603,   608,   609,    37,
      37,   604,   605,    37,   612,   613,   610,   614,   616,   618,
      66,    67,    68,   619,    69,   620,   621,   626,   627,   628,
      66,    67,    68,   641,    69,   629,     8,   472,   473,    70,
      71,    72,    73,   141,   650,    74,     8,    75,    76,    70,
      71,    72,    73,   630,   631,    74,   659,    75,    76,   142,
     644,   143,   632,   633,   315,   639,   634,    77,   144,    25,
     635,    78,    37,    37,   636,   637,   638,    77,   640,    25,
     642,    78,   643,   645,   649,   652,   651,   654,   671,    79,
     673,   653,    80,   658,    81,   657,    82,   661,    37,    79,
     668,   662,    80,   120,    81,   302,    82,    37,   663,   664,
     303,    66,    67,    68,   687,    69,   665,   666,   670,    37,
     672,    37,   674,   675,   678,   679,   680,     8,   688,   689,
      70,    71,    72,    73,   692,    37,    74,   681,    75,    76,
     693,   181,   182,   183,   184,   185,   186,   187,   188,   189,
     190,   191,   192,   694,   695,   696,   193,   287,    77,   143,
      25,   357,    78,   181,   182,   183,   184,   185,   186,   187,
     188,   189,   190,   225,   430,   327,   684,   331,   232,   168,
      79,   499,     0,    80,     0,    81,   389,    82,   288,   289,
     596,   303,   238,   239,     0,   611,     0,     0,   240,     0,
     241,   242,     0,   243,     0,   244,   245,   246,   247,   248,
     249,   250,   251,   252,   253,   254,   255,   256,   257,   258,
     259,   260,   261,   262,   263,   264,   265,   266,   267,   268,
     269,   270,   271,   272,   273,   274,   275,   276,   277,   278,
     279,   280,   281,   282,   283,     0,     0,     0,     0,     0,
       0,     0,     0,   498,   289,     0,     0,   238,   239,     0,
       0,     0,     0,   240,   306,   241,   242,   303,   243,     0,
     244,   245,   246,   247,   248,   249,   250,   251,   252,   253,
     254,   255,   256,   257,   258,   259,   260,   261,   262,   263,
     264,   265,   266,   267,   268,   269,   270,   271,   272,   273,
     274,   275,   276,   277,   278,   279,   280,   281,   282,   283,
       4,     0,     0,     5,     0,     6,     0,     0,     0,     0,
       7,     0,     0,     0,     0,     8,     0,     9,     0,     0,
       0,     0,   303,     0,     0,    10,     0,     0,    11,     0,
       0,    12,   347,     0,     0,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,     0,    24,    25,     0,
      26,    27,    28,    29,    30,    31,    32,     4,     0,     0,
       5,     0,     6,     0,     0,     0,     0,     7,     0,     0,
     447,     0,   448,     0,     9,     0,     0,     0,     0,    33,
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
       0,     7,     0,     0,     0,     0,   411,     0,     9,     0,
       0,     0,     0,    33,     0,     0,    10,     0,     0,    11,
       0,     0,    12,     0,     0,     0,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,     0,    24,    25,
       0,    26,    27,    28,    29,    30,    31,    32,     4,     0,
       0,     5,     0,     6,     0,     0,     0,     0,     7,     0,
       0,     0,     0,   422,     0,     9,     0,     0,     0,     0,
      33,     0,     0,    10,     0,     0,    11,     0,     0,    12,
       0,     0,     0,    13,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,     0,    24,    25,     0,    26,    27,
      28,    29,    30,    31,    32,     4,     0,     0,     5,     0,
       6,     0,     0,     0,     0,     7,     0,     0,     0,     0,
     423,     0,     9,     0,     0,     0,     0,    33,     0,     0,
      10,     0,     0,    11,     0,     0,    12,     0,     0,     0,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,     0,    24,    25,     0,    26,    27,    28,    29,    30,
      31,    32,     4,     0,     0,     5,     0,     6,     0,     0,
       0,     0,     7,     0,     0,     0,     0,   428,     0,     9,
       0,     0,     0,     0,    33,     0,     0,    10,     0,     0,
      11,     0,     0,    12,     0,     0,     0,    13,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,     0,    24,
      25,     0,    26,    27,    28,    29,    30,    31,    32,     4,
       0,     0,     5,     0,     6,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   513,     0,     9,     0,     0,     0,
       0,    33,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,   514,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     6,     0,     0,     0,     0,     7,     0,     0,     0,
       0,   532,     0,     9,     0,     0,     0,     0,    33,     0,
       0,    10,     0,     0,    11,     0,     0,    12,     0,     0,
       0,    13,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,     0,    24,    25,     0,    26,    27,    28,    29,
      30,    31,    32,     4,     0,     0,     5,     0,     6,     0,
       0,     0,     0,     7,     0,     0,     0,     0,   535,     0,
       9,     0,     0,     0,     0,    33,     0,     0,    10,     0,
       0,    11,     0,     0,    12,     0,     0,     0,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,     0,
      24,    25,     0,    26,    27,    28,    29,    30,    31,    32,
       4,     0,     0,     5,     0,     6,     0,     0,     0,     0,
       7,     0,     0,     0,     0,   556,     0,     9,     0,     0,
       0,     0,    33,     0,     0,    10,     0,     0,    11,     0,
       0,    12,     0,     0,     0,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,     0,    24,    25,     0,
      26,    27,    28,    29,    30,    31,    32,   312,     0,     0,
       5,     0,   567,     0,     0,     0,     0,     7,     0,     0,
       0,     0,     8,     0,     9,     0,     0,     0,     0,    33,
       0,     0,    10,     0,     0,    11,     0,     0,    12,     0,
       0,     0,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,     0,    24,    25,     0,    26,    27,    28,
      29,    30,    31,    32,     4,     0,     0,     5,     0,     6,
       0,     0,     0,     0,     7,     0,     0,     0,     0,   595,
       0,     9,     0,     0,     0,     0,   568,     0,     0,    10,
       0,     0,    11,     0,     0,    12,     0,     0,     0,    13,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
       0,    24,    25,     0,    26,    27,    28,    29,    30,    31,
      32,     4,     0,     0,     5,     0,     6,     0,     0,     0,
       0,     7,     0,     0,     0,     0,   597,     0,     9,     0,
       0,     0,     0,    33,     0,     0,    10,     0,     0,    11,
       0,     0,    12,     0,     0,     0,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,     0,    24,    25,
       0,    26,    27,    28,    29,    30,    31,    32,     4,     0,
       0,     5,     0,     6,     0,     0,     0,     0,     7,     0,
       0,     0,     0,   598,     0,     9,     0,     0,     0,     0,
      33,     0,     0,    10,     0,     0,    11,     0,     0,    12,
       0,     0,     0,    13,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,     0,    24,    25,     0,    26,    27,
      28,    29,    30,    31,    32,     4,     0,     0,     5,     0,
       6,     0,     0,     0,     0,     7,     0,     0,     0,     0,
     601,     0,     9,     0,     0,     0,     0,    33,     0,     0,
      10,     0,     0,    11,     0,     0,    12,     0,     0,     0,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,     0,    24,    25,     0,    26,    27,    28,    29,    30,
      31,    32,     4,     0,     0,     5,     0,     6,     0,     0,
       0,     0,     7,     0,     0,     0,     0,   602,     0,     9,
       0,     0,     0,     0,    33,     0,     0,    10,     0,     0,
      11,     0,     0,    12,     0,     0,     0,    13,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,     0,    24,
      25,     0,    26,    27,    28,    29,    30,    31,    32,     4,
       0,     0,     5,     0,     6,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   615,     0,     9,     0,     0,     0,
       0,    33,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     6,     0,     0,     0,     0,     7,     0,     0,     0,
       0,   617,     0,     9,     0,     0,     0,     0,    33,     0,
       0,    10,     0,     0,    11,     0,     0,    12,     0,     0,
       0,    13,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,     0,    24,    25,     0,    26,    27,    28,    29,
      30,    31,    32,     4,     0,     0,     5,     0,     6,     0,
       0,     0,     0,     7,     0,     0,     0,     0,   669,     0,
       9,     0,     0,     0,     0,    33,     0,     0,    10,     0,
       0,    11,     0,     0,    12,     0,     0,     0,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,     0,
      24,    25,     0,    26,    27,    28,    29,    30,    31,    32,
       4,     0,     0,     5,     0,     6,     0,     0,     0,     0,
       7,     0,     0,     0,     0,   691,     0,     9,     0,     0,
       0,     0,    33,     0,     0,    10,     0,     0,    11,     0,
       0,    12,     0,     0,     0,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,   312,    24,    25,     5,
      26,    27,    28,    29,    30,    31,    32,     0,     0,     0,
       0,     8,     0,     9,     0,   312,     0,     0,     5,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    33,
       8,     0,     9,    15,    16,     0,    18,    19,    20,     0,
       0,     0,     0,    24,    25,     0,    26,     0,     0,     0,
      30,    31,    15,    16,     0,    18,    19,    20,     0,     0,
       0,     0,    24,    25,     0,    26,     0,     0,     0,    30,
      31,     0,     0,     0,     0,   313,    66,    67,    68,     0,
      69,     0,     0,     0,     0,     0,     0,     0,     0,    66,
      67,    68,     8,    69,   655,    70,    71,    72,    73,     0,
       0,    74,     0,    75,    76,     8,    96,     0,    70,    71,
      72,    73,     0,     0,    74,     0,    75,    76,     0,     0,
       0,     0,     0,    77,     0,    25,     0,    78,     0,     0,
       0,     0,     0,     0,     0,     0,    77,     0,    25,     0,
      78,     0,     0,     0,     0,    79,     0,     0,    80,     0,
      81,     0,    82,    66,    67,    68,     0,    69,    79,     0,
       0,    80,     0,    81,     0,    82,    66,    67,    68,     8,
      69,     0,    70,    71,    72,    73,     0,     0,     0,     0,
      75,    76,     8,     0,     0,    70,    71,    72,    73,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      77,     0,    25,     0,    78,     0,     0,     0,     0,     0,
       0,     0,     0,    77,     0,    25,     0,    78,     0,     0,
       0,     0,    79,     0,     0,    80,     0,    81,     0,    82,
       0,   288,   289,     0,     0,   238,   239,     0,    80,     0,
      81,   240,    82,   241,   242,     0,   243,     0,   244,   245,
     246,   247,   248,   249,   250,   251,   252,   253,   254,   255,
     256,   257,   258,   259,   260,   261,   262,   263,   264,   265,
     266,   267,   268,   269,   270,   271,   272,   273,   274,   275,
     276,   277,   278,   279,   280,   281,   282,   283,     0,   288,
     289,     0,     0,   238,   239,     0,     0,     0,     0,   240,
       0,   241,   242,   290,   243,     0,   244,   245,   246,   247,
     248,   249,   250,   251,   252,   253,   254,   255,   256,   257,
     258,   259,   260,   261,   262,   263,   264,   265,   266,   267,
     268,   269,   270,   271,   272,   273,   274,   275,   276,   277,
     278,   279,   280,   281,   282,   283,     0,   236,     0,     0,
     237,   238,   239,     0,     0,     0,     0,   240,     0,   241,
     242,   646,   243,     0,   244,   245,   246,   247,   248,   249,
     250,   251,   252,   253,   254,   255,   256,   257,   258,   259,
     260,   261,   262,   263,   264,   265,   266,   267,   268,   269,
     270,   271,   272,   273,   274,   275,   276,   277,   278,   279,
     280,   281,   282,   283,   309,     0,     0,   310,   238,   239,
       0,     0,     0,     0,   240,     0,   241,   242,     0,   243,
       0,   244,   245,   246,   247,   248,   249,   250,   251,   252,
     253,   254,   255,   256,   257,   258,   259,   260,   261,   262,
     263,   264,   265,   266,   267,   268,   269,   270,   271,   272,
     273,   274,   275,   276,   277,   278,   279,   280,   281,   282,
     283,   336,     0,     0,   337,   238,   239,     0,     0,     0,
       0,   240,     0,   241,   242,     0,   243,     0,   244,   245,
     246,   247,   248,   249,   250,   251,   252,   253,   254,   255,
     256,   257,   258,   259,   260,   261,   262,   263,   264,   265,
     266,   267,   268,   269,   270,   271,   272,   273,   274,   275,
     276,   277,   278,   279,   280,   281,   282,   283
};

static const yytype_int16 yycheck[] =
{
       2,   174,   178,   439,   447,   194,    74,   450,   178,   519,
      79,    82,   524,     4,    37,   524,   112,    19,    20,    21,
     524,   524,    65,   178,     7,     4,     9,   178,   178,   524,
      13,    14,   210,   178,   524,    18,   524,   524,   524,    22,
     524,   178,   524,    26,   524,   178,   224,   178,     4,   227,
     524,   561,   562,   565,   524,     4,   565,   178,    81,    38,
      29,   565,   565,    54,     4,    75,    78,   178,     8,    34,
     565,    83,    61,     4,   144,   565,    86,   565,   565,   565,
      61,   565,    45,   565,     4,   565,   101,     4,    19,     4,
      53,   565,     4,    82,     4,   565,     4,    80,   671,    19,
     673,    82,    19,    52,    19,     4,   176,    19,    77,     4,
     112,    19,   180,   625,   624,   211,   625,    86,    83,    75,
      51,   625,   625,   166,    19,    33,   195,   196,   198,     4,
     625,    51,   175,     4,    51,   625,    51,   625,   625,   625,
      52,   625,   331,   625,    52,   625,    21,   522,    19,   659,
       4,   625,    83,    52,    81,   625,    83,    52,   141,   142,
       0,     4,   145,    83,     7,    19,    83,    75,    83,    83,
      45,    83,    37,   616,    77,    81,   178,    83,    53,    33,
      75,    52,    76,    86,    76,     4,    75,    81,     7,    81,
     173,   566,   207,     4,    15,    14,    61,    18,    52,   214,
      19,   216,    21,    76,   206,     4,     5,    76,    81,   211,
       4,   213,    81,   228,   197,    50,    81,   200,   201,   202,
     203,    80,    41,    42,    83,    44,    45,    46,     4,     4,
     666,     7,    51,    52,   305,    54,   307,     4,    14,    58,
      59,    55,   678,    19,    83,    21,     4,     5,    73,    74,
      10,    11,    12,    13,    71,    72,   299,     4,     5,   191,
     192,    83,    82,    75,    83,    41,    42,    83,    44,    45,
      46,   447,     4,    83,   450,    51,    52,   447,    54,    83,
     450,    83,    58,    59,    75,    83,    83,    83,    83,     4,
      83,    83,   447,    83,    75,   450,   447,   447,   313,   450,
     450,    61,   447,   286,   287,   450,    16,    83,    27,    26,
     447,     3,   295,   450,   447,   386,   447,   450,    54,   450,
     391,    75,   365,     4,    83,    75,   447,    83,    83,   450,
      75,    75,     4,    86,   377,   378,   447,    32,    75,   450,
      83,     4,    76,    81,     4,    76,   348,    75,   524,    76,
      76,     4,   395,   396,   524,     4,   358,    15,   360,   402,
     403,     4,   345,    75,   347,     4,   349,     5,   370,   524,
       4,    32,     5,   524,   524,    78,     6,    75,    75,   524,
      76,    75,    83,    83,    83,    76,    79,   524,    78,   565,
     405,   524,    81,   524,    81,   565,   398,    75,    75,    75,
     383,   384,    75,   524,    75,    33,   421,    76,    37,     4,
     565,   426,    76,   524,   565,   565,    61,    76,    83,    81,
     565,    76,    76,   406,   407,     8,    83,   605,   565,    80,
     413,   604,   565,     4,   565,    83,    76,    17,    37,   454,
     616,    83,    83,   458,   565,   447,   616,    83,   450,   625,
      39,   434,    16,   455,   565,   625,    83,    20,    83,   442,
     475,   616,   477,   478,     4,   616,   616,    83,    83,   484,
     625,   616,   487,    47,   625,   625,   459,   479,    76,   616,
     625,   483,    49,   616,    57,   616,    83,     4,   625,   504,
      83,    76,   625,   508,   625,   616,    14,    76,    76,    76,
      76,    76,   517,   518,   625,   616,    76,    16,     3,     3,
     512,    83,   514,    61,   625,    83,    82,   519,    83,     3,
     503,     5,   524,    83,   507,    83,   528,    83,   530,   531,
      83,     4,     4,   516,   536,    32,   538,    75,    22,    23,
      24,    25,    10,    11,    12,    13,    83,    83,   550,   564,
      83,    83,   554,    83,    47,    55,   539,    83,    83,   561,
     562,    75,    75,   565,   547,   548,     4,    16,    16,    29,
       3,     4,     5,    83,     7,    83,    83,    83,    83,    83,
       3,     4,     5,    47,     7,    83,    19,    71,    72,    22,
      23,    24,    25,    61,   609,    28,    19,    30,    31,    22,
      23,    24,    25,    83,    83,    28,   621,    30,    31,    77,
      56,    79,    83,    83,   616,    43,    83,    50,    86,    52,
      83,    54,   624,   625,    83,    83,    83,    50,    43,    52,
      83,    54,    83,    55,     5,    76,    83,    14,   653,    72,
     655,    83,    75,    83,    77,    14,    79,    83,   650,    72,
      76,    83,    75,    86,    77,    78,    79,   659,    83,    83,
      83,     3,     4,     5,   679,     7,    83,    83,    82,   671,
      83,   673,    83,    83,    83,    83,    51,    19,    83,     4,
      22,    23,    24,    25,    83,   687,    28,   670,    30,    31,
       4,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      70,    71,    72,     4,    83,    83,    89,   146,    50,    79,
      52,   213,    54,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,   126,   374,   179,   674,   193,   135,    75,
      72,   440,    -1,    75,    -1,    77,   298,    79,     4,     5,
     529,    83,     8,     9,    -1,   546,    -1,    -1,    14,    -1,
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
      -1,    19,    -1,    21,    -1,     4,    -1,    -1,     7,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    83,
      19,    -1,    21,    41,    42,    -1,    44,    45,    46,    -1,
      -1,    -1,    -1,    51,    52,    -1,    54,    -1,    -1,    -1,
      58,    59,    41,    42,    -1,    44,    45,    46,    -1,    -1,
      -1,    -1,    51,    52,    -1,    54,    -1,    -1,    -1,    58,
      59,    -1,    -1,    -1,    -1,    83,     3,     4,     5,    -1,
       7,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,     3,
       4,     5,    19,     7,    83,    22,    23,    24,    25,    -1,
      -1,    28,    -1,    30,    31,    19,    33,    -1,    22,    23,
      24,    25,    -1,    -1,    28,    -1,    30,    31,    -1,    -1,
      -1,    -1,    -1,    50,    -1,    52,    -1,    54,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    50,    -1,    52,    -1,
      54,    -1,    -1,    -1,    -1,    72,    -1,    -1,    75,    -1,
      77,    -1,    79,     3,     4,     5,    -1,     7,    72,    -1,
      -1,    75,    -1,    77,    -1,    79,     3,     4,     5,    19,
       7,    -1,    22,    23,    24,    25,    -1,    -1,    -1,    -1,
      30,    31,    19,    -1,    -1,    22,    23,    24,    25,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      50,    -1,    52,    -1,    54,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    50,    -1,    52,    -1,    54,    -1,    -1,
      -1,    -1,    72,    -1,    -1,    75,    -1,    77,    -1,    79,
      -1,     4,     5,    -1,    -1,     8,     9,    -1,    75,    -1,
      77,    14,    79,    16,    17,    -1,    19,    -1,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    -1,     4,
       5,    -1,    -1,     8,     9,    -1,    -1,    -1,    -1,    14,
      -1,    16,    17,    76,    19,    -1,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    -1,     4,    -1,    -1,
       7,     8,     9,    -1,    -1,    -1,    -1,    14,    -1,    16,
      17,    76,    19,    -1,    21,    22,    23,    24,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,     4,    -1,    -1,     7,     8,     9,
      -1,    -1,    -1,    -1,    14,    -1,    16,    17,    -1,    19,
      -1,    21,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,     4,    -1,    -1,     7,     8,     9,    -1,    -1,    -1,
      -1,    14,    -1,    16,    17,    -1,    19,    -1,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60
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
     145,   146,   147,   149,   150,   153,    33,   139,     4,     4,
      38,    83,   139,   139,     4,     4,     4,     7,   139,    94,
      94,     4,    75,    94,   120,   125,   139,    50,     4,    54,
      86,   139,     4,    19,    33,    52,    97,    98,   116,     4,
       4,     4,     5,     4,     5,    55,    83,    10,    11,    12,
      13,    61,    77,    79,    86,    92,    95,    82,    83,    83,
      83,    83,    83,    83,    83,    83,    83,    83,    83,    83,
      75,   139,   154,   155,   154,     4,    75,   142,   147,     4,
      75,   146,   139,   164,   164,    75,    86,   151,    16,    27,
      26,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      70,    71,    72,    95,   148,    73,    74,    77,    86,     3,
      54,    75,    37,    61,    81,     4,    89,    83,    83,    75,
      75,    75,   120,    81,    83,    86,    83,     4,    21,    45,
      53,    45,    53,     4,    75,    98,    32,    75,    83,     4,
       8,     4,   116,   139,   139,    96,     4,     7,     8,     9,
      14,    16,    17,    19,    21,    22,    23,    24,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,   161,   139,    61,    92,     4,     5,
      76,   160,   161,   162,    76,    81,    76,   154,    29,    75,
      76,    76,    78,    83,   139,   156,    80,   162,   154,     4,
       7,   161,     4,    83,    91,    94,    99,   100,   115,   127,
     128,   129,   131,   132,   133,   134,   138,   141,   142,   145,
     145,   148,   144,   146,   146,   139,     4,     7,   161,     4,
     139,   139,   139,   139,     4,    37,    81,    36,    89,    15,
     107,     4,   157,   159,   157,   120,    76,   125,    89,     4,
      89,     4,    52,     4,    52,    75,   157,     4,   117,   157,
      89,     5,     4,     5,    32,    78,     6,    75,    75,   139,
     139,    75,    83,    61,    82,    76,    81,   139,    76,   150,
     154,    81,   164,   164,    76,    75,    75,   152,    89,    83,
     144,    78,    75,    75,    76,    83,    33,    37,   139,     4,
     139,    19,   139,    15,    18,   108,    61,    76,    81,    76,
      76,    83,    19,    19,   154,    76,    83,    76,    19,     8,
     117,    80,   154,   154,     4,   163,   122,   139,   139,    83,
     164,    76,   164,    78,    80,   154,   154,    17,    19,   136,
      17,   137,   154,   154,    83,    89,   139,   139,    83,    37,
      83,    39,    16,   139,    83,    20,     3,     5,    22,    23,
      24,    25,    71,    72,   158,    83,     4,    83,    83,    89,
      47,    49,    76,    89,    83,    57,     4,    83,    76,    76,
     139,    76,     4,    19,    51,    83,   123,   122,     4,   160,
     139,    76,    76,    14,    83,   138,    14,    14,    83,   138,
      76,    76,    89,    19,    52,   102,    34,    83,    83,    89,
     139,    83,    83,    16,   109,    83,     3,     3,    89,    61,
      89,    89,    19,    83,    83,    19,    89,    83,    89,    82,
       4,     5,   124,     4,     4,    19,    75,    61,    82,   139,
      89,    83,    83,   139,    89,    83,    19,    32,    83,    94,
     139,    89,    89,   102,    83,   109,    83,     9,    83,    91,
      99,   100,   101,   103,   104,   105,   106,   110,   111,   112,
     113,   114,   115,   118,   119,   126,   127,   128,   129,   130,
     131,   132,   133,   134,   135,    19,   158,    19,    19,    47,
      55,    19,    19,   139,    75,    75,    81,    83,    83,    83,
       4,   163,   139,   139,    16,    19,    16,    19,    29,    83,
      83,    83,   102,   102,    89,   109,    83,    83,    83,    83,
      83,    83,    83,    83,    83,    83,    83,    83,    83,    43,
      43,    47,    83,    83,    56,    55,    76,   162,   157,     5,
      89,    83,    76,    83,    14,    83,   138,    14,    83,    89,
     102,    83,    83,    83,    83,    83,    83,    76,    76,    19,
      82,    89,    83,    89,    83,    83,   102,   122,    83,    83,
      51,   139,   136,   136,   137,    19,   122,    89,    83,     4,
      19,    19,    83,     4,     4,    83,    83
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
     103,   103,   104,   105,   106,   107,   107,   108,   108,   109,
     109,   109,   110,   110,   110,   110,   110,   110,   110,   110,
     110,   110,   110,   110,   110,   110,   110,   110,   110,   110,
     110,   110,   110,   110,   110,   110,   110,   110,   111,   111,
     112,   112,   113,   114,   115,   115,   115,   115,   115,   115,
     115,   115,   116,   116,   117,   118,   118,   118,   119,   120,
     120,   121,   121,   122,   122,   122,   123,   123,   123,   123,
     123,   124,   124,   125,   125,   126,   127,   127,   127,   127,
     127,   127,   127,   127,   128,   129,   129,   130,   131,   132,
     133,   133,   134,   134,   135,   135,   136,   136,   136,   136,
     137,   137,   137,   137,   137,   138,   138,   138,   138,   138,
     138,   138,   138,   138,   138,   138,   139,   140,   140,   141,
     141,   142,   142,   143,   143,   143,   144,   144,   144,   145,
     145,   145,   146,   146,   146,   146,   146,   147,   147,   147,
     147,   147,   148,   148,   148,   148,   148,   148,   148,   148,
     148,   148,   149,   149,   149,   149,   149,   149,   149,   149,
     149,   149,   149,   149,   149,   149,   149,   150,   150,   151,
     151,   151,   151,   152,   152,   153,   153,   154,   154,   155,
     155,   156,   156,   157,   157,   158,   158,   158,   158,   158,
     158,   158,   158,   159,   159,   159,   159,   160,   160,   161,
     161,   161,   161,   161,   161,   161,   161,   161,   161,   161,
     161,   161,   161,   161,   161,   161,   161,   161,   161,   161,
     161,   161,   161,   161,   161,   161,   161,   161,   161,   161,
     161,   161,   161,   161,   161,   161,   161,   161,   161,   161,
     161,   161,   161,   161,   161,   161,   162,   162,   162,   162,
     162,   162,   163,   163,   164,   164
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
       6,     6,    10,     3,     2,     3,     7,     8,     9,    10,
       9,    11,     6,     7,     7,     5,     6,     0,     3,     0,
       2,     2,     2,     2,     2,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     2,     1,     2,     1,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     1,    10,    10,
       9,    10,    10,     7,     2,     2,     2,     2,     4,     4,
       4,     6,     1,     4,     1,     9,     7,    10,     2,     1,
       3,    10,     9,     0,     2,     2,     3,    10,    10,     9,
       7,     1,     3,     1,     3,     7,     4,     4,     3,     4,
       4,     3,     3,     3,     2,     1,     2,     2,     2,     2,
       1,     2,     1,     2,     6,     6,     3,     3,     6,     7,
       0,     3,     6,     7,     7,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     3,     1,
       3,     1,     2,     1,     3,     4,     1,     3,     3,     1,
       3,     3,     1,     2,     2,     4,     5,     1,     4,     3,
       6,     6,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     3,     1,     1,     2,     4,     1,     1,
       1,     1,     1,     3,     3,     5,     1,     3,     5,     0,
       3,     3,     5,     0,     3,     2,     3,     0,     1,     1,
       3,     1,     4,     0,     1,     1,     2,     2,     1,     1,
       1,     1,     1,     1,     3,     3,     5,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     3,     3,     6,     6,
       6,     9,     1,     2,     0,     2
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
#line 510 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2574 "src/parser.tab.c"
        break;

    case YYSYMBOL_STRING: /* STRING  */
#line 510 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2580 "src/parser.tab.c"
        break;

    case YYSYMBOL_LENS_CONTENT: /* LENS_CONTENT  */
#line 510 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2586 "src/parser.tab.c"
        break;

    case YYSYMBOL_QUALIFIED_IDENT: /* QUALIFIED_IDENT  */
#line 510 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2592 "src/parser.tab.c"
        break;

    case YYSYMBOL_program: /* program  */
#line 533 "src/parser.y"
            { (void) ((*yyvaluep).stmt_list); }
#line 2598 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement_list: /* statement_list  */
#line 476 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2604 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement: /* statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2610 "src/parser.tab.c"
        break;

    case YYSYMBOL_assignment: /* assignment  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2616 "src/parser.tab.c"
        break;

    case YYSYMBOL_lvalue: /* lvalue  */
#line 511 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2622 "src/parser.tab.c"
        break;

    case YYSYMBOL_variable_name: /* variable_name  */
#line 510 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2628 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_lens: /* comparison_lens  */
#line 518 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 2634 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_name: /* modifier_name  */
#line 510 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2640 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_word: /* modifier_word  */
#line 510 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2646 "src/parser.tab.c"
        break;

    case YYSYMBOL_print_statement: /* print_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2652 "src/parser.tab.c"
        break;

    case YYSYMBOL_call_statement: /* call_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2658 "src/parser.tab.c"
        break;

    case YYSYMBOL_with_lock_statement: /* with_lock_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2664 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_end: /* for_end  */
#line 510 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2670 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_each_statement: /* for_each_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2676 "src/parser.tab.c"
        break;

    case YYSYMBOL_do_loop_statement: /* do_loop_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2682 "src/parser.tab.c"
        break;

    case YYSYMBOL_while_statement: /* while_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2688 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement: /* consider_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2694 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_branch_list: /* consider_branch_list  */
#line 516 "src/parser.y"
            { ast_free_consider_branch_list(((*yyvaluep).consider_branch_list)); }
#line 2700 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_else_opt: /* consider_else_opt  */
#line 476 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2706 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement_list: /* consider_statement_list  */
#line 476 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2712 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_body_statement: /* consider_body_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2718 "src/parser.tab.c"
        break;

    case YYSYMBOL_function_statement: /* function_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2724 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_statement: /* modifier_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2730 "src/parser.tab.c"
        break;

    case YYSYMBOL_program_statement: /* program_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2736 "src/parser.tab.c"
        break;

    case YYSYMBOL_library_statement: /* library_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2742 "src/parser.tab.c"
        break;

    case YYSYMBOL_use_statement: /* use_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2748 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_signature: /* modifier_signature  */
#line 519 "src/parser.y"
            { ast_free_modifier_signature(((*yyvaluep).modifier_signature)); }
#line 2754 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_context: /* modifier_context  */
#line 510 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2760 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_statement: /* watch_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2766 "src/parser.tab.c"
        break;

    case YYSYMBOL_unwatch_statement: /* unwatch_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2772 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_list: /* watch_target_list  */
#line 517 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2778 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_statement: /* server_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2784 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item_list: /* server_item_list  */
#line 523 "src/parser.y"
            { ast_free_server_item_list(((*yyvaluep).server_item_list)); }
#line 2790 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item: /* server_item  */
#line 522 "src/parser.y"
            { AstServerItemList one = ast_server_item_list_append(ast_server_item_list_empty(), ((*yyvaluep).server_item)); ast_free_server_item_list(one); }
#line 2796 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_string_list: /* server_string_list  */
#line 517 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2802 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_path: /* watch_target_path  */
#line 510 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2808 "src/parser.tab.c"
        break;

    case YYSYMBOL_without_watchers_statement: /* without_watchers_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2814 "src/parser.tab.c"
        break;

    case YYSYMBOL_on_error_statement: /* on_error_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2820 "src/parser.tab.c"
        break;

    case YYSYMBOL_error_statement: /* error_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2826 "src/parser.tab.c"
        break;

    case YYSYMBOL_return_statement: /* return_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2832 "src/parser.tab.c"
        break;

    case YYSYMBOL_label_statement: /* label_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2838 "src/parser.tab.c"
        break;

    case YYSYMBOL_goto_statement: /* goto_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2844 "src/parser.tab.c"
        break;

    case YYSYMBOL_gosub_statement: /* gosub_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2850 "src/parser.tab.c"
        break;

    case YYSYMBOL_break_statement: /* break_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2856 "src/parser.tab.c"
        break;

    case YYSYMBOL_continue_statement: /* continue_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2862 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_statement: /* if_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2868 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_block_tail: /* if_block_tail  */
#line 476 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2874 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_inline_tail: /* if_inline_tail  */
#line 476 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2880 "src/parser.tab.c"
        break;

    case YYSYMBOL_inline_statement: /* inline_statement  */
#line 512 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2886 "src/parser.tab.c"
        break;

    case YYSYMBOL_expression: /* expression  */
#line 511 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2892 "src/parser.tab.c"
        break;

    case YYSYMBOL_or_expression: /* or_expression  */
#line 511 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2898 "src/parser.tab.c"
        break;

    case YYSYMBOL_and_expression: /* and_expression  */
#line 511 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2904 "src/parser.tab.c"
        break;

    case YYSYMBOL_not_expression: /* not_expression  */
#line 511 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2910 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_expression: /* comparison_expression  */
#line 511 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2916 "src/parser.tab.c"
        break;

    case YYSYMBOL_additive_expression: /* additive_expression  */
#line 511 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2922 "src/parser.tab.c"
        break;

    case YYSYMBOL_multiplicative_expression: /* multiplicative_expression  */
#line 511 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2928 "src/parser.tab.c"
        break;

    case YYSYMBOL_unary_expression: /* unary_expression  */
#line 511 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2934 "src/parser.tab.c"
        break;

    case YYSYMBOL_postfix_expression: /* postfix_expression  */
#line 511 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2940 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_operator: /* comparison_operator  */
#line 510 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2946 "src/parser.tab.c"
        break;

    case YYSYMBOL_primary: /* primary  */
#line 511 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2952 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_literal: /* record_literal  */
#line 511 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2958 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_suffix: /* ident_suffix  */
#line 520 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 2964 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_dot_suffix: /* ident_dot_suffix  */
#line 520 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 2970 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list_opt: /* argument_list_opt  */
#line 514 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 2976 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list: /* argument_list  */
#line 514 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 2982 "src/parser.tab.c"
        break;

    case YYSYMBOL_array_argument_list: /* array_argument_list  */
#line 514 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 2988 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list_opt: /* parameter_list_opt  */
#line 517 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2994 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_default: /* parameter_default  */
#line 511 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3000 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list: /* parameter_list  */
#line 517 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3006 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_name: /* field_name  */
#line 510 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3012 "src/parser.tab.c"
        break;

    case YYSYMBOL_dot_field_name: /* dot_field_name  */
#line 510 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3018 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_field_list: /* record_field_list  */
#line 515 "src/parser.y"
            { ast_free_record_field_list(((*yyvaluep).record_field_list)); }
#line 3024 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_policy: /* field_policy  */
#line 521 "src/parser.y"
            { ast_free_expr(((*yyvaluep).field_policy).reset_expr); }
#line 3030 "src/parser.tab.c"
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
#line 538 "src/parser.y"
                     { ctx->parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3336 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 542 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3342 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 543 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3348 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 544 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3354 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 548 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3360 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 549 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3366 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 550 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3372 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 551 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3378 "src/parser.tab.c"
    break;

  case 10: /* statement: for_each_statement  */
#line 552 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3384 "src/parser.tab.c"
    break;

  case 11: /* statement: while_statement  */
#line 553 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3390 "src/parser.tab.c"
    break;

  case 12: /* statement: do_loop_statement  */
#line 554 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3396 "src/parser.tab.c"
    break;

  case 13: /* statement: consider_statement  */
#line 555 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3402 "src/parser.tab.c"
    break;

  case 14: /* statement: function_statement  */
#line 556 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3408 "src/parser.tab.c"
    break;

  case 15: /* statement: modifier_statement  */
#line 557 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3414 "src/parser.tab.c"
    break;

  case 16: /* statement: program_statement  */
#line 558 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3420 "src/parser.tab.c"
    break;

  case 17: /* statement: library_statement  */
#line 559 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3426 "src/parser.tab.c"
    break;

  case 18: /* statement: use_statement NEWLINE  */
#line 560 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3432 "src/parser.tab.c"
    break;

  case 19: /* statement: watch_statement  */
#line 561 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3438 "src/parser.tab.c"
    break;

  case 20: /* statement: server_statement  */
#line 562 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3444 "src/parser.tab.c"
    break;

  case 21: /* statement: unwatch_statement NEWLINE  */
#line 563 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3450 "src/parser.tab.c"
    break;

  case 22: /* statement: without_watchers_statement  */
#line 564 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3456 "src/parser.tab.c"
    break;

  case 23: /* statement: on_error_statement NEWLINE  */
#line 565 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3462 "src/parser.tab.c"
    break;

  case 24: /* statement: error_statement NEWLINE  */
#line 566 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3468 "src/parser.tab.c"
    break;

  case 25: /* statement: return_statement NEWLINE  */
#line 567 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3474 "src/parser.tab.c"
    break;

  case 26: /* statement: label_statement NEWLINE  */
#line 568 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3480 "src/parser.tab.c"
    break;

  case 27: /* statement: goto_statement NEWLINE  */
#line 569 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3486 "src/parser.tab.c"
    break;

  case 28: /* statement: gosub_statement NEWLINE  */
#line 570 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3492 "src/parser.tab.c"
    break;

  case 29: /* statement: break_statement NEWLINE  */
#line 571 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3498 "src/parser.tab.c"
    break;

  case 30: /* statement: continue_statement NEWLINE  */
#line 572 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3504 "src/parser.tab.c"
    break;

  case 31: /* statement: if_statement  */
#line 573 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3510 "src/parser.tab.c"
    break;

  case 32: /* statement: DIM  */
#line 579 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 3523 "src/parser.tab.c"
    break;

  case 33: /* assignment: lvalue OP_EQ expression  */
#line 590 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 3529 "src/parser.tab.c"
    break;

  case 34: /* assignment: lvalue compound_op expression  */
#line 595 "src/parser.y"
                                    { (yyval.stmt) = ast_assign_op((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr), (yyvsp[-1].op_char)); }
#line 3535 "src/parser.tab.c"
    break;

  case 35: /* assignment: lvalue comparison_lens compound_op expression  */
#line 596 "src/parser.y"
                                                    {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign_op((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr), (yyvsp[-1].op_char));
      }
#line 3549 "src/parser.tab.c"
    break;

  case 36: /* assignment: lvalue comparison_lens OP_EQ expression  */
#line 608 "src/parser.y"
                                              {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 3563 "src/parser.tab.c"
    break;

  case 37: /* compound_op: PLUS_EQ  */
#line 620 "src/parser.y"
               { (yyval.op_char) = '+'; }
#line 3569 "src/parser.tab.c"
    break;

  case 38: /* compound_op: MINUS_EQ  */
#line 621 "src/parser.y"
               { (yyval.op_char) = '-'; }
#line 3575 "src/parser.tab.c"
    break;

  case 39: /* compound_op: STAR_EQ  */
#line 622 "src/parser.y"
               { (yyval.op_char) = '*'; }
#line 3581 "src/parser.tab.c"
    break;

  case 40: /* compound_op: SLASH_EQ  */
#line 623 "src/parser.y"
               { (yyval.op_char) = '/'; }
#line 3587 "src/parser.tab.c"
    break;

  case 41: /* lvalue: variable_name  */
#line 627 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3593 "src/parser.tab.c"
    break;

  case 42: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 628 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3599 "src/parser.tab.c"
    break;

  case 43: /* lvalue: lvalue DOT dot_field_name  */
#line 629 "src/parser.y"
                                             { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3605 "src/parser.tab.c"
    break;

  case 44: /* variable_name: IDENT  */
#line 633 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 3611 "src/parser.tab.c"
    break;

  case 45: /* variable_name: END  */
#line 634 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 3617 "src/parser.tab.c"
    break;

  case 46: /* variable_name: NEXT  */
#line 635 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 3623 "src/parser.tab.c"
    break;

  case 47: /* $@1: %empty  */
#line 646 "src/parser.y"
             { lexer_begin_lens_content(ctx->active_lexer); }
#line 3629 "src/parser.tab.c"
    break;

  case 48: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 646 "src/parser.y"
                                                                                  {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 3637 "src/parser.tab.c"
    break;

  case 49: /* modifier_name: modifier_word  */
#line 652 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 3643 "src/parser.tab.c"
    break;

  case 50: /* modifier_name: modifier_name modifier_word  */
#line 653 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 3649 "src/parser.tab.c"
    break;

  case 51: /* modifier_word: IDENT  */
#line 657 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 3655 "src/parser.tab.c"
    break;

  case 52: /* modifier_word: TO  */
#line 658 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 3661 "src/parser.tab.c"
    break;

  case 53: /* modifier_word: END  */
#line 659 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 3667 "src/parser.tab.c"
    break;

  case 54: /* modifier_word: NEXT  */
#line 660 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 3673 "src/parser.tab.c"
    break;

  case 55: /* print_statement: PRINT expression  */
#line 664 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 3679 "src/parser.tab.c"
    break;

  case 56: /* print_statement: PRINT TO ERROR_VALUE expression  */
#line 670 "src/parser.y"
                                      { (yyval.stmt) = ast_print_error((yyvsp[0].expr)); }
#line 3685 "src/parser.tab.c"
    break;

  case 57: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 674 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 3691 "src/parser.tab.c"
    break;

  case 58: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 675 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 3702 "src/parser.tab.c"
    break;

  case 59: /* call_statement: lvalue DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 681 "src/parser.y"
                                                       {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 3712 "src/parser.tab.c"
    break;

  case 60: /* call_statement: lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 686 "src/parser.y"
                                                                 {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 3726 "src/parser.tab.c"
    break;

  case 61: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 695 "src/parser.y"
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
#line 3741 "src/parser.tab.c"
    break;

  case 62: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 708 "src/parser.y"
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
#line 3764 "src/parser.tab.c"
    break;

  case 63: /* for_end: END FOR NEWLINE  */
#line 737 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 3770 "src/parser.tab.c"
    break;

  case 64: /* for_end: NEXT NEWLINE  */
#line 738 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 3776 "src/parser.tab.c"
    break;

  case 65: /* for_end: NEXT variable_name NEWLINE  */
#line 739 "src/parser.y"
                                 { (yyval.text) = (yyvsp[-1].text); }
#line 3782 "src/parser.tab.c"
    break;

  case 66: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list for_end  */
#line 743 "src/parser.y"
                                                             {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3791 "src/parser.tab.c"
    break;

  case 67: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list for_end  */
#line 747 "src/parser.y"
                                                                  {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3800 "src/parser.tab.c"
    break;

  case 68: /* for_each_statement: FOR IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 763 "src/parser.y"
                                                                         {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { YYERROR; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3810 "src/parser.tab.c"
    break;

  case 69: /* for_each_statement: FOR EACH IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 768 "src/parser.y"
                                                                              {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { YYERROR; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3820 "src/parser.tab.c"
    break;

  case 70: /* for_each_statement: FOR IDENT OP_EQ expression TO expression NEWLINE statement_list for_end  */
#line 776 "src/parser.y"
                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].expr), NULL, (yyvsp[-1].stmt_list));
      }
#line 3829 "src/parser.tab.c"
    break;

  case 71: /* for_each_statement: FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list for_end  */
#line 780 "src/parser.y"
                                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-9].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-9].text), (yyvsp[-7].expr), (yyvsp[-5].expr), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 3838 "src/parser.tab.c"
    break;

  case 72: /* do_loop_statement: DO NEWLINE statement_list UNTIL expression NEWLINE  */
#line 801 "src/parser.y"
                                                         {
        (yyval.stmt) = ast_do_loop((yyvsp[-3].stmt_list), (yyvsp[-1].expr));
      }
#line 3846 "src/parser.tab.c"
    break;

  case 73: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 807 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 3854 "src/parser.tab.c"
    break;

  case 74: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 813 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 3862 "src/parser.tab.c"
    break;

  case 75: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 819 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 3870 "src/parser.tab.c"
    break;

  case 76: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 822 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 3878 "src/parser.tab.c"
    break;

  case 77: /* consider_else_opt: %empty  */
#line 828 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3884 "src/parser.tab.c"
    break;

  case 78: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 829 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3890 "src/parser.tab.c"
    break;

  case 79: /* consider_statement_list: %empty  */
#line 833 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3896 "src/parser.tab.c"
    break;

  case 80: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 834 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3902 "src/parser.tab.c"
    break;

  case 81: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 835 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3908 "src/parser.tab.c"
    break;

  case 82: /* consider_body_statement: assignment NEWLINE  */
#line 839 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3914 "src/parser.tab.c"
    break;

  case 83: /* consider_body_statement: print_statement NEWLINE  */
#line 840 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3920 "src/parser.tab.c"
    break;

  case 84: /* consider_body_statement: call_statement NEWLINE  */
#line 841 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3926 "src/parser.tab.c"
    break;

  case 85: /* consider_body_statement: with_lock_statement  */
#line 842 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3932 "src/parser.tab.c"
    break;

  case 86: /* consider_body_statement: for_each_statement  */
#line 843 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3938 "src/parser.tab.c"
    break;

  case 87: /* consider_body_statement: while_statement  */
#line 844 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3944 "src/parser.tab.c"
    break;

  case 88: /* consider_body_statement: do_loop_statement  */
#line 845 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3950 "src/parser.tab.c"
    break;

  case 89: /* consider_body_statement: consider_statement  */
#line 846 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3956 "src/parser.tab.c"
    break;

  case 90: /* consider_body_statement: function_statement  */
#line 847 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3962 "src/parser.tab.c"
    break;

  case 91: /* consider_body_statement: modifier_statement  */
#line 848 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3968 "src/parser.tab.c"
    break;

  case 92: /* consider_body_statement: program_statement  */
#line 849 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3974 "src/parser.tab.c"
    break;

  case 93: /* consider_body_statement: library_statement  */
#line 850 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3980 "src/parser.tab.c"
    break;

  case 94: /* consider_body_statement: use_statement NEWLINE  */
#line 851 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3986 "src/parser.tab.c"
    break;

  case 95: /* consider_body_statement: watch_statement  */
#line 852 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3992 "src/parser.tab.c"
    break;

  case 96: /* consider_body_statement: unwatch_statement NEWLINE  */
#line 853 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3998 "src/parser.tab.c"
    break;

  case 97: /* consider_body_statement: without_watchers_statement  */
#line 854 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4004 "src/parser.tab.c"
    break;

  case 98: /* consider_body_statement: on_error_statement NEWLINE  */
#line 855 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4010 "src/parser.tab.c"
    break;

  case 99: /* consider_body_statement: error_statement NEWLINE  */
#line 856 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4016 "src/parser.tab.c"
    break;

  case 100: /* consider_body_statement: return_statement NEWLINE  */
#line 857 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4022 "src/parser.tab.c"
    break;

  case 101: /* consider_body_statement: label_statement NEWLINE  */
#line 858 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4028 "src/parser.tab.c"
    break;

  case 102: /* consider_body_statement: goto_statement NEWLINE  */
#line 859 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4034 "src/parser.tab.c"
    break;

  case 103: /* consider_body_statement: gosub_statement NEWLINE  */
#line 860 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4040 "src/parser.tab.c"
    break;

  case 104: /* consider_body_statement: break_statement NEWLINE  */
#line 861 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4046 "src/parser.tab.c"
    break;

  case 105: /* consider_body_statement: continue_statement NEWLINE  */
#line 862 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4052 "src/parser.tab.c"
    break;

  case 106: /* consider_body_statement: if_statement  */
#line 863 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4058 "src/parser.tab.c"
    break;

  case 107: /* consider_body_statement: DIM  */
#line 869 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 4071 "src/parser.tab.c"
    break;

  case 108: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 880 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4079 "src/parser.tab.c"
    break;

  case 109: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 883 "src/parser.y"
                                                                                                            {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4088 "src/parser.tab.c"
    break;

  case 110: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 890 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 4096 "src/parser.tab.c"
    break;

  case 111: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 893 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 4104 "src/parser.tab.c"
    break;

  case 112: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 899 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4112 "src/parser.tab.c"
    break;

  case 113: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 905 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 4120 "src/parser.tab.c"
    break;

  case 114: /* use_statement: USE IDENT  */
#line 911 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4126 "src/parser.tab.c"
    break;

  case 115: /* use_statement: LOAD IDENT  */
#line 912 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4132 "src/parser.tab.c"
    break;

  case 116: /* use_statement: USE STRING  */
#line 913 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4138 "src/parser.tab.c"
    break;

  case 117: /* use_statement: LOAD STRING  */
#line 914 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4144 "src/parser.tab.c"
    break;

  case 118: /* use_statement: LOAD IDENT AS IDENT  */
#line 915 "src/parser.y"
                          { (yyval.stmt) = ast_use((yyvsp[-2].text), NULL, (yyvsp[0].text)); }
#line 4150 "src/parser.tab.c"
    break;

  case 119: /* use_statement: USE IDENT IDENT STRING  */
#line 916 "src/parser.y"
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
#line 4171 "src/parser.tab.c"
    break;

  case 120: /* use_statement: LOAD IDENT IDENT STRING  */
#line 932 "src/parser.y"
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
#line 4192 "src/parser.tab.c"
    break;

  case 121: /* use_statement: LOAD IDENT IDENT STRING AS IDENT  */
#line 948 "src/parser.y"
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
#line 4215 "src/parser.tab.c"
    break;

  case 122: /* modifier_signature: modifier_name  */
#line 969 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 4221 "src/parser.tab.c"
    break;

  case 123: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 970 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 4227 "src/parser.tab.c"
    break;

  case 124: /* modifier_context: IDENT  */
#line 974 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4233 "src/parser.tab.c"
    break;

  case 125: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 978 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4241 "src/parser.tab.c"
    break;

  case 126: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 981 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 4249 "src/parser.tab.c"
    break;

  case 127: /* watch_statement: WATCH IDENT LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 989 "src/parser.y"
                                                                                           {
        (yyval.stmt) = ast_watch((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4257 "src/parser.tab.c"
    break;

  case 128: /* unwatch_statement: UNWATCH expression  */
#line 995 "src/parser.y"
                         { (yyval.stmt) = ast_unwatch((yyvsp[0].expr)); }
#line 4263 "src/parser.tab.c"
    break;

  case 129: /* watch_target_list: watch_target_path  */
#line 999 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4269 "src/parser.tab.c"
    break;

  case 130: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 1000 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4275 "src/parser.tab.c"
    break;

  case 131: /* server_statement: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1019 "src/parser.y"
                                                                                             {
        (yyval.stmt) = ast_server((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4283 "src/parser.tab.c"
    break;

  case 132: /* server_statement: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1022 "src/parser.y"
                                                                           {
        (yyval.stmt) = ast_server((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4291 "src/parser.tab.c"
    break;

  case 133: /* server_item_list: %empty  */
#line 1028 "src/parser.y"
             { (yyval.server_item_list) = ast_server_item_list_empty(); }
#line 4297 "src/parser.tab.c"
    break;

  case 134: /* server_item_list: server_item_list NEWLINE  */
#line 1029 "src/parser.y"
                               { (yyval.server_item_list) = (yyvsp[-1].server_item_list); }
#line 4303 "src/parser.tab.c"
    break;

  case 135: /* server_item_list: server_item_list server_item  */
#line 1030 "src/parser.y"
                                   { (yyval.server_item_list) = ast_server_item_list_append((yyvsp[-1].server_item_list), (yyvsp[0].server_item)); }
#line 4309 "src/parser.tab.c"
    break;

  case 136: /* server_item: IDENT server_string_list NEWLINE  */
#line 1034 "src/parser.y"
                                       {
        (yyval.server_item) = ast_server_directive((yyvsp[-2].text), (yyvsp[-1].name_list), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 4317 "src/parser.tab.c"
    break;

  case 137: /* server_item: IDENT STRING LPAREN parameter_list_opt RPAREN NEWLINE statement_list END IDENT NEWLINE  */
#line 1037 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_handler((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4325 "src/parser.tab.c"
    break;

  case 138: /* server_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1040 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_site((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4333 "src/parser.tab.c"
    break;

  case 139: /* server_item: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1043 "src/parser.y"
                                                                           {
        (yyval.server_item) = ast_server_site((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 4341 "src/parser.tab.c"
    break;

  case 140: /* server_item: ON IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1046 "src/parser.y"
                                                     {
        (yyval.server_item) = ast_server_hook((yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 4349 "src/parser.tab.c"
    break;

  case 141: /* server_string_list: STRING  */
#line 1052 "src/parser.y"
             { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4355 "src/parser.tab.c"
    break;

  case 142: /* server_string_list: server_string_list COMMA STRING  */
#line 1053 "src/parser.y"
                                      { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4361 "src/parser.tab.c"
    break;

  case 143: /* watch_target_path: variable_name  */
#line 1057 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 4367 "src/parser.tab.c"
    break;

  case 144: /* watch_target_path: watch_target_path DOT IDENT  */
#line 1058 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 4373 "src/parser.tab.c"
    break;

  case 145: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 1062 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 4381 "src/parser.tab.c"
    break;

  case 146: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 1068 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 4387 "src/parser.tab.c"
    break;

  case 147: /* on_error_statement: ON ERROR_VALUE GOTO NEXT  */
#line 1069 "src/parser.y"
                               { (yyval.stmt) = ast_on_error_goto_next(); }
#line 4393 "src/parser.tab.c"
    break;

  case 148: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 1070 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 4399 "src/parser.tab.c"
    break;

  case 149: /* on_error_statement: ON IDENT GOTO NEXT  */
#line 1071 "src/parser.y"
                         {
        if (!warn_channel_ok(ctx, (yyvsp[-2].text), (yylsp[-2]).first_line, (yylsp[-2]).first_column)) { YYERROR; }
        free((yyvsp[-2].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_NEXT);
      }
#line 4409 "src/parser.tab.c"
    break;

  case 150: /* on_error_statement: ON IDENT GOTO IDENT  */
#line 1076 "src/parser.y"
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
#line 4427 "src/parser.tab.c"
    break;

  case 151: /* on_error_statement: ON IDENT STOP  */
#line 1089 "src/parser.y"
                    {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_STOP);
      }
#line 4437 "src/parser.tab.c"
    break;

  case 152: /* on_error_statement: ON IDENT PRINT  */
#line 1094 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_PRINT);
      }
#line 4447 "src/parser.tab.c"
    break;

  case 153: /* on_error_statement: ON IDENT IDENT  */
#line 1099 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        int mode = warn_mode_word(ctx, (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column);
        if (mode < 0) { free((yyvsp[-1].text)); free((yyvsp[0].text)); YYERROR; }
        free((yyvsp[-1].text)); free((yyvsp[0].text));
        (yyval.stmt) = ast_on_warning(mode);
      }
#line 4459 "src/parser.tab.c"
    break;

  case 154: /* error_statement: ERROR_VALUE expression  */
#line 1109 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 4465 "src/parser.tab.c"
    break;

  case 155: /* return_statement: RETURN  */
#line 1113 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 4471 "src/parser.tab.c"
    break;

  case 156: /* return_statement: RETURN expression  */
#line 1114 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 4477 "src/parser.tab.c"
    break;

  case 157: /* label_statement: variable_name COLON  */
#line 1118 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 4483 "src/parser.tab.c"
    break;

  case 158: /* goto_statement: GOTO variable_name  */
#line 1125 "src/parser.y"
                         { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 4489 "src/parser.tab.c"
    break;

  case 159: /* gosub_statement: GOSUB variable_name  */
#line 1129 "src/parser.y"
                          { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 4495 "src/parser.tab.c"
    break;

  case 160: /* break_statement: BREAK  */
#line 1138 "src/parser.y"
            { (yyval.stmt) = ast_break(NULL); }
#line 4501 "src/parser.tab.c"
    break;

  case 161: /* break_statement: BREAK IDENT  */
#line 1139 "src/parser.y"
                  { (yyval.stmt) = ast_break((yyvsp[0].text)); }
#line 4507 "src/parser.tab.c"
    break;

  case 162: /* continue_statement: CONTINUE  */
#line 1143 "src/parser.y"
               { (yyval.stmt) = ast_continue(NULL); }
#line 4513 "src/parser.tab.c"
    break;

  case 163: /* continue_statement: CONTINUE IDENT  */
#line 1144 "src/parser.y"
                     { (yyval.stmt) = ast_continue((yyvsp[0].text)); }
#line 4519 "src/parser.tab.c"
    break;

  case 164: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1148 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4528 "src/parser.tab.c"
    break;

  case 165: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1152 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4537 "src/parser.tab.c"
    break;

  case 166: /* if_block_tail: END IF NEWLINE  */
#line 1159 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4545 "src/parser.tab.c"
    break;

  case 167: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 1162 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4553 "src/parser.tab.c"
    break;

  case 168: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1165 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4561 "src/parser.tab.c"
    break;

  case 169: /* if_block_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1174 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 4573 "src/parser.tab.c"
    break;

  case 170: /* if_inline_tail: %empty  */
#line 1184 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4581 "src/parser.tab.c"
    break;

  case 171: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 1187 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4589 "src/parser.tab.c"
    break;

  case 172: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1190 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4597 "src/parser.tab.c"
    break;

  case 173: /* if_inline_tail: ELSE IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1196 "src/parser.y"
                                                                      {
        AstStmt *inner = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 4609 "src/parser.tab.c"
    break;

  case 174: /* if_inline_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1203 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 4621 "src/parser.tab.c"
    break;

  case 175: /* inline_statement: assignment  */
#line 1213 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4627 "src/parser.tab.c"
    break;

  case 176: /* inline_statement: print_statement  */
#line 1214 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4633 "src/parser.tab.c"
    break;

  case 177: /* inline_statement: call_statement  */
#line 1215 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4639 "src/parser.tab.c"
    break;

  case 178: /* inline_statement: use_statement  */
#line 1216 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4645 "src/parser.tab.c"
    break;

  case 179: /* inline_statement: on_error_statement  */
#line 1217 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4651 "src/parser.tab.c"
    break;

  case 180: /* inline_statement: error_statement  */
#line 1218 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4657 "src/parser.tab.c"
    break;

  case 181: /* inline_statement: return_statement  */
#line 1219 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4663 "src/parser.tab.c"
    break;

  case 182: /* inline_statement: goto_statement  */
#line 1220 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4669 "src/parser.tab.c"
    break;

  case 183: /* inline_statement: gosub_statement  */
#line 1221 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4675 "src/parser.tab.c"
    break;

  case 184: /* inline_statement: break_statement  */
#line 1222 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4681 "src/parser.tab.c"
    break;

  case 185: /* inline_statement: continue_statement  */
#line 1223 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4687 "src/parser.tab.c"
    break;

  case 186: /* expression: or_expression  */
#line 1227 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 4693 "src/parser.tab.c"
    break;

  case 187: /* or_expression: and_expression  */
#line 1231 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 4699 "src/parser.tab.c"
    break;

  case 188: /* or_expression: or_expression OR and_expression  */
#line 1232 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4705 "src/parser.tab.c"
    break;

  case 189: /* and_expression: not_expression  */
#line 1236 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 4711 "src/parser.tab.c"
    break;

  case 190: /* and_expression: and_expression AND not_expression  */
#line 1237 "src/parser.y"
                                        { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4717 "src/parser.tab.c"
    break;

  case 191: /* not_expression: comparison_expression  */
#line 1259 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 4723 "src/parser.tab.c"
    break;

  case 192: /* not_expression: NOT not_expression  */
#line 1260 "src/parser.y"
                         { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4729 "src/parser.tab.c"
    break;

  case 193: /* comparison_expression: additive_expression  */
#line 1264 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 4735 "src/parser.tab.c"
    break;

  case 194: /* comparison_expression: additive_expression comparison_operator additive_expression  */
#line 1265 "src/parser.y"
                                                                  { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4741 "src/parser.tab.c"
    break;

  case 195: /* comparison_expression: additive_expression comparison_lens comparison_operator additive_expression  */
#line 1266 "src/parser.y"
                                                                                  {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 4749 "src/parser.tab.c"
    break;

  case 196: /* additive_expression: multiplicative_expression  */
#line 1272 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 4755 "src/parser.tab.c"
    break;

  case 197: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 1273 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4761 "src/parser.tab.c"
    break;

  case 198: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 1274 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4767 "src/parser.tab.c"
    break;

  case 199: /* multiplicative_expression: unary_expression  */
#line 1278 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 4773 "src/parser.tab.c"
    break;

  case 200: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 1279 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4779 "src/parser.tab.c"
    break;

  case 201: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 1280 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4785 "src/parser.tab.c"
    break;

  case 202: /* unary_expression: postfix_expression  */
#line 1284 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 4791 "src/parser.tab.c"
    break;

  case 203: /* unary_expression: MINUS unary_expression  */
#line 1285 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4797 "src/parser.tab.c"
    break;

  case 204: /* unary_expression: NEW postfix_expression  */
#line 1286 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4803 "src/parser.tab.c"
    break;

  case 205: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 1287 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 4809 "src/parser.tab.c"
    break;

  case 206: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 1288 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4815 "src/parser.tab.c"
    break;

  case 207: /* postfix_expression: primary  */
#line 1292 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 4821 "src/parser.tab.c"
    break;

  case 208: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 1293 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4827 "src/parser.tab.c"
    break;

  case 209: /* postfix_expression: postfix_expression DOT dot_field_name  */
#line 1294 "src/parser.y"
                                            { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4833 "src/parser.tab.c"
    break;

  case 210: /* postfix_expression: postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1295 "src/parser.y"
                                                                   {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        (yyval.expr) = expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 4843 "src/parser.tab.c"
    break;

  case 211: /* postfix_expression: postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1300 "src/parser.y"
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
#line 4859 "src/parser.tab.c"
    break;

  case 212: /* comparison_operator: OP_EQ  */
#line 1314 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 4865 "src/parser.tab.c"
    break;

  case 213: /* comparison_operator: OP_NE  */
#line 1315 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 4871 "src/parser.tab.c"
    break;

  case 214: /* comparison_operator: OP_GT  */
#line 1316 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 4877 "src/parser.tab.c"
    break;

  case 215: /* comparison_operator: OP_LT  */
#line 1317 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 4883 "src/parser.tab.c"
    break;

  case 216: /* comparison_operator: OP_GE  */
#line 1318 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 4889 "src/parser.tab.c"
    break;

  case 217: /* comparison_operator: OP_LE  */
#line 1319 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 4895 "src/parser.tab.c"
    break;

  case 218: /* comparison_operator: OP_NGT  */
#line 1320 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 4901 "src/parser.tab.c"
    break;

  case 219: /* comparison_operator: OP_NLT  */
#line 1321 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 4907 "src/parser.tab.c"
    break;

  case 220: /* comparison_operator: OP_NGE  */
#line 1322 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 4913 "src/parser.tab.c"
    break;

  case 221: /* comparison_operator: OP_NLE  */
#line 1323 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 4919 "src/parser.tab.c"
    break;

  case 222: /* primary: NUMBER  */
#line 1327 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4925 "src/parser.tab.c"
    break;

  case 223: /* primary: WATCHERS LPAREN RPAREN  */
#line 1328 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_call(copy_const("watchers"), ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4931 "src/parser.tab.c"
    break;

  case 224: /* primary: duration_terms  */
#line 1329 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4937 "src/parser.tab.c"
    break;

  case 225: /* primary: STRING  */
#line 1330 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4943 "src/parser.tab.c"
    break;

  case 226: /* primary: variable_name ident_suffix  */
#line 1331 "src/parser.y"
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
#line 4965 "src/parser.tab.c"
    break;

  case 227: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1348 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 4976 "src/parser.tab.c"
    break;

  case 228: /* primary: ERROR_VALUE  */
#line 1354 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4982 "src/parser.tab.c"
    break;

  case 229: /* primary: TRUE  */
#line 1355 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4988 "src/parser.tab.c"
    break;

  case 230: /* primary: FALSE  */
#line 1356 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4994 "src/parser.tab.c"
    break;

  case 231: /* primary: NOTHING  */
#line 1357 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5000 "src/parser.tab.c"
    break;

  case 232: /* primary: UNKNOWN_VALUE  */
#line 1358 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5006 "src/parser.tab.c"
    break;

  case 233: /* primary: LPAREN expression RPAREN  */
#line 1359 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 5012 "src/parser.tab.c"
    break;

  case 234: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 1360 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5018 "src/parser.tab.c"
    break;

  case 235: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 1361 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5024 "src/parser.tab.c"
    break;

  case 236: /* primary: record_literal  */
#line 1362 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5030 "src/parser.tab.c"
    break;

  case 237: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 1366 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5036 "src/parser.tab.c"
    break;

  case 238: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 1367 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5042 "src/parser.tab.c"
    break;

  case 239: /* ident_suffix: %empty  */
#line 1371 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 5052 "src/parser.tab.c"
    break;

  case 240: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 1376 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5062 "src/parser.tab.c"
    break;

  case 241: /* ident_suffix: DOT dot_field_name ident_dot_suffix  */
#line 1381 "src/parser.y"
                                          {
        /* dot_field_name, not IDENT: a keyword is a legal FIELD name after a
         * dot, because nothing but a name can appear there. */
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 5073 "src/parser.tab.c"
    break;

  case 242: /* ident_suffix: DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1387 "src/parser.y"
                                                          {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        (yyval.ident_suffix).kind = IDENT_SUFFIX_METHOD;
        (yyval.ident_suffix).name = (yyvsp[-3].text);
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5087 "src/parser.tab.c"
    break;

  case 243: /* ident_dot_suffix: %empty  */
#line 1399 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 5097 "src/parser.tab.c"
    break;

  case 244: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 1404 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5107 "src/parser.tab.c"
    break;

  case 245: /* duration_terms: NUMBER IDENT  */
#line 1412 "src/parser.y"
                   {
        AstDuration duration = {0};
        (yyval.duration) = duration_add_unit(duration, (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 5116 "src/parser.tab.c"
    break;

  case 246: /* duration_terms: duration_terms NUMBER IDENT  */
#line 1416 "src/parser.y"
                                  {
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 5124 "src/parser.tab.c"
    break;

  case 247: /* argument_list_opt: %empty  */
#line 1422 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 5130 "src/parser.tab.c"
    break;

  case 248: /* argument_list_opt: argument_list  */
#line 1423 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 5136 "src/parser.tab.c"
    break;

  case 249: /* argument_list: expression  */
#line 1427 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 5142 "src/parser.tab.c"
    break;

  case 250: /* argument_list: argument_list COMMA expression  */
#line 1428 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 5148 "src/parser.tab.c"
    break;

  case 251: /* array_argument_list: expression  */
#line 1432 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 5154 "src/parser.tab.c"
    break;

  case 252: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 1433 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 5160 "src/parser.tab.c"
    break;

  case 253: /* parameter_list_opt: %empty  */
#line 1437 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 5166 "src/parser.tab.c"
    break;

  case 254: /* parameter_list_opt: parameter_list  */
#line 1438 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 5172 "src/parser.tab.c"
    break;

  case 255: /* parameter_default: NUMBER  */
#line 1452 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5178 "src/parser.tab.c"
    break;

  case 256: /* parameter_default: MINUS NUMBER  */
#line 1453 "src/parser.y"
                   { (yyval.expr) = expr_at(ast_number(-(yyvsp[0].number)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5184 "src/parser.tab.c"
    break;

  case 257: /* parameter_default: PLUS NUMBER  */
#line 1454 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5190 "src/parser.tab.c"
    break;

  case 258: /* parameter_default: STRING  */
#line 1455 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5196 "src/parser.tab.c"
    break;

  case 259: /* parameter_default: TRUE  */
#line 1456 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5202 "src/parser.tab.c"
    break;

  case 260: /* parameter_default: FALSE  */
#line 1457 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5208 "src/parser.tab.c"
    break;

  case 261: /* parameter_default: NOTHING  */
#line 1458 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5214 "src/parser.tab.c"
    break;

  case 262: /* parameter_default: UNKNOWN_VALUE  */
#line 1459 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5220 "src/parser.tab.c"
    break;

  case 263: /* parameter_list: IDENT  */
#line 1463 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5226 "src/parser.tab.c"
    break;

  case 264: /* parameter_list: IDENT OP_EQ parameter_default  */
#line 1464 "src/parser.y"
                                    {
        (yyval.name_list) = ast_name_list_append_default(ast_name_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 5234 "src/parser.tab.c"
    break;

  case 265: /* parameter_list: parameter_list COMMA IDENT  */
#line 1467 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5240 "src/parser.tab.c"
    break;

  case 266: /* parameter_list: parameter_list COMMA IDENT OP_EQ parameter_default  */
#line 1468 "src/parser.y"
                                                         {
        (yyval.name_list) = ast_name_list_append_default((yyvsp[-4].name_list), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 5248 "src/parser.tab.c"
    break;

  case 267: /* field_name: dot_field_name  */
#line 1483 "src/parser.y"
                     { (yyval.text) = (yyvsp[0].text); }
#line 5254 "src/parser.tab.c"
    break;

  case 268: /* field_name: STRING  */
#line 1490 "src/parser.y"
             { (yyval.text) = (yyvsp[0].text); }
#line 5260 "src/parser.tab.c"
    break;

  case 269: /* dot_field_name: IDENT  */
#line 1499 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 5266 "src/parser.tab.c"
    break;

  case 270: /* dot_field_name: AS  */
#line 1500 "src/parser.y"
                     { (yyval.text) = kw_name("as"); }
#line 5272 "src/parser.tab.c"
    break;

  case 271: /* dot_field_name: NEXT  */
#line 1501 "src/parser.y"
                     { (yyval.text) = kw_name("next"); }
#line 5278 "src/parser.tab.c"
    break;

  case 272: /* dot_field_name: STOP  */
#line 1502 "src/parser.y"
                     { (yyval.text) = kw_name("stop"); }
#line 5284 "src/parser.tab.c"
    break;

  case 273: /* dot_field_name: ERROR_VALUE  */
#line 1503 "src/parser.y"
                     { (yyval.text) = kw_name("error"); }
#line 5290 "src/parser.tab.c"
    break;

  case 274: /* dot_field_name: END  */
#line 1504 "src/parser.y"
                     { (yyval.text) = kw_name("end"); }
#line 5296 "src/parser.tab.c"
    break;

  case 275: /* dot_field_name: TO  */
#line 1505 "src/parser.y"
                     { (yyval.text) = kw_name("to"); }
#line 5302 "src/parser.tab.c"
    break;

  case 276: /* dot_field_name: IN  */
#line 1506 "src/parser.y"
                     { (yyval.text) = kw_name("in"); }
#line 5308 "src/parser.tab.c"
    break;

  case 277: /* dot_field_name: ON  */
#line 1507 "src/parser.y"
                     { (yyval.text) = kw_name("on"); }
#line 5314 "src/parser.tab.c"
    break;

  case 278: /* dot_field_name: NEW  */
#line 1508 "src/parser.y"
                     { (yyval.text) = kw_name("new"); }
#line 5320 "src/parser.tab.c"
    break;

  case 279: /* dot_field_name: EACH  */
#line 1509 "src/parser.y"
                     { (yyval.text) = kw_name("each"); }
#line 5326 "src/parser.tab.c"
    break;

  case 280: /* dot_field_name: WITH  */
#line 1510 "src/parser.y"
                     { (yyval.text) = kw_name("with"); }
#line 5332 "src/parser.tab.c"
    break;

  case 281: /* dot_field_name: WITHOUT  */
#line 1511 "src/parser.y"
                     { (yyval.text) = kw_name("without"); }
#line 5338 "src/parser.tab.c"
    break;

  case 282: /* dot_field_name: THEN  */
#line 1512 "src/parser.y"
                     { (yyval.text) = kw_name("then"); }
#line 5344 "src/parser.tab.c"
    break;

  case 283: /* dot_field_name: ELSE  */
#line 1513 "src/parser.y"
                     { (yyval.text) = kw_name("else"); }
#line 5350 "src/parser.tab.c"
    break;

  case 284: /* dot_field_name: FOR  */
#line 1514 "src/parser.y"
                     { (yyval.text) = kw_name("for"); }
#line 5356 "src/parser.tab.c"
    break;

  case 285: /* dot_field_name: IF  */
#line 1515 "src/parser.y"
                     { (yyval.text) = kw_name("if"); }
#line 5362 "src/parser.tab.c"
    break;

  case 286: /* dot_field_name: WHILE  */
#line 1516 "src/parser.y"
                     { (yyval.text) = kw_name("while"); }
#line 5368 "src/parser.tab.c"
    break;

  case 287: /* dot_field_name: DO  */
#line 1517 "src/parser.y"
                     { (yyval.text) = kw_name("do"); }
#line 5374 "src/parser.tab.c"
    break;

  case 288: /* dot_field_name: UNTIL  */
#line 1518 "src/parser.y"
                     { (yyval.text) = kw_name("until"); }
#line 5380 "src/parser.tab.c"
    break;

  case 289: /* dot_field_name: PRINT  */
#line 1519 "src/parser.y"
                     { (yyval.text) = kw_name("print"); }
#line 5386 "src/parser.tab.c"
    break;

  case 290: /* dot_field_name: RETURN  */
#line 1520 "src/parser.y"
                     { (yyval.text) = kw_name("return"); }
#line 5392 "src/parser.tab.c"
    break;

  case 291: /* dot_field_name: LOAD  */
#line 1521 "src/parser.y"
                     { (yyval.text) = kw_name("load"); }
#line 5398 "src/parser.tab.c"
    break;

  case 292: /* dot_field_name: USE  */
#line 1522 "src/parser.y"
                     { (yyval.text) = kw_name("use"); }
#line 5404 "src/parser.tab.c"
    break;

  case 293: /* dot_field_name: NOT  */
#line 1523 "src/parser.y"
                     { (yyval.text) = kw_name("not"); }
#line 5410 "src/parser.tab.c"
    break;

  case 294: /* dot_field_name: AND  */
#line 1524 "src/parser.y"
                     { (yyval.text) = kw_name("and"); }
#line 5416 "src/parser.tab.c"
    break;

  case 295: /* dot_field_name: OR  */
#line 1525 "src/parser.y"
                     { (yyval.text) = kw_name("or"); }
#line 5422 "src/parser.tab.c"
    break;

  case 296: /* dot_field_name: TRUE  */
#line 1526 "src/parser.y"
                     { (yyval.text) = kw_name("true"); }
#line 5428 "src/parser.tab.c"
    break;

  case 297: /* dot_field_name: FALSE  */
#line 1527 "src/parser.y"
                     { (yyval.text) = kw_name("false"); }
#line 5434 "src/parser.tab.c"
    break;

  case 298: /* dot_field_name: NOTHING  */
#line 1528 "src/parser.y"
                     { (yyval.text) = kw_name("nothing"); }
#line 5440 "src/parser.tab.c"
    break;

  case 299: /* dot_field_name: BREAK  */
#line 1529 "src/parser.y"
                     { (yyval.text) = kw_name("break"); }
#line 5446 "src/parser.tab.c"
    break;

  case 300: /* dot_field_name: CONTINUE  */
#line 1530 "src/parser.y"
                     { (yyval.text) = kw_name("continue"); }
#line 5452 "src/parser.tab.c"
    break;

  case 301: /* dot_field_name: GOTO  */
#line 1531 "src/parser.y"
                     { (yyval.text) = kw_name("goto"); }
#line 5458 "src/parser.tab.c"
    break;

  case 302: /* dot_field_name: GOSUB  */
#line 1532 "src/parser.y"
                     { (yyval.text) = kw_name("gosub"); }
#line 5464 "src/parser.tab.c"
    break;

  case 303: /* dot_field_name: SPAWN  */
#line 1533 "src/parser.y"
                     { (yyval.text) = kw_name("spawn"); }
#line 5470 "src/parser.tab.c"
    break;

  case 304: /* dot_field_name: EXPORT  */
#line 1534 "src/parser.y"
                     { (yyval.text) = kw_name("export"); }
#line 5476 "src/parser.tab.c"
    break;

  case 305: /* dot_field_name: LIBRARY  */
#line 1535 "src/parser.y"
                     { (yyval.text) = kw_name("library"); }
#line 5482 "src/parser.tab.c"
    break;

  case 306: /* dot_field_name: FUNCTION  */
#line 1536 "src/parser.y"
                     { (yyval.text) = kw_name("function"); }
#line 5488 "src/parser.tab.c"
    break;

  case 307: /* dot_field_name: MODIFIER  */
#line 1537 "src/parser.y"
                     { (yyval.text) = kw_name("modifier"); }
#line 5494 "src/parser.tab.c"
    break;

  case 308: /* dot_field_name: PROGRAM  */
#line 1538 "src/parser.y"
                     { (yyval.text) = kw_name("program"); }
#line 5500 "src/parser.tab.c"
    break;

  case 309: /* dot_field_name: WATCH  */
#line 1539 "src/parser.y"
                     { (yyval.text) = kw_name("watch"); }
#line 5506 "src/parser.tab.c"
    break;

  case 310: /* dot_field_name: WATCHERS  */
#line 1540 "src/parser.y"
                     { (yyval.text) = kw_name("watchers"); }
#line 5512 "src/parser.tab.c"
    break;

  case 311: /* dot_field_name: CONSIDER  */
#line 1541 "src/parser.y"
                     { (yyval.text) = kw_name("consider"); }
#line 5518 "src/parser.tab.c"
    break;

  case 312: /* dot_field_name: STEP  */
#line 1542 "src/parser.y"
                     { (yyval.text) = kw_name("step"); }
#line 5524 "src/parser.tab.c"
    break;

  case 313: /* dot_field_name: UNWATCH  */
#line 1543 "src/parser.y"
                     { (yyval.text) = kw_name("unwatch"); }
#line 5530 "src/parser.tab.c"
    break;

  case 314: /* dot_field_name: UNKNOWN_VALUE  */
#line 1544 "src/parser.y"
                     { (yyval.text) = kw_name("unknown"); }
#line 5536 "src/parser.tab.c"
    break;

  case 315: /* dot_field_name: DIM  */
#line 1545 "src/parser.y"
                     { (yyval.text) = kw_name("dim"); }
#line 5542 "src/parser.tab.c"
    break;

  case 316: /* record_field_list: field_name OP_EQ expression  */
#line 1549 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5548 "src/parser.tab.c"
    break;

  case 317: /* record_field_list: field_name COLON expression  */
#line 1550 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5554 "src/parser.tab.c"
    break;

  case 318: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1551 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5560 "src/parser.tab.c"
    break;

  case 319: /* record_field_list: record_field_list COMMA optional_newlines field_name OP_EQ expression  */
#line 1552 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5566 "src/parser.tab.c"
    break;

  case 320: /* record_field_list: record_field_list COMMA optional_newlines field_name COLON expression  */
#line 1553 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5572 "src/parser.tab.c"
    break;

  case 321: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1554 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5578 "src/parser.tab.c"
    break;

  case 322: /* field_policy: IDENT  */
#line 1562 "src/parser.y"
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
#line 5610 "src/parser.tab.c"
    break;

  case 323: /* field_policy: IDENT expression  */
#line 1589 "src/parser.y"
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
#line 5631 "src/parser.tab.c"
    break;


#line 5635 "src/parser.tab.c"

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

#line 1612 "src/parser.y"


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
