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
  YYSYMBOL_DO = 31,                        /* DO  */
  YYSYMBOL_LOOP = 32,                      /* LOOP  */
  YYSYMBOL_UNTIL = 33,                     /* UNTIL  */
  YYSYMBOL_IN = 34,                        /* IN  */
  YYSYMBOL_EACH = 35,                      /* EACH  */
  YYSYMBOL_WHILE = 36,                     /* WHILE  */
  YYSYMBOL_CONSIDER = 37,                  /* CONSIDER  */
  YYSYMBOL_BREAK = 38,                     /* BREAK  */
  YYSYMBOL_CONTINUE = 39,                  /* CONTINUE  */
  YYSYMBOL_FUNCTION = 40,                  /* FUNCTION  */
  YYSYMBOL_RETURN = 41,                    /* RETURN  */
  YYSYMBOL_GOTO = 42,                      /* GOTO  */
  YYSYMBOL_GOSUB = 43,                     /* GOSUB  */
  YYSYMBOL_WATCH = 44,                     /* WATCH  */
  YYSYMBOL_UNWATCH = 45,                   /* UNWATCH  */
  YYSYMBOL_WITHOUT = 46,                   /* WITHOUT  */
  YYSYMBOL_WATCHERS = 47,                  /* WATCHERS  */
  YYSYMBOL_ON = 48,                        /* ON  */
  YYSYMBOL_RESUME = 49,                    /* RESUME  */
  YYSYMBOL_NEXT = 50,                      /* NEXT  */
  YYSYMBOL_STOP = 51,                      /* STOP  */
  YYSYMBOL_ERROR_VALUE = 52,               /* ERROR_VALUE  */
  YYSYMBOL_MODIFIER = 53,                  /* MODIFIER  */
  YYSYMBOL_PROGRAM = 54,                   /* PROGRAM  */
  YYSYMBOL_LIBRARY = 55,                   /* LIBRARY  */
  YYSYMBOL_LOAD = 56,                      /* LOAD  */
  YYSYMBOL_USE = 57,                       /* USE  */
  YYSYMBOL_EXPORT = 58,                    /* EXPORT  */
  YYSYMBOL_OP_EQ = 59,                     /* OP_EQ  */
  YYSYMBOL_OP_NE = 60,                     /* OP_NE  */
  YYSYMBOL_OP_GT = 61,                     /* OP_GT  */
  YYSYMBOL_OP_LT = 62,                     /* OP_LT  */
  YYSYMBOL_OP_GE = 63,                     /* OP_GE  */
  YYSYMBOL_OP_LE = 64,                     /* OP_LE  */
  YYSYMBOL_OP_NGT = 65,                    /* OP_NGT  */
  YYSYMBOL_OP_NLT = 66,                    /* OP_NLT  */
  YYSYMBOL_OP_NGE = 67,                    /* OP_NGE  */
  YYSYMBOL_OP_NLE = 68,                    /* OP_NLE  */
  YYSYMBOL_PLUS = 69,                      /* PLUS  */
  YYSYMBOL_MINUS = 70,                     /* MINUS  */
  YYSYMBOL_STAR = 71,                      /* STAR  */
  YYSYMBOL_SLASH = 72,                     /* SLASH  */
  YYSYMBOL_LPAREN = 73,                    /* LPAREN  */
  YYSYMBOL_MOD_LPAREN = 74,                /* MOD_LPAREN  */
  YYSYMBOL_RPAREN = 75,                    /* RPAREN  */
  YYSYMBOL_LBRACKET = 76,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 77,                  /* RBRACKET  */
  YYSYMBOL_LBRACE = 78,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 79,                    /* RBRACE  */
  YYSYMBOL_COMMA = 80,                     /* COMMA  */
  YYSYMBOL_COLON = 81,                     /* COLON  */
  YYSYMBOL_NEWLINE = 82,                   /* NEWLINE  */
  YYSYMBOL_IF_WITHOUT_ELSE = 83,           /* IF_WITHOUT_ELSE  */
  YYSYMBOL_NO_DOT = 84,                    /* NO_DOT  */
  YYSYMBOL_DOT = 85,                       /* DOT  */
  YYSYMBOL_YYACCEPT = 86,                  /* $accept  */
  YYSYMBOL_program = 87,                   /* program  */
  YYSYMBOL_statement_list = 88,            /* statement_list  */
  YYSYMBOL_statement = 89,                 /* statement  */
  YYSYMBOL_assignment = 90,                /* assignment  */
  YYSYMBOL_lvalue = 91,                    /* lvalue  */
  YYSYMBOL_variable_name = 92,             /* variable_name  */
  YYSYMBOL_modifier = 93,                  /* modifier  */
  YYSYMBOL_comparison_lens = 94,           /* comparison_lens  */
  YYSYMBOL_95_1 = 95,                      /* $@1  */
  YYSYMBOL_modifier_name = 96,             /* modifier_name  */
  YYSYMBOL_modifier_word = 97,             /* modifier_word  */
  YYSYMBOL_print_statement = 98,           /* print_statement  */
  YYSYMBOL_call_statement = 99,            /* call_statement  */
  YYSYMBOL_with_lock_statement = 100,      /* with_lock_statement  */
  YYSYMBOL_for_each_statement = 101,       /* for_each_statement  */
  YYSYMBOL_do_loop_statement = 102,        /* do_loop_statement  */
  YYSYMBOL_while_statement = 103,          /* while_statement  */
  YYSYMBOL_consider_statement = 104,       /* consider_statement  */
  YYSYMBOL_consider_branch_list = 105,     /* consider_branch_list  */
  YYSYMBOL_consider_else_opt = 106,        /* consider_else_opt  */
  YYSYMBOL_consider_statement_list = 107,  /* consider_statement_list  */
  YYSYMBOL_consider_body_statement = 108,  /* consider_body_statement  */
  YYSYMBOL_function_statement = 109,       /* function_statement  */
  YYSYMBOL_modifier_statement = 110,       /* modifier_statement  */
  YYSYMBOL_program_statement = 111,        /* program_statement  */
  YYSYMBOL_library_statement = 112,        /* library_statement  */
  YYSYMBOL_use_statement = 113,            /* use_statement  */
  YYSYMBOL_modifier_signature = 114,       /* modifier_signature  */
  YYSYMBOL_modifier_context = 115,         /* modifier_context  */
  YYSYMBOL_watch_statement = 116,          /* watch_statement  */
  YYSYMBOL_unwatch_statement = 117,        /* unwatch_statement  */
  YYSYMBOL_watch_target_list = 118,        /* watch_target_list  */
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
  YYSYMBOL_record_field_list = 153,        /* record_field_list  */
  YYSYMBOL_field_policy = 154,             /* field_policy  */
  YYSYMBOL_optional_newlines = 155         /* optional_newlines  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;



/* Unqualified %code blocks.  */
#line 663 "src/parser.y"

static int yylex(YYSTYPE *lvalp, YYLTYPE *llocp, gb_parse_ctx *ctx);
static void yyerror(YYLTYPE *llocp, gb_parse_ctx *ctx, const char *message);
static void report_syntax_error(gb_parse_ctx *ctx, int line, int column,
                                int end_line, int end_column, const char *message);

#line 866 "src/parser.tab.c"

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
#define YYLAST   2196

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  86
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  70
/* YYNRULES -- Number of rules.  */
#define YYNRULES  278
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  582

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   340


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
      85
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   721,   721,   725,   726,   727,   731,   732,   733,   734,
     735,   736,   737,   738,   739,   740,   741,   742,   743,   744,
     745,   746,   747,   748,   749,   750,   751,   752,   753,   754,
     755,   759,   760,   772,   773,   774,   778,   779,   780,   785,
     786,   790,   794,   794,   800,   801,   805,   806,   807,   808,
     812,   818,   822,   823,   829,   834,   843,   856,   871,   874,
     880,   883,   891,   894,   900,   906,   912,   915,   921,   922,
     926,   927,   928,   932,   933,   934,   935,   936,   937,   938,
     939,   940,   941,   942,   943,   944,   945,   946,   947,   948,
     949,   950,   951,   952,   953,   954,   955,   956,   960,   963,
     970,   973,   979,   985,   991,   992,   993,   994,   995,  1011,
    1030,  1031,  1035,  1039,  1042,  1050,  1056,  1060,  1061,  1065,
    1066,  1070,  1076,  1077,  1078,  1082,  1086,  1087,  1091,  1098,
    1102,  1106,  1110,  1114,  1118,  1125,  1128,  1131,  1137,  1140,
    1143,  1149,  1150,  1151,  1152,  1153,  1154,  1155,  1156,  1157,
    1158,  1159,  1163,  1167,  1168,  1172,  1173,  1177,  1178,  1179,
    1182,  1194,  1195,  1196,  1200,  1201,  1202,  1206,  1207,  1208,
    1209,  1210,  1211,  1215,  1216,  1217,  1218,  1223,  1237,  1238,
    1239,  1240,  1241,  1242,  1243,  1244,  1245,  1246,  1250,  1251,
    1252,  1253,  1254,  1271,  1277,  1278,  1279,  1280,  1281,  1282,
    1283,  1284,  1285,  1289,  1290,  1294,  1299,  1304,  1308,  1320,
    1325,  1333,  1337,  1343,  1344,  1348,  1349,  1353,  1354,  1358,
    1359,  1363,  1364,  1377,  1382,  1383,  1384,  1385,  1386,  1387,
    1388,  1389,  1390,  1391,  1392,  1393,  1394,  1395,  1396,  1397,
    1398,  1399,  1400,  1401,  1402,  1403,  1404,  1405,  1406,  1407,
    1408,  1409,  1410,  1411,  1412,  1413,  1414,  1415,  1416,  1417,
    1418,  1419,  1420,  1421,  1422,  1423,  1424,  1425,  1426,  1430,
    1431,  1432,  1433,  1434,  1435,  1443,  1470,  1489,  1490
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
  "WITH", "NEW", "SPAWN", "FOR", "TO", "STEP", "DO", "LOOP", "UNTIL", "IN",
  "EACH", "WHILE", "CONSIDER", "BREAK", "CONTINUE", "FUNCTION", "RETURN",
  "GOTO", "GOSUB", "WATCH", "UNWATCH", "WITHOUT", "WATCHERS", "ON",
  "RESUME", "NEXT", "STOP", "ERROR_VALUE", "MODIFIER", "PROGRAM",
  "LIBRARY", "LOAD", "USE", "EXPORT", "OP_EQ", "OP_NE", "OP_GT", "OP_LT",
  "OP_GE", "OP_LE", "OP_NGT", "OP_NLT", "OP_NGE", "OP_NLE", "PLUS",
  "MINUS", "STAR", "SLASH", "LPAREN", "MOD_LPAREN", "RPAREN", "LBRACKET",
  "RBRACKET", "LBRACE", "RBRACE", "COMMA", "COLON", "NEWLINE",
  "IF_WITHOUT_ELSE", "NO_DOT", "DOT", "$accept", "program",
  "statement_list", "statement", "assignment", "lvalue", "variable_name",
  "modifier", "comparison_lens", "$@1", "modifier_name", "modifier_word",
  "print_statement", "call_statement", "with_lock_statement",
  "for_each_statement", "do_loop_statement", "while_statement",
  "consider_statement", "consider_branch_list", "consider_else_opt",
  "consider_statement_list", "consider_body_statement",
  "function_statement", "modifier_statement", "program_statement",
  "library_statement", "use_statement", "modifier_signature",
  "modifier_context", "watch_statement", "unwatch_statement",
  "watch_target_list", "watch_target_path", "without_watchers_statement",
  "on_error_statement", "error_statement", "return_statement",
  "label_statement", "goto_statement", "gosub_statement",
  "break_statement", "continue_statement", "if_statement", "if_block_tail",
  "if_inline_tail", "inline_statement", "expression", "or_expression",
  "and_expression", "comparison_expression", "additive_expression",
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

