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

#include <math.h>
#include "ast.h"
#include "gb_decimal.h"
#include "builtins.h"
#include "diagnostics.h"
#include "lexer.h"
#include "parse_ctx.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

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
/* A WARNING-severity diagnostic, which the sink has been able to carry since it
 * was written (gb_severity has ERROR/WARNING/NOTE, gb_severity_str renders all
 * three, and the JSON writer emits the field) and which NOTHING had ever
 * emitted. It does not set *ok, so the parse continues and the program runs.
 *
 * WHAT THIS CANNOT DO, stated because the limitation is real: `on warning stop`
 * does NOT escalate it. That channel is dynamically scoped over CALL FRAMES and
 * this fires while the file is being parsed, before any statement has run, so
 * there are no frames to consult. A project that wants unknown escapes to be
 * fatal has no switch for that today. Filed rather than papered over. */
static void report_diag_warning(gb_parse_ctx *ctx, gb_diag_code code, int line, int column,
                                int end_line, int end_column, const char *message) {
    gb_span span = { line, column, end_line, end_column };
    gb_diagnostics_add(ctx->diags, GB_SEVERITY_WARNING, code, 0,
                       ctx->active_parse_path, span, message);
}

static void report_diag_warning_lexeme(gb_parse_ctx *ctx, gb_diag_code code, int line, int column,
                                       const char *text, int len, const char *message) {
    int end_line = line;
    int end_column = column;
    for (int i = 0; i < len; i++) {
        if (text[i] == '\n') { end_line++; end_column = 1; } else { end_column++; }
    }
    report_diag_warning(ctx, code, line, column, end_line, end_column, message);
}

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
            } else if (start[i] == 'r') {
                /* CRLF is the line ending of most files a business receives,
                 * so `"\r\n"` is written often enough to need no chr(13)
                 * (gbasic-books volume 3, gap G5). It used to be an unknown
                 * escape: the two characters, and a warning. */
                text[out++] = '\r';
            } else if (start[i] == 'f') {
                /* A form feed is how a print-image report marks a page, and
                 * reading those reports is ARI's whole job (gap G29). */
                text[out++] = '\f';
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
                /* AN UNKNOWN ESCAPE KEEPS BOTH CHARACTERS AND WARNS (2026-09-30).
                 *
                 * It used to fail the parse, which made every regex in
                 * docs/text_design.md untypable -- `"\\$([0-9,]+)"` died on the
                 * `\\$` before the regex engine saw it, while the same document
                 * correctly states the regex dialect accepts `\\d`. Two true
                 * statements about different layers, and nothing said so.
                 *
                 * KEEPING BOTH CHARACTERS is what makes those patterns work:
                 * `\\d` in the source becomes the two bytes the regex engine
                 * wants. Passing them through SILENTLY was the other option and
                 * was rejected -- an unknown escape is also a good typo
                 * detector, so it is reported and the author decides.
                 *
                 * The pass-through never grows the buffer: two characters in,
                 * two out, where every other escape shrinks. */
                char message[96];
                snprintf(message, sizeof(message),
                         "unknown escape \\%c kept as the two characters; write \\\\%c to say so",
                         start[i], start[i]);
                report_diag_warning_lexeme(ctx, GB_DIAG_STRING_LITERAL, line, column,
                                           start, length, message);
                text[out++] = '\\';
                text[out++] = start[i];
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

/* The next `;` that SEPARATES STAGES, i.e. one not inside a string literal, or
 * NULL if there is none.
 *
 * DELIBERATELY A SECOND COPY of the same walk `modifier_args_next_comma` does
 * in src/eval.c, on the same terms the file already states for
 * `eval_modifier_arg_text` mirroring `copy_string_literal`: the eval-side
 * scanner cannot be reached from here. Held in step BY TEST rather than by
 * hope -- a clause carrying a `;` inside an argument (`{join "; "; trimmed}`)
 * drives both, and a change to one that is not made to the other fails. */
static const char *modifier_next_stage(const char *text) {
    int in_string = 0;
    int escape = 0;
    for (const char *p = text; *p; p++) {
        if (escape) {
            escape = 0;
            continue;
        }
        if (*p == '\\' && in_string) {
            escape = 1;
            continue;
        }
        if (*p == '"') {
            in_string = !in_string;
            continue;
        }
        if (*p == ';' && !in_string) {
            return p;
        }
    }
    return NULL;
}

static AstModifierUse parse_modifier_use_one(char *text);

/* A clause is a CHAIN OF STAGES separated by a top-level `;`, applied left to
 * right. The comma could not be used: it is already the ARGUMENT separator
 * (`{between "a", "b"}`), and arity cannot disambiguate the two because
 * optional arguments exist -- measured, see §9 of
 * docs/brace_modifier_design.md. `;` is unclaimed in gBASIC: it is not a token
 * at all, so `x = 1; y = 2` is a LEXER error, and nothing in the language
 * competes for it.
 *
 * SPLIT HERE RATHER THAN AT APPLY TIME because each stage may carry its own
 * library qualifier (`{housestyle.shout; trimmed}`), and the qualifier split
 * below would otherwise take the first `.` in the whole phrase. */
static AstModifierUse parse_modifier_use(char *text) {
    const char *sep = modifier_next_stage(text);
    if (!sep) {
        return parse_modifier_use_one(text);
    }

    /* EACH STAGE IS TRIMMED. `{trimmed; caseless}` is how anybody writes it, and
     * the space after the `;` would otherwise ride along in the name: declared
     * modifiers survive that (`modifier_phrase_matches` skips leading space)
     * but the BUILT-IN lenses are compared exactly, so the first run of this
     * reported `compare modifier not found:  caseless` -- with the space
     * visible in the message, naming a modifier that exists. */
    const char *head_start = text;
    while (*head_start == ' ' || *head_start == '\t') {
        head_start++;
    }
    const char *head_end = sep;
    while (head_end > head_start &&
           (head_end[-1] == ' ' || head_end[-1] == '\t')) {
        head_end--;
    }
    size_t head_len = (size_t)(head_end - head_start);
    char *head = malloc(head_len + 1);
    if (!head) {
        abort();
    }
    memcpy(head, head_start, head_len);
    head[head_len] = '\0';

    const char *tail_start = sep + 1;
    while (*tail_start == ' ' || *tail_start == '\t') {
        tail_start++;
    }
    char *tail = copy_const(tail_start);
    free(text);

    AstModifierUse first = parse_modifier_use_one(head);
    AstModifierUse rest = parse_modifier_use(tail);
    AstModifierUse *stage = malloc(sizeof(AstModifierUse));
    if (!stage) {
        abort();
    }
    *stage = rest;
    first.next = stage;
    return first;
}

static AstModifierUse parse_modifier_use_one(char *text) {
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
/* An OPERATOR WORD is not a duration unit, and the reader who wrote one was not
 * writing a duration at all. `7 mod 3` is the case that matters: MOD is an infix
 * operator in QBasic, so it is what a reader of the Core Language book types
 * first, and answering it with "the units are year, month, week..." names seven
 * things none of which is the problem. The reference already knows this trap --
 * "`7 mod 2` is duration syntax, not modulo -- `mod` is a call" -- so the page
 * was right and only the binary was unhelpful.
 *
 * Each entry names a call that EXISTS, measured 2026-09-30: mod(-7,3) is 2,
 * bxor(6,3) is 5, floor(7/2) is 3. `shl`/`shr` are deliberately ABSENT: gBASIC
 * has no shift builtin, so there is nothing to name, and a hint pointing at
 * something that does not exist is the web.configure mistake. They keep the
 * duration message, which is at least true.
 *
 * This cannot fire wrongly: every word here is one no duration unit spells. */
static const char *operator_word_remedy(const char *unit) {
    if (strcmp(unit, "mod") == 0) return "the remainder is mod(a, b)";
    if (strcmp(unit, "xor") == 0) return "bitwise XOR is bxor(a, b)";
    if (strcmp(unit, "div") == 0) return "integer division is floor(a / b)";
    return NULL;
}

static void duration_unit_error(gb_parse_ctx *ctx, char *unit,
                                int line, int column, int end_line, int end_column) {
    char message[256];
    char lowered[32];
    size_t i = 0;
    for (; unit[i] && i + 1 < sizeof(lowered); i++) {
        lowered[i] = (char)tolower((unsigned char)unit[i]);
    }
    lowered[i] = '\0';

    const char *remedy = operator_word_remedy(lowered);
    if (remedy != NULL) {
        /* Named in the spelling the AUTHOR typed -- `7 MOD 3` is how it is
         * written in the language they are coming from, and nobody should have to
         * discover that the diagnostic lower-cased their word. Same rule the
         * reserved-word diagnostics follow. */
        snprintf(message, sizeof(message),
                 "'%s' is not an operator in gBASIC; %s", unit, remedy);
    } else {
        snprintf(message, sizeof(message),
                 "unknown duration unit '%s' -- the units are year, month, week, day, "
                 "hour, minute and second, singular or plural", unit);
    }
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
    int was_cells = duration.cells;
    int terms_before = duration.years || duration.months || duration.weeks || duration.days ||
                       duration.hours || duration.minutes || duration.seconds || was_cells;
    if (unit_is(unit, "columns") || unit_is(unit, "column") ||
        unit_is(unit, "rows") || unit_is(unit, "row")) {
        /* a size in characters (docs/tui_design.md §8.1): one term, alone */
        if (terms_before) {
            duration.cells_mixed = 1;
        }
        duration.cells = unit[0] == 'c' ? 1 : 2;
        duration.cells_amount = amount;
        free(unit);
        return duration;
    }
    if (was_cells) {
        duration.cells_mixed = 1;
    }
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



#line 727 "src/parser.tab.c"

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
  YYSYMBOL_MODIFIER_VALUE = 9,             /* MODIFIER_VALUE  */
  YYSYMBOL_WINDOW_OPEN = 10,               /* WINDOW_OPEN  */
  YYSYMBOL_DATABASE_OPEN = 11,             /* DATABASE_OPEN  */
  YYSYMBOL_SQL_OPEN = 12,                  /* SQL_OPEN  */
  YYSYMBOL_SQL_TEXT = 13,                  /* SQL_TEXT  */
  YYSYMBOL_AS = 14,                        /* AS  */
  YYSYMBOL_DIM = 15,                       /* DIM  */
  YYSYMBOL_PLUS_EQ = 16,                   /* PLUS_EQ  */
  YYSYMBOL_MINUS_EQ = 17,                  /* MINUS_EQ  */
  YYSYMBOL_STAR_EQ = 18,                   /* STAR_EQ  */
  YYSYMBOL_SLASH_EQ = 19,                  /* SLASH_EQ  */
  YYSYMBOL_EXCLUDING = 20,                 /* EXCLUDING  */
  YYSYMBOL_INTERSECTING = 21,              /* INTERSECTING  */
  YYSYMBOL_IF = 22,                        /* IF  */
  YYSYMBOL_CONSIDER_IF = 23,               /* CONSIDER_IF  */
  YYSYMBOL_THEN = 24,                      /* THEN  */
  YYSYMBOL_ELSE = 25,                      /* ELSE  */
  YYSYMBOL_CONSIDER_ELSE = 26,             /* CONSIDER_ELSE  */
  YYSYMBOL_END = 27,                       /* END  */
  YYSYMBOL_END_CONSIDER = 28,              /* END_CONSIDER  */
  YYSYMBOL_PRINT = 29,                     /* PRINT  */
  YYSYMBOL_TRUE = 30,                      /* TRUE  */
  YYSYMBOL_FALSE = 31,                     /* FALSE  */
  YYSYMBOL_NOTHING = 32,                   /* NOTHING  */
  YYSYMBOL_UNKNOWN_VALUE = 33,             /* UNKNOWN_VALUE  */
  YYSYMBOL_AND = 34,                       /* AND  */
  YYSYMBOL_OR = 35,                        /* OR  */
  YYSYMBOL_NOT = 36,                       /* NOT  */
  YYSYMBOL_TRY = 37,                       /* TRY  */
  YYSYMBOL_WITH = 38,                      /* WITH  */
  YYSYMBOL_NEW = 39,                       /* NEW  */
  YYSYMBOL_SPAWN = 40,                     /* SPAWN  */
  YYSYMBOL_FOR = 41,                       /* FOR  */
  YYSYMBOL_TO = 42,                        /* TO  */
  YYSYMBOL_STEP = 43,                      /* STEP  */
  YYSYMBOL_DO = 44,                        /* DO  */
  YYSYMBOL_UNTIL = 45,                     /* UNTIL  */
  YYSYMBOL_IN = 46,                        /* IN  */
  YYSYMBOL_EACH = 47,                      /* EACH  */
  YYSYMBOL_WHILE = 48,                     /* WHILE  */
  YYSYMBOL_CONSIDER = 49,                  /* CONSIDER  */
  YYSYMBOL_BREAK = 50,                     /* BREAK  */
  YYSYMBOL_CONTINUE = 51,                  /* CONTINUE  */
  YYSYMBOL_FUNCTION = 52,                  /* FUNCTION  */
  YYSYMBOL_RETURN = 53,                    /* RETURN  */
  YYSYMBOL_GOTO = 54,                      /* GOTO  */
  YYSYMBOL_GOSUB = 55,                     /* GOSUB  */
  YYSYMBOL_WATCH = 56,                     /* WATCH  */
  YYSYMBOL_UNWATCH = 57,                   /* UNWATCH  */
  YYSYMBOL_WAIT = 58,                      /* WAIT  */
  YYSYMBOL_WITHOUT = 59,                   /* WITHOUT  */
  YYSYMBOL_WATCHERS = 60,                  /* WATCHERS  */
  YYSYMBOL_ON = 61,                        /* ON  */
  YYSYMBOL_NEXT = 62,                      /* NEXT  */
  YYSYMBOL_STOP = 63,                      /* STOP  */
  YYSYMBOL_ERROR_VALUE = 64,               /* ERROR_VALUE  */
  YYSYMBOL_MODIFIER = 65,                  /* MODIFIER  */
  YYSYMBOL_PROGRAM = 66,                   /* PROGRAM  */
  YYSYMBOL_LIBRARY = 67,                   /* LIBRARY  */
  YYSYMBOL_LOAD = 68,                      /* LOAD  */
  YYSYMBOL_USE = 69,                       /* USE  */
  YYSYMBOL_EXPORT = 70,                    /* EXPORT  */
  YYSYMBOL_OP_EQ = 71,                     /* OP_EQ  */
  YYSYMBOL_OP_NE = 72,                     /* OP_NE  */
  YYSYMBOL_OP_GT = 73,                     /* OP_GT  */
  YYSYMBOL_OP_LT = 74,                     /* OP_LT  */
  YYSYMBOL_OP_GE = 75,                     /* OP_GE  */
  YYSYMBOL_OP_LE = 76,                     /* OP_LE  */
  YYSYMBOL_OP_NGT = 77,                    /* OP_NGT  */
  YYSYMBOL_OP_NLT = 78,                    /* OP_NLT  */
  YYSYMBOL_OP_NGE = 79,                    /* OP_NGE  */
  YYSYMBOL_OP_NLE = 80,                    /* OP_NLE  */
  YYSYMBOL_PLUS = 81,                      /* PLUS  */
  YYSYMBOL_MINUS = 82,                     /* MINUS  */
  YYSYMBOL_STAR = 83,                      /* STAR  */
  YYSYMBOL_SLASH = 84,                     /* SLASH  */
  YYSYMBOL_LPAREN = 85,                    /* LPAREN  */
  YYSYMBOL_RPAREN = 86,                    /* RPAREN  */
  YYSYMBOL_LBRACKET = 87,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 88,                  /* RBRACKET  */
  YYSYMBOL_LBRACE = 89,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 90,                    /* RBRACE  */
  YYSYMBOL_COMMA = 91,                     /* COMMA  */
  YYSYMBOL_COLON = 92,                     /* COLON  */
  YYSYMBOL_NEWLINE = 93,                   /* NEWLINE  */
  YYSYMBOL_PATH_LBRACE = 94,               /* PATH_LBRACE  */
  YYSYMBOL_IF_WITHOUT_ELSE = 95,           /* IF_WITHOUT_ELSE  */
  YYSYMBOL_NO_DOT = 96,                    /* NO_DOT  */
  YYSYMBOL_DOT = 97,                       /* DOT  */
  YYSYMBOL_YYACCEPT = 98,                  /* $accept  */
  YYSYMBOL_program = 99,                   /* program  */
  YYSYMBOL_statement_list = 100,           /* statement_list  */
  YYSYMBOL_statement = 101,                /* statement  */
  YYSYMBOL_assignment = 102,               /* assignment  */
  YYSYMBOL_type_lock = 103,                /* type_lock  */
  YYSYMBOL_compound_op = 104,              /* compound_op  */
  YYSYMBOL_lvalue = 105,                   /* lvalue  */
  YYSYMBOL_variable_name = 106,            /* variable_name  */
  YYSYMBOL_comparison_lens = 107,          /* comparison_lens  */
  YYSYMBOL_108_1 = 108,                    /* $@1  */
  YYSYMBOL_modifier_name = 109,            /* modifier_name  */
  YYSYMBOL_modifier_word = 110,            /* modifier_word  */
  YYSYMBOL_print_statement = 111,          /* print_statement  */
  YYSYMBOL_call_statement = 112,           /* call_statement  */
  YYSYMBOL_with_lock_statement = 113,      /* with_lock_statement  */
  YYSYMBOL_for_end = 114,                  /* for_end  */
  YYSYMBOL_for_each_statement = 115,       /* for_each_statement  */
  YYSYMBOL_do_loop_statement = 116,        /* do_loop_statement  */
  YYSYMBOL_while_statement = 117,          /* while_statement  */
  YYSYMBOL_consider_statement = 118,       /* consider_statement  */
  YYSYMBOL_consider_branch_list = 119,     /* consider_branch_list  */
  YYSYMBOL_consider_else_opt = 120,        /* consider_else_opt  */
  YYSYMBOL_consider_statement_list = 121,  /* consider_statement_list  */
  YYSYMBOL_consider_body_statement = 122,  /* consider_body_statement  */
  YYSYMBOL_function_statement = 123,       /* function_statement  */
  YYSYMBOL_modifier_statement = 124,       /* modifier_statement  */
  YYSYMBOL_program_statement = 125,        /* program_statement  */
  YYSYMBOL_library_statement = 126,        /* library_statement  */
  YYSYMBOL_use_statement = 127,            /* use_statement  */
  YYSYMBOL_modifier_signature = 128,       /* modifier_signature  */
  YYSYMBOL_modifier_context = 129,         /* modifier_context  */
  YYSYMBOL_watch_statement = 130,          /* watch_statement  */
  YYSYMBOL_unwatch_statement = 131,        /* unwatch_statement  */
  YYSYMBOL_wait_statement = 132,           /* wait_statement  */
  YYSYMBOL_watch_target_list = 133,        /* watch_target_list  */
  YYSYMBOL_server_statement = 134,         /* server_statement  */
  YYSYMBOL_135_2 = 135,                    /* @2  */
  YYSYMBOL_136_3 = 136,                    /* @3  */
  YYSYMBOL_gsql_block = 137,               /* gsql_block  */
  YYSYMBOL_window_block = 138,             /* window_block  */
  YYSYMBOL_window_item_list = 139,         /* window_item_list  */
  YYSYMBOL_window_item = 140,              /* window_item  */
  YYSYMBOL_server_item_list = 141,         /* server_item_list  */
  YYSYMBOL_server_item = 142,              /* server_item  */
  YYSYMBOL_server_string_list = 143,       /* server_string_list  */
  YYSYMBOL_watch_target_path = 144,        /* watch_target_path  */
  YYSYMBOL_without_watchers_statement = 145, /* without_watchers_statement  */
  YYSYMBOL_on_error_statement = 146,       /* on_error_statement  */
  YYSYMBOL_error_statement = 147,          /* error_statement  */
  YYSYMBOL_return_statement = 148,         /* return_statement  */
  YYSYMBOL_label_statement = 149,          /* label_statement  */
  YYSYMBOL_goto_statement = 150,           /* goto_statement  */
  YYSYMBOL_gosub_statement = 151,          /* gosub_statement  */
  YYSYMBOL_break_statement = 152,          /* break_statement  */
  YYSYMBOL_continue_statement = 153,       /* continue_statement  */
  YYSYMBOL_if_statement = 154,             /* if_statement  */
  YYSYMBOL_if_block_tail = 155,            /* if_block_tail  */
  YYSYMBOL_if_inline_tail = 156,           /* if_inline_tail  */
  YYSYMBOL_inline_statement = 157,         /* inline_statement  */
  YYSYMBOL_expression = 158,               /* expression  */
  YYSYMBOL_conditional_expression = 159,   /* conditional_expression  */
  YYSYMBOL_or_expression = 160,            /* or_expression  */
  YYSYMBOL_and_expression = 161,           /* and_expression  */
  YYSYMBOL_not_expression = 162,           /* not_expression  */
  YYSYMBOL_comparison_expression = 163,    /* comparison_expression  */
  YYSYMBOL_set_expression = 164,           /* set_expression  */
  YYSYMBOL_additive_expression = 165,      /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 166, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 167,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 168,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 169,      /* comparison_operator  */
  YYSYMBOL_primary = 170,                  /* primary  */
  YYSYMBOL_record_literal = 171,           /* record_literal  */
  YYSYMBOL_ident_suffix = 172,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 173,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 174,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 175,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 176,            /* argument_list  */
  YYSYMBOL_array_argument_list = 177,      /* array_argument_list  */
  YYSYMBOL_parameter_list_opt = 178,       /* parameter_list_opt  */
  YYSYMBOL_parameter_default = 179,        /* parameter_default  */
  YYSYMBOL_parameter_list = 180,           /* parameter_list  */
  YYSYMBOL_field_name = 181,               /* field_name  */
  YYSYMBOL_projection_names = 182,         /* projection_names  */
  YYSYMBOL_dot_field_name = 183,           /* dot_field_name  */
  YYSYMBOL_record_field_list = 184,        /* record_field_list  */
  YYSYMBOL_field_policy = 185,             /* field_policy  */
  YYSYMBOL_optional_newlines = 186         /* optional_newlines  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;



/* Unqualified %code blocks.  */
#line 747 "src/parser.y"

static int yylex(YYSTYPE *lvalp, YYLTYPE *llocp, gb_parse_ctx *ctx);
static void yyerror(YYLTYPE *llocp, gb_parse_ctx *ctx, const char *message);
static void report_syntax_error(gb_parse_ctx *ctx, int line, int column,
                                int end_line, int end_column, const char *message);

/* THE DATA TYPES A LOCK MAY NAME (docs/typed_variables_design.md): one word,
 * and `money` takes its currency. Checked HERE, at parse time, so `x:int` is
 * refused before anything runs -- and with the gBASIC spelling, because a model
 * writing gBASIC brings Python's and TypeScript's names with it. Returns 1 when
 * the lock is good; otherwise reports and returns 0. `who` is the variable or
 * parameter, for the suggested spelling. */
static int type_lock_check(gb_parse_ctx *ctx, const char *who, const char *name,
                           const char *arg, int line, int column, int end_line,
                           int end_column) {
    static const char *const types[] = {
        "number", "string", "text", "boolean", "date", "time", "datetime",
        "duration", "money", "file", "dir", NULL
    };
    static const struct { const char *foreign; const char *ours; } near[] = {
        { "int", "number" }, { "integer", "number" }, { "float", "number" },
        { "double", "number" }, { "decimal", "number" }, { "real", "number" },
        { "numeric", "number" }, { "long", "number" }, { "bigint", "number" },
        { "str", "string" }, { "char", "string" }, { "varchar", "string" },
        { "bool", "boolean" }, { "timestamp", "datetime" },
        { "directory", "dir" }, { "folder", "dir" },
        { NULL, NULL }
    };
    char message[320];
    int known = 0;
    for (int i = 0; types[i]; i++) {
        if (strcmp(types[i], name) == 0) {
            known = 1;
            break;
        }
    }
    if (!known) {
        for (int i = 0; near[i].foreign; i++) {
            if (strcmp(near[i].foreign, name) == 0) {
                snprintf(message, sizeof message,
                         "'%s' is not a gBASIC type; write %s:%s", name, who, near[i].ours);
                report_syntax_error(ctx, line, column, end_line, end_column, message);
                return 0;
            }
        }
        size_t n = strlen(name);
        if (n == 3 && isupper((unsigned char)name[0]) && isupper((unsigned char)name[1]) &&
            isupper((unsigned char)name[2]) && !arg) {
            snprintf(message, sizeof message,
                     "a lock names a type, and money is one: write %s:money(%s)", who, name);
        } else if (strcmp(name, "array") == 0 || strcmp(name, "record") == 0 ||
                   strcmp(name, "list") == 0 || strcmp(name, "dict") == 0 ||
                   strcmp(name, "map") == 0 || strcmp(name, "object") == 0) {
            snprintf(message, sizeof message,
                     "'%s' cannot be a lock: a lock is for a data type (number, string, "
                     "boolean, date, time, datetime, duration, money(USD), file, dir)", name);
        } else {
            snprintf(message, sizeof message,
                     "'%s' is not a data type a lock can name; the types are number, string "
                     "(or text), boolean, date, time, datetime, duration, money(USD), file "
                     "and dir. A lens such as {trimmed} is written in braces", name);
        }
        report_syntax_error(ctx, line, column, end_line, end_column, message);
        return 0;
    }
    if (strcmp(name, "money") == 0) {
        if (!arg) {
            snprintf(message, sizeof message,
                     "money needs its currency: write %s:money(USD)", who);
            report_syntax_error(ctx, line, column, end_line, end_column, message);
            return 0;
        }
        if (strlen(arg) != 3 || !isupper((unsigned char)arg[0]) ||
            !isupper((unsigned char)arg[1]) || !isupper((unsigned char)arg[2])) {
            snprintf(message, sizeof message,
                     "money takes a three-letter currency code, like money(USD), not '%s'", arg);
            report_syntax_error(ctx, line, column, end_line, end_column, message);
            return 0;
        }
    } else if (arg) {
        snprintf(message, sizeof message,
                 "%s takes no argument; only money does, as in money(USD)", name);
        report_syntax_error(ctx, line, column, end_line, end_column, message);
        return 0;
    }
    return 1;
}

#line 1037 "src/parser.tab.c"

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
#define YYLAST   3736

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  98
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  89
/* YYNRULES -- Number of rules.  */
#define YYNRULES  381
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  889

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   352


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
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   919,   919,   923,   924,   925,   929,   930,   931,   932,
     933,   934,   935,   936,   937,   938,   939,   940,   941,   942,
     943,   944,   945,   946,   947,   948,   949,   950,   951,   952,
     953,   954,   955,   956,   957,   963,   974,   979,   980,   996,
    1003,  1017,  1018,  1022,  1023,  1024,  1025,  1029,  1030,  1034,
    1042,  1046,  1047,  1048,  1059,  1059,  1067,  1071,  1072,  1076,
    1077,  1078,  1079,  1083,  1089,  1093,  1096,  1097,  1103,  1108,
    1117,  1130,  1159,  1160,  1161,  1165,  1169,  1185,  1190,  1198,
    1202,  1223,  1229,  1235,  1241,  1244,  1250,  1251,  1255,  1256,
    1257,  1261,  1262,  1263,  1264,  1265,  1266,  1267,  1268,  1269,
    1270,  1271,  1272,  1273,  1274,  1275,  1276,  1277,  1278,  1279,
    1280,  1281,  1282,  1283,  1284,  1285,  1286,  1287,  1293,  1304,
    1307,  1314,  1323,  1335,  1338,  1344,  1350,  1356,  1357,  1358,
    1359,  1360,  1361,  1377,  1393,  1414,  1415,  1419,  1423,  1426,
    1434,  1440,  1447,  1448,  1452,  1453,  1473,  1472,  1480,  1479,
    1502,  1505,  1508,  1514,  1529,  1533,  1543,  1551,  1552,  1553,
    1558,  1563,  1567,  1572,  1577,  1591,  1606,  1611,  1622,  1637,
    1642,  1648,  1649,  1650,  1654,  1657,  1660,  1663,  1666,  1672,
    1673,  1677,  1678,  1682,  1688,  1689,  1690,  1691,  1696,  1710,
    1715,  1720,  1730,  1734,  1735,  1739,  1746,  1750,  1759,  1760,
    1764,  1765,  1769,  1773,  1780,  1783,  1786,  1795,  1805,  1808,
    1811,  1817,  1824,  1834,  1835,  1836,  1837,  1838,  1839,  1840,
    1841,  1842,  1843,  1844,  1852,  1853,  1857,  1858,  1864,  1865,
    1869,  1870,  1892,  1893,  1897,  1898,  1899,  1905,  1906,  1907,
    1911,  1912,  1913,  1917,  1918,  1919,  1923,  1924,  1931,  1935,
    1936,  1937,  1941,  1942,  1946,  1954,  1959,  1962,  1967,  1981,
    1982,  1983,  1984,  1985,  1986,  1987,  1988,  1989,  1990,  1994,
    2015,  2016,  2017,  2018,  2042,  2043,  2060,  2066,  2067,  2068,
    2069,  2070,  2071,  2072,  2073,  2074,  2078,  2079,  2083,  2088,
    2093,  2099,  2111,  2116,  2124,  2134,  2146,  2147,  2151,  2152,
    2156,  2157,  2161,  2162,  2176,  2180,  2185,  2186,  2187,  2188,
    2189,  2190,  2194,  2195,  2198,  2202,  2209,  2216,  2223,  2230,
    2245,  2252,  2261,  2262,  2266,  2267,  2268,  2269,  2270,  2271,
    2272,  2273,  2274,  2275,  2276,  2277,  2278,  2279,  2280,  2281,
    2282,  2283,  2284,  2285,  2286,  2287,  2288,  2289,  2290,  2291,
    2292,  2293,  2294,  2295,  2296,  2297,  2298,  2299,  2300,  2301,
    2302,  2303,  2304,  2305,  2306,  2307,  2308,  2309,  2310,  2311,
    2312,  2313,  2317,  2318,  2319,  2320,  2321,  2322,  2330,  2357,
    2376,  2377
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
  "STRING", "LENS_CONTENT", "QUALIFIED_IDENT", "MODIFIER_PREFIX",
  "MODIFIER_VALUE", "WINDOW_OPEN", "DATABASE_OPEN", "SQL_OPEN", "SQL_TEXT",
  "AS", "DIM", "PLUS_EQ", "MINUS_EQ", "STAR_EQ", "SLASH_EQ", "EXCLUDING",
  "INTERSECTING", "IF", "CONSIDER_IF", "THEN", "ELSE", "CONSIDER_ELSE",
  "END", "END_CONSIDER", "PRINT", "TRUE", "FALSE", "NOTHING",
  "UNKNOWN_VALUE", "AND", "OR", "NOT", "TRY", "WITH", "NEW", "SPAWN",
  "FOR", "TO", "STEP", "DO", "UNTIL", "IN", "EACH", "WHILE", "CONSIDER",
  "BREAK", "CONTINUE", "FUNCTION", "RETURN", "GOTO", "GOSUB", "WATCH",
  "UNWATCH", "WAIT", "WITHOUT", "WATCHERS", "ON", "NEXT", "STOP",
  "ERROR_VALUE", "MODIFIER", "PROGRAM", "LIBRARY", "LOAD", "USE", "EXPORT",
  "OP_EQ", "OP_NE", "OP_GT", "OP_LT", "OP_GE", "OP_LE", "OP_NGT", "OP_NLT",
  "OP_NGE", "OP_NLE", "PLUS", "MINUS", "STAR", "SLASH", "LPAREN", "RPAREN",
  "LBRACKET", "RBRACKET", "LBRACE", "RBRACE", "COMMA", "COLON", "NEWLINE",
  "PATH_LBRACE", "IF_WITHOUT_ELSE", "NO_DOT", "DOT", "$accept", "program",
  "statement_list", "statement", "assignment", "type_lock", "compound_op",
  "lvalue", "variable_name", "comparison_lens", "$@1", "modifier_name",
  "modifier_word", "print_statement", "call_statement",
  "with_lock_statement", "for_end", "for_each_statement",
  "do_loop_statement", "while_statement", "consider_statement",
  "consider_branch_list", "consider_else_opt", "consider_statement_list",
  "consider_body_statement", "function_statement", "modifier_statement",
  "program_statement", "library_statement", "use_statement",
  "modifier_signature", "modifier_context", "watch_statement",
  "unwatch_statement", "wait_statement", "watch_target_list",
  "server_statement", "@2", "@3", "gsql_block", "window_block",
  "window_item_list", "window_item", "server_item_list", "server_item",
  "server_string_list", "watch_target_path", "without_watchers_statement",
  "on_error_statement", "error_statement", "return_statement",
  "label_statement", "goto_statement", "gosub_statement",
  "break_statement", "continue_statement", "if_statement", "if_block_tail",
  "if_inline_tail", "inline_statement", "expression",
  "conditional_expression", "or_expression", "and_expression",
  "not_expression", "comparison_expression", "set_expression",
  "additive_expression", "multiplicative_expression", "unary_expression",
  "postfix_expression", "comparison_operator", "primary", "record_literal",
  "ident_suffix", "ident_dot_suffix", "duration_terms",
  "argument_list_opt", "argument_list", "array_argument_list",
  "parameter_list_opt", "parameter_default", "parameter_list",
  "field_name", "projection_names", "dot_field_name", "record_field_list",
  "field_policy", "optional_newlines", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-597)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -597,    18,  1383,  -597,    45,   -13,   132,   139,   173,  -597,
     882,  -597,   772,   882,   208,    75,   157,   882,   882,   228,
     268,   107,   882,    89,    89,   133,   882,   258,   218,    36,
    -597,   945,   215,   289,   327,   394,   398,   275,  -597,  -597,
     273,    78,   263,   282,   295,  -597,  -597,  -597,  -597,  -597,
    -597,  -597,  -597,  -597,   299,  -597,   314,   316,  -597,  -597,
    -597,  -597,   329,   331,   342,   346,   348,   350,   352,   354,
    -597,   335,   882,   882,   365,   372,   404,   466,  -597,  -597,
     389,   668,  -597,  -597,  -597,  -597,  -597,   983,   882,  1046,
     471,   392,  -597,   668,   882,  -597,  -597,  3599,    44,   454,
    -597,   124,   445,  -597,  -597,   560,   324,   332,  -597,   158,
    -597,  -597,   477,   418,  -597,  -597,   399,   178,   479,  -597,
     393,   397,  -597,  -597,   402,   407,  -597,  -597,  -597,   408,
      89,  -597,   171,   409,  -597,   882,   401,    81,   264,  3666,
    -597,  -597,  -597,  -597,  -597,    40,  -597,   460,   420,   410,
     217,  -597,   503,  -597,   215,  -597,  -597,  -597,  -597,  -597,
    -597,   882,   882,  -597,  3398,   882,   297,   506,  -597,  -597,
    -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,
    -597,  2996,  -597,   425,   421,   434,  3063,  3130,   517,  -597,
     882,  -597,  -597,  -597,   248,   438,   439,  -597,   443,   685,
    1115,  -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,
    -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,
    -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,
    -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,
    -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,
     882,  3465,  -597,  2893,   983,   983,   983,   668,   668,  -597,
    -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,   712,
     668,   668,   668,   668,   668,   882,  3599,  3532,   520,   882,
     882,   882,   882,   526,    -8,  1249,  -597,   508,   528,   528,
      89,   153,    89,  -597,   529,   493,  -597,  -597,  -597,    70,
    -597,    76,  -597,   452,   528,  -597,   534,   528,  -597,   536,
     542,   545,   510,  -597,   188,   541,   468,   470,  -597,  -597,
     882,   882,   472,   488,   475,  -597,   473,   114,  -597,   175,
    -597,   882,  -597,   474,   260,   476,   262,    69,   478,   485,
     882,  -597,  -597,  -597,  -597,  -597,   484,  -597,   486,   492,
     494,   497,   498,  -597,  -597,   495,  -597,  -597,  -597,  -597,
    -597,  -597,  -597,  -597,  -597,  -597,   491,   233,   445,  -597,
     324,   324,   668,   406,   332,   332,  -597,  -597,   256,   340,
    -597,   501,   504,  -597,  -597,  -597,   502,   499,   521,   544,
     882,   587,   882,  1450,   882,   333,   145,   507,   505,   512,
     272,   511,   409,  1517,  -597,   882,  1584,  -597,  -597,  -597,
    -597,   882,   513,  -597,   515,   514,  1651,   580,  -597,  -597,
     534,  -597,   882,   516,   882,   882,  -597,  -597,   597,   882,
     599,  -597,   882,   882,   518,  -597,  -597,  -597,    59,  -597,
     519,   592,   605,  -597,  -597,   533,  -597,   213,   286,  -597,
     882,   882,  -597,  1316,   506,   590,   983,   406,  -597,   882,
    -597,  3599,   882,   882,   531,  -597,   882,   882,   535,   581,
     548,   578,   618,   882,   550,   601,   540,   506,   360,   640,
     362,   552,  -597,   591,  -597,   595,   564,  -597,  -597,   553,
     584,   651,   563,   292,  -597,   572,   575,   576,  -597,   882,
     582,  -597,  -597,  -597,  -597,  1182,    54,   660,  -597,  -597,
     642,   574,  -597,   844,  -597,  -597,   588,   593,  1032,   648,
    -597,  2857,  -597,  -597,   293,  -597,   594,   596,  -597,  1718,
      19,   585,  -597,   882,  -597,   598,   603,   657,  -597,   604,
    -597,  -597,  -597,  -597,  -597,  -597,   680,   681,  -597,   614,
     506,  -597,   146,   506,  -597,  -597,  1785,   609,   610,  -597,
    1852,  -597,   612,  -597,  -597,  -597,  -597,  -597,  -597,  -597,
     617,    57,  -597,   625,   151,   103,   682,   707,  -597,  -597,
     709,    94,   710,   706,  -597,  -597,  -597,   882,  -597,   627,
     630,   882,  -597,   633,  -597,  -597,  -597,  1919,   686,    99,
    -597,   882,  -597,  -597,  1718,   636,  -597,  -597,   638,  2790,
    -597,  -597,  -597,   540,   644,  1986,   540,   506,   645,  2053,
    2120,   677,  -597,  -597,   670,  2187,  -597,  2254,   882,   428,
     735,   736,  -597,  -597,   136,   599,   882,   882,   243,   653,
     737,  3197,  -597,   655,   647,   658,   748,   661,   729,   738,
    2321,  -597,  -597,   739,  2388,  -597,   721,   667,  -597,   671,
     673,  1718,  1718,  -597,  -597,  2790,  -597,  -597,  -597,   675,
     678,   700,  -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,
    -597,  -597,   701,  -597,   702,   704,  -597,  -597,   705,   708,
     713,   714,   720,   722,   723,   724,  -597,  -597,  -597,   766,
    -597,   749,  -597,   767,   744,   728,   730,   756,   759,  -597,
     741,   743,   294,   740,   742,   825,   745,  -597,  -597,   746,
     826,  3264,  -597,  -597,   833,   747,   279,   154,  -597,   834,
    -597,   751,  -597,   837,   752,   820,  2929,   828,   762,  -597,
    -597,  -597,  -597,  -597,  1718,  2790,  -597,  -597,  -597,  -597,
    -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,  -597,
    2455,   763,   540,  2522,   765,   769,  -597,  -597,   770,   771,
    3331,   528,   855,  -597,  -597,  -597,   773,   776,  -597,   861,
     777,   283,   166,   174,   779,  -597,   780,   874,   786,   199,
    -597,   789,  -597,   795,  -597,   799,   800,  -597,  1718,  -597,
     842,  -597,  -597,   843,  -597,  -597,  -597,  -597,   803,   287,
     811,  -597,  2589,  -597,   882,   219,   805,  -597,   806,   896,
     897,  -597,  -597,   809,  -597,   899,  -597,  1316,  -597,  1316,
     590,  -597,  -597,   812,   814,  -597,   817,   818,   856,  -597,
     912,  -597,  -597,   827,   830,   226,   236,  -597,  2656,   831,
    -597,  -597,  -597,  -597,  -597,   278,  -597,  -597,   832,   835,
     296,  -597,  -597,   923,   926,   871,  -597,   930,   303,  2723,
    -597,  -597,   931,   845,   846,   847,   850,   932,   941,   854,
    -597,  -597,  -597,  -597,   858,   862,  -597,  -597,  -597
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       3,     0,     2,     1,    51,     0,     0,     0,     0,    35,
       0,    52,     0,     0,     0,     0,     0,     0,     0,   198,
     200,     0,   193,     0,     0,     0,     0,     0,     0,     0,
      53,     0,     0,     0,     0,     0,     0,     0,     4,     5,
       0,     0,    47,     0,     0,     9,    12,    14,    13,    15,
      16,    17,    18,    19,     0,    21,     0,     0,    22,    11,
      10,    25,     0,     0,     0,     0,     0,     0,     0,     0,
      34,     0,   296,   296,     0,     0,     0,   271,    51,   274,
       0,     0,   269,   278,   279,   280,   281,     0,     0,     0,
       0,     0,   277,     0,     0,   380,   380,     0,   288,     0,
     224,   226,   228,   230,   232,   234,   237,   240,   243,   246,
     252,   285,   273,     0,    63,    66,     0,     0,     0,     3,
       0,     0,   199,   201,     0,     0,   194,   196,   197,    51,
       0,   181,     0,   144,   141,     0,     0,     0,     0,     0,
     192,    59,    61,    60,    62,   135,    57,     0,     0,     0,
     128,   130,   127,   129,     0,     6,    56,    43,    44,    45,
      46,     0,     0,    54,     0,     0,     0,   195,     7,     8,
      20,    23,    24,    26,    27,    28,    29,    30,    31,    32,
      33,     0,   298,     0,   297,     0,     0,     0,     0,   294,
     296,   248,   233,   225,   249,     0,     0,   247,     0,     0,
       0,   324,   325,   371,   340,   337,   338,   329,   344,   352,
     353,   354,   370,   350,   351,   348,   349,   335,   333,   359,
     339,   330,   368,   342,   343,   331,   334,   341,   367,   355,
     356,   362,   345,   357,   358,   365,   369,   336,   366,   332,
     326,   327,   328,   363,   364,   361,   346,   347,   360,   270,
     296,     0,   275,     0,     0,     0,     0,     0,     0,   259,
     260,   261,   262,   263,   264,   265,   266,   267,   268,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     3,     0,   302,   302,
       0,     0,     0,     3,     0,   142,     3,   191,   190,     0,
     189,     0,   186,   324,   302,    58,     0,   302,     3,     0,
       0,     0,     0,    36,     0,     0,   324,     0,    50,    37,
       0,     0,    41,     0,   324,   321,     0,     0,   320,     0,
      65,     0,    67,     0,     0,     0,     0,     0,     0,     0,
     296,   272,   282,   283,   381,   300,   380,   286,   380,     0,
       0,   292,    51,     3,   213,    47,   214,   215,   216,   217,
     218,   219,   220,   221,   222,   223,     0,     0,   229,   231,
     238,   239,     0,   235,   241,   242,   244,   245,     0,     0,
     322,   324,     0,   255,   295,    64,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    86,   312,     0,   303,     0,
       0,     0,   145,     0,   182,     0,     0,   188,   187,   184,
     185,   296,     0,   137,     0,     0,     0,   133,   131,   132,
       0,    48,     0,     0,   296,   296,    40,    38,     0,     0,
       0,   148,     0,     0,     0,   380,   299,   157,     0,   151,
       0,     0,     0,   276,   250,     0,   380,     0,     0,   289,
     296,   296,   290,     0,     0,   208,     0,   236,   253,     0,
     256,     0,   296,   296,     0,     3,     0,     0,     0,     0,
       0,    52,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     3,    52,   143,    52,     0,   136,     3,     0,
      52,     0,     0,     0,    55,     0,     0,     0,    39,   378,
       0,   171,   372,   373,   146,     0,     0,     0,   157,   150,
       0,     0,   251,     0,   284,   287,     0,     0,     0,    52,
     202,     0,   203,   227,     0,   323,     0,     0,     3,     0,
       0,     0,     3,     0,    81,     0,     0,     0,    88,     0,
     304,   307,   308,   309,   310,   311,     0,     0,   313,   315,
       0,     3,   314,     0,     3,     3,     0,     0,     0,    70,
       0,     3,     0,   134,     3,    49,    68,    69,    42,   379,
       0,     0,   171,   324,     0,     0,     0,     0,   158,   159,
       0,     0,     0,     0,   301,   291,   293,     0,     3,     0,
       0,     0,     3,     0,   254,   257,   258,     0,    52,    53,
      75,     0,     3,     3,     0,     0,    82,    88,     0,    87,
      83,   306,   305,     0,     0,     0,     0,     0,     0,     0,
       0,    52,   139,   183,    52,     0,   126,     0,     0,     0,
       0,     0,   172,   173,     0,     0,     0,     0,     0,     0,
       0,     0,   157,     0,     0,     0,     0,     0,     0,     0,
       0,   205,   204,     0,     0,   209,    52,     0,    73,     0,
       0,     0,     0,    76,     3,    84,    88,   118,    89,     0,
       0,     0,    94,    96,    98,    97,    99,    90,   100,   101,
     102,   103,     0,   105,     0,     0,    95,   108,     0,     0,
       0,     0,     0,     0,     0,     0,   117,   316,     3,    52,
     319,   317,     3,    52,    52,     0,     0,    52,    52,   374,
       0,   179,     0,     0,     0,     0,     0,   375,   376,     0,
       0,     0,   157,   157,     0,     0,     0,     0,   155,     0,
     157,     0,   152,     0,     0,    52,     0,    52,     0,    72,
      74,     3,    79,    77,     0,    85,    91,    92,    93,   104,
     106,   107,   109,   110,   111,   112,   113,   114,   115,   116,
       0,     0,     0,     0,     0,     0,   138,   123,     0,     0,
       0,   302,     0,   174,   149,     3,     0,     0,   157,     0,
       0,     0,     0,     0,     0,   160,     0,     0,     0,     0,
     154,     0,     3,     0,     3,     0,     0,    71,     0,    78,
      52,   119,   318,    52,   120,   140,   125,   124,     0,     0,
       0,   180,     0,   147,     0,     0,     0,   162,     0,     0,
       0,   157,   157,     0,     3,     0,   153,     0,   206,     0,
     208,   210,    80,     0,     0,   171,     0,     0,    52,   377,
       0,   157,   161,     0,     0,     0,     0,   163,     0,     0,
     207,   212,   211,   121,   122,     0,   171,     3,     0,     0,
       0,   165,   167,     0,     0,    52,   156,     0,     0,     0,
     178,   166,     0,     0,     0,     0,     0,     0,    52,     0,
     168,   164,   170,   177,     0,     0,   169,   176,   175
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -597,  -597,   -88,  -597,  -248,  -463,   790,  -597,    -2,   852,
    -597,  -597,   813,  -247,  -237,  -589,  -596,  -577,  -576,  -574,
    -573,  -597,  -597,  -553,  -597,  -566,  -561,  -559,  -556,  -227,
     807,   539,  -546,  -545,  -541,  -117,  -597,  -597,  -597,  -597,
    -538,  -489,  -597,  -560,  -597,  -597,   674,  -534,  -223,  -219,
    -201,  -532,  -197,  -196,  -184,  -180,  -531,  -434,   130,  -511,
      29,   509,   716,   718,   -76,  -597,  -233,   202,   200,   -64,
     873,   694,  -597,   629,  -597,  -597,  -597,   -14,  -317,  -597,
    -280,  -588,  -597,   469,  -597,   -94,  -185,   345,   -92
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    39,    40,   323,   165,    41,    98,   166,
     315,   145,   146,    43,    44,    45,   600,    46,    47,    48,
      49,   395,   475,   609,   677,    50,    51,    52,    53,    54,
     147,   414,    55,    56,    57,   132,    58,   572,   501,    59,
      60,   506,   579,   571,   633,   712,   133,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,   520,   522,   366,
     182,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   270,   110,   111,   252,   452,   112,   183,   184,   346,
     397,   548,   398,   327,   379,   328,   329,   500,   199
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      42,   334,   336,   249,   200,   354,   356,   589,   663,   399,
     593,   192,   634,   291,   549,   348,   357,   191,     3,   581,
     672,   127,   128,   131,   412,   697,   358,   415,   700,   197,
     359,   285,   673,   674,   360,   675,   676,   373,   390,    99,
     137,   114,   115,   678,   141,   249,   120,   121,   679,    71,
     680,   126,   361,   681,   665,   134,   362,   363,   575,   185,
     140,   629,   601,   683,   684,   742,   743,   142,   685,   364,
     318,   686,    73,   365,   407,   687,   672,   691,   696,   117,
     409,   576,   143,   391,   630,   297,   156,   614,   673,   674,
     618,   675,   676,    78,   157,   158,   159,   160,   575,   678,
     138,   507,   144,    78,   679,   493,   680,   638,   639,   681,
     298,   124,   602,   745,   125,   577,    11,   193,   631,   683,
     684,   646,   118,   198,   685,   304,    11,   686,   131,   250,
      72,   687,   408,   691,   696,   299,    74,   129,   410,   457,
     629,   251,   524,    75,   300,   640,   254,   578,   799,   161,
     632,    30,   508,   727,   701,   577,   672,   351,   575,   255,
      11,    30,   441,   715,   295,   162,   442,   163,   673,   674,
     575,   675,   676,   400,   802,   164,   338,    76,   575,   678,
     369,   787,   380,   383,   679,   432,   680,   578,   641,   681,
     313,   314,   658,   819,   319,    30,   642,   631,   393,   683,
     684,   820,   832,   575,   685,   403,   433,   686,   406,   376,
     377,   687,   116,   691,   696,   577,   476,   616,   130,   141,
     416,   309,   636,   575,   281,   795,   825,   577,   345,   632,
     575,   310,   122,   782,   783,   577,   349,   477,   617,   401,
     575,   789,   142,   637,   292,   275,   840,   578,   719,   282,
     119,   355,   276,   863,   447,   277,   448,   143,   456,   578,
     577,   434,   292,   864,   293,   453,   435,   578,   255,   283,
     354,   356,   123,   354,   356,   855,   421,   144,   136,   422,
     577,   357,   629,    42,   357,   720,   339,   577,   131,   815,
     131,   358,   578,   148,   358,   359,   868,   577,   359,   360,
     575,   514,   360,   135,   378,   867,   344,   629,   385,   386,
     387,   388,   578,   157,   158,   159,   160,   361,   301,   578,
     361,   362,   363,   872,   362,   363,   445,   302,   721,   578,
     877,   149,   845,   846,   364,   275,   722,   364,   365,   631,
     154,   365,   276,   505,   458,   277,   438,   459,   440,   426,
     427,   435,   860,   435,   513,   167,   473,   577,   481,   474,
     436,   669,   670,   292,   631,   786,   155,   525,   320,   818,
     435,   632,   671,   836,   435,   168,   515,   529,   435,   344,
     565,   594,   682,   331,   331,   772,   688,   773,   169,   578,
     689,    42,   170,   850,   556,   851,   632,   486,   150,   151,
     560,    42,   152,   153,    42,   271,   272,   171,   690,   172,
     495,   496,   692,   693,    42,   273,   274,   669,   670,   468,
     181,   470,   173,   472,   174,   694,   257,   258,   671,   695,
     460,   461,   710,   711,   484,   175,   516,   517,   682,   176,
     597,   177,   688,   178,   604,   179,   689,   180,   526,   527,
     186,    42,   550,   551,   553,   554,   726,   187,   498,   370,
     371,   502,   503,   615,   690,   188,   619,   620,   692,   693,
     189,   374,   375,   625,   190,   195,   627,   196,   253,   256,
     278,   694,   279,   284,   280,   695,   286,   288,   354,   356,
     287,   810,   289,   290,   296,   530,   531,   669,   670,   357,
     650,   306,   537,   308,   654,   307,   294,   311,   671,   358,
     322,   330,   331,   359,   661,   662,   355,   360,   682,   355,
     332,   337,   688,   340,   384,   341,   689,    42,   569,   342,
     389,   394,   396,   404,   405,   361,   781,   411,   413,   362,
     363,   417,   584,   540,   690,   541,   418,   423,   692,   693,
     419,   420,   364,   424,    42,   425,   365,   428,    42,   429,
     430,   694,   605,   466,   443,   695,   431,   437,   156,   439,
     542,   543,   544,   545,    96,   446,   744,   435,   449,   450,
     257,   258,   451,    72,   455,   809,   462,   454,   464,   463,
     467,   469,   465,   478,   491,    42,   479,   659,   480,   487,
     489,   497,    42,   499,   482,   510,   494,    42,   488,   511,
     760,   504,   509,    42,   763,   521,   649,    42,    42,   512,
     653,   546,   547,    42,   528,    42,   535,   533,   532,   539,
     660,   259,   260,   261,   262,   263,   264,   265,   266,   267,
     268,   534,   536,   538,   552,   555,   561,   557,    42,   163,
     559,   562,    42,   798,   558,   563,   564,   709,   566,    42,
      42,   567,   568,    42,   580,   717,   718,   583,   570,   582,
     590,    77,    78,    79,   585,    80,    81,    82,   603,   586,
     595,   608,   596,   611,   612,   613,   643,   812,    77,    78,
      79,   606,    80,    81,    82,    11,   607,   610,    83,    84,
      85,    86,   622,   623,   827,   626,   829,    89,    90,   628,
     635,   644,    11,   645,   647,    83,    84,    85,    86,   648,
     651,    87,    88,   652,    89,    90,   655,   657,    91,   664,
      30,   666,    92,   705,   355,   706,   848,   698,   702,   713,
     714,   724,    42,    42,   729,    91,   723,    30,   728,    92,
      93,   730,   731,    94,   732,    95,   733,    96,    42,   738,
     739,    42,   734,   736,   740,    97,   741,    93,   746,   869,
      94,   747,    95,   343,    96,    77,    78,    79,   344,    80,
      81,    82,    97,   259,   260,   261,   262,   263,   264,   265,
     266,   267,   268,   748,   749,   750,    42,   751,   752,    11,
     765,   753,    83,    84,    85,    86,   754,   755,    87,    88,
      42,    89,    90,   756,   113,   757,   758,   759,   761,   764,
     762,   766,   768,   767,   769,    42,   770,    42,   771,   776,
     779,   777,    91,   774,    30,   775,    92,   784,   788,   778,
     785,   791,   793,   839,   790,   792,    42,    77,    78,    79,
     796,    80,    81,    82,    93,   797,   801,    94,   804,    95,
     811,    96,   805,   806,   807,   816,   813,    42,   814,    97,
     817,    11,   821,   822,    83,    84,    85,    86,   823,   824,
      87,    88,   826,    89,    90,    77,    78,    79,   828,    80,
      81,    82,   830,   831,   833,   834,   835,   837,   841,   842,
     843,   844,   847,   849,    91,   853,    30,   854,    92,    11,
     856,   857,    83,    84,    85,    86,   859,   858,    87,    88,
     861,    89,    90,   862,   866,   870,    93,   873,   871,    94,
     874,    95,   875,    96,   876,   879,   884,   344,   880,   881,
     882,    97,    91,   883,    30,   885,    92,   886,    77,    78,
      79,   887,    80,    81,    82,   888,   321,   269,   305,   492,
     852,   312,   194,   372,    93,   523,   402,    94,   444,    95,
     367,    96,    11,   368,   574,    83,    84,    85,    86,    97,
     716,    87,    88,     0,    89,    90,    77,    78,    79,     0,
      80,    81,    82,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    91,     0,    30,     0,    92,
      11,     0,     0,    83,    84,    85,    86,     0,     0,    87,
       0,     0,    89,    90,     0,     0,     0,    93,     0,     0,
      94,     0,    95,     0,    96,     0,   352,     0,     0,     5,
       0,     0,   139,    91,     0,    30,     0,    92,     0,    77,
      78,    79,     0,    80,   587,    82,     0,     0,     0,    11,
       0,    12,     0,     0,     0,    93,     0,     0,    94,    13,
      95,     0,    96,    11,     0,     0,    83,    84,    85,    86,
      97,     0,    19,    20,     0,    22,    23,    24,     0,     0,
       0,     0,     0,    29,    30,     0,    31,     0,     0,     0,
      35,    36,     0,     0,     0,     0,    91,     0,    30,     0,
      92,     0,     0,     0,     0,     0,     0,     0,     0,   324,
     325,     0,     0,     0,     0,   588,     0,     0,     0,   202,
     203,    94,     0,    95,     0,    96,     0,   204,     0,   205,
     206,     0,   207,    97,   208,   209,   210,   211,   212,   213,
     214,   215,   216,   217,   218,   219,   220,   221,   222,   223,
     224,   225,   226,   227,   228,   229,   230,   231,   232,   233,
     234,   235,   236,     0,   237,   238,   239,   240,   241,   242,
     243,   244,   245,   246,   247,   248,   573,   325,     0,     0,
       0,     0,     0,     0,     0,     0,   202,   203,     0,     0,
       0,     0,     0,     0,   204,   347,   205,   206,   344,   207,
       0,   208,   209,   210,   211,   212,   213,   214,   215,   216,
     217,   218,   219,   220,   221,   222,   223,   224,   225,   226,
     227,   228,   229,   230,   231,   232,   233,   234,   235,   236,
       0,   237,   238,   239,   240,   241,   242,   243,   244,   245,
     246,   247,   248,     4,     0,     0,     5,     0,     0,     6,
       7,     8,     0,     0,     9,     0,     0,     0,     0,     0,
       0,    10,     0,     0,     0,   344,    11,     0,    12,     0,
       0,     0,     0,     0,     0,     0,    13,    14,     0,     0,
      15,     0,     0,    16,   392,     0,     0,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,     0,
      29,    30,     0,    31,    32,    33,    34,    35,    36,    37,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     0,
       0,     9,     0,     0,     0,     0,     0,     0,    10,     0,
       0,   518,    38,   519,     0,    12,     0,     0,     0,     0,
       0,     0,     0,    13,    14,     0,     0,    15,     0,     0,
      16,     0,     0,     0,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,     0,    29,    30,     0,
      31,    32,    33,    34,    35,    36,    37,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    38,
      11,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,    14,     0,     0,    15,     0,     0,    16,     0,     0,
       0,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,     0,    29,    30,     0,    31,    32,    33,
      34,    35,    36,    37,     4,     0,     0,     5,     0,     0,
       6,     7,     8,     0,     0,     9,     0,     0,     0,     0,
       0,     0,    10,     0,     0,     0,    38,   471,     0,    12,
       0,     0,     0,     0,     0,     0,     0,    13,    14,     0,
       0,    15,     0,     0,    16,     0,     0,     0,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
       0,    29,    30,     0,    31,    32,    33,    34,    35,    36,
      37,     4,     0,     0,     5,     0,     0,     6,     7,     8,
       0,     0,     9,     0,     0,     0,     0,     0,     0,    10,
       0,     0,     0,    38,   483,     0,    12,     0,     0,     0,
       0,     0,     0,     0,    13,    14,     0,     0,    15,     0,
       0,    16,     0,     0,     0,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,     0,    29,    30,
       0,    31,    32,    33,    34,    35,    36,    37,     4,     0,
       0,     5,     0,     0,     6,     7,     8,     0,     0,     9,
       0,     0,     0,     0,     0,     0,    10,     0,     0,     0,
      38,   485,     0,    12,     0,     0,     0,     0,     0,     0,
       0,    13,    14,     0,     0,    15,     0,     0,    16,     0,
       0,     0,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,     0,    29,    30,     0,    31,    32,
      33,    34,    35,    36,    37,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    38,   490,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,    14,
       0,     0,    15,     0,     0,    16,     0,     0,     0,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,     0,    29,    30,     0,    31,    32,    33,    34,    35,
      36,    37,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     0,     0,     9,     0,     0,     0,     0,     0,     0,
      10,     0,     0,     0,    38,   598,     0,    12,     0,     0,
       0,     0,     0,     0,     0,    13,    14,     0,     0,    15,
       0,     0,    16,     0,     0,     0,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,     0,    29,
     599,     0,    31,    32,    33,    34,    35,    36,    37,     4,
       0,     0,     5,     0,     0,     6,     7,     8,     0,     0,
       9,     0,     0,     0,     0,     0,     0,    10,     0,     0,
       0,    38,   621,     0,    12,     0,     0,     0,     0,     0,
       0,     0,    13,    14,     0,     0,    15,     0,     0,    16,
       0,     0,     0,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,    28,     0,    29,    30,     0,    31,
      32,    33,    34,    35,    36,    37,     4,     0,     0,     5,
       0,     0,     6,     7,     8,     0,     0,     9,     0,     0,
       0,     0,     0,     0,    10,     0,     0,     0,    38,   624,
       0,    12,     0,     0,     0,     0,     0,     0,     0,    13,
      14,     0,     0,    15,     0,     0,    16,     0,     0,     0,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,     0,    29,    30,     0,    31,    32,    33,    34,
      35,    36,    37,     4,     0,     0,     5,     0,     0,     6,
       7,     8,     0,     0,     9,     0,     0,     0,     0,     0,
       0,    10,     0,     0,     0,    38,   656,     0,    12,     0,
       0,     0,     0,     0,     0,     0,    13,    14,     0,     0,
      15,     0,     0,    16,     0,     0,     0,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,     0,
      29,    30,     0,    31,    32,    33,    34,    35,    36,    37,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     0,
       0,     9,     0,     0,     0,     0,     0,     0,    10,     0,
       0,     0,    38,   699,     0,    12,     0,     0,     0,     0,
       0,     0,     0,    13,    14,     0,     0,    15,     0,     0,
      16,     0,     0,     0,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,     0,    29,    30,     0,
      31,    32,    33,    34,    35,    36,    37,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    38,
     703,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,    14,     0,     0,    15,     0,     0,    16,     0,     0,
       0,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,     0,    29,    30,     0,    31,    32,    33,
      34,    35,    36,    37,     4,     0,     0,     5,     0,     0,
       6,     7,     8,     0,     0,     9,     0,     0,     0,     0,
       0,     0,    10,     0,     0,     0,    38,   704,     0,    12,
       0,     0,     0,     0,     0,     0,     0,    13,    14,     0,
       0,    15,     0,     0,    16,     0,     0,     0,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
       0,    29,    30,     0,    31,    32,    33,    34,    35,    36,
      37,     4,     0,     0,     5,     0,     0,     6,     7,     8,
       0,     0,     9,     0,     0,     0,     0,     0,     0,    10,
       0,     0,     0,    38,   707,     0,    12,     0,     0,     0,
       0,     0,     0,     0,    13,    14,     0,     0,    15,     0,
       0,    16,     0,     0,     0,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,     0,    29,    30,
       0,    31,    32,    33,    34,    35,    36,    37,     4,     0,
       0,     5,     0,     0,     6,     7,     8,     0,     0,     9,
       0,     0,     0,     0,     0,     0,    10,     0,     0,     0,
      38,   708,     0,    12,     0,     0,     0,     0,     0,     0,
       0,    13,    14,     0,     0,    15,     0,     0,    16,     0,
       0,     0,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,     0,    29,    30,     0,    31,    32,
      33,    34,    35,    36,    37,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    38,   735,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,    14,
       0,     0,    15,     0,     0,    16,     0,     0,     0,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,     0,    29,    30,     0,    31,    32,    33,    34,    35,
      36,    37,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     0,     0,     9,     0,     0,     0,     0,     0,     0,
      10,     0,     0,     0,    38,   737,     0,    12,     0,     0,
       0,     0,     0,     0,     0,    13,    14,     0,     0,    15,
       0,     0,    16,     0,     0,     0,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,     0,    29,
      30,     0,    31,    32,    33,    34,    35,    36,    37,     4,
       0,     0,     5,     0,     0,     6,     7,     8,     0,     0,
       9,     0,     0,     0,     0,     0,     0,    10,     0,     0,
       0,    38,   800,     0,    12,     0,     0,     0,     0,     0,
       0,     0,    13,    14,     0,     0,    15,     0,     0,    16,
       0,     0,     0,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,    28,     0,    29,    30,     0,    31,
      32,    33,    34,    35,    36,    37,     4,     0,     0,     5,
       0,     0,     6,     7,     8,     0,     0,     9,     0,     0,
       0,     0,     0,     0,    10,     0,     0,     0,    38,   803,
       0,    12,     0,     0,     0,     0,     0,     0,     0,    13,
      14,     0,     0,    15,     0,     0,    16,     0,     0,     0,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,     0,    29,    30,     0,    31,    32,    33,    34,
      35,    36,    37,     4,     0,     0,     5,     0,     0,     6,
       7,     8,     0,     0,     9,     0,     0,     0,     0,     0,
       0,    10,     0,     0,     0,    38,   838,     0,    12,     0,
       0,     0,     0,     0,     0,     0,    13,    14,     0,     0,
      15,     0,     0,    16,     0,     0,     0,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,     0,
      29,    30,     0,    31,    32,    33,    34,    35,    36,    37,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     0,
       0,     9,     0,     0,     0,     0,     0,     0,    10,     0,
       0,     0,    38,   865,     0,    12,     0,     0,     0,     0,
       0,     0,     0,    13,    14,     0,     0,    15,     0,     0,
      16,     0,     0,     0,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,     0,    29,    30,     0,
      31,    32,    33,    34,    35,    36,    37,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    38,
     878,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,    14,     0,     0,    15,     0,     0,    16,     0,     0,
       0,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,     0,    29,    30,     0,    31,    32,    33,
      34,    35,    36,    37,   352,     0,     0,     5,     0,     0,
       6,     0,     0,     0,     0,   667,     0,     0,     0,     0,
       0,     0,    10,     0,     0,     0,    38,    11,     0,    12,
       0,     0,     0,     0,     0,     0,     0,    13,    14,     0,
       0,    15,     0,     0,    16,     0,     0,     0,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
       0,    29,    30,     0,    31,    32,    33,    34,    35,    36,
      37,   352,     0,     0,     5,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   591,
       0,     0,     0,   668,    11,     0,    12,     0,     0,     0,
       0,     0,     0,     0,    13,     0,     0,   352,     0,     0,
       5,     0,     0,     0,     0,     0,     0,    19,    20,     0,
      22,    23,    24,     0,     0,     0,     0,     0,    29,    30,
      11,    31,    12,     0,     0,    35,    36,     0,     0,     0,
      13,     0,     0,   352,     0,     0,     5,     0,     0,     0,
       0,     0,     0,    19,    20,     0,    22,    23,    24,     0,
     592,     0,     0,     0,    29,    30,    11,    31,    12,     0,
       0,    35,    36,     0,     0,     0,    13,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    19,
      20,     0,    22,    23,    24,     0,   353,     0,     0,     0,
      29,    30,     0,    31,     0,     0,     0,    35,    36,     0,
     324,   325,     0,     0,     0,     0,     0,     0,     0,     0,
     202,   203,     0,     0,     0,     0,     0,     0,   204,     0,
     205,   206,   794,   207,     0,   208,   209,   210,   211,   212,
     213,   214,   215,   216,   217,   218,   219,   220,   221,   222,
     223,   224,   225,   226,   227,   228,   229,   230,   231,   232,
     233,   234,   235,   236,     0,   237,   238,   239,   240,   241,
     242,   243,   244,   245,   246,   247,   248,   324,   325,     0,
       0,     0,     0,     0,     0,     0,     0,   202,   203,     0,
       0,     0,   326,     0,     0,   204,     0,   205,   206,     0,
     207,     0,   208,   209,   210,   211,   212,   213,   214,   215,
     216,   217,   218,   219,   220,   221,   222,   223,   224,   225,
     226,   227,   228,   229,   230,   231,   232,   233,   234,   235,
     236,     0,   237,   238,   239,   240,   241,   242,   243,   244,
     245,   246,   247,   248,   324,   325,     0,     0,     0,     0,
       0,     0,     0,     0,   202,   203,     0,     0,     0,   333,
       0,     0,   204,     0,   205,   206,     0,   207,     0,   208,
     209,   210,   211,   212,   213,   214,   215,   216,   217,   218,
     219,   220,   221,   222,   223,   224,   225,   226,   227,   228,
     229,   230,   231,   232,   233,   234,   235,   236,     0,   237,
     238,   239,   240,   241,   242,   243,   244,   245,   246,   247,
     248,   324,   325,     0,     0,     0,     0,     0,     0,     0,
       0,   202,   203,     0,     0,     0,   335,     0,     0,   204,
       0,   205,   206,     0,   207,     0,   208,   209,   210,   211,
     212,   213,   214,   215,   216,   217,   218,   219,   220,   221,
     222,   223,   224,   225,   226,   227,   228,   229,   230,   231,
     232,   233,   234,   235,   236,     0,   237,   238,   239,   240,
     241,   242,   243,   244,   245,   246,   247,   248,   324,   325,
       0,     0,     0,     0,     0,     0,     0,     0,   202,   203,
       0,     0,     0,   725,     0,     0,   204,     0,   205,   206,
       0,   207,     0,   208,   209,   210,   211,   212,   213,   214,
     215,   216,   217,   218,   219,   220,   221,   222,   223,   224,
     225,   226,   227,   228,   229,   230,   231,   232,   233,   234,
     235,   236,     0,   237,   238,   239,   240,   241,   242,   243,
     244,   245,   246,   247,   248,   324,   325,     0,     0,     0,
       0,     0,     0,     0,     0,   202,   203,     0,     0,     0,
     780,     0,     0,   204,     0,   205,   206,     0,   207,     0,
     208,   209,   210,   211,   212,   213,   214,   215,   216,   217,
     218,   219,   220,   221,   222,   223,   224,   225,   226,   227,
     228,   229,   230,   231,   232,   233,   234,   235,   236,     0,
     237,   238,   239,   240,   241,   242,   243,   244,   245,   246,
     247,   248,   316,     0,     0,   317,     0,     0,     0,     0,
       0,     0,   202,   203,     0,     0,     0,   808,     0,     0,
     204,     0,   205,   206,     0,   207,     0,   208,   209,   210,
     211,   212,   213,   214,   215,   216,   217,   218,   219,   220,
     221,   222,   223,   224,   225,   226,   227,   228,   229,   230,
     231,   232,   233,   234,   235,   236,     0,   237,   238,   239,
     240,   241,   242,   243,   244,   245,   246,   247,   248,   201,
       0,     0,   350,     0,     0,     0,     0,     0,     0,   202,
     203,     0,     0,     0,     0,     0,     0,   204,     0,   205,
     206,     0,   207,     0,   208,   209,   210,   211,   212,   213,
     214,   215,   216,   217,   218,   219,   220,   221,   222,   223,
     224,   225,   226,   227,   228,   229,   230,   231,   232,   233,
     234,   235,   236,     0,   237,   238,   239,   240,   241,   242,
     243,   244,   245,   246,   247,   248,   381,     0,     0,   382,
       0,     0,     0,     0,     0,     0,   202,   203,     0,     0,
       0,     0,     0,     0,   204,     0,   205,   206,     0,   207,
       0,   208,   209,   210,   211,   212,   213,   214,   215,   216,
     217,   218,   219,   220,   221,   222,   223,   224,   225,   226,
     227,   228,   229,   230,   231,   232,   233,   234,   235,   236,
       0,   237,   238,   239,   240,   241,   242,   243,   244,   245,
     246,   247,   248,   201,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   202,   203,     0,     0,     0,     0,     0,
       0,   204,     0,   205,   206,     0,   207,     0,   208,   209,
     210,   211,   212,   213,   214,   215,   216,   217,   218,   219,
     220,   221,   222,   223,   224,   225,   226,   227,   228,   229,
     230,   231,   232,   233,   234,   235,   236,     0,   237,   238,
     239,   240,   241,   242,   243,   244,   245,   246,   247,   248,
     303,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     202,   203,     0,     0,     0,     0,     0,     0,   204,     0,
     205,   206,     0,   207,     0,   208,   209,   210,   211,   212,
     213,   214,   215,   216,   217,   218,   219,   220,   221,   222,
     223,   224,   225,   226,   227,   228,   229,   230,   231,   232,
     233,   234,   235,   236,     0,   237,   238,   239,   240,   241,
     242,   243,   244,   245,   246,   247,   248
};

static const yytype_int16 yycheck[] =
{
       2,   186,   187,    97,    96,   253,   253,   518,   604,   289,
     521,    87,   572,   130,   477,   200,   253,    81,     0,   508,
     609,    23,    24,    25,   304,   613,   253,   307,   616,    93,
     253,   119,   609,   609,   253,   609,   609,   270,    46,    10,
       4,    12,    13,   609,     4,   139,    17,    18,   609,     4,
     609,    22,   253,   609,   607,    26,   253,   253,     4,    73,
      31,     4,    43,   609,   609,   661,   662,    27,   609,   253,
     164,   609,    85,   253,     4,   609,   665,   609,   609,     4,
       4,    27,    42,    91,    27,     4,     8,   550,   665,   665,
     553,   665,   665,     4,    16,    17,    18,    19,     4,   665,
      64,    42,    62,     4,   665,   422,   665,     4,     5,   665,
      29,     4,    93,   666,     7,    61,    27,    88,    61,   665,
     665,    27,    47,    94,   665,    85,    27,   665,   130,    85,
      85,   665,    62,   665,   665,    54,     4,     4,    62,   372,
       4,    97,   459,     4,    63,    42,    22,    93,   744,    71,
      93,    62,    93,   642,   617,    61,   745,   251,     4,    35,
      27,    62,    93,    27,   135,    87,    97,    89,   745,   745,
       4,   745,   745,   290,   762,    97,   190,     4,     4,   745,
     256,    27,   276,   277,   745,    71,   745,    93,    85,   745,
     161,   162,    93,    27,   165,    62,    93,    61,   286,   745,
     745,    27,   798,     4,   745,   293,    92,   745,   296,   273,
     274,   745,     4,   745,   745,    61,    71,    71,    85,     4,
     308,     4,    71,     4,    46,   736,    27,    61,   199,    93,
       4,    14,     4,   722,   723,    61,   250,    92,    92,    86,
       4,   730,    27,    92,    91,    87,    27,    93,     5,    71,
      93,   253,    94,    27,   346,    97,   348,    42,    25,    93,
      61,    86,    91,    27,    93,   353,    91,    93,    35,    91,
     518,   518,     4,   521,   521,   835,    88,    62,    60,    91,
      61,   518,     4,   285,   521,    42,    38,    61,   290,   778,
     292,   518,    93,     4,   521,   518,   856,    61,   521,   518,
       4,    88,   521,    45,   275,    27,    93,     4,   279,   280,
     281,   282,    93,    16,    17,    18,    19,   518,    54,    93,
     521,   518,   518,    27,   521,   521,   340,    63,    85,    93,
      27,     4,   821,   822,   518,    87,    93,   521,   518,    61,
      65,   521,    94,   435,    88,    97,    86,    91,    86,   320,
     321,    91,   841,    91,   446,    92,    23,    61,    86,    26,
     331,   609,   609,    91,    61,    86,    93,   461,    71,    86,
      91,    93,   609,    86,    91,    93,    90,   465,    91,    93,
      88,    88,   609,    91,    91,    91,   609,    93,    93,    93,
     609,   393,    93,   827,   482,   829,    93,   411,     4,     5,
     488,   403,     4,     5,   406,    81,    82,    93,   609,    93,
     424,   425,   609,   609,   416,    83,    84,   665,   665,   390,
      85,   392,    93,   394,    93,   609,    20,    21,   665,   609,
      90,    91,     4,     5,   405,    93,   450,   451,   665,    93,
     528,    93,   665,    93,   532,    93,   665,    93,   462,   463,
      85,   453,    92,    93,    92,    93,   641,    85,   429,   257,
     258,   432,   433,   551,   665,    61,   554,   555,   665,   665,
       4,   271,   272,   561,    85,     4,   564,    85,    24,    34,
       3,   665,    64,     4,    85,   665,    93,    85,   736,   736,
      93,   771,    85,    85,    93,   466,   467,   745,   745,   736,
     588,    41,   473,    93,   592,    85,    97,     4,   745,   736,
       4,    86,    91,   736,   602,   603,   518,   736,   745,   521,
      86,     4,   745,    85,     4,    86,   745,   529,   499,    86,
       4,    23,     4,     4,    41,   736,   721,    85,     4,   736,
     736,     5,   513,     3,   745,     5,     4,     6,   745,   745,
       5,    41,   736,    85,   556,    85,   736,    85,   560,    71,
      85,   745,   533,    42,    86,   745,    93,    93,     8,    93,
      30,    31,    32,    33,    89,    91,   664,    91,    86,    85,
      20,    21,    85,    85,    93,   770,    85,    92,    86,    85,
      46,     4,    93,    86,    14,   597,    91,   599,    86,    86,
      86,     4,   604,     4,    93,    13,    90,   609,    93,     4,
     698,    93,    93,   615,   702,    25,   587,   619,   620,    86,
     591,    81,    82,   625,    93,   627,    48,    46,    93,    28,
     601,    71,    72,    73,    74,    75,    76,    77,    78,    79,
      80,    93,    24,    93,     4,    93,    93,    56,   650,    89,
      86,    67,   654,   741,    59,     4,    93,   628,    86,   661,
     662,    86,    86,   665,     4,   636,   637,    93,    86,    27,
      22,     3,     4,     5,    86,     7,     8,     9,    93,    86,
      86,    24,    86,     3,     3,    71,     4,   775,     3,     4,
       5,    93,     7,     8,     9,    27,    93,    93,    30,    31,
      32,    33,    93,    93,   792,    93,   794,    39,    40,    92,
      85,     4,    27,     4,     4,    30,    31,    32,    33,    13,
      93,    36,    37,    93,    39,    40,    93,    41,    60,    93,
      62,    93,    64,    56,   736,    65,   824,    93,    93,     4,
       4,     4,   744,   745,    97,    60,    93,    62,    93,    64,
      82,    93,     4,    85,    93,    87,    27,    89,   760,    38,
      93,   763,    24,    24,    93,    97,    93,    82,    93,   857,
      85,    93,    87,    88,    89,     3,     4,     5,    93,     7,
       8,     9,    97,    71,    72,    73,    74,    75,    76,    77,
      78,    79,    80,    93,    93,    93,   798,    93,    93,    27,
      56,    93,    30,    31,    32,    33,    93,    93,    36,    37,
     812,    39,    40,    93,    42,    93,    93,    93,    52,    52,
      71,    93,    66,    93,    65,   827,    85,   829,    85,     4,
       4,    86,    60,    93,    62,    93,    64,     4,     4,    93,
      93,     4,    22,   814,    93,    93,   848,     3,     4,     5,
      22,     7,     8,     9,    82,    93,    93,    85,    93,    87,
       5,    89,    93,    93,    93,     4,    93,   869,    92,    97,
      93,    27,    93,    93,    30,    31,    32,    33,     4,    93,
      36,    37,    93,    39,    40,     3,     4,     5,    93,     7,
       8,     9,    93,    93,    52,    52,    93,    86,    93,    93,
       4,     4,    93,     4,    60,    93,    62,    93,    64,    27,
      93,    93,    30,    31,    32,    33,     4,    61,    36,    37,
      93,    39,    40,    93,    93,    93,    82,     4,    93,    85,
       4,    87,    61,    89,     4,     4,     4,    93,    93,    93,
      93,    97,    60,    93,    62,     4,    64,    93,     3,     4,
       5,    93,     7,     8,     9,    93,   166,   105,   145,   420,
     830,   154,    89,   269,    82,   456,   292,    85,   339,    87,
     254,    89,    27,   255,   505,    30,    31,    32,    33,    97,
     635,    36,    37,    -1,    39,    40,     3,     4,     5,    -1,
       7,     8,     9,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    60,    -1,    62,    -1,    64,
      27,    -1,    -1,    30,    31,    32,    33,    -1,    -1,    36,
      -1,    -1,    39,    40,    -1,    -1,    -1,    82,    -1,    -1,
      85,    -1,    87,    -1,    89,    -1,     4,    -1,    -1,     7,
      -1,    -1,    97,    60,    -1,    62,    -1,    64,    -1,     3,
       4,     5,    -1,     7,    22,     9,    -1,    -1,    -1,    27,
      -1,    29,    -1,    -1,    -1,    82,    -1,    -1,    85,    37,
      87,    -1,    89,    27,    -1,    -1,    30,    31,    32,    33,
      97,    -1,    50,    51,    -1,    53,    54,    55,    -1,    -1,
      -1,    -1,    -1,    61,    62,    -1,    64,    -1,    -1,    -1,
      68,    69,    -1,    -1,    -1,    -1,    60,    -1,    62,    -1,
      64,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,     4,
       5,    -1,    -1,    -1,    -1,    93,    -1,    -1,    -1,    14,
      15,    85,    -1,    87,    -1,    89,    -1,    22,    -1,    24,
      25,    -1,    27,    97,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    -1,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,     4,     5,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    14,    15,    -1,    -1,
      -1,    -1,    -1,    -1,    22,    90,    24,    25,    93,    27,
      -1,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      -1,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,     4,    -1,    -1,     7,    -1,    -1,    10,
      11,    12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,
      -1,    22,    -1,    -1,    -1,    93,    27,    -1,    29,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    37,    38,    -1,    -1,
      41,    -1,    -1,    44,    45,    -1,    -1,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    -1,
      61,    62,    -1,    64,    65,    66,    67,    68,    69,    70,
       4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,    -1,
      -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,
      -1,    25,    93,    27,    -1,    29,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    37,    38,    -1,    -1,    41,    -1,    -1,
      44,    -1,    -1,    -1,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    -1,    61,    62,    -1,
      64,    65,    66,    67,    68,    69,    70,     4,    -1,    -1,
       7,    -1,    -1,    10,    11,    12,    -1,    -1,    15,    -1,
      -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,    93,
      27,    -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      37,    38,    -1,    -1,    41,    -1,    -1,    44,    -1,    -1,
      -1,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    -1,    61,    62,    -1,    64,    65,    66,
      67,    68,    69,    70,     4,    -1,    -1,     7,    -1,    -1,
      10,    11,    12,    -1,    -1,    15,    -1,    -1,    -1,    -1,
      -1,    -1,    22,    -1,    -1,    -1,    93,    27,    -1,    29,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    38,    -1,
      -1,    41,    -1,    -1,    44,    -1,    -1,    -1,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      -1,    61,    62,    -1,    64,    65,    66,    67,    68,    69,
      70,     4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,
      -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,
      -1,    -1,    -1,    93,    27,    -1,    29,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    37,    38,    -1,    -1,    41,    -1,
      -1,    44,    -1,    -1,    -1,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    -1,    61,    62,
      -1,    64,    65,    66,    67,    68,    69,    70,     4,    -1,
      -1,     7,    -1,    -1,    10,    11,    12,    -1,    -1,    15,
      -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,
      93,    27,    -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    37,    38,    -1,    -1,    41,    -1,    -1,    44,    -1,
      -1,    -1,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    -1,    61,    62,    -1,    64,    65,
      66,    67,    68,    69,    70,     4,    -1,    -1,     7,    -1,
      -1,    10,    11,    12,    -1,    -1,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    -1,    -1,    93,    27,    -1,
      29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    38,
      -1,    -1,    41,    -1,    -1,    44,    -1,    -1,    -1,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    -1,    61,    62,    -1,    64,    65,    66,    67,    68,
      69,    70,     4,    -1,    -1,     7,    -1,    -1,    10,    11,
      12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,
      22,    -1,    -1,    -1,    93,    27,    -1,    29,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    37,    38,    -1,    -1,    41,
      -1,    -1,    44,    -1,    -1,    -1,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    -1,    61,
      62,    -1,    64,    65,    66,    67,    68,    69,    70,     4,
      -1,    -1,     7,    -1,    -1,    10,    11,    12,    -1,    -1,
      15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,
      -1,    93,    27,    -1,    29,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    37,    38,    -1,    -1,    41,    -1,    -1,    44,
      -1,    -1,    -1,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    -1,    61,    62,    -1,    64,
      65,    66,    67,    68,    69,    70,     4,    -1,    -1,     7,
      -1,    -1,    10,    11,    12,    -1,    -1,    15,    -1,    -1,
      -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,    93,    27,
      -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,
      38,    -1,    -1,    41,    -1,    -1,    44,    -1,    -1,    -1,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    -1,    61,    62,    -1,    64,    65,    66,    67,
      68,    69,    70,     4,    -1,    -1,     7,    -1,    -1,    10,
      11,    12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,
      -1,    22,    -1,    -1,    -1,    93,    27,    -1,    29,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    37,    38,    -1,    -1,
      41,    -1,    -1,    44,    -1,    -1,    -1,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    -1,
      61,    62,    -1,    64,    65,    66,    67,    68,    69,    70,
       4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,    -1,
      -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,
      -1,    -1,    93,    27,    -1,    29,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    37,    38,    -1,    -1,    41,    -1,    -1,
      44,    -1,    -1,    -1,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    -1,    61,    62,    -1,
      64,    65,    66,    67,    68,    69,    70,     4,    -1,    -1,
       7,    -1,    -1,    10,    11,    12,    -1,    -1,    15,    -1,
      -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,    93,
      27,    -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      37,    38,    -1,    -1,    41,    -1,    -1,    44,    -1,    -1,
      -1,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    -1,    61,    62,    -1,    64,    65,    66,
      67,    68,    69,    70,     4,    -1,    -1,     7,    -1,    -1,
      10,    11,    12,    -1,    -1,    15,    -1,    -1,    -1,    -1,
      -1,    -1,    22,    -1,    -1,    -1,    93,    27,    -1,    29,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    38,    -1,
      -1,    41,    -1,    -1,    44,    -1,    -1,    -1,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      -1,    61,    62,    -1,    64,    65,    66,    67,    68,    69,
      70,     4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,
      -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,
      -1,    -1,    -1,    93,    27,    -1,    29,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    37,    38,    -1,    -1,    41,    -1,
      -1,    44,    -1,    -1,    -1,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    -1,    61,    62,
      -1,    64,    65,    66,    67,    68,    69,    70,     4,    -1,
      -1,     7,    -1,    -1,    10,    11,    12,    -1,    -1,    15,
      -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,
      93,    27,    -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    37,    38,    -1,    -1,    41,    -1,    -1,    44,    -1,
      -1,    -1,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    -1,    61,    62,    -1,    64,    65,
      66,    67,    68,    69,    70,     4,    -1,    -1,     7,    -1,
      -1,    10,    11,    12,    -1,    -1,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    -1,    -1,    93,    27,    -1,
      29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    38,
      -1,    -1,    41,    -1,    -1,    44,    -1,    -1,    -1,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    -1,    61,    62,    -1,    64,    65,    66,    67,    68,
      69,    70,     4,    -1,    -1,     7,    -1,    -1,    10,    11,
      12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,
      22,    -1,    -1,    -1,    93,    27,    -1,    29,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    37,    38,    -1,    -1,    41,
      -1,    -1,    44,    -1,    -1,    -1,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    -1,    61,
      62,    -1,    64,    65,    66,    67,    68,    69,    70,     4,
      -1,    -1,     7,    -1,    -1,    10,    11,    12,    -1,    -1,
      15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,
      -1,    93,    27,    -1,    29,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    37,    38,    -1,    -1,    41,    -1,    -1,    44,
      -1,    -1,    -1,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    -1,    61,    62,    -1,    64,
      65,    66,    67,    68,    69,    70,     4,    -1,    -1,     7,
      -1,    -1,    10,    11,    12,    -1,    -1,    15,    -1,    -1,
      -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,    93,    27,
      -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,
      38,    -1,    -1,    41,    -1,    -1,    44,    -1,    -1,    -1,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    -1,    61,    62,    -1,    64,    65,    66,    67,
      68,    69,    70,     4,    -1,    -1,     7,    -1,    -1,    10,
      11,    12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,
      -1,    22,    -1,    -1,    -1,    93,    27,    -1,    29,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    37,    38,    -1,    -1,
      41,    -1,    -1,    44,    -1,    -1,    -1,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    -1,
      61,    62,    -1,    64,    65,    66,    67,    68,    69,    70,
       4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,    -1,
      -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,
      -1,    -1,    93,    27,    -1,    29,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    37,    38,    -1,    -1,    41,    -1,    -1,
      44,    -1,    -1,    -1,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    -1,    61,    62,    -1,
      64,    65,    66,    67,    68,    69,    70,     4,    -1,    -1,
       7,    -1,    -1,    10,    11,    12,    -1,    -1,    15,    -1,
      -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,    93,
      27,    -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      37,    38,    -1,    -1,    41,    -1,    -1,    44,    -1,    -1,
      -1,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    -1,    61,    62,    -1,    64,    65,    66,
      67,    68,    69,    70,     4,    -1,    -1,     7,    -1,    -1,
      10,    -1,    -1,    -1,    -1,    15,    -1,    -1,    -1,    -1,
      -1,    -1,    22,    -1,    -1,    -1,    93,    27,    -1,    29,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    38,    -1,
      -1,    41,    -1,    -1,    44,    -1,    -1,    -1,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      -1,    61,    62,    -1,    64,    65,    66,    67,    68,    69,
      70,     4,    -1,    -1,     7,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    22,
      -1,    -1,    -1,    93,    27,    -1,    29,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    37,    -1,    -1,     4,    -1,    -1,
       7,    -1,    -1,    -1,    -1,    -1,    -1,    50,    51,    -1,
      53,    54,    55,    -1,    -1,    -1,    -1,    -1,    61,    62,
      27,    64,    29,    -1,    -1,    68,    69,    -1,    -1,    -1,
      37,    -1,    -1,     4,    -1,    -1,     7,    -1,    -1,    -1,
      -1,    -1,    -1,    50,    51,    -1,    53,    54,    55,    -1,
      93,    -1,    -1,    -1,    61,    62,    27,    64,    29,    -1,
      -1,    68,    69,    -1,    -1,    -1,    37,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    50,
      51,    -1,    53,    54,    55,    -1,    93,    -1,    -1,    -1,
      61,    62,    -1,    64,    -1,    -1,    -1,    68,    69,    -1,
       4,     5,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      14,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,
      24,    25,    93,    27,    -1,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    -1,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,     4,     5,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    14,    15,    -1,
      -1,    -1,    86,    -1,    -1,    22,    -1,    24,    25,    -1,
      27,    -1,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    -1,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,    70,     4,     5,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    14,    15,    -1,    -1,    -1,    86,
      -1,    -1,    22,    -1,    24,    25,    -1,    27,    -1,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    -1,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      70,     4,     5,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    14,    15,    -1,    -1,    -1,    86,    -1,    -1,    22,
      -1,    24,    25,    -1,    27,    -1,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    -1,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,     4,     5,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    14,    15,
      -1,    -1,    -1,    86,    -1,    -1,    22,    -1,    24,    25,
      -1,    27,    -1,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    -1,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69,    70,     4,     5,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    14,    15,    -1,    -1,    -1,
      86,    -1,    -1,    22,    -1,    24,    25,    -1,    27,    -1,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    -1,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,     4,    -1,    -1,     7,    -1,    -1,    -1,    -1,
      -1,    -1,    14,    15,    -1,    -1,    -1,    86,    -1,    -1,
      22,    -1,    24,    25,    -1,    27,    -1,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    -1,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    70,     4,
      -1,    -1,     7,    -1,    -1,    -1,    -1,    -1,    -1,    14,
      15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,    24,
      25,    -1,    27,    -1,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    -1,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,     4,    -1,    -1,     7,
      -1,    -1,    -1,    -1,    -1,    -1,    14,    15,    -1,    -1,
      -1,    -1,    -1,    -1,    22,    -1,    24,    25,    -1,    27,
      -1,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      -1,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,     4,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    14,    15,    -1,    -1,    -1,    -1,    -1,
      -1,    22,    -1,    24,    25,    -1,    27,    -1,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    -1,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
       4,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      14,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,
      24,    25,    -1,    27,    -1,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    -1,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    99,   100,     0,     4,     7,    10,    11,    12,    15,
      22,    27,    29,    37,    38,    41,    44,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    61,
      62,    64,    65,    66,    67,    68,    69,    70,    93,   101,
     102,   105,   106,   111,   112,   113,   115,   116,   117,   118,
     123,   124,   125,   126,   127,   130,   131,   132,   134,   137,
     138,   145,   146,   147,   148,   149,   150,   151,   152,   153,
     154,     4,    85,    85,     4,     4,     4,     3,     4,     5,
       7,     8,     9,    30,    31,    32,    33,    36,    37,    39,
      40,    60,    64,    82,    85,    87,    89,    97,   106,   158,
     159,   160,   161,   162,   163,   164,   165,   166,   167,   168,
     170,   171,   174,    42,   158,   158,     4,     4,    47,    93,
     158,   158,     4,     4,     4,     7,   158,   106,   106,     4,
      85,   106,   133,   144,   158,    45,    60,     4,    64,    97,
     158,     4,    27,    42,    62,   109,   110,   128,     4,     4,
       4,     5,     4,     5,    65,    93,     8,    16,    17,    18,
      19,    71,    87,    89,    97,   104,   107,    92,    93,    93,
      93,    93,    93,    93,    93,    93,    93,    93,    93,    93,
      93,    85,   158,   175,   176,   175,    85,    85,    61,     4,
      85,   167,   162,   158,   168,     4,    85,   167,   158,   186,
     186,     4,    14,    15,    22,    24,    25,    27,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    70,   183,
      85,    97,   172,    24,    22,    35,    34,    20,    21,    71,
      72,    73,    74,    75,    76,    77,    78,    79,    80,   107,
     169,    81,    82,    83,    84,    87,    94,    97,     3,    64,
      85,    46,    71,    91,     4,   100,    93,    93,    85,    85,
      85,   133,    91,    93,    97,   158,    93,     4,    29,    54,
      63,    54,    63,     4,    85,   110,    41,    85,    93,     4,
      14,     4,   128,   158,   158,   108,     4,     7,   183,   158,
      71,   104,     4,   103,     4,     5,    86,   181,   183,   184,
      86,    91,    86,    86,   184,    86,   184,     4,   175,    38,
      85,    86,    86,    88,    93,   158,   177,    90,   184,   175,
       7,   183,     4,    93,   102,   106,   111,   112,   127,   146,
     147,   148,   150,   151,   152,   153,   157,   160,   161,   162,
     165,   165,   169,   164,   166,   166,   167,   167,   158,   182,
     183,     4,     7,   183,     4,   158,   158,   158,   158,     4,
      46,    91,    45,   100,    23,   119,     4,   178,   180,   178,
     133,    86,   144,   100,     4,    41,   100,     4,    62,     4,
      62,    85,   178,     4,   129,   178,   100,     5,     4,     5,
      41,    88,    91,     6,    85,    85,   158,   158,    85,    71,
      85,    93,    71,    92,    86,    91,   158,    93,    86,    93,
      86,    93,    97,    86,   171,   175,    91,   186,   186,    86,
      85,    85,   173,   100,    92,    93,    25,   164,    88,    91,
      90,    91,    85,    85,    86,    93,    42,    46,   158,     4,
     158,    27,   158,    23,    26,   120,    71,    92,    86,    91,
      86,    86,    93,    27,   158,    27,   175,    86,    93,    86,
      27,    14,   129,   176,    90,   175,   175,     4,   158,     4,
     185,   136,   158,   158,    93,   186,   139,    42,    93,    93,
      13,     4,    86,   186,    88,    90,   175,   175,    25,    27,
     155,    25,   156,   159,   176,   183,   175,   175,    93,   100,
     158,   158,    93,    46,    93,    48,    24,   158,    93,    28,
       3,     5,    30,    31,    32,    33,    81,    82,   179,   103,
      92,    93,     4,    92,    93,    93,   100,    56,    59,    86,
     100,    93,    67,     4,    93,    88,    86,    86,    86,   158,
      86,   141,   135,     4,   181,     4,    27,    61,    93,   140,
       4,   139,    27,    93,   158,    86,    86,    22,    93,   157,
      22,    22,    93,   157,    88,    86,    86,   100,    27,    62,
     114,    43,    93,    93,   100,   158,    93,    93,    24,   121,
      93,     3,     3,    71,   103,   100,    71,    92,   103,   100,
     100,    27,    93,    93,    27,   100,    93,   100,    92,     4,
      27,    61,    93,   142,   141,    85,    71,    92,     4,     5,
      42,    85,    93,     4,     4,     4,    27,     4,    13,   158,
     100,    93,    93,   158,   100,    93,    27,    41,    93,   106,
     158,   100,   100,   114,    93,   121,    93,    15,    93,   102,
     111,   112,   113,   115,   116,   117,   118,   122,   123,   124,
     125,   126,   127,   130,   131,   132,   138,   145,   146,   147,
     148,   149,   150,   151,   152,   153,   154,   179,    93,    27,
     179,   103,    93,    27,    27,    56,    65,    27,    27,   158,
       4,     5,   143,     4,     4,    27,   185,   158,   158,     5,
      42,    85,    93,    93,     4,    86,   184,   139,    93,    97,
      93,     4,    93,    27,    24,    27,    24,    27,    38,    93,
      93,    93,   114,   114,   100,   121,    93,    93,    93,    93,
      93,    93,    93,    93,    93,    93,    93,    93,    93,    93,
     100,    52,    71,   100,    52,    56,    93,    93,    66,    65,
      85,    85,    91,    93,    93,    93,     4,    86,    93,     4,
      86,   184,   139,   139,     4,    93,    86,    27,     4,   139,
      93,     4,    93,    22,    93,   157,    22,    93,   100,   114,
      27,    93,   179,    27,    93,    93,    93,    93,    86,   184,
     178,     5,   100,    93,    92,   139,     4,    93,    86,    27,
      27,    93,    93,     4,    93,    27,    93,   100,    93,   100,
      93,    93,   114,    52,    52,    93,    86,    86,    27,   158,
      27,    93,    93,     4,     4,   139,   139,    93,   100,     4,
     155,   155,   156,    93,    93,   141,    93,    93,    61,     4,
     139,    93,    93,    27,    27,    27,    93,    27,   141,   100,
      93,    93,    27,     4,     4,    61,     4,    27,    27,     4,
      93,    93,    93,    93,     4,     4,    93,    93,    93
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    98,    99,   100,   100,   100,   101,   101,   101,   101,
     101,   101,   101,   101,   101,   101,   101,   101,   101,   101,
     101,   101,   101,   101,   101,   101,   101,   101,   101,   101,
     101,   101,   101,   101,   101,   101,   102,   102,   102,   102,
     102,   103,   103,   104,   104,   104,   104,   105,   105,   105,
     105,   106,   106,   106,   108,   107,   107,   109,   109,   110,
     110,   110,   110,   111,   111,   112,   112,   112,   112,   112,
     112,   113,   114,   114,   114,   115,   115,   115,   115,   115,
     115,   116,   117,   118,   119,   119,   120,   120,   121,   121,
     121,   122,   122,   122,   122,   122,   122,   122,   122,   122,
     122,   122,   122,   122,   122,   122,   122,   122,   122,   122,
     122,   122,   122,   122,   122,   122,   122,   122,   122,   123,
     123,   123,   123,   124,   124,   125,   126,   127,   127,   127,
     127,   127,   127,   127,   127,   128,   128,   129,   130,   130,
     130,   131,   132,   132,   133,   133,   135,   134,   136,   134,
     137,   137,   137,   137,   138,   138,   138,   139,   139,   139,
     140,   140,   140,   140,   140,   140,   140,   140,   140,   140,
     140,   141,   141,   141,   142,   142,   142,   142,   142,   143,
     143,   144,   144,   145,   146,   146,   146,   146,   146,   146,
     146,   146,   147,   148,   148,   149,   150,   151,   152,   152,
     153,   153,   154,   154,   155,   155,   155,   155,   156,   156,
     156,   156,   156,   157,   157,   157,   157,   157,   157,   157,
     157,   157,   157,   157,   158,   158,   159,   159,   160,   160,
     161,   161,   162,   162,   163,   163,   163,   164,   164,   164,
     165,   165,   165,   166,   166,   166,   167,   167,   167,   167,
     167,   167,   168,   168,   168,   168,   168,   168,   168,   169,
     169,   169,   169,   169,   169,   169,   169,   169,   169,   170,
     170,   170,   170,   170,   170,   170,   170,   170,   170,   170,
     170,   170,   170,   170,   170,   170,   171,   171,   172,   172,
     172,   172,   173,   173,   174,   174,   175,   175,   176,   176,
     177,   177,   178,   178,   179,   179,   179,   179,   179,   179,
     179,   179,   180,   180,   180,   180,   180,   180,   180,   180,
     181,   181,   182,   182,   183,   183,   183,   183,   183,   183,
     183,   183,   183,   183,   183,   183,   183,   183,   183,   183,
     183,   183,   183,   183,   183,   183,   183,   183,   183,   183,
     183,   183,   183,   183,   183,   183,   183,   183,   183,   183,
     183,   183,   183,   183,   183,   183,   183,   183,   183,   183,
     183,   183,   184,   184,   184,   184,   184,   184,   185,   185,
     186,   186
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       2,     1,     1,     2,     2,     1,     2,     2,     2,     2,
       2,     2,     2,     2,     1,     1,     3,     3,     4,     5,
       4,     1,     4,     1,     1,     1,     1,     1,     4,     6,
       3,     1,     1,     1,     0,     4,     1,     1,     2,     1,
       1,     1,     1,     2,     4,     4,     2,     4,     6,     6,
       6,    10,     3,     2,     3,     7,     8,     9,    10,     9,
      11,     6,     7,     7,     5,     6,     0,     3,     0,     2,
       2,     2,     2,     2,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     2,     1,     2,     2,     1,     2,
       2,     2,     2,     2,     2,     2,     2,     1,     1,    10,
      10,    12,    12,     9,    10,    10,     7,     2,     2,     2,
       2,     4,     4,     4,     6,     1,     4,     1,     9,     7,
      10,     2,     3,     5,     1,     3,     0,    11,     0,    10,
       6,     5,     9,    11,    10,     9,    13,     0,     2,     2,
       4,     6,     5,     6,     9,     7,     8,     7,     9,    10,
       9,     0,     2,     2,     3,    10,    10,     9,     7,     1,
       3,     1,     3,     7,     4,     4,     3,     4,     4,     3,
       3,     3,     2,     1,     2,     2,     2,     2,     1,     2,
       1,     2,     6,     6,     3,     3,     6,     7,     0,     3,
       6,     7,     7,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     1,     5,     1,     3,
       1,     3,     1,     2,     1,     3,     4,     1,     3,     3,
       1,     3,     3,     1,     3,     3,     1,     2,     2,     2,
       4,     5,     1,     4,     6,     3,     4,     6,     6,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       2,     1,     3,     1,     1,     2,     4,     1,     1,     1,
       1,     1,     3,     3,     5,     1,     3,     5,     0,     3,
       3,     5,     0,     3,     2,     3,     0,     1,     1,     3,
       1,     4,     0,     1,     1,     2,     2,     1,     1,     1,
       1,     1,     1,     3,     3,     3,     5,     5,     7,     5,
       1,     1,     1,     3,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     3,     3,     6,     6,     6,     9,     1,     2,
       0,     2
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
#line 891 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3267 "src/parser.tab.c"
        break;

    case YYSYMBOL_STRING: /* STRING  */
#line 891 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3273 "src/parser.tab.c"
        break;

    case YYSYMBOL_LENS_CONTENT: /* LENS_CONTENT  */
#line 891 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3279 "src/parser.tab.c"
        break;

    case YYSYMBOL_QUALIFIED_IDENT: /* QUALIFIED_IDENT  */
#line 891 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3285 "src/parser.tab.c"
        break;

    case YYSYMBOL_MODIFIER_PREFIX: /* MODIFIER_PREFIX  */
#line 891 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3291 "src/parser.tab.c"
        break;

    case YYSYMBOL_MODIFIER_VALUE: /* MODIFIER_VALUE  */
#line 891 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3297 "src/parser.tab.c"
        break;

    case YYSYMBOL_SQL_TEXT: /* SQL_TEXT  */
#line 891 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3303 "src/parser.tab.c"
        break;

    case YYSYMBOL_program: /* program  */
#line 914 "src/parser.y"
            { (void) ((*yyvaluep).stmt_list); }
#line 3309 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement_list: /* statement_list  */
#line 851 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3315 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement: /* statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3321 "src/parser.tab.c"
        break;

    case YYSYMBOL_assignment: /* assignment  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3327 "src/parser.tab.c"
        break;

    case YYSYMBOL_type_lock: /* type_lock  */
#line 904 "src/parser.y"
            { ast_type_lock_free(((*yyvaluep).type_lock)); }
#line 3333 "src/parser.tab.c"
        break;

    case YYSYMBOL_lvalue: /* lvalue  */
#line 892 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3339 "src/parser.tab.c"
        break;

    case YYSYMBOL_variable_name: /* variable_name  */
#line 891 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3345 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_lens: /* comparison_lens  */
#line 898 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 3351 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_name: /* modifier_name  */
#line 891 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3357 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_word: /* modifier_word  */
#line 891 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3363 "src/parser.tab.c"
        break;

    case YYSYMBOL_print_statement: /* print_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3369 "src/parser.tab.c"
        break;

    case YYSYMBOL_call_statement: /* call_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3375 "src/parser.tab.c"
        break;

    case YYSYMBOL_with_lock_statement: /* with_lock_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3381 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_end: /* for_end  */
#line 891 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3387 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_each_statement: /* for_each_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3393 "src/parser.tab.c"
        break;

    case YYSYMBOL_do_loop_statement: /* do_loop_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3399 "src/parser.tab.c"
        break;

    case YYSYMBOL_while_statement: /* while_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3405 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement: /* consider_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3411 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_branch_list: /* consider_branch_list  */
#line 896 "src/parser.y"
            { ast_free_consider_branch_list(((*yyvaluep).consider_branch_list)); }
#line 3417 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_else_opt: /* consider_else_opt  */
#line 851 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3423 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement_list: /* consider_statement_list  */
#line 851 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3429 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_body_statement: /* consider_body_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3435 "src/parser.tab.c"
        break;

    case YYSYMBOL_function_statement: /* function_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3441 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_statement: /* modifier_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3447 "src/parser.tab.c"
        break;

    case YYSYMBOL_program_statement: /* program_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3453 "src/parser.tab.c"
        break;

    case YYSYMBOL_library_statement: /* library_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3459 "src/parser.tab.c"
        break;

    case YYSYMBOL_use_statement: /* use_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3465 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_signature: /* modifier_signature  */
#line 899 "src/parser.y"
            { ast_free_modifier_signature(((*yyvaluep).modifier_signature)); }
#line 3471 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_context: /* modifier_context  */
#line 891 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3477 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_statement: /* watch_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3483 "src/parser.tab.c"
        break;

    case YYSYMBOL_unwatch_statement: /* unwatch_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3489 "src/parser.tab.c"
        break;

    case YYSYMBOL_wait_statement: /* wait_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3495 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_list: /* watch_target_list  */
#line 897 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3501 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_statement: /* server_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3507 "src/parser.tab.c"
        break;

    case YYSYMBOL_gsql_block: /* gsql_block  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3513 "src/parser.tab.c"
        break;

    case YYSYMBOL_window_block: /* window_block  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3519 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item_list: /* server_item_list  */
#line 903 "src/parser.y"
            { ast_free_server_item_list(((*yyvaluep).server_item_list)); }
#line 3525 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item: /* server_item  */
#line 902 "src/parser.y"
            { AstServerItemList one = ast_server_item_list_append(ast_server_item_list_empty(), ((*yyvaluep).server_item)); ast_free_server_item_list(one); }
#line 3531 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_string_list: /* server_string_list  */
#line 897 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3537 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_path: /* watch_target_path  */
#line 891 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3543 "src/parser.tab.c"
        break;

    case YYSYMBOL_without_watchers_statement: /* without_watchers_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3549 "src/parser.tab.c"
        break;

    case YYSYMBOL_on_error_statement: /* on_error_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3555 "src/parser.tab.c"
        break;

    case YYSYMBOL_error_statement: /* error_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3561 "src/parser.tab.c"
        break;

    case YYSYMBOL_return_statement: /* return_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3567 "src/parser.tab.c"
        break;

    case YYSYMBOL_label_statement: /* label_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3573 "src/parser.tab.c"
        break;

    case YYSYMBOL_goto_statement: /* goto_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3579 "src/parser.tab.c"
        break;

    case YYSYMBOL_gosub_statement: /* gosub_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3585 "src/parser.tab.c"
        break;

    case YYSYMBOL_break_statement: /* break_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3591 "src/parser.tab.c"
        break;

    case YYSYMBOL_continue_statement: /* continue_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3597 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_statement: /* if_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3603 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_block_tail: /* if_block_tail  */
#line 851 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3609 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_inline_tail: /* if_inline_tail  */
#line 851 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3615 "src/parser.tab.c"
        break;

    case YYSYMBOL_inline_statement: /* inline_statement  */
#line 893 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3621 "src/parser.tab.c"
        break;

    case YYSYMBOL_expression: /* expression  */
#line 892 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3627 "src/parser.tab.c"
        break;

    case YYSYMBOL_conditional_expression: /* conditional_expression  */
#line 892 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3633 "src/parser.tab.c"
        break;

    case YYSYMBOL_or_expression: /* or_expression  */
#line 892 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3639 "src/parser.tab.c"
        break;

    case YYSYMBOL_and_expression: /* and_expression  */
#line 892 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3645 "src/parser.tab.c"
        break;

    case YYSYMBOL_not_expression: /* not_expression  */
#line 892 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3651 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_expression: /* comparison_expression  */
#line 892 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3657 "src/parser.tab.c"
        break;

    case YYSYMBOL_set_expression: /* set_expression  */
#line 892 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3663 "src/parser.tab.c"
        break;

    case YYSYMBOL_additive_expression: /* additive_expression  */
#line 892 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3669 "src/parser.tab.c"
        break;

    case YYSYMBOL_multiplicative_expression: /* multiplicative_expression  */
#line 892 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3675 "src/parser.tab.c"
        break;

    case YYSYMBOL_unary_expression: /* unary_expression  */
#line 892 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3681 "src/parser.tab.c"
        break;

    case YYSYMBOL_postfix_expression: /* postfix_expression  */
#line 892 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3687 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_operator: /* comparison_operator  */
#line 891 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3693 "src/parser.tab.c"
        break;

    case YYSYMBOL_primary: /* primary  */
#line 892 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3699 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_literal: /* record_literal  */
#line 892 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3705 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_suffix: /* ident_suffix  */
#line 900 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3711 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_dot_suffix: /* ident_dot_suffix  */
#line 900 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3717 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list_opt: /* argument_list_opt  */
#line 894 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3723 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list: /* argument_list  */
#line 894 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3729 "src/parser.tab.c"
        break;

    case YYSYMBOL_array_argument_list: /* array_argument_list  */
#line 894 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3735 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list_opt: /* parameter_list_opt  */
#line 897 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3741 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_default: /* parameter_default  */
#line 892 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3747 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list: /* parameter_list  */
#line 897 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3753 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_name: /* field_name  */
#line 891 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3759 "src/parser.tab.c"
        break;

    case YYSYMBOL_projection_names: /* projection_names  */
#line 897 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3765 "src/parser.tab.c"
        break;

    case YYSYMBOL_dot_field_name: /* dot_field_name  */
#line 891 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3771 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_field_list: /* record_field_list  */
#line 895 "src/parser.y"
            { ast_free_record_field_list(((*yyvaluep).record_field_list)); }
#line 3777 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_policy: /* field_policy  */
#line 901 "src/parser.y"
            { ast_free_expr(((*yyvaluep).field_policy).reset_expr); }
#line 3783 "src/parser.tab.c"
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


/* User initialization code.  */
#line 742 "src/parser.y"
{ (void)yynerrs; }

#line 3883 "src/parser.tab.c"

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
#line 919 "src/parser.y"
                     { ctx->parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 4096 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 923 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4102 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 924 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 4108 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 925 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 4114 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 929 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4120 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 930 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4126 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 931 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4132 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 932 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4138 "src/parser.tab.c"
    break;

  case 10: /* statement: window_block  */
#line 933 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4144 "src/parser.tab.c"
    break;

  case 11: /* statement: gsql_block  */
#line 934 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4150 "src/parser.tab.c"
    break;

  case 12: /* statement: for_each_statement  */
#line 935 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4156 "src/parser.tab.c"
    break;

  case 13: /* statement: while_statement  */
#line 936 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4162 "src/parser.tab.c"
    break;

  case 14: /* statement: do_loop_statement  */
#line 937 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4168 "src/parser.tab.c"
    break;

  case 15: /* statement: consider_statement  */
#line 938 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4174 "src/parser.tab.c"
    break;

  case 16: /* statement: function_statement  */
#line 939 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4180 "src/parser.tab.c"
    break;

  case 17: /* statement: modifier_statement  */
#line 940 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4186 "src/parser.tab.c"
    break;

  case 18: /* statement: program_statement  */
#line 941 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4192 "src/parser.tab.c"
    break;

  case 19: /* statement: library_statement  */
#line 942 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4198 "src/parser.tab.c"
    break;

  case 20: /* statement: use_statement NEWLINE  */
#line 943 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4204 "src/parser.tab.c"
    break;

  case 21: /* statement: watch_statement  */
#line 944 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4210 "src/parser.tab.c"
    break;

  case 22: /* statement: server_statement  */
#line 945 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4216 "src/parser.tab.c"
    break;

  case 23: /* statement: unwatch_statement NEWLINE  */
#line 946 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4222 "src/parser.tab.c"
    break;

  case 24: /* statement: wait_statement NEWLINE  */
#line 947 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4228 "src/parser.tab.c"
    break;

  case 25: /* statement: without_watchers_statement  */
#line 948 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4234 "src/parser.tab.c"
    break;

  case 26: /* statement: on_error_statement NEWLINE  */
#line 949 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4240 "src/parser.tab.c"
    break;

  case 27: /* statement: error_statement NEWLINE  */
#line 950 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4246 "src/parser.tab.c"
    break;

  case 28: /* statement: return_statement NEWLINE  */
#line 951 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4252 "src/parser.tab.c"
    break;

  case 29: /* statement: label_statement NEWLINE  */
#line 952 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4258 "src/parser.tab.c"
    break;

  case 30: /* statement: goto_statement NEWLINE  */
#line 953 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4264 "src/parser.tab.c"
    break;

  case 31: /* statement: gosub_statement NEWLINE  */
#line 954 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4270 "src/parser.tab.c"
    break;

  case 32: /* statement: break_statement NEWLINE  */
#line 955 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4276 "src/parser.tab.c"
    break;

  case 33: /* statement: continue_statement NEWLINE  */
#line 956 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4282 "src/parser.tab.c"
    break;

  case 34: /* statement: if_statement  */
#line 957 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4288 "src/parser.tab.c"
    break;

  case 35: /* statement: DIM  */
#line 963 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 4301 "src/parser.tab.c"
    break;

  case 36: /* assignment: lvalue OP_EQ expression  */
#line 974 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 4307 "src/parser.tab.c"
    break;

  case 37: /* assignment: lvalue compound_op expression  */
#line 979 "src/parser.y"
                                    { (yyval.stmt) = ast_assign_op((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr), (yyvsp[-1].op_char)); }
#line 4313 "src/parser.tab.c"
    break;

  case 38: /* assignment: lvalue comparison_lens compound_op expression  */
#line 980 "src/parser.y"
                                                    {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign_op((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr), (yyvsp[-1].op_char));
      }
#line 4327 "src/parser.tab.c"
    break;

  case 39: /* assignment: variable_name COLON type_lock OP_EQ expression  */
#line 996 "src/parser.y"
                                                     {
        if (!type_lock_check(ctx, (yyvsp[-4].text), (yyvsp[-2].type_lock).name, (yyvsp[-2].type_lock).arg, (yylsp[-2]).first_line, (yylsp[-2]).first_column,
                             (yylsp[-2]).last_line, (yylsp[-2]).last_column)) {
            YYERROR;
        }
        (yyval.stmt) = ast_assign_locked(expr_at(ast_ident((yyvsp[-4].text)), (yylsp[-4]).first_line, (yylsp[-4]).first_column), (yyvsp[-2].type_lock), (yyvsp[0].expr));
      }
#line 4339 "src/parser.tab.c"
    break;

  case 40: /* assignment: lvalue comparison_lens OP_EQ expression  */
#line 1003 "src/parser.y"
                                              {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 4353 "src/parser.tab.c"
    break;

  case 41: /* type_lock: IDENT  */
#line 1017 "src/parser.y"
            { (yyval.type_lock) = ast_type_lock((yyvsp[0].text), NULL, (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4359 "src/parser.tab.c"
    break;

  case 42: /* type_lock: IDENT LPAREN IDENT RPAREN  */
#line 1018 "src/parser.y"
                                { (yyval.type_lock) = ast_type_lock((yyvsp[-3].text), (yyvsp[-1].text), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 4365 "src/parser.tab.c"
    break;

  case 43: /* compound_op: PLUS_EQ  */
#line 1022 "src/parser.y"
               { (yyval.op_char) = '+'; }
#line 4371 "src/parser.tab.c"
    break;

  case 44: /* compound_op: MINUS_EQ  */
#line 1023 "src/parser.y"
               { (yyval.op_char) = '-'; }
#line 4377 "src/parser.tab.c"
    break;

  case 45: /* compound_op: STAR_EQ  */
#line 1024 "src/parser.y"
               { (yyval.op_char) = '*'; }
#line 4383 "src/parser.tab.c"
    break;

  case 46: /* compound_op: SLASH_EQ  */
#line 1025 "src/parser.y"
               { (yyval.op_char) = '/'; }
#line 4389 "src/parser.tab.c"
    break;

  case 47: /* lvalue: variable_name  */
#line 1029 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4395 "src/parser.tab.c"
    break;

  case 48: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 1030 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4401 "src/parser.tab.c"
    break;

  case 49: /* lvalue: lvalue LBRACKET expression COMMA argument_list RBRACKET  */
#line 1034 "src/parser.y"
                                                                           {
        AstExprList items = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[-3].expr));
        for (size_t i = 0; i < (yyvsp[-1].expr_list).count; i++) {
            items = ast_expr_list_append(items, (yyvsp[-1].expr_list).items[i]);
        }
        free((yyvsp[-1].expr_list).items);
        (yyval.expr) = expr_at(ast_index_list((yyvsp[-5].expr), items), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 4414 "src/parser.tab.c"
    break;

  case 50: /* lvalue: lvalue DOT dot_field_name  */
#line 1042 "src/parser.y"
                                             { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4420 "src/parser.tab.c"
    break;

  case 51: /* variable_name: IDENT  */
#line 1046 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 4426 "src/parser.tab.c"
    break;

  case 52: /* variable_name: END  */
#line 1047 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 4432 "src/parser.tab.c"
    break;

  case 53: /* variable_name: NEXT  */
#line 1048 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 4438 "src/parser.tab.c"
    break;

  case 54: /* $@1: %empty  */
#line 1059 "src/parser.y"
             { lexer_begin_lens_content(ctx->active_lexer); }
#line 4444 "src/parser.tab.c"
    break;

  case 55: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 1059 "src/parser.y"
                                                                                  {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 4452 "src/parser.tab.c"
    break;

  case 56: /* comparison_lens: MODIFIER_PREFIX  */
#line 1067 "src/parser.y"
                      { (yyval.modifier) = parse_modifier_use((yyvsp[0].text)); }
#line 4458 "src/parser.tab.c"
    break;

  case 57: /* modifier_name: modifier_word  */
#line 1071 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 4464 "src/parser.tab.c"
    break;

  case 58: /* modifier_name: modifier_name modifier_word  */
#line 1072 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 4470 "src/parser.tab.c"
    break;

  case 59: /* modifier_word: IDENT  */
#line 1076 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4476 "src/parser.tab.c"
    break;

  case 60: /* modifier_word: TO  */
#line 1077 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 4482 "src/parser.tab.c"
    break;

  case 61: /* modifier_word: END  */
#line 1078 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 4488 "src/parser.tab.c"
    break;

  case 62: /* modifier_word: NEXT  */
#line 1079 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 4494 "src/parser.tab.c"
    break;

  case 63: /* print_statement: PRINT expression  */
#line 1083 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 4500 "src/parser.tab.c"
    break;

  case 64: /* print_statement: PRINT TO ERROR_VALUE expression  */
#line 1089 "src/parser.y"
                                      { (yyval.stmt) = ast_print_error((yyvsp[0].expr)); }
#line 4506 "src/parser.tab.c"
    break;

  case 65: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 1093 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 4512 "src/parser.tab.c"
    break;

  case 66: /* call_statement: TRY expression  */
#line 1096 "src/parser.y"
                     { (yyval.stmt) = ast_expr_stmt(expr_at(ast_unary(copy_const("try"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column)); }
#line 4518 "src/parser.tab.c"
    break;

  case 67: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1097 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 4529 "src/parser.tab.c"
    break;

  case 68: /* call_statement: lvalue DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1103 "src/parser.y"
                                                       {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 4539 "src/parser.tab.c"
    break;

  case 69: /* call_statement: lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1108 "src/parser.y"
                                                                 {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 4553 "src/parser.tab.c"
    break;

  case 70: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1117 "src/parser.y"
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
#line 4568 "src/parser.tab.c"
    break;

  case 71: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 1130 "src/parser.y"
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
#line 4591 "src/parser.tab.c"
    break;

  case 72: /* for_end: END FOR NEWLINE  */
#line 1159 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 4597 "src/parser.tab.c"
    break;

  case 73: /* for_end: NEXT NEWLINE  */
#line 1160 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 4603 "src/parser.tab.c"
    break;

  case 74: /* for_end: NEXT variable_name NEWLINE  */
#line 1161 "src/parser.y"
                                 { (yyval.text) = (yyvsp[-1].text); }
#line 4609 "src/parser.tab.c"
    break;

  case 75: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list for_end  */
#line 1165 "src/parser.y"
                                                             {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4618 "src/parser.tab.c"
    break;

  case 76: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list for_end  */
#line 1169 "src/parser.y"
                                                                  {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4627 "src/parser.tab.c"
    break;

  case 77: /* for_each_statement: FOR IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1185 "src/parser.y"
                                                                         {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { YYERROR; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4637 "src/parser.tab.c"
    break;

  case 78: /* for_each_statement: FOR EACH IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1190 "src/parser.y"
                                                                              {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { YYERROR; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4647 "src/parser.tab.c"
    break;

  case 79: /* for_each_statement: FOR IDENT OP_EQ expression TO expression NEWLINE statement_list for_end  */
#line 1198 "src/parser.y"
                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].expr), NULL, (yyvsp[-1].stmt_list));
      }
#line 4656 "src/parser.tab.c"
    break;

  case 80: /* for_each_statement: FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list for_end  */
#line 1202 "src/parser.y"
                                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-9].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-9].text), (yyvsp[-7].expr), (yyvsp[-5].expr), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4665 "src/parser.tab.c"
    break;

  case 81: /* do_loop_statement: DO NEWLINE statement_list UNTIL expression NEWLINE  */
#line 1223 "src/parser.y"
                                                         {
        (yyval.stmt) = ast_do_loop((yyvsp[-3].stmt_list), (yyvsp[-1].expr));
      }
#line 4673 "src/parser.tab.c"
    break;

  case 82: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 1229 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 4681 "src/parser.tab.c"
    break;

  case 83: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 1235 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 4689 "src/parser.tab.c"
    break;

  case 84: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 1241 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 4697 "src/parser.tab.c"
    break;

  case 85: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 1244 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 4705 "src/parser.tab.c"
    break;

  case 86: /* consider_else_opt: %empty  */
#line 1250 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4711 "src/parser.tab.c"
    break;

  case 87: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 1251 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 4717 "src/parser.tab.c"
    break;

  case 88: /* consider_statement_list: %empty  */
#line 1255 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4723 "src/parser.tab.c"
    break;

  case 89: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 1256 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 4729 "src/parser.tab.c"
    break;

  case 90: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 1257 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 4735 "src/parser.tab.c"
    break;

  case 91: /* consider_body_statement: assignment NEWLINE  */
#line 1261 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4741 "src/parser.tab.c"
    break;

  case 92: /* consider_body_statement: print_statement NEWLINE  */
#line 1262 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4747 "src/parser.tab.c"
    break;

  case 93: /* consider_body_statement: call_statement NEWLINE  */
#line 1263 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4753 "src/parser.tab.c"
    break;

  case 94: /* consider_body_statement: with_lock_statement  */
#line 1264 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4759 "src/parser.tab.c"
    break;

  case 95: /* consider_body_statement: window_block  */
#line 1265 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4765 "src/parser.tab.c"
    break;

  case 96: /* consider_body_statement: for_each_statement  */
#line 1266 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4771 "src/parser.tab.c"
    break;

  case 97: /* consider_body_statement: while_statement  */
#line 1267 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4777 "src/parser.tab.c"
    break;

  case 98: /* consider_body_statement: do_loop_statement  */
#line 1268 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4783 "src/parser.tab.c"
    break;

  case 99: /* consider_body_statement: consider_statement  */
#line 1269 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4789 "src/parser.tab.c"
    break;

  case 100: /* consider_body_statement: function_statement  */
#line 1270 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4795 "src/parser.tab.c"
    break;

  case 101: /* consider_body_statement: modifier_statement  */
#line 1271 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4801 "src/parser.tab.c"
    break;

  case 102: /* consider_body_statement: program_statement  */
#line 1272 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4807 "src/parser.tab.c"
    break;

  case 103: /* consider_body_statement: library_statement  */
#line 1273 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4813 "src/parser.tab.c"
    break;

  case 104: /* consider_body_statement: use_statement NEWLINE  */
#line 1274 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4819 "src/parser.tab.c"
    break;

  case 105: /* consider_body_statement: watch_statement  */
#line 1275 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4825 "src/parser.tab.c"
    break;

  case 106: /* consider_body_statement: unwatch_statement NEWLINE  */
#line 1276 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4831 "src/parser.tab.c"
    break;

  case 107: /* consider_body_statement: wait_statement NEWLINE  */
#line 1277 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4837 "src/parser.tab.c"
    break;

  case 108: /* consider_body_statement: without_watchers_statement  */
#line 1278 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4843 "src/parser.tab.c"
    break;

  case 109: /* consider_body_statement: on_error_statement NEWLINE  */
#line 1279 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4849 "src/parser.tab.c"
    break;

  case 110: /* consider_body_statement: error_statement NEWLINE  */
#line 1280 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4855 "src/parser.tab.c"
    break;

  case 111: /* consider_body_statement: return_statement NEWLINE  */
#line 1281 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4861 "src/parser.tab.c"
    break;

  case 112: /* consider_body_statement: label_statement NEWLINE  */
#line 1282 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4867 "src/parser.tab.c"
    break;

  case 113: /* consider_body_statement: goto_statement NEWLINE  */
#line 1283 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4873 "src/parser.tab.c"
    break;

  case 114: /* consider_body_statement: gosub_statement NEWLINE  */
#line 1284 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4879 "src/parser.tab.c"
    break;

  case 115: /* consider_body_statement: break_statement NEWLINE  */
#line 1285 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4885 "src/parser.tab.c"
    break;

  case 116: /* consider_body_statement: continue_statement NEWLINE  */
#line 1286 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4891 "src/parser.tab.c"
    break;

  case 117: /* consider_body_statement: if_statement  */
#line 1287 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4897 "src/parser.tab.c"
    break;

  case 118: /* consider_body_statement: DIM  */
#line 1293 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 4910 "src/parser.tab.c"
    break;

  case 119: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1304 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4918 "src/parser.tab.c"
    break;

  case 120: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1307 "src/parser.y"
                                                                                                            {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4927 "src/parser.tab.c"
    break;

  case 121: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN COLON type_lock NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1314 "src/parser.y"
                                                                                                                  {
        char who[160];
        snprintf(who, sizeof who, "%s(...)", (yyvsp[-10].text));
        if (!type_lock_check(ctx, who, (yyvsp[-5].type_lock).name, (yyvsp[-5].type_lock).arg, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                             (yylsp[-5]).last_line, (yylsp[-5]).last_column)) {
            YYERROR;
        }
        (yyval.stmt) = ast_function_returning((yyvsp[-10].text), (yyvsp[-8].name_list), (yyvsp[-5].type_lock), (yyvsp[-3].stmt_list));
      }
#line 4941 "src/parser.tab.c"
    break;

  case 122: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN COLON type_lock NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1323 "src/parser.y"
                                                                                                                            {
        char who[160];
        snprintf(who, sizeof who, "%s(...)", (yyvsp[-10].text));
        if (!type_lock_check(ctx, who, (yyvsp[-5].type_lock).name, (yyvsp[-5].type_lock).arg, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                             (yylsp[-5]).last_line, (yylsp[-5]).last_column)) {
            YYERROR;
        }
        (yyval.stmt) = ast_function_returning((yyvsp[-10].text), (yyvsp[-8].name_list), (yyvsp[-5].type_lock), (yyvsp[-3].stmt_list));
      }
#line 4955 "src/parser.tab.c"
    break;

  case 123: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 1335 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 4963 "src/parser.tab.c"
    break;

  case 124: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 1338 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 4971 "src/parser.tab.c"
    break;

  case 125: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 1344 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4979 "src/parser.tab.c"
    break;

  case 126: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 1350 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 4987 "src/parser.tab.c"
    break;

  case 127: /* use_statement: USE IDENT  */
#line 1356 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4993 "src/parser.tab.c"
    break;

  case 128: /* use_statement: LOAD IDENT  */
#line 1357 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4999 "src/parser.tab.c"
    break;

  case 129: /* use_statement: USE STRING  */
#line 1358 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 5005 "src/parser.tab.c"
    break;

  case 130: /* use_statement: LOAD STRING  */
#line 1359 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 5011 "src/parser.tab.c"
    break;

  case 131: /* use_statement: LOAD IDENT AS IDENT  */
#line 1360 "src/parser.y"
                          { (yyval.stmt) = ast_use((yyvsp[-2].text), NULL, (yyvsp[0].text)); }
#line 5017 "src/parser.tab.c"
    break;

  case 132: /* use_statement: USE IDENT IDENT STRING  */
#line 1361 "src/parser.y"
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
#line 5038 "src/parser.tab.c"
    break;

  case 133: /* use_statement: LOAD IDENT IDENT STRING  */
#line 1377 "src/parser.y"
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
#line 5059 "src/parser.tab.c"
    break;

  case 134: /* use_statement: LOAD IDENT IDENT STRING AS IDENT  */
#line 1393 "src/parser.y"
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
#line 5082 "src/parser.tab.c"
    break;

  case 135: /* modifier_signature: modifier_name  */
#line 1414 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 5088 "src/parser.tab.c"
    break;

  case 136: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 1415 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 5094 "src/parser.tab.c"
    break;

  case 137: /* modifier_context: IDENT  */
#line 1419 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 5100 "src/parser.tab.c"
    break;

  case 138: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1423 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 5108 "src/parser.tab.c"
    break;

  case 139: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 1426 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 5116 "src/parser.tab.c"
    break;

  case 140: /* watch_statement: WATCH IDENT LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1434 "src/parser.y"
                                                                                           {
        (yyval.stmt) = ast_watch((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 5124 "src/parser.tab.c"
    break;

  case 141: /* unwatch_statement: UNWATCH expression  */
#line 1440 "src/parser.y"
                         { (yyval.stmt) = ast_unwatch((yyvsp[0].expr)); }
#line 5130 "src/parser.tab.c"
    break;

  case 142: /* wait_statement: WAIT UNTIL expression  */
#line 1447 "src/parser.y"
                            { (yyval.stmt) = ast_wait((yyvsp[0].expr), NULL); }
#line 5136 "src/parser.tab.c"
    break;

  case 143: /* wait_statement: WAIT UNTIL expression FOR expression  */
#line 1448 "src/parser.y"
                                           { (yyval.stmt) = ast_wait((yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 5142 "src/parser.tab.c"
    break;

  case 144: /* watch_target_list: watch_target_path  */
#line 1452 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5148 "src/parser.tab.c"
    break;

  case 145: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 1453 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5154 "src/parser.tab.c"
    break;

  case 146: /* @2: %empty  */
#line 1473 "src/parser.y"
      { (yyval.stmt) = NULL;   /* the mid-rule carries no value; typed so bison stays quiet */
        server_head_note(ctx, (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column); }
#line 5161 "src/parser.tab.c"
    break;

  case 147: /* server_statement: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE @2 server_item_list END IDENT NEWLINE  */
#line 1475 "src/parser.y"
                                         {
        ctx->bad_block_word[0] = '\0';
        (yyval.stmt) = ast_server((yyvsp[-10].text), (yyvsp[-9].text), (yyvsp[-7].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 5170 "src/parser.tab.c"
    break;

  case 148: /* @3: %empty  */
#line 1480 "src/parser.y"
      { (yyval.stmt) = NULL;   /* the mid-rule carries no value; typed so bison stays quiet */
        server_head_note(ctx, (yyvsp[-4].text), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5177 "src/parser.tab.c"
    break;

  case 149: /* server_statement: IDENT IDENT LPAREN RPAREN NEWLINE @3 server_item_list END IDENT NEWLINE  */
#line 1482 "src/parser.y"
                                         {
        ctx->bad_block_word[0] = '\0';
        (yyval.stmt) = ast_server((yyvsp[-9].text), (yyvsp[-8].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 5186 "src/parser.tab.c"
    break;

  case 150: /* gsql_block: DATABASE_OPEN IDENT LPAREN record_field_list RPAREN NEWLINE  */
#line 1502 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_database((yyvsp[-4].text), (yyvsp[-2].record_field_list), (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 5194 "src/parser.tab.c"
    break;

  case 151: /* gsql_block: DATABASE_OPEN IDENT LPAREN RPAREN NEWLINE  */
#line 1505 "src/parser.y"
                                                {
        (yyval.stmt) = ast_database((yyvsp[-3].text), ast_record_field_list_empty(), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5202 "src/parser.tab.c"
    break;

  case 152: /* gsql_block: SQL_OPEN IDENT ON IDENT NEWLINE SQL_TEXT END IDENT NEWLINE  */
#line 1508 "src/parser.y"
                                                                 {
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_sql((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].text), (yylsp[-3]).first_line, (yylsp[-3]).first_column, (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5211 "src/parser.tab.c"
    break;

  case 153: /* gsql_block: SQL_OPEN IDENT ON IDENT DOT IDENT NEWLINE SQL_TEXT END IDENT NEWLINE  */
#line 1514 "src/parser.y"
                                                                           {
        free((yyvsp[-1].text));
        size_t qn = strlen((yyvsp[-7].text)) + strlen((yyvsp[-5].text)) + 2;
        char *q = malloc(qn);
        if (!q) {
            abort();
        }
        snprintf(q, qn, "%s.%s", (yyvsp[-7].text), (yyvsp[-5].text));
        free((yyvsp[-7].text));
        free((yyvsp[-5].text));
        (yyval.stmt) = ast_sql((yyvsp[-9].text), q, (yyvsp[-3].text), (yylsp[-3]).first_line, (yylsp[-3]).first_column, (yylsp[-10]).first_line, (yylsp[-10]).first_column);
      }
#line 5228 "src/parser.tab.c"
    break;

  case 154: /* window_block: WINDOW_OPEN IDENT LPAREN record_field_list RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1529 "src/parser.y"
                                                                                                   {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), NULL, NULL, NULL, (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5237 "src/parser.tab.c"
    break;

  case 155: /* window_block: WINDOW_OPEN IDENT LPAREN RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1533 "src/parser.y"
                                                                                 {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text), NULL, NULL, NULL,
                        (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5247 "src/parser.tab.c"
    break;

  case 156: /* window_block: WINDOW_OPEN IDENT LPAREN record_field_list RPAREN TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1543 "src/parser.y"
                                                                                                                  {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-11].text), (yyvsp[-9].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text),
                        (yylsp[-12]).first_line, (yylsp[-12]).first_column);
      }
#line 5257 "src/parser.tab.c"
    break;

  case 157: /* window_item_list: %empty  */
#line 1551 "src/parser.y"
             { (yyval.window_item_list) = ast_window_item_list_empty(); }
#line 5263 "src/parser.tab.c"
    break;

  case 158: /* window_item_list: window_item_list NEWLINE  */
#line 1552 "src/parser.y"
                               { (yyval.window_item_list) = (yyvsp[-1].window_item_list); }
#line 5269 "src/parser.tab.c"
    break;

  case 159: /* window_item_list: window_item_list window_item  */
#line 1553 "src/parser.y"
                                   { (yyval.window_item_list) = ast_window_item_list_append((yyvsp[-1].window_item_list), (yyvsp[0].window_item)); }
#line 5275 "src/parser.tab.c"
    break;

  case 160: /* window_item: IDENT LPAREN RPAREN NEWLINE  */
#line 1558 "src/parser.y"
                                  {
        (yyval.window_item) = ast_window_leaf((yyvsp[-3].text), NULL, ast_record_field_list_empty(),
                             (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 5284 "src/parser.tab.c"
    break;

  case 161: /* window_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE  */
#line 1563 "src/parser.y"
                                                          {
        (yyval.window_item) = ast_window_leaf((yyvsp[-5].text), (yyvsp[-4].text), (yyvsp[-2].record_field_list), (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 5292 "src/parser.tab.c"
    break;

  case 162: /* window_item: IDENT IDENT LPAREN RPAREN NEWLINE  */
#line 1567 "src/parser.y"
                                        {
        (yyval.window_item) = ast_window_leaf((yyvsp[-4].text), (yyvsp[-3].text), ast_record_field_list_empty(),
                             (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5301 "src/parser.tab.c"
    break;

  case 163: /* window_item: IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1572 "src/parser.y"
                                                       {
        (yyval.window_item) = ast_window_container((yyvsp[-5].text), NULL, ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                  (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 5310 "src/parser.tab.c"
    break;

  case 164: /* window_item: IDENT LPAREN record_field_list RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1577 "src/parser.y"
                                                                                       {
        (yyval.window_item) = ast_window_container((yyvsp[-8].text), NULL, (yyvsp[-6].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5318 "src/parser.tab.c"
    break;

  case 165: /* window_item: IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1591 "src/parser.y"
                                                             {
        (yyval.window_item) = ast_window_named_form((yyvsp[-6].text), (yyvsp[-5].text), NULL, NULL, NULL, (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                   (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5327 "src/parser.tab.c"
    break;

  case 166: /* window_item: IDENT IDENT STRING NEWLINE window_item_list END IDENT NEWLINE  */
#line 1606 "src/parser.y"
                                                                    {
        (yyval.window_item) = ast_window_named_container((yyvsp[-7].text), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                        (yylsp[-7]).first_line, (yylsp[-7]).first_column);
      }
#line 5336 "src/parser.tab.c"
    break;

  case 167: /* window_item: IDENT STRING NEWLINE window_item_list END IDENT NEWLINE  */
#line 1611 "src/parser.y"
                                                              {
        (yyval.window_item) = ast_window_container((yyvsp[-6].text), (yyvsp[-5].text), ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                  (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5345 "src/parser.tab.c"
    break;

  case 168: /* window_item: IDENT TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1622 "src/parser.y"
                                                                      {
        (yyval.window_item) = ast_window_form((yyvsp[-8].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                             (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5354 "src/parser.tab.c"
    break;

  case 169: /* window_item: IDENT IDENT TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1637 "src/parser.y"
                                                                            {
        (yyval.window_item) = ast_window_named_form((yyvsp[-9].text), (yyvsp[-8].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                   (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5363 "src/parser.tab.c"
    break;

  case 170: /* window_item: ON IDENT DOT IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1642 "src/parser.y"
                                                               {
        (yyval.window_item) = ast_window_handler((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5371 "src/parser.tab.c"
    break;

  case 171: /* server_item_list: %empty  */
#line 1648 "src/parser.y"
             { (yyval.server_item_list) = ast_server_item_list_empty(); }
#line 5377 "src/parser.tab.c"
    break;

  case 172: /* server_item_list: server_item_list NEWLINE  */
#line 1649 "src/parser.y"
                               { (yyval.server_item_list) = (yyvsp[-1].server_item_list); }
#line 5383 "src/parser.tab.c"
    break;

  case 173: /* server_item_list: server_item_list server_item  */
#line 1650 "src/parser.y"
                                   { (yyval.server_item_list) = ast_server_item_list_append((yyvsp[-1].server_item_list), (yyvsp[0].server_item)); }
#line 5389 "src/parser.tab.c"
    break;

  case 174: /* server_item: IDENT server_string_list NEWLINE  */
#line 1654 "src/parser.y"
                                       {
        (yyval.server_item) = ast_server_directive((yyvsp[-2].text), (yyvsp[-1].name_list), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 5397 "src/parser.tab.c"
    break;

  case 175: /* server_item: IDENT STRING LPAREN parameter_list_opt RPAREN NEWLINE statement_list END IDENT NEWLINE  */
#line 1657 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_handler((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5405 "src/parser.tab.c"
    break;

  case 176: /* server_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1660 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_site((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5413 "src/parser.tab.c"
    break;

  case 177: /* server_item: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1663 "src/parser.y"
                                                                           {
        (yyval.server_item) = ast_server_site((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5421 "src/parser.tab.c"
    break;

  case 178: /* server_item: ON IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1666 "src/parser.y"
                                                     {
        (yyval.server_item) = ast_server_hook((yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5429 "src/parser.tab.c"
    break;

  case 179: /* server_string_list: STRING  */
#line 1672 "src/parser.y"
             { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5435 "src/parser.tab.c"
    break;

  case 180: /* server_string_list: server_string_list COMMA STRING  */
#line 1673 "src/parser.y"
                                      { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5441 "src/parser.tab.c"
    break;

  case 181: /* watch_target_path: variable_name  */
#line 1677 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 5447 "src/parser.tab.c"
    break;

  case 182: /* watch_target_path: watch_target_path DOT IDENT  */
#line 1678 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 5453 "src/parser.tab.c"
    break;

  case 183: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 1682 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 5461 "src/parser.tab.c"
    break;

  case 184: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 1688 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 5467 "src/parser.tab.c"
    break;

  case 185: /* on_error_statement: ON ERROR_VALUE GOTO NEXT  */
#line 1689 "src/parser.y"
                               { (yyval.stmt) = ast_on_error_goto_next(); }
#line 5473 "src/parser.tab.c"
    break;

  case 186: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 1690 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 5479 "src/parser.tab.c"
    break;

  case 187: /* on_error_statement: ON IDENT GOTO NEXT  */
#line 1691 "src/parser.y"
                         {
        if (!warn_channel_ok(ctx, (yyvsp[-2].text), (yylsp[-2]).first_line, (yylsp[-2]).first_column)) { YYERROR; }
        free((yyvsp[-2].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_NEXT);
      }
#line 5489 "src/parser.tab.c"
    break;

  case 188: /* on_error_statement: ON IDENT GOTO IDENT  */
#line 1696 "src/parser.y"
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
        (yyval.stmt) = NULL;   /* never used: YYERROR discards it, but bison asks every rule to set one */
        YYERROR;
      }
#line 5508 "src/parser.tab.c"
    break;

  case 189: /* on_error_statement: ON IDENT STOP  */
#line 1710 "src/parser.y"
                    {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_STOP);
      }
#line 5518 "src/parser.tab.c"
    break;

  case 190: /* on_error_statement: ON IDENT PRINT  */
#line 1715 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_PRINT);
      }
#line 5528 "src/parser.tab.c"
    break;

  case 191: /* on_error_statement: ON IDENT IDENT  */
#line 1720 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        int mode = warn_mode_word(ctx, (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column);
        if (mode < 0) { free((yyvsp[-1].text)); free((yyvsp[0].text)); YYERROR; }
        free((yyvsp[-1].text)); free((yyvsp[0].text));
        (yyval.stmt) = ast_on_warning(mode);
      }
#line 5540 "src/parser.tab.c"
    break;

  case 192: /* error_statement: ERROR_VALUE expression  */
#line 1730 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 5546 "src/parser.tab.c"
    break;

  case 193: /* return_statement: RETURN  */
#line 1734 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 5552 "src/parser.tab.c"
    break;

  case 194: /* return_statement: RETURN expression  */
#line 1735 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 5558 "src/parser.tab.c"
    break;

  case 195: /* label_statement: variable_name COLON  */
#line 1739 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 5564 "src/parser.tab.c"
    break;

  case 196: /* goto_statement: GOTO variable_name  */
#line 1746 "src/parser.y"
                         { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 5570 "src/parser.tab.c"
    break;

  case 197: /* gosub_statement: GOSUB variable_name  */
#line 1750 "src/parser.y"
                          { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 5576 "src/parser.tab.c"
    break;

  case 198: /* break_statement: BREAK  */
#line 1759 "src/parser.y"
            { (yyval.stmt) = ast_break(NULL); }
#line 5582 "src/parser.tab.c"
    break;

  case 199: /* break_statement: BREAK IDENT  */
#line 1760 "src/parser.y"
                  { (yyval.stmt) = ast_break((yyvsp[0].text)); }
#line 5588 "src/parser.tab.c"
    break;

  case 200: /* continue_statement: CONTINUE  */
#line 1764 "src/parser.y"
               { (yyval.stmt) = ast_continue(NULL); }
#line 5594 "src/parser.tab.c"
    break;

  case 201: /* continue_statement: CONTINUE IDENT  */
#line 1765 "src/parser.y"
                     { (yyval.stmt) = ast_continue((yyvsp[0].text)); }
#line 5600 "src/parser.tab.c"
    break;

  case 202: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1769 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 5609 "src/parser.tab.c"
    break;

  case 203: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1773 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 5618 "src/parser.tab.c"
    break;

  case 204: /* if_block_tail: END IF NEWLINE  */
#line 1780 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 5626 "src/parser.tab.c"
    break;

  case 205: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 1783 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 5634 "src/parser.tab.c"
    break;

  case 206: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1786 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 5642 "src/parser.tab.c"
    break;

  case 207: /* if_block_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1795 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5654 "src/parser.tab.c"
    break;

  case 208: /* if_inline_tail: %empty  */
#line 1805 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 5662 "src/parser.tab.c"
    break;

  case 209: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 1808 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 5670 "src/parser.tab.c"
    break;

  case 210: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1811 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 5678 "src/parser.tab.c"
    break;

  case 211: /* if_inline_tail: ELSE IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1817 "src/parser.y"
                                                                      {
        AstStmt *inner = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5690 "src/parser.tab.c"
    break;

  case 212: /* if_inline_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1824 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5702 "src/parser.tab.c"
    break;

  case 213: /* inline_statement: assignment  */
#line 1834 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5708 "src/parser.tab.c"
    break;

  case 214: /* inline_statement: print_statement  */
#line 1835 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5714 "src/parser.tab.c"
    break;

  case 215: /* inline_statement: call_statement  */
#line 1836 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5720 "src/parser.tab.c"
    break;

  case 216: /* inline_statement: use_statement  */
#line 1837 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5726 "src/parser.tab.c"
    break;

  case 217: /* inline_statement: on_error_statement  */
#line 1838 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5732 "src/parser.tab.c"
    break;

  case 218: /* inline_statement: error_statement  */
#line 1839 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5738 "src/parser.tab.c"
    break;

  case 219: /* inline_statement: return_statement  */
#line 1840 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5744 "src/parser.tab.c"
    break;

  case 220: /* inline_statement: goto_statement  */
#line 1841 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5750 "src/parser.tab.c"
    break;

  case 221: /* inline_statement: gosub_statement  */
#line 1842 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5756 "src/parser.tab.c"
    break;

  case 222: /* inline_statement: break_statement  */
#line 1843 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5762 "src/parser.tab.c"
    break;

  case 223: /* inline_statement: continue_statement  */
#line 1844 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5768 "src/parser.tab.c"
    break;

  case 224: /* expression: conditional_expression  */
#line 1852 "src/parser.y"
                             { (yyval.expr) = (yyvsp[0].expr); }
#line 5774 "src/parser.tab.c"
    break;

  case 225: /* expression: TRY expression  */
#line 1853 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_unary(copy_const("try"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5780 "src/parser.tab.c"
    break;

  case 226: /* conditional_expression: or_expression  */
#line 1857 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 5786 "src/parser.tab.c"
    break;

  case 227: /* conditional_expression: or_expression IF or_expression ELSE conditional_expression  */
#line 1858 "src/parser.y"
                                                                 {
        (yyval.expr) = expr_at(ast_conditional((yyvsp[-2].expr), (yyvsp[-4].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 5794 "src/parser.tab.c"
    break;

  case 228: /* or_expression: and_expression  */
#line 1864 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5800 "src/parser.tab.c"
    break;

  case 229: /* or_expression: or_expression OR and_expression  */
#line 1865 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5806 "src/parser.tab.c"
    break;

  case 230: /* and_expression: not_expression  */
#line 1869 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5812 "src/parser.tab.c"
    break;

  case 231: /* and_expression: and_expression AND not_expression  */
#line 1870 "src/parser.y"
                                        { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5818 "src/parser.tab.c"
    break;

  case 232: /* not_expression: comparison_expression  */
#line 1892 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 5824 "src/parser.tab.c"
    break;

  case 233: /* not_expression: NOT not_expression  */
#line 1893 "src/parser.y"
                         { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5830 "src/parser.tab.c"
    break;

  case 234: /* comparison_expression: set_expression  */
#line 1897 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5836 "src/parser.tab.c"
    break;

  case 235: /* comparison_expression: set_expression comparison_operator set_expression  */
#line 1898 "src/parser.y"
                                                        { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5842 "src/parser.tab.c"
    break;

  case 236: /* comparison_expression: set_expression comparison_lens comparison_operator set_expression  */
#line 1899 "src/parser.y"
                                                                        {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 5850 "src/parser.tab.c"
    break;

  case 237: /* set_expression: additive_expression  */
#line 1905 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 5856 "src/parser.tab.c"
    break;

  case 238: /* set_expression: set_expression EXCLUDING additive_expression  */
#line 1906 "src/parser.y"
                                                   { (yyval.expr) = expr_at(ast_binary(copy_const("excluding"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5862 "src/parser.tab.c"
    break;

  case 239: /* set_expression: set_expression INTERSECTING additive_expression  */
#line 1907 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("intersecting"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5868 "src/parser.tab.c"
    break;

  case 240: /* additive_expression: multiplicative_expression  */
#line 1911 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 5874 "src/parser.tab.c"
    break;

  case 241: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 1912 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5880 "src/parser.tab.c"
    break;

  case 242: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 1913 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5886 "src/parser.tab.c"
    break;

  case 243: /* multiplicative_expression: unary_expression  */
#line 1917 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 5892 "src/parser.tab.c"
    break;

  case 244: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 1918 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5898 "src/parser.tab.c"
    break;

  case 245: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 1919 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5904 "src/parser.tab.c"
    break;

  case 246: /* unary_expression: postfix_expression  */
#line 1923 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 5910 "src/parser.tab.c"
    break;

  case 247: /* unary_expression: MINUS unary_expression  */
#line 1924 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5916 "src/parser.tab.c"
    break;

  case 248: /* unary_expression: MODIFIER_PREFIX unary_expression  */
#line 1931 "src/parser.y"
                                       {
        (yyval.expr) = expr_at(ast_modifier_apply(parse_modifier_use((yyvsp[-1].text)), (yyvsp[0].expr)),
                     (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 5925 "src/parser.tab.c"
    break;

  case 249: /* unary_expression: NEW postfix_expression  */
#line 1935 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5931 "src/parser.tab.c"
    break;

  case 250: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 1936 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 5937 "src/parser.tab.c"
    break;

  case 251: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 1937 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5943 "src/parser.tab.c"
    break;

  case 252: /* postfix_expression: primary  */
#line 1941 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 5949 "src/parser.tab.c"
    break;

  case 253: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 1942 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5955 "src/parser.tab.c"
    break;

  case 254: /* postfix_expression: postfix_expression LBRACKET expression COMMA argument_list RBRACKET  */
#line 1946 "src/parser.y"
                                                                          {
        AstExprList items = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[-3].expr));
        for (size_t i = 0; i < (yyvsp[-1].expr_list).count; i++) {
            items = ast_expr_list_append(items, (yyvsp[-1].expr_list).items[i]);
        }
        free((yyvsp[-1].expr_list).items);
        (yyval.expr) = expr_at(ast_index_list((yyvsp[-5].expr), items), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5968 "src/parser.tab.c"
    break;

  case 255: /* postfix_expression: postfix_expression DOT dot_field_name  */
#line 1954 "src/parser.y"
                                            { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5974 "src/parser.tab.c"
    break;

  case 256: /* postfix_expression: postfix_expression PATH_LBRACE projection_names RBRACE  */
#line 1959 "src/parser.y"
                                                             {
        (yyval.expr) = expr_at(ast_project((yyvsp[-3].expr), (yyvsp[-1].name_list)), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 5982 "src/parser.tab.c"
    break;

  case 257: /* postfix_expression: postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1962 "src/parser.y"
                                                                   {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        (yyval.expr) = expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5992 "src/parser.tab.c"
    break;

  case 258: /* postfix_expression: postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1967 "src/parser.y"
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
#line 6008 "src/parser.tab.c"
    break;

  case 259: /* comparison_operator: OP_EQ  */
#line 1981 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 6014 "src/parser.tab.c"
    break;

  case 260: /* comparison_operator: OP_NE  */
#line 1982 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 6020 "src/parser.tab.c"
    break;

  case 261: /* comparison_operator: OP_GT  */
#line 1983 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 6026 "src/parser.tab.c"
    break;

  case 262: /* comparison_operator: OP_LT  */
#line 1984 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 6032 "src/parser.tab.c"
    break;

  case 263: /* comparison_operator: OP_GE  */
#line 1985 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 6038 "src/parser.tab.c"
    break;

  case 264: /* comparison_operator: OP_LE  */
#line 1986 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 6044 "src/parser.tab.c"
    break;

  case 265: /* comparison_operator: OP_NGT  */
#line 1987 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 6050 "src/parser.tab.c"
    break;

  case 266: /* comparison_operator: OP_NLT  */
#line 1988 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 6056 "src/parser.tab.c"
    break;

  case 267: /* comparison_operator: OP_NGE  */
#line 1989 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 6062 "src/parser.tab.c"
    break;

  case 268: /* comparison_operator: OP_NLE  */
#line 1990 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 6068 "src/parser.tab.c"
    break;

  case 269: /* primary: MODIFIER_VALUE  */
#line 1994 "src/parser.y"
                     {
        /* A DECLARATION IS NOT AN EXPRESSION CONTEXT, which is the line the
         * `server` block's literal-only head already draws. `type: {USD}`
         * names a type inside a `window`; `m = {USD}` is refused here rather
         * than becoming a 28th value kind with answers owed for rendering,
         * `encode` and comparison. See docs/qt_gui_design.md §5.2. */
        if (ctx->window_depth <= 0) {
            report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                                (yylsp[0]).last_line, (yylsp[0]).last_column,
                                "a modifier names a type only inside a declaration; "
                                "apply it to something (`{USD}19.99`) or use a clause "
                                "(`x {USD}= 19.99`)");
            free((yyvsp[0].text));
            (yyvsp[0].text) = NULL;
            YYERROR;
        }
        (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column);
      }
#line 6091 "src/parser.tab.c"
    break;

  case 270: /* primary: DOT dot_field_name  */
#line 2015 "src/parser.y"
                         { (yyval.expr) = expr_at(ast_column((yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6097 "src/parser.tab.c"
    break;

  case 271: /* primary: NUMBER  */
#line 2016 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact, (yyvsp[0].numlit).scale), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6103 "src/parser.tab.c"
    break;

  case 272: /* primary: WATCHERS LPAREN RPAREN  */
#line 2017 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_call(copy_const("watchers"), ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 6109 "src/parser.tab.c"
    break;

  case 273: /* primary: duration_terms  */
#line 2018 "src/parser.y"
                     {
        if ((yyvsp[0].duration).cells) {
            /* `40 columns` is the record `{ columns: 40 }`, a size the `ui`
             * backends convert; one term, a whole number of at least 1 */
            if ((yyvsp[0].duration).cells_mixed) {
                report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column,
                                    "a size in columns or rows is one term on its own, "
                                    "as `40 columns` or `12 rows`: it cannot join a duration or another size");
                YYERROR;
            }
            if ((yyvsp[0].duration).cells_amount < 1 || (yyvsp[0].duration).cells_amount != floor((yyvsp[0].duration).cells_amount) || (yyvsp[0].duration).cells_amount > 100000) {
                report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column,
                                    "a size in columns or rows is a whole number from 1, as `40 columns`");
                YYERROR;
            }
            AstRecordFieldList f = ast_record_field_list_empty();
            f = ast_record_field_list_append(f, copy_const((yyvsp[0].duration).cells == 1 ? "columns" : "rows"),
                                             expr_at(ast_number((yyvsp[0].duration).cells_amount, (long long)(yyvsp[0].duration).cells_amount, 1, 0),
                                                     (yylsp[0]).first_line, (yylsp[0]).first_column));
            (yyval.expr) = expr_at(ast_record(f), (yylsp[0]).first_line, (yylsp[0]).first_column);
        } else {
            (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column);
        }
      }
#line 6138 "src/parser.tab.c"
    break;

  case 274: /* primary: STRING  */
#line 2042 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6144 "src/parser.tab.c"
    break;

  case 275: /* primary: variable_name ident_suffix  */
#line 2043 "src/parser.y"
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
#line 6166 "src/parser.tab.c"
    break;

  case 276: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 2060 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 6177 "src/parser.tab.c"
    break;

  case 277: /* primary: ERROR_VALUE  */
#line 2066 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6183 "src/parser.tab.c"
    break;

  case 278: /* primary: TRUE  */
#line 2067 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6189 "src/parser.tab.c"
    break;

  case 279: /* primary: FALSE  */
#line 2068 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6195 "src/parser.tab.c"
    break;

  case 280: /* primary: NOTHING  */
#line 2069 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6201 "src/parser.tab.c"
    break;

  case 281: /* primary: UNKNOWN_VALUE  */
#line 2070 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6207 "src/parser.tab.c"
    break;

  case 282: /* primary: LPAREN expression RPAREN  */
#line 2071 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); (yyval.expr)->parenthesized = 1; }
#line 6213 "src/parser.tab.c"
    break;

  case 283: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 2072 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 6219 "src/parser.tab.c"
    break;

  case 284: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 2073 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 6225 "src/parser.tab.c"
    break;

  case 285: /* primary: record_literal  */
#line 2074 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 6231 "src/parser.tab.c"
    break;

  case 286: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 2078 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 6237 "src/parser.tab.c"
    break;

  case 287: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 2079 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 6243 "src/parser.tab.c"
    break;

  case 288: /* ident_suffix: %empty  */
#line 2083 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 6253 "src/parser.tab.c"
    break;

  case 289: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 2088 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 6263 "src/parser.tab.c"
    break;

  case 290: /* ident_suffix: DOT dot_field_name ident_dot_suffix  */
#line 2093 "src/parser.y"
                                          {
        /* dot_field_name, not IDENT: a keyword is a legal FIELD name after a
         * dot, because nothing but a name can appear there. */
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 6274 "src/parser.tab.c"
    break;

  case 291: /* ident_suffix: DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 2099 "src/parser.y"
                                                          {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        (yyval.ident_suffix).kind = IDENT_SUFFIX_METHOD;
        (yyval.ident_suffix).name = (yyvsp[-3].text);
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 6288 "src/parser.tab.c"
    break;

  case 292: /* ident_dot_suffix: %empty  */
#line 2111 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 6298 "src/parser.tab.c"
    break;

  case 293: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 2116 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 6308 "src/parser.tab.c"
    break;

  case 294: /* duration_terms: NUMBER IDENT  */
#line 2124 "src/parser.y"
                   {
        AstDuration duration = {0};
        char *bad = NULL;
        (yyval.duration) = duration_add_unit(duration, (yyvsp[-1].numlit).value, (yyvsp[0].text), &bad);
        if (bad) {
            duration_unit_error(ctx, bad, (yylsp[0]).first_line, (yylsp[0]).first_column,
                                (yylsp[0]).last_line, (yylsp[0]).last_column);
            YYERROR;
        }
      }
#line 6323 "src/parser.tab.c"
    break;

  case 295: /* duration_terms: duration_terms NUMBER IDENT  */
#line 2134 "src/parser.y"
                                  {
        char *bad = NULL;
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].numlit).value, (yyvsp[0].text), &bad);
        if (bad) {
            duration_unit_error(ctx, bad, (yylsp[0]).first_line, (yylsp[0]).first_column,
                                (yylsp[0]).last_line, (yylsp[0]).last_column);
            YYERROR;
        }
      }
#line 6337 "src/parser.tab.c"
    break;

  case 296: /* argument_list_opt: %empty  */
#line 2146 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 6343 "src/parser.tab.c"
    break;

  case 297: /* argument_list_opt: argument_list  */
#line 2147 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 6349 "src/parser.tab.c"
    break;

  case 298: /* argument_list: expression  */
#line 2151 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 6355 "src/parser.tab.c"
    break;

  case 299: /* argument_list: argument_list COMMA expression  */
#line 2152 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 6361 "src/parser.tab.c"
    break;

  case 300: /* array_argument_list: expression  */
#line 2156 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 6367 "src/parser.tab.c"
    break;

  case 301: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 2157 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 6373 "src/parser.tab.c"
    break;

  case 302: /* parameter_list_opt: %empty  */
#line 2161 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 6379 "src/parser.tab.c"
    break;

  case 303: /* parameter_list_opt: parameter_list  */
#line 2162 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 6385 "src/parser.tab.c"
    break;

  case 304: /* parameter_default: NUMBER  */
#line 2176 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact, (yyvsp[0].numlit).scale), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6391 "src/parser.tab.c"
    break;

  case 305: /* parameter_default: MINUS NUMBER  */
#line 2180 "src/parser.y"
                   { (yyval.expr) = expr_at(ast_number(-(yyvsp[0].numlit).value,
                                             (yyvsp[0].numlit).exact == LLONG_MIN ? 0 : -(yyvsp[0].numlit).exact,
                                             (yyvsp[0].numlit).exact == LLONG_MIN ? 0 : (yyvsp[0].numlit).is_exact,
                                             (yyvsp[0].numlit).exact == LLONG_MIN ? 0 : (yyvsp[0].numlit).scale),
                                  (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6401 "src/parser.tab.c"
    break;

  case 306: /* parameter_default: PLUS NUMBER  */
#line 2185 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact, (yyvsp[0].numlit).scale), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6407 "src/parser.tab.c"
    break;

  case 307: /* parameter_default: STRING  */
#line 2186 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6413 "src/parser.tab.c"
    break;

  case 308: /* parameter_default: TRUE  */
#line 2187 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6419 "src/parser.tab.c"
    break;

  case 309: /* parameter_default: FALSE  */
#line 2188 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6425 "src/parser.tab.c"
    break;

  case 310: /* parameter_default: NOTHING  */
#line 2189 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6431 "src/parser.tab.c"
    break;

  case 311: /* parameter_default: UNKNOWN_VALUE  */
#line 2190 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6437 "src/parser.tab.c"
    break;

  case 312: /* parameter_list: IDENT  */
#line 2194 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 6443 "src/parser.tab.c"
    break;

  case 313: /* parameter_list: IDENT OP_EQ parameter_default  */
#line 2195 "src/parser.y"
                                    {
        (yyval.name_list) = ast_name_list_append_default(ast_name_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 6451 "src/parser.tab.c"
    break;

  case 314: /* parameter_list: parameter_list COMMA IDENT  */
#line 2198 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 6457 "src/parser.tab.c"
    break;

  case 315: /* parameter_list: IDENT COLON type_lock  */
#line 2202 "src/parser.y"
                            {
        if (!type_lock_check(ctx, (yyvsp[-2].text), (yyvsp[0].type_lock).name, (yyvsp[0].type_lock).arg, (yylsp[0]).first_line, (yylsp[0]).first_column,
                             (yylsp[0]).last_line, (yylsp[0]).last_column)) {
            YYERROR;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append(ast_name_list_empty(), (yyvsp[-2].text)), (yyvsp[0].type_lock));
      }
#line 6469 "src/parser.tab.c"
    break;

  case 316: /* parameter_list: IDENT COLON type_lock OP_EQ parameter_default  */
#line 2209 "src/parser.y"
                                                    {
        if (!type_lock_check(ctx, (yyvsp[-4].text), (yyvsp[-2].type_lock).name, (yyvsp[-2].type_lock).arg, (yylsp[-2]).first_line, (yylsp[-2]).first_column,
                             (yylsp[-2]).last_line, (yylsp[-2]).last_column)) {
            YYERROR;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append_default(ast_name_list_empty(), (yyvsp[-4].text), (yyvsp[0].expr)), (yyvsp[-2].type_lock));
      }
#line 6481 "src/parser.tab.c"
    break;

  case 317: /* parameter_list: parameter_list COMMA IDENT COLON type_lock  */
#line 2216 "src/parser.y"
                                                 {
        if (!type_lock_check(ctx, (yyvsp[-2].text), (yyvsp[0].type_lock).name, (yyvsp[0].type_lock).arg, (yylsp[0]).first_line, (yylsp[0]).first_column,
                             (yylsp[0]).last_line, (yylsp[0]).last_column)) {
            YYERROR;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append((yyvsp[-4].name_list), (yyvsp[-2].text)), (yyvsp[0].type_lock));
      }
#line 6493 "src/parser.tab.c"
    break;

  case 318: /* parameter_list: parameter_list COMMA IDENT COLON type_lock OP_EQ parameter_default  */
#line 2223 "src/parser.y"
                                                                         {
        if (!type_lock_check(ctx, (yyvsp[-4].text), (yyvsp[-2].type_lock).name, (yyvsp[-2].type_lock).arg, (yylsp[-2]).first_line, (yylsp[-2]).first_column,
                             (yylsp[-2]).last_line, (yylsp[-2]).last_column)) {
            YYERROR;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append_default((yyvsp[-6].name_list), (yyvsp[-4].text), (yyvsp[0].expr)), (yyvsp[-2].type_lock));
      }
#line 6505 "src/parser.tab.c"
    break;

  case 319: /* parameter_list: parameter_list COMMA IDENT OP_EQ parameter_default  */
#line 2230 "src/parser.y"
                                                         {
        (yyval.name_list) = ast_name_list_append_default((yyvsp[-4].name_list), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 6513 "src/parser.tab.c"
    break;

  case 320: /* field_name: dot_field_name  */
#line 2245 "src/parser.y"
                     { (yyval.text) = (yyvsp[0].text); }
#line 6519 "src/parser.tab.c"
    break;

  case 321: /* field_name: STRING  */
#line 2252 "src/parser.y"
             { (yyval.text) = (yyvsp[0].text); }
#line 6525 "src/parser.tab.c"
    break;

  case 322: /* projection_names: dot_field_name  */
#line 2261 "src/parser.y"
                     { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 6531 "src/parser.tab.c"
    break;

  case 323: /* projection_names: projection_names COMMA dot_field_name  */
#line 2262 "src/parser.y"
                                            { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 6537 "src/parser.tab.c"
    break;

  case 324: /* dot_field_name: IDENT  */
#line 2266 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 6543 "src/parser.tab.c"
    break;

  case 325: /* dot_field_name: AS  */
#line 2267 "src/parser.y"
                     { (yyval.text) = kw_name("as"); }
#line 6549 "src/parser.tab.c"
    break;

  case 326: /* dot_field_name: NEXT  */
#line 2268 "src/parser.y"
                     { (yyval.text) = kw_name("next"); }
#line 6555 "src/parser.tab.c"
    break;

  case 327: /* dot_field_name: STOP  */
#line 2269 "src/parser.y"
                     { (yyval.text) = kw_name("stop"); }
#line 6561 "src/parser.tab.c"
    break;

  case 328: /* dot_field_name: ERROR_VALUE  */
#line 2270 "src/parser.y"
                     { (yyval.text) = kw_name("error"); }
#line 6567 "src/parser.tab.c"
    break;

  case 329: /* dot_field_name: END  */
#line 2271 "src/parser.y"
                     { (yyval.text) = kw_name("end"); }
#line 6573 "src/parser.tab.c"
    break;

  case 330: /* dot_field_name: TO  */
#line 2272 "src/parser.y"
                     { (yyval.text) = kw_name("to"); }
#line 6579 "src/parser.tab.c"
    break;

  case 331: /* dot_field_name: IN  */
#line 2273 "src/parser.y"
                     { (yyval.text) = kw_name("in"); }
#line 6585 "src/parser.tab.c"
    break;

  case 332: /* dot_field_name: ON  */
#line 2274 "src/parser.y"
                     { (yyval.text) = kw_name("on"); }
#line 6591 "src/parser.tab.c"
    break;

  case 333: /* dot_field_name: NEW  */
#line 2275 "src/parser.y"
                     { (yyval.text) = kw_name("new"); }
#line 6597 "src/parser.tab.c"
    break;

  case 334: /* dot_field_name: EACH  */
#line 2276 "src/parser.y"
                     { (yyval.text) = kw_name("each"); }
#line 6603 "src/parser.tab.c"
    break;

  case 335: /* dot_field_name: WITH  */
#line 2277 "src/parser.y"
                     { (yyval.text) = kw_name("with"); }
#line 6609 "src/parser.tab.c"
    break;

  case 336: /* dot_field_name: WITHOUT  */
#line 2278 "src/parser.y"
                     { (yyval.text) = kw_name("without"); }
#line 6615 "src/parser.tab.c"
    break;

  case 337: /* dot_field_name: THEN  */
#line 2279 "src/parser.y"
                     { (yyval.text) = kw_name("then"); }
#line 6621 "src/parser.tab.c"
    break;

  case 338: /* dot_field_name: ELSE  */
#line 2280 "src/parser.y"
                     { (yyval.text) = kw_name("else"); }
#line 6627 "src/parser.tab.c"
    break;

  case 339: /* dot_field_name: FOR  */
#line 2281 "src/parser.y"
                     { (yyval.text) = kw_name("for"); }
#line 6633 "src/parser.tab.c"
    break;

  case 340: /* dot_field_name: IF  */
#line 2282 "src/parser.y"
                     { (yyval.text) = kw_name("if"); }
#line 6639 "src/parser.tab.c"
    break;

  case 341: /* dot_field_name: WHILE  */
#line 2283 "src/parser.y"
                     { (yyval.text) = kw_name("while"); }
#line 6645 "src/parser.tab.c"
    break;

  case 342: /* dot_field_name: DO  */
#line 2284 "src/parser.y"
                     { (yyval.text) = kw_name("do"); }
#line 6651 "src/parser.tab.c"
    break;

  case 343: /* dot_field_name: UNTIL  */
#line 2285 "src/parser.y"
                     { (yyval.text) = kw_name("until"); }
#line 6657 "src/parser.tab.c"
    break;

  case 344: /* dot_field_name: PRINT  */
#line 2286 "src/parser.y"
                     { (yyval.text) = kw_name("print"); }
#line 6663 "src/parser.tab.c"
    break;

  case 345: /* dot_field_name: RETURN  */
#line 2287 "src/parser.y"
                     { (yyval.text) = kw_name("return"); }
#line 6669 "src/parser.tab.c"
    break;

  case 346: /* dot_field_name: LOAD  */
#line 2288 "src/parser.y"
                     { (yyval.text) = kw_name("load"); }
#line 6675 "src/parser.tab.c"
    break;

  case 347: /* dot_field_name: USE  */
#line 2289 "src/parser.y"
                     { (yyval.text) = kw_name("use"); }
#line 6681 "src/parser.tab.c"
    break;

  case 348: /* dot_field_name: NOT  */
#line 2290 "src/parser.y"
                     { (yyval.text) = kw_name("not"); }
#line 6687 "src/parser.tab.c"
    break;

  case 349: /* dot_field_name: TRY  */
#line 2291 "src/parser.y"
                     { (yyval.text) = kw_name("try"); }
#line 6693 "src/parser.tab.c"
    break;

  case 350: /* dot_field_name: AND  */
#line 2292 "src/parser.y"
                     { (yyval.text) = kw_name("and"); }
#line 6699 "src/parser.tab.c"
    break;

  case 351: /* dot_field_name: OR  */
#line 2293 "src/parser.y"
                     { (yyval.text) = kw_name("or"); }
#line 6705 "src/parser.tab.c"
    break;

  case 352: /* dot_field_name: TRUE  */
#line 2294 "src/parser.y"
                     { (yyval.text) = kw_name("true"); }
#line 6711 "src/parser.tab.c"
    break;

  case 353: /* dot_field_name: FALSE  */
#line 2295 "src/parser.y"
                     { (yyval.text) = kw_name("false"); }
#line 6717 "src/parser.tab.c"
    break;

  case 354: /* dot_field_name: NOTHING  */
#line 2296 "src/parser.y"
                     { (yyval.text) = kw_name("nothing"); }
#line 6723 "src/parser.tab.c"
    break;

  case 355: /* dot_field_name: BREAK  */
#line 2297 "src/parser.y"
                     { (yyval.text) = kw_name("break"); }
#line 6729 "src/parser.tab.c"
    break;

  case 356: /* dot_field_name: CONTINUE  */
#line 2298 "src/parser.y"
                     { (yyval.text) = kw_name("continue"); }
#line 6735 "src/parser.tab.c"
    break;

  case 357: /* dot_field_name: GOTO  */
#line 2299 "src/parser.y"
                     { (yyval.text) = kw_name("goto"); }
#line 6741 "src/parser.tab.c"
    break;

  case 358: /* dot_field_name: GOSUB  */
#line 2300 "src/parser.y"
                     { (yyval.text) = kw_name("gosub"); }
#line 6747 "src/parser.tab.c"
    break;

  case 359: /* dot_field_name: SPAWN  */
#line 2301 "src/parser.y"
                     { (yyval.text) = kw_name("spawn"); }
#line 6753 "src/parser.tab.c"
    break;

  case 360: /* dot_field_name: EXPORT  */
#line 2302 "src/parser.y"
                     { (yyval.text) = kw_name("export"); }
#line 6759 "src/parser.tab.c"
    break;

  case 361: /* dot_field_name: LIBRARY  */
#line 2303 "src/parser.y"
                     { (yyval.text) = kw_name("library"); }
#line 6765 "src/parser.tab.c"
    break;

  case 362: /* dot_field_name: FUNCTION  */
#line 2304 "src/parser.y"
                     { (yyval.text) = kw_name("function"); }
#line 6771 "src/parser.tab.c"
    break;

  case 363: /* dot_field_name: MODIFIER  */
#line 2305 "src/parser.y"
                     { (yyval.text) = kw_name("modifier"); }
#line 6777 "src/parser.tab.c"
    break;

  case 364: /* dot_field_name: PROGRAM  */
#line 2306 "src/parser.y"
                     { (yyval.text) = kw_name("program"); }
#line 6783 "src/parser.tab.c"
    break;

  case 365: /* dot_field_name: WATCH  */
#line 2307 "src/parser.y"
                     { (yyval.text) = kw_name("watch"); }
#line 6789 "src/parser.tab.c"
    break;

  case 366: /* dot_field_name: WATCHERS  */
#line 2308 "src/parser.y"
                     { (yyval.text) = kw_name("watchers"); }
#line 6795 "src/parser.tab.c"
    break;

  case 367: /* dot_field_name: CONSIDER  */
#line 2309 "src/parser.y"
                     { (yyval.text) = kw_name("consider"); }
#line 6801 "src/parser.tab.c"
    break;

  case 368: /* dot_field_name: STEP  */
#line 2310 "src/parser.y"
                     { (yyval.text) = kw_name("step"); }
#line 6807 "src/parser.tab.c"
    break;

  case 369: /* dot_field_name: UNWATCH  */
#line 2311 "src/parser.y"
                     { (yyval.text) = kw_name("unwatch"); }
#line 6813 "src/parser.tab.c"
    break;

  case 370: /* dot_field_name: UNKNOWN_VALUE  */
#line 2312 "src/parser.y"
                     { (yyval.text) = kw_name("unknown"); }
#line 6819 "src/parser.tab.c"
    break;

  case 371: /* dot_field_name: DIM  */
#line 2313 "src/parser.y"
                     { (yyval.text) = kw_name("dim"); }
#line 6825 "src/parser.tab.c"
    break;

  case 372: /* record_field_list: field_name OP_EQ expression  */
#line 2317 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6831 "src/parser.tab.c"
    break;

  case 373: /* record_field_list: field_name COLON expression  */
#line 2318 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6837 "src/parser.tab.c"
    break;

  case 374: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 2319 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 6843 "src/parser.tab.c"
    break;

  case 375: /* record_field_list: record_field_list COMMA optional_newlines field_name OP_EQ expression  */
#line 2320 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6849 "src/parser.tab.c"
    break;

  case 376: /* record_field_list: record_field_list COMMA optional_newlines field_name COLON expression  */
#line 2321 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6855 "src/parser.tab.c"
    break;

  case 377: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 2322 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 6861 "src/parser.tab.c"
    break;

  case 378: /* field_policy: IDENT  */
#line 2330 "src/parser.y"
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
#line 6893 "src/parser.tab.c"
    break;

  case 379: /* field_policy: IDENT expression  */
#line 2357 "src/parser.y"
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
#line 6914 "src/parser.tab.c"
    break;


#line 6918 "src/parser.tab.c"

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

#line 2380 "src/parser.y"


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
    /* EVERY FIELD IS SET BY HAND HERE, with no memset, so a new one that is not
     * listed carries whatever was on the stack. Measured the hard way: the
     * window depth below was read as garbage, which made `type: {USD}` refuse
     * INSIDE a declaration and `m = {USD}` succeed outside one -- wrong in both
     * directions at once, which is the signature of an uninitialised read
     * rather than of inverted logic. */
    ctx.window_depth = 0;
    ctx.synth_parens = 0;
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

/* THE CLASSIC-BASIC STATEMENT WORDS THAT ARE NOT RESERVED, AND MUST NOT BE.
 *
 * `dim` has had a sentence since the beginning; `let` and `rem` did not, which
 * is what makes it an inconsistency rather than a policy -- the same shape as
 * nine module dispatchers each holding their own copy of one format. Measured
 * 2026-10-04 by sweeping what a QBasic reader types: `MOD`, `&` and `dim` each
 * name a remedy, while `<>`, `let` and `rem` gave a bare syntax error.
 *
 * AND THE TWO HERE WERE WORSE THAN TERSE, THEY MISDIRECTED. `let x = 1` and
 * `rem a note` parse as the beginning of a CALL, so bison reported `expecting
 * LPAREN` -- telling a beginner to add a parenthesis, which is the one change
 * that cannot help. Reports-the-wrong-cause, in a beginner's path, for the word
 * every BASIC book opens with.
 *
 * A MESSAGE, NOT A KEYWORD, which is the rule `sub` already set above: `let`
 * and `rem` are ordinary identifiers and reserving them would break any program
 * that uses one as a name. So the SOURCE is asked, read-only and after the
 * parse is already over, exactly as the statement-initial reserved-word rule
 * does one function up -- here by walking back to the start of the error's own
 * line, since `let` sits two tokens behind the `=` the parser tripped on and
 * the ctx carries only one.
 *
 * `(` IS THE DISCRIMINATOR: a program may legitimately define `function
 * let(x)`, and a syntax error INSIDE such a call must not be answered with
 * advice about a statement the author did not write. */
static const char *syntax_error_basic_word(gb_parse_ctx *ctx) {
    if (!ctx->tok_after || !ctx->active_lexer || !ctx->active_lexer->source) {
        return NULL;
    }
    const char *base = ctx->active_lexer->source;
    const char *p = ctx->tok_after;
    if (p < base) {
        return NULL;
    }
    /* an error AT the newline that ends a line (`endif` alone) is that line's,
       not the next one's */
    if (p > base && p[-1] == '\n') {
        p--;
    }
    while (p > base && p[-1] != '\n') {
        p--;
    }
    /* VB'S AND QBASIC'S LINE CONTINUATION (DOGFOOD 2026-10-15): a `_` alone at
       the end of the line before, which bison reported as the next line's
       first token being unexpected. `_` is a legal name and `my_` an ordinary
       one, so only a `_` standing alone counts. */
    if (p > base) {
        const char *q = p - 1;          /* the line feed */
        while (q > base && (q[-1] == ' ' || q[-1] == '\t' || q[-1] == '\r')) {
            q--;
        }
        if (q > base && q[-1] == '_' &&
            (q - 1 == base || !(isalnum((unsigned char)q[-2]) || q[-2] == '_'))) {
            return "a `_` ending the line before is VB's line continuation, which gBASIC does not have: inside "
                   "( [ { a line continues by itself, so delete the `_`; outside them, put the expression in "
                   "parentheses";
        }
    }
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    const char *w = p;
    /* A BUILT-IN FUNCTION WRITTEN AS A STATEMENT, `sleep 0.1` (DOGFOOD
       2026-11-18): QBasic's SLEEP takes its argument bare, and bison's
       `unexpected NUMBER` names neither the word nor the parentheses. A name
       followed by `(`, `=`, `.`, `[` or a line's end is something else. */
    {
        const char *q = p;
        while ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9') || *q == '_') {
            q++;
        }
        size_t fn = (size_t)(q - p);
        const char *after = q;
        while (*after == ' ' || *after == '\t') {
            after++;
        }
        static _Thread_local char call_advice[200];
        /* not `exit`, whose classic `exit for` is answered below */
        int classic_exit = fn == 4 && tolower((unsigned char)p[0]) == 'e' && tolower((unsigned char)p[1]) == 'x' &&
                           tolower((unsigned char)p[2]) == 'i' && tolower((unsigned char)p[3]) == 't';
        if (!classic_exit && fn > 0 && fn < 40 && after > q && *after != '(' && *after != '=' && *after != '.' &&
            *after != '[' && *after != '\n' && *after != '\r' && *after != '\0' && *after != '\'') {
            char name[40];
            memcpy(name, p, fn);
            name[fn] = '\0';
            if (gbasic_builtin_function(name)) {
                snprintf(call_advice, sizeof call_advice,
                         "`%s` is a function, so its arguments go in parentheses: write `%s(...)`", name, name);
                return call_advice;
            }
        }
    }
    while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) {
        p++;
    }
    size_t n = (size_t)(p - w);
    if (n < 3 || n > 6) {
        return NULL;
    }
    char word[8];
    for (size_t i = 0; i < n; i++) {
        char c = w[i];
        word[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    word[n] = '\0';
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    /* `try` ALONE ON A LINE is another language's block (2026-11-07): gBASIC's
       `try` is an expression, and the reader needs to be told so, not that a
       newline was unexpected */
    if (strcmp(word, "try") == 0 && (*p == '\n' || *p == '\r' || *p == '\0' || *p == ':' || *p == '{')) {
        return "`try` is an expression, not a block: write `x = try f(a)` (the value, or nothing if it raised; "
               "`if error then` reads what it caught), or put several statements under `on error goto next`";
    }
    if (strcmp(word, "catch") == 0) {
        return "`catch` is not gBASIC; there is no try/catch block: write `x = try f(a)` and then `if error then`, "
               "or catch a whole function with `on error goto label`";
    }
    if (*p == '(') {
        return NULL;       /* a call, not a classic-BASIC statement */
    }
    /* QBasic's and VB's loop exits (DOGFOOD 2026-11-18): `exit for` said
       `unexpected FOR`, which names the half that is right */
    if (strcmp(word, "exit") == 0) {
        if (strncmp(p, "function", 8) == 0 || strncmp(p, "sub", 3) == 0) {
            return "`exit function` is not gBASIC; leave a function with `return`";
        }
        static _Thread_local char exit_advice[220];
        const char *nx = p;
        while ((*nx >= 'a' && *nx <= 'z') || (*nx >= 'A' && *nx <= 'Z')) {
            nx++;
        }
        snprintf(exit_advice, sizeof exit_advice,
                 "`exit %.*s` is not gBASIC; leave a loop with `break` (a `for` by its variable, `break i`)",
                 (int)(nx - p), p);
        return exit_advice;
    }
    if (strcmp(word, "let") == 0) {
        return "`let` is not a gBASIC statement; assign directly (x = 1)";
    }
    if (strcmp(word, "rem") == 0) {
        return "`rem` is not a gBASIC comment; a comment starts with ' and runs to the end of the line";
    }
    /* VB's, Ada's and Python's spellings of a chain (DOGFOOD 2026-11-02): the
       same misdirection, `expecting LPAREN`, for the word every one of those
       readers types */
    if (strcmp(word, "elseif") == 0) {
        return "`elseif` is not gBASIC; write `else if`, two words, and one `end if` closes the whole chain";
    }
    if (strcmp(word, "elsif") == 0) {
        return "`elsif` is not gBASIC; write `else if`, two words, and one `end if` closes the whole chain";
    }
    if (strcmp(word, "elif") == 0) {
        return "`elif` is not gBASIC; write `else if`, two words, and one `end if` closes the whole chain";
    }
    if (strcmp(word, "endif") == 0) {
        return "`endif` is not gBASIC; an if ends with `end if`, two words";
    }
    return NULL;
}

/* BISON NAMES ITS TOKENS, AND A READER KNOWS THEM BY WHAT THEY WROTE
 * (Matthew, 2026-11-20, beside `syntax error` for "lexer error"):
 * `unexpected RPAREN, expecting IDENT` is the grammar's vocabulary, so each
 * name is spelled as it is typed -- `unexpected `)`, expecting name`. A
 * keyword is its own word, asked of the lexer's classifier so the list lives
 * in one place; the rest are this table. A token missing from both prints
 * its bison name, and run_qbasic_diagnostics' `spelled` tier fails on one.
 *
 * ONLY BISON'S OWN SENTENCE: a message that starts "syntax error, ", and only
 * up to the first ` -- `, past which is advice written in words already.
 * Runs AFTER the reserved-word note, which asks the bison text for "IDENT". */
static const struct { const char *name; const char *shown; } bison_spelled[] = {
    { "IDENT", "name" }, { "QUALIFIED_IDENT", "library name (`lib.name`)" },
    { "STRING", "string" }, { "NUMBER", "number" },
    { "NEWLINE", "end of line" }, { "SQL_TEXT", "SQL" },
    { "LENS_CONTENT", "lens" }, { "MODIFIER_PREFIX", "modifier (`{...}`)" },
    { "MODIFIER_VALUE", "modifier (`{...}`)" },
    { "WINDOW_OPEN", "`window`" }, { "DATABASE_OPEN", "`database`" },
    { "SQL_OPEN", "`sql`" }, { "CONSIDER_IF", "`if`" },
    { "CONSIDER_ELSE", "`else`" }, { "END_CONSIDER", "`end consider`" },
    { "ERROR_VALUE", "`error`" }, { "UNKNOWN_VALUE", "`unknown`" },
    { "WAIT", "`wait`" },   /* a keyword only before `until`, so the lexer's list lacks it */
    { "PLUS_EQ", "`+=`" }, { "MINUS_EQ", "`-=`" }, { "STAR_EQ", "`*=`" },
    { "SLASH_EQ", "`/=`" },
    { "OP_EQ", "`=`" }, { "OP_NE", "`!=`" }, { "OP_GT", "`>`" }, { "OP_LT", "`<`" },
    { "OP_GE", "`>=`" }, { "OP_LE", "`<=`" }, { "OP_NGT", "`!>`" },
    { "OP_NLT", "`!<`" }, { "OP_NGE", "`!>=`" }, { "OP_NLE", "`!<=`" },
    { "PLUS", "`+`" }, { "MINUS", "`-`" }, { "STAR", "`*`" }, { "SLASH", "`/`" },
    { "LPAREN", "`(`" }, { "RPAREN", "`)`" }, { "LBRACKET", "`[`" },
    { "RBRACKET", "`]`" }, { "LBRACE", "`{`" }, { "RBRACE", "`}`" },
    { "PATH_LBRACE", "`{`" }, { "COMMA", "`,`" }, { "COLON", "`:`" },
    { "DOT", "`.`" },
};

static int spell_bison_tokens(const char *message, char *out, size_t size) {
    static const char lead[] = "syntax error, ";
    if (!message || strncmp(message, lead, sizeof lead - 1) != 0) {
        return 0;
    }
    const char *advice = strstr(message, " -- ");
    size_t used = 0;
    const char *p = message;
    while (*p && used + 1 < size) {
        if ((advice && p >= advice) || !isupper((unsigned char)*p) ||
            (p > message && (isalnum((unsigned char)p[-1]) || p[-1] == '_'))) {
            out[used++] = *p++;
            continue;
        }
        const char *e = p;
        while (isupper((unsigned char)*e) || *e == '_' || isdigit((unsigned char)*e)) {
            e++;
        }
        int len = (int)(e - p);
        const char *shown = NULL;
        char word[64];
        for (size_t i = 0; i < sizeof bison_spelled / sizeof bison_spelled[0]; i++) {
            if ((int)strlen(bison_spelled[i].name) == len &&
                strncmp(bison_spelled[i].name, p, len) == 0) {
                shown = bison_spelled[i].shown;
                break;
            }
        }
        if (!shown && len > 1 && len < (int)sizeof(word) - 2) {
            char lower[48];
            for (int i = 0; i < len; i++) {
                lower[i] = (char)tolower((unsigned char)p[i]);
            }
            lower[len] = '\0';
            if (lexer_identifier_type(lower, len) != TOKEN_IDENT) {
                snprintf(word, sizeof word, "`%s`", lower);
                shown = word;
            }
        }
        if (shown) {
            int n = snprintf(out + used, size - used, "%s", shown);
            if (n < 0 || (size_t)n >= size - used) {
                return 0;
            }
            used += (size_t)n;
        } else {
            size_t n = (size_t)len < size - used - 1 ? (size_t)len : size - used - 1;
            memcpy(out + used, p, n);
            used += n;
        }
        p = e;
    }
    if (*p) {
        return 0;
    }
    out[used] = '\0';
    return 1;
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
    /* A CLASSIC-BASIC STATEMENT WORD REPLACES the message rather than appending
     * to it, unlike the reserved-word note: bison's own sentence here is about
     * a parenthesis it wanted, which is the wrong advice entirely, so carrying
     * it alongside would leave the misdirection in place beside the fix. */
    const char *basic_word = syntax_error_basic_word(ctx);
    if (basic_word) {
        message = basic_word;
    }
    char reworded[512];
    const char *reserved = syntax_error_reserved_word(ctx, message);
    if (reserved) {
        snprintf(reworded, sizeof(reworded),
                 "%s -- '%s' is a reserved word and cannot be used as a name",
                 message, reserved);
        message = reworded;
    }
    char spelled[640];
    if (spell_bison_tokens(message, spelled, sizeof(spelled))) {
        message = spelled;
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
    if (ctx->synth_parens > 0) {
        /* the `(` then the `)` of a bare `stretch`, at the word's end */
        int which = ctx->synth_parens--;
        llocp->first_line = llocp->last_line = ctx->la_end_line;
        llocp->first_column = llocp->last_column = ctx->la_end_column;
        return which == 2 ? LPAREN : RPAREN;
    }
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
        /* a window item that is a bare `stretch` or `separator`, the only
         * two leaves that take neither a name nor options */
        if (ctx->window_depth > 0 && ctx->tok_prev_type == TOKEN_NEWLINE &&
            ((token.length == 7 && strncmp(token.start, "stretch", 7) == 0) ||
             (token.length == 9 && strncmp(token.start, "separator", 9) == 0))) {
            const char *after = token.start + token.length;
            while (*after == ' ' || *after == '\t') {
                after++;
            }
            if (*after == '\n' || *after == '\r' || *after == '\0' || *after == '\'') {
                ctx->synth_parens = 2;
            }
        }
        return IDENT;
    case TOKEN_QUALIFIED_IDENT:
        lvalp->text = copy_text(token.start, token.length);
        return QUALIFIED_IDENT;
    case TOKEN_AS: return AS;
    case TOKEN_NUMBER:
    {
        /* Convert exactly the token's bytes (handles decimal and 0x hex), so a
         * following character can never extend what strtod reads. */
        /* ALL OF IT: a literal longer than this buffer used to be cut to
         * its first 63 characters, so `1` and 69 zeros read as 1e62
         * (2026-11-15); a longer one is copied to the heap. */
        char small[64];
        size_t nlen = token.length > 0 ? (size_t)token.length : 0;
        char *numbuf = nlen < sizeof small ? small : malloc(nlen + 1);
        if (!numbuf) {
            abort();
        }
        memcpy(numbuf, token.start, nlen);
        numbuf[nlen] = '\0';
        lvalp->numlit.value = strtod(numbuf, NULL);
        /* EXACTNESS COMES FROM THE DIGITS. An integer literal the runtime can
         * hold exactly is EXACT; everything else is inexact, which is what every
         * number in this language was until now.
         *
         * DECIMAL AND HEX INTEGERS ONLY. A literal carrying a `.` or an exponent
         * is inexact BY DESIGN rather than by omission: `1e20` is integral and
         * exactly representable, and the notation still says "approximately this
         * magnitude" -- `number("100000000000000000000")` is how to ask for the
         * integer. Recorded as an open decision in the design and settled here.
         *
         * OUT OF `int64` RANGE IS INEXACT, not an error: the double is what the
         * language has always given for such a literal, and refusing would stop a
         * program that runs today. */
        lvalp->numlit.exact = 0;
        lvalp->numlit.is_exact = 0;
        lvalp->numlit.scale = 0;
        /* AN EXACT DECIMAL (docs/exact_decimal_design.md §2): a literal with a
         * point and no exponent is its digits, `6331.475` exactly, where it
         * was the nearest double. One that does not fit an int64 at scale <= 18
         * stays the double, as an out-of-range integer does. `2.0` is the
         * exact DECIMAL 2, not the exact integer (see gb_decimal_from_text). */
        if (strchr(numbuf, '.') && !strpbrk(numbuf, "eExX")) {
            long long mantissa = 0;
            int scale = 0;
            if (gb_decimal_from_text(numbuf, &mantissa, &scale)) {
                lvalp->numlit.exact = mantissa;
                lvalp->numlit.scale = scale;
                lvalp->numlit.value = gb_decimal_to_double(mantissa, scale);
            }
            if (numbuf != small) {
                free(numbuf);
            }
            return NUMBER;
        }
        {
            const char *digits = numbuf;
            int base = 10;
            if (numbuf[0] == '0' && (numbuf[1] == 'x' || numbuf[1] == 'X')) {
                digits = numbuf + 2;
                base = 16;
            }
            int integral = digits[0] != '\0';
            for (const char *c = digits; *c; c++) {
                int ok = base == 16
                    ? ((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f') ||
                       (*c >= 'A' && *c <= 'F'))
                    : (*c >= '0' && *c <= '9');
                if (!ok) {
                    integral = 0;
                    break;
                }
            }
            if (integral) {
                errno = 0;
                char *end = NULL;
                long long parsed = strtoll(digits, &end, base);
                if (errno == 0 && end && *end == '\0') {
                    lvalp->numlit.exact = parsed;
                    lvalp->numlit.is_exact = 1;
                    /* The double is kept in sync as a LOSSY VIEW, which is what
                     * lets every existing reader of it go on being correct. */
                    lvalp->numlit.value = (double)parsed;
                }
            }
        }
        if (numbuf != small) {
            free(numbuf);
        }
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
    case TOKEN_TRY: return TRY;
    case TOKEN_EXCLUDING: return EXCLUDING;
    case TOKEN_INTERSECTING: return INTERSECTING;
    case TOKEN_WITH: return WITH;
    case TOKEN_NEW: return NEW;
    case TOKEN_SPAWN: return SPAWN;
    case TOKEN_FOR: return FOR;
    case TOKEN_TO: return TO;
    case TOKEN_STEP: return STEP;
    case TOKEN_DO: return DO;
    case TOKEN_UNTIL: return UNTIL;
    case TOKEN_WAIT: return WAIT;
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
    case TOKEN_MODIFIER_VALUE:
        lvalp->text = copy_text(token.start, token.length);
        return MODIFIER_VALUE;
    case TOKEN_WINDOW_OPEN:
        /* RAISED HERE RATHER THAN IN A MID-RULE ACTION, which measured 4
         * conflicts: a mid-rule action is an empty nonterminal, and three
         * alternatives sharing `WINDOW_OPEN <action>` make the parser choose
         * before it has seen enough. This token appears nowhere but a window
         * head, so raising the depth as it is produced has the same effect and
         * costs nothing. It is lowered at each block's reduction. */
        ctx->window_depth++;
        return WINDOW_OPEN;
    case TOKEN_DATABASE_OPEN: return DATABASE_OPEN;
    case TOKEN_SQL_OPEN: return SQL_OPEN;
    case TOKEN_SQL_TEXT:
        lvalp->text = copy_text(token.start, token.length);
        return SQL_TEXT;
    case TOKEN_LBRACE: return LBRACE;
    case TOKEN_PATH_LBRACE: return PATH_LBRACE;
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
