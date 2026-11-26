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
        return 0;       /* the closer is the refusing rule's, freed by GB_REFUSE */
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



#line 728 "src/parser.tab.c"

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
  YYSYMBOL_SHAPE_OPEN = 13,                /* SHAPE_OPEN  */
  YYSYMBOL_SQL_TEXT = 14,                  /* SQL_TEXT  */
  YYSYMBOL_AS = 15,                        /* AS  */
  YYSYMBOL_DIM = 16,                       /* DIM  */
  YYSYMBOL_PLUS_EQ = 17,                   /* PLUS_EQ  */
  YYSYMBOL_MINUS_EQ = 18,                  /* MINUS_EQ  */
  YYSYMBOL_STAR_EQ = 19,                   /* STAR_EQ  */
  YYSYMBOL_SLASH_EQ = 20,                  /* SLASH_EQ  */
  YYSYMBOL_EXCLUDING = 21,                 /* EXCLUDING  */
  YYSYMBOL_INTERSECTING = 22,              /* INTERSECTING  */
  YYSYMBOL_IF = 23,                        /* IF  */
  YYSYMBOL_CONSIDER_IF = 24,               /* CONSIDER_IF  */
  YYSYMBOL_THEN = 25,                      /* THEN  */
  YYSYMBOL_ELSE = 26,                      /* ELSE  */
  YYSYMBOL_CONSIDER_ELSE = 27,             /* CONSIDER_ELSE  */
  YYSYMBOL_END = 28,                       /* END  */
  YYSYMBOL_END_CONSIDER = 29,              /* END_CONSIDER  */
  YYSYMBOL_PRINT = 30,                     /* PRINT  */
  YYSYMBOL_TRUE = 31,                      /* TRUE  */
  YYSYMBOL_FALSE = 32,                     /* FALSE  */
  YYSYMBOL_NOTHING = 33,                   /* NOTHING  */
  YYSYMBOL_UNKNOWN_VALUE = 34,             /* UNKNOWN_VALUE  */
  YYSYMBOL_AND = 35,                       /* AND  */
  YYSYMBOL_OR = 36,                        /* OR  */
  YYSYMBOL_NOT = 37,                       /* NOT  */
  YYSYMBOL_TRY = 38,                       /* TRY  */
  YYSYMBOL_WITH = 39,                      /* WITH  */
  YYSYMBOL_NEW = 40,                       /* NEW  */
  YYSYMBOL_SPAWN = 41,                     /* SPAWN  */
  YYSYMBOL_FOR = 42,                       /* FOR  */
  YYSYMBOL_TO = 43,                        /* TO  */
  YYSYMBOL_STEP = 44,                      /* STEP  */
  YYSYMBOL_DO = 45,                        /* DO  */
  YYSYMBOL_UNTIL = 46,                     /* UNTIL  */
  YYSYMBOL_IN = 47,                        /* IN  */
  YYSYMBOL_EACH = 48,                      /* EACH  */
  YYSYMBOL_WHILE = 49,                     /* WHILE  */
  YYSYMBOL_CONSIDER = 50,                  /* CONSIDER  */
  YYSYMBOL_BREAK = 51,                     /* BREAK  */
  YYSYMBOL_CONTINUE = 52,                  /* CONTINUE  */
  YYSYMBOL_FUNCTION = 53,                  /* FUNCTION  */
  YYSYMBOL_RETURN = 54,                    /* RETURN  */
  YYSYMBOL_GOTO = 55,                      /* GOTO  */
  YYSYMBOL_GOSUB = 56,                     /* GOSUB  */
  YYSYMBOL_WATCH = 57,                     /* WATCH  */
  YYSYMBOL_UNWATCH = 58,                   /* UNWATCH  */
  YYSYMBOL_WAIT = 59,                      /* WAIT  */
  YYSYMBOL_WITHOUT = 60,                   /* WITHOUT  */
  YYSYMBOL_WATCHERS = 61,                  /* WATCHERS  */
  YYSYMBOL_ON = 62,                        /* ON  */
  YYSYMBOL_NEXT = 63,                      /* NEXT  */
  YYSYMBOL_STOP = 64,                      /* STOP  */
  YYSYMBOL_ERROR_VALUE = 65,               /* ERROR_VALUE  */
  YYSYMBOL_MODIFIER = 66,                  /* MODIFIER  */
  YYSYMBOL_PROGRAM = 67,                   /* PROGRAM  */
  YYSYMBOL_LIBRARY = 68,                   /* LIBRARY  */
  YYSYMBOL_LOAD = 69,                      /* LOAD  */
  YYSYMBOL_USE = 70,                       /* USE  */
  YYSYMBOL_EXPORT = 71,                    /* EXPORT  */
  YYSYMBOL_OP_EQ = 72,                     /* OP_EQ  */
  YYSYMBOL_OP_NE = 73,                     /* OP_NE  */
  YYSYMBOL_OP_GT = 74,                     /* OP_GT  */
  YYSYMBOL_OP_LT = 75,                     /* OP_LT  */
  YYSYMBOL_OP_GE = 76,                     /* OP_GE  */
  YYSYMBOL_OP_LE = 77,                     /* OP_LE  */
  YYSYMBOL_OP_NGT = 78,                    /* OP_NGT  */
  YYSYMBOL_OP_NLT = 79,                    /* OP_NLT  */
  YYSYMBOL_OP_NGE = 80,                    /* OP_NGE  */
  YYSYMBOL_OP_NLE = 81,                    /* OP_NLE  */
  YYSYMBOL_PLUS = 82,                      /* PLUS  */
  YYSYMBOL_MINUS = 83,                     /* MINUS  */
  YYSYMBOL_STAR = 84,                      /* STAR  */
  YYSYMBOL_SLASH = 85,                     /* SLASH  */
  YYSYMBOL_LPAREN = 86,                    /* LPAREN  */
  YYSYMBOL_RPAREN = 87,                    /* RPAREN  */
  YYSYMBOL_LBRACKET = 88,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 89,                  /* RBRACKET  */
  YYSYMBOL_LBRACE = 90,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 91,                    /* RBRACE  */
  YYSYMBOL_COMMA = 92,                     /* COMMA  */
  YYSYMBOL_COLON = 93,                     /* COLON  */
  YYSYMBOL_NEWLINE = 94,                   /* NEWLINE  */
  YYSYMBOL_PATH_LBRACE = 95,               /* PATH_LBRACE  */
  YYSYMBOL_IF_WITHOUT_ELSE = 96,           /* IF_WITHOUT_ELSE  */
  YYSYMBOL_NO_DOT = 97,                    /* NO_DOT  */
  YYSYMBOL_DOT = 98,                       /* DOT  */
  YYSYMBOL_YYACCEPT = 99,                  /* $accept  */
  YYSYMBOL_program = 100,                  /* program  */
  YYSYMBOL_statement_list = 101,           /* statement_list  */
  YYSYMBOL_statement = 102,                /* statement  */
  YYSYMBOL_assignment = 103,               /* assignment  */
  YYSYMBOL_type_lock = 104,                /* type_lock  */
  YYSYMBOL_compound_op = 105,              /* compound_op  */
  YYSYMBOL_lvalue = 106,                   /* lvalue  */
  YYSYMBOL_variable_name = 107,            /* variable_name  */
  YYSYMBOL_comparison_lens = 108,          /* comparison_lens  */
  YYSYMBOL_109_1 = 109,                    /* $@1  */
  YYSYMBOL_modifier_name = 110,            /* modifier_name  */
  YYSYMBOL_modifier_word = 111,            /* modifier_word  */
  YYSYMBOL_print_statement = 112,          /* print_statement  */
  YYSYMBOL_call_statement = 113,           /* call_statement  */
  YYSYMBOL_with_lock_statement = 114,      /* with_lock_statement  */
  YYSYMBOL_for_end = 115,                  /* for_end  */
  YYSYMBOL_for_each_statement = 116,       /* for_each_statement  */
  YYSYMBOL_do_loop_statement = 117,        /* do_loop_statement  */
  YYSYMBOL_while_statement = 118,          /* while_statement  */
  YYSYMBOL_consider_statement = 119,       /* consider_statement  */
  YYSYMBOL_consider_branch_list = 120,     /* consider_branch_list  */
  YYSYMBOL_consider_else_opt = 121,        /* consider_else_opt  */
  YYSYMBOL_consider_statement_list = 122,  /* consider_statement_list  */
  YYSYMBOL_consider_body_statement = 123,  /* consider_body_statement  */
  YYSYMBOL_function_statement = 124,       /* function_statement  */
  YYSYMBOL_modifier_statement = 125,       /* modifier_statement  */
  YYSYMBOL_program_statement = 126,        /* program_statement  */
  YYSYMBOL_library_statement = 127,        /* library_statement  */
  YYSYMBOL_use_statement = 128,            /* use_statement  */
  YYSYMBOL_modifier_signature = 129,       /* modifier_signature  */
  YYSYMBOL_modifier_context = 130,         /* modifier_context  */
  YYSYMBOL_watch_statement = 131,          /* watch_statement  */
  YYSYMBOL_unwatch_statement = 132,        /* unwatch_statement  */
  YYSYMBOL_wait_statement = 133,           /* wait_statement  */
  YYSYMBOL_watch_target_list = 134,        /* watch_target_list  */
  YYSYMBOL_server_statement = 135,         /* server_statement  */
  YYSYMBOL_136_2 = 136,                    /* @2  */
  YYSYMBOL_137_3 = 137,                    /* @3  */
  YYSYMBOL_shape_block = 138,              /* shape_block  */
  YYSYMBOL_shape_body = 139,               /* shape_body  */
  YYSYMBOL_shape_field = 140,              /* shape_field  */
  YYSYMBOL_gsql_block = 141,               /* gsql_block  */
  YYSYMBOL_window_block = 142,             /* window_block  */
  YYSYMBOL_window_item_list = 143,         /* window_item_list  */
  YYSYMBOL_window_item = 144,              /* window_item  */
  YYSYMBOL_server_item_list = 145,         /* server_item_list  */
  YYSYMBOL_server_item = 146,              /* server_item  */
  YYSYMBOL_server_string_list = 147,       /* server_string_list  */
  YYSYMBOL_watch_target_path = 148,        /* watch_target_path  */
  YYSYMBOL_without_watchers_statement = 149, /* without_watchers_statement  */
  YYSYMBOL_on_error_statement = 150,       /* on_error_statement  */
  YYSYMBOL_error_statement = 151,          /* error_statement  */
  YYSYMBOL_return_statement = 152,         /* return_statement  */
  YYSYMBOL_label_statement = 153,          /* label_statement  */
  YYSYMBOL_goto_statement = 154,           /* goto_statement  */
  YYSYMBOL_gosub_statement = 155,          /* gosub_statement  */
  YYSYMBOL_break_statement = 156,          /* break_statement  */
  YYSYMBOL_continue_statement = 157,       /* continue_statement  */
  YYSYMBOL_if_statement = 158,             /* if_statement  */
  YYSYMBOL_if_block_tail = 159,            /* if_block_tail  */
  YYSYMBOL_if_inline_tail = 160,           /* if_inline_tail  */
  YYSYMBOL_inline_statement = 161,         /* inline_statement  */
  YYSYMBOL_expression = 162,               /* expression  */
  YYSYMBOL_conditional_expression = 163,   /* conditional_expression  */
  YYSYMBOL_or_expression = 164,            /* or_expression  */
  YYSYMBOL_and_expression = 165,           /* and_expression  */
  YYSYMBOL_not_expression = 166,           /* not_expression  */
  YYSYMBOL_comparison_expression = 167,    /* comparison_expression  */
  YYSYMBOL_set_expression = 168,           /* set_expression  */
  YYSYMBOL_additive_expression = 169,      /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 170, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 171,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 172,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 173,      /* comparison_operator  */
  YYSYMBOL_primary = 174,                  /* primary  */
  YYSYMBOL_record_literal = 175,           /* record_literal  */
  YYSYMBOL_ident_suffix = 176,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 177,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 178,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 179,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 180,            /* argument_list  */
  YYSYMBOL_array_argument_list = 181,      /* array_argument_list  */
  YYSYMBOL_parameter_list_opt = 182,       /* parameter_list_opt  */
  YYSYMBOL_parameter_default = 183,        /* parameter_default  */
  YYSYMBOL_parameter_list = 184,           /* parameter_list  */
  YYSYMBOL_field_name = 185,               /* field_name  */
  YYSYMBOL_projection_names = 186,         /* projection_names  */
  YYSYMBOL_dot_field_name = 187,           /* dot_field_name  */
  YYSYMBOL_record_field_list = 188,        /* record_field_list  */
  YYSYMBOL_field_policy = 189,             /* field_policy  */
  YYSYMBOL_optional_newlines = 190         /* optional_newlines  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;



/* Unqualified %code blocks.  */
#line 750 "src/parser.y"

static int yylex(YYSTYPE *lvalp, YYLTYPE *llocp, gb_parse_ctx *ctx);
static void yyerror(YYLTYPE *llocp, gb_parse_ctx *ctx, const char *message);
static void report_syntax_error(gb_parse_ctx *ctx, int line, int column,
                                int end_line, int end_column, const char *message);

/* A RULE'S OWN REFUSAL, WITHOUT A LEAK (2026-11-26). Bison's YYERROR pops
 * the refusing rule's symbols WITHOUT their %destructor ("Do not reclaim the
 * symbols of the rule whose action triggered this YYERROR", yyerrorlab), so
 * every refusal leaked what it had read: `x:int = 5` lost the name, the type
 * and the expression, and gbasic-lsp parses on every keystroke. Setting
 * yylen to 0 leaves them on the stack, where error recovery pops each through
 * its destructor. So an action that has freed or moved a symbol sets it to
 * NULL before refusing, and every destructor takes NULL. */
#define GB_REFUSE do { yylen = 0; YYERROR; } while (0)

/* A SHAPE'S DEFAULT IS A LITERAL (docs/shapes_design.md §2.1): a number,
 * text, true/false, nothing, unknown, a duration, a signed number, and
 * `[ ]` and `{ }` of those. Because a shape is HOISTED, and the hoist runs
 * no user code (tests/run_pre_registration.sh's contract, which gBASIC
 * Studio's hoisting mirrors), a default that called a function would run it
 * before the program began. A value computed at each `new` is `reset`'s. */
static int shape_default_literal(const AstExpr *e) {
    if (!e) {
        return 1;
    }
    switch (e->kind) {
    case AST_EXPR_NUMBER: case AST_EXPR_STRING: case AST_EXPR_BOOL:
    case AST_EXPR_NULL: case AST_EXPR_UNKNOWN: case AST_EXPR_DURATION:
        return 1;
    case AST_EXPR_UNARY:
        return (strcmp(e->as.unary.op, "-") == 0 || strcmp(e->as.unary.op, "+") == 0) &&
               e->as.unary.expr->kind == AST_EXPR_NUMBER;
    case AST_EXPR_ARRAY:
        for (size_t i = 0; i < e->as.array.count; i++) {
            if (!shape_default_literal(e->as.array.items[i])) {
                return 0;
            }
        }
        return 1;
    case AST_EXPR_RECORD:
        for (size_t i = 0; i < e->as.record.count; i++) {
            if (e->as.record.items[i].policy != AST_FIELD_POLICY_COPY ||
                !shape_default_literal(e->as.record.items[i].value)) {
                return 0;
            }
        }
        return 1;
    default:
        return 0;
    }
}

static int shape_default_check(gb_parse_ctx *ctx, const char *field, const AstExpr *e,
                               int line, int column, int end_line, int end_column) {
    if (shape_default_literal(e)) {
        return 1;
    }
    char message[400];
    snprintf(message, sizeof message,
             "%s's default is not a literal: a shape's default is a number, text, true or "
             "false, nothing, a duration, or [ ] and { } of those, since a shape is declared "
             "before the program runs. For a value computed at each `new`, write "
             "%s (reset <expression>):<type>", field, field);
    report_syntax_error(ctx, line, column, end_line, end_column, message);
    return 0;
}

/* A FIX, MACHINE-APPLICABLE (2026-11-26, the second AI aid): an advice
 * function that knows the rewrite FOR CERTAIN offers it here, and
 * report_syntax_error puts it on the diagnostic, where --json-diagnostics
 * prints it as "fix". Only certain rewrites -- `a mod b` is offered only where
 * precedence cannot move under it -- since an agent applies a fix unread.
 *
 * Positions are counted from the lookahead token, whose first byte is known
 * to be at (la_line, la_column): the lines between are counted, the column is
 * the byte offset in its own line, as gb_span's units are. */
static int fix_position(gb_parse_ctx *ctx, const char *p, int *line, int *column) {
    if (!ctx->active_lexer || !ctx->active_lexer->source || !ctx->tok_start || ctx->la_line <= 0) {
        return 0;
    }
    const char *base = ctx->active_lexer->source;
    const char *anchor = ctx->tok_start;
    if (p < base) {
        return 0;
    }
    int l = ctx->la_line;
    if (p < anchor) {
        for (const char *q = p; q < anchor; q++) {
            if (*q == '\n') l--;
        }
    } else {
        for (const char *q = anchor; q < p; q++) {
            if (*q == '\0') return 0;
            if (*q == '\n') l++;
        }
    }
    const char *start = p;
    while (start > base && start[-1] != '\n') {
        start--;
    }
    *line = l;
    *column = (int)(p - start) + 1;
    return 1;
}

static void fix_offer_span(gb_parse_ctx *ctx, gb_span span, const char *text) {
    int n = snprintf(ctx->fix.text, sizeof ctx->fix.text, "%s", text);
    ctx->fix.set = n >= 0 && (size_t)n < sizeof ctx->fix.text;
    ctx->fix.span = span;
}

/* replace the source bytes [from, to) with text */
static void fix_offer(gb_parse_ctx *ctx, const char *from, const char *to, const char *text) {
    gb_span span;
    if (to < from || !fix_position(ctx, from, &span.start_line, &span.start_column) ||
        !fix_position(ctx, to, &span.end_line, &span.end_column)) {
        return;
    }
    fix_offer_span(ctx, span, text);
}

/* THE DATA TYPES A LOCK MAY NAME (docs/typed_variables_design.md): one word,
 * and `money` takes its currency. Checked HERE, at parse time, so `x:int` is
 * refused before anything runs -- and with the gBASIC spelling, because a model
 * writing gBASIC brings Python's and TypeScript's names with it. Returns 1 when
 * the lock is good; otherwise reports and returns 0. `who` is the variable or
 * parameter, for the suggested spelling. */
/* `shape_ok`: a shape's field, where a name that is not a data type names
 * another shape (docs/shapes_design.md §2.2) -- answered 2, checked when the
 * shape is declared. A foreign type's name (`int`) is still refused there. */
