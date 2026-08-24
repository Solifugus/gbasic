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


#line 701 "src/parser.tab.c"

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
  YYSYMBOL_NEXT = 49,                      /* NEXT  */
  YYSYMBOL_STOP = 50,                      /* STOP  */
  YYSYMBOL_ERROR_VALUE = 51,               /* ERROR_VALUE  */
  YYSYMBOL_MODIFIER = 52,                  /* MODIFIER  */
  YYSYMBOL_PROGRAM = 53,                   /* PROGRAM  */
  YYSYMBOL_LIBRARY = 54,                   /* LIBRARY  */
  YYSYMBOL_LOAD = 55,                      /* LOAD  */
  YYSYMBOL_USE = 56,                       /* USE  */
  YYSYMBOL_EXPORT = 57,                    /* EXPORT  */
  YYSYMBOL_OP_EQ = 58,                     /* OP_EQ  */
  YYSYMBOL_OP_NE = 59,                     /* OP_NE  */
  YYSYMBOL_OP_GT = 60,                     /* OP_GT  */
  YYSYMBOL_OP_LT = 61,                     /* OP_LT  */
  YYSYMBOL_OP_GE = 62,                     /* OP_GE  */
  YYSYMBOL_OP_LE = 63,                     /* OP_LE  */
  YYSYMBOL_OP_NGT = 64,                    /* OP_NGT  */
  YYSYMBOL_OP_NLT = 65,                    /* OP_NLT  */
  YYSYMBOL_OP_NGE = 66,                    /* OP_NGE  */
  YYSYMBOL_OP_NLE = 67,                    /* OP_NLE  */
  YYSYMBOL_PLUS = 68,                      /* PLUS  */
  YYSYMBOL_MINUS = 69,                     /* MINUS  */
  YYSYMBOL_STAR = 70,                      /* STAR  */
  YYSYMBOL_SLASH = 71,                     /* SLASH  */
  YYSYMBOL_LPAREN = 72,                    /* LPAREN  */
  YYSYMBOL_MOD_LPAREN = 73,                /* MOD_LPAREN  */
  YYSYMBOL_RPAREN = 74,                    /* RPAREN  */
  YYSYMBOL_LBRACKET = 75,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 76,                  /* RBRACKET  */
  YYSYMBOL_LBRACE = 77,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 78,                    /* RBRACE  */
  YYSYMBOL_COMMA = 79,                     /* COMMA  */
  YYSYMBOL_COLON = 80,                     /* COLON  */
  YYSYMBOL_NEWLINE = 81,                   /* NEWLINE  */
  YYSYMBOL_IF_WITHOUT_ELSE = 82,           /* IF_WITHOUT_ELSE  */
  YYSYMBOL_NO_DOT = 83,                    /* NO_DOT  */
  YYSYMBOL_DOT = 84,                       /* DOT  */
  YYSYMBOL_YYACCEPT = 85,                  /* $accept  */
  YYSYMBOL_program = 86,                   /* program  */
  YYSYMBOL_statement_list = 87,            /* statement_list  */
  YYSYMBOL_statement = 88,                 /* statement  */
  YYSYMBOL_assignment = 89,                /* assignment  */
  YYSYMBOL_lvalue = 90,                    /* lvalue  */
  YYSYMBOL_variable_name = 91,             /* variable_name  */
  YYSYMBOL_modifier = 92,                  /* modifier  */
  YYSYMBOL_comparison_lens = 93,           /* comparison_lens  */
  YYSYMBOL_94_1 = 94,                      /* $@1  */
  YYSYMBOL_modifier_name = 95,             /* modifier_name  */
  YYSYMBOL_modifier_word = 96,             /* modifier_word  */
  YYSYMBOL_print_statement = 97,           /* print_statement  */
  YYSYMBOL_call_statement = 98,            /* call_statement  */
  YYSYMBOL_with_lock_statement = 99,       /* with_lock_statement  */
  YYSYMBOL_for_each_statement = 100,       /* for_each_statement  */
  YYSYMBOL_do_loop_statement = 101,        /* do_loop_statement  */
  YYSYMBOL_while_statement = 102,          /* while_statement  */
  YYSYMBOL_consider_statement = 103,       /* consider_statement  */
  YYSYMBOL_consider_branch_list = 104,     /* consider_branch_list  */
  YYSYMBOL_consider_else_opt = 105,        /* consider_else_opt  */
  YYSYMBOL_consider_statement_list = 106,  /* consider_statement_list  */
  YYSYMBOL_consider_body_statement = 107,  /* consider_body_statement  */
  YYSYMBOL_function_statement = 108,       /* function_statement  */
  YYSYMBOL_modifier_statement = 109,       /* modifier_statement  */
  YYSYMBOL_program_statement = 110,        /* program_statement  */
  YYSYMBOL_library_statement = 111,        /* library_statement  */
  YYSYMBOL_use_statement = 112,            /* use_statement  */
  YYSYMBOL_modifier_signature = 113,       /* modifier_signature  */
  YYSYMBOL_modifier_context = 114,         /* modifier_context  */
  YYSYMBOL_watch_statement = 115,          /* watch_statement  */
  YYSYMBOL_unwatch_statement = 116,        /* unwatch_statement  */
  YYSYMBOL_watch_target_list = 117,        /* watch_target_list  */
  YYSYMBOL_server_statement = 118,         /* server_statement  */
  YYSYMBOL_server_item_list = 119,         /* server_item_list  */
  YYSYMBOL_server_item = 120,              /* server_item  */
  YYSYMBOL_server_string_list = 121,       /* server_string_list  */
  YYSYMBOL_watch_target_path = 122,        /* watch_target_path  */
  YYSYMBOL_without_watchers_statement = 123, /* without_watchers_statement  */
  YYSYMBOL_on_error_statement = 124,       /* on_error_statement  */
  YYSYMBOL_error_statement = 125,          /* error_statement  */
  YYSYMBOL_return_statement = 126,         /* return_statement  */
  YYSYMBOL_label_statement = 127,          /* label_statement  */
  YYSYMBOL_goto_statement = 128,           /* goto_statement  */
  YYSYMBOL_gosub_statement = 129,          /* gosub_statement  */
  YYSYMBOL_break_statement = 130,          /* break_statement  */
  YYSYMBOL_continue_statement = 131,       /* continue_statement  */
  YYSYMBOL_if_statement = 132,             /* if_statement  */
  YYSYMBOL_if_block_tail = 133,            /* if_block_tail  */
  YYSYMBOL_if_inline_tail = 134,           /* if_inline_tail  */
  YYSYMBOL_inline_statement = 135,         /* inline_statement  */
  YYSYMBOL_expression = 136,               /* expression  */
  YYSYMBOL_or_expression = 137,            /* or_expression  */
  YYSYMBOL_and_expression = 138,           /* and_expression  */
  YYSYMBOL_comparison_expression = 139,    /* comparison_expression  */
  YYSYMBOL_additive_expression = 140,      /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 141, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 142,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 143,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 144,      /* comparison_operator  */
  YYSYMBOL_primary = 145,                  /* primary  */
  YYSYMBOL_record_literal = 146,           /* record_literal  */
  YYSYMBOL_ident_suffix = 147,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 148,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 149,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 150,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 151,            /* argument_list  */
  YYSYMBOL_array_argument_list = 152,      /* array_argument_list  */
  YYSYMBOL_parameter_list_opt = 153,       /* parameter_list_opt  */
  YYSYMBOL_parameter_list = 154,           /* parameter_list  */
  YYSYMBOL_field_name = 155,               /* field_name  */
  YYSYMBOL_dot_field_name = 156,           /* dot_field_name  */
  YYSYMBOL_record_field_list = 157,        /* record_field_list  */
  YYSYMBOL_field_policy = 158,             /* field_policy  */
  YYSYMBOL_optional_newlines = 159         /* optional_newlines  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;



/* Unqualified %code blocks.  */
#line 697 "src/parser.y"

static int yylex(YYSTYPE *lvalp, YYLTYPE *llocp, gb_parse_ctx *ctx);
static void yyerror(YYLTYPE *llocp, gb_parse_ctx *ctx, const char *message);
static void report_syntax_error(gb_parse_ctx *ctx, int line, int column,
                                int end_line, int end_column, const char *message);

