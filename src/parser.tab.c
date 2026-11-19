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



#line 709 "src/parser.tab.c"

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
  YYSYMBOL_or_expression = 159,            /* or_expression  */
  YYSYMBOL_and_expression = 160,           /* and_expression  */
  YYSYMBOL_not_expression = 161,           /* not_expression  */
  YYSYMBOL_comparison_expression = 162,    /* comparison_expression  */
  YYSYMBOL_set_expression = 163,           /* set_expression  */
  YYSYMBOL_additive_expression = 164,      /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 165, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 166,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 167,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 168,      /* comparison_operator  */
  YYSYMBOL_primary = 169,                  /* primary  */
  YYSYMBOL_record_literal = 170,           /* record_literal  */
  YYSYMBOL_ident_suffix = 171,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 172,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 173,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 174,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 175,            /* argument_list  */
  YYSYMBOL_array_argument_list = 176,      /* array_argument_list  */
  YYSYMBOL_parameter_list_opt = 177,       /* parameter_list_opt  */
  YYSYMBOL_parameter_default = 178,        /* parameter_default  */
  YYSYMBOL_parameter_list = 179,           /* parameter_list  */
  YYSYMBOL_field_name = 180,               /* field_name  */
  YYSYMBOL_projection_names = 181,         /* projection_names  */
  YYSYMBOL_dot_field_name = 182,           /* dot_field_name  */
  YYSYMBOL_record_field_list = 183,        /* record_field_list  */
  YYSYMBOL_field_policy = 184,             /* field_policy  */
  YYSYMBOL_optional_newlines = 185         /* optional_newlines  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;



/* Unqualified %code blocks.  */
#line 729 "src/parser.y"

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

