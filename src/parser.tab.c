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



#line 708 "src/parser.tab.c"

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
  YYSYMBOL_WITHOUT = 58,                   /* WITHOUT  */
  YYSYMBOL_WATCHERS = 59,                  /* WATCHERS  */
  YYSYMBOL_ON = 60,                        /* ON  */
  YYSYMBOL_NEXT = 61,                      /* NEXT  */
  YYSYMBOL_STOP = 62,                      /* STOP  */
  YYSYMBOL_ERROR_VALUE = 63,               /* ERROR_VALUE  */
  YYSYMBOL_MODIFIER = 64,                  /* MODIFIER  */
  YYSYMBOL_PROGRAM = 65,                   /* PROGRAM  */
  YYSYMBOL_LIBRARY = 66,                   /* LIBRARY  */
  YYSYMBOL_LOAD = 67,                      /* LOAD  */
  YYSYMBOL_USE = 68,                       /* USE  */
  YYSYMBOL_EXPORT = 69,                    /* EXPORT  */
  YYSYMBOL_OP_EQ = 70,                     /* OP_EQ  */
  YYSYMBOL_OP_NE = 71,                     /* OP_NE  */
  YYSYMBOL_OP_GT = 72,                     /* OP_GT  */
  YYSYMBOL_OP_LT = 73,                     /* OP_LT  */
  YYSYMBOL_OP_GE = 74,                     /* OP_GE  */
  YYSYMBOL_OP_LE = 75,                     /* OP_LE  */
  YYSYMBOL_OP_NGT = 76,                    /* OP_NGT  */
  YYSYMBOL_OP_NLT = 77,                    /* OP_NLT  */
  YYSYMBOL_OP_NGE = 78,                    /* OP_NGE  */
  YYSYMBOL_OP_NLE = 79,                    /* OP_NLE  */
  YYSYMBOL_PLUS = 80,                      /* PLUS  */
  YYSYMBOL_MINUS = 81,                     /* MINUS  */
  YYSYMBOL_STAR = 82,                      /* STAR  */
  YYSYMBOL_SLASH = 83,                     /* SLASH  */
  YYSYMBOL_LPAREN = 84,                    /* LPAREN  */
  YYSYMBOL_RPAREN = 85,                    /* RPAREN  */
  YYSYMBOL_LBRACKET = 86,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 87,                  /* RBRACKET  */
  YYSYMBOL_LBRACE = 88,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 89,                    /* RBRACE  */
  YYSYMBOL_COMMA = 90,                     /* COMMA  */
  YYSYMBOL_COLON = 91,                     /* COLON  */
  YYSYMBOL_NEWLINE = 92,                   /* NEWLINE  */
  YYSYMBOL_IF_WITHOUT_ELSE = 93,           /* IF_WITHOUT_ELSE  */
  YYSYMBOL_NO_DOT = 94,                    /* NO_DOT  */
  YYSYMBOL_DOT = 95,                       /* DOT  */
  YYSYMBOL_YYACCEPT = 96,                  /* $accept  */
  YYSYMBOL_program = 97,                   /* program  */
  YYSYMBOL_statement_list = 98,            /* statement_list  */
  YYSYMBOL_statement = 99,                 /* statement  */
  YYSYMBOL_assignment = 100,               /* assignment  */
  YYSYMBOL_type_lock = 101,                /* type_lock  */
  YYSYMBOL_compound_op = 102,              /* compound_op  */
  YYSYMBOL_lvalue = 103,                   /* lvalue  */
  YYSYMBOL_variable_name = 104,            /* variable_name  */
  YYSYMBOL_comparison_lens = 105,          /* comparison_lens  */
  YYSYMBOL_106_1 = 106,                    /* $@1  */
  YYSYMBOL_modifier_name = 107,            /* modifier_name  */
  YYSYMBOL_modifier_word = 108,            /* modifier_word  */
  YYSYMBOL_print_statement = 109,          /* print_statement  */
  YYSYMBOL_call_statement = 110,           /* call_statement  */
  YYSYMBOL_with_lock_statement = 111,      /* with_lock_statement  */
  YYSYMBOL_for_end = 112,                  /* for_end  */
  YYSYMBOL_for_each_statement = 113,       /* for_each_statement  */
  YYSYMBOL_do_loop_statement = 114,        /* do_loop_statement  */
  YYSYMBOL_while_statement = 115,          /* while_statement  */
  YYSYMBOL_consider_statement = 116,       /* consider_statement  */
  YYSYMBOL_consider_branch_list = 117,     /* consider_branch_list  */
  YYSYMBOL_consider_else_opt = 118,        /* consider_else_opt  */
  YYSYMBOL_consider_statement_list = 119,  /* consider_statement_list  */
  YYSYMBOL_consider_body_statement = 120,  /* consider_body_statement  */
  YYSYMBOL_function_statement = 121,       /* function_statement  */
  YYSYMBOL_modifier_statement = 122,       /* modifier_statement  */
  YYSYMBOL_program_statement = 123,        /* program_statement  */
  YYSYMBOL_library_statement = 124,        /* library_statement  */
  YYSYMBOL_use_statement = 125,            /* use_statement  */
  YYSYMBOL_modifier_signature = 126,       /* modifier_signature  */
  YYSYMBOL_modifier_context = 127,         /* modifier_context  */
  YYSYMBOL_watch_statement = 128,          /* watch_statement  */
  YYSYMBOL_unwatch_statement = 129,        /* unwatch_statement  */
  YYSYMBOL_watch_target_list = 130,        /* watch_target_list  */
  YYSYMBOL_server_statement = 131,         /* server_statement  */
  YYSYMBOL_132_2 = 132,                    /* @2  */
  YYSYMBOL_133_3 = 133,                    /* @3  */
  YYSYMBOL_gsql_block = 134,               /* gsql_block  */
  YYSYMBOL_window_block = 135,             /* window_block  */
  YYSYMBOL_window_item_list = 136,         /* window_item_list  */
  YYSYMBOL_window_item = 137,              /* window_item  */
  YYSYMBOL_server_item_list = 138,         /* server_item_list  */
  YYSYMBOL_server_item = 139,              /* server_item  */
  YYSYMBOL_server_string_list = 140,       /* server_string_list  */
  YYSYMBOL_watch_target_path = 141,        /* watch_target_path  */
  YYSYMBOL_without_watchers_statement = 142, /* without_watchers_statement  */
  YYSYMBOL_on_error_statement = 143,       /* on_error_statement  */
  YYSYMBOL_error_statement = 144,          /* error_statement  */
  YYSYMBOL_return_statement = 145,         /* return_statement  */
  YYSYMBOL_label_statement = 146,          /* label_statement  */
  YYSYMBOL_goto_statement = 147,           /* goto_statement  */
  YYSYMBOL_gosub_statement = 148,          /* gosub_statement  */
  YYSYMBOL_break_statement = 149,          /* break_statement  */
  YYSYMBOL_continue_statement = 150,       /* continue_statement  */
  YYSYMBOL_if_statement = 151,             /* if_statement  */
  YYSYMBOL_if_block_tail = 152,            /* if_block_tail  */
  YYSYMBOL_if_inline_tail = 153,           /* if_inline_tail  */
  YYSYMBOL_inline_statement = 154,         /* inline_statement  */
  YYSYMBOL_expression = 155,               /* expression  */
  YYSYMBOL_or_expression = 156,            /* or_expression  */
  YYSYMBOL_and_expression = 157,           /* and_expression  */
  YYSYMBOL_not_expression = 158,           /* not_expression  */
  YYSYMBOL_comparison_expression = 159,    /* comparison_expression  */
  YYSYMBOL_set_expression = 160,           /* set_expression  */
  YYSYMBOL_additive_expression = 161,      /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 162, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 163,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 164,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 165,      /* comparison_operator  */
  YYSYMBOL_primary = 166,                  /* primary  */
  YYSYMBOL_record_literal = 167,           /* record_literal  */
  YYSYMBOL_ident_suffix = 168,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 169,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 170,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 171,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 172,            /* argument_list  */
  YYSYMBOL_array_argument_list = 173,      /* array_argument_list  */
  YYSYMBOL_parameter_list_opt = 174,       /* parameter_list_opt  */
  YYSYMBOL_parameter_default = 175,        /* parameter_default  */
  YYSYMBOL_parameter_list = 176,           /* parameter_list  */
  YYSYMBOL_field_name = 177,               /* field_name  */
  YYSYMBOL_dot_field_name = 178,           /* dot_field_name  */
  YYSYMBOL_record_field_list = 179,        /* record_field_list  */
  YYSYMBOL_field_policy = 180,             /* field_policy  */
  YYSYMBOL_optional_newlines = 181         /* optional_newlines  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;



/* Unqualified %code blocks.  */
#line 723 "src/parser.y"

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