static int type_lock_check_as(gb_parse_ctx *ctx, const char *who, const char *name,
                              const char *arg, int line, int column, int end_line,
                              int end_column, int shape_ok) {
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
                if (!arg) {
                    fix_offer_span(ctx, (gb_span){ line, column, end_line, end_column }, near[i].ours);
                }
                report_syntax_error(ctx, line, column, end_line, end_column, message);
                return 0;
            }
        }
        size_t n = strlen(name);
        int collection = strcmp(name, "array") == 0 || strcmp(name, "record") == 0 ||
                         strcmp(name, "list") == 0 || strcmp(name, "dict") == 0 ||
                         strcmp(name, "map") == 0 || strcmp(name, "object") == 0;
        int currency = n == 3 && isupper((unsigned char)name[0]) && isupper((unsigned char)name[1]) &&
                       isupper((unsigned char)name[2]);
        if (shape_ok && !arg && !collection && !currency) {
            return 2;
        }
        if (currency && !arg) {
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

static int type_lock_check(gb_parse_ctx *ctx, const char *who, const char *name,
                           const char *arg, int line, int column, int end_line,
                           int end_column) {
    return type_lock_check_as(ctx, who, name, arg, line, column, end_line, end_column, 0);
}

#line 1177 "src/parser.tab.c"

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
#define YYLAST   3956

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  99
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  92
/* YYNRULES -- Number of rules.  */
#define YYNRULES  397
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  951

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   353


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
      95,    96,    97,    98
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,  1061,  1061,  1065,  1066,  1067,  1071,  1072,  1073,  1074,
    1075,  1076,  1077,  1078,  1079,  1080,  1081,  1082,  1083,  1084,
    1085,  1086,  1087,  1088,  1089,  1090,  1091,  1092,  1093,  1094,
    1095,  1096,  1097,  1098,  1099,  1100,  1106,  1117,  1122,  1123,
    1139,  1146,  1160,  1161,  1165,  1166,  1167,  1168,  1172,  1173,
    1177,  1185,  1189,  1190,  1191,  1202,  1202,  1210,  1214,  1215,
    1219,  1220,  1221,  1222,  1226,  1232,  1236,  1239,  1240,  1246,
    1251,  1260,  1273,  1302,  1303,  1304,  1308,  1312,  1328,  1333,
    1342,  1348,  1354,  1361,  1371,  1375,  1396,  1402,  1408,  1414,
    1417,  1423,  1424,  1428,  1429,  1430,  1434,  1435,  1436,  1437,
    1438,  1439,  1440,  1441,  1442,  1443,  1444,  1445,  1446,  1447,
    1448,  1449,  1450,  1451,  1452,  1453,  1454,  1455,  1456,  1457,
    1458,  1459,  1460,  1466,  1477,  1480,  1487,  1496,  1508,  1511,
    1517,  1523,  1529,  1530,  1531,  1532,  1533,  1534,  1550,  1566,
    1587,  1588,  1592,  1596,  1599,  1607,  1613,  1620,  1621,  1625,
    1626,  1646,  1645,  1653,  1652,  1676,  1689,  1690,  1691,  1692,
    1704,  1709,  1716,  1722,  1727,  1734,  1747,  1750,  1753,  1759,
    1774,  1778,  1788,  1796,  1797,  1798,  1803,  1808,  1812,  1817,
    1822,  1836,  1851,  1856,  1867,  1882,  1887,  1893,  1894,  1895,
    1899,  1902,  1905,  1908,  1911,  1917,  1918,  1922,  1923,  1927,
    1933,  1934,  1935,  1936,  1941,  1956,  1961,  1966,  1976,  1980,
    1981,  1985,  1992,  1996,  2005,  2006,  2010,  2011,  2015,  2019,
    2026,  2029,  2032,  2041,  2051,  2054,  2057,  2063,  2070,  2080,
    2081,  2082,  2083,  2084,  2085,  2086,  2087,  2088,  2089,  2090,
    2098,  2099,  2103,  2104,  2110,  2111,  2115,  2116,  2138,  2139,
    2143,  2144,  2145,  2151,  2152,  2153,  2157,  2158,  2159,  2163,
    2164,  2165,  2169,  2170,  2177,  2181,  2182,  2183,  2187,  2188,
    2192,  2200,  2205,  2208,  2213,  2227,  2228,  2229,  2230,  2231,
    2232,  2233,  2234,  2235,  2236,  2240,  2261,  2262,  2263,  2264,
    2288,  2289,  2306,  2312,  2313,  2314,  2315,  2316,  2317,  2318,
    2319,  2320,  2324,  2325,  2329,  2334,  2339,  2345,  2357,  2362,
    2370,  2381,  2394,  2395,  2399,  2400,  2404,  2405,  2409,  2410,
    2424,  2428,  2433,  2434,  2435,  2436,  2437,  2438,  2442,  2443,
    2446,  2450,  2457,  2464,  2471,  2478,  2493,  2500,  2509,  2510,
    2514,  2515,  2516,  2517,  2518,  2519,  2520,  2521,  2522,  2523,
    2524,  2525,  2526,  2527,  2528,  2529,  2530,  2531,  2532,  2533,
    2534,  2535,  2536,  2537,  2538,  2539,  2540,  2541,  2542,  2543,
    2544,  2545,  2546,  2547,  2548,  2549,  2550,  2551,  2552,  2553,
    2554,  2555,  2556,  2557,  2558,  2559,  2560,  2561,  2565,  2566,
    2567,  2568,  2569,  2570,  2578,  2605,  2624,  2625
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
  "MODIFIER_VALUE", "WINDOW_OPEN", "DATABASE_OPEN", "SQL_OPEN",
  "SHAPE_OPEN", "SQL_TEXT", "AS", "DIM", "PLUS_EQ", "MINUS_EQ", "STAR_EQ",
  "SLASH_EQ", "EXCLUDING", "INTERSECTING", "IF", "CONSIDER_IF", "THEN",
  "ELSE", "CONSIDER_ELSE", "END", "END_CONSIDER", "PRINT", "TRUE", "FALSE",
  "NOTHING", "UNKNOWN_VALUE", "AND", "OR", "NOT", "TRY", "WITH", "NEW",
  "SPAWN", "FOR", "TO", "STEP", "DO", "UNTIL", "IN", "EACH", "WHILE",
  "CONSIDER", "BREAK", "CONTINUE", "FUNCTION", "RETURN", "GOTO", "GOSUB",
  "WATCH", "UNWATCH", "WAIT", "WITHOUT", "WATCHERS", "ON", "NEXT", "STOP",
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
  "server_statement", "@2", "@3", "shape_block", "shape_body",
  "shape_field", "gsql_block", "window_block", "window_item_list",
  "window_item", "server_item_list", "server_item", "server_string_list",
  "watch_target_path", "without_watchers_statement", "on_error_statement",
  "error_statement", "return_statement", "label_statement",
  "goto_statement", "gosub_statement", "break_statement",
  "continue_statement", "if_statement", "if_block_tail", "if_inline_tail",
  "inline_statement", "expression", "conditional_expression",
  "or_expression", "and_expression", "not_expression",
  "comparison_expression", "set_expression", "additive_expression",
  "multiplicative_expression", "unary_expression", "postfix_expression",
  "comparison_operator", "primary", "record_literal", "ident_suffix",
  "ident_dot_suffix", "duration_terms", "argument_list_opt",
  "argument_list", "array_argument_list", "parameter_list_opt",
  "parameter_default", "parameter_list", "field_name", "projection_names",
  "dot_field_name", "record_field_list", "field_policy",
  "optional_newlines", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-630)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -630,    79,  1560,  -630,    17,    66,   163,   199,   224,   235,
    -630,   942,  -630,   834,   942,   255,    46,   116,   942,   942,
     262,   279,   275,   942,   221,   221,   294,   942,   324,   266,
      74,  -630,  1005,   217,   357,   373,   397,   455,   321,  -630,
    -630,   323,    55,   322,   335,   343,  -630,  -630,  -630,  -630,
    -630,  -630,  -630,  -630,  -630,   349,  -630,   361,   363,  -630,
    -630,  -630,  -630,  -630,   375,   383,   391,   393,   402,   403,
     404,   405,  -630,   367,   942,   942,   415,   416,   438,   409,
     501,  -630,  -630,   421,  1107,  -630,  -630,  -630,  -630,  -630,
    1044,   942,  1146,   505,   424,  -630,  1107,   942,  -630,  -630,
    3817,   177,   486,  -630,   100,   477,  -630,  -630,   596,   380,
     382,  -630,   315,  -630,  -630,   514,   453,  -630,  -630,   435,
     313,   518,  -630,   430,   431,  -630,  -630,   441,   445,  -630,
    -630,  -630,   447,   221,  -630,    71,   436,  -630,   942,   442,
     147,   206,  3885,  -630,  -630,  -630,  -630,  -630,   325,  -630,
     497,   458,   456,    83,  -630,   543,  -630,   217,  -630,  -630,
    -630,  -630,  -630,  -630,   942,   942,  -630,  3613,   942,   364,
     549,  -630,  -630,  -630,  -630,  -630,  -630,  -630,  -630,  -630,
    -630,  -630,  -630,  -630,  3205,  -630,   467,   464,   471,  3273,
    3341,   555,  -630,  -630,   942,  -630,  -630,  -630,   134,   474,
     481,  -630,   482,   736,  1215,  -630,  -630,  -630,  -630,  -630,
    -630,  -630,  -630,  -630,  -630,  -630,  -630,  -630,  -630,  -630,
    -630,  -630,  -630,  -630,  -630,  -630,  -630,  -630,  -630,  -630,
    -630,  -630,  -630,  -630,  -630,  -630,  -630,  -630,  -630,  -630,
    -630,  -630,  -630,  -630,  -630,  -630,  -630,  -630,  -630,  -630,
    -630,  -630,  -630,  -630,   942,  3681,  -630,  3118,  1044,  1044,
    1044,  1107,  1107,  -630,  -630,  -630,  -630,  -630,  -630,  -630,
    -630,  -630,  -630,   533,  1107,  1107,  1107,  1107,  1107,   942,
    3817,  3749,   562,   942,   942,   942,   942,   566,   549,   125,
    1423,  -630,   552,   577,   577,   221,   122,   221,  -630,   580,
     544,  -630,  -630,  -630,    72,  -630,    84,  -630,   499,   577,
    -630,   583,   577,  -630,   587,   584,   589,   554,  -630,   336,
     591,   512,   513,  -630,  -630,   942,   942,   515,   528,   516,
    -630,   509,    69,  -630,   241,  -630,   942,  -630,   521,   259,
     522,   280,   295,  1355,   532,   530,   942,  -630,  -630,  -630,
    -630,  -630,   529,  -630,   531,   535,   539,   541,   542,  -630,
    -630,   536,  -630,  -630,  -630,  -630,  -630,  -630,  -630,  -630,
    -630,  -630,   537,   299,   477,  -630,   380,   380,  1107,   450,
     382,   382,  -630,  -630,   342,   384,  -630,   546,   548,  -630,
    -630,  -630,   550,   545,   592,   594,    34,   942,   632,   549,
     942,  1628,   942,   414,   161,   551,   553,   560,   284,   558,
     436,  1696,  -630,   942,  1764,  -630,  -630,  -630,  -630,   942,
     561,  -630,   563,   567,  1832,   627,  -630,  -630,   583,  -630,
     942,   564,   942,   942,  -630,  -630,   655,   942,   656,  -630,
     942,   942,   568,  -630,  -630,  -630,    50,  -630,   570,   647,
     661,   662,   275,  -630,  -630,  -630,   158,  -630,  -630,   593,
    -630,   297,   356,  -630,   942,   942,  -630,  1492,   549,   652,
    1044,   450,  -630,   942,  -630,  3817,   942,   942,   588,  -630,
     942,   942,   942,   677,   590,   640,    42,   597,   639,   665,
     942,   598,   664,    77,   549,   386,   691,   388,   605,  -630,
     643,  -630,   641,   618,  -630,  -630,   612,   642,   703,   614,
     359,  -630,   622,   624,   625,  -630,   942,   626,  -630,  -630,
    -630,  -630,  1285,    87,   710,  -630,  -630,   687,   628,   629,
     942,   656,   549,  -630,   903,  -630,  -630,   631,   633,  3045,
     693,  -630,  3065,  -630,  -630,   360,  -630,   634,   637,  -630,
    1900,    27,   636,   638,   672,  -630,   942,   942,   722,  -630,
     648,   653,   702,  -630,   654,  -630,  -630,  -630,  -630,  -630,
    -630,   725,   728,  -630,   663,   549,  -630,   176,   549,  -630,
    -630,  1968,   657,   658,  -630,  2036,  -630,   659,  -630,  -630,
    -630,  -630,  -630,  -630,  -630,   644,   112,  -630,   650,   180,
      62,   729,   730,  -630,  -630,   742,   118,   745,   740,  -630,
     666,   668,    20,  -630,  -630,  -630,   942,  -630,   667,   671,
     942,  -630,   678,  -630,  -630,  -630,  2104,   708,    92,  -630,
     942,  -630,  -630,  -630,   942,  1900,   681,   685,   709,  -630,
    -630,   686,  2988,  -630,  -630,  -630,    77,   688,  2172,    77,
     549,   690,  2240,  2308,   700,  -630,  -630,   692,  2376,  -630,
    2444,   942,   479,   755,   758,  -630,  -630,   133,   656,   942,
     942,    56,   696,   762,  3409,  -630,   698,   673,   701,   777,
     704,   759,  -630,   185,   942,  -630,   761,  2512,  -630,  -630,
     769,  2580,  -630,   757,   706,  -630,   711,   713,  1900,  1900,
    1900,   716,  -630,  -630,  -630,   942,  2988,  -630,  -630,  -630,
     717,   718,   719,  -630,  -630,  -630,  -630,  -630,  -630,  -630,
    -630,  -630,  -630,   720,  -630,   721,   723,  -630,  -630,   726,
     727,   733,   734,   735,   737,   738,   739,  -630,  -630,  -630,
     749,  -630,   731,  -630,   751,   766,   741,   746,   778,   750,
    -630,   760,   763,   242,   756,   765,   832,   764,  -630,  -630,
     767,   840,  3477,  -630,  -630,   848,   775,   329,   162,  -630,
     849,  -630,   776,  -630,   850,   942,   549,   779,   782,   833,
    3138,   837,   784,  -630,  -630,  -630,  -630,  -630,  -630,  -630,
    1900,  1900,   785,  2988,  -630,  -630,  -630,  -630,  -630,  -630,
    -630,  -630,  -630,  -630,  -630,  -630,  -630,  -630,  2648,   786,
      77,  2716,   787,   789,  -630,  -630,   791,   792,  3545,   577,
     852,  -630,  -630,  -630,   793,   770,  -630,   851,   794,   331,
     164,   209,   796,  -630,   797,   888,   799,   212,  -630,   800,
     802,    25,  -630,  -630,   804,  -630,   806,   808,  -630,  1900,
    1900,  -630,  -630,  -630,   856,  -630,  -630,   860,  -630,  -630,
    -630,  -630,   809,   340,   817,  -630,  2784,  -630,   942,   219,
     820,  -630,   822,   914,   915,  -630,  -630,   827,  -630,   919,
    -630,  -630,   942,  -630,  1492,  -630,  1492,   652,  -630,  -630,
    -630,  1900,   831,   836,  -630,   839,   844,   864,  -630,   923,
    -630,  -630,   845,   854,   258,   268,  -630,  2852,   858,   859,
    -630,  -630,  -630,  -630,  -630,  -630,   281,  -630,  -630,   861,
     862,   303,  -630,  -630,   924,   938,   892,  -630,  -630,   953,
     304,  2920,  -630,  -630,   954,   865,   866,   867,   868,   959,
     961,   873,  -630,  -630,  -630,  -630,   875,   877,  -630,  -630,
    -630
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       3,     0,     2,     1,    52,     0,     0,     0,     0,     0,
      36,     0,    53,     0,     0,     0,     0,     0,     0,     0,
     214,   216,     0,   209,     0,     0,     0,     0,     0,     0,
       0,    54,     0,     0,     0,     0,     0,     0,     0,     4,
       5,     0,     0,    48,     0,     0,     9,    13,    15,    14,
      16,    17,    18,    19,    20,     0,    22,     0,     0,    23,
      12,    11,    10,    26,     0,     0,     0,     0,     0,     0,
       0,     0,    35,     0,   312,   312,     0,     0,     0,     0,
     287,    52,   290,     0,     0,   285,   294,   295,   296,   297,
       0,     0,     0,     0,     0,   293,     0,     0,   396,   396,
       0,   304,     0,   240,   242,   244,   246,   248,   250,   253,
     256,   259,   262,   268,   301,   289,     0,    64,    67,     0,
       0,     0,     3,     0,     0,   215,   217,     0,     0,   210,
     212,   213,    52,     0,   197,     0,   149,   146,     0,     0,
       0,     0,     0,   208,    60,    62,    61,    63,   140,    58,
       0,     0,     0,   133,   135,   132,   134,     0,     6,    57,
      44,    45,    46,    47,     0,     0,    55,     0,     0,     0,
     211,     7,     8,    21,    24,    25,    27,    28,    29,    30,
      31,    32,    33,    34,     0,   314,     0,   313,     0,     0,
       0,     0,   156,   310,   312,   264,   249,   241,   265,     0,
       0,   263,     0,     0,     0,   340,   341,   387,   356,   353,
     354,   345,   360,   368,   369,   370,   386,   366,   367,   364,
     365,   351,   349,   375,   355,   346,   384,   358,   359,   347,
     350,   357,   383,   371,   372,   378,   361,   373,   374,   381,
     385,   352,   382,   348,   342,   343,   344,   379,   380,   377,
     362,   363,   376,   286,   312,     0,   291,     0,     0,     0,
       0,     0,     0,   275,   276,   277,   278,   279,   280,   281,
     282,   283,   284,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     3,     0,   318,   318,     0,     0,     0,     3,     0,
     147,     3,   207,   206,     0,   205,     0,   202,   340,   318,
      59,     0,   318,     3,     0,     0,     0,     0,    37,     0,
       0,   340,     0,    51,    38,     0,     0,    42,     0,   340,
     337,     0,     0,   336,     0,    66,     0,    68,     0,     0,
       0,     0,     0,     0,     0,     0,   312,   288,   298,   299,
     397,   316,   396,   302,   396,     0,     0,   308,    52,     3,
     229,    48,   230,   231,   232,   233,   234,   235,   236,   237,
     238,   239,     0,     0,   245,   247,   254,   255,     0,   251,
     257,   258,   260,   261,     0,     0,   338,   340,     0,   271,
     311,    65,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    91,   328,     0,   319,     0,     0,     0,
     150,     0,   198,     0,     0,   204,   203,   200,   201,   312,
       0,   142,     0,     0,     0,   138,   136,   137,     0,    49,
       0,     0,   312,   312,    41,    39,     0,     0,     0,   153,
       0,     0,     0,   396,   315,   173,     0,   167,     0,     0,
       0,   345,   378,   157,   159,   158,     0,   292,   266,     0,
     396,     0,     0,   305,   312,   312,   306,     0,     0,   224,
       0,   252,   269,     0,   272,     0,   312,   312,     0,     3,
       0,     0,     0,     0,     0,     0,     0,     0,    53,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     3,
      53,   148,    53,     0,   141,     3,     0,    53,     0,     0,
       0,    56,     0,     0,     0,    40,   394,     0,   187,   388,
     389,   151,     0,     0,     0,   173,   166,     0,     0,     0,
       0,     0,     0,   267,     0,   300,   303,     0,     0,     0,
      53,   218,     0,   219,   243,     0,   339,     0,     0,     3,
       0,     0,     0,     0,     0,     3,     0,     0,     0,    86,
       0,     0,     0,    93,     0,   320,   323,   324,   325,   326,
     327,     0,     0,   329,   331,     0,     3,   330,     0,     3,
       3,     0,     0,     0,    71,     0,     3,     0,   139,     3,
      50,    69,    70,    43,   395,     0,     0,   187,   340,     0,
       0,     0,     0,   174,   175,     0,     0,     0,     0,   155,
       0,     0,     0,   317,   307,   309,     0,     3,     0,     0,
       0,     3,     0,   270,   273,   274,     0,    53,    54,    76,
       0,     3,     3,     3,     0,     0,     0,     0,     0,    87,
      93,     0,    92,    88,   322,   321,     0,     0,     0,     0,
       0,     0,     0,     0,    53,   144,   199,    53,     0,   131,
       0,     0,     0,     0,     0,   188,   189,     0,     0,     0,
       0,     0,     0,     0,     0,   173,     0,     0,     0,     0,
       0,     0,   162,     0,     0,   160,     0,     0,   221,   220,
       0,     0,   225,    53,     0,    74,     0,     0,     0,     0,
       0,     0,    77,     3,     3,     0,    89,    93,   123,    94,
       0,     0,     0,    99,   101,   103,   102,   104,    95,   105,
     106,   107,   108,     0,   110,     0,     0,   100,   113,     0,
       0,     0,     0,     0,     0,     0,     0,   122,   332,     3,
      53,   335,   333,     3,    53,    53,     0,     0,    53,    53,
     390,     0,   195,     0,     0,     0,     0,     0,   391,   392,
       0,     0,     0,   173,   173,     0,     0,     0,     0,   171,
       0,   173,     0,   168,     0,     0,     0,     0,     0,    53,
       0,    53,     0,    73,    75,     3,    84,    78,    80,     3,
       0,     0,     0,    90,    96,    97,    98,   109,   111,   112,
     114,   115,   116,   117,   118,   119,   120,   121,     0,     0,
       0,     0,     0,     0,   143,   128,     0,     0,     0,   318,
       0,   190,   154,     3,     0,     0,   173,     0,     0,     0,
       0,     0,     0,   176,     0,     0,     0,     0,   170,     0,
       0,     0,   161,     3,     0,     3,     0,     0,    72,     0,
       0,    79,    81,     3,    53,   124,   334,    53,   125,   145,
     130,   129,     0,     0,     0,   196,     0,   152,     0,     0,
       0,   178,     0,     0,     0,   173,   173,     0,     3,     0,
     169,   165,     0,   163,     0,   222,     0,   224,   226,    85,
      82,     0,     0,     0,   187,     0,     0,    53,   393,     0,
     173,   177,     0,     0,     0,     0,   179,     0,     0,     0,
     223,   228,   227,    83,   126,   127,     0,   187,     3,     0,
       0,     0,   181,   183,     0,     0,    53,   172,   164,     0,
       0,     0,   194,   182,     0,     0,     0,     0,     0,     0,
      53,     0,   184,   180,   186,   193,     0,     0,   185,   192,
     191
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -630,  -630,   -60,  -630,  -252,  -230,   803,  -630,    -2,   869,
    -630,  -630,   830,  -250,  -248,  -629,  -615,  -611,  -606,  -605,
    -604,  -630,  -630,  -550,  -630,  -332,  -602,  -600,  -599,  -242,
     824,   556,  -594,  -593,  -589,  -116,  -630,  -630,  -630,  -630,
    -630,  -630,  -630,  -588,  -521,  -630,  -579,  -630,  -630,   695,
    -578,  -238,  -228,  -218,  -574,  -216,  -202,  -200,  -198,  -573,
    -537,    98,  -512,    33,   517,   732,   743,   -82,  -630,  -240,
     230,   218,   -70,   896,   744,  -630,   649,  -630,  -630,  -630,
     -69,  -344,  -630,  -284,  -614,  -630,   473,  -630,   -97,  -188,
    -498,   -87
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    40,    41,   328,   168,    42,   101,   169,
     320,   148,   149,    44,    45,    46,   629,    47,    48,    49,
      50,   403,   492,   642,   718,    51,    52,    53,    54,    55,
     150,   422,    56,    57,    58,   135,    59,   597,   518,    60,
     343,   455,    61,    62,   523,   604,   596,   666,   753,   136,
      63,    64,    65,    66,    67,    68,    69,    70,    71,    72,
     541,   543,   372,   185,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,   274,   113,   114,   256,   466,   115,
     186,   187,   352,   405,   573,   406,   332,   385,   333,   334,
     517,   203
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      43,   339,   341,   253,   606,   360,   188,   362,   196,   363,
     407,   454,   204,   713,   195,   364,   354,   296,   667,   365,
     702,    73,   130,   131,   134,   420,   201,   618,   423,   366,
     622,   714,   738,   611,   379,   741,   715,   716,   717,   367,
     720,   368,   721,   722,   102,   253,   117,   118,   724,   725,
     120,   123,   124,   726,   727,   369,   129,   370,   396,   371,
     137,   760,   290,   159,   728,   143,   671,   672,   732,   737,
     323,   630,   160,   161,   162,   163,   415,   713,   140,     3,
     565,   482,   566,   786,   787,   788,   510,   314,   417,   557,
     706,   600,   684,   524,   121,   714,    81,   882,   315,   761,
     715,   716,   717,    74,   720,   673,   721,   722,   567,   568,
     569,   570,   724,   725,   685,   601,   662,   726,   727,   883,
      12,   631,   600,   258,   197,   344,   483,   164,   728,   545,
     202,   134,   732,   737,   558,   416,   259,   662,   471,   141,
     663,   440,   762,   165,   525,   166,   679,   418,   674,   602,
     763,   302,    75,   167,   768,    31,   675,   793,   357,   571,
     572,   756,   441,   297,   713,   298,   600,    76,   600,   486,
     757,   300,   397,   345,   664,   851,   852,   303,   375,   408,
     602,   603,   714,   386,   389,   355,   695,   715,   716,   717,
     835,   720,   873,   721,   722,   664,   856,   318,   319,   724,
     725,   324,   304,    77,   726,   727,   665,   382,   383,   409,
     122,   305,   603,   600,   297,   728,   600,   398,   399,   732,
     737,   144,   279,   600,   602,    81,   602,   665,    78,   280,
     530,   401,   281,   493,   889,   890,   351,   874,   411,    79,
     879,   414,   830,   831,   531,   145,   456,   899,   649,    12,
     837,   532,   669,   424,   494,   361,   603,   775,   603,   119,
     146,   306,   600,   254,   574,   461,   125,   462,   846,   650,
     307,   602,   600,   670,   602,   255,   913,   459,   776,   127,
     147,   602,   128,   126,    31,   662,   924,   360,    43,   362,
     360,   363,   362,   134,   363,   134,   925,   364,   132,   467,
     364,   365,   612,   603,   365,   869,   603,   600,   662,   929,
     719,   366,   384,   603,   366,   916,   391,   392,   393,   394,
     602,   367,    12,   368,   367,   470,   368,   139,   442,   144,
     602,   934,   939,   443,   820,   259,   821,   369,   930,   370,
     369,   371,   370,   664,   371,   647,   446,   910,   651,   911,
     503,   443,   603,   145,   904,   905,   522,    31,   434,   435,
     285,   151,   603,   512,   513,   602,   664,   448,   146,   444,
     138,   498,   443,   534,   719,   665,   297,   152,   546,   921,
     133,   160,   161,   162,   163,   286,   535,   157,   147,   449,
     710,   350,   711,   450,   712,   537,   538,   603,   665,    43,
     723,   153,   154,   279,   729,   287,   288,   547,   548,    43,
     280,   309,    43,   281,   730,   170,   834,   158,   872,   550,
     742,   443,    43,   443,   731,   429,   733,   895,   430,   171,
     484,   472,   443,   487,   473,   489,   325,   172,   490,   581,
     734,   491,   735,   173,   736,   585,   501,   536,   590,   623,
     350,   336,   336,   184,   710,   174,   711,   175,   712,   155,
     156,   719,   275,   276,   723,    43,   277,   278,   729,   176,
     515,   261,   262,   519,   520,   474,   475,   177,   730,   575,
     576,   578,   579,   751,   752,   178,   767,   179,   731,   626,
     733,   376,   377,   380,   381,   635,   180,   181,   182,   183,
     191,   189,   190,   192,   734,   193,   735,   194,   736,   199,
     200,   257,   260,   551,   552,   553,   648,   282,   283,   652,
     653,   284,   289,   562,   291,   292,   658,   293,   360,   660,
     362,   294,   363,   295,   299,   864,   301,   361,   364,   311,
     361,   710,   365,   711,   312,   712,   841,   316,    43,   594,
     313,   723,   366,   327,   335,   729,   336,   687,   337,   342,
     346,   691,   367,   610,   368,   730,   390,   613,   347,   348,
     395,   698,   699,   700,   829,   731,   402,   733,   369,    43,
     370,   404,   371,    43,   412,   419,   413,   421,   426,   636,
     637,   734,   425,   735,   427,   736,   428,   431,   432,   433,
     437,   436,   438,   439,   159,   263,   264,   265,   266,   267,
     268,   269,   270,   271,   272,   445,   447,   261,   262,   457,
      99,   460,   463,   443,    43,   464,   696,   465,    74,   468,
     863,   469,   476,    43,   477,   480,   485,   478,   495,   479,
      43,   481,   508,   790,   791,   496,    43,   497,   504,   686,
      43,    43,   499,   690,   506,   511,    43,   505,    43,   514,
     516,   527,   521,   697,   526,   528,   529,   701,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   542,   808,
     533,   554,   549,   811,   555,    43,   166,   556,   560,    43,
     561,   559,   563,   564,   750,   577,    43,    43,    43,   580,
     582,   583,   758,   759,    43,   584,   586,   588,   589,   591,
     587,   592,   593,   595,   605,   607,   619,   777,   614,   634,
     615,   624,   608,   609,   625,   849,   638,   641,   644,   850,
     632,   645,   633,   676,   677,   646,   668,   661,   792,    80,
      81,    82,   639,    83,    84,    85,   678,   640,   643,   680,
     694,   655,   656,   659,   681,   683,   705,   746,   747,   754,
     682,   688,   755,   866,    12,   689,   765,    86,    87,    88,
      89,   770,   692,    90,    91,   703,    92,    93,   361,   704,
     707,   772,   739,   884,   743,   886,   778,   774,    43,    43,
     764,    43,   769,   891,   780,   771,   782,    94,   773,    31,
     783,    95,   809,   810,   812,   784,    43,   785,   840,    43,
     789,   794,   795,   796,   797,   798,   817,   799,   907,    96,
     800,   801,    97,   813,    98,   349,    99,   802,   803,   804,
     350,   805,   806,   807,   100,   814,   824,    80,    81,    82,
     815,    83,    84,    85,   827,   816,   818,    43,    43,   819,
     822,   825,   832,   836,   839,   870,   844,   865,   931,   823,
     847,   826,    12,   868,    43,    86,    87,    88,    89,   833,
     838,    90,    91,   842,    92,    93,   843,   116,   848,   853,
     855,   858,    43,   859,    43,   860,   861,   867,   871,    43,
     875,   876,   877,   878,   880,    94,   881,    31,   885,    95,
     887,   898,   888,   894,   896,    43,    80,    81,    82,   892,
      83,    84,    85,   893,   900,   909,   901,    96,   902,   903,
      97,   906,    98,   908,    99,   914,   919,   920,   935,    43,
     915,    12,   100,   917,    86,    87,    88,    89,   918,   922,
      90,    91,   936,    92,    93,    80,    81,    82,   923,    83,
      84,    85,   927,   928,   937,   932,   933,   938,   941,   942,
     943,   944,   945,   946,    94,   947,    31,   948,    95,   949,
      12,   950,   326,    86,    87,    88,    89,   273,   310,    90,
      91,   317,    92,    93,   509,   912,    96,   544,   198,    97,
     373,    98,   410,    99,   458,   599,     0,   350,     0,     0,
       0,   100,   374,    94,     0,    31,     0,    95,    80,    81,
      82,     0,    83,    84,    85,     0,     0,   378,     0,     0,
       0,     0,     0,     0,     0,    96,     0,     0,    97,     0,
      98,     0,    99,    12,     0,     0,    86,    87,    88,    89,
     100,     0,    90,    91,     0,    92,    93,    80,    81,    82,
       0,    83,    84,    85,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    94,     0,    31,     0,
      95,     0,    12,     0,     0,    86,    87,    88,    89,     0,
       0,    90,     0,     0,    92,    93,     0,     0,    96,     0,
       0,    97,     0,    98,     0,    99,     0,     0,     0,     0,
       0,     0,     0,   142,     0,    94,     0,    31,     0,    95,
      80,    81,    82,     0,    83,    84,    85,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    96,     0,     0,
      97,     0,    98,     0,    99,    12,     0,     0,    86,    87,
      88,    89,   100,     0,     0,     0,     0,    92,    93,    80,
      81,    82,     0,    83,     0,    85,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    94,     0,
      31,     0,    95,     0,    12,     0,     0,    86,    87,    88,
      89,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      96,     0,     0,    97,     0,    98,     0,    99,     0,     0,
       0,     0,     0,     0,     0,   100,     0,    94,     0,    31,
       0,    95,     0,     0,     0,     0,     0,     0,     0,   329,
     330,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     206,   207,    97,     0,    98,     0,    99,     0,   208,     0,
     209,   210,     0,   211,   100,   212,   213,   214,   215,   216,
     217,   218,   219,   220,   221,   222,   223,   224,   225,   226,
     227,   228,   229,   230,   231,   232,   233,   234,   235,   236,
     237,   238,   239,   240,     0,   241,   242,   243,   244,   245,
     246,   247,   248,   249,   250,   251,   252,     0,     0,   598,
     330,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     206,   207,     0,     0,     0,     0,   353,     0,   208,   350,
     209,   210,     0,   211,     0,   212,   213,   214,   215,   216,
     217,   218,   219,   220,   221,   222,   223,   224,   225,   226,
     227,   228,   229,   230,   231,   232,   233,   234,   235,   236,
     237,   238,   239,   240,     0,   241,   242,   243,   244,   245,
     246,   247,   248,   249,   250,   251,   252,     0,     0,   205,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     206,   207,     0,     0,     0,     0,     0,     0,   208,   350,
     209,   210,     0,   451,     0,   212,   213,   214,   215,   216,
     217,   218,   219,   220,   221,   222,   223,   224,   225,   226,
     227,   228,   229,   230,   231,   232,   233,   234,   452,   236,
     237,   238,   239,   240,     0,   241,   242,   243,   244,   245,
     246,   247,   248,   249,   250,   251,   252,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     9,     0,     0,    10,
       0,     0,     0,     0,     0,     0,    11,     0,     0,   453,
       0,    12,     0,    13,     0,     0,     0,     0,     0,     0,
       0,    14,    15,     0,     0,    16,     0,     0,    17,   400,
       0,     0,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,     0,    30,    31,     0,    32,    33,
      34,    35,    36,    37,    38,     0,     4,     0,     0,     5,
       0,     0,     6,     7,     8,     9,     0,     0,    10,     0,
       0,     0,     0,     0,     0,    11,     0,    39,   539,     0,
     540,     0,    13,     0,     0,     0,     0,     0,     0,     0,
      14,    15,     0,     0,    16,     0,     0,    17,     0,     0,
       0,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,    29,     0,    30,    31,     0,    32,    33,    34,
      35,    36,    37,    38,     4,     0,     0,     5,     0,     0,
       6,     7,     8,     9,     0,     0,    10,     0,     0,     0,
       0,     0,     0,    11,     0,     0,    39,     0,    12,     0,
      13,     0,     0,     0,     0,     0,     0,     0,    14,    15,
       0,     0,    16,     0,     0,    17,     0,     0,     0,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,     0,    30,    31,     0,    32,    33,    34,    35,    36,
      37,    38,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     9,     0,     0,    10,     0,     0,     0,     0,     0,
       0,    11,     0,     0,    39,     0,   488,     0,    13,     0,
       0,     0,     0,     0,     0,     0,    14,    15,     0,     0,
      16,     0,     0,    17,     0,     0,     0,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    29,     0,
      30,    31,     0,    32,    33,    34,    35,    36,    37,    38,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     9,
       0,     0,    10,     0,     0,     0,     0,     0,     0,    11,
       0,     0,    39,     0,   500,     0,    13,     0,     0,     0,
       0,     0,     0,     0,    14,    15,     0,     0,    16,     0,
       0,    17,     0,     0,     0,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,     0,    30,    31,
       0,    32,    33,    34,    35,    36,    37,    38,     4,     0,
       0,     5,     0,     0,     6,     7,     8,     9,     0,     0,
      10,     0,     0,     0,     0,     0,     0,    11,     0,     0,
      39,     0,   502,     0,    13,     0,     0,     0,     0,     0,
       0,     0,    14,    15,     0,     0,    16,     0,     0,    17,
       0,     0,     0,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,     0,    30,    31,     0,    32,
      33,    34,    35,    36,    37,    38,     4,     0,     0,     5,
       0,     0,     6,     7,     8,     9,     0,     0,    10,     0,
       0,     0,     0,     0,     0,    11,     0,     0,    39,     0,
     507,     0,    13,     0,     0,     0,     0,     0,     0,     0,
      14,    15,     0,     0,    16,     0,     0,    17,     0,     0,
       0,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,    29,     0,    30,    31,     0,    32,    33,    34,
      35,    36,    37,    38,     4,     0,     0,     5,     0,     0,
       6,     7,     8,     9,     0,     0,    10,     0,     0,     0,
       0,     0,     0,    11,     0,     0,    39,     0,   627,     0,
      13,     0,     0,     0,     0,     0,     0,     0,    14,    15,
       0,     0,    16,     0,     0,    17,     0,     0,     0,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,     0,    30,   628,     0,    32,    33,    34,    35,    36,
      37,    38,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     9,     0,     0,    10,     0,     0,     0,     0,     0,
       0,    11,     0,     0,    39,     0,   654,     0,    13,     0,
       0,     0,     0,     0,     0,     0,    14,    15,     0,     0,
      16,     0,     0,    17,     0,     0,     0,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    29,     0,
      30,    31,     0,    32,    33,    34,    35,    36,    37,    38,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     9,
       0,     0,    10,     0,     0,     0,     0,     0,     0,    11,
       0,     0,    39,     0,   657,     0,    13,     0,     0,     0,
       0,     0,     0,     0,    14,    15,     0,     0,    16,     0,
       0,    17,     0,     0,     0,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,     0,    30,    31,
       0,    32,    33,    34,    35,    36,    37,    38,     4,     0,
       0,     5,     0,     0,     6,     7,     8,     9,     0,     0,
      10,     0,     0,     0,     0,     0,     0,    11,     0,     0,
      39,     0,   693,     0,    13,     0,     0,     0,     0,     0,
       0,     0,    14,    15,     0,     0,    16,     0,     0,    17,
       0,     0,     0,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,     0,    30,    31,     0,    32,
      33,    34,    35,    36,    37,    38,     4,     0,     0,     5,
       0,     0,     6,     7,     8,     9,     0,     0,    10,     0,
       0,     0,     0,     0,     0,    11,     0,     0,    39,     0,
     740,     0,    13,     0,     0,     0,     0,     0,     0,     0,
      14,    15,     0,     0,    16,     0,     0,    17,     0,     0,
       0,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,    29,     0,    30,    31,     0,    32,    33,    34,
      35,    36,    37,    38,     4,     0,     0,     5,     0,     0,
       6,     7,     8,     9,     0,     0,    10,     0,     0,     0,
       0,     0,     0,    11,     0,     0,    39,     0,   744,     0,
      13,     0,     0,     0,     0,     0,     0,     0,    14,    15,
       0,     0,    16,     0,     0,    17,     0,     0,     0,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,     0,    30,    31,     0,    32,    33,    34,    35,    36,
      37,    38,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     9,     0,     0,    10,     0,     0,     0,     0,     0,
       0,    11,     0,     0,    39,     0,   745,     0,    13,     0,
       0,     0,     0,     0,     0,     0,    14,    15,     0,     0,
      16,     0,     0,    17,     0,     0,     0,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    29,     0,
      30,    31,     0,    32,    33,    34,    35,    36,    37,    38,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     9,
       0,     0,    10,     0,     0,     0,     0,     0,     0,    11,
       0,     0,    39,     0,   748,     0,    13,     0,     0,     0,
       0,     0,     0,     0,    14,    15,     0,     0,    16,     0,
       0,    17,     0,     0,     0,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,     0,    30,    31,
       0,    32,    33,    34,    35,    36,    37,    38,     4,     0,
       0,     5,     0,     0,     6,     7,     8,     9,     0,     0,
      10,     0,     0,     0,     0,     0,     0,    11,     0,     0,
      39,     0,   749,     0,    13,     0,     0,     0,     0,     0,
       0,     0,    14,    15,     0,     0,    16,     0,     0,    17,
       0,     0,     0,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,     0,    30,    31,     0,    32,
      33,    34,    35,    36,    37,    38,     4,     0,     0,     5,
       0,     0,     6,     7,     8,     9,     0,     0,    10,     0,
       0,     0,     0,     0,     0,    11,     0,     0,    39,     0,
     779,     0,    13,     0,     0,     0,     0,     0,     0,     0,
      14,    15,     0,     0,    16,     0,     0,    17,     0,     0,
       0,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,    29,     0,    30,    31,     0,    32,    33,    34,
      35,    36,    37,    38,     4,     0,     0,     5,     0,     0,
       6,     7,     8,     9,     0,     0,    10,     0,     0,     0,
       0,     0,     0,    11,     0,     0,    39,     0,   781,     0,
      13,     0,     0,     0,     0,     0,     0,     0,    14,    15,
       0,     0,    16,     0,     0,    17,     0,     0,     0,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,     0,    30,    31,     0,    32,    33,    34,    35,    36,
      37,    38,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     9,     0,     0,    10,     0,     0,     0,     0,     0,
       0,    11,     0,     0,    39,     0,   854,     0,    13,     0,
       0,     0,     0,     0,     0,     0,    14,    15,     0,     0,
      16,     0,     0,    17,     0,     0,     0,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    29,     0,
      30,    31,     0,    32,    33,    34,    35,    36,    37,    38,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     9,
       0,     0,    10,     0,     0,     0,     0,     0,     0,    11,
       0,     0,    39,     0,   857,     0,    13,     0,     0,     0,
       0,     0,     0,     0,    14,    15,     0,     0,    16,     0,
       0,    17,     0,     0,     0,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,     0,    30,    31,
       0,    32,    33,    34,    35,    36,    37,    38,     4,     0,
       0,     5,     0,     0,     6,     7,     8,     9,     0,     0,
      10,     0,     0,     0,     0,     0,     0,    11,     0,     0,
      39,     0,   897,     0,    13,     0,     0,     0,     0,     0,
       0,     0,    14,    15,     0,     0,    16,     0,     0,    17,
       0,     0,     0,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,     0,    30,    31,     0,    32,
      33,    34,    35,    36,    37,    38,     4,     0,     0,     5,
       0,     0,     6,     7,     8,     9,     0,     0,    10,     0,
       0,     0,     0,     0,     0,    11,     0,     0,    39,     0,
     926,     0,    13,     0,     0,     0,     0,     0,     0,     0,
      14,    15,     0,     0,    16,     0,     0,    17,     0,     0,
       0,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,    29,     0,    30,    31,     0,    32,    33,    34,
      35,    36,    37,    38,     4,     0,     0,     5,     0,     0,
       6,     7,     8,     9,     0,     0,    10,     0,     0,     0,
       0,     0,     0,    11,     0,     0,    39,     0,   940,     0,
      13,     0,     0,     0,     0,     0,     0,     0,    14,    15,
       0,     0,    16,     0,     0,    17,     0,     0,     0,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,     0,    30,    31,     0,    32,    33,    34,    35,    36,
      37,    38,   358,     0,     0,     5,     0,     0,     6,     0,
       0,     0,     0,     0,   708,     0,     0,     0,     0,     0,
       0,    11,     0,     0,    39,     0,    12,     0,    13,     0,
       0,     0,     0,     0,     0,     0,    14,    15,     0,     0,
      16,     0,     0,    17,     0,     0,     0,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    29,   358,
      30,    31,     5,    32,    33,    34,    35,    36,    37,    38,
       0,     0,     0,     0,     0,     0,     0,     0,   616,   358,
       0,     0,     5,    12,     0,    13,     0,     0,     0,     0,
       0,     0,   709,    14,     0,     0,     0,     0,   620,     0,
       0,     0,     0,    12,     0,    13,    20,    21,     0,    23,
      24,    25,     0,    14,     0,     0,     0,    30,    31,     0,
      32,     0,     0,     0,    36,    37,    20,    21,     0,    23,
      24,    25,   358,     0,     0,     5,     0,    30,    31,     0,
      32,     0,     0,     0,    36,    37,     0,     0,     0,   617,
       0,     0,   358,     0,     0,     5,    12,     0,    13,     0,
       0,     0,     0,     0,     0,     0,    14,     0,     0,   621,
       0,     0,     0,     0,     0,     0,    12,     0,    13,    20,
      21,     0,    23,    24,    25,     0,    14,     0,     0,     0,
      30,    31,     0,    32,     0,     0,     0,    36,    37,    20,
      21,     0,    23,    24,    25,     0,     0,     0,     0,     0,
      30,    31,     0,    32,     0,     0,     0,    36,    37,   329,
     330,     0,   359,     0,     0,     0,     0,     0,     0,     0,
     206,   207,     0,     0,     0,     0,     0,     0,   208,     0,
     209,   210,   845,   211,     0,   212,   213,   214,   215,   216,
     217,   218,   219,   220,   221,   222,   223,   224,   225,   226,
     227,   228,   229,   230,   231,   232,   233,   234,   235,   236,
     237,   238,   239,   240,     0,   241,   242,   243,   244,   245,
     246,   247,   248,   249,   250,   251,   252,   329,   330,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   206,   207,
       0,     0,   331,     0,     0,     0,   208,     0,   209,   210,
       0,   211,     0,   212,   213,   214,   215,   216,   217,   218,
     219,   220,   221,   222,   223,   224,   225,   226,   227,   228,
     229,   230,   231,   232,   233,   234,   235,   236,   237,   238,
     239,   240,     0,   241,   242,   243,   244,   245,   246,   247,
     248,   249,   250,   251,   252,   329,   330,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   206,   207,     0,     0,
     338,     0,     0,     0,   208,     0,   209,   210,     0,   211,
       0,   212,   213,   214,   215,   216,   217,   218,   219,   220,
     221,   222,   223,   224,   225,   226,   227,   228,   229,   230,
     231,   232,   233,   234,   235,   236,   237,   238,   239,   240,
       0,   241,   242,   243,   244,   245,   246,   247,   248,   249,
     250,   251,   252,   329,   330,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   206,   207,     0,     0,   340,     0,
       0,     0,   208,     0,   209,   210,     0,   211,     0,   212,
     213,   214,   215,   216,   217,   218,   219,   220,   221,   222,
     223,   224,   225,   226,   227,   228,   229,   230,   231,   232,
     233,   234,   235,   236,   237,   238,   239,   240,     0,   241,
     242,   243,   244,   245,   246,   247,   248,   249,   250,   251,
     252,   329,   330,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   206,   207,     0,     0,   766,     0,     0,     0,
     208,     0,   209,   210,     0,   211,     0,   212,   213,   214,
     215,   216,   217,   218,   219,   220,   221,   222,   223,   224,
     225,   226,   227,   228,   229,   230,   231,   232,   233,   234,
     235,   236,   237,   238,   239,   240,     0,   241,   242,   243,
     244,   245,   246,   247,   248,   249,   250,   251,   252,   329,
     330,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     206,   207,     0,     0,   828,     0,     0,     0,   208,     0,
     209,   210,     0,   211,     0,   212,   213,   214,   215,   216,
     217,   218,   219,   220,   221,   222,   223,   224,   225,   226,
     227,   228,   229,   230,   231,   232,   233,   234,   235,   236,
     237,   238,   239,   240,     0,   241,   242,   243,   244,   245,
     246,   247,   248,   249,   250,   251,   252,   321,     0,     0,
     322,     0,     0,     0,     0,     0,     0,     0,   206,   207,
       0,     0,   862,     0,     0,     0,   208,     0,   209,   210,
       0,   211,     0,   212,   213,   214,   215,   216,   217,   218,
     219,   220,   221,   222,   223,   224,   225,   226,   227,   228,
     229,   230,   231,   232,   233,   234,   235,   236,   237,   238,
     239,   240,     0,   241,   242,   243,   244,   245,   246,   247,
     248,   249,   250,   251,   252,   205,     0,     0,   356,     0,
       0,     0,     0,     0,     0,     0,   206,   207,     0,     0,
       0,     0,     0,     0,   208,     0,   209,   210,     0,   211,
       0,   212,   213,   214,   215,   216,   217,   218,   219,   220,
     221,   222,   223,   224,   225,   226,   227,   228,   229,   230,
     231,   232,   233,   234,   235,   236,   237,   238,   239,   240,
       0,   241,   242,   243,   244,   245,   246,   247,   248,   249,
     250,   251,   252,   387,     0,     0,   388,     0,     0,     0,
       0,     0,     0,     0,   206,   207,     0,     0,     0,     0,
       0,     0,   208,     0,   209,   210,     0,   211,     0,   212,
     213,   214,   215,   216,   217,   218,   219,   220,   221,   222,
     223,   224,   225,   226,   227,   228,   229,   230,   231,   232,
     233,   234,   235,   236,   237,   238,   239,   240,     0,   241,
     242,   243,   244,   245,   246,   247,   248,   249,   250,   251,
     252,   205,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   206,   207,     0,     0,     0,     0,     0,     0,
     208,     0,   209,   210,     0,   211,     0,   212,   213,   214,
     215,   216,   217,   218,   219,   220,   221,   222,   223,   224,
     225,   226,   227,   228,   229,   230,   231,   232,   233,   234,
     235,   236,   237,   238,   239,   240,     0,   241,   242,   243,
     244,   245,   246,   247,   248,   249,   250,   251,   252,   308,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     206,   207,     0,     0,     0,     0,     0,     0,   208,     0,
     209,   210,     0,   211,     0,   212,   213,   214,   215,   216,
     217,   218,   219,   220,   221,   222,   223,   224,   225,   226,
     227,   228,   229,   230,   231,   232,   233,   234,   235,   236,
     237,   238,   239,   240,     0,   241,   242,   243,   244,   245,
     246,   247,   248,   249,   250,   251,   252
};

