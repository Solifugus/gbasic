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

/* Report a diagnostic with an explicit span. Routes through gb_report_to, which
 * pushes to the per-parse sink (ctx->diags) or, when that is NULL, prints in the
 * legacy stderr format. */
static void report_diag(gb_parse_ctx *ctx, gb_diag_code code, int line, int column,
                        int end_line, int end_column, const char *message) {
    gb_span span = { line, column, end_line, end_column };
    gb_report_to(ctx->diags, code, 0, ctx->active_parse_path, span, message);
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

static int modifier_lparen_ahead(gb_parse_ctx *ctx, const char *start) {
    const char *p = start + 1;
    int saw_term = 0;

    while (*p == ' ' || *p == '\t' || *p == '\r') {
        p++;
    }
    while (*p && *p != ')' && *p != '\n') {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '_') {
            saw_term = 1;
            while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                   (*p >= '0' && *p <= '9') || *p == '_') {
                p++;
            }
        } else if ((*p >= '0' && *p <= '9') || *p == '"') {
            saw_term = 1;
            if (*p == '"') {
                p++;
                while (*p && *p != '"' && *p != '\n') {
                    p++;
                }
                if (*p != '"') {
                    return 0;
                }
                p++;
            } else {
                while (*p >= '0' && *p <= '9') {
                    p++;
                }
                if (*p == '.') {
                    p++;
                    while (*p >= '0' && *p <= '9') {
                        p++;
                    }
                }
            }
        } else if (*p == ' ' || *p == '\t' || *p == '\r') {
            p++;
        } else if (*p == ',') {
            return 0;
        } else if (*p == '+' || *p == '-' || *p == '*' || *p == '/' ||
                   *p == '.' || *p == '[' || *p == ']') {
            p++;
        } else {
            return 0;
        }
    }
    if (*p != ')' || !saw_term) {
        return 0;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r') {
        p++;
    }
    int followed_by_comparison = *p == '=' || *p == '>' || *p == '<' ||
        (*p == '!' && (p[1] == '=' || p[1] == '>' || p[1] == '<'));
    if (!followed_by_comparison) {
        return 0;
    }

    const char *name_end = start;
    while (name_end > ctx->active_lexer->source &&
           (name_end[-1] == ' ' || name_end[-1] == '\t' || name_end[-1] == '\r')) {
        name_end--;
    }
    const char *name_start = name_end;
    while (name_start > ctx->active_lexer->source &&
           ((name_start[-1] >= 'A' && name_start[-1] <= 'Z') ||
            (name_start[-1] >= 'a' && name_start[-1] <= 'z') ||
            (name_start[-1] >= '0' && name_start[-1] <= '9') ||
            name_start[-1] == '_')) {
        name_start--;
    }
    if (name_start < name_end) {
        char *name = copy_text(name_start, (int)(name_end - name_start));
        int is_function = gbasic_builtin_function(name) || source_declares_function(ctx, name);
        free(name);
        if (is_function) {
            return 0;
        }
    }

    return 1;
}


#line 530 "src/parser.tab.c"

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
  YYSYMBOL_MOD_CONTENT = 6,                /* MOD_CONTENT  */
  YYSYMBOL_LENS_CONTENT = 7,               /* LENS_CONTENT  */
  YYSYMBOL_QUALIFIED_IDENT = 8,            /* QUALIFIED_IDENT  */
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
  YYSYMBOL_IN = 29,                        /* IN  */
  YYSYMBOL_EACH = 30,                      /* EACH  */
  YYSYMBOL_WHILE = 31,                     /* WHILE  */
  YYSYMBOL_CONSIDER = 32,                  /* CONSIDER  */
  YYSYMBOL_BREAK = 33,                     /* BREAK  */
  YYSYMBOL_CONTINUE = 34,                  /* CONTINUE  */
  YYSYMBOL_FUNCTION = 35,                  /* FUNCTION  */
  YYSYMBOL_RETURN = 36,                    /* RETURN  */
  YYSYMBOL_GOTO = 37,                      /* GOTO  */
  YYSYMBOL_GOSUB = 38,                     /* GOSUB  */
  YYSYMBOL_WATCH = 39,                     /* WATCH  */
  YYSYMBOL_WITHOUT = 40,                   /* WITHOUT  */
  YYSYMBOL_WATCHERS = 41,                  /* WATCHERS  */
  YYSYMBOL_ON = 42,                        /* ON  */
  YYSYMBOL_RESUME = 43,                    /* RESUME  */
  YYSYMBOL_NEXT = 44,                      /* NEXT  */
  YYSYMBOL_STOP = 45,                      /* STOP  */
  YYSYMBOL_ERROR_VALUE = 46,               /* ERROR_VALUE  */
  YYSYMBOL_MODIFIER = 47,                  /* MODIFIER  */
  YYSYMBOL_PROGRAM = 48,                   /* PROGRAM  */
  YYSYMBOL_LIBRARY = 49,                   /* LIBRARY  */
  YYSYMBOL_LOAD = 50,                      /* LOAD  */
  YYSYMBOL_USE = 51,                       /* USE  */
  YYSYMBOL_EXPORT = 52,                    /* EXPORT  */
  YYSYMBOL_OP_EQ = 53,                     /* OP_EQ  */
  YYSYMBOL_OP_NE = 54,                     /* OP_NE  */
  YYSYMBOL_OP_GT = 55,                     /* OP_GT  */
  YYSYMBOL_OP_LT = 56,                     /* OP_LT  */
  YYSYMBOL_OP_GE = 57,                     /* OP_GE  */
  YYSYMBOL_OP_LE = 58,                     /* OP_LE  */
  YYSYMBOL_OP_NGT = 59,                    /* OP_NGT  */
  YYSYMBOL_OP_NLT = 60,                    /* OP_NLT  */
  YYSYMBOL_OP_NGE = 61,                    /* OP_NGE  */
  YYSYMBOL_OP_NLE = 62,                    /* OP_NLE  */
  YYSYMBOL_PLUS = 63,                      /* PLUS  */
  YYSYMBOL_MINUS = 64,                     /* MINUS  */
  YYSYMBOL_STAR = 65,                      /* STAR  */
  YYSYMBOL_SLASH = 66,                     /* SLASH  */
  YYSYMBOL_LPAREN = 67,                    /* LPAREN  */
  YYSYMBOL_MOD_LPAREN = 68,                /* MOD_LPAREN  */
  YYSYMBOL_RPAREN = 69,                    /* RPAREN  */
  YYSYMBOL_LBRACKET = 70,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 71,                  /* RBRACKET  */
  YYSYMBOL_LBRACE = 72,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 73,                    /* RBRACE  */
  YYSYMBOL_COMMA = 74,                     /* COMMA  */
  YYSYMBOL_COLON = 75,                     /* COLON  */
  YYSYMBOL_NEWLINE = 76,                   /* NEWLINE  */
  YYSYMBOL_IF_WITHOUT_ELSE = 77,           /* IF_WITHOUT_ELSE  */
  YYSYMBOL_NO_DOT = 78,                    /* NO_DOT  */
  YYSYMBOL_DOT = 79,                       /* DOT  */
  YYSYMBOL_YYACCEPT = 80,                  /* $accept  */
  YYSYMBOL_program = 81,                   /* program  */
  YYSYMBOL_statement_list = 82,            /* statement_list  */
  YYSYMBOL_statement = 83,                 /* statement  */
  YYSYMBOL_assignment = 84,                /* assignment  */
  YYSYMBOL_lvalue = 85,                    /* lvalue  */
  YYSYMBOL_variable_name = 86,             /* variable_name  */
  YYSYMBOL_modifier = 87,                  /* modifier  */
  YYSYMBOL_comparison_lens = 88,           /* comparison_lens  */
  YYSYMBOL_89_1 = 89,                      /* $@1  */
  YYSYMBOL_modifier_name = 90,             /* modifier_name  */
  YYSYMBOL_modifier_word = 91,             /* modifier_word  */
  YYSYMBOL_print_statement = 92,           /* print_statement  */
  YYSYMBOL_call_statement = 93,            /* call_statement  */
  YYSYMBOL_with_lock_statement = 94,       /* with_lock_statement  */
  YYSYMBOL_for_each_statement = 95,        /* for_each_statement  */
  YYSYMBOL_while_statement = 96,           /* while_statement  */
  YYSYMBOL_consider_statement = 97,        /* consider_statement  */
  YYSYMBOL_consider_branch_list = 98,      /* consider_branch_list  */
  YYSYMBOL_consider_else_opt = 99,         /* consider_else_opt  */
  YYSYMBOL_consider_statement_list = 100,  /* consider_statement_list  */
  YYSYMBOL_consider_body_statement = 101,  /* consider_body_statement  */
  YYSYMBOL_function_statement = 102,       /* function_statement  */
  YYSYMBOL_modifier_statement = 103,       /* modifier_statement  */
  YYSYMBOL_program_statement = 104,        /* program_statement  */
  YYSYMBOL_library_statement = 105,        /* library_statement  */
  YYSYMBOL_use_statement = 106,            /* use_statement  */
  YYSYMBOL_modifier_signature = 107,       /* modifier_signature  */
  YYSYMBOL_modifier_context = 108,         /* modifier_context  */
  YYSYMBOL_watch_statement = 109,          /* watch_statement  */
  YYSYMBOL_watch_target_list = 110,        /* watch_target_list  */
  YYSYMBOL_watch_target_path = 111,        /* watch_target_path  */
  YYSYMBOL_without_watchers_statement = 112, /* without_watchers_statement  */
  YYSYMBOL_on_error_statement = 113,       /* on_error_statement  */
  YYSYMBOL_error_statement = 114,          /* error_statement  */
  YYSYMBOL_return_statement = 115,         /* return_statement  */
  YYSYMBOL_label_statement = 116,          /* label_statement  */
  YYSYMBOL_goto_statement = 117,           /* goto_statement  */
  YYSYMBOL_gosub_statement = 118,          /* gosub_statement  */
  YYSYMBOL_break_statement = 119,          /* break_statement  */
  YYSYMBOL_continue_statement = 120,       /* continue_statement  */
  YYSYMBOL_if_statement = 121,             /* if_statement  */
  YYSYMBOL_if_block_tail = 122,            /* if_block_tail  */
  YYSYMBOL_if_inline_tail = 123,           /* if_inline_tail  */
  YYSYMBOL_inline_statement = 124,         /* inline_statement  */
  YYSYMBOL_expression = 125,               /* expression  */
  YYSYMBOL_or_expression = 126,            /* or_expression  */
  YYSYMBOL_and_expression = 127,           /* and_expression  */
  YYSYMBOL_comparison_expression = 128,    /* comparison_expression  */
  YYSYMBOL_additive_expression = 129,      /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 130, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 131,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 132,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 133,      /* comparison_operator  */
  YYSYMBOL_primary = 134,                  /* primary  */
  YYSYMBOL_record_literal = 135,           /* record_literal  */
  YYSYMBOL_ident_suffix = 136,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 137,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 138,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 139,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 140,            /* argument_list  */
  YYSYMBOL_array_argument_list = 141,      /* array_argument_list  */
  YYSYMBOL_parameter_list_opt = 142,       /* parameter_list_opt  */
  YYSYMBOL_parameter_list = 143,           /* parameter_list  */
  YYSYMBOL_record_field_list = 144,        /* record_field_list  */
  YYSYMBOL_field_policy = 145,             /* field_policy  */
  YYSYMBOL_optional_newlines = 146         /* optional_newlines  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;



/* Unqualified %code blocks.  */
#line 519 "src/parser.y"

