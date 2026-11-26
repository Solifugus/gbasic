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
#line 748 "src/parser.y"

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
                if (!arg) {
                    fix_offer_span(ctx, (gb_span){ line, column, end_line, end_column }, near[i].ours);
                }
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

#line 1105 "src/parser.tab.c"

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
#define YYLAST   3811

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  98
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  89
/* YYNRULES -- Number of rules.  */
#define YYNRULES  385
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  917

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
       0,   987,   987,   991,   992,   993,   997,   998,   999,  1000,
    1001,  1002,  1003,  1004,  1005,  1006,  1007,  1008,  1009,  1010,
    1011,  1012,  1013,  1014,  1015,  1016,  1017,  1018,  1019,  1020,
    1021,  1022,  1023,  1024,  1025,  1031,  1042,  1047,  1048,  1064,
    1071,  1085,  1086,  1090,  1091,  1092,  1093,  1097,  1098,  1102,
    1110,  1114,  1115,  1116,  1127,  1127,  1135,  1139,  1140,  1144,
    1145,  1146,  1147,  1151,  1157,  1161,  1164,  1165,  1171,  1176,
    1185,  1198,  1227,  1228,  1229,  1233,  1237,  1253,  1258,  1267,
    1273,  1279,  1286,  1296,  1300,  1321,  1327,  1333,  1339,  1342,
    1348,  1349,  1353,  1354,  1355,  1359,  1360,  1361,  1362,  1363,
    1364,  1365,  1366,  1367,  1368,  1369,  1370,  1371,  1372,  1373,
    1374,  1375,  1376,  1377,  1378,  1379,  1380,  1381,  1382,  1383,
    1384,  1385,  1391,  1402,  1405,  1412,  1421,  1433,  1436,  1442,
    1448,  1454,  1455,  1456,  1457,  1458,  1459,  1475,  1491,  1512,
    1513,  1517,  1521,  1524,  1532,  1538,  1545,  1546,  1550,  1551,
    1571,  1570,  1578,  1577,  1600,  1603,  1606,  1612,  1627,  1631,
    1641,  1649,  1650,  1651,  1656,  1661,  1665,  1670,  1675,  1689,
    1704,  1709,  1720,  1735,  1740,  1746,  1747,  1748,  1752,  1755,
    1758,  1761,  1764,  1770,  1771,  1775,  1776,  1780,  1786,  1787,
    1788,  1789,  1794,  1809,  1814,  1819,  1829,  1833,  1834,  1838,
    1845,  1849,  1858,  1859,  1863,  1864,  1868,  1872,  1879,  1882,
    1885,  1894,  1904,  1907,  1910,  1916,  1923,  1933,  1934,  1935,
    1936,  1937,  1938,  1939,  1940,  1941,  1942,  1943,  1951,  1952,
    1956,  1957,  1963,  1964,  1968,  1969,  1991,  1992,  1996,  1997,
    1998,  2004,  2005,  2006,  2010,  2011,  2012,  2016,  2017,  2018,
    2022,  2023,  2030,  2034,  2035,  2036,  2040,  2041,  2045,  2053,
    2058,  2061,  2066,  2080,  2081,  2082,  2083,  2084,  2085,  2086,
    2087,  2088,  2089,  2093,  2114,  2115,  2116,  2117,  2141,  2142,
    2159,  2165,  2166,  2167,  2168,  2169,  2170,  2171,  2172,  2173,
    2177,  2178,  2182,  2187,  2192,  2198,  2210,  2215,  2223,  2234,
    2247,  2248,  2252,  2253,  2257,  2258,  2262,  2263,  2277,  2281,
    2286,  2287,  2288,  2289,  2290,  2291,  2295,  2296,  2299,  2303,
    2310,  2317,  2324,  2331,  2346,  2353,  2362,  2363,  2367,  2368,
    2369,  2370,  2371,  2372,  2373,  2374,  2375,  2376,  2377,  2378,
    2379,  2380,  2381,  2382,  2383,  2384,  2385,  2386,  2387,  2388,
    2389,  2390,  2391,  2392,  2393,  2394,  2395,  2396,  2397,  2398,
    2399,  2400,  2401,  2402,  2403,  2404,  2405,  2406,  2407,  2408,
    2409,  2410,  2411,  2412,  2413,  2414,  2418,  2419,  2420,  2421,
    2422,  2423,  2431,  2458,  2477,  2478
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