#line 1013 "src/parser.tab.c"

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
#define YYLAST   3580

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  96
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  86
/* YYNRULES -- Number of rules.  */
#define YYNRULES  369
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  861

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   350


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
      95
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   895,   895,   899,   900,   901,   905,   906,   907,   908,
     909,   910,   911,   912,   913,   914,   915,   916,   917,   918,
     919,   920,   921,   922,   923,   924,   925,   926,   927,   928,
     929,   930,   931,   932,   938,   949,   954,   955,   971,   978,
     992,   993,   997,   998,   999,  1000,  1004,  1005,  1006,  1010,
    1011,  1012,  1023,  1023,  1031,  1035,  1036,  1040,  1041,  1042,
    1043,  1047,  1053,  1057,  1060,  1061,  1067,  1072,  1081,  1094,
    1123,  1124,  1125,  1129,  1133,  1149,  1154,  1162,  1166,  1187,
    1193,  1199,  1205,  1208,  1214,  1215,  1219,  1220,  1221,  1225,
    1226,  1227,  1228,  1229,  1230,  1231,  1232,  1233,  1234,  1235,
    1236,  1237,  1238,  1239,  1240,  1241,  1242,  1243,  1244,  1245,
    1246,  1247,  1248,  1249,  1250,  1256,  1267,  1270,  1277,  1286,
    1298,  1301,  1307,  1313,  1319,  1320,  1321,  1322,  1323,  1324,
    1340,  1356,  1377,  1378,  1382,  1386,  1389,  1397,  1403,  1407,
    1408,  1428,  1427,  1435,  1434,  1457,  1460,  1463,  1469,  1484,
    1488,  1498,  1506,  1507,  1508,  1513,  1518,  1522,  1527,  1532,
    1546,  1561,  1566,  1577,  1592,  1597,  1603,  1604,  1605,  1609,
    1612,  1615,  1618,  1621,  1627,  1628,  1632,  1633,  1637,  1643,
    1644,  1645,  1646,  1651,  1664,  1669,  1674,  1684,  1688,  1689,
    1693,  1700,  1704,  1713,  1714,  1718,  1719,  1723,  1727,  1734,
    1737,  1740,  1749,  1759,  1762,  1765,  1771,  1778,  1788,  1789,
    1790,  1791,  1792,  1793,  1794,  1795,  1796,  1797,  1798,  1806,
    1807,  1811,  1812,  1816,  1817,  1839,  1840,  1844,  1845,  1846,
    1852,  1853,  1854,  1858,  1859,  1860,  1864,  1865,  1866,  1870,
    1871,  1878,  1882,  1883,  1884,  1888,  1889,  1890,  1891,  1896,
    1910,  1911,  1912,  1913,  1914,  1915,  1916,  1917,  1918,  1919,
    1923,  1941,  1942,  1943,  1944,  1945,  1962,  1968,  1969,  1970,
    1971,  1972,  1973,  1974,  1975,  1976,  1980,  1981,  1985,  1990,
    1995,  2001,  2013,  2018,  2026,  2036,  2048,  2049,  2053,  2054,
    2058,  2059,  2063,  2064,  2078,  2082,  2086,  2087,  2088,  2089,
    2090,  2091,  2095,  2096,  2099,  2103,  2110,  2117,  2124,  2131,
    2146,  2153,  2162,  2163,  2164,  2165,  2166,  2167,  2168,  2169,
    2170,  2171,  2172,  2173,  2174,  2175,  2176,  2177,  2178,  2179,
    2180,  2181,  2182,  2183,  2184,  2185,  2186,  2187,  2188,  2189,
    2190,  2191,  2192,  2193,  2194,  2195,  2196,  2197,  2198,  2199,
    2200,  2201,  2202,  2203,  2204,  2205,  2206,  2207,  2208,  2209,
    2213,  2214,  2215,  2216,  2217,  2218,  2226,  2253,  2272,  2273
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
  "UNWATCH", "WITHOUT", "WATCHERS", "ON", "NEXT", "STOP", "ERROR_VALUE",
  "MODIFIER", "PROGRAM", "LIBRARY", "LOAD", "USE", "EXPORT", "OP_EQ",
  "OP_NE", "OP_GT", "OP_LT", "OP_GE", "OP_LE", "OP_NGT", "OP_NLT",
  "OP_NGE", "OP_NLE", "PLUS", "MINUS", "STAR", "SLASH", "LPAREN", "RPAREN",
  "LBRACKET", "RBRACKET", "LBRACE", "RBRACE", "COMMA", "COLON", "NEWLINE",
  "IF_WITHOUT_ELSE", "NO_DOT", "DOT", "$accept", "program",
  "statement_list", "statement", "assignment", "type_lock", "compound_op",
  "lvalue", "variable_name", "comparison_lens", "$@1", "modifier_name",
  "modifier_word", "print_statement", "call_statement",
  "with_lock_statement", "for_end", "for_each_statement",
  "do_loop_statement", "while_statement", "consider_statement",
  "consider_branch_list", "consider_else_opt", "consider_statement_list",
  "consider_body_statement", "function_statement", "modifier_statement",
  "program_statement", "library_statement", "use_statement",
  "modifier_signature", "modifier_context", "watch_statement",
  "unwatch_statement", "watch_target_list", "server_statement", "@2", "@3",
  "gsql_block", "window_block", "window_item_list", "window_item",
  "server_item_list", "server_item", "server_string_list",
  "watch_target_path", "without_watchers_statement", "on_error_statement",
  "error_statement", "return_statement", "label_statement",
  "goto_statement", "gosub_statement", "break_statement",
  "continue_statement", "if_statement", "if_block_tail", "if_inline_tail",
  "inline_statement", "expression", "or_expression", "and_expression",
  "not_expression", "comparison_expression", "set_expression",
  "additive_expression", "multiplicative_expression", "unary_expression",
  "postfix_expression", "comparison_operator", "primary", "record_literal",
  "ident_suffix", "ident_dot_suffix", "duration_terms",
  "argument_list_opt", "argument_list", "array_argument_list",
  "parameter_list_opt", "parameter_default", "parameter_list",
  "field_name", "dot_field_name", "record_field_list", "field_policy",
  "optional_newlines", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-661)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -661,    92,  1155,  -661,    73,    81,    75,   211,   255,  -661,
    2756,  -661,  2716,  2756,   259,    79,   107,  2756,  2756,   272,
     283,   270,  2756,    67,    67,    66,  2756,   146,    35,  -661,
     616,    60,   298,   310,   321,   348,   254,  -661,  -661,   236,
     367,   252,   258,   262,  -661,  -661,  -661,  -661,  -661,  -661,
    -661,  -661,  -661,   268,  -661,   277,  -661,  -661,  -661,  -661,
     282,   289,   290,   297,   299,   301,   302,   303,  -661,   263,
    2756,  2756,   312,   315,   342,   400,  -661,  -661,   322,  2856,
    -661,  -661,  -661,  -661,  -661,  2818,  2756,  2918,   404,   327,
    -661,  2856,  2756,  -661,  -661,   159,   388,   378,   380,  -661,
    -661,   590,   278,   280,  -661,    86,  -661,  -661,   415,   356,
    -661,  -661,   337,   134,   418,  -661,   333,   334,  -661,  -661,
     345,   347,  -661,  -661,  -661,   350,    67,  -661,   138,   340,
    -661,   349,   160,   149,   435,  -661,  -661,  -661,  -661,  -661,
     237,  -661,   399,   360,   355,   163,  -661,   445,  -661,    60,
    -661,  -661,  -661,  -661,  -661,  -661,  2756,  2756,  -661,  3379,
    2756,   253,   446,  -661,  -661,  -661,  -661,  -661,  -661,  -661,
    -661,  -661,  -661,  -661,  -661,  2983,  -661,   366,   362,   372,
    3049,  3115,   454,  -661,  2756,  -661,  -661,  -661,    87,   377,
     379,  -661,   382,   720,   891,  2756,  3445,  -661,  2642,  2818,
    2818,  2856,  2856,  -661,  -661,  -661,  -661,  -661,  -661,  -661,
    -661,  -661,  -661,   756,  2856,  2856,  2856,  2856,  2856,  2756,
    3511,   459,  2756,  2756,  2756,  2756,   461,    50,  1023,  -661,
     451,   464,   464,    67,    -5,    67,  -661,   473,  -661,  -661,
    -661,    49,  -661,    77,  -661,   394,   464,  -661,   475,   464,
    -661,   476,   481,   482,   439,  -661,   402,   480,   406,   407,
    -661,  -661,  -661,  -661,  -661,  -661,  -661,  -661,  -661,  -661,
    -661,  -661,  -661,  -661,  -661,  -661,  -661,  -661,  -661,  -661,
    -661,  -661,  -661,  -661,  -661,  -661,  -661,  -661,  -661,  -661,
    -661,  -661,  -661,  -661,  -661,  -661,  -661,  -661,  -661,  -661,
    -661,  -661,  -661,  -661,  -661,  -661,  -661,  -661,  -661,  2756,
    2756,   408,   423,   410,  -661,   405,    18,  -661,    14,  -661,
    2756,  -661,   409,   123,   412,   193,   204,   411,   417,  2756,
    -661,  -661,  -661,  -661,  -661,   419,  -661,   420,   414,  -661,
     416,   424,   427,  -661,  -661,   421,  -661,  -661,  -661,  -661,
    -661,  -661,  -661,  -661,  -661,  -661,   422,   380,  -661,   278,
     278,  2856,   344,   280,   280,  -661,  -661,   428,   432,   441,
    -661,  -661,  -661,   434,   431,   465,   486,  2756,   522,  2756,
    1221,  2756,   287,   157,   443,   449,   450,   195,   448,   340,
    1287,  -661,  1353,  -661,  -661,  -661,  -661,  2756,   462,  -661,
     452,   463,  1419,   532,  -661,  -661,   475,  -661,   460,  2756,
    2756,  -661,  -661,   551,  2756,   552,  -661,  2756,  2756,   467,
    -661,  -661,  -661,   118,  -661,   468,   544,   557,  -661,  -661,
     487,  -661,   197,   230,  -661,  2756,  2756,  -661,  1089,   446,
     541,   344,  -661,  2756,  2756,   483,  -661,  2756,  2756,   485,
     527,   488,   526,   554,  2756,   492,   558,   760,   446,   275,
     575,   279,   493,  -661,   533,   530,   508,  -661,  -661,   503,
     534,   592,   509,  -661,   517,   519,   520,  -661,  2756,   521,
    -661,  -661,  -661,  -661,   957,    45,   608,  -661,  -661,   586,
     523,  -661,   816,  -661,  -661,   529,   542,   681,   596,  -661,
    2607,  -661,   543,   546,  -661,  1485,   128,   537,  -661,  2756,
    -661,   540,   549,   611,  -661,   550,  -661,  -661,  -661,  -661,
    -661,  -661,   633,   641,  -661,   580,   446,  -661,   169,   446,
    -661,  -661,  1551,   553,   559,  -661,  1617,  -661,   562,  -661,
    -661,  -661,  -661,  -661,  -661,   567,    57,  -661,   587,   176,
      64,   666,   668,  -661,  -661,   669,    70,   670,   663,  -661,
    -661,  -661,  2756,  -661,   588,   589,  2756,  -661,   591,  -661,
    -661,  1683,   643,   115,  -661,  2756,  -661,  -661,  1485,   594,
    -661,  -661,   595,  2541,  -661,  -661,  -661,   760,   598,  1749,
     760,   446,   599,  1815,  1881,   626,  -661,  -661,   628,  1947,
    -661,  2013,  2756,   368,   689,   690,  -661,  -661,    74,   552,
    2756,  2756,    61,   603,   692,  3181,  -661,   606,   604,   609,
     701,   614,   682,   688,  2079,  -661,  -661,   693,  2145,  -661,
     675,   622,  -661,   627,   629,  1485,  1485,  -661,  -661,  2541,
    -661,  -661,  -661,   634,   645,   646,  -661,  -661,  -661,  -661,
    -661,  -661,  -661,  -661,  -661,  -661,   647,  -661,   648,  -661,
    -661,   651,   653,   654,   662,   672,   674,   677,   678,  -661,
    -661,  -661,   703,  -661,   650,  -661,   706,   705,   679,   680,
     697,   710,  -661,   683,   691,   203,   684,   685,   774,   695,
    -661,  -661,   694,   780,  3247,  -661,  -661,   781,   696,   201,
      91,  -661,   783,  -661,   702,  -661,   785,   704,   773,  2677,
     776,   708,  -661,  -661,  -661,  -661,  -661,  1485,  2541,  -661,
    -661,  -661,  -661,  -661,  -661,  -661,  -661,  -661,  -661,  -661,
    -661,  -661,  2211,   711,   760,  2277,   713,   717,  -661,  -661,
     718,   719,  3313,   464,   797,  -661,  -661,  -661,   721,   724,
    -661,   812,   725,   218,   174,   189,   730,  -661,   744,   833,
     746,   196,  -661,   750,  -661,   752,  -661,   753,   758,  -661,
    1485,  -661,   799,  -661,  -661,   802,  -661,  -661,  -661,  -661,
     765,   226,   775,  -661,  2343,  -661,  2756,   198,   766,  -661,
     767,   857,   858,  -661,  -661,   771,  -661,   860,  -661,  1089,
    -661,  1089,   541,  -661,  -661,   777,   778,  -661,   779,   782,
     805,  -661,   862,  -661,  -661,   784,   786,   202,   205,  -661,
    2409,   788,  -661,  -661,  -661,  -661,  -661,   208,  -661,  -661,
     789,   790,   213,  -661,  -661,   863,   864,   813,  -661,   868,
     215,  2475,  -661,  -661,   879,   792,   793,   794,   795,   884,
     885,   798,  -661,  -661,  -661,  -661,   800,   801,  -661,  -661,
    -661
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       3,     0,     2,     1,    49,     0,     0,     0,     0,    34,
       0,    50,     0,     0,     0,     0,     0,     0,     0,   193,
     195,     0,   188,     0,     0,     0,     0,     0,     0,    51,
       0,     0,     0,     0,     0,     0,     0,     4,     5,     0,
       0,    46,     0,     0,     9,    12,    14,    13,    15,    16,
      17,    18,    19,     0,    21,     0,    22,    11,    10,    24,
       0,     0,     0,     0,     0,     0,     0,     0,    33,     0,
     286,   286,     0,     0,     0,   261,    49,   264,     0,     0,
     260,   268,   269,   270,   271,     0,     0,     0,     0,     0,
     267,     0,     0,   368,   368,   278,     0,   219,   221,   223,
     225,   227,   230,   233,   236,   239,   245,   275,   263,     0,
      61,    64,     0,     0,     0,     3,     0,     0,   194,   196,
       0,     0,   189,   191,   192,    49,     0,   176,     0,   139,
     138,     0,     0,     0,     0,   187,    57,    59,    58,    60,
     132,    55,     0,     0,     0,   125,   127,   124,   126,     0,
       6,    54,    42,    43,    44,    45,     0,     0,    52,     0,
       0,     0,   190,     7,     8,    20,    23,    25,    26,    27,
      28,    29,    30,    31,    32,     0,   288,     0,   287,     0,
       0,     0,     0,   284,   286,   241,   226,   220,   242,     0,
       0,   240,     0,     0,     0,   286,     0,   265,     0,     0,
       0,     0,     0,   250,   251,   252,   253,   254,   255,   256,
     257,   258,   259,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     3,
       0,   292,   292,     0,     0,     0,     3,     0,     3,   186,
     185,     0,   184,     0,   181,     0,   292,    56,     0,   292,
       3,     0,     0,     0,     0,    35,     0,     0,   312,     0,
     313,   359,   328,   325,   326,   317,   332,   340,   341,   342,
     358,   338,   339,   336,   337,   323,   321,   347,   327,   318,
     356,   330,   331,   319,   322,   329,   355,   343,   344,   350,
     333,   345,   346,   353,   357,   324,   354,   320,   314,   315,
     316,   351,   352,   349,   334,   335,   348,    48,    36,     0,
       0,    40,     0,   312,   311,     0,     0,   310,     0,    63,
       0,    65,     0,     0,     0,     0,     0,     0,     0,   286,
     262,   272,   273,   369,   290,   368,   276,   368,     0,   312,
       0,   282,    49,     3,   208,    46,   209,   210,   211,   212,
     213,   214,   215,   216,   217,   218,     0,   222,   224,   231,
     232,     0,   228,   234,   235,   237,   238,     0,   312,     0,
     247,   285,    62,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    84,   302,     0,   293,     0,     0,     0,   140,
       0,   177,     0,   183,   182,   179,   180,   286,     0,   134,
       0,     0,     0,   130,   128,   129,     0,    47,     0,   286,
     286,    39,    37,     0,     0,     0,   143,     0,     0,     0,
     368,   289,   152,     0,   146,     0,     0,     0,   266,   243,
       0,   368,     0,     0,   279,   286,   286,   280,     0,     0,
     203,   229,   246,   286,   286,     0,     3,     0,     0,     0,
       0,     0,    50,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     3,    50,    50,     0,   133,     3,     0,
      50,     0,     0,    53,     0,     0,     0,    38,   366,     0,
     166,   360,   361,   141,     0,     0,     0,   152,   145,     0,
       0,   244,     0,   274,   277,     0,     0,     0,    50,   197,
       0,   198,     0,     0,     3,     0,     0,     0,     3,     0,
      79,     0,     0,     0,    86,     0,   294,   297,   298,   299,
     300,   301,     0,     0,   303,   305,     0,     3,   304,     0,
       3,     3,     0,     0,     0,    68,     0,     3,     0,   131,
       3,    66,    67,    41,   367,     0,     0,   166,   312,     0,
       0,     0,     0,   153,   154,     0,     0,     0,     0,   291,
     281,   283,     0,     3,     0,     0,     0,     3,     0,   248,
     249,     0,    50,    51,    73,     0,     3,     3,     0,     0,
      80,    86,     0,    85,    81,   296,   295,     0,     0,     0,
       0,     0,     0,     0,     0,    50,   136,   178,    50,     0,
     123,     0,     0,     0,     0,     0,   167,   168,     0,     0,
       0,     0,     0,     0,     0,     0,   152,     0,     0,     0,
       0,     0,     0,     0,     0,   200,   199,     0,     0,   204,
      50,     0,    71,     0,     0,     0,     0,    74,     3,    82,
      86,   115,    87,     0,     0,     0,    92,    94,    96,    95,
      97,    88,    98,    99,   100,   101,     0,   103,     0,    93,
     105,     0,     0,     0,     0,     0,     0,     0,     0,   114,
     306,     3,    50,   309,   307,     3,    50,    50,     0,     0,
      50,    50,   362,     0,   174,     0,     0,     0,     0,     0,
     363,   364,     0,     0,     0,   152,   152,     0,     0,     0,
       0,   150,     0,   152,     0,   147,     0,     0,    50,     0,
      50,     0,    70,    72,     3,    77,    75,     0,    83,    89,
      90,    91,   102,   104,   106,   107,   108,   109,   110,   111,
     112,   113,     0,     0,     0,     0,     0,     0,   135,   120,
       0,     0,     0,   292,     0,   169,   144,     3,     0,     0,
     152,     0,     0,     0,     0,     0,     0,   155,     0,     0,
       0,     0,   149,     0,     3,     0,     3,     0,     0,    69,
       0,    76,    50,   116,   308,    50,   117,   137,   122,   121,
       0,     0,     0,   175,     0,   142,     0,     0,     0,   157,
       0,     0,     0,   152,   152,     0,     3,     0,   148,     0,
     201,     0,   203,   205,    78,     0,     0,   166,     0,     0,
      50,   365,     0,   152,   156,     0,     0,     0,     0,   158,
       0,     0,   202,   207,   206,   118,   119,     0,   166,     3,
       0,     0,     0,   160,   162,     0,     0,    50,   151,     0,
       0,     0,   173,   161,     0,     0,     0,     0,     0,     0,
      50,     0,   163,   159,   165,   172,     0,     0,   164,   171,
     170
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -661,  -661,  -107,  -661,  -196,  -393,   733,  -661,    -2,   806,
    -661,  -661,   751,  -191,  -185,  -572,  -573,  -564,  -563,  -557,
    -550,  -661,  -661,  -508,  -661,  -549,  -548,  -539,  -532,  -180,
     749,   495,  -531,  -528,   -98,  -661,  -661,  -661,  -661,  -527,
    -458,  -661,  -546,  -661,  -661,   664,  -526,  -173,  -168,  -167,
    -524,  -166,  -156,  -155,  -151,  -523,  -660,   101,  -491,    28,
    -661,   712,   -48,  -661,  -198,   175,   164,   -43,   822,   699,
    -661,   582,  -661,  -661,  -661,   173,  -661,  -661,  -222,  -575,
    -661,   430,   -73,  -177,   308,   -80
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    38,    39,   312,   160,    40,    95,   161,
     257,   140,   141,    42,    43,    44,   574,    45,    46,    47,
      48,   382,   456,   583,   651,    49,    50,    51,    52,    53,
     142,   400,    54,    55,   128,    56,   547,   480,    57,    58,
     485,   554,   546,   607,   685,   129,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,   499,   501,   356,   176,
      97,    98,    99,   100,   101,   102,   103,   104,   105,   214,
     106,   107,   197,   437,   108,   177,   178,   335,   384,   524,
     385,   316,   317,   318,   479,   193
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      41,   608,   344,   323,   325,   637,   564,   346,   228,   568,
     386,   646,   670,   347,   194,   673,   362,   337,   348,   647,
     648,   123,   124,   127,   398,   349,   649,   401,   234,   556,
     350,   351,   352,   650,   652,   653,   185,   186,    96,   132,
     110,   111,   353,   354,   654,   116,   117,   355,   191,   550,
     122,   655,   657,   393,   130,   658,   659,   660,   135,   664,
     669,   603,   715,   716,   136,   525,   692,   646,   612,   613,
     125,    76,   551,   639,   550,   647,   648,    69,   603,    72,
     388,   395,   649,   113,   604,   235,   307,   137,   417,   650,
     652,   653,     3,    11,    11,   550,   377,   620,   133,   419,
     654,   688,   138,   693,   420,   552,   614,   655,   657,   418,
     394,   658,   659,   660,   187,   664,   669,   605,   759,    76,
     192,   139,   380,   341,   127,   328,   114,    29,    29,   390,
     552,   392,   718,   588,   605,   387,   592,   553,   396,   822,
     378,   823,    11,   402,   771,   694,   646,   370,   615,   606,
     126,   552,   358,   695,   647,   648,   616,    70,   700,   774,
     486,   649,   553,   441,   239,    71,   606,   251,   650,   652,
     653,   575,   219,   219,   365,   366,    29,   252,   550,   654,
     224,   220,   220,   553,   255,   256,   655,   657,   308,   240,
     658,   659,   660,   550,   664,   669,   345,   804,   674,   115,
     550,   791,   550,   243,   225,   131,   550,   632,   423,   550,
     487,   244,   603,   420,   241,    73,   792,   550,   767,   603,
     576,   334,   242,   797,   226,   812,    41,   457,   235,   835,
     236,   127,   836,   127,   552,   839,   438,   754,   755,   590,
     844,   136,   849,   195,   179,   761,   610,   367,   458,   552,
     372,   373,   374,   375,   196,   432,   552,   433,   552,    74,
     591,   827,   552,   112,   137,   552,   553,   611,   605,   152,
     153,   154,   155,   552,   120,   605,   118,   121,   425,   138,
     462,   553,   840,   420,   493,   235,   758,   119,   553,   333,
     553,   420,   787,   744,   553,   745,   426,   553,   139,   427,
     606,   344,   143,   790,   344,   553,   346,   606,   420,   346,
     454,   808,   347,   455,   144,   347,   420,   348,   149,   494,
     348,   246,   333,   309,   349,   145,   146,   349,   150,   350,
     351,   352,   350,   351,   352,   817,   818,   411,   412,   505,
     484,   353,   354,   162,   353,   354,   355,   175,   421,   355,
     163,   492,   147,   148,   164,   832,   532,   327,   215,   216,
     165,   536,   217,   218,   201,   202,   526,   527,   338,   166,
     529,   530,   683,   684,   167,   151,   359,   360,    41,   363,
     364,   168,   169,   152,   153,   154,   155,   643,    41,   170,
      41,   171,   644,   172,   173,   174,   180,   571,   645,   181,
      41,   578,   182,   656,   183,   449,   184,   451,   189,   453,
     661,   190,   198,   199,   200,   662,   663,   665,   221,   222,
     589,   223,   227,   593,   594,   229,   230,   666,   667,   231,
     599,   232,   668,   601,   233,   237,    41,   156,   699,   245,
     248,   238,   477,   643,   249,   481,   482,   250,   644,   253,
     311,   319,   320,   157,   645,   158,   624,   321,   326,   656,
     628,   329,   159,   371,   330,   376,   661,   331,   383,   635,
     636,   662,   663,   665,   381,   506,   507,   391,   397,   399,
     406,   403,   513,   666,   667,   404,   408,   405,   668,   407,
     409,   410,   413,   414,   415,   345,   428,   416,   345,   434,
     435,   422,   430,    41,   424,    94,   544,   447,   436,   431,
     420,    70,   439,   344,   440,   442,   443,   753,   346,   445,
     559,   782,   643,   446,   347,   444,   450,   644,   459,   348,
      41,   717,   448,   645,    41,   461,   349,   579,   656,   460,
     463,   350,   351,   352,   468,   661,   471,   467,   469,   473,
     662,   663,   665,   353,   354,   476,   478,   489,   355,   483,
     488,   490,   666,   667,   732,   781,   500,   668,   735,    41,
     466,   633,   491,   509,   511,   504,    41,   508,   512,   528,
     510,    41,   474,   475,   514,   531,   515,    41,   534,   533,
     623,    41,    41,   535,   627,   537,   539,    41,   151,    41,
     538,   540,   541,   634,   542,   543,   545,   770,   495,   496,
     201,   202,   555,   557,   560,   558,   502,   503,   565,    75,
      76,    77,    41,    78,    79,    80,    41,   561,   569,   577,
     682,   570,   580,    41,    41,   582,   585,    41,   690,   691,
     784,   581,   584,    11,   586,   596,    81,    82,    83,    84,
     587,   597,    85,    86,   600,    87,    88,   799,   602,   801,
     203,   204,   205,   206,   207,   208,   209,   210,   211,   212,
     617,   609,   618,   619,   621,    89,   622,    29,   158,    90,
     625,   626,   678,   629,   631,   342,   638,   640,     5,   820,
     671,   675,   679,   686,   687,   696,   697,    91,   701,   702,
      92,   703,    93,   562,    94,   704,   705,   345,    11,   706,
      12,   134,   707,   711,   712,    41,    41,   709,    13,   713,
     734,   714,   841,    75,    76,    77,   719,    78,    79,    80,
      41,    19,    20,    41,    22,    23,    24,   720,   721,   722,
     723,    28,    29,   724,    30,   725,   726,    11,    34,    35,
      81,    82,    83,    84,   727,   733,    85,    86,   736,    87,
      88,   737,   740,   516,   728,   517,   729,   742,    41,   730,
     731,   738,   739,   563,   741,   743,   746,   747,   748,    89,
     749,    29,    41,    90,   751,   756,   750,   760,   757,   763,
     518,   519,   520,   521,   762,   765,   764,    41,   768,    41,
     769,    91,   783,   773,    92,   776,    93,   332,    94,   777,
     778,   779,   333,   785,   811,   786,   788,   789,    41,    75,
      76,    77,   793,    78,    79,    80,   203,   204,   205,   206,
     207,   208,   209,   210,   211,   212,   794,   795,   796,    41,
     522,   523,   798,    11,   800,   802,    81,    82,    83,    84,
     803,   805,    85,    86,   806,    87,    88,   807,   813,   814,
     809,   815,   816,   819,   821,   830,   831,   845,   846,   825,
     826,   828,   848,   847,   829,    89,   833,    29,   834,    90,
     838,   842,   843,   851,   852,   853,   854,   855,   856,   857,
     858,   247,   859,   860,   310,   313,   314,    91,   254,   389,
      92,   472,    93,   824,    94,   260,   261,   213,   333,   188,
     429,   357,   361,   262,   549,   263,   264,   689,   265,     0,
     266,   267,   268,   269,   270,   271,   272,   273,   274,   275,
     276,   277,   278,   279,   280,   281,   282,   283,   284,   285,
     286,   287,   288,   289,   290,   291,   292,   293,   294,   295,
     296,   297,   298,   299,   300,   301,   302,   303,   304,   305,
     306,   548,   314,     0,     0,     0,     0,     0,     0,     0,
       0,   260,   261,     0,     0,     0,     0,     0,     0,   262,
     336,   263,   264,   333,   265,     0,   266,   267,   268,   269,
     270,   271,   272,   273,   274,   275,   276,   277,   278,   279,
     280,   281,   282,   283,   284,   285,   286,   287,   288,   289,
     290,   291,   292,   293,   294,   295,   296,   297,   298,   299,
     300,   301,   302,   303,   304,   305,   306,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,   333,
      11,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,    14,     0,     0,    15,     0,     0,    16,   379,     0,
       0,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,     0,    28,    29,     0,    30,    31,    32,    33,
      34,    35,    36,     4,     0,     0,     5,     0,     0,     6,
       7,     8,     0,     0,     9,     0,     0,     0,     0,     0,
       0,    10,     0,     0,   497,    37,   498,     0,    12,     0,
       0,     0,     0,     0,     0,     0,    13,    14,     0,     0,
      15,     0,     0,    16,     0,     0,     0,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,     0,    28,
      29,     0,    30,    31,    32,    33,    34,    35,    36,     4,
       0,     0,     5,     0,     0,     6,     7,     8,     0,     0,
       9,     0,     0,     0,     0,     0,     0,    10,     0,     0,
       0,    37,    11,     0,    12,     0,     0,     0,     0,     0,
       0,     0,    13,    14,     0,     0,    15,     0,     0,    16,
       0,     0,     0,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,     0,    28,    29,     0,    30,    31,
      32,    33,    34,    35,    36,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    37,   452,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,    14,
       0,     0,    15,     0,     0,    16,     0,     0,     0,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
       0,    28,    29,     0,    30,    31,    32,    33,    34,    35,
      36,     4,     0,     0,     5,     0,     0,     6,     7,     8,
       0,     0,     9,     0,     0,     0,     0,     0,     0,    10,
       0,     0,     0,    37,   464,     0,    12,     0,     0,     0,
       0,     0,     0,     0,    13,    14,     0,     0,    15,     0,
       0,    16,     0,     0,     0,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,     0,    28,    29,     0,
      30,    31,    32,    33,    34,    35,    36,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    37,
     465,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,    14,     0,     0,    15,     0,     0,    16,     0,     0,
       0,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,     0,    28,    29,     0,    30,    31,    32,    33,
      34,    35,    36,     4,     0,     0,     5,     0,     0,     6,
       7,     8,     0,     0,     9,     0,     0,     0,     0,     0,
       0,    10,     0,     0,     0,    37,   470,     0,    12,     0,
       0,     0,     0,     0,     0,     0,    13,    14,     0,     0,
      15,     0,     0,    16,     0,     0,     0,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,     0,    28,
      29,     0,    30,    31,    32,    33,    34,    35,    36,     4,
       0,     0,     5,     0,     0,     6,     7,     8,     0,     0,
       9,     0,     0,     0,     0,     0,     0,    10,     0,     0,
       0,    37,   572,     0,    12,     0,     0,     0,     0,     0,
       0,     0,    13,    14,     0,     0,    15,     0,     0,    16,
       0,     0,     0,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,     0,    28,   573,     0,    30,    31,
      32,    33,    34,    35,    36,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    37,   595,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,    14,
       0,     0,    15,     0,     0,    16,     0,     0,     0,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
       0,    28,    29,     0,    30,    31,    32,    33,    34,    35,
      36,     4,     0,     0,     5,     0,     0,     6,     7,     8,
       0,     0,     9,     0,     0,     0,     0,     0,     0,    10,
       0,     0,     0,    37,   598,     0,    12,     0,     0,     0,
       0,     0,     0,     0,    13,    14,     0,     0,    15,     0,
       0,    16,     0,     0,     0,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,     0,    28,    29,     0,
      30,    31,    32,    33,    34,    35,    36,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    37,
     630,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,    14,     0,     0,    15,     0,     0,    16,     0,     0,
       0,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,     0,    28,    29,     0,    30,    31,    32,    33,
      34,    35,    36,     4,     0,     0,     5,     0,     0,     6,
       7,     8,     0,     0,     9,     0,     0,     0,     0,     0,
       0,    10,     0,     0,     0,    37,   672,     0,    12,     0,
       0,     0,     0,     0,     0,     0,    13,    14,     0,     0,
      15,     0,     0,    16,     0,     0,     0,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,     0,    28,
      29,     0,    30,    31,    32,    33,    34,    35,    36,     4,
       0,     0,     5,     0,     0,     6,     7,     8,     0,     0,
       9,     0,     0,     0,     0,     0,     0,    10,     0,     0,
       0,    37,   676,     0,    12,     0,     0,     0,     0,     0,
       0,     0,    13,    14,     0,     0,    15,     0,     0,    16,
       0,     0,     0,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,     0,    28,    29,     0,    30,    31,
      32,    33,    34,    35,    36,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    37,   677,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,    14,
       0,     0,    15,     0,     0,    16,     0,     0,     0,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
       0,    28,    29,     0,    30,    31,    32,    33,    34,    35,
      36,     4,     0,     0,     5,     0,     0,     6,     7,     8,
       0,     0,     9,     0,     0,     0,     0,     0,     0,    10,
       0,     0,     0,    37,   680,     0,    12,     0,     0,     0,
       0,     0,     0,     0,    13,    14,     0,     0,    15,     0,
       0,    16,     0,     0,     0,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,     0,    28,    29,     0,
      30,    31,    32,    33,    34,    35,    36,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    37,
     681,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,    14,     0,     0,    15,     0,     0,    16,     0,     0,
       0,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,     0,    28,    29,     0,    30,    31,    32,    33,
      34,    35,    36,     4,     0,     0,     5,     0,     0,     6,
       7,     8,     0,     0,     9,     0,     0,     0,     0,     0,
       0,    10,     0,     0,     0,    37,   708,     0,    12,     0,
       0,     0,     0,     0,     0,     0,    13,    14,     0,     0,
      15,     0,     0,    16,     0,     0,     0,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,     0,    28,
      29,     0,    30,    31,    32,    33,    34,    35,    36,     4,
       0,     0,     5,     0,     0,     6,     7,     8,     0,     0,
       9,     0,     0,     0,     0,     0,     0,    10,     0,     0,
       0,    37,   710,     0,    12,     0,     0,     0,     0,     0,
       0,     0,    13,    14,     0,     0,    15,     0,     0,    16,
       0,     0,     0,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,     0,    28,    29,     0,    30,    31,
      32,    33,    34,    35,    36,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    37,   772,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,    14,
       0,     0,    15,     0,     0,    16,     0,     0,     0,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
       0,    28,    29,     0,    30,    31,    32,    33,    34,    35,
      36,     4,     0,     0,     5,     0,     0,     6,     7,     8,
       0,     0,     9,     0,     0,     0,     0,     0,     0,    10,
       0,     0,     0,    37,   775,     0,    12,     0,     0,     0,
       0,     0,     0,     0,    13,    14,     0,     0,    15,     0,
       0,    16,     0,     0,     0,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,     0,    28,    29,     0,
      30,    31,    32,    33,    34,    35,    36,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    37,
     810,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,    14,     0,     0,    15,     0,     0,    16,     0,     0,
       0,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,     0,    28,    29,     0,    30,    31,    32,    33,
      34,    35,    36,     4,     0,     0,     5,     0,     0,     6,
       7,     8,     0,     0,     9,     0,     0,     0,     0,     0,
       0,    10,     0,     0,     0,    37,   837,     0,    12,     0,
       0,     0,     0,     0,     0,     0,    13,    14,     0,     0,
      15,     0,     0,    16,     0,     0,     0,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,     0,    28,
      29,     0,    30,    31,    32,    33,    34,    35,    36,     4,
       0,     0,     5,     0,     0,     6,     7,     8,     0,     0,
       9,     0,     0,     0,     0,     0,     0,    10,     0,     0,
       0,    37,   850,     0,    12,     0,     0,     0,     0,     0,
       0,     0,    13,    14,     0,     0,    15,     0,     0,    16,
       0,     0,     0,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,     0,    28,    29,     0,    30,    31,
      32,    33,    34,    35,    36,   342,     0,     0,     5,     0,
       0,     6,     0,     0,     0,     0,   641,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    37,    11,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,    14,
       0,     0,    15,     0,     0,    16,     0,     0,     0,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
       0,    28,    29,     0,    30,    31,    32,    33,    34,    35,
      36,   342,     0,     0,     5,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   566,
       0,     0,     0,   642,    11,     0,    12,     0,     0,     0,
       0,     0,     0,     0,    13,     0,   342,     0,     0,     5,
       0,     0,     0,     0,     0,     0,     0,    19,    20,     0,
      22,    23,    24,     0,     0,     0,     0,    28,    29,    11,
      30,    12,     0,     0,    34,    35,     0,     0,     0,    13,
       0,   342,     0,     0,     5,     0,     0,     0,     0,     0,
       0,     0,    19,    20,     0,    22,    23,    24,     0,   567,
       0,     0,    28,    29,    11,    30,    12,     0,     0,    34,
      35,     0,     0,     0,    13,     0,     0,     0,     0,    75,
      76,    77,     0,    78,    79,    80,     0,    19,    20,     0,
      22,    23,    24,     0,   343,     0,     0,    28,    29,     0,
      30,     0,     0,    11,    34,    35,    81,    82,    83,    84,
       0,     0,    85,    86,     0,    87,    88,     0,   109,    75,
      76,    77,     0,    78,    79,    80,     0,     0,     0,   766,
       0,     0,     0,     0,     0,    89,     0,    29,     0,    90,
       0,     0,     0,    11,     0,     0,    81,    82,    83,    84,
       0,     0,    85,    86,     0,    87,    88,    91,     0,     0,
      92,     0,    93,     0,    94,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    89,     0,    29,     0,    90,
       0,    75,    76,    77,     0,    78,    79,    80,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    91,     0,     0,
      92,     0,    93,     0,    94,    11,     0,     0,    81,    82,
      83,    84,     0,     0,    85,     0,     0,    87,    88,    75,
      76,    77,     0,    78,    79,    80,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    89,     0,    29,
       0,    90,     0,    11,     0,     0,    81,    82,    83,    84,
       0,     0,     0,     0,     0,    87,    88,     0,     0,    91,
       0,     0,    92,     0,    93,     0,    94,     0,     0,     0,
       0,     0,     0,     0,     0,    89,     0,    29,     0,    90,
       0,    75,    76,    77,     0,    78,     0,    80,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    91,     0,     0,
      92,     0,    93,     0,    94,    11,     0,     0,    81,    82,
      83,    84,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    89,     0,    29,
       0,    90,     0,     0,     0,     0,     0,   313,   314,     0,
       0,     0,     0,     0,     0,     0,     0,   260,   261,     0,
       0,     0,    92,     0,    93,   262,    94,   263,   264,     0,
     265,     0,   266,   267,   268,   269,   270,   271,   272,   273,
     274,   275,   276,   277,   278,   279,   280,   281,   282,   283,
     284,   285,   286,   287,   288,   289,   290,   291,   292,   293,
     294,   295,   296,   297,   298,   299,   300,   301,   302,   303,
     304,   305,   306,   313,   314,     0,     0,     0,     0,     0,
       0,     0,     0,   260,   261,     0,     0,     0,   315,     0,
       0,   262,     0,   263,   264,     0,   265,     0,   266,   267,
     268,   269,   270,   271,   272,   273,   274,   275,   276,   277,
     278,   279,   280,   281,   282,   283,   284,   285,   286,   287,
     288,   289,   290,   291,   292,   293,   294,   295,   296,   297,
     298,   299,   300,   301,   302,   303,   304,   305,   306,   313,
     314,     0,     0,     0,     0,     0,     0,     0,     0,   260,
     261,     0,     0,     0,   322,     0,     0,   262,     0,   263,
     264,     0,   265,     0,   266,   267,   268,   269,   270,   271,
     272,   273,   274,   275,   276,   277,   278,   279,   280,   281,
     282,   283,   284,   285,   286,   287,   288,   289,   290,   291,
     292,   293,   294,   295,   296,   297,   298,   299,   300,   301,
     302,   303,   304,   305,   306,   313,   314,     0,     0,     0,
       0,     0,     0,     0,     0,   260,   261,     0,     0,     0,
     324,     0,     0,   262,     0,   263,   264,     0,   265,     0,
     266,   267,   268,   269,   270,   271,   272,   273,   274,   275,
     276,   277,   278,   279,   280,   281,   282,   283,   284,   285,
     286,   287,   288,   289,   290,   291,   292,   293,   294,   295,
     296,   297,   298,   299,   300,   301,   302,   303,   304,   305,
     306,   313,   314,     0,     0,     0,     0,     0,     0,     0,
       0,   260,   261,     0,     0,     0,   698,     0,     0,   262,
       0,   263,   264,     0,   265,     0,   266,   267,   268,   269,
     270,   271,   272,   273,   274,   275,   276,   277,   278,   279,
     280,   281,   282,   283,   284,   285,   286,   287,   288,   289,
     290,   291,   292,   293,   294,   295,   296,   297,   298,   299,
     300,   301,   302,   303,   304,   305,   306,   313,   314,     0,
       0,     0,     0,     0,     0,     0,     0,   260,   261,     0,
       0,     0,   752,     0,     0,   262,     0,   263,   264,     0,
     265,     0,   266,   267,   268,   269,   270,   271,   272,   273,
     274,   275,   276,   277,   278,   279,   280,   281,   282,   283,
     284,   285,   286,   287,   288,   289,   290,   291,   292,   293,
     294,   295,   296,   297,   298,   299,   300,   301,   302,   303,
     304,   305,   306,   258,     0,     0,   259,     0,     0,     0,
       0,     0,     0,   260,   261,     0,     0,     0,   780,     0,
       0,   262,     0,   263,   264,     0,   265,     0,   266,   267,
     268,   269,   270,   271,   272,   273,   274,   275,   276,   277,
     278,   279,   280,   281,   282,   283,   284,   285,   286,   287,
     288,   289,   290,   291,   292,   293,   294,   295,   296,   297,
     298,   299,   300,   301,   302,   303,   304,   305,   306,   339,
       0,     0,   340,     0,     0,     0,     0,     0,     0,   260,
     261,     0,     0,     0,     0,     0,     0,   262,     0,   263,
     264,     0,   265,     0,   266,   267,   268,   269,   270,   271,
     272,   273,   274,   275,   276,   277,   278,   279,   280,   281,
     282,   283,   284,   285,   286,   287,   288,   289,   290,   291,
     292,   293,   294,   295,   296,   297,   298,   299,   300,   301,
     302,   303,   304,   305,   306,   368,     0,     0,   369,     0,
       0,     0,     0,     0,     0,   260,   261,     0,     0,     0,
       0,     0,     0,   262,     0,   263,   264,     0,   265,     0,
     266,   267,   268,   269,   270,   271,   272,   273,   274,   275,
     276,   277,   278,   279,   280,   281,   282,   283,   284,   285,
     286,   287,   288,   289,   290,   291,   292,   293,   294,   295,
     296,   297,   298,   299,   300,   301,   302,   303,   304,   305,
     306
};

