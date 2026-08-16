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
    /* --- A clause body begins with a NAME (PLAT-CLAUSE-B, option B narrow) ---
     *
     * A clause always opens with the modifier's name, and a modifier name is a
     * sequence of `modifier_word`, which the grammar defines as
     * `IDENT | TO | END | NEXT` — always identifier-shaped. So a body whose
     * first character starts a NUMBER or a STRING cannot be a clause, whatever
     * follows it.
     *
     * That rejects `kind(1) = "record"` and `kind("q") = "record"`, calls to a
     * `load`ed library's function which options A and F cannot reach: the
     * preceding token is an ordinary identifier and there is no dot, and the
     * function check cannot see across a file boundary. Those used to PARSE and
     * then fail at run time with `compare modifier not found: 1`.
     *
     * It does NOT reject `kind(x) = "record"`. That is not an oversight and no
     * refinement here can fix it: `name(caseless) = "joe"` and
     * `kind(x) = "record"` are the same tokens in the same order —
     * IDENT `(` IDENT `)` `=` STRING — and the first must be a clause while the
     * second must be a call. Separating them needs to know whether `caseless`
     * is a registered modifier or `kind` is callable, and neither fact exists
     * until eval (docs/gbasic_clause_recognition.md §1, §8).
     *
     * Identifier-start is A-Z, a-z and `_`, matching the lexer's own test at
     * src/lexer.c:428 (`isalpha(ch) || ch == '_'`). Nothing calls setlocale, so
     * that runs in the C locale and is ASCII-only; a non-ASCII byte does not
     * start an identifier there either. */
    if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '_')) {
        return 0;
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
                /* Skip the string. A backslash escapes the next character, so
                 * that `"\""` is one string rather than two — without this, the
                 * scan stops at the ESCAPED quote, reads the real closing quote
                 * as the start of a second string, finds no terminator before
                 * the newline and gives up, and the whole clause silently
                 * degrades to an ordinary parenthesised expression. This is the
                 * first of three string scanners a modifier clause passes
                 * through; the others are modifier_content_token (src/lexer.c)
                 * and modifier_string_literal (src/eval.c). */
                p++;
                while (*p && *p != '"' && *p != '\n') {
                    if (*p == '\\' && p[1] && p[1] != '\n') {
                        p += 2;
                        continue;
                    }
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
    /* --- What may precede a clause (PLAT-CLAUSE, option A) -------------------
     *
     * Only two grammar positions ever consume a clause, and both put a TARGET
     * to its left: `lvalue modifier OP_EQ expression` and
     * `additive_expression modifier comparison_operator additive_expression`.
     * So the `(` must follow something that can END an expression. A keyword,
     * an operator, `=`, `,`, `(`, `[`, `.`, or the start of a statement cannot,
     * and a `(` after any of those is an ordinary parenthesis.
     *
     * Without this the lookahead consulted nothing to its left except a
     * function name, so `if (a - b) > 0` — and the same shape after `while`,
     * `return`, `print`, `=`, an operator, a comma, an opening paren, or at the
     * start of a statement — was read as a clause and failed to parse.
     *
     * The permitted set is deliberately WIDER than the set of legal targets.
     * `is_modifier_target_expr` accepts only an identifier, a field or an index,
     * so a string, a number or a call result can never be a legal target — but
     * they can end an expression, so they are allowed through here and rejected
     * by that check instead, which reports "modifier target must be a variable,
     * field, or index". Rejecting them here would replace a precise diagnostic
     * with a generic syntax error. Deciding "could this be a clause" is this
     * function's job; deciding "is this a legal target" is the grammar's. */
    if (name_start < name_end) {
        /* An identifier- or number-shaped run precedes the `(`. */
        if (*name_start >= '0' && *name_start <= '9') {
            /* A numeric literal. Cannot be a legal target, but ends an
             * expression; the grammar action produces the error. */
        } else {
            char *name = copy_text(name_start, (int)(name_end - name_start));
            int is_function = gbasic_builtin_function(name) || source_declares_function(ctx, name);
            free(name);
            if (is_function) {
                return 0;
            }
            /* Option A: a reserved word cannot be a modifier target. `end` and
             * `next` are exempt because `variable_name` admits them as ordinary
             * variable names. */
            TokenType word = lexer_identifier_type(name_start, (int)(name_end - name_start));
            if (word != TOKEN_IDENT && word != TOKEN_END && word != TOKEN_NEXT) {
                return 0;
            }
            /* --- A qualified name is a call (PLAT-CLAUSE, option F) ----------
             *
             * `helper.kind(1) = "record"` and `holder.m(1) = "record"` are
             * calls, but the function check above cannot know it: it re-scans
             * only the file being parsed, so a `load`ed library's functions are
             * invisible, and the name run stops at the dot, so it tests `kind`
             * or `m` rather than the qualified name.
             *
             * The shape settles it without needing to know any names — but only
             * the EXACT shape the lexer turns into a QUALIFIED_IDENT, which is
             * `IDENT . IDENT (`: one plain identifier, one dot, one identifier,
             * then the paren (see identifier_token, src/lexer.c). For that shape
             * the grammar has a call production and no clause production, so
             * reading it as a clause could only ever be wrong.
             *
             * Anything else keeps its clause. In particular a FIELD target
             * reaches here legitimately when the chain is broken by an index —
             * `player.inventory[slot].name(trimmed) = v` is a working clause in
             * examples/nested_lvalue_test.bas, and `]` before the dot means the
             * lexer does not build a QUALIFIED_IDENT. Rejecting every dotted
             * name, rather than this one shape, breaks it. */
            if (name_start > ctx->active_lexer->source && name_start[-1] == '.') {
                const char *dot = name_start - 1;
                const char *seg = dot;
                while (seg > ctx->active_lexer->source &&
                       ((seg[-1] >= 'A' && seg[-1] <= 'Z') ||
                        (seg[-1] >= 'a' && seg[-1] <= 'z') ||
                        (seg[-1] >= '0' && seg[-1] <= '9') ||
                        seg[-1] == '_')) {
                    seg--;
                }
                int rooted_at_plain_ident =
                    seg < dot &&
                    !(seg[0] >= '0' && seg[0] <= '9') &&
                    (seg == ctx->active_lexer->source ||
                     (seg[-1] != '.' && seg[-1] != ']')) &&
                    lexer_identifier_type(seg, (int)(dot - seg)) == TOKEN_IDENT;
                if (rooted_at_plain_ident) {
                    return 0;
                }
            }
        }
    } else {
        /* No identifier or number run. Only these can still end an expression. */
        if (name_end == ctx->active_lexer->source) {
            return 0;
        }
        char prev = name_end[-1];
        if (prev != ')' && prev != ']' && prev != '"') {
            return 0;
        }
    }

    return 1;
}


#line 669 "src/parser.tab.c"

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
  YYSYMBOL_AS = 9,                         /* AS  */
  YYSYMBOL_IF = 10,                        /* IF  */
  YYSYMBOL_CONSIDER_IF = 11,               /* CONSIDER_IF  */
  YYSYMBOL_THEN = 12,                      /* THEN  */
  YYSYMBOL_ELSE = 13,                      /* ELSE  */
  YYSYMBOL_CONSIDER_ELSE = 14,             /* CONSIDER_ELSE  */
  YYSYMBOL_END = 15,                       /* END  */
  YYSYMBOL_END_CONSIDER = 16,              /* END_CONSIDER  */
  YYSYMBOL_PRINT = 17,                     /* PRINT  */
  YYSYMBOL_TRUE = 18,                      /* TRUE  */
  YYSYMBOL_FALSE = 19,                     /* FALSE  */
  YYSYMBOL_NOTHING = 20,                   /* NOTHING  */
  YYSYMBOL_UNKNOWN_VALUE = 21,             /* UNKNOWN_VALUE  */
  YYSYMBOL_AND = 22,                       /* AND  */
  YYSYMBOL_OR = 23,                        /* OR  */
  YYSYMBOL_NOT = 24,                       /* NOT  */
  YYSYMBOL_WITH = 25,                      /* WITH  */
  YYSYMBOL_NEW = 26,                       /* NEW  */
  YYSYMBOL_SPAWN = 27,                     /* SPAWN  */
  YYSYMBOL_FOR = 28,                       /* FOR  */
  YYSYMBOL_TO = 29,                        /* TO  */
  YYSYMBOL_STEP = 30,                      /* STEP  */
  YYSYMBOL_IN = 31,                        /* IN  */
  YYSYMBOL_EACH = 32,                      /* EACH  */
  YYSYMBOL_WHILE = 33,                     /* WHILE  */
  YYSYMBOL_CONSIDER = 34,                  /* CONSIDER  */
  YYSYMBOL_BREAK = 35,                     /* BREAK  */
  YYSYMBOL_CONTINUE = 36,                  /* CONTINUE  */
  YYSYMBOL_FUNCTION = 37,                  /* FUNCTION  */
  YYSYMBOL_RETURN = 38,                    /* RETURN  */
  YYSYMBOL_GOTO = 39,                      /* GOTO  */
  YYSYMBOL_GOSUB = 40,                     /* GOSUB  */
  YYSYMBOL_WATCH = 41,                     /* WATCH  */
  YYSYMBOL_WITHOUT = 42,                   /* WITHOUT  */
  YYSYMBOL_WATCHERS = 43,                  /* WATCHERS  */
  YYSYMBOL_ON = 44,                        /* ON  */
  YYSYMBOL_RESUME = 45,                    /* RESUME  */
  YYSYMBOL_NEXT = 46,                      /* NEXT  */
  YYSYMBOL_STOP = 47,                      /* STOP  */
  YYSYMBOL_ERROR_VALUE = 48,               /* ERROR_VALUE  */
  YYSYMBOL_MODIFIER = 49,                  /* MODIFIER  */
  YYSYMBOL_PROGRAM = 50,                   /* PROGRAM  */
  YYSYMBOL_LIBRARY = 51,                   /* LIBRARY  */
  YYSYMBOL_LOAD = 52,                      /* LOAD  */
  YYSYMBOL_USE = 53,                       /* USE  */
  YYSYMBOL_EXPORT = 54,                    /* EXPORT  */
  YYSYMBOL_OP_EQ = 55,                     /* OP_EQ  */
  YYSYMBOL_OP_NE = 56,                     /* OP_NE  */
  YYSYMBOL_OP_GT = 57,                     /* OP_GT  */
  YYSYMBOL_OP_LT = 58,                     /* OP_LT  */
  YYSYMBOL_OP_GE = 59,                     /* OP_GE  */
  YYSYMBOL_OP_LE = 60,                     /* OP_LE  */
  YYSYMBOL_OP_NGT = 61,                    /* OP_NGT  */
  YYSYMBOL_OP_NLT = 62,                    /* OP_NLT  */
  YYSYMBOL_OP_NGE = 63,                    /* OP_NGE  */
  YYSYMBOL_OP_NLE = 64,                    /* OP_NLE  */
  YYSYMBOL_PLUS = 65,                      /* PLUS  */
  YYSYMBOL_MINUS = 66,                     /* MINUS  */
  YYSYMBOL_STAR = 67,                      /* STAR  */
  YYSYMBOL_SLASH = 68,                     /* SLASH  */
  YYSYMBOL_LPAREN = 69,                    /* LPAREN  */
  YYSYMBOL_MOD_LPAREN = 70,                /* MOD_LPAREN  */
  YYSYMBOL_RPAREN = 71,                    /* RPAREN  */
  YYSYMBOL_LBRACKET = 72,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 73,                  /* RBRACKET  */
  YYSYMBOL_LBRACE = 74,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 75,                    /* RBRACE  */
  YYSYMBOL_COMMA = 76,                     /* COMMA  */
  YYSYMBOL_COLON = 77,                     /* COLON  */
  YYSYMBOL_NEWLINE = 78,                   /* NEWLINE  */
  YYSYMBOL_IF_WITHOUT_ELSE = 79,           /* IF_WITHOUT_ELSE  */
  YYSYMBOL_NO_DOT = 80,                    /* NO_DOT  */
  YYSYMBOL_DOT = 81,                       /* DOT  */
  YYSYMBOL_YYACCEPT = 82,                  /* $accept  */
  YYSYMBOL_program = 83,                   /* program  */
  YYSYMBOL_statement_list = 84,            /* statement_list  */
  YYSYMBOL_statement = 85,                 /* statement  */
  YYSYMBOL_assignment = 86,                /* assignment  */
  YYSYMBOL_lvalue = 87,                    /* lvalue  */
  YYSYMBOL_variable_name = 88,             /* variable_name  */
  YYSYMBOL_modifier = 89,                  /* modifier  */
  YYSYMBOL_comparison_lens = 90,           /* comparison_lens  */
  YYSYMBOL_91_1 = 91,                      /* $@1  */
  YYSYMBOL_modifier_name = 92,             /* modifier_name  */
  YYSYMBOL_modifier_word = 93,             /* modifier_word  */
  YYSYMBOL_print_statement = 94,           /* print_statement  */
  YYSYMBOL_call_statement = 95,            /* call_statement  */
  YYSYMBOL_with_lock_statement = 96,       /* with_lock_statement  */
  YYSYMBOL_for_each_statement = 97,        /* for_each_statement  */
  YYSYMBOL_while_statement = 98,           /* while_statement  */
  YYSYMBOL_consider_statement = 99,        /* consider_statement  */
  YYSYMBOL_consider_branch_list = 100,     /* consider_branch_list  */
  YYSYMBOL_consider_else_opt = 101,        /* consider_else_opt  */
  YYSYMBOL_consider_statement_list = 102,  /* consider_statement_list  */
  YYSYMBOL_consider_body_statement = 103,  /* consider_body_statement  */
  YYSYMBOL_function_statement = 104,       /* function_statement  */
  YYSYMBOL_modifier_statement = 105,       /* modifier_statement  */
  YYSYMBOL_program_statement = 106,        /* program_statement  */
  YYSYMBOL_library_statement = 107,        /* library_statement  */
  YYSYMBOL_use_statement = 108,            /* use_statement  */
  YYSYMBOL_modifier_signature = 109,       /* modifier_signature  */
  YYSYMBOL_modifier_context = 110,         /* modifier_context  */
  YYSYMBOL_watch_statement = 111,          /* watch_statement  */
  YYSYMBOL_watch_target_list = 112,        /* watch_target_list  */
  YYSYMBOL_watch_target_path = 113,        /* watch_target_path  */
  YYSYMBOL_without_watchers_statement = 114, /* without_watchers_statement  */
  YYSYMBOL_on_error_statement = 115,       /* on_error_statement  */
  YYSYMBOL_error_statement = 116,          /* error_statement  */
  YYSYMBOL_return_statement = 117,         /* return_statement  */
  YYSYMBOL_label_statement = 118,          /* label_statement  */
  YYSYMBOL_goto_statement = 119,           /* goto_statement  */
  YYSYMBOL_gosub_statement = 120,          /* gosub_statement  */
  YYSYMBOL_break_statement = 121,          /* break_statement  */
  YYSYMBOL_continue_statement = 122,       /* continue_statement  */
  YYSYMBOL_if_statement = 123,             /* if_statement  */
  YYSYMBOL_if_block_tail = 124,            /* if_block_tail  */
  YYSYMBOL_if_inline_tail = 125,           /* if_inline_tail  */
  YYSYMBOL_inline_statement = 126,         /* inline_statement  */
  YYSYMBOL_expression = 127,               /* expression  */
  YYSYMBOL_or_expression = 128,            /* or_expression  */
  YYSYMBOL_and_expression = 129,           /* and_expression  */
  YYSYMBOL_comparison_expression = 130,    /* comparison_expression  */
  YYSYMBOL_additive_expression = 131,      /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 132, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 133,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 134,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 135,      /* comparison_operator  */
  YYSYMBOL_primary = 136,                  /* primary  */
  YYSYMBOL_record_literal = 137,           /* record_literal  */
  YYSYMBOL_ident_suffix = 138,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 139,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 140,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 141,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 142,            /* argument_list  */
  YYSYMBOL_array_argument_list = 143,      /* array_argument_list  */
  YYSYMBOL_parameter_list_opt = 144,       /* parameter_list_opt  */
  YYSYMBOL_parameter_list = 145,           /* parameter_list  */
  YYSYMBOL_field_name = 146,               /* field_name  */
  YYSYMBOL_record_field_list = 147,        /* record_field_list  */
  YYSYMBOL_field_policy = 148,             /* field_policy  */
  YYSYMBOL_optional_newlines = 149         /* optional_newlines  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;



/* Unqualified %code blocks.  */
#line 663 "src/parser.y"

static int yylex(YYSTYPE *lvalp, YYLTYPE *llocp, gb_parse_ctx *ctx);
static void yyerror(YYLTYPE *llocp, gb_parse_ctx *ctx, const char *message);
static void report_syntax_error(gb_parse_ctx *ctx, int line, int column,
                                int end_line, int end_column, const char *message);

#line 860 "src/parser.tab.c"

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
#define YYLAST   1873

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  82
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  68
/* YYNRULES -- Number of rules.  */
#define YYNRULES  263
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  546

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   336


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
      75,    76,    77,    78,    79,    80,    81
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   721,   721,   725,   726,   727,   731,   732,   733,   734,
     735,   736,   737,   738,   739,   740,   741,   742,   743,   744,
     745,   746,   747,   748,   749,   750,   751,   752,   753,   757,
     758,   770,   771,   772,   776,   777,   778,   782,   786,   786,
     792,   793,   797,   798,   799,   800,   804,   810,   814,   815,
     821,   826,   835,   848,   863,   866,   872,   875,   881,   887,
     893,   896,   902,   903,   907,   908,   909,   913,   914,   915,
     916,   917,   918,   919,   920,   921,   922,   923,   924,   925,
     926,   927,   928,   929,   930,   931,   932,   933,   934,   935,
     939,   942,   949,   952,   958,   964,   970,   971,   972,   973,
     974,   990,  1009,  1010,  1014,  1018,  1021,  1027,  1028,  1032,
    1033,  1037,  1043,  1044,  1045,  1049,  1053,  1054,  1058,  1062,
    1066,  1070,  1074,  1078,  1082,  1089,  1092,  1095,  1101,  1104,
    1107,  1113,  1114,  1115,  1116,  1117,  1118,  1119,  1120,  1121,
    1122,  1123,  1127,  1131,  1132,  1136,  1137,  1141,  1142,  1143,
    1146,  1158,  1159,  1160,  1164,  1165,  1166,  1170,  1171,  1172,
    1173,  1174,  1175,  1179,  1180,  1181,  1182,  1187,  1201,  1202,
    1203,  1204,  1205,  1206,  1207,  1208,  1209,  1210,  1214,  1215,
    1216,  1217,  1234,  1240,  1241,  1242,  1243,  1244,  1245,  1246,
    1247,  1248,  1252,  1253,  1257,  1262,  1267,  1271,  1283,  1288,
    1296,  1300,  1306,  1307,  1311,  1312,  1316,  1317,  1321,  1322,
    1326,  1327,  1340,  1341,  1342,  1343,  1344,  1345,  1346,  1347,
    1348,  1349,  1350,  1351,  1352,  1353,  1354,  1355,  1356,  1357,
    1358,  1359,  1360,  1361,  1362,  1363,  1364,  1365,  1366,  1367,
    1368,  1369,  1370,  1371,  1372,  1373,  1374,  1375,  1376,  1377,
    1378,  1379,  1380,  1381,  1385,  1386,  1387,  1388,  1389,  1390,
    1398,  1425,  1444,  1445
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
  "STRING", "MOD_CONTENT", "LENS_CONTENT", "QUALIFIED_IDENT", "AS", "IF",
  "CONSIDER_IF", "THEN", "ELSE", "CONSIDER_ELSE", "END", "END_CONSIDER",
  "PRINT", "TRUE", "FALSE", "NOTHING", "UNKNOWN_VALUE", "AND", "OR", "NOT",
  "WITH", "NEW", "SPAWN", "FOR", "TO", "STEP", "IN", "EACH", "WHILE",
  "CONSIDER", "BREAK", "CONTINUE", "FUNCTION", "RETURN", "GOTO", "GOSUB",
  "WATCH", "WITHOUT", "WATCHERS", "ON", "RESUME", "NEXT", "STOP",
  "ERROR_VALUE", "MODIFIER", "PROGRAM", "LIBRARY", "LOAD", "USE", "EXPORT",
  "OP_EQ", "OP_NE", "OP_GT", "OP_LT", "OP_GE", "OP_LE", "OP_NGT", "OP_NLT",
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
  "parameter_list", "field_name", "record_field_list", "field_policy",
  "optional_newlines", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-428)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -428,    19,   752,  -428,   -46,    -1,  1799,  -428,  1767,    89,
      20,  1799,  1799,  -428,  -428,   104,  1799,    96,   126,   152,
      48,    91,  -428,    23,   254,   160,   193,    49,   167,   168,
    -428,  -428,   141,   113,   146,   147,   162,  -428,  -428,  -428,
    -428,  -428,  -428,  -428,  -428,   171,  -428,  -428,   173,   176,
     181,   185,   188,   189,   192,   194,  -428,  1799,  1799,   270,
    -428,  -428,   206,  -428,  -428,  -428,  -428,  1799,   414,   272,
    -428,  1799,  1799,  -428,  -428,    15,   266,   258,   260,  -428,
     492,   125,  -428,   -17,  -428,  -428,   281,   238,  -428,   220,
      17,   286,   214,   216,   232,   233,  -428,  -428,  -428,    36,
    -428,   110,   222,   226,    88,   302,  -428,  -428,  -428,  -428,
    -428,   186,  -428,   279,   239,   237,   307,  -428,   315,  -428,
     254,  -428,  1799,   317,  1799,   124,   274,  -428,  -428,  -428,
    -428,  -428,  -428,  -428,  -428,  -428,  -428,  -428,  -428,  -428,
     250,   249,   256,  -428,  1799,  -428,    -5,   264,  -428,   269,
     515,   586,  1799,   157,  -428,    71,  1799,  1799,  -428,  -428,
    -428,  -428,  -428,  -428,  -428,  -428,  -428,  -428,  1799,  1799,
    -428,   512,   512,  1799,  1799,  1799,  1799,   158,   331,  1799,
    1799,  1799,  1799,   306,  -428,   330,   338,   338,    69,    36,
    -428,   339,  -428,   340,   299,  -428,   277,   338,  -428,   343,
     338,  -428,   345,   346,   320,  -428,  -428,   282,   283,   289,
    1799,  -428,  1799,  -428,   290,   291,  1799,  -428,  -428,  -428,
    -428,   287,   298,  -428,  -428,  -428,  -428,  -428,  -428,  -428,
    -428,  -428,  -428,  -428,  -428,  -428,  -428,  -428,  -428,  -428,
    -428,  -428,  -428,  -428,  -428,  -428,  -428,  -428,  -428,  -428,
    -428,  -428,  -428,  -428,  -428,  -428,  -428,  -428,  -428,  -428,
    -428,  -428,  -428,  -428,  -428,     1,   293,   301,   308,   310,
    -428,  -428,  -428,  -428,  -428,  -428,  -428,  -428,  -428,  -428,
    -428,  -428,  -428,   292,   260,  -428,   125,   125,   368,  1799,
    1799,   130,  -428,  -428,   303,   311,   316,  -428,  -428,   313,
     309,   357,  1799,   803,  1799,   136,  -428,   318,   312,   319,
     321,   222,   854,  -428,   905,  -428,  -428,  1799,   322,  -428,
     324,   323,   956,  -428,  -428,   343,  -428,  1799,  1799,  -428,
    -428,  -428,  -428,   325,  -428,    85,   391,  1799,  1799,  -428,
     102,  -428,  1799,  -428,  1799,   701,   384,   328,   130,   130,
    -428,  1799,  1799,   327,  -428,  1799,   329,   365,   388,  1799,
     333,   393,   334,   409,   336,  -428,   379,   381,   353,  -428,
    -428,   348,   377,   352,   360,   366,  -428,   133,  -428,  1799,
     367,  -428,  -428,   650,  -428,   369,   371,   174,   426,  -428,
    1721,  -428,  -428,   372,   380,  -428,  1007,    16,  -428,   361,
     370,   438,  -428,   376,  -428,  -428,  -428,  1058,   386,   387,
    -428,  1109,  -428,   389,  -428,  -428,  -428,  -428,  -428,   382,
     397,     3,  -428,  -428,  -428,   392,   394,  -428,   395,  -428,
    -428,  1160,   430,  1799,  -428,  1211,  -428,  -428,   396,  1262,
    -428,  1313,  1364,   428,  -428,  -428,   431,  1415,  -428,  1466,
    1799,   391,  1799,  1799,  1517,  -428,  -428,  1568,  -428,   450,
     399,   401,  1619,   453,  1262,  -428,  -428,   406,   411,   412,
    -428,  -428,  -428,  -428,  -428,  -428,  -428,  -428,  -428,   416,
    -428,  -428,   417,   418,   419,   420,   422,   424,   425,   427,
    -428,   455,   467,   429,   432,   437,   457,  -428,   441,  -428,
    -428,   498,   499,   435,  -428,  -428,   487,   439,  1262,  -428,
    -428,  -428,  -428,  -428,  -428,  -428,  -428,  -428,  -428,  -428,
    -428,   446,   447,  -428,  -428,   448,   449,   451,   459,   460,
    -428,  1670,   462,  -428,  -428,  -428,  -428,  -428,  1799,  -428,
    -428,   488,  -428,  -428,   465,  -428
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       3,     0,     2,     1,    34,     0,     0,    35,     0,     0,
       0,     0,     0,   121,   122,     0,   116,     0,     0,     0,
       0,     0,    36,     0,     0,     0,     0,     0,     0,     0,
       4,     5,     0,     0,    31,     0,     0,     9,    10,    11,
      12,    13,    14,    15,    16,     0,    18,    19,     0,     0,
       0,     0,     0,     0,     0,     0,    28,   202,   202,   178,
      34,   180,     0,   184,   185,   186,   187,     0,     0,     0,
     183,     0,     0,   262,   262,   194,     0,   142,   143,   145,
     147,   151,   154,   157,   163,   191,   179,     0,    46,     0,
       0,     0,     0,     0,     0,     0,   117,   119,   120,     0,
     109,     0,   107,     0,     0,     0,   115,    42,    44,    43,
      45,   102,    40,     0,     0,     0,    97,    99,    96,    98,
       0,     6,     0,     0,     0,     0,     0,   118,     7,     8,
      17,    20,    21,    22,    23,    24,    25,    26,    27,   204,
       0,   203,     0,   200,   202,   158,   160,     0,   159,     0,
       0,     0,   202,     0,   181,     0,     0,     0,   168,   169,
     170,   171,   172,   173,   174,   175,   176,   177,     0,     0,
      38,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     3,     0,   208,   208,     0,     0,
       3,     0,     3,     0,     0,   114,     0,   208,    41,     0,
     208,     3,     0,     0,     0,    29,    37,     0,    33,     0,
       0,    48,     0,    49,     0,     0,   202,   188,   189,   263,
     206,   262,   212,   213,   228,   225,   226,   217,   230,   237,
     238,   239,   235,   236,   234,   223,   221,   244,   227,   218,
     219,   222,   229,   253,   240,   241,   247,   231,   242,   243,
     251,   224,   252,   220,   250,   214,   215,   216,   248,   249,
     246,   232,   233,   245,   192,     0,   262,     0,   198,     0,
       3,   131,    31,   132,   133,   134,   135,   136,   137,   138,
     139,   140,   141,     0,   144,   146,   152,   153,     0,     0,
       0,   148,   155,   156,     0,   165,     0,   201,    47,     0,
       0,     0,     0,     0,     0,    62,   210,     0,   209,     0,
       0,   108,     0,   110,     0,   112,   113,   202,     0,   104,
       0,     0,     0,   101,   100,     0,    32,   202,   202,    30,
     205,   182,   161,     0,   262,     0,     0,     0,     0,   262,
       0,   195,   202,   196,   202,     0,   128,     0,   150,   149,
     164,   202,   202,     0,     3,     0,     0,    35,     0,     0,
       0,     0,     0,     0,     0,     3,    35,    35,     0,   103,
       3,     0,    35,     0,     0,     0,   162,     0,   190,   260,
       0,   254,   255,     0,   193,     0,     0,     0,    35,   123,
       0,   124,    39,     0,     0,     3,     0,     0,     3,     0,
       0,     0,    64,     0,     3,   211,     3,     0,     0,     0,
      52,     0,     3,     0,     3,    50,    51,   207,   261,     0,
     212,     0,   199,   197,     3,     0,     0,     3,     0,   166,
     167,     0,    35,     0,     3,     0,    58,    64,     0,    63,
      59,     0,     0,    35,   106,   111,    35,     0,    95,     0,
       0,     0,     0,     0,     0,   126,   125,     0,   129,    35,
       0,     0,     0,    35,    60,    64,    65,     0,     0,     0,
      70,    71,    72,    73,    66,    74,    75,    76,    77,     0,
      79,    80,     0,     0,     0,     0,     0,     0,     0,     0,
      89,    35,    35,     0,     0,    35,    35,   256,     0,   257,
     258,    35,    35,     0,    54,     3,    35,     0,    61,    67,
      68,    69,    78,    81,    82,    83,    84,    85,    86,    87,
      88,     0,     0,   105,    92,     0,     0,     0,     0,     0,
      53,     0,     0,    55,    90,    91,    94,    93,     0,   127,
     130,    35,    56,   259,     0,    57
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -428,  -428,    87,  -428,  -154,  -428,    14,   464,  -428,  -428,
    -428,   454,  -152,  -148,  -427,  -425,  -407,  -405,  -428,  -428,
    -402,  -428,  -403,  -394,  -379,  -377,  -146,   440,   204,  -374,
     478,   342,  -366,  -144,  -142,  -140,  -365,  -134,  -130,  -126,
    -125,  -362,  -428,  -428,  -203,    -6,  -428,   390,   402,  -169,
      60,   -49,   496,    59,  -428,   363,  -428,  -428,  -428,    64,
    -428,  -428,    37,  -428,   196,  -428,   129,   -66
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    31,    32,    33,    75,   126,   172,   288,
     111,   112,    35,    36,    37,    38,    39,    40,   305,   361,
     439,   474,    41,    42,    43,    44,    45,   113,   320,    46,
     101,   102,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,   389,   391,   283,   139,    77,    78,    79,    80,
      81,    82,    83,   173,    84,    85,   154,   343,    86,   140,
     141,   221,   307,   308,   265,   266,   380,   150
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      76,   271,    88,   273,   291,    92,    93,   274,   151,   275,
      96,   276,   470,   277,   471,   278,    34,   106,   145,     3,
     215,   279,   148,    57,    90,   280,    59,    60,    61,   281,
     282,    62,   472,   100,   473,   464,   475,   470,     7,   471,
      60,    63,    64,    65,    66,   476,   433,    67,   181,    68,
      69,     7,    91,   116,   117,   176,   337,   472,   452,   473,
     477,   475,   478,   508,   177,   480,   149,   176,    58,    22,
     476,    70,   182,   481,   485,     4,   177,   490,   338,     5,
     453,   470,    22,   471,   152,   477,     7,   478,     8,    71,
     480,   103,    72,    89,   434,    73,   153,    74,   481,   485,
      97,   472,   490,   473,   105,   475,    13,    14,    94,    16,
      17,    18,    95,   100,   476,    21,   205,    22,   207,    23,
     348,   349,   142,    27,    28,   292,   293,   193,   208,   477,
      98,   478,   209,   194,   480,   195,    59,    60,    61,   104,
     310,    62,   481,   485,   220,   189,   490,   359,     7,   270,
     360,    63,    64,    65,    66,   335,    60,    67,   378,    68,
      69,   268,   295,   219,   114,   269,   296,     7,   122,   272,
     294,   118,   119,   298,   299,   300,   301,   384,     4,    22,
     219,    70,     5,   123,   425,   124,   189,   428,   190,     7,
     107,     8,   174,   175,   125,   168,   169,   115,    22,    71,
     340,   108,    72,   100,   329,    73,   330,    74,   214,    13,
      14,   219,    16,    17,    18,   109,   267,   120,    21,   121,
      22,    99,    23,   127,   309,   128,    27,    28,   286,   287,
     289,   290,   110,   271,   318,   273,   271,   321,   273,   274,
     129,   275,   274,   276,   275,   277,   276,   278,   277,   130,
     278,   131,   424,   279,   132,   197,   279,   280,   107,   133,
     280,   281,   282,   134,   281,   282,   135,   136,   377,   108,
     137,   303,   138,   383,   143,   144,   147,   312,   155,   314,
     333,   156,   157,   109,   178,   467,   179,   468,   322,   180,
     183,   469,   184,   479,   185,   482,   356,   483,   358,   484,
     110,   186,   187,   191,   192,   486,   196,   199,   200,   487,
     467,   202,   468,   488,   489,   201,   469,    34,   479,   203,
     482,   211,   483,   206,   484,   212,    34,   213,    34,   210,
     486,   381,   382,   216,   487,   297,    34,   302,   488,   489,
     217,   304,   306,   313,   315,   316,   317,   319,   325,   397,
     323,   324,   327,   401,   467,   326,   468,   345,   328,    34,
     469,   331,   479,   334,   482,    74,   483,   336,   484,   339,
     346,   417,   341,   418,   486,   347,   350,   342,   487,   344,
     351,   368,   488,   489,   353,   352,   355,   354,   363,   362,
     364,   374,   375,   369,   371,   379,   376,   390,   399,   365,
     400,   272,   370,   392,   272,   395,   385,   398,   386,   403,
      34,   402,   404,   405,   406,   393,   394,    59,    60,    61,
     408,    34,    62,   409,   410,    34,   412,   461,   413,     7,
     414,   415,    63,    64,    65,    66,   426,   416,   419,   436,
     422,   396,   423,   429,   497,    34,   499,   500,   437,    34,
     438,   430,   407,    34,   440,    34,    34,   411,   460,   450,
      22,    34,    70,    34,   444,   445,   451,   448,    34,   493,
     455,    34,   456,   458,   465,   503,    34,   504,    34,   505,
     494,   507,   431,    72,   509,   435,    73,   525,    74,   510,
     511,   441,   521,   442,   512,   513,   514,   515,   516,   447,
     517,   449,   518,   519,   522,   520,   526,   523,   528,   529,
     524,   454,   527,   530,   457,   532,   544,   533,    59,    60,
      61,   462,    34,    62,   534,   535,   536,   537,   538,   373,
       7,   311,   543,    63,    64,    65,    66,   539,   540,    67,
     542,    68,    69,   545,   171,    34,   284,   158,   159,   160,
     161,   162,   163,   164,   165,   166,   167,   168,   169,   285,
     204,    22,   123,    70,   146,   198,   170,   158,   159,   160,
     161,   162,   163,   164,   165,   166,   167,   188,   332,   421,
     498,    71,     0,     0,    72,     0,     0,    73,   218,    74,
     222,     0,   531,   219,     0,   223,   224,     0,   225,   226,
       0,   227,     0,   228,   229,   230,   231,     0,   232,   233,
     234,   235,   236,   237,   238,   239,     0,   240,   241,   242,
     243,   244,   245,   246,   247,   248,   249,   250,   251,   252,
     253,   254,   255,   256,   257,   258,   259,   260,   261,   262,
     263,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   420,     0,     0,     0,     0,   223,
     224,   264,   225,   226,   219,   227,     0,   228,   229,   230,
     231,     0,   232,   233,   234,   235,   236,   237,   238,   239,
       0,   240,   241,   242,   243,   244,   245,   246,   247,   248,
     249,   250,   251,   252,   253,   254,   255,   256,   257,   258,
     259,   260,   261,   262,   263,     4,     0,     0,     0,     5,
       0,     6,     0,     0,   387,     0,   388,     0,     8,     0,
       0,     0,     0,     0,     0,     0,     9,     0,   219,    10,
       0,     0,     0,     0,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,     0,    21,     0,    22,     0,    23,
      24,    25,    26,    27,    28,    29,     4,     0,     0,     0,
       5,     0,     6,     0,     0,     0,     0,     7,     0,     8,
       0,     0,     0,     0,     0,     0,     0,     9,     0,    30,
      10,     0,     0,     0,     0,    11,    12,    13,    14,    15,
      16,    17,    18,    19,    20,     0,    21,     0,    22,     0,
      23,    24,    25,    26,    27,    28,    29,     4,     0,     0,
       0,     5,     0,     6,     0,     0,     0,     0,   357,     0,
       8,     0,     0,     0,     0,     0,     0,     0,     9,     0,
      30,    10,     0,     0,     0,     0,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     4,     0,
       0,     0,     5,     0,     6,     0,     0,     0,     0,   366,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    30,    10,     0,     0,     0,     0,    11,    12,    13,
      14,    15,    16,    17,    18,    19,    20,     0,    21,     0,
      22,     0,    23,    24,    25,    26,    27,    28,    29,     4,
       0,     0,     0,     5,     0,     6,     0,     0,     0,     0,
     367,     0,     8,     0,     0,     0,     0,     0,     0,     0,
       9,     0,    30,    10,     0,     0,     0,     0,    11,    12,
      13,    14,    15,    16,    17,    18,    19,    20,     0,    21,
       0,    22,     0,    23,    24,    25,    26,    27,    28,    29,
       4,     0,     0,     0,     5,     0,     6,     0,     0,     0,
       0,   372,     0,     8,     0,     0,     0,     0,     0,     0,
       0,     9,     0,    30,    10,     0,     0,     0,     0,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,     0,
      21,     0,    22,     0,    23,    24,    25,    26,    27,    28,
      29,     4,     0,     0,     0,     5,     0,     6,     0,     0,
       0,     0,   432,     0,     8,     0,     0,     0,     0,     0,
       0,     0,     9,     0,    30,    10,     0,     0,     0,     0,
      11,    12,    13,    14,    15,    16,    17,    18,    19,    20,
       0,    21,     0,    22,     0,    23,    24,    25,    26,    27,
      28,    29,     4,     0,     0,     0,     5,     0,     6,     0,
       0,     0,     0,   443,     0,     8,     0,     0,     0,     0,
       0,     0,     0,     9,     0,    30,    10,     0,     0,     0,
       0,    11,    12,    13,    14,    15,    16,    17,    18,    19,
      20,     0,    21,     0,    22,     0,    23,    24,    25,    26,
      27,    28,    29,     4,     0,     0,     0,     5,     0,     6,
       0,     0,     0,     0,   446,     0,     8,     0,     0,     0,
       0,     0,     0,     0,     9,     0,    30,    10,     0,     0,
       0,     0,    11,    12,    13,    14,    15,    16,    17,    18,
      19,    20,     0,    21,     0,    22,     0,    23,    24,    25,
      26,    27,    28,    29,     4,     0,     0,     0,     5,     0,
       6,     0,     0,     0,     0,   459,     0,     8,     0,     0,
       0,     0,     0,     0,     0,     9,     0,    30,    10,     0,
       0,     0,     0,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,     0,    21,     0,    22,     0,    23,    24,
      25,    26,    27,    28,    29,     4,     0,     0,     0,     5,
       0,     6,     0,     0,     0,     0,   463,     0,     8,     0,
       0,     0,     0,     0,     0,     0,     9,     0,    30,    10,
       0,     0,     0,     0,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,     0,    21,     0,    22,     0,    23,
      24,    25,    26,    27,    28,    29,     4,     0,     0,     0,
       5,     0,     6,     0,     0,     0,     0,     7,     0,     8,
       0,     0,     0,     0,     0,     0,     0,     9,     0,    30,
      10,     0,     0,     0,     0,    11,    12,    13,    14,    15,
      16,    17,    18,    19,    20,     0,    21,     0,    22,     0,
      23,    24,    25,    26,    27,    28,    29,     4,     0,     0,
       0,     5,     0,     6,     0,     0,     0,     0,   491,     0,
       8,     0,     0,     0,     0,     0,     0,     0,     9,     0,
     466,    10,     0,     0,     0,     0,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     4,     0,
       0,     0,     5,     0,     6,     0,     0,     0,     0,   492,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    30,    10,     0,     0,     0,     0,    11,    12,    13,
      14,    15,    16,    17,    18,    19,    20,     0,    21,     0,
      22,     0,    23,    24,    25,    26,    27,    28,    29,     4,
       0,     0,     0,     5,     0,     6,     0,     0,     0,     0,
     495,     0,     8,     0,     0,     0,     0,     0,     0,     0,
       9,     0,    30,    10,     0,     0,     0,     0,    11,    12,
      13,    14,    15,    16,    17,    18,    19,    20,     0,    21,
       0,    22,     0,    23,    24,    25,    26,    27,    28,    29,
       4,     0,     0,     0,     5,     0,     6,     0,     0,     0,
       0,   496,     0,     8,     0,     0,     0,     0,     0,     0,
       0,     9,     0,    30,    10,     0,     0,     0,     0,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,     0,
      21,     0,    22,     0,    23,    24,    25,    26,    27,    28,
      29,     4,     0,     0,     0,     5,     0,     6,     0,     0,
       0,     0,   501,     0,     8,     0,     0,     0,     0,     0,
       0,     0,     9,     0,    30,    10,     0,     0,     0,     0,
      11,    12,    13,    14,    15,    16,    17,    18,    19,    20,
       0,    21,     0,    22,     0,    23,    24,    25,    26,    27,
      28,    29,     4,     0,     0,     0,     5,     0,     6,     0,
       0,     0,     0,   502,     0,     8,     0,     0,     0,     0,
       0,     0,     0,     9,     0,    30,    10,     0,     0,     0,
       0,    11,    12,    13,    14,    15,    16,    17,    18,    19,
      20,     0,    21,     0,    22,     0,    23,    24,    25,    26,
      27,    28,    29,     4,     0,     0,     0,     5,     0,     6,
       0,     0,     0,     0,   506,     0,     8,     0,     0,     0,
       0,     0,     0,     0,     9,     0,    30,    10,     0,     0,
       0,     0,    11,    12,    13,    14,    15,    16,    17,    18,
      19,    20,     0,    21,     0,    22,     0,    23,    24,    25,
      26,    27,    28,    29,     4,     0,     0,     0,     5,     0,
       6,     0,     0,     0,     0,   541,     0,     8,     0,     0,
       0,     0,     0,     0,     0,     9,     0,    30,    10,     0,
       0,     0,     0,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,     0,    21,     0,    22,     0,    23,    24,
      25,    26,    27,    28,    29,     4,     0,     0,     0,     5,
       0,     0,     0,     0,     0,     0,     7,     0,     8,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    30,     0,
       0,     0,     0,     0,     0,     0,    13,    14,     0,    16,
      17,    18,     0,     0,     0,    21,     0,    22,     0,    23,
      59,    60,    61,    27,    28,    62,     0,     0,     0,     0,
       0,     0,     7,     0,     0,    63,    64,    65,    66,     0,
       0,    67,     0,    68,    69,     0,    87,     0,     0,   427,
       0,     0,    59,    60,    61,     0,     0,    62,     0,     0,
       0,     0,     0,    22,     7,    70,     0,    63,    64,    65,
      66,     0,     0,    67,     0,    68,    69,     0,     0,     0,
       0,     0,     0,    71,     0,     0,    72,     0,     0,    73,
       0,    74,     0,     0,     0,    22,     0,    70,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    71,     0,     0,    72,     0,
       0,    73,     0,    74
};