#define YYPACT_NINF (-610)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -610,    50,  1458,  -610,    21,   -10,   139,   183,   237,  -610,
     919,  -610,   807,   919,   270,    85,   177,   919,   919,   291,
     298,   181,   919,   191,   191,   318,   919,   190,   246,    73,
    -610,   957,   336,   377,   402,   153,   321,   351,  -610,  -610,
     325,    64,   328,   341,   360,  -610,  -610,  -610,  -610,  -610,
    -610,  -610,  -610,  -610,   362,  -610,   364,   367,  -610,  -610,
    -610,  -610,   369,   370,   383,   384,   385,   387,   388,   389,
    -610,   274,   919,   919,   358,   401,   408,   466,  -610,  -610,
     403,  1058,  -610,  -610,  -610,  -610,  -610,  1020,   919,  1121,
     483,   405,  -610,  1058,   919,  -610,  -610,  3674,   142,   467,
    -610,    81,   458,  -610,  -610,   630,   307,   326,  -610,   244,
    -610,  -610,   490,   430,  -610,  -610,   410,   320,   497,  -610,
     409,   411,  -610,  -610,   418,   420,  -610,  -610,  -610,   424,
     191,  -610,   131,   413,  -610,   919,   421,   203,    87,  3741,
    -610,  -610,  -610,  -610,  -610,   216,  -610,   470,   432,   428,
     105,  -610,   519,  -610,   336,  -610,  -610,  -610,  -610,  -610,
    -610,   919,   919,  -610,  3473,   919,   330,   520,  -610,  -610,
    -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,
    -610,  3071,  -610,   443,   439,   446,  3138,  3205,   527,  -610,
     919,  -610,  -610,  -610,   296,   450,   451,  -610,   452,   660,
    1190,  -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,
    -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,
    -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,
    -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,
    -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,
     919,  3540,  -610,  2968,  1020,  1020,  1020,  1058,  1058,  -610,
    -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,   522,
    1058,  1058,  1058,  1058,  1058,   919,  3674,  3607,   532,   919,
     919,   919,   919,   537,   520,   339,  1324,  -610,   524,   547,
     547,   191,   182,   191,  -610,   549,   515,  -610,  -610,  -610,
      33,  -610,    59,  -610,   476,   547,  -610,   558,   547,  -610,
     560,   562,   564,   531,  -610,   168,   568,   492,   493,  -610,
    -610,   919,   919,   494,   504,   496,  -610,   489,    34,  -610,
     266,  -610,   919,  -610,   491,   276,   495,   279,   151,   499,
     498,   919,  -610,  -610,  -610,  -610,  -610,   512,  -610,   513,
     500,   521,   525,   526,  -610,  -610,   517,  -610,  -610,  -610,
    -610,  -610,  -610,  -610,  -610,  -610,  -610,   523,   134,   458,
    -610,   307,   307,  1058,   407,   326,   326,  -610,  -610,   265,
     342,  -610,   528,   530,  -610,  -610,  -610,   503,   529,   541,
     566,    56,   919,   613,   520,   919,  1525,   919,   376,   101,
     533,   534,   538,   282,   535,   413,  1592,  -610,   919,  1659,
    -610,  -610,  -610,  -610,   919,   543,  -610,   540,   548,  1726,
     604,  -610,  -610,   558,  -610,   919,   545,   919,   919,  -610,
    -610,   616,   919,   619,  -610,   919,   919,   550,  -610,  -610,
    -610,    46,  -610,   552,   623,   637,  -610,  -610,   556,  -610,
     154,   315,  -610,   919,   919,  -610,  1391,   520,   621,  1020,
     407,  -610,   919,  -610,  3674,   919,   919,   554,  -610,   919,
     919,   919,   644,   559,   603,    61,   561,   605,   631,   919,
     563,   629,   231,   520,   347,   656,   355,   577,  -610,   620,
    -610,   602,   586,  -610,  -610,   584,   611,   678,   590,   334,
    -610,   598,   599,   600,  -610,   919,   609,  -610,  -610,  -610,
    -610,  1257,    52,   684,  -610,  -610,   662,   622,  -610,   851,
    -610,  -610,   626,   627,  1107,   692,  -610,  2932,  -610,  -610,
     350,  -610,   632,   635,  -610,  1793,    -8,   624,   633,   670,
    -610,   919,   919,   719,  -610,   634,   636,   701,  -610,   638,
    -610,  -610,  -610,  -610,  -610,  -610,   725,   727,  -610,   661,
     520,  -610,   133,   520,  -610,  -610,  1860,   640,   641,  -610,
    1927,  -610,   642,  -610,  -610,  -610,  -610,  -610,  -610,  -610,
     646,    72,  -610,   651,   148,    69,   735,   736,  -610,  -610,
     739,    83,   740,   733,  -610,  -610,  -610,   919,  -610,   657,
     658,   919,  -610,   663,  -610,  -610,  -610,  1994,   713,    74,
    -610,   919,  -610,  -610,  -610,   919,  1793,   665,   666,   709,
    -610,  -610,   667,  2865,  -610,  -610,  -610,   231,   668,  2061,
     231,   520,   671,  2128,  2195,   710,  -610,  -610,   702,  2262,
    -610,  2329,   919,   445,   764,   765,  -610,  -610,    88,   619,
     919,   919,    49,   677,   767,  3272,  -610,   679,   676,   681,
     772,   685,   750,   755,  2396,  -610,  -610,   757,  2463,  -610,
     744,   691,  -610,   693,   694,  1793,  1793,  1793,   695,  -610,
    -610,  -610,   919,  2865,  -610,  -610,  -610,   696,   697,   698,
    -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,
     700,  -610,   703,   704,  -610,  -610,   705,   706,   707,   708,
     711,   712,   714,   715,  -610,  -610,  -610,   743,  -610,   732,
    -610,   754,   729,   716,   720,   751,   756,  -610,   737,   738,
     251,   731,   748,   816,   742,  -610,  -610,   749,   821,  3339,
    -610,  -610,   822,   752,   283,   136,  -610,   825,  -610,   758,
    -610,   826,   760,   809,  3004,   810,   768,  -610,  -610,  -610,
    -610,  -610,  -610,  -610,  1793,  1793,   769,  2865,  -610,  -610,
    -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,  -610,
    -610,  -610,  2530,   771,   231,  2597,   773,   777,  -610,  -610,
     779,   781,  3406,   547,   828,  -610,  -610,  -610,   782,   784,
    -610,   832,   786,   306,   144,   156,   787,  -610,   792,   844,
     793,   162,  -610,   800,  -610,   804,  -610,   805,   806,  -610,
    1793,  1793,  -610,  -610,  -610,   813,  -610,  -610,   848,  -610,
    -610,  -610,  -610,   808,   309,   791,  -610,  2664,  -610,   919,
     199,   812,  -610,   814,   898,   899,  -610,  -610,   815,  -610,
     902,  -610,  1391,  -610,  1391,   621,  -610,  -610,  -610,  1793,
     817,   819,  -610,   823,   824,   853,  -610,   905,  -610,  -610,
     827,   836,   206,   242,  -610,  2731,   837,  -610,  -610,  -610,
    -610,  -610,  -610,   267,  -610,  -610,   838,   839,   278,  -610,
    -610,   914,   915,   860,  -610,   921,   294,  2798,  -610,  -610,
     930,   842,   846,   849,   850,   933,   937,   852,  -610,  -610,
    -610,  -610,   854,   861,  -610,  -610,  -610
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       3,     0,     2,     1,    51,     0,     0,     0,     0,    35,
       0,    52,     0,     0,     0,     0,     0,     0,     0,   202,
     204,     0,   197,     0,     0,     0,     0,     0,     0,     0,
      53,     0,     0,     0,     0,     0,     0,     0,     4,     5,
       0,     0,    47,     0,     0,     9,    12,    14,    13,    15,
      16,    17,    18,    19,     0,    21,     0,     0,    22,    11,
      10,    25,     0,     0,     0,     0,     0,     0,     0,     0,
      34,     0,   300,   300,     0,     0,     0,   275,    51,   278,
       0,     0,   273,   282,   283,   284,   285,     0,     0,     0,
       0,     0,   281,     0,     0,   384,   384,     0,   292,     0,
     228,   230,   232,   234,   236,   238,   241,   244,   247,   250,
     256,   289,   277,     0,    63,    66,     0,     0,     0,     3,
       0,     0,   203,   205,     0,     0,   198,   200,   201,    51,
       0,   185,     0,   148,   145,     0,     0,     0,     0,     0,
     196,    59,    61,    60,    62,   139,    57,     0,     0,     0,
     132,   134,   131,   133,     0,     6,    56,    43,    44,    45,
      46,     0,     0,    54,     0,     0,     0,   199,     7,     8,
      20,    23,    24,    26,    27,    28,    29,    30,    31,    32,
      33,     0,   302,     0,   301,     0,     0,     0,     0,   298,
     300,   252,   237,   229,   253,     0,     0,   251,     0,     0,
       0,   328,   329,   375,   344,   341,   342,   333,   348,   356,
     357,   358,   374,   354,   355,   352,   353,   339,   337,   363,
     343,   334,   372,   346,   347,   335,   338,   345,   371,   359,
     360,   366,   349,   361,   362,   369,   373,   340,   370,   336,
     330,   331,   332,   367,   368,   365,   350,   351,   364,   274,
     300,     0,   279,     0,     0,     0,     0,     0,     0,   263,
     264,   265,   266,   267,   268,   269,   270,   271,   272,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     3,     0,   306,
     306,     0,     0,     0,     3,     0,   146,     3,   195,   194,
       0,   193,     0,   190,   328,   306,    58,     0,   306,     3,
       0,     0,     0,     0,    36,     0,     0,   328,     0,    50,
      37,     0,     0,    41,     0,   328,   325,     0,     0,   324,
       0,    65,     0,    67,     0,     0,     0,     0,     0,     0,
       0,   300,   276,   286,   287,   385,   304,   384,   290,   384,
       0,     0,   296,    51,     3,   217,    47,   218,   219,   220,
     221,   222,   223,   224,   225,   226,   227,     0,     0,   233,
     235,   242,   243,     0,   239,   245,   246,   248,   249,     0,
       0,   326,   328,     0,   259,   299,    64,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    90,   316,
       0,   307,     0,     0,     0,   149,     0,   186,     0,     0,
     192,   191,   188,   189,   300,     0,   141,     0,     0,     0,
     137,   135,   136,     0,    48,     0,     0,   300,   300,    40,
      38,     0,     0,     0,   152,     0,     0,     0,   384,   303,
     161,     0,   155,     0,     0,     0,   280,   254,     0,   384,
       0,     0,   293,   300,   300,   294,     0,     0,   212,     0,
     240,   257,     0,   260,     0,   300,   300,     0,     3,     0,
       0,     0,     0,     0,     0,     0,     0,    52,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     3,    52,
     147,    52,     0,   140,     3,     0,    52,     0,     0,     0,
      55,     0,     0,     0,    39,   382,     0,   175,   376,   377,
     150,     0,     0,     0,   161,   154,     0,     0,   255,     0,
     288,   291,     0,     0,     0,    52,   206,     0,   207,   231,
       0,   327,     0,     0,     3,     0,     0,     0,     0,     0,
       3,     0,     0,     0,    85,     0,     0,     0,    92,     0,
     308,   311,   312,   313,   314,   315,     0,     0,   317,   319,
       0,     3,   318,     0,     3,     3,     0,     0,     0,    70,
       0,     3,     0,   138,     3,    49,    68,    69,    42,   383,
       0,     0,   175,   328,     0,     0,     0,     0,   162,   163,
       0,     0,     0,     0,   305,   295,   297,     0,     3,     0,
       0,     0,     3,     0,   258,   261,   262,     0,    52,    53,
      75,     0,     3,     3,     3,     0,     0,     0,     0,     0,
      86,    92,     0,    91,    87,   310,   309,     0,     0,     0,
       0,     0,     0,     0,     0,    52,   143,   187,    52,     0,
     130,     0,     0,     0,     0,     0,   176,   177,     0,     0,
       0,     0,     0,     0,     0,     0,   161,     0,     0,     0,
       0,     0,     0,     0,     0,   209,   208,     0,     0,   213,
      52,     0,    73,     0,     0,     0,     0,     0,     0,    76,
       3,     3,     0,    88,    92,   122,    93,     0,     0,     0,
      98,   100,   102,   101,   103,    94,   104,   105,   106,   107,
       0,   109,     0,     0,    99,   112,     0,     0,     0,     0,
       0,     0,     0,     0,   121,   320,     3,    52,   323,   321,
       3,    52,    52,     0,     0,    52,    52,   378,     0,   183,
       0,     0,     0,     0,     0,   379,   380,     0,     0,     0,
     161,   161,     0,     0,     0,     0,   159,     0,   161,     0,
     156,     0,     0,    52,     0,    52,     0,    72,    74,     3,
      83,    77,    79,     3,     0,     0,     0,    89,    95,    96,
      97,   108,   110,   111,   113,   114,   115,   116,   117,   118,
     119,   120,     0,     0,     0,     0,     0,     0,   142,   127,
       0,     0,     0,   306,     0,   178,   153,     3,     0,     0,
     161,     0,     0,     0,     0,     0,     0,   164,     0,     0,
       0,     0,   158,     0,     3,     0,     3,     0,     0,    71,
       0,     0,    78,    80,     3,    52,   123,   322,    52,   124,
     144,   129,   128,     0,     0,     0,   184,     0,   151,     0,
       0,     0,   166,     0,     0,     0,   161,   161,     0,     3,
       0,   157,     0,   210,     0,   212,   214,    84,    81,     0,
       0,     0,   175,     0,     0,    52,   381,     0,   161,   165,
       0,     0,     0,     0,   167,     0,     0,   211,   216,   215,
      82,   125,   126,     0,   175,     3,     0,     0,     0,   169,
     171,     0,     0,    52,   160,     0,     0,     0,   182,   170,
       0,     0,     0,     0,     0,     0,    52,     0,   172,   168,
     174,   181,     0,     0,   173,   180,   179
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -610,  -610,   -22,  -610,  -248,  -267,   797,  -610,    -2,   862,
    -610,  -610,   829,  -247,  -241,  -599,  -609,  -593,  -590,  -589,
    -587,  -610,  -610,  -564,  -610,  -585,  -583,  -575,  -571,  -239,
     799,   546,  -565,  -561,  -559,  -121,  -610,  -610,  -610,  -610,
    -558,  -510,  -610,  -555,  -610,  -610,   664,  -554,  -227,  -210,
    -209,  -553,  -208,  -204,  -200,  -194,  -552,  -475,   113,  -509,
      29,   511,   717,   718,   -77,  -610,  -259,   194,   196,   -73,
     883,   722,  -610,   645,  -610,  -610,  -610,    31,  -287,  -610,
    -277,  -598,  -610,   464,  -610,   -78,  -184,   327,   -95
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    39,    40,   324,   165,    41,    98,   166,
     316,   145,   146,    43,    44,    45,   610,    46,    47,    48,
      49,   398,   481,   623,   695,    50,    51,    52,    53,    54,
     147,   417,    55,    56,    57,   132,    58,   582,   507,    59,
      60,   512,   589,   581,   647,   730,   133,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,   526,   528,   367,
     182,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   270,   110,   111,   252,   455,   112,   183,   184,   347,
     400,   558,   401,   328,   380,   329,   330,   506,   199
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      42,   200,   335,   337,   591,   355,   357,   679,   191,   292,
     192,   374,   358,   402,   359,   599,   349,   391,   603,   249,
     197,   127,   128,   131,   690,    71,   360,   648,   415,   715,
     691,   418,   718,   692,   693,   611,   694,   410,   696,    99,
     697,   114,   115,   361,   362,   363,   120,   121,   698,   364,
       3,   126,   699,   365,   737,   134,   585,   683,   701,   366,
     140,   249,   702,   412,   703,   704,   760,   761,   762,   705,
     709,   714,   156,   652,   653,    73,   643,   137,    78,   586,
     157,   158,   159,   160,   690,   612,   319,   585,   513,   117,
     691,   738,   643,   692,   693,   411,   694,   286,   696,   644,
     697,    11,   471,   254,   185,   435,    72,   542,   698,   310,
     660,   654,   699,   587,   460,   733,   255,   193,   701,   311,
     767,   413,   702,   198,   703,   704,   436,   475,   131,   705,
     709,   714,   118,   645,   739,   161,    30,   138,   499,   514,
     585,   302,   740,    74,   587,   588,   745,   472,   585,   645,
     303,   162,   543,   163,   655,   822,   823,   150,   151,   459,
     585,   164,   656,   809,   296,   646,   585,   672,   690,   255,
     403,   844,   482,   352,   691,   530,   588,   692,   693,   370,
     694,   646,   696,   845,   697,   124,   827,    75,   125,   850,
     314,   315,   698,   483,   320,    78,   699,   587,   381,   384,
     377,   378,   701,   585,   630,   587,   702,   298,   703,   704,
     585,   857,   858,   705,   709,   714,   559,   587,    11,   650,
     141,   339,   293,   587,   294,   631,   867,   250,   346,   588,
     804,   805,   299,   891,   550,   135,   551,   588,   811,   251,
     651,    76,   520,   142,   444,   817,   585,   345,   445,   588,
     880,   356,   450,    30,   451,   588,   424,   300,   143,   425,
     587,   552,   553,   554,   555,   396,   301,   587,   404,   892,
     119,   643,   406,   293,   116,   409,   355,   357,   144,   355,
     357,   350,   585,   358,    42,   359,   358,   419,   359,   131,
     840,   131,   588,   628,   895,   122,   632,   360,   643,   588,
     360,   305,   123,   587,   379,   900,   136,   883,   386,   387,
     388,   389,   556,   557,   361,   362,   363,   361,   362,   363,
     364,   905,   129,   364,   365,   152,   153,   365,   645,   896,
     366,   275,   456,   366,   340,   588,   872,   873,   276,   587,
     141,   277,   794,   511,   795,    11,   157,   158,   159,   160,
     429,   430,   437,   461,   519,   645,   462,   438,   888,   181,
     646,   439,   441,   142,   719,   443,   281,   438,   487,   808,
     438,   588,   448,   293,   438,   687,   688,   877,   143,   878,
      30,   148,   689,   275,   700,   392,   531,   646,   271,   272,
     276,   282,   843,   277,    42,   863,   706,   438,   144,   479,
     438,   321,   480,   130,    42,   521,   149,    42,   345,   273,
     274,   283,   284,   707,   708,   710,   154,    42,   155,   711,
     167,   473,   575,   712,   476,   332,   478,   257,   258,   713,
     393,   394,   463,   464,   168,   687,   688,   490,   604,   560,
     561,   332,   689,   186,   700,   492,   535,   563,   564,   728,
     729,   371,   372,   169,    42,   170,   706,   171,   501,   502,
     172,   504,   173,   174,   508,   509,   566,   375,   376,   188,
     189,   744,   570,   707,   708,   710,   175,   176,   177,   711,
     178,   179,   180,   712,   522,   523,   187,   195,   190,   713,
     196,   253,   256,   278,   279,   280,   532,   533,   536,   537,
     538,   285,   287,   289,   288,   290,   355,   357,   547,   291,
     295,   307,   607,   358,   297,   359,   835,   308,   616,   687,
     688,   309,   356,   312,   323,   356,   689,   360,   700,   331,
     332,   338,   333,    42,   579,   341,   385,   342,   343,   629,
     706,   390,   633,   634,   361,   362,   363,   397,   594,   639,
     364,   399,   641,   407,   365,   803,   408,   707,   708,   710,
     366,   414,   416,   711,    42,   420,   421,   712,    42,   422,
     617,   618,   423,   713,   426,   432,   664,   427,   428,   431,
     668,   433,   434,   469,   440,   446,   452,    96,   442,   467,
     675,   676,   677,   259,   260,   261,   262,   263,   264,   265,
     266,   267,   268,   449,   438,    42,   453,   673,   834,   457,
     454,    72,   470,   465,    42,   466,   458,   474,   497,   484,
     503,    42,   468,   505,   486,   485,   663,    42,   488,   493,
     667,    42,    42,   494,   495,   500,   516,    42,   156,    42,
     674,   517,   518,   510,   678,   515,   527,   534,   539,   541,
     257,   258,   540,   545,   544,   546,   548,   549,   764,   765,
     562,   568,    42,    77,    78,    79,    42,    80,    81,    82,
     565,   727,   569,    42,    42,    42,   567,   571,   572,   735,
     736,    42,   573,   574,   576,   577,   578,    11,   590,   592,
      83,    84,    85,    86,   782,   580,    87,    88,   785,    89,
      90,   259,   260,   261,   262,   263,   264,   265,   266,   267,
     268,   766,   595,   596,   600,   593,   615,   613,   605,   163,
      91,   606,    30,   619,    92,   622,   614,   620,   625,   621,
     626,   624,   627,   636,   637,   640,   649,   820,   642,   657,
     658,   821,    93,   659,   661,    94,   662,    95,   344,    96,
     665,   666,   356,   345,   671,   682,   669,    97,   680,   681,
     684,   716,    42,    42,   720,    42,   723,   724,   731,   732,
     741,   742,   746,   747,   748,   837,   749,   751,   750,   752,
      42,   754,   756,    42,   757,   787,   758,   759,   763,   768,
     769,   770,   852,   771,   854,   783,   772,   773,   774,   775,
     776,   777,   859,   784,   778,   779,   786,   780,   781,   788,
      77,    78,    79,   789,    80,    81,    82,   790,    42,    42,
     798,   791,   792,   793,   796,   801,   806,   875,   799,   810,
     813,   815,   818,   836,    11,    42,   841,    83,    84,    85,
      86,   797,   800,    87,    88,   807,    89,    90,   848,   113,
      42,   812,    42,   814,    77,    78,    79,    42,    80,    81,
      82,   819,   824,   897,   826,   860,   829,    91,   866,    30,
     830,    92,   831,    42,   832,   838,   839,   864,    11,   842,
     846,    83,    84,    85,    86,   847,   849,    87,    88,    93,
      89,    90,    94,   851,    95,    42,    96,   853,   855,   856,
     861,   862,   870,   871,    97,   868,   876,   869,   874,   887,
     881,    91,   882,    30,   886,    92,   884,   885,   901,   902,
     889,   903,    77,    78,    79,   904,    80,    81,    82,   890,
     894,   898,   899,    93,   907,   908,    94,   912,    95,   909,
      96,   913,   910,   911,   345,   914,    11,   915,    97,    83,
      84,    85,    86,   313,   916,    87,    88,   405,    89,    90,
      77,    78,    79,   322,    80,    81,    82,   269,   879,   498,
     529,   368,   194,   369,   306,   584,   734,     0,     0,    91,
       0,    30,     0,    92,    11,   447,     0,    83,    84,    85,
      86,   373,     0,    87,    88,     0,    89,    90,     0,     0,
       0,    93,     0,     0,    94,     0,    95,     0,    96,     0,
       0,     0,     0,     0,     0,     0,    97,    91,     0,    30,
       0,    92,     0,    77,    78,    79,     0,    80,    81,    82,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    93,
       0,     0,    94,     0,    95,     0,    96,    11,     0,     0,
      83,    84,    85,    86,   139,     0,    87,     0,     0,    89,
      90,    77,    78,    79,     0,    80,    81,    82,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      91,     0,    30,     0,    92,    11,     0,     0,    83,    84,
      85,    86,     0,     0,     0,     0,     0,    89,    90,     0,
       0,     0,    93,     0,     0,    94,     0,    95,     0,    96,
       0,   353,     0,     0,     5,     0,     0,    97,    91,     0,
      30,     0,    92,     0,    77,    78,    79,     0,    80,   597,
      82,     0,     0,     0,    11,     0,    12,     0,     0,     0,
      93,     0,     0,    94,    13,    95,     0,    96,    11,     0,
       0,    83,    84,    85,    86,    97,     0,    19,    20,     0,
      22,    23,    24,     0,     0,     0,     0,     0,    29,    30,
       0,    31,     0,     0,     0,    35,    36,     0,     0,     0,
       0,    91,     0,    30,     0,    92,     0,     0,     0,     0,
       0,     0,     0,     0,   325,   326,     0,     0,     0,     0,
     598,     0,     0,     0,   202,   203,    94,     0,    95,     0,
      96,     0,   204,     0,   205,   206,     0,   207,    97,   208,
     209,   210,   211,   212,   213,   214,   215,   216,   217,   218,
     219,   220,   221,   222,   223,   224,   225,   226,   227,   228,
     229,   230,   231,   232,   233,   234,   235,   236,     0,   237,
     238,   239,   240,   241,   242,   243,   244,   245,   246,   247,
     248,   583,   326,     0,     0,     0,     0,     0,     0,     0,
       0,   202,   203,     0,     0,     0,     0,     0,     0,   204,
     348,   205,   206,   345,   207,     0,   208,   209,   210,   211,
     212,   213,   214,   215,   216,   217,   218,   219,   220,   221,
     222,   223,   224,   225,   226,   227,   228,   229,   230,   231,
     232,   233,   234,   235,   236,     0,   237,   238,   239,   240,
     241,   242,   243,   244,   245,   246,   247,   248,     4,     0,
       0,     5,     0,     0,     6,     7,     8,     0,     0,     9,
       0,     0,     0,     0,     0,     0,    10,     0,     0,     0,
     345,    11,     0,    12,     0,     0,     0,     0,     0,     0,
       0,    13,    14,     0,     0,    15,     0,     0,    16,   395,
       0,     0,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,     0,    29,    30,     0,    31,    32,
      33,    34,    35,    36,    37,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,   524,    38,   525,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,    14,
       0,     0,    15,     0,     0,    16,     0,     0,     0,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,     0,    29,    30,     0,    31,    32,    33,    34,    35,
      36,    37,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     0,     0,     9,     0,     0,     0,     0,     0,     0,
      10,     0,     0,     0,    38,    11,     0,    12,     0,     0,
       0,     0,     0,     0,     0,    13,    14,     0,     0,    15,
       0,     0,    16,     0,     0,     0,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,     0,    29,
      30,     0,    31,    32,    33,    34,    35,    36,    37,     4,
       0,     0,     5,     0,     0,     6,     7,     8,     0,     0,
       9,     0,     0,     0,     0,     0,     0,    10,     0,     0,
       0,    38,   477,     0,    12,     0,     0,     0,     0,     0,
       0,     0,    13,    14,     0,     0,    15,     0,     0,    16,
       0,     0,     0,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,    28,     0,    29,    30,     0,    31,
      32,    33,    34,    35,    36,    37,     4,     0,     0,     5,
       0,     0,     6,     7,     8,     0,     0,     9,     0,     0,
       0,     0,     0,     0,    10,     0,     0,     0,    38,   489,
       0,    12,     0,     0,     0,     0,     0,     0,     0,    13,
      14,     0,     0,    15,     0,     0,    16,     0,     0,     0,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,     0,    29,    30,     0,    31,    32,    33,    34,
      35,    36,    37,     4,     0,     0,     5,     0,     0,     6,
       7,     8,     0,     0,     9,     0,     0,     0,     0,     0,
       0,    10,     0,     0,     0,    38,   491,     0,    12,     0,
       0,     0,     0,     0,     0,     0,    13,    14,     0,     0,
      15,     0,     0,    16,     0,     0,     0,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,     0,
      29,    30,     0,    31,    32,    33,    34,    35,    36,    37,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     0,
       0,     9,     0,     0,     0,     0,     0,     0,    10,     0,
       0,     0,    38,   496,     0,    12,     0,     0,     0,     0,
       0,     0,     0,    13,    14,     0,     0,    15,     0,     0,
      16,     0,     0,     0,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,     0,    29,    30,     0,
      31,    32,    33,    34,    35,    36,    37,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    38,
     608,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,    14,     0,     0,    15,     0,     0,    16,     0,     0,
       0,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,     0,    29,   609,     0,    31,    32,    33,
      34,    35,    36,    37,     4,     0,     0,     5,     0,     0,
       6,     7,     8,     0,     0,     9,     0,     0,     0,     0,
       0,     0,    10,     0,     0,     0,    38,   635,     0,    12,
       0,     0,     0,     0,     0,     0,     0,    13,    14,     0,
       0,    15,     0,     0,    16,     0,     0,     0,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
       0,    29,    30,     0,    31,    32,    33,    34,    35,    36,
      37,     4,     0,     0,     5,     0,     0,     6,     7,     8,
       0,     0,     9,     0,     0,     0,     0,     0,     0,    10,
       0,     0,     0,    38,   638,     0,    12,     0,     0,     0,
       0,     0,     0,     0,    13,    14,     0,     0,    15,     0,
       0,    16,     0,     0,     0,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,     0,    29,    30,
       0,    31,    32,    33,    34,    35,    36,    37,     4,     0,
       0,     5,     0,     0,     6,     7,     8,     0,     0,     9,
       0,     0,     0,     0,     0,     0,    10,     0,     0,     0,
      38,   670,     0,    12,     0,     0,     0,     0,     0,     0,
       0,    13,    14,     0,     0,    15,     0,     0,    16,     0,
       0,     0,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,     0,    29,    30,     0,    31,    32,
      33,    34,    35,    36,    37,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    38,   717,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,    14,
       0,     0,    15,     0,     0,    16,     0,     0,     0,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,     0,    29,    30,     0,    31,    32,    33,    34,    35,
      36,    37,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     0,     0,     9,     0,     0,     0,     0,     0,     0,
      10,     0,     0,     0,    38,   721,     0,    12,     0,     0,
       0,     0,     0,     0,     0,    13,    14,     0,     0,    15,
       0,     0,    16,     0,     0,     0,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,     0,    29,
      30,     0,    31,    32,    33,    34,    35,    36,    37,     4,
       0,     0,     5,     0,     0,     6,     7,     8,     0,     0,
       9,     0,     0,     0,     0,     0,     0,    10,     0,     0,
       0,    38,   722,     0,    12,     0,     0,     0,     0,     0,
       0,     0,    13,    14,     0,     0,    15,     0,     0,    16,
       0,     0,     0,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,    28,     0,    29,    30,     0,    31,
      32,    33,    34,    35,    36,    37,     4,     0,     0,     5,
       0,     0,     6,     7,     8,     0,     0,     9,     0,     0,
       0,     0,     0,     0,    10,     0,     0,     0,    38,   725,
       0,    12,     0,     0,     0,     0,     0,     0,     0,    13,
      14,     0,     0,    15,     0,     0,    16,     0,     0,     0,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,     0,    29,    30,     0,    31,    32,    33,    34,
      35,    36,    37,     4,     0,     0,     5,     0,     0,     6,
       7,     8,     0,     0,     9,     0,     0,     0,     0,     0,
       0,    10,     0,     0,     0,    38,   726,     0,    12,     0,
       0,     0,     0,     0,     0,     0,    13,    14,     0,     0,
      15,     0,     0,    16,     0,     0,     0,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,     0,
      29,    30,     0,    31,    32,    33,    34,    35,    36,    37,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     0,
       0,     9,     0,     0,     0,     0,     0,     0,    10,     0,
       0,     0,    38,   753,     0,    12,     0,     0,     0,     0,
       0,     0,     0,    13,    14,     0,     0,    15,     0,     0,
      16,     0,     0,     0,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,     0,    29,    30,     0,
      31,    32,    33,    34,    35,    36,    37,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    38,
     755,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,    14,     0,     0,    15,     0,     0,    16,     0,     0,
       0,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,     0,    29,    30,     0,    31,    32,    33,
      34,    35,    36,    37,     4,     0,     0,     5,     0,     0,
       6,     7,     8,     0,     0,     9,     0,     0,     0,     0,
       0,     0,    10,     0,     0,     0,    38,   825,     0,    12,
       0,     0,     0,     0,     0,     0,     0,    13,    14,     0,
       0,    15,     0,     0,    16,     0,     0,     0,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
       0,    29,    30,     0,    31,    32,    33,    34,    35,    36,
      37,     4,     0,     0,     5,     0,     0,     6,     7,     8,
       0,     0,     9,     0,     0,     0,     0,     0,     0,    10,
       0,     0,     0,    38,   828,     0,    12,     0,     0,     0,
       0,     0,     0,     0,    13,    14,     0,     0,    15,     0,
       0,    16,     0,     0,     0,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,     0,    29,    30,
       0,    31,    32,    33,    34,    35,    36,    37,     4,     0,
       0,     5,     0,     0,     6,     7,     8,     0,     0,     9,
       0,     0,     0,     0,     0,     0,    10,     0,     0,     0,
      38,   865,     0,    12,     0,     0,     0,     0,     0,     0,
       0,    13,    14,     0,     0,    15,     0,     0,    16,     0,
       0,     0,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,     0,    29,    30,     0,    31,    32,
      33,    34,    35,    36,    37,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    38,   893,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,    14,
       0,     0,    15,     0,     0,    16,     0,     0,     0,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,     0,    29,    30,     0,    31,    32,    33,    34,    35,
      36,    37,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     0,     0,     9,     0,     0,     0,     0,     0,     0,
      10,     0,     0,     0,    38,   906,     0,    12,     0,     0,
       0,     0,     0,     0,     0,    13,    14,     0,     0,    15,
       0,     0,    16,     0,     0,     0,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,     0,    29,
      30,     0,    31,    32,    33,    34,    35,    36,    37,   353,
       0,     0,     5,     0,     0,     6,     0,     0,     0,     0,
     685,     0,     0,     0,     0,     0,     0,    10,     0,     0,
       0,    38,    11,     0,    12,     0,     0,     0,     0,     0,
       0,     0,    13,    14,     0,     0,    15,     0,     0,    16,
       0,     0,     0,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,    28,     0,    29,    30,     0,    31,
      32,    33,    34,    35,    36,    37,   353,     0,     0,     5,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   601,     0,     0,     0,   686,    11,
       0,    12,     0,     0,     0,     0,     0,     0,     0,    13,
       0,     0,   353,     0,     0,     5,     0,     0,     0,     0,
       0,     0,    19,    20,     0,    22,    23,    24,     0,     0,
       0,     0,     0,    29,    30,    11,    31,    12,     0,     0,
      35,    36,     0,     0,     0,    13,     0,     0,   353,     0,
       0,     5,     0,     0,     0,     0,     0,     0,    19,    20,
       0,    22,    23,    24,     0,   602,     0,     0,     0,    29,
      30,    11,    31,    12,     0,     0,    35,    36,     0,     0,
       0,    13,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    19,    20,     0,    22,    23,    24,
       0,   354,     0,     0,     0,    29,    30,     0,    31,     0,
       0,     0,    35,    36,     0,   325,   326,     0,     0,     0,
       0,     0,     0,     0,     0,   202,   203,     0,     0,     0,
       0,     0,     0,   204,     0,   205,   206,   816,   207,     0,
     208,   209,   210,   211,   212,   213,   214,   215,   216,   217,
     218,   219,   220,   221,   222,   223,   224,   225,   226,   227,
     228,   229,   230,   231,   232,   233,   234,   235,   236,     0,
     237,   238,   239,   240,   241,   242,   243,   244,   245,   246,
     247,   248,   325,   326,     0,     0,     0,     0,     0,     0,
       0,     0,   202,   203,     0,     0,     0,   327,     0,     0,
     204,     0,   205,   206,     0,   207,     0,   208,   209,   210,
     211,   212,   213,   214,   215,   216,   217,   218,   219,   220,
     221,   222,   223,   224,   225,   226,   227,   228,   229,   230,
     231,   232,   233,   234,   235,   236,     0,   237,   238,   239,
     240,   241,   242,   243,   244,   245,   246,   247,   248,   325,
     326,     0,     0,     0,     0,     0,     0,     0,     0,   202,
     203,     0,     0,     0,   334,     0,     0,   204,     0,   205,
     206,     0,   207,     0,   208,   209,   210,   211,   212,   213,
     214,   215,   216,   217,   218,   219,   220,   221,   222,   223,
     224,   225,   226,   227,   228,   229,   230,   231,   232,   233,
     234,   235,   236,     0,   237,   238,   239,   240,   241,   242,
     243,   244,   245,   246,   247,   248,   325,   326,     0,     0,
       0,     0,     0,     0,     0,     0,   202,   203,     0,     0,
       0,   336,     0,     0,   204,     0,   205,   206,     0,   207,
       0,   208,   209,   210,   211,   212,   213,   214,   215,   216,
     217,   218,   219,   220,   221,   222,   223,   224,   225,   226,
     227,   228,   229,   230,   231,   232,   233,   234,   235,   236,
       0,   237,   238,   239,   240,   241,   242,   243,   244,   245,
     246,   247,   248,   325,   326,     0,     0,     0,     0,     0,
       0,     0,     0,   202,   203,     0,     0,     0,   743,     0,
       0,   204,     0,   205,   206,     0,   207,     0,   208,   209,
     210,   211,   212,   213,   214,   215,   216,   217,   218,   219,
     220,   221,   222,   223,   224,   225,   226,   227,   228,   229,
     230,   231,   232,   233,   234,   235,   236,     0,   237,   238,
     239,   240,   241,   242,   243,   244,   245,   246,   247,   248,
     325,   326,     0,     0,     0,     0,     0,     0,     0,     0,
     202,   203,     0,     0,     0,   802,     0,     0,   204,     0,
     205,   206,     0,   207,     0,   208,   209,   210,   211,   212,
     213,   214,   215,   216,   217,   218,   219,   220,   221,   222,
     223,   224,   225,   226,   227,   228,   229,   230,   231,   232,
     233,   234,   235,   236,     0,   237,   238,   239,   240,   241,
     242,   243,   244,   245,   246,   247,   248,   317,     0,     0,
     318,     0,     0,     0,     0,     0,     0,   202,   203,     0,
       0,     0,   833,     0,     0,   204,     0,   205,   206,     0,
     207,     0,   208,   209,   210,   211,   212,   213,   214,   215,
     216,   217,   218,   219,   220,   221,   222,   223,   224,   225,
     226,   227,   228,   229,   230,   231,   232,   233,   234,   235,
     236,     0,   237,   238,   239,   240,   241,   242,   243,   244,
     245,   246,   247,   248,   201,     0,     0,   351,     0,     0,
       0,     0,     0,     0,   202,   203,     0,     0,     0,     0,
       0,     0,   204,     0,   205,   206,     0,   207,     0,   208,
     209,   210,   211,   212,   213,   214,   215,   216,   217,   218,
     219,   220,   221,   222,   223,   224,   225,   226,   227,   228,
     229,   230,   231,   232,   233,   234,   235,   236,     0,   237,
     238,   239,   240,   241,   242,   243,   244,   245,   246,   247,
     248,   382,     0,     0,   383,     0,     0,     0,     0,     0,
       0,   202,   203,     0,     0,     0,     0,     0,     0,   204,
       0,   205,   206,     0,   207,     0,   208,   209,   210,   211,
     212,   213,   214,   215,   216,   217,   218,   219,   220,   221,
     222,   223,   224,   225,   226,   227,   228,   229,   230,   231,
     232,   233,   234,   235,   236,     0,   237,   238,   239,   240,
     241,   242,   243,   244,   245,   246,   247,   248,   201,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   202,   203,
       0,     0,     0,     0,     0,     0,   204,     0,   205,   206,
       0,   207,     0,   208,   209,   210,   211,   212,   213,   214,
     215,   216,   217,   218,   219,   220,   221,   222,   223,   224,
     225,   226,   227,   228,   229,   230,   231,   232,   233,   234,
     235,   236,     0,   237,   238,   239,   240,   241,   242,   243,
     244,   245,   246,   247,   248,   304,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   202,   203,     0,     0,     0,
       0,     0,     0,   204,     0,   205,   206,     0,   207,     0,
     208,   209,   210,   211,   212,   213,   214,   215,   216,   217,
     218,   219,   220,   221,   222,   223,   224,   225,   226,   227,
     228,   229,   230,   231,   232,   233,   234,   235,   236,     0,
     237,   238,   239,   240,   241,   242,   243,   244,   245,   246,
     247,   248
};