#line 1018 "src/parser.tab.c"

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
#define YYLAST   3668

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  98
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  88
/* YYNRULES -- Number of rules.  */
#define YYNRULES  379
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  884

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
       0,   901,   901,   905,   906,   907,   911,   912,   913,   914,
     915,   916,   917,   918,   919,   920,   921,   922,   923,   924,
     925,   926,   927,   928,   929,   930,   931,   932,   933,   934,
     935,   936,   937,   938,   939,   945,   956,   961,   962,   978,
     985,   999,  1000,  1004,  1005,  1006,  1007,  1011,  1012,  1016,
    1024,  1028,  1029,  1030,  1041,  1041,  1049,  1053,  1054,  1058,
    1059,  1060,  1061,  1065,  1071,  1075,  1078,  1079,  1085,  1090,
    1099,  1112,  1141,  1142,  1143,  1147,  1151,  1167,  1172,  1180,
    1184,  1205,  1211,  1217,  1223,  1226,  1232,  1233,  1237,  1238,
    1239,  1243,  1244,  1245,  1246,  1247,  1248,  1249,  1250,  1251,
    1252,  1253,  1254,  1255,  1256,  1257,  1258,  1259,  1260,  1261,
    1262,  1263,  1264,  1265,  1266,  1267,  1268,  1269,  1275,  1286,
    1289,  1296,  1305,  1317,  1320,  1326,  1332,  1338,  1339,  1340,
    1341,  1342,  1343,  1359,  1375,  1396,  1397,  1401,  1405,  1408,
    1416,  1422,  1429,  1430,  1434,  1435,  1455,  1454,  1462,  1461,
    1484,  1487,  1490,  1496,  1511,  1515,  1525,  1533,  1534,  1535,
    1540,  1545,  1549,  1554,  1559,  1573,  1588,  1593,  1604,  1619,
    1624,  1630,  1631,  1632,  1636,  1639,  1642,  1645,  1648,  1654,
    1655,  1659,  1660,  1664,  1670,  1671,  1672,  1673,  1678,  1692,
    1697,  1702,  1712,  1716,  1717,  1721,  1728,  1732,  1741,  1742,
    1746,  1747,  1751,  1755,  1762,  1765,  1768,  1777,  1787,  1790,
    1793,  1799,  1806,  1816,  1817,  1818,  1819,  1820,  1821,  1822,
    1823,  1824,  1825,  1826,  1834,  1835,  1839,  1840,  1844,  1845,
    1867,  1868,  1872,  1873,  1874,  1880,  1881,  1882,  1886,  1887,
    1888,  1892,  1893,  1894,  1898,  1899,  1906,  1910,  1911,  1912,
    1916,  1917,  1921,  1929,  1934,  1937,  1942,  1956,  1957,  1958,
    1959,  1960,  1961,  1962,  1963,  1964,  1965,  1969,  1990,  1991,
    1992,  1993,  1994,  1995,  2012,  2018,  2019,  2020,  2021,  2022,
    2023,  2024,  2025,  2026,  2030,  2031,  2035,  2040,  2045,  2051,
    2063,  2068,  2076,  2086,  2098,  2099,  2103,  2104,  2108,  2109,
    2113,  2114,  2128,  2132,  2137,  2138,  2139,  2140,  2141,  2142,
    2146,  2147,  2150,  2154,  2161,  2168,  2175,  2182,  2197,  2204,
    2213,  2214,  2218,  2219,  2220,  2221,  2222,  2223,  2224,  2225,
    2226,  2227,  2228,  2229,  2230,  2231,  2232,  2233,  2234,  2235,
    2236,  2237,  2238,  2239,  2240,  2241,  2242,  2243,  2244,  2245,
    2246,  2247,  2248,  2249,  2250,  2251,  2252,  2253,  2254,  2255,
    2256,  2257,  2258,  2259,  2260,  2261,  2262,  2263,  2264,  2265,
    2269,  2270,  2271,  2272,  2273,  2274,  2282,  2309,  2328,  2329
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
  "if_inline_tail", "inline_statement", "expression", "or_expression",
  "and_expression", "not_expression", "comparison_expression",
  "set_expression", "additive_expression", "multiplicative_expression",
  "unary_expression", "postfix_expression", "comparison_operator",
  "primary", "record_literal", "ident_suffix", "ident_dot_suffix",
  "duration_terms", "argument_list_opt", "argument_list",
  "array_argument_list", "parameter_list_opt", "parameter_default",
  "parameter_list", "field_name", "projection_names", "dot_field_name",
  "record_field_list", "field_policy", "optional_newlines", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-594)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -594,    91,  1315,  -594,    24,    19,   148,   219,   223,  -594,
     862,  -594,   757,   862,   231,    81,    41,   862,   862,   240,
     255,   159,   862,   227,   227,   318,   862,   120,   244,    48,
    -594,   873,   328,   310,   355,    67,   286,   271,  -594,  -594,
     259,   131,   277,   280,   294,  -594,  -594,  -594,  -594,  -594,
    -594,  -594,  -594,  -594,   298,  -594,   304,   309,  -594,  -594,
    -594,  -594,   313,   320,   354,   376,   380,   389,   393,   401,
    -594,   336,   862,   862,   340,   410,   383,   375,  -594,  -594,
     420,   960,  -594,  -594,  -594,  -594,  -594,   555,   862,   971,
     502,   422,  -594,   960,   862,  -594,  -594,  3531,   155,   489,
     479,   483,  -594,  -594,   388,   282,   348,  -594,   161,  -594,
    -594,   518,   460,  -594,  -594,   443,    42,   527,  -594,   440,
     441,  -594,  -594,   457,   458,  -594,  -594,  -594,   459,   227,
    -594,   152,   450,  -594,   862,   452,   322,    23,  3598,  -594,
    -594,  -594,  -594,  -594,   215,  -594,   507,   465,   461,   208,
    -594,   547,  -594,   328,  -594,  -594,  -594,  -594,  -594,  -594,
     862,   862,  -594,  3330,   862,   522,   548,  -594,  -594,  -594,
    -594,  -594,  -594,  -594,  -594,  -594,  -594,  -594,  -594,  -594,
    2928,  -594,   469,   466,   475,  2995,  3062,   571,  -594,   862,
    -594,  -594,  -594,    70,   491,   492,  -594,   493,   658,  1047,
    -594,  -594,  -594,  -594,  -594,  -594,  -594,  -594,  -594,  -594,
    -594,  -594,  -594,  -594,  -594,  -594,  -594,  -594,  -594,  -594,
    -594,  -594,  -594,  -594,  -594,  -594,  -594,  -594,  -594,  -594,
    -594,  -594,  -594,  -594,  -594,  -594,  -594,  -594,  -594,  -594,
    -594,  -594,  -594,  -594,  -594,  -594,  -594,  -594,  -594,   862,
    3397,  -594,  2825,   555,   555,   960,   960,  -594,  -594,  -594,
    -594,  -594,  -594,  -594,  -594,  -594,  -594,   494,   960,   960,
     960,   960,   960,   862,  3531,  3464,   573,   862,   862,   862,
     862,   579,   138,  1181,  -594,   566,   592,   592,   227,   130,
     227,  -594,   594,   558,  -594,  -594,  -594,    94,  -594,   111,
    -594,   515,   592,  -594,   597,   592,  -594,   598,   600,   601,
     564,  -594,   243,   603,   526,   531,  -594,  -594,   862,   862,
     536,   551,   540,  -594,   514,     7,  -594,   170,  -594,   862,
    -594,   533,   176,   534,   177,   181,   542,   541,   862,  -594,
    -594,  -594,  -594,  -594,   538,  -594,   543,   545,   550,   553,
     554,  -594,  -594,   544,  -594,  -594,  -594,  -594,  -594,  -594,
    -594,  -594,  -594,  -594,   539,   483,  -594,   282,   282,   960,
     414,   348,   348,  -594,  -594,   249,   359,  -594,   556,   560,
    -594,  -594,  -594,   562,   557,   591,   607,   862,   642,   862,
    1382,   862,   351,    61,   563,   565,   578,   225,   575,   450,
    1449,  -594,   862,  1516,  -594,  -594,  -594,  -594,   862,   583,
    -594,   577,   585,  1583,   659,  -594,  -594,   597,  -594,   862,
     582,   862,   862,  -594,  -594,   671,   862,   673,  -594,   862,
     862,   586,  -594,  -594,  -594,    26,  -594,   587,   665,   677,
    -594,  -594,   596,  -594,   153,   288,  -594,   862,   862,  -594,
    1248,   548,   661,   414,  -594,   862,  -594,  3531,   862,   862,
     590,  -594,   862,   862,   599,   647,   606,   648,   676,   862,
     608,   675,   448,   548,   349,   700,   379,   612,  -594,   652,
    -594,   651,   625,  -594,  -594,   619,   646,   710,   622,   332,
    -594,   630,   633,   635,  -594,   862,   637,  -594,  -594,  -594,
    -594,  1114,    49,   720,  -594,  -594,   698,   634,  -594,   771,
    -594,  -594,   640,   650,   680,   706,  -594,  2789,  -594,   338,
    -594,   664,   666,  -594,  1650,     0,   639,  -594,   862,  -594,
     670,   674,   730,  -594,   678,  -594,  -594,  -594,  -594,  -594,
    -594,   754,   755,  -594,   688,   548,  -594,   142,   548,  -594,
    -594,  1717,   679,   684,  -594,  1784,  -594,   689,  -594,  -594,
    -594,  -594,  -594,  -594,  -594,   691,    62,  -594,   683,   144,
      52,   765,   766,  -594,  -594,   777,    75,   781,   773,  -594,
    -594,  -594,   862,  -594,   699,   702,   862,  -594,   707,  -594,
    -594,  -594,  1851,   768,    76,  -594,   862,  -594,  -594,  1650,
     713,  -594,  -594,   719,  2722,  -594,  -594,  -594,   448,   721,
    1918,   448,   548,   722,  1985,  2052,   760,  -594,  -594,   748,
    2119,  -594,  2186,   862,   471,   814,   819,  -594,  -594,    79,
     673,   862,   862,   154,   731,   821,  3129,  -594,   733,   732,
     734,   824,   737,   805,   810,  2253,  -594,  -594,   812,  2320,
    -594,   800,   747,  -594,   750,   752,  1650,  1650,  -594,  -594,
    2722,  -594,  -594,  -594,   756,   758,   759,  -594,  -594,  -594,
    -594,  -594,  -594,  -594,  -594,  -594,  -594,   762,  -594,   764,
     770,  -594,  -594,   779,   780,   782,   786,   790,   791,   792,
     793,  -594,  -594,  -594,   795,  -594,   788,  -594,   796,   794,
     797,   798,   808,   822,  -594,   776,   803,   236,   804,   815,
     892,   825,  -594,  -594,   823,   903,  3196,  -594,  -594,   910,
     826,   229,    93,  -594,   911,  -594,   827,  -594,   913,   828,
     896,  2861,   901,   832,  -594,  -594,  -594,  -594,  -594,  1650,
    2722,  -594,  -594,  -594,  -594,  -594,  -594,  -594,  -594,  -594,
    -594,  -594,  -594,  -594,  -594,  2387,   834,   448,  2454,   835,
     836,  -594,  -594,   837,   838,  3263,   592,   927,  -594,  -594,
    -594,   841,   844,  -594,   934,   846,   239,    97,   156,   847,
    -594,   848,   938,   850,   172,  -594,   852,  -594,   853,  -594,
     855,   857,  -594,  1650,  -594,   900,  -594,  -594,   902,  -594,
    -594,  -594,  -594,   860,   242,   870,  -594,  2521,  -594,   862,
     210,   864,  -594,   868,   962,   967,  -594,  -594,   879,  -594,
     969,  -594,  1248,  -594,  1248,   661,  -594,  -594,   884,   886,
    -594,   888,   889,   922,  -594,   980,  -594,  -594,   893,   895,
     256,   260,  -594,  2588,   904,  -594,  -594,  -594,  -594,  -594,
     314,  -594,  -594,   912,   914,   331,  -594,  -594,   981,   985,
     933,  -594,   991,   334,  2655,  -594,  -594,   992,   915,   916,
     917,   918,  1002,  1008,   920,  -594,  -594,  -594,  -594,   921,
     923,  -594,  -594,  -594
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
      34,     0,   294,   294,     0,     0,     0,   269,    51,   272,
       0,     0,   267,   276,   277,   278,   279,     0,     0,     0,
       0,     0,   275,     0,     0,   378,   378,     0,   286,     0,
     224,   226,   228,   230,   232,   235,   238,   241,   244,   250,
     283,   271,     0,    63,    66,     0,     0,     0,     3,     0,
       0,   199,   201,     0,     0,   194,   196,   197,    51,     0,
     181,     0,   144,   141,     0,     0,     0,     0,     0,   192,
      59,    61,    60,    62,   135,    57,     0,     0,     0,   128,
     130,   127,   129,     0,     6,    56,    43,    44,    45,    46,
       0,     0,    54,     0,     0,     0,   195,     7,     8,    20,
      23,    24,    26,    27,    28,    29,    30,    31,    32,    33,
       0,   296,     0,   295,     0,     0,     0,     0,   292,   294,
     246,   231,   225,   247,     0,     0,   245,     0,     0,     0,
     322,   323,   369,   338,   335,   336,   327,   342,   350,   351,
     352,   368,   348,   349,   346,   347,   333,   331,   357,   337,
     328,   366,   340,   341,   329,   332,   339,   365,   353,   354,
     360,   343,   355,   356,   363,   367,   334,   364,   330,   324,
     325,   326,   361,   362,   359,   344,   345,   358,   268,   294,
       0,   273,     0,     0,     0,     0,     0,   257,   258,   259,
     260,   261,   262,   263,   264,   265,   266,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     3,     0,   300,   300,     0,     0,
       0,     3,     0,   142,     3,   191,   190,     0,   189,     0,
     186,   322,   300,    58,     0,   300,     3,     0,     0,     0,
       0,    36,     0,     0,   322,     0,    50,    37,     0,     0,
      41,     0,   322,   319,     0,     0,   318,     0,    65,     0,
      67,     0,     0,     0,     0,     0,     0,     0,   294,   270,
     280,   281,   379,   298,   378,   284,   378,     0,     0,   290,
      51,     3,   213,    47,   214,   215,   216,   217,   218,   219,
     220,   221,   222,   223,     0,   227,   229,   236,   237,     0,
     233,   239,   240,   242,   243,     0,     0,   320,   322,     0,
     253,   293,    64,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    86,   310,     0,   301,     0,     0,     0,   145,
       0,   182,     0,     0,   188,   187,   184,   185,   294,     0,
     137,     0,     0,     0,   133,   131,   132,     0,    48,     0,
       0,   294,   294,    40,    38,     0,     0,     0,   148,     0,
       0,     0,   378,   297,   157,     0,   151,     0,     0,     0,
     274,   248,     0,   378,     0,     0,   287,   294,   294,   288,
       0,     0,   208,   234,   251,     0,   254,     0,   294,   294,
       0,     3,     0,     0,     0,     0,     0,    52,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     3,    52,
     143,    52,     0,   136,     3,     0,    52,     0,     0,     0,
      55,     0,     0,     0,    39,   376,     0,   171,   370,   371,
     146,     0,     0,     0,   157,   150,     0,     0,   249,     0,
     282,   285,     0,     0,     0,    52,   202,     0,   203,     0,
     321,     0,     0,     3,     0,     0,     0,     3,     0,    81,
       0,     0,     0,    88,     0,   302,   305,   306,   307,   308,
     309,     0,     0,   311,   313,     0,     3,   312,     0,     3,
       3,     0,     0,     0,    70,     0,     3,     0,   134,     3,
      49,    68,    69,    42,   377,     0,     0,   171,   322,     0,
       0,     0,     0,   158,   159,     0,     0,     0,     0,   299,
     289,   291,     0,     3,     0,     0,     0,     3,     0,   252,
     255,   256,     0,    52,    53,    75,     0,     3,     3,     0,
       0,    82,    88,     0,    87,    83,   304,   303,     0,     0,
       0,     0,     0,     0,     0,     0,    52,   139,   183,    52,
       0,   126,     0,     0,     0,     0,     0,   172,   173,     0,
       0,     0,     0,     0,     0,     0,     0,   157,     0,     0,
       0,     0,     0,     0,     0,     0,   205,   204,     0,     0,
     209,    52,     0,    73,     0,     0,     0,     0,    76,     3,
      84,    88,   118,    89,     0,     0,     0,    94,    96,    98,
      97,    99,    90,   100,   101,   102,   103,     0,   105,     0,
       0,    95,   108,     0,     0,     0,     0,     0,     0,     0,
       0,   117,   314,     3,    52,   317,   315,     3,    52,    52,
       0,     0,    52,    52,   372,     0,   179,     0,     0,     0,
       0,     0,   373,   374,     0,     0,     0,   157,   157,     0,
       0,     0,     0,   155,     0,   157,     0,   152,     0,     0,
      52,     0,    52,     0,    72,    74,     3,    79,    77,     0,
      85,    91,    92,    93,   104,   106,   107,   109,   110,   111,
     112,   113,   114,   115,   116,     0,     0,     0,     0,     0,
       0,   138,   123,     0,     0,     0,   300,     0,   174,   149,
       3,     0,     0,   157,     0,     0,     0,     0,     0,     0,
     160,     0,     0,     0,     0,   154,     0,     3,     0,     3,
       0,     0,    71,     0,    78,    52,   119,   316,    52,   120,
     140,   125,   124,     0,     0,     0,   180,     0,   147,     0,
       0,     0,   162,     0,     0,     0,   157,   157,     0,     3,
       0,   153,     0,   206,     0,   208,   210,    80,     0,     0,
     171,     0,     0,    52,   375,     0,   157,   161,     0,     0,
       0,     0,   163,     0,     0,   207,   212,   211,   121,   122,
       0,   171,     3,     0,     0,     0,   165,   167,     0,     0,
      52,   156,     0,     0,     0,   178,   166,     0,     0,     0,
       0,     0,     0,    52,     0,   168,   164,   170,   177,     0,
       0,   169,   176,   175
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -594,  -594,  -113,  -594,  -248,  -461,   854,  -594,    -2,   919,
    -594,  -594,   871,  -244,  -242,  -579,  -593,  -570,  -565,  -560,
    -555,  -594,  -594,  -520,  -594,  -553,  -549,  -546,  -543,  -238,
     865,   604,  -542,  -539,  -535,  -111,  -594,  -594,  -594,  -594,
    -534,  -493,  -594,  -532,  -594,  -594,   727,  -531,  -232,  -222,
    -221,  -530,  -220,  -215,  -205,  -204,  -529,  -480,   200,  -501,
      28,  -594,   774,   -80,  -594,  -226,   245,   234,   -64,   937,
     761,  -594,   692,  -594,  -594,  -594,   -54,  -359,  -594,  -278,
    -575,  -594,   529,  -594,   -71,  -184,   402,   -93
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    39,    40,   321,   164,    41,    98,   165,
     313,   144,   145,    43,    44,    45,   595,    46,    47,    48,
      49,   392,   471,   604,   672,    50,    51,    52,    53,    54,
     146,   411,    55,    56,    57,   131,    58,   567,   497,    59,
      60,   502,   574,   566,   628,   707,   132,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,   516,   518,   364,
     181,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     268,   109,   110,   251,   449,   111,   182,   183,   344,   394,
     543,   395,   325,   376,   326,   327,   496,   198
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      42,   332,   334,   199,   352,   283,   658,   191,   354,   396,
     355,   576,   544,   584,   356,   346,   588,   190,   289,   184,
     357,   126,   127,   130,   409,   667,   248,   412,    71,   196,
     358,   359,   360,   692,   668,   629,   695,   361,    99,   669,
     113,   114,   370,   596,   670,   119,   120,   362,   363,   671,
     125,   673,   136,   570,   133,   674,   633,   634,   675,   139,
     489,   676,   678,   737,   738,   679,   624,   248,   503,   680,
     681,   149,   150,   682,   686,   691,   571,   299,   429,   570,
      78,   667,   660,   624,   609,   116,   300,   613,   279,   625,
     668,     3,   316,   597,   635,   669,   519,   570,   404,   430,
     670,   570,   641,    11,    73,   671,   710,   673,   337,    72,
     572,   674,   137,   280,   675,   406,   192,   676,   678,   504,
     782,   679,   197,   626,   814,   680,   681,   130,   117,   682,
     686,   691,   472,   281,   118,   336,   572,   636,    30,   155,
     626,   740,   573,   453,   722,   637,   794,   156,   157,   158,
     159,   696,    74,   473,   572,   627,   405,   273,   572,   714,
     570,   667,   293,   123,   274,   134,   124,   275,   573,   653,
     668,   390,   627,   407,   366,   669,   570,   397,   400,   349,
     670,   403,   797,   815,   387,   671,   573,   673,   311,   312,
     573,   674,   317,   413,   675,   347,   715,   676,   678,   820,
     827,   679,   160,   377,   380,   680,   681,   373,   374,   682,
     686,   691,   307,   611,   570,   631,   398,   572,   161,   140,
     162,   290,   308,    75,   777,   778,   343,    76,   163,   388,
     790,    78,   784,   572,   612,   115,   632,   835,   450,   716,
     249,   510,   141,   290,   121,   291,   342,   717,   273,   573,
     353,   444,   250,   445,    11,   274,   431,   142,   275,   122,
     570,   432,   435,   437,   570,   573,   352,   432,   432,   352,
     354,   572,   355,   354,   438,   355,   356,   143,   439,   356,
     810,    42,   357,   858,   442,   357,   130,   859,   130,    30,
     151,   152,   358,   359,   360,   358,   359,   360,   850,   361,
     302,   375,   361,   573,   135,   382,   383,   384,   385,   362,
     363,   477,   362,   363,   147,   781,   290,   572,   624,   863,
     432,   572,   128,   840,   841,   813,   295,   767,   831,   768,
     432,   418,   140,   432,   419,   570,   153,   454,   624,   501,
     455,   862,   845,   855,   846,    11,   423,   424,   524,   573,
     509,   296,   154,   573,   482,   141,   664,   433,   867,   148,
     665,   872,   666,   269,   270,   551,   677,   491,   492,   166,
     142,   555,   683,   167,   469,   626,   297,   470,   511,   188,
      30,   342,   684,   685,   687,   298,   520,   168,    42,   688,
     143,   169,   572,   512,   513,   626,   155,   170,    42,   689,
     690,    42,   171,   129,   521,   522,   172,   627,   255,   256,
     592,    42,   664,   173,   599,   464,   665,   466,   666,   468,
     560,   180,   677,   329,   573,   185,   589,   627,   683,   329,
     480,   271,   272,   610,   255,   256,   614,   615,   684,   685,
     687,   545,   546,   620,   187,   688,   622,   174,    42,   456,
     457,   535,   721,   536,   494,   689,   690,   498,   499,   257,
     258,   259,   260,   261,   262,   263,   264,   265,   266,   175,
     645,   548,   549,   176,   649,   705,   706,   162,   537,   538,
     539,   540,   177,   352,   656,   657,   178,   354,   805,   355,
     525,   526,   664,   356,   179,   186,   665,   532,   666,   357,
     367,   368,   677,   371,   372,   189,   194,   195,   683,   358,
     359,   360,   353,   252,   253,   353,   361,   254,   684,   685,
     687,   276,    42,   564,   277,   688,   362,   363,   278,   541,
     542,   282,   776,   284,   285,   689,   690,   579,   156,   157,
     158,   159,   286,   287,   288,   294,   739,   292,   304,    42,
     305,   309,   320,    42,   306,   328,   600,   329,    77,    78,
      79,   330,    80,    81,    82,   257,   258,   259,   260,   261,
     262,   263,   264,   265,   266,   335,   338,   381,   339,   340,
     755,   804,    11,   386,   758,    83,    84,    85,    86,   391,
      42,    87,   654,   318,    89,    90,   393,    42,   401,   402,
     408,   410,    42,   414,   415,   417,   416,   428,    42,   420,
     644,   421,    42,    42,   648,    91,   422,    30,    42,    92,
      42,   425,   426,   793,   655,   427,   434,   436,   440,   443,
      96,   446,   452,   462,   432,   447,   451,    93,   448,    72,
      94,   458,    95,    42,    96,   459,   465,    42,   460,   474,
     461,   704,    97,   463,    42,    42,   475,   807,    42,   712,
     713,    77,    78,    79,   476,    80,    81,    82,   478,   483,
     484,   485,   490,   487,   822,   493,   824,   495,   506,   500,
     505,   507,   508,   523,   350,    11,   517,     5,    83,    84,
      85,    86,   527,   528,    87,    88,   530,    89,    90,   529,
     531,   533,   582,   534,   547,   550,   843,    11,   552,    12,
     553,   554,   556,   557,   558,   559,   561,    13,    91,   562,
      30,   563,    92,   565,   575,   577,   580,   578,   585,   353,
      19,    20,   598,    22,    23,    24,   581,    42,    42,   864,
      93,    29,    30,    94,    31,    95,   341,    96,    35,    36,
     590,   342,   591,    42,   603,    97,    42,   606,   607,   608,
      77,    78,    79,   601,    80,    81,    82,   602,   630,   638,
     639,   605,   617,   583,    77,    78,    79,   618,    80,    81,
      82,   640,   621,   623,    11,   642,   643,    83,    84,    85,
      86,    42,   646,    87,    88,   647,    89,    90,    11,   112,
     650,    83,    84,    85,    86,    42,   659,    87,    88,   652,
      89,    90,   661,   701,   693,   697,   700,    91,   708,    30,
      42,    92,    42,   709,   718,   719,   723,   725,   726,   724,
     727,    91,   728,    30,   729,    92,   731,   834,   733,    93,
     734,    42,    94,   735,    95,   736,    96,   756,   759,   741,
     760,   742,   743,    93,    97,   744,    94,   745,    95,   757,
      96,   765,    42,   746,   342,    77,    78,    79,    97,    80,
      81,    82,   747,   748,   763,   749,    77,    78,    79,   750,
      80,    81,    82,   751,   752,   753,   754,   764,   766,    11,
     761,   762,    83,    84,    85,    86,   771,   769,    87,    88,
      11,    89,    90,    83,    84,    85,    86,   774,   770,    87,
      88,   772,    89,    90,   779,   783,   773,   786,   788,   780,
     785,   787,    91,   791,    30,   792,    92,   796,   799,   800,
     801,   802,   806,    91,   808,    30,   809,    92,   811,   812,
     816,   817,   818,   819,    93,   821,   823,    94,   825,    95,
     826,    96,   828,   830,   829,    93,   832,   836,    94,    97,
      95,   837,    96,    77,    78,    79,   838,    80,    81,    82,
     138,   839,   842,   844,    77,    78,    79,   848,    80,   849,
      82,   851,   852,   853,   854,   868,   856,    11,   857,   869,
      83,    84,    85,    86,   870,   871,   874,   861,    11,    89,
      90,    83,    84,    85,    86,   865,   879,   866,   875,   876,
     877,   878,   880,   881,   882,   303,   883,   399,   310,   319,
      91,   488,    30,   267,    92,   847,   193,   365,   369,   441,
     569,    91,   711,    30,     0,    92,     0,     0,     0,     0,
       0,     0,    93,     0,     0,    94,     0,    95,     0,    96,
       0,   322,   323,     0,     0,     0,    94,    97,    95,     0,
      96,   201,   202,     0,     0,     0,     0,     0,    97,   203,
       0,   204,   205,     0,   206,     0,   207,   208,   209,   210,
     211,   212,   213,   214,   215,   216,   217,   218,   219,   220,
     221,   222,   223,   224,   225,   226,   227,   228,   229,   230,
     231,   232,   233,   234,   235,     0,   236,   237,   238,   239,
     240,   241,   242,   243,   244,   245,   246,   247,   568,   323,
       0,     0,     0,     0,     0,     0,     0,     0,   201,   202,
       0,     0,     0,     0,     0,     0,   203,   345,   204,   205,
     342,   206,     0,   207,   208,   209,   210,   211,   212,   213,
     214,   215,   216,   217,   218,   219,   220,   221,   222,   223,
     224,   225,   226,   227,   228,   229,   230,   231,   232,   233,
     234,   235,     0,   236,   237,   238,   239,   240,   241,   242,
     243,   244,   245,   246,   247,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,   342,    11,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,    14,
       0,     0,    15,     0,     0,    16,   389,     0,     0,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,     0,    29,    30,     0,    31,    32,    33,    34,    35,
      36,    37,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     0,     0,     9,     0,     0,     0,     0,     0,     0,
      10,     0,     0,   514,    38,   515,     0,    12,     0,     0,
       0,     0,     0,     0,     0,    13,    14,     0,     0,    15,
       0,     0,    16,     0,     0,     0,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,     0,    29,
      30,     0,    31,    32,    33,    34,    35,    36,    37,     4,
       0,     0,     5,     0,     0,     6,     7,     8,     0,     0,
       9,     0,     0,     0,     0,     0,     0,    10,     0,     0,
       0,    38,    11,     0,    12,     0,     0,     0,     0,     0,
       0,     0,    13,    14,     0,     0,    15,     0,     0,    16,
       0,     0,     0,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,    28,     0,    29,    30,     0,    31,
      32,    33,    34,    35,    36,    37,     4,     0,     0,     5,
       0,     0,     6,     7,     8,     0,     0,     9,     0,     0,
       0,     0,     0,     0,    10,     0,     0,     0,    38,   467,
       0,    12,     0,     0,     0,     0,     0,     0,     0,    13,
      14,     0,     0,    15,     0,     0,    16,     0,     0,     0,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,     0,    29,    30,     0,    31,    32,    33,    34,
      35,    36,    37,     4,     0,     0,     5,     0,     0,     6,
       7,     8,     0,     0,     9,     0,     0,     0,     0,     0,
       0,    10,     0,     0,     0,    38,   479,     0,    12,     0,
       0,     0,     0,     0,     0,     0,    13,    14,     0,     0,
      15,     0,     0,    16,     0,     0,     0,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,     0,
      29,    30,     0,    31,    32,    33,    34,    35,    36,    37,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     0,
       0,     9,     0,     0,     0,     0,     0,     0,    10,     0,
       0,     0,    38,   481,     0,    12,     0,     0,     0,     0,
       0,     0,     0,    13,    14,     0,     0,    15,     0,     0,
      16,     0,     0,     0,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,     0,    29,    30,     0,
      31,    32,    33,    34,    35,    36,    37,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    38,
     486,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,    14,     0,     0,    15,     0,     0,    16,     0,     0,
       0,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,     0,    29,    30,     0,    31,    32,    33,
      34,    35,    36,    37,     4,     0,     0,     5,     0,     0,
       6,     7,     8,     0,     0,     9,     0,     0,     0,     0,
       0,     0,    10,     0,     0,     0,    38,   593,     0,    12,
       0,     0,     0,     0,     0,     0,     0,    13,    14,     0,
       0,    15,     0,     0,    16,     0,     0,     0,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
       0,    29,   594,     0,    31,    32,    33,    34,    35,    36,
      37,     4,     0,     0,     5,     0,     0,     6,     7,     8,
       0,     0,     9,     0,     0,     0,     0,     0,     0,    10,
       0,     0,     0,    38,   616,     0,    12,     0,     0,     0,
       0,     0,     0,     0,    13,    14,     0,     0,    15,     0,
       0,    16,     0,     0,     0,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,     0,    29,    30,
       0,    31,    32,    33,    34,    35,    36,    37,     4,     0,
       0,     5,     0,     0,     6,     7,     8,     0,     0,     9,
       0,     0,     0,     0,     0,     0,    10,     0,     0,     0,
      38,   619,     0,    12,     0,     0,     0,     0,     0,     0,
       0,    13,    14,     0,     0,    15,     0,     0,    16,     0,
       0,     0,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,     0,    29,    30,     0,    31,    32,
      33,    34,    35,    36,    37,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    38,   651,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,    14,
       0,     0,    15,     0,     0,    16,     0,     0,     0,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,     0,    29,    30,     0,    31,    32,    33,    34,    35,
      36,    37,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     0,     0,     9,     0,     0,     0,     0,     0,     0,
      10,     0,     0,     0,    38,   694,     0,    12,     0,     0,
       0,     0,     0,     0,     0,    13,    14,     0,     0,    15,
       0,     0,    16,     0,     0,     0,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,     0,    29,
      30,     0,    31,    32,    33,    34,    35,    36,    37,     4,
       0,     0,     5,     0,     0,     6,     7,     8,     0,     0,
       9,     0,     0,     0,     0,     0,     0,    10,     0,     0,
       0,    38,   698,     0,    12,     0,     0,     0,     0,     0,
       0,     0,    13,    14,     0,     0,    15,     0,     0,    16,
       0,     0,     0,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,    28,     0,    29,    30,     0,    31,
      32,    33,    34,    35,    36,    37,     4,     0,     0,     5,
       0,     0,     6,     7,     8,     0,     0,     9,     0,     0,
       0,     0,     0,     0,    10,     0,     0,     0,    38,   699,
       0,    12,     0,     0,     0,     0,     0,     0,     0,    13,
      14,     0,     0,    15,     0,     0,    16,     0,     0,     0,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,     0,    29,    30,     0,    31,    32,    33,    34,
      35,    36,    37,     4,     0,     0,     5,     0,     0,     6,
       7,     8,     0,     0,     9,     0,     0,     0,     0,     0,
       0,    10,     0,     0,     0,    38,   702,     0,    12,     0,
       0,     0,     0,     0,     0,     0,    13,    14,     0,     0,
      15,     0,     0,    16,     0,     0,     0,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,     0,
      29,    30,     0,    31,    32,    33,    34,    35,    36,    37,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     0,
       0,     9,     0,     0,     0,     0,     0,     0,    10,     0,
       0,     0,    38,   703,     0,    12,     0,     0,     0,     0,
       0,     0,     0,    13,    14,     0,     0,    15,     0,     0,
      16,     0,     0,     0,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,     0,    29,    30,     0,
      31,    32,    33,    34,    35,    36,    37,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    38,
     730,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,    14,     0,     0,    15,     0,     0,    16,     0,     0,
       0,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,     0,    29,    30,     0,    31,    32,    33,
      34,    35,    36,    37,     4,     0,     0,     5,     0,     0,
       6,     7,     8,     0,     0,     9,     0,     0,     0,     0,
       0,     0,    10,     0,     0,     0,    38,   732,     0,    12,
       0,     0,     0,     0,     0,     0,     0,    13,    14,     0,
       0,    15,     0,     0,    16,     0,     0,     0,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
       0,    29,    30,     0,    31,    32,    33,    34,    35,    36,
      37,     4,     0,     0,     5,     0,     0,     6,     7,     8,
       0,     0,     9,     0,     0,     0,     0,     0,     0,    10,
       0,     0,     0,    38,   795,     0,    12,     0,     0,     0,
       0,     0,     0,     0,    13,    14,     0,     0,    15,     0,
       0,    16,     0,     0,     0,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,     0,    29,    30,
       0,    31,    32,    33,    34,    35,    36,    37,     4,     0,
       0,     5,     0,     0,     6,     7,     8,     0,     0,     9,
       0,     0,     0,     0,     0,     0,    10,     0,     0,     0,
      38,   798,     0,    12,     0,     0,     0,     0,     0,     0,
       0,    13,    14,     0,     0,    15,     0,     0,    16,     0,
       0,     0,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,     0,    29,    30,     0,    31,    32,
      33,    34,    35,    36,    37,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    38,   833,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,    14,
       0,     0,    15,     0,     0,    16,     0,     0,     0,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,     0,    29,    30,     0,    31,    32,    33,    34,    35,
      36,    37,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     0,     0,     9,     0,     0,     0,     0,     0,     0,
      10,     0,     0,     0,    38,   860,     0,    12,     0,     0,
       0,     0,     0,     0,     0,    13,    14,     0,     0,    15,
       0,     0,    16,     0,     0,     0,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,     0,    29,
      30,     0,    31,    32,    33,    34,    35,    36,    37,     4,
       0,     0,     5,     0,     0,     6,     7,     8,     0,     0,
       9,     0,     0,     0,     0,     0,     0,    10,     0,     0,
       0,    38,   873,     0,    12,     0,     0,     0,     0,     0,
       0,     0,    13,    14,     0,     0,    15,     0,     0,    16,
       0,     0,     0,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,    28,     0,    29,    30,     0,    31,
      32,    33,    34,    35,    36,    37,   350,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,   662,     0,     0,
       0,     0,     0,     0,    10,     0,     0,     0,    38,    11,
       0,    12,     0,     0,     0,     0,     0,     0,     0,    13,
      14,     0,     0,    15,     0,     0,    16,     0,     0,     0,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,     0,    29,    30,     0,    31,    32,    33,    34,
      35,    36,    37,   350,     0,     0,     5,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   586,     0,     0,     0,   663,    11,     0,    12,     0,
       0,     0,     0,     0,     0,     0,    13,     0,     0,   350,
       0,     0,     5,     0,     0,     0,     0,     0,     0,    19,
      20,     0,    22,    23,    24,     0,     0,     0,     0,     0,
      29,    30,    11,    31,    12,     0,     0,    35,    36,     0,
       0,     0,    13,     0,     0,   350,     0,     0,     5,     0,
       0,     0,     0,     0,     0,    19,    20,     0,    22,    23,
      24,     0,   587,     0,     0,     0,    29,    30,    11,    31,
      12,     0,     0,    35,    36,     0,     0,     0,    13,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    19,    20,     0,    22,    23,    24,     0,   351,     0,
       0,     0,    29,    30,     0,    31,     0,     0,     0,    35,
      36,     0,   322,   323,     0,     0,     0,     0,     0,     0,
       0,     0,   201,   202,     0,     0,     0,     0,     0,     0,
     203,     0,   204,   205,   789,   206,     0,   207,   208,   209,
     210,   211,   212,   213,   214,   215,   216,   217,   218,   219,
     220,   221,   222,   223,   224,   225,   226,   227,   228,   229,
     230,   231,   232,   233,   234,   235,     0,   236,   237,   238,
     239,   240,   241,   242,   243,   244,   245,   246,   247,   322,
     323,     0,     0,     0,     0,     0,     0,     0,     0,   201,
     202,     0,     0,     0,   324,     0,     0,   203,     0,   204,
     205,     0,   206,     0,   207,   208,   209,   210,   211,   212,
     213,   214,   215,   216,   217,   218,   219,   220,   221,   222,
     223,   224,   225,   226,   227,   228,   229,   230,   231,   232,
     233,   234,   235,     0,   236,   237,   238,   239,   240,   241,
     242,   243,   244,   245,   246,   247,   322,   323,     0,     0,
       0,     0,     0,     0,     0,     0,   201,   202,     0,     0,
       0,   331,     0,     0,   203,     0,   204,   205,     0,   206,
       0,   207,   208,   209,   210,   211,   212,   213,   214,   215,
     216,   217,   218,   219,   220,   221,   222,   223,   224,   225,
     226,   227,   228,   229,   230,   231,   232,   233,   234,   235,
       0,   236,   237,   238,   239,   240,   241,   242,   243,   244,
     245,   246,   247,   322,   323,     0,     0,     0,     0,     0,
       0,     0,     0,   201,   202,     0,     0,     0,   333,     0,
       0,   203,     0,   204,   205,     0,   206,     0,   207,   208,
     209,   210,   211,   212,   213,   214,   215,   216,   217,   218,
     219,   220,   221,   222,   223,   224,   225,   226,   227,   228,
     229,   230,   231,   232,   233,   234,   235,     0,   236,   237,
     238,   239,   240,   241,   242,   243,   244,   245,   246,   247,
     322,   323,     0,     0,     0,     0,     0,     0,     0,     0,
     201,   202,     0,     0,     0,   720,     0,     0,   203,     0,
     204,   205,     0,   206,     0,   207,   208,   209,   210,   211,
     212,   213,   214,   215,   216,   217,   218,   219,   220,   221,
     222,   223,   224,   225,   226,   227,   228,   229,   230,   231,
     232,   233,   234,   235,     0,   236,   237,   238,   239,   240,
     241,   242,   243,   244,   245,   246,   247,   322,   323,     0,
       0,     0,     0,     0,     0,     0,     0,   201,   202,     0,
       0,     0,   775,     0,     0,   203,     0,   204,   205,     0,
     206,     0,   207,   208,   209,   210,   211,   212,   213,   214,
     215,   216,   217,   218,   219,   220,   221,   222,   223,   224,
     225,   226,   227,   228,   229,   230,   231,   232,   233,   234,
     235,     0,   236,   237,   238,   239,   240,   241,   242,   243,
     244,   245,   246,   247,   314,     0,     0,   315,     0,     0,
       0,     0,     0,     0,   201,   202,     0,     0,     0,   803,
       0,     0,   203,     0,   204,   205,     0,   206,     0,   207,
     208,   209,   210,   211,   212,   213,   214,   215,   216,   217,
     218,   219,   220,   221,   222,   223,   224,   225,   226,   227,
     228,   229,   230,   231,   232,   233,   234,   235,     0,   236,
     237,   238,   239,   240,   241,   242,   243,   244,   245,   246,
     247,   200,     0,     0,   348,     0,     0,     0,     0,     0,
       0,   201,   202,     0,     0,     0,     0,     0,     0,   203,
       0,   204,   205,     0,   206,     0,   207,   208,   209,   210,
     211,   212,   213,   214,   215,   216,   217,   218,   219,   220,
     221,   222,   223,   224,   225,   226,   227,   228,   229,   230,
     231,   232,   233,   234,   235,     0,   236,   237,   238,   239,
     240,   241,   242,   243,   244,   245,   246,   247,   378,     0,
       0,   379,     0,     0,     0,     0,     0,     0,   201,   202,
       0,     0,     0,     0,     0,     0,   203,     0,   204,   205,
       0,   206,     0,   207,   208,   209,   210,   211,   212,   213,
     214,   215,   216,   217,   218,   219,   220,   221,   222,   223,
     224,   225,   226,   227,   228,   229,   230,   231,   232,   233,
     234,   235,     0,   236,   237,   238,   239,   240,   241,   242,
     243,   244,   245,   246,   247,   200,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   201,   202,     0,     0,     0,
       0,     0,     0,   203,     0,   204,   205,     0,   206,     0,
     207,   208,   209,   210,   211,   212,   213,   214,   215,   216,
     217,   218,   219,   220,   221,   222,   223,   224,   225,   226,
     227,   228,   229,   230,   231,   232,   233,   234,   235,     0,
     236,   237,   238,   239,   240,   241,   242,   243,   244,   245,
     246,   247,   301,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   201,   202,     0,     0,     0,     0,     0,     0,
     203,     0,   204,   205,     0,   206,     0,   207,   208,   209,
     210,   211,   212,   213,   214,   215,   216,   217,   218,   219,
     220,   221,   222,   223,   224,   225,   226,   227,   228,   229,
     230,   231,   232,   233,   234,   235,     0,   236,   237,   238,
     239,   240,   241,   242,   243,   244,   245,   246,   247
};