#define YYPACT_NINF (-458)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -458,    40,   834,  -458,   -20,    -2,  2118,  -458,  2082,    74,
      16,    28,  -458,  -458,  2118,  2118,  -458,  -458,    59,  2118,
     195,   195,   169,  2118,    75,    78,  -458,    61,   148,   137,
     155,   186,   221,   147,  -458,  -458,   122,    98,   125,   135,
     139,  -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,
     141,  -458,   156,  -458,   157,   159,   161,   164,   165,   167,
     170,   173,  -458,  2118,  2118,   236,  -458,  -458,   171,  -458,
    -458,  -458,  -458,  2118,   483,   254,   188,  -458,  2118,  2118,
    -458,  -458,     4,   252,   243,   245,  -458,   556,   158,  -458,
      85,  -458,  -458,   266,   218,  -458,   200,     2,   270,  -458,
     194,   196,   206,   207,  -458,  -458,  -458,   208,   195,  -458,
      15,   199,  -458,   204,   154,   283,  -458,  -458,  -458,  -458,
    -458,    10,  -458,   261,   217,   209,   288,  -458,   289,  -458,
     148,  -458,  2118,   290,  2118,    99,   235,  -458,  -458,  -458,
    -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,
    -458,   220,   224,   223,  -458,  2118,  -458,    33,   228,   233,
    -458,   237,   505,   656,  2118,   134,  -458,   112,  2118,  2118,
    -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,
    2118,  2118,  -458,   396,   396,  2118,  2118,  2118,  2118,   185,
     306,  2118,  2118,  2118,  2118,   280,   889,  -458,   305,   316,
     316,   195,   -32,   195,  -458,   317,  -458,   318,   273,  -458,
     251,   316,  -458,   322,   316,  -458,   323,   327,   307,  -458,
    -458,   257,   271,   277,  2118,  -458,  2118,  -458,   279,   282,
    2118,  -458,  -458,  -458,  -458,  -458,   278,   291,  -458,  -458,
    -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,
    -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,
    -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,
    -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,
    -458,  -458,  -458,  -458,   -27,   281,   292,   293,   298,  -458,
    -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,
    -458,  -458,   295,   245,  -458,   158,   158,   358,  2118,  2118,
     162,  -458,  -458,   297,   302,   308,  -458,  -458,   310,   300,
     359,  2118,    14,   944,  2118,   181,  -458,   312,   311,   319,
      11,   314,   199,   999,  -458,  1054,  -458,  -458,  2118,   324,
    -458,   321,   325,  1109,  -458,  -458,   322,  -458,  2118,  2118,
    -458,  -458,  -458,  -458,   326,  -458,    94,   389,  2118,  2118,
    -458,   129,  -458,  2118,  -458,  2118,   779,   391,   329,   162,
     162,  -458,  2118,  2118,   328,  -458,  2118,   330,  2118,  2118,
     369,   394,  2118,   331,   393,   332,   407,   334,   337,  -458,
     376,   375,   347,  -458,  -458,   341,   370,   344,   353,   354,
    -458,   581,  -458,  2118,   356,  -458,  -458,   724,  -458,   360,
     362,  1989,   428,  -458,  2025,  -458,  -458,   364,   365,  -458,
    1164,     8,  -458,   361,   363,   366,   367,   432,  -458,   368,
    -458,  -458,  -458,  -458,  1219,   371,   383,  -458,  1274,  -458,
     384,  -458,  -458,  -458,  -458,  -458,   386,   379,    84,  -458,
    -458,  -458,   387,   388,  -458,   390,  -458,  -458,  1329,   440,
    2118,  -458,  1384,  -458,  -458,  -458,  -458,   397,  1439,  -458,
    1494,  1549,  1604,   429,  -458,  -458,   425,  1659,  -458,  1714,
    2118,   389,  2118,  2118,  1769,  -458,  -458,  1824,  -458,   455,
     400,   411,  1879,   461,  1439,  -458,  -458,   412,   413,   414,
    -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,
     417,  -458,   424,  -458,   430,   435,   436,   437,   439,   445,
     446,   452,  -458,   471,   474,   478,   454,   457,   486,   488,
    -458,   467,  -458,  -458,   533,   534,   463,  -458,  -458,   518,
     465,  1439,  -458,  -458,  -458,  -458,  -458,  -458,  -458,  -458,
    -458,  -458,  -458,  -458,  -458,   466,   468,   469,  -458,  -458,
     476,   480,   472,   481,   482,  -458,  1934,   484,  -458,  -458,
    -458,  -458,  -458,  -458,  2118,  -458,  -458,   521,  -458,  -458,
     485,  -458
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       3,     0,     2,     1,    36,     0,     0,    37,     0,     0,
       0,     0,    39,    40,     0,     0,   131,   132,     0,   126,
       0,     0,     0,     0,     0,     0,    38,     0,     0,     0,
       0,     0,     0,     0,     4,     5,     0,     0,    33,     0,
       0,     9,    10,    12,    11,    13,    14,    15,    16,    17,
       0,    19,     0,    21,     0,     0,     0,     0,     0,     0,
       0,     0,    30,   213,   213,   188,    36,   191,     0,   195,
     196,   197,   198,     0,     0,     0,     0,   194,     0,     0,
     277,   277,   205,     0,   152,   153,   155,   157,   161,   164,
     167,   173,   202,   190,     0,    50,     0,     0,     0,     3,
       0,     0,     0,     0,   127,   129,   130,    36,     0,   119,
       0,   117,   116,     0,     0,     0,   125,    46,    48,    47,
      49,   110,    44,     0,     0,     0,   105,   107,   104,   106,
       0,     6,     0,     0,     0,     0,     0,   128,     7,     8,
      18,    20,    22,    23,    24,    25,    26,    27,    28,    29,
     215,     0,   214,     0,   211,   213,   168,   170,     0,     0,
     169,     0,     0,     0,   213,     0,   192,     0,     0,     0,
     178,   179,   180,   181,   182,   183,   184,   185,   186,   187,
       0,     0,    42,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     3,     0,   219,
     219,     0,     0,     0,     3,     0,     3,     0,     0,   124,
       0,   219,    45,     0,   219,     3,     0,     0,     0,    31,
      41,     0,    35,     0,     0,    52,     0,    53,     0,     0,
     213,   189,   199,   200,   278,   217,   277,   223,   224,   225,
     240,   237,   238,   229,   245,   252,   253,   254,   250,   251,
     249,   235,   233,   259,   239,   230,   242,   243,   244,   231,
     234,   241,   268,   255,   256,   262,   246,   257,   258,   266,
     236,   267,   232,   265,   226,   227,   228,   263,   264,   261,
     247,   248,   260,   203,     0,   277,     0,   209,     0,     3,
     141,    33,   142,   143,   144,   145,   146,   147,   148,   149,
     150,   151,     0,   154,   156,   162,   163,     0,     0,     0,
     158,   165,   166,     0,   175,     0,   212,    51,     0,     0,
       0,     0,    39,     0,     0,    68,   221,     0,   220,     0,
       0,     0,   118,     0,   120,     0,   122,   123,   213,     0,
     112,     0,     0,     0,   109,   108,     0,    34,   213,   213,
      32,   216,   193,   171,     0,   277,     0,     0,     0,     0,
     277,     0,   206,   213,   207,   213,     0,   138,     0,   160,
     159,   174,   213,   213,     0,     3,     0,     0,     0,     0,
      37,     0,     0,     0,     0,     0,     0,     0,     0,     3,
      37,    37,     0,   111,     3,     0,    37,     0,     0,     0,
     172,     0,   201,   275,     0,   269,   270,     0,   204,     0,
       0,     0,    37,   133,     0,   134,    43,     0,     0,     3,
       0,     0,     3,     0,     0,     0,     0,     0,    70,     0,
       3,   222,     3,     3,     0,     0,     0,    56,     0,     3,
       0,     3,    54,    55,   218,   276,     0,   223,     0,   210,
     208,     3,     0,     0,     3,     0,   176,   177,     0,    37,
       0,     3,     0,    62,    63,    64,    70,     0,    69,    65,
       0,     0,     0,    37,   114,   121,    37,     0,   103,     0,
       0,     0,     0,     0,     0,   136,   135,     0,   139,    37,
       0,     0,     0,    37,    66,    70,    71,     0,     0,     0,
      76,    77,    79,    78,    80,    72,    81,    82,    83,    84,
       0,    86,     0,    88,     0,     0,     0,     0,     0,     0,
       0,     0,    97,    37,    37,    37,     0,     0,    37,    37,
     271,     0,   272,   273,    37,    37,     0,    58,     3,    37,
       0,    67,    73,    74,    75,    85,    87,    89,    90,    91,
      92,    93,    94,    95,    96,     0,     0,     0,   113,   100,
       0,     0,     0,     0,     0,    57,     0,     0,    59,    98,
      99,   115,   102,   101,     0,   137,   140,    37,    60,   274,
       0,    61
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -458,  -458,   -92,  -458,  -163,  -458,    13,   473,  -458,  -458,
    -458,   444,  -161,  -157,  -457,  -449,  -445,  -442,  -439,  -458,
    -458,  -436,  -458,  -437,  -426,  -424,  -422,  -155,   441,   226,
    -394,  -393,  -103,   373,  -362,  -151,  -149,  -143,  -328,  -139,
    -126,  -111,  -105,  -319,  -458,  -458,  -199,    -6,  -458,   401,
     404,  -184,    54,   -51,   496,    53,  -458,   345,  -458,  -458,
    -458,    69,  -458,  -458,   -33,  -458,   183,  -458,    96,   -78
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    35,    36,    37,    82,   136,   184,   307,
     121,   122,    39,    40,    41,    42,    43,    44,    45,   325,
     384,   468,   505,    46,    47,    48,    49,    50,   123,   341,
      51,    52,   110,   111,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,   413,   415,   302,   150,    84,    85,
      86,    87,    88,    89,    90,   185,    91,    92,   166,   364,
      93,   151,   152,   236,   327,   328,   284,   285,   404,   162
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      83,   310,    95,   163,   290,   202,   292,   196,   100,   101,
     293,   500,   294,   104,   117,    38,   295,   112,   296,   501,
      97,   116,   156,   502,   297,   118,   503,   160,   298,   504,
     494,   506,   358,   105,   106,   109,   193,   500,   460,   119,
       3,   299,   507,   331,   508,   501,   509,   378,   203,   502,
     379,    98,   503,    63,   359,   504,   300,   506,   229,   541,
     120,   194,   301,   102,    65,    66,    67,   103,   507,    68,
     508,    64,   509,   161,   511,   512,     7,   164,    96,    69,
      70,    71,    72,   211,   500,    73,   388,    74,    75,   165,
     461,   203,   501,    12,    13,   203,   502,   204,   330,   503,
     511,   512,   504,   222,   506,   323,   513,   223,    76,   188,
      99,    26,   333,    77,   335,   507,     4,   508,   189,   509,
       5,   109,   113,   343,   369,   370,   219,     7,   221,     8,
     114,    78,   513,   153,    79,   311,   312,    80,   287,    81,
     517,   124,   288,   482,    12,    13,   115,   511,   512,   522,
      16,    17,   117,    19,    20,    21,   235,   132,   356,   125,
      25,   188,    26,   118,    27,   483,   517,   329,    31,    32,
     189,   402,   133,   107,   134,   522,   234,   119,   339,   513,
     291,   342,   313,   135,     7,   317,   318,   319,   320,   314,
     126,   127,   382,   315,   289,   383,   207,   366,   120,    66,
     130,    12,    13,   208,   131,   209,   137,   361,   408,    38,
       7,   234,   452,   517,   109,   455,   109,   138,   350,    26,
     351,   139,   522,   140,   228,   128,   129,    12,    13,   186,
     187,   180,   181,   286,   305,   306,   308,   309,   141,   142,
     154,   143,   108,   144,   155,    26,   145,   146,   290,   147,
     292,   290,   148,   292,   293,   149,   294,   293,   158,   294,
     295,   159,   296,   295,   167,   296,   168,   169,   297,   190,
     191,   297,   298,   192,   195,   298,   197,   401,   198,   199,
     200,   201,   407,   420,   205,   299,   206,   210,   299,   213,
     214,   215,   216,   217,   224,   225,   220,   434,   227,   354,
     300,   230,   438,   300,   226,   497,   301,   498,   231,   301,
     316,   499,   232,   510,   321,   377,   324,   514,   381,   515,
     326,   334,   336,   337,   338,   516,   340,   458,   344,   518,
     462,   497,   345,   498,   347,   346,    38,   499,   470,   510,
     471,   472,   519,   514,   348,   515,    38,   477,    38,   479,
     349,   516,   405,   406,   352,   518,    38,   520,   355,   484,
      81,   360,   487,   521,   357,   368,   363,   362,   519,   492,
     421,   365,   423,   424,   371,   372,   427,   367,   497,    38,
     498,   373,   375,   520,   499,   374,   510,   385,   376,   521,
     514,   386,   515,   403,   387,   444,   389,   445,   516,   393,
     395,   400,   518,   394,   414,   425,   426,   392,   416,   429,
     419,   431,   422,   428,   430,   519,   432,   398,   399,   433,
     435,   436,   437,   439,   291,   440,   441,   291,   442,   443,
     520,   446,   409,    38,   410,   449,   521,   450,   453,   456,
     457,   417,   418,   463,   467,   464,   566,    38,   465,   466,
     469,    38,   481,   474,   491,   170,   171,   172,   173,   174,
     175,   176,   177,   178,   179,   475,   478,   480,   490,   485,
     486,    38,   488,   526,   530,    38,   532,   533,   527,   495,
     536,    38,   537,    38,    38,    38,    65,    66,    67,   540,
      38,    68,    38,   538,   542,   543,   544,    38,     7,   545,
      38,    69,    70,    71,    72,    38,   546,    38,    65,    66,
      67,   555,   547,    68,   556,    12,    13,   548,   549,   550,
       7,   551,   557,    69,    70,    71,    72,   552,   553,    73,
      76,    74,    75,    26,   554,    77,   558,    12,    13,   559,
     560,   561,   562,   563,   564,   565,   567,   568,   569,   580,
     570,   571,    76,   574,    38,    26,    79,    77,   572,    80,
     183,    81,   573,   575,   576,   212,   578,   581,   579,   303,
     157,   218,   397,   304,   353,    78,   332,   531,    79,    38,
       0,    80,   233,    81,    65,    66,    67,   234,     0,    68,
     448,     0,     0,     0,     0,     0,     7,     0,     0,    69,
      70,    71,    72,     0,     0,    73,     0,    74,    75,     0,
       0,     0,     0,    12,    13,   170,   171,   172,   173,   174,
     175,   176,   177,   178,   179,   180,   181,     0,    76,     0,
     133,    26,     0,    77,   182,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    78,     0,     0,    79,     0,     0,    80,     0,    81,
     237,   238,     0,   234,     0,   239,   240,     0,   241,   242,
       0,   243,     0,   244,   245,   246,   247,     0,   248,   249,
     250,   251,   252,   253,   254,   255,     0,   256,   257,   258,
     259,   260,   261,   262,   263,   264,   265,   266,   267,   268,
     269,     0,   270,   271,   272,   273,   274,   275,   276,   277,
     278,   279,   280,   281,   282,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   447,   238,
       0,     0,     0,   239,   240,   283,   241,   242,   234,   243,
       0,   244,   245,   246,   247,     0,   248,   249,   250,   251,
     252,   253,   254,   255,     0,   256,   257,   258,   259,   260,
     261,   262,   263,   264,   265,   266,   267,   268,   269,     0,
     270,   271,   272,   273,   274,   275,   276,   277,   278,   279,
     280,   281,   282,     4,     0,     0,     0,     5,     0,     6,
       0,     0,   411,     0,   412,     0,     8,     0,     0,     0,
       0,     0,     0,     0,     9,     0,   234,    10,     0,     0,
      11,    12,    13,     0,     0,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,     0,    25,     0,    26,
       0,    27,    28,    29,    30,    31,    32,    33,     4,     0,
       0,     0,     5,     0,     6,     0,     0,     0,     0,     7,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    34,    10,     0,     0,    11,    12,    13,     0,     0,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,    25,     0,    26,     0,    27,    28,    29,    30,
      31,    32,    33,     4,     0,     0,     0,     5,     0,     6,
       0,     0,     0,     0,     7,     0,     8,     0,     0,     0,
       0,     0,     0,     0,     9,     0,    34,    10,     0,     0,
      11,   322,    13,     0,     0,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,     0,    25,     0,    26,
       0,    27,    28,    29,    30,    31,    32,    33,     4,     0,
       0,     0,     5,     0,     6,     0,     0,     0,     0,   380,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    34,    10,     0,     0,    11,    12,    13,     0,     0,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,    25,     0,    26,     0,    27,    28,    29,    30,
      31,    32,    33,     4,     0,     0,     0,     5,     0,     6,
       0,     0,     0,     0,   390,     0,     8,     0,     0,     0,
       0,     0,     0,     0,     9,     0,    34,    10,     0,     0,
      11,    12,    13,     0,     0,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,     0,    25,     0,    26,
       0,    27,    28,    29,    30,    31,    32,    33,     4,     0,
       0,     0,     5,     0,     6,     0,     0,     0,     0,   391,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    34,    10,     0,     0,    11,    12,    13,     0,     0,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,    25,     0,    26,     0,    27,    28,    29,    30,
      31,    32,    33,     4,     0,     0,     0,     5,     0,     6,
       0,     0,     0,     0,   396,     0,     8,     0,     0,     0,
       0,     0,     0,     0,     9,     0,    34,    10,     0,     0,
      11,    12,    13,     0,     0,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,     0,    25,     0,    26,
       0,    27,    28,    29,    30,    31,    32,    33,     4,     0,
       0,     0,     5,     0,     6,     0,     0,     0,     0,   459,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    34,    10,     0,     0,    11,    12,    13,     0,     0,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,    25,     0,    26,     0,    27,    28,    29,    30,
      31,    32,    33,     4,     0,     0,     0,     5,     0,     6,
       0,     0,     0,     0,   473,     0,     8,     0,     0,     0,
       0,     0,     0,     0,     9,     0,    34,    10,     0,     0,
      11,    12,    13,     0,     0,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,     0,    25,     0,    26,
       0,    27,    28,    29,    30,    31,    32,    33,     4,     0,
       0,     0,     5,     0,     6,     0,     0,     0,     0,   476,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    34,    10,     0,     0,    11,    12,    13,     0,     0,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,    25,     0,    26,     0,    27,    28,    29,    30,
      31,    32,    33,     4,     0,     0,     0,     5,     0,     6,
       0,     0,     0,     0,   489,     0,     8,     0,     0,     0,
       0,     0,     0,     0,     9,     0,    34,    10,     0,     0,
      11,    12,    13,     0,     0,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,     0,    25,     0,    26,
       0,    27,    28,    29,    30,    31,    32,    33,     4,     0,
       0,     0,     5,     0,     6,     0,     0,     0,     0,   493,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    34,    10,     0,     0,    11,    12,    13,     0,     0,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,    25,     0,    26,     0,    27,    28,    29,    30,
      31,    32,    33,     4,     0,     0,     0,     5,     0,     6,
       0,     0,     0,     0,     7,     0,     8,     0,     0,     0,
       0,     0,     0,     0,     9,     0,    34,    10,     0,     0,
      11,    12,    13,     0,     0,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,     0,    25,     0,    26,
       0,    27,    28,    29,    30,    31,    32,    33,     4,     0,
       0,     0,     5,     0,     6,     0,     0,     0,     0,   523,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,   496,    10,     0,     0,    11,    12,    13,     0,     0,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,    25,     0,    26,     0,    27,    28,    29,    30,
      31,    32,    33,     4,     0,     0,     0,     5,     0,     6,
       0,     0,     0,     0,   524,     0,     8,     0,     0,     0,
       0,     0,     0,     0,     9,     0,    34,    10,     0,     0,
      11,    12,    13,     0,     0,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,     0,    25,     0,    26,
       0,    27,    28,    29,    30,    31,    32,    33,     4,     0,
       0,     0,     5,     0,     6,     0,     0,     0,     0,   525,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    34,    10,     0,     0,    11,    12,    13,     0,     0,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,    25,     0,    26,     0,    27,    28,    29,    30,
      31,    32,    33,     4,     0,     0,     0,     5,     0,     6,
       0,     0,     0,     0,   528,     0,     8,     0,     0,     0,
       0,     0,     0,     0,     9,     0,    34,    10,     0,     0,
      11,    12,    13,     0,     0,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,     0,    25,     0,    26,
       0,    27,    28,    29,    30,    31,    32,    33,     4,     0,
       0,     0,     5,     0,     6,     0,     0,     0,     0,   529,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    34,    10,     0,     0,    11,    12,    13,     0,     0,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,    25,     0,    26,     0,    27,    28,    29,    30,
      31,    32,    33,     4,     0,     0,     0,     5,     0,     6,
       0,     0,     0,     0,   534,     0,     8,     0,     0,     0,
       0,     0,     0,     0,     9,     0,    34,    10,     0,     0,
      11,    12,    13,     0,     0,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,     0,    25,     0,    26,
       0,    27,    28,    29,    30,    31,    32,    33,     4,     0,
       0,     0,     5,     0,     6,     0,     0,     0,     0,   535,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    34,    10,     0,     0,    11,    12,    13,     0,     0,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,    25,     0,    26,     0,    27,    28,    29,    30,
      31,    32,    33,     4,     0,     0,     0,     5,     0,     6,
       0,     0,     0,     0,   539,     0,     8,     0,     0,     0,
       0,     0,     0,     0,     9,     0,    34,    10,     0,     0,
      11,    12,    13,     0,     0,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,     0,    25,     0,    26,
       0,    27,    28,    29,    30,    31,    32,    33,     4,     0,
       0,     0,     5,     0,     6,     0,     0,     0,     0,   577,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    34,    10,     0,     0,    11,    12,    13,     0,     0,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,    25,     0,    26,     0,    27,    28,    29,    30,
      31,    32,    33,     4,     0,     0,     0,     5,     0,     0,
       0,     0,     0,     0,     7,     0,     8,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    34,     0,     0,     0,
       0,    12,    13,     0,     0,     0,     0,    16,    17,     4,
      19,    20,    21,     5,     0,     0,     0,    25,     0,    26,
       7,    27,     8,     0,     0,    31,    32,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    12,    13,     0,
       0,     0,     0,    16,    17,     0,    19,    20,    21,     0,
       0,   451,     0,    25,     0,    26,     0,    27,     0,     0,
       0,    31,    32,     0,     0,    65,    66,    67,     0,     0,
      68,     0,     0,     0,     0,     0,     0,     7,     0,     0,
      69,    70,    71,    72,     0,     0,    73,   454,    74,    75,
       0,    94,     0,     0,    12,    13,     0,     0,     0,     0,
       0,    65,    66,    67,     0,     0,    68,     0,     0,    76,
       0,     0,    26,     7,    77,     0,    69,    70,    71,    72,
       0,     0,    73,     0,    74,    75,     0,     0,     0,     0,
      12,    13,    78,     0,     0,    79,     0,     0,    80,     0,
      81,     0,     0,     0,     0,    76,     0,     0,    26,     0,
      77,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    78,     0,
       0,    79,     0,     0,    80,     0,    81
};