static const yytype_int16 yycheck[] =
{
       2,    96,   186,   187,   514,   253,   253,   616,    81,   130,
      87,   270,   253,   290,   253,   524,   200,   284,   527,    97,
      93,    23,    24,    25,   623,     4,   253,   582,   305,   627,
     623,   308,   630,   623,   623,    43,   623,     4,   623,    10,
     623,    12,    13,   253,   253,   253,    17,    18,   623,   253,
       0,    22,   623,   253,     5,    26,     4,   621,   623,   253,
      31,   139,   623,     4,   623,   623,   675,   676,   677,   623,
     623,   623,     8,     4,     5,    85,     4,     4,     4,    27,
      16,    17,    18,    19,   683,    93,   164,     4,    42,     4,
     683,    42,     4,   683,   683,    62,   683,   119,   683,    27,
     683,    27,    46,    22,    73,    71,    85,    46,   683,     4,
      27,    42,   683,    61,   373,    27,    35,    88,   683,    14,
     684,    62,   683,    94,   683,   683,    92,   394,   130,   683,
     683,   683,    47,    61,    85,    71,    62,    64,   425,    93,
       4,    54,    93,     4,    61,    93,   656,    91,     4,    61,
      63,    87,    91,    89,    85,   764,   765,     4,     5,    25,
       4,    97,    93,    27,   135,    93,     4,    93,   767,    35,
     291,    27,    71,   251,   767,   462,    93,   767,   767,   256,
     767,    93,   767,    27,   767,     4,   784,     4,     7,    27,
     161,   162,   767,    92,   165,     4,   767,    61,   276,   277,
     273,   274,   767,     4,    71,    61,   767,     4,   767,   767,
       4,   820,   821,   767,   767,   767,   483,    61,    27,    71,
       4,   190,    91,    61,    93,    92,    27,    85,   199,    93,
     740,   741,    29,    27,     3,    45,     5,    93,   748,    97,
      92,     4,    88,    27,    93,   754,     4,    93,    97,    93,
     859,   253,   347,    62,   349,    93,    88,    54,    42,    91,
      61,    30,    31,    32,    33,   287,    63,    61,    86,    27,
      93,     4,   294,    91,     4,   297,   524,   524,    62,   527,
     527,   250,     4,   524,   286,   524,   527,   309,   527,   291,
     800,   293,    93,   560,    27,     4,   563,   524,     4,    93,
     527,    85,     4,    61,   275,    27,    60,   862,   279,   280,
     281,   282,    81,    82,   524,   524,   524,   527,   527,   527,
     524,    27,     4,   527,   524,     4,     5,   527,    61,   884,
     524,    87,   354,   527,    38,    93,   846,   847,    94,    61,
       4,    97,    91,   438,    93,    27,    16,    17,    18,    19,
     321,   322,    86,    88,   449,    61,    91,    91,   868,    85,
      93,   332,    86,    27,   631,    86,    46,    91,    86,    86,
      91,    93,   341,    91,    91,   623,   623,   852,    42,   854,
      62,     4,   623,    87,   623,    46,   464,    93,    81,    82,
      94,    71,    86,    97,   396,    86,   623,    91,    62,    23,
      91,    71,    26,    85,   406,    90,     4,   409,    93,    83,
      84,    91,    92,   623,   623,   623,    65,   419,    93,   623,
      92,   392,    88,   623,   395,    91,   397,    20,    21,   623,
      91,    92,    90,    91,    93,   683,   683,   408,    88,    92,
      93,    91,   683,    85,   683,   414,   468,    92,    93,     4,
       5,   257,   258,    93,   456,    93,   683,    93,   427,   428,
      93,   432,    93,    93,   435,   436,   488,   271,   272,    61,
       4,   655,   494,   683,   683,   683,    93,    93,    93,   683,
      93,    93,    93,   683,   453,   454,    85,     4,    85,   683,
      85,    24,    34,     3,    64,    85,   465,   466,   469,   470,
     471,     4,    93,    85,    93,    85,   754,   754,   479,    85,
      97,    41,   534,   754,    93,   754,   793,    85,   540,   767,
     767,    93,   524,     4,     4,   527,   767,   754,   767,    86,
      91,     4,    86,   535,   505,    85,     4,    86,    86,   561,
     767,     4,   564,   565,   754,   754,   754,    23,   519,   571,
     754,     4,   574,     4,   754,   739,    41,   767,   767,   767,
     754,    85,     4,   767,   566,     5,     4,   767,   570,     5,
     541,   542,    41,   767,     6,    71,   598,    85,    85,    85,
     602,    85,    93,    42,    93,    86,    86,    89,    93,    86,
     612,   613,   614,    71,    72,    73,    74,    75,    76,    77,
      78,    79,    80,    91,    91,   607,    85,   609,   792,    92,
      85,    85,    46,    85,   616,    85,    93,     4,    14,    86,
       4,   623,    93,     4,    86,    91,   597,   629,    93,    86,
     601,   633,   634,    93,    86,    90,    13,   639,     8,   641,
     611,     4,    86,    93,   615,    93,    25,    93,     4,    46,
      20,    21,    93,    48,    93,    24,    93,    28,   680,   681,
       4,    59,   664,     3,     4,     5,   668,     7,     8,     9,
      93,   642,    86,   675,   676,   677,    56,    93,    67,   650,
     651,   683,     4,    93,    86,    86,    86,    27,     4,    27,
      30,    31,    32,    33,   716,    86,    36,    37,   720,    39,
      40,    71,    72,    73,    74,    75,    76,    77,    78,    79,
      80,   682,    86,    86,    22,    93,    46,    93,    86,    89,
      60,    86,    62,     4,    64,    24,    93,    93,     3,    93,
       3,    93,    71,    93,    93,    93,    85,   759,    92,     4,
       4,   763,    82,     4,     4,    85,    13,    87,    88,    89,
      93,    93,   754,    93,    41,    46,    93,    97,    93,    93,
      93,    93,   764,   765,    93,   767,    56,    65,     4,     4,
      93,     4,    93,    97,    93,   797,     4,    27,    93,    24,
     782,    24,    38,   785,    93,    56,    93,    93,    93,    93,
      93,    93,   814,    93,   816,    52,    93,    93,    93,    93,
      93,    93,   824,    71,    93,    93,    52,    93,    93,    93,
       3,     4,     5,    93,     7,     8,     9,    66,   820,   821,
       4,    65,    85,    85,    93,     4,     4,   849,    86,     4,
       4,    22,    22,     5,    27,   837,     4,    30,    31,    32,
      33,    93,    93,    36,    37,    93,    39,    40,     4,    42,
     852,    93,   854,    93,     3,     4,     5,   859,     7,     8,
       9,    93,    93,   885,    93,    52,    93,    60,   839,    62,
      93,    64,    93,   875,    93,    93,    92,    86,    27,    93,
      93,    30,    31,    32,    33,    93,    93,    36,    37,    82,
      39,    40,    85,    93,    87,   897,    89,    93,    93,    93,
      52,    93,     4,     4,    97,    93,     4,    93,    93,     4,
      93,    60,    93,    62,    61,    64,    93,    93,     4,     4,
      93,    61,     3,     4,     5,     4,     7,     8,     9,    93,
      93,    93,    93,    82,     4,    93,    85,     4,    87,    93,
      89,     4,    93,    93,    93,    93,    27,    93,    97,    30,
      31,    32,    33,   154,    93,    36,    37,   293,    39,    40,
       3,     4,     5,   166,     7,     8,     9,   105,   855,   423,
     459,   254,    89,   255,   145,   511,   649,    -1,    -1,    60,
      -1,    62,    -1,    64,    27,   340,    -1,    30,    31,    32,
      33,   269,    -1,    36,    37,    -1,    39,    40,    -1,    -1,
      -1,    82,    -1,    -1,    85,    -1,    87,    -1,    89,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    97,    60,    -1,    62,
      -1,    64,    -1,     3,     4,     5,    -1,     7,     8,     9,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    82,
      -1,    -1,    85,    -1,    87,    -1,    89,    27,    -1,    -1,
      30,    31,    32,    33,    97,    -1,    36,    -1,    -1,    39,
      40,     3,     4,     5,    -1,     7,     8,     9,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      60,    -1,    62,    -1,    64,    27,    -1,    -1,    30,    31,
      32,    33,    -1,    -1,    -1,    -1,    -1,    39,    40,    -1,
      -1,    -1,    82,    -1,    -1,    85,    -1,    87,    -1,    89,
      -1,     4,    -1,    -1,     7,    -1,    -1,    97,    60,    -1,
      62,    -1,    64,    -1,     3,     4,     5,    -1,     7,    22,
       9,    -1,    -1,    -1,    27,    -1,    29,    -1,    -1,    -1,
      82,    -1,    -1,    85,    37,    87,    -1,    89,    27,    -1,
      -1,    30,    31,    32,    33,    97,    -1,    50,    51,    -1,
      53,    54,    55,    -1,    -1,    -1,    -1,    -1,    61,    62,
      -1,    64,    -1,    -1,    -1,    68,    69,    -1,    -1,    -1,
      -1,    60,    -1,    62,    -1,    64,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,     4,     5,    -1,    -1,    -1,    -1,
      93,    -1,    -1,    -1,    14,    15,    85,    -1,    87,    -1,
      89,    -1,    22,    -1,    24,    25,    -1,    27,    97,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    -1,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      70,     4,     5,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    14,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,
      90,    24,    25,    93,    27,    -1,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    -1,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,     4,    -1,
      -1,     7,    -1,    -1,    10,    11,    12,    -1,    -1,    15,
      -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,
      93,    27,    -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    37,    38,    -1,    -1,    41,    -1,    -1,    44,    45,
      -1,    -1,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    -1,    61,    62,    -1,    64,    65,
      66,    67,    68,    69,    70,     4,    -1,    -1,     7,    -1,
      -1,    10,    11,    12,    -1,    -1,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    -1,    25,    93,    27,    -1,
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
      -1,    -1,     7,    -1,    -1,    10,    -1,    -1,    -1,    -1,
      15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,
      -1,    93,    27,    -1,    29,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    37,    38,    -1,    -1,    41,    -1,    -1,    44,
      -1,    -1,    -1,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    -1,    61,    62,    -1,    64,
      65,    66,    67,    68,    69,    70,     4,    -1,    -1,     7,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,    93,    27,
      -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,
      -1,    -1,     4,    -1,    -1,     7,    -1,    -1,    -1,    -1,
      -1,    -1,    50,    51,    -1,    53,    54,    55,    -1,    -1,
      -1,    -1,    -1,    61,    62,    27,    64,    29,    -1,    -1,
      68,    69,    -1,    -1,    -1,    37,    -1,    -1,     4,    -1,
      -1,     7,    -1,    -1,    -1,    -1,    -1,    -1,    50,    51,
      -1,    53,    54,    55,    -1,    93,    -1,    -1,    -1,    61,
      62,    27,    64,    29,    -1,    -1,    68,    69,    -1,    -1,
      -1,    37,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    50,    51,    -1,    53,    54,    55,
      -1,    93,    -1,    -1,    -1,    61,    62,    -1,    64,    -1,
      -1,    -1,    68,    69,    -1,     4,     5,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    14,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    24,    25,    93,    27,    -1,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    -1,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,     4,     5,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    14,    15,    -1,    -1,    -1,    86,    -1,    -1,
      22,    -1,    24,    25,    -1,    27,    -1,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    -1,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    70,     4,
       5,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    14,
      15,    -1,    -1,    -1,    86,    -1,    -1,    22,    -1,    24,
      25,    -1,    27,    -1,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    -1,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,     4,     5,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    14,    15,    -1,    -1,
      -1,    86,    -1,    -1,    22,    -1,    24,    25,    -1,    27,
      -1,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      -1,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,     4,     5,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    14,    15,    -1,    -1,    -1,    86,    -1,
      -1,    22,    -1,    24,    25,    -1,    27,    -1,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    -1,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
       4,     5,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      14,    15,    -1,    -1,    -1,    86,    -1,    -1,    22,    -1,
      24,    25,    -1,    27,    -1,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    -1,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,     4,    -1,    -1,
       7,    -1,    -1,    -1,    -1,    -1,    -1,    14,    15,    -1,
      -1,    -1,    86,    -1,    -1,    22,    -1,    24,    25,    -1,
      27,    -1,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    -1,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,    70,     4,    -1,    -1,     7,    -1,    -1,
      -1,    -1,    -1,    -1,    14,    15,    -1,    -1,    -1,    -1,
      -1,    -1,    22,    -1,    24,    25,    -1,    27,    -1,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    -1,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      70,     4,    -1,    -1,     7,    -1,    -1,    -1,    -1,    -1,
      -1,    14,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,
      -1,    24,    25,    -1,    27,    -1,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    -1,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,     4,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    14,    15,
      -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,    24,    25,
      -1,    27,    -1,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    -1,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69,    70,     4,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    14,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    24,    25,    -1,    27,    -1,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    -1,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70
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
      85,    46,    71,    91,    92,     4,   100,    93,    93,    85,
      85,    85,   133,    91,    93,    97,   158,    93,     4,    29,
      54,    63,    54,    63,     4,    85,   110,    41,    85,    93,
       4,    14,     4,   128,   158,   158,   108,     4,     7,   183,
     158,    71,   104,     4,   103,     4,     5,    86,   181,   183,
     184,    86,    91,    86,    86,   184,    86,   184,     4,   175,
      38,    85,    86,    86,    88,    93,   158,   177,    90,   184,
     175,     7,   183,     4,    93,   102,   106,   111,   112,   127,
     146,   147,   148,   150,   151,   152,   153,   157,   160,   161,
     162,   165,   165,   169,   164,   166,   166,   167,   167,   158,
     182,   183,     4,     7,   183,     4,   158,   158,   158,   158,
       4,   103,    46,    91,    92,    45,   100,    23,   119,     4,
     178,   180,   178,   133,    86,   144,   100,     4,    41,   100,
       4,    62,     4,    62,    85,   178,     4,   129,   178,   100,
       5,     4,     5,    41,    88,    91,     6,    85,    85,   158,
     158,    85,    71,    85,    93,    71,    92,    86,    91,   158,
      93,    86,    93,    86,    93,    97,    86,   171,   175,    91,
     186,   186,    86,    85,    85,   173,   100,    92,    93,    25,
     164,    88,    91,    90,    91,    85,    85,    86,    93,    42,
      46,    46,    91,   158,     4,   103,   158,    27,   158,    23,
      26,   120,    71,    92,    86,    91,    86,    86,    93,    27,
     158,    27,   175,    86,    93,    86,    27,    14,   129,   176,
      90,   175,   175,     4,   158,     4,   185,   136,   158,   158,
      93,   186,   139,    42,    93,    93,    13,     4,    86,   186,
      88,    90,   175,   175,    25,    27,   155,    25,   156,   159,
     176,   183,   175,   175,    93,   100,   158,   158,   158,     4,
      93,    46,    46,    91,    93,    48,    24,   158,    93,    28,
       3,     5,    30,    31,    32,    33,    81,    82,   179,   103,
      92,    93,     4,    92,    93,    93,   100,    56,    59,    86,
     100,    93,    67,     4,    93,    88,    86,    86,    86,   158,
      86,   141,   135,     4,   181,     4,    27,    61,    93,   140,
       4,   139,    27,    93,   158,    86,    86,    22,    93,   157,
      22,    22,    93,   157,    88,    86,    86,   100,    27,    62,
     114,    43,    93,    93,    93,    46,   100,   158,   158,     4,
      93,    93,    24,   121,    93,     3,     3,    71,   103,   100,
      71,    92,   103,   100,   100,    27,    93,    93,    27,   100,
      93,   100,    92,     4,    27,    61,    93,   142,   141,    85,
      71,    92,     4,     5,    42,    85,    93,     4,     4,     4,
      27,     4,    13,   158,   100,    93,    93,   158,   100,    93,
      27,    41,    93,   106,   158,   100,   100,   100,   158,   114,
      93,    93,    46,   121,    93,    15,    93,   102,   111,   112,
     113,   115,   116,   117,   118,   122,   123,   124,   125,   126,
     127,   130,   131,   132,   138,   145,   146,   147,   148,   149,
     150,   151,   152,   153,   154,   179,    93,    27,   179,   103,
      93,    27,    27,    56,    65,    27,    27,   158,     4,     5,
     143,     4,     4,    27,   185,   158,   158,     5,    42,    85,
      93,    93,     4,    86,   184,   139,    93,    97,    93,     4,
      93,    27,    24,    27,    24,    27,    38,    93,    93,    93,
     114,   114,   114,    93,   100,   100,   158,   121,    93,    93,
      93,    93,    93,    93,    93,    93,    93,    93,    93,    93,
      93,    93,   100,    52,    71,   100,    52,    56,    93,    93,
      66,    65,    85,    85,    91,    93,    93,    93,     4,    86,
      93,     4,    86,   184,   139,   139,     4,    93,    86,    27,
       4,   139,    93,     4,    93,    22,    93,   157,    22,    93,
     100,   100,   114,   114,    93,    27,    93,   179,    27,    93,
      93,    93,    93,    86,   184,   178,     5,   100,    93,    92,
     139,     4,    93,    86,    27,    27,    93,    93,     4,    93,
      27,    93,   100,    93,   100,    93,    93,   114,   114,   100,
      52,    52,    93,    86,    86,    27,   158,    27,    93,    93,
       4,     4,   139,   139,    93,   100,     4,   155,   155,   156,
     114,    93,    93,   141,    93,    93,    61,     4,   139,    93,
      93,    27,    27,    27,    93,    27,   141,   100,    93,    93,
      27,     4,     4,    61,     4,    27,    27,     4,    93,    93,
      93,    93,     4,     4,    93,    93,    93
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
     115,   115,   115,   115,   115,   116,   117,   118,   119,   119,
     120,   120,   121,   121,   121,   122,   122,   122,   122,   122,
     122,   122,   122,   122,   122,   122,   122,   122,   122,   122,
     122,   122,   122,   122,   122,   122,   122,   122,   122,   122,
     122,   122,   122,   123,   123,   123,   123,   124,   124,   125,
     126,   127,   127,   127,   127,   127,   127,   127,   127,   128,
     128,   129,   130,   130,   130,   131,   132,   132,   133,   133,
     135,   134,   136,   134,   137,   137,   137,   137,   138,   138,
     138,   139,   139,   139,   140,   140,   140,   140,   140,   140,
     140,   140,   140,   140,   140,   141,   141,   141,   142,   142,
     142,   142,   142,   143,   143,   144,   144,   145,   146,   146,
     146,   146,   146,   146,   146,   146,   147,   148,   148,   149,
     150,   151,   152,   152,   153,   153,   154,   154,   155,   155,
     155,   155,   156,   156,   156,   156,   156,   157,   157,   157,
     157,   157,   157,   157,   157,   157,   157,   157,   158,   158,
     159,   159,   160,   160,   161,   161,   162,   162,   163,   163,
     163,   164,   164,   164,   165,   165,   165,   166,   166,   166,
     167,   167,   167,   167,   167,   167,   168,   168,   168,   168,
     168,   168,   168,   169,   169,   169,   169,   169,   169,   169,
     169,   169,   169,   170,   170,   170,   170,   170,   170,   170,
     170,   170,   170,   170,   170,   170,   170,   170,   170,   170,
     171,   171,   172,   172,   172,   172,   173,   173,   174,   174,
     175,   175,   176,   176,   177,   177,   178,   178,   179,   179,
     179,   179,   179,   179,   179,   179,   180,   180,   180,   180,
     180,   180,   180,   180,   181,   181,   182,   182,   183,   183,
     183,   183,   183,   183,   183,   183,   183,   183,   183,   183,
     183,   183,   183,   183,   183,   183,   183,   183,   183,   183,
     183,   183,   183,   183,   183,   183,   183,   183,   183,   183,
     183,   183,   183,   183,   183,   183,   183,   183,   183,   183,
     183,   183,   183,   183,   183,   183,   184,   184,   184,   184,
     184,   184,   185,   185,   186,   186
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
      10,    11,    12,     9,    11,     6,     7,     7,     5,     6,
       0,     3,     0,     2,     2,     2,     2,     2,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     2,     1,
       2,     2,     1,     2,     2,     2,     2,     2,     2,     2,
       2,     1,     1,    10,    10,    12,    12,     9,    10,    10,
       7,     2,     2,     2,     2,     4,     4,     4,     6,     1,
       4,     1,     9,     7,    10,     2,     3,     5,     1,     3,
       0,    11,     0,    10,     6,     5,     9,    11,    10,     9,
      13,     0,     2,     2,     4,     6,     5,     6,     9,     7,
       8,     7,     9,    10,     9,     0,     2,     2,     3,    10,
      10,     9,     7,     1,     3,     1,     3,     7,     4,     4,
       3,     4,     4,     3,     3,     3,     2,     1,     2,     2,
       2,     2,     1,     2,     1,     2,     6,     6,     3,     3,
       6,     7,     0,     3,     6,     7,     7,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     2,
       1,     5,     1,     3,     1,     3,     1,     2,     1,     3,
       4,     1,     3,     3,     1,     3,     3,     1,     3,     3,
       1,     2,     2,     2,     4,     5,     1,     4,     6,     3,
       4,     6,     6,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     2,     1,     3,     1,     1,     2,
       4,     1,     1,     1,     1,     1,     3,     3,     5,     1,
       3,     5,     0,     3,     3,     5,     0,     3,     2,     3,
       0,     1,     1,     3,     1,     4,     0,     1,     1,     2,
       2,     1,     1,     1,     1,     1,     1,     3,     3,     3,
       5,     5,     7,     5,     1,     1,     1,     3,     1,     1,
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
#line 959 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3360 "src/parser.tab.c"
        break;

    case YYSYMBOL_STRING: /* STRING  */
#line 959 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3366 "src/parser.tab.c"
        break;

    case YYSYMBOL_LENS_CONTENT: /* LENS_CONTENT  */
#line 959 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3372 "src/parser.tab.c"
        break;

    case YYSYMBOL_QUALIFIED_IDENT: /* QUALIFIED_IDENT  */
#line 959 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3378 "src/parser.tab.c"
        break;

    case YYSYMBOL_MODIFIER_PREFIX: /* MODIFIER_PREFIX  */
#line 959 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3384 "src/parser.tab.c"
        break;

    case YYSYMBOL_MODIFIER_VALUE: /* MODIFIER_VALUE  */
#line 959 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3390 "src/parser.tab.c"
        break;

    case YYSYMBOL_SQL_TEXT: /* SQL_TEXT  */
#line 959 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3396 "src/parser.tab.c"
        break;

    case YYSYMBOL_program: /* program  */
#line 982 "src/parser.y"
            { (void) ((*yyvaluep).stmt_list); }
#line 3402 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement_list: /* statement_list  */
#line 919 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3408 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement: /* statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3414 "src/parser.tab.c"
        break;

    case YYSYMBOL_assignment: /* assignment  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3420 "src/parser.tab.c"
        break;

    case YYSYMBOL_type_lock: /* type_lock  */
#line 972 "src/parser.y"
            { ast_type_lock_free(((*yyvaluep).type_lock)); }
#line 3426 "src/parser.tab.c"
        break;

    case YYSYMBOL_lvalue: /* lvalue  */
#line 960 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3432 "src/parser.tab.c"
        break;

    case YYSYMBOL_variable_name: /* variable_name  */
#line 959 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3438 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_lens: /* comparison_lens  */
#line 966 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 3444 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_name: /* modifier_name  */
#line 959 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3450 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_word: /* modifier_word  */
#line 959 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3456 "src/parser.tab.c"
        break;

    case YYSYMBOL_print_statement: /* print_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3462 "src/parser.tab.c"
        break;

    case YYSYMBOL_call_statement: /* call_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3468 "src/parser.tab.c"
        break;

    case YYSYMBOL_with_lock_statement: /* with_lock_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3474 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_end: /* for_end  */
#line 959 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3480 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_each_statement: /* for_each_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3486 "src/parser.tab.c"
        break;

    case YYSYMBOL_do_loop_statement: /* do_loop_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3492 "src/parser.tab.c"
        break;

    case YYSYMBOL_while_statement: /* while_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3498 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement: /* consider_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3504 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_branch_list: /* consider_branch_list  */
#line 964 "src/parser.y"
            { ast_free_consider_branch_list(((*yyvaluep).consider_branch_list)); }
#line 3510 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_else_opt: /* consider_else_opt  */
#line 919 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3516 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement_list: /* consider_statement_list  */
#line 919 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3522 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_body_statement: /* consider_body_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3528 "src/parser.tab.c"
        break;

    case YYSYMBOL_function_statement: /* function_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3534 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_statement: /* modifier_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3540 "src/parser.tab.c"
        break;

    case YYSYMBOL_program_statement: /* program_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3546 "src/parser.tab.c"
        break;

    case YYSYMBOL_library_statement: /* library_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3552 "src/parser.tab.c"
        break;

    case YYSYMBOL_use_statement: /* use_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3558 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_signature: /* modifier_signature  */
#line 967 "src/parser.y"
            { ast_free_modifier_signature(((*yyvaluep).modifier_signature)); }
#line 3564 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_context: /* modifier_context  */
#line 959 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3570 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_statement: /* watch_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3576 "src/parser.tab.c"
        break;

    case YYSYMBOL_unwatch_statement: /* unwatch_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3582 "src/parser.tab.c"
        break;

    case YYSYMBOL_wait_statement: /* wait_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3588 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_list: /* watch_target_list  */
#line 965 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3594 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_statement: /* server_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3600 "src/parser.tab.c"
        break;

    case YYSYMBOL_gsql_block: /* gsql_block  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3606 "src/parser.tab.c"
        break;

    case YYSYMBOL_window_block: /* window_block  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3612 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item_list: /* server_item_list  */
#line 971 "src/parser.y"
            { ast_free_server_item_list(((*yyvaluep).server_item_list)); }
#line 3618 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item: /* server_item  */
#line 970 "src/parser.y"
            { AstServerItemList one = ast_server_item_list_append(ast_server_item_list_empty(), ((*yyvaluep).server_item)); ast_free_server_item_list(one); }
#line 3624 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_string_list: /* server_string_list  */
#line 965 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3630 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_path: /* watch_target_path  */
#line 959 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3636 "src/parser.tab.c"
        break;

    case YYSYMBOL_without_watchers_statement: /* without_watchers_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3642 "src/parser.tab.c"
        break;

    case YYSYMBOL_on_error_statement: /* on_error_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3648 "src/parser.tab.c"
        break;

    case YYSYMBOL_error_statement: /* error_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3654 "src/parser.tab.c"
        break;

    case YYSYMBOL_return_statement: /* return_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3660 "src/parser.tab.c"
        break;

    case YYSYMBOL_label_statement: /* label_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3666 "src/parser.tab.c"
        break;

    case YYSYMBOL_goto_statement: /* goto_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3672 "src/parser.tab.c"
        break;

    case YYSYMBOL_gosub_statement: /* gosub_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3678 "src/parser.tab.c"
        break;

    case YYSYMBOL_break_statement: /* break_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3684 "src/parser.tab.c"
        break;

    case YYSYMBOL_continue_statement: /* continue_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3690 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_statement: /* if_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3696 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_block_tail: /* if_block_tail  */
#line 919 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3702 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_inline_tail: /* if_inline_tail  */
#line 919 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3708 "src/parser.tab.c"
        break;

    case YYSYMBOL_inline_statement: /* inline_statement  */
#line 961 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3714 "src/parser.tab.c"
        break;

    case YYSYMBOL_expression: /* expression  */
#line 960 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3720 "src/parser.tab.c"
        break;

    case YYSYMBOL_conditional_expression: /* conditional_expression  */
#line 960 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3726 "src/parser.tab.c"
        break;

    case YYSYMBOL_or_expression: /* or_expression  */
#line 960 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3732 "src/parser.tab.c"
        break;

    case YYSYMBOL_and_expression: /* and_expression  */
#line 960 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3738 "src/parser.tab.c"
        break;

    case YYSYMBOL_not_expression: /* not_expression  */
#line 960 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3744 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_expression: /* comparison_expression  */
#line 960 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3750 "src/parser.tab.c"
        break;

    case YYSYMBOL_set_expression: /* set_expression  */
#line 960 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3756 "src/parser.tab.c"
        break;

    case YYSYMBOL_additive_expression: /* additive_expression  */
#line 960 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3762 "src/parser.tab.c"
        break;

    case YYSYMBOL_multiplicative_expression: /* multiplicative_expression  */
#line 960 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3768 "src/parser.tab.c"
        break;

    case YYSYMBOL_unary_expression: /* unary_expression  */
#line 960 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3774 "src/parser.tab.c"
        break;

    case YYSYMBOL_postfix_expression: /* postfix_expression  */
#line 960 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3780 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_operator: /* comparison_operator  */
#line 959 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3786 "src/parser.tab.c"
        break;

    case YYSYMBOL_primary: /* primary  */
#line 960 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3792 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_literal: /* record_literal  */
#line 960 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3798 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_suffix: /* ident_suffix  */
#line 968 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3804 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_dot_suffix: /* ident_dot_suffix  */
#line 968 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3810 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list_opt: /* argument_list_opt  */
#line 962 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3816 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list: /* argument_list  */
#line 962 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3822 "src/parser.tab.c"
        break;

    case YYSYMBOL_array_argument_list: /* array_argument_list  */
#line 962 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3828 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list_opt: /* parameter_list_opt  */
#line 965 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3834 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_default: /* parameter_default  */
#line 960 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3840 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list: /* parameter_list  */
#line 965 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3846 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_name: /* field_name  */
#line 959 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3852 "src/parser.tab.c"
        break;

    case YYSYMBOL_projection_names: /* projection_names  */
#line 965 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3858 "src/parser.tab.c"
        break;

    case YYSYMBOL_dot_field_name: /* dot_field_name  */
#line 959 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3864 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_field_list: /* record_field_list  */
#line 963 "src/parser.y"
            { ast_free_record_field_list(((*yyvaluep).record_field_list)); }
#line 3870 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_policy: /* field_policy  */
#line 969 "src/parser.y"
            { ast_free_expr(((*yyvaluep).field_policy).reset_expr); }
#line 3876 "src/parser.tab.c"
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
#line 743 "src/parser.y"
{ (void)yynerrs; }

#line 3976 "src/parser.tab.c"

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
#line 987 "src/parser.y"
                     { ctx->parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 4189 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 991 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4195 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 992 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 4201 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 993 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 4207 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 997 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4213 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 998 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4219 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 999 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4225 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 1000 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4231 "src/parser.tab.c"
    break;

  case 10: /* statement: window_block  */
#line 1001 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4237 "src/parser.tab.c"
    break;

  case 11: /* statement: gsql_block  */
#line 1002 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4243 "src/parser.tab.c"
    break;

  case 12: /* statement: for_each_statement  */
#line 1003 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4249 "src/parser.tab.c"
    break;

  case 13: /* statement: while_statement  */
#line 1004 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4255 "src/parser.tab.c"
    break;

  case 14: /* statement: do_loop_statement  */
#line 1005 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4261 "src/parser.tab.c"
    break;

  case 15: /* statement: consider_statement  */
#line 1006 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4267 "src/parser.tab.c"
    break;

  case 16: /* statement: function_statement  */
#line 1007 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4273 "src/parser.tab.c"
    break;

  case 17: /* statement: modifier_statement  */
#line 1008 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4279 "src/parser.tab.c"
    break;

  case 18: /* statement: program_statement  */
#line 1009 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4285 "src/parser.tab.c"
    break;

  case 19: /* statement: library_statement  */
#line 1010 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4291 "src/parser.tab.c"
    break;

  case 20: /* statement: use_statement NEWLINE  */
#line 1011 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4297 "src/parser.tab.c"
    break;

  case 21: /* statement: watch_statement  */
#line 1012 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4303 "src/parser.tab.c"
    break;

  case 22: /* statement: server_statement  */
#line 1013 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4309 "src/parser.tab.c"
    break;

  case 23: /* statement: unwatch_statement NEWLINE  */
#line 1014 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4315 "src/parser.tab.c"
    break;

  case 24: /* statement: wait_statement NEWLINE  */
#line 1015 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4321 "src/parser.tab.c"
    break;

  case 25: /* statement: without_watchers_statement  */
#line 1016 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4327 "src/parser.tab.c"
    break;

  case 26: /* statement: on_error_statement NEWLINE  */
#line 1017 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4333 "src/parser.tab.c"
    break;

  case 27: /* statement: error_statement NEWLINE  */
#line 1018 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4339 "src/parser.tab.c"
    break;

  case 28: /* statement: return_statement NEWLINE  */
#line 1019 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4345 "src/parser.tab.c"
    break;

  case 29: /* statement: label_statement NEWLINE  */
#line 1020 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4351 "src/parser.tab.c"
    break;

  case 30: /* statement: goto_statement NEWLINE  */
#line 1021 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4357 "src/parser.tab.c"
    break;

  case 31: /* statement: gosub_statement NEWLINE  */
#line 1022 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4363 "src/parser.tab.c"
    break;

  case 32: /* statement: break_statement NEWLINE  */
#line 1023 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4369 "src/parser.tab.c"
    break;

  case 33: /* statement: continue_statement NEWLINE  */
#line 1024 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4375 "src/parser.tab.c"
    break;

  case 34: /* statement: if_statement  */
#line 1025 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4381 "src/parser.tab.c"
    break;

  case 35: /* statement: DIM  */
#line 1031 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        GB_REFUSE;
      }
#line 4394 "src/parser.tab.c"
    break;

  case 36: /* assignment: lvalue OP_EQ expression  */
#line 1042 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 4400 "src/parser.tab.c"
    break;

  case 37: /* assignment: lvalue compound_op expression  */
#line 1047 "src/parser.y"
                                    { (yyval.stmt) = ast_assign_op((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr), (yyvsp[-1].op_char)); }
#line 4406 "src/parser.tab.c"
    break;

  case 38: /* assignment: lvalue comparison_lens compound_op expression  */
#line 1048 "src/parser.y"
                                                    {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            GB_REFUSE;
        }
        (yyval.stmt) = ast_assign_op((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr), (yyvsp[-1].op_char));
      }