#line 902 "src/parser.tab.c"

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
#define YYLAST   2644

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  85
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  75
/* YYNRULES -- Number of rules.  */
#define YYNRULES  299
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  649

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   339


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
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   760,   760,   764,   765,   766,   770,   771,   772,   773,
     774,   775,   776,   777,   778,   779,   780,   781,   782,   783,
     784,   785,   786,   787,   788,   789,   790,   791,   792,   793,
     794,   795,   799,   800,   812,   813,   814,   818,   819,   820,
     825,   826,   830,   834,   834,   840,   841,   845,   846,   847,
     848,   852,   858,   862,   863,   869,   874,   883,   896,   911,
     914,   920,   923,   931,   934,   940,   946,   952,   955,   961,
     962,   966,   967,   968,   972,   973,   974,   975,   976,   977,
     978,   979,   980,   981,   982,   983,   984,   985,   986,   987,
     988,   989,   990,   991,   992,   993,   994,   995,   996,  1000,
    1003,  1010,  1013,  1019,  1025,  1031,  1032,  1033,  1034,  1035,
    1051,  1070,  1071,  1075,  1079,  1082,  1090,  1096,  1100,  1101,
    1120,  1123,  1129,  1130,  1131,  1135,  1138,  1141,  1144,  1147,
    1153,  1154,  1158,  1159,  1163,  1169,  1170,  1171,  1172,  1177,
    1190,  1195,  1200,  1210,  1214,  1215,  1219,  1226,  1230,  1234,
    1238,  1242,  1246,  1253,  1256,  1259,  1265,  1268,  1271,  1277,
    1278,  1279,  1280,  1281,  1282,  1283,  1284,  1285,  1286,  1287,
    1291,  1295,  1296,  1300,  1301,  1305,  1306,  1307,  1310,  1322,
    1323,  1324,  1328,  1329,  1330,  1334,  1335,  1336,  1337,  1338,
    1339,  1343,  1344,  1345,  1346,  1351,  1365,  1366,  1367,  1368,
    1369,  1370,  1371,  1372,  1373,  1374,  1378,  1379,  1380,  1381,
    1382,  1399,  1405,  1406,  1407,  1408,  1409,  1410,  1411,  1412,
    1413,  1417,  1418,  1422,  1427,  1432,  1438,  1450,  1455,  1463,
    1467,  1473,  1474,  1478,  1479,  1483,  1484,  1488,  1489,  1493,
    1494,  1507,  1514,  1523,  1524,  1525,  1526,  1527,  1528,  1529,
    1530,  1531,  1532,  1533,  1534,  1535,  1536,  1537,  1538,  1539,
    1540,  1541,  1542,  1543,  1544,  1545,  1546,  1547,  1548,  1549,
    1550,  1551,  1552,  1553,  1554,  1555,  1556,  1557,  1558,  1559,
    1560,  1561,  1562,  1563,  1564,  1565,  1566,  1567,  1568,  1569,
    1573,  1574,  1575,  1576,  1577,  1578,  1586,  1613,  1632,  1633
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
  "GOTO", "GOSUB", "WATCH", "UNWATCH", "WITHOUT", "WATCHERS", "ON", "NEXT",
  "STOP", "ERROR_VALUE", "MODIFIER", "PROGRAM", "LIBRARY", "LOAD", "USE",
  "EXPORT", "OP_EQ", "OP_NE", "OP_GT", "OP_LT", "OP_GE", "OP_LE", "OP_NGT",
  "OP_NLT", "OP_NGE", "OP_NLE", "PLUS", "MINUS", "STAR", "SLASH", "LPAREN",
  "MOD_LPAREN", "RPAREN", "LBRACKET", "RBRACKET", "LBRACE", "RBRACE",
  "COMMA", "COLON", "NEWLINE", "IF_WITHOUT_ELSE", "NO_DOT", "DOT",
  "$accept", "program", "statement_list", "statement", "assignment",
  "lvalue", "variable_name", "modifier", "comparison_lens", "$@1",
  "modifier_name", "modifier_word", "print_statement", "call_statement",
  "with_lock_statement", "for_each_statement", "do_loop_statement",
  "while_statement", "consider_statement", "consider_branch_list",
  "consider_else_opt", "consider_statement_list",
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
    -492,    13,   872,  -492,     2,   -49,  2264,  -492,  2226,    94,
      69,    24,  -492,  -492,  2264,  2264,  -492,  -492,   145,  2264,
      80,    80,    78,  2264,    72,    18,  -492,   157,   144,   128,
     188,   147,   254,   142,  -492,  -492,   120,   113,   141,   130,
     150,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,
     156,  -492,   159,  -492,  -492,   174,   179,   187,   195,   198,
     201,   203,   204,  -492,   217,  2264,  2264,   269,  -492,  -492,
     220,  -492,  -492,  -492,  -492,  2264,  2302,   290,   223,  -492,
    2264,  2264,  -492,  -492,     3,   285,   275,   279,  -492,   184,
     192,  -492,    95,  -492,  -492,   299,   252,  -492,   232,    96,
     302,  -492,   227,   228,   238,   242,  -492,  -492,  -492,   247,
      80,  -492,     9,   239,  -492,   241,   124,    30,   320,  -492,
    -492,  -492,  -492,  -492,   153,  -492,   298,   255,   248,   324,
    -492,   327,  -492,   144,  -492,  2264,   328,  2264,  2479,   277,
    -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,
    -492,  -492,  -492,  2363,  -492,   259,   257,   264,  -492,  2264,
    -492,   -17,   267,   268,  -492,   270,   537,   697,  2264,  2533,
    -492,  2114,  2264,  2264,  -492,  -492,  -492,  -492,  -492,  -492,
    -492,  -492,  -492,  -492,  2264,  2264,  -492,   410,   410,  2264,
    2264,  2264,  2264,  2587,   339,  2264,  2264,  2264,  2264,   314,
     926,  -492,   341,   349,   349,    80,   117,    80,  -492,   350,
    -492,  -492,  -492,    57,  -492,    66,  -492,   283,   349,  -492,
     358,   349,  -492,   360,   361,   335,  -492,  -492,   291,   297,
     313,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,
    -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,
    -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,
    -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,
    -492,  -492,  -492,  -492,  -492,  -492,  -492,  -492,  2264,   315,
    -492,   293,    65,  -492,   121,  -492,  2264,  -492,   316,   312,
    2264,  -492,  -492,  -492,  -492,  -492,   317,  -492,   318,   326,
    -492,   319,   322,   329,  -492,  -492,  -492,  -492,  -492,  -492,
    -492,  -492,  -492,  -492,  -492,  -492,  -492,   321,   279,  -492,
     192,   192,   396,  2264,  2264,   196,  -492,  -492,   330,   332,
     337,  -492,  -492,  -492,   336,   333,   386,  2264,   131,   980,
    2264,   216,  -492,   342,   345,   351,   133,   347,   239,  1034,
    -492,  1088,  -492,  -492,  -492,  -492,  2264,   355,  -492,   352,
     356,  1142,  -492,  -492,   358,  -492,  2264,  2264,  -492,   431,
    -492,  2264,  2264,   362,  -492,  -492,  -492,  -492,   370,  -492,
     134,   158,  -492,  2264,  2264,  -492,   818,   432,   368,   196,
     196,  -492,  2264,  2264,   369,  -492,  2264,   371,  2264,  2264,
     415,   441,  2264,   374,   440,   376,   455,   379,   382,  -492,
     420,   419,   404,  -492,  -492,   398,   426,   400,   408,   409,
    2264,   411,    53,  -492,  -492,  -492,   764,  -492,   623,  -492,
    -492,   413,   416,  2134,   479,  -492,  2184,  -492,  -492,   417,
     418,  -492,  1196,   -18,  -492,   412,   421,   423,   425,   487,
    -492,   427,  -492,  -492,  -492,  -492,  1250,   429,   430,  -492,
    1304,  -492,   433,  -492,  -492,  -492,  -492,   435,   262,   496,
     497,  -492,  -492,    61,   445,   105,  -492,  -492,  -492,  -492,
     437,   439,  -492,   442,  -492,  -492,  1358,   484,  2264,  -492,
    1412,  -492,  -492,  -492,  -492,   444,  1466,  -492,  1520,  1574,
    1628,   463,  -492,  -492,   461,  1682,  -492,  1736,  2264,   449,
     450,   175,   446,   447,   526,   431,  2264,  2264,  1790,  -492,
    -492,  1844,  -492,   506,   451,   452,  1898,   507,  1466,  -492,
    -492,   453,   458,   462,  -492,  -492,  -492,  -492,  -492,  -492,
    -492,  -492,  -492,  -492,   465,  -492,   466,  -492,   468,   469,
     470,   472,   473,   478,   481,   485,  -492,   504,   520,   492,
     486,   490,   512,   516,  -492,  2421,   349,   567,  -492,  -492,
    -492,   493,   499,  -492,  -492,   565,   566,   498,  -492,  -492,
     549,   500,  1466,  -492,  -492,  -492,  -492,  -492,  -492,  -492,
    -492,  -492,  -492,  -492,  -492,  -492,   501,   502,   508,  -492,
    -492,   509,   510,   511,   154,   513,  -492,  1952,  -492,   505,
     514,   515,  -492,  2006,   517,  -492,  -492,  -492,  -492,  -492,
    -492,  -492,   518,   519,   530,  2264,  -492,  -492,   569,  -492,
      74,  -492,  -492,   521,  -492,   522,   589,    99,  2060,  -492,
    -492,   523,   590,   597,  -492,   527,   529,  -492,  -492
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       3,     0,     2,     1,    37,     0,     0,    38,     0,     0,
       0,     0,    40,    41,     0,     0,   149,   150,     0,   144,
       0,     0,     0,     0,     0,     0,    39,     0,     0,     0,
       0,     0,     0,     0,     4,     5,     0,     0,    34,     0,
       0,     9,    10,    12,    11,    13,    14,    15,    16,    17,
       0,    19,     0,    20,    22,     0,     0,     0,     0,     0,
       0,     0,     0,    31,     0,   231,   231,   206,    37,   209,
       0,   213,   214,   215,   216,     0,     0,     0,     0,   212,
       0,     0,   298,   298,   223,     0,   170,   171,   173,   175,
     179,   182,   185,   191,   220,   208,     0,    51,     0,     0,
       0,     3,     0,     0,     0,     0,   145,   147,   148,    37,
       0,   132,     0,   118,   117,     0,     0,     0,     0,   143,
      47,    49,    48,    50,   111,    45,     0,     0,     0,   106,
     108,   105,   107,     0,     6,     0,     0,     0,     0,     0,
     146,     7,     8,    18,    21,    23,    24,    25,    26,    27,
      28,    29,    30,     0,   233,     0,   232,     0,   229,   231,
     186,   188,     0,     0,   187,     0,     0,     0,   231,     0,
     210,     0,     0,     0,   196,   197,   198,   199,   200,   201,
     202,   203,   204,   205,     0,     0,    43,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     3,     0,   237,   237,     0,     0,     0,     3,     0,
       3,   142,   141,     0,   140,     0,   137,     0,   237,    46,
       0,   237,     3,     0,     0,     0,    32,    42,     0,   243,
       0,   244,   259,   256,   257,   248,   264,   271,   272,   273,
     289,   269,   270,   268,   254,   252,   278,   258,   249,   287,
     261,   262,   263,   250,   253,   260,   286,   274,   275,   281,
     265,   276,   277,   284,   288,   255,   285,   251,   245,   246,
     247,   282,   283,   280,   266,   267,   279,    36,     0,   243,
     242,     0,     0,   241,     0,    53,     0,    54,     0,     0,
     231,   207,   217,   218,   299,   235,   298,   221,   298,     0,
     243,     0,   227,    37,     3,   159,    34,   160,   161,   162,
     163,   164,   165,   166,   167,   168,   169,     0,   172,   174,
     180,   181,     0,     0,     0,   176,   183,   184,     0,   243,
       0,   193,   230,    52,     0,     0,     0,     0,    40,     0,
       0,    69,   239,     0,   238,     0,     0,     0,   119,     0,
     133,     0,   139,   138,   135,   136,   231,     0,   113,     0,
       0,     0,   110,   109,     0,    35,   231,   231,    33,     0,
     122,     0,     0,     0,   298,   234,   211,   189,     0,   298,
       0,     0,   224,   231,   231,   225,     0,   156,     0,   178,
     177,   192,   231,   231,     0,     3,     0,     0,     0,     0,
      38,     0,     0,     0,     0,     0,     0,     0,     0,     3,
      38,    38,     0,   112,     3,     0,    38,     0,     0,     0,
     296,     0,     0,   290,   291,   122,     0,   190,     0,   219,
     222,     0,     0,     0,    38,   151,     0,   152,    44,     0,
       0,     3,     0,     0,     3,     0,     0,     0,     0,     0,
      71,     0,     3,   240,     3,     3,     0,     0,     0,    57,
       0,     3,     0,     3,    55,    56,   297,     0,     0,     0,
       0,   123,   124,     0,   243,     0,   236,   226,   228,     3,
       0,     0,     3,     0,   194,   195,     0,    38,     0,     3,
       0,    63,    64,    65,    71,     0,    70,    66,     0,     0,
       0,    38,   115,   134,    38,     0,   104,     0,     0,     0,
     130,     0,     0,     0,     0,     0,     0,     0,     0,   154,
     153,     0,   157,    38,     0,     0,     0,    38,    67,    71,
      72,     0,     0,     0,    77,    78,    80,    79,    81,    73,
      82,    83,    84,    85,     0,    87,     0,    89,     0,     0,
       0,     0,     0,     0,     0,     0,    98,    38,    38,    38,
       0,     0,    38,    38,   292,     0,   237,     0,   125,   121,
       3,     0,     0,   293,   294,    38,    38,     0,    59,     3,
      38,     0,    68,    74,    75,    76,    86,    88,    90,    91,
      92,    93,    94,    95,    96,    97,     0,     0,     0,   114,
     101,     0,     0,     0,     0,     0,   131,     0,   120,     0,
       0,     0,    58,     0,     0,    60,    99,   100,   116,   103,
     102,   122,     0,     0,    38,     0,   155,   158,    38,    61,
       0,   122,     3,     0,   295,     0,     0,     0,     0,   129,
      62,     0,     0,    38,   128,     0,     0,   127,   126
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -492,  -492,   -84,  -492,  -164,  -492,    -2,   528,  -492,  -492,
    -492,   483,  -156,  -155,  -491,  -486,  -485,  -482,  -466,  -492,
    -492,  -435,  -492,  -464,  -462,  -457,  -451,  -150,   482,   256,
    -449,  -447,  -106,  -492,  -422,  -492,  -492,   414,  -443,  -146,
    -145,  -140,  -442,  -121,  -120,  -116,  -115,  -436,  -492,  -492,
    -198,    21,  -492,   457,   443,  -187,    86,   -47,   543,    87,
    -492,   334,  -492,  -492,  -492,    55,  -492,  -492,  -180,  -492,
     199,   -86,  -166,   107,   -74
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    35,    36,    37,    84,   139,   188,   322,
     124,   125,    39,    40,    41,    42,    43,    44,    45,   341,
     404,   496,   539,    46,    47,    48,    49,    50,   126,   359,
      51,    52,   112,    53,   422,   472,   511,   113,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,   435,   437,
     317,   154,    86,    87,    88,    89,    90,    91,    92,   189,
      93,    94,   170,   385,    95,   155,   156,   296,   343,   344,
     282,   283,   284,   421,   166
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      38,   298,   325,   473,   206,   534,    64,   305,   289,   167,
     535,   536,   488,     3,   537,   307,   308,   200,   107,   108,
     111,   309,   116,    66,   345,   310,   311,    85,   160,    97,
     538,   312,   540,   164,   541,   102,   103,   534,   357,   542,
     106,   360,   535,   536,   114,   543,   537,   545,   119,   546,
     313,   314,   277,   547,   551,   315,   316,   468,   192,   528,
     556,   352,   538,   489,   540,   468,   541,   193,   469,   117,
     354,   542,   215,    99,    65,   168,   514,   543,   468,   545,
     216,   546,   109,   302,    68,   547,   551,   169,   207,   636,
     208,   534,   556,     7,   582,     7,   535,   536,    98,   346,
     537,   470,   165,   468,   100,   101,   353,   331,   111,   470,
      12,    13,    12,    13,   642,   355,   538,   339,   540,   115,
     541,   157,   470,   371,   349,   542,   351,    26,   211,    26,
     197,   543,   127,   545,   471,   546,   389,   390,   361,   547,
     551,   212,   471,   326,   327,   372,   556,   470,   120,   104,
     110,   129,   130,   105,   198,   471,   226,   120,   228,   121,
      67,    68,    69,   516,   398,    70,   213,   399,   121,   306,
     192,   135,     7,   122,   214,    71,    72,    73,    74,   193,
     471,    75,   122,    76,    77,   517,   136,   295,   137,    12,
      13,   347,   128,   123,   133,   373,   207,   138,    38,   630,
     374,   134,   123,   111,    78,   111,    26,   408,    79,   637,
     429,   141,   207,   328,   288,   294,   333,   334,   335,   336,
     386,   140,   380,   299,   381,   218,    80,   402,   622,    81,
     403,   142,    82,   374,    83,   480,   430,   143,   483,   294,
     144,   118,   174,   175,   176,   177,   178,   179,   180,   181,
     182,   183,   184,   185,   567,   145,   568,   136,   131,   132,
     146,   186,   190,   191,   184,   185,   509,   510,   147,   305,
     320,   321,   305,   158,   323,   324,   148,   307,   308,   149,
     307,   308,   150,   309,   151,   152,   309,   310,   311,   153,
     310,   311,   159,   312,   162,   163,   312,   171,   172,   368,
     426,   173,   194,   195,   196,   428,   199,   375,   201,   202,
     203,   442,   313,   314,   204,   313,   314,   315,   316,   205,
     315,   316,   210,   209,   217,   456,   220,   221,   223,   222,
     460,   224,   531,   285,   227,   278,   286,    38,   287,   290,
     532,   533,   291,   332,   292,   378,   544,    38,   337,    38,
     548,   549,   340,   342,   350,   356,   550,   486,   397,    38,
     490,   401,   358,   364,   531,   362,   363,   365,   498,   366,
     499,   500,   532,   533,   370,   552,   553,   505,   544,   507,
     554,   555,   548,   549,    38,   367,   605,   369,   550,    83,
     376,   383,   423,   424,   384,   518,   379,   374,   521,   604,
     382,    65,   387,   388,   392,   526,   391,   552,   553,   393,
     394,   412,   554,   555,   395,   396,   405,   443,   531,   445,
     446,   418,   419,   449,   406,   407,   532,   533,   409,   413,
     415,   306,   544,   414,   306,   420,   548,   549,   431,   432,
      38,   466,   550,   425,   427,   436,   438,   439,   440,   476,
     441,   447,   444,   448,    38,   450,   451,   452,    38,   453,
     454,   552,   553,   455,   457,   458,   554,   555,   174,   175,
     176,   177,   178,   179,   180,   181,   182,   183,   459,   461,
     462,   463,   464,   465,    38,   467,   607,   477,    38,   481,
     478,   484,   485,   491,    38,   613,    38,    38,    38,   495,
     512,   513,   492,    38,   493,    38,   494,   560,   497,   525,
     502,   503,   524,   561,   506,   508,    38,   515,   519,    38,
     520,   565,   566,   522,    38,   529,    38,   569,   570,   564,
     571,   577,   578,   579,   583,   581,   598,   573,   574,   584,
      67,    68,    69,   585,   596,    70,   586,   587,   638,   588,
     589,   590,     7,   591,   592,    71,    72,    73,    74,   593,
     597,    75,   594,    76,    77,   601,   595,   599,   602,    12,
      13,   600,   606,   609,   608,   610,   611,   614,   633,   612,
      38,   615,   616,   617,    78,   625,    26,   623,    79,   618,
     619,   620,   621,   641,   645,   626,   627,   635,   629,   631,
     632,   646,   639,   640,   644,    38,    80,   219,   647,    81,
     648,    38,    82,   293,    83,   225,   319,   187,   294,   161,
     417,   348,   572,   377,     0,   475,    67,    68,    69,   318,
       0,    70,     0,     0,     0,     0,    38,     0,     7,     0,
       0,    71,    72,    73,    74,     0,   634,    75,     0,    76,
      77,     0,     0,     0,     0,    12,    13,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      78,     0,    26,     0,    79,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    80,     0,     0,    81,     0,     0,    82,     0,
      83,   279,   280,     0,   294,     0,   231,   232,     0,   233,
     234,     0,   235,     0,   236,   237,   238,   239,   240,   241,
     242,   243,   244,   245,   246,   247,   248,   249,   250,   251,
     252,   253,   254,   255,   256,   257,   258,   259,   260,   261,
     262,   263,   264,   265,   266,   267,   268,   269,   270,   271,
     272,   273,   274,   275,   276,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   474,   280,
       0,     0,     0,   231,   232,   297,   233,   234,   294,   235,
       0,   236,   237,   238,   239,   240,   241,   242,   243,   244,
     245,   246,   247,   248,   249,   250,   251,   252,   253,   254,
     255,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,     4,     0,     0,     0,     5,     0,     6,     0,
       0,   433,     0,   434,     0,     8,     0,     0,     0,     0,
       0,     0,     0,     9,     0,   294,    10,     0,     0,    11,
      12,    13,     0,     0,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,     0,    25,    26,     0,    27,
      28,    29,    30,    31,    32,    33,     4,     0,     0,     0,
       5,     0,     6,     0,     0,     0,     0,     7,     0,     8,
       0,     0,     0,     0,     0,     0,     0,     9,     0,    34,
      10,     0,     0,    11,    12,    13,     0,     0,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,     0,
      25,    26,     0,    27,    28,    29,    30,    31,    32,    33,
       4,     0,     0,     0,     5,     0,     6,     0,     0,     0,
       0,     7,     0,     8,     0,     0,     0,     0,     0,     0,
       0,     9,     0,    34,    10,     0,     0,    11,   338,    13,
       0,     0,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,     0,    25,    26,     0,    27,    28,    29,
      30,    31,    32,    33,     4,     0,     0,     0,     5,     0,
       6,     0,     0,     0,     0,   400,     0,     8,     0,     0,
       0,     0,     0,     0,     0,     9,     0,    34,    10,     0,
       0,    11,    12,    13,     0,     0,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,     0,    25,    26,
       0,    27,    28,    29,    30,    31,    32,    33,     4,     0,
       0,     0,     5,     0,     6,     0,     0,     0,     0,   410,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    34,    10,     0,     0,    11,    12,    13,     0,     0,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,    25,    26,     0,    27,    28,    29,    30,    31,
      32,    33,     4,     0,     0,     0,     5,     0,     6,     0,
       0,     0,     0,   411,     0,     8,     0,     0,     0,     0,
       0,     0,     0,     9,     0,    34,    10,     0,     0,    11,
      12,    13,     0,     0,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,     0,    25,    26,     0,    27,
      28,    29,    30,    31,    32,    33,     4,     0,     0,     0,
       5,     0,     6,     0,     0,     0,     0,   416,     0,     8,
       0,     0,     0,     0,     0,     0,     0,     9,     0,    34,
      10,     0,     0,    11,    12,    13,     0,     0,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,     0,
      25,    26,     0,    27,    28,    29,    30,    31,    32,    33,
       4,     0,     0,     0,     5,     0,     6,     0,     0,     0,
       0,   487,     0,     8,     0,     0,     0,     0,     0,     0,
       0,     9,     0,    34,    10,     0,     0,    11,    12,    13,
       0,     0,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,     0,    25,    26,     0,    27,    28,    29,
      30,    31,    32,    33,     4,     0,     0,     0,     5,     0,
       6,     0,     0,     0,     0,   501,     0,     8,     0,     0,
       0,     0,     0,     0,     0,     9,     0,    34,    10,     0,
       0,    11,    12,    13,     0,     0,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,     0,    25,    26,
       0,    27,    28,    29,    30,    31,    32,    33,     4,     0,
       0,     0,     5,     0,     6,     0,     0,     0,     0,   504,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    34,    10,     0,     0,    11,    12,    13,     0,     0,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,    25,    26,     0,    27,    28,    29,    30,    31,
      32,    33,     4,     0,     0,     0,     5,     0,     6,     0,
       0,     0,     0,   523,     0,     8,     0,     0,     0,     0,
       0,     0,     0,     9,     0,    34,    10,     0,     0,    11,
      12,    13,     0,     0,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,     0,    25,    26,     0,    27,
      28,    29,    30,    31,    32,    33,     4,     0,     0,     0,
       5,     0,     6,     0,     0,     0,     0,   527,     0,     8,
       0,     0,     0,     0,     0,     0,     0,     9,     0,    34,
      10,     0,     0,    11,    12,    13,     0,     0,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,     0,
      25,    26,     0,    27,    28,    29,    30,    31,    32,    33,
     303,     0,     0,     0,     5,     0,     6,     0,     0,     0,
       0,     7,     0,     8,     0,     0,     0,     0,     0,     0,
       0,     9,     0,    34,    10,     0,     0,    11,    12,    13,
       0,     0,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,     0,    25,    26,     0,    27,    28,    29,
      30,    31,    32,    33,     4,     0,     0,     0,     5,     0,
       6,     0,     0,     0,     0,   557,     0,     8,     0,     0,
       0,     0,     0,     0,     0,     9,     0,   530,    10,     0,
       0,    11,    12,    13,     0,     0,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,     0,    25,    26,
       0,    27,    28,    29,    30,    31,    32,    33,     4,     0,
       0,     0,     5,     0,     6,     0,     0,     0,     0,   558,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    34,    10,     0,     0,    11,    12,    13,     0,     0,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,    25,    26,     0,    27,    28,    29,    30,    31,
      32,    33,     4,     0,     0,     0,     5,     0,     6,     0,
       0,     0,     0,   559,     0,     8,     0,     0,     0,     0,
       0,     0,     0,     9,     0,    34,    10,     0,     0,    11,
      12,    13,     0,     0,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,     0,    25,    26,     0,    27,
      28,    29,    30,    31,    32,    33,     4,     0,     0,     0,
       5,     0,     6,     0,     0,     0,     0,   562,     0,     8,
       0,     0,     0,     0,     0,     0,     0,     9,     0,    34,
      10,     0,     0,    11,    12,    13,     0,     0,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,     0,
      25,    26,     0,    27,    28,    29,    30,    31,    32,    33,
       4,     0,     0,     0,     5,     0,     6,     0,     0,     0,
       0,   563,     0,     8,     0,     0,     0,     0,     0,     0,
       0,     9,     0,    34,    10,     0,     0,    11,    12,    13,
       0,     0,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,     0,    25,    26,     0,    27,    28,    29,
      30,    31,    32,    33,     4,     0,     0,     0,     5,     0,
       6,     0,     0,     0,     0,   575,     0,     8,     0,     0,
       0,     0,     0,     0,     0,     9,     0,    34,    10,     0,
       0,    11,    12,    13,     0,     0,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,     0,    25,    26,
       0,    27,    28,    29,    30,    31,    32,    33,     4,     0,
       0,     0,     5,     0,     6,     0,     0,     0,     0,   576,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
       0,    34,    10,     0,     0,    11,    12,    13,     0,     0,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,    25,    26,     0,    27,    28,    29,    30,    31,
      32,    33,     4,     0,     0,     0,     5,     0,     6,     0,
       0,     0,     0,   580,     0,     8,     0,     0,     0,     0,
       0,     0,     0,     9,     0,    34,    10,     0,     0,    11,
      12,    13,     0,     0,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,     0,    25,    26,     0,    27,
      28,    29,    30,    31,    32,    33,     4,     0,     0,     0,
       5,     0,     6,     0,     0,     0,     0,   624,     0,     8,
       0,     0,     0,     0,     0,     0,     0,     9,     0,    34,
      10,     0,     0,    11,    12,    13,     0,     0,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,     0,
      25,    26,     0,    27,    28,    29,    30,    31,    32,    33,
       4,     0,     0,     0,     5,     0,     6,     0,     0,     0,
       0,   628,     0,     8,     0,     0,     0,     0,     0,     0,
       0,     9,     0,    34,    10,     0,     0,    11,    12,    13,
       0,     0,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,     0,    25,    26,     0,    27,    28,    29,
      30,    31,    32,    33,     4,     0,     0,     0,     5,     0,
       6,     0,     0,     0,     0,   643,     0,     8,     0,     0,
       0,     0,     0,     0,     0,     9,     0,    34,    10,     0,
       0,    11,    12,    13,     0,     0,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,     0,    25,    26,
       0,    27,    28,    29,    30,    31,    32,    33,   303,     0,
       0,     0,     5,     0,     0,     0,     0,     0,     0,     7,
       0,     8,     0,     0,     0,     0,     0,     0,   303,     0,
       0,    34,     5,     0,     0,     0,    12,    13,     0,     7,
       0,     8,    16,    17,     0,    19,    20,    21,     0,     0,
       0,     0,    25,    26,     0,    27,    12,    13,     0,    31,
      32,     0,    16,    17,     0,    19,    20,    21,     0,     0,
       0,     0,    25,    26,     0,    27,     0,     0,   303,    31,
      32,     0,     5,     0,     0,   304,     0,     0,     0,     7,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   479,    12,    13,     0,     0,
       0,     0,    16,    17,     0,    19,    20,    21,     0,    67,
      68,    69,    25,    26,    70,    27,     0,     0,     0,    31,
      32,     7,     0,     0,    71,    72,    73,    74,     0,     0,
      75,     0,    76,    77,     0,    96,     0,     0,    12,    13,
       0,     0,     0,     0,     0,   482,     0,    67,    68,    69,
       0,     0,    70,    78,     0,    26,     0,    79,     0,     7,
       0,     0,    71,    72,    73,    74,     0,     0,    75,     0,
      76,    77,     0,     0,     0,    80,    12,    13,    81,     0,
       0,    82,     0,    83,     0,    67,    68,    69,     0,     0,
      70,    78,     0,    26,     0,    79,     0,     7,     0,     0,
      71,    72,    73,    74,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    80,    12,    13,    81,     0,     0,    82,
       0,    83,     0,     0,     0,     0,     0,     0,     0,    78,
       0,    26,     0,    79,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   279,   280,     0,
       0,     0,   231,   232,    81,   233,   234,    82,   235,    83,
     236,   237,   238,   239,   240,   241,   242,   243,   244,   245,
     246,   247,   248,   249,   250,   251,   252,   253,   254,   255,
     256,   257,   258,   259,   260,   261,   262,   263,   264,   265,
     266,   267,   268,   269,   270,   271,   272,   273,   274,   275,
     276,     0,     0,     0,     0,   279,   280,     0,     0,     0,
     231,   232,     0,   233,   234,     0,   235,   281,   236,   237,
     238,   239,   240,   241,   242,   243,   244,   245,   246,   247,
     248,   249,   250,   251,   252,   253,   254,   255,   256,   257,
     258,   259,   260,   261,   262,   263,   264,   265,   266,   267,
     268,   269,   270,   271,   272,   273,   274,   275,   276,     0,
       0,     0,     0,   229,     0,     0,     0,   230,   231,   232,
       0,   233,   234,     0,   235,   603,   236,   237,   238,   239,
     240,   241,   242,   243,   244,   245,   246,   247,   248,   249,
     250,   251,   252,   253,   254,   255,   256,   257,   258,   259,
     260,   261,   262,   263,   264,   265,   266,   267,   268,   269,
     270,   271,   272,   273,   274,   275,   276,   300,     0,     0,
       0,   301,   231,   232,     0,   233,   234,     0,   235,     0,
     236,   237,   238,   239,   240,   241,   242,   243,   244,   245,
     246,   247,   248,   249,   250,   251,   252,   253,   254,   255,
     256,   257,   258,   259,   260,   261,   262,   263,   264,   265,
     266,   267,   268,   269,   270,   271,   272,   273,   274,   275,
     276,   329,     0,     0,     0,   330,   231,   232,     0,   233,
     234,     0,   235,     0,   236,   237,   238,   239,   240,   241,
     242,   243,   244,   245,   246,   247,   248,   249,   250,   251,
     252,   253,   254,   255,   256,   257,   258,   259,   260,   261,
     262,   263,   264,   265,   266,   267,   268,   269,   270,   271,
     272,   273,   274,   275,   276
};

