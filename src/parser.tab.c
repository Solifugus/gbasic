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
  YYSYMBOL_IF_WITHOUT_ELSE = 94,           /* IF_WITHOUT_ELSE  */
  YYSYMBOL_NO_DOT = 95,                    /* NO_DOT  */
  YYSYMBOL_DOT = 96,                       /* DOT  */
  YYSYMBOL_YYACCEPT = 97,                  /* $accept  */
  YYSYMBOL_program = 98,                   /* program  */
  YYSYMBOL_statement_list = 99,            /* statement_list  */
  YYSYMBOL_statement = 100,                /* statement  */
  YYSYMBOL_assignment = 101,               /* assignment  */
  YYSYMBOL_type_lock = 102,                /* type_lock  */
  YYSYMBOL_compound_op = 103,              /* compound_op  */
  YYSYMBOL_lvalue = 104,                   /* lvalue  */
  YYSYMBOL_variable_name = 105,            /* variable_name  */
  YYSYMBOL_comparison_lens = 106,          /* comparison_lens  */
  YYSYMBOL_107_1 = 107,                    /* $@1  */
  YYSYMBOL_modifier_name = 108,            /* modifier_name  */
  YYSYMBOL_modifier_word = 109,            /* modifier_word  */
  YYSYMBOL_print_statement = 110,          /* print_statement  */
  YYSYMBOL_call_statement = 111,           /* call_statement  */
  YYSYMBOL_with_lock_statement = 112,      /* with_lock_statement  */
  YYSYMBOL_for_end = 113,                  /* for_end  */
  YYSYMBOL_for_each_statement = 114,       /* for_each_statement  */
  YYSYMBOL_do_loop_statement = 115,        /* do_loop_statement  */
  YYSYMBOL_while_statement = 116,          /* while_statement  */
  YYSYMBOL_consider_statement = 117,       /* consider_statement  */
  YYSYMBOL_consider_branch_list = 118,     /* consider_branch_list  */
  YYSYMBOL_consider_else_opt = 119,        /* consider_else_opt  */
  YYSYMBOL_consider_statement_list = 120,  /* consider_statement_list  */
  YYSYMBOL_consider_body_statement = 121,  /* consider_body_statement  */
  YYSYMBOL_function_statement = 122,       /* function_statement  */
  YYSYMBOL_modifier_statement = 123,       /* modifier_statement  */
  YYSYMBOL_program_statement = 124,        /* program_statement  */
  YYSYMBOL_library_statement = 125,        /* library_statement  */
  YYSYMBOL_use_statement = 126,            /* use_statement  */
  YYSYMBOL_modifier_signature = 127,       /* modifier_signature  */
  YYSYMBOL_modifier_context = 128,         /* modifier_context  */
  YYSYMBOL_watch_statement = 129,          /* watch_statement  */
  YYSYMBOL_unwatch_statement = 130,        /* unwatch_statement  */
  YYSYMBOL_wait_statement = 131,           /* wait_statement  */
  YYSYMBOL_watch_target_list = 132,        /* watch_target_list  */
  YYSYMBOL_server_statement = 133,         /* server_statement  */
  YYSYMBOL_134_2 = 134,                    /* @2  */
  YYSYMBOL_135_3 = 135,                    /* @3  */
  YYSYMBOL_gsql_block = 136,               /* gsql_block  */
  YYSYMBOL_window_block = 137,             /* window_block  */
  YYSYMBOL_window_item_list = 138,         /* window_item_list  */
  YYSYMBOL_window_item = 139,              /* window_item  */
  YYSYMBOL_server_item_list = 140,         /* server_item_list  */
  YYSYMBOL_server_item = 141,              /* server_item  */
  YYSYMBOL_server_string_list = 142,       /* server_string_list  */
  YYSYMBOL_watch_target_path = 143,        /* watch_target_path  */
  YYSYMBOL_without_watchers_statement = 144, /* without_watchers_statement  */
  YYSYMBOL_on_error_statement = 145,       /* on_error_statement  */
  YYSYMBOL_error_statement = 146,          /* error_statement  */
  YYSYMBOL_return_statement = 147,         /* return_statement  */
  YYSYMBOL_label_statement = 148,          /* label_statement  */
  YYSYMBOL_goto_statement = 149,           /* goto_statement  */
  YYSYMBOL_gosub_statement = 150,          /* gosub_statement  */
  YYSYMBOL_break_statement = 151,          /* break_statement  */
  YYSYMBOL_continue_statement = 152,       /* continue_statement  */
  YYSYMBOL_if_statement = 153,             /* if_statement  */
  YYSYMBOL_if_block_tail = 154,            /* if_block_tail  */
  YYSYMBOL_if_inline_tail = 155,           /* if_inline_tail  */
  YYSYMBOL_inline_statement = 156,         /* inline_statement  */
  YYSYMBOL_expression = 157,               /* expression  */
  YYSYMBOL_or_expression = 158,            /* or_expression  */
  YYSYMBOL_and_expression = 159,           /* and_expression  */
  YYSYMBOL_not_expression = 160,           /* not_expression  */
  YYSYMBOL_comparison_expression = 161,    /* comparison_expression  */
  YYSYMBOL_set_expression = 162,           /* set_expression  */
  YYSYMBOL_additive_expression = 163,      /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 164, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 165,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 166,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 167,      /* comparison_operator  */
  YYSYMBOL_primary = 168,                  /* primary  */
  YYSYMBOL_record_literal = 169,           /* record_literal  */
  YYSYMBOL_ident_suffix = 170,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 171,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 172,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 173,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 174,            /* argument_list  */
  YYSYMBOL_array_argument_list = 175,      /* array_argument_list  */
  YYSYMBOL_parameter_list_opt = 176,       /* parameter_list_opt  */
  YYSYMBOL_parameter_default = 177,        /* parameter_default  */
  YYSYMBOL_parameter_list = 178,           /* parameter_list  */
  YYSYMBOL_field_name = 179,               /* field_name  */
  YYSYMBOL_dot_field_name = 180,           /* dot_field_name  */
  YYSYMBOL_record_field_list = 181,        /* record_field_list  */
  YYSYMBOL_field_policy = 182,             /* field_policy  */
  YYSYMBOL_optional_newlines = 183         /* optional_newlines  */
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