static const yytype_int16 yycheck[] =
{
       6,   185,     8,    81,   167,   108,   167,    99,    14,    15,
     167,   468,   167,    19,     4,     2,   167,    23,   167,   468,
       4,    27,    73,   468,   167,    15,   468,    78,   167,   468,
     466,   468,    59,    20,    21,    22,    34,   494,    30,    29,
       0,   167,   468,    75,   468,   494,   468,    33,    80,   494,
      36,    35,   494,    73,    81,   494,   167,   494,    25,   495,
      50,    59,   167,     4,     3,     4,     5,     8,   494,     8,
     494,    73,   494,    79,   468,   468,    15,    73,     4,    18,
      19,    20,    21,    73,   541,    24,    75,    26,    27,    85,
      82,    80,   541,    32,    33,    80,   541,    82,   201,   541,
     494,   494,   541,     4,   541,   197,   468,     8,    47,    76,
      82,    50,   204,    52,   206,   541,     4,   541,    85,   541,
       8,   108,    47,   215,   308,   309,   132,    15,   134,    17,
      52,    70,   494,    64,    73,   186,   187,    76,     4,    78,
     468,     4,     8,    59,    32,    33,    85,   541,   541,   468,
      38,    39,     4,    41,    42,    43,   162,    59,   236,     4,
      48,    76,    50,    15,    52,    81,   494,   200,    56,    57,
      85,    77,    74,     4,    76,   494,    82,    29,   211,   541,
     167,   214,   188,    85,    15,   191,   192,   193,   194,     4,
       4,     5,    11,     8,    82,    14,    42,   289,    50,     4,
      53,    32,    33,    49,    82,    51,    81,   285,    79,   196,
      15,    82,   411,   541,   201,   414,   203,    82,   224,    50,
     226,    82,   541,    82,   155,     4,     5,    32,    33,    71,
      72,    69,    70,   164,   180,   181,   183,   184,    82,    82,
       4,    82,    73,    82,    73,    50,    82,    82,   411,    82,
     411,   414,    82,   414,   411,    82,   411,   414,     4,   414,
     411,    73,   411,   414,    12,   414,    23,    22,   411,     3,
      52,   414,   411,    73,     4,   414,    82,   355,    82,    73,
      73,    73,   360,   375,    85,   411,    82,     4,   414,    28,
      73,    82,     4,     4,    59,    75,     6,   389,    75,   230,
     411,    73,   394,   414,    80,   468,   411,   468,    75,   414,
       4,   468,    75,   468,    34,   321,    11,   468,   324,   468,
       4,     4,     4,    50,    73,   468,     4,   419,     5,   468,
     422,   494,     5,   494,    77,    28,   323,   494,   430,   494,
     432,   433,   468,   494,    73,   494,   333,   439,   335,   441,
      73,   494,   358,   359,    75,   494,   343,   468,    80,   451,
      78,    80,   454,   468,    73,     7,    73,    75,   494,   461,
     376,    73,   378,   379,    77,    73,   382,    82,   541,   366,
     541,    73,    82,   494,   541,    75,   541,    75,    29,   494,
     541,    80,   541,     4,    75,   401,    82,   403,   541,    75,
      75,    75,   541,    82,    13,    36,    12,   338,    79,    16,
      82,     4,    82,    82,    82,   541,    82,   348,   349,    82,
      44,    46,    75,    82,   411,    55,    82,   414,    75,    75,
     541,    75,   363,   420,   365,    75,   541,    75,    10,    75,
      75,   372,   373,    82,    12,    82,   538,   434,    82,    82,
      82,   438,    73,    82,   460,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    82,    82,    81,    28,    82,
      82,   458,    82,    44,   480,   462,   482,   483,    53,    82,
      25,   468,    82,   470,   471,   472,     3,     4,     5,    28,
     477,     8,   479,    82,    82,    82,    82,   484,    15,    82,
     487,    18,    19,    20,    21,   492,    82,   494,     3,     4,
       5,    40,    82,     8,    40,    32,    33,    82,    82,    82,
      15,    82,    44,    18,    19,    20,    21,    82,    82,    24,
      47,    26,    27,    50,    82,    52,    82,    32,    33,    82,
      54,    53,    75,    10,    10,    82,    28,    82,    82,    28,
      82,    82,    47,    81,   541,    50,    73,    52,    82,    76,
      87,    78,    82,    82,    82,   121,    82,    82,   574,   168,
      74,   130,   346,   169,   229,    70,   203,   481,    73,   566,
      -1,    76,    77,    78,     3,     4,     5,    82,    -1,     8,
     407,    -1,    -1,    -1,    -1,    -1,    15,    -1,    -1,    18,
      19,    20,    21,    -1,    -1,    24,    -1,    26,    27,    -1,
      -1,    -1,    -1,    32,    33,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,    -1,    47,    -1,
      74,    50,    -1,    52,    78,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    70,    -1,    -1,    73,    -1,    -1,    76,    -1,    78,
       4,     5,    -1,    82,    -1,     9,    10,    -1,    12,    13,
      -1,    15,    -1,    17,    18,    19,    20,    -1,    22,    23,
      24,    25,    26,    27,    28,    29,    -1,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    -1,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,     4,     5,
      -1,    -1,    -1,     9,    10,    79,    12,    13,    82,    15,
      -1,    17,    18,    19,    20,    -1,    22,    23,    24,    25,
      26,    27,    28,    29,    -1,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    -1,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,     4,    -1,    -1,    -1,     8,    -1,    10,
      -1,    -1,    13,    -1,    15,    -1,    17,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    25,    -1,    82,    28,    -1,    -1,
      31,    32,    33,    -1,    -1,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    -1,    48,    -1,    50,
      -1,    52,    53,    54,    55,    56,    57,    58,     4,    -1,
      -1,    -1,     8,    -1,    10,    -1,    -1,    -1,    -1,    15,
      -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,
      -1,    82,    28,    -1,    -1,    31,    32,    33,    -1,    -1,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    -1,    50,    -1,    52,    53,    54,    55,
      56,    57,    58,     4,    -1,    -1,    -1,     8,    -1,    10,
      -1,    -1,    -1,    -1,    15,    -1,    17,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    25,    -1,    82,    28,    -1,    -1,
      31,    32,    33,    -1,    -1,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    -1,    48,    -1,    50,
      -1,    52,    53,    54,    55,    56,    57,    58,     4,    -1,
      -1,    -1,     8,    -1,    10,    -1,    -1,    -1,    -1,    15,
      -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,
      -1,    82,    28,    -1,    -1,    31,    32,    33,    -1,    -1,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    -1,    50,    -1,    52,    53,    54,    55,
      56,    57,    58,     4,    -1,    -1,    -1,     8,    -1,    10,
      -1,    -1,    -1,    -1,    15,    -1,    17,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    25,    -1,    82,    28,    -1,    -1,
      31,    32,    33,    -1,    -1,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    -1,    48,    -1,    50,
      -1,    52,    53,    54,    55,    56,    57,    58,     4,    -1,
      -1,    -1,     8,    -1,    10,    -1,    -1,    -1,    -1,    15,
      -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,
      -1,    82,    28,    -1,    -1,    31,    32,    33,    -1,    -1,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    -1,    50,    -1,    52,    53,    54,    55,
      56,    57,    58,     4,    -1,    -1,    -1,     8,    -1,    10,
      -1,    -1,    -1,    -1,    15,    -1,    17,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    25,    -1,    82,    28,    -1,    -1,
      31,    32,    33,    -1,    -1,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    -1,    48,    -1,    50,
      -1,    52,    53,    54,    55,    56,    57,    58,     4,    -1,
      -1,    -1,     8,    -1,    10,    -1,    -1,    -1,    -1,    15,
      -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,
      -1,    82,    28,    -1,    -1,    31,    32,    33,    -1,    -1,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    -1,    50,    -1,    52,    53,    54,    55,
      56,    57,    58,     4,    -1,    -1,    -1,     8,    -1,    10,
      -1,    -1,    -1,    -1,    15,    -1,    17,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    25,    -1,    82,    28,    -1,    -1,
      31,    32,    33,    -1,    -1,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    -1,    48,    -1,    50,
      -1,    52,    53,    54,    55,    56,    57,    58,     4,    -1,
      -1,    -1,     8,    -1,    10,    -1,    -1,    -1,    -1,    15,
      -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,
      -1,    82,    28,    -1,    -1,    31,    32,    33,    -1,    -1,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    -1,    50,    -1,    52,    53,    54,    55,
      56,    57,    58,     4,    -1,    -1,    -1,     8,    -1,    10,
      -1,    -1,    -1,    -1,    15,    -1,    17,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    25,    -1,    82,    28,    -1,    -1,
      31,    32,    33,    -1,    -1,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    -1,    48,    -1,    50,
      -1,    52,    53,    54,    55,    56,    57,    58,     4,    -1,
      -1,    -1,     8,    -1,    10,    -1,    -1,    -1,    -1,    15,
      -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,
      -1,    82,    28,    -1,    -1,    31,    32,    33,    -1,    -1,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    -1,    50,    -1,    52,    53,    54,    55,
      56,    57,    58,     4,    -1,    -1,    -1,     8,    -1,    10,
      -1,    -1,    -1,    -1,    15,    -1,    17,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    25,    -1,    82,    28,    -1,    -1,
      31,    32,    33,    -1,    -1,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    -1,    48,    -1,    50,
      -1,    52,    53,    54,    55,    56,    57,    58,     4,    -1,
      -1,    -1,     8,    -1,    10,    -1,    -1,    -1,    -1,    15,
      -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,
      -1,    82,    28,    -1,    -1,    31,    32,    33,    -1,    -1,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    -1,    50,    -1,    52,    53,    54,    55,
      56,    57,    58,     4,    -1,    -1,    -1,     8,    -1,    10,
      -1,    -1,    -1,    -1,    15,    -1,    17,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    25,    -1,    82,    28,    -1,    -1,
      31,    32,    33,    -1,    -1,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    -1,    48,    -1,    50,
      -1,    52,    53,    54,    55,    56,    57,    58,     4,    -1,
      -1,    -1,     8,    -1,    10,    -1,    -1,    -1,    -1,    15,
      -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,
      -1,    82,    28,    -1,    -1,    31,    32,    33,    -1,    -1,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    -1,    50,    -1,    52,    53,    54,    55,
      56,    57,    58,     4,    -1,    -1,    -1,     8,    -1,    10,
      -1,    -1,    -1,    -1,    15,    -1,    17,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    25,    -1,    82,    28,    -1,    -1,
      31,    32,    33,    -1,    -1,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    -1,    48,    -1,    50,
      -1,    52,    53,    54,    55,    56,    57,    58,     4,    -1,
      -1,    -1,     8,    -1,    10,    -1,    -1,    -1,    -1,    15,
      -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,
      -1,    82,    28,    -1,    -1,    31,    32,    33,    -1,    -1,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    -1,    50,    -1,    52,    53,    54,    55,
      56,    57,    58,     4,    -1,    -1,    -1,     8,    -1,    10,
      -1,    -1,    -1,    -1,    15,    -1,    17,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    25,    -1,    82,    28,    -1,    -1,
      31,    32,    33,    -1,    -1,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    -1,    48,    -1,    50,
      -1,    52,    53,    54,    55,    56,    57,    58,     4,    -1,
      -1,    -1,     8,    -1,    10,    -1,    -1,    -1,    -1,    15,
      -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,
      -1,    82,    28,    -1,    -1,    31,    32,    33,    -1,    -1,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    -1,    50,    -1,    52,    53,    54,    55,
      56,    57,    58,     4,    -1,    -1,    -1,     8,    -1,    10,
      -1,    -1,    -1,    -1,    15,    -1,    17,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    25,    -1,    82,    28,    -1,    -1,
      31,    32,    33,    -1,    -1,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    -1,    48,    -1,    50,
      -1,    52,    53,    54,    55,    56,    57,    58,     4,    -1,
      -1,    -1,     8,    -1,    10,    -1,    -1,    -1,    -1,    15,
      -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,
      -1,    82,    28,    -1,    -1,    31,    32,    33,    -1,    -1,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    -1,    50,    -1,    52,    53,    54,    55,
      56,    57,    58,     4,    -1,    -1,    -1,     8,    -1,    -1,
      -1,    -1,    -1,    -1,    15,    -1,    17,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    82,    -1,    -1,    -1,
      -1,    32,    33,    -1,    -1,    -1,    -1,    38,    39,     4,
      41,    42,    43,     8,    -1,    -1,    -1,    48,    -1,    50,
      15,    52,    17,    -1,    -1,    56,    57,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    32,    33,    -1,
      -1,    -1,    -1,    38,    39,    -1,    41,    42,    43,    -1,
      -1,    82,    -1,    48,    -1,    50,    -1,    52,    -1,    -1,
      -1,    56,    57,    -1,    -1,     3,     4,     5,    -1,    -1,
       8,    -1,    -1,    -1,    -1,    -1,    -1,    15,    -1,    -1,
      18,    19,    20,    21,    -1,    -1,    24,    82,    26,    27,
      -1,    29,    -1,    -1,    32,    33,    -1,    -1,    -1,    -1,
      -1,     3,     4,     5,    -1,    -1,     8,    -1,    -1,    47,
      -1,    -1,    50,    15,    52,    -1,    18,    19,    20,    21,
      -1,    -1,    24,    -1,    26,    27,    -1,    -1,    -1,    -1,
      32,    33,    70,    -1,    -1,    73,    -1,    -1,    76,    -1,
      78,    -1,    -1,    -1,    -1,    47,    -1,    -1,    50,    -1,
      52,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    70,    -1,
      -1,    73,    -1,    -1,    76,    -1,    78
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    87,    88,     0,     4,     8,    10,    15,    17,    25,
      28,    31,    32,    33,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    48,    50,    52,    53,    54,
      55,    56,    57,    58,    82,    89,    90,    91,    92,    98,
      99,   100,   101,   102,   103,   104,   109,   110,   111,   112,
     113,   116,   117,   120,   121,   122,   123,   124,   125,   126,
     127,   128,   129,    73,    73,     3,     4,     5,     8,    18,
      19,    20,    21,    24,    26,    27,    47,    52,    70,    73,
      76,    78,    92,   133,   134,   135,   136,   137,   138,   139,
     140,   142,   143,   146,    29,   133,     4,     4,    35,    82,
     133,   133,     4,     8,   133,    92,    92,     4,    73,    92,
     118,   119,   133,    47,    52,    85,   133,     4,    15,    29,
      50,    96,    97,   114,     4,     4,     4,     5,     4,     5,
      53,    82,    59,    74,    76,    85,    93,    81,    82,    82,
      82,    82,    82,    82,    82,    82,    82,    82,    82,    82,
     133,   147,   148,   147,     4,    73,   139,   140,     4,    73,
     139,   133,   155,   155,    73,    85,   144,    12,    23,    22,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    78,    93,    94,   141,    71,    72,    76,    85,
       3,    52,    73,    34,    59,     4,    88,    82,    82,    73,
      73,    73,   118,    80,    82,    85,    82,    42,    49,    51,
       4,    73,    97,    28,    73,    82,     4,     4,   114,   133,
       6,   133,     4,     8,    59,    75,    80,    75,   147,    25,
      73,    75,    75,    77,    82,   133,   149,     4,     5,     9,
      10,    12,    13,    15,    17,    18,    19,    20,    22,    23,
      24,    25,    26,    27,    28,    29,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    79,   152,   153,   147,     4,     8,    82,
      90,    92,    98,    99,   113,   121,   122,   123,   125,   126,
     127,   128,   132,   135,   136,   138,   138,    95,   141,   141,
     137,   139,   139,   133,     4,     8,     4,   133,   133,   133,
     133,    34,    32,    88,    11,   105,     4,   150,   151,   150,
     118,    75,   119,    88,     4,    88,     4,    50,    73,   150,
       4,   115,   150,    88,     5,     5,    28,    77,    73,    73,
     133,   133,    75,   143,   147,    80,   155,    73,    59,    81,
      80,   155,    75,    73,   145,    73,    88,    82,     7,   137,
     137,    77,    73,    73,    75,    82,    29,   133,    33,    36,
      15,   133,    11,    14,   106,    75,    80,    75,    75,    82,
      15,    15,   147,    75,    82,    75,    15,   115,   147,   147,
      75,   155,    77,     4,   154,   133,   133,   155,    79,   147,
     147,    13,    15,   130,    13,   131,    79,   147,   147,    82,
      88,   133,    82,   133,   133,    36,    12,   133,    82,    16,
      82,     4,    82,    82,    88,    44,    46,    75,    88,    82,
      55,    82,    75,    75,   133,   133,    75,     4,   152,    75,
      75,    82,   132,    10,    82,   132,    75,    75,    88,    15,
      30,    82,    88,    82,    82,    82,    82,    12,   107,    82,
      88,    88,    88,    15,    82,    82,    15,    88,    82,    88,
      81,    73,    59,    81,    88,    82,    82,    88,    82,    15,
      28,   133,    88,    15,   107,    82,    82,    90,    98,    99,
     100,   101,   102,   103,   104,   108,   109,   110,   111,   112,
     113,   116,   117,   120,   121,   122,   123,   124,   125,   126,
     127,   128,   129,    15,    15,    15,    44,    53,    15,    15,
     133,   154,   133,   133,    15,    15,    25,    82,    82,    15,
      28,   107,    82,    82,    82,    82,    82,    82,    82,    82,
      82,    82,    82,    82,    82,    40,    40,    44,    82,    82,
      54,    53,    75,    10,    10,    82,    88,    28,    82,    82,
      82,    82,    82,    82,    81,    82,    82,    15,    82,   133,
      28,    82
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    86,    87,    88,    88,    88,    89,    89,    89,    89,
      89,    89,    89,    89,    89,    89,    89,    89,    89,    89,
      89,    89,    89,    89,    89,    89,    89,    89,    89,    89,
      89,    90,    90,    91,    91,    91,    92,    92,    92,    92,
      92,    93,    95,    94,    96,    96,    97,    97,    97,    97,
      98,    98,    99,    99,    99,    99,    99,   100,   101,   101,
     101,   101,   102,   102,   103,   104,   105,   105,   106,   106,
     107,   107,   107,   108,   108,   108,   108,   108,   108,   108,
     108,   108,   108,   108,   108,   108,   108,   108,   108,   108,
     108,   108,   108,   108,   108,   108,   108,   108,   109,   109,
     110,   110,   111,   112,   113,   113,   113,   113,   113,   113,
     114,   114,   115,   116,   116,   116,   117,   118,   118,   119,
     119,   120,   121,   121,   121,   122,   123,   123,   124,   125,
     126,   127,   128,   129,   129,   130,   130,   130,   131,   131,
     131,   132,   132,   132,   132,   132,   132,   132,   132,   132,
     132,   132,   133,   134,   134,   135,   135,   136,   136,   136,
     136,   137,   137,   137,   138,   138,   138,   139,   139,   139,
     139,   139,   139,   140,   140,   140,   140,   140,   141,   141,
     141,   141,   141,   141,   141,   141,   141,   141,   142,   142,
     142,   142,   142,   142,   142,   142,   142,   142,   142,   142,
     142,   142,   142,   143,   143,   144,   144,   144,   144,   145,
     145,   146,   146,   147,   147,   148,   148,   149,   149,   150,
     150,   151,   151,   152,   152,   152,   152,   152,   152,   152,
     152,   152,   152,   152,   152,   152,   152,   152,   152,   152,
     152,   152,   152,   152,   152,   152,   152,   152,   152,   152,
     152,   152,   152,   152,   152,   152,   152,   152,   152,   152,
     152,   152,   152,   152,   152,   152,   152,   152,   152,   153,
     153,   153,   153,   153,   153,   154,   154,   155,   155
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     2,     1,
       2,     1,     2,     2,     2,     2,     2,     2,     2,     2,
       1,     3,     4,     1,     4,     3,     1,     1,     1,     1,
       1,     2,     0,     4,     1,     2,     1,     1,     1,     1,
       2,     4,     4,     4,     6,     6,     6,    10,     9,    10,
      11,    13,     7,     7,     7,     7,     5,     6,     0,     3,
       0,     2,     2,     2,     2,     2,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     1,     2,     1,     2,
       2,     2,     2,     2,     2,     2,     2,     1,    10,    10,
       9,    10,    10,     7,     2,     2,     2,     2,     4,     4,
       1,     4,     1,     9,     7,    10,     2,     1,     3,     1,
       3,     7,     4,     4,     3,     2,     1,     2,     2,     2,
       2,     1,     1,     6,     6,     3,     3,     6,     0,     3,
       6,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     3,     1,     3,     1,     3,     4,
       4,     1,     3,     3,     1,     3,     3,     1,     2,     2,
       2,     4,     5,     1,     4,     3,     6,     6,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     3,
       1,     1,     2,     4,     1,     1,     1,     1,     1,     3,
       3,     5,     1,     3,     5,     0,     3,     3,     5,     0,
       3,     2,     3,     0,     1,     1,     3,     1,     4,     0,
       1,     1,     3,     1,     1,     1,     1,     1,     1,     1,
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
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2653 "src/parser.tab.c"
        break;

    case YYSYMBOL_STRING: /* STRING  */
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2659 "src/parser.tab.c"
        break;

    case YYSYMBOL_MOD_CONTENT: /* MOD_CONTENT  */
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2665 "src/parser.tab.c"
        break;

    case YYSYMBOL_LENS_CONTENT: /* LENS_CONTENT  */
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2671 "src/parser.tab.c"
        break;

    case YYSYMBOL_QUALIFIED_IDENT: /* QUALIFIED_IDENT  */
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2677 "src/parser.tab.c"
        break;

    case YYSYMBOL_program: /* program  */
#line 716 "src/parser.y"
            { (void) ((*yyvaluep).stmt_list); }
#line 2683 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement_list: /* statement_list  */
#line 698 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2689 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement: /* statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2695 "src/parser.tab.c"
        break;

    case YYSYMBOL_assignment: /* assignment  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2701 "src/parser.tab.c"
        break;

    case YYSYMBOL_lvalue: /* lvalue  */
#line 696 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2707 "src/parser.tab.c"
        break;

    case YYSYMBOL_variable_name: /* variable_name  */
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2713 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier: /* modifier  */
#line 703 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 2719 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_lens: /* comparison_lens  */
#line 703 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 2725 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_name: /* modifier_name  */
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2731 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_word: /* modifier_word  */
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2737 "src/parser.tab.c"
        break;

    case YYSYMBOL_print_statement: /* print_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2743 "src/parser.tab.c"
        break;

    case YYSYMBOL_call_statement: /* call_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2749 "src/parser.tab.c"
        break;

    case YYSYMBOL_with_lock_statement: /* with_lock_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2755 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_each_statement: /* for_each_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2761 "src/parser.tab.c"
        break;

    case YYSYMBOL_do_loop_statement: /* do_loop_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2767 "src/parser.tab.c"
        break;

    case YYSYMBOL_while_statement: /* while_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2773 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement: /* consider_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2779 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_branch_list: /* consider_branch_list  */
#line 701 "src/parser.y"
            { ast_free_consider_branch_list(((*yyvaluep).consider_branch_list)); }
#line 2785 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_else_opt: /* consider_else_opt  */
#line 698 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2791 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement_list: /* consider_statement_list  */
#line 698 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2797 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_body_statement: /* consider_body_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2803 "src/parser.tab.c"
        break;

    case YYSYMBOL_function_statement: /* function_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2809 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_statement: /* modifier_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2815 "src/parser.tab.c"
        break;

    case YYSYMBOL_program_statement: /* program_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2821 "src/parser.tab.c"
        break;

    case YYSYMBOL_library_statement: /* library_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2827 "src/parser.tab.c"
        break;

    case YYSYMBOL_use_statement: /* use_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2833 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_signature: /* modifier_signature  */
#line 704 "src/parser.y"
            { ast_free_modifier_signature(((*yyvaluep).modifier_signature)); }
#line 2839 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_context: /* modifier_context  */
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2845 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_statement: /* watch_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2851 "src/parser.tab.c"
        break;

    case YYSYMBOL_unwatch_statement: /* unwatch_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2857 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_list: /* watch_target_list  */
#line 702 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 2863 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_path: /* watch_target_path  */
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2869 "src/parser.tab.c"
        break;

    case YYSYMBOL_without_watchers_statement: /* without_watchers_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2875 "src/parser.tab.c"
        break;

    case YYSYMBOL_on_error_statement: /* on_error_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2881 "src/parser.tab.c"
        break;

    case YYSYMBOL_error_statement: /* error_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2887 "src/parser.tab.c"
        break;

    case YYSYMBOL_return_statement: /* return_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2893 "src/parser.tab.c"
        break;

    case YYSYMBOL_label_statement: /* label_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2899 "src/parser.tab.c"
        break;

    case YYSYMBOL_goto_statement: /* goto_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2905 "src/parser.tab.c"
        break;

    case YYSYMBOL_gosub_statement: /* gosub_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2911 "src/parser.tab.c"
        break;

    case YYSYMBOL_break_statement: /* break_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2917 "src/parser.tab.c"
        break;

    case YYSYMBOL_continue_statement: /* continue_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2923 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_statement: /* if_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2929 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_block_tail: /* if_block_tail  */
#line 698 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2935 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_inline_tail: /* if_inline_tail  */
#line 698 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2941 "src/parser.tab.c"
        break;

    case YYSYMBOL_inline_statement: /* inline_statement  */
#line 697 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2947 "src/parser.tab.c"
        break;

    case YYSYMBOL_expression: /* expression  */
#line 696 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2953 "src/parser.tab.c"
        break;

    case YYSYMBOL_or_expression: /* or_expression  */
#line 696 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2959 "src/parser.tab.c"
        break;

    case YYSYMBOL_and_expression: /* and_expression  */
#line 696 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2965 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_expression: /* comparison_expression  */
#line 696 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2971 "src/parser.tab.c"
        break;

    case YYSYMBOL_additive_expression: /* additive_expression  */
#line 696 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2977 "src/parser.tab.c"
        break;

    case YYSYMBOL_multiplicative_expression: /* multiplicative_expression  */
#line 696 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2983 "src/parser.tab.c"
        break;

    case YYSYMBOL_unary_expression: /* unary_expression  */
#line 696 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2989 "src/parser.tab.c"
        break;

    case YYSYMBOL_postfix_expression: /* postfix_expression  */
#line 696 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2995 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_operator: /* comparison_operator  */
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3001 "src/parser.tab.c"
        break;

    case YYSYMBOL_primary: /* primary  */
#line 696 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3007 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_literal: /* record_literal  */
#line 696 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3013 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_suffix: /* ident_suffix  */
#line 705 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3019 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_dot_suffix: /* ident_dot_suffix  */
#line 705 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3025 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list_opt: /* argument_list_opt  */
#line 699 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3031 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list: /* argument_list  */
#line 699 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3037 "src/parser.tab.c"
        break;

    case YYSYMBOL_array_argument_list: /* array_argument_list  */
#line 699 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3043 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list_opt: /* parameter_list_opt  */
#line 702 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3049 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list: /* parameter_list  */
#line 702 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3055 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_name: /* field_name  */
#line 695 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3061 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_field_list: /* record_field_list  */
#line 700 "src/parser.y"
            { ast_free_record_field_list(((*yyvaluep).record_field_list)); }
#line 3067 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_policy: /* field_policy  */
#line 706 "src/parser.y"
            { ast_free_expr(((*yyvaluep).field_policy).reset_expr); }
#line 3073 "src/parser.tab.c"
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
#line 3379 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 725 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3385 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 726 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3391 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 727 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3397 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 731 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3403 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 732 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3409 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 733 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3415 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 734 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3421 "src/parser.tab.c"
    break;

  case 10: /* statement: for_each_statement  */
#line 735 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3427 "src/parser.tab.c"
    break;

  case 11: /* statement: while_statement  */
#line 736 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3433 "src/parser.tab.c"
    break;

  case 12: /* statement: do_loop_statement  */
#line 737 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3439 "src/parser.tab.c"
    break;

  case 13: /* statement: consider_statement  */
#line 738 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3445 "src/parser.tab.c"
    break;

  case 14: /* statement: function_statement  */
#line 739 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3451 "src/parser.tab.c"
    break;

  case 15: /* statement: modifier_statement  */
#line 740 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3457 "src/parser.tab.c"
    break;

  case 16: /* statement: program_statement  */
#line 741 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3463 "src/parser.tab.c"
    break;

  case 17: /* statement: library_statement  */
#line 742 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3469 "src/parser.tab.c"
    break;

  case 18: /* statement: use_statement NEWLINE  */
#line 743 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3475 "src/parser.tab.c"
    break;

  case 19: /* statement: watch_statement  */
#line 744 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3481 "src/parser.tab.c"
    break;

  case 20: /* statement: unwatch_statement NEWLINE  */
#line 745 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3487 "src/parser.tab.c"
    break;

  case 21: /* statement: without_watchers_statement  */
#line 746 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3493 "src/parser.tab.c"
    break;

  case 22: /* statement: on_error_statement NEWLINE  */
#line 747 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3499 "src/parser.tab.c"
    break;

  case 23: /* statement: error_statement NEWLINE  */
#line 748 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3505 "src/parser.tab.c"
    break;

  case 24: /* statement: return_statement NEWLINE  */
#line 749 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3511 "src/parser.tab.c"
    break;

  case 25: /* statement: label_statement NEWLINE  */
#line 750 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3517 "src/parser.tab.c"
    break;

  case 26: /* statement: goto_statement NEWLINE  */
#line 751 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3523 "src/parser.tab.c"
    break;

  case 27: /* statement: gosub_statement NEWLINE  */
#line 752 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3529 "src/parser.tab.c"
    break;

  case 28: /* statement: break_statement NEWLINE  */
#line 753 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3535 "src/parser.tab.c"
    break;

  case 29: /* statement: continue_statement NEWLINE  */
#line 754 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3541 "src/parser.tab.c"
    break;

  case 30: /* statement: if_statement  */
#line 755 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3547 "src/parser.tab.c"
    break;

  case 31: /* assignment: lvalue OP_EQ expression  */
#line 759 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 3553 "src/parser.tab.c"
    break;

  case 32: /* assignment: lvalue modifier OP_EQ expression  */
#line 760 "src/parser.y"
                                       {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 3567 "src/parser.tab.c"
    break;

  case 33: /* lvalue: variable_name  */
#line 772 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3573 "src/parser.tab.c"
    break;

  case 34: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 773 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3579 "src/parser.tab.c"
    break;

  case 35: /* lvalue: lvalue DOT IDENT  */
#line 774 "src/parser.y"
                                    { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3585 "src/parser.tab.c"
    break;

  case 36: /* variable_name: IDENT  */
#line 778 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 3591 "src/parser.tab.c"
    break;

  case 37: /* variable_name: END  */
#line 779 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 3597 "src/parser.tab.c"
    break;

  case 38: /* variable_name: NEXT  */
#line 780 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 3603 "src/parser.tab.c"
    break;

  case 39: /* variable_name: LOOP  */
#line 785 "src/parser.y"
                        { (yyval.text) = copy_const("loop"); }
#line 3609 "src/parser.tab.c"
    break;

  case 40: /* variable_name: UNTIL  */
#line 786 "src/parser.y"
                         { (yyval.text) = copy_const("until"); }
#line 3615 "src/parser.tab.c"
    break;

  case 41: /* modifier: MOD_LPAREN MOD_CONTENT  */
#line 790 "src/parser.y"
                             { (yyval.modifier) = parse_modifier_use((yyvsp[0].text)); }
#line 3621 "src/parser.tab.c"
    break;

  case 42: /* $@1: %empty  */
#line 794 "src/parser.y"
             { lexer_begin_lens_content(ctx->active_lexer); }
#line 3627 "src/parser.tab.c"
    break;

  case 43: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 794 "src/parser.y"
                                                                                  {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 3635 "src/parser.tab.c"
    break;

  case 44: /* modifier_name: modifier_word  */
#line 800 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 3641 "src/parser.tab.c"
    break;

  case 45: /* modifier_name: modifier_name modifier_word  */
#line 801 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 3647 "src/parser.tab.c"
    break;

  case 46: /* modifier_word: IDENT  */
#line 805 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 3653 "src/parser.tab.c"
    break;

  case 47: /* modifier_word: TO  */
#line 806 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 3659 "src/parser.tab.c"
    break;

  case 48: /* modifier_word: END  */
#line 807 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 3665 "src/parser.tab.c"
    break;

  case 49: /* modifier_word: NEXT  */
#line 808 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 3671 "src/parser.tab.c"
    break;

  case 50: /* print_statement: PRINT expression  */
#line 812 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 3677 "src/parser.tab.c"
    break;

  case 51: /* print_statement: PRINT TO ERROR_VALUE expression  */
#line 818 "src/parser.y"
                                      { (yyval.stmt) = ast_print_error((yyvsp[0].expr)); }
#line 3683 "src/parser.tab.c"
    break;

  case 52: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 822 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 3689 "src/parser.tab.c"
    break;

  case 53: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 823 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 3700 "src/parser.tab.c"
    break;

  case 54: /* call_statement: lvalue DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 829 "src/parser.y"
                                                       {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 3710 "src/parser.tab.c"
    break;

  case 55: /* call_statement: lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 834 "src/parser.y"
                                                                 {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 3724 "src/parser.tab.c"
    break;

  case 56: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 843 "src/parser.y"
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
#line 3739 "src/parser.tab.c"
    break;

  case 57: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 856 "src/parser.y"
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
#line 3756 "src/parser.tab.c"
    break;

  case 58: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list END FOR NEWLINE  */
#line 871 "src/parser.y"
                                                                     {
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 3764 "src/parser.tab.c"
    break;

  case 59: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list END FOR NEWLINE  */
#line 874 "src/parser.y"
                                                                          {
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 3772 "src/parser.tab.c"
    break;

  case 60: /* for_each_statement: FOR IDENT OP_EQ expression TO expression NEWLINE statement_list END FOR NEWLINE  */
#line 880 "src/parser.y"
                                                                                      {
        (yyval.stmt) = ast_for_range((yyvsp[-9].text), (yyvsp[-7].expr), (yyvsp[-5].expr), NULL, (yyvsp[-3].stmt_list));
      }
#line 3780 "src/parser.tab.c"
    break;

  case 61: /* for_each_statement: FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list END FOR NEWLINE  */
#line 883 "src/parser.y"
                                                                                                      {
        (yyval.stmt) = ast_for_range((yyvsp[-11].text), (yyvsp[-9].expr), (yyvsp[-7].expr), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 3788 "src/parser.tab.c"
    break;

  case 62: /* do_loop_statement: DO NEWLINE statement_list LOOP UNTIL expression NEWLINE  */
#line 891 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_do_loop((yyvsp[-4].stmt_list), (yyvsp[-1].expr), 1);
      }
#line 3796 "src/parser.tab.c"
    break;

  case 63: /* do_loop_statement: DO NEWLINE statement_list LOOP WHILE expression NEWLINE  */
#line 894 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_do_loop((yyvsp[-4].stmt_list), (yyvsp[-1].expr), 0);
      }
#line 3804 "src/parser.tab.c"
    break;

  case 64: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 900 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 3812 "src/parser.tab.c"
    break;

  case 65: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 906 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 3820 "src/parser.tab.c"
    break;

  case 66: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 912 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 3828 "src/parser.tab.c"
    break;

  case 67: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 915 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 3836 "src/parser.tab.c"
    break;

  case 68: /* consider_else_opt: %empty  */
#line 921 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3842 "src/parser.tab.c"
    break;

  case 69: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 922 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3848 "src/parser.tab.c"
    break;

  case 70: /* consider_statement_list: %empty  */
#line 926 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3854 "src/parser.tab.c"
    break;

  case 71: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 927 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3860 "src/parser.tab.c"
    break;

  case 72: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 928 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3866 "src/parser.tab.c"
    break;

  case 73: /* consider_body_statement: assignment NEWLINE  */
#line 932 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3872 "src/parser.tab.c"
    break;

  case 74: /* consider_body_statement: print_statement NEWLINE  */
#line 933 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3878 "src/parser.tab.c"
    break;

  case 75: /* consider_body_statement: call_statement NEWLINE  */
#line 934 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3884 "src/parser.tab.c"
    break;

  case 76: /* consider_body_statement: with_lock_statement  */
#line 935 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3890 "src/parser.tab.c"
    break;

  case 77: /* consider_body_statement: for_each_statement  */
#line 936 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3896 "src/parser.tab.c"
    break;

  case 78: /* consider_body_statement: while_statement  */
#line 937 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3902 "src/parser.tab.c"
    break;

  case 79: /* consider_body_statement: do_loop_statement  */
#line 938 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3908 "src/parser.tab.c"
    break;

  case 80: /* consider_body_statement: consider_statement  */
#line 939 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3914 "src/parser.tab.c"
    break;

  case 81: /* consider_body_statement: function_statement  */
#line 940 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3920 "src/parser.tab.c"
    break;

  case 82: /* consider_body_statement: modifier_statement  */
#line 941 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3926 "src/parser.tab.c"
    break;

  case 83: /* consider_body_statement: program_statement  */
#line 942 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3932 "src/parser.tab.c"
    break;

  case 84: /* consider_body_statement: library_statement  */
#line 943 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3938 "src/parser.tab.c"
    break;

  case 85: /* consider_body_statement: use_statement NEWLINE  */
#line 944 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3944 "src/parser.tab.c"
    break;

  case 86: /* consider_body_statement: watch_statement  */
#line 945 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3950 "src/parser.tab.c"
    break;

  case 87: /* consider_body_statement: unwatch_statement NEWLINE  */
#line 946 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3956 "src/parser.tab.c"
    break;

  case 88: /* consider_body_statement: without_watchers_statement  */
#line 947 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3962 "src/parser.tab.c"
    break;

  case 89: /* consider_body_statement: on_error_statement NEWLINE  */
#line 948 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3968 "src/parser.tab.c"
    break;

  case 90: /* consider_body_statement: error_statement NEWLINE  */
#line 949 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3974 "src/parser.tab.c"
    break;

  case 91: /* consider_body_statement: return_statement NEWLINE  */
#line 950 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3980 "src/parser.tab.c"
    break;

  case 92: /* consider_body_statement: label_statement NEWLINE  */
#line 951 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3986 "src/parser.tab.c"
    break;

  case 93: /* consider_body_statement: goto_statement NEWLINE  */
#line 952 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3992 "src/parser.tab.c"
    break;

  case 94: /* consider_body_statement: gosub_statement NEWLINE  */
#line 953 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3998 "src/parser.tab.c"
    break;

  case 95: /* consider_body_statement: break_statement NEWLINE  */
#line 954 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4004 "src/parser.tab.c"
    break;

  case 96: /* consider_body_statement: continue_statement NEWLINE  */
#line 955 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4010 "src/parser.tab.c"
    break;

  case 97: /* consider_body_statement: if_statement  */
#line 956 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4016 "src/parser.tab.c"
    break;

  case 98: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 960 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4024 "src/parser.tab.c"
    break;

  case 99: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 963 "src/parser.y"
                                                                                                            {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4033 "src/parser.tab.c"
    break;

  case 100: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 970 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 4041 "src/parser.tab.c"
    break;

  case 101: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 973 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 4049 "src/parser.tab.c"
    break;

  case 102: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 979 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4057 "src/parser.tab.c"
    break;

  case 103: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 985 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 4065 "src/parser.tab.c"
    break;

  case 104: /* use_statement: USE IDENT  */
#line 991 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4071 "src/parser.tab.c"
    break;

  case 105: /* use_statement: LOAD IDENT  */
#line 992 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4077 "src/parser.tab.c"
    break;

  case 106: /* use_statement: USE STRING  */
#line 993 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4083 "src/parser.tab.c"
    break;

  case 107: /* use_statement: LOAD STRING  */
#line 994 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4089 "src/parser.tab.c"
    break;

  case 108: /* use_statement: USE IDENT IDENT STRING  */
#line 995 "src/parser.y"
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
#line 4110 "src/parser.tab.c"
    break;

  case 109: /* use_statement: LOAD IDENT IDENT STRING  */
#line 1011 "src/parser.y"
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
#line 4131 "src/parser.tab.c"
    break;

  case 110: /* modifier_signature: modifier_name  */
#line 1030 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 4137 "src/parser.tab.c"
    break;

  case 111: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 1031 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 4143 "src/parser.tab.c"
    break;

  case 112: /* modifier_context: IDENT  */
#line 1035 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4149 "src/parser.tab.c"
    break;

  case 113: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1039 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4157 "src/parser.tab.c"
    break;

  case 114: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 1042 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 4165 "src/parser.tab.c"
    break;

  case 115: /* watch_statement: WATCH IDENT LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1050 "src/parser.y"
                                                                                           {
        (yyval.stmt) = ast_watch((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4173 "src/parser.tab.c"
    break;

  case 116: /* unwatch_statement: UNWATCH expression  */
#line 1056 "src/parser.y"
                         { (yyval.stmt) = ast_unwatch((yyvsp[0].expr)); }
#line 4179 "src/parser.tab.c"
    break;

  case 117: /* watch_target_list: watch_target_path  */
#line 1060 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4185 "src/parser.tab.c"
    break;

  case 118: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 1061 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4191 "src/parser.tab.c"
    break;

  case 119: /* watch_target_path: variable_name  */
#line 1065 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 4197 "src/parser.tab.c"
    break;

  case 120: /* watch_target_path: watch_target_path DOT IDENT  */
#line 1066 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 4203 "src/parser.tab.c"
    break;

  case 121: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 1070 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 4211 "src/parser.tab.c"
    break;

  case 122: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 1076 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 4217 "src/parser.tab.c"
    break;

  case 123: /* on_error_statement: ON ERROR_VALUE RESUME NEXT  */
#line 1077 "src/parser.y"
                                 { (yyval.stmt) = ast_on_error_resume_next(); }
#line 4223 "src/parser.tab.c"
    break;

  case 124: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 1078 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 4229 "src/parser.tab.c"
    break;

  case 125: /* error_statement: ERROR_VALUE expression  */
#line 1082 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 4235 "src/parser.tab.c"
    break;

  case 126: /* return_statement: RETURN  */
#line 1086 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 4241 "src/parser.tab.c"
    break;

  case 127: /* return_statement: RETURN expression  */
#line 1087 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 4247 "src/parser.tab.c"
    break;

  case 128: /* label_statement: variable_name COLON  */
#line 1091 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 4253 "src/parser.tab.c"
    break;

  case 129: /* goto_statement: GOTO variable_name  */
#line 1098 "src/parser.y"
                         { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 4259 "src/parser.tab.c"
    break;

  case 130: /* gosub_statement: GOSUB variable_name  */
#line 1102 "src/parser.y"
                          { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 4265 "src/parser.tab.c"
    break;

  case 131: /* break_statement: BREAK  */
#line 1106 "src/parser.y"
            { (yyval.stmt) = ast_break(); }
#line 4271 "src/parser.tab.c"
    break;

  case 132: /* continue_statement: CONTINUE  */
#line 1110 "src/parser.y"
               { (yyval.stmt) = ast_continue(); }
#line 4277 "src/parser.tab.c"
    break;

  case 133: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1114 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4286 "src/parser.tab.c"
    break;

  case 134: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1118 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4295 "src/parser.tab.c"
    break;

  case 135: /* if_block_tail: END IF NEWLINE  */
#line 1125 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4303 "src/parser.tab.c"
    break;

  case 136: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 1128 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4311 "src/parser.tab.c"
    break;

  case 137: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1131 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4319 "src/parser.tab.c"
    break;

  case 138: /* if_inline_tail: %empty  */
#line 1137 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4327 "src/parser.tab.c"
    break;

  case 139: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 1140 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4335 "src/parser.tab.c"
    break;

  case 140: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1143 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4343 "src/parser.tab.c"
    break;

  case 141: /* inline_statement: assignment  */
#line 1149 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4349 "src/parser.tab.c"
    break;

  case 142: /* inline_statement: print_statement  */
#line 1150 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4355 "src/parser.tab.c"
    break;

  case 143: /* inline_statement: call_statement  */
#line 1151 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4361 "src/parser.tab.c"
    break;

  case 144: /* inline_statement: use_statement  */
#line 1152 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4367 "src/parser.tab.c"
    break;

  case 145: /* inline_statement: on_error_statement  */
#line 1153 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4373 "src/parser.tab.c"
    break;

  case 146: /* inline_statement: error_statement  */
#line 1154 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4379 "src/parser.tab.c"
    break;

  case 147: /* inline_statement: return_statement  */
#line 1155 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4385 "src/parser.tab.c"
    break;

  case 148: /* inline_statement: goto_statement  */
#line 1156 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4391 "src/parser.tab.c"
    break;

  case 149: /* inline_statement: gosub_statement  */
#line 1157 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4397 "src/parser.tab.c"
    break;

  case 150: /* inline_statement: break_statement  */
#line 1158 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4403 "src/parser.tab.c"
    break;

  case 151: /* inline_statement: continue_statement  */
#line 1159 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4409 "src/parser.tab.c"
    break;

  case 152: /* expression: or_expression  */
#line 1163 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 4415 "src/parser.tab.c"
    break;

  case 153: /* or_expression: and_expression  */
#line 1167 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 4421 "src/parser.tab.c"
    break;

  case 154: /* or_expression: or_expression OR and_expression  */
#line 1168 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4427 "src/parser.tab.c"
    break;

  case 155: /* and_expression: comparison_expression  */
#line 1172 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 4433 "src/parser.tab.c"
    break;

  case 156: /* and_expression: and_expression AND comparison_expression  */
#line 1173 "src/parser.y"
                                               { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4439 "src/parser.tab.c"
    break;

  case 157: /* comparison_expression: additive_expression  */
#line 1177 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 4445 "src/parser.tab.c"
    break;

  case 158: /* comparison_expression: additive_expression comparison_operator additive_expression  */
#line 1178 "src/parser.y"
                                                                  { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4451 "src/parser.tab.c"
    break;

  case 159: /* comparison_expression: additive_expression comparison_lens comparison_operator additive_expression  */
#line 1179 "src/parser.y"
                                                                                  {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 4459 "src/parser.tab.c"
    break;

  case 160: /* comparison_expression: additive_expression modifier comparison_operator additive_expression  */
#line 1182 "src/parser.y"
                                                                           {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 4473 "src/parser.tab.c"
    break;

  case 161: /* additive_expression: multiplicative_expression  */
#line 1194 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 4479 "src/parser.tab.c"
    break;

  case 162: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 1195 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4485 "src/parser.tab.c"
    break;

  case 163: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 1196 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4491 "src/parser.tab.c"
    break;

  case 164: /* multiplicative_expression: unary_expression  */
#line 1200 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 4497 "src/parser.tab.c"
    break;

  case 165: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 1201 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4503 "src/parser.tab.c"
    break;

  case 166: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 1202 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4509 "src/parser.tab.c"
    break;

  case 167: /* unary_expression: postfix_expression  */
#line 1206 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 4515 "src/parser.tab.c"
    break;

  case 168: /* unary_expression: NOT unary_expression  */
#line 1207 "src/parser.y"
                           { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4521 "src/parser.tab.c"
    break;

  case 169: /* unary_expression: MINUS unary_expression  */
#line 1208 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4527 "src/parser.tab.c"
    break;

  case 170: /* unary_expression: NEW postfix_expression  */
#line 1209 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4533 "src/parser.tab.c"
    break;

  case 171: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 1210 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 4539 "src/parser.tab.c"
    break;

  case 172: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 1211 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4545 "src/parser.tab.c"
    break;

  case 173: /* postfix_expression: primary  */
#line 1215 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 4551 "src/parser.tab.c"
    break;

  case 174: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 1216 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4557 "src/parser.tab.c"
    break;

  case 175: /* postfix_expression: postfix_expression DOT IDENT  */
#line 1217 "src/parser.y"
                                   { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4563 "src/parser.tab.c"
    break;

  case 176: /* postfix_expression: postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1218 "src/parser.y"
                                                                   {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        (yyval.expr) = expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 4573 "src/parser.tab.c"
    break;

  case 177: /* postfix_expression: postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1223 "src/parser.y"
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
#line 4589 "src/parser.tab.c"
    break;

  case 178: /* comparison_operator: OP_EQ  */
#line 1237 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 4595 "src/parser.tab.c"
    break;

  case 179: /* comparison_operator: OP_NE  */
#line 1238 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 4601 "src/parser.tab.c"
    break;

  case 180: /* comparison_operator: OP_GT  */
#line 1239 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 4607 "src/parser.tab.c"
    break;

  case 181: /* comparison_operator: OP_LT  */
#line 1240 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 4613 "src/parser.tab.c"
    break;

  case 182: /* comparison_operator: OP_GE  */
#line 1241 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 4619 "src/parser.tab.c"
    break;

  case 183: /* comparison_operator: OP_LE  */
#line 1242 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 4625 "src/parser.tab.c"
    break;

  case 184: /* comparison_operator: OP_NGT  */
#line 1243 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 4631 "src/parser.tab.c"
    break;

  case 185: /* comparison_operator: OP_NLT  */
#line 1244 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 4637 "src/parser.tab.c"
    break;

  case 186: /* comparison_operator: OP_NGE  */
#line 1245 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 4643 "src/parser.tab.c"
    break;

  case 187: /* comparison_operator: OP_NLE  */
#line 1246 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 4649 "src/parser.tab.c"
    break;

  case 188: /* primary: NUMBER  */
#line 1250 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4655 "src/parser.tab.c"
    break;

  case 189: /* primary: WATCHERS LPAREN RPAREN  */
#line 1251 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_call(copy_const("watchers"), ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4661 "src/parser.tab.c"
    break;

  case 190: /* primary: duration_terms  */
#line 1252 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4667 "src/parser.tab.c"
    break;

  case 191: /* primary: STRING  */
#line 1253 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4673 "src/parser.tab.c"
    break;

  case 192: /* primary: variable_name ident_suffix  */
#line 1254 "src/parser.y"
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
#line 4695 "src/parser.tab.c"
    break;

  case 193: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1271 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 4706 "src/parser.tab.c"
    break;

  case 194: /* primary: ERROR_VALUE  */
#line 1277 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4712 "src/parser.tab.c"
    break;

  case 195: /* primary: TRUE  */
#line 1278 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4718 "src/parser.tab.c"
    break;

  case 196: /* primary: FALSE  */
#line 1279 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4724 "src/parser.tab.c"
    break;

  case 197: /* primary: NOTHING  */
#line 1280 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4730 "src/parser.tab.c"
    break;

  case 198: /* primary: UNKNOWN_VALUE  */
#line 1281 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4736 "src/parser.tab.c"
    break;

  case 199: /* primary: LPAREN expression RPAREN  */
#line 1282 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 4742 "src/parser.tab.c"
    break;

  case 200: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 1283 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4748 "src/parser.tab.c"
    break;

  case 201: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 1284 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4754 "src/parser.tab.c"
    break;

  case 202: /* primary: record_literal  */
#line 1285 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 4760 "src/parser.tab.c"
    break;

  case 203: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 1289 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4766 "src/parser.tab.c"
    break;

  case 204: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 1290 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4772 "src/parser.tab.c"
    break;

  case 205: /* ident_suffix: %empty  */
#line 1294 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 4782 "src/parser.tab.c"
    break;

  case 206: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 1299 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 4792 "src/parser.tab.c"
    break;

  case 207: /* ident_suffix: DOT IDENT ident_dot_suffix  */
#line 1304 "src/parser.y"
                                 {
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 4801 "src/parser.tab.c"
    break;

  case 208: /* ident_suffix: DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1308 "src/parser.y"
                                                          {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        (yyval.ident_suffix).kind = IDENT_SUFFIX_METHOD;
        (yyval.ident_suffix).name = (yyvsp[-3].text);
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 4815 "src/parser.tab.c"
    break;

  case 209: /* ident_dot_suffix: %empty  */
#line 1320 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 4825 "src/parser.tab.c"
    break;

  case 210: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 1325 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 4835 "src/parser.tab.c"
    break;

  case 211: /* duration_terms: NUMBER IDENT  */
#line 1333 "src/parser.y"
                   {
        AstDuration duration = {0};
        (yyval.duration) = duration_add_unit(duration, (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 4844 "src/parser.tab.c"
    break;

  case 212: /* duration_terms: duration_terms NUMBER IDENT  */
#line 1337 "src/parser.y"
                                  {
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 4852 "src/parser.tab.c"
    break;

  case 213: /* argument_list_opt: %empty  */
#line 1343 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 4858 "src/parser.tab.c"
    break;

  case 214: /* argument_list_opt: argument_list  */
#line 1344 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 4864 "src/parser.tab.c"
    break;

  case 215: /* argument_list: expression  */
#line 1348 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 4870 "src/parser.tab.c"
    break;

  case 216: /* argument_list: argument_list COMMA expression  */
#line 1349 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 4876 "src/parser.tab.c"
    break;

  case 217: /* array_argument_list: expression  */
#line 1353 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 4882 "src/parser.tab.c"
    break;

  case 218: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 1354 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 4888 "src/parser.tab.c"
    break;

  case 219: /* parameter_list_opt: %empty  */
#line 1358 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 4894 "src/parser.tab.c"
    break;

  case 220: /* parameter_list_opt: parameter_list  */
#line 1359 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 4900 "src/parser.tab.c"
    break;

  case 221: /* parameter_list: IDENT  */
#line 1363 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4906 "src/parser.tab.c"
    break;

  case 222: /* parameter_list: parameter_list COMMA IDENT  */
#line 1364 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4912 "src/parser.tab.c"
    break;

  case 223: /* field_name: IDENT  */
#line 1377 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4918 "src/parser.tab.c"
    break;

  case 224: /* field_name: STRING  */
#line 1382 "src/parser.y"
             { (yyval.text) = (yyvsp[0].text); }
#line 4924 "src/parser.tab.c"
    break;

  case 225: /* field_name: AS  */
#line 1383 "src/parser.y"
                     { (yyval.text) = kw_name("as"); }
#line 4930 "src/parser.tab.c"
    break;

  case 226: /* field_name: NEXT  */
#line 1384 "src/parser.y"
                     { (yyval.text) = kw_name("next"); }
#line 4936 "src/parser.tab.c"
    break;

  case 227: /* field_name: STOP  */
#line 1385 "src/parser.y"
                     { (yyval.text) = kw_name("stop"); }
#line 4942 "src/parser.tab.c"
    break;

  case 228: /* field_name: ERROR_VALUE  */
#line 1386 "src/parser.y"
                     { (yyval.text) = kw_name("error"); }
#line 4948 "src/parser.tab.c"
    break;

  case 229: /* field_name: END  */
#line 1387 "src/parser.y"
                     { (yyval.text) = kw_name("end"); }
#line 4954 "src/parser.tab.c"
    break;

  case 230: /* field_name: TO  */
#line 1388 "src/parser.y"
                     { (yyval.text) = kw_name("to"); }
#line 4960 "src/parser.tab.c"
    break;

  case 231: /* field_name: IN  */
#line 1389 "src/parser.y"
                     { (yyval.text) = kw_name("in"); }
#line 4966 "src/parser.tab.c"
    break;

  case 232: /* field_name: ON  */
#line 1390 "src/parser.y"
                     { (yyval.text) = kw_name("on"); }
#line 4972 "src/parser.tab.c"
    break;

  case 233: /* field_name: NEW  */
#line 1391 "src/parser.y"
                     { (yyval.text) = kw_name("new"); }
#line 4978 "src/parser.tab.c"
    break;

  case 234: /* field_name: EACH  */
#line 1392 "src/parser.y"
                     { (yyval.text) = kw_name("each"); }
#line 4984 "src/parser.tab.c"
    break;

  case 235: /* field_name: WITH  */
#line 1393 "src/parser.y"
                     { (yyval.text) = kw_name("with"); }
#line 4990 "src/parser.tab.c"
    break;

  case 236: /* field_name: WITHOUT  */
#line 1394 "src/parser.y"
                     { (yyval.text) = kw_name("without"); }
#line 4996 "src/parser.tab.c"
    break;

  case 237: /* field_name: THEN  */
#line 1395 "src/parser.y"
                     { (yyval.text) = kw_name("then"); }
#line 5002 "src/parser.tab.c"
    break;

  case 238: /* field_name: ELSE  */
#line 1396 "src/parser.y"
                     { (yyval.text) = kw_name("else"); }
#line 5008 "src/parser.tab.c"
    break;

  case 239: /* field_name: FOR  */
#line 1397 "src/parser.y"
                     { (yyval.text) = kw_name("for"); }
#line 5014 "src/parser.tab.c"
    break;

  case 240: /* field_name: IF  */
#line 1398 "src/parser.y"
                     { (yyval.text) = kw_name("if"); }
#line 5020 "src/parser.tab.c"
    break;

  case 241: /* field_name: WHILE  */
#line 1399 "src/parser.y"
                     { (yyval.text) = kw_name("while"); }
#line 5026 "src/parser.tab.c"
    break;

  case 242: /* field_name: DO  */
#line 1400 "src/parser.y"
                     { (yyval.text) = kw_name("do"); }
#line 5032 "src/parser.tab.c"
    break;

  case 243: /* field_name: LOOP  */
#line 1401 "src/parser.y"
                     { (yyval.text) = kw_name("loop"); }
#line 5038 "src/parser.tab.c"
    break;

  case 244: /* field_name: UNTIL  */
#line 1402 "src/parser.y"
                     { (yyval.text) = kw_name("until"); }
#line 5044 "src/parser.tab.c"
    break;

  case 245: /* field_name: PRINT  */
#line 1403 "src/parser.y"
                     { (yyval.text) = kw_name("print"); }
#line 5050 "src/parser.tab.c"
    break;

  case 246: /* field_name: RETURN  */
#line 1404 "src/parser.y"
                     { (yyval.text) = kw_name("return"); }
#line 5056 "src/parser.tab.c"
    break;

  case 247: /* field_name: LOAD  */
#line 1405 "src/parser.y"
                     { (yyval.text) = kw_name("load"); }
#line 5062 "src/parser.tab.c"
    break;

  case 248: /* field_name: USE  */
#line 1406 "src/parser.y"
                     { (yyval.text) = kw_name("use"); }
#line 5068 "src/parser.tab.c"
    break;

  case 249: /* field_name: NOT  */
#line 1407 "src/parser.y"
                     { (yyval.text) = kw_name("not"); }
#line 5074 "src/parser.tab.c"
    break;

  case 250: /* field_name: AND  */
#line 1408 "src/parser.y"
                     { (yyval.text) = kw_name("and"); }
#line 5080 "src/parser.tab.c"
    break;

  case 251: /* field_name: OR  */
#line 1409 "src/parser.y"
                     { (yyval.text) = kw_name("or"); }
#line 5086 "src/parser.tab.c"
    break;

  case 252: /* field_name: TRUE  */
#line 1410 "src/parser.y"
                     { (yyval.text) = kw_name("true"); }
#line 5092 "src/parser.tab.c"
    break;

  case 253: /* field_name: FALSE  */
#line 1411 "src/parser.y"
                     { (yyval.text) = kw_name("false"); }
#line 5098 "src/parser.tab.c"
    break;

  case 254: /* field_name: NOTHING  */
#line 1412 "src/parser.y"
                     { (yyval.text) = kw_name("nothing"); }
#line 5104 "src/parser.tab.c"
    break;

  case 255: /* field_name: BREAK  */
#line 1413 "src/parser.y"
                     { (yyval.text) = kw_name("break"); }
#line 5110 "src/parser.tab.c"
    break;

  case 256: /* field_name: CONTINUE  */
#line 1414 "src/parser.y"
                     { (yyval.text) = kw_name("continue"); }
#line 5116 "src/parser.tab.c"
    break;

  case 257: /* field_name: GOTO  */
#line 1415 "src/parser.y"
                     { (yyval.text) = kw_name("goto"); }
#line 5122 "src/parser.tab.c"
    break;

  case 258: /* field_name: GOSUB  */
#line 1416 "src/parser.y"
                     { (yyval.text) = kw_name("gosub"); }
#line 5128 "src/parser.tab.c"
    break;

  case 259: /* field_name: SPAWN  */
#line 1417 "src/parser.y"
                     { (yyval.text) = kw_name("spawn"); }
#line 5134 "src/parser.tab.c"
    break;

  case 260: /* field_name: EXPORT  */
#line 1418 "src/parser.y"
                     { (yyval.text) = kw_name("export"); }
#line 5140 "src/parser.tab.c"
    break;

  case 261: /* field_name: LIBRARY  */
#line 1419 "src/parser.y"
                     { (yyval.text) = kw_name("library"); }
#line 5146 "src/parser.tab.c"
    break;

  case 262: /* field_name: FUNCTION  */
#line 1420 "src/parser.y"
                     { (yyval.text) = kw_name("function"); }
#line 5152 "src/parser.tab.c"
    break;

  case 263: /* field_name: MODIFIER  */
#line 1421 "src/parser.y"
                     { (yyval.text) = kw_name("modifier"); }
#line 5158 "src/parser.tab.c"
    break;

  case 264: /* field_name: PROGRAM  */
#line 1422 "src/parser.y"
                     { (yyval.text) = kw_name("program"); }
#line 5164 "src/parser.tab.c"
    break;

  case 265: /* field_name: RESUME  */
#line 1423 "src/parser.y"
                     { (yyval.text) = kw_name("resume"); }
#line 5170 "src/parser.tab.c"
    break;

  case 266: /* field_name: WATCH  */
#line 1424 "src/parser.y"
                     { (yyval.text) = kw_name("watch"); }
#line 5176 "src/parser.tab.c"
    break;

  case 267: /* field_name: WATCHERS  */
#line 1425 "src/parser.y"
                     { (yyval.text) = kw_name("watchers"); }
#line 5182 "src/parser.tab.c"
    break;

  case 268: /* field_name: CONSIDER  */
#line 1426 "src/parser.y"
                     { (yyval.text) = kw_name("consider"); }
#line 5188 "src/parser.tab.c"
    break;

  case 269: /* record_field_list: field_name OP_EQ expression  */
#line 1430 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5194 "src/parser.tab.c"
    break;

  case 270: /* record_field_list: field_name COLON expression  */
#line 1431 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5200 "src/parser.tab.c"
    break;

  case 271: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1432 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5206 "src/parser.tab.c"
    break;

  case 272: /* record_field_list: record_field_list COMMA optional_newlines field_name OP_EQ expression  */
#line 1433 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5212 "src/parser.tab.c"
    break;

  case 273: /* record_field_list: record_field_list COMMA optional_newlines field_name COLON expression  */
#line 1434 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5218 "src/parser.tab.c"
    break;

  case 274: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1435 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5224 "src/parser.tab.c"
    break;

  case 275: /* field_policy: IDENT  */
#line 1443 "src/parser.y"
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
#line 5256 "src/parser.tab.c"
    break;

  case 276: /* field_policy: IDENT expression  */
#line 1470 "src/parser.y"
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
#line 5277 "src/parser.tab.c"
    break;


#line 5281 "src/parser.tab.c"

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

#line 1493 "src/parser.y"


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