static const yytype_int16 yycheck[] =
{
       2,   185,   186,    96,   252,   118,   599,    87,   252,   287,
     252,   504,   473,   514,   252,   199,   517,    81,   129,    73,
     252,    23,    24,    25,   302,   604,    97,   305,     4,    93,
     252,   252,   252,   608,   604,   567,   611,   252,    10,   604,
      12,    13,   268,    43,   604,    17,    18,   252,   252,   604,
      22,   604,     4,     4,    26,   604,     4,     5,   604,    31,
     419,   604,   604,   656,   657,   604,     4,   138,    42,   604,
     604,     4,     5,   604,   604,   604,    27,    54,    71,     4,
       4,   660,   602,     4,   545,     4,    63,   548,    46,    27,
     660,     0,   163,    93,    42,   660,   455,     4,     4,    92,
     660,     4,    27,    27,    85,   660,    27,   660,    38,    85,
      61,   660,    64,    71,   660,     4,    88,   660,   660,    93,
      27,   660,    94,    61,    27,   660,   660,   129,    47,   660,
     660,   660,    71,    91,    93,   189,    61,    85,    62,     8,
      61,   661,    93,   369,   637,    93,   739,    16,    17,    18,
      19,   612,     4,    92,    61,    93,    62,    87,    61,     5,
       4,   740,   134,     4,    94,    45,     7,    97,    93,    93,
     740,   284,    93,    62,   254,   740,     4,   288,   291,   250,
     740,   294,   757,    27,    46,   740,    93,   740,   160,   161,
      93,   740,   164,   306,   740,   249,    42,   740,   740,    27,
     793,   740,    71,   274,   275,   740,   740,   271,   272,   740,
     740,   740,     4,    71,     4,    71,    86,    61,    87,     4,
      89,    91,    14,     4,   717,   718,   198,     4,    97,    91,
     731,     4,   725,    61,    92,     4,    92,    27,   351,    85,
      85,    88,    27,    91,     4,    93,    93,    93,    87,    93,
     252,   344,    97,   346,    27,    94,    86,    42,    97,     4,
       4,    91,    86,    86,     4,    93,   514,    91,    91,   517,
     514,    61,   514,   517,    93,   517,   514,    62,    97,   517,
     773,   283,   514,    27,   338,   517,   288,    27,   290,    62,
       4,     5,   514,   514,   514,   517,   517,   517,   830,   514,
      85,   273,   517,    93,    60,   277,   278,   279,   280,   514,
     514,    86,   517,   517,     4,    86,    91,    61,     4,   851,
      91,    61,     4,   816,   817,    86,     4,    91,    86,    93,
      91,    88,     4,    91,    91,     4,    65,    88,     4,   432,
      91,    27,   822,   836,   824,    27,   318,   319,   461,    93,
     443,    29,    93,    93,   408,    27,   604,   329,    27,     4,
     604,    27,   604,    81,    82,   478,   604,   421,   422,    92,
      42,   484,   604,    93,    23,    61,    54,    26,    90,     4,
      62,    93,   604,   604,   604,    63,   457,    93,   390,   604,
      62,    93,    61,   447,   448,    61,     8,    93,   400,   604,
     604,   403,    93,    85,   458,   459,    93,    93,    20,    21,
     523,   413,   660,    93,   527,   387,   660,   389,   660,   391,
      88,    85,   660,    91,    93,    85,    88,    93,   660,    91,
     402,    83,    84,   546,    20,    21,   549,   550,   660,   660,
     660,    92,    93,   556,    61,   660,   559,    93,   450,    90,
      91,     3,   636,     5,   426,   660,   660,   429,   430,    71,
      72,    73,    74,    75,    76,    77,    78,    79,    80,    93,
     583,    92,    93,    93,   587,     4,     5,    89,    30,    31,
      32,    33,    93,   731,   597,   598,    93,   731,   766,   731,
     462,   463,   740,   731,    93,    85,   740,   469,   740,   731,
     255,   256,   740,   269,   270,    85,     4,    85,   740,   731,
     731,   731,   514,    24,    35,   517,   731,    34,   740,   740,
     740,     3,   524,   495,    64,   740,   731,   731,    85,    81,
      82,     4,   716,    93,    93,   740,   740,   509,    16,    17,
      18,    19,    85,    85,    85,    93,   659,    97,    41,   551,
      85,     4,     4,   555,    93,    86,   528,    91,     3,     4,
       5,    86,     7,     8,     9,    71,    72,    73,    74,    75,
      76,    77,    78,    79,    80,     4,    85,     4,    86,    86,
     693,   765,    27,     4,   697,    30,    31,    32,    33,    23,
     592,    36,   594,    71,    39,    40,     4,   599,     4,    41,
      85,     4,   604,     5,     4,    41,     5,    93,   610,     6,
     582,    85,   614,   615,   586,    60,    85,    62,   620,    64,
     622,    85,    71,   736,   596,    85,    93,    93,    86,    91,
      89,    86,    93,    42,    91,    85,    92,    82,    85,    85,
      85,    85,    87,   645,    89,    85,     4,   649,    86,    86,
      93,   623,    97,    46,   656,   657,    91,   770,   660,   631,
     632,     3,     4,     5,    86,     7,     8,     9,    93,    86,
      93,    86,    90,    14,   787,     4,   789,     4,    13,    93,
      93,     4,    86,    93,     4,    27,    25,     7,    30,    31,
      32,    33,    93,    46,    36,    37,    48,    39,    40,    93,
      24,    93,    22,    28,     4,    93,   819,    27,    56,    29,
      59,    86,    93,    67,     4,    93,    86,    37,    60,    86,
      62,    86,    64,    86,     4,    27,    86,    93,    22,   731,
      50,    51,    93,    53,    54,    55,    86,   739,   740,   852,
      82,    61,    62,    85,    64,    87,    88,    89,    68,    69,
      86,    93,    86,   755,    24,    97,   758,     3,     3,    71,
       3,     4,     5,    93,     7,     8,     9,    93,    85,     4,
       4,    93,    93,    93,     3,     4,     5,    93,     7,     8,
       9,     4,    93,    92,    27,     4,    13,    30,    31,    32,
      33,   793,    93,    36,    37,    93,    39,    40,    27,    42,
      93,    30,    31,    32,    33,   807,    93,    36,    37,    41,
      39,    40,    93,    65,    93,    93,    56,    60,     4,    62,
     822,    64,   824,     4,    93,     4,    93,    93,     4,    97,
      93,    60,    27,    62,    24,    64,    24,   809,    38,    82,
      93,   843,    85,    93,    87,    93,    89,    52,    52,    93,
      56,    93,    93,    82,    97,    93,    85,    93,    87,    71,
      89,    85,   864,    93,    93,     3,     4,     5,    97,     7,
       8,     9,    93,    93,    66,    93,     3,     4,     5,    93,
       7,     8,     9,    93,    93,    93,    93,    65,    85,    27,
      93,    93,    30,    31,    32,    33,     4,    93,    36,    37,
      27,    39,    40,    30,    31,    32,    33,     4,    93,    36,
      37,    86,    39,    40,     4,     4,    93,     4,    22,    93,
      93,    93,    60,    22,    62,    93,    64,    93,    93,    93,
      93,    93,     5,    60,    93,    62,    92,    64,     4,    93,
      93,    93,     4,    93,    82,    93,    93,    85,    93,    87,
      93,    89,    52,    93,    52,    82,    86,    93,    85,    97,
      87,    93,    89,     3,     4,     5,     4,     7,     8,     9,
      97,     4,    93,     4,     3,     4,     5,    93,     7,    93,
       9,    93,    93,    61,     4,     4,    93,    27,    93,     4,
      30,    31,    32,    33,    61,     4,     4,    93,    27,    39,
      40,    30,    31,    32,    33,    93,     4,    93,    93,    93,
      93,    93,     4,    93,    93,   144,    93,   290,   153,   165,
      60,   417,    62,   104,    64,   825,    89,   253,   267,   337,
     501,    60,   630,    62,    -1,    64,    -1,    -1,    -1,    -1,
      -1,    -1,    82,    -1,    -1,    85,    -1,    87,    -1,    89,
      -1,     4,     5,    -1,    -1,    -1,    85,    97,    87,    -1,
      89,    14,    15,    -1,    -1,    -1,    -1,    -1,    97,    22,
      -1,    24,    25,    -1,    27,    -1,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    -1,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,     4,     5,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    14,    15,
      -1,    -1,    -1,    -1,    -1,    -1,    22,    90,    24,    25,
      93,    27,    -1,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    -1,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69,    70,     4,    -1,    -1,     7,    -1,
      -1,    10,    11,    12,    -1,    -1,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    -1,    -1,    93,    27,    -1,
      29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    38,
      -1,    -1,    41,    -1,    -1,    44,    45,    -1,    -1,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    -1,    61,    62,    -1,    64,    65,    66,    67,    68,
      69,    70,     4,    -1,    -1,     7,    -1,    -1,    10,    11,
      12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,
      22,    -1,    -1,    25,    93,    27,    -1,    29,    -1,    -1,
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
      -1,    -1,     7,    -1,    -1,    10,    11,    12,    -1,    -1,
      15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,
      -1,    93,    27,    -1,    29,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    37,    38,    -1,    -1,    41,    -1,    -1,    44,
      -1,    -1,    -1,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    -1,    61,    62,    -1,    64,
      65,    66,    67,    68,    69,    70,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    15,    -1,    -1,
      -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,    93,    27,
      -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,
      38,    -1,    -1,    41,    -1,    -1,    44,    -1,    -1,    -1,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    -1,    61,    62,    -1,    64,    65,    66,    67,
      68,    69,    70,     4,    -1,    -1,     7,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    22,    -1,    -1,    -1,    93,    27,    -1,    29,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    37,    -1,    -1,     4,
      -1,    -1,     7,    -1,    -1,    -1,    -1,    -1,    -1,    50,
      51,    -1,    53,    54,    55,    -1,    -1,    -1,    -1,    -1,
      61,    62,    27,    64,    29,    -1,    -1,    68,    69,    -1,
      -1,    -1,    37,    -1,    -1,     4,    -1,    -1,     7,    -1,
      -1,    -1,    -1,    -1,    -1,    50,    51,    -1,    53,    54,
      55,    -1,    93,    -1,    -1,    -1,    61,    62,    27,    64,
      29,    -1,    -1,    68,    69,    -1,    -1,    -1,    37,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    50,    51,    -1,    53,    54,    55,    -1,    93,    -1,
      -1,    -1,    61,    62,    -1,    64,    -1,    -1,    -1,    68,
      69,    -1,     4,     5,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    14,    15,    -1,    -1,    -1,    -1,    -1,    -1,
      22,    -1,    24,    25,    93,    27,    -1,    29,    30,    31,
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
      64,    65,    66,    67,    68,    69,    70,     4,     5,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    14,    15,    -1,
      -1,    -1,    86,    -1,    -1,    22,    -1,    24,    25,    -1,
      27,    -1,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    -1,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,    70,     4,    -1,    -1,     7,    -1,    -1,
      -1,    -1,    -1,    -1,    14,    15,    -1,    -1,    -1,    86,
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
      -1,     7,    -1,    -1,    -1,    -1,    -1,    -1,    14,    15,
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
      69,    70,     4,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    14,    15,    -1,    -1,    -1,    -1,    -1,    -1,
      22,    -1,    24,    25,    -1,    27,    -1,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    -1,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    70
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
     159,   160,   161,   162,   163,   164,   165,   166,   167,   169,
     170,   173,    42,   158,   158,     4,     4,    47,    93,   158,
     158,     4,     4,     4,     7,   158,   106,   106,     4,    85,
     106,   133,   144,   158,    45,    60,     4,    64,    97,   158,
       4,    27,    42,    62,   109,   110,   128,     4,     4,     4,
       5,     4,     5,    65,    93,     8,    16,    17,    18,    19,
      71,    87,    89,    97,   104,   107,    92,    93,    93,    93,
      93,    93,    93,    93,    93,    93,    93,    93,    93,    93,
      85,   158,   174,   175,   174,    85,    85,    61,     4,    85,
     166,   161,   158,   167,     4,    85,   166,   158,   185,   185,
       4,    14,    15,    22,    24,    25,    27,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,   182,    85,
      97,   171,    24,    35,    34,    20,    21,    71,    72,    73,
      74,    75,    76,    77,    78,    79,    80,   107,   168,    81,
      82,    83,    84,    87,    94,    97,     3,    64,    85,    46,
      71,    91,     4,   100,    93,    93,    85,    85,    85,   133,
      91,    93,    97,   158,    93,     4,    29,    54,    63,    54,
      63,     4,    85,   110,    41,    85,    93,     4,    14,     4,
     128,   158,   158,   108,     4,     7,   182,   158,    71,   104,
       4,   103,     4,     5,    86,   180,   182,   183,    86,    91,
      86,    86,   183,    86,   183,     4,   174,    38,    85,    86,
      86,    88,    93,   158,   176,    90,   183,   174,     7,   182,
       4,    93,   102,   106,   111,   112,   127,   146,   147,   148,
     150,   151,   152,   153,   157,   160,   161,   164,   164,   168,
     163,   165,   165,   166,   166,   158,   181,   182,     4,     7,
     182,     4,   158,   158,   158,   158,     4,    46,    91,    45,
     100,    23,   119,     4,   177,   179,   177,   133,    86,   144,
     100,     4,    41,   100,     4,    62,     4,    62,    85,   177,
       4,   129,   177,   100,     5,     4,     5,    41,    88,    91,
       6,    85,    85,   158,   158,    85,    71,    85,    93,    71,
      92,    86,    91,   158,    93,    86,    93,    86,    93,    97,
      86,   170,   174,    91,   185,   185,    86,    85,    85,   172,
     100,    92,    93,   163,    88,    91,    90,    91,    85,    85,
      86,    93,    42,    46,   158,     4,   158,    27,   158,    23,
      26,   120,    71,    92,    86,    91,    86,    86,    93,    27,
     158,    27,   174,    86,    93,    86,    27,    14,   129,   175,
      90,   174,   174,     4,   158,     4,   184,   136,   158,   158,
      93,   185,   139,    42,    93,    93,    13,     4,    86,   185,
      88,    90,   174,   174,    25,    27,   155,    25,   156,   175,
     182,   174,   174,    93,   100,   158,   158,    93,    46,    93,
      48,    24,   158,    93,    28,     3,     5,    30,    31,    32,
      33,    81,    82,   178,   103,    92,    93,     4,    92,    93,
      93,   100,    56,    59,    86,   100,    93,    67,     4,    93,
      88,    86,    86,    86,   158,    86,   141,   135,     4,   180,
       4,    27,    61,    93,   140,     4,   139,    27,    93,   158,
      86,    86,    22,    93,   157,    22,    22,    93,   157,    88,
      86,    86,   100,    27,    62,   114,    43,    93,    93,   100,
     158,    93,    93,    24,   121,    93,     3,     3,    71,   103,
     100,    71,    92,   103,   100,   100,    27,    93,    93,    27,
     100,    93,   100,    92,     4,    27,    61,    93,   142,   141,
      85,    71,    92,     4,     5,    42,    85,    93,     4,     4,
       4,    27,     4,    13,   158,   100,    93,    93,   158,   100,
      93,    27,    41,    93,   106,   158,   100,   100,   114,    93,
     121,    93,    15,    93,   102,   111,   112,   113,   115,   116,
     117,   118,   122,   123,   124,   125,   126,   127,   130,   131,
     132,   138,   145,   146,   147,   148,   149,   150,   151,   152,
     153,   154,   178,    93,    27,   178,   103,    93,    27,    27,
      56,    65,    27,    27,   158,     4,     5,   143,     4,     4,
      27,   184,   158,   158,     5,    42,    85,    93,    93,     4,
      86,   183,   139,    93,    97,    93,     4,    93,    27,    24,
      27,    24,    27,    38,    93,    93,    93,   114,   114,   100,
     121,    93,    93,    93,    93,    93,    93,    93,    93,    93,
      93,    93,    93,    93,    93,   100,    52,    71,   100,    52,
      56,    93,    93,    66,    65,    85,    85,    91,    93,    93,
      93,     4,    86,    93,     4,    86,   183,   139,   139,     4,
      93,    86,    27,     4,   139,    93,     4,    93,    22,    93,
     157,    22,    93,   100,   114,    27,    93,   178,    27,    93,
      93,    93,    93,    86,   183,   177,     5,   100,    93,    92,
     139,     4,    93,    86,    27,    27,    93,    93,     4,    93,
      27,    93,   100,    93,   100,    93,    93,   114,    52,    52,
      93,    86,    86,    27,   158,    27,    93,    93,     4,     4,
     139,   139,    93,   100,     4,   155,   155,   156,    93,    93,
     141,    93,    93,    61,     4,   139,    93,    93,    27,    27,
      27,    93,    27,   141,   100,    93,    93,    27,     4,     4,
      61,     4,    27,    27,     4,    93,    93,    93,    93,     4,
       4,    93,    93,    93
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
     161,   161,   162,   162,   162,   163,   163,   163,   164,   164,
     164,   165,   165,   165,   166,   166,   166,   166,   166,   166,
     167,   167,   167,   167,   167,   167,   167,   168,   168,   168,
     168,   168,   168,   168,   168,   168,   168,   169,   169,   169,
     169,   169,   169,   169,   169,   169,   169,   169,   169,   169,
     169,   169,   169,   169,   170,   170,   171,   171,   171,   171,
     172,   172,   173,   173,   174,   174,   175,   175,   176,   176,
     177,   177,   178,   178,   178,   178,   178,   178,   178,   178,
     179,   179,   179,   179,   179,   179,   179,   179,   180,   180,
     181,   181,   182,   182,   182,   182,   182,   182,   182,   182,
     182,   182,   182,   182,   182,   182,   182,   182,   182,   182,
     182,   182,   182,   182,   182,   182,   182,   182,   182,   182,
     182,   182,   182,   182,   182,   182,   182,   182,   182,   182,
     182,   182,   182,   182,   182,   182,   182,   182,   182,   182,
     183,   183,   183,   183,   183,   183,   184,   184,   185,   185
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
       1,     1,     1,     1,     1,     2,     1,     3,     1,     3,
       1,     2,     1,     3,     4,     1,     3,     3,     1,     3,
       3,     1,     3,     3,     1,     2,     2,     2,     4,     5,
       1,     4,     6,     3,     4,     6,     6,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     2,     1,
       3,     1,     1,     2,     4,     1,     1,     1,     1,     1,
       3,     3,     5,     1,     3,     5,     0,     3,     3,     5,
       0,     3,     2,     3,     0,     1,     1,     3,     1,     4,
       0,     1,     1,     2,     2,     1,     1,     1,     1,     1,
       1,     3,     3,     3,     5,     5,     7,     5,     1,     1,
       1,     3,     1,     1,     1,     1,     1,     1,     1,     1,
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
#line 873 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3230 "src/parser.tab.c"
        break;

    case YYSYMBOL_STRING: /* STRING  */
#line 873 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3236 "src/parser.tab.c"
        break;

    case YYSYMBOL_LENS_CONTENT: /* LENS_CONTENT  */
#line 873 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3242 "src/parser.tab.c"
        break;

    case YYSYMBOL_QUALIFIED_IDENT: /* QUALIFIED_IDENT  */
#line 873 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3248 "src/parser.tab.c"
        break;

    case YYSYMBOL_MODIFIER_PREFIX: /* MODIFIER_PREFIX  */
#line 873 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3254 "src/parser.tab.c"
        break;

    case YYSYMBOL_MODIFIER_VALUE: /* MODIFIER_VALUE  */
#line 873 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3260 "src/parser.tab.c"
        break;

    case YYSYMBOL_SQL_TEXT: /* SQL_TEXT  */
#line 873 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3266 "src/parser.tab.c"
        break;

    case YYSYMBOL_program: /* program  */
#line 896 "src/parser.y"
            { (void) ((*yyvaluep).stmt_list); }
#line 3272 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement_list: /* statement_list  */
#line 833 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3278 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement: /* statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3284 "src/parser.tab.c"
        break;

    case YYSYMBOL_assignment: /* assignment  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3290 "src/parser.tab.c"
        break;

    case YYSYMBOL_type_lock: /* type_lock  */
#line 886 "src/parser.y"
            { ast_type_lock_free(((*yyvaluep).type_lock)); }
#line 3296 "src/parser.tab.c"
        break;

    case YYSYMBOL_lvalue: /* lvalue  */
#line 874 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3302 "src/parser.tab.c"
        break;

    case YYSYMBOL_variable_name: /* variable_name  */
#line 873 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3308 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_lens: /* comparison_lens  */
#line 880 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 3314 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_name: /* modifier_name  */
#line 873 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3320 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_word: /* modifier_word  */
#line 873 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3326 "src/parser.tab.c"
        break;

    case YYSYMBOL_print_statement: /* print_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3332 "src/parser.tab.c"
        break;

    case YYSYMBOL_call_statement: /* call_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3338 "src/parser.tab.c"
        break;

    case YYSYMBOL_with_lock_statement: /* with_lock_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3344 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_end: /* for_end  */
#line 873 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3350 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_each_statement: /* for_each_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3356 "src/parser.tab.c"
        break;

    case YYSYMBOL_do_loop_statement: /* do_loop_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3362 "src/parser.tab.c"
        break;

    case YYSYMBOL_while_statement: /* while_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3368 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement: /* consider_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3374 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_branch_list: /* consider_branch_list  */
#line 878 "src/parser.y"
            { ast_free_consider_branch_list(((*yyvaluep).consider_branch_list)); }
#line 3380 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_else_opt: /* consider_else_opt  */
#line 833 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3386 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement_list: /* consider_statement_list  */
#line 833 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3392 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_body_statement: /* consider_body_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3398 "src/parser.tab.c"
        break;

    case YYSYMBOL_function_statement: /* function_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3404 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_statement: /* modifier_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3410 "src/parser.tab.c"
        break;

    case YYSYMBOL_program_statement: /* program_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3416 "src/parser.tab.c"
        break;

    case YYSYMBOL_library_statement: /* library_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3422 "src/parser.tab.c"
        break;

    case YYSYMBOL_use_statement: /* use_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3428 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_signature: /* modifier_signature  */
#line 881 "src/parser.y"
            { ast_free_modifier_signature(((*yyvaluep).modifier_signature)); }
#line 3434 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_context: /* modifier_context  */
#line 873 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3440 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_statement: /* watch_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3446 "src/parser.tab.c"
        break;

    case YYSYMBOL_unwatch_statement: /* unwatch_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3452 "src/parser.tab.c"
        break;

    case YYSYMBOL_wait_statement: /* wait_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3458 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_list: /* watch_target_list  */
#line 879 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3464 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_statement: /* server_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3470 "src/parser.tab.c"
        break;

    case YYSYMBOL_gsql_block: /* gsql_block  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3476 "src/parser.tab.c"
        break;

    case YYSYMBOL_window_block: /* window_block  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3482 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item_list: /* server_item_list  */
#line 885 "src/parser.y"
            { ast_free_server_item_list(((*yyvaluep).server_item_list)); }
#line 3488 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item: /* server_item  */
#line 884 "src/parser.y"
            { AstServerItemList one = ast_server_item_list_append(ast_server_item_list_empty(), ((*yyvaluep).server_item)); ast_free_server_item_list(one); }
#line 3494 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_string_list: /* server_string_list  */
#line 879 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3500 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_path: /* watch_target_path  */
#line 873 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3506 "src/parser.tab.c"
        break;

    case YYSYMBOL_without_watchers_statement: /* without_watchers_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3512 "src/parser.tab.c"
        break;

    case YYSYMBOL_on_error_statement: /* on_error_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3518 "src/parser.tab.c"
        break;

    case YYSYMBOL_error_statement: /* error_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3524 "src/parser.tab.c"
        break;

    case YYSYMBOL_return_statement: /* return_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3530 "src/parser.tab.c"
        break;

    case YYSYMBOL_label_statement: /* label_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3536 "src/parser.tab.c"
        break;

    case YYSYMBOL_goto_statement: /* goto_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3542 "src/parser.tab.c"
        break;

    case YYSYMBOL_gosub_statement: /* gosub_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3548 "src/parser.tab.c"
        break;

    case YYSYMBOL_break_statement: /* break_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3554 "src/parser.tab.c"
        break;

    case YYSYMBOL_continue_statement: /* continue_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3560 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_statement: /* if_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3566 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_block_tail: /* if_block_tail  */
#line 833 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3572 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_inline_tail: /* if_inline_tail  */
#line 833 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3578 "src/parser.tab.c"
        break;

    case YYSYMBOL_inline_statement: /* inline_statement  */
#line 875 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3584 "src/parser.tab.c"
        break;

    case YYSYMBOL_expression: /* expression  */
#line 874 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3590 "src/parser.tab.c"
        break;

    case YYSYMBOL_or_expression: /* or_expression  */
#line 874 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3596 "src/parser.tab.c"
        break;

    case YYSYMBOL_and_expression: /* and_expression  */
#line 874 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3602 "src/parser.tab.c"
        break;

    case YYSYMBOL_not_expression: /* not_expression  */
#line 874 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3608 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_expression: /* comparison_expression  */
#line 874 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3614 "src/parser.tab.c"
        break;

    case YYSYMBOL_set_expression: /* set_expression  */
#line 874 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3620 "src/parser.tab.c"
        break;

    case YYSYMBOL_additive_expression: /* additive_expression  */
#line 874 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3626 "src/parser.tab.c"
        break;

    case YYSYMBOL_multiplicative_expression: /* multiplicative_expression  */
#line 874 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3632 "src/parser.tab.c"
        break;

    case YYSYMBOL_unary_expression: /* unary_expression  */
#line 874 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3638 "src/parser.tab.c"
        break;

    case YYSYMBOL_postfix_expression: /* postfix_expression  */
#line 874 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3644 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_operator: /* comparison_operator  */
#line 873 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3650 "src/parser.tab.c"
        break;

    case YYSYMBOL_primary: /* primary  */
#line 874 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3656 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_literal: /* record_literal  */
#line 874 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3662 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_suffix: /* ident_suffix  */
#line 882 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3668 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_dot_suffix: /* ident_dot_suffix  */
#line 882 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3674 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list_opt: /* argument_list_opt  */
#line 876 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3680 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list: /* argument_list  */
#line 876 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3686 "src/parser.tab.c"
        break;

    case YYSYMBOL_array_argument_list: /* array_argument_list  */
#line 876 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3692 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list_opt: /* parameter_list_opt  */
#line 879 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3698 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_default: /* parameter_default  */
#line 874 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3704 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list: /* parameter_list  */
#line 879 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3710 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_name: /* field_name  */
#line 873 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3716 "src/parser.tab.c"
        break;

    case YYSYMBOL_projection_names: /* projection_names  */
#line 879 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3722 "src/parser.tab.c"
        break;

    case YYSYMBOL_dot_field_name: /* dot_field_name  */
#line 873 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3728 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_field_list: /* record_field_list  */
#line 877 "src/parser.y"
            { ast_free_record_field_list(((*yyvaluep).record_field_list)); }
#line 3734 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_policy: /* field_policy  */
#line 883 "src/parser.y"
            { ast_free_expr(((*yyvaluep).field_policy).reset_expr); }
#line 3740 "src/parser.tab.c"
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
#line 724 "src/parser.y"
{ (void)yynerrs; }

#line 3840 "src/parser.tab.c"

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
#line 901 "src/parser.y"
                     { ctx->parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 4053 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 905 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4059 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 906 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 4065 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 907 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 4071 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 911 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4077 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 912 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4083 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 913 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4089 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 914 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4095 "src/parser.tab.c"
    break;

  case 10: /* statement: window_block  */
#line 915 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4101 "src/parser.tab.c"
    break;

  case 11: /* statement: gsql_block  */
#line 916 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4107 "src/parser.tab.c"
    break;

  case 12: /* statement: for_each_statement  */
#line 917 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4113 "src/parser.tab.c"
    break;

  case 13: /* statement: while_statement  */
#line 918 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4119 "src/parser.tab.c"
    break;

  case 14: /* statement: do_loop_statement  */
#line 919 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4125 "src/parser.tab.c"
    break;

  case 15: /* statement: consider_statement  */
#line 920 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4131 "src/parser.tab.c"
    break;

  case 16: /* statement: function_statement  */
#line 921 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4137 "src/parser.tab.c"
    break;

  case 17: /* statement: modifier_statement  */
#line 922 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4143 "src/parser.tab.c"
    break;

  case 18: /* statement: program_statement  */
#line 923 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4149 "src/parser.tab.c"
    break;

  case 19: /* statement: library_statement  */
#line 924 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4155 "src/parser.tab.c"
    break;

  case 20: /* statement: use_statement NEWLINE  */
#line 925 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4161 "src/parser.tab.c"
    break;

  case 21: /* statement: watch_statement  */
#line 926 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4167 "src/parser.tab.c"
    break;

  case 22: /* statement: server_statement  */
#line 927 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4173 "src/parser.tab.c"
    break;

  case 23: /* statement: unwatch_statement NEWLINE  */
#line 928 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4179 "src/parser.tab.c"
    break;

  case 24: /* statement: wait_statement NEWLINE  */
#line 929 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4185 "src/parser.tab.c"
    break;

  case 25: /* statement: without_watchers_statement  */
#line 930 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4191 "src/parser.tab.c"
    break;

  case 26: /* statement: on_error_statement NEWLINE  */
#line 931 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4197 "src/parser.tab.c"
    break;

  case 27: /* statement: error_statement NEWLINE  */
#line 932 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4203 "src/parser.tab.c"
    break;

  case 28: /* statement: return_statement NEWLINE  */
#line 933 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4209 "src/parser.tab.c"
    break;

  case 29: /* statement: label_statement NEWLINE  */
#line 934 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4215 "src/parser.tab.c"
    break;

  case 30: /* statement: goto_statement NEWLINE  */
#line 935 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4221 "src/parser.tab.c"
    break;

  case 31: /* statement: gosub_statement NEWLINE  */
#line 936 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4227 "src/parser.tab.c"
    break;

  case 32: /* statement: break_statement NEWLINE  */
#line 937 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4233 "src/parser.tab.c"
    break;

  case 33: /* statement: continue_statement NEWLINE  */
#line 938 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4239 "src/parser.tab.c"
    break;

  case 34: /* statement: if_statement  */
#line 939 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4245 "src/parser.tab.c"
    break;

  case 35: /* statement: DIM  */
#line 945 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 4258 "src/parser.tab.c"
    break;

  case 36: /* assignment: lvalue OP_EQ expression  */
#line 956 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 4264 "src/parser.tab.c"
    break;

  case 37: /* assignment: lvalue compound_op expression  */
#line 961 "src/parser.y"
                                    { (yyval.stmt) = ast_assign_op((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr), (yyvsp[-1].op_char)); }
#line 4270 "src/parser.tab.c"
    break;

  case 38: /* assignment: lvalue comparison_lens compound_op expression  */
#line 962 "src/parser.y"
                                                    {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign_op((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr), (yyvsp[-1].op_char));
      }
#line 4284 "src/parser.tab.c"
    break;

  case 39: /* assignment: variable_name COLON type_lock OP_EQ expression  */
#line 978 "src/parser.y"
                                                     {
        if (!type_lock_check(ctx, (yyvsp[-4].text), (yyvsp[-2].type_lock).name, (yyvsp[-2].type_lock).arg, (yylsp[-2]).first_line, (yylsp[-2]).first_column,
                             (yylsp[-2]).last_line, (yylsp[-2]).last_column)) {
            YYERROR;
        }
        (yyval.stmt) = ast_assign_locked(expr_at(ast_ident((yyvsp[-4].text)), (yylsp[-4]).first_line, (yylsp[-4]).first_column), (yyvsp[-2].type_lock), (yyvsp[0].expr));
      }
#line 4296 "src/parser.tab.c"
    break;

  case 40: /* assignment: lvalue comparison_lens OP_EQ expression  */
#line 985 "src/parser.y"
                                              {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 4310 "src/parser.tab.c"
    break;

  case 41: /* type_lock: IDENT  */
#line 999 "src/parser.y"
            { (yyval.type_lock) = ast_type_lock((yyvsp[0].text), NULL, (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4316 "src/parser.tab.c"
    break;

  case 42: /* type_lock: IDENT LPAREN IDENT RPAREN  */
#line 1000 "src/parser.y"
                                { (yyval.type_lock) = ast_type_lock((yyvsp[-3].text), (yyvsp[-1].text), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 4322 "src/parser.tab.c"
    break;

  case 43: /* compound_op: PLUS_EQ  */
#line 1004 "src/parser.y"
               { (yyval.op_char) = '+'; }
#line 4328 "src/parser.tab.c"
    break;

  case 44: /* compound_op: MINUS_EQ  */
#line 1005 "src/parser.y"
               { (yyval.op_char) = '-'; }
#line 4334 "src/parser.tab.c"
    break;

  case 45: /* compound_op: STAR_EQ  */
#line 1006 "src/parser.y"
               { (yyval.op_char) = '*'; }
#line 4340 "src/parser.tab.c"
    break;

  case 46: /* compound_op: SLASH_EQ  */
#line 1007 "src/parser.y"
               { (yyval.op_char) = '/'; }
#line 4346 "src/parser.tab.c"
    break;

  case 47: /* lvalue: variable_name  */
#line 1011 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4352 "src/parser.tab.c"
    break;

  case 48: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 1012 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4358 "src/parser.tab.c"
    break;

  case 49: /* lvalue: lvalue LBRACKET expression COMMA argument_list RBRACKET  */
#line 1016 "src/parser.y"
                                                                           {
        AstExprList items = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[-3].expr));
        for (size_t i = 0; i < (yyvsp[-1].expr_list).count; i++) {
            items = ast_expr_list_append(items, (yyvsp[-1].expr_list).items[i]);
        }
        free((yyvsp[-1].expr_list).items);
        (yyval.expr) = expr_at(ast_index_list((yyvsp[-5].expr), items), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 4371 "src/parser.tab.c"
    break;

  case 50: /* lvalue: lvalue DOT dot_field_name  */
#line 1024 "src/parser.y"
                                             { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4377 "src/parser.tab.c"
    break;

  case 51: /* variable_name: IDENT  */
#line 1028 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 4383 "src/parser.tab.c"
    break;

  case 52: /* variable_name: END  */
#line 1029 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 4389 "src/parser.tab.c"
    break;

  case 53: /* variable_name: NEXT  */
#line 1030 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 4395 "src/parser.tab.c"
    break;

  case 54: /* $@1: %empty  */
#line 1041 "src/parser.y"
             { lexer_begin_lens_content(ctx->active_lexer); }
#line 4401 "src/parser.tab.c"
    break;

  case 55: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 1041 "src/parser.y"
                                                                                  {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 4409 "src/parser.tab.c"
    break;

  case 56: /* comparison_lens: MODIFIER_PREFIX  */
#line 1049 "src/parser.y"
                      { (yyval.modifier) = parse_modifier_use((yyvsp[0].text)); }
#line 4415 "src/parser.tab.c"
    break;

  case 57: /* modifier_name: modifier_word  */
#line 1053 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 4421 "src/parser.tab.c"
    break;

  case 58: /* modifier_name: modifier_name modifier_word  */
#line 1054 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 4427 "src/parser.tab.c"
    break;

  case 59: /* modifier_word: IDENT  */
#line 1058 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4433 "src/parser.tab.c"
    break;

  case 60: /* modifier_word: TO  */
#line 1059 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 4439 "src/parser.tab.c"
    break;

  case 61: /* modifier_word: END  */
#line 1060 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 4445 "src/parser.tab.c"
    break;

  case 62: /* modifier_word: NEXT  */
#line 1061 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 4451 "src/parser.tab.c"
    break;

  case 63: /* print_statement: PRINT expression  */
#line 1065 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 4457 "src/parser.tab.c"
    break;

  case 64: /* print_statement: PRINT TO ERROR_VALUE expression  */
#line 1071 "src/parser.y"
                                      { (yyval.stmt) = ast_print_error((yyvsp[0].expr)); }
#line 4463 "src/parser.tab.c"
    break;

  case 65: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 1075 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 4469 "src/parser.tab.c"
    break;

  case 66: /* call_statement: TRY expression  */
#line 1078 "src/parser.y"
                     { (yyval.stmt) = ast_expr_stmt(expr_at(ast_unary(copy_const("try"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column)); }
#line 4475 "src/parser.tab.c"
    break;

  case 67: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1079 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 4486 "src/parser.tab.c"
    break;

  case 68: /* call_statement: lvalue DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1085 "src/parser.y"
                                                       {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 4496 "src/parser.tab.c"
    break;

  case 69: /* call_statement: lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1090 "src/parser.y"
                                                                 {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 4510 "src/parser.tab.c"
    break;

  case 70: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1099 "src/parser.y"
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
#line 4525 "src/parser.tab.c"
    break;

  case 71: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 1112 "src/parser.y"
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
#line 4548 "src/parser.tab.c"
    break;

  case 72: /* for_end: END FOR NEWLINE  */
#line 1141 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 4554 "src/parser.tab.c"
    break;

  case 73: /* for_end: NEXT NEWLINE  */
#line 1142 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 4560 "src/parser.tab.c"
    break;

  case 74: /* for_end: NEXT variable_name NEWLINE  */
#line 1143 "src/parser.y"
                                 { (yyval.text) = (yyvsp[-1].text); }
#line 4566 "src/parser.tab.c"
    break;

  case 75: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list for_end  */
#line 1147 "src/parser.y"
                                                             {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4575 "src/parser.tab.c"
    break;

  case 76: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list for_end  */
#line 1151 "src/parser.y"
                                                                  {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4584 "src/parser.tab.c"
    break;

  case 77: /* for_each_statement: FOR IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1167 "src/parser.y"
                                                                         {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { YYERROR; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4594 "src/parser.tab.c"
    break;

  case 78: /* for_each_statement: FOR EACH IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1172 "src/parser.y"
                                                                              {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { YYERROR; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4604 "src/parser.tab.c"
    break;

  case 79: /* for_each_statement: FOR IDENT OP_EQ expression TO expression NEWLINE statement_list for_end  */
#line 1180 "src/parser.y"
                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].expr), NULL, (yyvsp[-1].stmt_list));
      }
#line 4613 "src/parser.tab.c"
    break;

  case 80: /* for_each_statement: FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list for_end  */
#line 1184 "src/parser.y"
                                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-9].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-9].text), (yyvsp[-7].expr), (yyvsp[-5].expr), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4622 "src/parser.tab.c"
    break;

  case 81: /* do_loop_statement: DO NEWLINE statement_list UNTIL expression NEWLINE  */
#line 1205 "src/parser.y"
                                                         {
        (yyval.stmt) = ast_do_loop((yyvsp[-3].stmt_list), (yyvsp[-1].expr));
      }
#line 4630 "src/parser.tab.c"
    break;

  case 82: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 1211 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 4638 "src/parser.tab.c"
    break;

  case 83: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 1217 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 4646 "src/parser.tab.c"
    break;

  case 84: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 1223 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 4654 "src/parser.tab.c"
    break;

  case 85: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 1226 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 4662 "src/parser.tab.c"
    break;

  case 86: /* consider_else_opt: %empty  */
#line 1232 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4668 "src/parser.tab.c"
    break;

  case 87: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 1233 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 4674 "src/parser.tab.c"
    break;

  case 88: /* consider_statement_list: %empty  */
#line 1237 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4680 "src/parser.tab.c"
    break;

  case 89: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 1238 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 4686 "src/parser.tab.c"
    break;

  case 90: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 1239 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 4692 "src/parser.tab.c"
    break;

  case 91: /* consider_body_statement: assignment NEWLINE  */
#line 1243 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4698 "src/parser.tab.c"
    break;

  case 92: /* consider_body_statement: print_statement NEWLINE  */
#line 1244 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4704 "src/parser.tab.c"
    break;

  case 93: /* consider_body_statement: call_statement NEWLINE  */
#line 1245 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4710 "src/parser.tab.c"
    break;

  case 94: /* consider_body_statement: with_lock_statement  */
#line 1246 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4716 "src/parser.tab.c"
    break;

  case 95: /* consider_body_statement: window_block  */
#line 1247 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4722 "src/parser.tab.c"
    break;

  case 96: /* consider_body_statement: for_each_statement  */
#line 1248 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4728 "src/parser.tab.c"
    break;

  case 97: /* consider_body_statement: while_statement  */
#line 1249 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4734 "src/parser.tab.c"
    break;

  case 98: /* consider_body_statement: do_loop_statement  */
#line 1250 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4740 "src/parser.tab.c"
    break;

  case 99: /* consider_body_statement: consider_statement  */
#line 1251 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4746 "src/parser.tab.c"
    break;

  case 100: /* consider_body_statement: function_statement  */
#line 1252 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4752 "src/parser.tab.c"
    break;

  case 101: /* consider_body_statement: modifier_statement  */
#line 1253 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4758 "src/parser.tab.c"
    break;

  case 102: /* consider_body_statement: program_statement  */
#line 1254 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4764 "src/parser.tab.c"
    break;

  case 103: /* consider_body_statement: library_statement  */
#line 1255 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4770 "src/parser.tab.c"
    break;

  case 104: /* consider_body_statement: use_statement NEWLINE  */
#line 1256 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4776 "src/parser.tab.c"
    break;

  case 105: /* consider_body_statement: watch_statement  */
#line 1257 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4782 "src/parser.tab.c"
    break;

  case 106: /* consider_body_statement: unwatch_statement NEWLINE  */
#line 1258 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4788 "src/parser.tab.c"
    break;

  case 107: /* consider_body_statement: wait_statement NEWLINE  */
#line 1259 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4794 "src/parser.tab.c"
    break;

  case 108: /* consider_body_statement: without_watchers_statement  */
#line 1260 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4800 "src/parser.tab.c"
    break;

  case 109: /* consider_body_statement: on_error_statement NEWLINE  */
#line 1261 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4806 "src/parser.tab.c"
    break;

  case 110: /* consider_body_statement: error_statement NEWLINE  */
#line 1262 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4812 "src/parser.tab.c"
    break;

  case 111: /* consider_body_statement: return_statement NEWLINE  */
#line 1263 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4818 "src/parser.tab.c"
    break;

  case 112: /* consider_body_statement: label_statement NEWLINE  */
#line 1264 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4824 "src/parser.tab.c"
    break;

  case 113: /* consider_body_statement: goto_statement NEWLINE  */
#line 1265 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4830 "src/parser.tab.c"
    break;

  case 114: /* consider_body_statement: gosub_statement NEWLINE  */
#line 1266 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4836 "src/parser.tab.c"
    break;

  case 115: /* consider_body_statement: break_statement NEWLINE  */
#line 1267 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4842 "src/parser.tab.c"
    break;

  case 116: /* consider_body_statement: continue_statement NEWLINE  */
#line 1268 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4848 "src/parser.tab.c"
    break;

  case 117: /* consider_body_statement: if_statement  */
#line 1269 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4854 "src/parser.tab.c"
    break;

  case 118: /* consider_body_statement: DIM  */
#line 1275 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 4867 "src/parser.tab.c"
    break;

  case 119: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1286 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4875 "src/parser.tab.c"
    break;

  case 120: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1289 "src/parser.y"
                                                                                                            {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4884 "src/parser.tab.c"
    break;

  case 121: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN COLON type_lock NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1296 "src/parser.y"
                                                                                                                  {
        char who[160];
        snprintf(who, sizeof who, "%s(...)", (yyvsp[-10].text));
        if (!type_lock_check(ctx, who, (yyvsp[-5].type_lock).name, (yyvsp[-5].type_lock).arg, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                             (yylsp[-5]).last_line, (yylsp[-5]).last_column)) {
            YYERROR;
        }
        (yyval.stmt) = ast_function_returning((yyvsp[-10].text), (yyvsp[-8].name_list), (yyvsp[-5].type_lock), (yyvsp[-3].stmt_list));
      }
#line 4898 "src/parser.tab.c"
    break;

  case 122: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN COLON type_lock NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1305 "src/parser.y"
                                                                                                                            {
        char who[160];
        snprintf(who, sizeof who, "%s(...)", (yyvsp[-10].text));
        if (!type_lock_check(ctx, who, (yyvsp[-5].type_lock).name, (yyvsp[-5].type_lock).arg, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                             (yylsp[-5]).last_line, (yylsp[-5]).last_column)) {
            YYERROR;
        }
        (yyval.stmt) = ast_function_returning((yyvsp[-10].text), (yyvsp[-8].name_list), (yyvsp[-5].type_lock), (yyvsp[-3].stmt_list));
      }
#line 4912 "src/parser.tab.c"
    break;

  case 123: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 1317 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 4920 "src/parser.tab.c"
    break;

  case 124: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 1320 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 4928 "src/parser.tab.c"
    break;

  case 125: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 1326 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4936 "src/parser.tab.c"
    break;

  case 126: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 1332 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 4944 "src/parser.tab.c"
    break;

  case 127: /* use_statement: USE IDENT  */
#line 1338 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4950 "src/parser.tab.c"
    break;

  case 128: /* use_statement: LOAD IDENT  */
#line 1339 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4956 "src/parser.tab.c"
    break;

  case 129: /* use_statement: USE STRING  */
#line 1340 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4962 "src/parser.tab.c"
    break;

  case 130: /* use_statement: LOAD STRING  */
#line 1341 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4968 "src/parser.tab.c"
    break;

  case 131: /* use_statement: LOAD IDENT AS IDENT  */
#line 1342 "src/parser.y"
                          { (yyval.stmt) = ast_use((yyvsp[-2].text), NULL, (yyvsp[0].text)); }
#line 4974 "src/parser.tab.c"
    break;

  case 132: /* use_statement: USE IDENT IDENT STRING  */
#line 1343 "src/parser.y"
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
#line 4995 "src/parser.tab.c"
    break;

  case 133: /* use_statement: LOAD IDENT IDENT STRING  */
#line 1359 "src/parser.y"
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
#line 5016 "src/parser.tab.c"
    break;

  case 134: /* use_statement: LOAD IDENT IDENT STRING AS IDENT  */
#line 1375 "src/parser.y"
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
#line 5039 "src/parser.tab.c"
    break;

  case 135: /* modifier_signature: modifier_name  */
#line 1396 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 5045 "src/parser.tab.c"
    break;

  case 136: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 1397 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 5051 "src/parser.tab.c"
    break;

  case 137: /* modifier_context: IDENT  */
#line 1401 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 5057 "src/parser.tab.c"
    break;

  case 138: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1405 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 5065 "src/parser.tab.c"
    break;

  case 139: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 1408 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 5073 "src/parser.tab.c"
    break;

  case 140: /* watch_statement: WATCH IDENT LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1416 "src/parser.y"
                                                                                           {
        (yyval.stmt) = ast_watch((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 5081 "src/parser.tab.c"
    break;

  case 141: /* unwatch_statement: UNWATCH expression  */
#line 1422 "src/parser.y"
                         { (yyval.stmt) = ast_unwatch((yyvsp[0].expr)); }
#line 5087 "src/parser.tab.c"
    break;

  case 142: /* wait_statement: WAIT UNTIL expression  */
#line 1429 "src/parser.y"
                            { (yyval.stmt) = ast_wait((yyvsp[0].expr), NULL); }
#line 5093 "src/parser.tab.c"
    break;

  case 143: /* wait_statement: WAIT UNTIL expression FOR expression  */
#line 1430 "src/parser.y"
                                           { (yyval.stmt) = ast_wait((yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 5099 "src/parser.tab.c"
    break;

  case 144: /* watch_target_list: watch_target_path  */
#line 1434 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5105 "src/parser.tab.c"
    break;

  case 145: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 1435 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5111 "src/parser.tab.c"
    break;

  case 146: /* @2: %empty  */
#line 1455 "src/parser.y"
      { (yyval.stmt) = NULL;   /* the mid-rule carries no value; typed so bison stays quiet */
        server_head_note(ctx, (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column); }
#line 5118 "src/parser.tab.c"
    break;

  case 147: /* server_statement: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE @2 server_item_list END IDENT NEWLINE  */
#line 1457 "src/parser.y"
                                         {
        ctx->bad_block_word[0] = '\0';
        (yyval.stmt) = ast_server((yyvsp[-10].text), (yyvsp[-9].text), (yyvsp[-7].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 5127 "src/parser.tab.c"
    break;

  case 148: /* @3: %empty  */
#line 1462 "src/parser.y"
      { (yyval.stmt) = NULL;   /* the mid-rule carries no value; typed so bison stays quiet */
        server_head_note(ctx, (yyvsp[-4].text), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5134 "src/parser.tab.c"
    break;

  case 149: /* server_statement: IDENT IDENT LPAREN RPAREN NEWLINE @3 server_item_list END IDENT NEWLINE  */
#line 1464 "src/parser.y"
                                         {
        ctx->bad_block_word[0] = '\0';
        (yyval.stmt) = ast_server((yyvsp[-9].text), (yyvsp[-8].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 5143 "src/parser.tab.c"
    break;

  case 150: /* gsql_block: DATABASE_OPEN IDENT LPAREN record_field_list RPAREN NEWLINE  */
#line 1484 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_database((yyvsp[-4].text), (yyvsp[-2].record_field_list), (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 5151 "src/parser.tab.c"
    break;

  case 151: /* gsql_block: DATABASE_OPEN IDENT LPAREN RPAREN NEWLINE  */
#line 1487 "src/parser.y"
                                                {
        (yyval.stmt) = ast_database((yyvsp[-3].text), ast_record_field_list_empty(), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5159 "src/parser.tab.c"
    break;

  case 152: /* gsql_block: SQL_OPEN IDENT ON IDENT NEWLINE SQL_TEXT END IDENT NEWLINE  */
#line 1490 "src/parser.y"
                                                                 {
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_sql((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].text), (yylsp[-3]).first_line, (yylsp[-3]).first_column, (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5168 "src/parser.tab.c"
    break;

  case 153: /* gsql_block: SQL_OPEN IDENT ON IDENT DOT IDENT NEWLINE SQL_TEXT END IDENT NEWLINE  */
#line 1496 "src/parser.y"
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
#line 5185 "src/parser.tab.c"
    break;

  case 154: /* window_block: WINDOW_OPEN IDENT LPAREN record_field_list RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1511 "src/parser.y"
                                                                                                   {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), NULL, NULL, NULL, (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5194 "src/parser.tab.c"
    break;

  case 155: /* window_block: WINDOW_OPEN IDENT LPAREN RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1515 "src/parser.y"
                                                                                 {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text), NULL, NULL, NULL,
                        (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5204 "src/parser.tab.c"
    break;

  case 156: /* window_block: WINDOW_OPEN IDENT LPAREN record_field_list RPAREN TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1525 "src/parser.y"
                                                                                                                  {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-11].text), (yyvsp[-9].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text),
                        (yylsp[-12]).first_line, (yylsp[-12]).first_column);
      }
#line 5214 "src/parser.tab.c"
    break;

  case 157: /* window_item_list: %empty  */
#line 1533 "src/parser.y"
             { (yyval.window_item_list) = ast_window_item_list_empty(); }
#line 5220 "src/parser.tab.c"
    break;

  case 158: /* window_item_list: window_item_list NEWLINE  */
#line 1534 "src/parser.y"
                               { (yyval.window_item_list) = (yyvsp[-1].window_item_list); }
#line 5226 "src/parser.tab.c"
    break;

  case 159: /* window_item_list: window_item_list window_item  */
#line 1535 "src/parser.y"
                                   { (yyval.window_item_list) = ast_window_item_list_append((yyvsp[-1].window_item_list), (yyvsp[0].window_item)); }
#line 5232 "src/parser.tab.c"
    break;

  case 160: /* window_item: IDENT LPAREN RPAREN NEWLINE  */
#line 1540 "src/parser.y"
                                  {
        (yyval.window_item) = ast_window_leaf((yyvsp[-3].text), NULL, ast_record_field_list_empty(),
                             (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 5241 "src/parser.tab.c"
    break;

  case 161: /* window_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE  */
#line 1545 "src/parser.y"
                                                          {
        (yyval.window_item) = ast_window_leaf((yyvsp[-5].text), (yyvsp[-4].text), (yyvsp[-2].record_field_list), (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 5249 "src/parser.tab.c"
    break;

  case 162: /* window_item: IDENT IDENT LPAREN RPAREN NEWLINE  */
#line 1549 "src/parser.y"
                                        {
        (yyval.window_item) = ast_window_leaf((yyvsp[-4].text), (yyvsp[-3].text), ast_record_field_list_empty(),
                             (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5258 "src/parser.tab.c"
    break;

  case 163: /* window_item: IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1554 "src/parser.y"
                                                       {
        (yyval.window_item) = ast_window_container((yyvsp[-5].text), NULL, ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                  (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 5267 "src/parser.tab.c"
    break;

  case 164: /* window_item: IDENT LPAREN record_field_list RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1559 "src/parser.y"
                                                                                       {
        (yyval.window_item) = ast_window_container((yyvsp[-8].text), NULL, (yyvsp[-6].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5275 "src/parser.tab.c"
    break;

  case 165: /* window_item: IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1573 "src/parser.y"
                                                             {
        (yyval.window_item) = ast_window_named_form((yyvsp[-6].text), (yyvsp[-5].text), NULL, NULL, NULL, (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                   (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5284 "src/parser.tab.c"
    break;

  case 166: /* window_item: IDENT IDENT STRING NEWLINE window_item_list END IDENT NEWLINE  */
#line 1588 "src/parser.y"
                                                                    {
        (yyval.window_item) = ast_window_named_container((yyvsp[-7].text), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                        (yylsp[-7]).first_line, (yylsp[-7]).first_column);
      }
#line 5293 "src/parser.tab.c"
    break;

  case 167: /* window_item: IDENT STRING NEWLINE window_item_list END IDENT NEWLINE  */
#line 1593 "src/parser.y"
                                                              {
        (yyval.window_item) = ast_window_container((yyvsp[-6].text), (yyvsp[-5].text), ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                  (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5302 "src/parser.tab.c"
    break;

  case 168: /* window_item: IDENT TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1604 "src/parser.y"
                                                                      {
        (yyval.window_item) = ast_window_form((yyvsp[-8].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                             (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5311 "src/parser.tab.c"
    break;

  case 169: /* window_item: IDENT IDENT TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1619 "src/parser.y"
                                                                            {
        (yyval.window_item) = ast_window_named_form((yyvsp[-9].text), (yyvsp[-8].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                   (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5320 "src/parser.tab.c"
    break;

  case 170: /* window_item: ON IDENT DOT IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1624 "src/parser.y"
                                                               {
        (yyval.window_item) = ast_window_handler((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5328 "src/parser.tab.c"
    break;

  case 171: /* server_item_list: %empty  */
#line 1630 "src/parser.y"
             { (yyval.server_item_list) = ast_server_item_list_empty(); }
#line 5334 "src/parser.tab.c"
    break;

  case 172: /* server_item_list: server_item_list NEWLINE  */
#line 1631 "src/parser.y"
                               { (yyval.server_item_list) = (yyvsp[-1].server_item_list); }
#line 5340 "src/parser.tab.c"
    break;

  case 173: /* server_item_list: server_item_list server_item  */
#line 1632 "src/parser.y"
                                   { (yyval.server_item_list) = ast_server_item_list_append((yyvsp[-1].server_item_list), (yyvsp[0].server_item)); }
#line 5346 "src/parser.tab.c"
    break;

  case 174: /* server_item: IDENT server_string_list NEWLINE  */
#line 1636 "src/parser.y"
                                       {
        (yyval.server_item) = ast_server_directive((yyvsp[-2].text), (yyvsp[-1].name_list), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 5354 "src/parser.tab.c"
    break;

  case 175: /* server_item: IDENT STRING LPAREN parameter_list_opt RPAREN NEWLINE statement_list END IDENT NEWLINE  */
#line 1639 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_handler((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5362 "src/parser.tab.c"
    break;

  case 176: /* server_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1642 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_site((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5370 "src/parser.tab.c"
    break;

  case 177: /* server_item: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1645 "src/parser.y"
                                                                           {
        (yyval.server_item) = ast_server_site((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5378 "src/parser.tab.c"
    break;

  case 178: /* server_item: ON IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1648 "src/parser.y"
                                                     {
        (yyval.server_item) = ast_server_hook((yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5386 "src/parser.tab.c"
    break;

  case 179: /* server_string_list: STRING  */
#line 1654 "src/parser.y"
             { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5392 "src/parser.tab.c"
    break;

  case 180: /* server_string_list: server_string_list COMMA STRING  */
#line 1655 "src/parser.y"
                                      { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5398 "src/parser.tab.c"
    break;

  case 181: /* watch_target_path: variable_name  */
#line 1659 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 5404 "src/parser.tab.c"
    break;

  case 182: /* watch_target_path: watch_target_path DOT IDENT  */
#line 1660 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 5410 "src/parser.tab.c"
    break;

  case 183: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 1664 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 5418 "src/parser.tab.c"
    break;

  case 184: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 1670 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 5424 "src/parser.tab.c"
    break;

  case 185: /* on_error_statement: ON ERROR_VALUE GOTO NEXT  */
#line 1671 "src/parser.y"
                               { (yyval.stmt) = ast_on_error_goto_next(); }
#line 5430 "src/parser.tab.c"
    break;

  case 186: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 1672 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 5436 "src/parser.tab.c"
    break;

  case 187: /* on_error_statement: ON IDENT GOTO NEXT  */
#line 1673 "src/parser.y"
                         {
        if (!warn_channel_ok(ctx, (yyvsp[-2].text), (yylsp[-2]).first_line, (yylsp[-2]).first_column)) { YYERROR; }
        free((yyvsp[-2].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_NEXT);
      }
#line 5446 "src/parser.tab.c"
    break;

  case 188: /* on_error_statement: ON IDENT GOTO IDENT  */
#line 1678 "src/parser.y"
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
#line 5465 "src/parser.tab.c"
    break;

  case 189: /* on_error_statement: ON IDENT STOP  */
#line 1692 "src/parser.y"
                    {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_STOP);
      }
#line 5475 "src/parser.tab.c"
    break;

  case 190: /* on_error_statement: ON IDENT PRINT  */
#line 1697 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_PRINT);
      }
#line 5485 "src/parser.tab.c"
    break;

  case 191: /* on_error_statement: ON IDENT IDENT  */
#line 1702 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        int mode = warn_mode_word(ctx, (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column);
        if (mode < 0) { free((yyvsp[-1].text)); free((yyvsp[0].text)); YYERROR; }
        free((yyvsp[-1].text)); free((yyvsp[0].text));
        (yyval.stmt) = ast_on_warning(mode);
      }
#line 5497 "src/parser.tab.c"
    break;

  case 192: /* error_statement: ERROR_VALUE expression  */
#line 1712 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 5503 "src/parser.tab.c"
    break;

  case 193: /* return_statement: RETURN  */
#line 1716 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 5509 "src/parser.tab.c"
    break;

  case 194: /* return_statement: RETURN expression  */
#line 1717 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 5515 "src/parser.tab.c"
    break;

  case 195: /* label_statement: variable_name COLON  */
#line 1721 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 5521 "src/parser.tab.c"
    break;

  case 196: /* goto_statement: GOTO variable_name  */
#line 1728 "src/parser.y"
                         { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 5527 "src/parser.tab.c"
    break;

  case 197: /* gosub_statement: GOSUB variable_name  */
#line 1732 "src/parser.y"
                          { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 5533 "src/parser.tab.c"
    break;

  case 198: /* break_statement: BREAK  */
#line 1741 "src/parser.y"
            { (yyval.stmt) = ast_break(NULL); }
#line 5539 "src/parser.tab.c"
    break;

  case 199: /* break_statement: BREAK IDENT  */
#line 1742 "src/parser.y"
                  { (yyval.stmt) = ast_break((yyvsp[0].text)); }
#line 5545 "src/parser.tab.c"
    break;

  case 200: /* continue_statement: CONTINUE  */
#line 1746 "src/parser.y"
               { (yyval.stmt) = ast_continue(NULL); }
#line 5551 "src/parser.tab.c"
    break;

  case 201: /* continue_statement: CONTINUE IDENT  */
#line 1747 "src/parser.y"
                     { (yyval.stmt) = ast_continue((yyvsp[0].text)); }
#line 5557 "src/parser.tab.c"
    break;

  case 202: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1751 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 5566 "src/parser.tab.c"
    break;

  case 203: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1755 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 5575 "src/parser.tab.c"
    break;

  case 204: /* if_block_tail: END IF NEWLINE  */
#line 1762 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 5583 "src/parser.tab.c"
    break;

  case 205: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 1765 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 5591 "src/parser.tab.c"
    break;

  case 206: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1768 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 5599 "src/parser.tab.c"
    break;

  case 207: /* if_block_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1777 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5611 "src/parser.tab.c"
    break;

  case 208: /* if_inline_tail: %empty  */
#line 1787 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 5619 "src/parser.tab.c"
    break;

  case 209: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 1790 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 5627 "src/parser.tab.c"
    break;

  case 210: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1793 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 5635 "src/parser.tab.c"
    break;

  case 211: /* if_inline_tail: ELSE IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1799 "src/parser.y"
                                                                      {
        AstStmt *inner = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5647 "src/parser.tab.c"
    break;

  case 212: /* if_inline_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1806 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5659 "src/parser.tab.c"
    break;

  case 213: /* inline_statement: assignment  */
#line 1816 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5665 "src/parser.tab.c"
    break;

  case 214: /* inline_statement: print_statement  */
#line 1817 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5671 "src/parser.tab.c"
    break;

  case 215: /* inline_statement: call_statement  */
#line 1818 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5677 "src/parser.tab.c"
    break;

  case 216: /* inline_statement: use_statement  */
#line 1819 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5683 "src/parser.tab.c"
    break;

  case 217: /* inline_statement: on_error_statement  */
#line 1820 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5689 "src/parser.tab.c"
    break;

  case 218: /* inline_statement: error_statement  */
#line 1821 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5695 "src/parser.tab.c"
    break;

  case 219: /* inline_statement: return_statement  */
#line 1822 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5701 "src/parser.tab.c"
    break;

  case 220: /* inline_statement: goto_statement  */
#line 1823 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5707 "src/parser.tab.c"
    break;

  case 221: /* inline_statement: gosub_statement  */
#line 1824 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5713 "src/parser.tab.c"
    break;

  case 222: /* inline_statement: break_statement  */
#line 1825 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5719 "src/parser.tab.c"
    break;

  case 223: /* inline_statement: continue_statement  */
#line 1826 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5725 "src/parser.tab.c"
    break;

  case 224: /* expression: or_expression  */
#line 1834 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 5731 "src/parser.tab.c"
    break;

  case 225: /* expression: TRY expression  */
#line 1835 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_unary(copy_const("try"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5737 "src/parser.tab.c"
    break;

  case 226: /* or_expression: and_expression  */
#line 1839 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5743 "src/parser.tab.c"
    break;

  case 227: /* or_expression: or_expression OR and_expression  */
#line 1840 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5749 "src/parser.tab.c"
    break;

  case 228: /* and_expression: not_expression  */
#line 1844 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5755 "src/parser.tab.c"
    break;

  case 229: /* and_expression: and_expression AND not_expression  */
#line 1845 "src/parser.y"
                                        { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5761 "src/parser.tab.c"
    break;

  case 230: /* not_expression: comparison_expression  */
#line 1867 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 5767 "src/parser.tab.c"
    break;

  case 231: /* not_expression: NOT not_expression  */
#line 1868 "src/parser.y"
                         { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5773 "src/parser.tab.c"
    break;

  case 232: /* comparison_expression: set_expression  */
#line 1872 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5779 "src/parser.tab.c"
    break;

  case 233: /* comparison_expression: set_expression comparison_operator set_expression  */
#line 1873 "src/parser.y"
                                                        { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5785 "src/parser.tab.c"
    break;

  case 234: /* comparison_expression: set_expression comparison_lens comparison_operator set_expression  */
#line 1874 "src/parser.y"
                                                                        {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 5793 "src/parser.tab.c"
    break;

  case 235: /* set_expression: additive_expression  */
#line 1880 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 5799 "src/parser.tab.c"
    break;

  case 236: /* set_expression: set_expression EXCLUDING additive_expression  */
#line 1881 "src/parser.y"
                                                   { (yyval.expr) = expr_at(ast_binary(copy_const("excluding"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5805 "src/parser.tab.c"
    break;

  case 237: /* set_expression: set_expression INTERSECTING additive_expression  */
#line 1882 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("intersecting"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5811 "src/parser.tab.c"
    break;

  case 238: /* additive_expression: multiplicative_expression  */
#line 1886 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 5817 "src/parser.tab.c"
    break;

  case 239: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 1887 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5823 "src/parser.tab.c"
    break;

  case 240: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 1888 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5829 "src/parser.tab.c"
    break;

  case 241: /* multiplicative_expression: unary_expression  */
#line 1892 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 5835 "src/parser.tab.c"
    break;

  case 242: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 1893 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5841 "src/parser.tab.c"
    break;

  case 243: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 1894 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5847 "src/parser.tab.c"
    break;

  case 244: /* unary_expression: postfix_expression  */
#line 1898 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 5853 "src/parser.tab.c"
    break;

  case 245: /* unary_expression: MINUS unary_expression  */
#line 1899 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5859 "src/parser.tab.c"
    break;

  case 246: /* unary_expression: MODIFIER_PREFIX unary_expression  */
#line 1906 "src/parser.y"
                                       {
        (yyval.expr) = expr_at(ast_modifier_apply(parse_modifier_use((yyvsp[-1].text)), (yyvsp[0].expr)),
                     (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 5868 "src/parser.tab.c"
    break;

  case 247: /* unary_expression: NEW postfix_expression  */
#line 1910 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5874 "src/parser.tab.c"
    break;

  case 248: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 1911 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 5880 "src/parser.tab.c"
    break;

  case 249: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 1912 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5886 "src/parser.tab.c"
    break;

  case 250: /* postfix_expression: primary  */
#line 1916 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 5892 "src/parser.tab.c"
    break;

  case 251: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 1917 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5898 "src/parser.tab.c"
    break;

  case 252: /* postfix_expression: postfix_expression LBRACKET expression COMMA argument_list RBRACKET  */
#line 1921 "src/parser.y"
                                                                          {
        AstExprList items = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[-3].expr));
        for (size_t i = 0; i < (yyvsp[-1].expr_list).count; i++) {
            items = ast_expr_list_append(items, (yyvsp[-1].expr_list).items[i]);
        }
        free((yyvsp[-1].expr_list).items);
        (yyval.expr) = expr_at(ast_index_list((yyvsp[-5].expr), items), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5911 "src/parser.tab.c"
    break;

  case 253: /* postfix_expression: postfix_expression DOT dot_field_name  */
#line 1929 "src/parser.y"
                                            { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5917 "src/parser.tab.c"
    break;

  case 254: /* postfix_expression: postfix_expression PATH_LBRACE projection_names RBRACE  */
#line 1934 "src/parser.y"
                                                             {
        (yyval.expr) = expr_at(ast_project((yyvsp[-3].expr), (yyvsp[-1].name_list)), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 5925 "src/parser.tab.c"
    break;

  case 255: /* postfix_expression: postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1937 "src/parser.y"
                                                                   {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        (yyval.expr) = expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5935 "src/parser.tab.c"
    break;

  case 256: /* postfix_expression: postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1942 "src/parser.y"
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
#line 5951 "src/parser.tab.c"
    break;

  case 257: /* comparison_operator: OP_EQ  */
#line 1956 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 5957 "src/parser.tab.c"
    break;

  case 258: /* comparison_operator: OP_NE  */
#line 1957 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 5963 "src/parser.tab.c"
    break;

  case 259: /* comparison_operator: OP_GT  */
#line 1958 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 5969 "src/parser.tab.c"
    break;

  case 260: /* comparison_operator: OP_LT  */
#line 1959 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 5975 "src/parser.tab.c"
    break;

  case 261: /* comparison_operator: OP_GE  */
#line 1960 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 5981 "src/parser.tab.c"
    break;

  case 262: /* comparison_operator: OP_LE  */
#line 1961 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 5987 "src/parser.tab.c"
    break;

  case 263: /* comparison_operator: OP_NGT  */
#line 1962 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 5993 "src/parser.tab.c"
    break;

  case 264: /* comparison_operator: OP_NLT  */
#line 1963 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 5999 "src/parser.tab.c"
    break;

  case 265: /* comparison_operator: OP_NGE  */
#line 1964 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 6005 "src/parser.tab.c"
    break;

  case 266: /* comparison_operator: OP_NLE  */
#line 1965 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 6011 "src/parser.tab.c"
    break;

  case 267: /* primary: MODIFIER_VALUE  */
#line 1969 "src/parser.y"
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
#line 6034 "src/parser.tab.c"
    break;

  case 268: /* primary: DOT dot_field_name  */
#line 1990 "src/parser.y"
                         { (yyval.expr) = expr_at(ast_column((yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6040 "src/parser.tab.c"
    break;

  case 269: /* primary: NUMBER  */
#line 1991 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact, (yyvsp[0].numlit).scale), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6046 "src/parser.tab.c"
    break;

  case 270: /* primary: WATCHERS LPAREN RPAREN  */
#line 1992 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_call(copy_const("watchers"), ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 6052 "src/parser.tab.c"
    break;

  case 271: /* primary: duration_terms  */
#line 1993 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6058 "src/parser.tab.c"
    break;

  case 272: /* primary: STRING  */
#line 1994 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6064 "src/parser.tab.c"
    break;

  case 273: /* primary: variable_name ident_suffix  */
#line 1995 "src/parser.y"
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
#line 6086 "src/parser.tab.c"
    break;

  case 274: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 2012 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 6097 "src/parser.tab.c"
    break;

  case 275: /* primary: ERROR_VALUE  */
#line 2018 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6103 "src/parser.tab.c"
    break;

  case 276: /* primary: TRUE  */
#line 2019 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6109 "src/parser.tab.c"
    break;

  case 277: /* primary: FALSE  */
#line 2020 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6115 "src/parser.tab.c"
    break;

  case 278: /* primary: NOTHING  */
#line 2021 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6121 "src/parser.tab.c"
    break;

  case 279: /* primary: UNKNOWN_VALUE  */
#line 2022 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6127 "src/parser.tab.c"
    break;

  case 280: /* primary: LPAREN expression RPAREN  */
#line 2023 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); (yyval.expr)->parenthesized = 1; }
#line 6133 "src/parser.tab.c"
    break;

  case 281: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 2024 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 6139 "src/parser.tab.c"
    break;

  case 282: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 2025 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 6145 "src/parser.tab.c"
    break;

  case 283: /* primary: record_literal  */
#line 2026 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 6151 "src/parser.tab.c"
    break;

  case 284: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 2030 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 6157 "src/parser.tab.c"
    break;

  case 285: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 2031 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 6163 "src/parser.tab.c"
    break;

  case 286: /* ident_suffix: %empty  */
#line 2035 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 6173 "src/parser.tab.c"
    break;

  case 287: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 2040 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 6183 "src/parser.tab.c"
    break;

  case 288: /* ident_suffix: DOT dot_field_name ident_dot_suffix  */
#line 2045 "src/parser.y"
                                          {
        /* dot_field_name, not IDENT: a keyword is a legal FIELD name after a
         * dot, because nothing but a name can appear there. */
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 6194 "src/parser.tab.c"
    break;

  case 289: /* ident_suffix: DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 2051 "src/parser.y"
                                                          {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        (yyval.ident_suffix).kind = IDENT_SUFFIX_METHOD;
        (yyval.ident_suffix).name = (yyvsp[-3].text);
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 6208 "src/parser.tab.c"
    break;

  case 290: /* ident_dot_suffix: %empty  */
#line 2063 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 6218 "src/parser.tab.c"
    break;

  case 291: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 2068 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 6228 "src/parser.tab.c"
    break;

  case 292: /* duration_terms: NUMBER IDENT  */
#line 2076 "src/parser.y"
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
#line 6243 "src/parser.tab.c"
    break;

  case 293: /* duration_terms: duration_terms NUMBER IDENT  */
#line 2086 "src/parser.y"
                                  {
        char *bad = NULL;
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].numlit).value, (yyvsp[0].text), &bad);
        if (bad) {
            duration_unit_error(ctx, bad, (yylsp[0]).first_line, (yylsp[0]).first_column,
                                (yylsp[0]).last_line, (yylsp[0]).last_column);
            YYERROR;
        }
      }
#line 6257 "src/parser.tab.c"
    break;

  case 294: /* argument_list_opt: %empty  */
#line 2098 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 6263 "src/parser.tab.c"
    break;

  case 295: /* argument_list_opt: argument_list  */
#line 2099 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 6269 "src/parser.tab.c"
    break;

  case 296: /* argument_list: expression  */
#line 2103 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 6275 "src/parser.tab.c"
    break;

  case 297: /* argument_list: argument_list COMMA expression  */
#line 2104 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 6281 "src/parser.tab.c"
    break;

  case 298: /* array_argument_list: expression  */
#line 2108 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 6287 "src/parser.tab.c"
    break;

  case 299: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 2109 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 6293 "src/parser.tab.c"
    break;

  case 300: /* parameter_list_opt: %empty  */
#line 2113 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 6299 "src/parser.tab.c"
    break;

  case 301: /* parameter_list_opt: parameter_list  */
#line 2114 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 6305 "src/parser.tab.c"
    break;

  case 302: /* parameter_default: NUMBER  */
#line 2128 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact, (yyvsp[0].numlit).scale), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6311 "src/parser.tab.c"
    break;

  case 303: /* parameter_default: MINUS NUMBER  */
#line 2132 "src/parser.y"
                   { (yyval.expr) = expr_at(ast_number(-(yyvsp[0].numlit).value,
                                             (yyvsp[0].numlit).exact == LLONG_MIN ? 0 : -(yyvsp[0].numlit).exact,
                                             (yyvsp[0].numlit).exact == LLONG_MIN ? 0 : (yyvsp[0].numlit).is_exact,
                                             (yyvsp[0].numlit).exact == LLONG_MIN ? 0 : (yyvsp[0].numlit).scale),
                                  (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6321 "src/parser.tab.c"
    break;

  case 304: /* parameter_default: PLUS NUMBER  */
#line 2137 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact, (yyvsp[0].numlit).scale), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6327 "src/parser.tab.c"
    break;

  case 305: /* parameter_default: STRING  */
#line 2138 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6333 "src/parser.tab.c"
    break;

  case 306: /* parameter_default: TRUE  */
#line 2139 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6339 "src/parser.tab.c"
    break;

  case 307: /* parameter_default: FALSE  */
#line 2140 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6345 "src/parser.tab.c"
    break;

  case 308: /* parameter_default: NOTHING  */
#line 2141 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6351 "src/parser.tab.c"
    break;

  case 309: /* parameter_default: UNKNOWN_VALUE  */
#line 2142 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6357 "src/parser.tab.c"
    break;

  case 310: /* parameter_list: IDENT  */
#line 2146 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 6363 "src/parser.tab.c"
    break;

  case 311: /* parameter_list: IDENT OP_EQ parameter_default  */
#line 2147 "src/parser.y"
                                    {
        (yyval.name_list) = ast_name_list_append_default(ast_name_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 6371 "src/parser.tab.c"
    break;

  case 312: /* parameter_list: parameter_list COMMA IDENT  */
#line 2150 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 6377 "src/parser.tab.c"
    break;

  case 313: /* parameter_list: IDENT COLON type_lock  */
#line 2154 "src/parser.y"
                            {
        if (!type_lock_check(ctx, (yyvsp[-2].text), (yyvsp[0].type_lock).name, (yyvsp[0].type_lock).arg, (yylsp[0]).first_line, (yylsp[0]).first_column,
                             (yylsp[0]).last_line, (yylsp[0]).last_column)) {
            YYERROR;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append(ast_name_list_empty(), (yyvsp[-2].text)), (yyvsp[0].type_lock));
      }
#line 6389 "src/parser.tab.c"
    break;

  case 314: /* parameter_list: IDENT COLON type_lock OP_EQ parameter_default  */
#line 2161 "src/parser.y"
                                                    {
        if (!type_lock_check(ctx, (yyvsp[-4].text), (yyvsp[-2].type_lock).name, (yyvsp[-2].type_lock).arg, (yylsp[-2]).first_line, (yylsp[-2]).first_column,
                             (yylsp[-2]).last_line, (yylsp[-2]).last_column)) {
            YYERROR;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append_default(ast_name_list_empty(), (yyvsp[-4].text), (yyvsp[0].expr)), (yyvsp[-2].type_lock));
      }
#line 6401 "src/parser.tab.c"
    break;

  case 315: /* parameter_list: parameter_list COMMA IDENT COLON type_lock  */
#line 2168 "src/parser.y"
                                                 {
        if (!type_lock_check(ctx, (yyvsp[-2].text), (yyvsp[0].type_lock).name, (yyvsp[0].type_lock).arg, (yylsp[0]).first_line, (yylsp[0]).first_column,
                             (yylsp[0]).last_line, (yylsp[0]).last_column)) {
            YYERROR;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append((yyvsp[-4].name_list), (yyvsp[-2].text)), (yyvsp[0].type_lock));
      }
#line 6413 "src/parser.tab.c"
    break;

  case 316: /* parameter_list: parameter_list COMMA IDENT COLON type_lock OP_EQ parameter_default  */
#line 2175 "src/parser.y"
                                                                         {
        if (!type_lock_check(ctx, (yyvsp[-4].text), (yyvsp[-2].type_lock).name, (yyvsp[-2].type_lock).arg, (yylsp[-2]).first_line, (yylsp[-2]).first_column,
                             (yylsp[-2]).last_line, (yylsp[-2]).last_column)) {
            YYERROR;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append_default((yyvsp[-6].name_list), (yyvsp[-4].text), (yyvsp[0].expr)), (yyvsp[-2].type_lock));
      }
#line 6425 "src/parser.tab.c"
    break;

  case 317: /* parameter_list: parameter_list COMMA IDENT OP_EQ parameter_default  */
#line 2182 "src/parser.y"
                                                         {
        (yyval.name_list) = ast_name_list_append_default((yyvsp[-4].name_list), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 6433 "src/parser.tab.c"
    break;

  case 318: /* field_name: dot_field_name  */
#line 2197 "src/parser.y"
                     { (yyval.text) = (yyvsp[0].text); }
#line 6439 "src/parser.tab.c"
    break;

  case 319: /* field_name: STRING  */
#line 2204 "src/parser.y"
             { (yyval.text) = (yyvsp[0].text); }
#line 6445 "src/parser.tab.c"
    break;

  case 320: /* projection_names: dot_field_name  */
#line 2213 "src/parser.y"
                     { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 6451 "src/parser.tab.c"
    break;

  case 321: /* projection_names: projection_names COMMA dot_field_name  */
#line 2214 "src/parser.y"
                                            { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 6457 "src/parser.tab.c"
    break;

  case 322: /* dot_field_name: IDENT  */
#line 2218 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 6463 "src/parser.tab.c"
    break;

  case 323: /* dot_field_name: AS  */
#line 2219 "src/parser.y"
                     { (yyval.text) = kw_name("as"); }
#line 6469 "src/parser.tab.c"
    break;

  case 324: /* dot_field_name: NEXT  */
#line 2220 "src/parser.y"
                     { (yyval.text) = kw_name("next"); }
#line 6475 "src/parser.tab.c"
    break;

  case 325: /* dot_field_name: STOP  */
#line 2221 "src/parser.y"
                     { (yyval.text) = kw_name("stop"); }
#line 6481 "src/parser.tab.c"
    break;

  case 326: /* dot_field_name: ERROR_VALUE  */
#line 2222 "src/parser.y"
                     { (yyval.text) = kw_name("error"); }
#line 6487 "src/parser.tab.c"
    break;

  case 327: /* dot_field_name: END  */
#line 2223 "src/parser.y"
                     { (yyval.text) = kw_name("end"); }
#line 6493 "src/parser.tab.c"
    break;

  case 328: /* dot_field_name: TO  */
#line 2224 "src/parser.y"
                     { (yyval.text) = kw_name("to"); }
#line 6499 "src/parser.tab.c"
    break;

  case 329: /* dot_field_name: IN  */
#line 2225 "src/parser.y"
                     { (yyval.text) = kw_name("in"); }
#line 6505 "src/parser.tab.c"
    break;

  case 330: /* dot_field_name: ON  */
#line 2226 "src/parser.y"
                     { (yyval.text) = kw_name("on"); }
#line 6511 "src/parser.tab.c"
    break;

  case 331: /* dot_field_name: NEW  */
#line 2227 "src/parser.y"
                     { (yyval.text) = kw_name("new"); }
#line 6517 "src/parser.tab.c"
    break;

  case 332: /* dot_field_name: EACH  */
#line 2228 "src/parser.y"
                     { (yyval.text) = kw_name("each"); }
#line 6523 "src/parser.tab.c"
    break;

  case 333: /* dot_field_name: WITH  */
#line 2229 "src/parser.y"
                     { (yyval.text) = kw_name("with"); }
#line 6529 "src/parser.tab.c"
    break;

  case 334: /* dot_field_name: WITHOUT  */
#line 2230 "src/parser.y"
                     { (yyval.text) = kw_name("without"); }
#line 6535 "src/parser.tab.c"
    break;

  case 335: /* dot_field_name: THEN  */
#line 2231 "src/parser.y"
                     { (yyval.text) = kw_name("then"); }
#line 6541 "src/parser.tab.c"
    break;

  case 336: /* dot_field_name: ELSE  */
#line 2232 "src/parser.y"
                     { (yyval.text) = kw_name("else"); }
#line 6547 "src/parser.tab.c"
    break;

  case 337: /* dot_field_name: FOR  */
#line 2233 "src/parser.y"
                     { (yyval.text) = kw_name("for"); }
#line 6553 "src/parser.tab.c"
    break;

  case 338: /* dot_field_name: IF  */
#line 2234 "src/parser.y"
                     { (yyval.text) = kw_name("if"); }
#line 6559 "src/parser.tab.c"
    break;

  case 339: /* dot_field_name: WHILE  */
#line 2235 "src/parser.y"
                     { (yyval.text) = kw_name("while"); }
#line 6565 "src/parser.tab.c"
    break;

  case 340: /* dot_field_name: DO  */
#line 2236 "src/parser.y"
                     { (yyval.text) = kw_name("do"); }
#line 6571 "src/parser.tab.c"
    break;

  case 341: /* dot_field_name: UNTIL  */
#line 2237 "src/parser.y"
                     { (yyval.text) = kw_name("until"); }
#line 6577 "src/parser.tab.c"
    break;

  case 342: /* dot_field_name: PRINT  */
#line 2238 "src/parser.y"
                     { (yyval.text) = kw_name("print"); }
#line 6583 "src/parser.tab.c"
    break;

  case 343: /* dot_field_name: RETURN  */
#line 2239 "src/parser.y"
                     { (yyval.text) = kw_name("return"); }
#line 6589 "src/parser.tab.c"
    break;

  case 344: /* dot_field_name: LOAD  */
#line 2240 "src/parser.y"
                     { (yyval.text) = kw_name("load"); }
#line 6595 "src/parser.tab.c"
    break;

  case 345: /* dot_field_name: USE  */
#line 2241 "src/parser.y"
                     { (yyval.text) = kw_name("use"); }
#line 6601 "src/parser.tab.c"
    break;

  case 346: /* dot_field_name: NOT  */
#line 2242 "src/parser.y"
                     { (yyval.text) = kw_name("not"); }
#line 6607 "src/parser.tab.c"
    break;

  case 347: /* dot_field_name: TRY  */
#line 2243 "src/parser.y"
                     { (yyval.text) = kw_name("try"); }
#line 6613 "src/parser.tab.c"
    break;

  case 348: /* dot_field_name: AND  */
#line 2244 "src/parser.y"
                     { (yyval.text) = kw_name("and"); }
#line 6619 "src/parser.tab.c"
    break;

  case 349: /* dot_field_name: OR  */
#line 2245 "src/parser.y"
                     { (yyval.text) = kw_name("or"); }
#line 6625 "src/parser.tab.c"
    break;

  case 350: /* dot_field_name: TRUE  */
#line 2246 "src/parser.y"
                     { (yyval.text) = kw_name("true"); }
#line 6631 "src/parser.tab.c"
    break;

  case 351: /* dot_field_name: FALSE  */
#line 2247 "src/parser.y"
                     { (yyval.text) = kw_name("false"); }
#line 6637 "src/parser.tab.c"
    break;

  case 352: /* dot_field_name: NOTHING  */
#line 2248 "src/parser.y"
                     { (yyval.text) = kw_name("nothing"); }
#line 6643 "src/parser.tab.c"
    break;

  case 353: /* dot_field_name: BREAK  */
#line 2249 "src/parser.y"
                     { (yyval.text) = kw_name("break"); }
#line 6649 "src/parser.tab.c"
    break;

  case 354: /* dot_field_name: CONTINUE  */
#line 2250 "src/parser.y"
                     { (yyval.text) = kw_name("continue"); }
#line 6655 "src/parser.tab.c"
    break;

  case 355: /* dot_field_name: GOTO  */
#line 2251 "src/parser.y"
                     { (yyval.text) = kw_name("goto"); }
#line 6661 "src/parser.tab.c"
    break;

  case 356: /* dot_field_name: GOSUB  */
#line 2252 "src/parser.y"
                     { (yyval.text) = kw_name("gosub"); }
#line 6667 "src/parser.tab.c"
    break;

  case 357: /* dot_field_name: SPAWN  */
#line 2253 "src/parser.y"
                     { (yyval.text) = kw_name("spawn"); }
#line 6673 "src/parser.tab.c"
    break;

  case 358: /* dot_field_name: EXPORT  */
#line 2254 "src/parser.y"
                     { (yyval.text) = kw_name("export"); }
#line 6679 "src/parser.tab.c"
    break;

  case 359: /* dot_field_name: LIBRARY  */
#line 2255 "src/parser.y"
                     { (yyval.text) = kw_name("library"); }
#line 6685 "src/parser.tab.c"
    break;

  case 360: /* dot_field_name: FUNCTION  */
#line 2256 "src/parser.y"
                     { (yyval.text) = kw_name("function"); }
#line 6691 "src/parser.tab.c"
    break;

  case 361: /* dot_field_name: MODIFIER  */
#line 2257 "src/parser.y"
                     { (yyval.text) = kw_name("modifier"); }
#line 6697 "src/parser.tab.c"
    break;

  case 362: /* dot_field_name: PROGRAM  */
#line 2258 "src/parser.y"
                     { (yyval.text) = kw_name("program"); }
#line 6703 "src/parser.tab.c"
    break;

  case 363: /* dot_field_name: WATCH  */
#line 2259 "src/parser.y"
                     { (yyval.text) = kw_name("watch"); }
#line 6709 "src/parser.tab.c"
    break;

  case 364: /* dot_field_name: WATCHERS  */
#line 2260 "src/parser.y"
                     { (yyval.text) = kw_name("watchers"); }
#line 6715 "src/parser.tab.c"
    break;

  case 365: /* dot_field_name: CONSIDER  */
#line 2261 "src/parser.y"
                     { (yyval.text) = kw_name("consider"); }
#line 6721 "src/parser.tab.c"
    break;

  case 366: /* dot_field_name: STEP  */
#line 2262 "src/parser.y"
                     { (yyval.text) = kw_name("step"); }
#line 6727 "src/parser.tab.c"
    break;

  case 367: /* dot_field_name: UNWATCH  */
#line 2263 "src/parser.y"
                     { (yyval.text) = kw_name("unwatch"); }
#line 6733 "src/parser.tab.c"
    break;

  case 368: /* dot_field_name: UNKNOWN_VALUE  */
#line 2264 "src/parser.y"
                     { (yyval.text) = kw_name("unknown"); }
#line 6739 "src/parser.tab.c"
    break;

  case 369: /* dot_field_name: DIM  */
#line 2265 "src/parser.y"
                     { (yyval.text) = kw_name("dim"); }
#line 6745 "src/parser.tab.c"
    break;

  case 370: /* record_field_list: field_name OP_EQ expression  */
#line 2269 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6751 "src/parser.tab.c"
    break;

  case 371: /* record_field_list: field_name COLON expression  */
#line 2270 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6757 "src/parser.tab.c"
    break;

  case 372: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 2271 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 6763 "src/parser.tab.c"
    break;

  case 373: /* record_field_list: record_field_list COMMA optional_newlines field_name OP_EQ expression  */
#line 2272 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6769 "src/parser.tab.c"
    break;

  case 374: /* record_field_list: record_field_list COMMA optional_newlines field_name COLON expression  */
#line 2273 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6775 "src/parser.tab.c"
    break;

  case 375: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 2274 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 6781 "src/parser.tab.c"
    break;

  case 376: /* field_policy: IDENT  */
#line 2282 "src/parser.y"
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
#line 6813 "src/parser.tab.c"
    break;

  case 377: /* field_policy: IDENT expression  */
#line 2309 "src/parser.y"
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
#line 6834 "src/parser.tab.c"
    break;


#line 6838 "src/parser.tab.c"

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

#line 2332 "src/parser.y"


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
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    const char *w = p;
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