#line 1015 "src/parser.tab.c"

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
#define YYLAST   3541

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  97
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  87
/* YYNRULES -- Number of rules.  */
#define YYNRULES  373
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  870

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   351


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
      95,    96
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   895,   895,   899,   900,   901,   905,   906,   907,   908,
     909,   910,   911,   912,   913,   914,   915,   916,   917,   918,
     919,   920,   921,   922,   923,   924,   925,   926,   927,   928,
     929,   930,   931,   932,   933,   939,   950,   955,   956,   972,
     979,   993,   994,   998,   999,  1000,  1001,  1005,  1006,  1007,
    1011,  1012,  1013,  1024,  1024,  1032,  1036,  1037,  1041,  1042,
    1043,  1044,  1048,  1054,  1058,  1061,  1062,  1068,  1073,  1082,
    1095,  1124,  1125,  1126,  1130,  1134,  1150,  1155,  1163,  1167,
    1188,  1194,  1200,  1206,  1209,  1215,  1216,  1220,  1221,  1222,
    1226,  1227,  1228,  1229,  1230,  1231,  1232,  1233,  1234,  1235,
    1236,  1237,  1238,  1239,  1240,  1241,  1242,  1243,  1244,  1245,
    1246,  1247,  1248,  1249,  1250,  1251,  1252,  1258,  1269,  1272,
    1279,  1288,  1300,  1303,  1309,  1315,  1321,  1322,  1323,  1324,
    1325,  1326,  1342,  1358,  1379,  1380,  1384,  1388,  1391,  1399,
    1405,  1412,  1413,  1417,  1418,  1438,  1437,  1445,  1444,  1467,
    1470,  1473,  1479,  1494,  1498,  1508,  1516,  1517,  1518,  1523,
    1528,  1532,  1537,  1542,  1556,  1571,  1576,  1587,  1602,  1607,
    1613,  1614,  1615,  1619,  1622,  1625,  1628,  1631,  1637,  1638,
    1642,  1643,  1647,  1653,  1654,  1655,  1656,  1661,  1674,  1679,
    1684,  1694,  1698,  1699,  1703,  1710,  1714,  1723,  1724,  1728,
    1729,  1733,  1737,  1744,  1747,  1750,  1759,  1769,  1772,  1775,
    1781,  1788,  1798,  1799,  1800,  1801,  1802,  1803,  1804,  1805,
    1806,  1807,  1808,  1816,  1817,  1821,  1822,  1826,  1827,  1849,
    1850,  1854,  1855,  1856,  1862,  1863,  1864,  1868,  1869,  1870,
    1874,  1875,  1876,  1880,  1881,  1888,  1892,  1893,  1894,  1898,
    1899,  1900,  1901,  1906,  1920,  1921,  1922,  1923,  1924,  1925,
    1926,  1927,  1928,  1929,  1933,  1951,  1952,  1953,  1954,  1955,
    1972,  1978,  1979,  1980,  1981,  1982,  1983,  1984,  1985,  1986,
    1990,  1991,  1995,  2000,  2005,  2011,  2023,  2028,  2036,  2046,
    2058,  2059,  2063,  2064,  2068,  2069,  2073,  2074,  2088,  2092,
    2096,  2097,  2098,  2099,  2100,  2101,  2105,  2106,  2109,  2113,
    2120,  2127,  2134,  2141,  2156,  2163,  2172,  2173,  2174,  2175,
    2176,  2177,  2178,  2179,  2180,  2181,  2182,  2183,  2184,  2185,
    2186,  2187,  2188,  2189,  2190,  2191,  2192,  2193,  2194,  2195,
    2196,  2197,  2198,  2199,  2200,  2201,  2202,  2203,  2204,  2205,
    2206,  2207,  2208,  2209,  2210,  2211,  2212,  2213,  2214,  2215,
    2216,  2217,  2218,  2219,  2223,  2224,  2225,  2226,  2227,  2228,
    2236,  2263,  2282,  2283
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
  "parameter_list", "field_name", "dot_field_name", "record_field_list",
  "field_policy", "optional_newlines", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-586)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -586,    96,  1115,  -586,    48,   -18,    69,    76,    99,  -586,
    2705,  -586,  2665,  2705,   117,    66,    43,  2705,  2705,   150,
     164,   146,  2705,   202,   202,   219,  2705,   131,   136,    74,
    -586,   644,   313,   227,   244,   255,   372,   153,  -586,  -586,
     187,   478,   191,   207,   220,  -586,  -586,  -586,  -586,  -586,
    -586,  -586,  -586,  -586,   231,  -586,   257,   298,  -586,  -586,
    -586,  -586,   304,   308,   310,   320,   326,   340,   341,   343,
    -586,   222,  2705,  2705,   273,   352,   383,   436,  -586,  -586,
     362,  2806,  -586,  -586,  -586,  -586,  -586,  2768,  2705,  2869,
     445,   368,  -586,  2806,  2705,  -586,  -586,    -6,   431,   421,
     423,  -586,  -586,  2839,   297,   306,  -586,    -1,  -586,  -586,
     456,   397,  -586,  -586,   377,   293,   459,  -586,   373,   374,
    -586,  -586,   380,   385,  -586,  -586,  -586,   388,   202,  -586,
     200,   378,  -586,  2705,   382,   332,    89,   475,  -586,  -586,
    -586,  -586,  -586,   384,  -586,   439,   399,   392,    67,  -586,
     483,  -586,   313,  -586,  -586,  -586,  -586,  -586,  -586,  2705,
    2705,  -586,  3337,  2705,   260,   485,  -586,  -586,  -586,  -586,
    -586,  -586,  -586,  -586,  -586,  -586,  -586,  -586,  -586,  2935,
    -586,   404,   401,   407,  3002,  3069,   494,  -586,  2705,  -586,
    -586,  -586,   214,   415,   418,  -586,   420,   743,   847,  2705,
    3404,  -586,   234,  2768,  2768,  2806,  2806,  -586,  -586,  -586,
    -586,  -586,  -586,  -586,  -586,  -586,  -586,   546,  2806,  2806,
    2806,  2806,  2806,  2705,  3471,   499,  2705,  2705,  2705,  2705,
     503,   129,   981,  -586,   486,   504,   504,   202,   183,   202,
    -586,   507,   471,  -586,  -586,  -586,    73,  -586,    79,  -586,
     429,   504,  -586,   511,   504,  -586,   512,   514,   515,   481,
    -586,   428,   517,   441,   443,  -586,  -586,  -586,  -586,  -586,
    -586,  -586,  -586,  -586,  -586,  -586,  -586,  -586,  -586,  -586,
    -586,  -586,  -586,  -586,  -586,  -586,  -586,  -586,  -586,  -586,
    -586,  -586,  -586,  -586,  -586,  -586,  -586,  -586,  -586,  -586,
    -586,  -586,  -586,  -586,  -586,  -586,  -586,  -586,  -586,  -586,
    -586,  -586,  -586,  -586,  2705,  2705,   447,   453,   448,  -586,
     452,     1,  -586,   223,  -586,  2705,  -586,   455,   237,   457,
     265,   111,   450,   464,  2705,  -586,  -586,  -586,  -586,  -586,
     467,  -586,   469,   460,  -586,   470,   476,   479,  -586,  -586,
     474,  -586,  -586,  -586,  -586,  -586,  -586,  -586,  -586,  -586,
    -586,   482,   423,  -586,   297,   297,  2806,   379,   306,   306,
    -586,  -586,   492,   484,   497,  -586,  -586,  -586,   505,   496,
     548,   547,  2705,   588,  2705,  1182,  2705,   196,   122,   509,
     516,   510,   280,   518,   378,  1249,  -586,  2705,  1316,  -586,
    -586,  -586,  -586,  2705,   519,  -586,   520,   522,  1383,   586,
    -586,  -586,   511,  -586,   513,  2705,  2705,  -586,  -586,   598,
    2705,   605,  -586,  2705,  2705,   521,  -586,  -586,  -586,    22,
    -586,   534,   599,   611,  -586,  -586,   542,  -586,   204,   157,
    -586,  2705,  2705,  -586,  1048,   485,   606,   379,  -586,  2705,
    2705,   537,  -586,  2705,  2705,   539,   592,   541,   591,   618,
    2705,   550,   626,   554,   485,   315,   651,   325,   563,  -586,
     601,  -586,   600,   572,  -586,  -586,   567,   594,   658,   570,
    -586,   578,   579,   580,  -586,  2705,   582,  -586,  -586,  -586,
    -586,   914,    47,   668,  -586,  -586,   652,   585,  -586,   757,
    -586,  -586,   596,   603,   666,   663,  -586,  2589,  -586,   604,
     608,  -586,  1450,    17,   593,  -586,  2705,  -586,   607,   609,
     667,  -586,   612,  -586,  -586,  -586,  -586,  -586,  -586,   684,
     689,  -586,   625,   485,  -586,   144,   485,  -586,  -586,  1517,
     614,   616,  -586,  1584,  -586,   617,  -586,  -586,  -586,  -586,
    -586,  -586,   619,    64,  -586,   613,   161,    97,   693,   697,
    -586,  -586,   708,    71,   709,   701,  -586,  -586,  -586,  2705,
    -586,   629,   632,  2705,  -586,   643,  -586,  -586,  1651,   677,
      80,  -586,  2705,  -586,  -586,  1450,   645,  -586,  -586,   648,
    2522,  -586,  -586,  -586,   554,   650,  1718,   554,   485,   656,
    1785,  1852,   681,  -586,  -586,   679,  1919,  -586,  1986,  2705,
     417,   741,   749,  -586,  -586,    84,   605,  2705,  2705,   118,
     661,   751,  3136,  -586,   664,   660,   665,   759,   674,   742,
     744,  2053,  -586,  -586,   747,  2120,  -586,   734,   685,  -586,
     688,   692,  1450,  1450,  -586,  -586,  2522,  -586,  -586,  -586,
     699,   702,   705,  -586,  -586,  -586,  -586,  -586,  -586,  -586,
    -586,  -586,  -586,   706,  -586,   707,   711,  -586,  -586,   716,
     717,   718,   719,   720,   721,   722,   723,  -586,  -586,  -586,
     750,  -586,   715,  -586,   766,   745,   727,   729,   758,   761,
    -586,   748,   752,   229,   736,   754,   830,   755,  -586,  -586,
     756,   831,  3203,  -586,  -586,   834,   760,   281,   102,  -586,
     836,  -586,   762,  -586,   839,   763,   823,  2625,   832,   764,
    -586,  -586,  -586,  -586,  -586,  1450,  2522,  -586,  -586,  -586,
    -586,  -586,  -586,  -586,  -586,  -586,  -586,  -586,  -586,  -586,
    -586,  2187,   765,   554,  2254,   767,   770,  -586,  -586,   771,
     772,  3270,   504,   854,  -586,  -586,  -586,   773,   775,  -586,
     864,   777,   294,   120,   147,   780,  -586,   782,   901,   827,
     151,  -586,   828,  -586,   829,  -586,   833,   837,  -586,  1450,
    -586,   871,  -586,  -586,   872,  -586,  -586,  -586,  -586,   838,
     301,   841,  -586,  2321,  -586,  2705,   182,   840,  -586,   842,
     921,   928,  -586,  -586,   849,  -586,   930,  -586,  1048,  -586,
    1048,   606,  -586,  -586,   879,   893,  -586,   894,   896,   929,
    -586,   990,  -586,  -586,   902,   904,   197,   201,  -586,  2388,
     905,  -586,  -586,  -586,  -586,  -586,   206,  -586,  -586,   906,
     907,   212,  -586,  -586,   997,   998,   943,  -586,  1001,   245,
    2455,  -586,  -586,  1002,   916,   918,   919,   920,  1010,  1011,
     923,  -586,  -586,  -586,  -586,   924,   927,  -586,  -586,  -586
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       3,     0,     2,     1,    50,     0,     0,     0,     0,    35,
       0,    51,     0,     0,     0,     0,     0,     0,     0,   197,
     199,     0,   192,     0,     0,     0,     0,     0,     0,     0,
      52,     0,     0,     0,     0,     0,     0,     0,     4,     5,
       0,     0,    47,     0,     0,     9,    12,    14,    13,    15,
      16,    17,    18,    19,     0,    21,     0,     0,    22,    11,
      10,    25,     0,     0,     0,     0,     0,     0,     0,     0,
      34,     0,   290,   290,     0,     0,     0,   265,    50,   268,
       0,     0,   264,   272,   273,   274,   275,     0,     0,     0,
       0,     0,   271,     0,     0,   372,   372,   282,     0,   223,
     225,   227,   229,   231,   234,   237,   240,   243,   249,   279,
     267,     0,    62,    65,     0,     0,     0,     3,     0,     0,
     198,   200,     0,     0,   193,   195,   196,    50,     0,   180,
       0,   143,   140,     0,     0,     0,     0,     0,   191,    58,
      60,    59,    61,   134,    56,     0,     0,     0,   127,   129,
     126,   128,     0,     6,    55,    43,    44,    45,    46,     0,
       0,    53,     0,     0,     0,   194,     7,     8,    20,    23,
      24,    26,    27,    28,    29,    30,    31,    32,    33,     0,
     292,     0,   291,     0,     0,     0,     0,   288,   290,   245,
     230,   224,   246,     0,     0,   244,     0,     0,     0,   290,
       0,   269,     0,     0,     0,     0,     0,   254,   255,   256,
     257,   258,   259,   260,   261,   262,   263,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     3,     0,   296,   296,     0,     0,     0,
       3,     0,   141,     3,   190,   189,     0,   188,     0,   185,
       0,   296,    57,     0,   296,     3,     0,     0,     0,     0,
      36,     0,     0,   316,     0,   317,   363,   332,   329,   330,
     321,   336,   344,   345,   346,   362,   342,   343,   340,   341,
     327,   325,   351,   331,   322,   360,   334,   335,   323,   326,
     333,   359,   347,   348,   354,   337,   349,   350,   357,   361,
     328,   358,   324,   318,   319,   320,   355,   356,   353,   338,
     339,   352,    49,    37,     0,     0,    41,     0,   316,   315,
       0,     0,   314,     0,    64,     0,    66,     0,     0,     0,
       0,     0,     0,     0,   290,   266,   276,   277,   373,   294,
     372,   280,   372,     0,   316,     0,   286,    50,     3,   212,
      47,   213,   214,   215,   216,   217,   218,   219,   220,   221,
     222,     0,   226,   228,   235,   236,     0,   232,   238,   239,
     241,   242,     0,   316,     0,   251,   289,    63,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    85,   306,     0,
     297,     0,     0,     0,   144,     0,   181,     0,     0,   187,
     186,   183,   184,   290,     0,   136,     0,     0,     0,   132,
     130,   131,     0,    48,     0,   290,   290,    40,    38,     0,
       0,     0,   147,     0,     0,     0,   372,   293,   156,     0,
     150,     0,     0,     0,   270,   247,     0,   372,     0,     0,
     283,   290,   290,   284,     0,     0,   207,   233,   250,   290,
     290,     0,     3,     0,     0,     0,     0,     0,    51,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     3,
      51,   142,    51,     0,   135,     3,     0,    51,     0,     0,
      54,     0,     0,     0,    39,   370,     0,   170,   364,   365,
     145,     0,     0,     0,   156,   149,     0,     0,   248,     0,
     278,   281,     0,     0,     0,    51,   201,     0,   202,     0,
       0,     3,     0,     0,     0,     3,     0,    80,     0,     0,
       0,    87,     0,   298,   301,   302,   303,   304,   305,     0,
       0,   307,   309,     0,     3,   308,     0,     3,     3,     0,
       0,     0,    69,     0,     3,     0,   133,     3,    67,    68,
      42,   371,     0,     0,   170,   316,     0,     0,     0,     0,
     157,   158,     0,     0,     0,     0,   295,   285,   287,     0,
       3,     0,     0,     0,     3,     0,   252,   253,     0,    51,
      52,    74,     0,     3,     3,     0,     0,    81,    87,     0,
      86,    82,   300,   299,     0,     0,     0,     0,     0,     0,
       0,     0,    51,   138,   182,    51,     0,   125,     0,     0,
       0,     0,     0,   171,   172,     0,     0,     0,     0,     0,
       0,     0,     0,   156,     0,     0,     0,     0,     0,     0,
       0,     0,   204,   203,     0,     0,   208,    51,     0,    72,
       0,     0,     0,     0,    75,     3,    83,    87,   117,    88,
       0,     0,     0,    93,    95,    97,    96,    98,    89,    99,
     100,   101,   102,     0,   104,     0,     0,    94,   107,     0,
       0,     0,     0,     0,     0,     0,     0,   116,   310,     3,
      51,   313,   311,     3,    51,    51,     0,     0,    51,    51,
     366,     0,   178,     0,     0,     0,     0,     0,   367,   368,
       0,     0,     0,   156,   156,     0,     0,     0,     0,   154,
       0,   156,     0,   151,     0,     0,    51,     0,    51,     0,
      71,    73,     3,    78,    76,     0,    84,    90,    91,    92,
     103,   105,   106,   108,   109,   110,   111,   112,   113,   114,
     115,     0,     0,     0,     0,     0,     0,   137,   122,     0,
       0,     0,   296,     0,   173,   148,     3,     0,     0,   156,
       0,     0,     0,     0,     0,     0,   159,     0,     0,     0,
       0,   153,     0,     3,     0,     3,     0,     0,    70,     0,
      77,    51,   118,   312,    51,   119,   139,   124,   123,     0,
       0,     0,   179,     0,   146,     0,     0,     0,   161,     0,
       0,     0,   156,   156,     0,     3,     0,   152,     0,   205,
       0,   207,   209,    79,     0,     0,   170,     0,     0,    51,
     369,     0,   156,   160,     0,     0,     0,     0,   162,     0,
       0,   206,   211,   210,   120,   121,     0,   170,     3,     0,
       0,     0,   164,   166,     0,     0,    51,   155,     0,     0,
       0,   177,   165,     0,     0,     0,     0,     0,     0,    51,
       0,   167,   163,   169,   176,     0,     0,   168,   175,   174
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -586,  -586,  -106,  -586,  -196,  -439,   857,  -586,    -2,   925,
    -586,  -586,   880,  -192,  -188,  -570,  -577,  -564,  -561,  -559,
    -557,  -586,  -586,  -519,  -586,  -554,  -547,  -546,  -542,  -186,
     875,   641,  -541,  -537,  -534,   -93,  -586,  -586,  -586,  -586,
    -532,  -477,  -586,  -551,  -586,  -586,   785,  -529,  -175,  -174,
    -170,  -528,  -163,  -155,  -147,  -145,  -527,  -440,   230,  -500,
      28,  -586,   851,   -74,  -586,  -200,   218,   210,   -51,   955,
     844,  -586,   724,  -586,  -586,  -586,   -68,  -586,  -586,  -217,
    -585,  -586,   565,    21,  -183,   446,   -72
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    39,    40,   317,   163,    41,    97,   164,
     262,   143,   144,    43,    44,    45,   581,    46,    47,    48,
      49,   387,   462,   590,   658,    50,    51,    52,    53,    54,
     145,   406,    55,    56,    57,   130,    58,   554,   487,    59,
      60,   492,   561,   553,   614,   693,   131,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,   506,   508,   361,
     180,    99,   100,   101,   102,   103,   104,   105,   106,   107,
     218,   108,   109,   201,   443,   110,   181,   182,   340,   389,
     531,   390,   321,   322,   323,   486,   197
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      42,   328,   330,   615,   571,   183,   349,   575,   644,   678,
     351,   232,   681,   190,   352,   342,   353,   563,   367,   391,
     653,   125,   126,   129,   198,   532,   654,   354,   355,   655,
     189,   656,   356,   657,   404,   238,   659,   407,    98,   357,
     112,   113,   195,   660,   661,   118,   119,   358,   662,   664,
     124,   557,    71,   665,   132,   359,   666,   360,   667,   138,
     582,   668,   672,   677,   493,   723,   724,    73,   610,   646,
     115,   256,   423,    74,   558,   557,   653,   399,   135,   199,
      75,   257,   654,   401,    78,   655,   223,   656,   610,   657,
     200,   611,   659,   424,   595,   224,     3,   599,   627,   660,
     661,   619,   620,    76,   662,   664,   557,    11,   559,   665,
     583,   696,   666,   116,   667,   494,   191,   668,   672,   677,
     332,   114,   196,   700,   557,   612,   129,   385,   726,   768,
     363,   343,   559,    72,   395,   400,   117,   398,   136,   621,
     560,   402,    30,   248,   392,   612,   708,   800,   780,   408,
     122,   557,   249,   123,   120,   557,   653,   613,   783,   682,
     701,   242,   654,   559,   560,   655,   447,   656,   121,   657,
     370,   371,   659,   639,   801,   382,   133,   613,   806,   660,
     661,   559,   622,   312,   662,   664,   557,   260,   261,   665,
     623,   313,   666,   463,   667,   560,   134,   668,   672,   677,
     350,   557,   813,   702,   432,   557,    78,   433,   559,   821,
     610,   703,   559,   560,   464,   597,   557,   776,   152,   460,
     383,   346,   461,   127,   844,   339,   763,   764,   845,    11,
      42,   146,   617,   848,   770,   129,   598,   129,   347,   853,
     560,     5,   444,   559,   560,   375,    11,   501,   147,   610,
     338,   372,   333,   618,   377,   378,   379,   380,   559,   148,
     149,    11,   559,    12,    30,   836,   436,   612,   438,   393,
     439,    13,   858,   559,   239,   560,   155,   156,   157,   158,
     153,    30,   796,   165,    19,    20,   849,    22,    23,    24,
     560,   239,   500,   240,   560,    29,    30,   338,    31,   613,
     166,   223,    35,    36,   128,   560,   612,   179,   349,   425,
     224,   349,   351,   167,   426,   351,   352,   139,   353,   352,
     753,   353,   754,   429,   168,   826,   827,   348,   426,   354,
     355,   314,   354,   355,   356,   473,   244,   356,   613,   228,
     140,   357,   417,   418,   357,   841,   512,   481,   482,   358,
     169,   431,   358,   427,   491,   141,   426,   359,   184,   360,
     359,   245,   360,   539,   229,   499,   468,   767,   831,   543,
     832,   239,   426,   502,   503,   142,   150,   151,   219,   220,
     799,   509,   510,    42,   230,   426,   246,   817,   139,   221,
     222,   170,   426,    42,   650,   247,    42,   171,   651,   205,
     206,   172,   652,   173,   663,   578,    42,   533,   534,   585,
     455,   140,   457,   174,   459,   669,   670,   536,   537,   175,
     671,   691,   692,   364,   365,   471,   141,   673,   596,   368,
     369,   600,   601,   176,   177,   674,   178,   185,   606,   707,
     187,   608,    42,   675,   186,   676,   142,   188,   484,   193,
     650,   488,   489,   194,   651,   202,   203,   204,   652,   225,
     663,   226,   227,   231,   631,   235,   233,   234,   635,   251,
     236,   669,   670,   237,   241,   243,   671,   642,   643,   250,
     253,   513,   514,   673,   254,   255,   154,   258,   520,   316,
     324,   674,   325,   326,   155,   156,   157,   158,   331,   675,
     334,   676,   350,   376,   335,   350,   336,   381,   388,   386,
      42,   396,   397,   551,   403,   405,   413,   409,   410,   762,
     411,   349,   412,   414,   420,   351,   415,   566,   416,   352,
     650,   353,   419,   421,   651,   791,   434,    42,   652,   725,
     663,    42,   354,   355,   586,   422,   440,   356,   428,   159,
     430,   669,   670,    96,   357,   441,   671,   523,   437,   524,
     426,   442,   358,   673,    72,   160,   445,   161,   790,   449,
     359,   674,   360,   741,   162,   446,    42,   744,   640,   675,
     448,   676,   450,    42,   525,   526,   527,   528,    42,   452,
     453,   451,   456,   454,    42,   465,   467,   630,    42,    42,
     478,   634,   483,   480,    42,   474,    42,   466,   476,   485,
     641,   469,   496,   475,   490,   497,   779,   207,   208,   209,
     210,   211,   212,   213,   214,   215,   216,   495,   498,    42,
     511,   507,   515,    42,   517,   529,   530,   690,   516,   518,
      42,    42,   519,   521,    42,   698,   699,    77,    78,    79,
     793,    80,    81,    82,   522,   535,   538,   540,   542,   541,
     544,   545,   546,   547,   548,   549,   550,   808,   552,   810,
     347,    11,   562,     5,    83,    84,    85,    86,   565,   564,
      87,    88,   567,    89,    90,   572,   584,   592,   569,   568,
     576,   589,   593,    11,   577,    12,   594,   624,   616,   829,
     587,   625,   588,    13,    91,   591,    30,   603,    92,   604,
     607,   609,   626,   628,   629,   350,    19,    20,   638,    22,
      23,    24,   632,    42,    42,   633,    93,    29,    30,    94,
      31,    95,   850,    96,    35,    36,   636,   686,   645,    42,
     137,   647,    42,   679,   687,   694,    77,    78,    79,   683,
      80,    81,    82,   695,   704,   705,   710,   709,   711,   570,
      77,    78,    79,   712,    80,    81,    82,   713,   715,   714,
      11,   717,   719,    83,    84,    85,    86,    42,   720,    87,
      88,   721,    89,    90,    11,   722,   743,    83,    84,    85,
      86,    42,   727,    87,    88,   728,    89,    90,   729,   730,
     731,   746,   742,    91,   732,    30,    42,    92,    42,   733,
     734,   735,   736,   737,   738,   739,   740,    91,   745,    30,
     747,    92,   748,   820,   749,    93,   750,    42,    94,   755,
      95,   337,    96,   751,   757,   760,   338,   752,   765,    93,
     769,   758,    94,   772,    95,   774,    96,   756,    42,   759,
     338,   318,   319,   766,   777,   771,   773,   778,   782,   792,
     785,   265,   266,   786,   787,   788,   794,   795,   797,   267,
     798,   268,   269,   802,   270,   803,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,   804,   300,   301,   302,   303,
     304,   305,   306,   307,   308,   309,   310,   311,   555,   319,
     805,   807,   809,   814,   815,   824,   811,   818,   265,   266,
     812,   816,   825,   822,   830,   823,   267,   341,   268,   269,
     338,   270,   828,   271,   272,   273,   274,   275,   276,   277,
     278,   279,   280,   281,   282,   283,   284,   285,   286,   287,
     288,   289,   290,   291,   292,   293,   294,   295,   296,   297,
     298,   299,   834,   300,   301,   302,   303,   304,   305,   306,
     307,   308,   309,   310,   311,     4,   835,   837,     5,   838,
     839,     6,     7,     8,   840,   842,     9,   843,   847,   851,
     852,   854,   855,    10,   856,   857,   860,   338,    11,   861,
      12,   862,   863,   864,   865,   866,   867,   868,    13,    14,
     869,   315,    15,   252,   394,    16,   384,   259,   217,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,   833,    29,    30,   192,    31,    32,    33,    34,    35,
      36,    37,     4,   479,   362,     5,   556,   435,     6,     7,
       8,   366,   697,     9,     0,     0,     0,     0,     0,     0,
      10,     0,     0,   504,    38,   505,     0,    12,     0,     0,
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
       0,     0,     0,     0,    10,     0,     0,     0,    38,   458,
       0,    12,     0,     0,     0,     0,     0,     0,     0,    13,
      14,     0,     0,    15,     0,     0,    16,     0,     0,     0,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,     0,    29,    30,     0,    31,    32,    33,    34,
      35,    36,    37,     4,     0,     0,     5,     0,     0,     6,
       7,     8,     0,     0,     9,     0,     0,     0,     0,     0,
       0,    10,     0,     0,     0,    38,   470,     0,    12,     0,
       0,     0,     0,     0,     0,     0,    13,    14,     0,     0,
      15,     0,     0,    16,     0,     0,     0,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,     0,
      29,    30,     0,    31,    32,    33,    34,    35,    36,    37,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     0,
       0,     9,     0,     0,     0,     0,     0,     0,    10,     0,
       0,     0,    38,   472,     0,    12,     0,     0,     0,     0,
       0,     0,     0,    13,    14,     0,     0,    15,     0,     0,
      16,     0,     0,     0,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,     0,    29,    30,     0,
      31,    32,    33,    34,    35,    36,    37,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    38,
     477,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,    14,     0,     0,    15,     0,     0,    16,     0,     0,
       0,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,     0,    29,    30,     0,    31,    32,    33,
      34,    35,    36,    37,     4,     0,     0,     5,     0,     0,
       6,     7,     8,     0,     0,     9,     0,     0,     0,     0,
       0,     0,    10,     0,     0,     0,    38,   579,     0,    12,
       0,     0,     0,     0,     0,     0,     0,    13,    14,     0,
       0,    15,     0,     0,    16,     0,     0,     0,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
       0,    29,   580,     0,    31,    32,    33,    34,    35,    36,
      37,     4,     0,     0,     5,     0,     0,     6,     7,     8,
       0,     0,     9,     0,     0,     0,     0,     0,     0,    10,
       0,     0,     0,    38,   602,     0,    12,     0,     0,     0,
       0,     0,     0,     0,    13,    14,     0,     0,    15,     0,
       0,    16,     0,     0,     0,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,     0,    29,    30,
       0,    31,    32,    33,    34,    35,    36,    37,     4,     0,
       0,     5,     0,     0,     6,     7,     8,     0,     0,     9,
       0,     0,     0,     0,     0,     0,    10,     0,     0,     0,
      38,   605,     0,    12,     0,     0,     0,     0,     0,     0,
       0,    13,    14,     0,     0,    15,     0,     0,    16,     0,
       0,     0,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,     0,    29,    30,     0,    31,    32,
      33,    34,    35,    36,    37,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    38,   637,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,    14,
       0,     0,    15,     0,     0,    16,     0,     0,     0,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,     0,    29,    30,     0,    31,    32,    33,    34,    35,
      36,    37,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     0,     0,     9,     0,     0,     0,     0,     0,     0,
      10,     0,     0,     0,    38,   680,     0,    12,     0,     0,
       0,     0,     0,     0,     0,    13,    14,     0,     0,    15,
       0,     0,    16,     0,     0,     0,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,     0,    29,
      30,     0,    31,    32,    33,    34,    35,    36,    37,     4,
       0,     0,     5,     0,     0,     6,     7,     8,     0,     0,
       9,     0,     0,     0,     0,     0,     0,    10,     0,     0,
       0,    38,   684,     0,    12,     0,     0,     0,     0,     0,
       0,     0,    13,    14,     0,     0,    15,     0,     0,    16,
       0,     0,     0,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,    28,     0,    29,    30,     0,    31,
      32,    33,    34,    35,    36,    37,     4,     0,     0,     5,
       0,     0,     6,     7,     8,     0,     0,     9,     0,     0,
       0,     0,     0,     0,    10,     0,     0,     0,    38,   685,
       0,    12,     0,     0,     0,     0,     0,     0,     0,    13,
      14,     0,     0,    15,     0,     0,    16,     0,     0,     0,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,     0,    29,    30,     0,    31,    32,    33,    34,
      35,    36,    37,     4,     0,     0,     5,     0,     0,     6,
       7,     8,     0,     0,     9,     0,     0,     0,     0,     0,
       0,    10,     0,     0,     0,    38,   688,     0,    12,     0,
       0,     0,     0,     0,     0,     0,    13,    14,     0,     0,
      15,     0,     0,    16,     0,     0,     0,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,     0,
      29,    30,     0,    31,    32,    33,    34,    35,    36,    37,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     0,
       0,     9,     0,     0,     0,     0,     0,     0,    10,     0,
       0,     0,    38,   689,     0,    12,     0,     0,     0,     0,
       0,     0,     0,    13,    14,     0,     0,    15,     0,     0,
      16,     0,     0,     0,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,     0,    29,    30,     0,
      31,    32,    33,    34,    35,    36,    37,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    38,
     716,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,    14,     0,     0,    15,     0,     0,    16,     0,     0,
       0,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,     0,    29,    30,     0,    31,    32,    33,
      34,    35,    36,    37,     4,     0,     0,     5,     0,     0,
       6,     7,     8,     0,     0,     9,     0,     0,     0,     0,
       0,     0,    10,     0,     0,     0,    38,   718,     0,    12,
       0,     0,     0,     0,     0,     0,     0,    13,    14,     0,
       0,    15,     0,     0,    16,     0,     0,     0,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
       0,    29,    30,     0,    31,    32,    33,    34,    35,    36,
      37,     4,     0,     0,     5,     0,     0,     6,     7,     8,
       0,     0,     9,     0,     0,     0,     0,     0,     0,    10,
       0,     0,     0,    38,   781,     0,    12,     0,     0,     0,
       0,     0,     0,     0,    13,    14,     0,     0,    15,     0,
       0,    16,     0,     0,     0,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,     0,    29,    30,
       0,    31,    32,    33,    34,    35,    36,    37,     4,     0,
       0,     5,     0,     0,     6,     7,     8,     0,     0,     9,
       0,     0,     0,     0,     0,     0,    10,     0,     0,     0,
      38,   784,     0,    12,     0,     0,     0,     0,     0,     0,
       0,    13,    14,     0,     0,    15,     0,     0,    16,     0,
       0,     0,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,     0,    29,    30,     0,    31,    32,
      33,    34,    35,    36,    37,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    38,   819,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,    14,
       0,     0,    15,     0,     0,    16,     0,     0,     0,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,     0,    29,    30,     0,    31,    32,    33,    34,    35,
      36,    37,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     0,     0,     9,     0,     0,     0,     0,     0,     0,
      10,     0,     0,     0,    38,   846,     0,    12,     0,     0,
       0,     0,     0,     0,     0,    13,    14,     0,     0,    15,
       0,     0,    16,     0,     0,     0,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,     0,    29,
      30,     0,    31,    32,    33,    34,    35,    36,    37,     4,
       0,     0,     5,     0,     0,     6,     7,     8,     0,     0,
       9,     0,     0,     0,     0,     0,     0,    10,     0,     0,
       0,    38,   859,     0,    12,     0,     0,     0,     0,     0,
       0,     0,    13,    14,     0,     0,    15,     0,     0,    16,
       0,     0,     0,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,    28,     0,    29,    30,     0,    31,
      32,    33,    34,    35,    36,    37,   347,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,   648,     0,     0,
       0,     0,     0,     0,    10,     0,     0,     0,    38,    11,
       0,    12,     0,     0,     0,     0,     0,     0,     0,    13,
      14,     0,     0,    15,     0,     0,    16,     0,     0,     0,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,     0,    29,    30,     0,    31,    32,    33,    34,
      35,    36,    37,   347,     0,     0,     5,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   573,     0,     0,     0,   649,    11,     0,    12,     0,
       0,     0,     0,     0,     0,     0,    13,     0,     0,   347,
       0,     0,     5,     0,     0,     0,     0,     0,     0,    19,
      20,     0,    22,    23,    24,     0,     0,     0,     0,     0,
      29,    30,    11,    31,    12,     0,     0,    35,    36,     0,
       0,     0,    13,     0,     0,     0,     0,     0,    77,    78,
      79,     0,    80,    81,    82,    19,    20,     0,    22,    23,
      24,     0,   574,     0,     0,     0,    29,    30,     0,    31,
       0,     0,    11,    35,    36,    83,    84,    85,    86,     0,
       0,    87,    88,     0,    89,    90,     0,   111,    77,    78,
      79,     0,    80,    81,    82,     0,     0,     0,   775,     0,
       0,     0,     0,     0,     0,    91,     0,    30,     0,    92,
       0,     0,    11,     0,     0,    83,    84,    85,    86,     0,
       0,    87,    88,     0,    89,    90,     0,    93,     0,     0,
      94,     0,    95,     0,    96,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    91,     0,    30,     0,    92,
       0,    77,    78,    79,     0,    80,    81,    82,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    93,     0,     0,
      94,     0,    95,     0,    96,    11,     0,     0,    83,    84,
      85,    86,     0,     0,    87,     0,     0,    89,    90,    77,
      78,    79,     0,    80,    81,    82,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    91,     0,
      30,     0,    92,    11,     0,     0,    83,    84,    85,    86,
       0,     0,     0,     0,     0,    89,    90,   154,     0,     0,
      93,     0,     0,    94,     0,    95,     0,    96,     0,   205,
     206,     0,     0,     0,     0,     0,    91,     0,    30,     0,
      92,     0,    77,    78,    79,     0,    80,     0,    82,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    93,     0,
       0,    94,     0,    95,     0,    96,    11,     0,     0,    83,
      84,    85,    86,     0,     0,     0,     0,     0,     0,     0,
     207,   208,   209,   210,   211,   212,   213,   214,   215,   216,
       0,     0,     0,     0,     0,     0,     0,     0,   161,    91,
       0,    30,     0,    92,     0,     0,     0,     0,     0,   318,
     319,     0,     0,     0,     0,     0,     0,     0,     0,   265,
     266,     0,     0,     0,    94,     0,    95,   267,    96,   268,
     269,     0,   270,     0,   271,   272,   273,   274,   275,   276,
     277,   278,   279,   280,   281,   282,   283,   284,   285,   286,
     287,   288,   289,   290,   291,   292,   293,   294,   295,   296,
     297,   298,   299,     0,   300,   301,   302,   303,   304,   305,
     306,   307,   308,   309,   310,   311,   318,   319,     0,     0,
       0,     0,     0,     0,     0,     0,   265,   266,     0,     0,
       0,   320,     0,     0,   267,     0,   268,   269,     0,   270,
       0,   271,   272,   273,   274,   275,   276,   277,   278,   279,
     280,   281,   282,   283,   284,   285,   286,   287,   288,   289,
     290,   291,   292,   293,   294,   295,   296,   297,   298,   299,
       0,   300,   301,   302,   303,   304,   305,   306,   307,   308,
     309,   310,   311,   318,   319,     0,     0,     0,     0,     0,
       0,     0,     0,   265,   266,     0,     0,     0,   327,     0,
       0,   267,     0,   268,   269,     0,   270,     0,   271,   272,
     273,   274,   275,   276,   277,   278,   279,   280,   281,   282,
     283,   284,   285,   286,   287,   288,   289,   290,   291,   292,
     293,   294,   295,   296,   297,   298,   299,     0,   300,   301,
     302,   303,   304,   305,   306,   307,   308,   309,   310,   311,
     318,   319,     0,     0,     0,     0,     0,     0,     0,     0,
     265,   266,     0,     0,     0,   329,     0,     0,   267,     0,
     268,   269,     0,   270,     0,   271,   272,   273,   274,   275,
     276,   277,   278,   279,   280,   281,   282,   283,   284,   285,
     286,   287,   288,   289,   290,   291,   292,   293,   294,   295,
     296,   297,   298,   299,     0,   300,   301,   302,   303,   304,
     305,   306,   307,   308,   309,   310,   311,   318,   319,     0,
       0,     0,     0,     0,     0,     0,     0,   265,   266,     0,
       0,     0,   706,     0,     0,   267,     0,   268,   269,     0,
     270,     0,   271,   272,   273,   274,   275,   276,   277,   278,
     279,   280,   281,   282,   283,   284,   285,   286,   287,   288,
     289,   290,   291,   292,   293,   294,   295,   296,   297,   298,
     299,     0,   300,   301,   302,   303,   304,   305,   306,   307,
     308,   309,   310,   311,   318,   319,     0,     0,     0,     0,
       0,     0,     0,     0,   265,   266,     0,     0,     0,   761,
       0,     0,   267,     0,   268,   269,     0,   270,     0,   271,
     272,   273,   274,   275,   276,   277,   278,   279,   280,   281,
     282,   283,   284,   285,   286,   287,   288,   289,   290,   291,
     292,   293,   294,   295,   296,   297,   298,   299,     0,   300,
     301,   302,   303,   304,   305,   306,   307,   308,   309,   310,
     311,   263,     0,     0,   264,     0,     0,     0,     0,     0,
       0,   265,   266,     0,     0,     0,   789,     0,     0,   267,
       0,   268,   269,     0,   270,     0,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,     0,   300,   301,   302,   303,
     304,   305,   306,   307,   308,   309,   310,   311,   344,     0,
       0,   345,     0,     0,     0,     0,     0,     0,   265,   266,
       0,     0,     0,     0,     0,     0,   267,     0,   268,   269,
       0,   270,     0,   271,   272,   273,   274,   275,   276,   277,
     278,   279,   280,   281,   282,   283,   284,   285,   286,   287,
     288,   289,   290,   291,   292,   293,   294,   295,   296,   297,
     298,   299,     0,   300,   301,   302,   303,   304,   305,   306,
     307,   308,   309,   310,   311,   373,     0,     0,   374,     0,
       0,     0,     0,     0,     0,   265,   266,     0,     0,     0,
       0,     0,     0,   267,     0,   268,   269,     0,   270,     0,
     271,   272,   273,   274,   275,   276,   277,   278,   279,   280,
     281,   282,   283,   284,   285,   286,   287,   288,   289,   290,
     291,   292,   293,   294,   295,   296,   297,   298,   299,     0,
     300,   301,   302,   303,   304,   305,   306,   307,   308,   309,
     310,   311
};