static const yytype_int16 yycheck[] =
{
       6,   155,     8,   155,   173,    11,    12,   155,    74,   155,
      16,   155,   439,   155,   439,   155,     2,    23,    67,     0,
      25,   155,    71,    69,     4,   155,     3,     4,     5,   155,
     155,     8,   439,    19,   439,   437,   439,   464,    15,   464,
       4,    18,    19,    20,    21,   439,    30,    24,    31,    26,
      27,    15,    32,     4,     5,    72,    55,   464,    55,   464,
     439,   464,   439,   465,    81,   439,    72,    72,    69,    46,
     464,    48,    55,   439,   439,     4,    81,   439,    77,     8,
      77,   508,    46,   508,    69,   464,    15,   464,    17,    66,
     464,    43,    69,     4,    78,    72,    81,    74,   464,   464,
       4,   508,   464,   508,    81,   508,    35,    36,     4,    38,
      39,    40,     8,    99,   508,    44,   122,    46,   124,    48,
     289,   290,    58,    52,    53,   174,   175,    39,     4,   508,
       4,   508,     8,    45,   508,    47,     3,     4,     5,    48,
      71,     8,   508,   508,   150,    76,   508,    11,    15,    78,
      14,    18,    19,    20,    21,   221,     4,    24,    73,    26,
      27,     4,     4,    78,     4,     8,     8,    15,    55,   155,
     176,     4,     5,   179,   180,   181,   182,    75,     4,    46,
      78,    48,     8,    70,   387,    72,    76,   390,    78,    15,
       4,    17,    67,    68,    81,    65,    66,     4,    46,    66,
     266,    15,    69,   189,   210,    72,   212,    74,   144,    35,
      36,    78,    38,    39,    40,    29,   152,    49,    44,    78,
      46,    69,    48,    77,   187,    78,    52,    53,   168,   169,
     171,   172,    46,   387,   197,   387,   390,   200,   390,   387,
      78,   387,   390,   387,   390,   387,   390,   387,   390,    78,
     390,    78,    78,   387,    78,    69,   390,   387,     4,    78,
     390,   387,   387,    78,   390,   390,    78,    78,   334,    15,
      78,   184,    78,   339,     4,    69,     4,   190,    12,   192,
     216,    23,    22,    29,     3,   439,    48,   439,   201,    69,
       4,   439,    78,   439,    78,   439,   302,   439,   304,   439,
      46,    69,    69,    81,    78,   439,     4,    28,    69,   439,
     464,     4,   464,   439,   439,    78,   464,   303,   464,     4,
     464,    71,   464,     6,   464,    76,   312,    71,   314,    55,
     464,   337,   338,    69,   464,     4,   322,    31,   464,   464,
      71,    11,     4,     4,     4,    46,    69,     4,    28,   355,
       5,     5,    69,   359,   508,    73,   508,   270,    69,   345,
     508,    71,   508,    76,   508,    74,   508,    69,   508,    76,
      78,   377,    71,   379,   508,     7,    73,    69,   508,    69,
      69,   317,   508,   508,    71,    69,    29,    78,    76,    71,
      71,   327,   328,    71,    71,     4,    71,    13,    33,    78,
      12,   387,    78,    75,   390,    78,   342,    78,   344,    16,
     396,    78,    78,     4,    78,   351,   352,     3,     4,     5,
      41,   407,     8,    42,    71,   411,    78,   433,    51,    15,
      78,    71,    18,    19,    20,    21,    10,    71,    71,    78,
      71,   354,    71,    71,   450,   431,   452,   453,    78,   435,
      12,    71,   365,   439,    78,   441,   442,   370,    28,    77,
      46,   447,    48,   449,    78,    78,    69,    78,   454,    41,
      78,   457,    78,    78,    78,    25,   462,    78,   464,    78,
      49,    28,   395,    69,    78,   398,    72,    50,    74,    78,
      78,   404,    37,   406,    78,    78,    78,    78,    78,   412,
      78,   414,    78,    78,    37,    78,    49,    78,    10,    10,
      78,   424,    71,    78,   427,    28,    28,    78,     3,     4,
       5,   434,   508,     8,    78,    78,    78,    78,    77,   325,
      15,   189,   538,    18,    19,    20,    21,    78,    78,    24,
      78,    26,    27,    78,    80,   531,   156,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,   157,
     120,    46,    70,    48,    68,   111,    74,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    99,   215,   383,
     451,    66,    -1,    -1,    69,    -1,    -1,    72,    73,    74,
       4,    -1,   505,    78,    -1,     9,    10,    -1,    12,    13,
      -1,    15,    -1,    17,    18,    19,    20,    -1,    22,    23,
      24,    25,    26,    27,    28,    29,    -1,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,     4,    -1,    -1,    -1,    -1,     9,
      10,    75,    12,    13,    78,    15,    -1,    17,    18,    19,
      20,    -1,    22,    23,    24,    25,    26,    27,    28,    29,
      -1,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,     4,    -1,    -1,    -1,     8,
      -1,    10,    -1,    -1,    13,    -1,    15,    -1,    17,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    25,    -1,    78,    28,
      -1,    -1,    -1,    -1,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    -1,    44,    -1,    46,    -1,    48,
      49,    50,    51,    52,    53,    54,     4,    -1,    -1,    -1,
       8,    -1,    10,    -1,    -1,    -1,    -1,    15,    -1,    17,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,    -1,    78,
      28,    -1,    -1,    -1,    -1,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    -1,    44,    -1,    46,    -1,
      48,    49,    50,    51,    52,    53,    54,     4,    -1,    -1,
      -1,     8,    -1,    10,    -1,    -1,    -1,    -1,    15,    -1,
      17,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,    -1,
      78,    28,    -1,    -1,    -1,    -1,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    -1,    44,    -1,    46,
      -1,    48,    49,    50,    51,    52,    53,    54,     4,    -1,
      -1,    -1,     8,    -1,    10,    -1,    -1,    -1,    -1,    15,
      -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,
      -1,    78,    28,    -1,    -1,    -1,    -1,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    -1,    44,    -1,
      46,    -1,    48,    49,    50,    51,    52,    53,    54,     4,
      -1,    -1,    -1,     8,    -1,    10,    -1,    -1,    -1,    -1,
      15,    -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      25,    -1,    78,    28,    -1,    -1,    -1,    -1,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    -1,    44,
      -1,    46,    -1,    48,    49,    50,    51,    52,    53,    54,
       4,    -1,    -1,    -1,     8,    -1,    10,    -1,    -1,    -1,
      -1,    15,    -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    25,    -1,    78,    28,    -1,    -1,    -1,    -1,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    -1,
      44,    -1,    46,    -1,    48,    49,    50,    51,    52,    53,
      54,     4,    -1,    -1,    -1,     8,    -1,    10,    -1,    -1,
      -1,    -1,    15,    -1,    17,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    25,    -1,    78,    28,    -1,    -1,    -1,    -1,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      -1,    44,    -1,    46,    -1,    48,    49,    50,    51,    52,
      53,    54,     4,    -1,    -1,    -1,     8,    -1,    10,    -1,
      -1,    -1,    -1,    15,    -1,    17,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    25,    -1,    78,    28,    -1,    -1,    -1,
      -1,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    -1,    44,    -1,    46,    -1,    48,    49,    50,    51,
      52,    53,    54,     4,    -1,    -1,    -1,     8,    -1,    10,
      -1,    -1,    -1,    -1,    15,    -1,    17,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    25,    -1,    78,    28,    -1,    -1,
      -1,    -1,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    -1,    44,    -1,    46,    -1,    48,    49,    50,
      51,    52,    53,    54,     4,    -1,    -1,    -1,     8,    -1,
      10,    -1,    -1,    -1,    -1,    15,    -1,    17,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    25,    -1,    78,    28,    -1,
      -1,    -1,    -1,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    -1,    44,    -1,    46,    -1,    48,    49,
      50,    51,    52,    53,    54,     4,    -1,    -1,    -1,     8,
      -1,    10,    -1,    -1,    -1,    -1,    15,    -1,    17,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    25,    -1,    78,    28,
      -1,    -1,    -1,    -1,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    -1,    44,    -1,    46,    -1,    48,
      49,    50,    51,    52,    53,    54,     4,    -1,    -1,    -1,
       8,    -1,    10,    -1,    -1,    -1,    -1,    15,    -1,    17,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,    -1,    78,
      28,    -1,    -1,    -1,    -1,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    -1,    44,    -1,    46,    -1,
      48,    49,    50,    51,    52,    53,    54,     4,    -1,    -1,
      -1,     8,    -1,    10,    -1,    -1,    -1,    -1,    15,    -1,
      17,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,    -1,
      78,    28,    -1,    -1,    -1,    -1,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    -1,    44,    -1,    46,
      -1,    48,    49,    50,    51,    52,    53,    54,     4,    -1,
      -1,    -1,     8,    -1,    10,    -1,    -1,    -1,    -1,    15,
      -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,
      -1,    78,    28,    -1,    -1,    -1,    -1,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    -1,    44,    -1,
      46,    -1,    48,    49,    50,    51,    52,    53,    54,     4,
      -1,    -1,    -1,     8,    -1,    10,    -1,    -1,    -1,    -1,
      15,    -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      25,    -1,    78,    28,    -1,    -1,    -1,    -1,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    -1,    44,
      -1,    46,    -1,    48,    49,    50,    51,    52,    53,    54,
       4,    -1,    -1,    -1,     8,    -1,    10,    -1,    -1,    -1,
      -1,    15,    -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    25,    -1,    78,    28,    -1,    -1,    -1,    -1,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    -1,
      44,    -1,    46,    -1,    48,    49,    50,    51,    52,    53,
      54,     4,    -1,    -1,    -1,     8,    -1,    10,    -1,    -1,
      -1,    -1,    15,    -1,    17,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    25,    -1,    78,    28,    -1,    -1,    -1,    -1,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      -1,    44,    -1,    46,    -1,    48,    49,    50,    51,    52,
      53,    54,     4,    -1,    -1,    -1,     8,    -1,    10,    -1,
      -1,    -1,    -1,    15,    -1,    17,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    25,    -1,    78,    28,    -1,    -1,    -1,
      -1,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    -1,    44,    -1,    46,    -1,    48,    49,    50,    51,
      52,    53,    54,     4,    -1,    -1,    -1,     8,    -1,    10,
      -1,    -1,    -1,    -1,    15,    -1,    17,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    25,    -1,    78,    28,    -1,    -1,
      -1,    -1,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    -1,    44,    -1,    46,    -1,    48,    49,    50,
      51,    52,    53,    54,     4,    -1,    -1,    -1,     8,    -1,
      10,    -1,    -1,    -1,    -1,    15,    -1,    17,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    25,    -1,    78,    28,    -1,
      -1,    -1,    -1,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    -1,    44,    -1,    46,    -1,    48,    49,
      50,    51,    52,    53,    54,     4,    -1,    -1,    -1,     8,
      -1,    -1,    -1,    -1,    -1,    -1,    15,    -1,    17,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    78,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    35,    36,    -1,    38,
      39,    40,    -1,    -1,    -1,    44,    -1,    46,    -1,    48,
       3,     4,     5,    52,    53,     8,    -1,    -1,    -1,    -1,
      -1,    -1,    15,    -1,    -1,    18,    19,    20,    21,    -1,
      -1,    24,    -1,    26,    27,    -1,    29,    -1,    -1,    78,
      -1,    -1,     3,     4,     5,    -1,    -1,     8,    -1,    -1,
      -1,    -1,    -1,    46,    15,    48,    -1,    18,    19,    20,
      21,    -1,    -1,    24,    -1,    26,    27,    -1,    -1,    -1,
      -1,    -1,    -1,    66,    -1,    -1,    69,    -1,    -1,    72,
      -1,    74,    -1,    -1,    -1,    46,    -1,    48,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    66,    -1,    -1,    69,    -1,
      -1,    72,    -1,    74
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    83,    84,     0,     4,     8,    10,    15,    17,    25,
      28,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    44,    46,    48,    49,    50,    51,    52,    53,    54,
      78,    85,    86,    87,    88,    94,    95,    96,    97,    98,
      99,   104,   105,   106,   107,   108,   111,   114,   115,   116,
     117,   118,   119,   120,   121,   122,   123,    69,    69,     3,
       4,     5,     8,    18,    19,    20,    21,    24,    26,    27,
      48,    66,    69,    72,    74,    88,   127,   128,   129,   130,
     131,   132,   133,   134,   136,   137,   140,    29,   127,     4,
       4,    32,   127,   127,     4,     8,   127,     4,     4,    69,
      88,   112,   113,    43,    48,    81,   127,     4,    15,    29,
      46,    92,    93,   109,     4,     4,     4,     5,     4,     5,
      49,    78,    55,    70,    72,    81,    89,    77,    78,    78,
      78,    78,    78,    78,    78,    78,    78,    78,    78,   127,
     141,   142,   141,     4,    69,   133,   134,     4,   133,   127,
     149,   149,    69,    81,   138,    12,    23,    22,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      74,    89,    90,   135,    67,    68,    72,    81,     3,    48,
      69,    31,    55,     4,    78,    78,    69,    69,   112,    76,
      78,    81,    78,    39,    45,    47,     4,    69,    93,    28,
      69,    78,     4,     4,   109,   127,     6,   127,     4,     8,
      55,    71,    76,    71,   141,    25,    69,    71,    73,    78,
     127,   143,     4,     9,    10,    12,    13,    15,    17,    18,
      19,    20,    22,    23,    24,    25,    26,    27,    28,    29,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    75,   146,   147,   141,     4,     8,
      78,    86,    88,    94,    95,   108,   115,   116,   117,   119,
     120,   121,   122,   126,   129,   130,   132,   132,    91,   135,
     135,   131,   133,   133,   127,     4,     8,     4,   127,   127,
     127,   127,    31,    84,    11,   100,     4,   144,   145,   144,
      71,   113,    84,     4,    84,     4,    46,    69,   144,     4,
     110,   144,    84,     5,     5,    28,    73,    69,    69,   127,
     127,    71,   137,   141,    76,   149,    69,    55,    77,    76,
     149,    71,    69,   139,    69,    84,    78,     7,   131,   131,
      73,    69,    69,    71,    78,    29,   127,    15,   127,    11,
      14,   101,    71,    76,    71,    78,    15,    15,   141,    71,
      78,    71,    15,   110,   141,   141,    71,   149,    73,     4,
     148,   127,   127,   149,    75,   141,   141,    13,    15,   124,
      13,   125,    75,   141,   141,    78,    84,   127,    78,    33,
      12,   127,    78,    16,    78,     4,    78,    84,    41,    42,
      71,    84,    78,    51,    78,    71,    71,   127,   127,    71,
       4,   146,    71,    71,    78,   126,    10,    78,   126,    71,
      71,    84,    15,    30,    78,    84,    78,    78,    12,   102,
      78,    84,    84,    15,    78,    78,    15,    84,    78,    84,
      77,    69,    55,    77,    84,    78,    78,    84,    78,    15,
      28,   127,    84,    15,   102,    78,    78,    86,    94,    95,
      96,    97,    98,    99,   103,   104,   105,   106,   107,   108,
     111,   114,   115,   116,   117,   118,   119,   120,   121,   122,
     123,    15,    15,    41,    49,    15,    15,   127,   148,   127,
     127,    15,    15,    25,    78,    78,    15,    28,   102,    78,
      78,    78,    78,    78,    78,    78,    78,    78,    78,    78,
      78,    37,    37,    78,    78,    50,    49,    71,    10,    10,
      78,    84,    28,    78,    78,    78,    78,    78,    77,    78,
      78,    15,    78,   127,    28,    78
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    82,    83,    84,    84,    84,    85,    85,    85,    85,
      85,    85,    85,    85,    85,    85,    85,    85,    85,    85,
      85,    85,    85,    85,    85,    85,    85,    85,    85,    86,
      86,    87,    87,    87,    88,    88,    88,    89,    91,    90,
      92,    92,    93,    93,    93,    93,    94,    94,    95,    95,
      95,    95,    95,    96,    97,    97,    97,    97,    98,    99,
     100,   100,   101,   101,   102,   102,   102,   103,   103,   103,
     103,   103,   103,   103,   103,   103,   103,   103,   103,   103,
     103,   103,   103,   103,   103,   103,   103,   103,   103,   103,
     104,   104,   105,   105,   106,   107,   108,   108,   108,   108,
     108,   108,   109,   109,   110,   111,   111,   112,   112,   113,
     113,   114,   115,   115,   115,   116,   117,   117,   118,   119,
     120,   121,   122,   123,   123,   124,   124,   124,   125,   125,
     125,   126,   126,   126,   126,   126,   126,   126,   126,   126,
     126,   126,   127,   128,   128,   129,   129,   130,   130,   130,
     130,   131,   131,   131,   132,   132,   132,   133,   133,   133,
     133,   133,   133,   134,   134,   134,   134,   134,   135,   135,
     135,   135,   135,   135,   135,   135,   135,   135,   136,   136,
     136,   136,   136,   136,   136,   136,   136,   136,   136,   136,
     136,   136,   137,   137,   138,   138,   138,   138,   139,   139,
     140,   140,   141,   141,   142,   142,   143,   143,   144,   144,
     145,   145,   146,   146,   146,   146,   146,   146,   146,   146,
     146,   146,   146,   146,   146,   146,   146,   146,   146,   146,
     146,   146,   146,   146,   146,   146,   146,   146,   146,   146,
     146,   146,   146,   146,   146,   146,   146,   146,   146,   146,
     146,   146,   146,   146,   147,   147,   147,   147,   147,   147,
     148,   148,   149,   149
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     1,     1,     1,     1,     2,     1,     1,
       2,     2,     2,     2,     2,     2,     2,     2,     1,     3,
       4,     1,     4,     3,     1,     1,     1,     2,     0,     4,
       1,     2,     1,     1,     1,     1,     2,     4,     4,     4,
       6,     6,     6,    10,     9,    10,    11,    13,     7,     7,
       5,     6,     0,     3,     0,     2,     2,     2,     2,     2,
       1,     1,     1,     1,     1,     1,     1,     1,     2,     1,
       1,     2,     2,     2,     2,     2,     2,     2,     2,     1,
      10,    10,     9,    10,    10,     7,     2,     2,     2,     2,
       4,     4,     1,     4,     1,     9,     7,     1,     3,     1,
       3,     7,     4,     4,     3,     2,     1,     2,     2,     2,
       2,     1,     1,     6,     6,     3,     3,     6,     0,     3,
       6,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     3,     1,     3,     1,     3,     4,
       4,     1,     3,     3,     1,     3,     3,     1,     2,     2,
       2,     4,     5,     1,     4,     3,     6,     6,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     2,     4,     1,     1,     1,     1,     1,     3,     3,
       5,     1,     3,     5,     0,     3,     3,     5,     0,     3,
       2,     3,     0,     1,     1,     3,     1,     4,     0,     1,
       1,     3,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     3,     3,     6,     6,     6,     9,
       1,     2,     0,     2
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
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2566 "src/parser.tab.c"
        break;

    case YYSYMBOL_STRING: /* STRING  */
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2572 "src/parser.tab.c"
        break;

    case YYSYMBOL_MOD_CONTENT: /* MOD_CONTENT  */
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2578 "src/parser.tab.c"
        break;

    case YYSYMBOL_LENS_CONTENT: /* LENS_CONTENT  */
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2584 "src/parser.tab.c"
        break;

    case YYSYMBOL_QUALIFIED_IDENT: /* QUALIFIED_IDENT  */
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2590 "src/parser.tab.c"
        break;

    case YYSYMBOL_program: /* program  */
#line 716 "src/parser.y"
            { (void) ((*yyvaluep).stmt_list); }
#line 2596 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement_list: /* statement_list  */
#line 698 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2602 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement: /* statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2608 "src/parser.tab.c"
        break;

    case YYSYMBOL_assignment: /* assignment  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2614 "src/parser.tab.c"
        break;

    case YYSYMBOL_lvalue: /* lvalue  */
#line 696 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2620 "src/parser.tab.c"
        break;

    case YYSYMBOL_variable_name: /* variable_name  */
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2626 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier: /* modifier  */
#line 703 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 2632 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_lens: /* comparison_lens  */
#line 703 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 2638 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_name: /* modifier_name  */
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2644 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_word: /* modifier_word  */
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2650 "src/parser.tab.c"
        break;

    case YYSYMBOL_print_statement: /* print_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2656 "src/parser.tab.c"
        break;

    case YYSYMBOL_call_statement: /* call_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2662 "src/parser.tab.c"
        break;

    case YYSYMBOL_with_lock_statement: /* with_lock_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2668 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_each_statement: /* for_each_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2674 "src/parser.tab.c"
        break;

    case YYSYMBOL_while_statement: /* while_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2680 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement: /* consider_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2686 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_branch_list: /* consider_branch_list  */
#line 701 "src/parser.y"
            { ast_free_consider_branch_list(((*yyvaluep).consider_branch_list)); }
#line 2692 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_else_opt: /* consider_else_opt  */
#line 698 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2698 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement_list: /* consider_statement_list  */
#line 698 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2704 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_body_statement: /* consider_body_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2710 "src/parser.tab.c"
        break;

    case YYSYMBOL_function_statement: /* function_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2716 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_statement: /* modifier_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2722 "src/parser.tab.c"
        break;

    case YYSYMBOL_program_statement: /* program_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2728 "src/parser.tab.c"
        break;

    case YYSYMBOL_library_statement: /* library_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2734 "src/parser.tab.c"
        break;

    case YYSYMBOL_use_statement: /* use_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2740 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_signature: /* modifier_signature  */
#line 704 "src/parser.y"
            { ast_free_modifier_signature(((*yyvaluep).modifier_signature)); }
#line 2746 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_context: /* modifier_context  */
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2752 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_statement: /* watch_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2758 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_list: /* watch_target_list  */
#line 702 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2764 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_path: /* watch_target_path  */
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2770 "src/parser.tab.c"
        break;

    case YYSYMBOL_without_watchers_statement: /* without_watchers_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2776 "src/parser.tab.c"
        break;

    case YYSYMBOL_on_error_statement: /* on_error_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2782 "src/parser.tab.c"
        break;

    case YYSYMBOL_error_statement: /* error_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2788 "src/parser.tab.c"
        break;

    case YYSYMBOL_return_statement: /* return_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2794 "src/parser.tab.c"
        break;

    case YYSYMBOL_label_statement: /* label_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2800 "src/parser.tab.c"
        break;

    case YYSYMBOL_goto_statement: /* goto_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2806 "src/parser.tab.c"
        break;

    case YYSYMBOL_gosub_statement: /* gosub_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2812 "src/parser.tab.c"
        break;

    case YYSYMBOL_break_statement: /* break_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2818 "src/parser.tab.c"
        break;

    case YYSYMBOL_continue_statement: /* continue_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2824 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_statement: /* if_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2830 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_block_tail: /* if_block_tail  */
#line 698 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2836 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_inline_tail: /* if_inline_tail  */
#line 698 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2842 "src/parser.tab.c"
        break;

    case YYSYMBOL_inline_statement: /* inline_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2848 "src/parser.tab.c"
        break;

    case YYSYMBOL_expression: /* expression  */
#line 696 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2854 "src/parser.tab.c"
        break;

    case YYSYMBOL_or_expression: /* or_expression  */
#line 696 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2860 "src/parser.tab.c"
        break;

    case YYSYMBOL_and_expression: /* and_expression  */
#line 696 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2866 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_expression: /* comparison_expression  */
#line 696 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2872 "src/parser.tab.c"
        break;

    case YYSYMBOL_additive_expression: /* additive_expression  */
#line 696 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2878 "src/parser.tab.c"
        break;

    case YYSYMBOL_multiplicative_expression: /* multiplicative_expression  */
#line 696 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2884 "src/parser.tab.c"
        break;

    case YYSYMBOL_unary_expression: /* unary_expression  */
#line 696 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2890 "src/parser.tab.c"
        break;

    case YYSYMBOL_postfix_expression: /* postfix_expression  */
#line 696 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2896 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_operator: /* comparison_operator  */
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2902 "src/parser.tab.c"
        break;

    case YYSYMBOL_primary: /* primary  */
#line 696 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2908 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_literal: /* record_literal  */
#line 696 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2914 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_suffix: /* ident_suffix  */
#line 705 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 2920 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_dot_suffix: /* ident_dot_suffix  */
#line 705 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 2926 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list_opt: /* argument_list_opt  */
#line 699 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 2932 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list: /* argument_list  */
#line 699 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 2938 "src/parser.tab.c"
        break;

    case YYSYMBOL_array_argument_list: /* array_argument_list  */
#line 699 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 2944 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list_opt: /* parameter_list_opt  */
#line 702 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2950 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list: /* parameter_list  */
#line 702 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2956 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_name: /* field_name  */
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2962 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_field_list: /* record_field_list  */
#line 700 "src/parser.y"
            { ast_free_record_field_list(((*yyvaluep).record_field_list)); }
#line 2968 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_policy: /* field_policy  */
#line 706 "src/parser.y"
            { ast_free_expr(((*yyvaluep).field_policy).reset_expr); }
#line 2974 "src/parser.tab.c"
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
#line 721 "src/parser.y"
                     { ctx->parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3280 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 725 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3286 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 726 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3292 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 727 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3298 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 731 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3304 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 732 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3310 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 733 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3316 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 734 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3322 "src/parser.tab.c"
    break;

  case 10: /* statement: for_each_statement  */
#line 735 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3328 "src/parser.tab.c"
    break;

  case 11: /* statement: while_statement  */
#line 736 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3334 "src/parser.tab.c"
    break;

  case 12: /* statement: consider_statement  */
#line 737 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3340 "src/parser.tab.c"
    break;

  case 13: /* statement: function_statement  */
#line 738 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3346 "src/parser.tab.c"
    break;

  case 14: /* statement: modifier_statement  */
#line 739 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3352 "src/parser.tab.c"
    break;

  case 15: /* statement: program_statement  */
#line 740 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3358 "src/parser.tab.c"
    break;

  case 16: /* statement: library_statement  */
#line 741 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3364 "src/parser.tab.c"
    break;

  case 17: /* statement: use_statement NEWLINE  */
#line 742 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3370 "src/parser.tab.c"
    break;

  case 18: /* statement: watch_statement  */
#line 743 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3376 "src/parser.tab.c"
    break;

  case 19: /* statement: without_watchers_statement  */
#line 744 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3382 "src/parser.tab.c"
    break;

  case 20: /* statement: on_error_statement NEWLINE  */
#line 745 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3388 "src/parser.tab.c"
    break;

  case 21: /* statement: error_statement NEWLINE  */
#line 746 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3394 "src/parser.tab.c"
    break;

  case 22: /* statement: return_statement NEWLINE  */
#line 747 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3400 "src/parser.tab.c"
    break;

  case 23: /* statement: label_statement NEWLINE  */
#line 748 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3406 "src/parser.tab.c"
    break;

  case 24: /* statement: goto_statement NEWLINE  */
#line 749 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3412 "src/parser.tab.c"
    break;

  case 25: /* statement: gosub_statement NEWLINE  */
#line 750 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3418 "src/parser.tab.c"
    break;

  case 26: /* statement: break_statement NEWLINE  */
#line 751 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3424 "src/parser.tab.c"
    break;

  case 27: /* statement: continue_statement NEWLINE  */
#line 752 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3430 "src/parser.tab.c"
    break;

  case 28: /* statement: if_statement  */
#line 753 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3436 "src/parser.tab.c"
    break;

  case 29: /* assignment: lvalue OP_EQ expression  */
#line 757 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 3442 "src/parser.tab.c"
    break;

  case 30: /* assignment: lvalue modifier OP_EQ expression  */
#line 758 "src/parser.y"
                                       {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 3456 "src/parser.tab.c"
    break;

  case 31: /* lvalue: variable_name  */
#line 770 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3462 "src/parser.tab.c"
    break;

  case 32: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 771 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3468 "src/parser.tab.c"
    break;

  case 33: /* lvalue: lvalue DOT IDENT  */
#line 772 "src/parser.y"
                                    { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3474 "src/parser.tab.c"
    break;

  case 34: /* variable_name: IDENT  */
#line 776 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 3480 "src/parser.tab.c"
    break;

  case 35: /* variable_name: END  */
#line 777 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 3486 "src/parser.tab.c"
    break;

  case 36: /* variable_name: NEXT  */
#line 778 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 3492 "src/parser.tab.c"
    break;

  case 37: /* modifier: MOD_LPAREN MOD_CONTENT  */
#line 782 "src/parser.y"
                             { (yyval.modifier) = parse_modifier_use((yyvsp[0].text)); }
#line 3498 "src/parser.tab.c"
    break;

  case 38: /* $@1: %empty  */
#line 786 "src/parser.y"
             { lexer_begin_lens_content(ctx->active_lexer); }
#line 3504 "src/parser.tab.c"
    break;

  case 39: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 786 "src/parser.y"
                                                                                  {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 3512 "src/parser.tab.c"
    break;

  case 40: /* modifier_name: modifier_word  */
#line 792 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 3518 "src/parser.tab.c"
    break;

  case 41: /* modifier_name: modifier_name modifier_word  */
#line 793 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 3524 "src/parser.tab.c"
    break;

  case 42: /* modifier_word: IDENT  */
#line 797 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 3530 "src/parser.tab.c"
    break;

  case 43: /* modifier_word: TO  */
#line 798 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 3536 "src/parser.tab.c"
    break;

  case 44: /* modifier_word: END  */
#line 799 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 3542 "src/parser.tab.c"
    break;

  case 45: /* modifier_word: NEXT  */
#line 800 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 3548 "src/parser.tab.c"
    break;

  case 46: /* print_statement: PRINT expression  */
#line 804 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 3554 "src/parser.tab.c"
    break;

  case 47: /* print_statement: PRINT TO ERROR_VALUE expression  */
#line 810 "src/parser.y"
                                      { (yyval.stmt) = ast_print_error((yyvsp[0].expr)); }
#line 3560 "src/parser.tab.c"
    break;

  case 48: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 814 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 3566 "src/parser.tab.c"
    break;

  case 49: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 815 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 3577 "src/parser.tab.c"
    break;

  case 50: /* call_statement: lvalue DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 821 "src/parser.y"
                                                       {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 3587 "src/parser.tab.c"
    break;

  case 51: /* call_statement: lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 826 "src/parser.y"
                                                                 {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 3601 "src/parser.tab.c"
    break;

  case 52: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 835 "src/parser.y"
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
#line 3616 "src/parser.tab.c"
    break;

  case 53: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 848 "src/parser.y"
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
#line 3633 "src/parser.tab.c"
    break;

  case 54: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list END FOR NEWLINE  */
#line 863 "src/parser.y"
                                                                     {
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 3641 "src/parser.tab.c"
    break;

  case 55: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list END FOR NEWLINE  */
#line 866 "src/parser.y"
                                                                          {
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 3649 "src/parser.tab.c"
    break;

  case 56: /* for_each_statement: FOR IDENT OP_EQ expression TO expression NEWLINE statement_list END FOR NEWLINE  */
#line 872 "src/parser.y"
                                                                                      {
        (yyval.stmt) = ast_for_range((yyvsp[-9].text), (yyvsp[-7].expr), (yyvsp[-5].expr), NULL, (yyvsp[-3].stmt_list));
      }
#line 3657 "src/parser.tab.c"
    break;

  case 57: /* for_each_statement: FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list END FOR NEWLINE  */
#line 875 "src/parser.y"
                                                                                                      {
        (yyval.stmt) = ast_for_range((yyvsp[-11].text), (yyvsp[-9].expr), (yyvsp[-7].expr), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 3665 "src/parser.tab.c"
    break;

  case 58: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 881 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 3673 "src/parser.tab.c"
    break;

  case 59: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 887 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 3681 "src/parser.tab.c"
    break;

  case 60: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 893 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 3689 "src/parser.tab.c"
    break;

  case 61: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 896 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 3697 "src/parser.tab.c"
    break;

  case 62: /* consider_else_opt: %empty  */
#line 902 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3703 "src/parser.tab.c"
    break;

  case 63: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 903 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3709 "src/parser.tab.c"
    break;

  case 64: /* consider_statement_list: %empty  */
#line 907 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3715 "src/parser.tab.c"
    break;

  case 65: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 908 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3721 "src/parser.tab.c"
    break;

  case 66: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 909 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3727 "src/parser.tab.c"
    break;

  case 67: /* consider_body_statement: assignment NEWLINE  */
#line 913 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3733 "src/parser.tab.c"
    break;

  case 68: /* consider_body_statement: print_statement NEWLINE  */
#line 914 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3739 "src/parser.tab.c"
    break;

  case 69: /* consider_body_statement: call_statement NEWLINE  */
#line 915 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3745 "src/parser.tab.c"
    break;

  case 70: /* consider_body_statement: with_lock_statement  */
#line 916 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3751 "src/parser.tab.c"
    break;

  case 71: /* consider_body_statement: for_each_statement  */
#line 917 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3757 "src/parser.tab.c"
    break;

  case 72: /* consider_body_statement: while_statement  */
#line 918 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3763 "src/parser.tab.c"
    break;

  case 73: /* consider_body_statement: consider_statement  */
#line 919 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3769 "src/parser.tab.c"
    break;

  case 74: /* consider_body_statement: function_statement  */
#line 920 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3775 "src/parser.tab.c"
    break;

  case 75: /* consider_body_statement: modifier_statement  */
#line 921 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3781 "src/parser.tab.c"
    break;

  case 76: /* consider_body_statement: program_statement  */
#line 922 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3787 "src/parser.tab.c"
    break;

  case 77: /* consider_body_statement: library_statement  */
#line 923 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3793 "src/parser.tab.c"
    break;

  case 78: /* consider_body_statement: use_statement NEWLINE  */
#line 924 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3799 "src/parser.tab.c"
    break;

  case 79: /* consider_body_statement: watch_statement  */
#line 925 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3805 "src/parser.tab.c"
    break;

  case 80: /* consider_body_statement: without_watchers_statement  */
#line 926 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3811 "src/parser.tab.c"
    break;

  case 81: /* consider_body_statement: on_error_statement NEWLINE  */
#line 927 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3817 "src/parser.tab.c"
    break;

  case 82: /* consider_body_statement: error_statement NEWLINE  */
#line 928 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3823 "src/parser.tab.c"
    break;

  case 83: /* consider_body_statement: return_statement NEWLINE  */
#line 929 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3829 "src/parser.tab.c"
    break;

  case 84: /* consider_body_statement: label_statement NEWLINE  */
#line 930 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3835 "src/parser.tab.c"
    break;

  case 85: /* consider_body_statement: goto_statement NEWLINE  */
#line 931 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3841 "src/parser.tab.c"
    break;

  case 86: /* consider_body_statement: gosub_statement NEWLINE  */
#line 932 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3847 "src/parser.tab.c"
    break;

  case 87: /* consider_body_statement: break_statement NEWLINE  */
#line 933 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3853 "src/parser.tab.c"
    break;

  case 88: /* consider_body_statement: continue_statement NEWLINE  */
#line 934 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3859 "src/parser.tab.c"
    break;

  case 89: /* consider_body_statement: if_statement  */
#line 935 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3865 "src/parser.tab.c"
    break;

  case 90: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 939 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 3873 "src/parser.tab.c"
    break;

  case 91: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 942 "src/parser.y"
                                                                                                            {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 3882 "src/parser.tab.c"
    break;

  case 92: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 949 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 3890 "src/parser.tab.c"
    break;

  case 93: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 952 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 3898 "src/parser.tab.c"
    break;

  case 94: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 958 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 3906 "src/parser.tab.c"
    break;

  case 95: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 964 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 3914 "src/parser.tab.c"
    break;

  case 96: /* use_statement: USE IDENT  */
#line 970 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 3920 "src/parser.tab.c"
    break;

  case 97: /* use_statement: LOAD IDENT  */
#line 971 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 3926 "src/parser.tab.c"
    break;

  case 98: /* use_statement: USE STRING  */
#line 972 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 3932 "src/parser.tab.c"
    break;

  case 99: /* use_statement: LOAD STRING  */
#line 973 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 3938 "src/parser.tab.c"
    break;

  case 100: /* use_statement: USE IDENT IDENT STRING  */
#line 974 "src/parser.y"
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
#line 3959 "src/parser.tab.c"
    break;

  case 101: /* use_statement: LOAD IDENT IDENT STRING  */
#line 990 "src/parser.y"
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
#line 3980 "src/parser.tab.c"
    break;

  case 102: /* modifier_signature: modifier_name  */
#line 1009 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 3986 "src/parser.tab.c"
    break;

  case 103: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 1010 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 3992 "src/parser.tab.c"
    break;

  case 104: /* modifier_context: IDENT  */
#line 1014 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 3998 "src/parser.tab.c"
    break;

  case 105: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1018 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch((yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4006 "src/parser.tab.c"
    break;

  case 106: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 1021 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch((yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 4014 "src/parser.tab.c"
    break;

  case 107: /* watch_target_list: watch_target_path  */
#line 1027 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4020 "src/parser.tab.c"
    break;

  case 108: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 1028 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4026 "src/parser.tab.c"
    break;

  case 109: /* watch_target_path: variable_name  */
#line 1032 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 4032 "src/parser.tab.c"
    break;

  case 110: /* watch_target_path: watch_target_path DOT IDENT  */
#line 1033 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 4038 "src/parser.tab.c"
    break;

  case 111: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 1037 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 4046 "src/parser.tab.c"
    break;

  case 112: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 1043 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 4052 "src/parser.tab.c"
    break;

  case 113: /* on_error_statement: ON ERROR_VALUE RESUME NEXT  */
#line 1044 "src/parser.y"
                                 { (yyval.stmt) = ast_on_error_resume_next(); }
#line 4058 "src/parser.tab.c"
    break;

  case 114: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 1045 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 4064 "src/parser.tab.c"
    break;

  case 115: /* error_statement: ERROR_VALUE expression  */
#line 1049 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 4070 "src/parser.tab.c"
    break;

  case 116: /* return_statement: RETURN  */
#line 1053 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 4076 "src/parser.tab.c"
    break;

  case 117: /* return_statement: RETURN expression  */
#line 1054 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 4082 "src/parser.tab.c"
    break;

  case 118: /* label_statement: variable_name COLON  */
#line 1058 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 4088 "src/parser.tab.c"
    break;

  case 119: /* goto_statement: GOTO IDENT  */
#line 1062 "src/parser.y"
                 { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 4094 "src/parser.tab.c"
    break;

  case 120: /* gosub_statement: GOSUB IDENT  */
#line 1066 "src/parser.y"
                  { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 4100 "src/parser.tab.c"
    break;

  case 121: /* break_statement: BREAK  */
#line 1070 "src/parser.y"
            { (yyval.stmt) = ast_break(); }
#line 4106 "src/parser.tab.c"
    break;

  case 122: /* continue_statement: CONTINUE  */
#line 1074 "src/parser.y"
               { (yyval.stmt) = ast_continue(); }
#line 4112 "src/parser.tab.c"
    break;

  case 123: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1078 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4121 "src/parser.tab.c"
    break;

  case 124: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1082 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4130 "src/parser.tab.c"
    break;

  case 125: /* if_block_tail: END IF NEWLINE  */
#line 1089 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4138 "src/parser.tab.c"
    break;

  case 126: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 1092 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4146 "src/parser.tab.c"
    break;

  case 127: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1095 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4154 "src/parser.tab.c"
    break;

  case 128: /* if_inline_tail: %empty  */
#line 1101 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4162 "src/parser.tab.c"
    break;

  case 129: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 1104 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4170 "src/parser.tab.c"
    break;

  case 130: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1107 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4178 "src/parser.tab.c"
    break;

  case 131: /* inline_statement: assignment  */
#line 1113 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4184 "src/parser.tab.c"
    break;

  case 132: /* inline_statement: print_statement  */
#line 1114 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4190 "src/parser.tab.c"
    break;

  case 133: /* inline_statement: call_statement  */
#line 1115 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4196 "src/parser.tab.c"
    break;

  case 134: /* inline_statement: use_statement  */
#line 1116 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4202 "src/parser.tab.c"
    break;

  case 135: /* inline_statement: on_error_statement  */
#line 1117 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4208 "src/parser.tab.c"
    break;

  case 136: /* inline_statement: error_statement  */
#line 1118 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4214 "src/parser.tab.c"
    break;

  case 137: /* inline_statement: return_statement  */
#line 1119 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4220 "src/parser.tab.c"
    break;

  case 138: /* inline_statement: goto_statement  */
#line 1120 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4226 "src/parser.tab.c"
    break;

  case 139: /* inline_statement: gosub_statement  */
#line 1121 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4232 "src/parser.tab.c"
    break;

  case 140: /* inline_statement: break_statement  */
#line 1122 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4238 "src/parser.tab.c"
    break;

  case 141: /* inline_statement: continue_statement  */
#line 1123 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4244 "src/parser.tab.c"
    break;

  case 142: /* expression: or_expression  */
#line 1127 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 4250 "src/parser.tab.c"
    break;

  case 143: /* or_expression: and_expression  */
#line 1131 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 4256 "src/parser.tab.c"
    break;

  case 144: /* or_expression: or_expression OR and_expression  */
#line 1132 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4262 "src/parser.tab.c"
    break;

  case 145: /* and_expression: comparison_expression  */
#line 1136 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 4268 "src/parser.tab.c"
    break;

  case 146: /* and_expression: and_expression AND comparison_expression  */
#line 1137 "src/parser.y"
                                               { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4274 "src/parser.tab.c"
    break;

  case 147: /* comparison_expression: additive_expression  */
#line 1141 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 4280 "src/parser.tab.c"
    break;

  case 148: /* comparison_expression: additive_expression comparison_operator additive_expression  */
#line 1142 "src/parser.y"
                                                                  { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4286 "src/parser.tab.c"
    break;

  case 149: /* comparison_expression: additive_expression comparison_lens comparison_operator additive_expression  */
#line 1143 "src/parser.y"
                                                                                  {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 4294 "src/parser.tab.c"
    break;

  case 150: /* comparison_expression: additive_expression modifier comparison_operator additive_expression  */
#line 1146 "src/parser.y"
                                                                           {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 4308 "src/parser.tab.c"
    break;

  case 151: /* additive_expression: multiplicative_expression  */
#line 1158 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 4314 "src/parser.tab.c"
    break;

  case 152: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 1159 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4320 "src/parser.tab.c"
    break;

  case 153: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 1160 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4326 "src/parser.tab.c"
    break;

  case 154: /* multiplicative_expression: unary_expression  */
#line 1164 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 4332 "src/parser.tab.c"
    break;

  case 155: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 1165 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4338 "src/parser.tab.c"
    break;

  case 156: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 1166 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4344 "src/parser.tab.c"
    break;

  case 157: /* unary_expression: postfix_expression  */
#line 1170 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 4350 "src/parser.tab.c"
    break;

  case 158: /* unary_expression: NOT unary_expression  */
#line 1171 "src/parser.y"
                           { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4356 "src/parser.tab.c"
    break;

  case 159: /* unary_expression: MINUS unary_expression  */
#line 1172 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4362 "src/parser.tab.c"
    break;

  case 160: /* unary_expression: NEW postfix_expression  */
#line 1173 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4368 "src/parser.tab.c"
    break;

  case 161: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 1174 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 4374 "src/parser.tab.c"
    break;

  case 162: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 1175 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4380 "src/parser.tab.c"
    break;

  case 163: /* postfix_expression: primary  */
#line 1179 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 4386 "src/parser.tab.c"
    break;

  case 164: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 1180 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4392 "src/parser.tab.c"
    break;

  case 165: /* postfix_expression: postfix_expression DOT IDENT  */
#line 1181 "src/parser.y"
                                   { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4398 "src/parser.tab.c"
    break;

  case 166: /* postfix_expression: postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1182 "src/parser.y"
                                                                   {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        (yyval.expr) = expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 4408 "src/parser.tab.c"
    break;

  case 167: /* postfix_expression: postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1187 "src/parser.y"
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
#line 4424 "src/parser.tab.c"
    break;

  case 168: /* comparison_operator: OP_EQ  */
#line 1201 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 4430 "src/parser.tab.c"
    break;

  case 169: /* comparison_operator: OP_NE  */
#line 1202 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 4436 "src/parser.tab.c"
    break;

  case 170: /* comparison_operator: OP_GT  */
#line 1203 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 4442 "src/parser.tab.c"
    break;

  case 171: /* comparison_operator: OP_LT  */
#line 1204 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 4448 "src/parser.tab.c"
    break;

  case 172: /* comparison_operator: OP_GE  */
#line 1205 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 4454 "src/parser.tab.c"
    break;

  case 173: /* comparison_operator: OP_LE  */
#line 1206 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 4460 "src/parser.tab.c"
    break;

  case 174: /* comparison_operator: OP_NGT  */
#line 1207 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 4466 "src/parser.tab.c"
    break;

  case 175: /* comparison_operator: OP_NLT  */
#line 1208 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 4472 "src/parser.tab.c"
    break;

  case 176: /* comparison_operator: OP_NGE  */
#line 1209 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 4478 "src/parser.tab.c"
    break;

  case 177: /* comparison_operator: OP_NLE  */
#line 1210 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 4484 "src/parser.tab.c"
    break;

  case 178: /* primary: NUMBER  */
#line 1214 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4490 "src/parser.tab.c"
    break;

  case 179: /* primary: duration_terms  */
#line 1215 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4496 "src/parser.tab.c"
    break;

  case 180: /* primary: STRING  */
#line 1216 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4502 "src/parser.tab.c"
    break;

  case 181: /* primary: variable_name ident_suffix  */
#line 1217 "src/parser.y"
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
#line 4524 "src/parser.tab.c"
    break;

  case 182: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1234 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 4535 "src/parser.tab.c"
    break;

  case 183: /* primary: ERROR_VALUE  */
#line 1240 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4541 "src/parser.tab.c"
    break;

  case 184: /* primary: TRUE  */
#line 1241 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4547 "src/parser.tab.c"
    break;

  case 185: /* primary: FALSE  */
#line 1242 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4553 "src/parser.tab.c"
    break;

  case 186: /* primary: NOTHING  */
#line 1243 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4559 "src/parser.tab.c"
    break;

  case 187: /* primary: UNKNOWN_VALUE  */
#line 1244 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4565 "src/parser.tab.c"
    break;

  case 188: /* primary: LPAREN expression RPAREN  */
#line 1245 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 4571 "src/parser.tab.c"
    break;

  case 189: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 1246 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4577 "src/parser.tab.c"
    break;

  case 190: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 1247 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4583 "src/parser.tab.c"
    break;

  case 191: /* primary: record_literal  */
#line 1248 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 4589 "src/parser.tab.c"
    break;

  case 192: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 1252 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4595 "src/parser.tab.c"
    break;

  case 193: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 1253 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4601 "src/parser.tab.c"
    break;

  case 194: /* ident_suffix: %empty  */
#line 1257 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 4611 "src/parser.tab.c"
    break;

  case 195: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 1262 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 4621 "src/parser.tab.c"
    break;

  case 196: /* ident_suffix: DOT IDENT ident_dot_suffix  */
#line 1267 "src/parser.y"
                                 {
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 4630 "src/parser.tab.c"
    break;

  case 197: /* ident_suffix: DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1271 "src/parser.y"
                                                          {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        (yyval.ident_suffix).kind = IDENT_SUFFIX_METHOD;
        (yyval.ident_suffix).name = (yyvsp[-3].text);
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 4644 "src/parser.tab.c"
    break;

  case 198: /* ident_dot_suffix: %empty  */
#line 1283 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 4654 "src/parser.tab.c"
    break;

  case 199: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 1288 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 4664 "src/parser.tab.c"
    break;

  case 200: /* duration_terms: NUMBER IDENT  */
#line 1296 "src/parser.y"
                   {
        AstDuration duration = {0};
        (yyval.duration) = duration_add_unit(duration, (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 4673 "src/parser.tab.c"
    break;

  case 201: /* duration_terms: duration_terms NUMBER IDENT  */
#line 1300 "src/parser.y"
                                  {
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 4681 "src/parser.tab.c"
    break;

  case 202: /* argument_list_opt: %empty  */
#line 1306 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 4687 "src/parser.tab.c"
    break;

  case 203: /* argument_list_opt: argument_list  */
#line 1307 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 4693 "src/parser.tab.c"
    break;

  case 204: /* argument_list: expression  */
#line 1311 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 4699 "src/parser.tab.c"
    break;

  case 205: /* argument_list: argument_list COMMA expression  */
#line 1312 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 4705 "src/parser.tab.c"
    break;

  case 206: /* array_argument_list: expression  */
#line 1316 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 4711 "src/parser.tab.c"
    break;

  case 207: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 1317 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 4717 "src/parser.tab.c"
    break;

  case 208: /* parameter_list_opt: %empty  */
#line 1321 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 4723 "src/parser.tab.c"
    break;

  case 209: /* parameter_list_opt: parameter_list  */
#line 1322 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 4729 "src/parser.tab.c"
    break;

  case 210: /* parameter_list: IDENT  */
#line 1326 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4735 "src/parser.tab.c"
    break;

  case 211: /* parameter_list: parameter_list COMMA IDENT  */
#line 1327 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4741 "src/parser.tab.c"
    break;

  case 212: /* field_name: IDENT  */
#line 1340 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4747 "src/parser.tab.c"
    break;

  case 213: /* field_name: AS  */
#line 1341 "src/parser.y"
                     { (yyval.text) = kw_name("as"); }
#line 4753 "src/parser.tab.c"
    break;

  case 214: /* field_name: NEXT  */
#line 1342 "src/parser.y"
                     { (yyval.text) = kw_name("next"); }
#line 4759 "src/parser.tab.c"
    break;

  case 215: /* field_name: STOP  */
#line 1343 "src/parser.y"
                     { (yyval.text) = kw_name("stop"); }
#line 4765 "src/parser.tab.c"
    break;

  case 216: /* field_name: ERROR_VALUE  */
#line 1344 "src/parser.y"
                     { (yyval.text) = kw_name("error"); }
#line 4771 "src/parser.tab.c"
    break;

  case 217: /* field_name: END  */
#line 1345 "src/parser.y"
                     { (yyval.text) = kw_name("end"); }
#line 4777 "src/parser.tab.c"
    break;

  case 218: /* field_name: TO  */
#line 1346 "src/parser.y"
                     { (yyval.text) = kw_name("to"); }
#line 4783 "src/parser.tab.c"
    break;

  case 219: /* field_name: IN  */
#line 1347 "src/parser.y"
                     { (yyval.text) = kw_name("in"); }
#line 4789 "src/parser.tab.c"
    break;

  case 220: /* field_name: ON  */
#line 1348 "src/parser.y"
                     { (yyval.text) = kw_name("on"); }
#line 4795 "src/parser.tab.c"
    break;

  case 221: /* field_name: NEW  */
#line 1349 "src/parser.y"
                     { (yyval.text) = kw_name("new"); }
#line 4801 "src/parser.tab.c"
    break;

  case 222: /* field_name: EACH  */
#line 1350 "src/parser.y"
                     { (yyval.text) = kw_name("each"); }
#line 4807 "src/parser.tab.c"
    break;

  case 223: /* field_name: WITH  */
#line 1351 "src/parser.y"
                     { (yyval.text) = kw_name("with"); }
#line 4813 "src/parser.tab.c"
    break;

  case 224: /* field_name: WITHOUT  */
#line 1352 "src/parser.y"
                     { (yyval.text) = kw_name("without"); }
#line 4819 "src/parser.tab.c"
    break;

  case 225: /* field_name: THEN  */
#line 1353 "src/parser.y"
                     { (yyval.text) = kw_name("then"); }
#line 4825 "src/parser.tab.c"
    break;

  case 226: /* field_name: ELSE  */
#line 1354 "src/parser.y"
                     { (yyval.text) = kw_name("else"); }
#line 4831 "src/parser.tab.c"
    break;

  case 227: /* field_name: FOR  */
#line 1355 "src/parser.y"
                     { (yyval.text) = kw_name("for"); }
#line 4837 "src/parser.tab.c"
    break;

  case 228: /* field_name: IF  */
#line 1356 "src/parser.y"
                     { (yyval.text) = kw_name("if"); }
#line 4843 "src/parser.tab.c"
    break;

  case 229: /* field_name: WHILE  */
#line 1357 "src/parser.y"
                     { (yyval.text) = kw_name("while"); }
#line 4849 "src/parser.tab.c"
    break;

  case 230: /* field_name: PRINT  */
#line 1358 "src/parser.y"
                     { (yyval.text) = kw_name("print"); }
#line 4855 "src/parser.tab.c"
    break;

  case 231: /* field_name: RETURN  */
#line 1359 "src/parser.y"
                     { (yyval.text) = kw_name("return"); }
#line 4861 "src/parser.tab.c"
    break;

  case 232: /* field_name: LOAD  */
#line 1360 "src/parser.y"
                     { (yyval.text) = kw_name("load"); }
#line 4867 "src/parser.tab.c"
    break;

  case 233: /* field_name: USE  */
#line 1361 "src/parser.y"
                     { (yyval.text) = kw_name("use"); }
#line 4873 "src/parser.tab.c"
    break;

  case 234: /* field_name: NOT  */
#line 1362 "src/parser.y"
                     { (yyval.text) = kw_name("not"); }
#line 4879 "src/parser.tab.c"
    break;

  case 235: /* field_name: AND  */
#line 1363 "src/parser.y"
                     { (yyval.text) = kw_name("and"); }
#line 4885 "src/parser.tab.c"
    break;

  case 236: /* field_name: OR  */
#line 1364 "src/parser.y"
                     { (yyval.text) = kw_name("or"); }
#line 4891 "src/parser.tab.c"
    break;

  case 237: /* field_name: TRUE  */
#line 1365 "src/parser.y"
                     { (yyval.text) = kw_name("true"); }
#line 4897 "src/parser.tab.c"
    break;

  case 238: /* field_name: FALSE  */
#line 1366 "src/parser.y"
                     { (yyval.text) = kw_name("false"); }
#line 4903 "src/parser.tab.c"
    break;

  case 239: /* field_name: NOTHING  */
#line 1367 "src/parser.y"
                     { (yyval.text) = kw_name("nothing"); }
#line 4909 "src/parser.tab.c"
    break;

  case 240: /* field_name: BREAK  */
#line 1368 "src/parser.y"
                     { (yyval.text) = kw_name("break"); }
#line 4915 "src/parser.tab.c"
    break;

  case 241: /* field_name: CONTINUE  */
#line 1369 "src/parser.y"
                     { (yyval.text) = kw_name("continue"); }
#line 4921 "src/parser.tab.c"
    break;

  case 242: /* field_name: GOTO  */
#line 1370 "src/parser.y"
                     { (yyval.text) = kw_name("goto"); }
#line 4927 "src/parser.tab.c"
    break;

  case 243: /* field_name: GOSUB  */
#line 1371 "src/parser.y"
                     { (yyval.text) = kw_name("gosub"); }
#line 4933 "src/parser.tab.c"
    break;

  case 244: /* field_name: SPAWN  */
#line 1372 "src/parser.y"
                     { (yyval.text) = kw_name("spawn"); }
#line 4939 "src/parser.tab.c"
    break;

  case 245: /* field_name: EXPORT  */
#line 1373 "src/parser.y"
                     { (yyval.text) = kw_name("export"); }
#line 4945 "src/parser.tab.c"
    break;

  case 246: /* field_name: LIBRARY  */
#line 1374 "src/parser.y"
                     { (yyval.text) = kw_name("library"); }
#line 4951 "src/parser.tab.c"
    break;

  case 247: /* field_name: FUNCTION  */
#line 1375 "src/parser.y"
                     { (yyval.text) = kw_name("function"); }
#line 4957 "src/parser.tab.c"
    break;

  case 248: /* field_name: MODIFIER  */
#line 1376 "src/parser.y"
                     { (yyval.text) = kw_name("modifier"); }
#line 4963 "src/parser.tab.c"
    break;

  case 249: /* field_name: PROGRAM  */
#line 1377 "src/parser.y"
                     { (yyval.text) = kw_name("program"); }
#line 4969 "src/parser.tab.c"
    break;

  case 250: /* field_name: RESUME  */
#line 1378 "src/parser.y"
                     { (yyval.text) = kw_name("resume"); }
#line 4975 "src/parser.tab.c"
    break;

  case 251: /* field_name: WATCH  */
#line 1379 "src/parser.y"
                     { (yyval.text) = kw_name("watch"); }
#line 4981 "src/parser.tab.c"
    break;

  case 252: /* field_name: WATCHERS  */
#line 1380 "src/parser.y"
                     { (yyval.text) = kw_name("watchers"); }
#line 4987 "src/parser.tab.c"
    break;

  case 253: /* field_name: CONSIDER  */
#line 1381 "src/parser.y"
                     { (yyval.text) = kw_name("consider"); }
#line 4993 "src/parser.tab.c"
    break;

  case 254: /* record_field_list: field_name OP_EQ expression  */
#line 1385 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 4999 "src/parser.tab.c"
    break;

  case 255: /* record_field_list: field_name COLON expression  */
#line 1386 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5005 "src/parser.tab.c"
    break;

  case 256: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1387 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5011 "src/parser.tab.c"
    break;

  case 257: /* record_field_list: record_field_list COMMA optional_newlines field_name OP_EQ expression  */
#line 1388 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5017 "src/parser.tab.c"
    break;

  case 258: /* record_field_list: record_field_list COMMA optional_newlines field_name COLON expression  */
#line 1389 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5023 "src/parser.tab.c"
    break;

  case 259: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1390 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5029 "src/parser.tab.c"
    break;

  case 260: /* field_policy: IDENT  */
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
#line 5061 "src/parser.tab.c"
    break;

  case 261: /* field_policy: IDENT expression  */
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
#line 5082 "src/parser.tab.c"
    break;


#line 5086 "src/parser.tab.c"

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
    case TOKEN_STEP: return STEP;
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