static int yylex(YYSTYPE *lvalp, YYLTYPE *llocp, gb_parse_ctx *ctx);
static void yyerror(YYLTYPE *llocp, gb_parse_ctx *ctx, const char *message);
static void report_syntax_error(gb_parse_ctx *ctx, int line, int column,
                                int end_line, int end_column, const char *message);

#line 718 "src/parser.tab.c"

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
#define YYLAST   1570

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  80
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  67
/* YYNRULES -- Number of rules.  */
#define YYNRULES  218
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  484

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   334


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
      75,    76,    77,    78,    79
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   544,   544,   548,   549,   550,   554,   555,   556,   557,
     558,   559,   560,   561,   562,   563,   564,   565,   566,   567,
     568,   569,   570,   571,   572,   573,   574,   575,   576,   580,
     581,   593,   594,   595,   599,   600,   601,   605,   609,   609,
     615,   616,   620,   621,   622,   623,   627,   631,   632,   638,
     643,   652,   665,   679,   682,   688,   694,   700,   703,   709,
     710,   714,   715,   716,   720,   721,   722,   723,   724,   725,
     726,   727,   728,   729,   730,   731,   732,   733,   734,   735,
     736,   737,   738,   739,   740,   741,   742,   746,   749,   756,
     759,   765,   771,   777,   778,   779,   780,   781,   794,   810,
     811,   815,   819,   822,   828,   829,   833,   834,   838,   844,
     845,   846,   850,   854,   855,   859,   863,   867,   871,   875,
     879,   883,   890,   893,   896,   902,   905,   908,   914,   915,
     916,   917,   918,   919,   920,   921,   922,   923,   924,   928,
     932,   933,   937,   938,   942,   943,   944,   947,   959,   960,
     961,   965,   966,   967,   971,   972,   973,   974,   975,   976,
     980,   981,   982,   983,   988,  1002,  1003,  1004,  1005,  1006,
    1007,  1008,  1009,  1010,  1011,  1015,  1016,  1017,  1018,  1035,
    1041,  1042,  1043,  1044,  1045,  1046,  1047,  1048,  1049,  1053,
    1054,  1058,  1063,  1068,  1072,  1084,  1089,  1097,  1101,  1107,
    1108,  1112,  1113,  1117,  1118,  1122,  1123,  1127,  1128,  1132,
    1133,  1134,  1135,  1136,  1137,  1145,  1170,  1188,  1189
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
  "STRING", "MOD_CONTENT", "LENS_CONTENT", "QUALIFIED_IDENT", "IF",
  "CONSIDER_IF", "THEN", "ELSE", "CONSIDER_ELSE", "END", "END_CONSIDER",
  "PRINT", "TRUE", "FALSE", "NOTHING", "UNKNOWN_VALUE", "AND", "OR", "NOT",
  "WITH", "NEW", "SPAWN", "FOR", "TO", "IN", "EACH", "WHILE", "CONSIDER",
  "BREAK", "CONTINUE", "FUNCTION", "RETURN", "GOTO", "GOSUB", "WATCH",
  "WITHOUT", "WATCHERS", "ON", "RESUME", "NEXT", "STOP", "ERROR_VALUE",
  "MODIFIER", "PROGRAM", "LIBRARY", "LOAD", "USE", "EXPORT", "OP_EQ",
  "OP_NE", "OP_GT", "OP_LT", "OP_GE", "OP_LE", "OP_NGT", "OP_NLT",
  "OP_NGE", "OP_NLE", "PLUS", "MINUS", "STAR", "SLASH", "LPAREN",
  "MOD_LPAREN", "RPAREN", "LBRACKET", "RBRACKET", "LBRACE", "RBRACE",
  "COMMA", "COLON", "NEWLINE", "IF_WITHOUT_ELSE", "NO_DOT", "DOT",
  "$accept", "program", "statement_list", "statement", "assignment",
  "lvalue", "variable_name", "modifier", "comparison_lens", "$@1",
  "modifier_name", "modifier_word", "print_statement", "call_statement",
  "with_lock_statement", "for_each_statement", "while_statement",
  "consider_statement", "consider_branch_list", "consider_else_opt",
  "consider_statement_list", "consider_body_statement",
  "function_statement", "modifier_statement", "program_statement",
  "library_statement", "use_statement", "modifier_signature",
  "modifier_context", "watch_statement", "watch_target_list",
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
  "parameter_list", "record_field_list", "field_policy",
  "optional_newlines", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-380)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -380,    23,   515,  -380,    34,    54,  1467,  -380,  1467,    73,
      22,  1467,  1467,  -380,  -380,    85,  1467,   148,   155,   166,
     125,   138,  -380,    24,   154,   208,   210,    60,   123,   185,
    -380,  -380,   126,    86,   144,   158,   162,  -380,  -380,  -380,
    -380,  -380,  -380,  -380,  -380,   169,  -380,  -380,   174,   180,
     182,   197,   202,   203,   205,   206,  -380,  1467,  1467,   243,
    -380,  -380,   220,  -380,  -380,  -380,  -380,  1467,  1498,   285,
    -380,  1467,  1467,  -380,  -380,    -7,   279,   272,   274,  -380,
    1492,   112,  -380,     6,  -380,  -380,   293,  -380,   231,   270,
     296,   227,   228,   239,   241,  -380,  -380,  -380,   167,  -380,
     -52,   232,   234,   198,   308,  -380,  -380,  -380,  -380,  -380,
     172,  -380,   286,   247,   240,   311,  -380,   316,  -380,   154,
    -380,  1467,   315,  1467,   171,   269,  -380,  -380,  -380,  -380,
    -380,  -380,  -380,  -380,  -380,  -380,  -380,  -380,  -380,   255,
     251,   258,  -380,  1467,  -380,    30,   262,  -380,   261,   435,
      10,  1467,   179,  -380,  1364,  1467,  1467,  -380,  -380,  -380,
    -380,  -380,  -380,  -380,  -380,  -380,  -380,  1467,  1467,  -380,
    1328,  1328,  1467,  1467,  1467,  1467,   209,   327,  1467,  1467,
     303,  -380,   326,   333,   333,    39,   167,  -380,   335,  -380,
     336,   298,  -380,   276,   333,  -380,   340,   333,  -380,   341,
     343,   318,  -380,  -380,   278,   284,   287,  1467,  -380,  1467,
    -380,   283,   281,  1467,  -380,  -380,  -380,  -380,   282,     0,
    -380,   288,   289,   292,   297,  -380,  -380,  -380,  -380,  -380,
    -380,  -380,  -380,  -380,  -380,  -380,  -380,  -380,   291,   274,
    -380,   112,   112,   348,  1467,  1467,   165,  -380,  -380,   294,
     301,   302,  -380,   304,   295,  1467,   565,  1467,    82,  -380,
     305,   309,   306,   314,   232,   615,  -380,   665,  -380,  -380,
    1467,   310,  -380,   321,   322,   715,  -380,  -380,   340,  -380,
    1467,  1467,  -380,  -380,  -380,  -380,   329,  -380,    91,  1467,
     356,  1467,  -380,   117,  -380,  1467,  -380,  1467,    98,   351,
     299,   165,   165,  -380,  1467,  1467,   323,  -380,   324,   339,
     366,  1467,   334,   363,   337,   380,   344,  -380,   346,   347,
     342,  -380,  -380,   345,   353,   349,   350,   354,  -380,   468,
    -380,  -380,  1467,   357,  -380,    11,  -380,   358,   359,  1391,
     394,  -380,  1418,  -380,  -380,   360,   361,  -380,   765,  -380,
     355,   368,   401,  -380,   369,  -380,  -380,  -380,   815,   370,
     371,  -380,   865,  -380,   372,  -380,  -380,  -380,  -380,  -380,
     330,    88,  -380,  -380,  -380,   374,   381,  -380,   383,  -380,
    -380,   915,   388,   965,  -380,  -380,   386,  1015,  -380,  1065,
    1115,   396,  -380,  -380,   390,  1165,  -380,  1215,  1467,  1467,
     356,  1467,  1265,  -380,  -380,  1315,  -380,   432,   387,   437,
    1015,  -380,  -380,   389,   391,   392,  -380,  -380,  -380,  -380,
    -380,  -380,  -380,  -380,  -380,   393,  -380,  -380,   399,   402,
     404,   407,   408,   413,   414,   416,  -380,   431,   442,   419,
     420,   422,   450,  -380,  -380,   429,  -380,   491,   492,   427,
    -380,   428,  1015,  -380,  -380,  -380,  -380,  -380,  -380,  -380,
    -380,  -380,  -380,  -380,  -380,   433,   434,  -380,  -380,   439,
     440,   438,   441,   444,  -380,  -380,  -380,  -380,  -380,  -380,
    1467,  -380,  -380,  -380
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       3,     0,     2,     1,    34,     0,     0,    35,     0,     0,
       0,     0,     0,   118,   119,     0,   113,     0,     0,     0,
       0,     0,    36,     0,     0,     0,     0,     0,     0,     0,
       4,     5,     0,     0,    31,     0,     0,     9,    10,    11,
      12,    13,    14,    15,    16,     0,    18,    19,     0,     0,
       0,     0,     0,     0,     0,     0,    28,   199,   199,   175,
      34,   177,     0,   181,   182,   183,   184,     0,     0,     0,
     180,     0,     0,   217,   217,   191,     0,   139,   140,   142,
     144,   148,   151,   154,   160,   188,   176,    46,     0,     0,
       0,     0,     0,     0,     0,   114,   116,   117,     0,   106,
       0,   104,     0,     0,     0,   112,    42,    44,    43,    45,
      99,    40,     0,     0,     0,    94,    96,    93,    95,     0,
       6,     0,     0,     0,     0,     0,   115,     7,     8,    17,
      20,    21,    22,    23,    24,    25,    26,    27,   201,     0,
     200,     0,   197,   199,   155,   157,     0,   156,     0,     0,
       0,   199,     0,   178,     0,     0,     0,   165,   166,   167,
     168,   169,   170,   171,   172,   173,   174,     0,     0,    38,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     3,     0,   205,   205,     0,     0,     3,     0,     3,
       0,     0,   111,     0,   205,    41,     0,   205,     3,     0,
       0,     0,    29,    37,     0,    33,     0,     0,    47,     0,
      48,     0,     0,   199,   185,   186,   218,   203,   217,     0,
     189,   217,     0,   195,     0,     3,   128,    31,   129,   130,
     131,   132,   133,   134,   135,   136,   137,   138,     0,   141,
     143,   149,   150,     0,     0,     0,   145,   152,   153,     0,
     162,     0,   198,     0,     0,     0,     0,     0,    59,   207,
       0,   206,     0,     0,   105,     0,   107,     0,   109,   110,
     199,     0,   101,     0,     0,     0,    98,    97,     0,    32,
     199,   199,    30,   202,   179,   158,     0,   217,     0,     0,
       0,     0,   217,     0,   192,   199,   193,   199,     0,   125,
       0,   147,   146,   161,   199,   199,     0,     3,     0,    35,
       0,     0,     0,     0,     0,     0,     0,     3,    35,    35,
       0,   100,     3,     0,    35,     0,     0,     0,   159,     0,
     187,   209,   215,     0,   210,     0,   190,     0,     0,     0,
      35,   120,     0,   121,    39,     0,     0,     3,     0,     3,
       0,     0,     0,    61,     0,     3,   208,     3,     0,     0,
       0,    51,     0,     3,     0,     3,    49,    50,   204,   216,
       0,     0,   196,   194,     3,     0,     0,     3,     0,   163,
     164,     0,    35,     0,    55,    61,     0,    60,    56,     0,
       0,    35,   103,   108,    35,     0,    92,     0,     0,     0,
       0,     0,     0,   123,   122,     0,   126,    35,     0,    35,
      57,    61,    62,     0,     0,     0,    67,    68,    69,    70,
      63,    71,    72,    73,    74,     0,    76,    77,     0,     0,
       0,     0,     0,     0,     0,     0,    86,    35,    35,     0,
       0,    35,    35,   211,   212,     0,   213,    35,    35,     0,
      53,     0,    58,    64,    65,    66,    75,    78,    79,    80,
      81,    82,    83,    84,    85,     0,     0,   102,    89,     0,
       0,     0,     0,     0,    52,    54,    87,    88,    91,    90,
       0,   124,   127,   214
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -380,  -380,    59,  -380,  -151,  -380,    -1,   445,  -380,  -380,
    -380,   398,  -150,  -145,  -379,  -362,  -354,  -353,  -380,  -380,
    -340,  -380,  -348,  -347,  -341,  -336,  -143,   403,   248,  -332,
     423,   332,  -329,  -135,  -134,  -133,  -328,  -124,  -119,  -118,
    -117,  -326,  -380,  -380,  -112,    -6,  -380,   373,   377,  -165,
      94,   -55,   459,   101,  -380,   325,  -380,  -380,  -380,   137,
    -380,  -380,   -33,  -380,  -380,   130,   -61
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    31,    32,    33,    75,   125,   171,   243,
     110,   111,    35,    36,    37,    38,    39,    40,   258,   313,
     387,   420,    41,    42,    43,    44,    45,   112,   273,    46,
     100,   101,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,   341,   343,   238,   138,    77,    78,    79,    80,
      81,    82,    83,   172,    84,    85,   153,   296,    86,   139,
     140,   218,   260,   261,   221,   333,   149
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      76,    34,    87,   226,   228,    91,    92,   246,   416,   229,
      95,   230,   144,   150,   219,   371,   147,   105,    99,   231,
     232,   233,   186,     3,   187,   417,    89,    59,    60,    61,
     234,   416,    62,   418,   419,   235,   236,   237,     7,   421,
     422,    63,    64,    65,    66,   410,   423,    67,   417,    68,
      69,   424,    90,   289,   212,   426,   418,   419,   427,   431,
     151,   436,   421,   422,   115,   116,   148,   290,    22,   423,
      70,   452,   152,   416,   424,   291,   175,    88,   426,   301,
     302,   427,   431,   220,   436,   176,   216,   216,    71,    93,
     417,    72,   311,    94,    73,   312,    74,    99,   418,   419,
     175,    57,     4,   104,   421,   422,     5,     6,   263,   176,
     339,   423,   340,   186,     8,   202,   424,   204,   247,   248,
     426,    58,     9,   427,   431,    10,   436,   117,   118,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,   121,
      21,   399,    22,   217,    23,    24,    25,    26,    27,    28,
      29,   262,    96,   227,   122,   400,   123,   288,   106,    97,
     293,   271,   330,   401,   274,   124,   102,   216,   107,   249,
      60,    60,   253,   254,    30,   205,   106,   173,   174,   206,
       7,     7,   108,   223,   103,    99,   107,   224,   226,   228,
     336,   226,   228,   216,   229,   141,   230,   229,   109,   230,
     108,   282,   120,   283,   231,   232,   233,   231,   232,   233,
      22,    22,   113,   250,   114,   234,   109,   251,   234,   126,
     235,   236,   237,   235,   236,   237,   329,   375,   167,   168,
     378,   335,   119,    98,   127,   190,   413,   414,   128,   194,
     256,   191,   415,   192,   425,   129,   265,   142,   267,   308,
     130,   310,   428,   429,   430,    34,   131,   275,   132,   413,
     414,   241,   242,   432,    34,   415,    34,   425,   433,   434,
     435,   244,   245,   133,    34,   428,   429,   430,   134,   135,
     211,   136,   137,   331,   298,   334,   432,   143,   222,   146,
     154,   433,   434,   435,   155,   156,   177,    34,   178,   179,
     180,   413,   414,   181,   182,   352,   183,   415,   184,   425,
     189,   188,   193,   196,   197,   199,   198,   428,   429,   430,
     200,   203,   207,   368,   208,   209,   369,   210,   432,   213,
     214,   252,   255,   433,   434,   435,   257,   259,   227,   266,
     268,   227,   269,   270,   272,   278,   276,    34,   277,   279,
     286,   280,   284,    74,   281,   300,   287,    34,   294,   295,
     332,    34,   292,   342,   297,   303,   348,   299,   304,   305,
     350,   307,   344,   306,   314,   316,   358,   351,   354,   321,
      34,   362,    34,   315,   356,   359,    34,   360,    34,    34,
     317,   323,   443,   444,    34,   446,    34,   322,   328,   347,
     349,    34,   364,   376,    34,   398,   381,   320,   383,    34,
     353,   361,   386,   355,   389,   408,   390,   326,   327,   366,
     357,   363,   395,   367,   397,   365,   370,   372,   373,   379,
     380,   384,   337,   402,   338,   439,   405,   440,    59,    60,
      61,   345,   346,    62,   385,   388,   392,   393,   396,     7,
     403,    34,    63,    64,    65,    66,   449,   404,    67,   406,
      68,    69,   411,   450,   451,   453,   465,   454,   455,   456,
     469,    59,    60,    61,   483,   457,    62,   466,   458,    22,
     459,    70,     7,   460,   461,    63,    64,    65,    66,   462,
     463,    67,   464,    68,    69,   467,   468,   470,   471,    71,
     472,   473,    72,   474,   475,    73,   215,    74,   195,   476,
     477,   216,    22,   480,    70,   478,   479,   481,   264,     4,
     482,   185,   201,     5,     6,   170,   325,   145,   239,     7,
     445,     8,    71,   240,     0,    72,     0,   285,    73,     9,
      74,     0,    10,     0,   216,     0,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     0,     4,
       0,     0,     0,     5,     6,     0,     0,     0,     0,   309,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    30,    10,     0,     0,     0,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     0,     4,
       0,     0,     0,     5,     6,     0,     0,     0,     0,   318,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    30,    10,     0,     0,     0,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     0,     4,
       0,     0,     0,     5,     6,     0,     0,     0,     0,   319,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    30,    10,     0,     0,     0,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     0,     4,
       0,     0,     0,     5,     6,     0,     0,     0,     0,   324,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    30,    10,     0,     0,     0,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     0,     4,
       0,     0,     0,     5,     6,     0,     0,     0,     0,   382,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    30,    10,     0,     0,     0,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     0,     4,
       0,     0,     0,     5,     6,     0,     0,     0,     0,   391,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    30,    10,     0,     0,     0,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     0,     4,
       0,     0,     0,     5,     6,     0,     0,     0,     0,   394,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    30,    10,     0,     0,     0,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     0,     4,
       0,     0,     0,     5,     6,     0,     0,     0,     0,   407,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    30,    10,     0,     0,     0,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     0,     4,
       0,     0,     0,     5,     6,     0,     0,     0,     0,   409,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    30,    10,     0,     0,     0,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     0,     4,
       0,     0,     0,     5,     6,     0,     0,     0,     0,     7,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    30,    10,     0,     0,     0,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     0,     4,
       0,     0,     0,     5,     6,     0,     0,     0,     0,   437,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,   412,    10,     0,     0,     0,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     0,     4,
       0,     0,     0,     5,     6,     0,     0,     0,     0,   438,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    30,    10,     0,     0,     0,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     0,     4,
       0,     0,     0,     5,     6,     0,     0,     0,     0,   441,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    30,    10,     0,     0,     0,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     0,     4,
       0,     0,     0,     5,     6,     0,     0,     0,     0,   442,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    30,    10,     0,     0,     0,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     0,     4,
       0,     0,     0,     5,     6,     0,     0,     0,     0,   447,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    30,    10,     0,     0,     0,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     0,     4,
       0,     0,     0,     5,     6,     0,     0,     0,     0,   448,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    30,    10,     0,     0,     0,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     4,     0,
       0,     0,     5,     0,     0,     0,     0,     0,     7,     0,
       8,   157,   158,   159,   160,   161,   162,   163,   164,   165,
     166,    30,     0,     0,     0,     4,     0,    13,    14,     5,
      16,    17,    18,     0,     0,     7,    21,     8,    22,     0,
      23,     0,     0,     0,    27,    28,     0,     0,     0,     0,
       0,     0,     4,     0,    13,    14,     5,    16,    17,    18,
       0,     0,     7,    21,     8,    22,     0,    23,     0,     0,
     225,    27,    28,     0,     0,     0,     0,     0,     0,     0,
       0,    13,    14,     0,    16,    17,    18,     0,     0,     0,
      21,     0,    22,     0,    23,     0,     0,   374,    27,    28,
      59,    60,    61,     0,     0,    62,     0,     0,     0,     0,
       0,     7,     0,     0,    63,    64,    65,    66,     0,     0,
      67,     0,    68,    69,   377,     0,     0,     0,     0,     0,
       0,    59,    60,    61,     0,     0,    62,     0,     0,     0,
       0,    22,     7,    70,     0,    63,    64,    65,    66,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    71,     0,     0,    72,     0,     0,    73,     0,    74,
       0,     0,    22,     0,    70,   157,   158,   159,   160,   161,
     162,   163,   164,   165,   166,   167,   168,     0,     0,     0,
     122,     0,     0,     0,   169,    72,     0,     0,    73,     0,
      74
};