static const yytype_int16 yycheck[] =
{
       2,   189,   190,   100,   525,   257,    75,   257,    90,   257,
     294,   343,    99,   642,    84,   257,   204,   133,   597,   257,
     635,     4,    24,    25,    26,   309,    96,   539,   312,   257,
     542,   642,   646,   531,   274,   649,   642,   642,   642,   257,
     642,   257,   642,   642,    11,   142,    13,    14,   642,   642,
       4,    18,    19,   642,   642,   257,    23,   257,   288,   257,
      27,     5,   122,     8,   642,    32,     4,     5,   642,   642,
     167,    44,    17,    18,    19,    20,     4,   706,     4,     0,
       3,    47,     5,   698,   699,   700,   430,     4,     4,    47,
     640,     4,    72,    43,    48,   706,     4,    72,    15,    43,
     706,   706,   706,    86,   706,    43,   706,   706,    31,    32,
      33,    34,   706,   706,    94,    28,     4,   706,   706,    94,
      28,    94,     4,    23,    91,   194,    92,    72,   706,   473,
      97,   133,   706,   706,    92,    63,    36,     4,   378,    65,
      28,    72,    86,    88,    94,    90,    28,    63,    86,    62,
      94,     4,    86,    98,   675,    63,    94,   707,   255,    82,
      83,    28,    93,    92,   793,    94,     4,     4,     4,   399,
     668,   138,    47,    39,    62,   790,   791,    30,   260,   295,
      62,    94,   793,   280,   281,   254,    94,   793,   793,   793,
      28,   793,    28,   793,   793,    62,   810,   164,   165,   793,
     793,   168,    55,     4,   793,   793,    94,   277,   278,    87,
      94,    64,    94,     4,    92,   793,     4,    92,    93,   793,
     793,     4,    88,     4,    62,     4,    62,    94,     4,    95,
      72,   291,    98,    72,   849,   850,   203,    28,   298,     4,
      28,   301,   763,   764,    86,    28,   343,    28,    72,    28,
     771,    93,    72,   313,    93,   257,    94,    72,    94,     4,
      43,    55,     4,    86,   494,   352,     4,   354,   780,    93,
      64,    62,     4,    93,    62,    98,   891,   346,    93,     4,
      63,    62,     7,     4,    63,     4,    28,   539,   290,   539,
     542,   539,   542,   295,   542,   297,    28,   539,     4,   359,
     542,   539,   532,    94,   542,   826,    94,     4,     4,    28,
     642,   539,   279,    94,   542,   894,   283,   284,   285,   286,
      62,   539,    28,   539,   542,    26,   542,    61,    87,     4,
      62,    28,    28,    92,    92,    36,    94,   539,   917,   539,
     542,   539,   542,    62,   542,   575,    87,   884,   578,   886,
     419,    92,    94,    28,   875,   876,   443,    63,   325,   326,
      47,     4,    94,   432,   433,    62,    62,    87,    43,   336,
      46,    87,    92,   460,   706,    94,    92,     4,   475,   900,
      86,    17,    18,    19,    20,    72,    89,    66,    63,    94,
     642,    94,   642,    98,   642,   464,   465,    94,    94,   401,
     642,     4,     5,    88,   642,    92,    93,   476,   477,   411,
      95,    86,   414,    98,   642,    93,    87,    94,    87,   479,
     650,    92,   424,    92,   642,    89,   642,    87,    92,    94,
     397,    89,    92,   400,    92,   402,    72,    94,    24,   499,
     642,    27,   642,    94,   642,   505,   413,    91,    89,    89,
      94,    92,    92,    86,   706,    94,   706,    94,   706,     4,
       5,   793,    82,    83,   706,   467,    84,    85,   706,    94,
     437,    21,    22,   440,   441,    91,    92,    94,   706,    93,
      94,    93,    94,     4,     5,    94,   674,    94,   706,   549,
     706,   261,   262,   275,   276,   555,    94,    94,    94,    94,
      62,    86,    86,    94,   706,     4,   706,    86,   706,     4,
      86,    25,    35,   480,   481,   482,   576,     3,    65,   579,
     580,    86,     4,   490,    94,    94,   586,    86,   780,   589,
     780,    86,   780,    86,    98,   819,    94,   539,   780,    42,
     542,   793,   780,   793,    86,   793,   776,     4,   550,   516,
      94,   793,   780,     4,    87,   793,    92,   617,    87,     4,
      86,   621,   780,   530,   780,   793,     4,   534,    87,    87,
       4,   631,   632,   633,   762,   793,    24,   793,   780,   581,
     780,     4,   780,   585,     4,    86,    42,     4,     4,   556,
     557,   793,     5,   793,     5,   793,    42,     6,    86,    86,
      72,    86,    86,    94,     8,    72,    73,    74,    75,    76,
      77,    78,    79,    80,    81,    94,    94,    21,    22,    87,
      90,    92,    87,    92,   626,    86,   628,    86,    86,    93,
     818,    94,    86,   635,    86,    43,     4,    87,    87,    94,
     642,    47,    15,   703,   704,    92,   648,    87,    87,   616,
     652,   653,    94,   620,    87,    91,   658,    94,   660,     4,
       4,    14,    94,   630,    94,     4,     4,   634,    72,    73,
      74,    75,    76,    77,    78,    79,    80,    81,    26,   739,
      87,     4,    94,   743,    94,   687,    90,    47,    49,   691,
      25,    94,    94,    29,   661,     4,   698,   699,   700,    94,
      57,    60,   669,   670,   706,    87,    94,     4,    94,    87,
      68,    87,    87,    87,     4,    28,    23,   684,    87,    47,
      87,    87,    94,    94,    87,   785,     4,    25,     3,   789,
      94,     3,    94,     4,     4,    72,    86,    93,   705,     3,
       4,     5,    94,     7,     8,     9,     4,    94,    94,     4,
      42,    94,    94,    94,    14,    87,    47,    57,    66,     4,
      94,    94,     4,   823,    28,    94,     4,    31,    32,    33,
      34,    98,    94,    37,    38,    94,    40,    41,   780,    94,
      94,     4,    94,   843,    94,   845,    25,    28,   790,   791,
      94,   793,    94,   853,    25,    94,    39,    61,    94,    63,
      94,    65,    53,    72,    53,    94,   808,    94,   775,   811,
      94,    94,    94,    94,    94,    94,    66,    94,   878,    83,
      94,    94,    86,    57,    88,    89,    90,    94,    94,    94,
      94,    94,    94,    94,    98,    94,     4,     3,     4,     5,
      94,     7,     8,     9,     4,    67,    86,   849,   850,    86,
      94,    87,     4,     4,     4,     4,    23,     5,   918,    94,
      23,    94,    28,    93,   866,    31,    32,    33,    34,    94,
      94,    37,    38,    94,    40,    41,    94,    43,    94,    94,
      94,    94,   884,    94,   886,    94,    94,    94,    94,   891,
      94,    94,     4,    94,    94,    61,    94,    63,    94,    65,
      94,   868,    94,    94,    87,   907,     3,     4,     5,    53,
       7,     8,     9,    53,    94,   882,    94,    83,     4,     4,
      86,    94,    88,     4,    90,    94,    62,     4,     4,   931,
      94,    28,    98,    94,    31,    32,    33,    34,    94,    94,
      37,    38,     4,    40,    41,     3,     4,     5,    94,     7,
       8,     9,    94,    94,    62,    94,    94,     4,     4,    94,
      94,    94,    94,     4,    61,     4,    63,    94,    65,    94,
      28,    94,   169,    31,    32,    33,    34,   108,   148,    37,
      38,   157,    40,    41,   428,   887,    83,   470,    92,    86,
     258,    88,   297,    90,   345,   522,    -1,    94,    -1,    -1,
      -1,    98,   259,    61,    -1,    63,    -1,    65,     3,     4,
       5,    -1,     7,     8,     9,    -1,    -1,   273,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    83,    -1,    -1,    86,    -1,
      88,    -1,    90,    28,    -1,    -1,    31,    32,    33,    34,
      98,    -1,    37,    38,    -1,    40,    41,     3,     4,     5,
      -1,     7,     8,     9,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    61,    -1,    63,    -1,
      65,    -1,    28,    -1,    -1,    31,    32,    33,    34,    -1,
      -1,    37,    -1,    -1,    40,    41,    -1,    -1,    83,    -1,
      -1,    86,    -1,    88,    -1,    90,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    98,    -1,    61,    -1,    63,    -1,    65,
       3,     4,     5,    -1,     7,     8,     9,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    83,    -1,    -1,
      86,    -1,    88,    -1,    90,    28,    -1,    -1,    31,    32,
      33,    34,    98,    -1,    -1,    -1,    -1,    40,    41,     3,
       4,     5,    -1,     7,    -1,     9,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    61,    -1,
      63,    -1,    65,    -1,    28,    -1,    -1,    31,    32,    33,
      34,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      83,    -1,    -1,    86,    -1,    88,    -1,    90,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    98,    -1,    61,    -1,    63,
      -1,    65,    -1,    -1,    -1,    -1,    -1,    -1,    -1,     4,
       5,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      15,    16,    86,    -1,    88,    -1,    90,    -1,    23,    -1,
      25,    26,    -1,    28,    98,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    -1,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    -1,    -1,     4,
       5,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      15,    16,    -1,    -1,    -1,    -1,    91,    -1,    23,    94,
      25,    26,    -1,    28,    -1,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    -1,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    -1,    -1,     4,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      15,    16,    -1,    -1,    -1,    -1,    -1,    -1,    23,    94,
      25,    26,    -1,    28,    -1,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    -1,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,     4,    -1,    -1,
       7,    -1,    -1,    10,    11,    12,    13,    -1,    -1,    16,
      -1,    -1,    -1,    -1,    -1,    -1,    23,    -1,    -1,    94,
      -1,    28,    -1,    30,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    38,    39,    -1,    -1,    42,    -1,    -1,    45,    46,
      -1,    -1,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    -1,    62,    63,    -1,    65,    66,
      67,    68,    69,    70,    71,    -1,     4,    -1,    -1,     7,
      -1,    -1,    10,    11,    12,    13,    -1,    -1,    16,    -1,
      -1,    -1,    -1,    -1,    -1,    23,    -1,    94,    26,    -1,
      28,    -1,    30,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      38,    39,    -1,    -1,    42,    -1,    -1,    45,    -1,    -1,
      -1,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    -1,    62,    63,    -1,    65,    66,    67,
      68,    69,    70,    71,     4,    -1,    -1,     7,    -1,    -1,
      10,    11,    12,    13,    -1,    -1,    16,    -1,    -1,    -1,
      -1,    -1,    -1,    23,    -1,    -1,    94,    -1,    28,    -1,
      30,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    38,    39,
      -1,    -1,    42,    -1,    -1,    45,    -1,    -1,    -1,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    -1,    62,    63,    -1,    65,    66,    67,    68,    69,
      70,    71,     4,    -1,    -1,     7,    -1,    -1,    10,    11,
      12,    13,    -1,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    23,    -1,    -1,    94,    -1,    28,    -1,    30,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    38,    39,    -1,    -1,
      42,    -1,    -1,    45,    -1,    -1,    -1,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    -1,
      62,    63,    -1,    65,    66,    67,    68,    69,    70,    71,
       4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,    13,
      -1,    -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    23,
      -1,    -1,    94,    -1,    28,    -1,    30,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    38,    39,    -1,    -1,    42,    -1,
      -1,    45,    -1,    -1,    -1,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    -1,    62,    63,
      -1,    65,    66,    67,    68,    69,    70,    71,     4,    -1,
      -1,     7,    -1,    -1,    10,    11,    12,    13,    -1,    -1,
      16,    -1,    -1,    -1,    -1,    -1,    -1,    23,    -1,    -1,
      94,    -1,    28,    -1,    30,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    38,    39,    -1,    -1,    42,    -1,    -1,    45,
      -1,    -1,    -1,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    -1,    62,    63,    -1,    65,
      66,    67,    68,    69,    70,    71,     4,    -1,    -1,     7,
      -1,    -1,    10,    11,    12,    13,    -1,    -1,    16,    -1,
      -1,    -1,    -1,    -1,    -1,    23,    -1,    -1,    94,    -1,
      28,    -1,    30,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      38,    39,    -1,    -1,    42,    -1,    -1,    45,    -1,    -1,
      -1,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    -1,    62,    63,    -1,    65,    66,    67,
      68,    69,    70,    71,     4,    -1,    -1,     7,    -1,    -1,
      10,    11,    12,    13,    -1,    -1,    16,    -1,    -1,    -1,
      -1,    -1,    -1,    23,    -1,    -1,    94,    -1,    28,    -1,
      30,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    38,    39,
      -1,    -1,    42,    -1,    -1,    45,    -1,    -1,    -1,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    -1,    62,    63,    -1,    65,    66,    67,    68,    69,
      70,    71,     4,    -1,    -1,     7,    -1,    -1,    10,    11,
      12,    13,    -1,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    23,    -1,    -1,    94,    -1,    28,    -1,    30,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    38,    39,    -1,    -1,
      42,    -1,    -1,    45,    -1,    -1,    -1,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    -1,
      62,    63,    -1,    65,    66,    67,    68,    69,    70,    71,
       4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,    13,
      -1,    -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    23,
      -1,    -1,    94,    -1,    28,    -1,    30,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    38,    39,    -1,    -1,    42,    -1,
      -1,    45,    -1,    -1,    -1,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    -1,    62,    63,
      -1,    65,    66,    67,    68,    69,    70,    71,     4,    -1,
      -1,     7,    -1,    -1,    10,    11,    12,    13,    -1,    -1,
      16,    -1,    -1,    -1,    -1,    -1,    -1,    23,    -1,    -1,
      94,    -1,    28,    -1,    30,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    38,    39,    -1,    -1,    42,    -1,    -1,    45,
      -1,    -1,    -1,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    -1,    62,    63,    -1,    65,
      66,    67,    68,    69,    70,    71,     4,    -1,    -1,     7,
      -1,    -1,    10,    11,    12,    13,    -1,    -1,    16,    -1,
      -1,    -1,    -1,    -1,    -1,    23,    -1,    -1,    94,    -1,
      28,    -1,    30,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      38,    39,    -1,    -1,    42,    -1,    -1,    45,    -1,    -1,
      -1,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    -1,    62,    63,    -1,    65,    66,    67,
      68,    69,    70,    71,     4,    -1,    -1,     7,    -1,    -1,
      10,    11,    12,    13,    -1,    -1,    16,    -1,    -1,    -1,
      -1,    -1,    -1,    23,    -1,    -1,    94,    -1,    28,    -1,
      30,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    38,    39,
      -1,    -1,    42,    -1,    -1,    45,    -1,    -1,    -1,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    -1,    62,    63,    -1,    65,    66,    67,    68,    69,
      70,    71,     4,    -1,    -1,     7,    -1,    -1,    10,    11,
      12,    13,    -1,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    23,    -1,    -1,    94,    -1,    28,    -1,    30,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    38,    39,    -1,    -1,
      42,    -1,    -1,    45,    -1,    -1,    -1,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    -1,
      62,    63,    -1,    65,    66,    67,    68,    69,    70,    71,
       4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,    13,
      -1,    -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    23,
      -1,    -1,    94,    -1,    28,    -1,    30,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    38,    39,    -1,    -1,    42,    -1,
      -1,    45,    -1,    -1,    -1,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    -1,    62,    63,
      -1,    65,    66,    67,    68,    69,    70,    71,     4,    -1,
      -1,     7,    -1,    -1,    10,    11,    12,    13,    -1,    -1,
      16,    -1,    -1,    -1,    -1,    -1,    -1,    23,    -1,    -1,
      94,    -1,    28,    -1,    30,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    38,    39,    -1,    -1,    42,    -1,    -1,    45,
      -1,    -1,    -1,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    -1,    62,    63,    -1,    65,
      66,    67,    68,    69,    70,    71,     4,    -1,    -1,     7,
      -1,    -1,    10,    11,    12,    13,    -1,    -1,    16,    -1,
      -1,    -1,    -1,    -1,    -1,    23,    -1,    -1,    94,    -1,
      28,    -1,    30,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      38,    39,    -1,    -1,    42,    -1,    -1,    45,    -1,    -1,
      -1,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    -1,    62,    63,    -1,    65,    66,    67,
      68,    69,    70,    71,     4,    -1,    -1,     7,    -1,    -1,
      10,    11,    12,    13,    -1,    -1,    16,    -1,    -1,    -1,
      -1,    -1,    -1,    23,    -1,    -1,    94,    -1,    28,    -1,
      30,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    38,    39,
      -1,    -1,    42,    -1,    -1,    45,    -1,    -1,    -1,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    -1,    62,    63,    -1,    65,    66,    67,    68,    69,
      70,    71,     4,    -1,    -1,     7,    -1,    -1,    10,    11,
      12,    13,    -1,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    23,    -1,    -1,    94,    -1,    28,    -1,    30,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    38,    39,    -1,    -1,
      42,    -1,    -1,    45,    -1,    -1,    -1,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    -1,
      62,    63,    -1,    65,    66,    67,    68,    69,    70,    71,
       4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,    13,
      -1,    -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    23,
      -1,    -1,    94,    -1,    28,    -1,    30,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    38,    39,    -1,    -1,    42,    -1,
      -1,    45,    -1,    -1,    -1,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    -1,    62,    63,
      -1,    65,    66,    67,    68,    69,    70,    71,     4,    -1,
      -1,     7,    -1,    -1,    10,    11,    12,    13,    -1,    -1,
      16,    -1,    -1,    -1,    -1,    -1,    -1,    23,    -1,    -1,
      94,    -1,    28,    -1,    30,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    38,    39,    -1,    -1,    42,    -1,    -1,    45,
      -1,    -1,    -1,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    -1,    62,    63,    -1,    65,
      66,    67,    68,    69,    70,    71,     4,    -1,    -1,     7,
      -1,    -1,    10,    11,    12,    13,    -1,    -1,    16,    -1,
      -1,    -1,    -1,    -1,    -1,    23,    -1,    -1,    94,    -1,
      28,    -1,    30,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      38,    39,    -1,    -1,    42,    -1,    -1,    45,    -1,    -1,
      -1,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    -1,    62,    63,    -1,    65,    66,    67,
      68,    69,    70,    71,     4,    -1,    -1,     7,    -1,    -1,
      10,    11,    12,    13,    -1,    -1,    16,    -1,    -1,    -1,
      -1,    -1,    -1,    23,    -1,    -1,    94,    -1,    28,    -1,
      30,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    38,    39,
      -1,    -1,    42,    -1,    -1,    45,    -1,    -1,    -1,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    -1,    62,    63,    -1,    65,    66,    67,    68,    69,
      70,    71,     4,    -1,    -1,     7,    -1,    -1,    10,    -1,
      -1,    -1,    -1,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    23,    -1,    -1,    94,    -1,    28,    -1,    30,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    38,    39,    -1,    -1,
      42,    -1,    -1,    45,    -1,    -1,    -1,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,     4,
      62,    63,     7,    65,    66,    67,    68,    69,    70,    71,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    23,     4,
      -1,    -1,     7,    28,    -1,    30,    -1,    -1,    -1,    -1,
      -1,    -1,    94,    38,    -1,    -1,    -1,    -1,    23,    -1,
      -1,    -1,    -1,    28,    -1,    30,    51,    52,    -1,    54,
      55,    56,    -1,    38,    -1,    -1,    -1,    62,    63,    -1,
      65,    -1,    -1,    -1,    69,    70,    51,    52,    -1,    54,
      55,    56,     4,    -1,    -1,     7,    -1,    62,    63,    -1,
      65,    -1,    -1,    -1,    69,    70,    -1,    -1,    -1,    94,
      -1,    -1,     4,    -1,    -1,     7,    28,    -1,    30,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    38,    -1,    -1,    94,
      -1,    -1,    -1,    -1,    -1,    -1,    28,    -1,    30,    51,
      52,    -1,    54,    55,    56,    -1,    38,    -1,    -1,    -1,
      62,    63,    -1,    65,    -1,    -1,    -1,    69,    70,    51,
      52,    -1,    54,    55,    56,    -1,    -1,    -1,    -1,    -1,
      62,    63,    -1,    65,    -1,    -1,    -1,    69,    70,     4,
       5,    -1,    94,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      15,    16,    -1,    -1,    -1,    -1,    -1,    -1,    23,    -1,
      25,    26,    94,    28,    -1,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    -1,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,     4,     5,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    15,    16,
      -1,    -1,    87,    -1,    -1,    -1,    23,    -1,    25,    26,
      -1,    28,    -1,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    -1,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,    70,    71,     4,     5,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    15,    16,    -1,    -1,
      87,    -1,    -1,    -1,    23,    -1,    25,    26,    -1,    28,
      -1,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      -1,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,     4,     5,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    15,    16,    -1,    -1,    87,    -1,
      -1,    -1,    23,    -1,    25,    26,    -1,    28,    -1,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    -1,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,     4,     5,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    15,    16,    -1,    -1,    87,    -1,    -1,    -1,
      23,    -1,    25,    26,    -1,    28,    -1,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    -1,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,    71,     4,
       5,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      15,    16,    -1,    -1,    87,    -1,    -1,    -1,    23,    -1,
      25,    26,    -1,    28,    -1,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    -1,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,     4,    -1,    -1,
       7,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    15,    16,
      -1,    -1,    87,    -1,    -1,    -1,    23,    -1,    25,    26,
      -1,    28,    -1,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    -1,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,    70,    71,     4,    -1,    -1,     7,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    15,    16,    -1,    -1,
      -1,    -1,    -1,    -1,    23,    -1,    25,    26,    -1,    28,
      -1,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      -1,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,     4,    -1,    -1,     7,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    15,    16,    -1,    -1,    -1,    -1,
      -1,    -1,    23,    -1,    25,    26,    -1,    28,    -1,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    -1,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,     4,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    15,    16,    -1,    -1,    -1,    -1,    -1,    -1,
      23,    -1,    25,    26,    -1,    28,    -1,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    -1,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,    71,     4,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      15,    16,    -1,    -1,    -1,    -1,    -1,    -1,    23,    -1,
      25,    26,    -1,    28,    -1,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    -1,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,   100,   101,     0,     4,     7,    10,    11,    12,    13,
      16,    23,    28,    30,    38,    39,    42,    45,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      62,    63,    65,    66,    67,    68,    69,    70,    71,    94,
     102,   103,   106,   107,   112,   113,   114,   116,   117,   118,
     119,   124,   125,   126,   127,   128,   131,   132,   133,   135,
     138,   141,   142,   149,   150,   151,   152,   153,   154,   155,
     156,   157,   158,     4,    86,    86,     4,     4,     4,     4,
       3,     4,     5,     7,     8,     9,    31,    32,    33,    34,
      37,    38,    40,    41,    61,    65,    83,    86,    88,    90,
      98,   107,   162,   163,   164,   165,   166,   167,   168,   169,
     170,   171,   172,   174,   175,   178,    43,   162,   162,     4,
       4,    48,    94,   162,   162,     4,     4,     4,     7,   162,
     107,   107,     4,    86,   107,   134,   148,   162,    46,    61,
       4,    65,    98,   162,     4,    28,    43,    63,   110,   111,
     129,     4,     4,     4,     5,     4,     5,    66,    94,     8,
      17,    18,    19,    20,    72,    88,    90,    98,   105,   108,
      93,    94,    94,    94,    94,    94,    94,    94,    94,    94,
      94,    94,    94,    94,    86,   162,   179,   180,   179,    86,
      86,    62,    94,     4,    86,   171,   166,   162,   172,     4,
      86,   171,   162,   190,   190,     4,    15,    16,    23,    25,
      26,    28,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,   187,    86,    98,   176,    25,    23,    36,
      35,    21,    22,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,   108,   173,    82,    83,    84,    85,    88,
      95,    98,     3,    65,    86,    47,    72,    92,    93,     4,
     101,    94,    94,    86,    86,    86,   134,    92,    94,    98,
     162,    94,     4,    30,    55,    64,    55,    64,     4,    86,
     111,    42,    86,    94,     4,    15,     4,   129,   162,   162,
     109,     4,     7,   187,   162,    72,   105,     4,   104,     4,
       5,    87,   185,   187,   188,    87,    92,    87,    87,   188,
      87,   188,     4,   139,   179,    39,    86,    87,    87,    89,
      94,   162,   181,    91,   188,   179,     7,   187,     4,    94,
     103,   107,   112,   113,   128,   150,   151,   152,   154,   155,
     156,   157,   161,   164,   165,   166,   169,   169,   173,   168,
     170,   170,   171,   171,   162,   186,   187,     4,     7,   187,
       4,   162,   162,   162,   162,     4,   104,    47,    92,    93,
      46,   101,    24,   120,     4,   182,   184,   182,   134,    87,
     148,   101,     4,    42,   101,     4,    63,     4,    63,    86,
     182,     4,   130,   182,   101,     5,     4,     5,    42,    89,
      92,     6,    86,    86,   162,   162,    86,    72,    86,    94,
      72,    93,    87,    92,   162,    94,    87,    94,    87,    94,
      98,    28,    53,    94,   124,   140,   187,    87,   175,   179,
      92,   190,   190,    87,    86,    86,   177,   101,    93,    94,
      26,   168,    89,    92,    91,    92,    86,    86,    87,    94,
      43,    47,    47,    92,   162,     4,   104,   162,    28,   162,
      24,    27,   121,    72,    93,    87,    92,    87,    87,    94,
      28,   162,    28,   179,    87,    94,    87,    28,    15,   130,
     180,    91,   179,   179,     4,   162,     4,   189,   137,   162,
     162,    94,   190,   143,    43,    94,    94,    14,     4,     4,
      72,    86,    93,    87,   190,    89,    91,   179,   179,    26,
      28,   159,    26,   160,   163,   180,   187,   179,   179,    94,
     101,   162,   162,   162,     4,    94,    47,    47,    92,    94,
      49,    25,   162,    94,    29,     3,     5,    31,    32,    33,
      34,    82,    83,   183,   104,    93,    94,     4,    93,    94,
      94,   101,    57,    60,    87,   101,    94,    68,     4,    94,
      89,    87,    87,    87,   162,    87,   145,   136,     4,   185,
       4,    28,    62,    94,   144,     4,   143,    28,    94,    94,
     162,   189,   104,   162,    87,    87,    23,    94,   161,    23,
      23,    94,   161,    89,    87,    87,   101,    28,    63,   115,
      44,    94,    94,    94,    47,   101,   162,   162,     4,    94,
      94,    25,   122,    94,     3,     3,    72,   104,   101,    72,
      93,   104,   101,   101,    28,    94,    94,    28,   101,    94,
     101,    93,     4,    28,    62,    94,   146,   145,    86,    72,
      93,     4,     5,    43,    86,    94,     4,     4,     4,    28,
       4,    14,    94,    87,    72,    94,   162,   101,    94,    94,
     162,   101,    94,    28,    42,    94,   107,   162,   101,   101,
     101,   162,   115,    94,    94,    47,   122,    94,    16,    94,
     103,   112,   113,   114,   116,   117,   118,   119,   123,   124,
     125,   126,   127,   128,   131,   132,   133,   142,   149,   150,
     151,   152,   153,   154,   155,   156,   157,   158,   183,    94,
      28,   183,   104,    94,    28,    28,    57,    66,    28,    28,
     162,     4,     5,   147,     4,     4,    28,   189,   162,   162,
       5,    43,    86,    94,    94,     4,    87,   188,   143,    94,
      98,    94,     4,    94,    28,    72,    93,   162,    25,    28,
      25,    28,    39,    94,    94,    94,   115,   115,   115,    94,
     101,   101,   162,   122,    94,    94,    94,    94,    94,    94,
      94,    94,    94,    94,    94,    94,    94,    94,   101,    53,
      72,   101,    53,    57,    94,    94,    67,    66,    86,    86,
      92,    94,    94,    94,     4,    87,    94,     4,    87,   188,
     143,   143,     4,    94,    87,    28,     4,   143,    94,     4,
     162,   104,    94,    94,    23,    94,   161,    23,    94,   101,
     101,   115,   115,    94,    28,    94,   183,    28,    94,    94,
      94,    94,    87,   188,   182,     5,   101,    94,    93,   143,
       4,    94,    87,    28,    28,    94,    94,     4,    94,    28,
      94,    94,    72,    94,   101,    94,   101,    94,    94,   115,
     115,   101,    53,    53,    94,    87,    87,    28,   162,    28,
      94,    94,     4,     4,   143,   143,    94,   101,     4,   162,
     159,   159,   160,   115,    94,    94,   145,    94,    94,    62,
       4,   143,    94,    94,    28,    28,    28,    94,    94,    28,
     145,   101,    94,    94,    28,     4,     4,    62,     4,    28,
      28,     4,    94,    94,    94,    94,     4,     4,    94,    94,
      94
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    99,   100,   101,   101,   101,   102,   102,   102,   102,
     102,   102,   102,   102,   102,   102,   102,   102,   102,   102,
     102,   102,   102,   102,   102,   102,   102,   102,   102,   102,
     102,   102,   102,   102,   102,   102,   102,   103,   103,   103,
     103,   103,   104,   104,   105,   105,   105,   105,   106,   106,
     106,   106,   107,   107,   107,   109,   108,   108,   110,   110,
     111,   111,   111,   111,   112,   112,   113,   113,   113,   113,
     113,   113,   114,   115,   115,   115,   116,   116,   116,   116,
     116,   116,   116,   116,   116,   116,   117,   118,   119,   120,
     120,   121,   121,   122,   122,   122,   123,   123,   123,   123,
     123,   123,   123,   123,   123,   123,   123,   123,   123,   123,
     123,   123,   123,   123,   123,   123,   123,   123,   123,   123,
     123,   123,   123,   123,   124,   124,   124,   124,   125,   125,
     126,   127,   128,   128,   128,   128,   128,   128,   128,   128,
     129,   129,   130,   131,   131,   131,   132,   133,   133,   134,
     134,   136,   135,   137,   135,   138,   139,   139,   139,   139,
     140,   140,   140,   140,   140,   140,   141,   141,   141,   141,
     142,   142,   142,   143,   143,   143,   144,   144,   144,   144,
     144,   144,   144,   144,   144,   144,   144,   145,   145,   145,
     146,   146,   146,   146,   146,   147,   147,   148,   148,   149,
     150,   150,   150,   150,   150,   150,   150,   150,   151,   152,
     152,   153,   154,   155,   156,   156,   157,   157,   158,   158,
     159,   159,   159,   159,   160,   160,   160,   160,   160,   161,
     161,   161,   161,   161,   161,   161,   161,   161,   161,   161,
     162,   162,   163,   163,   164,   164,   165,   165,   166,   166,
     167,   167,   167,   168,   168,   168,   169,   169,   169,   170,
     170,   170,   171,   171,   171,   171,   171,   171,   172,   172,
     172,   172,   172,   172,   172,   173,   173,   173,   173,   173,
     173,   173,   173,   173,   173,   174,   174,   174,   174,   174,
     174,   174,   174,   174,   174,   174,   174,   174,   174,   174,
     174,   174,   175,   175,   176,   176,   176,   176,   177,   177,
     178,   178,   179,   179,   180,   180,   181,   181,   182,   182,
     183,   183,   183,   183,   183,   183,   183,   183,   184,   184,
     184,   184,   184,   184,   184,   184,   185,   185,   186,   186,
     187,   187,   187,   187,   187,   187,   187,   187,   187,   187,
     187,   187,   187,   187,   187,   187,   187,   187,   187,   187,
     187,   187,   187,   187,   187,   187,   187,   187,   187,   187,
     187,   187,   187,   187,   187,   187,   187,   187,   187,   187,
     187,   187,   187,   187,   187,   187,   187,   187,   188,   188,
     188,   188,   188,   188,   189,   189,   190,   190
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     2,     1,     1,     2,     2,     1,     2,     2,     2,
       2,     2,     2,     2,     2,     1,     1,     3,     3,     4,
       5,     4,     1,     4,     1,     1,     1,     1,     1,     4,
       6,     3,     1,     1,     1,     0,     4,     1,     1,     2,
       1,     1,     1,     1,     2,     4,     4,     2,     4,     6,
       6,     6,    10,     3,     2,     3,     7,     8,     9,    10,
       9,    10,    11,    12,     9,    11,     6,     7,     7,     5,
       6,     0,     3,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     2,
       1,     2,     2,     1,     2,     2,     2,     2,     2,     2,
       2,     2,     1,     1,    10,    10,    12,    12,     9,    10,
      10,     7,     2,     2,     2,     2,     4,     4,     4,     6,
       1,     4,     1,     9,     7,    10,     2,     3,     5,     1,
       3,     0,    11,     0,    10,     7,     0,     2,     2,     2,
       4,     6,     4,     7,     9,     7,     6,     5,     9,    11,
      10,     9,    13,     0,     2,     2,     4,     6,     5,     6,
       9,     7,     8,     7,     9,    10,     9,     0,     2,     2,
       3,    10,    10,     9,     7,     1,     3,     1,     3,     7,
       4,     4,     3,     4,     4,     3,     3,     3,     2,     1,
       2,     2,     2,     2,     1,     2,     1,     2,     6,     6,
       3,     3,     6,     7,     0,     3,     6,     7,     7,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     2,     1,     5,     1,     3,     1,     3,     1,     2,
       1,     3,     4,     1,     3,     3,     1,     3,     3,     1,
       3,     3,     1,     2,     2,     2,     4,     5,     1,     4,
       6,     3,     4,     6,     6,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     2,     1,     3,     1,
       1,     2,     4,     1,     1,     1,     1,     1,     3,     3,
       5,     1,     3,     5,     0,     3,     3,     5,     0,     3,
       2,     3,     0,     1,     1,     3,     1,     4,     0,     1,
       1,     2,     2,     1,     1,     1,     1,     1,     1,     3,
       3,     3,     5,     5,     7,     5,     1,     1,     1,     3,
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
#line 1031 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3478 "src/parser.tab.c"
        break;

    case YYSYMBOL_STRING: /* STRING  */
#line 1031 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3484 "src/parser.tab.c"
        break;

    case YYSYMBOL_LENS_CONTENT: /* LENS_CONTENT  */
#line 1031 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3490 "src/parser.tab.c"
        break;

    case YYSYMBOL_QUALIFIED_IDENT: /* QUALIFIED_IDENT  */
#line 1031 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3496 "src/parser.tab.c"
        break;

    case YYSYMBOL_MODIFIER_PREFIX: /* MODIFIER_PREFIX  */
#line 1031 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3502 "src/parser.tab.c"
        break;

    case YYSYMBOL_MODIFIER_VALUE: /* MODIFIER_VALUE  */
#line 1031 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3508 "src/parser.tab.c"
        break;

    case YYSYMBOL_SQL_TEXT: /* SQL_TEXT  */
#line 1031 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3514 "src/parser.tab.c"
        break;

    case YYSYMBOL_program: /* program  */
#line 1056 "src/parser.y"
            { (void) ((*yyvaluep).stmt_list); }
#line 3520 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement_list: /* statement_list  */
#line 989 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3526 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement: /* statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3532 "src/parser.tab.c"
        break;

    case YYSYMBOL_assignment: /* assignment  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3538 "src/parser.tab.c"
        break;

    case YYSYMBOL_type_lock: /* type_lock  */
#line 1044 "src/parser.y"
            { ast_type_lock_free(((*yyvaluep).type_lock)); }
#line 3544 "src/parser.tab.c"
        break;

    case YYSYMBOL_lvalue: /* lvalue  */
#line 1032 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3550 "src/parser.tab.c"
        break;

    case YYSYMBOL_variable_name: /* variable_name  */
#line 1031 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3556 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_lens: /* comparison_lens  */
#line 1038 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 3562 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_name: /* modifier_name  */
#line 1031 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3568 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_word: /* modifier_word  */
#line 1031 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3574 "src/parser.tab.c"
        break;

    case YYSYMBOL_print_statement: /* print_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3580 "src/parser.tab.c"
        break;

    case YYSYMBOL_call_statement: /* call_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3586 "src/parser.tab.c"
        break;

    case YYSYMBOL_with_lock_statement: /* with_lock_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3592 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_end: /* for_end  */
#line 1031 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3598 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_each_statement: /* for_each_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3604 "src/parser.tab.c"
        break;

    case YYSYMBOL_do_loop_statement: /* do_loop_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3610 "src/parser.tab.c"
        break;

    case YYSYMBOL_while_statement: /* while_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3616 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement: /* consider_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3622 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_branch_list: /* consider_branch_list  */
#line 1036 "src/parser.y"
            { ast_free_consider_branch_list(((*yyvaluep).consider_branch_list)); }
#line 3628 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_else_opt: /* consider_else_opt  */
#line 989 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3634 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement_list: /* consider_statement_list  */
#line 989 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3640 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_body_statement: /* consider_body_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3646 "src/parser.tab.c"
        break;

    case YYSYMBOL_function_statement: /* function_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3652 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_statement: /* modifier_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3658 "src/parser.tab.c"
        break;

    case YYSYMBOL_program_statement: /* program_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3664 "src/parser.tab.c"
        break;

    case YYSYMBOL_library_statement: /* library_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3670 "src/parser.tab.c"
        break;

    case YYSYMBOL_use_statement: /* use_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3676 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_signature: /* modifier_signature  */
#line 1039 "src/parser.y"
            { ast_free_modifier_signature(((*yyvaluep).modifier_signature)); }
#line 3682 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_context: /* modifier_context  */
#line 1031 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3688 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_statement: /* watch_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3694 "src/parser.tab.c"
        break;

    case YYSYMBOL_unwatch_statement: /* unwatch_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3700 "src/parser.tab.c"
        break;

    case YYSYMBOL_wait_statement: /* wait_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3706 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_list: /* watch_target_list  */
#line 1037 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3712 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_statement: /* server_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3718 "src/parser.tab.c"
        break;

    case YYSYMBOL_shape_block: /* shape_block  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3724 "src/parser.tab.c"
        break;

    case YYSYMBOL_shape_body: /* shape_body  */
#line 1046 "src/parser.y"
            { ast_free_shape_body(((*yyvaluep).shape_body)); }
#line 3730 "src/parser.tab.c"
        break;

    case YYSYMBOL_shape_field: /* shape_field  */
#line 1045 "src/parser.y"
            { ast_free_shape_field(((*yyvaluep).shape_field)); }
#line 3736 "src/parser.tab.c"
        break;

    case YYSYMBOL_gsql_block: /* gsql_block  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3742 "src/parser.tab.c"
        break;

    case YYSYMBOL_window_block: /* window_block  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3748 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item_list: /* server_item_list  */
#line 1043 "src/parser.y"
            { ast_free_server_item_list(((*yyvaluep).server_item_list)); }
#line 3754 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item: /* server_item  */
#line 1042 "src/parser.y"
            { AstServerItemList one = ast_server_item_list_append(ast_server_item_list_empty(), ((*yyvaluep).server_item)); ast_free_server_item_list(one); }
#line 3760 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_string_list: /* server_string_list  */
#line 1037 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3766 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_path: /* watch_target_path  */
#line 1031 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3772 "src/parser.tab.c"
        break;

    case YYSYMBOL_without_watchers_statement: /* without_watchers_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3778 "src/parser.tab.c"
        break;

    case YYSYMBOL_on_error_statement: /* on_error_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3784 "src/parser.tab.c"
        break;

    case YYSYMBOL_error_statement: /* error_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3790 "src/parser.tab.c"
        break;

    case YYSYMBOL_return_statement: /* return_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3796 "src/parser.tab.c"
        break;

    case YYSYMBOL_label_statement: /* label_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3802 "src/parser.tab.c"
        break;

    case YYSYMBOL_goto_statement: /* goto_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3808 "src/parser.tab.c"
        break;

    case YYSYMBOL_gosub_statement: /* gosub_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3814 "src/parser.tab.c"
        break;

    case YYSYMBOL_break_statement: /* break_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3820 "src/parser.tab.c"
        break;

    case YYSYMBOL_continue_statement: /* continue_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3826 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_statement: /* if_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3832 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_block_tail: /* if_block_tail  */
#line 989 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3838 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_inline_tail: /* if_inline_tail  */
#line 989 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3844 "src/parser.tab.c"
        break;

    case YYSYMBOL_inline_statement: /* inline_statement  */
#line 1033 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3850 "src/parser.tab.c"
        break;

    case YYSYMBOL_expression: /* expression  */
#line 1032 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3856 "src/parser.tab.c"
        break;

    case YYSYMBOL_conditional_expression: /* conditional_expression  */
#line 1032 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3862 "src/parser.tab.c"
        break;

    case YYSYMBOL_or_expression: /* or_expression  */
#line 1032 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3868 "src/parser.tab.c"
        break;

    case YYSYMBOL_and_expression: /* and_expression  */
#line 1032 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3874 "src/parser.tab.c"
        break;

    case YYSYMBOL_not_expression: /* not_expression  */
#line 1032 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3880 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_expression: /* comparison_expression  */
#line 1032 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3886 "src/parser.tab.c"
        break;

    case YYSYMBOL_set_expression: /* set_expression  */
#line 1032 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3892 "src/parser.tab.c"
        break;

    case YYSYMBOL_additive_expression: /* additive_expression  */
#line 1032 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3898 "src/parser.tab.c"
        break;

    case YYSYMBOL_multiplicative_expression: /* multiplicative_expression  */
#line 1032 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3904 "src/parser.tab.c"
        break;

    case YYSYMBOL_unary_expression: /* unary_expression  */
#line 1032 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3910 "src/parser.tab.c"
        break;

    case YYSYMBOL_postfix_expression: /* postfix_expression  */
#line 1032 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3916 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_operator: /* comparison_operator  */
#line 1031 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3922 "src/parser.tab.c"
        break;

    case YYSYMBOL_primary: /* primary  */
#line 1032 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3928 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_literal: /* record_literal  */
#line 1032 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3934 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_suffix: /* ident_suffix  */
#line 1040 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3940 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_dot_suffix: /* ident_dot_suffix  */
#line 1040 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3946 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list_opt: /* argument_list_opt  */
#line 1034 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3952 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list: /* argument_list  */
#line 1034 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3958 "src/parser.tab.c"
        break;

    case YYSYMBOL_array_argument_list: /* array_argument_list  */
#line 1034 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3964 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list_opt: /* parameter_list_opt  */
#line 1037 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3970 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_default: /* parameter_default  */
#line 1032 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3976 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list: /* parameter_list  */
#line 1037 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3982 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_name: /* field_name  */
#line 1031 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3988 "src/parser.tab.c"
        break;

    case YYSYMBOL_projection_names: /* projection_names  */
#line 1037 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3994 "src/parser.tab.c"
        break;

    case YYSYMBOL_dot_field_name: /* dot_field_name  */
#line 1031 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 4000 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_field_list: /* record_field_list  */
#line 1035 "src/parser.y"
            { ast_free_record_field_list(((*yyvaluep).record_field_list)); }
#line 4006 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_policy: /* field_policy  */
#line 1041 "src/parser.y"
            { ast_free_expr(((*yyvaluep).field_policy).reset_expr); }
#line 4012 "src/parser.tab.c"
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
#line 745 "src/parser.y"
{ (void)yynerrs; }

#line 4112 "src/parser.tab.c"

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
#line 1061 "src/parser.y"
                     { ctx->parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 4325 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 1065 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4331 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 1066 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 4337 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 1067 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 4343 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 1071 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4349 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 1072 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4355 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 1073 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4361 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 1074 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4367 "src/parser.tab.c"
    break;

  case 10: /* statement: window_block  */
#line 1075 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4373 "src/parser.tab.c"
    break;

  case 11: /* statement: gsql_block  */
#line 1076 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4379 "src/parser.tab.c"
    break;

  case 12: /* statement: shape_block  */
#line 1077 "src/parser.y"
                  { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4385 "src/parser.tab.c"
    break;

  case 13: /* statement: for_each_statement  */
#line 1078 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4391 "src/parser.tab.c"
    break;

  case 14: /* statement: while_statement  */
#line 1079 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4397 "src/parser.tab.c"
    break;

  case 15: /* statement: do_loop_statement  */
#line 1080 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4403 "src/parser.tab.c"
    break;

  case 16: /* statement: consider_statement  */
#line 1081 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4409 "src/parser.tab.c"
    break;

  case 17: /* statement: function_statement  */
#line 1082 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4415 "src/parser.tab.c"
    break;

  case 18: /* statement: modifier_statement  */
#line 1083 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4421 "src/parser.tab.c"
    break;

  case 19: /* statement: program_statement  */
#line 1084 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4427 "src/parser.tab.c"
    break;

  case 20: /* statement: library_statement  */
#line 1085 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4433 "src/parser.tab.c"
    break;

  case 21: /* statement: use_statement NEWLINE  */
#line 1086 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4439 "src/parser.tab.c"
    break;

  case 22: /* statement: watch_statement  */
#line 1087 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4445 "src/parser.tab.c"
    break;

  case 23: /* statement: server_statement  */
#line 1088 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4451 "src/parser.tab.c"
    break;

  case 24: /* statement: unwatch_statement NEWLINE  */
#line 1089 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4457 "src/parser.tab.c"
    break;

  case 25: /* statement: wait_statement NEWLINE  */
#line 1090 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4463 "src/parser.tab.c"
    break;

  case 26: /* statement: without_watchers_statement  */
#line 1091 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4469 "src/parser.tab.c"
    break;

  case 27: /* statement: on_error_statement NEWLINE  */
#line 1092 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4475 "src/parser.tab.c"
    break;

  case 28: /* statement: error_statement NEWLINE  */
#line 1093 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4481 "src/parser.tab.c"
    break;

  case 29: /* statement: return_statement NEWLINE  */
#line 1094 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4487 "src/parser.tab.c"
    break;

  case 30: /* statement: label_statement NEWLINE  */
#line 1095 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4493 "src/parser.tab.c"
    break;

  case 31: /* statement: goto_statement NEWLINE  */
#line 1096 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4499 "src/parser.tab.c"
    break;

  case 32: /* statement: gosub_statement NEWLINE  */
#line 1097 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4505 "src/parser.tab.c"
    break;

  case 33: /* statement: break_statement NEWLINE  */
#line 1098 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4511 "src/parser.tab.c"
    break;

  case 34: /* statement: continue_statement NEWLINE  */
#line 1099 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4517 "src/parser.tab.c"
    break;

  case 35: /* statement: if_statement  */
#line 1100 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4523 "src/parser.tab.c"
    break;

  case 36: /* statement: DIM  */
#line 1106 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        GB_REFUSE;
      }
#line 4536 "src/parser.tab.c"
    break;

  case 37: /* assignment: lvalue OP_EQ expression  */
#line 1117 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 4542 "src/parser.tab.c"
    break;

  case 38: /* assignment: lvalue compound_op expression  */
#line 1122 "src/parser.y"
                                    { (yyval.stmt) = ast_assign_op((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr), (yyvsp[-1].op_char)); }
#line 4548 "src/parser.tab.c"
    break;

  case 39: /* assignment: lvalue comparison_lens compound_op expression  */
#line 1123 "src/parser.y"
                                                    {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            GB_REFUSE;
        }
        (yyval.stmt) = ast_assign_op((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr), (yyvsp[-1].op_char));
      }
#line 4562 "src/parser.tab.c"
    break;

  case 40: /* assignment: variable_name COLON type_lock OP_EQ expression  */
#line 1139 "src/parser.y"
                                                     {
        if (!type_lock_check(ctx, (yyvsp[-4].text), (yyvsp[-2].type_lock).name, (yyvsp[-2].type_lock).arg, (yylsp[-2]).first_line, (yylsp[-2]).first_column,
                             (yylsp[-2]).last_line, (yylsp[-2]).last_column)) {
            GB_REFUSE;
        }
        (yyval.stmt) = ast_assign_locked(expr_at(ast_ident((yyvsp[-4].text)), (yylsp[-4]).first_line, (yylsp[-4]).first_column), (yyvsp[-2].type_lock), (yyvsp[0].expr));
      }
#line 4574 "src/parser.tab.c"
    break;

  case 41: /* assignment: lvalue comparison_lens OP_EQ expression  */
#line 1146 "src/parser.y"
                                              {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            GB_REFUSE;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 4588 "src/parser.tab.c"
    break;

  case 42: /* type_lock: IDENT  */
#line 1160 "src/parser.y"
            { (yyval.type_lock) = ast_type_lock((yyvsp[0].text), NULL, (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4594 "src/parser.tab.c"
    break;

  case 43: /* type_lock: IDENT LPAREN IDENT RPAREN  */
#line 1161 "src/parser.y"
                                { (yyval.type_lock) = ast_type_lock((yyvsp[-3].text), (yyvsp[-1].text), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 4600 "src/parser.tab.c"
    break;

  case 44: /* compound_op: PLUS_EQ  */
#line 1165 "src/parser.y"
               { (yyval.op_char) = '+'; }
#line 4606 "src/parser.tab.c"
    break;

  case 45: /* compound_op: MINUS_EQ  */
#line 1166 "src/parser.y"
               { (yyval.op_char) = '-'; }
#line 4612 "src/parser.tab.c"
    break;

  case 46: /* compound_op: STAR_EQ  */
#line 1167 "src/parser.y"
               { (yyval.op_char) = '*'; }
#line 4618 "src/parser.tab.c"
    break;

  case 47: /* compound_op: SLASH_EQ  */
#line 1168 "src/parser.y"
               { (yyval.op_char) = '/'; }
#line 4624 "src/parser.tab.c"
    break;

  case 48: /* lvalue: variable_name  */
#line 1172 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4630 "src/parser.tab.c"
    break;

  case 49: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 1173 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4636 "src/parser.tab.c"
    break;

  case 50: /* lvalue: lvalue LBRACKET expression COMMA argument_list RBRACKET  */
#line 1177 "src/parser.y"
                                                                           {
        AstExprList items = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[-3].expr));
        for (size_t i = 0; i < (yyvsp[-1].expr_list).count; i++) {
            items = ast_expr_list_append(items, (yyvsp[-1].expr_list).items[i]);
        }
        free((yyvsp[-1].expr_list).items);
        (yyval.expr) = expr_at(ast_index_list((yyvsp[-5].expr), items), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 4649 "src/parser.tab.c"
    break;

  case 51: /* lvalue: lvalue DOT dot_field_name  */
#line 1185 "src/parser.y"
                                             { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4655 "src/parser.tab.c"
    break;

  case 52: /* variable_name: IDENT  */
#line 1189 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 4661 "src/parser.tab.c"
    break;

  case 53: /* variable_name: END  */
#line 1190 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 4667 "src/parser.tab.c"
    break;

  case 54: /* variable_name: NEXT  */
#line 1191 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 4673 "src/parser.tab.c"
    break;

  case 55: /* $@1: %empty  */
#line 1202 "src/parser.y"
             { lexer_begin_lens_content(ctx->active_lexer); }
#line 4679 "src/parser.tab.c"
    break;

  case 56: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 1202 "src/parser.y"
                                                                                  {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 4687 "src/parser.tab.c"
    break;

  case 57: /* comparison_lens: MODIFIER_PREFIX  */
#line 1210 "src/parser.y"
                      { (yyval.modifier) = parse_modifier_use((yyvsp[0].text)); }
#line 4693 "src/parser.tab.c"
    break;

  case 58: /* modifier_name: modifier_word  */
#line 1214 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 4699 "src/parser.tab.c"
    break;

  case 59: /* modifier_name: modifier_name modifier_word  */
#line 1215 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 4705 "src/parser.tab.c"
    break;

  case 60: /* modifier_word: IDENT  */
#line 1219 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4711 "src/parser.tab.c"
    break;

  case 61: /* modifier_word: TO  */
#line 1220 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 4717 "src/parser.tab.c"
    break;

  case 62: /* modifier_word: END  */
#line 1221 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 4723 "src/parser.tab.c"
    break;

  case 63: /* modifier_word: NEXT  */
#line 1222 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 4729 "src/parser.tab.c"
    break;

  case 64: /* print_statement: PRINT expression  */
#line 1226 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 4735 "src/parser.tab.c"
    break;

  case 65: /* print_statement: PRINT TO ERROR_VALUE expression  */
#line 1232 "src/parser.y"
                                      { (yyval.stmt) = ast_print_error((yyvsp[0].expr)); }
#line 4741 "src/parser.tab.c"
    break;

  case 66: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 1236 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 4747 "src/parser.tab.c"
    break;

  case 67: /* call_statement: TRY expression  */
#line 1239 "src/parser.y"
                     { (yyval.stmt) = ast_expr_stmt(expr_at(ast_unary(copy_const("try"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column)); }
#line 4753 "src/parser.tab.c"
    break;

  case 68: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1240 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 4764 "src/parser.tab.c"
    break;

  case 69: /* call_statement: lvalue DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1246 "src/parser.y"
                                                       {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 4774 "src/parser.tab.c"
    break;

  case 70: /* call_statement: lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1251 "src/parser.y"
                                                                 {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 4788 "src/parser.tab.c"
    break;

  case 71: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1260 "src/parser.y"
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
#line 4803 "src/parser.tab.c"
    break;

  case 72: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 1273 "src/parser.y"
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
            GB_REFUSE;
        }
        free((yyvsp[-8].text));
        (yyval.stmt) = is_lock ? ast_with_lock((yyvsp[-6].expr), (yyvsp[-3].stmt_list)) : ast_with_principal((yyvsp[-6].expr), (yyvsp[-3].stmt_list));
      }
#line 4826 "src/parser.tab.c"
    break;

  case 73: /* for_end: END FOR NEWLINE  */
#line 1302 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 4832 "src/parser.tab.c"
    break;

  case 74: /* for_end: NEXT NEWLINE  */
#line 1303 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 4838 "src/parser.tab.c"
    break;

  case 75: /* for_end: NEXT variable_name NEWLINE  */
#line 1304 "src/parser.y"
                                 { (yyval.text) = (yyvsp[-1].text); }
#line 4844 "src/parser.tab.c"
    break;

  case 76: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list for_end  */
#line 1308 "src/parser.y"
                                                             {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { GB_REFUSE; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4853 "src/parser.tab.c"
    break;

  case 77: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list for_end  */
#line 1312 "src/parser.y"
                                                                  {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { GB_REFUSE; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4862 "src/parser.tab.c"
    break;

  case 78: /* for_each_statement: FOR IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1328 "src/parser.y"
                                                                         {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { GB_REFUSE; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { GB_REFUSE; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4872 "src/parser.tab.c"
    break;

  case 79: /* for_each_statement: FOR EACH IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1333 "src/parser.y"
                                                                              {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { GB_REFUSE; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { GB_REFUSE; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4882 "src/parser.tab.c"
    break;

  case 80: /* for_each_statement: FOR IDENT COLON type_lock IN expression NEWLINE statement_list for_end  */
#line 1342 "src/parser.y"
                                                                             {
        if (!type_lock_check(ctx, (yyvsp[-7].text), (yyvsp[-5].type_lock).name, (yyvsp[-5].type_lock).arg, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                             (yylsp[-5]).last_line, (yylsp[-5]).last_column)) { GB_REFUSE; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { GB_REFUSE; }
        (yyval.stmt) = ast_for_each_locked((yyvsp[-7].text), (yyvsp[-5].type_lock), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4893 "src/parser.tab.c"
    break;

  case 81: /* for_each_statement: FOR EACH IDENT COLON type_lock IN expression NEWLINE statement_list for_end  */
#line 1348 "src/parser.y"
                                                                                  {
        if (!type_lock_check(ctx, (yyvsp[-7].text), (yyvsp[-5].type_lock).name, (yyvsp[-5].type_lock).arg, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                             (yylsp[-5]).last_line, (yylsp[-5]).last_column)) { GB_REFUSE; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { GB_REFUSE; }
        (yyval.stmt) = ast_for_each_locked((yyvsp[-7].text), (yyvsp[-5].type_lock), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4904 "src/parser.tab.c"
    break;

  case 82: /* for_each_statement: FOR IDENT COLON type_lock COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1354 "src/parser.y"
                                                                                         {
        if (!type_lock_check(ctx, (yyvsp[-9].text), (yyvsp[-7].type_lock).name, (yyvsp[-7].type_lock).arg, (yylsp[-7]).first_line, (yylsp[-7]).first_column,
                             (yylsp[-7]).last_line, (yylsp[-7]).last_column)) { GB_REFUSE; }
        if (!for_each_index_distinct(ctx, (yyvsp[-9].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { GB_REFUSE; }
        if (!for_end_matches(ctx, (yyvsp[-9].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { GB_REFUSE; }
        (yyval.stmt) = ast_for_each_locked((yyvsp[-9].text), (yyvsp[-7].type_lock), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4916 "src/parser.tab.c"
    break;

  case 83: /* for_each_statement: FOR EACH IDENT COLON type_lock COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1361 "src/parser.y"
                                                                                              {
        if (!type_lock_check(ctx, (yyvsp[-9].text), (yyvsp[-7].type_lock).name, (yyvsp[-7].type_lock).arg, (yylsp[-7]).first_line, (yylsp[-7]).first_column,
                             (yylsp[-7]).last_line, (yylsp[-7]).last_column)) { GB_REFUSE; }
        if (!for_each_index_distinct(ctx, (yyvsp[-9].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { GB_REFUSE; }
        if (!for_end_matches(ctx, (yyvsp[-9].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { GB_REFUSE; }
        (yyval.stmt) = ast_for_each_locked((yyvsp[-9].text), (yyvsp[-7].type_lock), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4928 "src/parser.tab.c"
    break;

  case 84: /* for_each_statement: FOR IDENT OP_EQ expression TO expression NEWLINE statement_list for_end  */
#line 1371 "src/parser.y"
                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { GB_REFUSE; }
        (yyval.stmt) = ast_for_range((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].expr), NULL, (yyvsp[-1].stmt_list));
      }
#line 4937 "src/parser.tab.c"
    break;

  case 85: /* for_each_statement: FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list for_end  */
#line 1375 "src/parser.y"
                                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-9].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { GB_REFUSE; }
        (yyval.stmt) = ast_for_range((yyvsp[-9].text), (yyvsp[-7].expr), (yyvsp[-5].expr), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4946 "src/parser.tab.c"
    break;

  case 86: /* do_loop_statement: DO NEWLINE statement_list UNTIL expression NEWLINE  */
#line 1396 "src/parser.y"
                                                         {
        (yyval.stmt) = ast_do_loop((yyvsp[-3].stmt_list), (yyvsp[-1].expr));
      }
#line 4954 "src/parser.tab.c"
    break;

  case 87: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 1402 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 4962 "src/parser.tab.c"
    break;

  case 88: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 1408 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 4970 "src/parser.tab.c"
    break;

  case 89: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 1414 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 4978 "src/parser.tab.c"
    break;

  case 90: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 1417 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 4986 "src/parser.tab.c"
    break;

  case 91: /* consider_else_opt: %empty  */
#line 1423 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4992 "src/parser.tab.c"
    break;

  case 92: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 1424 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 4998 "src/parser.tab.c"
    break;

  case 93: /* consider_statement_list: %empty  */
#line 1428 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 5004 "src/parser.tab.c"
    break;

  case 94: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 1429 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 5010 "src/parser.tab.c"
    break;

  case 95: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 1430 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 5016 "src/parser.tab.c"
    break;

  case 96: /* consider_body_statement: assignment NEWLINE  */
#line 1434 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 5022 "src/parser.tab.c"
    break;

  case 97: /* consider_body_statement: print_statement NEWLINE  */
#line 1435 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 5028 "src/parser.tab.c"
    break;

  case 98: /* consider_body_statement: call_statement NEWLINE  */
#line 1436 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 5034 "src/parser.tab.c"
    break;

  case 99: /* consider_body_statement: with_lock_statement  */
#line 1437 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5040 "src/parser.tab.c"
    break;

  case 100: /* consider_body_statement: window_block  */
#line 1438 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5046 "src/parser.tab.c"
    break;

  case 101: /* consider_body_statement: for_each_statement  */
#line 1439 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5052 "src/parser.tab.c"
    break;

  case 102: /* consider_body_statement: while_statement  */
#line 1440 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5058 "src/parser.tab.c"
    break;

  case 103: /* consider_body_statement: do_loop_statement  */
#line 1441 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5064 "src/parser.tab.c"
    break;

  case 104: /* consider_body_statement: consider_statement  */
#line 1442 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5070 "src/parser.tab.c"
    break;

  case 105: /* consider_body_statement: function_statement  */
#line 1443 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5076 "src/parser.tab.c"
    break;

  case 106: /* consider_body_statement: modifier_statement  */
#line 1444 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5082 "src/parser.tab.c"
    break;

  case 107: /* consider_body_statement: program_statement  */
#line 1445 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5088 "src/parser.tab.c"
    break;

  case 108: /* consider_body_statement: library_statement  */
#line 1446 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5094 "src/parser.tab.c"
    break;

  case 109: /* consider_body_statement: use_statement NEWLINE  */
#line 1447 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 5100 "src/parser.tab.c"
    break;

  case 110: /* consider_body_statement: watch_statement  */
#line 1448 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5106 "src/parser.tab.c"
    break;

  case 111: /* consider_body_statement: unwatch_statement NEWLINE  */
#line 1449 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 5112 "src/parser.tab.c"
    break;

  case 112: /* consider_body_statement: wait_statement NEWLINE  */
#line 1450 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 5118 "src/parser.tab.c"
    break;

  case 113: /* consider_body_statement: without_watchers_statement  */
#line 1451 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5124 "src/parser.tab.c"
    break;

  case 114: /* consider_body_statement: on_error_statement NEWLINE  */
#line 1452 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 5130 "src/parser.tab.c"
    break;

  case 115: /* consider_body_statement: error_statement NEWLINE  */
#line 1453 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 5136 "src/parser.tab.c"
    break;

  case 116: /* consider_body_statement: return_statement NEWLINE  */
#line 1454 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 5142 "src/parser.tab.c"
    break;

  case 117: /* consider_body_statement: label_statement NEWLINE  */
#line 1455 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 5148 "src/parser.tab.c"
    break;

  case 118: /* consider_body_statement: goto_statement NEWLINE  */
#line 1456 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 5154 "src/parser.tab.c"
    break;

  case 119: /* consider_body_statement: gosub_statement NEWLINE  */
#line 1457 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 5160 "src/parser.tab.c"
    break;

  case 120: /* consider_body_statement: break_statement NEWLINE  */
#line 1458 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 5166 "src/parser.tab.c"
    break;

  case 121: /* consider_body_statement: continue_statement NEWLINE  */
#line 1459 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 5172 "src/parser.tab.c"
    break;

  case 122: /* consider_body_statement: if_statement  */
#line 1460 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5178 "src/parser.tab.c"
    break;

  case 123: /* consider_body_statement: DIM  */
#line 1466 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        GB_REFUSE;
      }
#line 5191 "src/parser.tab.c"
    break;

  case 124: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1477 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 5199 "src/parser.tab.c"
    break;

  case 125: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1480 "src/parser.y"
                                                                                                            {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 5208 "src/parser.tab.c"
    break;

  case 126: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN COLON type_lock NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1487 "src/parser.y"
                                                                                                                  {
        char who[160];
        snprintf(who, sizeof who, "%s(...)", (yyvsp[-10].text));
        if (!type_lock_check(ctx, who, (yyvsp[-5].type_lock).name, (yyvsp[-5].type_lock).arg, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                             (yylsp[-5]).last_line, (yylsp[-5]).last_column)) {
            GB_REFUSE;
        }
        (yyval.stmt) = ast_function_returning((yyvsp[-10].text), (yyvsp[-8].name_list), (yyvsp[-5].type_lock), (yyvsp[-3].stmt_list));
      }
#line 5222 "src/parser.tab.c"
    break;

  case 127: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN COLON type_lock NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1496 "src/parser.y"
                                                                                                                            {
        char who[160];
        snprintf(who, sizeof who, "%s(...)", (yyvsp[-10].text));
        if (!type_lock_check(ctx, who, (yyvsp[-5].type_lock).name, (yyvsp[-5].type_lock).arg, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                             (yylsp[-5]).last_line, (yylsp[-5]).last_column)) {
            GB_REFUSE;
        }
        (yyval.stmt) = ast_function_returning((yyvsp[-10].text), (yyvsp[-8].name_list), (yyvsp[-5].type_lock), (yyvsp[-3].stmt_list));
      }
#line 5236 "src/parser.tab.c"
    break;

  case 128: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 1508 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 5244 "src/parser.tab.c"
    break;

  case 129: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 1511 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 5252 "src/parser.tab.c"
    break;

  case 130: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 1517 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 5260 "src/parser.tab.c"
    break;

  case 131: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 1523 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 5268 "src/parser.tab.c"
    break;

  case 132: /* use_statement: USE IDENT  */
#line 1529 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 5274 "src/parser.tab.c"
    break;

  case 133: /* use_statement: LOAD IDENT  */
#line 1530 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 5280 "src/parser.tab.c"
    break;

  case 134: /* use_statement: USE STRING  */
#line 1531 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 5286 "src/parser.tab.c"
    break;

  case 135: /* use_statement: LOAD STRING  */
#line 1532 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 5292 "src/parser.tab.c"
    break;

  case 136: /* use_statement: LOAD IDENT AS IDENT  */
#line 1533 "src/parser.y"
                          { (yyval.stmt) = ast_use((yyvsp[-2].text), NULL, (yyvsp[0].text)); }
#line 5298 "src/parser.tab.c"
    break;

  case 137: /* use_statement: USE IDENT IDENT STRING  */
#line 1534 "src/parser.y"
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
            GB_REFUSE;
        }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_use((yyvsp[-2].text), (yyvsp[0].text), NULL);
      }
#line 5319 "src/parser.tab.c"
    break;

  case 138: /* use_statement: LOAD IDENT IDENT STRING  */
#line 1550 "src/parser.y"
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
            GB_REFUSE;
        }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_use((yyvsp[-2].text), (yyvsp[0].text), NULL);
      }
#line 5340 "src/parser.tab.c"
    break;

  case 139: /* use_statement: LOAD IDENT IDENT STRING AS IDENT  */
#line 1566 "src/parser.y"
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
            GB_REFUSE;
        }
        free((yyvsp[-3].text));
        (yyval.stmt) = ast_use((yyvsp[-4].text), (yyvsp[-2].text), (yyvsp[0].text));
      }
#line 5363 "src/parser.tab.c"
    break;

  case 140: /* modifier_signature: modifier_name  */
#line 1587 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 5369 "src/parser.tab.c"
    break;

  case 141: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 1588 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 5375 "src/parser.tab.c"
    break;

  case 142: /* modifier_context: IDENT  */
#line 1592 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 5381 "src/parser.tab.c"
    break;

  case 143: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1596 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 5389 "src/parser.tab.c"
    break;

  case 144: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 1599 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 5397 "src/parser.tab.c"
    break;

  case 145: /* watch_statement: WATCH IDENT LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1607 "src/parser.y"
                                                                                           {
        (yyval.stmt) = ast_watch((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 5405 "src/parser.tab.c"
    break;

  case 146: /* unwatch_statement: UNWATCH expression  */
#line 1613 "src/parser.y"
                         { (yyval.stmt) = ast_unwatch((yyvsp[0].expr)); }
#line 5411 "src/parser.tab.c"
    break;

  case 147: /* wait_statement: WAIT UNTIL expression  */
#line 1620 "src/parser.y"
                            { (yyval.stmt) = ast_wait((yyvsp[0].expr), NULL); }
#line 5417 "src/parser.tab.c"
    break;

  case 148: /* wait_statement: WAIT UNTIL expression FOR expression  */
#line 1621 "src/parser.y"
                                           { (yyval.stmt) = ast_wait((yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 5423 "src/parser.tab.c"
    break;

  case 149: /* watch_target_list: watch_target_path  */
#line 1625 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5429 "src/parser.tab.c"
    break;

  case 150: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 1626 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5435 "src/parser.tab.c"
    break;

  case 151: /* @2: %empty  */
#line 1646 "src/parser.y"
      { (yyval.stmt) = NULL;   /* the mid-rule carries no value; typed so bison stays quiet */
        server_head_note(ctx, (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column); }
#line 5442 "src/parser.tab.c"
    break;

  case 152: /* server_statement: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE @2 server_item_list END IDENT NEWLINE  */
#line 1648 "src/parser.y"
                                         {
        ctx->bad_block_word[0] = '\0';
        (yyval.stmt) = ast_server((yyvsp[-10].text), (yyvsp[-9].text), (yyvsp[-7].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 5451 "src/parser.tab.c"
    break;

  case 153: /* @3: %empty  */
#line 1653 "src/parser.y"
      { (yyval.stmt) = NULL;   /* the mid-rule carries no value; typed so bison stays quiet */
        server_head_note(ctx, (yyvsp[-4].text), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5458 "src/parser.tab.c"
    break;

  case 154: /* server_statement: IDENT IDENT LPAREN RPAREN NEWLINE @3 server_item_list END IDENT NEWLINE  */
#line 1655 "src/parser.y"
                                         {
        ctx->bad_block_word[0] = '\0';
        (yyval.stmt) = ast_server((yyvsp[-9].text), (yyvsp[-8].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 5467 "src/parser.tab.c"
    break;

  case 155: /* shape_block: SHAPE_OPEN IDENT NEWLINE shape_body END IDENT NEWLINE  */
#line 1676 "src/parser.y"
                                                            {
        if (strcmp((yyvsp[-1].text), "shape") != 0) {
            char message[160];
            snprintf(message, sizeof message, "a shape ends with `end shape`, not `end %s`", (yyvsp[-1].text));
            report_syntax_error(ctx, (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column, message);
            GB_REFUSE;
        }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_shape((yyvsp[-5].text), (yyvsp[-3].shape_body).fields, (yyvsp[-3].shape_body).methods, (yyvsp[-3].shape_body).method_count, (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5482 "src/parser.tab.c"
    break;

  case 156: /* shape_body: %empty  */
#line 1689 "src/parser.y"
             { (yyval.shape_body) = ast_shape_body_empty(); }
#line 5488 "src/parser.tab.c"
    break;

  case 157: /* shape_body: shape_body NEWLINE  */
#line 1690 "src/parser.y"
                         { (yyval.shape_body) = (yyvsp[-1].shape_body); }
#line 5494 "src/parser.tab.c"
    break;

  case 158: /* shape_body: shape_body shape_field  */
#line 1691 "src/parser.y"
                             { (yyval.shape_body) = ast_shape_body_add_field((yyvsp[-1].shape_body), (yyvsp[0].shape_field)); }
#line 5500 "src/parser.tab.c"
    break;

  case 159: /* shape_body: shape_body function_statement  */
#line 1692 "src/parser.y"
                                    {
        if ((yyvsp[0].stmt)->as.function.object) {
            report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).first_line, (yylsp[0]).first_column,
                                "a method in a shape is named alone: write `function deposit(...)`, "
                                "and it is the shape's own");
            GB_REFUSE;
        }
        (yyval.shape_body) = ast_shape_body_add_method((yyvsp[-1].shape_body), (yyvsp[0].stmt));
      }
#line 5514 "src/parser.tab.c"
    break;

  case 160: /* shape_field: dot_field_name COLON type_lock NEWLINE  */
#line 1704 "src/parser.y"
                                             {
        if (!type_lock_check_as(ctx, (yyvsp[-3].text), (yyvsp[-1].type_lock).name, (yyvsp[-1].type_lock).arg, (yylsp[-1]).first_line, (yylsp[-1]).first_column,
                                (yylsp[-1]).last_line, (yylsp[-1]).last_column, 1)) { GB_REFUSE; }
        (yyval.shape_field) = (AstShapeField){ (yyvsp[-3].text), (yyvsp[-1].type_lock), NULL, AST_FIELD_POLICY_COPY, NULL, (yylsp[-3]).first_line, (yylsp[-3]).first_column };
      }
#line 5524 "src/parser.tab.c"
    break;

  case 161: /* shape_field: dot_field_name COLON type_lock OP_EQ expression NEWLINE  */
#line 1709 "src/parser.y"
                                                              {
        if (!type_lock_check_as(ctx, (yyvsp[-5].text), (yyvsp[-3].type_lock).name, (yyvsp[-3].type_lock).arg, (yylsp[-3]).first_line, (yylsp[-3]).first_column,
                                (yylsp[-3]).last_line, (yylsp[-3]).last_column, 1)) { GB_REFUSE; }
        if (!shape_default_check(ctx, (yyvsp[-5].text), (yyvsp[-1].expr), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line,
                                 (yylsp[-1]).last_column)) { GB_REFUSE; }
        (yyval.shape_field) = (AstShapeField){ (yyvsp[-5].text), (yyvsp[-3].type_lock), (yyvsp[-1].expr), AST_FIELD_POLICY_COPY, NULL, (yylsp[-5]).first_line, (yylsp[-5]).first_column };
      }
#line 5536 "src/parser.tab.c"
    break;

  case 162: /* shape_field: dot_field_name OP_EQ expression NEWLINE  */
#line 1716 "src/parser.y"
                                              {
        if (!shape_default_check(ctx, (yyvsp[-3].text), (yyvsp[-1].expr), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line,
                                 (yylsp[-1]).last_column)) { GB_REFUSE; }
        (yyval.shape_field) = (AstShapeField){ (yyvsp[-3].text), ast_type_lock(NULL, NULL, 0, 0), (yyvsp[-1].expr), AST_FIELD_POLICY_COPY, NULL,
                              (yylsp[-3]).first_line, (yylsp[-3]).first_column };
      }
#line 5547 "src/parser.tab.c"
    break;

  case 163: /* shape_field: dot_field_name LPAREN field_policy RPAREN COLON type_lock NEWLINE  */
#line 1722 "src/parser.y"
                                                                        {
        if (!type_lock_check_as(ctx, (yyvsp[-6].text), (yyvsp[-1].type_lock).name, (yyvsp[-1].type_lock).arg, (yylsp[-1]).first_line, (yylsp[-1]).first_column,
                                (yylsp[-1]).last_line, (yylsp[-1]).last_column, 1)) { GB_REFUSE; }
        (yyval.shape_field) = (AstShapeField){ (yyvsp[-6].text), (yyvsp[-1].type_lock), NULL, (yyvsp[-4].field_policy).policy, (yyvsp[-4].field_policy).reset_expr, (yylsp[-6]).first_line, (yylsp[-6]).first_column };
      }
#line 5557 "src/parser.tab.c"
    break;

  case 164: /* shape_field: dot_field_name LPAREN field_policy RPAREN COLON type_lock OP_EQ expression NEWLINE  */
#line 1727 "src/parser.y"
                                                                                         {
        if (!type_lock_check_as(ctx, (yyvsp[-8].text), (yyvsp[-3].type_lock).name, (yyvsp[-3].type_lock).arg, (yylsp[-3]).first_line, (yylsp[-3]).first_column,
                                (yylsp[-3]).last_line, (yylsp[-3]).last_column, 1)) { GB_REFUSE; }
        if (!shape_default_check(ctx, (yyvsp[-8].text), (yyvsp[-1].expr), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line,
                                 (yylsp[-1]).last_column)) { GB_REFUSE; }
        (yyval.shape_field) = (AstShapeField){ (yyvsp[-8].text), (yyvsp[-3].type_lock), (yyvsp[-1].expr), (yyvsp[-6].field_policy).policy, (yyvsp[-6].field_policy).reset_expr, (yylsp[-8]).first_line, (yylsp[-8]).first_column };
      }
#line 5569 "src/parser.tab.c"
    break;

  case 165: /* shape_field: dot_field_name LPAREN field_policy RPAREN OP_EQ expression NEWLINE  */
#line 1734 "src/parser.y"
                                                                         {
        if (!shape_default_check(ctx, (yyvsp[-6].text), (yyvsp[-1].expr), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line,
                                 (yylsp[-1]).last_column)) { GB_REFUSE; }
        (yyval.shape_field) = (AstShapeField){ (yyvsp[-6].text), ast_type_lock(NULL, NULL, 0, 0), (yyvsp[-1].expr), (yyvsp[-4].field_policy).policy, (yyvsp[-4].field_policy).reset_expr,
                              (yylsp[-6]).first_line, (yylsp[-6]).first_column };
      }
#line 5580 "src/parser.tab.c"
    break;

  case 166: /* gsql_block: DATABASE_OPEN IDENT LPAREN record_field_list RPAREN NEWLINE  */
#line 1747 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_database((yyvsp[-4].text), (yyvsp[-2].record_field_list), (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 5588 "src/parser.tab.c"
    break;

  case 167: /* gsql_block: DATABASE_OPEN IDENT LPAREN RPAREN NEWLINE  */
#line 1750 "src/parser.y"
                                                {
        (yyval.stmt) = ast_database((yyvsp[-3].text), ast_record_field_list_empty(), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5596 "src/parser.tab.c"
    break;

  case 168: /* gsql_block: SQL_OPEN IDENT ON IDENT NEWLINE SQL_TEXT END IDENT NEWLINE  */
#line 1753 "src/parser.y"
                                                                 {
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_sql((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].text), (yylsp[-3]).first_line, (yylsp[-3]).first_column, (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5605 "src/parser.tab.c"
    break;

  case 169: /* gsql_block: SQL_OPEN IDENT ON IDENT DOT IDENT NEWLINE SQL_TEXT END IDENT NEWLINE  */
#line 1759 "src/parser.y"
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
#line 5622 "src/parser.tab.c"
    break;

  case 170: /* window_block: WINDOW_OPEN IDENT LPAREN record_field_list RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1774 "src/parser.y"
                                                                                                   {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), NULL, NULL, NULL, (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5631 "src/parser.tab.c"
    break;

  case 171: /* window_block: WINDOW_OPEN IDENT LPAREN RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1778 "src/parser.y"
                                                                                 {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text), NULL, NULL, NULL,
                        (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5641 "src/parser.tab.c"
    break;

  case 172: /* window_block: WINDOW_OPEN IDENT LPAREN record_field_list RPAREN TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1788 "src/parser.y"
                                                                                                                  {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-11].text), (yyvsp[-9].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text),
                        (yylsp[-12]).first_line, (yylsp[-12]).first_column);
      }
#line 5651 "src/parser.tab.c"
    break;

  case 173: /* window_item_list: %empty  */
#line 1796 "src/parser.y"
             { (yyval.window_item_list) = ast_window_item_list_empty(); }
#line 5657 "src/parser.tab.c"
    break;

  case 174: /* window_item_list: window_item_list NEWLINE  */
#line 1797 "src/parser.y"
                               { (yyval.window_item_list) = (yyvsp[-1].window_item_list); }
#line 5663 "src/parser.tab.c"
    break;

  case 175: /* window_item_list: window_item_list window_item  */
#line 1798 "src/parser.y"
                                   { (yyval.window_item_list) = ast_window_item_list_append((yyvsp[-1].window_item_list), (yyvsp[0].window_item)); }
#line 5669 "src/parser.tab.c"
    break;

  case 176: /* window_item: IDENT LPAREN RPAREN NEWLINE  */
#line 1803 "src/parser.y"
                                  {
        (yyval.window_item) = ast_window_leaf((yyvsp[-3].text), NULL, ast_record_field_list_empty(),
                             (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 5678 "src/parser.tab.c"
    break;

  case 177: /* window_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE  */
#line 1808 "src/parser.y"
                                                          {
        (yyval.window_item) = ast_window_leaf((yyvsp[-5].text), (yyvsp[-4].text), (yyvsp[-2].record_field_list), (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 5686 "src/parser.tab.c"
    break;

  case 178: /* window_item: IDENT IDENT LPAREN RPAREN NEWLINE  */
#line 1812 "src/parser.y"
                                        {
        (yyval.window_item) = ast_window_leaf((yyvsp[-4].text), (yyvsp[-3].text), ast_record_field_list_empty(),
                             (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5695 "src/parser.tab.c"
    break;

  case 179: /* window_item: IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1817 "src/parser.y"
                                                       {
        (yyval.window_item) = ast_window_container((yyvsp[-5].text), NULL, ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                  (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 5704 "src/parser.tab.c"
    break;

  case 180: /* window_item: IDENT LPAREN record_field_list RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1822 "src/parser.y"
                                                                                       {
        (yyval.window_item) = ast_window_container((yyvsp[-8].text), NULL, (yyvsp[-6].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5712 "src/parser.tab.c"
    break;

  case 181: /* window_item: IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1836 "src/parser.y"
                                                             {
        (yyval.window_item) = ast_window_named_form((yyvsp[-6].text), (yyvsp[-5].text), NULL, NULL, NULL, (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                   (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5721 "src/parser.tab.c"
    break;

  case 182: /* window_item: IDENT IDENT STRING NEWLINE window_item_list END IDENT NEWLINE  */
#line 1851 "src/parser.y"
                                                                    {
        (yyval.window_item) = ast_window_named_container((yyvsp[-7].text), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                        (yylsp[-7]).first_line, (yylsp[-7]).first_column);
      }
#line 5730 "src/parser.tab.c"
    break;

  case 183: /* window_item: IDENT STRING NEWLINE window_item_list END IDENT NEWLINE  */
#line 1856 "src/parser.y"
                                                              {
        (yyval.window_item) = ast_window_container((yyvsp[-6].text), (yyvsp[-5].text), ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                  (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5739 "src/parser.tab.c"
    break;

  case 184: /* window_item: IDENT TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1867 "src/parser.y"
                                                                      {
        (yyval.window_item) = ast_window_form((yyvsp[-8].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                             (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5748 "src/parser.tab.c"
    break;

  case 185: /* window_item: IDENT IDENT TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1882 "src/parser.y"
                                                                            {
        (yyval.window_item) = ast_window_named_form((yyvsp[-9].text), (yyvsp[-8].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                   (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5757 "src/parser.tab.c"
    break;

  case 186: /* window_item: ON IDENT DOT IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1887 "src/parser.y"
                                                               {
        (yyval.window_item) = ast_window_handler((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5765 "src/parser.tab.c"
    break;

  case 187: /* server_item_list: %empty  */
#line 1893 "src/parser.y"
             { (yyval.server_item_list) = ast_server_item_list_empty(); }
#line 5771 "src/parser.tab.c"
    break;

  case 188: /* server_item_list: server_item_list NEWLINE  */
#line 1894 "src/parser.y"
                               { (yyval.server_item_list) = (yyvsp[-1].server_item_list); }
#line 5777 "src/parser.tab.c"
    break;

  case 189: /* server_item_list: server_item_list server_item  */
#line 1895 "src/parser.y"
                                   { (yyval.server_item_list) = ast_server_item_list_append((yyvsp[-1].server_item_list), (yyvsp[0].server_item)); }
#line 5783 "src/parser.tab.c"
    break;

  case 190: /* server_item: IDENT server_string_list NEWLINE  */
#line 1899 "src/parser.y"
                                       {
        (yyval.server_item) = ast_server_directive((yyvsp[-2].text), (yyvsp[-1].name_list), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 5791 "src/parser.tab.c"
    break;

  case 191: /* server_item: IDENT STRING LPAREN parameter_list_opt RPAREN NEWLINE statement_list END IDENT NEWLINE  */
#line 1902 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_handler((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5799 "src/parser.tab.c"
    break;

  case 192: /* server_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1905 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_site((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5807 "src/parser.tab.c"
    break;

  case 193: /* server_item: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1908 "src/parser.y"
                                                                           {
        (yyval.server_item) = ast_server_site((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5815 "src/parser.tab.c"
    break;

  case 194: /* server_item: ON IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1911 "src/parser.y"
                                                     {
        (yyval.server_item) = ast_server_hook((yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5823 "src/parser.tab.c"
    break;

  case 195: /* server_string_list: STRING  */
#line 1917 "src/parser.y"
             { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5829 "src/parser.tab.c"
    break;

  case 196: /* server_string_list: server_string_list COMMA STRING  */
#line 1918 "src/parser.y"
                                      { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5835 "src/parser.tab.c"
    break;

  case 197: /* watch_target_path: variable_name  */
#line 1922 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 5841 "src/parser.tab.c"
    break;

  case 198: /* watch_target_path: watch_target_path DOT IDENT  */
#line 1923 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 5847 "src/parser.tab.c"
    break;

  case 199: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 1927 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 5855 "src/parser.tab.c"
    break;

  case 200: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 1933 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 5861 "src/parser.tab.c"
    break;

  case 201: /* on_error_statement: ON ERROR_VALUE GOTO NEXT  */
#line 1934 "src/parser.y"
                               { (yyval.stmt) = ast_on_error_goto_next(); }
#line 5867 "src/parser.tab.c"
    break;

  case 202: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 1935 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 5873 "src/parser.tab.c"
    break;

  case 203: /* on_error_statement: ON IDENT GOTO NEXT  */
#line 1936 "src/parser.y"
                         {
        if (!warn_channel_ok(ctx, (yyvsp[-2].text), (yylsp[-2]).first_line, (yylsp[-2]).first_column)) { GB_REFUSE; }
        free((yyvsp[-2].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_NEXT);
      }
#line 5883 "src/parser.tab.c"
    break;

  case 204: /* on_error_statement: ON IDENT GOTO IDENT  */
#line 1941 "src/parser.y"
                          {
        /* A warning fires from a statement that SUCCEEDED, so a label jump
         * would mean leaving successful code on an advisory signal. Refused
         * BY NAME rather than as a bare syntax error. */
        if (!warn_channel_ok(ctx, (yyvsp[-2].text), (yylsp[-2]).first_line, (yylsp[-2]).first_column)) { GB_REFUSE; }
        report_diag(ctx, GB_DIAG_PARSE_ERROR, (yylsp[-1]).first_line, (yylsp[-1]).first_column,
                    (yylsp[-1]).first_line, (yylsp[-1]).first_column,
                    "on warning has no goto-label form: a warning does not abandon "
                    "its statement, so there is nothing to jump away from "
                    "(use goto next, stop, ignore or print)");
        free((yyvsp[-2].text)); free((yyvsp[0].text));
        (yyvsp[-2].text) = NULL; (yyvsp[0].text) = NULL;
        (yyval.stmt) = NULL;   /* never used: YYERROR discards it, but bison asks every rule to set one */
        GB_REFUSE;
      }
#line 5903 "src/parser.tab.c"
    break;

  case 205: /* on_error_statement: ON IDENT STOP  */
#line 1956 "src/parser.y"
                    {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { GB_REFUSE; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_STOP);
      }
#line 5913 "src/parser.tab.c"
    break;

  case 206: /* on_error_statement: ON IDENT PRINT  */
#line 1961 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { GB_REFUSE; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_PRINT);
      }
#line 5923 "src/parser.tab.c"
    break;

  case 207: /* on_error_statement: ON IDENT IDENT  */
#line 1966 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { GB_REFUSE; }
        int mode = warn_mode_word(ctx, (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column);
        if (mode < 0) { free((yyvsp[-1].text)); free((yyvsp[0].text)); (yyvsp[-1].text) = NULL; (yyvsp[0].text) = NULL; GB_REFUSE; }
        free((yyvsp[-1].text)); free((yyvsp[0].text));
        (yyval.stmt) = ast_on_warning(mode);
      }
#line 5935 "src/parser.tab.c"
    break;

  case 208: /* error_statement: ERROR_VALUE expression  */
#line 1976 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 5941 "src/parser.tab.c"
    break;

  case 209: /* return_statement: RETURN  */
#line 1980 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 5947 "src/parser.tab.c"
    break;

  case 210: /* return_statement: RETURN expression  */
#line 1981 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 5953 "src/parser.tab.c"
    break;

  case 211: /* label_statement: variable_name COLON  */
#line 1985 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 5959 "src/parser.tab.c"
    break;

  case 212: /* goto_statement: GOTO variable_name  */
#line 1992 "src/parser.y"
                         { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 5965 "src/parser.tab.c"
    break;

  case 213: /* gosub_statement: GOSUB variable_name  */
#line 1996 "src/parser.y"
                          { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 5971 "src/parser.tab.c"
    break;

  case 214: /* break_statement: BREAK  */
#line 2005 "src/parser.y"
            { (yyval.stmt) = ast_break(NULL); }
#line 5977 "src/parser.tab.c"
    break;

  case 215: /* break_statement: BREAK IDENT  */
#line 2006 "src/parser.y"
                  { (yyval.stmt) = ast_break((yyvsp[0].text)); }
#line 5983 "src/parser.tab.c"
    break;

  case 216: /* continue_statement: CONTINUE  */
#line 2010 "src/parser.y"
               { (yyval.stmt) = ast_continue(NULL); }
#line 5989 "src/parser.tab.c"
    break;

  case 217: /* continue_statement: CONTINUE IDENT  */
#line 2011 "src/parser.y"
                     { (yyval.stmt) = ast_continue((yyvsp[0].text)); }
#line 5995 "src/parser.tab.c"
    break;

  case 218: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 2015 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 6004 "src/parser.tab.c"
    break;

  case 219: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 2019 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 6013 "src/parser.tab.c"
    break;

  case 220: /* if_block_tail: END IF NEWLINE  */
#line 2026 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 6021 "src/parser.tab.c"
    break;

  case 221: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 2029 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 6029 "src/parser.tab.c"
    break;

  case 222: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 2032 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 6037 "src/parser.tab.c"
    break;

  case 223: /* if_block_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 2041 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 6049 "src/parser.tab.c"
    break;

  case 224: /* if_inline_tail: %empty  */
#line 2051 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 6057 "src/parser.tab.c"
    break;

  case 225: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 2054 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 6065 "src/parser.tab.c"
    break;

  case 226: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 2057 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 6073 "src/parser.tab.c"
    break;

  case 227: /* if_inline_tail: ELSE IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 2063 "src/parser.y"
                                                                      {
        AstStmt *inner = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 6085 "src/parser.tab.c"
    break;

  case 228: /* if_inline_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 2070 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 6097 "src/parser.tab.c"
    break;

  case 229: /* inline_statement: assignment  */
#line 2080 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 6103 "src/parser.tab.c"
    break;

  case 230: /* inline_statement: print_statement  */
#line 2081 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 6109 "src/parser.tab.c"
    break;

  case 231: /* inline_statement: call_statement  */
#line 2082 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 6115 "src/parser.tab.c"
    break;

  case 232: /* inline_statement: use_statement  */
#line 2083 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 6121 "src/parser.tab.c"
    break;

  case 233: /* inline_statement: on_error_statement  */
#line 2084 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 6127 "src/parser.tab.c"
    break;

  case 234: /* inline_statement: error_statement  */
#line 2085 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 6133 "src/parser.tab.c"
    break;

  case 235: /* inline_statement: return_statement  */
#line 2086 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 6139 "src/parser.tab.c"
    break;

  case 236: /* inline_statement: goto_statement  */
#line 2087 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 6145 "src/parser.tab.c"
    break;

  case 237: /* inline_statement: gosub_statement  */
#line 2088 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 6151 "src/parser.tab.c"
    break;

  case 238: /* inline_statement: break_statement  */
#line 2089 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 6157 "src/parser.tab.c"
    break;

  case 239: /* inline_statement: continue_statement  */
#line 2090 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 6163 "src/parser.tab.c"
    break;

  case 240: /* expression: conditional_expression  */
#line 2098 "src/parser.y"
                             { (yyval.expr) = (yyvsp[0].expr); }
#line 6169 "src/parser.tab.c"
    break;

  case 241: /* expression: TRY expression  */
#line 2099 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_unary(copy_const("try"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6175 "src/parser.tab.c"
    break;

  case 242: /* conditional_expression: or_expression  */
#line 2103 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 6181 "src/parser.tab.c"
    break;

  case 243: /* conditional_expression: or_expression IF or_expression ELSE conditional_expression  */
#line 2104 "src/parser.y"
                                                                 {
        (yyval.expr) = expr_at(ast_conditional((yyvsp[-2].expr), (yyvsp[-4].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 6189 "src/parser.tab.c"
    break;

  case 244: /* or_expression: and_expression  */
#line 2110 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 6195 "src/parser.tab.c"
    break;

  case 245: /* or_expression: or_expression OR and_expression  */
#line 2111 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6201 "src/parser.tab.c"
    break;

  case 246: /* and_expression: not_expression  */
#line 2115 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 6207 "src/parser.tab.c"
    break;

  case 247: /* and_expression: and_expression AND not_expression  */
#line 2116 "src/parser.y"
                                        { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6213 "src/parser.tab.c"
    break;

  case 248: /* not_expression: comparison_expression  */
#line 2138 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 6219 "src/parser.tab.c"
    break;

  case 249: /* not_expression: NOT not_expression  */
#line 2139 "src/parser.y"
                         { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6225 "src/parser.tab.c"
    break;

  case 250: /* comparison_expression: set_expression  */
#line 2143 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 6231 "src/parser.tab.c"
    break;

  case 251: /* comparison_expression: set_expression comparison_operator set_expression  */
#line 2144 "src/parser.y"
                                                        { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6237 "src/parser.tab.c"
    break;

  case 252: /* comparison_expression: set_expression comparison_lens comparison_operator set_expression  */
#line 2145 "src/parser.y"
                                                                        {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 6245 "src/parser.tab.c"
    break;

  case 253: /* set_expression: additive_expression  */
#line 2151 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 6251 "src/parser.tab.c"
    break;

  case 254: /* set_expression: set_expression EXCLUDING additive_expression  */
#line 2152 "src/parser.y"
                                                   { (yyval.expr) = expr_at(ast_binary(copy_const("excluding"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6257 "src/parser.tab.c"
    break;

  case 255: /* set_expression: set_expression INTERSECTING additive_expression  */
#line 2153 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("intersecting"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6263 "src/parser.tab.c"
    break;

  case 256: /* additive_expression: multiplicative_expression  */
#line 2157 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 6269 "src/parser.tab.c"
    break;

  case 257: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 2158 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6275 "src/parser.tab.c"
    break;

  case 258: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 2159 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6281 "src/parser.tab.c"
    break;

  case 259: /* multiplicative_expression: unary_expression  */
#line 2163 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 6287 "src/parser.tab.c"
    break;

  case 260: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 2164 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6293 "src/parser.tab.c"
    break;

  case 261: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 2165 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6299 "src/parser.tab.c"
    break;

  case 262: /* unary_expression: postfix_expression  */
#line 2169 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 6305 "src/parser.tab.c"
    break;

  case 263: /* unary_expression: MINUS unary_expression  */
#line 2170 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6311 "src/parser.tab.c"
    break;

  case 264: /* unary_expression: MODIFIER_PREFIX unary_expression  */
#line 2177 "src/parser.y"
                                       {
        (yyval.expr) = expr_at(ast_modifier_apply(parse_modifier_use((yyvsp[-1].text)), (yyvsp[0].expr)),
                     (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 6320 "src/parser.tab.c"
    break;

  case 265: /* unary_expression: NEW postfix_expression  */
#line 2181 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6326 "src/parser.tab.c"
    break;

  case 266: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 2182 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 6332 "src/parser.tab.c"
    break;

  case 267: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 2183 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 6338 "src/parser.tab.c"
    break;

  case 268: /* postfix_expression: primary  */
#line 2187 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 6344 "src/parser.tab.c"
    break;

  case 269: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 2188 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 6350 "src/parser.tab.c"
    break;

  case 270: /* postfix_expression: postfix_expression LBRACKET expression COMMA argument_list RBRACKET  */
#line 2192 "src/parser.y"
                                                                          {
        AstExprList items = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[-3].expr));
        for (size_t i = 0; i < (yyvsp[-1].expr_list).count; i++) {
            items = ast_expr_list_append(items, (yyvsp[-1].expr_list).items[i]);
        }
        free((yyvsp[-1].expr_list).items);
        (yyval.expr) = expr_at(ast_index_list((yyvsp[-5].expr), items), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 6363 "src/parser.tab.c"
    break;

  case 271: /* postfix_expression: postfix_expression DOT dot_field_name  */
#line 2200 "src/parser.y"
                                            { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6369 "src/parser.tab.c"
    break;

  case 272: /* postfix_expression: postfix_expression PATH_LBRACE projection_names RBRACE  */
#line 2205 "src/parser.y"
                                                             {
        (yyval.expr) = expr_at(ast_project((yyvsp[-3].expr), (yyvsp[-1].name_list)), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 6377 "src/parser.tab.c"
    break;

  case 273: /* postfix_expression: postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 2208 "src/parser.y"
                                                                   {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        (yyval.expr) = expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 6387 "src/parser.tab.c"
    break;

  case 274: /* postfix_expression: postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 2213 "src/parser.y"
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
#line 6403 "src/parser.tab.c"
    break;

  case 275: /* comparison_operator: OP_EQ  */
#line 2227 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 6409 "src/parser.tab.c"
    break;

  case 276: /* comparison_operator: OP_NE  */
#line 2228 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 6415 "src/parser.tab.c"
    break;

  case 277: /* comparison_operator: OP_GT  */
#line 2229 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 6421 "src/parser.tab.c"
    break;

  case 278: /* comparison_operator: OP_LT  */
#line 2230 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 6427 "src/parser.tab.c"
    break;

  case 279: /* comparison_operator: OP_GE  */
#line 2231 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 6433 "src/parser.tab.c"
    break;

  case 280: /* comparison_operator: OP_LE  */
#line 2232 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 6439 "src/parser.tab.c"
    break;

  case 281: /* comparison_operator: OP_NGT  */
#line 2233 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 6445 "src/parser.tab.c"
    break;

  case 282: /* comparison_operator: OP_NLT  */
#line 2234 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 6451 "src/parser.tab.c"
    break;

  case 283: /* comparison_operator: OP_NGE  */
#line 2235 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 6457 "src/parser.tab.c"
    break;

  case 284: /* comparison_operator: OP_NLE  */
#line 2236 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 6463 "src/parser.tab.c"
    break;

  case 285: /* primary: MODIFIER_VALUE  */
#line 2240 "src/parser.y"
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
            GB_REFUSE;
        }
        (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column);
      }
#line 6486 "src/parser.tab.c"
    break;

  case 286: /* primary: DOT dot_field_name  */
#line 2261 "src/parser.y"
                         { (yyval.expr) = expr_at(ast_column((yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6492 "src/parser.tab.c"
    break;

  case 287: /* primary: NUMBER  */
#line 2262 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact, (yyvsp[0].numlit).scale), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6498 "src/parser.tab.c"
    break;

  case 288: /* primary: WATCHERS LPAREN RPAREN  */
#line 2263 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_call(copy_const("watchers"), ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 6504 "src/parser.tab.c"
    break;

  case 289: /* primary: duration_terms  */
#line 2264 "src/parser.y"
                     {
        if ((yyvsp[0].duration).cells) {
            /* `40 columns` is the record `{ columns: 40 }`, a size the `ui`
             * backends convert; one term, a whole number of at least 1 */
            if ((yyvsp[0].duration).cells_mixed) {
                report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column,
                                    "a size in columns or rows is one term on its own, "
                                    "as `40 columns` or `12 rows`: it cannot join a duration or another size");
                GB_REFUSE;
            }
            if ((yyvsp[0].duration).cells_amount < 1 || (yyvsp[0].duration).cells_amount != floor((yyvsp[0].duration).cells_amount) || (yyvsp[0].duration).cells_amount > 100000) {
                report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column,
                                    "a size in columns or rows is a whole number from 1, as `40 columns`");
                GB_REFUSE;
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
#line 6533 "src/parser.tab.c"
    break;

  case 290: /* primary: STRING  */
#line 2288 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6539 "src/parser.tab.c"
    break;

  case 291: /* primary: variable_name ident_suffix  */
#line 2289 "src/parser.y"
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
#line 6561 "src/parser.tab.c"
    break;

  case 292: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 2306 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 6572 "src/parser.tab.c"
    break;

  case 293: /* primary: ERROR_VALUE  */
#line 2312 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6578 "src/parser.tab.c"
    break;

  case 294: /* primary: TRUE  */
#line 2313 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6584 "src/parser.tab.c"
    break;

  case 295: /* primary: FALSE  */
#line 2314 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6590 "src/parser.tab.c"
    break;

  case 296: /* primary: NOTHING  */
#line 2315 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6596 "src/parser.tab.c"
    break;

  case 297: /* primary: UNKNOWN_VALUE  */
#line 2316 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6602 "src/parser.tab.c"
    break;

  case 298: /* primary: LPAREN expression RPAREN  */
#line 2317 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); (yyval.expr)->parenthesized = 1; }
#line 6608 "src/parser.tab.c"
    break;

  case 299: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 2318 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 6614 "src/parser.tab.c"
    break;

  case 300: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 2319 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 6620 "src/parser.tab.c"
    break;

  case 301: /* primary: record_literal  */
#line 2320 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 6626 "src/parser.tab.c"
    break;

  case 302: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 2324 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 6632 "src/parser.tab.c"
    break;

  case 303: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 2325 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 6638 "src/parser.tab.c"
    break;

  case 304: /* ident_suffix: %empty  */
#line 2329 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 6648 "src/parser.tab.c"
    break;

  case 305: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 2334 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 6658 "src/parser.tab.c"
    break;

  case 306: /* ident_suffix: DOT dot_field_name ident_dot_suffix  */
#line 2339 "src/parser.y"
                                          {
        /* dot_field_name, not IDENT: a keyword is a legal FIELD name after a
         * dot, because nothing but a name can appear there. */
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 6669 "src/parser.tab.c"
    break;

  case 307: /* ident_suffix: DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 2345 "src/parser.y"
                                                          {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        (yyval.ident_suffix).kind = IDENT_SUFFIX_METHOD;
        (yyval.ident_suffix).name = (yyvsp[-3].text);
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 6683 "src/parser.tab.c"
    break;

  case 308: /* ident_dot_suffix: %empty  */
#line 2357 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 6693 "src/parser.tab.c"
    break;

  case 309: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 2362 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 6703 "src/parser.tab.c"
    break;

  case 310: /* duration_terms: NUMBER IDENT  */
#line 2370 "src/parser.y"
                   {
        AstDuration duration = {0};
        char *bad = NULL;
        (yyval.duration) = duration_add_unit(duration, (yyvsp[-1].numlit).value, (yyvsp[0].text), &bad);
        if (bad) {
            duration_unit_error(ctx, bad, (yylsp[0]).first_line, (yylsp[0]).first_column,
                                (yylsp[0]).last_line, (yylsp[0]).last_column);
            (yyvsp[0].text) = NULL;      /* duration_add_unit took the unit */
            GB_REFUSE;
        }
      }
#line 6719 "src/parser.tab.c"
    break;

  case 311: /* duration_terms: duration_terms NUMBER IDENT  */
#line 2381 "src/parser.y"
                                  {
        char *bad = NULL;
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].numlit).value, (yyvsp[0].text), &bad);
        if (bad) {
            duration_unit_error(ctx, bad, (yylsp[0]).first_line, (yylsp[0]).first_column,
                                (yylsp[0]).last_line, (yylsp[0]).last_column);
            (yyvsp[0].text) = NULL;      /* duration_add_unit took the unit */
            GB_REFUSE;
        }
      }
#line 6734 "src/parser.tab.c"
    break;

  case 312: /* argument_list_opt: %empty  */
#line 2394 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 6740 "src/parser.tab.c"
    break;

  case 313: /* argument_list_opt: argument_list  */
#line 2395 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 6746 "src/parser.tab.c"
    break;

  case 314: /* argument_list: expression  */
#line 2399 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 6752 "src/parser.tab.c"
    break;

  case 315: /* argument_list: argument_list COMMA expression  */
#line 2400 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 6758 "src/parser.tab.c"
    break;

  case 316: /* array_argument_list: expression  */
#line 2404 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 6764 "src/parser.tab.c"
    break;

  case 317: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 2405 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 6770 "src/parser.tab.c"
    break;

  case 318: /* parameter_list_opt: %empty  */
#line 2409 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 6776 "src/parser.tab.c"
    break;

  case 319: /* parameter_list_opt: parameter_list  */
#line 2410 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 6782 "src/parser.tab.c"
    break;

  case 320: /* parameter_default: NUMBER  */
#line 2424 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact, (yyvsp[0].numlit).scale), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6788 "src/parser.tab.c"
    break;

  case 321: /* parameter_default: MINUS NUMBER  */
#line 2428 "src/parser.y"
                   { (yyval.expr) = expr_at(ast_number(-(yyvsp[0].numlit).value,
                                             (yyvsp[0].numlit).exact == LLONG_MIN ? 0 : -(yyvsp[0].numlit).exact,
                                             (yyvsp[0].numlit).exact == LLONG_MIN ? 0 : (yyvsp[0].numlit).is_exact,
                                             (yyvsp[0].numlit).exact == LLONG_MIN ? 0 : (yyvsp[0].numlit).scale),
                                  (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6798 "src/parser.tab.c"
    break;

  case 322: /* parameter_default: PLUS NUMBER  */
#line 2433 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact, (yyvsp[0].numlit).scale), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6804 "src/parser.tab.c"
    break;

  case 323: /* parameter_default: STRING  */
#line 2434 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6810 "src/parser.tab.c"
    break;

  case 324: /* parameter_default: TRUE  */
#line 2435 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6816 "src/parser.tab.c"
    break;

  case 325: /* parameter_default: FALSE  */
#line 2436 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6822 "src/parser.tab.c"
    break;

  case 326: /* parameter_default: NOTHING  */
#line 2437 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6828 "src/parser.tab.c"
    break;

  case 327: /* parameter_default: UNKNOWN_VALUE  */
#line 2438 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6834 "src/parser.tab.c"
    break;

  case 328: /* parameter_list: IDENT  */
#line 2442 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 6840 "src/parser.tab.c"
    break;

  case 329: /* parameter_list: IDENT OP_EQ parameter_default  */
#line 2443 "src/parser.y"
                                    {
        (yyval.name_list) = ast_name_list_append_default(ast_name_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 6848 "src/parser.tab.c"
    break;

  case 330: /* parameter_list: parameter_list COMMA IDENT  */
#line 2446 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 6854 "src/parser.tab.c"
    break;

  case 331: /* parameter_list: IDENT COLON type_lock  */
#line 2450 "src/parser.y"
                            {
        if (!type_lock_check(ctx, (yyvsp[-2].text), (yyvsp[0].type_lock).name, (yyvsp[0].type_lock).arg, (yylsp[0]).first_line, (yylsp[0]).first_column,
                             (yylsp[0]).last_line, (yylsp[0]).last_column)) {
            GB_REFUSE;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append(ast_name_list_empty(), (yyvsp[-2].text)), (yyvsp[0].type_lock));
      }
#line 6866 "src/parser.tab.c"
    break;

  case 332: /* parameter_list: IDENT COLON type_lock OP_EQ parameter_default  */
#line 2457 "src/parser.y"
                                                    {
        if (!type_lock_check(ctx, (yyvsp[-4].text), (yyvsp[-2].type_lock).name, (yyvsp[-2].type_lock).arg, (yylsp[-2]).first_line, (yylsp[-2]).first_column,
                             (yylsp[-2]).last_line, (yylsp[-2]).last_column)) {
            GB_REFUSE;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append_default(ast_name_list_empty(), (yyvsp[-4].text), (yyvsp[0].expr)), (yyvsp[-2].type_lock));
      }
#line 6878 "src/parser.tab.c"
    break;

  case 333: /* parameter_list: parameter_list COMMA IDENT COLON type_lock  */
#line 2464 "src/parser.y"
                                                 {
        if (!type_lock_check(ctx, (yyvsp[-2].text), (yyvsp[0].type_lock).name, (yyvsp[0].type_lock).arg, (yylsp[0]).first_line, (yylsp[0]).first_column,
                             (yylsp[0]).last_line, (yylsp[0]).last_column)) {
            GB_REFUSE;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append((yyvsp[-4].name_list), (yyvsp[-2].text)), (yyvsp[0].type_lock));
      }
#line 6890 "src/parser.tab.c"
    break;

  case 334: /* parameter_list: parameter_list COMMA IDENT COLON type_lock OP_EQ parameter_default  */
#line 2471 "src/parser.y"
                                                                         {
        if (!type_lock_check(ctx, (yyvsp[-4].text), (yyvsp[-2].type_lock).name, (yyvsp[-2].type_lock).arg, (yylsp[-2]).first_line, (yylsp[-2]).first_column,
                             (yylsp[-2]).last_line, (yylsp[-2]).last_column)) {
            GB_REFUSE;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append_default((yyvsp[-6].name_list), (yyvsp[-4].text), (yyvsp[0].expr)), (yyvsp[-2].type_lock));
      }
#line 6902 "src/parser.tab.c"
    break;

  case 335: /* parameter_list: parameter_list COMMA IDENT OP_EQ parameter_default  */
#line 2478 "src/parser.y"
                                                         {
        (yyval.name_list) = ast_name_list_append_default((yyvsp[-4].name_list), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 6910 "src/parser.tab.c"
    break;

  case 336: /* field_name: dot_field_name  */
#line 2493 "src/parser.y"
                     { (yyval.text) = (yyvsp[0].text); }
#line 6916 "src/parser.tab.c"
    break;

  case 337: /* field_name: STRING  */
#line 2500 "src/parser.y"
             { (yyval.text) = (yyvsp[0].text); }
#line 6922 "src/parser.tab.c"
    break;

  case 338: /* projection_names: dot_field_name  */
#line 2509 "src/parser.y"
                     { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 6928 "src/parser.tab.c"
    break;

  case 339: /* projection_names: projection_names COMMA dot_field_name  */
#line 2510 "src/parser.y"
                                            { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 6934 "src/parser.tab.c"
    break;

  case 340: /* dot_field_name: IDENT  */
#line 2514 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 6940 "src/parser.tab.c"
    break;

  case 341: /* dot_field_name: AS  */
#line 2515 "src/parser.y"
                     { (yyval.text) = kw_name("as"); }
#line 6946 "src/parser.tab.c"
    break;

  case 342: /* dot_field_name: NEXT  */
#line 2516 "src/parser.y"
                     { (yyval.text) = kw_name("next"); }
#line 6952 "src/parser.tab.c"
    break;

  case 343: /* dot_field_name: STOP  */
#line 2517 "src/parser.y"
                     { (yyval.text) = kw_name("stop"); }
#line 6958 "src/parser.tab.c"
    break;

  case 344: /* dot_field_name: ERROR_VALUE  */
#line 2518 "src/parser.y"
                     { (yyval.text) = kw_name("error"); }
#line 6964 "src/parser.tab.c"
    break;

  case 345: /* dot_field_name: END  */
#line 2519 "src/parser.y"
                     { (yyval.text) = kw_name("end"); }
#line 6970 "src/parser.tab.c"
    break;

  case 346: /* dot_field_name: TO  */
#line 2520 "src/parser.y"
                     { (yyval.text) = kw_name("to"); }
#line 6976 "src/parser.tab.c"
    break;

  case 347: /* dot_field_name: IN  */
#line 2521 "src/parser.y"
                     { (yyval.text) = kw_name("in"); }
#line 6982 "src/parser.tab.c"
    break;

  case 348: /* dot_field_name: ON  */
#line 2522 "src/parser.y"
                     { (yyval.text) = kw_name("on"); }
#line 6988 "src/parser.tab.c"
    break;

  case 349: /* dot_field_name: NEW  */
#line 2523 "src/parser.y"
                     { (yyval.text) = kw_name("new"); }
#line 6994 "src/parser.tab.c"
    break;

  case 350: /* dot_field_name: EACH  */
#line 2524 "src/parser.y"
                     { (yyval.text) = kw_name("each"); }
#line 7000 "src/parser.tab.c"
    break;

  case 351: /* dot_field_name: WITH  */
#line 2525 "src/parser.y"
                     { (yyval.text) = kw_name("with"); }
#line 7006 "src/parser.tab.c"
    break;

  case 352: /* dot_field_name: WITHOUT  */
#line 2526 "src/parser.y"
                     { (yyval.text) = kw_name("without"); }
#line 7012 "src/parser.tab.c"
    break;

  case 353: /* dot_field_name: THEN  */
#line 2527 "src/parser.y"
                     { (yyval.text) = kw_name("then"); }
#line 7018 "src/parser.tab.c"
    break;

  case 354: /* dot_field_name: ELSE  */
#line 2528 "src/parser.y"
                     { (yyval.text) = kw_name("else"); }
#line 7024 "src/parser.tab.c"
    break;

  case 355: /* dot_field_name: FOR  */
#line 2529 "src/parser.y"
                     { (yyval.text) = kw_name("for"); }
#line 7030 "src/parser.tab.c"
    break;

  case 356: /* dot_field_name: IF  */
#line 2530 "src/parser.y"
                     { (yyval.text) = kw_name("if"); }
#line 7036 "src/parser.tab.c"
    break;

  case 357: /* dot_field_name: WHILE  */
#line 2531 "src/parser.y"
                     { (yyval.text) = kw_name("while"); }
#line 7042 "src/parser.tab.c"
    break;

  case 358: /* dot_field_name: DO  */
#line 2532 "src/parser.y"
                     { (yyval.text) = kw_name("do"); }
#line 7048 "src/parser.tab.c"
    break;

  case 359: /* dot_field_name: UNTIL  */
#line 2533 "src/parser.y"
                     { (yyval.text) = kw_name("until"); }
#line 7054 "src/parser.tab.c"
    break;

  case 360: /* dot_field_name: PRINT  */
#line 2534 "src/parser.y"
                     { (yyval.text) = kw_name("print"); }
#line 7060 "src/parser.tab.c"
    break;

  case 361: /* dot_field_name: RETURN  */
#line 2535 "src/parser.y"
                     { (yyval.text) = kw_name("return"); }
#line 7066 "src/parser.tab.c"
    break;

  case 362: /* dot_field_name: LOAD  */
#line 2536 "src/parser.y"
                     { (yyval.text) = kw_name("load"); }
#line 7072 "src/parser.tab.c"
    break;

  case 363: /* dot_field_name: USE  */
#line 2537 "src/parser.y"
                     { (yyval.text) = kw_name("use"); }
#line 7078 "src/parser.tab.c"
    break;

  case 364: /* dot_field_name: NOT  */
#line 2538 "src/parser.y"
                     { (yyval.text) = kw_name("not"); }
#line 7084 "src/parser.tab.c"
    break;

  case 365: /* dot_field_name: TRY  */
#line 2539 "src/parser.y"
                     { (yyval.text) = kw_name("try"); }
#line 7090 "src/parser.tab.c"
    break;

  case 366: /* dot_field_name: AND  */
#line 2540 "src/parser.y"
                     { (yyval.text) = kw_name("and"); }
#line 7096 "src/parser.tab.c"
    break;

  case 367: /* dot_field_name: OR  */
#line 2541 "src/parser.y"
                     { (yyval.text) = kw_name("or"); }
#line 7102 "src/parser.tab.c"
    break;

  case 368: /* dot_field_name: TRUE  */
#line 2542 "src/parser.y"
                     { (yyval.text) = kw_name("true"); }
#line 7108 "src/parser.tab.c"
    break;

  case 369: /* dot_field_name: FALSE  */
#line 2543 "src/parser.y"
                     { (yyval.text) = kw_name("false"); }
#line 7114 "src/parser.tab.c"
    break;

  case 370: /* dot_field_name: NOTHING  */
#line 2544 "src/parser.y"
                     { (yyval.text) = kw_name("nothing"); }
#line 7120 "src/parser.tab.c"
    break;

  case 371: /* dot_field_name: BREAK  */
#line 2545 "src/parser.y"
                     { (yyval.text) = kw_name("break"); }
#line 7126 "src/parser.tab.c"
    break;

  case 372: /* dot_field_name: CONTINUE  */
#line 2546 "src/parser.y"
                     { (yyval.text) = kw_name("continue"); }
#line 7132 "src/parser.tab.c"
    break;

  case 373: /* dot_field_name: GOTO  */
#line 2547 "src/parser.y"
                     { (yyval.text) = kw_name("goto"); }
#line 7138 "src/parser.tab.c"
    break;

  case 374: /* dot_field_name: GOSUB  */
#line 2548 "src/parser.y"
                     { (yyval.text) = kw_name("gosub"); }
#line 7144 "src/parser.tab.c"
    break;

  case 375: /* dot_field_name: SPAWN  */
#line 2549 "src/parser.y"
                     { (yyval.text) = kw_name("spawn"); }
#line 7150 "src/parser.tab.c"
    break;

  case 376: /* dot_field_name: EXPORT  */
#line 2550 "src/parser.y"
                     { (yyval.text) = kw_name("export"); }
#line 7156 "src/parser.tab.c"
    break;

  case 377: /* dot_field_name: LIBRARY  */
#line 2551 "src/parser.y"
                     { (yyval.text) = kw_name("library"); }
#line 7162 "src/parser.tab.c"
    break;

  case 378: /* dot_field_name: FUNCTION  */
#line 2552 "src/parser.y"
                     { (yyval.text) = kw_name("function"); }
#line 7168 "src/parser.tab.c"
    break;

  case 379: /* dot_field_name: MODIFIER  */
#line 2553 "src/parser.y"
                     { (yyval.text) = kw_name("modifier"); }
#line 7174 "src/parser.tab.c"
    break;

  case 380: /* dot_field_name: PROGRAM  */
#line 2554 "src/parser.y"
                     { (yyval.text) = kw_name("program"); }
#line 7180 "src/parser.tab.c"
    break;

  case 381: /* dot_field_name: WATCH  */
#line 2555 "src/parser.y"
                     { (yyval.text) = kw_name("watch"); }
#line 7186 "src/parser.tab.c"
    break;

  case 382: /* dot_field_name: WATCHERS  */
#line 2556 "src/parser.y"
                     { (yyval.text) = kw_name("watchers"); }
#line 7192 "src/parser.tab.c"
    break;

  case 383: /* dot_field_name: CONSIDER  */
#line 2557 "src/parser.y"
                     { (yyval.text) = kw_name("consider"); }
#line 7198 "src/parser.tab.c"
    break;

  case 384: /* dot_field_name: STEP  */
#line 2558 "src/parser.y"
                     { (yyval.text) = kw_name("step"); }
#line 7204 "src/parser.tab.c"
    break;

  case 385: /* dot_field_name: UNWATCH  */
#line 2559 "src/parser.y"
                     { (yyval.text) = kw_name("unwatch"); }
#line 7210 "src/parser.tab.c"
    break;

  case 386: /* dot_field_name: UNKNOWN_VALUE  */
#line 2560 "src/parser.y"
                     { (yyval.text) = kw_name("unknown"); }
#line 7216 "src/parser.tab.c"
    break;

  case 387: /* dot_field_name: DIM  */
#line 2561 "src/parser.y"
                     { (yyval.text) = kw_name("dim"); }
#line 7222 "src/parser.tab.c"
    break;

  case 388: /* record_field_list: field_name OP_EQ expression  */
#line 2565 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 7228 "src/parser.tab.c"
    break;

  case 389: /* record_field_list: field_name COLON expression  */
#line 2566 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 7234 "src/parser.tab.c"
    break;

  case 390: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 2567 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 7240 "src/parser.tab.c"
    break;

  case 391: /* record_field_list: record_field_list COMMA optional_newlines field_name OP_EQ expression  */
#line 2568 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 7246 "src/parser.tab.c"
    break;

  case 392: /* record_field_list: record_field_list COMMA optional_newlines field_name COLON expression  */
#line 2569 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 7252 "src/parser.tab.c"
    break;

  case 393: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 2570 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 7258 "src/parser.tab.c"
    break;

  case 394: /* field_policy: IDENT  */
#line 2578 "src/parser.y"
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
            GB_REFUSE;
        } else {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "unknown field policy (expected copy, link, reset, or exclude)");
            free((yyvsp[0].text));
            (yyvsp[0].text) = NULL;
            GB_REFUSE;
        }
        free((yyvsp[0].text));
        (yyval.field_policy) = spec;
      }
#line 7290 "src/parser.tab.c"
    break;

  case 395: /* field_policy: IDENT expression  */
#line 2605 "src/parser.y"
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
            GB_REFUSE;
        }
        free((yyvsp[-1].text));
        (yyval.field_policy) = spec;
      }
#line 7311 "src/parser.tab.c"
    break;


#line 7315 "src/parser.tab.c"

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

#line 2628 "src/parser.y"


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
    ctx.shape_open = 0;
    ctx.shape_fn_depth = 0;
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
    ctx.tok_start = NULL;
    ctx.tok_prev_start = NULL;
    ctx.tok_prev_after = NULL;
    ctx.fix.set = 0;
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
/* `a mod b` -> `mod(a, b)`, offered only when both operands are a plain name
 * or number and what stands either side binds looser than QBasic's MOD (which
 * is above + and -, below * and /), so the rewrite cannot regroup anything:
 * `x = a mod 7`, `if i mod 2 = 0 then`, `s + n mod 10`. Not `a * b mod 7`
 * (QBasic reads (a*b) MOD 7), not `-a mod 7`, not `a mod f(x)`. */
static int fix_atom_end(const char *p) {
    const char *q = p;
    if (isalpha((unsigned char)*q) || *q == '_') {
        while (isalnum((unsigned char)*q) || *q == '_') q++;
        char word[40];
        size_t n = (size_t)(q - p);
        if (n >= sizeof word) return 0;
        memcpy(word, p, n);
        word[n] = '\0';
        if (word_is_reserved(word)) return 0;
    } else if (isdigit((unsigned char)*q)) {
        while (isdigit((unsigned char)*q) || *q == '.') q++;
    } else {
        return 0;
    }
    return (int)(q - p);
}

/* a reserved word ends at p (looking left) or starts at p (looking right) */
static int fix_word_reserved(const char *from, const char *to) {
    char word[16];
    size_t n = (size_t)(to - from);
    if (n == 0 || n >= sizeof word) return 0;
    for (size_t i = 0; i < n; i++) word[i] = (char)tolower((unsigned char)from[i]);
    word[n] = '\0';
    return word_is_reserved(word);
}

static void operator_word_fix(gb_parse_ctx *ctx) {
    const char *base = ctx->active_lexer ? ctx->active_lexer->source : NULL;
    const char *left = ctx->tok_prev_start, *left_end = ctx->tok_prev_after;
    if (!base || !left || !left_end || left < base ||
        !(ctx->tok_prev_type == TOKEN_IDENT || ctx->tok_prev_type == TOKEN_NUMBER) ||
        fix_atom_end(left) != (int)(left_end - left)) {
        return;
    }
    /* what stands before the left operand */
    const char *b = left;
    while (b > base && (b[-1] == ' ' || b[-1] == '\t')) b--;
    if (b > base && b[-1] != '\n' && !strchr("=(,[<>+", b[-1])) {
        const char *w = b;
        while (w > base && (isalnum((unsigned char)w[-1]) || w[-1] == '_')) w--;
        if (w == b || !fix_word_reserved(w, b) || (w > base && w[-1] == '.')) {
            return;
        }
    }
    /* `+` only as a binary operator: something an operand ends with before it */
    if (b > base && b[-1] == '+') {
        const char *c = b - 1;
        while (c > base && (c[-1] == ' ' || c[-1] == '\t')) c--;
        if (c == base || !(isalnum((unsigned char)c[-1]) || c[-1] == '_' || c[-1] == ')' ||
                           c[-1] == ']' || c[-1] == '"')) {
            return;
        }
    }
    /* the right operand and what follows it */
    const char *r = ctx->tok_after;
    while (*r == ' ' || *r == '\t') r++;
    int rn = fix_atom_end(r);
    if (rn == 0) return;
    const char *after = r + rn;
    while (*after == ' ' || *after == '\t') after++;
    if (!(*after == '\0' || strchr("\n\r'),]=<>!+-", *after))) {
        const char *w = after;
        while (isalpha((unsigned char)*w)) w++;
        if (w == after || !fix_word_reserved(after, w)) {
            return;
        }
    }
    const char *right_end = r + rn;
    char text[200];
    int n = snprintf(text, sizeof text, "mod(%.*s, %.*s)", (int)(left_end - left), left, rn, r);
    if (n < 0 || (size_t)n >= sizeof text) return;
    fix_offer(ctx, left, right_end, text);
}

/* AN OPERATOR WORD AFTER A NAME (2026-11-25). `7 mod 3` is read as a
 * duration, so duration_unit_error names the remedy; `i mod 7` is a name
 * followed by a name, and said only `unexpected name, expecting `)``. The
 * word must follow something an expression ends with, and be one
 * operator_word_remedy knows, so it names a call that exists. */
static const char *syntax_error_operator_word(gb_parse_ctx *ctx) {
    static _Thread_local char advice[160];
    char lowered[8];
    size_t i = 0;
    for (; ctx->tok_word[i] && i + 1 < sizeof(lowered); i++) {
        lowered[i] = (char)tolower((unsigned char)ctx->tok_word[i]);
    }
    if (ctx->tok_word[i]) {
        return NULL;
    }
    lowered[i] = '\0';
    const char *remedy = operator_word_remedy(lowered);
    TokenType before = ctx->tok_prev_type;
    if (!remedy || ctx->tok_type != TOKEN_IDENT ||
        !(before == TOKEN_IDENT || before == TOKEN_NUMBER || before == TOKEN_STRING ||
          before == TOKEN_RPAREN || before == TOKEN_RBRACKET)) {
        return NULL;
    }
    snprintf(advice, sizeof advice, "'%s' is not an operator in gBASIC; %s", ctx->tok_word, remedy);
    if (strcmp(lowered, "mod") == 0) {
        operator_word_fix(ctx);
    }
    return advice;
}

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

/* `load NAME as ALIAS from "path"` -> `load NAME from "path" as ALIAS`, when
 * the line is exactly that (`p` is just past `load `) and the lookahead is
 * the `from`. */
static void load_alias_fix(gb_parse_ctx *ctx, const char *p) {
    const char *name = p;
    int nn = fix_atom_end(name);
    if (nn == 0 || !isalpha((unsigned char)*name)) return;
    const char *q = name + nn;
    while (*q == ' ' || *q == '\t') q++;
    const char *as = q;
    if (strncmp(q, "as", 2) != 0 || (q[2] != ' ' && q[2] != '\t')) return;
    q += 2;
    while (*q == ' ' || *q == '\t') q++;
    const char *alias = q;
    int an = fix_atom_end(alias);
    if (an == 0 || !isalpha((unsigned char)*alias)) return;
    q = alias + an;
    while (*q == ' ' || *q == '\t') q++;
    if (q != ctx->tok_start) return;
    q += 4;                                     /* `from` */
    while (*q == ' ' || *q == '\t') q++;
    if (*q != '"') return;
    const char *path = q++;
    while (*q && *q != '"' && *q != '\n') {
        if (*q == '\\' && q[1]) q++;
        q++;
    }
    if (*q != '"') return;
    const char *path_end = ++q;
    while (*q == ' ' || *q == '\t' || *q == '\r') q++;
    if (*q && *q != '\n' && *q != '\'') return;
    char text[256];
    int n = snprintf(text, sizeof text, "from %.*s as %.*s", (int)(path_end - path), path, an, alias);
    if (n < 0 || (size_t)n >= sizeof text) return;
    fix_offer(ctx, as, path_end, text);
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
                /* the rest of the line, to a comment, in the parentheses */
                const char *e = after;
                char quote = 0;
                while (*e && *e != '\n' && (quote || *e != '\'')) {
                    if (quote && *e == '\\' && e[1]) {
                        e++;
                    } else if (*e == '"') {
                        quote = !quote;
                    }
                    e++;
                }
                while (e > after && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) {
                    e--;
                }
                char text[200];
                int tn = snprintf(text, sizeof text, "(%.*s)", (int)(e - after), after);
                if (!quote && e > after && tn > 0 && (size_t)tn < sizeof text) {
                    fix_offer(ctx, q, e, text);
                }
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
    /* QBasic's other print separator (2026-11-26): `print a, b` and
       `print(a, b)` said "unexpected `,`", which names no rule */
    if (strcmp(word, "print") == 0 && ctx->tok_type == TOKEN_COMMA) {
        return "`print` takes one expression; join the parts with + -- print a + \" \" + b";
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
    /* AN ALIAS WRITTEN BEFORE THE PATH (2026-11-25): `load x as y from "f"`
       said `unexpected name, expecting end of line` at `from`; the alias goes
       last */
    if (strcmp(word, "load") == 0 && strcmp(ctx->tok_word, "from") == 0) {
        load_alias_fix(ctx, p);
        return "the alias goes last: write `load NAME from \"path\" as ALIAS`";
    }
    if (strcmp(word, "let") == 0) {
        fix_offer(ctx, w, p, "");
        return "`let` is not a gBASIC statement; assign directly (x = 1)";
    }
    if (strcmp(word, "rem") == 0) {
        fix_offer(ctx, w, w + n, "'");
        return "`rem` is not a gBASIC comment; a comment starts with ' and runs to the end of the line";
    }
    /* VB's, Ada's and Python's spellings of a chain (DOGFOOD 2026-11-02): the
       same misdirection, `expecting LPAREN`, for the word every one of those
       readers types */
    if (strcmp(word, "elseif") == 0) {
        fix_offer(ctx, w, w + n, "else if");
        return "`elseif` is not gBASIC; write `else if`, two words, and one `end if` closes the whole chain";
    }
    if (strcmp(word, "elsif") == 0) {
        fix_offer(ctx, w, w + n, "else if");
        return "`elsif` is not gBASIC; write `else if`, two words, and one `end if` closes the whole chain";
    }
    if (strcmp(word, "elif") == 0) {
        fix_offer(ctx, w, w + n, "else if");
        return "`elif` is not gBASIC; write `else if`, two words, and one `end if` closes the whole chain";
    }
    if (strcmp(word, "endif") == 0) {
        fix_offer(ctx, w, w + n, "end if");
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
    { "SQL_OPEN", "`sql`" }, { "SHAPE_OPEN", "`shape`" }, { "CONSIDER_IF", "`if`" },
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
        ctx->fix.set = 0;
        return;
    }
    /* the caller's fix (a type lock's), unless advice below replaces the
       message, which then brings its own or none */
    int fix_set = ctx->fix.set;
    gb_span fix_span = ctx->fix.span;
    char fix_text[sizeof ctx->fix.text];
    memcpy(fix_text, ctx->fix.text, sizeof fix_text);
    ctx->fix.set = 0;
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
        fix_set = 0;
    }
    /* A CLASSIC-BASIC STATEMENT WORD REPLACES the message rather than appending
     * to it, unlike the reserved-word note: bison's own sentence here is about
     * a parenthesis it wanted, which is the wrong advice entirely, so carrying
     * it alongside would leave the misdirection in place beside the fix. */
    const char *basic_word = syntax_error_basic_word(ctx);
    if (basic_word) {
        message = basic_word;
        fix_set = ctx->fix.set;
        fix_span = ctx->fix.span;
        memcpy(fix_text, ctx->fix.text, sizeof fix_text);
    }
    ctx->fix.set = 0;
    const char *operator_word = syntax_error_operator_word(ctx);
    if (operator_word) {
        message = operator_word;
        fix_set = ctx->fix.set;
        fix_span = ctx->fix.span;
        memcpy(fix_text, ctx->fix.text, sizeof fix_text);
    }
    ctx->fix.set = 0;
    /* a line of a shape's own body that is neither a field nor a method */
    /* bison's own sentence only: an action's refusal (`n:int`) says more */
    /* ...and the line opened with a word a field can be named (a keyword is
       one: `next:node`), followed by something that cannot go on a field */
    int after_first_word = 0;
    if (ctx->tok_prev_start && ctx->active_lexer && ctx->active_lexer->source) {
        const char *b = ctx->tok_prev_start;
        while (b > ctx->active_lexer->source && (b[-1] == ' ' || b[-1] == '\t')) b--;
        after_first_word = (b == ctx->active_lexer->source || b[-1] == '\n') &&
                           ctx->tok_type != TOKEN_COLON && ctx->tok_type != TOKEN_OP_EQ &&
                           ctx->tok_type != TOKEN_LPAREN;
    }
    int shape_line = ctx->shape_open && ctx->shape_fn_depth == 0 && !basic_word &&
                     !operator_word &&
                     (ctx->tok_prev_type == TOKEN_NEWLINE || after_first_word) &&
                     ctx->tok_type != TOKEN_NEWLINE && message &&
                     strncmp(message, "syntax error, ", 14) == 0;
    if (shape_line) {
        message = "a shape's body holds fields (`name:type = default`, or `name = value`) and "
                  "methods (`function ... end function`), and this line is neither";
        fix_set = 0;
    }
    char reworded[512];
    const char *reserved = shape_line ? NULL : syntax_error_reserved_word(ctx, message);
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
    if (fix_set) {
        gb_diagnostics_set_fix(ctx->diags, fix_span, fix_text);
    }
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
    ctx->tok_prev_start = ctx->tok_start;
    ctx->tok_prev_after = ctx->tok_after;
    ctx->tok_start = token.start;
    ctx->tok_after = token.start + token.length;
    if (token.type == TOKEN_SHAPE_OPEN) {
        ctx->shape_open = 1;
        ctx->shape_fn_depth = 0;
    } else if (ctx->shape_open && token.type == TOKEN_FUNCTION) {
        ctx->shape_fn_depth += ctx->tok_prev_type == TOKEN_END ? -1 : 1;
    } else if (ctx->shape_open && ctx->tok_prev_type == TOKEN_END && token.type == TOKEN_IDENT &&
               token.length == 5 && strncmp(token.start, "shape", 5) == 0) {
        ctx->shape_open = 0;
    }
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
    case TOKEN_SHAPE_OPEN: return SHAPE_OPEN;
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
            /* QBasic's not-equal, whose message names the one spelling */
            if (token.length == 2 && token.start[0] == '<' && token.start[1] == '>') {
                gb_diagnostics_set_fix(ctx->diags,
                                       (gb_span){ token.line, token.column, token.line, token.column + 2 },
                                       "!=");
            }
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