#line 4420 "src/parser.tab.c"
    break;

  case 39: /* assignment: variable_name COLON type_lock OP_EQ expression  */
#line 1064 "src/parser.y"
                                                     {
        if (!type_lock_check(ctx, (yyvsp[-4].text), (yyvsp[-2].type_lock).name, (yyvsp[-2].type_lock).arg, (yylsp[-2]).first_line, (yylsp[-2]).first_column,
                             (yylsp[-2]).last_line, (yylsp[-2]).last_column)) {
            GB_REFUSE;
        }
        (yyval.stmt) = ast_assign_locked(expr_at(ast_ident((yyvsp[-4].text)), (yylsp[-4]).first_line, (yylsp[-4]).first_column), (yyvsp[-2].type_lock), (yyvsp[0].expr));
      }
#line 4432 "src/parser.tab.c"
    break;

  case 40: /* assignment: lvalue comparison_lens OP_EQ expression  */
#line 1071 "src/parser.y"
                                              {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            GB_REFUSE;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 4446 "src/parser.tab.c"
    break;

  case 41: /* type_lock: IDENT  */
#line 1085 "src/parser.y"
            { (yyval.type_lock) = ast_type_lock((yyvsp[0].text), NULL, (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4452 "src/parser.tab.c"
    break;

  case 42: /* type_lock: IDENT LPAREN IDENT RPAREN  */
#line 1086 "src/parser.y"
                                { (yyval.type_lock) = ast_type_lock((yyvsp[-3].text), (yyvsp[-1].text), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 4458 "src/parser.tab.c"
    break;

  case 43: /* compound_op: PLUS_EQ  */
#line 1090 "src/parser.y"
               { (yyval.op_char) = '+'; }
#line 4464 "src/parser.tab.c"
    break;

  case 44: /* compound_op: MINUS_EQ  */
#line 1091 "src/parser.y"
               { (yyval.op_char) = '-'; }
#line 4470 "src/parser.tab.c"
    break;

  case 45: /* compound_op: STAR_EQ  */
#line 1092 "src/parser.y"
               { (yyval.op_char) = '*'; }
#line 4476 "src/parser.tab.c"
    break;

  case 46: /* compound_op: SLASH_EQ  */
#line 1093 "src/parser.y"
               { (yyval.op_char) = '/'; }
#line 4482 "src/parser.tab.c"
    break;

  case 47: /* lvalue: variable_name  */
#line 1097 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4488 "src/parser.tab.c"
    break;

  case 48: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 1098 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4494 "src/parser.tab.c"
    break;

  case 49: /* lvalue: lvalue LBRACKET expression COMMA argument_list RBRACKET  */
#line 1102 "src/parser.y"
                                                                           {
        AstExprList items = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[-3].expr));
        for (size_t i = 0; i < (yyvsp[-1].expr_list).count; i++) {
            items = ast_expr_list_append(items, (yyvsp[-1].expr_list).items[i]);
        }
        free((yyvsp[-1].expr_list).items);
        (yyval.expr) = expr_at(ast_index_list((yyvsp[-5].expr), items), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 4507 "src/parser.tab.c"
    break;

  case 50: /* lvalue: lvalue DOT dot_field_name  */
#line 1110 "src/parser.y"
                                             { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4513 "src/parser.tab.c"
    break;

  case 51: /* variable_name: IDENT  */
#line 1114 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 4519 "src/parser.tab.c"
    break;

  case 52: /* variable_name: END  */
#line 1115 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 4525 "src/parser.tab.c"
    break;

  case 53: /* variable_name: NEXT  */
#line 1116 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 4531 "src/parser.tab.c"
    break;

  case 54: /* $@1: %empty  */
#line 1127 "src/parser.y"
             { lexer_begin_lens_content(ctx->active_lexer); }
#line 4537 "src/parser.tab.c"
    break;

  case 55: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 1127 "src/parser.y"
                                                                                  {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 4545 "src/parser.tab.c"
    break;

  case 56: /* comparison_lens: MODIFIER_PREFIX  */
#line 1135 "src/parser.y"
                      { (yyval.modifier) = parse_modifier_use((yyvsp[0].text)); }
#line 4551 "src/parser.tab.c"
    break;

  case 57: /* modifier_name: modifier_word  */
#line 1139 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 4557 "src/parser.tab.c"
    break;

  case 58: /* modifier_name: modifier_name modifier_word  */
#line 1140 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 4563 "src/parser.tab.c"
    break;

  case 59: /* modifier_word: IDENT  */
#line 1144 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4569 "src/parser.tab.c"
    break;

  case 60: /* modifier_word: TO  */
#line 1145 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 4575 "src/parser.tab.c"
    break;

  case 61: /* modifier_word: END  */
#line 1146 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 4581 "src/parser.tab.c"
    break;

  case 62: /* modifier_word: NEXT  */
#line 1147 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 4587 "src/parser.tab.c"
    break;

  case 63: /* print_statement: PRINT expression  */
#line 1151 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 4593 "src/parser.tab.c"
    break;

  case 64: /* print_statement: PRINT TO ERROR_VALUE expression  */
#line 1157 "src/parser.y"
                                      { (yyval.stmt) = ast_print_error((yyvsp[0].expr)); }
#line 4599 "src/parser.tab.c"
    break;

  case 65: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 1161 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 4605 "src/parser.tab.c"
    break;

  case 66: /* call_statement: TRY expression  */
#line 1164 "src/parser.y"
                     { (yyval.stmt) = ast_expr_stmt(expr_at(ast_unary(copy_const("try"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column)); }
#line 4611 "src/parser.tab.c"
    break;

  case 67: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1165 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 4622 "src/parser.tab.c"
    break;

  case 68: /* call_statement: lvalue DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1171 "src/parser.y"
                                                       {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 4632 "src/parser.tab.c"
    break;

  case 69: /* call_statement: lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1176 "src/parser.y"
                                                                 {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 4646 "src/parser.tab.c"
    break;

  case 70: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1185 "src/parser.y"
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
#line 4661 "src/parser.tab.c"
    break;

  case 71: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 1198 "src/parser.y"
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
#line 4684 "src/parser.tab.c"
    break;

  case 72: /* for_end: END FOR NEWLINE  */
#line 1227 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 4690 "src/parser.tab.c"
    break;

  case 73: /* for_end: NEXT NEWLINE  */
#line 1228 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 4696 "src/parser.tab.c"
    break;

  case 74: /* for_end: NEXT variable_name NEWLINE  */
#line 1229 "src/parser.y"
                                 { (yyval.text) = (yyvsp[-1].text); }
#line 4702 "src/parser.tab.c"
    break;

  case 75: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list for_end  */
#line 1233 "src/parser.y"
                                                             {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { GB_REFUSE; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4711 "src/parser.tab.c"
    break;

  case 76: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list for_end  */
#line 1237 "src/parser.y"
                                                                  {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { GB_REFUSE; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4720 "src/parser.tab.c"
    break;

  case 77: /* for_each_statement: FOR IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1253 "src/parser.y"
                                                                         {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { GB_REFUSE; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { GB_REFUSE; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4730 "src/parser.tab.c"
    break;

  case 78: /* for_each_statement: FOR EACH IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1258 "src/parser.y"
                                                                              {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { GB_REFUSE; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { GB_REFUSE; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4740 "src/parser.tab.c"
    break;

  case 79: /* for_each_statement: FOR IDENT COLON type_lock IN expression NEWLINE statement_list for_end  */
#line 1267 "src/parser.y"
                                                                             {
        if (!type_lock_check(ctx, (yyvsp[-7].text), (yyvsp[-5].type_lock).name, (yyvsp[-5].type_lock).arg, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                             (yylsp[-5]).last_line, (yylsp[-5]).last_column)) { GB_REFUSE; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { GB_REFUSE; }
        (yyval.stmt) = ast_for_each_locked((yyvsp[-7].text), (yyvsp[-5].type_lock), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4751 "src/parser.tab.c"
    break;

  case 80: /* for_each_statement: FOR EACH IDENT COLON type_lock IN expression NEWLINE statement_list for_end  */
#line 1273 "src/parser.y"
                                                                                  {
        if (!type_lock_check(ctx, (yyvsp[-7].text), (yyvsp[-5].type_lock).name, (yyvsp[-5].type_lock).arg, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                             (yylsp[-5]).last_line, (yylsp[-5]).last_column)) { GB_REFUSE; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { GB_REFUSE; }
        (yyval.stmt) = ast_for_each_locked((yyvsp[-7].text), (yyvsp[-5].type_lock), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4762 "src/parser.tab.c"
    break;

  case 81: /* for_each_statement: FOR IDENT COLON type_lock COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1279 "src/parser.y"
                                                                                         {
        if (!type_lock_check(ctx, (yyvsp[-9].text), (yyvsp[-7].type_lock).name, (yyvsp[-7].type_lock).arg, (yylsp[-7]).first_line, (yylsp[-7]).first_column,
                             (yylsp[-7]).last_line, (yylsp[-7]).last_column)) { GB_REFUSE; }
        if (!for_each_index_distinct(ctx, (yyvsp[-9].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { GB_REFUSE; }
        if (!for_end_matches(ctx, (yyvsp[-9].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { GB_REFUSE; }
        (yyval.stmt) = ast_for_each_locked((yyvsp[-9].text), (yyvsp[-7].type_lock), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4774 "src/parser.tab.c"
    break;

  case 82: /* for_each_statement: FOR EACH IDENT COLON type_lock COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1286 "src/parser.y"
                                                                                              {
        if (!type_lock_check(ctx, (yyvsp[-9].text), (yyvsp[-7].type_lock).name, (yyvsp[-7].type_lock).arg, (yylsp[-7]).first_line, (yylsp[-7]).first_column,
                             (yylsp[-7]).last_line, (yylsp[-7]).last_column)) { GB_REFUSE; }
        if (!for_each_index_distinct(ctx, (yyvsp[-9].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { GB_REFUSE; }
        if (!for_end_matches(ctx, (yyvsp[-9].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { GB_REFUSE; }
        (yyval.stmt) = ast_for_each_locked((yyvsp[-9].text), (yyvsp[-7].type_lock), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4786 "src/parser.tab.c"
    break;

  case 83: /* for_each_statement: FOR IDENT OP_EQ expression TO expression NEWLINE statement_list for_end  */
#line 1296 "src/parser.y"
                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { GB_REFUSE; }
        (yyval.stmt) = ast_for_range((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].expr), NULL, (yyvsp[-1].stmt_list));
      }
#line 4795 "src/parser.tab.c"
    break;

  case 84: /* for_each_statement: FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list for_end  */
#line 1300 "src/parser.y"
                                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-9].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { GB_REFUSE; }
        (yyval.stmt) = ast_for_range((yyvsp[-9].text), (yyvsp[-7].expr), (yyvsp[-5].expr), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4804 "src/parser.tab.c"
    break;

  case 85: /* do_loop_statement: DO NEWLINE statement_list UNTIL expression NEWLINE  */
#line 1321 "src/parser.y"
                                                         {
        (yyval.stmt) = ast_do_loop((yyvsp[-3].stmt_list), (yyvsp[-1].expr));
      }
#line 4812 "src/parser.tab.c"
    break;

  case 86: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 1327 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 4820 "src/parser.tab.c"
    break;

  case 87: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 1333 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 4828 "src/parser.tab.c"
    break;

  case 88: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 1339 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 4836 "src/parser.tab.c"
    break;

  case 89: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 1342 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 4844 "src/parser.tab.c"
    break;

  case 90: /* consider_else_opt: %empty  */
#line 1348 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4850 "src/parser.tab.c"
    break;

  case 91: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 1349 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 4856 "src/parser.tab.c"
    break;

  case 92: /* consider_statement_list: %empty  */
#line 1353 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4862 "src/parser.tab.c"
    break;

  case 93: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 1354 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 4868 "src/parser.tab.c"
    break;

  case 94: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 1355 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 4874 "src/parser.tab.c"
    break;

  case 95: /* consider_body_statement: assignment NEWLINE  */
#line 1359 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4880 "src/parser.tab.c"
    break;

  case 96: /* consider_body_statement: print_statement NEWLINE  */
#line 1360 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4886 "src/parser.tab.c"
    break;

  case 97: /* consider_body_statement: call_statement NEWLINE  */
#line 1361 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4892 "src/parser.tab.c"
    break;

  case 98: /* consider_body_statement: with_lock_statement  */
#line 1362 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4898 "src/parser.tab.c"
    break;

  case 99: /* consider_body_statement: window_block  */
#line 1363 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4904 "src/parser.tab.c"
    break;

  case 100: /* consider_body_statement: for_each_statement  */
#line 1364 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4910 "src/parser.tab.c"
    break;

  case 101: /* consider_body_statement: while_statement  */
#line 1365 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4916 "src/parser.tab.c"
    break;

  case 102: /* consider_body_statement: do_loop_statement  */
#line 1366 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4922 "src/parser.tab.c"
    break;

  case 103: /* consider_body_statement: consider_statement  */
#line 1367 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4928 "src/parser.tab.c"
    break;

  case 104: /* consider_body_statement: function_statement  */
#line 1368 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4934 "src/parser.tab.c"
    break;

  case 105: /* consider_body_statement: modifier_statement  */
#line 1369 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4940 "src/parser.tab.c"
    break;

  case 106: /* consider_body_statement: program_statement  */
#line 1370 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4946 "src/parser.tab.c"
    break;

  case 107: /* consider_body_statement: library_statement  */
#line 1371 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4952 "src/parser.tab.c"
    break;

  case 108: /* consider_body_statement: use_statement NEWLINE  */
#line 1372 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4958 "src/parser.tab.c"
    break;

  case 109: /* consider_body_statement: watch_statement  */
#line 1373 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4964 "src/parser.tab.c"
    break;

  case 110: /* consider_body_statement: unwatch_statement NEWLINE  */
#line 1374 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4970 "src/parser.tab.c"
    break;

  case 111: /* consider_body_statement: wait_statement NEWLINE  */
#line 1375 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4976 "src/parser.tab.c"
    break;

  case 112: /* consider_body_statement: without_watchers_statement  */
#line 1376 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4982 "src/parser.tab.c"
    break;

  case 113: /* consider_body_statement: on_error_statement NEWLINE  */
#line 1377 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4988 "src/parser.tab.c"
    break;

  case 114: /* consider_body_statement: error_statement NEWLINE  */
#line 1378 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4994 "src/parser.tab.c"
    break;

  case 115: /* consider_body_statement: return_statement NEWLINE  */
#line 1379 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 5000 "src/parser.tab.c"
    break;

  case 116: /* consider_body_statement: label_statement NEWLINE  */
#line 1380 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 5006 "src/parser.tab.c"
    break;

  case 117: /* consider_body_statement: goto_statement NEWLINE  */
#line 1381 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 5012 "src/parser.tab.c"
    break;

  case 118: /* consider_body_statement: gosub_statement NEWLINE  */
#line 1382 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 5018 "src/parser.tab.c"
    break;

  case 119: /* consider_body_statement: break_statement NEWLINE  */
#line 1383 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 5024 "src/parser.tab.c"
    break;

  case 120: /* consider_body_statement: continue_statement NEWLINE  */
#line 1384 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 5030 "src/parser.tab.c"
    break;

  case 121: /* consider_body_statement: if_statement  */
#line 1385 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5036 "src/parser.tab.c"
    break;

  case 122: /* consider_body_statement: DIM  */
#line 1391 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        GB_REFUSE;
      }
#line 5049 "src/parser.tab.c"
    break;

  case 123: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1402 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 5057 "src/parser.tab.c"
    break;

  case 124: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1405 "src/parser.y"
                                                                                                            {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 5066 "src/parser.tab.c"
    break;

  case 125: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN COLON type_lock NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1412 "src/parser.y"
                                                                                                                  {
        char who[160];
        snprintf(who, sizeof who, "%s(...)", (yyvsp[-10].text));
        if (!type_lock_check(ctx, who, (yyvsp[-5].type_lock).name, (yyvsp[-5].type_lock).arg, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                             (yylsp[-5]).last_line, (yylsp[-5]).last_column)) {
            GB_REFUSE;
        }
        (yyval.stmt) = ast_function_returning((yyvsp[-10].text), (yyvsp[-8].name_list), (yyvsp[-5].type_lock), (yyvsp[-3].stmt_list));
      }
#line 5080 "src/parser.tab.c"
    break;

  case 126: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN COLON type_lock NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1421 "src/parser.y"
                                                                                                                            {
        char who[160];
        snprintf(who, sizeof who, "%s(...)", (yyvsp[-10].text));
        if (!type_lock_check(ctx, who, (yyvsp[-5].type_lock).name, (yyvsp[-5].type_lock).arg, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                             (yylsp[-5]).last_line, (yylsp[-5]).last_column)) {
            GB_REFUSE;
        }
        (yyval.stmt) = ast_function_returning((yyvsp[-10].text), (yyvsp[-8].name_list), (yyvsp[-5].type_lock), (yyvsp[-3].stmt_list));
      }
#line 5094 "src/parser.tab.c"
    break;

  case 127: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 1433 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 5102 "src/parser.tab.c"
    break;

  case 128: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 1436 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 5110 "src/parser.tab.c"
    break;

  case 129: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 1442 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 5118 "src/parser.tab.c"
    break;

  case 130: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 1448 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 5126 "src/parser.tab.c"
    break;

  case 131: /* use_statement: USE IDENT  */
#line 1454 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 5132 "src/parser.tab.c"
    break;

  case 132: /* use_statement: LOAD IDENT  */
#line 1455 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 5138 "src/parser.tab.c"
    break;

  case 133: /* use_statement: USE STRING  */
#line 1456 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 5144 "src/parser.tab.c"
    break;

  case 134: /* use_statement: LOAD STRING  */
#line 1457 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 5150 "src/parser.tab.c"
    break;

  case 135: /* use_statement: LOAD IDENT AS IDENT  */
#line 1458 "src/parser.y"
                          { (yyval.stmt) = ast_use((yyvsp[-2].text), NULL, (yyvsp[0].text)); }
#line 5156 "src/parser.tab.c"
    break;

  case 136: /* use_statement: USE IDENT IDENT STRING  */
#line 1459 "src/parser.y"
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
#line 5177 "src/parser.tab.c"
    break;

  case 137: /* use_statement: LOAD IDENT IDENT STRING  */
#line 1475 "src/parser.y"
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
#line 5198 "src/parser.tab.c"
    break;

  case 138: /* use_statement: LOAD IDENT IDENT STRING AS IDENT  */
#line 1491 "src/parser.y"
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
#line 5221 "src/parser.tab.c"
    break;

  case 139: /* modifier_signature: modifier_name  */
#line 1512 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 5227 "src/parser.tab.c"
    break;

  case 140: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 1513 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 5233 "src/parser.tab.c"
    break;

  case 141: /* modifier_context: IDENT  */
#line 1517 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 5239 "src/parser.tab.c"
    break;

  case 142: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1521 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 5247 "src/parser.tab.c"
    break;

  case 143: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 1524 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 5255 "src/parser.tab.c"
    break;

  case 144: /* watch_statement: WATCH IDENT LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1532 "src/parser.y"
                                                                                           {
        (yyval.stmt) = ast_watch((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 5263 "src/parser.tab.c"
    break;

  case 145: /* unwatch_statement: UNWATCH expression  */
#line 1538 "src/parser.y"
                         { (yyval.stmt) = ast_unwatch((yyvsp[0].expr)); }
#line 5269 "src/parser.tab.c"
    break;

  case 146: /* wait_statement: WAIT UNTIL expression  */
#line 1545 "src/parser.y"
                            { (yyval.stmt) = ast_wait((yyvsp[0].expr), NULL); }
#line 5275 "src/parser.tab.c"
    break;

  case 147: /* wait_statement: WAIT UNTIL expression FOR expression  */
#line 1546 "src/parser.y"
                                           { (yyval.stmt) = ast_wait((yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 5281 "src/parser.tab.c"
    break;

  case 148: /* watch_target_list: watch_target_path  */
#line 1550 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5287 "src/parser.tab.c"
    break;

  case 149: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 1551 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5293 "src/parser.tab.c"
    break;

  case 150: /* @2: %empty  */
#line 1571 "src/parser.y"
      { (yyval.stmt) = NULL;   /* the mid-rule carries no value; typed so bison stays quiet */
        server_head_note(ctx, (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column); }
#line 5300 "src/parser.tab.c"
    break;

  case 151: /* server_statement: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE @2 server_item_list END IDENT NEWLINE  */
#line 1573 "src/parser.y"
                                         {
        ctx->bad_block_word[0] = '\0';
        (yyval.stmt) = ast_server((yyvsp[-10].text), (yyvsp[-9].text), (yyvsp[-7].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 5309 "src/parser.tab.c"
    break;

  case 152: /* @3: %empty  */
#line 1578 "src/parser.y"
      { (yyval.stmt) = NULL;   /* the mid-rule carries no value; typed so bison stays quiet */
        server_head_note(ctx, (yyvsp[-4].text), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5316 "src/parser.tab.c"
    break;

  case 153: /* server_statement: IDENT IDENT LPAREN RPAREN NEWLINE @3 server_item_list END IDENT NEWLINE  */
#line 1580 "src/parser.y"
                                         {
        ctx->bad_block_word[0] = '\0';
        (yyval.stmt) = ast_server((yyvsp[-9].text), (yyvsp[-8].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 5325 "src/parser.tab.c"
    break;

  case 154: /* gsql_block: DATABASE_OPEN IDENT LPAREN record_field_list RPAREN NEWLINE  */
#line 1600 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_database((yyvsp[-4].text), (yyvsp[-2].record_field_list), (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 5333 "src/parser.tab.c"
    break;

  case 155: /* gsql_block: DATABASE_OPEN IDENT LPAREN RPAREN NEWLINE  */
#line 1603 "src/parser.y"
                                                {
        (yyval.stmt) = ast_database((yyvsp[-3].text), ast_record_field_list_empty(), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5341 "src/parser.tab.c"
    break;

  case 156: /* gsql_block: SQL_OPEN IDENT ON IDENT NEWLINE SQL_TEXT END IDENT NEWLINE  */
#line 1606 "src/parser.y"
                                                                 {
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_sql((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].text), (yylsp[-3]).first_line, (yylsp[-3]).first_column, (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5350 "src/parser.tab.c"
    break;

  case 157: /* gsql_block: SQL_OPEN IDENT ON IDENT DOT IDENT NEWLINE SQL_TEXT END IDENT NEWLINE  */
#line 1612 "src/parser.y"
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
#line 5367 "src/parser.tab.c"
    break;

  case 158: /* window_block: WINDOW_OPEN IDENT LPAREN record_field_list RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1627 "src/parser.y"
                                                                                                   {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), NULL, NULL, NULL, (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5376 "src/parser.tab.c"
    break;

  case 159: /* window_block: WINDOW_OPEN IDENT LPAREN RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1631 "src/parser.y"
                                                                                 {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text), NULL, NULL, NULL,
                        (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5386 "src/parser.tab.c"
    break;

  case 160: /* window_block: WINDOW_OPEN IDENT LPAREN record_field_list RPAREN TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1641 "src/parser.y"
                                                                                                                  {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-11].text), (yyvsp[-9].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text),
                        (yylsp[-12]).first_line, (yylsp[-12]).first_column);
      }
#line 5396 "src/parser.tab.c"
    break;

  case 161: /* window_item_list: %empty  */
#line 1649 "src/parser.y"
             { (yyval.window_item_list) = ast_window_item_list_empty(); }
#line 5402 "src/parser.tab.c"
    break;

  case 162: /* window_item_list: window_item_list NEWLINE  */
#line 1650 "src/parser.y"
                               { (yyval.window_item_list) = (yyvsp[-1].window_item_list); }
#line 5408 "src/parser.tab.c"
    break;

  case 163: /* window_item_list: window_item_list window_item  */
#line 1651 "src/parser.y"
                                   { (yyval.window_item_list) = ast_window_item_list_append((yyvsp[-1].window_item_list), (yyvsp[0].window_item)); }
#line 5414 "src/parser.tab.c"
    break;

  case 164: /* window_item: IDENT LPAREN RPAREN NEWLINE  */
#line 1656 "src/parser.y"
                                  {
        (yyval.window_item) = ast_window_leaf((yyvsp[-3].text), NULL, ast_record_field_list_empty(),
                             (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 5423 "src/parser.tab.c"
    break;

  case 165: /* window_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE  */
#line 1661 "src/parser.y"
                                                          {
        (yyval.window_item) = ast_window_leaf((yyvsp[-5].text), (yyvsp[-4].text), (yyvsp[-2].record_field_list), (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 5431 "src/parser.tab.c"
    break;

  case 166: /* window_item: IDENT IDENT LPAREN RPAREN NEWLINE  */
#line 1665 "src/parser.y"
                                        {
        (yyval.window_item) = ast_window_leaf((yyvsp[-4].text), (yyvsp[-3].text), ast_record_field_list_empty(),
                             (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5440 "src/parser.tab.c"
    break;

  case 167: /* window_item: IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1670 "src/parser.y"
                                                       {
        (yyval.window_item) = ast_window_container((yyvsp[-5].text), NULL, ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                  (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 5449 "src/parser.tab.c"
    break;

  case 168: /* window_item: IDENT LPAREN record_field_list RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1675 "src/parser.y"
                                                                                       {
        (yyval.window_item) = ast_window_container((yyvsp[-8].text), NULL, (yyvsp[-6].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5457 "src/parser.tab.c"
    break;

  case 169: /* window_item: IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1689 "src/parser.y"
                                                             {
        (yyval.window_item) = ast_window_named_form((yyvsp[-6].text), (yyvsp[-5].text), NULL, NULL, NULL, (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                   (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5466 "src/parser.tab.c"
    break;

  case 170: /* window_item: IDENT IDENT STRING NEWLINE window_item_list END IDENT NEWLINE  */
#line 1704 "src/parser.y"
                                                                    {
        (yyval.window_item) = ast_window_named_container((yyvsp[-7].text), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                        (yylsp[-7]).first_line, (yylsp[-7]).first_column);
      }
#line 5475 "src/parser.tab.c"
    break;

  case 171: /* window_item: IDENT STRING NEWLINE window_item_list END IDENT NEWLINE  */
#line 1709 "src/parser.y"
                                                              {
        (yyval.window_item) = ast_window_container((yyvsp[-6].text), (yyvsp[-5].text), ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                  (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5484 "src/parser.tab.c"
    break;

  case 172: /* window_item: IDENT TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1720 "src/parser.y"
                                                                      {
        (yyval.window_item) = ast_window_form((yyvsp[-8].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                             (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5493 "src/parser.tab.c"
    break;

  case 173: /* window_item: IDENT IDENT TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1735 "src/parser.y"
                                                                            {
        (yyval.window_item) = ast_window_named_form((yyvsp[-9].text), (yyvsp[-8].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                   (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5502 "src/parser.tab.c"
    break;

  case 174: /* window_item: ON IDENT DOT IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1740 "src/parser.y"
                                                               {
        (yyval.window_item) = ast_window_handler((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5510 "src/parser.tab.c"
    break;

  case 175: /* server_item_list: %empty  */
#line 1746 "src/parser.y"
             { (yyval.server_item_list) = ast_server_item_list_empty(); }
#line 5516 "src/parser.tab.c"
    break;

  case 176: /* server_item_list: server_item_list NEWLINE  */
#line 1747 "src/parser.y"
                               { (yyval.server_item_list) = (yyvsp[-1].server_item_list); }
#line 5522 "src/parser.tab.c"
    break;

  case 177: /* server_item_list: server_item_list server_item  */
#line 1748 "src/parser.y"
                                   { (yyval.server_item_list) = ast_server_item_list_append((yyvsp[-1].server_item_list), (yyvsp[0].server_item)); }
#line 5528 "src/parser.tab.c"
    break;

  case 178: /* server_item: IDENT server_string_list NEWLINE  */
#line 1752 "src/parser.y"
                                       {
        (yyval.server_item) = ast_server_directive((yyvsp[-2].text), (yyvsp[-1].name_list), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 5536 "src/parser.tab.c"
    break;

  case 179: /* server_item: IDENT STRING LPAREN parameter_list_opt RPAREN NEWLINE statement_list END IDENT NEWLINE  */
#line 1755 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_handler((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5544 "src/parser.tab.c"
    break;

  case 180: /* server_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1758 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_site((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5552 "src/parser.tab.c"
    break;

  case 181: /* server_item: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1761 "src/parser.y"
                                                                           {
        (yyval.server_item) = ast_server_site((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5560 "src/parser.tab.c"
    break;

  case 182: /* server_item: ON IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1764 "src/parser.y"
                                                     {
        (yyval.server_item) = ast_server_hook((yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5568 "src/parser.tab.c"
    break;

  case 183: /* server_string_list: STRING  */
#line 1770 "src/parser.y"
             { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5574 "src/parser.tab.c"
    break;

  case 184: /* server_string_list: server_string_list COMMA STRING  */
#line 1771 "src/parser.y"
                                      { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5580 "src/parser.tab.c"
    break;

  case 185: /* watch_target_path: variable_name  */
#line 1775 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 5586 "src/parser.tab.c"
    break;

  case 186: /* watch_target_path: watch_target_path DOT IDENT  */
#line 1776 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 5592 "src/parser.tab.c"
    break;

  case 187: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 1780 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 5600 "src/parser.tab.c"
    break;

  case 188: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 1786 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 5606 "src/parser.tab.c"
    break;

  case 189: /* on_error_statement: ON ERROR_VALUE GOTO NEXT  */
#line 1787 "src/parser.y"
                               { (yyval.stmt) = ast_on_error_goto_next(); }
#line 5612 "src/parser.tab.c"
    break;

  case 190: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 1788 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 5618 "src/parser.tab.c"
    break;

  case 191: /* on_error_statement: ON IDENT GOTO NEXT  */
#line 1789 "src/parser.y"
                         {
        if (!warn_channel_ok(ctx, (yyvsp[-2].text), (yylsp[-2]).first_line, (yylsp[-2]).first_column)) { GB_REFUSE; }
        free((yyvsp[-2].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_NEXT);
      }
#line 5628 "src/parser.tab.c"
    break;

  case 192: /* on_error_statement: ON IDENT GOTO IDENT  */
#line 1794 "src/parser.y"
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
#line 5648 "src/parser.tab.c"
    break;

  case 193: /* on_error_statement: ON IDENT STOP  */
#line 1809 "src/parser.y"
                    {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { GB_REFUSE; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_STOP);
      }
#line 5658 "src/parser.tab.c"
    break;

  case 194: /* on_error_statement: ON IDENT PRINT  */
#line 1814 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { GB_REFUSE; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_PRINT);
      }
#line 5668 "src/parser.tab.c"
    break;

  case 195: /* on_error_statement: ON IDENT IDENT  */
#line 1819 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { GB_REFUSE; }
        int mode = warn_mode_word(ctx, (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column);
        if (mode < 0) { free((yyvsp[-1].text)); free((yyvsp[0].text)); (yyvsp[-1].text) = NULL; (yyvsp[0].text) = NULL; GB_REFUSE; }
        free((yyvsp[-1].text)); free((yyvsp[0].text));
        (yyval.stmt) = ast_on_warning(mode);
      }
#line 5680 "src/parser.tab.c"
    break;

  case 196: /* error_statement: ERROR_VALUE expression  */
#line 1829 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 5686 "src/parser.tab.c"
    break;

  case 197: /* return_statement: RETURN  */
#line 1833 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 5692 "src/parser.tab.c"
    break;

  case 198: /* return_statement: RETURN expression  */
#line 1834 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 5698 "src/parser.tab.c"
    break;

  case 199: /* label_statement: variable_name COLON  */
#line 1838 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 5704 "src/parser.tab.c"
    break;

  case 200: /* goto_statement: GOTO variable_name  */
#line 1845 "src/parser.y"
                         { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 5710 "src/parser.tab.c"
    break;

  case 201: /* gosub_statement: GOSUB variable_name  */
#line 1849 "src/parser.y"
                          { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 5716 "src/parser.tab.c"
    break;

  case 202: /* break_statement: BREAK  */
#line 1858 "src/parser.y"
            { (yyval.stmt) = ast_break(NULL); }
#line 5722 "src/parser.tab.c"
    break;

  case 203: /* break_statement: BREAK IDENT  */
#line 1859 "src/parser.y"
                  { (yyval.stmt) = ast_break((yyvsp[0].text)); }
#line 5728 "src/parser.tab.c"
    break;

  case 204: /* continue_statement: CONTINUE  */
#line 1863 "src/parser.y"
               { (yyval.stmt) = ast_continue(NULL); }
#line 5734 "src/parser.tab.c"
    break;

  case 205: /* continue_statement: CONTINUE IDENT  */
#line 1864 "src/parser.y"
                     { (yyval.stmt) = ast_continue((yyvsp[0].text)); }
#line 5740 "src/parser.tab.c"
    break;

  case 206: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1868 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 5749 "src/parser.tab.c"
    break;

  case 207: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1872 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 5758 "src/parser.tab.c"
    break;

  case 208: /* if_block_tail: END IF NEWLINE  */
#line 1879 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 5766 "src/parser.tab.c"
    break;

  case 209: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 1882 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 5774 "src/parser.tab.c"
    break;

  case 210: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1885 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 5782 "src/parser.tab.c"
    break;

  case 211: /* if_block_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1894 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5794 "src/parser.tab.c"
    break;

  case 212: /* if_inline_tail: %empty  */
#line 1904 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 5802 "src/parser.tab.c"
    break;

  case 213: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 1907 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 5810 "src/parser.tab.c"
    break;

  case 214: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1910 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 5818 "src/parser.tab.c"
    break;

  case 215: /* if_inline_tail: ELSE IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1916 "src/parser.y"
                                                                      {
        AstStmt *inner = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5830 "src/parser.tab.c"
    break;

  case 216: /* if_inline_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1923 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5842 "src/parser.tab.c"
    break;

  case 217: /* inline_statement: assignment  */
#line 1933 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5848 "src/parser.tab.c"
    break;

  case 218: /* inline_statement: print_statement  */
#line 1934 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5854 "src/parser.tab.c"
    break;

  case 219: /* inline_statement: call_statement  */
#line 1935 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5860 "src/parser.tab.c"
    break;

  case 220: /* inline_statement: use_statement  */
#line 1936 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5866 "src/parser.tab.c"
    break;

  case 221: /* inline_statement: on_error_statement  */
#line 1937 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5872 "src/parser.tab.c"
    break;

  case 222: /* inline_statement: error_statement  */
#line 1938 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5878 "src/parser.tab.c"
    break;

  case 223: /* inline_statement: return_statement  */
#line 1939 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5884 "src/parser.tab.c"
    break;

  case 224: /* inline_statement: goto_statement  */
#line 1940 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5890 "src/parser.tab.c"
    break;

  case 225: /* inline_statement: gosub_statement  */
#line 1941 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5896 "src/parser.tab.c"
    break;

  case 226: /* inline_statement: break_statement  */
#line 1942 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5902 "src/parser.tab.c"
    break;

  case 227: /* inline_statement: continue_statement  */
#line 1943 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5908 "src/parser.tab.c"
    break;

  case 228: /* expression: conditional_expression  */
#line 1951 "src/parser.y"
                             { (yyval.expr) = (yyvsp[0].expr); }
#line 5914 "src/parser.tab.c"
    break;

  case 229: /* expression: TRY expression  */
#line 1952 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_unary(copy_const("try"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5920 "src/parser.tab.c"
    break;

  case 230: /* conditional_expression: or_expression  */
#line 1956 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 5926 "src/parser.tab.c"
    break;

  case 231: /* conditional_expression: or_expression IF or_expression ELSE conditional_expression  */
#line 1957 "src/parser.y"
                                                                 {
        (yyval.expr) = expr_at(ast_conditional((yyvsp[-2].expr), (yyvsp[-4].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 5934 "src/parser.tab.c"
    break;

  case 232: /* or_expression: and_expression  */
#line 1963 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5940 "src/parser.tab.c"
    break;

  case 233: /* or_expression: or_expression OR and_expression  */
#line 1964 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5946 "src/parser.tab.c"
    break;

  case 234: /* and_expression: not_expression  */
#line 1968 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5952 "src/parser.tab.c"
    break;

  case 235: /* and_expression: and_expression AND not_expression  */
#line 1969 "src/parser.y"
                                        { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5958 "src/parser.tab.c"
    break;

  case 236: /* not_expression: comparison_expression  */
#line 1991 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 5964 "src/parser.tab.c"
    break;

  case 237: /* not_expression: NOT not_expression  */
#line 1992 "src/parser.y"
                         { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5970 "src/parser.tab.c"
    break;

  case 238: /* comparison_expression: set_expression  */
#line 1996 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5976 "src/parser.tab.c"
    break;

  case 239: /* comparison_expression: set_expression comparison_operator set_expression  */
#line 1997 "src/parser.y"
                                                        { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5982 "src/parser.tab.c"
    break;

  case 240: /* comparison_expression: set_expression comparison_lens comparison_operator set_expression  */
#line 1998 "src/parser.y"
                                                                        {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 5990 "src/parser.tab.c"
    break;

  case 241: /* set_expression: additive_expression  */
#line 2004 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 5996 "src/parser.tab.c"
    break;

  case 242: /* set_expression: set_expression EXCLUDING additive_expression  */
#line 2005 "src/parser.y"
                                                   { (yyval.expr) = expr_at(ast_binary(copy_const("excluding"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6002 "src/parser.tab.c"
    break;

  case 243: /* set_expression: set_expression INTERSECTING additive_expression  */
#line 2006 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("intersecting"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6008 "src/parser.tab.c"
    break;

  case 244: /* additive_expression: multiplicative_expression  */
#line 2010 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 6014 "src/parser.tab.c"
    break;

  case 245: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 2011 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6020 "src/parser.tab.c"
    break;

  case 246: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 2012 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6026 "src/parser.tab.c"
    break;

  case 247: /* multiplicative_expression: unary_expression  */
#line 2016 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 6032 "src/parser.tab.c"
    break;

  case 248: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 2017 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6038 "src/parser.tab.c"
    break;

  case 249: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 2018 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6044 "src/parser.tab.c"
    break;

  case 250: /* unary_expression: postfix_expression  */
#line 2022 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 6050 "src/parser.tab.c"
    break;

  case 251: /* unary_expression: MINUS unary_expression  */
#line 2023 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6056 "src/parser.tab.c"
    break;

  case 252: /* unary_expression: MODIFIER_PREFIX unary_expression  */
#line 2030 "src/parser.y"
                                       {
        (yyval.expr) = expr_at(ast_modifier_apply(parse_modifier_use((yyvsp[-1].text)), (yyvsp[0].expr)),
                     (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 6065 "src/parser.tab.c"
    break;

  case 253: /* unary_expression: NEW postfix_expression  */
#line 2034 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6071 "src/parser.tab.c"
    break;

  case 254: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 2035 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 6077 "src/parser.tab.c"
    break;

  case 255: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 2036 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 6083 "src/parser.tab.c"
    break;

  case 256: /* postfix_expression: primary  */
#line 2040 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 6089 "src/parser.tab.c"
    break;

  case 257: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 2041 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 6095 "src/parser.tab.c"
    break;

  case 258: /* postfix_expression: postfix_expression LBRACKET expression COMMA argument_list RBRACKET  */
#line 2045 "src/parser.y"
                                                                          {
        AstExprList items = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[-3].expr));
        for (size_t i = 0; i < (yyvsp[-1].expr_list).count; i++) {
            items = ast_expr_list_append(items, (yyvsp[-1].expr_list).items[i]);
        }
        free((yyvsp[-1].expr_list).items);
        (yyval.expr) = expr_at(ast_index_list((yyvsp[-5].expr), items), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 6108 "src/parser.tab.c"
    break;

  case 259: /* postfix_expression: postfix_expression DOT dot_field_name  */
#line 2053 "src/parser.y"
                                            { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6114 "src/parser.tab.c"
    break;

  case 260: /* postfix_expression: postfix_expression PATH_LBRACE projection_names RBRACE  */
#line 2058 "src/parser.y"
                                                             {
        (yyval.expr) = expr_at(ast_project((yyvsp[-3].expr), (yyvsp[-1].name_list)), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 6122 "src/parser.tab.c"
    break;

  case 261: /* postfix_expression: postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 2061 "src/parser.y"
                                                                   {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        (yyval.expr) = expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 6132 "src/parser.tab.c"
    break;

  case 262: /* postfix_expression: postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 2066 "src/parser.y"
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
#line 6148 "src/parser.tab.c"
    break;

  case 263: /* comparison_operator: OP_EQ  */
#line 2080 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 6154 "src/parser.tab.c"
    break;

  case 264: /* comparison_operator: OP_NE  */
#line 2081 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 6160 "src/parser.tab.c"
    break;

  case 265: /* comparison_operator: OP_GT  */
#line 2082 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 6166 "src/parser.tab.c"
    break;

  case 266: /* comparison_operator: OP_LT  */
#line 2083 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 6172 "src/parser.tab.c"
    break;

  case 267: /* comparison_operator: OP_GE  */
#line 2084 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 6178 "src/parser.tab.c"
    break;

  case 268: /* comparison_operator: OP_LE  */
#line 2085 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 6184 "src/parser.tab.c"
    break;

  case 269: /* comparison_operator: OP_NGT  */
#line 2086 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 6190 "src/parser.tab.c"
    break;

  case 270: /* comparison_operator: OP_NLT  */
#line 2087 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 6196 "src/parser.tab.c"
    break;

  case 271: /* comparison_operator: OP_NGE  */
#line 2088 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 6202 "src/parser.tab.c"
    break;

  case 272: /* comparison_operator: OP_NLE  */
#line 2089 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 6208 "src/parser.tab.c"
    break;

  case 273: /* primary: MODIFIER_VALUE  */
#line 2093 "src/parser.y"
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
#line 6231 "src/parser.tab.c"
    break;

  case 274: /* primary: DOT dot_field_name  */
#line 2114 "src/parser.y"
                         { (yyval.expr) = expr_at(ast_column((yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6237 "src/parser.tab.c"
    break;

  case 275: /* primary: NUMBER  */
#line 2115 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact, (yyvsp[0].numlit).scale), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6243 "src/parser.tab.c"
    break;

  case 276: /* primary: WATCHERS LPAREN RPAREN  */
#line 2116 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_call(copy_const("watchers"), ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 6249 "src/parser.tab.c"
    break;

  case 277: /* primary: duration_terms  */
#line 2117 "src/parser.y"
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
#line 6278 "src/parser.tab.c"
    break;

  case 278: /* primary: STRING  */
#line 2141 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6284 "src/parser.tab.c"
    break;

  case 279: /* primary: variable_name ident_suffix  */
#line 2142 "src/parser.y"
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
#line 6306 "src/parser.tab.c"
    break;

  case 280: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 2159 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 6317 "src/parser.tab.c"
    break;

  case 281: /* primary: ERROR_VALUE  */
#line 2165 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6323 "src/parser.tab.c"
    break;

  case 282: /* primary: TRUE  */
#line 2166 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6329 "src/parser.tab.c"
    break;

  case 283: /* primary: FALSE  */
#line 2167 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6335 "src/parser.tab.c"
    break;

  case 284: /* primary: NOTHING  */
#line 2168 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6341 "src/parser.tab.c"
    break;

  case 285: /* primary: UNKNOWN_VALUE  */
#line 2169 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6347 "src/parser.tab.c"
    break;

  case 286: /* primary: LPAREN expression RPAREN  */
#line 2170 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); (yyval.expr)->parenthesized = 1; }
#line 6353 "src/parser.tab.c"
    break;

  case 287: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 2171 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 6359 "src/parser.tab.c"
    break;

  case 288: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 2172 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 6365 "src/parser.tab.c"
    break;

  case 289: /* primary: record_literal  */
#line 2173 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 6371 "src/parser.tab.c"
    break;

  case 290: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 2177 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 6377 "src/parser.tab.c"
    break;

  case 291: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 2178 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 6383 "src/parser.tab.c"
    break;

  case 292: /* ident_suffix: %empty  */
#line 2182 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 6393 "src/parser.tab.c"
    break;

  case 293: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 2187 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 6403 "src/parser.tab.c"
    break;

  case 294: /* ident_suffix: DOT dot_field_name ident_dot_suffix  */
#line 2192 "src/parser.y"
                                          {
        /* dot_field_name, not IDENT: a keyword is a legal FIELD name after a
         * dot, because nothing but a name can appear there. */
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 6414 "src/parser.tab.c"
    break;

  case 295: /* ident_suffix: DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 2198 "src/parser.y"
                                                          {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        (yyval.ident_suffix).kind = IDENT_SUFFIX_METHOD;
        (yyval.ident_suffix).name = (yyvsp[-3].text);
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 6428 "src/parser.tab.c"
    break;

  case 296: /* ident_dot_suffix: %empty  */
#line 2210 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 6438 "src/parser.tab.c"
    break;

  case 297: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 2215 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 6448 "src/parser.tab.c"
    break;

  case 298: /* duration_terms: NUMBER IDENT  */
#line 2223 "src/parser.y"
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
#line 6464 "src/parser.tab.c"
    break;

  case 299: /* duration_terms: duration_terms NUMBER IDENT  */
#line 2234 "src/parser.y"
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
#line 6479 "src/parser.tab.c"
    break;

  case 300: /* argument_list_opt: %empty  */
#line 2247 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 6485 "src/parser.tab.c"
    break;

  case 301: /* argument_list_opt: argument_list  */
#line 2248 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 6491 "src/parser.tab.c"
    break;

  case 302: /* argument_list: expression  */
#line 2252 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 6497 "src/parser.tab.c"
    break;

  case 303: /* argument_list: argument_list COMMA expression  */
#line 2253 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 6503 "src/parser.tab.c"
    break;

  case 304: /* array_argument_list: expression  */
#line 2257 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 6509 "src/parser.tab.c"
    break;

  case 305: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 2258 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 6515 "src/parser.tab.c"
    break;

  case 306: /* parameter_list_opt: %empty  */
#line 2262 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 6521 "src/parser.tab.c"
    break;

  case 307: /* parameter_list_opt: parameter_list  */
#line 2263 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 6527 "src/parser.tab.c"
    break;

  case 308: /* parameter_default: NUMBER  */
#line 2277 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact, (yyvsp[0].numlit).scale), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6533 "src/parser.tab.c"
    break;

  case 309: /* parameter_default: MINUS NUMBER  */
#line 2281 "src/parser.y"
                   { (yyval.expr) = expr_at(ast_number(-(yyvsp[0].numlit).value,
                                             (yyvsp[0].numlit).exact == LLONG_MIN ? 0 : -(yyvsp[0].numlit).exact,
                                             (yyvsp[0].numlit).exact == LLONG_MIN ? 0 : (yyvsp[0].numlit).is_exact,
                                             (yyvsp[0].numlit).exact == LLONG_MIN ? 0 : (yyvsp[0].numlit).scale),
                                  (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6543 "src/parser.tab.c"
    break;

  case 310: /* parameter_default: PLUS NUMBER  */
#line 2286 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact, (yyvsp[0].numlit).scale), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6549 "src/parser.tab.c"
    break;

  case 311: /* parameter_default: STRING  */
#line 2287 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6555 "src/parser.tab.c"
    break;

  case 312: /* parameter_default: TRUE  */
#line 2288 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6561 "src/parser.tab.c"
    break;

  case 313: /* parameter_default: FALSE  */
#line 2289 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6567 "src/parser.tab.c"
    break;

  case 314: /* parameter_default: NOTHING  */
#line 2290 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6573 "src/parser.tab.c"
    break;

  case 315: /* parameter_default: UNKNOWN_VALUE  */
#line 2291 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6579 "src/parser.tab.c"
    break;

  case 316: /* parameter_list: IDENT  */
#line 2295 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 6585 "src/parser.tab.c"
    break;

  case 317: /* parameter_list: IDENT OP_EQ parameter_default  */
#line 2296 "src/parser.y"
                                    {
        (yyval.name_list) = ast_name_list_append_default(ast_name_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 6593 "src/parser.tab.c"
    break;

  case 318: /* parameter_list: parameter_list COMMA IDENT  */
#line 2299 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 6599 "src/parser.tab.c"
    break;

  case 319: /* parameter_list: IDENT COLON type_lock  */
#line 2303 "src/parser.y"
                            {
        if (!type_lock_check(ctx, (yyvsp[-2].text), (yyvsp[0].type_lock).name, (yyvsp[0].type_lock).arg, (yylsp[0]).first_line, (yylsp[0]).first_column,
                             (yylsp[0]).last_line, (yylsp[0]).last_column)) {
            GB_REFUSE;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append(ast_name_list_empty(), (yyvsp[-2].text)), (yyvsp[0].type_lock));
      }
#line 6611 "src/parser.tab.c"
    break;

  case 320: /* parameter_list: IDENT COLON type_lock OP_EQ parameter_default  */
#line 2310 "src/parser.y"
                                                    {
        if (!type_lock_check(ctx, (yyvsp[-4].text), (yyvsp[-2].type_lock).name, (yyvsp[-2].type_lock).arg, (yylsp[-2]).first_line, (yylsp[-2]).first_column,
                             (yylsp[-2]).last_line, (yylsp[-2]).last_column)) {
            GB_REFUSE;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append_default(ast_name_list_empty(), (yyvsp[-4].text), (yyvsp[0].expr)), (yyvsp[-2].type_lock));
      }
#line 6623 "src/parser.tab.c"
    break;

  case 321: /* parameter_list: parameter_list COMMA IDENT COLON type_lock  */
#line 2317 "src/parser.y"
                                                 {
        if (!type_lock_check(ctx, (yyvsp[-2].text), (yyvsp[0].type_lock).name, (yyvsp[0].type_lock).arg, (yylsp[0]).first_line, (yylsp[0]).first_column,
                             (yylsp[0]).last_line, (yylsp[0]).last_column)) {
            GB_REFUSE;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append((yyvsp[-4].name_list), (yyvsp[-2].text)), (yyvsp[0].type_lock));
      }
#line 6635 "src/parser.tab.c"
    break;

  case 322: /* parameter_list: parameter_list COMMA IDENT COLON type_lock OP_EQ parameter_default  */
#line 2324 "src/parser.y"
                                                                         {
        if (!type_lock_check(ctx, (yyvsp[-4].text), (yyvsp[-2].type_lock).name, (yyvsp[-2].type_lock).arg, (yylsp[-2]).first_line, (yylsp[-2]).first_column,
                             (yylsp[-2]).last_line, (yylsp[-2]).last_column)) {
            GB_REFUSE;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append_default((yyvsp[-6].name_list), (yyvsp[-4].text), (yyvsp[0].expr)), (yyvsp[-2].type_lock));
      }
#line 6647 "src/parser.tab.c"
    break;

  case 323: /* parameter_list: parameter_list COMMA IDENT OP_EQ parameter_default  */
#line 2331 "src/parser.y"
                                                         {
        (yyval.name_list) = ast_name_list_append_default((yyvsp[-4].name_list), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 6655 "src/parser.tab.c"
    break;

  case 324: /* field_name: dot_field_name  */
#line 2346 "src/parser.y"
                     { (yyval.text) = (yyvsp[0].text); }
#line 6661 "src/parser.tab.c"
    break;

  case 325: /* field_name: STRING  */
#line 2353 "src/parser.y"
             { (yyval.text) = (yyvsp[0].text); }
#line 6667 "src/parser.tab.c"
    break;

  case 326: /* projection_names: dot_field_name  */
#line 2362 "src/parser.y"
                     { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 6673 "src/parser.tab.c"
    break;

  case 327: /* projection_names: projection_names COMMA dot_field_name  */
#line 2363 "src/parser.y"
                                            { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 6679 "src/parser.tab.c"
    break;

  case 328: /* dot_field_name: IDENT  */
#line 2367 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 6685 "src/parser.tab.c"
    break;

  case 329: /* dot_field_name: AS  */
#line 2368 "src/parser.y"
                     { (yyval.text) = kw_name("as"); }
#line 6691 "src/parser.tab.c"
    break;

  case 330: /* dot_field_name: NEXT  */
#line 2369 "src/parser.y"
                     { (yyval.text) = kw_name("next"); }
#line 6697 "src/parser.tab.c"
    break;

  case 331: /* dot_field_name: STOP  */
#line 2370 "src/parser.y"
                     { (yyval.text) = kw_name("stop"); }
#line 6703 "src/parser.tab.c"
    break;

  case 332: /* dot_field_name: ERROR_VALUE  */
#line 2371 "src/parser.y"
                     { (yyval.text) = kw_name("error"); }
#line 6709 "src/parser.tab.c"
    break;

  case 333: /* dot_field_name: END  */
#line 2372 "src/parser.y"
                     { (yyval.text) = kw_name("end"); }
#line 6715 "src/parser.tab.c"
    break;

  case 334: /* dot_field_name: TO  */
#line 2373 "src/parser.y"
                     { (yyval.text) = kw_name("to"); }
#line 6721 "src/parser.tab.c"
    break;

  case 335: /* dot_field_name: IN  */
#line 2374 "src/parser.y"
                     { (yyval.text) = kw_name("in"); }
#line 6727 "src/parser.tab.c"
    break;

  case 336: /* dot_field_name: ON  */
#line 2375 "src/parser.y"
                     { (yyval.text) = kw_name("on"); }
#line 6733 "src/parser.tab.c"
    break;

  case 337: /* dot_field_name: NEW  */
#line 2376 "src/parser.y"
                     { (yyval.text) = kw_name("new"); }
#line 6739 "src/parser.tab.c"
    break;

  case 338: /* dot_field_name: EACH  */
#line 2377 "src/parser.y"
                     { (yyval.text) = kw_name("each"); }
#line 6745 "src/parser.tab.c"
    break;

  case 339: /* dot_field_name: WITH  */
#line 2378 "src/parser.y"
                     { (yyval.text) = kw_name("with"); }
#line 6751 "src/parser.tab.c"
    break;

  case 340: /* dot_field_name: WITHOUT  */
#line 2379 "src/parser.y"
                     { (yyval.text) = kw_name("without"); }
#line 6757 "src/parser.tab.c"
    break;

  case 341: /* dot_field_name: THEN  */
#line 2380 "src/parser.y"
                     { (yyval.text) = kw_name("then"); }
#line 6763 "src/parser.tab.c"
    break;

  case 342: /* dot_field_name: ELSE  */
#line 2381 "src/parser.y"
                     { (yyval.text) = kw_name("else"); }
#line 6769 "src/parser.tab.c"
    break;

  case 343: /* dot_field_name: FOR  */
#line 2382 "src/parser.y"
                     { (yyval.text) = kw_name("for"); }
#line 6775 "src/parser.tab.c"
    break;

  case 344: /* dot_field_name: IF  */
#line 2383 "src/parser.y"
                     { (yyval.text) = kw_name("if"); }
#line 6781 "src/parser.tab.c"
    break;

  case 345: /* dot_field_name: WHILE  */
#line 2384 "src/parser.y"
                     { (yyval.text) = kw_name("while"); }
#line 6787 "src/parser.tab.c"
    break;

  case 346: /* dot_field_name: DO  */
#line 2385 "src/parser.y"
                     { (yyval.text) = kw_name("do"); }
#line 6793 "src/parser.tab.c"
    break;

  case 347: /* dot_field_name: UNTIL  */
#line 2386 "src/parser.y"
                     { (yyval.text) = kw_name("until"); }
#line 6799 "src/parser.tab.c"
    break;

  case 348: /* dot_field_name: PRINT  */
#line 2387 "src/parser.y"
                     { (yyval.text) = kw_name("print"); }
#line 6805 "src/parser.tab.c"
    break;

  case 349: /* dot_field_name: RETURN  */
#line 2388 "src/parser.y"
                     { (yyval.text) = kw_name("return"); }
#line 6811 "src/parser.tab.c"
    break;

  case 350: /* dot_field_name: LOAD  */
#line 2389 "src/parser.y"
                     { (yyval.text) = kw_name("load"); }
#line 6817 "src/parser.tab.c"
    break;

  case 351: /* dot_field_name: USE  */
#line 2390 "src/parser.y"
                     { (yyval.text) = kw_name("use"); }
#line 6823 "src/parser.tab.c"
    break;

  case 352: /* dot_field_name: NOT  */
#line 2391 "src/parser.y"
                     { (yyval.text) = kw_name("not"); }
#line 6829 "src/parser.tab.c"
    break;

  case 353: /* dot_field_name: TRY  */
#line 2392 "src/parser.y"
                     { (yyval.text) = kw_name("try"); }
#line 6835 "src/parser.tab.c"
    break;

  case 354: /* dot_field_name: AND  */
#line 2393 "src/parser.y"
                     { (yyval.text) = kw_name("and"); }
#line 6841 "src/parser.tab.c"
    break;

  case 355: /* dot_field_name: OR  */
#line 2394 "src/parser.y"
                     { (yyval.text) = kw_name("or"); }
#line 6847 "src/parser.tab.c"
    break;

  case 356: /* dot_field_name: TRUE  */
#line 2395 "src/parser.y"
                     { (yyval.text) = kw_name("true"); }
#line 6853 "src/parser.tab.c"
    break;

  case 357: /* dot_field_name: FALSE  */
#line 2396 "src/parser.y"
                     { (yyval.text) = kw_name("false"); }
#line 6859 "src/parser.tab.c"
    break;

  case 358: /* dot_field_name: NOTHING  */
#line 2397 "src/parser.y"
                     { (yyval.text) = kw_name("nothing"); }
#line 6865 "src/parser.tab.c"
    break;

  case 359: /* dot_field_name: BREAK  */
#line 2398 "src/parser.y"
                     { (yyval.text) = kw_name("break"); }
#line 6871 "src/parser.tab.c"
    break;

  case 360: /* dot_field_name: CONTINUE  */
#line 2399 "src/parser.y"
                     { (yyval.text) = kw_name("continue"); }
#line 6877 "src/parser.tab.c"
    break;

  case 361: /* dot_field_name: GOTO  */
#line 2400 "src/parser.y"
                     { (yyval.text) = kw_name("goto"); }
#line 6883 "src/parser.tab.c"
    break;

  case 362: /* dot_field_name: GOSUB  */
#line 2401 "src/parser.y"
                     { (yyval.text) = kw_name("gosub"); }
#line 6889 "src/parser.tab.c"
    break;

  case 363: /* dot_field_name: SPAWN  */
#line 2402 "src/parser.y"
                     { (yyval.text) = kw_name("spawn"); }
#line 6895 "src/parser.tab.c"
    break;

  case 364: /* dot_field_name: EXPORT  */
#line 2403 "src/parser.y"
                     { (yyval.text) = kw_name("export"); }
#line 6901 "src/parser.tab.c"
    break;

  case 365: /* dot_field_name: LIBRARY  */
#line 2404 "src/parser.y"
                     { (yyval.text) = kw_name("library"); }
#line 6907 "src/parser.tab.c"
    break;

  case 366: /* dot_field_name: FUNCTION  */
#line 2405 "src/parser.y"
                     { (yyval.text) = kw_name("function"); }
#line 6913 "src/parser.tab.c"
    break;

  case 367: /* dot_field_name: MODIFIER  */
#line 2406 "src/parser.y"
                     { (yyval.text) = kw_name("modifier"); }
#line 6919 "src/parser.tab.c"
    break;

  case 368: /* dot_field_name: PROGRAM  */
#line 2407 "src/parser.y"
                     { (yyval.text) = kw_name("program"); }
#line 6925 "src/parser.tab.c"
    break;

  case 369: /* dot_field_name: WATCH  */
#line 2408 "src/parser.y"
                     { (yyval.text) = kw_name("watch"); }
#line 6931 "src/parser.tab.c"
    break;

  case 370: /* dot_field_name: WATCHERS  */
#line 2409 "src/parser.y"
                     { (yyval.text) = kw_name("watchers"); }
#line 6937 "src/parser.tab.c"
    break;

  case 371: /* dot_field_name: CONSIDER  */
#line 2410 "src/parser.y"
                     { (yyval.text) = kw_name("consider"); }
#line 6943 "src/parser.tab.c"
    break;

  case 372: /* dot_field_name: STEP  */
#line 2411 "src/parser.y"
                     { (yyval.text) = kw_name("step"); }
#line 6949 "src/parser.tab.c"
    break;

  case 373: /* dot_field_name: UNWATCH  */
#line 2412 "src/parser.y"
                     { (yyval.text) = kw_name("unwatch"); }
#line 6955 "src/parser.tab.c"
    break;

  case 374: /* dot_field_name: UNKNOWN_VALUE  */
#line 2413 "src/parser.y"
                     { (yyval.text) = kw_name("unknown"); }
#line 6961 "src/parser.tab.c"
    break;

  case 375: /* dot_field_name: DIM  */
#line 2414 "src/parser.y"
                     { (yyval.text) = kw_name("dim"); }
#line 6967 "src/parser.tab.c"
    break;

  case 376: /* record_field_list: field_name OP_EQ expression  */
#line 2418 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6973 "src/parser.tab.c"
    break;

  case 377: /* record_field_list: field_name COLON expression  */
#line 2419 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6979 "src/parser.tab.c"
    break;

  case 378: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 2420 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 6985 "src/parser.tab.c"
    break;

  case 379: /* record_field_list: record_field_list COMMA optional_newlines field_name OP_EQ expression  */
#line 2421 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6991 "src/parser.tab.c"
    break;

  case 380: /* record_field_list: record_field_list COMMA optional_newlines field_name COLON expression  */
#line 2422 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6997 "src/parser.tab.c"
    break;

  case 381: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 2423 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 7003 "src/parser.tab.c"
    break;

  case 382: /* field_policy: IDENT  */
#line 2431 "src/parser.y"
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
#line 7035 "src/parser.tab.c"
    break;

  case 383: /* field_policy: IDENT expression  */
#line 2458 "src/parser.y"
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
#line 7056 "src/parser.tab.c"
    break;


#line 7060 "src/parser.tab.c"

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

#line 2481 "src/parser.y"


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