static const yytype_int16 yycheck[] =
{
       6,     2,     8,   154,   154,    11,    12,   172,   387,   154,
      16,   154,    67,    74,     4,     4,    71,    23,    19,   154,
     154,   154,    74,     0,    76,   387,     4,     3,     4,     5,
     154,   410,     8,   387,   387,   154,   154,   154,    14,   387,
     387,    17,    18,    19,    20,   385,   387,    23,   410,    25,
      26,   387,    30,    53,    24,   387,   410,   410,   387,   387,
      67,   387,   410,   410,     4,     5,    72,    67,    44,   410,
      46,   411,    79,   452,   410,    75,    70,     4,   410,   244,
     245,   410,   410,    73,   410,    79,    76,    76,    64,     4,
     452,    67,    10,     8,    70,    13,    72,    98,   452,   452,
      70,    67,     4,    79,   452,   452,     8,     9,    69,    79,
      12,   452,    14,    74,    16,   121,   452,   123,   173,   174,
     452,    67,    24,   452,   452,    27,   452,     4,     5,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    53,
      42,    53,    44,   149,    46,    47,    48,    49,    50,    51,
      52,   184,     4,   154,    68,    67,    70,   218,     4,     4,
     221,   194,    71,    75,   197,    79,    41,    76,    14,   175,
       4,     4,   178,   179,    76,     4,     4,    65,    66,     8,
      14,    14,    28,     4,    46,   186,    14,     8,   339,   339,
      73,   342,   342,    76,   339,    58,   339,   342,    44,   342,
      28,   207,    76,   209,   339,   339,   339,   342,   342,   342,
      44,    44,     4,     4,     4,   339,    44,     8,   342,    75,
     339,   339,   339,   342,   342,   342,   287,   339,    63,    64,
     342,   292,    47,    67,    76,    37,   387,   387,    76,    67,
     181,    43,   387,    45,   387,    76,   187,     4,   189,   255,
      76,   257,   387,   387,   387,   256,    76,   198,    76,   410,
     410,   167,   168,   387,   265,   410,   267,   410,   387,   387,
     387,   170,   171,    76,   275,   410,   410,   410,    76,    76,
     143,    76,    76,   289,   225,   291,   410,    67,   151,     4,
      11,   410,   410,   410,    22,    21,     3,   298,    67,    29,
       4,   452,   452,    76,    76,   311,    67,   452,    67,   452,
      76,    79,     4,    27,    67,     4,    76,   452,   452,   452,
       4,     6,    53,   329,    69,    74,   332,    69,   452,    67,
      69,     4,    29,   452,   452,   452,    10,     4,   339,     4,
       4,   342,    44,    67,     4,    27,     5,   348,     5,    71,
     213,    67,    69,    72,    67,     7,    74,   358,    69,    67,
       4,   362,    74,    12,    67,    71,   307,    76,    67,    67,
      31,    76,    73,    69,    69,    69,   317,    11,    15,    69,
     381,   322,   383,    74,     4,    39,   387,    40,   389,   390,
      76,    69,   398,   399,   395,   401,   397,    76,    69,    76,
      76,   402,    49,     9,   405,    75,   347,   270,   349,   410,
      76,    69,    11,    76,   355,    27,   357,   280,   281,    69,
      76,    76,   363,    69,   365,    76,    69,    69,    69,    69,
      69,    76,   295,   374,   297,    39,   377,    47,     3,     4,
       5,   304,   305,     8,    76,    76,    76,    76,    76,    14,
      76,   452,    17,    18,    19,    20,    24,    76,    23,    76,
      25,    26,    76,    76,    27,    76,    35,    76,    76,    76,
      48,     3,     4,     5,   480,    76,     8,    35,    76,    44,
      76,    46,    14,    76,    76,    17,    18,    19,    20,    76,
      76,    23,    76,    25,    26,    76,    76,    47,    69,    64,
       9,     9,    67,    76,    76,    70,    71,    72,   110,    76,
      76,    76,    44,    75,    46,    76,    76,    76,   186,     4,
      76,    98,   119,     8,     9,    80,   278,    68,   155,    14,
     400,    16,    64,   156,    -1,    67,    -1,   212,    70,    24,
      72,    -1,    27,    -1,    76,    -1,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    -1,    42,    -1,    44,
      -1,    46,    47,    48,    49,    50,    51,    52,    -1,     4,
      -1,    -1,    -1,     8,     9,    -1,    -1,    -1,    -1,    14,
      -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,
      -1,    76,    27,    -1,    -1,    -1,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    -1,    42,    -1,    44,
      -1,    46,    47,    48,    49,    50,    51,    52,    -1,     4,
      -1,    -1,    -1,     8,     9,    -1,    -1,    -1,    -1,    14,
      -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,
      -1,    76,    27,    -1,    -1,    -1,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    -1,    42,    -1,    44,
      -1,    46,    47,    48,    49,    50,    51,    52,    -1,     4,
      -1,    -1,    -1,     8,     9,    -1,    -1,    -1,    -1,    14,
      -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,
      -1,    76,    27,    -1,    -1,    -1,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    -1,    42,    -1,    44,
      -1,    46,    47,    48,    49,    50,    51,    52,    -1,     4,
      -1,    -1,    -1,     8,     9,    -1,    -1,    -1,    -1,    14,
      -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,
      -1,    76,    27,    -1,    -1,    -1,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    -1,    42,    -1,    44,
      -1,    46,    47,    48,    49,    50,    51,    52,    -1,     4,
      -1,    -1,    -1,     8,     9,    -1,    -1,    -1,    -1,    14,
      -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,
      -1,    76,    27,    -1,    -1,    -1,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    -1,    42,    -1,    44,
      -1,    46,    47,    48,    49,    50,    51,    52,    -1,     4,
      -1,    -1,    -1,     8,     9,    -1,    -1,    -1,    -1,    14,
      -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,
      -1,    76,    27,    -1,    -1,    -1,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    -1,    42,    -1,    44,
      -1,    46,    47,    48,    49,    50,    51,    52,    -1,     4,
      -1,    -1,    -1,     8,     9,    -1,    -1,    -1,    -1,    14,
      -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,
      -1,    76,    27,    -1,    -1,    -1,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    -1,    42,    -1,    44,
      -1,    46,    47,    48,    49,    50,    51,    52,    -1,     4,
      -1,    -1,    -1,     8,     9,    -1,    -1,    -1,    -1,    14,
      -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,
      -1,    76,    27,    -1,    -1,    -1,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    -1,    42,    -1,    44,
      -1,    46,    47,    48,    49,    50,    51,    52,    -1,     4,
      -1,    -1,    -1,     8,     9,    -1,    -1,    -1,    -1,    14,
      -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,
      -1,    76,    27,    -1,    -1,    -1,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    -1,    42,    -1,    44,
      -1,    46,    47,    48,    49,    50,    51,    52,    -1,     4,
      -1,    -1,    -1,     8,     9,    -1,    -1,    -1,    -1,    14,
      -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,
      -1,    76,    27,    -1,    -1,    -1,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    -1,    42,    -1,    44,
      -1,    46,    47,    48,    49,    50,    51,    52,    -1,     4,
      -1,    -1,    -1,     8,     9,    -1,    -1,    -1,    -1,    14,
      -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,
      -1,    76,    27,    -1,    -1,    -1,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    -1,    42,    -1,    44,
      -1,    46,    47,    48,    49,    50,    51,    52,    -1,     4,
      -1,    -1,    -1,     8,     9,    -1,    -1,    -1,    -1,    14,
      -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,
      -1,    76,    27,    -1,    -1,    -1,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    -1,    42,    -1,    44,
      -1,    46,    47,    48,    49,    50,    51,    52,    -1,     4,
      -1,    -1,    -1,     8,     9,    -1,    -1,    -1,    -1,    14,
      -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,
      -1,    76,    27,    -1,    -1,    -1,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    -1,    42,    -1,    44,
      -1,    46,    47,    48,    49,    50,    51,    52,    -1,     4,
      -1,    -1,    -1,     8,     9,    -1,    -1,    -1,    -1,    14,
      -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,
      -1,    76,    27,    -1,    -1,    -1,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    -1,    42,    -1,    44,
      -1,    46,    47,    48,    49,    50,    51,    52,    -1,     4,
      -1,    -1,    -1,     8,     9,    -1,    -1,    -1,    -1,    14,
      -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,
      -1,    76,    27,    -1,    -1,    -1,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    -1,    42,    -1,    44,
      -1,    46,    47,    48,    49,    50,    51,    52,    -1,     4,
      -1,    -1,    -1,     8,     9,    -1,    -1,    -1,    -1,    14,
      -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,
      -1,    76,    27,    -1,    -1,    -1,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    -1,    42,    -1,    44,
      -1,    46,    47,    48,    49,    50,    51,    52,     4,    -1,
      -1,    -1,     8,    -1,    -1,    -1,    -1,    -1,    14,    -1,
      16,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    76,    -1,    -1,    -1,     4,    -1,    33,    34,     8,
      36,    37,    38,    -1,    -1,    14,    42,    16,    44,    -1,
      46,    -1,    -1,    -1,    50,    51,    -1,    -1,    -1,    -1,
      -1,    -1,     4,    -1,    33,    34,     8,    36,    37,    38,
      -1,    -1,    14,    42,    16,    44,    -1,    46,    -1,    -1,
      76,    50,    51,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    33,    34,    -1,    36,    37,    38,    -1,    -1,    -1,
      42,    -1,    44,    -1,    46,    -1,    -1,    76,    50,    51,
       3,     4,     5,    -1,    -1,     8,    -1,    -1,    -1,    -1,
      -1,    14,    -1,    -1,    17,    18,    19,    20,    -1,    -1,
      23,    -1,    25,    26,    76,    -1,    -1,    -1,    -1,    -1,
      -1,     3,     4,     5,    -1,    -1,     8,    -1,    -1,    -1,
      -1,    44,    14,    46,    -1,    17,    18,    19,    20,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    64,    -1,    -1,    67,    -1,    -1,    70,    -1,    72,
      -1,    -1,    44,    -1,    46,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    -1,    -1,    -1,
      68,    -1,    -1,    -1,    72,    67,    -1,    -1,    70,    -1,
      72
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    81,    82,     0,     4,     8,     9,    14,    16,    24,
      27,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    42,    44,    46,    47,    48,    49,    50,    51,    52,
      76,    83,    84,    85,    86,    92,    93,    94,    95,    96,
      97,   102,   103,   104,   105,   106,   109,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,    67,    67,     3,
       4,     5,     8,    17,    18,    19,    20,    23,    25,    26,
      46,    64,    67,    70,    72,    86,   125,   126,   127,   128,
     129,   130,   131,   132,   134,   135,   138,   125,     4,     4,
      30,   125,   125,     4,     8,   125,     4,     4,    67,    86,
     110,   111,    41,    46,    79,   125,     4,    14,    28,    44,
      90,    91,   107,     4,     4,     4,     5,     4,     5,    47,
      76,    53,    68,    70,    79,    87,    75,    76,    76,    76,
      76,    76,    76,    76,    76,    76,    76,    76,   125,   139,
     140,   139,     4,    67,   131,   132,     4,   131,   125,   146,
     146,    67,    79,   136,    11,    22,    21,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    72,
      87,    88,   133,    65,    66,    70,    79,     3,    67,    29,
       4,    76,    76,    67,    67,   110,    74,    76,    79,    76,
      37,    43,    45,     4,    67,    91,    27,    67,    76,     4,
       4,   107,   125,     6,   125,     4,     8,    53,    69,    74,
      69,   139,    24,    67,    69,    71,    76,   125,   141,     4,
      73,   144,   139,     4,     8,    76,    84,    86,    92,    93,
     106,   113,   114,   115,   117,   118,   119,   120,   124,   127,
     128,   130,   130,    89,   133,   133,   129,   131,   131,   125,
       4,     8,     4,   125,   125,    29,    82,    10,    98,     4,
     142,   143,   142,    69,   111,    82,     4,    82,     4,    44,
      67,   142,     4,   108,   142,    82,     5,     5,    27,    71,
      67,    67,   125,   125,    69,   135,   139,    74,   146,    53,
      67,    75,    74,   146,    69,    67,   137,    67,    82,    76,
       7,   129,   129,    71,    67,    67,    69,    76,   125,    14,
     125,    10,    13,    99,    69,    74,    69,    76,    14,    14,
     139,    69,    76,    69,    14,   108,   139,   139,    69,   146,
      71,   125,     4,   145,   125,   146,    73,   139,   139,    12,
      14,   122,    12,   123,    73,   139,   139,    76,    82,    76,
      31,    11,   125,    76,    15,    76,     4,    76,    82,    39,
      40,    69,    82,    76,    49,    76,    69,    69,   125,   125,
      69,     4,    69,    69,    76,   124,     9,    76,   124,    69,
      69,    82,    14,    82,    76,    76,    11,   100,    76,    82,
      82,    14,    76,    76,    14,    82,    76,    82,    75,    53,
      67,    75,    82,    76,    76,    82,    76,    14,    27,    14,
     100,    76,    76,    84,    92,    93,    94,    95,    96,    97,
     101,   102,   103,   104,   105,   106,   109,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,    14,    14,    39,
      47,    14,    14,   125,   125,   145,   125,    14,    14,    24,
      76,    27,   100,    76,    76,    76,    76,    76,    76,    76,
      76,    76,    76,    76,    76,    35,    35,    76,    76,    48,
      47,    69,     9,     9,    76,    76,    76,    76,    76,    76,
      75,    76,    76,   125
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    80,    81,    82,    82,    82,    83,    83,    83,    83,
      83,    83,    83,    83,    83,    83,    83,    83,    83,    83,
      83,    83,    83,    83,    83,    83,    83,    83,    83,    84,
      84,    85,    85,    85,    86,    86,    86,    87,    89,    88,
      90,    90,    91,    91,    91,    91,    92,    93,    93,    93,
      93,    93,    94,    95,    95,    96,    97,    98,    98,    99,
      99,   100,   100,   100,   101,   101,   101,   101,   101,   101,
     101,   101,   101,   101,   101,   101,   101,   101,   101,   101,
     101,   101,   101,   101,   101,   101,   101,   102,   102,   103,
     103,   104,   105,   106,   106,   106,   106,   106,   106,   107,
     107,   108,   109,   109,   110,   110,   111,   111,   112,   113,
     113,   113,   114,   115,   115,   116,   117,   118,   119,   120,
     121,   121,   122,   122,   122,   123,   123,   123,   124,   124,
     124,   124,   124,   124,   124,   124,   124,   124,   124,   125,
     126,   126,   127,   127,   128,   128,   128,   128,   129,   129,
     129,   130,   130,   130,   131,   131,   131,   131,   131,   131,
     132,   132,   132,   132,   132,   133,   133,   133,   133,   133,
     133,   133,   133,   133,   133,   134,   134,   134,   134,   134,
     134,   134,   134,   134,   134,   134,   134,   134,   134,   135,
     135,   136,   136,   136,   136,   137,   137,   138,   138,   139,
     139,   140,   140,   141,   141,   142,   142,   143,   143,   144,
     144,   144,   144,   144,   144,   145,   145,   146,   146
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     1,     1,     1,     1,     2,     1,     1,
       2,     2,     2,     2,     2,     2,     2,     2,     1,     3,
       4,     1,     4,     3,     1,     1,     1,     2,     0,     4,
       1,     2,     1,     1,     1,     1,     2,     4,     4,     6,
       6,     6,    10,     9,    10,     7,     7,     5,     6,     0,
       3,     0,     2,     2,     2,     2,     2,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     1,     1,     2,     2,
       2,     2,     2,     2,     2,     2,     1,    10,    10,     9,
      10,    10,     7,     2,     2,     2,     2,     4,     4,     1,
       4,     1,     9,     7,     1,     3,     1,     3,     7,     4,
       4,     3,     2,     1,     2,     2,     2,     2,     1,     1,
       6,     6,     3,     3,     6,     0,     3,     6,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     3,     1,     3,     1,     3,     4,     4,     1,     3,
       3,     1,     3,     3,     1,     2,     2,     2,     4,     5,
       1,     4,     3,     6,     6,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     2,     4,
       1,     1,     1,     1,     1,     3,     3,     5,     1,     3,
       5,     0,     3,     3,     5,     0,     3,     2,     3,     0,
       1,     1,     3,     1,     4,     0,     1,     1,     3,     3,
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
  YY_USE (yykind);
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
#line 544 "src/parser.y"
                     { ctx->parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 2627 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 548 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 2633 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 549 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 2639 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 550 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 2645 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 554 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 2651 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 555 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 2657 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 556 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 2663 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 557 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 2669 "src/parser.tab.c"
    break;

  case 10: /* statement: for_each_statement  */
#line 558 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 2675 "src/parser.tab.c"
    break;

  case 11: /* statement: while_statement  */
#line 559 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 2681 "src/parser.tab.c"
    break;

  case 12: /* statement: consider_statement  */
#line 560 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 2687 "src/parser.tab.c"
    break;

  case 13: /* statement: function_statement  */
#line 561 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 2693 "src/parser.tab.c"
    break;

  case 14: /* statement: modifier_statement  */
#line 562 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 2699 "src/parser.tab.c"
    break;

  case 15: /* statement: program_statement  */
#line 563 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 2705 "src/parser.tab.c"
    break;

  case 16: /* statement: library_statement  */
#line 564 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 2711 "src/parser.tab.c"
    break;

  case 17: /* statement: use_statement NEWLINE  */
#line 565 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 2717 "src/parser.tab.c"
    break;

  case 18: /* statement: watch_statement  */
#line 566 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 2723 "src/parser.tab.c"
    break;

  case 19: /* statement: without_watchers_statement  */
#line 567 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 2729 "src/parser.tab.c"
    break;

  case 20: /* statement: on_error_statement NEWLINE  */
#line 568 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 2735 "src/parser.tab.c"
    break;

  case 21: /* statement: error_statement NEWLINE  */
#line 569 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 2741 "src/parser.tab.c"
    break;

  case 22: /* statement: return_statement NEWLINE  */
#line 570 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 2747 "src/parser.tab.c"
    break;

  case 23: /* statement: label_statement NEWLINE  */
#line 571 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 2753 "src/parser.tab.c"
    break;

  case 24: /* statement: goto_statement NEWLINE  */
#line 572 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 2759 "src/parser.tab.c"
    break;

  case 25: /* statement: gosub_statement NEWLINE  */
#line 573 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 2765 "src/parser.tab.c"
    break;

  case 26: /* statement: break_statement NEWLINE  */
#line 574 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 2771 "src/parser.tab.c"
    break;

  case 27: /* statement: continue_statement NEWLINE  */
#line 575 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 2777 "src/parser.tab.c"
    break;

  case 28: /* statement: if_statement  */
#line 576 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 2783 "src/parser.tab.c"
    break;

  case 29: /* assignment: lvalue OP_EQ expression  */
#line 580 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 2789 "src/parser.tab.c"
    break;

  case 30: /* assignment: lvalue modifier OP_EQ expression  */
#line 581 "src/parser.y"
                                       {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 2803 "src/parser.tab.c"
    break;

  case 31: /* lvalue: variable_name  */
#line 593 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2809 "src/parser.tab.c"
    break;

  case 32: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 594 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 2815 "src/parser.tab.c"
    break;

  case 33: /* lvalue: lvalue DOT IDENT  */
#line 595 "src/parser.y"
                                    { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2821 "src/parser.tab.c"
    break;

  case 34: /* variable_name: IDENT  */
#line 599 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 2827 "src/parser.tab.c"
    break;

  case 35: /* variable_name: END  */
#line 600 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 2833 "src/parser.tab.c"
    break;

  case 36: /* variable_name: NEXT  */
#line 601 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 2839 "src/parser.tab.c"
    break;

  case 37: /* modifier: MOD_LPAREN MOD_CONTENT  */
#line 605 "src/parser.y"
                             { (yyval.modifier) = parse_modifier_use((yyvsp[0].text)); }
#line 2845 "src/parser.tab.c"
    break;

  case 38: /* $@1: %empty  */
#line 609 "src/parser.y"
             { lexer_begin_lens_content(ctx->active_lexer); }
#line 2851 "src/parser.tab.c"
    break;

  case 39: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 609 "src/parser.y"
                                                                                  {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 2859 "src/parser.tab.c"
    break;

  case 40: /* modifier_name: modifier_word  */
#line 615 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 2865 "src/parser.tab.c"
    break;

  case 41: /* modifier_name: modifier_name modifier_word  */
#line 616 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 2871 "src/parser.tab.c"
    break;

  case 42: /* modifier_word: IDENT  */
#line 620 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 2877 "src/parser.tab.c"
    break;

  case 43: /* modifier_word: TO  */
#line 621 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 2883 "src/parser.tab.c"
    break;

  case 44: /* modifier_word: END  */
#line 622 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 2889 "src/parser.tab.c"
    break;

  case 45: /* modifier_word: NEXT  */
#line 623 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 2895 "src/parser.tab.c"
    break;

  case 46: /* print_statement: PRINT expression  */
#line 627 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 2901 "src/parser.tab.c"
    break;

  case 47: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 631 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 2907 "src/parser.tab.c"
    break;

  case 48: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 632 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 2918 "src/parser.tab.c"
    break;

  case 49: /* call_statement: lvalue DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 638 "src/parser.y"
                                                       {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 2928 "src/parser.tab.c"
    break;

  case 50: /* call_statement: lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 643 "src/parser.y"
                                                                 {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 2942 "src/parser.tab.c"
    break;

  case 51: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 652 "src/parser.y"
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
#line 2957 "src/parser.tab.c"
    break;

  case 52: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 665 "src/parser.y"
                                                                                  {
        if (strcmp((yyvsp[-8].text), "lock") != 0) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "expected lock in with lock block");
            free((yyvsp[-8].text));
            YYERROR;
        }
        free((yyvsp[-8].text));
        (yyval.stmt) = ast_with_lock((yyvsp[-6].expr), (yyvsp[-3].stmt_list));
      }
#line 2973 "src/parser.tab.c"
    break;

  case 53: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list END FOR NEWLINE  */
#line 679 "src/parser.y"
                                                                     {
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 2981 "src/parser.tab.c"
    break;

  case 54: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list END FOR NEWLINE  */
#line 682 "src/parser.y"
                                                                          {
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 2989 "src/parser.tab.c"
    break;

  case 55: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 688 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 2997 "src/parser.tab.c"
    break;

  case 56: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 694 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 3005 "src/parser.tab.c"
    break;

  case 57: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 700 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 3013 "src/parser.tab.c"
    break;

  case 58: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 703 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 3021 "src/parser.tab.c"
    break;

  case 59: /* consider_else_opt: %empty  */
#line 709 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3027 "src/parser.tab.c"
    break;

  case 60: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 710 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3033 "src/parser.tab.c"
    break;

  case 61: /* consider_statement_list: %empty  */
#line 714 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3039 "src/parser.tab.c"
    break;

  case 62: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 715 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3045 "src/parser.tab.c"
    break;

  case 63: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 716 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3051 "src/parser.tab.c"
    break;

  case 64: /* consider_body_statement: assignment NEWLINE  */
#line 720 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3057 "src/parser.tab.c"
    break;

  case 65: /* consider_body_statement: print_statement NEWLINE  */
#line 721 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3063 "src/parser.tab.c"
    break;

  case 66: /* consider_body_statement: call_statement NEWLINE  */
#line 722 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3069 "src/parser.tab.c"
    break;

  case 67: /* consider_body_statement: with_lock_statement  */
#line 723 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3075 "src/parser.tab.c"
    break;

  case 68: /* consider_body_statement: for_each_statement  */
#line 724 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3081 "src/parser.tab.c"
    break;

  case 69: /* consider_body_statement: while_statement  */
#line 725 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3087 "src/parser.tab.c"
    break;

  case 70: /* consider_body_statement: consider_statement  */
#line 726 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3093 "src/parser.tab.c"
    break;

  case 71: /* consider_body_statement: function_statement  */
#line 727 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3099 "src/parser.tab.c"
    break;

  case 72: /* consider_body_statement: modifier_statement  */
#line 728 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3105 "src/parser.tab.c"
    break;

  case 73: /* consider_body_statement: program_statement  */
#line 729 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3111 "src/parser.tab.c"
    break;

  case 74: /* consider_body_statement: library_statement  */
#line 730 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3117 "src/parser.tab.c"
    break;

  case 75: /* consider_body_statement: use_statement NEWLINE  */
#line 731 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3123 "src/parser.tab.c"
    break;

  case 76: /* consider_body_statement: watch_statement  */
#line 732 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3129 "src/parser.tab.c"
    break;

  case 77: /* consider_body_statement: without_watchers_statement  */
#line 733 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3135 "src/parser.tab.c"
    break;

  case 78: /* consider_body_statement: on_error_statement NEWLINE  */
#line 734 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3141 "src/parser.tab.c"
    break;

  case 79: /* consider_body_statement: error_statement NEWLINE  */
#line 735 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3147 "src/parser.tab.c"
    break;

  case 80: /* consider_body_statement: return_statement NEWLINE  */
#line 736 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3153 "src/parser.tab.c"
    break;

  case 81: /* consider_body_statement: label_statement NEWLINE  */
#line 737 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3159 "src/parser.tab.c"
    break;

  case 82: /* consider_body_statement: goto_statement NEWLINE  */
#line 738 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3165 "src/parser.tab.c"
    break;

  case 83: /* consider_body_statement: gosub_statement NEWLINE  */
#line 739 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3171 "src/parser.tab.c"
    break;

  case 84: /* consider_body_statement: break_statement NEWLINE  */
#line 740 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3177 "src/parser.tab.c"
    break;

  case 85: /* consider_body_statement: continue_statement NEWLINE  */
#line 741 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3183 "src/parser.tab.c"
    break;

  case 86: /* consider_body_statement: if_statement  */
#line 742 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3189 "src/parser.tab.c"
    break;

  case 87: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 746 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 3197 "src/parser.tab.c"
    break;

  case 88: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 749 "src/parser.y"
                                                                                                            {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 3206 "src/parser.tab.c"
    break;

  case 89: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 756 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 3214 "src/parser.tab.c"
    break;

  case 90: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 759 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 3222 "src/parser.tab.c"
    break;

  case 91: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 765 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 3230 "src/parser.tab.c"
    break;

  case 92: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 771 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 3238 "src/parser.tab.c"
    break;

  case 93: /* use_statement: USE IDENT  */
#line 777 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 3244 "src/parser.tab.c"
    break;

  case 94: /* use_statement: LOAD IDENT  */
#line 778 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 3250 "src/parser.tab.c"
    break;

  case 95: /* use_statement: USE STRING  */
#line 779 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 3256 "src/parser.tab.c"
    break;

  case 96: /* use_statement: LOAD STRING  */
#line 780 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 3262 "src/parser.tab.c"
    break;

  case 97: /* use_statement: USE IDENT IDENT STRING  */
#line 781 "src/parser.y"
                             {
        if (strcmp((yyvsp[-1].text), "from") != 0) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "expected from in use statement");
            free((yyvsp[-2].text));
            free((yyvsp[-1].text));
            free((yyvsp[0].text));
            YYERROR;
        }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_use((yyvsp[-2].text), (yyvsp[0].text));
      }
#line 3280 "src/parser.tab.c"
    break;

  case 98: /* use_statement: LOAD IDENT IDENT STRING  */
#line 794 "src/parser.y"
                              {
        if (strcmp((yyvsp[-1].text), "from") != 0) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "expected from in load statement");
            free((yyvsp[-2].text));
            free((yyvsp[-1].text));
            free((yyvsp[0].text));
            YYERROR;
        }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_use((yyvsp[-2].text), (yyvsp[0].text));
      }
#line 3298 "src/parser.tab.c"
    break;

  case 99: /* modifier_signature: modifier_name  */
#line 810 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 3304 "src/parser.tab.c"
    break;

  case 100: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 811 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 3310 "src/parser.tab.c"
    break;

  case 101: /* modifier_context: IDENT  */
#line 815 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 3316 "src/parser.tab.c"
    break;

  case 102: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 819 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch((yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 3324 "src/parser.tab.c"
    break;

  case 103: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 822 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch((yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 3332 "src/parser.tab.c"
    break;

  case 104: /* watch_target_list: watch_target_path  */
#line 828 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 3338 "src/parser.tab.c"
    break;

  case 105: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 829 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 3344 "src/parser.tab.c"
    break;

  case 106: /* watch_target_path: variable_name  */
#line 833 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 3350 "src/parser.tab.c"
    break;

  case 107: /* watch_target_path: watch_target_path DOT IDENT  */
#line 834 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 3356 "src/parser.tab.c"
    break;

  case 108: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 838 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 3364 "src/parser.tab.c"
    break;

  case 109: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 844 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 3370 "src/parser.tab.c"
    break;

  case 110: /* on_error_statement: ON ERROR_VALUE RESUME NEXT  */
#line 845 "src/parser.y"
                                 { (yyval.stmt) = ast_on_error_resume_next(); }
#line 3376 "src/parser.tab.c"
    break;

  case 111: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 846 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 3382 "src/parser.tab.c"
    break;

  case 112: /* error_statement: ERROR_VALUE expression  */
#line 850 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 3388 "src/parser.tab.c"
    break;

  case 113: /* return_statement: RETURN  */
#line 854 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 3394 "src/parser.tab.c"
    break;

  case 114: /* return_statement: RETURN expression  */
#line 855 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 3400 "src/parser.tab.c"
    break;

  case 115: /* label_statement: variable_name COLON  */
#line 859 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 3406 "src/parser.tab.c"
    break;

  case 116: /* goto_statement: GOTO IDENT  */
#line 863 "src/parser.y"
                 { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 3412 "src/parser.tab.c"
    break;

  case 117: /* gosub_statement: GOSUB IDENT  */
#line 867 "src/parser.y"
                  { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 3418 "src/parser.tab.c"
    break;

  case 118: /* break_statement: BREAK  */
#line 871 "src/parser.y"
            { (yyval.stmt) = ast_break(); }
#line 3424 "src/parser.tab.c"
    break;

  case 119: /* continue_statement: CONTINUE  */
#line 875 "src/parser.y"
               { (yyval.stmt) = ast_continue(); }
#line 3430 "src/parser.tab.c"
    break;

  case 120: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 879 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 3439 "src/parser.tab.c"
    break;

  case 121: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 883 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 3448 "src/parser.tab.c"
    break;

  case 122: /* if_block_tail: END IF NEWLINE  */
#line 890 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 3456 "src/parser.tab.c"
    break;

  case 123: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 893 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 3464 "src/parser.tab.c"
    break;

  case 124: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 896 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 3472 "src/parser.tab.c"
    break;

  case 125: /* if_inline_tail: %empty  */
#line 902 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 3480 "src/parser.tab.c"
    break;

  case 126: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 905 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 3488 "src/parser.tab.c"
    break;

  case 127: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 908 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 3496 "src/parser.tab.c"
    break;

  case 128: /* inline_statement: assignment  */
#line 914 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3502 "src/parser.tab.c"
    break;

  case 129: /* inline_statement: print_statement  */
#line 915 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3508 "src/parser.tab.c"
    break;

  case 130: /* inline_statement: call_statement  */
#line 916 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3514 "src/parser.tab.c"
    break;

  case 131: /* inline_statement: use_statement  */
#line 917 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3520 "src/parser.tab.c"
    break;

  case 132: /* inline_statement: on_error_statement  */
#line 918 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3526 "src/parser.tab.c"
    break;

  case 133: /* inline_statement: error_statement  */
#line 919 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3532 "src/parser.tab.c"
    break;

  case 134: /* inline_statement: return_statement  */
#line 920 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3538 "src/parser.tab.c"
    break;

  case 135: /* inline_statement: goto_statement  */
#line 921 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3544 "src/parser.tab.c"
    break;

  case 136: /* inline_statement: gosub_statement  */
#line 922 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3550 "src/parser.tab.c"
    break;

  case 137: /* inline_statement: break_statement  */
#line 923 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3556 "src/parser.tab.c"
    break;

  case 138: /* inline_statement: continue_statement  */
#line 924 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3562 "src/parser.tab.c"
    break;

  case 139: /* expression: or_expression  */
#line 928 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 3568 "src/parser.tab.c"
    break;

  case 140: /* or_expression: and_expression  */
#line 932 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 3574 "src/parser.tab.c"
    break;

  case 141: /* or_expression: or_expression OR and_expression  */
#line 933 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3580 "src/parser.tab.c"
    break;

  case 142: /* and_expression: comparison_expression  */
#line 937 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 3586 "src/parser.tab.c"
    break;

  case 143: /* and_expression: and_expression AND comparison_expression  */
#line 938 "src/parser.y"
                                               { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3592 "src/parser.tab.c"
    break;

  case 144: /* comparison_expression: additive_expression  */
#line 942 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 3598 "src/parser.tab.c"
    break;

  case 145: /* comparison_expression: additive_expression comparison_operator additive_expression  */
#line 943 "src/parser.y"
                                                                  { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3604 "src/parser.tab.c"
    break;

  case 146: /* comparison_expression: additive_expression comparison_lens comparison_operator additive_expression  */
#line 944 "src/parser.y"
                                                                                  {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 3612 "src/parser.tab.c"
    break;

  case 147: /* comparison_expression: additive_expression modifier comparison_operator additive_expression  */
#line 947 "src/parser.y"
                                                                           {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 3626 "src/parser.tab.c"
    break;

  case 148: /* additive_expression: multiplicative_expression  */
#line 959 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 3632 "src/parser.tab.c"
    break;

  case 149: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 960 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3638 "src/parser.tab.c"
    break;

  case 150: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 961 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3644 "src/parser.tab.c"
    break;

  case 151: /* multiplicative_expression: unary_expression  */
#line 965 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 3650 "src/parser.tab.c"
    break;

  case 152: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 966 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3656 "src/parser.tab.c"
    break;

  case 153: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 967 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3662 "src/parser.tab.c"
    break;

  case 154: /* unary_expression: postfix_expression  */
#line 971 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 3668 "src/parser.tab.c"
    break;

  case 155: /* unary_expression: NOT unary_expression  */
#line 972 "src/parser.y"
                           { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3674 "src/parser.tab.c"
    break;

  case 156: /* unary_expression: MINUS unary_expression  */
#line 973 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3680 "src/parser.tab.c"
    break;

  case 157: /* unary_expression: NEW postfix_expression  */
#line 974 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3686 "src/parser.tab.c"
    break;

  case 158: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 975 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 3692 "src/parser.tab.c"
    break;

  case 159: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 976 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 3698 "src/parser.tab.c"
    break;

  case 160: /* postfix_expression: primary  */
#line 980 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 3704 "src/parser.tab.c"
    break;

  case 161: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 981 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3710 "src/parser.tab.c"
    break;

  case 162: /* postfix_expression: postfix_expression DOT IDENT  */
#line 982 "src/parser.y"
                                   { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3716 "src/parser.tab.c"
    break;

  case 163: /* postfix_expression: postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 983 "src/parser.y"
                                                                   {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        (yyval.expr) = expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 3726 "src/parser.tab.c"
    break;

  case 164: /* postfix_expression: postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 988 "src/parser.y"
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
#line 3742 "src/parser.tab.c"
    break;

  case 165: /* comparison_operator: OP_EQ  */
#line 1002 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 3748 "src/parser.tab.c"
    break;

  case 166: /* comparison_operator: OP_NE  */
#line 1003 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 3754 "src/parser.tab.c"
    break;

  case 167: /* comparison_operator: OP_GT  */
#line 1004 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 3760 "src/parser.tab.c"
    break;

  case 168: /* comparison_operator: OP_LT  */
#line 1005 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 3766 "src/parser.tab.c"
    break;

  case 169: /* comparison_operator: OP_GE  */
#line 1006 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 3772 "src/parser.tab.c"
    break;

  case 170: /* comparison_operator: OP_LE  */
#line 1007 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 3778 "src/parser.tab.c"
    break;

  case 171: /* comparison_operator: OP_NGT  */
#line 1008 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 3784 "src/parser.tab.c"
    break;

  case 172: /* comparison_operator: OP_NLT  */
#line 1009 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 3790 "src/parser.tab.c"
    break;

  case 173: /* comparison_operator: OP_NGE  */
#line 1010 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 3796 "src/parser.tab.c"
    break;

  case 174: /* comparison_operator: OP_NLE  */
#line 1011 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 3802 "src/parser.tab.c"
    break;

  case 175: /* primary: NUMBER  */
#line 1015 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3808 "src/parser.tab.c"
    break;

  case 176: /* primary: duration_terms  */
#line 1016 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3814 "src/parser.tab.c"
    break;

  case 177: /* primary: STRING  */
#line 1017 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3820 "src/parser.tab.c"
    break;

  case 178: /* primary: variable_name ident_suffix  */
#line 1018 "src/parser.y"
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
#line 3842 "src/parser.tab.c"
    break;

  case 179: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1035 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 3853 "src/parser.tab.c"
    break;

  case 180: /* primary: ERROR_VALUE  */
#line 1041 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3859 "src/parser.tab.c"
    break;

  case 181: /* primary: TRUE  */
#line 1042 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3865 "src/parser.tab.c"
    break;

  case 182: /* primary: FALSE  */
#line 1043 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3871 "src/parser.tab.c"
    break;

  case 183: /* primary: NOTHING  */
#line 1044 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3877 "src/parser.tab.c"
    break;

  case 184: /* primary: UNKNOWN_VALUE  */
#line 1045 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3883 "src/parser.tab.c"
    break;

  case 185: /* primary: LPAREN expression RPAREN  */
#line 1046 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 3889 "src/parser.tab.c"
    break;

  case 186: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 1047 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3895 "src/parser.tab.c"
    break;

  case 187: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 1048 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 3901 "src/parser.tab.c"
    break;

  case 188: /* primary: record_literal  */
#line 1049 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 3907 "src/parser.tab.c"
    break;

  case 189: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 1053 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3913 "src/parser.tab.c"
    break;

  case 190: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 1054 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 3919 "src/parser.tab.c"
    break;

  case 191: /* ident_suffix: %empty  */
#line 1058 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 3929 "src/parser.tab.c"
    break;

  case 192: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 1063 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 3939 "src/parser.tab.c"
    break;

  case 193: /* ident_suffix: DOT IDENT ident_dot_suffix  */
#line 1068 "src/parser.y"
                                 {
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 3948 "src/parser.tab.c"
    break;

  case 194: /* ident_suffix: DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1072 "src/parser.y"
                                                          {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        (yyval.ident_suffix).kind = IDENT_SUFFIX_METHOD;
        (yyval.ident_suffix).name = (yyvsp[-3].text);
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 3962 "src/parser.tab.c"
    break;

  case 195: /* ident_dot_suffix: %empty  */
#line 1084 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 3972 "src/parser.tab.c"
    break;

  case 196: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 1089 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 3982 "src/parser.tab.c"
    break;

  case 197: /* duration_terms: NUMBER IDENT  */
#line 1097 "src/parser.y"
                   {
        AstDuration duration = {0};
        (yyval.duration) = duration_add_unit(duration, (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 3991 "src/parser.tab.c"
    break;

  case 198: /* duration_terms: duration_terms NUMBER IDENT  */
#line 1101 "src/parser.y"
                                  {
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 3999 "src/parser.tab.c"
    break;

  case 199: /* argument_list_opt: %empty  */
#line 1107 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 4005 "src/parser.tab.c"
    break;

  case 200: /* argument_list_opt: argument_list  */
#line 1108 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 4011 "src/parser.tab.c"
    break;

  case 201: /* argument_list: expression  */
#line 1112 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 4017 "src/parser.tab.c"
    break;

  case 202: /* argument_list: argument_list COMMA expression  */
#line 1113 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 4023 "src/parser.tab.c"
    break;

  case 203: /* array_argument_list: expression  */
#line 1117 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 4029 "src/parser.tab.c"
    break;

  case 204: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 1118 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 4035 "src/parser.tab.c"
    break;

  case 205: /* parameter_list_opt: %empty  */
#line 1122 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 4041 "src/parser.tab.c"
    break;

  case 206: /* parameter_list_opt: parameter_list  */
#line 1123 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 4047 "src/parser.tab.c"
    break;

  case 207: /* parameter_list: IDENT  */
#line 1127 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4053 "src/parser.tab.c"
    break;

  case 208: /* parameter_list: parameter_list COMMA IDENT  */
#line 1128 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4059 "src/parser.tab.c"
    break;

  case 209: /* record_field_list: IDENT OP_EQ expression  */
#line 1132 "src/parser.y"
                             { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 4065 "src/parser.tab.c"
    break;

  case 210: /* record_field_list: IDENT COLON expression  */
#line 1133 "src/parser.y"
                             { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 4071 "src/parser.tab.c"
    break;

  case 211: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1134 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 4077 "src/parser.tab.c"
    break;

  case 212: /* record_field_list: record_field_list COMMA optional_newlines IDENT OP_EQ expression  */
#line 1135 "src/parser.y"
                                                                       { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 4083 "src/parser.tab.c"
    break;

  case 213: /* record_field_list: record_field_list COMMA optional_newlines IDENT COLON expression  */
#line 1136 "src/parser.y"
                                                                       { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 4089 "src/parser.tab.c"
    break;

  case 214: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1137 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 4095 "src/parser.tab.c"
    break;

  case 215: /* field_policy: IDENT  */
#line 1145 "src/parser.y"
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
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "reset policy requires a value, e.g. (reset 0)");
            YYERROR;
        } else {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "unknown field policy (expected copy, link, reset, or exclude)");
            free((yyvsp[0].text));
            YYERROR;
        }
        free((yyvsp[0].text));
        (yyval.field_policy) = spec;
      }
#line 4125 "src/parser.tab.c"
    break;

  case 216: /* field_policy: IDENT expression  */
#line 1170 "src/parser.y"
                       {
        FieldPolicySpec spec;
        if (strcmp((yyvsp[-1].text), "reset") == 0) {
            spec.policy = AST_FIELD_POLICY_RESET;
            spec.reset_expr = (yyvsp[0].expr);
        } else {
            free((yyvsp[-1].text));
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "only the reset policy takes a value");
            YYERROR;
        }
        free((yyvsp[-1].text));
        (yyval.field_policy) = spec;
      }
#line 4145 "src/parser.tab.c"
    break;


#line 4149 "src/parser.tab.c"

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

#line 1192 "src/parser.y"


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
    case TOKEN_MOD_CONTENT:
        lvalp->text = copy_text(token.start, token.length);
        return MOD_CONTENT;
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
    case TOKEN_WITHOUT: return WITHOUT;
    case TOKEN_WATCHERS: return WATCHERS;
    case TOKEN_ON: return ON;
    case TOKEN_RESUME: return RESUME;
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
        if (modifier_lparen_ahead(ctx, token.start)) {
            lexer_begin_modifier_content(ctx->active_lexer);
            return MOD_LPAREN;
        }
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
    default:
        fprintf(stderr, "unexpected token %s at %d:%d\n",
                token_type_name(token.type), token.line, token.column);
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