static const yytype_int16 yycheck[] =
{
       2,   167,   189,   425,   110,   496,     4,   171,    25,    83,
     496,   496,    30,     0,   496,   171,   171,   101,    20,    21,
      22,   171,     4,    72,   204,   171,   171,     6,    75,     8,
     496,   171,   496,    80,   496,    14,    15,   528,   218,   496,
      19,   221,   528,   528,    23,   496,   528,   496,    27,   496,
     171,   171,   138,   496,   496,   171,   171,     4,    75,   494,
     496,     4,   528,    81,   528,     4,   528,    84,    15,    51,
       4,   528,    42,     4,    72,    72,    15,   528,     4,   528,
      50,   528,     4,   169,     4,   528,   528,    84,    79,    15,
      81,   582,   528,    15,   529,    15,   582,   582,     4,   205,
     582,    48,    81,     4,    35,    81,    49,   193,   110,    48,
      32,    33,    32,    33,    15,    49,   582,   201,   582,    47,
     582,    66,    48,    58,   208,   582,   210,    49,     4,    49,
      34,   582,     4,   582,    81,   582,   323,   324,   222,   582,
     582,    17,    81,   190,   191,    80,   582,    48,     4,     4,
      72,     4,     5,     8,    58,    81,   135,     4,   137,    15,
       3,     4,     5,    58,    33,     8,    42,    36,    15,   171,
      75,    58,    15,    29,    50,    18,    19,    20,    21,    84,
      81,    24,    29,    26,    27,    80,    73,   166,    75,    32,
      33,    74,     4,    49,    52,    74,    79,    84,   200,   621,
      79,    81,    49,   205,    47,   207,    49,    74,    51,   631,
      76,    81,    79,   192,   159,    81,   195,   196,   197,   198,
     304,    80,   296,   168,   298,    72,    69,    11,    74,    72,
      14,    81,    75,    79,    77,   433,    78,    81,   436,    81,
      81,    84,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69,    79,    81,    81,    73,     4,     5,
      81,    77,    70,    71,    68,    69,     4,     5,    81,   433,
     184,   185,   436,     4,   187,   188,    81,   433,   433,    81,
     436,   436,    81,   433,    81,    81,   436,   433,   433,    72,
     436,   436,    72,   433,     4,    72,   436,    12,    23,   278,
     374,    22,     3,    51,    72,   379,     4,   286,    81,    81,
      72,   395,   433,   433,    72,   436,   436,   433,   433,    72,
     436,   436,    81,    84,     4,   409,    28,    72,     4,    81,
     414,     4,   496,    74,     6,    58,    79,   339,    74,    72,
     496,   496,    74,     4,    74,   290,   496,   349,    34,   351,
     496,   496,    11,     4,     4,    72,   496,   441,   337,   361,
     444,   340,     4,    28,   528,     5,     5,    76,   452,    72,
     454,   455,   528,   528,    81,   496,   496,   461,   528,   463,
     496,   496,   528,   528,   386,    72,   566,    72,   528,    77,
      74,    72,   371,   372,    72,   479,    79,    79,   482,   565,
      74,    72,    81,     7,    72,   489,    76,   528,   528,    72,
      74,   356,   528,   528,    81,    29,    74,   396,   582,   398,
     399,   366,   367,   402,    79,    74,   582,   582,    81,    74,
      74,   433,   582,    81,   436,     4,   582,   582,   383,   384,
     442,   420,   582,    81,    74,    13,    78,   392,   393,   428,
      81,    36,    81,    12,   456,    81,    16,    81,   460,     4,
      81,   582,   582,    81,    44,    46,   582,   582,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    74,    81,
      54,    81,    74,    74,   486,    74,   570,    74,   490,    10,
      74,    74,    74,    81,   496,   579,   498,   499,   500,    12,
       4,     4,    81,   505,    81,   507,    81,    44,    81,   488,
      81,    81,    28,    52,    81,    80,   518,    72,    81,   521,
      81,    72,    72,    81,   526,    81,   528,    81,    81,   508,
       4,    25,    81,    81,    81,    28,    44,   516,   517,    81,
       3,     4,     5,    81,    40,     8,    81,    81,   632,    81,
      81,    81,    15,    81,    81,    18,    19,    20,    21,    81,
      40,    24,    81,    26,    27,    53,    81,    81,    52,    32,
      33,    81,     5,    74,    81,    10,    10,    28,    48,    81,
     582,    81,    81,    81,    47,    80,    49,    74,    51,    81,
      81,    81,    81,     4,     4,    81,    81,    28,    81,    81,
      81,     4,    81,    81,    81,   607,    69,   124,    81,    72,
      81,   613,    75,    76,    77,   133,   173,    89,    81,    76,
     364,   207,   515,   289,    -1,   426,     3,     4,     5,   172,
      -1,     8,    -1,    -1,    -1,    -1,   638,    -1,    15,    -1,
      -1,    18,    19,    20,    21,    -1,   625,    24,    -1,    26,
      27,    -1,    -1,    -1,    -1,    32,    33,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      47,    -1,    49,    -1,    51,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    69,    -1,    -1,    72,    -1,    -1,    75,    -1,
      77,     4,     5,    -1,    81,    -1,     9,    10,    -1,    12,
      13,    -1,    15,    -1,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,     4,     5,
      -1,    -1,    -1,     9,    10,    78,    12,    13,    81,    15,
      -1,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,     4,    -1,    -1,    -1,     8,    -1,    10,    -1,
      -1,    13,    -1,    15,    -1,    17,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    25,    -1,    81,    28,    -1,    -1,    31,
      32,    33,    -1,    -1,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    -1,    48,    49,    -1,    51,
      52,    53,    54,    55,    56,    57,     4,    -1,    -1,    -1,
       8,    -1,    10,    -1,    -1,    -1,    -1,    15,    -1,    17,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,    -1,    81,
      28,    -1,    -1,    31,    32,    33,    -1,    -1,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    -1,
      48,    49,    -1,    51,    52,    53,    54,    55,    56,    57,
       4,    -1,    -1,    -1,     8,    -1,    10,    -1,    -1,    -1,
      -1,    15,    -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    25,    -1,    81,    28,    -1,    -1,    31,    32,    33,
      -1,    -1,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    -1,    48,    49,    -1,    51,    52,    53,
      54,    55,    56,    57,     4,    -1,    -1,    -1,     8,    -1,
      10,    -1,    -1,    -1,    -1,    15,    -1,    17,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    25,    -1,    81,    28,    -1,
      -1,    31,    32,    33,    -1,    -1,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    -1,    48,    49,
      -1,    51,    52,    53,    54,    55,    56,    57,     4,    -1,
      -1,    -1,     8,    -1,    10,    -1,    -1,    -1,    -1,    15,
      -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,
      -1,    81,    28,    -1,    -1,    31,    32,    33,    -1,    -1,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    49,    -1,    51,    52,    53,    54,    55,
      56,    57,     4,    -1,    -1,    -1,     8,    -1,    10,    -1,
      -1,    -1,    -1,    15,    -1,    17,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    25,    -1,    81,    28,    -1,    -1,    31,
      32,    33,    -1,    -1,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    -1,    48,    49,    -1,    51,
      52,    53,    54,    55,    56,    57,     4,    -1,    -1,    -1,
       8,    -1,    10,    -1,    -1,    -1,    -1,    15,    -1,    17,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,    -1,    81,
      28,    -1,    -1,    31,    32,    33,    -1,    -1,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    -1,
      48,    49,    -1,    51,    52,    53,    54,    55,    56,    57,
       4,    -1,    -1,    -1,     8,    -1,    10,    -1,    -1,    -1,
      -1,    15,    -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    25,    -1,    81,    28,    -1,    -1,    31,    32,    33,
      -1,    -1,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    -1,    48,    49,    -1,    51,    52,    53,
      54,    55,    56,    57,     4,    -1,    -1,    -1,     8,    -1,
      10,    -1,    -1,    -1,    -1,    15,    -1,    17,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    25,    -1,    81,    28,    -1,
      -1,    31,    32,    33,    -1,    -1,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    -1,    48,    49,
      -1,    51,    52,    53,    54,    55,    56,    57,     4,    -1,
      -1,    -1,     8,    -1,    10,    -1,    -1,    -1,    -1,    15,
      -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,
      -1,    81,    28,    -1,    -1,    31,    32,    33,    -1,    -1,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    49,    -1,    51,    52,    53,    54,    55,
      56,    57,     4,    -1,    -1,    -1,     8,    -1,    10,    -1,
      -1,    -1,    -1,    15,    -1,    17,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    25,    -1,    81,    28,    -1,    -1,    31,
      32,    33,    -1,    -1,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    -1,    48,    49,    -1,    51,
      52,    53,    54,    55,    56,    57,     4,    -1,    -1,    -1,
       8,    -1,    10,    -1,    -1,    -1,    -1,    15,    -1,    17,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,    -1,    81,
      28,    -1,    -1,    31,    32,    33,    -1,    -1,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    -1,
      48,    49,    -1,    51,    52,    53,    54,    55,    56,    57,
       4,    -1,    -1,    -1,     8,    -1,    10,    -1,    -1,    -1,
      -1,    15,    -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    25,    -1,    81,    28,    -1,    -1,    31,    32,    33,
      -1,    -1,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    -1,    48,    49,    -1,    51,    52,    53,
      54,    55,    56,    57,     4,    -1,    -1,    -1,     8,    -1,
      10,    -1,    -1,    -1,    -1,    15,    -1,    17,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    25,    -1,    81,    28,    -1,
      -1,    31,    32,    33,    -1,    -1,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    -1,    48,    49,
      -1,    51,    52,    53,    54,    55,    56,    57,     4,    -1,
      -1,    -1,     8,    -1,    10,    -1,    -1,    -1,    -1,    15,
      -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,
      -1,    81,    28,    -1,    -1,    31,    32,    33,    -1,    -1,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    49,    -1,    51,    52,    53,    54,    55,
      56,    57,     4,    -1,    -1,    -1,     8,    -1,    10,    -1,
      -1,    -1,    -1,    15,    -1,    17,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    25,    -1,    81,    28,    -1,    -1,    31,
      32,    33,    -1,    -1,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    -1,    48,    49,    -1,    51,
      52,    53,    54,    55,    56,    57,     4,    -1,    -1,    -1,
       8,    -1,    10,    -1,    -1,    -1,    -1,    15,    -1,    17,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,    -1,    81,
      28,    -1,    -1,    31,    32,    33,    -1,    -1,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    -1,
      48,    49,    -1,    51,    52,    53,    54,    55,    56,    57,
       4,    -1,    -1,    -1,     8,    -1,    10,    -1,    -1,    -1,
      -1,    15,    -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    25,    -1,    81,    28,    -1,    -1,    31,    32,    33,
      -1,    -1,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    -1,    48,    49,    -1,    51,    52,    53,
      54,    55,    56,    57,     4,    -1,    -1,    -1,     8,    -1,
      10,    -1,    -1,    -1,    -1,    15,    -1,    17,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    25,    -1,    81,    28,    -1,
      -1,    31,    32,    33,    -1,    -1,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    -1,    48,    49,
      -1,    51,    52,    53,    54,    55,    56,    57,     4,    -1,
      -1,    -1,     8,    -1,    10,    -1,    -1,    -1,    -1,    15,
      -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,
      -1,    81,    28,    -1,    -1,    31,    32,    33,    -1,    -1,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    49,    -1,    51,    52,    53,    54,    55,
      56,    57,     4,    -1,    -1,    -1,     8,    -1,    10,    -1,
      -1,    -1,    -1,    15,    -1,    17,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    25,    -1,    81,    28,    -1,    -1,    31,
      32,    33,    -1,    -1,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    -1,    48,    49,    -1,    51,
      52,    53,    54,    55,    56,    57,     4,    -1,    -1,    -1,
       8,    -1,    10,    -1,    -1,    -1,    -1,    15,    -1,    17,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,    -1,    81,
      28,    -1,    -1,    31,    32,    33,    -1,    -1,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    -1,
      48,    49,    -1,    51,    52,    53,    54,    55,    56,    57,
       4,    -1,    -1,    -1,     8,    -1,    10,    -1,    -1,    -1,
      -1,    15,    -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    25,    -1,    81,    28,    -1,    -1,    31,    32,    33,
      -1,    -1,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    -1,    48,    49,    -1,    51,    52,    53,
      54,    55,    56,    57,     4,    -1,    -1,    -1,     8,    -1,
      10,    -1,    -1,    -1,    -1,    15,    -1,    17,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    25,    -1,    81,    28,    -1,
      -1,    31,    32,    33,    -1,    -1,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    -1,    48,    49,
      -1,    51,    52,    53,    54,    55,    56,    57,     4,    -1,
      -1,    -1,     8,    -1,    -1,    -1,    -1,    -1,    -1,    15,
      -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,     4,    -1,
      -1,    81,     8,    -1,    -1,    -1,    32,    33,    -1,    15,
      -1,    17,    38,    39,    -1,    41,    42,    43,    -1,    -1,
      -1,    -1,    48,    49,    -1,    51,    32,    33,    -1,    55,
      56,    -1,    38,    39,    -1,    41,    42,    43,    -1,    -1,
      -1,    -1,    48,    49,    -1,    51,    -1,    -1,     4,    55,
      56,    -1,     8,    -1,    -1,    81,    -1,    -1,    -1,    15,
      -1,    17,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    81,    32,    33,    -1,    -1,
      -1,    -1,    38,    39,    -1,    41,    42,    43,    -1,     3,
       4,     5,    48,    49,     8,    51,    -1,    -1,    -1,    55,
      56,    15,    -1,    -1,    18,    19,    20,    21,    -1,    -1,
      24,    -1,    26,    27,    -1,    29,    -1,    -1,    32,    33,
      -1,    -1,    -1,    -1,    -1,    81,    -1,     3,     4,     5,
      -1,    -1,     8,    47,    -1,    49,    -1,    51,    -1,    15,
      -1,    -1,    18,    19,    20,    21,    -1,    -1,    24,    -1,
      26,    27,    -1,    -1,    -1,    69,    32,    33,    72,    -1,
      -1,    75,    -1,    77,    -1,     3,     4,     5,    -1,    -1,
       8,    47,    -1,    49,    -1,    51,    -1,    15,    -1,    -1,
      18,    19,    20,    21,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    69,    32,    33,    72,    -1,    -1,    75,
      -1,    77,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    47,
      -1,    49,    -1,    51,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,     4,     5,    -1,
      -1,    -1,     9,    10,    72,    12,    13,    75,    15,    77,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    -1,    -1,    -1,    -1,     4,     5,    -1,    -1,    -1,
       9,    10,    -1,    12,    13,    -1,    15,    74,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    -1,
      -1,    -1,    -1,     4,    -1,    -1,    -1,     8,     9,    10,
      -1,    12,    13,    -1,    15,    74,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,     4,    -1,    -1,
      -1,     8,     9,    10,    -1,    12,    13,    -1,    15,    -1,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,     4,    -1,    -1,    -1,     8,     9,    10,    -1,    12,
      13,    -1,    15,    -1,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    86,    87,     0,     4,     8,    10,    15,    17,    25,
      28,    31,    32,    33,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    48,    49,    51,    52,    53,
      54,    55,    56,    57,    81,    88,    89,    90,    91,    97,
      98,    99,   100,   101,   102,   103,   108,   109,   110,   111,
     112,   115,   116,   118,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,     4,    72,    72,     3,     4,     5,
       8,    18,    19,    20,    21,    24,    26,    27,    47,    51,
      69,    72,    75,    77,    91,   136,   137,   138,   139,   140,
     141,   142,   143,   145,   146,   149,    29,   136,     4,     4,
      35,    81,   136,   136,     4,     8,   136,    91,    91,     4,
      72,    91,   117,   122,   136,    47,     4,    51,    84,   136,
       4,    15,    29,    49,    95,    96,   113,     4,     4,     4,
       5,     4,     5,    52,    81,    58,    73,    75,    84,    92,
      80,    81,    81,    81,    81,    81,    81,    81,    81,    81,
      81,    81,    81,    72,   136,   150,   151,   150,     4,    72,
     142,   143,     4,    72,   142,   136,   159,   159,    72,    84,
     147,    12,    23,    22,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    77,    92,    93,   144,
      70,    71,    75,    84,     3,    51,    72,    34,    58,     4,
      87,    81,    81,    72,    72,    72,   117,    79,    81,    84,
      81,     4,    17,    42,    50,    42,    50,     4,    72,    96,
      28,    72,    81,     4,     4,   113,   136,     6,   136,     4,
       8,     9,    10,    12,    13,    15,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,   156,    58,     4,
       5,    74,   155,   156,   157,    74,    79,    74,   150,    25,
      72,    74,    74,    76,    81,   136,   152,    78,   157,   150,
       4,     8,   156,     4,    81,    89,    91,    97,    98,   112,
     124,   125,   126,   128,   129,   130,   131,   135,   138,   139,
     141,   141,    94,   144,   144,   140,   142,   142,   136,     4,
       8,   156,     4,   136,   136,   136,   136,    34,    32,    87,
      11,   104,     4,   153,   154,   153,   117,    74,   122,    87,
       4,    87,     4,    49,     4,    49,    72,   153,     4,   114,
     153,    87,     5,     5,    28,    76,    72,    72,   136,    72,
      81,    58,    80,    74,    79,   136,    74,   146,   150,    79,
     159,   159,    74,    72,    72,   148,    87,    81,     7,   140,
     140,    76,    72,    72,    74,    81,    29,   136,    33,    36,
      15,   136,    11,    14,   105,    74,    79,    74,    74,    81,
      15,    15,   150,    74,    81,    74,    15,   114,   150,   150,
       4,   158,   119,   136,   136,    81,   159,    74,   159,    76,
      78,   150,   150,    13,    15,   133,    13,   134,    78,   150,
     150,    81,    87,   136,    81,   136,   136,    36,    12,   136,
      81,    16,    81,     4,    81,    81,    87,    44,    46,    74,
      87,    81,    54,    81,    74,    74,   136,    74,     4,    15,
      48,    81,   120,   119,     4,   155,   136,    74,    74,    81,
     135,    10,    81,   135,    74,    74,    87,    15,    30,    81,
      87,    81,    81,    81,    81,    12,   106,    81,    87,    87,
      87,    15,    81,    81,    15,    87,    81,    87,    80,     4,
       5,   121,     4,     4,    15,    72,    58,    80,    87,    81,
      81,    87,    81,    15,    28,   136,    87,    15,   106,    81,
      81,    89,    97,    98,    99,   100,   101,   102,   103,   107,
     108,   109,   110,   111,   112,   115,   116,   123,   124,   125,
     126,   127,   128,   129,   130,   131,   132,    15,    15,    15,
      44,    52,    15,    15,   136,    72,    72,    79,    81,    81,
      81,     4,   158,   136,   136,    15,    15,    25,    81,    81,
      15,    28,   106,    81,    81,    81,    81,    81,    81,    81,
      81,    81,    81,    81,    81,    81,    40,    40,    44,    81,
      81,    53,    52,    74,   157,   153,     5,    87,    81,    74,
      10,    10,    81,    87,    28,    81,    81,    81,    81,    81,
      81,    81,    74,    74,    15,    80,    81,    81,    15,    81,
     119,    81,    81,    48,   136,    28,    15,   119,    87,    81,
      81,     4,    15,    15,    81,     4,     4,    81,    81
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    85,    86,    87,    87,    87,    88,    88,    88,    88,
      88,    88,    88,    88,    88,    88,    88,    88,    88,    88,
      88,    88,    88,    88,    88,    88,    88,    88,    88,    88,
      88,    88,    89,    89,    90,    90,    90,    91,    91,    91,
      91,    91,    92,    94,    93,    95,    95,    96,    96,    96,
      96,    97,    97,    98,    98,    98,    98,    98,    99,   100,
     100,   100,   100,   101,   101,   102,   103,   104,   104,   105,
     105,   106,   106,   106,   107,   107,   107,   107,   107,   107,
     107,   107,   107,   107,   107,   107,   107,   107,   107,   107,
     107,   107,   107,   107,   107,   107,   107,   107,   107,   108,
     108,   109,   109,   110,   111,   112,   112,   112,   112,   112,
     112,   113,   113,   114,   115,   115,   115,   116,   117,   117,
     118,   118,   119,   119,   119,   120,   120,   120,   120,   120,
     121,   121,   122,   122,   123,   124,   124,   124,   124,   124,
     124,   124,   124,   125,   126,   126,   127,   128,   129,   130,
     131,   132,   132,   133,   133,   133,   134,   134,   134,   135,
     135,   135,   135,   135,   135,   135,   135,   135,   135,   135,
     136,   137,   137,   138,   138,   139,   139,   139,   139,   140,
     140,   140,   141,   141,   141,   142,   142,   142,   142,   142,
     142,   143,   143,   143,   143,   143,   144,   144,   144,   144,
     144,   144,   144,   144,   144,   144,   145,   145,   145,   145,
     145,   145,   145,   145,   145,   145,   145,   145,   145,   145,
     145,   146,   146,   147,   147,   147,   147,   148,   148,   149,
     149,   150,   150,   151,   151,   152,   152,   153,   153,   154,
     154,   155,   155,   156,   156,   156,   156,   156,   156,   156,
     156,   156,   156,   156,   156,   156,   156,   156,   156,   156,
     156,   156,   156,   156,   156,   156,   156,   156,   156,   156,
     156,   156,   156,   156,   156,   156,   156,   156,   156,   156,
     156,   156,   156,   156,   156,   156,   156,   156,   156,   156,
     157,   157,   157,   157,   157,   157,   158,   158,   159,   159
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     2,     1,
       1,     2,     1,     2,     2,     2,     2,     2,     2,     2,
       2,     1,     3,     4,     1,     4,     3,     1,     1,     1,
       1,     1,     2,     0,     4,     1,     2,     1,     1,     1,
       1,     2,     4,     4,     4,     6,     6,     6,    10,     9,
      10,    11,    13,     7,     7,     7,     7,     5,     6,     0,
       3,     0,     2,     2,     2,     2,     2,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     2,     1,     2,     1,
       2,     2,     2,     2,     2,     2,     2,     2,     1,    10,
      10,     9,    10,    10,     7,     2,     2,     2,     2,     4,
       4,     1,     4,     1,     9,     7,    10,     2,     1,     3,
      10,     9,     0,     2,     2,     3,    10,    10,     9,     7,
       1,     3,     1,     3,     7,     4,     4,     3,     4,     4,
       3,     3,     3,     2,     1,     2,     2,     2,     2,     1,
       1,     6,     6,     3,     3,     6,     0,     3,     6,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     3,     1,     3,     1,     3,     4,     4,     1,
       3,     3,     1,     3,     3,     1,     2,     2,     2,     4,
       5,     1,     4,     3,     6,     6,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     3,     1,     1,
       2,     4,     1,     1,     1,     1,     1,     3,     3,     5,
       1,     3,     5,     0,     3,     3,     5,     0,     3,     2,
       3,     0,     1,     1,     3,     1,     4,     0,     1,     1,
       3,     1,     1,     1,     1,     1,     1,     1,     1,     1,
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
#line 732 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2805 "src/parser.tab.c"
        break;

    case YYSYMBOL_STRING: /* STRING  */
#line 732 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2811 "src/parser.tab.c"
        break;

    case YYSYMBOL_MOD_CONTENT: /* MOD_CONTENT  */
#line 732 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2817 "src/parser.tab.c"
        break;

    case YYSYMBOL_LENS_CONTENT: /* LENS_CONTENT  */
#line 732 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2823 "src/parser.tab.c"
        break;

    case YYSYMBOL_QUALIFIED_IDENT: /* QUALIFIED_IDENT  */
#line 732 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2829 "src/parser.tab.c"
        break;

    case YYSYMBOL_program: /* program  */
#line 755 "src/parser.y"
            { (void) ((*yyvaluep).stmt_list); }
#line 2835 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement_list: /* statement_list  */
#line 735 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2841 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement: /* statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2847 "src/parser.tab.c"
        break;

    case YYSYMBOL_assignment: /* assignment  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2853 "src/parser.tab.c"
        break;

    case YYSYMBOL_lvalue: /* lvalue  */
#line 733 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2859 "src/parser.tab.c"
        break;

    case YYSYMBOL_variable_name: /* variable_name  */
#line 732 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2865 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier: /* modifier  */
#line 740 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 2871 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_lens: /* comparison_lens  */
#line 740 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 2877 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_name: /* modifier_name  */
#line 732 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2883 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_word: /* modifier_word  */
#line 732 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2889 "src/parser.tab.c"
        break;

    case YYSYMBOL_print_statement: /* print_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2895 "src/parser.tab.c"
        break;

    case YYSYMBOL_call_statement: /* call_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2901 "src/parser.tab.c"
        break;

    case YYSYMBOL_with_lock_statement: /* with_lock_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2907 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_each_statement: /* for_each_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2913 "src/parser.tab.c"
        break;

    case YYSYMBOL_do_loop_statement: /* do_loop_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2919 "src/parser.tab.c"
        break;

    case YYSYMBOL_while_statement: /* while_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2925 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement: /* consider_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2931 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_branch_list: /* consider_branch_list  */
#line 738 "src/parser.y"
            { ast_free_consider_branch_list(((*yyvaluep).consider_branch_list)); }
#line 2937 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_else_opt: /* consider_else_opt  */
#line 735 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2943 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement_list: /* consider_statement_list  */
#line 735 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2949 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_body_statement: /* consider_body_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2955 "src/parser.tab.c"
        break;

    case YYSYMBOL_function_statement: /* function_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2961 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_statement: /* modifier_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2967 "src/parser.tab.c"
        break;

    case YYSYMBOL_program_statement: /* program_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2973 "src/parser.tab.c"
        break;

    case YYSYMBOL_library_statement: /* library_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2979 "src/parser.tab.c"
        break;

    case YYSYMBOL_use_statement: /* use_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2985 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_signature: /* modifier_signature  */
#line 741 "src/parser.y"
            { ast_free_modifier_signature(((*yyvaluep).modifier_signature)); }
#line 2991 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_context: /* modifier_context  */
#line 732 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2997 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_statement: /* watch_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3003 "src/parser.tab.c"
        break;

    case YYSYMBOL_unwatch_statement: /* unwatch_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3009 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_list: /* watch_target_list  */
#line 739 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3015 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_statement: /* server_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3021 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item_list: /* server_item_list  */
#line 745 "src/parser.y"
            { ast_free_server_item_list(((*yyvaluep).server_item_list)); }
#line 3027 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item: /* server_item  */
#line 744 "src/parser.y"
            { AstServerItemList one = ast_server_item_list_append(ast_server_item_list_empty(), ((*yyvaluep).server_item)); ast_free_server_item_list(one); }
#line 3033 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_string_list: /* server_string_list  */
#line 739 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3039 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_path: /* watch_target_path  */
#line 732 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3045 "src/parser.tab.c"
        break;

    case YYSYMBOL_without_watchers_statement: /* without_watchers_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3051 "src/parser.tab.c"
        break;

    case YYSYMBOL_on_error_statement: /* on_error_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3057 "src/parser.tab.c"
        break;

    case YYSYMBOL_error_statement: /* error_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3063 "src/parser.tab.c"
        break;

    case YYSYMBOL_return_statement: /* return_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3069 "src/parser.tab.c"
        break;

    case YYSYMBOL_label_statement: /* label_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3075 "src/parser.tab.c"
        break;

    case YYSYMBOL_goto_statement: /* goto_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3081 "src/parser.tab.c"
        break;

    case YYSYMBOL_gosub_statement: /* gosub_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3087 "src/parser.tab.c"
        break;

    case YYSYMBOL_break_statement: /* break_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3093 "src/parser.tab.c"
        break;

    case YYSYMBOL_continue_statement: /* continue_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3099 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_statement: /* if_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3105 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_block_tail: /* if_block_tail  */
#line 735 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3111 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_inline_tail: /* if_inline_tail  */
#line 735 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3117 "src/parser.tab.c"
        break;

    case YYSYMBOL_inline_statement: /* inline_statement  */
#line 734 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3123 "src/parser.tab.c"
        break;

    case YYSYMBOL_expression: /* expression  */
#line 733 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3129 "src/parser.tab.c"
        break;

    case YYSYMBOL_or_expression: /* or_expression  */
#line 733 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3135 "src/parser.tab.c"
        break;

    case YYSYMBOL_and_expression: /* and_expression  */
#line 733 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3141 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_expression: /* comparison_expression  */
#line 733 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3147 "src/parser.tab.c"
        break;

    case YYSYMBOL_additive_expression: /* additive_expression  */
#line 733 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3153 "src/parser.tab.c"
        break;

    case YYSYMBOL_multiplicative_expression: /* multiplicative_expression  */
#line 733 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3159 "src/parser.tab.c"
        break;

    case YYSYMBOL_unary_expression: /* unary_expression  */
#line 733 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3165 "src/parser.tab.c"
        break;

    case YYSYMBOL_postfix_expression: /* postfix_expression  */
#line 733 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3171 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_operator: /* comparison_operator  */
#line 732 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3177 "src/parser.tab.c"
        break;

    case YYSYMBOL_primary: /* primary  */
#line 733 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3183 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_literal: /* record_literal  */
#line 733 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3189 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_suffix: /* ident_suffix  */
#line 742 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3195 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_dot_suffix: /* ident_dot_suffix  */
#line 742 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3201 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list_opt: /* argument_list_opt  */
#line 736 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3207 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list: /* argument_list  */
#line 736 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3213 "src/parser.tab.c"
        break;

    case YYSYMBOL_array_argument_list: /* array_argument_list  */
#line 736 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3219 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list_opt: /* parameter_list_opt  */
#line 739 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3225 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list: /* parameter_list  */
#line 739 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3231 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_name: /* field_name  */
#line 732 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3237 "src/parser.tab.c"
        break;

    case YYSYMBOL_dot_field_name: /* dot_field_name  */
#line 732 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3243 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_field_list: /* record_field_list  */
#line 737 "src/parser.y"
            { ast_free_record_field_list(((*yyvaluep).record_field_list)); }
#line 3249 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_policy: /* field_policy  */
#line 743 "src/parser.y"
            { ast_free_expr(((*yyvaluep).field_policy).reset_expr); }
#line 3255 "src/parser.tab.c"
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
#line 760 "src/parser.y"
                     { ctx->parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3561 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 764 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3567 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 765 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3573 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 766 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3579 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 770 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3585 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 771 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3591 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 772 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3597 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 773 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3603 "src/parser.tab.c"
    break;

  case 10: /* statement: for_each_statement  */
#line 774 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3609 "src/parser.tab.c"
    break;

  case 11: /* statement: while_statement  */
#line 775 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3615 "src/parser.tab.c"
    break;

  case 12: /* statement: do_loop_statement  */
#line 776 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3621 "src/parser.tab.c"
    break;

  case 13: /* statement: consider_statement  */
#line 777 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3627 "src/parser.tab.c"
    break;

  case 14: /* statement: function_statement  */
#line 778 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3633 "src/parser.tab.c"
    break;

  case 15: /* statement: modifier_statement  */
#line 779 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3639 "src/parser.tab.c"
    break;

  case 16: /* statement: program_statement  */
#line 780 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3645 "src/parser.tab.c"
    break;

  case 17: /* statement: library_statement  */
#line 781 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3651 "src/parser.tab.c"
    break;

  case 18: /* statement: use_statement NEWLINE  */
#line 782 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3657 "src/parser.tab.c"
    break;

  case 19: /* statement: watch_statement  */
#line 783 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3663 "src/parser.tab.c"
    break;

  case 20: /* statement: server_statement  */
#line 784 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3669 "src/parser.tab.c"
    break;

  case 21: /* statement: unwatch_statement NEWLINE  */
#line 785 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3675 "src/parser.tab.c"
    break;

  case 22: /* statement: without_watchers_statement  */
#line 786 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3681 "src/parser.tab.c"
    break;

  case 23: /* statement: on_error_statement NEWLINE  */
#line 787 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3687 "src/parser.tab.c"
    break;

  case 24: /* statement: error_statement NEWLINE  */
#line 788 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3693 "src/parser.tab.c"
    break;

  case 25: /* statement: return_statement NEWLINE  */
#line 789 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3699 "src/parser.tab.c"
    break;

  case 26: /* statement: label_statement NEWLINE  */
#line 790 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3705 "src/parser.tab.c"
    break;

  case 27: /* statement: goto_statement NEWLINE  */
#line 791 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3711 "src/parser.tab.c"
    break;

  case 28: /* statement: gosub_statement NEWLINE  */
#line 792 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3717 "src/parser.tab.c"
    break;

  case 29: /* statement: break_statement NEWLINE  */
#line 793 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3723 "src/parser.tab.c"
    break;

  case 30: /* statement: continue_statement NEWLINE  */
#line 794 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3729 "src/parser.tab.c"
    break;

  case 31: /* statement: if_statement  */
#line 795 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3735 "src/parser.tab.c"
    break;

  case 32: /* assignment: lvalue OP_EQ expression  */
#line 799 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 3741 "src/parser.tab.c"
    break;

  case 33: /* assignment: lvalue modifier OP_EQ expression  */
#line 800 "src/parser.y"
                                       {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 3755 "src/parser.tab.c"
    break;

  case 34: /* lvalue: variable_name  */
#line 812 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3761 "src/parser.tab.c"
    break;

  case 35: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 813 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3767 "src/parser.tab.c"
    break;

  case 36: /* lvalue: lvalue DOT dot_field_name  */
#line 814 "src/parser.y"
                                             { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3773 "src/parser.tab.c"
    break;

  case 37: /* variable_name: IDENT  */
#line 818 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 3779 "src/parser.tab.c"
    break;

  case 38: /* variable_name: END  */
#line 819 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 3785 "src/parser.tab.c"
    break;

  case 39: /* variable_name: NEXT  */
#line 820 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 3791 "src/parser.tab.c"
    break;

  case 40: /* variable_name: LOOP  */
#line 825 "src/parser.y"
                        { (yyval.text) = copy_const("loop"); }
#line 3797 "src/parser.tab.c"
    break;

  case 41: /* variable_name: UNTIL  */
#line 826 "src/parser.y"
                         { (yyval.text) = copy_const("until"); }
#line 3803 "src/parser.tab.c"
    break;

  case 42: /* modifier: MOD_LPAREN MOD_CONTENT  */
#line 830 "src/parser.y"
                             { (yyval.modifier) = parse_modifier_use((yyvsp[0].text)); }
#line 3809 "src/parser.tab.c"
    break;

  case 43: /* $@1: %empty  */
#line 834 "src/parser.y"
             { lexer_begin_lens_content(ctx->active_lexer); }
#line 3815 "src/parser.tab.c"
    break;

  case 44: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 834 "src/parser.y"
                                                                                  {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 3823 "src/parser.tab.c"
    break;

  case 45: /* modifier_name: modifier_word  */
#line 840 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 3829 "src/parser.tab.c"
    break;

  case 46: /* modifier_name: modifier_name modifier_word  */
#line 841 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 3835 "src/parser.tab.c"
    break;

  case 47: /* modifier_word: IDENT  */
#line 845 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 3841 "src/parser.tab.c"
    break;

  case 48: /* modifier_word: TO  */
#line 846 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 3847 "src/parser.tab.c"
    break;

  case 49: /* modifier_word: END  */
#line 847 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 3853 "src/parser.tab.c"
    break;

  case 50: /* modifier_word: NEXT  */
#line 848 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 3859 "src/parser.tab.c"
    break;

  case 51: /* print_statement: PRINT expression  */
#line 852 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 3865 "src/parser.tab.c"
    break;

  case 52: /* print_statement: PRINT TO ERROR_VALUE expression  */
#line 858 "src/parser.y"
                                      { (yyval.stmt) = ast_print_error((yyvsp[0].expr)); }
#line 3871 "src/parser.tab.c"
    break;

  case 53: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 862 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 3877 "src/parser.tab.c"
    break;

  case 54: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 863 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 3888 "src/parser.tab.c"
    break;

  case 55: /* call_statement: lvalue DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 869 "src/parser.y"
                                                       {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 3898 "src/parser.tab.c"
    break;

  case 56: /* call_statement: lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 874 "src/parser.y"
                                                                 {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 3912 "src/parser.tab.c"
    break;

  case 57: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 883 "src/parser.y"
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
#line 3927 "src/parser.tab.c"
    break;

  case 58: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 896 "src/parser.y"
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
#line 3944 "src/parser.tab.c"
    break;

  case 59: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list END FOR NEWLINE  */
#line 911 "src/parser.y"
                                                                     {
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 3952 "src/parser.tab.c"
    break;

  case 60: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list END FOR NEWLINE  */
#line 914 "src/parser.y"
                                                                          {
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 3960 "src/parser.tab.c"
    break;

  case 61: /* for_each_statement: FOR IDENT OP_EQ expression TO expression NEWLINE statement_list END FOR NEWLINE  */
#line 920 "src/parser.y"
                                                                                      {
        (yyval.stmt) = ast_for_range((yyvsp[-9].text), (yyvsp[-7].expr), (yyvsp[-5].expr), NULL, (yyvsp[-3].stmt_list));
      }
#line 3968 "src/parser.tab.c"
    break;

  case 62: /* for_each_statement: FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list END FOR NEWLINE  */
#line 923 "src/parser.y"
                                                                                                      {
        (yyval.stmt) = ast_for_range((yyvsp[-11].text), (yyvsp[-9].expr), (yyvsp[-7].expr), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 3976 "src/parser.tab.c"
    break;

  case 63: /* do_loop_statement: DO NEWLINE statement_list LOOP UNTIL expression NEWLINE  */
#line 931 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_do_loop((yyvsp[-4].stmt_list), (yyvsp[-1].expr), 1);
      }
#line 3984 "src/parser.tab.c"
    break;

  case 64: /* do_loop_statement: DO NEWLINE statement_list LOOP WHILE expression NEWLINE  */
#line 934 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_do_loop((yyvsp[-4].stmt_list), (yyvsp[-1].expr), 0);
      }
#line 3992 "src/parser.tab.c"
    break;

  case 65: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 940 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 4000 "src/parser.tab.c"
    break;

  case 66: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 946 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 4008 "src/parser.tab.c"
    break;

  case 67: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 952 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 4016 "src/parser.tab.c"
    break;

  case 68: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 955 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 4024 "src/parser.tab.c"
    break;

  case 69: /* consider_else_opt: %empty  */
#line 961 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4030 "src/parser.tab.c"
    break;

  case 70: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 962 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 4036 "src/parser.tab.c"
    break;

  case 71: /* consider_statement_list: %empty  */
#line 966 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4042 "src/parser.tab.c"
    break;

  case 72: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 967 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 4048 "src/parser.tab.c"
    break;

  case 73: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 968 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 4054 "src/parser.tab.c"
    break;

  case 74: /* consider_body_statement: assignment NEWLINE  */
#line 972 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4060 "src/parser.tab.c"
    break;

  case 75: /* consider_body_statement: print_statement NEWLINE  */
#line 973 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4066 "src/parser.tab.c"
    break;

  case 76: /* consider_body_statement: call_statement NEWLINE  */
#line 974 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4072 "src/parser.tab.c"
    break;

  case 77: /* consider_body_statement: with_lock_statement  */
#line 975 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4078 "src/parser.tab.c"
    break;

  case 78: /* consider_body_statement: for_each_statement  */
#line 976 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4084 "src/parser.tab.c"
    break;

  case 79: /* consider_body_statement: while_statement  */
#line 977 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4090 "src/parser.tab.c"
    break;

  case 80: /* consider_body_statement: do_loop_statement  */
#line 978 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4096 "src/parser.tab.c"
    break;

  case 81: /* consider_body_statement: consider_statement  */
#line 979 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4102 "src/parser.tab.c"
    break;

  case 82: /* consider_body_statement: function_statement  */
#line 980 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4108 "src/parser.tab.c"
    break;

  case 83: /* consider_body_statement: modifier_statement  */
#line 981 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4114 "src/parser.tab.c"
    break;

  case 84: /* consider_body_statement: program_statement  */
#line 982 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4120 "src/parser.tab.c"
    break;

  case 85: /* consider_body_statement: library_statement  */
#line 983 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4126 "src/parser.tab.c"
    break;

  case 86: /* consider_body_statement: use_statement NEWLINE  */
#line 984 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4132 "src/parser.tab.c"
    break;

  case 87: /* consider_body_statement: watch_statement  */
#line 985 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4138 "src/parser.tab.c"
    break;

  case 88: /* consider_body_statement: unwatch_statement NEWLINE  */
#line 986 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4144 "src/parser.tab.c"
    break;

  case 89: /* consider_body_statement: without_watchers_statement  */
#line 987 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4150 "src/parser.tab.c"
    break;

  case 90: /* consider_body_statement: on_error_statement NEWLINE  */
#line 988 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4156 "src/parser.tab.c"
    break;

  case 91: /* consider_body_statement: error_statement NEWLINE  */
#line 989 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4162 "src/parser.tab.c"
    break;

  case 92: /* consider_body_statement: return_statement NEWLINE  */
#line 990 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4168 "src/parser.tab.c"
    break;

  case 93: /* consider_body_statement: label_statement NEWLINE  */
#line 991 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4174 "src/parser.tab.c"
    break;

  case 94: /* consider_body_statement: goto_statement NEWLINE  */
#line 992 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4180 "src/parser.tab.c"
    break;

  case 95: /* consider_body_statement: gosub_statement NEWLINE  */
#line 993 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4186 "src/parser.tab.c"
    break;

  case 96: /* consider_body_statement: break_statement NEWLINE  */
#line 994 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4192 "src/parser.tab.c"
    break;

  case 97: /* consider_body_statement: continue_statement NEWLINE  */
#line 995 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4198 "src/parser.tab.c"
    break;

  case 98: /* consider_body_statement: if_statement  */
#line 996 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4204 "src/parser.tab.c"
    break;

  case 99: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1000 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4212 "src/parser.tab.c"
    break;

  case 100: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1003 "src/parser.y"
                                                                                                            {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4221 "src/parser.tab.c"
    break;

  case 101: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 1010 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 4229 "src/parser.tab.c"
    break;

  case 102: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 1013 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 4237 "src/parser.tab.c"
    break;

  case 103: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 1019 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4245 "src/parser.tab.c"
    break;

  case 104: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 1025 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 4253 "src/parser.tab.c"
    break;

  case 105: /* use_statement: USE IDENT  */
#line 1031 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4259 "src/parser.tab.c"
    break;

  case 106: /* use_statement: LOAD IDENT  */
#line 1032 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4265 "src/parser.tab.c"
    break;

  case 107: /* use_statement: USE STRING  */
#line 1033 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4271 "src/parser.tab.c"
    break;

  case 108: /* use_statement: LOAD STRING  */
#line 1034 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 4277 "src/parser.tab.c"
    break;

  case 109: /* use_statement: USE IDENT IDENT STRING  */
#line 1035 "src/parser.y"
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
#line 4298 "src/parser.tab.c"
    break;

  case 110: /* use_statement: LOAD IDENT IDENT STRING  */
#line 1051 "src/parser.y"
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
#line 4319 "src/parser.tab.c"
    break;

  case 111: /* modifier_signature: modifier_name  */
#line 1070 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 4325 "src/parser.tab.c"
    break;

  case 112: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 1071 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 4331 "src/parser.tab.c"
    break;

  case 113: /* modifier_context: IDENT  */
#line 1075 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4337 "src/parser.tab.c"
    break;

  case 114: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1079 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4345 "src/parser.tab.c"
    break;

  case 115: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 1082 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 4353 "src/parser.tab.c"
    break;

  case 116: /* watch_statement: WATCH IDENT LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1090 "src/parser.y"
                                                                                           {
        (yyval.stmt) = ast_watch((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4361 "src/parser.tab.c"
    break;

  case 117: /* unwatch_statement: UNWATCH expression  */
#line 1096 "src/parser.y"
                         { (yyval.stmt) = ast_unwatch((yyvsp[0].expr)); }
#line 4367 "src/parser.tab.c"
    break;

  case 118: /* watch_target_list: watch_target_path  */
#line 1100 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4373 "src/parser.tab.c"
    break;

  case 119: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 1101 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4379 "src/parser.tab.c"
    break;

  case 120: /* server_statement: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1120 "src/parser.y"
                                                                                             {
        (yyval.stmt) = ast_server((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4387 "src/parser.tab.c"
    break;

  case 121: /* server_statement: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1123 "src/parser.y"
                                                                           {
        (yyval.stmt) = ast_server((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4395 "src/parser.tab.c"
    break;

  case 122: /* server_item_list: %empty  */
#line 1129 "src/parser.y"
             { (yyval.server_item_list) = ast_server_item_list_empty(); }
#line 4401 "src/parser.tab.c"
    break;

  case 123: /* server_item_list: server_item_list NEWLINE  */
#line 1130 "src/parser.y"
                               { (yyval.server_item_list) = (yyvsp[-1].server_item_list); }
#line 4407 "src/parser.tab.c"
    break;

  case 124: /* server_item_list: server_item_list server_item  */
#line 1131 "src/parser.y"
                                   { (yyval.server_item_list) = ast_server_item_list_append((yyvsp[-1].server_item_list), (yyvsp[0].server_item)); }
#line 4413 "src/parser.tab.c"
    break;

  case 125: /* server_item: IDENT server_string_list NEWLINE  */
#line 1135 "src/parser.y"
                                       {
        (yyval.server_item) = ast_server_directive((yyvsp[-2].text), (yyvsp[-1].name_list), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 4421 "src/parser.tab.c"
    break;

  case 126: /* server_item: IDENT STRING LPAREN parameter_list_opt RPAREN NEWLINE statement_list END IDENT NEWLINE  */
#line 1138 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_handler((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4429 "src/parser.tab.c"
    break;

  case 127: /* server_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1141 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_site((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4437 "src/parser.tab.c"
    break;

  case 128: /* server_item: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1144 "src/parser.y"
                                                                           {
        (yyval.server_item) = ast_server_site((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 4445 "src/parser.tab.c"
    break;

  case 129: /* server_item: ON IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1147 "src/parser.y"
                                                     {
        (yyval.server_item) = ast_server_hook((yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 4453 "src/parser.tab.c"
    break;

  case 130: /* server_string_list: STRING  */
#line 1153 "src/parser.y"
             { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4459 "src/parser.tab.c"
    break;

  case 131: /* server_string_list: server_string_list COMMA STRING  */
#line 1154 "src/parser.y"
                                      { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4465 "src/parser.tab.c"
    break;

  case 132: /* watch_target_path: variable_name  */
#line 1158 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 4471 "src/parser.tab.c"
    break;

  case 133: /* watch_target_path: watch_target_path DOT IDENT  */
#line 1159 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 4477 "src/parser.tab.c"
    break;

  case 134: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 1163 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 4485 "src/parser.tab.c"
    break;

  case 135: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 1169 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 4491 "src/parser.tab.c"
    break;

  case 136: /* on_error_statement: ON ERROR_VALUE GOTO NEXT  */
#line 1170 "src/parser.y"
                               { (yyval.stmt) = ast_on_error_goto_next(); }
#line 4497 "src/parser.tab.c"
    break;

  case 137: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 1171 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 4503 "src/parser.tab.c"
    break;

  case 138: /* on_error_statement: ON IDENT GOTO NEXT  */
#line 1172 "src/parser.y"
                         {
        if (!warn_channel_ok(ctx, (yyvsp[-2].text), (yylsp[-2]).first_line, (yylsp[-2]).first_column)) { YYERROR; }
        free((yyvsp[-2].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_NEXT);
      }
#line 4513 "src/parser.tab.c"
    break;

  case 139: /* on_error_statement: ON IDENT GOTO IDENT  */
#line 1177 "src/parser.y"
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
#line 4531 "src/parser.tab.c"
    break;

  case 140: /* on_error_statement: ON IDENT STOP  */
#line 1190 "src/parser.y"
                    {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_STOP);
      }
#line 4541 "src/parser.tab.c"
    break;

  case 141: /* on_error_statement: ON IDENT PRINT  */
#line 1195 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_PRINT);
      }
#line 4551 "src/parser.tab.c"
    break;

  case 142: /* on_error_statement: ON IDENT IDENT  */
#line 1200 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        int mode = warn_mode_word(ctx, (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column);
        if (mode < 0) { free((yyvsp[-1].text)); free((yyvsp[0].text)); YYERROR; }
        free((yyvsp[-1].text)); free((yyvsp[0].text));
        (yyval.stmt) = ast_on_warning(mode);
      }
#line 4563 "src/parser.tab.c"
    break;

  case 143: /* error_statement: ERROR_VALUE expression  */
#line 1210 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 4569 "src/parser.tab.c"
    break;

  case 144: /* return_statement: RETURN  */
#line 1214 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 4575 "src/parser.tab.c"
    break;

  case 145: /* return_statement: RETURN expression  */
#line 1215 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 4581 "src/parser.tab.c"
    break;

  case 146: /* label_statement: variable_name COLON  */
#line 1219 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 4587 "src/parser.tab.c"
    break;

  case 147: /* goto_statement: GOTO variable_name  */
#line 1226 "src/parser.y"
                         { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 4593 "src/parser.tab.c"
    break;

  case 148: /* gosub_statement: GOSUB variable_name  */
#line 1230 "src/parser.y"
                          { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 4599 "src/parser.tab.c"
    break;

  case 149: /* break_statement: BREAK  */
#line 1234 "src/parser.y"
            { (yyval.stmt) = ast_break(); }
#line 4605 "src/parser.tab.c"
    break;

  case 150: /* continue_statement: CONTINUE  */
#line 1238 "src/parser.y"
               { (yyval.stmt) = ast_continue(); }
#line 4611 "src/parser.tab.c"
    break;

  case 151: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1242 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4620 "src/parser.tab.c"
    break;

  case 152: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1246 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4629 "src/parser.tab.c"
    break;

  case 153: /* if_block_tail: END IF NEWLINE  */
#line 1253 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4637 "src/parser.tab.c"
    break;

  case 154: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 1256 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4645 "src/parser.tab.c"
    break;

  case 155: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1259 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4653 "src/parser.tab.c"
    break;

  case 156: /* if_inline_tail: %empty  */
#line 1265 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4661 "src/parser.tab.c"
    break;

  case 157: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 1268 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4669 "src/parser.tab.c"
    break;

  case 158: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1271 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4677 "src/parser.tab.c"
    break;

  case 159: /* inline_statement: assignment  */
#line 1277 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4683 "src/parser.tab.c"
    break;

  case 160: /* inline_statement: print_statement  */
#line 1278 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4689 "src/parser.tab.c"
    break;

  case 161: /* inline_statement: call_statement  */
#line 1279 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4695 "src/parser.tab.c"
    break;

  case 162: /* inline_statement: use_statement  */
#line 1280 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4701 "src/parser.tab.c"
    break;

  case 163: /* inline_statement: on_error_statement  */
#line 1281 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4707 "src/parser.tab.c"
    break;

  case 164: /* inline_statement: error_statement  */
#line 1282 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4713 "src/parser.tab.c"
    break;

  case 165: /* inline_statement: return_statement  */
#line 1283 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4719 "src/parser.tab.c"
    break;

  case 166: /* inline_statement: goto_statement  */
#line 1284 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4725 "src/parser.tab.c"
    break;

  case 167: /* inline_statement: gosub_statement  */
#line 1285 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4731 "src/parser.tab.c"
    break;

  case 168: /* inline_statement: break_statement  */
#line 1286 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4737 "src/parser.tab.c"
    break;

  case 169: /* inline_statement: continue_statement  */
#line 1287 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4743 "src/parser.tab.c"
    break;

  case 170: /* expression: or_expression  */
#line 1291 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 4749 "src/parser.tab.c"
    break;

  case 171: /* or_expression: and_expression  */
#line 1295 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 4755 "src/parser.tab.c"
    break;

  case 172: /* or_expression: or_expression OR and_expression  */
#line 1296 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4761 "src/parser.tab.c"
    break;

  case 173: /* and_expression: comparison_expression  */
#line 1300 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 4767 "src/parser.tab.c"
    break;

  case 174: /* and_expression: and_expression AND comparison_expression  */
#line 1301 "src/parser.y"
                                               { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4773 "src/parser.tab.c"
    break;

  case 175: /* comparison_expression: additive_expression  */
#line 1305 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 4779 "src/parser.tab.c"
    break;

  case 176: /* comparison_expression: additive_expression comparison_operator additive_expression  */
#line 1306 "src/parser.y"
                                                                  { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4785 "src/parser.tab.c"
    break;

  case 177: /* comparison_expression: additive_expression comparison_lens comparison_operator additive_expression  */
#line 1307 "src/parser.y"
                                                                                  {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 4793 "src/parser.tab.c"
    break;

  case 178: /* comparison_expression: additive_expression modifier comparison_operator additive_expression  */
#line 1310 "src/parser.y"
                                                                           {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 4807 "src/parser.tab.c"
    break;

  case 179: /* additive_expression: multiplicative_expression  */
#line 1322 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 4813 "src/parser.tab.c"
    break;

  case 180: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 1323 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4819 "src/parser.tab.c"
    break;

  case 181: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 1324 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4825 "src/parser.tab.c"
    break;

  case 182: /* multiplicative_expression: unary_expression  */
#line 1328 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 4831 "src/parser.tab.c"
    break;

  case 183: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 1329 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4837 "src/parser.tab.c"
    break;

  case 184: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 1330 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4843 "src/parser.tab.c"
    break;

  case 185: /* unary_expression: postfix_expression  */
#line 1334 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 4849 "src/parser.tab.c"
    break;

  case 186: /* unary_expression: NOT unary_expression  */
#line 1335 "src/parser.y"
                           { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4855 "src/parser.tab.c"
    break;

  case 187: /* unary_expression: MINUS unary_expression  */
#line 1336 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4861 "src/parser.tab.c"
    break;

  case 188: /* unary_expression: NEW postfix_expression  */
#line 1337 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4867 "src/parser.tab.c"
    break;

  case 189: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 1338 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 4873 "src/parser.tab.c"
    break;

  case 190: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 1339 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4879 "src/parser.tab.c"
    break;

  case 191: /* postfix_expression: primary  */
#line 1343 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 4885 "src/parser.tab.c"
    break;

  case 192: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 1344 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4891 "src/parser.tab.c"
    break;

  case 193: /* postfix_expression: postfix_expression DOT dot_field_name  */
#line 1345 "src/parser.y"
                                            { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4897 "src/parser.tab.c"
    break;

  case 194: /* postfix_expression: postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1346 "src/parser.y"
                                                                   {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        (yyval.expr) = expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 4907 "src/parser.tab.c"
    break;

  case 195: /* postfix_expression: postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1351 "src/parser.y"
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
#line 4923 "src/parser.tab.c"
    break;

  case 196: /* comparison_operator: OP_EQ  */
#line 1365 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 4929 "src/parser.tab.c"
    break;

  case 197: /* comparison_operator: OP_NE  */
#line 1366 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 4935 "src/parser.tab.c"
    break;

  case 198: /* comparison_operator: OP_GT  */
#line 1367 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 4941 "src/parser.tab.c"
    break;

  case 199: /* comparison_operator: OP_LT  */
#line 1368 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 4947 "src/parser.tab.c"
    break;

  case 200: /* comparison_operator: OP_GE  */
#line 1369 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 4953 "src/parser.tab.c"
    break;

  case 201: /* comparison_operator: OP_LE  */
#line 1370 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 4959 "src/parser.tab.c"
    break;

  case 202: /* comparison_operator: OP_NGT  */
#line 1371 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 4965 "src/parser.tab.c"
    break;

  case 203: /* comparison_operator: OP_NLT  */
#line 1372 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 4971 "src/parser.tab.c"
    break;

  case 204: /* comparison_operator: OP_NGE  */
#line 1373 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 4977 "src/parser.tab.c"
    break;

  case 205: /* comparison_operator: OP_NLE  */
#line 1374 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 4983 "src/parser.tab.c"
    break;

  case 206: /* primary: NUMBER  */
#line 1378 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4989 "src/parser.tab.c"
    break;

  case 207: /* primary: WATCHERS LPAREN RPAREN  */
#line 1379 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_call(copy_const("watchers"), ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4995 "src/parser.tab.c"
    break;

  case 208: /* primary: duration_terms  */
#line 1380 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5001 "src/parser.tab.c"
    break;

  case 209: /* primary: STRING  */
#line 1381 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5007 "src/parser.tab.c"
    break;

  case 210: /* primary: variable_name ident_suffix  */
#line 1382 "src/parser.y"
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
#line 5029 "src/parser.tab.c"
    break;

  case 211: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1399 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 5040 "src/parser.tab.c"
    break;

  case 212: /* primary: ERROR_VALUE  */
#line 1405 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5046 "src/parser.tab.c"
    break;

  case 213: /* primary: TRUE  */
#line 1406 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5052 "src/parser.tab.c"
    break;

  case 214: /* primary: FALSE  */
#line 1407 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5058 "src/parser.tab.c"
    break;

  case 215: /* primary: NOTHING  */
#line 1408 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5064 "src/parser.tab.c"
    break;

  case 216: /* primary: UNKNOWN_VALUE  */
#line 1409 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5070 "src/parser.tab.c"
    break;

  case 217: /* primary: LPAREN expression RPAREN  */
#line 1410 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 5076 "src/parser.tab.c"
    break;

  case 218: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 1411 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5082 "src/parser.tab.c"
    break;

  case 219: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 1412 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5088 "src/parser.tab.c"
    break;

  case 220: /* primary: record_literal  */
#line 1413 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5094 "src/parser.tab.c"
    break;

  case 221: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 1417 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5100 "src/parser.tab.c"
    break;

  case 222: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 1418 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5106 "src/parser.tab.c"
    break;

  case 223: /* ident_suffix: %empty  */
#line 1422 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 5116 "src/parser.tab.c"
    break;

  case 224: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 1427 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5126 "src/parser.tab.c"
    break;

  case 225: /* ident_suffix: DOT dot_field_name ident_dot_suffix  */
#line 1432 "src/parser.y"
                                          {
        /* dot_field_name, not IDENT: a keyword is a legal FIELD name after a
         * dot, because nothing but a name can appear there. */
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 5137 "src/parser.tab.c"
    break;

  case 226: /* ident_suffix: DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1438 "src/parser.y"
                                                          {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        (yyval.ident_suffix).kind = IDENT_SUFFIX_METHOD;
        (yyval.ident_suffix).name = (yyvsp[-3].text);
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5151 "src/parser.tab.c"
    break;

  case 227: /* ident_dot_suffix: %empty  */
#line 1450 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 5161 "src/parser.tab.c"
    break;

  case 228: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 1455 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5171 "src/parser.tab.c"
    break;

  case 229: /* duration_terms: NUMBER IDENT  */
#line 1463 "src/parser.y"
                   {
        AstDuration duration = {0};
        (yyval.duration) = duration_add_unit(duration, (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 5180 "src/parser.tab.c"
    break;

  case 230: /* duration_terms: duration_terms NUMBER IDENT  */
#line 1467 "src/parser.y"
                                  {
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 5188 "src/parser.tab.c"
    break;

  case 231: /* argument_list_opt: %empty  */
#line 1473 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 5194 "src/parser.tab.c"
    break;

  case 232: /* argument_list_opt: argument_list  */
#line 1474 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 5200 "src/parser.tab.c"
    break;

  case 233: /* argument_list: expression  */
#line 1478 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 5206 "src/parser.tab.c"
    break;

  case 234: /* argument_list: argument_list COMMA expression  */
#line 1479 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 5212 "src/parser.tab.c"
    break;

  case 235: /* array_argument_list: expression  */
#line 1483 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 5218 "src/parser.tab.c"
    break;

  case 236: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 1484 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 5224 "src/parser.tab.c"
    break;

  case 237: /* parameter_list_opt: %empty  */
#line 1488 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 5230 "src/parser.tab.c"
    break;

  case 238: /* parameter_list_opt: parameter_list  */
#line 1489 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 5236 "src/parser.tab.c"
    break;

  case 239: /* parameter_list: IDENT  */
#line 1493 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5242 "src/parser.tab.c"
    break;

  case 240: /* parameter_list: parameter_list COMMA IDENT  */
#line 1494 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5248 "src/parser.tab.c"
    break;

  case 241: /* field_name: dot_field_name  */
#line 1507 "src/parser.y"
                     { (yyval.text) = (yyvsp[0].text); }
#line 5254 "src/parser.tab.c"
    break;

  case 242: /* field_name: STRING  */
#line 1514 "src/parser.y"
             { (yyval.text) = (yyvsp[0].text); }
#line 5260 "src/parser.tab.c"
    break;

  case 243: /* dot_field_name: IDENT  */
#line 1523 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 5266 "src/parser.tab.c"
    break;

  case 244: /* dot_field_name: AS  */
#line 1524 "src/parser.y"
                     { (yyval.text) = kw_name("as"); }
#line 5272 "src/parser.tab.c"
    break;

  case 245: /* dot_field_name: NEXT  */
#line 1525 "src/parser.y"
                     { (yyval.text) = kw_name("next"); }
#line 5278 "src/parser.tab.c"
    break;

  case 246: /* dot_field_name: STOP  */
#line 1526 "src/parser.y"
                     { (yyval.text) = kw_name("stop"); }
#line 5284 "src/parser.tab.c"
    break;

  case 247: /* dot_field_name: ERROR_VALUE  */
#line 1527 "src/parser.y"
                     { (yyval.text) = kw_name("error"); }
#line 5290 "src/parser.tab.c"
    break;

  case 248: /* dot_field_name: END  */
#line 1528 "src/parser.y"
                     { (yyval.text) = kw_name("end"); }
#line 5296 "src/parser.tab.c"
    break;

  case 249: /* dot_field_name: TO  */
#line 1529 "src/parser.y"
                     { (yyval.text) = kw_name("to"); }
#line 5302 "src/parser.tab.c"
    break;

  case 250: /* dot_field_name: IN  */
#line 1530 "src/parser.y"
                     { (yyval.text) = kw_name("in"); }
#line 5308 "src/parser.tab.c"
    break;

  case 251: /* dot_field_name: ON  */
#line 1531 "src/parser.y"
                     { (yyval.text) = kw_name("on"); }
#line 5314 "src/parser.tab.c"
    break;

  case 252: /* dot_field_name: NEW  */
#line 1532 "src/parser.y"
                     { (yyval.text) = kw_name("new"); }
#line 5320 "src/parser.tab.c"
    break;

  case 253: /* dot_field_name: EACH  */
#line 1533 "src/parser.y"
                     { (yyval.text) = kw_name("each"); }
#line 5326 "src/parser.tab.c"
    break;

  case 254: /* dot_field_name: WITH  */
#line 1534 "src/parser.y"
                     { (yyval.text) = kw_name("with"); }
#line 5332 "src/parser.tab.c"
    break;

  case 255: /* dot_field_name: WITHOUT  */
#line 1535 "src/parser.y"
                     { (yyval.text) = kw_name("without"); }
#line 5338 "src/parser.tab.c"
    break;

  case 256: /* dot_field_name: THEN  */
#line 1536 "src/parser.y"
                     { (yyval.text) = kw_name("then"); }
#line 5344 "src/parser.tab.c"
    break;

  case 257: /* dot_field_name: ELSE  */
#line 1537 "src/parser.y"
                     { (yyval.text) = kw_name("else"); }
#line 5350 "src/parser.tab.c"
    break;

  case 258: /* dot_field_name: FOR  */
#line 1538 "src/parser.y"
                     { (yyval.text) = kw_name("for"); }
#line 5356 "src/parser.tab.c"
    break;

  case 259: /* dot_field_name: IF  */
#line 1539 "src/parser.y"
                     { (yyval.text) = kw_name("if"); }
#line 5362 "src/parser.tab.c"
    break;

  case 260: /* dot_field_name: WHILE  */
#line 1540 "src/parser.y"
                     { (yyval.text) = kw_name("while"); }
#line 5368 "src/parser.tab.c"
    break;

  case 261: /* dot_field_name: DO  */
#line 1541 "src/parser.y"
                     { (yyval.text) = kw_name("do"); }
#line 5374 "src/parser.tab.c"
    break;

  case 262: /* dot_field_name: LOOP  */
#line 1542 "src/parser.y"
                     { (yyval.text) = kw_name("loop"); }
#line 5380 "src/parser.tab.c"
    break;

  case 263: /* dot_field_name: UNTIL  */
#line 1543 "src/parser.y"
                     { (yyval.text) = kw_name("until"); }
#line 5386 "src/parser.tab.c"
    break;

  case 264: /* dot_field_name: PRINT  */
#line 1544 "src/parser.y"
                     { (yyval.text) = kw_name("print"); }
#line 5392 "src/parser.tab.c"
    break;

  case 265: /* dot_field_name: RETURN  */
#line 1545 "src/parser.y"
                     { (yyval.text) = kw_name("return"); }
#line 5398 "src/parser.tab.c"
    break;

  case 266: /* dot_field_name: LOAD  */
#line 1546 "src/parser.y"
                     { (yyval.text) = kw_name("load"); }
#line 5404 "src/parser.tab.c"
    break;

  case 267: /* dot_field_name: USE  */
#line 1547 "src/parser.y"
                     { (yyval.text) = kw_name("use"); }
#line 5410 "src/parser.tab.c"
    break;

  case 268: /* dot_field_name: NOT  */
#line 1548 "src/parser.y"
                     { (yyval.text) = kw_name("not"); }
#line 5416 "src/parser.tab.c"
    break;

  case 269: /* dot_field_name: AND  */
#line 1549 "src/parser.y"
                     { (yyval.text) = kw_name("and"); }
#line 5422 "src/parser.tab.c"
    break;

  case 270: /* dot_field_name: OR  */
#line 1550 "src/parser.y"
                     { (yyval.text) = kw_name("or"); }
#line 5428 "src/parser.tab.c"
    break;

  case 271: /* dot_field_name: TRUE  */
#line 1551 "src/parser.y"
                     { (yyval.text) = kw_name("true"); }
#line 5434 "src/parser.tab.c"
    break;

  case 272: /* dot_field_name: FALSE  */
#line 1552 "src/parser.y"
                     { (yyval.text) = kw_name("false"); }
#line 5440 "src/parser.tab.c"
    break;

  case 273: /* dot_field_name: NOTHING  */
#line 1553 "src/parser.y"
                     { (yyval.text) = kw_name("nothing"); }
#line 5446 "src/parser.tab.c"
    break;

  case 274: /* dot_field_name: BREAK  */
#line 1554 "src/parser.y"
                     { (yyval.text) = kw_name("break"); }
#line 5452 "src/parser.tab.c"
    break;

  case 275: /* dot_field_name: CONTINUE  */
#line 1555 "src/parser.y"
                     { (yyval.text) = kw_name("continue"); }
#line 5458 "src/parser.tab.c"
    break;

  case 276: /* dot_field_name: GOTO  */
#line 1556 "src/parser.y"
                     { (yyval.text) = kw_name("goto"); }
#line 5464 "src/parser.tab.c"
    break;

  case 277: /* dot_field_name: GOSUB  */
#line 1557 "src/parser.y"
                     { (yyval.text) = kw_name("gosub"); }
#line 5470 "src/parser.tab.c"
    break;

  case 278: /* dot_field_name: SPAWN  */
#line 1558 "src/parser.y"
                     { (yyval.text) = kw_name("spawn"); }
#line 5476 "src/parser.tab.c"
    break;

  case 279: /* dot_field_name: EXPORT  */
#line 1559 "src/parser.y"
                     { (yyval.text) = kw_name("export"); }
#line 5482 "src/parser.tab.c"
    break;

  case 280: /* dot_field_name: LIBRARY  */
#line 1560 "src/parser.y"
                     { (yyval.text) = kw_name("library"); }
#line 5488 "src/parser.tab.c"
    break;

  case 281: /* dot_field_name: FUNCTION  */
#line 1561 "src/parser.y"
                     { (yyval.text) = kw_name("function"); }
#line 5494 "src/parser.tab.c"
    break;

  case 282: /* dot_field_name: MODIFIER  */
#line 1562 "src/parser.y"
                     { (yyval.text) = kw_name("modifier"); }
#line 5500 "src/parser.tab.c"
    break;

  case 283: /* dot_field_name: PROGRAM  */
#line 1563 "src/parser.y"
                     { (yyval.text) = kw_name("program"); }
#line 5506 "src/parser.tab.c"
    break;

  case 284: /* dot_field_name: WATCH  */
#line 1564 "src/parser.y"
                     { (yyval.text) = kw_name("watch"); }
#line 5512 "src/parser.tab.c"
    break;

  case 285: /* dot_field_name: WATCHERS  */
#line 1565 "src/parser.y"
                     { (yyval.text) = kw_name("watchers"); }
#line 5518 "src/parser.tab.c"
    break;

  case 286: /* dot_field_name: CONSIDER  */
#line 1566 "src/parser.y"
                     { (yyval.text) = kw_name("consider"); }
#line 5524 "src/parser.tab.c"
    break;

  case 287: /* dot_field_name: STEP  */
#line 1567 "src/parser.y"
                     { (yyval.text) = kw_name("step"); }
#line 5530 "src/parser.tab.c"
    break;

  case 288: /* dot_field_name: UNWATCH  */
#line 1568 "src/parser.y"
                     { (yyval.text) = kw_name("unwatch"); }
#line 5536 "src/parser.tab.c"
    break;

  case 289: /* dot_field_name: UNKNOWN_VALUE  */
#line 1569 "src/parser.y"
                     { (yyval.text) = kw_name("unknown"); }
#line 5542 "src/parser.tab.c"
    break;

  case 290: /* record_field_list: field_name OP_EQ expression  */
#line 1573 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5548 "src/parser.tab.c"
    break;

  case 291: /* record_field_list: field_name COLON expression  */
#line 1574 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5554 "src/parser.tab.c"
    break;

  case 292: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1575 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5560 "src/parser.tab.c"
    break;

  case 293: /* record_field_list: record_field_list COMMA optional_newlines field_name OP_EQ expression  */
#line 1576 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5566 "src/parser.tab.c"
    break;

  case 294: /* record_field_list: record_field_list COMMA optional_newlines field_name COLON expression  */
#line 1577 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5572 "src/parser.tab.c"
    break;

  case 295: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1578 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5578 "src/parser.tab.c"
    break;

  case 296: /* field_policy: IDENT  */
#line 1586 "src/parser.y"
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

  case 297: /* field_policy: IDENT expression  */
#line 1613 "src/parser.y"
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

#line 1636 "src/parser.y"


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