static const yytype_int16 yycheck[] =
{
       2,   184,   185,   554,   504,    73,   202,   507,   585,   594,
     202,   117,   597,    87,   202,   198,   202,   494,   218,   236,
     590,    23,    24,    25,    96,   464,   590,   202,   202,   590,
      81,   590,   202,   590,   251,   128,   590,   254,    10,   202,
      12,    13,    93,   590,   590,    17,    18,   202,   590,   590,
      22,     4,     4,   590,    26,   202,   590,   202,   590,    31,
      43,   590,   590,   590,    42,   642,   643,    85,     4,   588,
       4,     4,    71,     4,    27,     4,   646,     4,     4,    85,
       4,    14,   646,     4,     4,   646,    87,   646,     4,   646,
      96,    27,   646,    92,   533,    96,     0,   536,    27,   646,
     646,     4,     5,     4,   646,   646,     4,    27,    61,   646,
      93,    27,   646,    47,   646,    93,    88,   646,   646,   646,
     188,     4,    94,     5,     4,    61,   128,   233,   647,    27,
     204,   199,    61,    85,   240,    62,    93,   243,    64,    42,
      93,    62,    62,    54,   237,    61,   623,    27,   725,   255,
       4,     4,    63,     7,     4,     4,   726,    93,   743,   598,
      42,   133,   726,    61,    93,   726,   366,   726,     4,   726,
     221,   222,   726,    93,    27,    46,    45,    93,    27,   726,
     726,    61,    85,   162,   726,   726,     4,   159,   160,   726,
      93,   163,   726,    71,   726,    93,    60,   726,   726,   726,
     202,     4,   779,    85,    93,     4,     4,    96,    61,    27,
       4,    93,    61,    93,    92,    71,     4,   717,    65,    23,
      91,   200,    26,     4,    27,   197,   703,   704,    27,    27,
     232,     4,    71,    27,   711,   237,    92,   239,     4,    27,
      93,     7,   348,    61,    93,   224,    27,    90,     4,     4,
      93,   223,    38,    92,   226,   227,   228,   229,    61,     4,
       5,    27,    61,    29,    62,   816,   334,    61,   340,    86,
     342,    37,    27,    61,    91,    93,    16,    17,    18,    19,
      93,    62,   759,    92,    50,    51,   837,    53,    54,    55,
      93,    91,    88,    93,    93,    61,    62,    93,    64,    93,
      93,    87,    68,    69,    85,    93,    61,    85,   504,    86,
      96,   507,   504,    93,    91,   507,   504,     4,   504,   507,
      91,   507,    93,    86,    93,   802,   803,    93,    91,   504,
     504,    71,   507,   507,   504,   403,     4,   507,    93,    46,
      27,   504,   314,   315,   507,   822,   452,   415,   416,   504,
      93,    86,   507,   325,   426,    42,    91,   504,    85,   504,
     507,    29,   507,   469,    71,   437,    86,    86,   808,   475,
     810,    91,    91,   441,   442,    62,     4,     5,    81,    82,
      86,   449,   450,   385,    91,    91,    54,    86,     4,    83,
      84,    93,    91,   395,   590,    63,   398,    93,   590,    20,
      21,    93,   590,    93,   590,   511,   408,    92,    93,   515,
     382,    27,   384,    93,   386,   590,   590,    92,    93,    93,
     590,     4,     5,   205,   206,   397,    42,   590,   534,   219,
     220,   537,   538,    93,    93,   590,    93,    85,   544,   622,
       4,   547,   444,   590,    61,   590,    62,    85,   420,     4,
     646,   423,   424,    85,   646,    24,    35,    34,   646,     3,
     646,    64,    85,     4,   570,    85,    93,    93,   574,    85,
      85,   646,   646,    85,    96,    93,   646,   583,   584,     4,
      41,   453,   454,   646,    85,    93,     8,     4,   460,     4,
      86,   646,    91,    86,    16,    17,    18,    19,     4,   646,
      85,   646,   504,     4,    86,   507,    86,     4,     4,    23,
     512,     4,    41,   485,    85,     4,    88,     5,     4,   702,
       5,   717,    41,     6,    71,   717,    85,   499,    85,   717,
     726,   717,    85,    85,   726,   752,    86,   539,   726,   645,
     726,   543,   717,   717,   516,    93,    86,   717,    93,    71,
      93,   726,   726,    89,   717,    85,   726,     3,    91,     5,
      91,    85,   717,   726,    85,    87,    92,    89,   751,    85,
     717,   726,   717,   679,    96,    93,   578,   683,   580,   726,
      88,   726,    85,   585,    30,    31,    32,    33,   590,    93,
      42,    86,     4,    46,   596,    86,    86,   569,   600,   601,
      14,   573,     4,    90,   606,    86,   608,    91,    86,     4,
     582,    93,    13,    93,    93,     4,   722,    71,    72,    73,
      74,    75,    76,    77,    78,    79,    80,    93,    86,   631,
      93,    25,    93,   635,    93,    81,    82,   609,    46,    48,
     642,   643,    24,    93,   646,   617,   618,     3,     4,     5,
     756,     7,     8,     9,    28,     4,    93,    56,    86,    59,
      93,    67,     4,    93,    86,    86,    86,   773,    86,   775,
       4,    27,     4,     7,    30,    31,    32,    33,    93,    27,
      36,    37,    86,    39,    40,    22,    93,     3,    22,    86,
      86,    24,     3,    27,    86,    29,    71,     4,    85,   805,
      93,     4,    93,    37,    60,    93,    62,    93,    64,    93,
      93,    92,     4,     4,    13,   717,    50,    51,    41,    53,
      54,    55,    93,   725,   726,    93,    82,    61,    62,    85,
      64,    87,   838,    89,    68,    69,    93,    56,    93,   741,
      96,    93,   744,    93,    65,     4,     3,     4,     5,    93,
       7,     8,     9,     4,    93,     4,    96,    93,    93,    93,
       3,     4,     5,     4,     7,     8,     9,    93,    24,    27,
      27,    24,    38,    30,    31,    32,    33,   779,    93,    36,
      37,    93,    39,    40,    27,    93,    71,    30,    31,    32,
      33,   793,    93,    36,    37,    93,    39,    40,    93,    93,
      93,    56,    52,    60,    93,    62,   808,    64,   810,    93,
      93,    93,    93,    93,    93,    93,    93,    60,    52,    62,
      93,    64,    93,   795,    66,    82,    65,   829,    85,    93,
      87,    88,    89,    85,     4,     4,    93,    85,     4,    82,
       4,    86,    85,     4,    87,    22,    89,    93,   850,    93,
      93,     4,     5,    93,    22,    93,    93,    93,    93,     5,
      93,    14,    15,    93,    93,    93,    93,    92,     4,    22,
      93,    24,    25,    93,    27,    93,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,     4,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,     4,     5,
      93,    93,    93,    52,    52,     4,    93,    86,    14,    15,
      93,    93,     4,    93,     4,    93,    22,    90,    24,    25,
      93,    27,    93,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    93,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69,    70,     4,    93,    93,     7,    93,
      61,    10,    11,    12,     4,    93,    15,    93,    93,    93,
      93,     4,     4,    22,    61,     4,     4,    93,    27,    93,
      29,    93,    93,    93,     4,     4,    93,    93,    37,    38,
      93,   164,    41,   143,   239,    44,    45,   152,   103,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,   811,    61,    62,    89,    64,    65,    66,    67,    68,
      69,    70,     4,   412,   203,     7,   491,   333,    10,    11,
      12,   217,   616,    15,    -1,    -1,    -1,    -1,    -1,    -1,
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
      -1,    -1,    37,    -1,    -1,    -1,    -1,    -1,     3,     4,
       5,    -1,     7,     8,     9,    50,    51,    -1,    53,    54,
      55,    -1,    93,    -1,    -1,    -1,    61,    62,    -1,    64,
      -1,    -1,    27,    68,    69,    30,    31,    32,    33,    -1,
      -1,    36,    37,    -1,    39,    40,    -1,    42,     3,     4,
       5,    -1,     7,     8,     9,    -1,    -1,    -1,    93,    -1,
      -1,    -1,    -1,    -1,    -1,    60,    -1,    62,    -1,    64,
      -1,    -1,    27,    -1,    -1,    30,    31,    32,    33,    -1,
      -1,    36,    37,    -1,    39,    40,    -1,    82,    -1,    -1,
      85,    -1,    87,    -1,    89,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    60,    -1,    62,    -1,    64,
      -1,     3,     4,     5,    -1,     7,     8,     9,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    82,    -1,    -1,
      85,    -1,    87,    -1,    89,    27,    -1,    -1,    30,    31,
      32,    33,    -1,    -1,    36,    -1,    -1,    39,    40,     3,
       4,     5,    -1,     7,     8,     9,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    60,    -1,
      62,    -1,    64,    27,    -1,    -1,    30,    31,    32,    33,
      -1,    -1,    -1,    -1,    -1,    39,    40,     8,    -1,    -1,
      82,    -1,    -1,    85,    -1,    87,    -1,    89,    -1,    20,
      21,    -1,    -1,    -1,    -1,    -1,    60,    -1,    62,    -1,
      64,    -1,     3,     4,     5,    -1,     7,    -1,     9,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    82,    -1,
      -1,    85,    -1,    87,    -1,    89,    27,    -1,    -1,    30,
      31,    32,    33,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      71,    72,    73,    74,    75,    76,    77,    78,    79,    80,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    89,    60,
      -1,    62,    -1,    64,    -1,    -1,    -1,    -1,    -1,     4,
       5,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    14,
      15,    -1,    -1,    -1,    85,    -1,    87,    22,    89,    24,
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
      67,    68,    69,    70,     4,     5,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    14,    15,    -1,    -1,    -1,    86,
      -1,    -1,    22,    -1,    24,    25,    -1,    27,    -1,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    -1,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      70,     4,    -1,    -1,     7,    -1,    -1,    -1,    -1,    -1,
      -1,    14,    15,    -1,    -1,    -1,    86,    -1,    -1,    22,
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
      66,    67,    68,    69,    70,     4,    -1,    -1,     7,    -1,
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
       0,    98,    99,     0,     4,     7,    10,    11,    12,    15,
      22,    27,    29,    37,    38,    41,    44,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    61,
      62,    64,    65,    66,    67,    68,    69,    70,    93,   100,
     101,   104,   105,   110,   111,   112,   114,   115,   116,   117,
     122,   123,   124,   125,   126,   129,   130,   131,   133,   136,
     137,   144,   145,   146,   147,   148,   149,   150,   151,   152,
     153,     4,    85,    85,     4,     4,     4,     3,     4,     5,
       7,     8,     9,    30,    31,    32,    33,    36,    37,    39,
      40,    60,    64,    82,    85,    87,    89,   105,   157,   158,
     159,   160,   161,   162,   163,   164,   165,   166,   168,   169,
     172,    42,   157,   157,     4,     4,    47,    93,   157,   157,
       4,     4,     4,     7,   157,   105,   105,     4,    85,   105,
     132,   143,   157,    45,    60,     4,    64,    96,   157,     4,
      27,    42,    62,   108,   109,   127,     4,     4,     4,     5,
       4,     5,    65,    93,     8,    16,    17,    18,    19,    71,
      87,    89,    96,   103,   106,    92,    93,    93,    93,    93,
      93,    93,    93,    93,    93,    93,    93,    93,    93,    85,
     157,   173,   174,   173,    85,    85,    61,     4,    85,   165,
     160,   157,   166,     4,    85,   165,   157,   183,   183,    85,
      96,   170,    24,    35,    34,    20,    21,    71,    72,    73,
      74,    75,    76,    77,    78,    79,    80,   106,   167,    81,
      82,    83,    84,    87,    96,     3,    64,    85,    46,    71,
      91,     4,    99,    93,    93,    85,    85,    85,   132,    91,
      93,    96,   157,    93,     4,    29,    54,    63,    54,    63,
       4,    85,   109,    41,    85,    93,     4,    14,     4,   127,
     157,   157,   107,     4,     7,    14,    15,    22,    24,    25,
      27,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,   180,   157,    71,   103,     4,   102,     4,     5,
      86,   179,   180,   181,    86,    91,    86,    86,   181,    86,
     181,     4,   173,    38,    85,    86,    86,    88,    93,   157,
     175,    90,   181,   173,     4,     7,   180,     4,    93,   101,
     105,   110,   111,   126,   145,   146,   147,   149,   150,   151,
     152,   156,   159,   160,   163,   163,   167,   162,   164,   164,
     165,   165,   157,     4,     7,   180,     4,   157,   157,   157,
     157,     4,    46,    91,    45,    99,    23,   118,     4,   176,
     178,   176,   132,    86,   143,    99,     4,    41,    99,     4,
      62,     4,    62,    85,   176,     4,   128,   176,    99,     5,
       4,     5,    41,    88,     6,    85,    85,   157,   157,    85,
      71,    85,    93,    71,    92,    86,    91,   157,    93,    86,
      93,    86,    93,    96,    86,   169,   173,    91,   183,   183,
      86,    85,    85,   171,    99,    92,    93,   162,    88,    85,
      85,    86,    93,    42,    46,   157,     4,   157,    27,   157,
      23,    26,   119,    71,    92,    86,    91,    86,    86,    93,
      27,   157,    27,   173,    86,    93,    86,    27,    14,   128,
      90,   173,   173,     4,   157,     4,   182,   135,   157,   157,
      93,   183,   138,    42,    93,    93,    13,     4,    86,   183,
      88,    90,   173,   173,    25,    27,   154,    25,   155,   173,
     173,    93,    99,   157,   157,    93,    46,    93,    48,    24,
     157,    93,    28,     3,     5,    30,    31,    32,    33,    81,
      82,   177,   102,    92,    93,     4,    92,    93,    93,    99,
      56,    59,    86,    99,    93,    67,     4,    93,    86,    86,
      86,   157,    86,   140,   134,     4,   179,     4,    27,    61,
      93,   139,     4,   138,    27,    93,   157,    86,    86,    22,
      93,   156,    22,    22,    93,   156,    86,    86,    99,    27,
      62,   113,    43,    93,    93,    99,   157,    93,    93,    24,
     120,    93,     3,     3,    71,   102,    99,    71,    92,   102,
      99,    99,    27,    93,    93,    27,    99,    93,    99,    92,
       4,    27,    61,    93,   141,   140,    85,    71,    92,     4,
       5,    42,    85,    93,     4,     4,     4,    27,     4,    13,
     157,    99,    93,    93,   157,    99,    93,    27,    41,    93,
     105,   157,    99,    99,   113,    93,   120,    93,    15,    93,
     101,   110,   111,   112,   114,   115,   116,   117,   121,   122,
     123,   124,   125,   126,   129,   130,   131,   137,   144,   145,
     146,   147,   148,   149,   150,   151,   152,   153,   177,    93,
      27,   177,   102,    93,    27,    27,    56,    65,    27,    27,
     157,     4,     5,   142,     4,     4,    27,   182,   157,   157,
       5,    42,    85,    93,    93,     4,    86,   181,   138,    93,
      96,    93,     4,    93,    27,    24,    27,    24,    27,    38,
      93,    93,    93,   113,   113,    99,   120,    93,    93,    93,
      93,    93,    93,    93,    93,    93,    93,    93,    93,    93,
      93,    99,    52,    71,    99,    52,    56,    93,    93,    66,
      65,    85,    85,    91,    93,    93,    93,     4,    86,    93,
       4,    86,   181,   138,   138,     4,    93,    86,    27,     4,
     138,    93,     4,    93,    22,    93,   156,    22,    93,    99,
     113,    27,    93,   177,    27,    93,    93,    93,    93,    86,
     181,   176,     5,    99,    93,    92,   138,     4,    93,    86,
      27,    27,    93,    93,     4,    93,    27,    93,    99,    93,
      99,    93,    93,   113,    52,    52,    93,    86,    86,    27,
     157,    27,    93,    93,     4,     4,   138,   138,    93,    99,
       4,   154,   154,   155,    93,    93,   140,    93,    93,    61,
       4,   138,    93,    93,    27,    27,    27,    93,    27,   140,
      99,    93,    93,    27,     4,     4,    61,     4,    27,    27,
       4,    93,    93,    93,    93,     4,     4,    93,    93,    93
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    97,    98,    99,    99,    99,   100,   100,   100,   100,
     100,   100,   100,   100,   100,   100,   100,   100,   100,   100,
     100,   100,   100,   100,   100,   100,   100,   100,   100,   100,
     100,   100,   100,   100,   100,   100,   101,   101,   101,   101,
     101,   102,   102,   103,   103,   103,   103,   104,   104,   104,
     105,   105,   105,   107,   106,   106,   108,   108,   109,   109,
     109,   109,   110,   110,   111,   111,   111,   111,   111,   111,
     112,   113,   113,   113,   114,   114,   114,   114,   114,   114,
     115,   116,   117,   118,   118,   119,   119,   120,   120,   120,
     121,   121,   121,   121,   121,   121,   121,   121,   121,   121,
     121,   121,   121,   121,   121,   121,   121,   121,   121,   121,
     121,   121,   121,   121,   121,   121,   121,   121,   122,   122,
     122,   122,   123,   123,   124,   125,   126,   126,   126,   126,
     126,   126,   126,   126,   127,   127,   128,   129,   129,   129,
     130,   131,   131,   132,   132,   134,   133,   135,   133,   136,
     136,   136,   136,   137,   137,   137,   138,   138,   138,   139,
     139,   139,   139,   139,   139,   139,   139,   139,   139,   139,
     140,   140,   140,   141,   141,   141,   141,   141,   142,   142,
     143,   143,   144,   145,   145,   145,   145,   145,   145,   145,
     145,   146,   147,   147,   148,   149,   150,   151,   151,   152,
     152,   153,   153,   154,   154,   154,   154,   155,   155,   155,
     155,   155,   156,   156,   156,   156,   156,   156,   156,   156,
     156,   156,   156,   157,   157,   158,   158,   159,   159,   160,
     160,   161,   161,   161,   162,   162,   162,   163,   163,   163,
     164,   164,   164,   165,   165,   165,   165,   165,   165,   166,
     166,   166,   166,   166,   167,   167,   167,   167,   167,   167,
     167,   167,   167,   167,   168,   168,   168,   168,   168,   168,
     168,   168,   168,   168,   168,   168,   168,   168,   168,   168,
     169,   169,   170,   170,   170,   170,   171,   171,   172,   172,
     173,   173,   174,   174,   175,   175,   176,   176,   177,   177,
     177,   177,   177,   177,   177,   177,   178,   178,   178,   178,
     178,   178,   178,   178,   179,   179,   180,   180,   180,   180,
     180,   180,   180,   180,   180,   180,   180,   180,   180,   180,
     180,   180,   180,   180,   180,   180,   180,   180,   180,   180,
     180,   180,   180,   180,   180,   180,   180,   180,   180,   180,
     180,   180,   180,   180,   180,   180,   180,   180,   180,   180,
     180,   180,   180,   180,   181,   181,   181,   181,   181,   181,
     182,   182,   183,   183
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       2,     1,     1,     2,     2,     1,     2,     2,     2,     2,
       2,     2,     2,     2,     1,     1,     3,     3,     4,     5,
       4,     1,     4,     1,     1,     1,     1,     1,     4,     3,
       1,     1,     1,     0,     4,     1,     1,     2,     1,     1,
       1,     1,     2,     4,     4,     2,     4,     6,     6,     6,
      10,     3,     2,     3,     7,     8,     9,    10,     9,    11,
       6,     7,     7,     5,     6,     0,     3,     0,     2,     2,
       2,     2,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     2,     1,     2,     2,     1,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     1,    10,    10,
      12,    12,     9,    10,    10,     7,     2,     2,     2,     2,
       4,     4,     4,     6,     1,     4,     1,     9,     7,    10,
       2,     3,     5,     1,     3,     0,    11,     0,    10,     6,
       5,     9,    11,    10,     9,    13,     0,     2,     2,     4,
       6,     5,     6,     9,     7,     8,     7,     9,    10,     9,
       0,     2,     2,     3,    10,    10,     9,     7,     1,     3,
       1,     3,     7,     4,     4,     3,     4,     4,     3,     3,
       3,     2,     1,     2,     2,     2,     2,     1,     2,     1,
       2,     6,     6,     3,     3,     6,     7,     0,     3,     6,
       7,     7,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     2,     1,     3,     1,     3,     1,
       2,     1,     3,     4,     1,     3,     3,     1,     3,     3,
       1,     3,     3,     1,     2,     2,     2,     4,     5,     1,
       4,     3,     6,     6,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     3,     1,     1,     2,
       4,     1,     1,     1,     1,     1,     3,     3,     5,     1,
       3,     5,     0,     3,     3,     5,     0,     3,     2,     3,
       0,     1,     1,     3,     1,     4,     0,     1,     1,     2,
       2,     1,     1,     1,     1,     1,     1,     3,     3,     3,
       5,     5,     7,     5,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
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
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3197 "src/parser.tab.c"
        break;

    case YYSYMBOL_STRING: /* STRING  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3203 "src/parser.tab.c"
        break;

    case YYSYMBOL_LENS_CONTENT: /* LENS_CONTENT  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3209 "src/parser.tab.c"
        break;

    case YYSYMBOL_QUALIFIED_IDENT: /* QUALIFIED_IDENT  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3215 "src/parser.tab.c"
        break;

    case YYSYMBOL_MODIFIER_PREFIX: /* MODIFIER_PREFIX  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3221 "src/parser.tab.c"
        break;

    case YYSYMBOL_MODIFIER_VALUE: /* MODIFIER_VALUE  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3227 "src/parser.tab.c"
        break;

    case YYSYMBOL_SQL_TEXT: /* SQL_TEXT  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3233 "src/parser.tab.c"
        break;

    case YYSYMBOL_program: /* program  */
#line 890 "src/parser.y"
            { (void) ((*yyvaluep).stmt_list); }
#line 3239 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement_list: /* statement_list  */
#line 827 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3245 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement: /* statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3251 "src/parser.tab.c"
        break;

    case YYSYMBOL_assignment: /* assignment  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3257 "src/parser.tab.c"
        break;

    case YYSYMBOL_type_lock: /* type_lock  */
#line 880 "src/parser.y"
            { ast_type_lock_free(((*yyvaluep).type_lock)); }
#line 3263 "src/parser.tab.c"
        break;

    case YYSYMBOL_lvalue: /* lvalue  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3269 "src/parser.tab.c"
        break;

    case YYSYMBOL_variable_name: /* variable_name  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3275 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_lens: /* comparison_lens  */
#line 874 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 3281 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_name: /* modifier_name  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3287 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_word: /* modifier_word  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3293 "src/parser.tab.c"
        break;

    case YYSYMBOL_print_statement: /* print_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3299 "src/parser.tab.c"
        break;

    case YYSYMBOL_call_statement: /* call_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3305 "src/parser.tab.c"
        break;

    case YYSYMBOL_with_lock_statement: /* with_lock_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3311 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_end: /* for_end  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3317 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_each_statement: /* for_each_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3323 "src/parser.tab.c"
        break;

    case YYSYMBOL_do_loop_statement: /* do_loop_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3329 "src/parser.tab.c"
        break;

    case YYSYMBOL_while_statement: /* while_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3335 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement: /* consider_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3341 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_branch_list: /* consider_branch_list  */
#line 872 "src/parser.y"
            { ast_free_consider_branch_list(((*yyvaluep).consider_branch_list)); }
#line 3347 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_else_opt: /* consider_else_opt  */
#line 827 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3353 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement_list: /* consider_statement_list  */
#line 827 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3359 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_body_statement: /* consider_body_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3365 "src/parser.tab.c"
        break;

    case YYSYMBOL_function_statement: /* function_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3371 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_statement: /* modifier_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3377 "src/parser.tab.c"
        break;

    case YYSYMBOL_program_statement: /* program_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3383 "src/parser.tab.c"
        break;

    case YYSYMBOL_library_statement: /* library_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3389 "src/parser.tab.c"
        break;

    case YYSYMBOL_use_statement: /* use_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3395 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_signature: /* modifier_signature  */
#line 875 "src/parser.y"
            { ast_free_modifier_signature(((*yyvaluep).modifier_signature)); }
#line 3401 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_context: /* modifier_context  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3407 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_statement: /* watch_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3413 "src/parser.tab.c"
        break;

    case YYSYMBOL_unwatch_statement: /* unwatch_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3419 "src/parser.tab.c"
        break;

    case YYSYMBOL_wait_statement: /* wait_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3425 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_list: /* watch_target_list  */
#line 873 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3431 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_statement: /* server_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3437 "src/parser.tab.c"
        break;

    case YYSYMBOL_gsql_block: /* gsql_block  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3443 "src/parser.tab.c"
        break;

    case YYSYMBOL_window_block: /* window_block  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3449 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item_list: /* server_item_list  */
#line 879 "src/parser.y"
            { ast_free_server_item_list(((*yyvaluep).server_item_list)); }
#line 3455 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item: /* server_item  */
#line 878 "src/parser.y"
            { AstServerItemList one = ast_server_item_list_append(ast_server_item_list_empty(), ((*yyvaluep).server_item)); ast_free_server_item_list(one); }
#line 3461 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_string_list: /* server_string_list  */
#line 873 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3467 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_path: /* watch_target_path  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3473 "src/parser.tab.c"
        break;

    case YYSYMBOL_without_watchers_statement: /* without_watchers_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3479 "src/parser.tab.c"
        break;

    case YYSYMBOL_on_error_statement: /* on_error_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3485 "src/parser.tab.c"
        break;

    case YYSYMBOL_error_statement: /* error_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3491 "src/parser.tab.c"
        break;

    case YYSYMBOL_return_statement: /* return_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3497 "src/parser.tab.c"
        break;

    case YYSYMBOL_label_statement: /* label_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3503 "src/parser.tab.c"
        break;

    case YYSYMBOL_goto_statement: /* goto_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3509 "src/parser.tab.c"
        break;

    case YYSYMBOL_gosub_statement: /* gosub_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3515 "src/parser.tab.c"
        break;

    case YYSYMBOL_break_statement: /* break_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3521 "src/parser.tab.c"
        break;

    case YYSYMBOL_continue_statement: /* continue_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3527 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_statement: /* if_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3533 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_block_tail: /* if_block_tail  */
#line 827 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3539 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_inline_tail: /* if_inline_tail  */
#line 827 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3545 "src/parser.tab.c"
        break;

    case YYSYMBOL_inline_statement: /* inline_statement  */
#line 868 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3551 "src/parser.tab.c"
        break;

    case YYSYMBOL_expression: /* expression  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3557 "src/parser.tab.c"
        break;

    case YYSYMBOL_or_expression: /* or_expression  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3563 "src/parser.tab.c"
        break;

    case YYSYMBOL_and_expression: /* and_expression  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3569 "src/parser.tab.c"
        break;

    case YYSYMBOL_not_expression: /* not_expression  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3575 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_expression: /* comparison_expression  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3581 "src/parser.tab.c"
        break;

    case YYSYMBOL_set_expression: /* set_expression  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3587 "src/parser.tab.c"
        break;

    case YYSYMBOL_additive_expression: /* additive_expression  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3593 "src/parser.tab.c"
        break;

    case YYSYMBOL_multiplicative_expression: /* multiplicative_expression  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3599 "src/parser.tab.c"
        break;

    case YYSYMBOL_unary_expression: /* unary_expression  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3605 "src/parser.tab.c"
        break;

    case YYSYMBOL_postfix_expression: /* postfix_expression  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3611 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_operator: /* comparison_operator  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3617 "src/parser.tab.c"
        break;

    case YYSYMBOL_primary: /* primary  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3623 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_literal: /* record_literal  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3629 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_suffix: /* ident_suffix  */
#line 876 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3635 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_dot_suffix: /* ident_dot_suffix  */
#line 876 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3641 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list_opt: /* argument_list_opt  */
#line 870 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3647 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list: /* argument_list  */
#line 870 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3653 "src/parser.tab.c"
        break;

    case YYSYMBOL_array_argument_list: /* array_argument_list  */
#line 870 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3659 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list_opt: /* parameter_list_opt  */
#line 873 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3665 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_default: /* parameter_default  */
#line 867 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3671 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list: /* parameter_list  */
#line 873 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3677 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_name: /* field_name  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3683 "src/parser.tab.c"
        break;

    case YYSYMBOL_dot_field_name: /* dot_field_name  */
#line 866 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3689 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_field_list: /* record_field_list  */
#line 871 "src/parser.y"
            { ast_free_record_field_list(((*yyvaluep).record_field_list)); }
#line 3695 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_policy: /* field_policy  */
#line 877 "src/parser.y"
            { ast_free_expr(((*yyvaluep).field_policy).reset_expr); }
#line 3701 "src/parser.tab.c"
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
#line 4007 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 899 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4013 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 900 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 4019 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 901 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 4025 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 905 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4031 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 906 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4037 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 907 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4043 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 908 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4049 "src/parser.tab.c"
    break;

  case 10: /* statement: window_block  */
#line 909 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4055 "src/parser.tab.c"
    break;

  case 11: /* statement: gsql_block  */
#line 910 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4061 "src/parser.tab.c"
    break;

  case 12: /* statement: for_each_statement  */
#line 911 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4067 "src/parser.tab.c"
    break;

  case 13: /* statement: while_statement  */
#line 912 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4073 "src/parser.tab.c"
    break;

  case 14: /* statement: do_loop_statement  */
#line 913 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4079 "src/parser.tab.c"
    break;

  case 15: /* statement: consider_statement  */
#line 914 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4085 "src/parser.tab.c"
    break;

  case 16: /* statement: function_statement  */
#line 915 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4091 "src/parser.tab.c"
    break;

  case 17: /* statement: modifier_statement  */
#line 916 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4097 "src/parser.tab.c"
    break;

  case 18: /* statement: program_statement  */
#line 917 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4103 "src/parser.tab.c"
    break;

  case 19: /* statement: library_statement  */
#line 918 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4109 "src/parser.tab.c"
    break;

  case 20: /* statement: use_statement NEWLINE  */
#line 919 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4115 "src/parser.tab.c"
    break;

  case 21: /* statement: watch_statement  */
#line 920 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4121 "src/parser.tab.c"
    break;

  case 22: /* statement: server_statement  */
#line 921 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4127 "src/parser.tab.c"
    break;

  case 23: /* statement: unwatch_statement NEWLINE  */
#line 922 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4133 "src/parser.tab.c"
    break;

  case 24: /* statement: wait_statement NEWLINE  */
#line 923 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4139 "src/parser.tab.c"
    break;

  case 25: /* statement: without_watchers_statement  */
#line 924 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4145 "src/parser.tab.c"
    break;

  case 26: /* statement: on_error_statement NEWLINE  */
#line 925 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4151 "src/parser.tab.c"
    break;

  case 27: /* statement: error_statement NEWLINE  */
#line 926 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4157 "src/parser.tab.c"
    break;

  case 28: /* statement: return_statement NEWLINE  */
#line 927 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4163 "src/parser.tab.c"
    break;

  case 29: /* statement: label_statement NEWLINE  */
#line 928 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4169 "src/parser.tab.c"
    break;

  case 30: /* statement: goto_statement NEWLINE  */
#line 929 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4175 "src/parser.tab.c"
    break;

  case 31: /* statement: gosub_statement NEWLINE  */
#line 930 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4181 "src/parser.tab.c"
    break;

  case 32: /* statement: break_statement NEWLINE  */
#line 931 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4187 "src/parser.tab.c"
    break;

  case 33: /* statement: continue_statement NEWLINE  */
#line 932 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4193 "src/parser.tab.c"
    break;

  case 34: /* statement: if_statement  */
#line 933 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4199 "src/parser.tab.c"
    break;

  case 35: /* statement: DIM  */
#line 939 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 4212 "src/parser.tab.c"
    break;

  case 36: /* assignment: lvalue OP_EQ expression  */
#line 950 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 4218 "src/parser.tab.c"
    break;

  case 37: /* assignment: lvalue compound_op expression  */
#line 955 "src/parser.y"
                                    { (yyval.stmt) = ast_assign_op((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr), (yyvsp[-1].op_char)); }
#line 4224 "src/parser.tab.c"
    break;

  case 38: /* assignment: lvalue comparison_lens compound_op expression  */
#line 956 "src/parser.y"
                                                    {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign_op((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr), (yyvsp[-1].op_char));
      }
#line 4238 "src/parser.tab.c"
    break;

  case 39: /* assignment: variable_name COLON type_lock OP_EQ expression  */
#line 972 "src/parser.y"
                                                     {
        if (!type_lock_check(ctx, (yyvsp[-4].text), (yyvsp[-2].type_lock).name, (yyvsp[-2].type_lock).arg, (yylsp[-2]).first_line, (yylsp[-2]).first_column,
                             (yylsp[-2]).last_line, (yylsp[-2]).last_column)) {
            YYERROR;
        }
        (yyval.stmt) = ast_assign_locked(expr_at(ast_ident((yyvsp[-4].text)), (yylsp[-4]).first_line, (yylsp[-4]).first_column), (yyvsp[-2].type_lock), (yyvsp[0].expr));
      }
#line 4250 "src/parser.tab.c"
    break;

  case 40: /* assignment: lvalue comparison_lens OP_EQ expression  */
#line 979 "src/parser.y"
                                              {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 4264 "src/parser.tab.c"
    break;

  case 41: /* type_lock: IDENT  */
#line 993 "src/parser.y"
            { (yyval.type_lock) = ast_type_lock((yyvsp[0].text), NULL, (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4270 "src/parser.tab.c"
    break;

  case 42: /* type_lock: IDENT LPAREN IDENT RPAREN  */
#line 994 "src/parser.y"
                                { (yyval.type_lock) = ast_type_lock((yyvsp[-3].text), (yyvsp[-1].text), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 4276 "src/parser.tab.c"
    break;

  case 43: /* compound_op: PLUS_EQ  */
#line 998 "src/parser.y"
               { (yyval.op_char) = '+'; }
#line 4282 "src/parser.tab.c"
    break;

  case 44: /* compound_op: MINUS_EQ  */
#line 999 "src/parser.y"
               { (yyval.op_char) = '-'; }
#line 4288 "src/parser.tab.c"
    break;

  case 45: /* compound_op: STAR_EQ  */
#line 1000 "src/parser.y"
               { (yyval.op_char) = '*'; }
#line 4294 "src/parser.tab.c"
    break;

  case 46: /* compound_op: SLASH_EQ  */
#line 1001 "src/parser.y"
               { (yyval.op_char) = '/'; }
#line 4300 "src/parser.tab.c"
    break;

  case 47: /* lvalue: variable_name  */
#line 1005 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4306 "src/parser.tab.c"
    break;

  case 48: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 1006 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4312 "src/parser.tab.c"
    break;

  case 49: /* lvalue: lvalue DOT dot_field_name  */
#line 1007 "src/parser.y"
                                             { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4318 "src/parser.tab.c"
    break;

  case 50: /* variable_name: IDENT  */
#line 1011 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 4324 "src/parser.tab.c"
    break;

  case 51: /* variable_name: END  */
#line 1012 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 4330 "src/parser.tab.c"
    break;

  case 52: /* variable_name: NEXT  */
#line 1013 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 4336 "src/parser.tab.c"
    break;

  case 53: /* $@1: %empty  */
#line 1024 "src/parser.y"
             { lexer_begin_lens_content(ctx->active_lexer); }
#line 4342 "src/parser.tab.c"
    break;

  case 54: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 1024 "src/parser.y"
                                                                                  {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 4350 "src/parser.tab.c"
    break;

  case 55: /* comparison_lens: MODIFIER_PREFIX  */
#line 1032 "src/parser.y"
                      { (yyval.modifier) = parse_modifier_use((yyvsp[0].text)); }
#line 4356 "src/parser.tab.c"
    break;

  case 56: /* modifier_name: modifier_word  */
#line 1036 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 4362 "src/parser.tab.c"
    break;

  case 57: /* modifier_name: modifier_name modifier_word  */
#line 1037 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 4368 "src/parser.tab.c"
    break;

  case 58: /* modifier_word: IDENT  */
#line 1041 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4374 "src/parser.tab.c"
    break;

  case 59: /* modifier_word: TO  */
#line 1042 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 4380 "src/parser.tab.c"
    break;

  case 60: /* modifier_word: END  */
#line 1043 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 4386 "src/parser.tab.c"
    break;

  case 61: /* modifier_word: NEXT  */
#line 1044 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 4392 "src/parser.tab.c"
    break;

  case 62: /* print_statement: PRINT expression  */
#line 1048 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 4398 "src/parser.tab.c"
    break;

  case 63: /* print_statement: PRINT TO ERROR_VALUE expression  */
#line 1054 "src/parser.y"
                                      { (yyval.stmt) = ast_print_error((yyvsp[0].expr)); }
#line 4404 "src/parser.tab.c"
    break;

  case 64: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 1058 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 4410 "src/parser.tab.c"
    break;

  case 65: /* call_statement: TRY expression  */
#line 1061 "src/parser.y"
                     { (yyval.stmt) = ast_expr_stmt(expr_at(ast_unary(copy_const("try"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column)); }
#line 4416 "src/parser.tab.c"
    break;

  case 66: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1062 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 4427 "src/parser.tab.c"
    break;

  case 67: /* call_statement: lvalue DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1068 "src/parser.y"
                                                       {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 4437 "src/parser.tab.c"
    break;

  case 68: /* call_statement: lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1073 "src/parser.y"
                                                                 {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 4451 "src/parser.tab.c"
    break;

  case 69: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1082 "src/parser.y"
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
#line 4466 "src/parser.tab.c"
    break;

  case 70: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 1095 "src/parser.y"
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
#line 4489 "src/parser.tab.c"
    break;

  case 71: /* for_end: END FOR NEWLINE  */
#line 1124 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 4495 "src/parser.tab.c"
    break;

  case 72: /* for_end: NEXT NEWLINE  */
#line 1125 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 4501 "src/parser.tab.c"
    break;

  case 73: /* for_end: NEXT variable_name NEWLINE  */
#line 1126 "src/parser.y"
                                 { (yyval.text) = (yyvsp[-1].text); }
#line 4507 "src/parser.tab.c"
    break;

  case 74: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list for_end  */
#line 1130 "src/parser.y"
                                                             {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4516 "src/parser.tab.c"
    break;

  case 75: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list for_end  */
#line 1134 "src/parser.y"
                                                                  {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4525 "src/parser.tab.c"
    break;

  case 76: /* for_each_statement: FOR IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1150 "src/parser.y"
                                                                         {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { YYERROR; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4535 "src/parser.tab.c"
    break;

  case 77: /* for_each_statement: FOR EACH IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1155 "src/parser.y"
                                                                              {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { YYERROR; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4545 "src/parser.tab.c"
    break;

  case 78: /* for_each_statement: FOR IDENT OP_EQ expression TO expression NEWLINE statement_list for_end  */
#line 1163 "src/parser.y"
                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].expr), NULL, (yyvsp[-1].stmt_list));
      }
#line 4554 "src/parser.tab.c"
    break;

  case 79: /* for_each_statement: FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list for_end  */
#line 1167 "src/parser.y"
                                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-9].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-9].text), (yyvsp[-7].expr), (yyvsp[-5].expr), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4563 "src/parser.tab.c"
    break;

  case 80: /* do_loop_statement: DO NEWLINE statement_list UNTIL expression NEWLINE  */
#line 1188 "src/parser.y"
                                                         {
        (yyval.stmt) = ast_do_loop((yyvsp[-3].stmt_list), (yyvsp[-1].expr));
      }
#line 4571 "src/parser.tab.c"
    break;

  case 81: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 1194 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 4579 "src/parser.tab.c"
    break;

  case 82: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 1200 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 4587 "src/parser.tab.c"
    break;

  case 83: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 1206 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 4595 "src/parser.tab.c"
    break;

  case 84: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 1209 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 4603 "src/parser.tab.c"
    break;

  case 85: /* consider_else_opt: %empty  */
#line 1215 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4609 "src/parser.tab.c"
    break;

  case 86: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 1216 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 4615 "src/parser.tab.c"
    break;

  case 87: /* consider_statement_list: %empty  */
#line 1220 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4621 "src/parser.tab.c"
    break;

  case 88: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 1221 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 4627 "src/parser.tab.c"
    break;

  case 89: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 1222 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 4633 "src/parser.tab.c"
    break;

  case 90: /* consider_body_statement: assignment NEWLINE  */
#line 1226 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4639 "src/parser.tab.c"
    break;

  case 91: /* consider_body_statement: print_statement NEWLINE  */
#line 1227 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4645 "src/parser.tab.c"
    break;

  case 92: /* consider_body_statement: call_statement NEWLINE  */
#line 1228 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4651 "src/parser.tab.c"
    break;

  case 93: /* consider_body_statement: with_lock_statement  */
#line 1229 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4657 "src/parser.tab.c"
    break;

  case 94: /* consider_body_statement: window_block  */
#line 1230 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4663 "src/parser.tab.c"
    break;

  case 95: /* consider_body_statement: for_each_statement  */
#line 1231 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4669 "src/parser.tab.c"
    break;

  case 96: /* consider_body_statement: while_statement  */
#line 1232 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4675 "src/parser.tab.c"
    break;

  case 97: /* consider_body_statement: do_loop_statement  */
#line 1233 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4681 "src/parser.tab.c"
    break;

  case 98: /* consider_body_statement: consider_statement  */
#line 1234 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4687 "src/parser.tab.c"
    break;

  case 99: /* consider_body_statement: function_statement  */
#line 1235 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4693 "src/parser.tab.c"
    break;

  case 100: /* consider_body_statement: modifier_statement  */
#line 1236 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4699 "src/parser.tab.c"
    break;

  case 101: /* consider_body_statement: program_statement  */
#line 1237 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4705 "src/parser.tab.c"
    break;

  case 102: /* consider_body_statement: library_statement  */
#line 1238 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4711 "src/parser.tab.c"
    break;

  case 103: /* consider_body_statement: use_statement NEWLINE  */
#line 1239 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4717 "src/parser.tab.c"
    break;

  case 104: /* consider_body_statement: watch_statement  */
#line 1240 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4723 "src/parser.tab.c"
    break;

  case 105: /* consider_body_statement: unwatch_statement NEWLINE  */
#line 1241 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4729 "src/parser.tab.c"
    break;

  case 106: /* consider_body_statement: wait_statement NEWLINE  */
#line 1242 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4735 "src/parser.tab.c"
    break;

  case 107: /* consider_body_statement: without_watchers_statement  */
#line 1243 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4741 "src/parser.tab.c"
    break;

  case 108: /* consider_body_statement: on_error_statement NEWLINE  */
#line 1244 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4747 "src/parser.tab.c"
    break;

  case 109: /* consider_body_statement: error_statement NEWLINE  */
#line 1245 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4753 "src/parser.tab.c"
    break;

  case 110: /* consider_body_statement: return_statement NEWLINE  */
#line 1246 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4759 "src/parser.tab.c"
    break;

  case 111: /* consider_body_statement: label_statement NEWLINE  */
#line 1247 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4765 "src/parser.tab.c"
    break;

  case 112: /* consider_body_statement: goto_statement NEWLINE  */
#line 1248 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4771 "src/parser.tab.c"
    break;

  case 113: /* consider_body_statement: gosub_statement NEWLINE  */
#line 1249 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4777 "src/parser.tab.c"
    break;

  case 114: /* consider_body_statement: break_statement NEWLINE  */
#line 1250 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4783 "src/parser.tab.c"
    break;

  case 115: /* consider_body_statement: continue_statement NEWLINE  */
#line 1251 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4789 "src/parser.tab.c"
    break;

  case 116: /* consider_body_statement: if_statement  */
#line 1252 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4795 "src/parser.tab.c"
    break;

  case 117: /* consider_body_statement: DIM  */
#line 1258 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 4808 "src/parser.tab.c"
    break;

  case 118: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1269 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4816 "src/parser.tab.c"
    break;

  case 119: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1272 "src/parser.y"
                                                                                                            {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4825 "src/parser.tab.c"
    break;

  case 120: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN COLON type_lock NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1279 "src/parser.y"
                                                                                                                  {
        char who[160];
        snprintf(who, sizeof who, "%s(...)", (yyvsp[-10].text));
        if (!type_lock_check(ctx, who, (yyvsp[-5].type_lock).name, (yyvsp[-5].type_lock).arg, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                             (yylsp[-5]).last_line, (yylsp[-5]).last_column)) {
            YYERROR;
        }
        (yyval.stmt) = ast_function_returning((yyvsp[-10].text), (yyvsp[-8].name_list), (yyvsp[-5].type_lock), (yyvsp[-3].stmt_list));
      }
#line 4839 "src/parser.tab.c"
    break;

  case 121: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN COLON type_lock NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1288 "src/parser.y"
                                                                                                                            {
        char who[160];
        snprintf(who, sizeof who, "%s(...)", (yyvsp[-10].text));
        if (!type_lock_check(ctx, who, (yyvsp[-5].type_lock).name, (yyvsp[-5].type_lock).arg, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                             (yylsp[-5]).last_line, (yylsp[-5]).last_column)) {
            YYERROR;
        }
        (yyval.stmt) = ast_function_returning((yyvsp[-10].text), (yyvsp[-8].name_list), (yyvsp[-5].type_lock), (yyvsp[-3].stmt_list));
      }
#line 4853 "src/parser.tab.c"
    break;

  case 122: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 1300 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 4861 "src/parser.tab.c"
    break;

  case 123: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 1303 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 4869 "src/parser.tab.c"
    break;

  case 124: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 1309 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4877 "src/parser.tab.c"
    break;

  case 125: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 1315 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 4885 "src/parser.tab.c"
    break;

  case 126: /* use_statement: USE IDENT  */
#line 1321 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4891 "src/parser.tab.c"
    break;

  case 127: /* use_statement: LOAD IDENT  */
#line 1322 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4897 "src/parser.tab.c"
    break;

  case 128: /* use_statement: USE STRING  */
#line 1323 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4903 "src/parser.tab.c"
    break;

  case 129: /* use_statement: LOAD STRING  */
#line 1324 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4909 "src/parser.tab.c"
    break;

  case 130: /* use_statement: LOAD IDENT AS IDENT  */
#line 1325 "src/parser.y"
                          { (yyval.stmt) = ast_use((yyvsp[-2].text), NULL, (yyvsp[0].text)); }
#line 4915 "src/parser.tab.c"
    break;

  case 131: /* use_statement: USE IDENT IDENT STRING  */
#line 1326 "src/parser.y"
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
#line 4936 "src/parser.tab.c"
    break;

  case 132: /* use_statement: LOAD IDENT IDENT STRING  */
#line 1342 "src/parser.y"
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
#line 4957 "src/parser.tab.c"
    break;

  case 133: /* use_statement: LOAD IDENT IDENT STRING AS IDENT  */
#line 1358 "src/parser.y"
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
#line 4980 "src/parser.tab.c"
    break;

  case 134: /* modifier_signature: modifier_name  */
#line 1379 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 4986 "src/parser.tab.c"
    break;

  case 135: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 1380 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 4992 "src/parser.tab.c"
    break;

  case 136: /* modifier_context: IDENT  */
#line 1384 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4998 "src/parser.tab.c"
    break;

  case 137: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1388 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 5006 "src/parser.tab.c"
    break;

  case 138: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 1391 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 5014 "src/parser.tab.c"
    break;

  case 139: /* watch_statement: WATCH IDENT LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1399 "src/parser.y"
                                                                                           {
        (yyval.stmt) = ast_watch((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 5022 "src/parser.tab.c"
    break;

  case 140: /* unwatch_statement: UNWATCH expression  */
#line 1405 "src/parser.y"
                         { (yyval.stmt) = ast_unwatch((yyvsp[0].expr)); }
#line 5028 "src/parser.tab.c"
    break;

  case 141: /* wait_statement: WAIT UNTIL expression  */
#line 1412 "src/parser.y"
                            { (yyval.stmt) = ast_wait((yyvsp[0].expr), NULL); }
#line 5034 "src/parser.tab.c"
    break;

  case 142: /* wait_statement: WAIT UNTIL expression FOR expression  */
#line 1413 "src/parser.y"
                                           { (yyval.stmt) = ast_wait((yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 5040 "src/parser.tab.c"
    break;

  case 143: /* watch_target_list: watch_target_path  */
#line 1417 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5046 "src/parser.tab.c"
    break;

  case 144: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 1418 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5052 "src/parser.tab.c"
    break;

  case 145: /* @2: %empty  */
#line 1438 "src/parser.y"
      { (yyval.stmt) = NULL;   /* the mid-rule carries no value; typed so bison stays quiet */
        server_head_note(ctx, (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column); }
#line 5059 "src/parser.tab.c"
    break;

  case 146: /* server_statement: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE @2 server_item_list END IDENT NEWLINE  */
#line 1440 "src/parser.y"
                                         {
        ctx->bad_block_word[0] = '\0';
        (yyval.stmt) = ast_server((yyvsp[-10].text), (yyvsp[-9].text), (yyvsp[-7].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 5068 "src/parser.tab.c"
    break;

  case 147: /* @3: %empty  */
#line 1445 "src/parser.y"
      { (yyval.stmt) = NULL;   /* the mid-rule carries no value; typed so bison stays quiet */
        server_head_note(ctx, (yyvsp[-4].text), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5075 "src/parser.tab.c"
    break;

  case 148: /* server_statement: IDENT IDENT LPAREN RPAREN NEWLINE @3 server_item_list END IDENT NEWLINE  */
#line 1447 "src/parser.y"
                                         {
        ctx->bad_block_word[0] = '\0';
        (yyval.stmt) = ast_server((yyvsp[-9].text), (yyvsp[-8].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 5084 "src/parser.tab.c"
    break;

  case 149: /* gsql_block: DATABASE_OPEN IDENT LPAREN record_field_list RPAREN NEWLINE  */
#line 1467 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_database((yyvsp[-4].text), (yyvsp[-2].record_field_list), (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 5092 "src/parser.tab.c"
    break;

  case 150: /* gsql_block: DATABASE_OPEN IDENT LPAREN RPAREN NEWLINE  */
#line 1470 "src/parser.y"
                                                {
        (yyval.stmt) = ast_database((yyvsp[-3].text), ast_record_field_list_empty(), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5100 "src/parser.tab.c"
    break;

  case 151: /* gsql_block: SQL_OPEN IDENT ON IDENT NEWLINE SQL_TEXT END IDENT NEWLINE  */
#line 1473 "src/parser.y"
                                                                 {
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_sql((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].text), (yylsp[-3]).first_line, (yylsp[-3]).first_column, (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5109 "src/parser.tab.c"
    break;

  case 152: /* gsql_block: SQL_OPEN IDENT ON IDENT DOT IDENT NEWLINE SQL_TEXT END IDENT NEWLINE  */
#line 1479 "src/parser.y"
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
#line 5126 "src/parser.tab.c"
    break;

  case 153: /* window_block: WINDOW_OPEN IDENT LPAREN record_field_list RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1494 "src/parser.y"
                                                                                                   {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), NULL, NULL, NULL, (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5135 "src/parser.tab.c"
    break;

  case 154: /* window_block: WINDOW_OPEN IDENT LPAREN RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1498 "src/parser.y"
                                                                                 {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text), NULL, NULL, NULL,
                        (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5145 "src/parser.tab.c"
    break;

  case 155: /* window_block: WINDOW_OPEN IDENT LPAREN record_field_list RPAREN TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1508 "src/parser.y"
                                                                                                                  {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-11].text), (yyvsp[-9].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text),
                        (yylsp[-12]).first_line, (yylsp[-12]).first_column);
      }
#line 5155 "src/parser.tab.c"
    break;

  case 156: /* window_item_list: %empty  */
#line 1516 "src/parser.y"
             { (yyval.window_item_list) = ast_window_item_list_empty(); }
#line 5161 "src/parser.tab.c"
    break;

  case 157: /* window_item_list: window_item_list NEWLINE  */
#line 1517 "src/parser.y"
                               { (yyval.window_item_list) = (yyvsp[-1].window_item_list); }
#line 5167 "src/parser.tab.c"
    break;

  case 158: /* window_item_list: window_item_list window_item  */
#line 1518 "src/parser.y"
                                   { (yyval.window_item_list) = ast_window_item_list_append((yyvsp[-1].window_item_list), (yyvsp[0].window_item)); }
#line 5173 "src/parser.tab.c"
    break;

  case 159: /* window_item: IDENT LPAREN RPAREN NEWLINE  */
#line 1523 "src/parser.y"
                                  {
        (yyval.window_item) = ast_window_leaf((yyvsp[-3].text), NULL, ast_record_field_list_empty(),
                             (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 5182 "src/parser.tab.c"
    break;

  case 160: /* window_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE  */
#line 1528 "src/parser.y"
                                                          {
        (yyval.window_item) = ast_window_leaf((yyvsp[-5].text), (yyvsp[-4].text), (yyvsp[-2].record_field_list), (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 5190 "src/parser.tab.c"
    break;

  case 161: /* window_item: IDENT IDENT LPAREN RPAREN NEWLINE  */
#line 1532 "src/parser.y"
                                        {
        (yyval.window_item) = ast_window_leaf((yyvsp[-4].text), (yyvsp[-3].text), ast_record_field_list_empty(),
                             (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5199 "src/parser.tab.c"
    break;

  case 162: /* window_item: IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1537 "src/parser.y"
                                                       {
        (yyval.window_item) = ast_window_container((yyvsp[-5].text), NULL, ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                  (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 5208 "src/parser.tab.c"
    break;

  case 163: /* window_item: IDENT LPAREN record_field_list RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1542 "src/parser.y"
                                                                                       {
        (yyval.window_item) = ast_window_container((yyvsp[-8].text), NULL, (yyvsp[-6].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5216 "src/parser.tab.c"
    break;

  case 164: /* window_item: IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1556 "src/parser.y"
                                                             {
        (yyval.window_item) = ast_window_named_form((yyvsp[-6].text), (yyvsp[-5].text), NULL, NULL, NULL, (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                   (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5225 "src/parser.tab.c"
    break;

  case 165: /* window_item: IDENT IDENT STRING NEWLINE window_item_list END IDENT NEWLINE  */
#line 1571 "src/parser.y"
                                                                    {
        (yyval.window_item) = ast_window_named_container((yyvsp[-7].text), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                        (yylsp[-7]).first_line, (yylsp[-7]).first_column);
      }
#line 5234 "src/parser.tab.c"
    break;

  case 166: /* window_item: IDENT STRING NEWLINE window_item_list END IDENT NEWLINE  */
#line 1576 "src/parser.y"
                                                              {
        (yyval.window_item) = ast_window_container((yyvsp[-6].text), (yyvsp[-5].text), ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                  (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5243 "src/parser.tab.c"
    break;

  case 167: /* window_item: IDENT TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1587 "src/parser.y"
                                                                      {
        (yyval.window_item) = ast_window_form((yyvsp[-8].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                             (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5252 "src/parser.tab.c"
    break;

  case 168: /* window_item: IDENT IDENT TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1602 "src/parser.y"
                                                                            {
        (yyval.window_item) = ast_window_named_form((yyvsp[-9].text), (yyvsp[-8].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                   (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5261 "src/parser.tab.c"
    break;

  case 169: /* window_item: ON IDENT DOT IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1607 "src/parser.y"
                                                               {
        (yyval.window_item) = ast_window_handler((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5269 "src/parser.tab.c"
    break;

  case 170: /* server_item_list: %empty  */
#line 1613 "src/parser.y"
             { (yyval.server_item_list) = ast_server_item_list_empty(); }
#line 5275 "src/parser.tab.c"
    break;

  case 171: /* server_item_list: server_item_list NEWLINE  */
#line 1614 "src/parser.y"
                               { (yyval.server_item_list) = (yyvsp[-1].server_item_list); }
#line 5281 "src/parser.tab.c"
    break;

  case 172: /* server_item_list: server_item_list server_item  */
#line 1615 "src/parser.y"
                                   { (yyval.server_item_list) = ast_server_item_list_append((yyvsp[-1].server_item_list), (yyvsp[0].server_item)); }
#line 5287 "src/parser.tab.c"
    break;

  case 173: /* server_item: IDENT server_string_list NEWLINE  */
#line 1619 "src/parser.y"
                                       {
        (yyval.server_item) = ast_server_directive((yyvsp[-2].text), (yyvsp[-1].name_list), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 5295 "src/parser.tab.c"
    break;

  case 174: /* server_item: IDENT STRING LPAREN parameter_list_opt RPAREN NEWLINE statement_list END IDENT NEWLINE  */
#line 1622 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_handler((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5303 "src/parser.tab.c"
    break;

  case 175: /* server_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1625 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_site((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5311 "src/parser.tab.c"
    break;

  case 176: /* server_item: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1628 "src/parser.y"
                                                                           {
        (yyval.server_item) = ast_server_site((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5319 "src/parser.tab.c"
    break;

  case 177: /* server_item: ON IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1631 "src/parser.y"
                                                     {
        (yyval.server_item) = ast_server_hook((yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5327 "src/parser.tab.c"
    break;

  case 178: /* server_string_list: STRING  */
#line 1637 "src/parser.y"
             { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5333 "src/parser.tab.c"
    break;

  case 179: /* server_string_list: server_string_list COMMA STRING  */
#line 1638 "src/parser.y"
                                      { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5339 "src/parser.tab.c"
    break;

  case 180: /* watch_target_path: variable_name  */
#line 1642 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 5345 "src/parser.tab.c"
    break;

  case 181: /* watch_target_path: watch_target_path DOT IDENT  */
#line 1643 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 5351 "src/parser.tab.c"
    break;

  case 182: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 1647 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 5359 "src/parser.tab.c"
    break;

  case 183: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 1653 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 5365 "src/parser.tab.c"
    break;

  case 184: /* on_error_statement: ON ERROR_VALUE GOTO NEXT  */
#line 1654 "src/parser.y"
                               { (yyval.stmt) = ast_on_error_goto_next(); }
#line 5371 "src/parser.tab.c"
    break;

  case 185: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 1655 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 5377 "src/parser.tab.c"
    break;

  case 186: /* on_error_statement: ON IDENT GOTO NEXT  */
#line 1656 "src/parser.y"
                         {
        if (!warn_channel_ok(ctx, (yyvsp[-2].text), (yylsp[-2]).first_line, (yylsp[-2]).first_column)) { YYERROR; }
        free((yyvsp[-2].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_NEXT);
      }
#line 5387 "src/parser.tab.c"
    break;

  case 187: /* on_error_statement: ON IDENT GOTO IDENT  */
#line 1661 "src/parser.y"
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
#line 5405 "src/parser.tab.c"
    break;

  case 188: /* on_error_statement: ON IDENT STOP  */
#line 1674 "src/parser.y"
                    {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_STOP);
      }
#line 5415 "src/parser.tab.c"
    break;

  case 189: /* on_error_statement: ON IDENT PRINT  */
#line 1679 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_PRINT);
      }
#line 5425 "src/parser.tab.c"
    break;

  case 190: /* on_error_statement: ON IDENT IDENT  */
#line 1684 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        int mode = warn_mode_word(ctx, (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column);
        if (mode < 0) { free((yyvsp[-1].text)); free((yyvsp[0].text)); YYERROR; }
        free((yyvsp[-1].text)); free((yyvsp[0].text));
        (yyval.stmt) = ast_on_warning(mode);
      }
#line 5437 "src/parser.tab.c"
    break;

  case 191: /* error_statement: ERROR_VALUE expression  */
#line 1694 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 5443 "src/parser.tab.c"
    break;

  case 192: /* return_statement: RETURN  */
#line 1698 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 5449 "src/parser.tab.c"
    break;

  case 193: /* return_statement: RETURN expression  */
#line 1699 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 5455 "src/parser.tab.c"
    break;

  case 194: /* label_statement: variable_name COLON  */
#line 1703 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 5461 "src/parser.tab.c"
    break;

  case 195: /* goto_statement: GOTO variable_name  */
#line 1710 "src/parser.y"
                         { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 5467 "src/parser.tab.c"
    break;

  case 196: /* gosub_statement: GOSUB variable_name  */
#line 1714 "src/parser.y"
                          { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 5473 "src/parser.tab.c"
    break;

  case 197: /* break_statement: BREAK  */
#line 1723 "src/parser.y"
            { (yyval.stmt) = ast_break(NULL); }
#line 5479 "src/parser.tab.c"
    break;

  case 198: /* break_statement: BREAK IDENT  */
#line 1724 "src/parser.y"
                  { (yyval.stmt) = ast_break((yyvsp[0].text)); }
#line 5485 "src/parser.tab.c"
    break;

  case 199: /* continue_statement: CONTINUE  */
#line 1728 "src/parser.y"
               { (yyval.stmt) = ast_continue(NULL); }
#line 5491 "src/parser.tab.c"
    break;

  case 200: /* continue_statement: CONTINUE IDENT  */
#line 1729 "src/parser.y"
                     { (yyval.stmt) = ast_continue((yyvsp[0].text)); }
#line 5497 "src/parser.tab.c"
    break;

  case 201: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1733 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 5506 "src/parser.tab.c"
    break;

  case 202: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1737 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 5515 "src/parser.tab.c"
    break;

  case 203: /* if_block_tail: END IF NEWLINE  */
#line 1744 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 5523 "src/parser.tab.c"
    break;

  case 204: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 1747 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 5531 "src/parser.tab.c"
    break;

  case 205: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1750 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 5539 "src/parser.tab.c"
    break;

  case 206: /* if_block_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1759 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5551 "src/parser.tab.c"
    break;

  case 207: /* if_inline_tail: %empty  */
#line 1769 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 5559 "src/parser.tab.c"
    break;

  case 208: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 1772 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 5567 "src/parser.tab.c"
    break;

  case 209: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1775 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 5575 "src/parser.tab.c"
    break;

  case 210: /* if_inline_tail: ELSE IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1781 "src/parser.y"
                                                                      {
        AstStmt *inner = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5587 "src/parser.tab.c"
    break;

  case 211: /* if_inline_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1788 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5599 "src/parser.tab.c"
    break;

  case 212: /* inline_statement: assignment  */
#line 1798 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5605 "src/parser.tab.c"
    break;

  case 213: /* inline_statement: print_statement  */
#line 1799 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5611 "src/parser.tab.c"
    break;

  case 214: /* inline_statement: call_statement  */
#line 1800 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5617 "src/parser.tab.c"
    break;

  case 215: /* inline_statement: use_statement  */
#line 1801 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5623 "src/parser.tab.c"
    break;

  case 216: /* inline_statement: on_error_statement  */
#line 1802 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5629 "src/parser.tab.c"
    break;

  case 217: /* inline_statement: error_statement  */
#line 1803 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5635 "src/parser.tab.c"
    break;

  case 218: /* inline_statement: return_statement  */
#line 1804 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5641 "src/parser.tab.c"
    break;

  case 219: /* inline_statement: goto_statement  */
#line 1805 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5647 "src/parser.tab.c"
    break;

  case 220: /* inline_statement: gosub_statement  */
#line 1806 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5653 "src/parser.tab.c"
    break;

  case 221: /* inline_statement: break_statement  */
#line 1807 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5659 "src/parser.tab.c"
    break;

  case 222: /* inline_statement: continue_statement  */
#line 1808 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5665 "src/parser.tab.c"
    break;

  case 223: /* expression: or_expression  */
#line 1816 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 5671 "src/parser.tab.c"
    break;

  case 224: /* expression: TRY expression  */
#line 1817 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_unary(copy_const("try"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5677 "src/parser.tab.c"
    break;

  case 225: /* or_expression: and_expression  */
#line 1821 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5683 "src/parser.tab.c"
    break;

  case 226: /* or_expression: or_expression OR and_expression  */
#line 1822 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5689 "src/parser.tab.c"
    break;

  case 227: /* and_expression: not_expression  */
#line 1826 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5695 "src/parser.tab.c"
    break;

  case 228: /* and_expression: and_expression AND not_expression  */
#line 1827 "src/parser.y"
                                        { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5701 "src/parser.tab.c"
    break;

  case 229: /* not_expression: comparison_expression  */
#line 1849 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 5707 "src/parser.tab.c"
    break;

  case 230: /* not_expression: NOT not_expression  */
#line 1850 "src/parser.y"
                         { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5713 "src/parser.tab.c"
    break;

  case 231: /* comparison_expression: set_expression  */
#line 1854 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5719 "src/parser.tab.c"
    break;

  case 232: /* comparison_expression: set_expression comparison_operator set_expression  */
#line 1855 "src/parser.y"
                                                        { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5725 "src/parser.tab.c"
    break;

  case 233: /* comparison_expression: set_expression comparison_lens comparison_operator set_expression  */
#line 1856 "src/parser.y"
                                                                        {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 5733 "src/parser.tab.c"
    break;

  case 234: /* set_expression: additive_expression  */
#line 1862 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 5739 "src/parser.tab.c"
    break;

  case 235: /* set_expression: set_expression EXCLUDING additive_expression  */
#line 1863 "src/parser.y"
                                                   { (yyval.expr) = expr_at(ast_binary(copy_const("excluding"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5745 "src/parser.tab.c"
    break;

  case 236: /* set_expression: set_expression INTERSECTING additive_expression  */
#line 1864 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("intersecting"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5751 "src/parser.tab.c"
    break;

  case 237: /* additive_expression: multiplicative_expression  */
#line 1868 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 5757 "src/parser.tab.c"
    break;

  case 238: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 1869 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5763 "src/parser.tab.c"
    break;

  case 239: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 1870 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5769 "src/parser.tab.c"
    break;

  case 240: /* multiplicative_expression: unary_expression  */
#line 1874 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 5775 "src/parser.tab.c"
    break;

  case 241: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 1875 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5781 "src/parser.tab.c"
    break;

  case 242: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 1876 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5787 "src/parser.tab.c"
    break;

  case 243: /* unary_expression: postfix_expression  */
#line 1880 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 5793 "src/parser.tab.c"
    break;

  case 244: /* unary_expression: MINUS unary_expression  */
#line 1881 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5799 "src/parser.tab.c"
    break;

  case 245: /* unary_expression: MODIFIER_PREFIX unary_expression  */
#line 1888 "src/parser.y"
                                       {
        (yyval.expr) = expr_at(ast_modifier_apply(parse_modifier_use((yyvsp[-1].text)), (yyvsp[0].expr)),
                     (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 5808 "src/parser.tab.c"
    break;

  case 246: /* unary_expression: NEW postfix_expression  */
#line 1892 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5814 "src/parser.tab.c"
    break;

  case 247: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 1893 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 5820 "src/parser.tab.c"
    break;

  case 248: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 1894 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5826 "src/parser.tab.c"
    break;

  case 249: /* postfix_expression: primary  */
#line 1898 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 5832 "src/parser.tab.c"
    break;

  case 250: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 1899 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5838 "src/parser.tab.c"
    break;

  case 251: /* postfix_expression: postfix_expression DOT dot_field_name  */
#line 1900 "src/parser.y"
                                            { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5844 "src/parser.tab.c"
    break;

  case 252: /* postfix_expression: postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1901 "src/parser.y"
                                                                   {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        (yyval.expr) = expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5854 "src/parser.tab.c"
    break;

  case 253: /* postfix_expression: postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1906 "src/parser.y"
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
#line 5870 "src/parser.tab.c"
    break;

  case 254: /* comparison_operator: OP_EQ  */
#line 1920 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 5876 "src/parser.tab.c"
    break;

  case 255: /* comparison_operator: OP_NE  */
#line 1921 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 5882 "src/parser.tab.c"
    break;

  case 256: /* comparison_operator: OP_GT  */
#line 1922 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 5888 "src/parser.tab.c"
    break;

  case 257: /* comparison_operator: OP_LT  */
#line 1923 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 5894 "src/parser.tab.c"
    break;

  case 258: /* comparison_operator: OP_GE  */
#line 1924 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 5900 "src/parser.tab.c"
    break;

  case 259: /* comparison_operator: OP_LE  */
#line 1925 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 5906 "src/parser.tab.c"
    break;

  case 260: /* comparison_operator: OP_NGT  */
#line 1926 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 5912 "src/parser.tab.c"
    break;

  case 261: /* comparison_operator: OP_NLT  */
#line 1927 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 5918 "src/parser.tab.c"
    break;

  case 262: /* comparison_operator: OP_NGE  */
#line 1928 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 5924 "src/parser.tab.c"
    break;

  case 263: /* comparison_operator: OP_NLE  */
#line 1929 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 5930 "src/parser.tab.c"
    break;

  case 264: /* primary: MODIFIER_VALUE  */
#line 1933 "src/parser.y"
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
#line 5953 "src/parser.tab.c"
    break;

  case 265: /* primary: NUMBER  */
#line 1951 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5959 "src/parser.tab.c"
    break;

  case 266: /* primary: WATCHERS LPAREN RPAREN  */
#line 1952 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_call(copy_const("watchers"), ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5965 "src/parser.tab.c"
    break;

  case 267: /* primary: duration_terms  */
#line 1953 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5971 "src/parser.tab.c"
    break;

  case 268: /* primary: STRING  */
#line 1954 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5977 "src/parser.tab.c"
    break;

  case 269: /* primary: variable_name ident_suffix  */
#line 1955 "src/parser.y"
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
#line 5999 "src/parser.tab.c"
    break;

  case 270: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1972 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 6010 "src/parser.tab.c"
    break;

  case 271: /* primary: ERROR_VALUE  */
#line 1978 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6016 "src/parser.tab.c"
    break;

  case 272: /* primary: TRUE  */
#line 1979 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6022 "src/parser.tab.c"
    break;

  case 273: /* primary: FALSE  */
#line 1980 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6028 "src/parser.tab.c"
    break;

  case 274: /* primary: NOTHING  */
#line 1981 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6034 "src/parser.tab.c"
    break;

  case 275: /* primary: UNKNOWN_VALUE  */
#line 1982 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6040 "src/parser.tab.c"
    break;

  case 276: /* primary: LPAREN expression RPAREN  */
#line 1983 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 6046 "src/parser.tab.c"
    break;

  case 277: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 1984 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 6052 "src/parser.tab.c"
    break;

  case 278: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 1985 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 6058 "src/parser.tab.c"
    break;

  case 279: /* primary: record_literal  */
#line 1986 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 6064 "src/parser.tab.c"
    break;

  case 280: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 1990 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 6070 "src/parser.tab.c"
    break;

  case 281: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 1991 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 6076 "src/parser.tab.c"
    break;

  case 282: /* ident_suffix: %empty  */
#line 1995 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 6086 "src/parser.tab.c"
    break;

  case 283: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 2000 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 6096 "src/parser.tab.c"
    break;

  case 284: /* ident_suffix: DOT dot_field_name ident_dot_suffix  */
#line 2005 "src/parser.y"
                                          {
        /* dot_field_name, not IDENT: a keyword is a legal FIELD name after a
         * dot, because nothing but a name can appear there. */
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 6107 "src/parser.tab.c"
    break;

  case 285: /* ident_suffix: DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 2011 "src/parser.y"
                                                          {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        (yyval.ident_suffix).kind = IDENT_SUFFIX_METHOD;
        (yyval.ident_suffix).name = (yyvsp[-3].text);
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 6121 "src/parser.tab.c"
    break;

  case 286: /* ident_dot_suffix: %empty  */
#line 2023 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 6131 "src/parser.tab.c"
    break;

  case 287: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 2028 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 6141 "src/parser.tab.c"
    break;

  case 288: /* duration_terms: NUMBER IDENT  */
#line 2036 "src/parser.y"
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
#line 6156 "src/parser.tab.c"
    break;

  case 289: /* duration_terms: duration_terms NUMBER IDENT  */
#line 2046 "src/parser.y"
                                  {
        char *bad = NULL;
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].numlit).value, (yyvsp[0].text), &bad);
        if (bad) {
            duration_unit_error(ctx, bad, (yylsp[0]).first_line, (yylsp[0]).first_column,
                                (yylsp[0]).last_line, (yylsp[0]).last_column);
            YYERROR;
        }
      }
#line 6170 "src/parser.tab.c"
    break;

  case 290: /* argument_list_opt: %empty  */
#line 2058 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 6176 "src/parser.tab.c"
    break;

  case 291: /* argument_list_opt: argument_list  */
#line 2059 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 6182 "src/parser.tab.c"
    break;

  case 292: /* argument_list: expression  */
#line 2063 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 6188 "src/parser.tab.c"
    break;

  case 293: /* argument_list: argument_list COMMA expression  */
#line 2064 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 6194 "src/parser.tab.c"
    break;

  case 294: /* array_argument_list: expression  */
#line 2068 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 6200 "src/parser.tab.c"
    break;

  case 295: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 2069 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 6206 "src/parser.tab.c"
    break;

  case 296: /* parameter_list_opt: %empty  */
#line 2073 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 6212 "src/parser.tab.c"
    break;

  case 297: /* parameter_list_opt: parameter_list  */
#line 2074 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 6218 "src/parser.tab.c"
    break;

  case 298: /* parameter_default: NUMBER  */
#line 2088 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6224 "src/parser.tab.c"
    break;

  case 299: /* parameter_default: MINUS NUMBER  */
#line 2092 "src/parser.y"
                   { (yyval.expr) = expr_at(ast_number(-(yyvsp[0].numlit).value,
                                             (yyvsp[0].numlit).exact == LLONG_MIN ? 0 : -(yyvsp[0].numlit).exact,
                                             (yyvsp[0].numlit).exact == LLONG_MIN ? 0 : (yyvsp[0].numlit).is_exact),
                                  (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6233 "src/parser.tab.c"
    break;

  case 300: /* parameter_default: PLUS NUMBER  */
#line 2096 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6239 "src/parser.tab.c"
    break;

  case 301: /* parameter_default: STRING  */
#line 2097 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6245 "src/parser.tab.c"
    break;

  case 302: /* parameter_default: TRUE  */
#line 2098 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6251 "src/parser.tab.c"
    break;

  case 303: /* parameter_default: FALSE  */
#line 2099 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6257 "src/parser.tab.c"
    break;

  case 304: /* parameter_default: NOTHING  */
#line 2100 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6263 "src/parser.tab.c"
    break;

  case 305: /* parameter_default: UNKNOWN_VALUE  */
#line 2101 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6269 "src/parser.tab.c"
    break;

  case 306: /* parameter_list: IDENT  */
#line 2105 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 6275 "src/parser.tab.c"
    break;

  case 307: /* parameter_list: IDENT OP_EQ parameter_default  */
#line 2106 "src/parser.y"
                                    {
        (yyval.name_list) = ast_name_list_append_default(ast_name_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 6283 "src/parser.tab.c"
    break;

  case 308: /* parameter_list: parameter_list COMMA IDENT  */
#line 2109 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 6289 "src/parser.tab.c"
    break;

  case 309: /* parameter_list: IDENT COLON type_lock  */
#line 2113 "src/parser.y"
                            {
        if (!type_lock_check(ctx, (yyvsp[-2].text), (yyvsp[0].type_lock).name, (yyvsp[0].type_lock).arg, (yylsp[0]).first_line, (yylsp[0]).first_column,
                             (yylsp[0]).last_line, (yylsp[0]).last_column)) {
            YYERROR;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append(ast_name_list_empty(), (yyvsp[-2].text)), (yyvsp[0].type_lock));
      }
#line 6301 "src/parser.tab.c"
    break;

  case 310: /* parameter_list: IDENT COLON type_lock OP_EQ parameter_default  */
#line 2120 "src/parser.y"
                                                    {
        if (!type_lock_check(ctx, (yyvsp[-4].text), (yyvsp[-2].type_lock).name, (yyvsp[-2].type_lock).arg, (yylsp[-2]).first_line, (yylsp[-2]).first_column,
                             (yylsp[-2]).last_line, (yylsp[-2]).last_column)) {
            YYERROR;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append_default(ast_name_list_empty(), (yyvsp[-4].text), (yyvsp[0].expr)), (yyvsp[-2].type_lock));
      }
#line 6313 "src/parser.tab.c"
    break;

  case 311: /* parameter_list: parameter_list COMMA IDENT COLON type_lock  */
#line 2127 "src/parser.y"
                                                 {
        if (!type_lock_check(ctx, (yyvsp[-2].text), (yyvsp[0].type_lock).name, (yyvsp[0].type_lock).arg, (yylsp[0]).first_line, (yylsp[0]).first_column,
                             (yylsp[0]).last_line, (yylsp[0]).last_column)) {
            YYERROR;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append((yyvsp[-4].name_list), (yyvsp[-2].text)), (yyvsp[0].type_lock));
      }
#line 6325 "src/parser.tab.c"
    break;

  case 312: /* parameter_list: parameter_list COMMA IDENT COLON type_lock OP_EQ parameter_default  */
#line 2134 "src/parser.y"
                                                                         {
        if (!type_lock_check(ctx, (yyvsp[-4].text), (yyvsp[-2].type_lock).name, (yyvsp[-2].type_lock).arg, (yylsp[-2]).first_line, (yylsp[-2]).first_column,
                             (yylsp[-2]).last_line, (yylsp[-2]).last_column)) {
            YYERROR;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append_default((yyvsp[-6].name_list), (yyvsp[-4].text), (yyvsp[0].expr)), (yyvsp[-2].type_lock));
      }
#line 6337 "src/parser.tab.c"
    break;

  case 313: /* parameter_list: parameter_list COMMA IDENT OP_EQ parameter_default  */
#line 2141 "src/parser.y"
                                                         {
        (yyval.name_list) = ast_name_list_append_default((yyvsp[-4].name_list), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 6345 "src/parser.tab.c"
    break;

  case 314: /* field_name: dot_field_name  */
#line 2156 "src/parser.y"
                     { (yyval.text) = (yyvsp[0].text); }
#line 6351 "src/parser.tab.c"
    break;

  case 315: /* field_name: STRING  */
#line 2163 "src/parser.y"
             { (yyval.text) = (yyvsp[0].text); }
#line 6357 "src/parser.tab.c"
    break;

  case 316: /* dot_field_name: IDENT  */
#line 2172 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 6363 "src/parser.tab.c"
    break;

  case 317: /* dot_field_name: AS  */
#line 2173 "src/parser.y"
                     { (yyval.text) = kw_name("as"); }
#line 6369 "src/parser.tab.c"
    break;

  case 318: /* dot_field_name: NEXT  */
#line 2174 "src/parser.y"
                     { (yyval.text) = kw_name("next"); }
#line 6375 "src/parser.tab.c"
    break;

  case 319: /* dot_field_name: STOP  */
#line 2175 "src/parser.y"
                     { (yyval.text) = kw_name("stop"); }
#line 6381 "src/parser.tab.c"
    break;

  case 320: /* dot_field_name: ERROR_VALUE  */
#line 2176 "src/parser.y"
                     { (yyval.text) = kw_name("error"); }
#line 6387 "src/parser.tab.c"
    break;

  case 321: /* dot_field_name: END  */
#line 2177 "src/parser.y"
                     { (yyval.text) = kw_name("end"); }
#line 6393 "src/parser.tab.c"
    break;

  case 322: /* dot_field_name: TO  */
#line 2178 "src/parser.y"
                     { (yyval.text) = kw_name("to"); }
#line 6399 "src/parser.tab.c"
    break;

  case 323: /* dot_field_name: IN  */
#line 2179 "src/parser.y"
                     { (yyval.text) = kw_name("in"); }
#line 6405 "src/parser.tab.c"
    break;

  case 324: /* dot_field_name: ON  */
#line 2180 "src/parser.y"
                     { (yyval.text) = kw_name("on"); }
#line 6411 "src/parser.tab.c"
    break;

  case 325: /* dot_field_name: NEW  */
#line 2181 "src/parser.y"
                     { (yyval.text) = kw_name("new"); }
#line 6417 "src/parser.tab.c"
    break;

  case 326: /* dot_field_name: EACH  */
#line 2182 "src/parser.y"
                     { (yyval.text) = kw_name("each"); }
#line 6423 "src/parser.tab.c"
    break;

  case 327: /* dot_field_name: WITH  */
#line 2183 "src/parser.y"
                     { (yyval.text) = kw_name("with"); }
#line 6429 "src/parser.tab.c"
    break;

  case 328: /* dot_field_name: WITHOUT  */
#line 2184 "src/parser.y"
                     { (yyval.text) = kw_name("without"); }
#line 6435 "src/parser.tab.c"
    break;

  case 329: /* dot_field_name: THEN  */
#line 2185 "src/parser.y"
                     { (yyval.text) = kw_name("then"); }
#line 6441 "src/parser.tab.c"
    break;

  case 330: /* dot_field_name: ELSE  */
#line 2186 "src/parser.y"
                     { (yyval.text) = kw_name("else"); }
#line 6447 "src/parser.tab.c"
    break;

  case 331: /* dot_field_name: FOR  */
#line 2187 "src/parser.y"
                     { (yyval.text) = kw_name("for"); }
#line 6453 "src/parser.tab.c"
    break;

  case 332: /* dot_field_name: IF  */
#line 2188 "src/parser.y"
                     { (yyval.text) = kw_name("if"); }
#line 6459 "src/parser.tab.c"
    break;

  case 333: /* dot_field_name: WHILE  */
#line 2189 "src/parser.y"
                     { (yyval.text) = kw_name("while"); }
#line 6465 "src/parser.tab.c"
    break;

  case 334: /* dot_field_name: DO  */
#line 2190 "src/parser.y"
                     { (yyval.text) = kw_name("do"); }
#line 6471 "src/parser.tab.c"
    break;

  case 335: /* dot_field_name: UNTIL  */
#line 2191 "src/parser.y"
                     { (yyval.text) = kw_name("until"); }
#line 6477 "src/parser.tab.c"
    break;

  case 336: /* dot_field_name: PRINT  */
#line 2192 "src/parser.y"
                     { (yyval.text) = kw_name("print"); }
#line 6483 "src/parser.tab.c"
    break;

  case 337: /* dot_field_name: RETURN  */
#line 2193 "src/parser.y"
                     { (yyval.text) = kw_name("return"); }
#line 6489 "src/parser.tab.c"
    break;

  case 338: /* dot_field_name: LOAD  */
#line 2194 "src/parser.y"
                     { (yyval.text) = kw_name("load"); }
#line 6495 "src/parser.tab.c"
    break;

  case 339: /* dot_field_name: USE  */
#line 2195 "src/parser.y"
                     { (yyval.text) = kw_name("use"); }
#line 6501 "src/parser.tab.c"
    break;

  case 340: /* dot_field_name: NOT  */
#line 2196 "src/parser.y"
                     { (yyval.text) = kw_name("not"); }
#line 6507 "src/parser.tab.c"
    break;

  case 341: /* dot_field_name: TRY  */
#line 2197 "src/parser.y"
                     { (yyval.text) = kw_name("try"); }
#line 6513 "src/parser.tab.c"
    break;

  case 342: /* dot_field_name: AND  */
#line 2198 "src/parser.y"
                     { (yyval.text) = kw_name("and"); }
#line 6519 "src/parser.tab.c"
    break;

  case 343: /* dot_field_name: OR  */
#line 2199 "src/parser.y"
                     { (yyval.text) = kw_name("or"); }
#line 6525 "src/parser.tab.c"
    break;

  case 344: /* dot_field_name: TRUE  */
#line 2200 "src/parser.y"
                     { (yyval.text) = kw_name("true"); }
#line 6531 "src/parser.tab.c"
    break;

  case 345: /* dot_field_name: FALSE  */
#line 2201 "src/parser.y"
                     { (yyval.text) = kw_name("false"); }
#line 6537 "src/parser.tab.c"
    break;

  case 346: /* dot_field_name: NOTHING  */
#line 2202 "src/parser.y"
                     { (yyval.text) = kw_name("nothing"); }
#line 6543 "src/parser.tab.c"
    break;

  case 347: /* dot_field_name: BREAK  */
#line 2203 "src/parser.y"
                     { (yyval.text) = kw_name("break"); }
#line 6549 "src/parser.tab.c"
    break;

  case 348: /* dot_field_name: CONTINUE  */
#line 2204 "src/parser.y"
                     { (yyval.text) = kw_name("continue"); }
#line 6555 "src/parser.tab.c"
    break;

  case 349: /* dot_field_name: GOTO  */
#line 2205 "src/parser.y"
                     { (yyval.text) = kw_name("goto"); }
#line 6561 "src/parser.tab.c"
    break;

  case 350: /* dot_field_name: GOSUB  */
#line 2206 "src/parser.y"
                     { (yyval.text) = kw_name("gosub"); }
#line 6567 "src/parser.tab.c"
    break;

  case 351: /* dot_field_name: SPAWN  */
#line 2207 "src/parser.y"
                     { (yyval.text) = kw_name("spawn"); }
#line 6573 "src/parser.tab.c"
    break;

  case 352: /* dot_field_name: EXPORT  */
#line 2208 "src/parser.y"
                     { (yyval.text) = kw_name("export"); }
#line 6579 "src/parser.tab.c"
    break;

  case 353: /* dot_field_name: LIBRARY  */
#line 2209 "src/parser.y"
                     { (yyval.text) = kw_name("library"); }
#line 6585 "src/parser.tab.c"
    break;

  case 354: /* dot_field_name: FUNCTION  */
#line 2210 "src/parser.y"
                     { (yyval.text) = kw_name("function"); }
#line 6591 "src/parser.tab.c"
    break;

  case 355: /* dot_field_name: MODIFIER  */
#line 2211 "src/parser.y"
                     { (yyval.text) = kw_name("modifier"); }
#line 6597 "src/parser.tab.c"
    break;

  case 356: /* dot_field_name: PROGRAM  */
#line 2212 "src/parser.y"
                     { (yyval.text) = kw_name("program"); }
#line 6603 "src/parser.tab.c"
    break;

  case 357: /* dot_field_name: WATCH  */
#line 2213 "src/parser.y"
                     { (yyval.text) = kw_name("watch"); }
#line 6609 "src/parser.tab.c"
    break;

  case 358: /* dot_field_name: WATCHERS  */
#line 2214 "src/parser.y"
                     { (yyval.text) = kw_name("watchers"); }
#line 6615 "src/parser.tab.c"
    break;

  case 359: /* dot_field_name: CONSIDER  */
#line 2215 "src/parser.y"
                     { (yyval.text) = kw_name("consider"); }
#line 6621 "src/parser.tab.c"
    break;

  case 360: /* dot_field_name: STEP  */
#line 2216 "src/parser.y"
                     { (yyval.text) = kw_name("step"); }
#line 6627 "src/parser.tab.c"
    break;

  case 361: /* dot_field_name: UNWATCH  */
#line 2217 "src/parser.y"
                     { (yyval.text) = kw_name("unwatch"); }
#line 6633 "src/parser.tab.c"
    break;

  case 362: /* dot_field_name: UNKNOWN_VALUE  */
#line 2218 "src/parser.y"
                     { (yyval.text) = kw_name("unknown"); }
#line 6639 "src/parser.tab.c"
    break;

  case 363: /* dot_field_name: DIM  */
#line 2219 "src/parser.y"
                     { (yyval.text) = kw_name("dim"); }
#line 6645 "src/parser.tab.c"
    break;

  case 364: /* record_field_list: field_name OP_EQ expression  */
#line 2223 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6651 "src/parser.tab.c"
    break;

  case 365: /* record_field_list: field_name COLON expression  */
#line 2224 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6657 "src/parser.tab.c"
    break;

  case 366: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 2225 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 6663 "src/parser.tab.c"
    break;

  case 367: /* record_field_list: record_field_list COMMA optional_newlines field_name OP_EQ expression  */
#line 2226 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6669 "src/parser.tab.c"
    break;

  case 368: /* record_field_list: record_field_list COMMA optional_newlines field_name COLON expression  */
#line 2227 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6675 "src/parser.tab.c"
    break;

  case 369: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 2228 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 6681 "src/parser.tab.c"
    break;

  case 370: /* field_policy: IDENT  */
#line 2236 "src/parser.y"
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
#line 6713 "src/parser.tab.c"
    break;

  case 371: /* field_policy: IDENT expression  */
#line 2263 "src/parser.y"
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
#line 6734 "src/parser.tab.c"
    break;


#line 6738 "src/parser.tab.c"

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

#line 2286 "src/parser.y"


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