static const yytype_int16 yycheck[] =
{
       2,   547,   198,   180,   181,   578,   497,   198,   115,   500,
     232,   583,   587,   198,    94,   590,   214,   194,   198,   583,
     583,    23,    24,    25,   246,   198,   583,   249,   126,   487,
     198,   198,   198,   583,   583,   583,    79,    85,    10,     4,
      12,    13,   198,   198,   583,    17,    18,   198,    91,     4,
      22,   583,   583,     4,    26,   583,   583,   583,    30,   583,
     583,     4,   635,   636,     4,   458,     5,   639,     4,     5,
       4,     4,    27,   581,     4,   639,   639,     4,     4,     4,
      85,     4,   639,     4,    27,    90,   159,    27,    70,   639,
     639,   639,     0,    27,    27,     4,    46,    27,    63,    85,
     639,    27,    42,    42,    90,    60,    42,   639,   639,    91,
      61,   639,   639,   639,    86,   639,   639,    60,    27,     4,
      92,    61,   229,   196,   126,    38,    47,    61,    61,   236,
      60,   238,   640,   526,    60,   233,   529,    92,    61,   799,
      90,   801,    27,   250,   717,    84,   718,   220,    84,    92,
      84,    60,   200,    92,   718,   718,    92,    84,   616,   734,
      42,   718,    92,   361,     4,    84,    92,     4,   718,   718,
     718,    43,    86,    86,   217,   218,    61,    14,     4,   718,
      46,    95,    95,    92,   156,   157,   718,   718,   160,    29,
     718,   718,   718,     4,   718,   718,   198,   770,   591,    92,
       4,    27,     4,    54,    70,    59,     4,    92,    85,     4,
      92,    62,     4,    90,    54,     4,    27,     4,   709,     4,
      92,   193,    62,    27,    90,    27,   228,    70,    90,    27,
      92,   233,    27,   235,    60,    27,   343,   695,   696,    70,
      27,     4,    27,    84,    71,   703,    70,   219,    91,    60,
     222,   223,   224,   225,    95,   335,    60,   337,    60,     4,
      91,   807,    60,     4,    27,    60,    92,    91,    60,    16,
      17,    18,    19,    60,     4,    60,     4,     7,    85,    42,
      85,    92,   828,    90,    87,    90,    85,     4,    92,    92,
      92,    90,   750,    90,    92,    92,    92,    92,    61,    95,
      92,   497,     4,    85,   500,    92,   497,    92,    90,   500,
      23,    85,   497,    26,     4,   500,    90,   497,    64,    89,
     500,    84,    92,    70,   497,     4,     5,   500,    92,   497,
     497,   497,   500,   500,   500,   793,   794,   309,   310,   446,
     420,   497,   497,    91,   500,   500,   497,    84,   320,   500,
      92,   431,     4,     5,    92,   813,   463,   184,    80,    81,
      92,   468,    82,    83,    20,    21,    91,    92,   195,    92,
      91,    92,     4,     5,    92,     8,   201,   202,   380,   215,
     216,    92,    92,    16,    17,    18,    19,   583,   390,    92,
     392,    92,   583,    92,    92,    92,    84,   504,   583,    84,
     402,   508,    60,   583,     4,   377,    84,   379,     4,   381,
     583,    84,    24,    35,    34,   583,   583,   583,     3,    63,
     527,    84,     4,   530,   531,    92,    92,   583,   583,    84,
     537,    84,   583,   540,    84,    95,   438,    70,   615,     4,
      41,    92,   414,   639,    84,   417,   418,    92,   639,     4,
       4,    85,    90,    86,   639,    88,   563,    85,     4,   639,
     567,    84,    95,     4,    85,     4,   639,    85,     4,   576,
     577,   639,   639,   639,    23,   447,   448,     4,    84,     4,
      41,     5,   454,   639,   639,     4,     6,     5,   639,    87,
      84,    84,    84,    70,    84,   497,    85,    92,   500,    85,
      84,    92,   329,   505,    92,    88,   478,    42,    84,    90,
      90,    84,    91,   709,    92,    87,    84,   694,   709,    85,
     492,   743,   718,    92,   709,    84,     4,   718,    85,   709,
     532,   638,    46,   718,   536,    85,   709,   509,   718,    90,
      92,   709,   709,   709,    92,   718,    14,    85,    85,    89,
     718,   718,   718,   709,   709,     4,     4,    13,   709,    92,
      92,     4,   718,   718,   671,   742,    25,   718,   675,   571,
     397,   573,    85,    46,    48,    92,   578,    92,    24,     4,
      92,   583,   409,   410,    92,    92,    28,   589,    58,    56,
     562,   593,   594,    85,   566,    92,     4,   599,     8,   601,
      66,    92,    85,   575,    85,    85,    85,   714,   435,   436,
      20,    21,     4,    27,    85,    92,   443,   444,    22,     3,
       4,     5,   624,     7,     8,     9,   628,    85,    85,    92,
     602,    85,    92,   635,   636,    24,     3,   639,   610,   611,
     747,    92,    92,    27,     3,    92,    30,    31,    32,    33,
      70,    92,    36,    37,    92,    39,    40,   764,    91,   766,
      70,    71,    72,    73,    74,    75,    76,    77,    78,    79,
       4,    84,     4,     4,     4,    59,    13,    61,    88,    63,
      92,    92,    56,    92,    41,     4,    92,    92,     7,   796,
      92,    92,    64,     4,     4,    92,     4,    81,    92,    95,
      84,    92,    86,    22,    88,     4,    92,   709,    27,    27,
      29,    95,    24,    38,    92,   717,   718,    24,    37,    92,
      70,    92,   829,     3,     4,     5,    92,     7,     8,     9,
     732,    50,    51,   735,    53,    54,    55,    92,    92,    92,
      92,    60,    61,    92,    63,    92,    92,    27,    67,    68,
      30,    31,    32,    33,    92,    52,    36,    37,    52,    39,
      40,    56,    65,     3,    92,     5,    92,    84,   770,    92,
      92,    92,    92,    92,    64,    84,    92,    92,     4,    59,
      85,    61,   784,    63,     4,     4,    92,     4,    92,     4,
      30,    31,    32,    33,    92,    22,    92,   799,    22,   801,
      92,    81,     5,    92,    84,    92,    86,    87,    88,    92,
      92,    92,    92,    92,   786,    91,     4,    92,   820,     3,
       4,     5,    92,     7,     8,     9,    70,    71,    72,    73,
      74,    75,    76,    77,    78,    79,    92,     4,    92,   841,
      80,    81,    92,    27,    92,    92,    30,    31,    32,    33,
      92,    52,    36,    37,    52,    39,    40,    92,    92,    92,
      85,     4,     4,    92,     4,    60,     4,     4,     4,    92,
      92,    92,     4,    60,    92,    59,    92,    61,    92,    63,
      92,    92,    92,     4,    92,    92,    92,    92,     4,     4,
      92,   140,    92,    92,   161,     4,     5,    81,   149,   235,
      84,   406,    86,   802,    88,    14,    15,   101,    92,    87,
     328,   199,   213,    22,   484,    24,    25,   609,    27,    -1,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,     4,     5,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    14,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,
      89,    24,    25,    92,    27,    -1,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,     4,    -1,    -1,
       7,    -1,    -1,    10,    11,    12,    -1,    -1,    15,    -1,
      -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,    92,
      27,    -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      37,    38,    -1,    -1,    41,    -1,    -1,    44,    45,    -1,
      -1,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    -1,    60,    61,    -1,    63,    64,    65,    66,
      67,    68,    69,     4,    -1,    -1,     7,    -1,    -1,    10,
      11,    12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,
      -1,    22,    -1,    -1,    25,    92,    27,    -1,    29,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    37,    38,    -1,    -1,
      41,    -1,    -1,    44,    -1,    -1,    -1,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    -1,    60,
      61,    -1,    63,    64,    65,    66,    67,    68,    69,     4,
      -1,    -1,     7,    -1,    -1,    10,    11,    12,    -1,    -1,
      15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,
      -1,    92,    27,    -1,    29,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    37,    38,    -1,    -1,    41,    -1,    -1,    44,
      -1,    -1,    -1,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    -1,    60,    61,    -1,    63,    64,
      65,    66,    67,    68,    69,     4,    -1,    -1,     7,    -1,
      -1,    10,    11,    12,    -1,    -1,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    -1,    -1,    92,    27,    -1,
      29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    38,
      -1,    -1,    41,    -1,    -1,    44,    -1,    -1,    -1,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      -1,    60,    61,    -1,    63,    64,    65,    66,    67,    68,
      69,     4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,
      -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,
      -1,    -1,    -1,    92,    27,    -1,    29,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    37,    38,    -1,    -1,    41,    -1,
      -1,    44,    -1,    -1,    -1,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    -1,    60,    61,    -1,
      63,    64,    65,    66,    67,    68,    69,     4,    -1,    -1,
       7,    -1,    -1,    10,    11,    12,    -1,    -1,    15,    -1,
      -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,    92,
      27,    -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      37,    38,    -1,    -1,    41,    -1,    -1,    44,    -1,    -1,
      -1,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    -1,    60,    61,    -1,    63,    64,    65,    66,
      67,    68,    69,     4,    -1,    -1,     7,    -1,    -1,    10,
      11,    12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,
      -1,    22,    -1,    -1,    -1,    92,    27,    -1,    29,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    37,    38,    -1,    -1,
      41,    -1,    -1,    44,    -1,    -1,    -1,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    -1,    60,
      61,    -1,    63,    64,    65,    66,    67,    68,    69,     4,
      -1,    -1,     7,    -1,    -1,    10,    11,    12,    -1,    -1,
      15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,
      -1,    92,    27,    -1,    29,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    37,    38,    -1,    -1,    41,    -1,    -1,    44,
      -1,    -1,    -1,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    -1,    60,    61,    -1,    63,    64,
      65,    66,    67,    68,    69,     4,    -1,    -1,     7,    -1,
      -1,    10,    11,    12,    -1,    -1,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    -1,    -1,    92,    27,    -1,
      29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    38,
      -1,    -1,    41,    -1,    -1,    44,    -1,    -1,    -1,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      -1,    60,    61,    -1,    63,    64,    65,    66,    67,    68,
      69,     4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,
      -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,
      -1,    -1,    -1,    92,    27,    -1,    29,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    37,    38,    -1,    -1,    41,    -1,
      -1,    44,    -1,    -1,    -1,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    -1,    60,    61,    -1,
      63,    64,    65,    66,    67,    68,    69,     4,    -1,    -1,
       7,    -1,    -1,    10,    11,    12,    -1,    -1,    15,    -1,
      -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,    92,
      27,    -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      37,    38,    -1,    -1,    41,    -1,    -1,    44,    -1,    -1,
      -1,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    -1,    60,    61,    -1,    63,    64,    65,    66,
      67,    68,    69,     4,    -1,    -1,     7,    -1,    -1,    10,
      11,    12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,
      -1,    22,    -1,    -1,    -1,    92,    27,    -1,    29,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    37,    38,    -1,    -1,
      41,    -1,    -1,    44,    -1,    -1,    -1,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    -1,    60,
      61,    -1,    63,    64,    65,    66,    67,    68,    69,     4,
      -1,    -1,     7,    -1,    -1,    10,    11,    12,    -1,    -1,
      15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,
      -1,    92,    27,    -1,    29,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    37,    38,    -1,    -1,    41,    -1,    -1,    44,
      -1,    -1,    -1,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    -1,    60,    61,    -1,    63,    64,
      65,    66,    67,    68,    69,     4,    -1,    -1,     7,    -1,
      -1,    10,    11,    12,    -1,    -1,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    -1,    -1,    92,    27,    -1,
      29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    38,
      -1,    -1,    41,    -1,    -1,    44,    -1,    -1,    -1,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      -1,    60,    61,    -1,    63,    64,    65,    66,    67,    68,
      69,     4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,
      -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,
      -1,    -1,    -1,    92,    27,    -1,    29,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    37,    38,    -1,    -1,    41,    -1,
      -1,    44,    -1,    -1,    -1,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    -1,    60,    61,    -1,
      63,    64,    65,    66,    67,    68,    69,     4,    -1,    -1,
       7,    -1,    -1,    10,    11,    12,    -1,    -1,    15,    -1,
      -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,    92,
      27,    -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      37,    38,    -1,    -1,    41,    -1,    -1,    44,    -1,    -1,
      -1,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    -1,    60,    61,    -1,    63,    64,    65,    66,
      67,    68,    69,     4,    -1,    -1,     7,    -1,    -1,    10,
      11,    12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,
      -1,    22,    -1,    -1,    -1,    92,    27,    -1,    29,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    37,    38,    -1,    -1,
      41,    -1,    -1,    44,    -1,    -1,    -1,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    -1,    60,
      61,    -1,    63,    64,    65,    66,    67,    68,    69,     4,
      -1,    -1,     7,    -1,    -1,    10,    11,    12,    -1,    -1,
      15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,
      -1,    92,    27,    -1,    29,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    37,    38,    -1,    -1,    41,    -1,    -1,    44,
      -1,    -1,    -1,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    -1,    60,    61,    -1,    63,    64,
      65,    66,    67,    68,    69,     4,    -1,    -1,     7,    -1,
      -1,    10,    11,    12,    -1,    -1,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    -1,    -1,    92,    27,    -1,
      29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    38,
      -1,    -1,    41,    -1,    -1,    44,    -1,    -1,    -1,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      -1,    60,    61,    -1,    63,    64,    65,    66,    67,    68,
      69,     4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,
      -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,
      -1,    -1,    -1,    92,    27,    -1,    29,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    37,    38,    -1,    -1,    41,    -1,
      -1,    44,    -1,    -1,    -1,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    -1,    60,    61,    -1,
      63,    64,    65,    66,    67,    68,    69,     4,    -1,    -1,
       7,    -1,    -1,    10,    11,    12,    -1,    -1,    15,    -1,
      -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,    92,
      27,    -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      37,    38,    -1,    -1,    41,    -1,    -1,    44,    -1,    -1,
      -1,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    -1,    60,    61,    -1,    63,    64,    65,    66,
      67,    68,    69,     4,    -1,    -1,     7,    -1,    -1,    10,
      11,    12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,
      -1,    22,    -1,    -1,    -1,    92,    27,    -1,    29,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    37,    38,    -1,    -1,
      41,    -1,    -1,    44,    -1,    -1,    -1,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    -1,    60,
      61,    -1,    63,    64,    65,    66,    67,    68,    69,     4,
      -1,    -1,     7,    -1,    -1,    10,    11,    12,    -1,    -1,
      15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,
      -1,    92,    27,    -1,    29,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    37,    38,    -1,    -1,    41,    -1,    -1,    44,
      -1,    -1,    -1,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    -1,    60,    61,    -1,    63,    64,
      65,    66,    67,    68,    69,     4,    -1,    -1,     7,    -1,
      -1,    10,    -1,    -1,    -1,    -1,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    -1,    -1,    92,    27,    -1,
      29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    38,
      -1,    -1,    41,    -1,    -1,    44,    -1,    -1,    -1,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      -1,    60,    61,    -1,    63,    64,    65,    66,    67,    68,
      69,     4,    -1,    -1,     7,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    22,
      -1,    -1,    -1,    92,    27,    -1,    29,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    37,    -1,     4,    -1,    -1,     7,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    50,    51,    -1,
      53,    54,    55,    -1,    -1,    -1,    -1,    60,    61,    27,
      63,    29,    -1,    -1,    67,    68,    -1,    -1,    -1,    37,
      -1,     4,    -1,    -1,     7,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    50,    51,    -1,    53,    54,    55,    -1,    92,
      -1,    -1,    60,    61,    27,    63,    29,    -1,    -1,    67,
      68,    -1,    -1,    -1,    37,    -1,    -1,    -1,    -1,     3,
       4,     5,    -1,     7,     8,     9,    -1,    50,    51,    -1,
      53,    54,    55,    -1,    92,    -1,    -1,    60,    61,    -1,
      63,    -1,    -1,    27,    67,    68,    30,    31,    32,    33,
      -1,    -1,    36,    37,    -1,    39,    40,    -1,    42,     3,
       4,     5,    -1,     7,     8,     9,    -1,    -1,    -1,    92,
      -1,    -1,    -1,    -1,    -1,    59,    -1,    61,    -1,    63,
      -1,    -1,    -1,    27,    -1,    -1,    30,    31,    32,    33,
      -1,    -1,    36,    37,    -1,    39,    40,    81,    -1,    -1,
      84,    -1,    86,    -1,    88,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    59,    -1,    61,    -1,    63,
      -1,     3,     4,     5,    -1,     7,     8,     9,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    81,    -1,    -1,
      84,    -1,    86,    -1,    88,    27,    -1,    -1,    30,    31,
      32,    33,    -1,    -1,    36,    -1,    -1,    39,    40,     3,
       4,     5,    -1,     7,     8,     9,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    59,    -1,    61,
      -1,    63,    -1,    27,    -1,    -1,    30,    31,    32,    33,
      -1,    -1,    -1,    -1,    -1,    39,    40,    -1,    -1,    81,
      -1,    -1,    84,    -1,    86,    -1,    88,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    59,    -1,    61,    -1,    63,
      -1,     3,     4,     5,    -1,     7,    -1,     9,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    81,    -1,    -1,
      84,    -1,    86,    -1,    88,    27,    -1,    -1,    30,    31,
      32,    33,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    59,    -1,    61,
      -1,    63,    -1,    -1,    -1,    -1,    -1,     4,     5,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    14,    15,    -1,
      -1,    -1,    84,    -1,    86,    22,    88,    24,    25,    -1,
      27,    -1,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,     4,     5,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    14,    15,    -1,    -1,    -1,    85,    -1,
      -1,    22,    -1,    24,    25,    -1,    27,    -1,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,     4,
       5,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    14,
      15,    -1,    -1,    -1,    85,    -1,    -1,    22,    -1,    24,
      25,    -1,    27,    -1,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,     4,     5,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    14,    15,    -1,    -1,    -1,
      85,    -1,    -1,    22,    -1,    24,    25,    -1,    27,    -1,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,     4,     5,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    14,    15,    -1,    -1,    -1,    85,    -1,    -1,    22,
      -1,    24,    25,    -1,    27,    -1,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,     4,     5,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    14,    15,    -1,
      -1,    -1,    85,    -1,    -1,    22,    -1,    24,    25,    -1,
      27,    -1,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,     4,    -1,    -1,     7,    -1,    -1,    -1,
      -1,    -1,    -1,    14,    15,    -1,    -1,    -1,    85,    -1,
      -1,    22,    -1,    24,    25,    -1,    27,    -1,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,     4,
      -1,    -1,     7,    -1,    -1,    -1,    -1,    -1,    -1,    14,
      15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,    24,
      25,    -1,    27,    -1,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,     4,    -1,    -1,     7,    -1,
      -1,    -1,    -1,    -1,    -1,    14,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    24,    25,    -1,    27,    -1,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    97,    98,     0,     4,     7,    10,    11,    12,    15,
      22,    27,    29,    37,    38,    41,    44,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    60,    61,
      63,    64,    65,    66,    67,    68,    69,    92,    99,   100,
     103,   104,   109,   110,   111,   113,   114,   115,   116,   121,
     122,   123,   124,   125,   128,   129,   131,   134,   135,   142,
     143,   144,   145,   146,   147,   148,   149,   150,   151,     4,
      84,    84,     4,     4,     4,     3,     4,     5,     7,     8,
       9,    30,    31,    32,    33,    36,    37,    39,    40,    59,
      63,    81,    84,    86,    88,   104,   155,   156,   157,   158,
     159,   160,   161,   162,   163,   164,   166,   167,   170,    42,
     155,   155,     4,     4,    47,    92,   155,   155,     4,     4,
       4,     7,   155,   104,   104,     4,    84,   104,   130,   141,
     155,    59,     4,    63,    95,   155,     4,    27,    42,    61,
     107,   108,   126,     4,     4,     4,     5,     4,     5,    64,
      92,     8,    16,    17,    18,    19,    70,    86,    88,    95,
     102,   105,    91,    92,    92,    92,    92,    92,    92,    92,
      92,    92,    92,    92,    92,    84,   155,   171,   172,   171,
      84,    84,    60,     4,    84,   163,   158,   155,   164,     4,
      84,   163,   155,   181,   181,    84,    95,   168,    24,    35,
      34,    20,    21,    70,    71,    72,    73,    74,    75,    76,
      77,    78,    79,   105,   165,    80,    81,    82,    83,    86,
      95,     3,    63,    84,    46,    70,    90,     4,    98,    92,
      92,    84,    84,    84,   130,    90,    92,    95,    92,     4,
      29,    54,    62,    54,    62,     4,    84,   108,    41,    84,
      92,     4,    14,     4,   126,   155,   155,   106,     4,     7,
      14,    15,    22,    24,    25,    27,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,   178,   155,    70,
     102,     4,   101,     4,     5,    85,   177,   178,   179,    85,
      90,    85,    85,   179,    85,   179,     4,   171,    38,    84,
      85,    85,    87,    92,   155,   173,    89,   179,   171,     4,
       7,   178,     4,    92,   100,   104,   109,   110,   125,   143,
     144,   145,   147,   148,   149,   150,   154,   157,   158,   161,
     161,   165,   160,   162,   162,   163,   163,   155,     4,     7,
     178,     4,   155,   155,   155,   155,     4,    46,    90,    45,
      98,    23,   117,     4,   174,   176,   174,   130,    85,   141,
      98,     4,    98,     4,    61,     4,    61,    84,   174,     4,
     127,   174,    98,     5,     4,     5,    41,    87,     6,    84,
      84,   155,   155,    84,    70,    84,    92,    70,    91,    85,
      90,   155,    92,    85,    92,    85,    92,    95,    85,   167,
     171,    90,   181,   181,    85,    84,    84,   169,    98,    91,
      92,   160,    87,    84,    84,    85,    92,    42,    46,   155,
       4,   155,    27,   155,    23,    26,   118,    70,    91,    85,
      90,    85,    85,    92,    27,    27,   171,    85,    92,    85,
      27,    14,   127,    89,   171,   171,     4,   155,     4,   180,
     133,   155,   155,    92,   181,   136,    42,    92,    92,    13,
       4,    85,   181,    87,    89,   171,   171,    25,    27,   152,
      25,   153,   171,   171,    92,    98,   155,   155,    92,    46,
      92,    48,    24,   155,    92,    28,     3,     5,    30,    31,
      32,    33,    80,    81,   175,   101,    91,    92,     4,    91,
      92,    92,    98,    56,    58,    85,    98,    92,    66,     4,
      92,    85,    85,    85,   155,    85,   138,   132,     4,   177,
       4,    27,    60,    92,   137,     4,   136,    27,    92,   155,
      85,    85,    22,    92,   154,    22,    22,    92,   154,    85,
      85,    98,    27,    61,   112,    43,    92,    92,    98,   155,
      92,    92,    24,   119,    92,     3,     3,    70,   101,    98,
      70,    91,   101,    98,    98,    27,    92,    92,    27,    98,
      92,    98,    91,     4,    27,    60,    92,   139,   138,    84,
      70,    91,     4,     5,    42,    84,    92,     4,     4,     4,
      27,     4,    13,   155,    98,    92,    92,   155,    98,    92,
      27,    41,    92,   104,   155,    98,    98,   112,    92,   119,
      92,    15,    92,   100,   109,   110,   111,   113,   114,   115,
     116,   120,   121,   122,   123,   124,   125,   128,   129,   135,
     142,   143,   144,   145,   146,   147,   148,   149,   150,   151,
     175,    92,    27,   175,   101,    92,    27,    27,    56,    64,
      27,    27,   155,     4,     5,   140,     4,     4,    27,   180,
     155,   155,     5,    42,    84,    92,    92,     4,    85,   179,
     136,    92,    95,    92,     4,    92,    27,    24,    27,    24,
      27,    38,    92,    92,    92,   112,   112,    98,   119,    92,
      92,    92,    92,    92,    92,    92,    92,    92,    92,    92,
      92,    92,    98,    52,    70,    98,    52,    56,    92,    92,
      65,    64,    84,    84,    90,    92,    92,    92,     4,    85,
      92,     4,    85,   179,   136,   136,     4,    92,    85,    27,
       4,   136,    92,     4,    92,    22,    92,   154,    22,    92,
      98,   112,    27,    92,   175,    27,    92,    92,    92,    92,
      85,   179,   174,     5,    98,    92,    91,   136,     4,    92,
      85,    27,    27,    92,    92,     4,    92,    27,    92,    98,
      92,    98,    92,    92,   112,    52,    52,    92,    85,    85,
      27,   155,    27,    92,    92,     4,     4,   136,   136,    92,
      98,     4,   152,   152,   153,    92,    92,   138,    92,    92,
      60,     4,   136,    92,    92,    27,    27,    27,    92,    27,
     138,    98,    92,    92,    27,     4,     4,    60,     4,    27,
      27,     4,    92,    92,    92,    92,     4,     4,    92,    92,
      92
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    96,    97,    98,    98,    98,    99,    99,    99,    99,
      99,    99,    99,    99,    99,    99,    99,    99,    99,    99,
      99,    99,    99,    99,    99,    99,    99,    99,    99,    99,
      99,    99,    99,    99,    99,   100,   100,   100,   100,   100,
     101,   101,   102,   102,   102,   102,   103,   103,   103,   104,
     104,   104,   106,   105,   105,   107,   107,   108,   108,   108,
     108,   109,   109,   110,   110,   110,   110,   110,   110,   111,
     112,   112,   112,   113,   113,   113,   113,   113,   113,   114,
     115,   116,   117,   117,   118,   118,   119,   119,   119,   120,
     120,   120,   120,   120,   120,   120,   120,   120,   120,   120,
     120,   120,   120,   120,   120,   120,   120,   120,   120,   120,
     120,   120,   120,   120,   120,   120,   121,   121,   121,   121,
     122,   122,   123,   124,   125,   125,   125,   125,   125,   125,
     125,   125,   126,   126,   127,   128,   128,   128,   129,   130,
     130,   132,   131,   133,   131,   134,   134,   134,   134,   135,
     135,   135,   136,   136,   136,   137,   137,   137,   137,   137,
     137,   137,   137,   137,   137,   137,   138,   138,   138,   139,
     139,   139,   139,   139,   140,   140,   141,   141,   142,   143,
     143,   143,   143,   143,   143,   143,   143,   144,   145,   145,
     146,   147,   148,   149,   149,   150,   150,   151,   151,   152,
     152,   152,   152,   153,   153,   153,   153,   153,   154,   154,
     154,   154,   154,   154,   154,   154,   154,   154,   154,   155,
     155,   156,   156,   157,   157,   158,   158,   159,   159,   159,
     160,   160,   160,   161,   161,   161,   162,   162,   162,   163,
     163,   163,   163,   163,   163,   164,   164,   164,   164,   164,
     165,   165,   165,   165,   165,   165,   165,   165,   165,   165,
     166,   166,   166,   166,   166,   166,   166,   166,   166,   166,
     166,   166,   166,   166,   166,   166,   167,   167,   168,   168,
     168,   168,   169,   169,   170,   170,   171,   171,   172,   172,
     173,   173,   174,   174,   175,   175,   175,   175,   175,   175,
     175,   175,   176,   176,   176,   176,   176,   176,   176,   176,
     177,   177,   178,   178,   178,   178,   178,   178,   178,   178,
     178,   178,   178,   178,   178,   178,   178,   178,   178,   178,
     178,   178,   178,   178,   178,   178,   178,   178,   178,   178,
     178,   178,   178,   178,   178,   178,   178,   178,   178,   178,
     178,   178,   178,   178,   178,   178,   178,   178,   178,   178,
     179,   179,   179,   179,   179,   179,   180,   180,   181,   181
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       2,     1,     1,     2,     1,     2,     2,     2,     2,     2,
       2,     2,     2,     1,     1,     3,     3,     4,     5,     4,
       1,     4,     1,     1,     1,     1,     1,     4,     3,     1,
       1,     1,     0,     4,     1,     1,     2,     1,     1,     1,
       1,     2,     4,     4,     2,     4,     6,     6,     6,    10,
       3,     2,     3,     7,     8,     9,    10,     9,    11,     6,
       7,     7,     5,     6,     0,     3,     0,     2,     2,     2,
       2,     2,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     2,     1,     2,     1,     2,     2,     2,     2,
       2,     2,     2,     2,     1,     1,    10,    10,    12,    12,
       9,    10,    10,     7,     2,     2,     2,     2,     4,     4,
       4,     6,     1,     4,     1,     9,     7,    10,     2,     1,
       3,     0,    11,     0,    10,     6,     5,     9,    11,    10,
       9,    13,     0,     2,     2,     4,     6,     5,     6,     9,
       7,     8,     7,     9,    10,     9,     0,     2,     2,     3,
      10,    10,     9,     7,     1,     3,     1,     3,     7,     4,
       4,     3,     4,     4,     3,     3,     3,     2,     1,     2,
       2,     2,     2,     1,     2,     1,     2,     6,     6,     3,
       3,     6,     7,     0,     3,     6,     7,     7,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       2,     1,     3,     1,     3,     1,     2,     1,     3,     4,
       1,     3,     3,     1,     3,     3,     1,     3,     3,     1,
       2,     2,     2,     4,     5,     1,     4,     3,     6,     6,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     3,     1,     1,     2,     4,     1,     1,     1,
       1,     1,     3,     3,     5,     1,     3,     5,     0,     3,
       3,     5,     0,     3,     2,     3,     0,     1,     1,     3,
       1,     4,     0,     1,     1,     2,     2,     1,     1,     1,
       1,     1,     1,     3,     3,     3,     5,     5,     7,     5,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
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
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3200 "src/parser.tab.c"
        break;

    case YYSYMBOL_STRING: /* STRING  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3206 "src/parser.tab.c"
        break;

    case YYSYMBOL_LENS_CONTENT: /* LENS_CONTENT  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3212 "src/parser.tab.c"
        break;

    case YYSYMBOL_QUALIFIED_IDENT: /* QUALIFIED_IDENT  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3218 "src/parser.tab.c"
        break;

    case YYSYMBOL_MODIFIER_PREFIX: /* MODIFIER_PREFIX  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3224 "src/parser.tab.c"
        break;

    case YYSYMBOL_MODIFIER_VALUE: /* MODIFIER_VALUE  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3230 "src/parser.tab.c"
        break;

    case YYSYMBOL_SQL_TEXT: /* SQL_TEXT  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3236 "src/parser.tab.c"
        break;

    case YYSYMBOL_program: /* program  */
#line 890 "src/parser.y"
            { (void) ((*yyvaluep).stmt_list); }
#line 3242 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement_list: /* statement_list  */
#line 827 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3248 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement: /* statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3254 "src/parser.tab.c"
        break;

    case YYSYMBOL_assignment: /* assignment  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3260 "src/parser.tab.c"
        break;

    case YYSYMBOL_type_lock: /* type_lock  */
#line 880 "src/parser.y"
            { ast_type_lock_free(((*yyvaluep).type_lock)); }
#line 3266 "src/parser.tab.c"
        break;

    case YYSYMBOL_lvalue: /* lvalue  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3272 "src/parser.tab.c"
        break;

    case YYSYMBOL_variable_name: /* variable_name  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3278 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_lens: /* comparison_lens  */
#line 874 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 3284 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_name: /* modifier_name  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3290 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_word: /* modifier_word  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3296 "src/parser.tab.c"
        break;

    case YYSYMBOL_print_statement: /* print_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3302 "src/parser.tab.c"
        break;

    case YYSYMBOL_call_statement: /* call_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3308 "src/parser.tab.c"
        break;

    case YYSYMBOL_with_lock_statement: /* with_lock_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3314 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_end: /* for_end  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3320 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_each_statement: /* for_each_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3326 "src/parser.tab.c"
        break;

    case YYSYMBOL_do_loop_statement: /* do_loop_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3332 "src/parser.tab.c"
        break;

    case YYSYMBOL_while_statement: /* while_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3338 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement: /* consider_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3344 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_branch_list: /* consider_branch_list  */
#line 872 "src/parser.y"
            { ast_free_consider_branch_list(((*yyvaluep).consider_branch_list)); }
#line 3350 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_else_opt: /* consider_else_opt  */
#line 827 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3356 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement_list: /* consider_statement_list  */
#line 827 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3362 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_body_statement: /* consider_body_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3368 "src/parser.tab.c"
        break;

    case YYSYMBOL_function_statement: /* function_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3374 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_statement: /* modifier_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3380 "src/parser.tab.c"
        break;

    case YYSYMBOL_program_statement: /* program_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3386 "src/parser.tab.c"
        break;

    case YYSYMBOL_library_statement: /* library_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3392 "src/parser.tab.c"
        break;

    case YYSYMBOL_use_statement: /* use_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3398 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_signature: /* modifier_signature  */
#line 875 "src/parser.y"
            { ast_free_modifier_signature(((*yyvaluep).modifier_signature)); }
#line 3404 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_context: /* modifier_context  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3410 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_statement: /* watch_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3416 "src/parser.tab.c"
        break;

    case YYSYMBOL_unwatch_statement: /* unwatch_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3422 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_list: /* watch_target_list  */
#line 873 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3428 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_statement: /* server_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3434 "src/parser.tab.c"
        break;

    case YYSYMBOL_gsql_block: /* gsql_block  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3440 "src/parser.tab.c"
        break;

    case YYSYMBOL_window_block: /* window_block  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3446 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item_list: /* server_item_list  */
#line 879 "src/parser.y"
            { ast_free_server_item_list(((*yyvaluep).server_item_list)); }
#line 3452 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item: /* server_item  */
#line 878 "src/parser.y"
            { AstServerItemList one = ast_server_item_list_append(ast_server_item_list_empty(), ((*yyvaluep).server_item)); ast_free_server_item_list(one); }
#line 3458 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_string_list: /* server_string_list  */
#line 873 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3464 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_path: /* watch_target_path  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3470 "src/parser.tab.c"
        break;

    case YYSYMBOL_without_watchers_statement: /* without_watchers_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3476 "src/parser.tab.c"
        break;

    case YYSYMBOL_on_error_statement: /* on_error_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3482 "src/parser.tab.c"
        break;

    case YYSYMBOL_error_statement: /* error_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3488 "src/parser.tab.c"
        break;

    case YYSYMBOL_return_statement: /* return_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3494 "src/parser.tab.c"
        break;

    case YYSYMBOL_label_statement: /* label_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3500 "src/parser.tab.c"
        break;

    case YYSYMBOL_goto_statement: /* goto_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3506 "src/parser.tab.c"
        break;

    case YYSYMBOL_gosub_statement: /* gosub_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3512 "src/parser.tab.c"
        break;

    case YYSYMBOL_break_statement: /* break_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3518 "src/parser.tab.c"
        break;

    case YYSYMBOL_continue_statement: /* continue_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3524 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_statement: /* if_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3530 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_block_tail: /* if_block_tail  */
#line 827 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3536 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_inline_tail: /* if_inline_tail  */
#line 827 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3542 "src/parser.tab.c"
        break;

    case YYSYMBOL_inline_statement: /* inline_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3548 "src/parser.tab.c"
        break;

    case YYSYMBOL_expression: /* expression  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3554 "src/parser.tab.c"
        break;

    case YYSYMBOL_or_expression: /* or_expression  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3560 "src/parser.tab.c"
        break;

    case YYSYMBOL_and_expression: /* and_expression  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3566 "src/parser.tab.c"
        break;

    case YYSYMBOL_not_expression: /* not_expression  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3572 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_expression: /* comparison_expression  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3578 "src/parser.tab.c"
        break;

    case YYSYMBOL_set_expression: /* set_expression  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3584 "src/parser.tab.c"
        break;

    case YYSYMBOL_additive_expression: /* additive_expression  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3590 "src/parser.tab.c"
        break;

    case YYSYMBOL_multiplicative_expression: /* multiplicative_expression  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3596 "src/parser.tab.c"
        break;

    case YYSYMBOL_unary_expression: /* unary_expression  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3602 "src/parser.tab.c"
        break;

    case YYSYMBOL_postfix_expression: /* postfix_expression  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3608 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_operator: /* comparison_operator  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3614 "src/parser.tab.c"
        break;

    case YYSYMBOL_primary: /* primary  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3620 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_literal: /* record_literal  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3626 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_suffix: /* ident_suffix  */
#line 876 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3632 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_dot_suffix: /* ident_dot_suffix  */
#line 876 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3638 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list_opt: /* argument_list_opt  */
#line 870 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3644 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list: /* argument_list  */
#line 870 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3650 "src/parser.tab.c"
        break;

    case YYSYMBOL_array_argument_list: /* array_argument_list  */
#line 870 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3656 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list_opt: /* parameter_list_opt  */
#line 873 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3662 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_default: /* parameter_default  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3668 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list: /* parameter_list  */
#line 873 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3674 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_name: /* field_name  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3680 "src/parser.tab.c"
        break;

    case YYSYMBOL_dot_field_name: /* dot_field_name  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3686 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_field_list: /* record_field_list  */
#line 871 "src/parser.y"
            { ast_free_record_field_list(((*yyvaluep).record_field_list)); }
#line 3692 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_policy: /* field_policy  */
#line 877 "src/parser.y"
            { ast_free_expr(((*yyvaluep).field_policy).reset_expr); }
#line 3698 "src/parser.tab.c"
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
#line 895 "src/parser.y"
                     { ctx->parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 4004 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 899 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4010 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 900 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 4016 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 901 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 4022 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 905 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4028 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 906 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4034 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 907 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4040 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 908 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4046 "src/parser.tab.c"
    break;

  case 10: /* statement: window_block  */
#line 909 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4052 "src/parser.tab.c"
    break;

  case 11: /* statement: gsql_block  */
#line 910 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4058 "src/parser.tab.c"
    break;

  case 12: /* statement: for_each_statement  */
#line 911 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4064 "src/parser.tab.c"
    break;

  case 13: /* statement: while_statement  */
#line 912 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4070 "src/parser.tab.c"
    break;

  case 14: /* statement: do_loop_statement  */
#line 913 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4076 "src/parser.tab.c"
    break;

  case 15: /* statement: consider_statement  */
#line 914 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4082 "src/parser.tab.c"
    break;

  case 16: /* statement: function_statement  */
#line 915 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4088 "src/parser.tab.c"
    break;

  case 17: /* statement: modifier_statement  */
#line 916 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4094 "src/parser.tab.c"
    break;

  case 18: /* statement: program_statement  */
#line 917 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4100 "src/parser.tab.c"
    break;

  case 19: /* statement: library_statement  */
#line 918 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4106 "src/parser.tab.c"
    break;

  case 20: /* statement: use_statement NEWLINE  */
#line 919 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4112 "src/parser.tab.c"
    break;

  case 21: /* statement: watch_statement  */
#line 920 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4118 "src/parser.tab.c"
    break;

  case 22: /* statement: server_statement  */
#line 921 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4124 "src/parser.tab.c"
    break;

  case 23: /* statement: unwatch_statement NEWLINE  */
#line 922 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4130 "src/parser.tab.c"
    break;

  case 24: /* statement: without_watchers_statement  */
#line 923 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4136 "src/parser.tab.c"
    break;

  case 25: /* statement: on_error_statement NEWLINE  */
#line 924 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4142 "src/parser.tab.c"
    break;

  case 26: /* statement: error_statement NEWLINE  */
#line 925 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4148 "src/parser.tab.c"
    break;

  case 27: /* statement: return_statement NEWLINE  */
#line 926 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4154 "src/parser.tab.c"
    break;

  case 28: /* statement: label_statement NEWLINE  */
#line 927 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4160 "src/parser.tab.c"
    break;

  case 29: /* statement: goto_statement NEWLINE  */
#line 928 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4166 "src/parser.tab.c"
    break;

  case 30: /* statement: gosub_statement NEWLINE  */
#line 929 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4172 "src/parser.tab.c"
    break;

  case 31: /* statement: break_statement NEWLINE  */
#line 930 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4178 "src/parser.tab.c"
    break;

  case 32: /* statement: continue_statement NEWLINE  */
#line 931 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4184 "src/parser.tab.c"
    break;

  case 33: /* statement: if_statement  */
#line 932 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4190 "src/parser.tab.c"
    break;

  case 34: /* statement: DIM  */
#line 938 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 4203 "src/parser.tab.c"
    break;

  case 35: /* assignment: lvalue OP_EQ expression  */
#line 949 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 4209 "src/parser.tab.c"
    break;

  case 36: /* assignment: lvalue compound_op expression  */
#line 954 "src/parser.y"
                                    { (yyval.stmt) = ast_assign_op((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr), (yyvsp[-1].op_char)); }
#line 4215 "src/parser.tab.c"
    break;

  case 37: /* assignment: lvalue comparison_lens compound_op expression  */
#line 955 "src/parser.y"
                                                    {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign_op((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr), (yyvsp[-1].op_char));
      }
#line 4229 "src/parser.tab.c"
    break;

  case 38: /* assignment: variable_name COLON type_lock OP_EQ expression  */
#line 971 "src/parser.y"
                                                     {
        if (!type_lock_check(ctx, (yyvsp[-4].text), (yyvsp[-2].type_lock).name, (yyvsp[-2].type_lock).arg, (yylsp[-2]).first_line, (yylsp[-2]).first_column,
                             (yylsp[-2]).last_line, (yylsp[-2]).last_column)) {
            YYERROR;
        }
        (yyval.stmt) = ast_assign_locked(expr_at(ast_ident((yyvsp[-4].text)), (yylsp[-4]).first_line, (yylsp[-4]).first_column), (yyvsp[-2].type_lock), (yyvsp[0].expr));
      }
#line 4241 "src/parser.tab.c"
    break;

  case 39: /* assignment: lvalue comparison_lens OP_EQ expression  */
#line 978 "src/parser.y"
                                              {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 4255 "src/parser.tab.c"
    break;

  case 40: /* type_lock: IDENT  */
#line 992 "src/parser.y"
            { (yyval.type_lock) = ast_type_lock((yyvsp[0].text), NULL, (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4261 "src/parser.tab.c"
    break;

  case 41: /* type_lock: IDENT LPAREN IDENT RPAREN  */
#line 993 "src/parser.y"
                                { (yyval.type_lock) = ast_type_lock((yyvsp[-3].text), (yyvsp[-1].text), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 4267 "src/parser.tab.c"
    break;

  case 42: /* compound_op: PLUS_EQ  */
#line 997 "src/parser.y"
               { (yyval.op_char) = '+'; }
#line 4273 "src/parser.tab.c"
    break;

  case 43: /* compound_op: MINUS_EQ  */
#line 998 "src/parser.y"
               { (yyval.op_char) = '-'; }
#line 4279 "src/parser.tab.c"
    break;

  case 44: /* compound_op: STAR_EQ  */
#line 999 "src/parser.y"
               { (yyval.op_char) = '*'; }
#line 4285 "src/parser.tab.c"
    break;

  case 45: /* compound_op: SLASH_EQ  */
#line 1000 "src/parser.y"
               { (yyval.op_char) = '/'; }
#line 4291 "src/parser.tab.c"
    break;

  case 46: /* lvalue: variable_name  */
#line 1004 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4297 "src/parser.tab.c"
    break;

  case 47: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 1005 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4303 "src/parser.tab.c"
    break;

  case 48: /* lvalue: lvalue DOT dot_field_name  */
#line 1006 "src/parser.y"
                                             { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4309 "src/parser.tab.c"
    break;

  case 49: /* variable_name: IDENT  */
#line 1010 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 4315 "src/parser.tab.c"
    break;

  case 50: /* variable_name: END  */
#line 1011 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 4321 "src/parser.tab.c"
    break;

  case 51: /* variable_name: NEXT  */
#line 1012 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 4327 "src/parser.tab.c"
    break;

  case 52: /* $@1: %empty  */
#line 1023 "src/parser.y"
             { lexer_begin_lens_content(ctx->active_lexer); }
#line 4333 "src/parser.tab.c"
    break;

  case 53: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 1023 "src/parser.y"
                                                                                  {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 4341 "src/parser.tab.c"
    break;

  case 54: /* comparison_lens: MODIFIER_PREFIX  */
#line 1031 "src/parser.y"
                      { (yyval.modifier) = parse_modifier_use((yyvsp[0].text)); }
#line 4347 "src/parser.tab.c"
    break;

  case 55: /* modifier_name: modifier_word  */
#line 1035 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 4353 "src/parser.tab.c"
    break;

  case 56: /* modifier_name: modifier_name modifier_word  */
#line 1036 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 4359 "src/parser.tab.c"
    break;

  case 57: /* modifier_word: IDENT  */
#line 1040 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4365 "src/parser.tab.c"
    break;

  case 58: /* modifier_word: TO  */
#line 1041 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 4371 "src/parser.tab.c"
    break;

  case 59: /* modifier_word: END  */
#line 1042 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 4377 "src/parser.tab.c"
    break;

  case 60: /* modifier_word: NEXT  */
#line 1043 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 4383 "src/parser.tab.c"
    break;

  case 61: /* print_statement: PRINT expression  */
#line 1047 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 4389 "src/parser.tab.c"
    break;

  case 62: /* print_statement: PRINT TO ERROR_VALUE expression  */
#line 1053 "src/parser.y"
                                      { (yyval.stmt) = ast_print_error((yyvsp[0].expr)); }
#line 4395 "src/parser.tab.c"
    break;

  case 63: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 1057 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 4401 "src/parser.tab.c"
    break;

  case 64: /* call_statement: TRY expression  */
#line 1060 "src/parser.y"
                     { (yyval.stmt) = ast_expr_stmt(expr_at(ast_unary(copy_const("try"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column)); }
#line 4407 "src/parser.tab.c"
    break;

  case 65: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1061 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 4418 "src/parser.tab.c"
    break;

  case 66: /* call_statement: lvalue DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1067 "src/parser.y"
                                                       {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 4428 "src/parser.tab.c"
    break;

  case 67: /* call_statement: lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1072 "src/parser.y"
                                                                 {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 4442 "src/parser.tab.c"
    break;

  case 68: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1081 "src/parser.y"
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
#line 4457 "src/parser.tab.c"
    break;

  case 69: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 1094 "src/parser.y"
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
#line 4480 "src/parser.tab.c"
    break;

  case 70: /* for_end: END FOR NEWLINE  */
#line 1123 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 4486 "src/parser.tab.c"
    break;

  case 71: /* for_end: NEXT NEWLINE  */
#line 1124 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 4492 "src/parser.tab.c"
    break;

  case 72: /* for_end: NEXT variable_name NEWLINE  */
#line 1125 "src/parser.y"
                                 { (yyval.text) = (yyvsp[-1].text); }
#line 4498 "src/parser.tab.c"
    break;

  case 73: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list for_end  */
#line 1129 "src/parser.y"
                                                             {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4507 "src/parser.tab.c"
    break;

  case 74: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list for_end  */
#line 1133 "src/parser.y"
                                                                  {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4516 "src/parser.tab.c"
    break;

  case 75: /* for_each_statement: FOR IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1149 "src/parser.y"
                                                                         {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { YYERROR; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4526 "src/parser.tab.c"
    break;

  case 76: /* for_each_statement: FOR EACH IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1154 "src/parser.y"
                                                                              {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { YYERROR; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4536 "src/parser.tab.c"
    break;

  case 77: /* for_each_statement: FOR IDENT OP_EQ expression TO expression NEWLINE statement_list for_end  */
#line 1162 "src/parser.y"
                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].expr), NULL, (yyvsp[-1].stmt_list));
      }
#line 4545 "src/parser.tab.c"
    break;

  case 78: /* for_each_statement: FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list for_end  */
#line 1166 "src/parser.y"
                                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-9].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-9].text), (yyvsp[-7].expr), (yyvsp[-5].expr), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4554 "src/parser.tab.c"
    break;

  case 79: /* do_loop_statement: DO NEWLINE statement_list UNTIL expression NEWLINE  */
#line 1187 "src/parser.y"
                                                         {
        (yyval.stmt) = ast_do_loop((yyvsp[-3].stmt_list), (yyvsp[-1].expr));
      }
#line 4562 "src/parser.tab.c"
    break;

  case 80: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 1193 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 4570 "src/parser.tab.c"
    break;

  case 81: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 1199 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 4578 "src/parser.tab.c"
    break;

  case 82: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 1205 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 4586 "src/parser.tab.c"
    break;

  case 83: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 1208 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 4594 "src/parser.tab.c"
    break;

  case 84: /* consider_else_opt: %empty  */
#line 1214 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4600 "src/parser.tab.c"
    break;

  case 85: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 1215 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 4606 "src/parser.tab.c"
    break;

  case 86: /* consider_statement_list: %empty  */
#line 1219 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4612 "src/parser.tab.c"
    break;

  case 87: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 1220 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 4618 "src/parser.tab.c"
    break;

  case 88: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 1221 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 4624 "src/parser.tab.c"
    break;

  case 89: /* consider_body_statement: assignment NEWLINE  */
#line 1225 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4630 "src/parser.tab.c"
    break;

  case 90: /* consider_body_statement: print_statement NEWLINE  */
#line 1226 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4636 "src/parser.tab.c"
    break;

  case 91: /* consider_body_statement: call_statement NEWLINE  */
#line 1227 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4642 "src/parser.tab.c"
    break;

  case 92: /* consider_body_statement: with_lock_statement  */
#line 1228 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4648 "src/parser.tab.c"
    break;

  case 93: /* consider_body_statement: window_block  */
#line 1229 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4654 "src/parser.tab.c"
    break;

  case 94: /* consider_body_statement: for_each_statement  */
#line 1230 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4660 "src/parser.tab.c"
    break;

  case 95: /* consider_body_statement: while_statement  */
#line 1231 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4666 "src/parser.tab.c"
    break;

  case 96: /* consider_body_statement: do_loop_statement  */
#line 1232 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4672 "src/parser.tab.c"
    break;

  case 97: /* consider_body_statement: consider_statement  */
#line 1233 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4678 "src/parser.tab.c"
    break;

  case 98: /* consider_body_statement: function_statement  */
#line 1234 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4684 "src/parser.tab.c"
    break;

  case 99: /* consider_body_statement: modifier_statement  */
#line 1235 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4690 "src/parser.tab.c"
    break;

  case 100: /* consider_body_statement: program_statement  */
#line 1236 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4696 "src/parser.tab.c"
    break;

  case 101: /* consider_body_statement: library_statement  */
#line 1237 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4702 "src/parser.tab.c"
    break;

  case 102: /* consider_body_statement: use_statement NEWLINE  */
#line 1238 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4708 "src/parser.tab.c"
    break;

  case 103: /* consider_body_statement: watch_statement  */
#line 1239 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4714 "src/parser.tab.c"
    break;

  case 104: /* consider_body_statement: unwatch_statement NEWLINE  */
#line 1240 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4720 "src/parser.tab.c"
    break;

  case 105: /* consider_body_statement: without_watchers_statement  */
#line 1241 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4726 "src/parser.tab.c"
    break;

  case 106: /* consider_body_statement: on_error_statement NEWLINE  */
#line 1242 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4732 "src/parser.tab.c"
    break;

  case 107: /* consider_body_statement: error_statement NEWLINE  */
#line 1243 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4738 "src/parser.tab.c"
    break;

  case 108: /* consider_body_statement: return_statement NEWLINE  */
#line 1244 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4744 "src/parser.tab.c"
    break;

  case 109: /* consider_body_statement: label_statement NEWLINE  */
#line 1245 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4750 "src/parser.tab.c"
    break;

  case 110: /* consider_body_statement: goto_statement NEWLINE  */
#line 1246 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4756 "src/parser.tab.c"
    break;

  case 111: /* consider_body_statement: gosub_statement NEWLINE  */
#line 1247 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4762 "src/parser.tab.c"
    break;

  case 112: /* consider_body_statement: break_statement NEWLINE  */
#line 1248 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4768 "src/parser.tab.c"
    break;

  case 113: /* consider_body_statement: continue_statement NEWLINE  */
#line 1249 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4774 "src/parser.tab.c"
    break;

  case 114: /* consider_body_statement: if_statement  */
#line 1250 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4780 "src/parser.tab.c"
    break;

  case 115: /* consider_body_statement: DIM  */
#line 1256 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 4793 "src/parser.tab.c"
    break;

  case 116: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1267 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4801 "src/parser.tab.c"
    break;

  case 117: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1270 "src/parser.y"
                                                                                                            {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4810 "src/parser.tab.c"
    break;

  case 118: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN COLON type_lock NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1277 "src/parser.y"
                                                                                                                  {
        char who[160];
        snprintf(who, sizeof who, "%s(...)", (yyvsp[-10].text));
        if (!type_lock_check(ctx, who, (yyvsp[-5].type_lock).name, (yyvsp[-5].type_lock).arg, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                             (yylsp[-5]).last_line, (yylsp[-5]).last_column)) {
            YYERROR;
        }
        (yyval.stmt) = ast_function_returning((yyvsp[-10].text), (yyvsp[-8].name_list), (yyvsp[-5].type_lock), (yyvsp[-3].stmt_list));
      }
#line 4824 "src/parser.tab.c"
    break;

  case 119: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN COLON type_lock NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1286 "src/parser.y"
                                                                                                                            {
        char who[160];
        snprintf(who, sizeof who, "%s(...)", (yyvsp[-10].text));
        if (!type_lock_check(ctx, who, (yyvsp[-5].type_lock).name, (yyvsp[-5].type_lock).arg, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                             (yylsp[-5]).last_line, (yylsp[-5]).last_column)) {
            YYERROR;
        }
        (yyval.stmt) = ast_function_returning((yyvsp[-10].text), (yyvsp[-8].name_list), (yyvsp[-5].type_lock), (yyvsp[-3].stmt_list));
      }
#line 4838 "src/parser.tab.c"
    break;

  case 120: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 1298 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 4846 "src/parser.tab.c"
    break;

  case 121: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 1301 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 4854 "src/parser.tab.c"
    break;

  case 122: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 1307 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4862 "src/parser.tab.c"
    break;

  case 123: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 1313 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 4870 "src/parser.tab.c"
    break;

  case 124: /* use_statement: USE IDENT  */
#line 1319 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4876 "src/parser.tab.c"
    break;

  case 125: /* use_statement: LOAD IDENT  */
#line 1320 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4882 "src/parser.tab.c"
    break;

  case 126: /* use_statement: USE STRING  */
#line 1321 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4888 "src/parser.tab.c"
    break;

  case 127: /* use_statement: LOAD STRING  */
#line 1322 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4894 "src/parser.tab.c"
    break;

  case 128: /* use_statement: LOAD IDENT AS IDENT  */
#line 1323 "src/parser.y"
                          { (yyval.stmt) = ast_use((yyvsp[-2].text), NULL, (yyvsp[0].text)); }
#line 4900 "src/parser.tab.c"
    break;

  case 129: /* use_statement: USE IDENT IDENT STRING  */
#line 1324 "src/parser.y"
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
#line 4921 "src/parser.tab.c"
    break;

  case 130: /* use_statement: LOAD IDENT IDENT STRING  */
#line 1340 "src/parser.y"
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
#line 4942 "src/parser.tab.c"
    break;

  case 131: /* use_statement: LOAD IDENT IDENT STRING AS IDENT  */
#line 1356 "src/parser.y"
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
#line 4965 "src/parser.tab.c"
    break;

  case 132: /* modifier_signature: modifier_name  */
#line 1377 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 4971 "src/parser.tab.c"
    break;

  case 133: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 1378 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 4977 "src/parser.tab.c"
    break;

  case 134: /* modifier_context: IDENT  */
#line 1382 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4983 "src/parser.tab.c"
    break;

  case 135: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1386 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4991 "src/parser.tab.c"
    break;

  case 136: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 1389 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 4999 "src/parser.tab.c"
    break;

  case 137: /* watch_statement: WATCH IDENT LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1397 "src/parser.y"
                                                                                           {
        (yyval.stmt) = ast_watch((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 5007 "src/parser.tab.c"
    break;

  case 138: /* unwatch_statement: UNWATCH expression  */
#line 1403 "src/parser.y"
                         { (yyval.stmt) = ast_unwatch((yyvsp[0].expr)); }
#line 5013 "src/parser.tab.c"
    break;

  case 139: /* watch_target_list: watch_target_path  */
#line 1407 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5019 "src/parser.tab.c"
    break;

  case 140: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 1408 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5025 "src/parser.tab.c"
    break;

  case 141: /* @2: %empty  */
#line 1428 "src/parser.y"
      { (yyval.stmt) = NULL;   /* the mid-rule carries no value; typed so bison stays quiet */
        server_head_note(ctx, (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column); }
#line 5032 "src/parser.tab.c"
    break;

  case 142: /* server_statement: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE @2 server_item_list END IDENT NEWLINE  */
#line 1430 "src/parser.y"
                                         {
        ctx->bad_block_word[0] = '\0';
        (yyval.stmt) = ast_server((yyvsp[-10].text), (yyvsp[-9].text), (yyvsp[-7].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 5041 "src/parser.tab.c"
    break;

  case 143: /* @3: %empty  */
#line 1435 "src/parser.y"
      { (yyval.stmt) = NULL;   /* the mid-rule carries no value; typed so bison stays quiet */
        server_head_note(ctx, (yyvsp[-4].text), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5048 "src/parser.tab.c"
    break;

  case 144: /* server_statement: IDENT IDENT LPAREN RPAREN NEWLINE @3 server_item_list END IDENT NEWLINE  */
#line 1437 "src/parser.y"
                                         {
        ctx->bad_block_word[0] = '\0';
        (yyval.stmt) = ast_server((yyvsp[-9].text), (yyvsp[-8].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 5057 "src/parser.tab.c"
    break;

  case 145: /* gsql_block: DATABASE_OPEN IDENT LPAREN record_field_list RPAREN NEWLINE  */
#line 1457 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_database((yyvsp[-4].text), (yyvsp[-2].record_field_list), (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 5065 "src/parser.tab.c"
    break;

  case 146: /* gsql_block: DATABASE_OPEN IDENT LPAREN RPAREN NEWLINE  */
#line 1460 "src/parser.y"
                                                {
        (yyval.stmt) = ast_database((yyvsp[-3].text), ast_record_field_list_empty(), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5073 "src/parser.tab.c"
    break;

  case 147: /* gsql_block: SQL_OPEN IDENT ON IDENT NEWLINE SQL_TEXT END IDENT NEWLINE  */
#line 1463 "src/parser.y"
                                                                 {
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_sql((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].text), (yylsp[-3]).first_line, (yylsp[-3]).first_column, (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5082 "src/parser.tab.c"
    break;

  case 148: /* gsql_block: SQL_OPEN IDENT ON IDENT DOT IDENT NEWLINE SQL_TEXT END IDENT NEWLINE  */
#line 1469 "src/parser.y"
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
#line 5099 "src/parser.tab.c"
    break;

  case 149: /* window_block: WINDOW_OPEN IDENT LPAREN record_field_list RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1484 "src/parser.y"
                                                                                                   {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), NULL, NULL, NULL, (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5108 "src/parser.tab.c"
    break;

  case 150: /* window_block: WINDOW_OPEN IDENT LPAREN RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1488 "src/parser.y"
                                                                                 {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text), NULL, NULL, NULL,
                        (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5118 "src/parser.tab.c"
    break;

  case 151: /* window_block: WINDOW_OPEN IDENT LPAREN record_field_list RPAREN TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1498 "src/parser.y"
                                                                                                                  {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-11].text), (yyvsp[-9].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text),
                        (yylsp[-12]).first_line, (yylsp[-12]).first_column);
      }
#line 5128 "src/parser.tab.c"
    break;

  case 152: /* window_item_list: %empty  */
#line 1506 "src/parser.y"
             { (yyval.window_item_list) = ast_window_item_list_empty(); }
#line 5134 "src/parser.tab.c"
    break;

  case 153: /* window_item_list: window_item_list NEWLINE  */
#line 1507 "src/parser.y"
                               { (yyval.window_item_list) = (yyvsp[-1].window_item_list); }
#line 5140 "src/parser.tab.c"
    break;

  case 154: /* window_item_list: window_item_list window_item  */
#line 1508 "src/parser.y"
                                   { (yyval.window_item_list) = ast_window_item_list_append((yyvsp[-1].window_item_list), (yyvsp[0].window_item)); }
#line 5146 "src/parser.tab.c"
    break;

  case 155: /* window_item: IDENT LPAREN RPAREN NEWLINE  */
#line 1513 "src/parser.y"
                                  {
        (yyval.window_item) = ast_window_leaf((yyvsp[-3].text), NULL, ast_record_field_list_empty(),
                             (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 5155 "src/parser.tab.c"
    break;

  case 156: /* window_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE  */
#line 1518 "src/parser.y"
                                                          {
        (yyval.window_item) = ast_window_leaf((yyvsp[-5].text), (yyvsp[-4].text), (yyvsp[-2].record_field_list), (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 5163 "src/parser.tab.c"
    break;

  case 157: /* window_item: IDENT IDENT LPAREN RPAREN NEWLINE  */
#line 1522 "src/parser.y"
                                        {
        (yyval.window_item) = ast_window_leaf((yyvsp[-4].text), (yyvsp[-3].text), ast_record_field_list_empty(),
                             (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5172 "src/parser.tab.c"
    break;

  case 158: /* window_item: IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1527 "src/parser.y"
                                                       {
        (yyval.window_item) = ast_window_container((yyvsp[-5].text), NULL, ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                  (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 5181 "src/parser.tab.c"
    break;

  case 159: /* window_item: IDENT LPAREN record_field_list RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1532 "src/parser.y"
                                                                                       {
        (yyval.window_item) = ast_window_container((yyvsp[-8].text), NULL, (yyvsp[-6].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5189 "src/parser.tab.c"
    break;

  case 160: /* window_item: IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1546 "src/parser.y"
                                                             {
        (yyval.window_item) = ast_window_named_form((yyvsp[-6].text), (yyvsp[-5].text), NULL, NULL, NULL, (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                   (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5198 "src/parser.tab.c"
    break;

  case 161: /* window_item: IDENT IDENT STRING NEWLINE window_item_list END IDENT NEWLINE  */
#line 1561 "src/parser.y"
                                                                    {
        (yyval.window_item) = ast_window_named_container((yyvsp[-7].text), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                        (yylsp[-7]).first_line, (yylsp[-7]).first_column);
      }
#line 5207 "src/parser.tab.c"
    break;

  case 162: /* window_item: IDENT STRING NEWLINE window_item_list END IDENT NEWLINE  */
#line 1566 "src/parser.y"
                                                              {
        (yyval.window_item) = ast_window_container((yyvsp[-6].text), (yyvsp[-5].text), ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                  (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5216 "src/parser.tab.c"
    break;

  case 163: /* window_item: IDENT TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1577 "src/parser.y"
                                                                      {
        (yyval.window_item) = ast_window_form((yyvsp[-8].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                             (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5225 "src/parser.tab.c"
    break;

  case 164: /* window_item: IDENT IDENT TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1592 "src/parser.y"
                                                                            {
        (yyval.window_item) = ast_window_named_form((yyvsp[-9].text), (yyvsp[-8].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                   (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5234 "src/parser.tab.c"
    break;

  case 165: /* window_item: ON IDENT DOT IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1597 "src/parser.y"
                                                               {
        (yyval.window_item) = ast_window_handler((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5242 "src/parser.tab.c"
    break;

  case 166: /* server_item_list: %empty  */
#line 1603 "src/parser.y"
             { (yyval.server_item_list) = ast_server_item_list_empty(); }
#line 5248 "src/parser.tab.c"
    break;

  case 167: /* server_item_list: server_item_list NEWLINE  */
#line 1604 "src/parser.y"
                               { (yyval.server_item_list) = (yyvsp[-1].server_item_list); }
#line 5254 "src/parser.tab.c"
    break;

  case 168: /* server_item_list: server_item_list server_item  */
#line 1605 "src/parser.y"
                                   { (yyval.server_item_list) = ast_server_item_list_append((yyvsp[-1].server_item_list), (yyvsp[0].server_item)); }
#line 5260 "src/parser.tab.c"
    break;

  case 169: /* server_item: IDENT server_string_list NEWLINE  */
#line 1609 "src/parser.y"
                                       {
        (yyval.server_item) = ast_server_directive((yyvsp[-2].text), (yyvsp[-1].name_list), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 5268 "src/parser.tab.c"
    break;

  case 170: /* server_item: IDENT STRING LPAREN parameter_list_opt RPAREN NEWLINE statement_list END IDENT NEWLINE  */
#line 1612 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_handler((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5276 "src/parser.tab.c"
    break;

  case 171: /* server_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1615 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_site((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5284 "src/parser.tab.c"
    break;

  case 172: /* server_item: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1618 "src/parser.y"
                                                                           {
        (yyval.server_item) = ast_server_site((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5292 "src/parser.tab.c"
    break;

  case 173: /* server_item: ON IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1621 "src/parser.y"
                                                     {
        (yyval.server_item) = ast_server_hook((yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5300 "src/parser.tab.c"
    break;

  case 174: /* server_string_list: STRING  */
#line 1627 "src/parser.y"
             { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5306 "src/parser.tab.c"
    break;

  case 175: /* server_string_list: server_string_list COMMA STRING  */
#line 1628 "src/parser.y"
                                      { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5312 "src/parser.tab.c"
    break;

  case 176: /* watch_target_path: variable_name  */
#line 1632 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 5318 "src/parser.tab.c"
    break;

  case 177: /* watch_target_path: watch_target_path DOT IDENT  */
#line 1633 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 5324 "src/parser.tab.c"
    break;

  case 178: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 1637 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 5332 "src/parser.tab.c"
    break;

  case 179: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 1643 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 5338 "src/parser.tab.c"
    break;

  case 180: /* on_error_statement: ON ERROR_VALUE GOTO NEXT  */
#line 1644 "src/parser.y"
                               { (yyval.stmt) = ast_on_error_goto_next(); }
#line 5344 "src/parser.tab.c"
    break;

  case 181: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 1645 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 5350 "src/parser.tab.c"
    break;

  case 182: /* on_error_statement: ON IDENT GOTO NEXT  */
#line 1646 "src/parser.y"
                         {
        if (!warn_channel_ok(ctx, (yyvsp[-2].text), (yylsp[-2]).first_line, (yylsp[-2]).first_column)) { YYERROR; }
        free((yyvsp[-2].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_NEXT);
      }
#line 5360 "src/parser.tab.c"
    break;

  case 183: /* on_error_statement: ON IDENT GOTO IDENT  */
#line 1651 "src/parser.y"
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
#line 5378 "src/parser.tab.c"
    break;

  case 184: /* on_error_statement: ON IDENT STOP  */
#line 1664 "src/parser.y"
                    {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_STOP);
      }
#line 5388 "src/parser.tab.c"
    break;

  case 185: /* on_error_statement: ON IDENT PRINT  */
#line 1669 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_PRINT);
      }
#line 5398 "src/parser.tab.c"
    break;

  case 186: /* on_error_statement: ON IDENT IDENT  */
#line 1674 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        int mode = warn_mode_word(ctx, (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column);
        if (mode < 0) { free((yyvsp[-1].text)); free((yyvsp[0].text)); YYERROR; }
        free((yyvsp[-1].text)); free((yyvsp[0].text));
        (yyval.stmt) = ast_on_warning(mode);
      }
#line 5410 "src/parser.tab.c"
    break;

  case 187: /* error_statement: ERROR_VALUE expression  */
#line 1684 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 5416 "src/parser.tab.c"
    break;

  case 188: /* return_statement: RETURN  */
#line 1688 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 5422 "src/parser.tab.c"
    break;

  case 189: /* return_statement: RETURN expression  */
#line 1689 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 5428 "src/parser.tab.c"
    break;

  case 190: /* label_statement: variable_name COLON  */
#line 1693 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 5434 "src/parser.tab.c"
    break;

  case 191: /* goto_statement: GOTO variable_name  */
#line 1700 "src/parser.y"
                         { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 5440 "src/parser.tab.c"
    break;

  case 192: /* gosub_statement: GOSUB variable_name  */
#line 1704 "src/parser.y"
                          { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 5446 "src/parser.tab.c"
    break;

  case 193: /* break_statement: BREAK  */
#line 1713 "src/parser.y"
            { (yyval.stmt) = ast_break(NULL); }
#line 5452 "src/parser.tab.c"
    break;

  case 194: /* break_statement: BREAK IDENT  */
#line 1714 "src/parser.y"
                  { (yyval.stmt) = ast_break((yyvsp[0].text)); }
#line 5458 "src/parser.tab.c"
    break;

  case 195: /* continue_statement: CONTINUE  */
#line 1718 "src/parser.y"
               { (yyval.stmt) = ast_continue(NULL); }
#line 5464 "src/parser.tab.c"
    break;

  case 196: /* continue_statement: CONTINUE IDENT  */
#line 1719 "src/parser.y"
                     { (yyval.stmt) = ast_continue((yyvsp[0].text)); }
#line 5470 "src/parser.tab.c"
    break;

  case 197: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1723 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 5479 "src/parser.tab.c"
    break;

  case 198: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1727 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 5488 "src/parser.tab.c"
    break;

  case 199: /* if_block_tail: END IF NEWLINE  */
#line 1734 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 5496 "src/parser.tab.c"
    break;

  case 200: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 1737 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 5504 "src/parser.tab.c"
    break;

  case 201: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1740 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 5512 "src/parser.tab.c"
    break;

  case 202: /* if_block_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1749 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5524 "src/parser.tab.c"
    break;

  case 203: /* if_inline_tail: %empty  */
#line 1759 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 5532 "src/parser.tab.c"
    break;

  case 204: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 1762 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 5540 "src/parser.tab.c"
    break;

  case 205: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1765 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 5548 "src/parser.tab.c"
    break;

  case 206: /* if_inline_tail: ELSE IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1771 "src/parser.y"
                                                                      {
        AstStmt *inner = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5560 "src/parser.tab.c"
    break;

  case 207: /* if_inline_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1778 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5572 "src/parser.tab.c"
    break;

  case 208: /* inline_statement: assignment  */
#line 1788 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5578 "src/parser.tab.c"
    break;

  case 209: /* inline_statement: print_statement  */
#line 1789 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5584 "src/parser.tab.c"
    break;

  case 210: /* inline_statement: call_statement  */
#line 1790 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5590 "src/parser.tab.c"
    break;

  case 211: /* inline_statement: use_statement  */
#line 1791 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5596 "src/parser.tab.c"
    break;

  case 212: /* inline_statement: on_error_statement  */
#line 1792 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5602 "src/parser.tab.c"
    break;

  case 213: /* inline_statement: error_statement  */
#line 1793 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5608 "src/parser.tab.c"
    break;

  case 214: /* inline_statement: return_statement  */
#line 1794 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5614 "src/parser.tab.c"
    break;

  case 215: /* inline_statement: goto_statement  */
#line 1795 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5620 "src/parser.tab.c"
    break;

  case 216: /* inline_statement: gosub_statement  */
#line 1796 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5626 "src/parser.tab.c"
    break;

  case 217: /* inline_statement: break_statement  */
#line 1797 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5632 "src/parser.tab.c"
    break;

  case 218: /* inline_statement: continue_statement  */
#line 1798 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5638 "src/parser.tab.c"
    break;

  case 219: /* expression: or_expression  */
#line 1806 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 5644 "src/parser.tab.c"
    break;

  case 220: /* expression: TRY expression  */
#line 1807 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_unary(copy_const("try"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5650 "src/parser.tab.c"
    break;

  case 221: /* or_expression: and_expression  */
#line 1811 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5656 "src/parser.tab.c"
    break;

  case 222: /* or_expression: or_expression OR and_expression  */
#line 1812 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5662 "src/parser.tab.c"
    break;

  case 223: /* and_expression: not_expression  */
#line 1816 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5668 "src/parser.tab.c"
    break;

  case 224: /* and_expression: and_expression AND not_expression  */
#line 1817 "src/parser.y"
                                        { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5674 "src/parser.tab.c"
    break;

  case 225: /* not_expression: comparison_expression  */
#line 1839 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 5680 "src/parser.tab.c"
    break;

  case 226: /* not_expression: NOT not_expression  */
#line 1840 "src/parser.y"
                         { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5686 "src/parser.tab.c"
    break;

  case 227: /* comparison_expression: set_expression  */
#line 1844 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5692 "src/parser.tab.c"
    break;

  case 228: /* comparison_expression: set_expression comparison_operator set_expression  */
#line 1845 "src/parser.y"
                                                        { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5698 "src/parser.tab.c"
    break;

  case 229: /* comparison_expression: set_expression comparison_lens comparison_operator set_expression  */
#line 1846 "src/parser.y"
                                                                        {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 5706 "src/parser.tab.c"
    break;

  case 230: /* set_expression: additive_expression  */
#line 1852 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 5712 "src/parser.tab.c"
    break;

  case 231: /* set_expression: set_expression EXCLUDING additive_expression  */
#line 1853 "src/parser.y"
                                                   { (yyval.expr) = expr_at(ast_binary(copy_const("excluding"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5718 "src/parser.tab.c"
    break;

  case 232: /* set_expression: set_expression INTERSECTING additive_expression  */
#line 1854 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("intersecting"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5724 "src/parser.tab.c"
    break;

  case 233: /* additive_expression: multiplicative_expression  */
#line 1858 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 5730 "src/parser.tab.c"
    break;

  case 234: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 1859 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5736 "src/parser.tab.c"
    break;

  case 235: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 1860 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5742 "src/parser.tab.c"
    break;

  case 236: /* multiplicative_expression: unary_expression  */
#line 1864 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 5748 "src/parser.tab.c"
    break;

  case 237: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 1865 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5754 "src/parser.tab.c"
    break;

  case 238: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 1866 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5760 "src/parser.tab.c"
    break;

  case 239: /* unary_expression: postfix_expression  */
#line 1870 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 5766 "src/parser.tab.c"
    break;

  case 240: /* unary_expression: MINUS unary_expression  */
#line 1871 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5772 "src/parser.tab.c"
    break;

  case 241: /* unary_expression: MODIFIER_PREFIX unary_expression  */
#line 1878 "src/parser.y"
                                       {
        (yyval.expr) = expr_at(ast_modifier_apply(parse_modifier_use((yyvsp[-1].text)), (yyvsp[0].expr)),
                     (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 5781 "src/parser.tab.c"
    break;

  case 242: /* unary_expression: NEW postfix_expression  */
#line 1882 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5787 "src/parser.tab.c"
    break;

  case 243: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 1883 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 5793 "src/parser.tab.c"
    break;

  case 244: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 1884 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5799 "src/parser.tab.c"
    break;

  case 245: /* postfix_expression: primary  */
#line 1888 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 5805 "src/parser.tab.c"
    break;

  case 246: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 1889 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5811 "src/parser.tab.c"
    break;

  case 247: /* postfix_expression: postfix_expression DOT dot_field_name  */
#line 1890 "src/parser.y"
                                            { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5817 "src/parser.tab.c"
    break;

  case 248: /* postfix_expression: postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1891 "src/parser.y"
                                                                   {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        (yyval.expr) = expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5827 "src/parser.tab.c"
    break;

  case 249: /* postfix_expression: postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1896 "src/parser.y"
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
#line 5843 "src/parser.tab.c"
    break;

  case 250: /* comparison_operator: OP_EQ  */
#line 1910 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 5849 "src/parser.tab.c"
    break;

  case 251: /* comparison_operator: OP_NE  */
#line 1911 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 5855 "src/parser.tab.c"
    break;

  case 252: /* comparison_operator: OP_GT  */
#line 1912 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 5861 "src/parser.tab.c"
    break;

  case 253: /* comparison_operator: OP_LT  */
#line 1913 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 5867 "src/parser.tab.c"
    break;

  case 254: /* comparison_operator: OP_GE  */
#line 1914 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 5873 "src/parser.tab.c"
    break;

  case 255: /* comparison_operator: OP_LE  */
#line 1915 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 5879 "src/parser.tab.c"
    break;

  case 256: /* comparison_operator: OP_NGT  */
#line 1916 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 5885 "src/parser.tab.c"
    break;

  case 257: /* comparison_operator: OP_NLT  */
#line 1917 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 5891 "src/parser.tab.c"
    break;

  case 258: /* comparison_operator: OP_NGE  */
#line 1918 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 5897 "src/parser.tab.c"
    break;

  case 259: /* comparison_operator: OP_NLE  */
#line 1919 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 5903 "src/parser.tab.c"
    break;

  case 260: /* primary: MODIFIER_VALUE  */
#line 1923 "src/parser.y"
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
#line 5926 "src/parser.tab.c"
    break;

  case 261: /* primary: NUMBER  */
#line 1941 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5932 "src/parser.tab.c"
    break;

  case 262: /* primary: WATCHERS LPAREN RPAREN  */
#line 1942 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_call(copy_const("watchers"), ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5938 "src/parser.tab.c"
    break;

  case 263: /* primary: duration_terms  */
#line 1943 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5944 "src/parser.tab.c"
    break;

  case 264: /* primary: STRING  */
#line 1944 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5950 "src/parser.tab.c"
    break;

  case 265: /* primary: variable_name ident_suffix  */
#line 1945 "src/parser.y"
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
#line 5972 "src/parser.tab.c"
    break;

  case 266: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1962 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 5983 "src/parser.tab.c"
    break;

  case 267: /* primary: ERROR_VALUE  */
#line 1968 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5989 "src/parser.tab.c"
    break;

  case 268: /* primary: TRUE  */
#line 1969 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5995 "src/parser.tab.c"
    break;

  case 269: /* primary: FALSE  */
#line 1970 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6001 "src/parser.tab.c"
    break;

  case 270: /* primary: NOTHING  */
#line 1971 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6007 "src/parser.tab.c"
    break;

  case 271: /* primary: UNKNOWN_VALUE  */
#line 1972 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6013 "src/parser.tab.c"
    break;

  case 272: /* primary: LPAREN expression RPAREN  */
#line 1973 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 6019 "src/parser.tab.c"
    break;

  case 273: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 1974 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 6025 "src/parser.tab.c"
    break;

  case 274: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 1975 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 6031 "src/parser.tab.c"
    break;

  case 275: /* primary: record_literal  */
#line 1976 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 6037 "src/parser.tab.c"
    break;

  case 276: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 1980 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 6043 "src/parser.tab.c"
    break;

  case 277: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 1981 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 6049 "src/parser.tab.c"
    break;

  case 278: /* ident_suffix: %empty  */
#line 1985 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 6059 "src/parser.tab.c"
    break;

  case 279: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 1990 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 6069 "src/parser.tab.c"
    break;

  case 280: /* ident_suffix: DOT dot_field_name ident_dot_suffix  */
#line 1995 "src/parser.y"
                                          {
        /* dot_field_name, not IDENT: a keyword is a legal FIELD name after a
         * dot, because nothing but a name can appear there. */
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 6080 "src/parser.tab.c"
    break;

  case 281: /* ident_suffix: DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 2001 "src/parser.y"
                                                          {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        (yyval.ident_suffix).kind = IDENT_SUFFIX_METHOD;
        (yyval.ident_suffix).name = (yyvsp[-3].text);
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 6094 "src/parser.tab.c"
    break;

  case 282: /* ident_dot_suffix: %empty  */
#line 2013 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 6104 "src/parser.tab.c"
    break;

  case 283: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 2018 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 6114 "src/parser.tab.c"
    break;

  case 284: /* duration_terms: NUMBER IDENT  */
#line 2026 "src/parser.y"
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
#line 6129 "src/parser.tab.c"
    break;

  case 285: /* duration_terms: duration_terms NUMBER IDENT  */
#line 2036 "src/parser.y"
                                  {
        char *bad = NULL;
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].numlit).value, (yyvsp[0].text), &bad);
        if (bad) {
            duration_unit_error(ctx, bad, (yylsp[0]).first_line, (yylsp[0]).first_column,
                                (yylsp[0]).last_line, (yylsp[0]).last_column);
            YYERROR;
        }
      }
#line 6143 "src/parser.tab.c"
    break;

  case 286: /* argument_list_opt: %empty  */
#line 2048 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 6149 "src/parser.tab.c"
    break;

  case 287: /* argument_list_opt: argument_list  */
#line 2049 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 6155 "src/parser.tab.c"
    break;

  case 288: /* argument_list: expression  */
#line 2053 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 6161 "src/parser.tab.c"
    break;

  case 289: /* argument_list: argument_list COMMA expression  */
#line 2054 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 6167 "src/parser.tab.c"
    break;

  case 290: /* array_argument_list: expression  */
#line 2058 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 6173 "src/parser.tab.c"
    break;

  case 291: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 2059 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 6179 "src/parser.tab.c"
    break;

  case 292: /* parameter_list_opt: %empty  */
#line 2063 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 6185 "src/parser.tab.c"
    break;

  case 293: /* parameter_list_opt: parameter_list  */
#line 2064 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 6191 "src/parser.tab.c"
    break;

  case 294: /* parameter_default: NUMBER  */
#line 2078 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6197 "src/parser.tab.c"
    break;

  case 295: /* parameter_default: MINUS NUMBER  */
#line 2082 "src/parser.y"
                   { (yyval.expr) = expr_at(ast_number(-(yyvsp[0].numlit).value,
                                             (yyvsp[0].numlit).exact == LLONG_MIN ? 0 : -(yyvsp[0].numlit).exact,
                                             (yyvsp[0].numlit).exact == LLONG_MIN ? 0 : (yyvsp[0].numlit).is_exact),
                                  (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6206 "src/parser.tab.c"
    break;

  case 296: /* parameter_default: PLUS NUMBER  */
#line 2086 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6212 "src/parser.tab.c"
    break;

  case 297: /* parameter_default: STRING  */
#line 2087 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6218 "src/parser.tab.c"
    break;

  case 298: /* parameter_default: TRUE  */
#line 2088 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6224 "src/parser.tab.c"
    break;

  case 299: /* parameter_default: FALSE  */
#line 2089 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6230 "src/parser.tab.c"
    break;

  case 300: /* parameter_default: NOTHING  */
#line 2090 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6236 "src/parser.tab.c"
    break;

  case 301: /* parameter_default: UNKNOWN_VALUE  */
#line 2091 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6242 "src/parser.tab.c"
    break;

  case 302: /* parameter_list: IDENT  */
#line 2095 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 6248 "src/parser.tab.c"
    break;

  case 303: /* parameter_list: IDENT OP_EQ parameter_default  */
#line 2096 "src/parser.y"
                                    {
        (yyval.name_list) = ast_name_list_append_default(ast_name_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 6256 "src/parser.tab.c"
    break;

  case 304: /* parameter_list: parameter_list COMMA IDENT  */
#line 2099 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 6262 "src/parser.tab.c"
    break;

  case 305: /* parameter_list: IDENT COLON type_lock  */
#line 2103 "src/parser.y"
                            {
        if (!type_lock_check(ctx, (yyvsp[-2].text), (yyvsp[0].type_lock).name, (yyvsp[0].type_lock).arg, (yylsp[0]).first_line, (yylsp[0]).first_column,
                             (yylsp[0]).last_line, (yylsp[0]).last_column)) {
            YYERROR;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append(ast_name_list_empty(), (yyvsp[-2].text)), (yyvsp[0].type_lock));
      }
#line 6274 "src/parser.tab.c"
    break;

  case 306: /* parameter_list: IDENT COLON type_lock OP_EQ parameter_default  */
#line 2110 "src/parser.y"
                                                    {
        if (!type_lock_check(ctx, (yyvsp[-4].text), (yyvsp[-2].type_lock).name, (yyvsp[-2].type_lock).arg, (yylsp[-2]).first_line, (yylsp[-2]).first_column,
                             (yylsp[-2]).last_line, (yylsp[-2]).last_column)) {
            YYERROR;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append_default(ast_name_list_empty(), (yyvsp[-4].text), (yyvsp[0].expr)), (yyvsp[-2].type_lock));
      }
#line 6286 "src/parser.tab.c"
    break;

  case 307: /* parameter_list: parameter_list COMMA IDENT COLON type_lock  */
#line 2117 "src/parser.y"
                                                 {
        if (!type_lock_check(ctx, (yyvsp[-2].text), (yyvsp[0].type_lock).name, (yyvsp[0].type_lock).arg, (yylsp[0]).first_line, (yylsp[0]).first_column,
                             (yylsp[0]).last_line, (yylsp[0]).last_column)) {
            YYERROR;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append((yyvsp[-4].name_list), (yyvsp[-2].text)), (yyvsp[0].type_lock));
      }
#line 6298 "src/parser.tab.c"
    break;

  case 308: /* parameter_list: parameter_list COMMA IDENT COLON type_lock OP_EQ parameter_default  */
#line 2124 "src/parser.y"
                                                                         {
        if (!type_lock_check(ctx, (yyvsp[-4].text), (yyvsp[-2].type_lock).name, (yyvsp[-2].type_lock).arg, (yylsp[-2]).first_line, (yylsp[-2]).first_column,
                             (yylsp[-2]).last_line, (yylsp[-2]).last_column)) {
            YYERROR;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append_default((yyvsp[-6].name_list), (yyvsp[-4].text), (yyvsp[0].expr)), (yyvsp[-2].type_lock));
      }
#line 6310 "src/parser.tab.c"
    break;

  case 309: /* parameter_list: parameter_list COMMA IDENT OP_EQ parameter_default  */
#line 2131 "src/parser.y"
                                                         {
        (yyval.name_list) = ast_name_list_append_default((yyvsp[-4].name_list), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 6318 "src/parser.tab.c"
    break;

  case 310: /* field_name: dot_field_name  */
#line 2146 "src/parser.y"
                     { (yyval.text) = (yyvsp[0].text); }
#line 6324 "src/parser.tab.c"
    break;

  case 311: /* field_name: STRING  */
#line 2153 "src/parser.y"
             { (yyval.text) = (yyvsp[0].text); }
#line 6330 "src/parser.tab.c"
    break;

  case 312: /* dot_field_name: IDENT  */
#line 2162 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 6336 "src/parser.tab.c"
    break;

  case 313: /* dot_field_name: AS  */
#line 2163 "src/parser.y"
                     { (yyval.text) = kw_name("as"); }
#line 6342 "src/parser.tab.c"
    break;

  case 314: /* dot_field_name: NEXT  */
#line 2164 "src/parser.y"
                     { (yyval.text) = kw_name("next"); }
#line 6348 "src/parser.tab.c"
    break;

  case 315: /* dot_field_name: STOP  */
#line 2165 "src/parser.y"
                     { (yyval.text) = kw_name("stop"); }
#line 6354 "src/parser.tab.c"
    break;

  case 316: /* dot_field_name: ERROR_VALUE  */
#line 2166 "src/parser.y"
                     { (yyval.text) = kw_name("error"); }
#line 6360 "src/parser.tab.c"
    break;

  case 317: /* dot_field_name: END  */
#line 2167 "src/parser.y"
                     { (yyval.text) = kw_name("end"); }
#line 6366 "src/parser.tab.c"
    break;

  case 318: /* dot_field_name: TO  */
#line 2168 "src/parser.y"
                     { (yyval.text) = kw_name("to"); }
#line 6372 "src/parser.tab.c"
    break;

  case 319: /* dot_field_name: IN  */
#line 2169 "src/parser.y"
                     { (yyval.text) = kw_name("in"); }
#line 6378 "src/parser.tab.c"
    break;

  case 320: /* dot_field_name: ON  */
#line 2170 "src/parser.y"
                     { (yyval.text) = kw_name("on"); }
#line 6384 "src/parser.tab.c"
    break;

  case 321: /* dot_field_name: NEW  */
#line 2171 "src/parser.y"
                     { (yyval.text) = kw_name("new"); }
#line 6390 "src/parser.tab.c"
    break;

  case 322: /* dot_field_name: EACH  */
#line 2172 "src/parser.y"
                     { (yyval.text) = kw_name("each"); }
#line 6396 "src/parser.tab.c"
    break;

  case 323: /* dot_field_name: WITH  */
#line 2173 "src/parser.y"
                     { (yyval.text) = kw_name("with"); }
#line 6402 "src/parser.tab.c"
    break;

  case 324: /* dot_field_name: WITHOUT  */
#line 2174 "src/parser.y"
                     { (yyval.text) = kw_name("without"); }
#line 6408 "src/parser.tab.c"
    break;

  case 325: /* dot_field_name: THEN  */
#line 2175 "src/parser.y"
                     { (yyval.text) = kw_name("then"); }
#line 6414 "src/parser.tab.c"
    break;

  case 326: /* dot_field_name: ELSE  */
#line 2176 "src/parser.y"
                     { (yyval.text) = kw_name("else"); }
#line 6420 "src/parser.tab.c"
    break;

  case 327: /* dot_field_name: FOR  */
#line 2177 "src/parser.y"
                     { (yyval.text) = kw_name("for"); }
#line 6426 "src/parser.tab.c"
    break;

  case 328: /* dot_field_name: IF  */
#line 2178 "src/parser.y"
                     { (yyval.text) = kw_name("if"); }
#line 6432 "src/parser.tab.c"
    break;

  case 329: /* dot_field_name: WHILE  */
#line 2179 "src/parser.y"
                     { (yyval.text) = kw_name("while"); }
#line 6438 "src/parser.tab.c"
    break;

  case 330: /* dot_field_name: DO  */
#line 2180 "src/parser.y"
                     { (yyval.text) = kw_name("do"); }
#line 6444 "src/parser.tab.c"
    break;

  case 331: /* dot_field_name: UNTIL  */
#line 2181 "src/parser.y"
                     { (yyval.text) = kw_name("until"); }
#line 6450 "src/parser.tab.c"
    break;

  case 332: /* dot_field_name: PRINT  */
#line 2182 "src/parser.y"
                     { (yyval.text) = kw_name("print"); }
#line 6456 "src/parser.tab.c"
    break;

  case 333: /* dot_field_name: RETURN  */
#line 2183 "src/parser.y"
                     { (yyval.text) = kw_name("return"); }
#line 6462 "src/parser.tab.c"
    break;

  case 334: /* dot_field_name: LOAD  */
#line 2184 "src/parser.y"
                     { (yyval.text) = kw_name("load"); }
#line 6468 "src/parser.tab.c"
    break;

  case 335: /* dot_field_name: USE  */
#line 2185 "src/parser.y"
                     { (yyval.text) = kw_name("use"); }
#line 6474 "src/parser.tab.c"
    break;

  case 336: /* dot_field_name: NOT  */
#line 2186 "src/parser.y"
                     { (yyval.text) = kw_name("not"); }
#line 6480 "src/parser.tab.c"
    break;

  case 337: /* dot_field_name: TRY  */
#line 2187 "src/parser.y"
                     { (yyval.text) = kw_name("try"); }
#line 6486 "src/parser.tab.c"
    break;

  case 338: /* dot_field_name: AND  */
#line 2188 "src/parser.y"
                     { (yyval.text) = kw_name("and"); }
#line 6492 "src/parser.tab.c"
    break;

  case 339: /* dot_field_name: OR  */
#line 2189 "src/parser.y"
                     { (yyval.text) = kw_name("or"); }
#line 6498 "src/parser.tab.c"
    break;

  case 340: /* dot_field_name: TRUE  */
#line 2190 "src/parser.y"
                     { (yyval.text) = kw_name("true"); }
#line 6504 "src/parser.tab.c"
    break;

  case 341: /* dot_field_name: FALSE  */
#line 2191 "src/parser.y"
                     { (yyval.text) = kw_name("false"); }
#line 6510 "src/parser.tab.c"
    break;

  case 342: /* dot_field_name: NOTHING  */
#line 2192 "src/parser.y"
                     { (yyval.text) = kw_name("nothing"); }
#line 6516 "src/parser.tab.c"
    break;

  case 343: /* dot_field_name: BREAK  */
#line 2193 "src/parser.y"
                     { (yyval.text) = kw_name("break"); }
#line 6522 "src/parser.tab.c"
    break;

  case 344: /* dot_field_name: CONTINUE  */
#line 2194 "src/parser.y"
                     { (yyval.text) = kw_name("continue"); }
#line 6528 "src/parser.tab.c"
    break;

  case 345: /* dot_field_name: GOTO  */
#line 2195 "src/parser.y"
                     { (yyval.text) = kw_name("goto"); }
#line 6534 "src/parser.tab.c"
    break;

  case 346: /* dot_field_name: GOSUB  */
#line 2196 "src/parser.y"
                     { (yyval.text) = kw_name("gosub"); }
#line 6540 "src/parser.tab.c"
    break;

  case 347: /* dot_field_name: SPAWN  */
#line 2197 "src/parser.y"
                     { (yyval.text) = kw_name("spawn"); }
#line 6546 "src/parser.tab.c"
    break;

  case 348: /* dot_field_name: EXPORT  */
#line 2198 "src/parser.y"
                     { (yyval.text) = kw_name("export"); }
#line 6552 "src/parser.tab.c"
    break;

  case 349: /* dot_field_name: LIBRARY  */
#line 2199 "src/parser.y"
                     { (yyval.text) = kw_name("library"); }
#line 6558 "src/parser.tab.c"
    break;

  case 350: /* dot_field_name: FUNCTION  */
#line 2200 "src/parser.y"
                     { (yyval.text) = kw_name("function"); }
#line 6564 "src/parser.tab.c"
    break;

  case 351: /* dot_field_name: MODIFIER  */
#line 2201 "src/parser.y"
                     { (yyval.text) = kw_name("modifier"); }
#line 6570 "src/parser.tab.c"
    break;

  case 352: /* dot_field_name: PROGRAM  */
#line 2202 "src/parser.y"
                     { (yyval.text) = kw_name("program"); }
#line 6576 "src/parser.tab.c"
    break;

  case 353: /* dot_field_name: WATCH  */
#line 2203 "src/parser.y"
                     { (yyval.text) = kw_name("watch"); }
#line 6582 "src/parser.tab.c"
    break;

  case 354: /* dot_field_name: WATCHERS  */
#line 2204 "src/parser.y"
                     { (yyval.text) = kw_name("watchers"); }
#line 6588 "src/parser.tab.c"
    break;

  case 355: /* dot_field_name: CONSIDER  */
#line 2205 "src/parser.y"
                     { (yyval.text) = kw_name("consider"); }
#line 6594 "src/parser.tab.c"
    break;

  case 356: /* dot_field_name: STEP  */
#line 2206 "src/parser.y"
                     { (yyval.text) = kw_name("step"); }
#line 6600 "src/parser.tab.c"
    break;

  case 357: /* dot_field_name: UNWATCH  */
#line 2207 "src/parser.y"
                     { (yyval.text) = kw_name("unwatch"); }
#line 6606 "src/parser.tab.c"
    break;

  case 358: /* dot_field_name: UNKNOWN_VALUE  */
#line 2208 "src/parser.y"
                     { (yyval.text) = kw_name("unknown"); }
#line 6612 "src/parser.tab.c"
    break;

  case 359: /* dot_field_name: DIM  */
#line 2209 "src/parser.y"
                     { (yyval.text) = kw_name("dim"); }
#line 6618 "src/parser.tab.c"
    break;

  case 360: /* record_field_list: field_name OP_EQ expression  */
#line 2213 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6624 "src/parser.tab.c"
    break;

  case 361: /* record_field_list: field_name COLON expression  */
#line 2214 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6630 "src/parser.tab.c"
    break;

  case 362: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 2215 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 6636 "src/parser.tab.c"
    break;

  case 363: /* record_field_list: record_field_list COMMA optional_newlines field_name OP_EQ expression  */
#line 2216 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6642 "src/parser.tab.c"
    break;

  case 364: /* record_field_list: record_field_list COMMA optional_newlines field_name COLON expression  */
#line 2217 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6648 "src/parser.tab.c"
    break;

  case 365: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 2218 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 6654 "src/parser.tab.c"
    break;

  case 366: /* field_policy: IDENT  */
#line 2226 "src/parser.y"
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
#line 6686 "src/parser.tab.c"
    break;

  case 367: /* field_policy: IDENT expression  */
#line 2253 "src/parser.y"
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
#line 6707 "src/parser.tab.c"
    break;


#line 6711 "src/parser.tab.c"

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

#line 2276 "src/parser.y"


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
        char numbuf[64];
        size_t nlen = token.length < sizeof(numbuf) - 1 ? token.length : sizeof(numbuf) - 1;
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
