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



#line 698 "src/parser.tab.c"

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
  YYSYMBOL_AS = 11,                        /* AS  */
  YYSYMBOL_DIM = 12,                       /* DIM  */
  YYSYMBOL_PLUS_EQ = 13,                   /* PLUS_EQ  */
  YYSYMBOL_MINUS_EQ = 14,                  /* MINUS_EQ  */
  YYSYMBOL_STAR_EQ = 15,                   /* STAR_EQ  */
  YYSYMBOL_SLASH_EQ = 16,                  /* SLASH_EQ  */
  YYSYMBOL_EXCLUDING = 17,                 /* EXCLUDING  */
  YYSYMBOL_INTERSECTING = 18,              /* INTERSECTING  */
  YYSYMBOL_IF = 19,                        /* IF  */
  YYSYMBOL_CONSIDER_IF = 20,               /* CONSIDER_IF  */
  YYSYMBOL_THEN = 21,                      /* THEN  */
  YYSYMBOL_ELSE = 22,                      /* ELSE  */
  YYSYMBOL_CONSIDER_ELSE = 23,             /* CONSIDER_ELSE  */
  YYSYMBOL_END = 24,                       /* END  */
  YYSYMBOL_END_CONSIDER = 25,              /* END_CONSIDER  */
  YYSYMBOL_PRINT = 26,                     /* PRINT  */
  YYSYMBOL_TRUE = 27,                      /* TRUE  */
  YYSYMBOL_FALSE = 28,                     /* FALSE  */
  YYSYMBOL_NOTHING = 29,                   /* NOTHING  */
  YYSYMBOL_UNKNOWN_VALUE = 30,             /* UNKNOWN_VALUE  */
  YYSYMBOL_AND = 31,                       /* AND  */
  YYSYMBOL_OR = 32,                        /* OR  */
  YYSYMBOL_NOT = 33,                       /* NOT  */
  YYSYMBOL_WITH = 34,                      /* WITH  */
  YYSYMBOL_NEW = 35,                       /* NEW  */
  YYSYMBOL_SPAWN = 36,                     /* SPAWN  */
  YYSYMBOL_FOR = 37,                       /* FOR  */
  YYSYMBOL_TO = 38,                        /* TO  */
  YYSYMBOL_STEP = 39,                      /* STEP  */
  YYSYMBOL_DO = 40,                        /* DO  */
  YYSYMBOL_UNTIL = 41,                     /* UNTIL  */
  YYSYMBOL_IN = 42,                        /* IN  */
  YYSYMBOL_EACH = 43,                      /* EACH  */
  YYSYMBOL_WHILE = 44,                     /* WHILE  */
  YYSYMBOL_CONSIDER = 45,                  /* CONSIDER  */
  YYSYMBOL_BREAK = 46,                     /* BREAK  */
  YYSYMBOL_CONTINUE = 47,                  /* CONTINUE  */
  YYSYMBOL_FUNCTION = 48,                  /* FUNCTION  */
  YYSYMBOL_RETURN = 49,                    /* RETURN  */
  YYSYMBOL_GOTO = 50,                      /* GOTO  */
  YYSYMBOL_GOSUB = 51,                     /* GOSUB  */
  YYSYMBOL_WATCH = 52,                     /* WATCH  */
  YYSYMBOL_UNWATCH = 53,                   /* UNWATCH  */
  YYSYMBOL_WITHOUT = 54,                   /* WITHOUT  */
  YYSYMBOL_WATCHERS = 55,                  /* WATCHERS  */
  YYSYMBOL_ON = 56,                        /* ON  */
  YYSYMBOL_NEXT = 57,                      /* NEXT  */
  YYSYMBOL_STOP = 58,                      /* STOP  */
  YYSYMBOL_ERROR_VALUE = 59,               /* ERROR_VALUE  */
  YYSYMBOL_MODIFIER = 60,                  /* MODIFIER  */
  YYSYMBOL_PROGRAM = 61,                   /* PROGRAM  */
  YYSYMBOL_LIBRARY = 62,                   /* LIBRARY  */
  YYSYMBOL_LOAD = 63,                      /* LOAD  */
  YYSYMBOL_USE = 64,                       /* USE  */
  YYSYMBOL_EXPORT = 65,                    /* EXPORT  */
  YYSYMBOL_OP_EQ = 66,                     /* OP_EQ  */
  YYSYMBOL_OP_NE = 67,                     /* OP_NE  */
  YYSYMBOL_OP_GT = 68,                     /* OP_GT  */
  YYSYMBOL_OP_LT = 69,                     /* OP_LT  */
  YYSYMBOL_OP_GE = 70,                     /* OP_GE  */
  YYSYMBOL_OP_LE = 71,                     /* OP_LE  */
  YYSYMBOL_OP_NGT = 72,                    /* OP_NGT  */
  YYSYMBOL_OP_NLT = 73,                    /* OP_NLT  */
  YYSYMBOL_OP_NGE = 74,                    /* OP_NGE  */
  YYSYMBOL_OP_NLE = 75,                    /* OP_NLE  */
  YYSYMBOL_PLUS = 76,                      /* PLUS  */
  YYSYMBOL_MINUS = 77,                     /* MINUS  */
  YYSYMBOL_STAR = 78,                      /* STAR  */
  YYSYMBOL_SLASH = 79,                     /* SLASH  */
  YYSYMBOL_LPAREN = 80,                    /* LPAREN  */
  YYSYMBOL_RPAREN = 81,                    /* RPAREN  */
  YYSYMBOL_LBRACKET = 82,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 83,                  /* RBRACKET  */
  YYSYMBOL_LBRACE = 84,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 85,                    /* RBRACE  */
  YYSYMBOL_COMMA = 86,                     /* COMMA  */
  YYSYMBOL_COLON = 87,                     /* COLON  */
  YYSYMBOL_NEWLINE = 88,                   /* NEWLINE  */
  YYSYMBOL_IF_WITHOUT_ELSE = 89,           /* IF_WITHOUT_ELSE  */
  YYSYMBOL_NO_DOT = 90,                    /* NO_DOT  */
  YYSYMBOL_DOT = 91,                       /* DOT  */
  YYSYMBOL_YYACCEPT = 92,                  /* $accept  */
  YYSYMBOL_program = 93,                   /* program  */
  YYSYMBOL_statement_list = 94,            /* statement_list  */
  YYSYMBOL_statement = 95,                 /* statement  */
  YYSYMBOL_assignment = 96,                /* assignment  */
  YYSYMBOL_compound_op = 97,               /* compound_op  */
  YYSYMBOL_lvalue = 98,                    /* lvalue  */
  YYSYMBOL_variable_name = 99,             /* variable_name  */
  YYSYMBOL_comparison_lens = 100,          /* comparison_lens  */
  YYSYMBOL_101_1 = 101,                    /* $@1  */
  YYSYMBOL_modifier_name = 102,            /* modifier_name  */
  YYSYMBOL_modifier_word = 103,            /* modifier_word  */
  YYSYMBOL_print_statement = 104,          /* print_statement  */
  YYSYMBOL_call_statement = 105,           /* call_statement  */
  YYSYMBOL_with_lock_statement = 106,      /* with_lock_statement  */
  YYSYMBOL_for_end = 107,                  /* for_end  */
  YYSYMBOL_for_each_statement = 108,       /* for_each_statement  */
  YYSYMBOL_do_loop_statement = 109,        /* do_loop_statement  */
  YYSYMBOL_while_statement = 110,          /* while_statement  */
  YYSYMBOL_consider_statement = 111,       /* consider_statement  */
  YYSYMBOL_consider_branch_list = 112,     /* consider_branch_list  */
  YYSYMBOL_consider_else_opt = 113,        /* consider_else_opt  */
  YYSYMBOL_consider_statement_list = 114,  /* consider_statement_list  */
  YYSYMBOL_consider_body_statement = 115,  /* consider_body_statement  */
  YYSYMBOL_function_statement = 116,       /* function_statement  */
  YYSYMBOL_modifier_statement = 117,       /* modifier_statement  */
  YYSYMBOL_program_statement = 118,        /* program_statement  */
  YYSYMBOL_library_statement = 119,        /* library_statement  */
  YYSYMBOL_use_statement = 120,            /* use_statement  */
  YYSYMBOL_modifier_signature = 121,       /* modifier_signature  */
  YYSYMBOL_modifier_context = 122,         /* modifier_context  */
  YYSYMBOL_watch_statement = 123,          /* watch_statement  */
  YYSYMBOL_unwatch_statement = 124,        /* unwatch_statement  */
  YYSYMBOL_watch_target_list = 125,        /* watch_target_list  */
  YYSYMBOL_server_statement = 126,         /* server_statement  */
  YYSYMBOL_127_2 = 127,                    /* @2  */
  YYSYMBOL_128_3 = 128,                    /* @3  */
  YYSYMBOL_window_block = 129,             /* window_block  */
  YYSYMBOL_window_item_list = 130,         /* window_item_list  */
  YYSYMBOL_window_item = 131,              /* window_item  */
  YYSYMBOL_server_item_list = 132,         /* server_item_list  */
  YYSYMBOL_server_item = 133,              /* server_item  */
  YYSYMBOL_server_string_list = 134,       /* server_string_list  */
  YYSYMBOL_watch_target_path = 135,        /* watch_target_path  */
  YYSYMBOL_without_watchers_statement = 136, /* without_watchers_statement  */
  YYSYMBOL_on_error_statement = 137,       /* on_error_statement  */
  YYSYMBOL_error_statement = 138,          /* error_statement  */
  YYSYMBOL_return_statement = 139,         /* return_statement  */
  YYSYMBOL_label_statement = 140,          /* label_statement  */
  YYSYMBOL_goto_statement = 141,           /* goto_statement  */
  YYSYMBOL_gosub_statement = 142,          /* gosub_statement  */
  YYSYMBOL_break_statement = 143,          /* break_statement  */
  YYSYMBOL_continue_statement = 144,       /* continue_statement  */
  YYSYMBOL_if_statement = 145,             /* if_statement  */
  YYSYMBOL_if_block_tail = 146,            /* if_block_tail  */
  YYSYMBOL_if_inline_tail = 147,           /* if_inline_tail  */
  YYSYMBOL_inline_statement = 148,         /* inline_statement  */
  YYSYMBOL_expression = 149,               /* expression  */
  YYSYMBOL_or_expression = 150,            /* or_expression  */
  YYSYMBOL_and_expression = 151,           /* and_expression  */
  YYSYMBOL_not_expression = 152,           /* not_expression  */
  YYSYMBOL_comparison_expression = 153,    /* comparison_expression  */
  YYSYMBOL_set_expression = 154,           /* set_expression  */
  YYSYMBOL_additive_expression = 155,      /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 156, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 157,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 158,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 159,      /* comparison_operator  */
  YYSYMBOL_primary = 160,                  /* primary  */
  YYSYMBOL_record_literal = 161,           /* record_literal  */
  YYSYMBOL_ident_suffix = 162,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 163,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 164,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 165,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 166,            /* argument_list  */
  YYSYMBOL_array_argument_list = 167,      /* array_argument_list  */
  YYSYMBOL_parameter_list_opt = 168,       /* parameter_list_opt  */
  YYSYMBOL_parameter_default = 169,        /* parameter_default  */
  YYSYMBOL_parameter_list = 170,           /* parameter_list  */
  YYSYMBOL_field_name = 171,               /* field_name  */
  YYSYMBOL_dot_field_name = 172,           /* dot_field_name  */
  YYSYMBOL_record_field_list = 173,        /* record_field_list  */
  YYSYMBOL_field_policy = 174,             /* field_policy  */
  YYSYMBOL_optional_newlines = 175         /* optional_newlines  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;



/* Unqualified %code blocks.  */
#line 710 "src/parser.y"

static int yylex(YYSTYPE *lvalp, YYLTYPE *llocp, gb_parse_ctx *ctx);
static void yyerror(YYLTYPE *llocp, gb_parse_ctx *ctx, const char *message);
static void report_syntax_error(gb_parse_ctx *ctx, int line, int column,
                                int end_line, int end_column, const char *message);

#line 915 "src/parser.tab.c"

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
#define YYLAST   3087

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  92
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  84
/* YYNRULES -- Number of rules.  */
#define YYNRULES  351
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  795

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   346


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
      85,    86,    87,    88,    89,    90,    91
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   797,   797,   801,   802,   803,   807,   808,   809,   810,
     811,   812,   813,   814,   815,   816,   817,   818,   819,   820,
     821,   822,   823,   824,   825,   826,   827,   828,   829,   830,
     831,   832,   833,   839,   850,   855,   856,   868,   880,   881,
     882,   883,   887,   888,   889,   893,   894,   895,   906,   906,
     914,   918,   919,   923,   924,   925,   926,   930,   936,   940,
     941,   947,   952,   961,   974,  1003,  1004,  1005,  1009,  1013,
    1029,  1034,  1042,  1046,  1067,  1073,  1079,  1085,  1088,  1094,
    1095,  1099,  1100,  1101,  1105,  1106,  1107,  1108,  1109,  1110,
    1111,  1112,  1113,  1114,  1115,  1116,  1117,  1118,  1119,  1120,
    1121,  1122,  1123,  1124,  1125,  1126,  1127,  1128,  1129,  1130,
    1136,  1147,  1150,  1157,  1160,  1166,  1172,  1178,  1179,  1180,
    1181,  1182,  1183,  1199,  1215,  1236,  1237,  1241,  1245,  1248,
    1256,  1262,  1266,  1267,  1287,  1286,  1294,  1293,  1312,  1316,
    1326,  1334,  1335,  1336,  1341,  1346,  1350,  1355,  1360,  1374,
    1379,  1390,  1405,  1410,  1416,  1417,  1418,  1422,  1425,  1428,
    1431,  1434,  1440,  1441,  1445,  1446,  1450,  1456,  1457,  1458,
    1459,  1464,  1477,  1482,  1487,  1497,  1501,  1502,  1506,  1513,
    1517,  1526,  1527,  1531,  1532,  1536,  1540,  1547,  1550,  1553,
    1562,  1572,  1575,  1578,  1584,  1591,  1601,  1602,  1603,  1604,
    1605,  1606,  1607,  1608,  1609,  1610,  1611,  1615,  1619,  1620,
    1624,  1625,  1647,  1648,  1652,  1653,  1654,  1660,  1661,  1662,
    1666,  1667,  1668,  1672,  1673,  1674,  1678,  1679,  1686,  1690,
    1691,  1692,  1696,  1697,  1698,  1699,  1704,  1718,  1719,  1720,
    1721,  1722,  1723,  1724,  1725,  1726,  1727,  1731,  1749,  1750,
    1751,  1752,  1753,  1770,  1776,  1777,  1778,  1779,  1780,  1781,
    1782,  1783,  1784,  1788,  1789,  1793,  1798,  1803,  1809,  1821,
    1826,  1834,  1844,  1856,  1857,  1861,  1862,  1866,  1867,  1871,
    1872,  1886,  1890,  1894,  1895,  1896,  1897,  1898,  1899,  1903,
    1904,  1907,  1908,  1923,  1930,  1939,  1940,  1941,  1942,  1943,
    1944,  1945,  1946,  1947,  1948,  1949,  1950,  1951,  1952,  1953,
    1954,  1955,  1956,  1957,  1958,  1959,  1960,  1961,  1962,  1963,
    1964,  1965,  1966,  1967,  1968,  1969,  1970,  1971,  1972,  1973,
    1974,  1975,  1976,  1977,  1978,  1979,  1980,  1981,  1982,  1983,
    1984,  1985,  1989,  1990,  1991,  1992,  1993,  1994,  2002,  2029,
    2048,  2049
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
  "MODIFIER_VALUE", "WINDOW_OPEN", "AS", "DIM", "PLUS_EQ", "MINUS_EQ",
  "STAR_EQ", "SLASH_EQ", "EXCLUDING", "INTERSECTING", "IF", "CONSIDER_IF",
  "THEN", "ELSE", "CONSIDER_ELSE", "END", "END_CONSIDER", "PRINT", "TRUE",
  "FALSE", "NOTHING", "UNKNOWN_VALUE", "AND", "OR", "NOT", "WITH", "NEW",
  "SPAWN", "FOR", "TO", "STEP", "DO", "UNTIL", "IN", "EACH", "WHILE",
  "CONSIDER", "BREAK", "CONTINUE", "FUNCTION", "RETURN", "GOTO", "GOSUB",
  "WATCH", "UNWATCH", "WITHOUT", "WATCHERS", "ON", "NEXT", "STOP",
  "ERROR_VALUE", "MODIFIER", "PROGRAM", "LIBRARY", "LOAD", "USE", "EXPORT",
  "OP_EQ", "OP_NE", "OP_GT", "OP_LT", "OP_GE", "OP_LE", "OP_NGT", "OP_NLT",
  "OP_NGE", "OP_NLE", "PLUS", "MINUS", "STAR", "SLASH", "LPAREN", "RPAREN",
  "LBRACKET", "RBRACKET", "LBRACE", "RBRACE", "COMMA", "COLON", "NEWLINE",
  "IF_WITHOUT_ELSE", "NO_DOT", "DOT", "$accept", "program",
  "statement_list", "statement", "assignment", "compound_op", "lvalue",
  "variable_name", "comparison_lens", "$@1", "modifier_name",
  "modifier_word", "print_statement", "call_statement",
  "with_lock_statement", "for_end", "for_each_statement",
  "do_loop_statement", "while_statement", "consider_statement",
  "consider_branch_list", "consider_else_opt", "consider_statement_list",
  "consider_body_statement", "function_statement", "modifier_statement",
  "program_statement", "library_statement", "use_statement",
  "modifier_signature", "modifier_context", "watch_statement",
  "unwatch_statement", "watch_target_list", "server_statement", "@2", "@3",
  "window_block", "window_item_list", "window_item", "server_item_list",
  "server_item", "server_string_list", "watch_target_path",
  "without_watchers_statement", "on_error_statement", "error_statement",
  "return_statement", "label_statement", "goto_statement",
  "gosub_statement", "break_statement", "continue_statement",
  "if_statement", "if_block_tail", "if_inline_tail", "inline_statement",
  "expression", "or_expression", "and_expression", "not_expression",
  "comparison_expression", "set_expression", "additive_expression",
  "multiplicative_expression", "unary_expression", "postfix_expression",
  "comparison_operator", "primary", "record_literal", "ident_suffix",
  "ident_dot_suffix", "duration_terms", "argument_list_opt",
  "argument_list", "array_argument_list", "parameter_list_opt",
  "parameter_default", "parameter_list", "field_name", "dot_field_name",
  "record_field_list", "field_policy", "optional_newlines", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-538)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -538,   147,  1086,  -538,    38,   -44,    89,  -538,  2437,  -538,
    2424,   157,   166,    95,  2437,  2437,   185,   204,   258,  2437,
     125,   125,    60,  2437,   229,    24,  -538,   613,   209,   259,
     284,   264,   269,   244,  -538,  -538,   227,   617,   242,   246,
     252,  -538,  -538,  -538,  -538,  -538,  -538,  -538,  -538,  -538,
     254,  -538,   256,  -538,  -538,  -538,   265,   268,   280,   281,
     282,   289,   291,   293,  -538,   292,  2437,  2437,   304,   378,
    -538,  -538,   306,  2519,  -538,  -538,  -538,  -538,  -538,  2437,
     768,   385,   311,  -538,  2519,  2437,  -538,  -538,    57,   372,
     362,   364,  -538,  -538,   497,   216,   220,  -538,   112,  -538,
    -538,   394,   340,  -538,   322,   130,   397,  -538,   315,   317,
    -538,  -538,   326,   327,  -538,  -538,  -538,   339,   125,  -538,
     137,   329,  -538,   334,    63,    93,   419,  -538,  -538,  -538,
    -538,  -538,   180,  -538,   389,   347,   341,   184,  -538,   424,
    -538,   209,  -538,  -538,  -538,  -538,  -538,  -538,  2437,  2437,
    -538,  2898,  2437,   511,  -538,  -538,  -538,  -538,  -538,  -538,
    -538,  -538,  -538,  -538,  -538,  -538,  -538,  2583,  -538,   349,
     346,   352,  2646,  -538,  2437,  -538,  -538,    51,   354,   355,
    -538,   357,   724,   832,  2437,  2960,  -538,  2334,  2437,  2437,
    2519,  2519,  -538,  -538,  -538,  -538,  -538,  -538,  -538,  -538,
    -538,  -538,  2463,  2519,  2519,  2519,  2519,  2519,  2437,  3022,
     438,  2437,  2437,  2437,  2437,   439,    37,   960,  -538,   427,
     440,   440,   125,   -12,   125,  -538,   445,  -538,  -538,  -538,
      68,  -538,    71,  -538,   371,   440,  -538,   448,   440,  -538,
     451,   449,   452,   422,  -538,   379,   455,   383,   384,  -538,
    -538,  -538,  -538,  -538,  -538,  -538,  -538,  -538,  -538,  -538,
    -538,  -538,  -538,  -538,  -538,  -538,  -538,  -538,  -538,  -538,
    -538,  -538,  -538,  -538,  -538,  -538,  -538,  -538,  -538,  -538,
    -538,  -538,  -538,  -538,  -538,  -538,  -538,  -538,  -538,  -538,
    -538,  -538,  -538,  -538,  -538,  -538,  -538,  2437,  2437,   386,
    -538,   381,    20,  -538,    92,  -538,  2437,  -538,   382,   124,
     390,   388,  2437,  -538,  -538,  -538,  -538,  -538,   391,  -538,
     396,   393,  -538,   403,   404,   406,  -538,  -538,  -538,  -538,
    -538,  -538,  -538,  -538,  -538,  -538,  -538,  -538,  -538,   402,
     364,  -538,   216,   216,  2519,   321,   220,   220,  -538,  -538,
     408,   412,   414,  -538,  -538,  -538,   418,   415,   464,   462,
    2437,   502,  2437,  1149,  2437,   262,   442,   431,   430,   436,
     145,   441,   329,  1212,  -538,  1275,  -538,  -538,  -538,  -538,
    2437,   437,  -538,   443,   447,  1338,   523,  -538,  -538,   448,
    -538,   454,  2437,  2437,  -538,  -538,   532,  -538,  2437,  2437,
     453,  -538,  -538,  -538,    23,  -538,  -538,   456,  -538,    36,
     231,  -538,  2437,  2437,  -538,  1023,   521,   321,  -538,  2437,
    2437,   460,  -538,  2437,  2437,   466,   508,   468,   514,   539,
    2437,   474,   550,   273,   488,   578,   495,   500,  -538,   533,
     535,   509,  -538,  -538,   510,   534,   593,   515,  -538,   518,
     524,  2437,   525,  -538,  -538,  -538,  -538,   897,    56,   598,
    -538,  -538,   758,  -538,  -538,   526,   527,   195,   585,  -538,
     656,  -538,   529,   530,  -538,  1401,    12,   531,  -538,  2437,
    -538,   540,   541,   591,  -538,   546,  -538,  -538,  -538,  -538,
    -538,  -538,   610,   611,  -538,  -538,   549,  -538,  -538,  1464,
     547,   548,  -538,  1527,  -538,   551,  -538,  -538,  -538,  -538,
    -538,   536,    66,  -538,   558,    70,    72,   623,   640,  -538,
    -538,   641,    74,  -538,  -538,  -538,  2437,  -538,   559,   562,
    2437,  -538,   563,  -538,  -538,  1590,   616,    58,  -538,  2437,
    -538,  -538,  1401,   566,  -538,  -538,   567,  1653,  -538,  -538,
    -538,  1716,   273,  1779,  1842,   604,  -538,  -538,   597,  1905,
    -538,  1968,  2437,   342,   655,   657,  -538,  -538,   102,   532,
    2437,  2437,     8,   574,   660,  2709,  -538,   577,   580,   581,
     669,   653,  2031,  -538,  -538,   663,  2094,  -538,   642,   589,
    -538,   590,   599,  1401,  1401,  -538,  -538,  1653,  -538,  -538,
    -538,   600,   601,   603,  -538,  -538,  -538,  -538,  -538,  -538,
    -538,  -538,  -538,  -538,   606,  -538,   608,  -538,  -538,   612,
     621,   622,   628,   629,   630,   633,   634,  -538,   637,  -538,
     638,   646,   635,   636,   620,   632,  -538,   631,   650,   239,
     647,   648,   722,   658,  -538,  -538,   730,  2772,  -538,  -538,
     733,   654,   169,   182,  -538,   737,  -538,   659,   661,   727,
    2362,   731,   667,  -538,  -538,  -538,  -538,  -538,  1401,  1653,
    -538,  -538,  -538,  -538,  -538,  -538,  -538,  -538,  -538,  -538,
    -538,  -538,  -538,   670,   676,   680,  -538,  -538,   681,   682,
    2835,   440,   738,  -538,  -538,  -538,   688,   691,   776,   696,
     175,   183,   187,   701,  -538,   702,   795,   712,   193,  -538,
    -538,   714,  -538,   715,   717,  -538,  1401,  -538,  -538,  -538,
    -538,  -538,  -538,   721,   186,   729,  -538,  2157,  -538,  2437,
     723,  -538,   726,   812,   814,  -538,  -538,   732,  -538,   815,
    1023,  -538,  1023,   521,  -538,  -538,  -538,   734,   736,   765,
    -538,  -538,  -538,   740,   741,   208,   253,  -538,  2220,   742,
    -538,  -538,  -538,   287,  -538,  -538,   743,   302,  -538,  -538,
     822,   828,   777,  -538,   830,   308,  2283,  -538,   835,   753,
     757,   759,   761,   851,   853,   810,  -538,  -538,  -538,  -538,
     811,   816,  -538,  -538,  -538
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       3,     0,     2,     1,    45,     0,     0,    33,     0,    46,
       0,     0,     0,     0,     0,     0,   181,   183,     0,   176,
       0,     0,     0,     0,     0,     0,    47,     0,     0,     0,
       0,     0,     0,     0,     4,     5,     0,     0,    42,     0,
       0,     9,    11,    13,    12,    14,    15,    16,    17,    18,
       0,    20,     0,    21,    10,    23,     0,     0,     0,     0,
       0,     0,     0,     0,    32,     0,   273,   273,     0,   248,
      45,   251,     0,     0,   247,   255,   256,   257,   258,     0,
       0,     0,     0,   254,     0,     0,   350,   350,   265,     0,
     207,   208,   210,   212,   214,   217,   220,   223,   226,   232,
     262,   250,     0,    57,     0,     0,     0,     3,     0,     0,
     182,   184,     0,     0,   177,   179,   180,    45,     0,   164,
       0,   132,   131,     0,     0,     0,     0,   175,    53,    55,
      54,    56,   125,    51,     0,     0,     0,   118,   120,   117,
     119,     0,     6,    50,    38,    39,    40,    41,     0,     0,
      48,     0,     0,     0,   178,     7,     8,    19,    22,    24,
      25,    26,    27,    28,    29,    30,    31,     0,   275,     0,
     274,     0,     0,   271,   273,   228,   213,   229,     0,     0,
     227,     0,     0,     0,   273,     0,   252,     0,     0,     0,
       0,     0,   237,   238,   239,   240,   241,   242,   243,   244,
     245,   246,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     3,     0,
     279,   279,     0,     0,     0,     3,     0,     3,   174,   173,
       0,   172,     0,   169,     0,   279,    52,     0,   279,     3,
       0,     0,     0,     0,    34,     0,     0,   295,     0,   296,
     341,   311,   308,   309,   300,   315,   322,   323,   324,   340,
     320,   321,   319,   306,   304,   329,   310,   301,   338,   313,
     314,   302,   305,   312,   337,   325,   326,   332,   316,   327,
     328,   335,   339,   307,   336,   303,   297,   298,   299,   333,
     334,   331,   317,   318,   330,    44,    35,     0,     0,   295,
     294,     0,     0,   293,     0,    59,     0,    60,     0,     0,
       0,     0,   273,   249,   259,   260,   351,   277,   350,   263,
     350,     0,   295,     0,   269,    45,     3,   196,    42,   197,
     198,   199,   200,   201,   202,   203,   204,   205,   206,     0,
     209,   211,   218,   219,     0,   215,   221,   222,   224,   225,
       0,   295,     0,   234,   272,    58,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    79,   289,     0,   280,     0,
       0,     0,   133,     0,   165,     0,   171,   170,   167,   168,
     273,     0,   127,     0,     0,     0,   123,   121,   122,     0,
      43,     0,   273,   273,    37,    36,     0,   136,     0,     0,
       0,   350,   276,   141,     0,   253,   230,     0,   350,     0,
       0,   266,   273,   273,   267,     0,   191,   216,   233,   273,
     273,     0,     3,     0,     0,     0,     0,     0,    46,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     3,    46,
      46,     0,   126,     3,     0,    46,     0,     0,    49,     0,
       0,   348,     0,   154,   342,   343,   134,     0,     0,     0,
     141,   231,     0,   261,   264,     0,     0,     0,    46,   185,
       0,   186,     0,     0,     3,     0,     0,     0,     3,     0,
      74,     0,     0,     0,    81,     0,   281,   284,   285,   286,
     287,   288,     0,     0,   290,     3,   291,     3,     3,     0,
       0,     0,    63,     0,     3,     0,   124,     3,    61,    62,
     349,     0,     0,   154,   295,     0,     0,     0,     0,   142,
     143,     0,     0,   278,   268,   270,     0,     3,     0,     0,
       0,     3,     0,   235,   236,     0,    46,    47,    68,     0,
       3,     3,     0,     0,    75,    81,     0,    80,    76,   283,
     282,     0,     0,     0,     0,    46,   129,   166,    46,     0,
     116,     0,     0,     0,     0,     0,   155,   156,     0,     0,
       0,     0,     0,     0,     0,     0,   141,     0,     0,     0,
       0,     0,     0,   188,   187,     0,     0,   192,    46,     0,
      66,     0,     0,     0,     0,    69,     3,    77,    81,   110,
      82,     0,     0,     0,    87,    89,    91,    90,    92,    83,
      93,    94,    95,    96,     0,    98,     0,    88,   100,     0,
       0,     0,     0,     0,     0,     0,     0,   109,    46,   292,
      46,    46,     0,     0,    46,    46,   344,     0,   162,     0,
       0,     0,     0,     0,   345,   346,     0,     0,   141,   141,
       0,     0,     0,     0,   139,     0,   141,     0,     0,    46,
       0,    46,     0,    65,    67,     3,    72,    70,     0,    78,
      84,    85,    86,    97,    99,   101,   102,   103,   104,   105,
     106,   107,   108,     0,     0,     0,   128,   113,     0,     0,
       0,   279,     0,   157,   137,     3,     0,     0,     0,     0,
       0,     0,     0,     0,   144,     0,     0,     0,     0,   138,
       3,     0,     3,     0,     0,    64,     0,    71,   111,   112,
     130,   115,   114,     0,     0,     0,   163,     0,   135,     0,
       0,   146,     0,     0,     0,   141,   141,     0,     3,     0,
       0,   189,     0,   191,   193,    73,   154,     0,     0,    46,
     347,   141,   145,     0,     0,     0,     0,   147,     0,     0,
     190,   195,   194,     0,   154,     3,     0,     0,   149,   150,
       0,     0,    46,   140,     0,     0,     0,   161,     0,     0,
       0,     0,     0,     0,    46,     0,   151,   148,   153,   160,
       0,     0,   152,   159,   158
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -538,  -538,   -86,  -538,  -181,   747,  -538,    -2,   809,  -538,
    -538,   773,  -180,  -173,  -534,  -537,  -531,  -524,  -510,  -506,
    -538,  -538,  -464,  -538,  -503,  -502,  -500,  -498,  -162,   766,
     517,  -495,  -494,  -108,  -538,  -538,  -538,  -493,  -456,  -538,
    -511,  -538,  -538,   686,  -492,  -160,  -149,  -147,  -489,  -139,
    -137,  -122,  -116,  -488,  -405,   168,  -459,    16,  -538,   725,
     -62,  -538,  -194,   172,   155,   -51,   834,   710,  -538,   652,
    -538,  -538,  -538,   181,  -538,  -538,  -206,   363,  -538,   465,
    -117,  -171,   399,   -84
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    35,    36,   152,    37,    88,   153,   246,
     132,   133,    39,    40,    41,   538,    42,    43,    44,    45,
     365,   432,   547,   609,    46,    47,    48,    49,    50,   134,
     383,    51,    52,   120,    53,   513,   453,    54,   458,   520,
     512,   567,   639,   121,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,   469,   471,   339,   168,    90,    91,
      92,    93,    94,    95,    96,    97,    98,   203,    99,   100,
     186,   414,   101,   169,   170,   318,   367,   494,   368,   302,
     303,   304,   452,   182
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      38,   309,   568,   183,   522,   595,   327,   329,   528,   345,
     223,   532,   320,   604,   330,   369,   605,   176,   115,   116,
     119,   217,   175,   606,    89,   331,   103,   332,   124,   381,
     108,   109,   384,   180,   295,   114,    67,   607,   333,   122,
     334,   608,    65,   127,   610,   611,   646,   612,   335,   613,
     336,   539,   615,   616,   617,   618,   666,   667,   622,   627,
     516,   459,    70,   604,   117,   337,   605,   228,   324,   371,
     563,   338,   376,   606,   224,   378,   572,   573,   516,   360,
     517,   597,     9,   125,     9,   311,   398,   607,   647,   229,
     564,   608,   353,    68,   610,   611,   648,   612,   580,   613,
     540,   181,   615,   616,   617,   618,   563,   399,   622,   627,
     574,   460,   518,   230,   370,    26,   119,    26,    66,   463,
     653,   231,   565,   361,   316,   377,   642,   341,   379,    70,
     518,   717,   363,   208,   669,   604,   570,   184,   605,   373,
     118,   375,   209,   232,   519,   606,   590,     3,   185,     9,
     417,   233,   575,   385,   566,   348,   349,   571,   565,   607,
     576,   104,   519,   608,   244,   245,   610,   611,   296,   612,
     105,   613,   213,   400,   615,   616,   617,   618,   401,   745,
     622,   627,    26,   107,   128,   328,   516,   516,   240,   110,
     566,   516,   701,   702,   208,   241,   214,   516,   317,   325,
     708,   713,     5,   209,   129,   404,   706,   733,   111,   106,
     401,   734,   516,   128,   526,    38,   215,   739,   130,     9,
     119,    10,   119,   224,   350,   225,   437,   355,   356,   357,
     358,   224,   770,   129,   409,   763,   410,   131,   518,   518,
     415,    16,    17,   518,    19,    20,    21,   130,   171,   518,
     705,    25,    26,   775,    27,   401,   732,   516,    31,    32,
     235,   401,   112,   135,   518,   113,   131,   747,   137,   138,
     519,   519,   401,   139,   140,   519,   486,   771,   487,   755,
     756,   519,   430,   527,   123,   431,   327,   329,   136,   327,
     329,   563,   204,   205,   330,   767,   519,   330,   206,   207,
     488,   489,   490,   491,   141,   331,   516,   332,   331,   518,
     332,   774,   563,   394,   395,   142,   464,   457,   333,   316,
     334,   333,   402,   334,   462,   692,   778,   693,   335,   154,
     336,   335,   783,   336,   155,   760,   475,   761,   190,   191,
     156,   519,   157,   565,   158,   337,   637,   638,   337,   492,
     493,   338,   499,   159,   338,   310,   160,   503,   518,   346,
     347,    38,   342,   343,   565,   321,   601,   602,   161,   162,
     163,    38,   167,    38,   603,   566,   425,   164,   427,   165,
     429,   166,   173,    38,   172,   614,   174,   619,   535,   178,
     519,   179,   542,   187,   188,   189,   566,   210,   620,   211,
     621,   216,   212,   218,   652,   219,   220,   221,   623,   551,
     624,   553,   554,    38,   454,   455,   601,   602,   559,   222,
     226,   561,   227,   234,   603,   625,   237,   238,   242,   239,
     305,   626,   306,   307,   312,   614,   313,   619,   314,   476,
     477,   582,   354,   359,   366,   586,   483,   364,   620,   374,
     621,   380,   382,   387,   593,   594,   386,   388,   623,   389,
     624,   391,   390,   392,   393,   328,   396,   510,   328,   397,
     403,   405,    87,    38,   411,   625,   700,   408,   523,   327,
     329,   626,   401,   412,   413,   725,    66,   330,   601,   602,
     416,   418,   419,   407,   420,   543,   603,    38,   331,   421,
     332,    38,   423,   422,   424,   143,   426,   614,   433,   619,
     668,   333,   434,   334,   190,   191,   435,   436,   442,   724,
     620,   335,   621,   336,   144,   145,   146,   147,   444,   438,
     623,   443,   624,    38,   446,   591,   451,   461,   337,   448,
      38,   456,   581,   470,   338,    38,   585,   625,   474,    38,
     479,    38,    38,   626,   478,   592,   480,    38,   481,    38,
     482,   441,   484,   192,   193,   194,   195,   196,   197,   198,
     199,   200,   201,   449,   450,   485,   495,   297,   636,   716,
      38,   150,   496,   497,    38,   500,   644,   645,   498,   501,
     502,    38,    38,   465,   466,    38,   505,   506,   504,   508,
     472,   473,   521,   507,   529,   509,   511,   524,   525,   727,
     533,   534,   546,   549,   550,   552,    69,    70,    71,   541,
      72,    73,    74,   562,   740,   143,   742,   577,   544,   545,
     144,   145,   146,   147,   548,   556,   557,     9,   569,   560,
      75,    76,    77,    78,   578,   579,    79,   583,    80,    81,
     584,   587,   758,   589,   596,   598,   632,   633,   328,   640,
     325,   641,   649,     5,   650,   654,    38,    38,    82,   656,
      26,   655,    83,   657,   658,   530,   662,   663,   664,   776,
       9,   688,    10,   148,   660,   683,   684,   665,   670,   671,
      84,   672,   689,    85,   673,    86,   674,    87,   685,   149,
     675,   150,    16,    17,   126,    19,    20,    21,   151,   676,
     677,   690,    25,    26,    38,    27,   678,   679,   680,    31,
      32,   681,   682,   686,   687,    38,   696,    69,    70,    71,
     691,    72,    73,    74,   698,   694,   695,   703,    38,   697,
      38,   707,   704,   726,   531,   750,   711,   709,     9,   710,
     714,    75,    76,    77,    78,   715,    38,    79,   718,    80,
      81,    69,    70,    71,   719,    72,    73,    74,   720,   721,
     722,    69,    70,    71,    38,    72,   728,    74,   729,    82,
     730,    26,     9,    83,   731,    75,    76,    77,    78,   735,
     736,    79,     9,    80,    81,    75,    76,    77,    78,   737,
     738,    84,   741,   743,    85,   744,    86,   315,    87,   746,
     748,   751,   316,    82,   752,    26,   753,    83,   754,   759,
     757,   766,   764,    82,   765,    26,   779,    83,   768,   769,
     773,   777,   780,   781,   782,    84,   299,   300,    85,   785,
      86,   786,    87,   249,   250,   787,   316,   788,    85,   789,
      86,   251,    87,   252,   253,   790,   254,   791,   255,   256,
     257,   258,   259,   260,   261,   262,   263,   264,   265,   266,
     267,   268,   269,   270,   271,   272,   273,   274,   275,   276,
     277,   278,   279,   280,   281,   282,   283,   284,   285,   286,
     287,   288,   289,   290,   291,   292,   293,   294,   792,   793,
     298,   514,   300,   202,   794,   236,   447,   243,   249,   250,
     372,   762,   344,   340,   177,   629,   251,   319,   252,   253,
     316,   254,   515,   255,   256,   257,   258,   259,   260,   261,
     262,   263,   264,   265,   266,   267,   268,   269,   270,   271,
     272,   273,   274,   275,   276,   277,   278,   279,   280,   281,
     282,   283,   284,   285,   286,   287,   288,   289,   290,   291,
     292,   293,   294,   406,     4,     0,     0,     5,   643,     0,
       6,     0,     7,     0,     0,     0,     0,     0,     0,     8,
       0,     0,     0,     0,     9,   316,    10,     0,     0,     0,
       0,     0,     0,     0,    11,     0,     0,    12,     0,     0,
      13,   362,     0,     0,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,     0,    25,    26,     0,    27,
      28,    29,    30,    31,    32,    33,     0,     4,     0,     0,
       5,     0,     0,     6,     0,     7,     0,     0,     0,     0,
       0,     0,     8,     0,     0,   467,     0,   468,    34,    10,
       0,     0,     0,     0,     0,     0,     0,    11,     0,     0,
      12,     0,     0,    13,     0,     0,     0,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,     0,    25,
      26,     0,    27,    28,    29,    30,    31,    32,    33,     0,
       4,     0,     0,     5,     0,     0,     6,     0,     7,     0,
       0,     0,     0,     0,     0,     8,     0,     0,     0,     0,
       9,    34,    10,     0,     0,     0,     0,     0,     0,     0,
      11,     0,     0,    12,     0,     0,    13,     0,     0,     0,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,    25,    26,     0,    27,    28,    29,    30,    31,
      32,    33,     0,     4,     0,     0,     5,     0,     0,     6,
       0,     7,     0,     0,     0,     0,     0,     0,     8,     0,
       0,     0,     0,   428,    34,    10,     0,     0,     0,     0,
       0,     0,     0,    11,     0,     0,    12,     0,     0,    13,
       0,     0,     0,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,     0,    25,    26,     0,    27,    28,
      29,    30,    31,    32,    33,     0,     4,     0,     0,     5,
       0,     0,     6,     0,     7,     0,     0,     0,     0,     0,
       0,     8,     0,     0,     0,     0,   439,    34,    10,     0,
       0,     0,     0,     0,     0,     0,    11,     0,     0,    12,
       0,     0,    13,     0,     0,     0,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,     0,    25,    26,
       0,    27,    28,    29,    30,    31,    32,    33,     0,     4,
       0,     0,     5,     0,     0,     6,     0,     7,     0,     0,
       0,     0,     0,     0,     8,     0,     0,     0,     0,   440,
      34,    10,     0,     0,     0,     0,     0,     0,     0,    11,
       0,     0,    12,     0,     0,    13,     0,     0,     0,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
       0,    25,    26,     0,    27,    28,    29,    30,    31,    32,
      33,     0,     4,     0,     0,     5,     0,     0,     6,     0,
       7,     0,     0,     0,     0,     0,     0,     8,     0,     0,
       0,     0,   445,    34,    10,     0,     0,     0,     0,     0,
       0,     0,    11,     0,     0,    12,     0,     0,    13,     0,
       0,     0,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,     0,    25,    26,     0,    27,    28,    29,
      30,    31,    32,    33,     0,     4,     0,     0,     5,     0,
       0,     6,     0,     7,     0,     0,     0,     0,     0,     0,
       8,     0,     0,     0,     0,   536,    34,    10,     0,     0,
       0,     0,     0,     0,     0,    11,     0,     0,    12,     0,
       0,    13,     0,     0,     0,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,     0,    25,   537,     0,
      27,    28,    29,    30,    31,    32,    33,     0,     4,     0,
       0,     5,     0,     0,     6,     0,     7,     0,     0,     0,
       0,     0,     0,     8,     0,     0,     0,     0,   555,    34,
      10,     0,     0,     0,     0,     0,     0,     0,    11,     0,
       0,    12,     0,     0,    13,     0,     0,     0,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,     0,
      25,    26,     0,    27,    28,    29,    30,    31,    32,    33,
       0,     4,     0,     0,     5,     0,     0,     6,     0,     7,
       0,     0,     0,     0,     0,     0,     8,     0,     0,     0,
       0,   558,    34,    10,     0,     0,     0,     0,     0,     0,
       0,    11,     0,     0,    12,     0,     0,    13,     0,     0,
       0,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,     0,    25,    26,     0,    27,    28,    29,    30,
      31,    32,    33,     0,     4,     0,     0,     5,     0,     0,
       6,     0,     7,     0,     0,     0,     0,     0,     0,     8,
       0,     0,     0,     0,   588,    34,    10,     0,     0,     0,
       0,     0,     0,     0,    11,     0,     0,    12,     0,     0,
      13,     0,     0,     0,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,     0,    25,    26,     0,    27,
      28,    29,    30,    31,    32,    33,     0,   325,     0,     0,
       5,     0,     0,     6,     0,   599,     0,     0,     0,     0,
       0,     0,     8,     0,     0,     0,     0,     9,    34,    10,
       0,     0,     0,     0,     0,     0,     0,    11,     0,     0,
      12,     0,     0,    13,     0,     0,     0,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,     0,    25,
      26,     0,    27,    28,    29,    30,    31,    32,    33,     0,
       4,     0,     0,     5,     0,     0,     6,     0,     7,     0,
       0,     0,     0,     0,     0,     8,     0,     0,     0,     0,
     628,   600,    10,     0,     0,     0,     0,     0,     0,     0,
      11,     0,     0,    12,     0,     0,    13,     0,     0,     0,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,    25,    26,     0,    27,    28,    29,    30,    31,
      32,    33,     0,     4,     0,     0,     5,     0,     0,     6,
       0,     7,     0,     0,     0,     0,     0,     0,     8,     0,
       0,     0,     0,   630,    34,    10,     0,     0,     0,     0,
       0,     0,     0,    11,     0,     0,    12,     0,     0,    13,
       0,     0,     0,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,     0,    25,    26,     0,    27,    28,
      29,    30,    31,    32,    33,     0,     4,     0,     0,     5,
       0,     0,     6,     0,     7,     0,     0,     0,     0,     0,
       0,     8,     0,     0,     0,     0,   631,    34,    10,     0,
       0,     0,     0,     0,     0,     0,    11,     0,     0,    12,
       0,     0,    13,     0,     0,     0,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,     0,    25,    26,
       0,    27,    28,    29,    30,    31,    32,    33,     0,     4,
       0,     0,     5,     0,     0,     6,     0,     7,     0,     0,
       0,     0,     0,     0,     8,     0,     0,     0,     0,   634,
      34,    10,     0,     0,     0,     0,     0,     0,     0,    11,
       0,     0,    12,     0,     0,    13,     0,     0,     0,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
       0,    25,    26,     0,    27,    28,    29,    30,    31,    32,
      33,     0,     4,     0,     0,     5,     0,     0,     6,     0,
       7,     0,     0,     0,     0,     0,     0,     8,     0,     0,
       0,     0,   635,    34,    10,     0,     0,     0,     0,     0,
       0,     0,    11,     0,     0,    12,     0,     0,    13,     0,
       0,     0,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,     0,    25,    26,     0,    27,    28,    29,
      30,    31,    32,    33,     0,     4,     0,     0,     5,     0,
       0,     6,     0,     7,     0,     0,     0,     0,     0,     0,
       8,     0,     0,     0,     0,   659,    34,    10,     0,     0,
       0,     0,     0,     0,     0,    11,     0,     0,    12,     0,
       0,    13,     0,     0,     0,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,     0,    25,    26,     0,
      27,    28,    29,    30,    31,    32,    33,     0,     4,     0,
       0,     5,     0,     0,     6,     0,     7,     0,     0,     0,
       0,     0,     0,     8,     0,     0,     0,     0,   661,    34,
      10,     0,     0,     0,     0,     0,     0,     0,    11,     0,
       0,    12,     0,     0,    13,     0,     0,     0,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,     0,
      25,    26,     0,    27,    28,    29,    30,    31,    32,    33,
       0,     4,     0,     0,     5,     0,     0,     6,     0,     7,
       0,     0,     0,     0,     0,     0,     8,     0,     0,     0,
       0,   749,    34,    10,     0,     0,     0,     0,     0,     0,
       0,    11,     0,     0,    12,     0,     0,    13,     0,     0,
       0,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,     0,    25,    26,     0,    27,    28,    29,    30,
      31,    32,    33,     0,     4,     0,     0,     5,     0,     0,
       6,     0,     7,     0,     0,     0,     0,     0,     0,     8,
       0,     0,     0,     0,   772,    34,    10,     0,     0,     0,
       0,     0,     0,     0,    11,     0,     0,    12,     0,     0,
      13,     0,     0,     0,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,     0,    25,    26,     0,    27,
      28,    29,    30,    31,    32,    33,     0,     4,     0,     0,
       5,     0,     0,     6,     0,     7,     0,     0,     0,     0,
       0,     0,     8,     0,     0,     0,     0,   784,    34,    10,
       0,     0,     0,     0,     0,     0,     0,    11,     0,     0,
      12,     0,     0,    13,     0,     0,     0,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,   325,    25,
      26,     5,    27,    28,    29,    30,    31,    32,    33,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     9,     0,
      10,     0,     0,     0,     0,     0,   325,     0,     0,     5,
       0,    34,     0,     0,     0,     0,     0,     0,     0,     0,
      16,    17,     0,    19,    20,    21,     9,     0,    10,     0,
      25,    26,     0,    27,     0,     0,     0,    31,    32,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    16,    17,
       0,    19,    20,    21,     0,     0,     0,     0,    25,    26,
       0,    27,   326,     0,     0,    31,    32,    69,    70,    71,
       0,    72,    73,    74,     0,     0,     0,     0,     0,     0,
      69,    70,    71,     0,    72,    73,    74,     0,     9,     0,
     712,    75,    76,    77,    78,     0,     0,    79,     0,    80,
      81,     9,   102,     0,    75,    76,    77,    78,     0,     0,
      79,     0,    80,    81,     0,     0,     0,     0,     0,    82,
       0,    26,     0,    83,     0,     0,     0,     0,     0,     0,
       0,     0,    82,     0,    26,     0,    83,     0,     0,     0,
       0,    84,     0,     0,    85,     0,    86,     0,    87,     0,
       0,     0,     0,     0,    84,     0,     0,    85,     0,    86,
       0,    87,    69,    70,    71,     0,    72,    73,    74,   192,
     193,   194,   195,   196,   197,   198,   199,   200,   201,     0,
       0,     0,     0,     9,     0,     0,    75,    76,    77,    78,
       0,     0,     0,     0,    80,    81,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    82,     0,    26,     0,    83,     0,
       0,     0,     0,     0,     0,     0,     0,   299,   300,     0,
       0,     0,     0,     0,   249,   250,    84,     0,     0,    85,
       0,    86,   251,    87,   252,   253,     0,   254,     0,   255,
     256,   257,   258,   259,   260,   261,   262,   263,   264,   265,
     266,   267,   268,   269,   270,   271,   272,   273,   274,   275,
     276,   277,   278,   279,   280,   281,   282,   283,   284,   285,
     286,   287,   288,   289,   290,   291,   292,   293,   294,     0,
     299,   300,     0,     0,     0,     0,     0,   249,   250,     0,
       0,     0,     0,     0,   301,   251,     0,   252,   253,     0,
     254,     0,   255,   256,   257,   258,   259,   260,   261,   262,
     263,   264,   265,   266,   267,   268,   269,   270,   271,   272,
     273,   274,   275,   276,   277,   278,   279,   280,   281,   282,
     283,   284,   285,   286,   287,   288,   289,   290,   291,   292,
     293,   294,     0,   299,   300,     0,     0,     0,     0,     0,
     249,   250,     0,     0,     0,     0,     0,   308,   251,     0,
     252,   253,     0,   254,     0,   255,   256,   257,   258,   259,
     260,   261,   262,   263,   264,   265,   266,   267,   268,   269,
     270,   271,   272,   273,   274,   275,   276,   277,   278,   279,
     280,   281,   282,   283,   284,   285,   286,   287,   288,   289,
     290,   291,   292,   293,   294,     0,   299,   300,     0,     0,
       0,     0,     0,   249,   250,     0,     0,     0,     0,     0,
     651,   251,     0,   252,   253,     0,   254,     0,   255,   256,
     257,   258,   259,   260,   261,   262,   263,   264,   265,   266,
     267,   268,   269,   270,   271,   272,   273,   274,   275,   276,
     277,   278,   279,   280,   281,   282,   283,   284,   285,   286,
     287,   288,   289,   290,   291,   292,   293,   294,     0,   299,
     300,     0,     0,     0,     0,     0,   249,   250,     0,     0,
       0,     0,     0,   699,   251,     0,   252,   253,     0,   254,
       0,   255,   256,   257,   258,   259,   260,   261,   262,   263,
     264,   265,   266,   267,   268,   269,   270,   271,   272,   273,
     274,   275,   276,   277,   278,   279,   280,   281,   282,   283,
     284,   285,   286,   287,   288,   289,   290,   291,   292,   293,
     294,     0,   247,     0,     0,   248,     0,     0,     0,   249,
     250,     0,     0,     0,     0,     0,   723,   251,     0,   252,
     253,     0,   254,     0,   255,   256,   257,   258,   259,   260,
     261,   262,   263,   264,   265,   266,   267,   268,   269,   270,
     271,   272,   273,   274,   275,   276,   277,   278,   279,   280,
     281,   282,   283,   284,   285,   286,   287,   288,   289,   290,
     291,   292,   293,   294,   322,     0,     0,   323,     0,     0,
       0,   249,   250,     0,     0,     0,     0,     0,     0,   251,
       0,   252,   253,     0,   254,     0,   255,   256,   257,   258,
     259,   260,   261,   262,   263,   264,   265,   266,   267,   268,
     269,   270,   271,   272,   273,   274,   275,   276,   277,   278,
     279,   280,   281,   282,   283,   284,   285,   286,   287,   288,
     289,   290,   291,   292,   293,   294,   351,     0,     0,   352,
       0,     0,     0,   249,   250,     0,     0,     0,     0,     0,
       0,   251,     0,   252,   253,     0,   254,     0,   255,   256,
     257,   258,   259,   260,   261,   262,   263,   264,   265,   266,
     267,   268,   269,   270,   271,   272,   273,   274,   275,   276,
     277,   278,   279,   280,   281,   282,   283,   284,   285,   286,
     287,   288,   289,   290,   291,   292,   293,   294
};

static const yytype_int16 yycheck[] =
{
       2,   172,   513,    87,   460,   542,   187,   187,   467,   203,
     118,   470,   183,   547,   187,   221,   547,    79,    20,    21,
      22,   107,    73,   547,     8,   187,    10,   187,     4,   235,
      14,    15,   238,    84,   151,    19,    80,   547,   187,    23,
     187,   547,     4,    27,   547,   547,    38,   547,   187,   547,
     187,    39,   547,   547,   547,   547,   593,   594,   547,   547,
       4,    38,     4,   597,     4,   187,   597,     4,   185,    81,
       4,   187,     4,   597,    86,     4,     4,     5,     4,    42,
      24,   545,    24,    59,    24,    34,    66,   597,    80,    26,
      24,   597,   209,     4,   597,   597,    88,   597,    24,   597,
      88,    85,   597,   597,   597,   597,     4,    87,   597,   597,
      38,    88,    56,    50,   222,    57,   118,    57,    80,    83,
     576,    58,    56,    86,    88,    57,    24,   189,    57,     4,
      56,   668,   218,    82,   598,   669,    66,    80,   669,   225,
      80,   227,    91,    50,    88,   669,    88,     0,    91,    24,
     344,    58,    80,   239,    88,   206,   207,    87,    56,   669,
      88,     4,    88,   669,   148,   149,   669,   669,   152,   669,
       4,   669,    42,    81,   669,   669,   669,   669,    86,   716,
     669,   669,    57,    88,     4,   187,     4,     4,     4,     4,
      88,     4,   648,   649,    82,    11,    66,     4,   182,     4,
     656,   660,     7,    91,    24,    81,    24,    24,     4,    43,
      86,    24,     4,     4,    19,   217,    86,    24,    38,    24,
     222,    26,   224,    86,   208,    88,    81,   211,   212,   213,
     214,    86,    24,    24,   318,   746,   320,    57,    56,    56,
     326,    46,    47,    56,    49,    50,    51,    38,    67,    56,
      81,    56,    57,   764,    59,    86,    81,     4,    63,    64,
      80,    86,     4,     4,    56,     7,    57,    81,     4,     5,
      88,    88,    86,     4,     5,    88,     3,    24,     5,   735,
     736,    88,    20,    88,    55,    23,   467,   467,     4,   470,
     470,     4,    76,    77,   467,   751,    88,   470,    78,    79,
      27,    28,    29,    30,    60,   467,     4,   467,   470,    56,
     470,    24,     4,   297,   298,    88,    85,   401,   467,    88,
     467,   470,   306,   470,   408,    86,    24,    88,   467,    87,
     467,   470,    24,   470,    88,   740,   422,   742,    17,    18,
      88,    88,    88,    56,    88,   467,     4,     5,   470,    76,
      77,   467,   438,    88,   470,   174,    88,   443,    56,   204,
     205,   363,   190,   191,    56,   184,   547,   547,    88,    88,
      88,   373,    80,   375,   547,    88,   360,    88,   362,    88,
     364,    88,     4,   385,    80,   547,    80,   547,   474,     4,
      88,    80,   478,    21,    32,    31,    88,     3,   547,    59,
     547,     4,    80,    88,   575,    88,    80,    80,   547,   495,
     547,   497,   498,   415,   398,   399,   597,   597,   504,    80,
      91,   507,    88,     4,   597,   547,    37,    80,     4,    88,
      81,   547,    86,    81,    80,   597,    81,   597,    81,   423,
     424,   527,     4,     4,     4,   531,   430,    20,   597,     4,
     597,    80,     4,     4,   540,   541,     5,     5,   597,    37,
     597,     6,    83,    80,    80,   467,    80,   451,   470,    88,
      88,    81,    84,   475,    81,   597,   647,    86,   462,   660,
     660,   597,    86,    80,    80,   691,    80,   660,   669,   669,
      88,    83,    80,   312,    80,   479,   669,   499,   660,    81,
     660,   503,    38,    88,    42,     8,     4,   669,    66,   669,
     596,   660,    81,   660,    17,    18,    86,    81,    81,   690,
     669,   660,   669,   660,    13,    14,    15,    16,    81,    88,
     669,    88,   669,   535,    11,   537,     4,    81,   660,    85,
     542,    88,   526,    22,   660,   547,   530,   669,    88,   551,
      42,   553,   554,   669,    88,   539,    88,   559,    44,   561,
      21,   380,    88,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    75,   392,   393,    25,    88,    66,   562,   665,
     582,    84,     4,    88,   586,    52,   570,   571,    88,    54,
      81,   593,   594,   412,   413,   597,    62,     4,    88,    81,
     419,   420,     4,    88,    19,    81,    81,    81,    81,   695,
      81,    81,    21,     3,     3,    66,     3,     4,     5,    88,
       7,     8,     9,    87,   710,     8,   712,     4,    88,    88,
      13,    14,    15,    16,    88,    88,    88,    24,    80,    88,
      27,    28,    29,    30,     4,     4,    33,    88,    35,    36,
      88,    88,   738,    37,    88,    88,    52,    60,   660,     4,
       4,     4,    88,     7,     4,    88,   668,   669,    55,    88,
      57,    91,    59,     4,    21,    19,    34,    88,    88,   765,
      24,    61,    26,    66,    21,    48,    48,    88,    88,    88,
      77,    88,    60,    80,    88,    82,    88,    84,    52,    82,
      88,    84,    46,    47,    91,    49,    50,    51,    91,    88,
      88,    80,    56,    57,   716,    59,    88,    88,    88,    63,
      64,    88,    88,    88,    88,   727,     4,     3,     4,     5,
      80,     7,     8,     9,     4,    88,    88,     4,   740,    81,
     742,     4,    88,     5,    88,   729,    19,    88,    24,    88,
      19,    27,    28,    29,    30,    88,   758,    33,    88,    35,
      36,     3,     4,     5,    88,     7,     8,     9,    88,    88,
      88,     3,     4,     5,   776,     7,    88,     9,    87,    55,
       4,    57,    24,    59,    88,    27,    28,    29,    30,    88,
      88,    33,    24,    35,    36,    27,    28,    29,    30,     4,
      88,    77,    88,    88,    80,    88,    82,    83,    84,    88,
      81,    88,    88,    55,    88,    57,     4,    59,     4,     4,
      88,    56,    88,    55,    88,    57,     4,    59,    88,    88,
      88,    88,     4,    56,     4,    77,     4,     5,    80,     4,
      82,    88,    84,    11,    12,    88,    88,    88,    80,    88,
      82,    19,    84,    21,    22,     4,    24,     4,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    88,    88,
     153,     4,     5,    94,    88,   132,   389,   141,    11,    12,
     224,   743,   202,   188,    80,   552,    19,    85,    21,    22,
      88,    24,   457,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,   311,     4,    -1,    -1,     7,   569,    -1,
      10,    -1,    12,    -1,    -1,    -1,    -1,    -1,    -1,    19,
      -1,    -1,    -1,    -1,    24,    88,    26,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    34,    -1,    -1,    37,    -1,    -1,
      40,    41,    -1,    -1,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    -1,    56,    57,    -1,    59,
      60,    61,    62,    63,    64,    65,    -1,     4,    -1,    -1,
       7,    -1,    -1,    10,    -1,    12,    -1,    -1,    -1,    -1,
      -1,    -1,    19,    -1,    -1,    22,    -1,    24,    88,    26,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    34,    -1,    -1,
      37,    -1,    -1,    40,    -1,    -1,    -1,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    -1,    56,
      57,    -1,    59,    60,    61,    62,    63,    64,    65,    -1,
       4,    -1,    -1,     7,    -1,    -1,    10,    -1,    12,    -1,
      -1,    -1,    -1,    -1,    -1,    19,    -1,    -1,    -1,    -1,
      24,    88,    26,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      34,    -1,    -1,    37,    -1,    -1,    40,    -1,    -1,    -1,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    -1,    56,    57,    -1,    59,    60,    61,    62,    63,
      64,    65,    -1,     4,    -1,    -1,     7,    -1,    -1,    10,
      -1,    12,    -1,    -1,    -1,    -1,    -1,    -1,    19,    -1,
      -1,    -1,    -1,    24,    88,    26,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    34,    -1,    -1,    37,    -1,    -1,    40,
      -1,    -1,    -1,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    -1,    56,    57,    -1,    59,    60,
      61,    62,    63,    64,    65,    -1,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    12,    -1,    -1,    -1,    -1,    -1,
      -1,    19,    -1,    -1,    -1,    -1,    24,    88,    26,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    34,    -1,    -1,    37,
      -1,    -1,    40,    -1,    -1,    -1,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    -1,    56,    57,
      -1,    59,    60,    61,    62,    63,    64,    65,    -1,     4,
      -1,    -1,     7,    -1,    -1,    10,    -1,    12,    -1,    -1,
      -1,    -1,    -1,    -1,    19,    -1,    -1,    -1,    -1,    24,
      88,    26,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    34,
      -1,    -1,    37,    -1,    -1,    40,    -1,    -1,    -1,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      -1,    56,    57,    -1,    59,    60,    61,    62,    63,    64,
      65,    -1,     4,    -1,    -1,     7,    -1,    -1,    10,    -1,
      12,    -1,    -1,    -1,    -1,    -1,    -1,    19,    -1,    -1,
      -1,    -1,    24,    88,    26,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    34,    -1,    -1,    37,    -1,    -1,    40,    -1,
      -1,    -1,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    -1,    56,    57,    -1,    59,    60,    61,
      62,    63,    64,    65,    -1,     4,    -1,    -1,     7,    -1,
      -1,    10,    -1,    12,    -1,    -1,    -1,    -1,    -1,    -1,
      19,    -1,    -1,    -1,    -1,    24,    88,    26,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    34,    -1,    -1,    37,    -1,
      -1,    40,    -1,    -1,    -1,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    -1,    56,    57,    -1,
      59,    60,    61,    62,    63,    64,    65,    -1,     4,    -1,
      -1,     7,    -1,    -1,    10,    -1,    12,    -1,    -1,    -1,
      -1,    -1,    -1,    19,    -1,    -1,    -1,    -1,    24,    88,
      26,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    34,    -1,
      -1,    37,    -1,    -1,    40,    -1,    -1,    -1,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    -1,
      56,    57,    -1,    59,    60,    61,    62,    63,    64,    65,
      -1,     4,    -1,    -1,     7,    -1,    -1,    10,    -1,    12,
      -1,    -1,    -1,    -1,    -1,    -1,    19,    -1,    -1,    -1,
      -1,    24,    88,    26,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    34,    -1,    -1,    37,    -1,    -1,    40,    -1,    -1,
      -1,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    -1,    56,    57,    -1,    59,    60,    61,    62,
      63,    64,    65,    -1,     4,    -1,    -1,     7,    -1,    -1,
      10,    -1,    12,    -1,    -1,    -1,    -1,    -1,    -1,    19,
      -1,    -1,    -1,    -1,    24,    88,    26,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    34,    -1,    -1,    37,    -1,    -1,
      40,    -1,    -1,    -1,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    -1,    56,    57,    -1,    59,
      60,    61,    62,    63,    64,    65,    -1,     4,    -1,    -1,
       7,    -1,    -1,    10,    -1,    12,    -1,    -1,    -1,    -1,
      -1,    -1,    19,    -1,    -1,    -1,    -1,    24,    88,    26,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    34,    -1,    -1,
      37,    -1,    -1,    40,    -1,    -1,    -1,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    -1,    56,
      57,    -1,    59,    60,    61,    62,    63,    64,    65,    -1,
       4,    -1,    -1,     7,    -1,    -1,    10,    -1,    12,    -1,
      -1,    -1,    -1,    -1,    -1,    19,    -1,    -1,    -1,    -1,
      24,    88,    26,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      34,    -1,    -1,    37,    -1,    -1,    40,    -1,    -1,    -1,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    -1,    56,    57,    -1,    59,    60,    61,    62,    63,
      64,    65,    -1,     4,    -1,    -1,     7,    -1,    -1,    10,
      -1,    12,    -1,    -1,    -1,    -1,    -1,    -1,    19,    -1,
      -1,    -1,    -1,    24,    88,    26,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    34,    -1,    -1,    37,    -1,    -1,    40,
      -1,    -1,    -1,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    -1,    56,    57,    -1,    59,    60,
      61,    62,    63,    64,    65,    -1,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    12,    -1,    -1,    -1,    -1,    -1,
      -1,    19,    -1,    -1,    -1,    -1,    24,    88,    26,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    34,    -1,    -1,    37,
      -1,    -1,    40,    -1,    -1,    -1,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    -1,    56,    57,
      -1,    59,    60,    61,    62,    63,    64,    65,    -1,     4,
      -1,    -1,     7,    -1,    -1,    10,    -1,    12,    -1,    -1,
      -1,    -1,    -1,    -1,    19,    -1,    -1,    -1,    -1,    24,
      88,    26,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    34,
      -1,    -1,    37,    -1,    -1,    40,    -1,    -1,    -1,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      -1,    56,    57,    -1,    59,    60,    61,    62,    63,    64,
      65,    -1,     4,    -1,    -1,     7,    -1,    -1,    10,    -1,
      12,    -1,    -1,    -1,    -1,    -1,    -1,    19,    -1,    -1,
      -1,    -1,    24,    88,    26,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    34,    -1,    -1,    37,    -1,    -1,    40,    -1,
      -1,    -1,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    -1,    56,    57,    -1,    59,    60,    61,
      62,    63,    64,    65,    -1,     4,    -1,    -1,     7,    -1,
      -1,    10,    -1,    12,    -1,    -1,    -1,    -1,    -1,    -1,
      19,    -1,    -1,    -1,    -1,    24,    88,    26,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    34,    -1,    -1,    37,    -1,
      -1,    40,    -1,    -1,    -1,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    -1,    56,    57,    -1,
      59,    60,    61,    62,    63,    64,    65,    -1,     4,    -1,
      -1,     7,    -1,    -1,    10,    -1,    12,    -1,    -1,    -1,
      -1,    -1,    -1,    19,    -1,    -1,    -1,    -1,    24,    88,
      26,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    34,    -1,
      -1,    37,    -1,    -1,    40,    -1,    -1,    -1,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    -1,
      56,    57,    -1,    59,    60,    61,    62,    63,    64,    65,
      -1,     4,    -1,    -1,     7,    -1,    -1,    10,    -1,    12,
      -1,    -1,    -1,    -1,    -1,    -1,    19,    -1,    -1,    -1,
      -1,    24,    88,    26,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    34,    -1,    -1,    37,    -1,    -1,    40,    -1,    -1,
      -1,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    -1,    56,    57,    -1,    59,    60,    61,    62,
      63,    64,    65,    -1,     4,    -1,    -1,     7,    -1,    -1,
      10,    -1,    12,    -1,    -1,    -1,    -1,    -1,    -1,    19,
      -1,    -1,    -1,    -1,    24,    88,    26,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    34,    -1,    -1,    37,    -1,    -1,
      40,    -1,    -1,    -1,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    -1,    56,    57,    -1,    59,
      60,    61,    62,    63,    64,    65,    -1,     4,    -1,    -1,
       7,    -1,    -1,    10,    -1,    12,    -1,    -1,    -1,    -1,
      -1,    -1,    19,    -1,    -1,    -1,    -1,    24,    88,    26,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    34,    -1,    -1,
      37,    -1,    -1,    40,    -1,    -1,    -1,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,     4,    56,
      57,     7,    59,    60,    61,    62,    63,    64,    65,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,    -1,
      26,    -1,    -1,    -1,    -1,    -1,     4,    -1,    -1,     7,
      -1,    88,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      46,    47,    -1,    49,    50,    51,    24,    -1,    26,    -1,
      56,    57,    -1,    59,    -1,    -1,    -1,    63,    64,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    46,    47,
      -1,    49,    50,    51,    -1,    -1,    -1,    -1,    56,    57,
      -1,    59,    88,    -1,    -1,    63,    64,     3,     4,     5,
      -1,     7,     8,     9,    -1,    -1,    -1,    -1,    -1,    -1,
       3,     4,     5,    -1,     7,     8,     9,    -1,    24,    -1,
      88,    27,    28,    29,    30,    -1,    -1,    33,    -1,    35,
      36,    24,    38,    -1,    27,    28,    29,    30,    -1,    -1,
      33,    -1,    35,    36,    -1,    -1,    -1,    -1,    -1,    55,
      -1,    57,    -1,    59,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    55,    -1,    57,    -1,    59,    -1,    -1,    -1,
      -1,    77,    -1,    -1,    80,    -1,    82,    -1,    84,    -1,
      -1,    -1,    -1,    -1,    77,    -1,    -1,    80,    -1,    82,
      -1,    84,     3,     4,     5,    -1,     7,     8,     9,    66,
      67,    68,    69,    70,    71,    72,    73,    74,    75,    -1,
      -1,    -1,    -1,    24,    -1,    -1,    27,    28,    29,    30,
      -1,    -1,    -1,    -1,    35,    36,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    55,    -1,    57,    -1,    59,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,     4,     5,    -1,
      -1,    -1,    -1,    -1,    11,    12,    77,    -1,    -1,    80,
      -1,    82,    19,    84,    21,    22,    -1,    24,    -1,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    -1,
       4,     5,    -1,    -1,    -1,    -1,    -1,    11,    12,    -1,
      -1,    -1,    -1,    -1,    81,    19,    -1,    21,    22,    -1,
      24,    -1,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    -1,     4,     5,    -1,    -1,    -1,    -1,    -1,
      11,    12,    -1,    -1,    -1,    -1,    -1,    81,    19,    -1,
      21,    22,    -1,    24,    -1,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    -1,     4,     5,    -1,    -1,
      -1,    -1,    -1,    11,    12,    -1,    -1,    -1,    -1,    -1,
      81,    19,    -1,    21,    22,    -1,    24,    -1,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    -1,     4,
       5,    -1,    -1,    -1,    -1,    -1,    11,    12,    -1,    -1,
      -1,    -1,    -1,    81,    19,    -1,    21,    22,    -1,    24,
      -1,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    -1,     4,    -1,    -1,     7,    -1,    -1,    -1,    11,
      12,    -1,    -1,    -1,    -1,    -1,    81,    19,    -1,    21,
      22,    -1,    24,    -1,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,     4,    -1,    -1,     7,    -1,    -1,
      -1,    11,    12,    -1,    -1,    -1,    -1,    -1,    -1,    19,
      -1,    21,    22,    -1,    24,    -1,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,     4,    -1,    -1,     7,
      -1,    -1,    -1,    11,    12,    -1,    -1,    -1,    -1,    -1,
      -1,    19,    -1,    21,    22,    -1,    24,    -1,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    93,    94,     0,     4,     7,    10,    12,    19,    24,
      26,    34,    37,    40,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    56,    57,    59,    60,    61,
      62,    63,    64,    65,    88,    95,    96,    98,    99,   104,
     105,   106,   108,   109,   110,   111,   116,   117,   118,   119,
     120,   123,   124,   126,   129,   136,   137,   138,   139,   140,
     141,   142,   143,   144,   145,     4,    80,    80,     4,     3,
       4,     5,     7,     8,     9,    27,    28,    29,    30,    33,
      35,    36,    55,    59,    77,    80,    82,    84,    99,   149,
     150,   151,   152,   153,   154,   155,   156,   157,   158,   160,
     161,   164,    38,   149,     4,     4,    43,    88,   149,   149,
       4,     4,     4,     7,   149,    99,    99,     4,    80,    99,
     125,   135,   149,    55,     4,    59,    91,   149,     4,    24,
      38,    57,   102,   103,   121,     4,     4,     4,     5,     4,
       5,    60,    88,     8,    13,    14,    15,    16,    66,    82,
      84,    91,    97,   100,    87,    88,    88,    88,    88,    88,
      88,    88,    88,    88,    88,    88,    88,    80,   149,   165,
     166,   165,    80,     4,    80,   157,   152,   158,     4,    80,
     157,   149,   175,   175,    80,    91,   162,    21,    32,    31,
      17,    18,    66,    67,    68,    69,    70,    71,    72,    73,
      74,    75,   100,   159,    76,    77,    78,    79,    82,    91,
       3,    59,    80,    42,    66,    86,     4,    94,    88,    88,
      80,    80,    80,   125,    86,    88,    91,    88,     4,    26,
      50,    58,    50,    58,     4,    80,   103,    37,    80,    88,
       4,    11,     4,   121,   149,   149,   101,     4,     7,    11,
      12,    19,    21,    22,    24,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,   172,   149,    66,    97,     4,
       5,    81,   171,   172,   173,    81,    86,    81,    81,   173,
     165,    34,    80,    81,    81,    83,    88,   149,   167,    85,
     173,   165,     4,     7,   172,     4,    88,    96,    99,   104,
     105,   120,   137,   138,   139,   141,   142,   143,   144,   148,
     151,   152,   155,   155,   159,   154,   156,   156,   157,   157,
     149,     4,     7,   172,     4,   149,   149,   149,   149,     4,
      42,    86,    41,    94,    20,   112,     4,   168,   170,   168,
     125,    81,   135,    94,     4,    94,     4,    57,     4,    57,
      80,   168,     4,   122,   168,    94,     5,     4,     5,    37,
      83,     6,    80,    80,   149,   149,    80,    88,    66,    87,
      81,    86,   149,    88,    81,    81,   161,   165,    86,   175,
     175,    81,    80,    80,   163,    94,    88,   154,    83,    80,
      80,    81,    88,    38,    42,   149,     4,   149,    24,   149,
      20,    23,   113,    66,    81,    86,    81,    81,    88,    24,
      24,   165,    81,    88,    81,    24,    11,   122,    85,   165,
     165,     4,   174,   128,   149,   149,    88,   175,   130,    38,
      88,    81,   175,    83,    85,   165,   165,    22,    24,   146,
      22,   147,   165,   165,    88,    94,   149,   149,    88,    42,
      88,    44,    21,   149,    88,    25,     3,     5,    27,    28,
      29,    30,    76,    77,   169,    88,     4,    88,    88,    94,
      52,    54,    81,    94,    88,    62,     4,    88,    81,    81,
     149,    81,   132,   127,     4,   171,     4,    24,    56,    88,
     131,     4,   130,   149,    81,    81,    19,    88,   148,    19,
      19,    88,   148,    81,    81,    94,    24,    57,   107,    39,
      88,    88,    94,   149,    88,    88,    21,   114,    88,     3,
       3,    94,    66,    94,    94,    24,    88,    88,    24,    94,
      88,    94,    87,     4,    24,    56,    88,   133,   132,    80,
      66,    87,     4,     5,    38,    80,    88,     4,     4,     4,
      24,   149,    94,    88,    88,   149,    94,    88,    24,    37,
      88,    99,   149,    94,    94,   107,    88,   114,    88,    12,
      88,    96,   104,   105,   106,   108,   109,   110,   111,   115,
     116,   117,   118,   119,   120,   123,   124,   129,   136,   137,
     138,   139,   140,   141,   142,   143,   144,   145,    24,   169,
      24,    24,    52,    60,    24,    24,   149,     4,     5,   134,
       4,     4,    24,   174,   149,   149,    38,    80,    88,    88,
       4,    81,   173,   130,    88,    91,    88,     4,    21,    24,
      21,    24,    34,    88,    88,    88,   107,   107,    94,   114,
      88,    88,    88,    88,    88,    88,    88,    88,    88,    88,
      88,    88,    88,    48,    48,    52,    88,    88,    61,    60,
      80,    80,    86,    88,    88,    88,     4,    81,     4,    81,
     173,   130,   130,     4,    88,    81,    24,     4,   130,    88,
      88,    19,    88,   148,    19,    88,    94,   107,    88,    88,
      88,    88,    88,    81,   173,   168,     5,    94,    88,    87,
       4,    88,    81,    24,    24,    88,    88,     4,    88,    24,
      94,    88,    94,    88,    88,   107,    88,    81,    81,    24,
     149,    88,    88,     4,     4,   130,   130,    88,    94,     4,
     146,   146,   147,   132,    88,    88,    56,   130,    88,    88,
      24,    24,    24,    88,    24,   132,    94,    88,    24,     4,
       4,    56,     4,    24,    24,     4,    88,    88,    88,    88,
       4,     4,    88,    88,    88
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    92,    93,    94,    94,    94,    95,    95,    95,    95,
      95,    95,    95,    95,    95,    95,    95,    95,    95,    95,
      95,    95,    95,    95,    95,    95,    95,    95,    95,    95,
      95,    95,    95,    95,    96,    96,    96,    96,    97,    97,
      97,    97,    98,    98,    98,    99,    99,    99,   101,   100,
     100,   102,   102,   103,   103,   103,   103,   104,   104,   105,
     105,   105,   105,   105,   106,   107,   107,   107,   108,   108,
     108,   108,   108,   108,   109,   110,   111,   112,   112,   113,
     113,   114,   114,   114,   115,   115,   115,   115,   115,   115,
     115,   115,   115,   115,   115,   115,   115,   115,   115,   115,
     115,   115,   115,   115,   115,   115,   115,   115,   115,   115,
     115,   116,   116,   117,   117,   118,   119,   120,   120,   120,
     120,   120,   120,   120,   120,   121,   121,   122,   123,   123,
     123,   124,   125,   125,   127,   126,   128,   126,   129,   129,
     129,   130,   130,   130,   131,   131,   131,   131,   131,   131,
     131,   131,   131,   131,   132,   132,   132,   133,   133,   133,
     133,   133,   134,   134,   135,   135,   136,   137,   137,   137,
     137,   137,   137,   137,   137,   138,   139,   139,   140,   141,
     142,   143,   143,   144,   144,   145,   145,   146,   146,   146,
     146,   147,   147,   147,   147,   147,   148,   148,   148,   148,
     148,   148,   148,   148,   148,   148,   148,   149,   150,   150,
     151,   151,   152,   152,   153,   153,   153,   154,   154,   154,
     155,   155,   155,   156,   156,   156,   157,   157,   157,   157,
     157,   157,   158,   158,   158,   158,   158,   159,   159,   159,
     159,   159,   159,   159,   159,   159,   159,   160,   160,   160,
     160,   160,   160,   160,   160,   160,   160,   160,   160,   160,
     160,   160,   160,   161,   161,   162,   162,   162,   162,   163,
     163,   164,   164,   165,   165,   166,   166,   167,   167,   168,
     168,   169,   169,   169,   169,   169,   169,   169,   169,   170,
     170,   170,   170,   171,   171,   172,   172,   172,   172,   172,
     172,   172,   172,   172,   172,   172,   172,   172,   172,   172,
     172,   172,   172,   172,   172,   172,   172,   172,   172,   172,
     172,   172,   172,   172,   172,   172,   172,   172,   172,   172,
     172,   172,   172,   172,   172,   172,   172,   172,   172,   172,
     172,   172,   173,   173,   173,   173,   173,   173,   174,   174,
     175,   175
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     2,
       1,     1,     2,     1,     2,     2,     2,     2,     2,     2,
       2,     2,     1,     1,     3,     3,     4,     4,     1,     1,
       1,     1,     1,     4,     3,     1,     1,     1,     0,     4,
       1,     1,     2,     1,     1,     1,     1,     2,     4,     4,
       4,     6,     6,     6,    10,     3,     2,     3,     7,     8,
       9,    10,     9,    11,     6,     7,     7,     5,     6,     0,
       3,     0,     2,     2,     2,     2,     2,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     2,     1,     2,
       1,     2,     2,     2,     2,     2,     2,     2,     2,     1,
       1,    10,    10,     9,    10,    10,     7,     2,     2,     2,
       2,     4,     4,     4,     6,     1,     4,     1,     9,     7,
      10,     2,     1,     3,     0,    11,     0,    10,    10,     9,
      13,     0,     2,     2,     4,     6,     5,     6,     9,     7,
       7,     9,    10,     9,     0,     2,     2,     3,    10,    10,
       9,     7,     1,     3,     1,     3,     7,     4,     4,     3,
       4,     4,     3,     3,     3,     2,     1,     2,     2,     2,
       2,     1,     2,     1,     2,     6,     6,     3,     3,     6,
       7,     0,     3,     6,     7,     7,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     3,
       1,     3,     1,     2,     1,     3,     4,     1,     3,     3,
       1,     3,     3,     1,     3,     3,     1,     2,     2,     2,
       4,     5,     1,     4,     3,     6,     6,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     3,
       1,     1,     2,     4,     1,     1,     1,     1,     1,     3,
       3,     5,     1,     3,     5,     0,     3,     3,     5,     0,
       3,     2,     3,     0,     1,     1,     3,     1,     4,     0,
       1,     1,     2,     2,     1,     1,     1,     1,     1,     1,
       3,     3,     5,     1,     1,     1,     1,     1,     1,     1,
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
#line 769 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2975 "src/parser.tab.c"
        break;

    case YYSYMBOL_STRING: /* STRING  */
#line 769 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2981 "src/parser.tab.c"
        break;

    case YYSYMBOL_LENS_CONTENT: /* LENS_CONTENT  */
#line 769 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2987 "src/parser.tab.c"
        break;

    case YYSYMBOL_QUALIFIED_IDENT: /* QUALIFIED_IDENT  */
#line 769 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2993 "src/parser.tab.c"
        break;

    case YYSYMBOL_MODIFIER_PREFIX: /* MODIFIER_PREFIX  */
#line 769 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2999 "src/parser.tab.c"
        break;

    case YYSYMBOL_MODIFIER_VALUE: /* MODIFIER_VALUE  */
#line 769 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3005 "src/parser.tab.c"
        break;

    case YYSYMBOL_program: /* program  */
#line 792 "src/parser.y"
            { (void) ((*yyvaluep).stmt_list); }
#line 3011 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement_list: /* statement_list  */
#line 732 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3017 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement: /* statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3023 "src/parser.tab.c"
        break;

    case YYSYMBOL_assignment: /* assignment  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3029 "src/parser.tab.c"
        break;

    case YYSYMBOL_lvalue: /* lvalue  */
#line 770 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3035 "src/parser.tab.c"
        break;

    case YYSYMBOL_variable_name: /* variable_name  */
#line 769 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3041 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_lens: /* comparison_lens  */
#line 777 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 3047 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_name: /* modifier_name  */
#line 769 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3053 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_word: /* modifier_word  */
#line 769 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3059 "src/parser.tab.c"
        break;

    case YYSYMBOL_print_statement: /* print_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3065 "src/parser.tab.c"
        break;

    case YYSYMBOL_call_statement: /* call_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3071 "src/parser.tab.c"
        break;

    case YYSYMBOL_with_lock_statement: /* with_lock_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3077 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_end: /* for_end  */
#line 769 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3083 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_each_statement: /* for_each_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3089 "src/parser.tab.c"
        break;

    case YYSYMBOL_do_loop_statement: /* do_loop_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3095 "src/parser.tab.c"
        break;

    case YYSYMBOL_while_statement: /* while_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3101 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement: /* consider_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3107 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_branch_list: /* consider_branch_list  */
#line 775 "src/parser.y"
            { ast_free_consider_branch_list(((*yyvaluep).consider_branch_list)); }
#line 3113 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_else_opt: /* consider_else_opt  */
#line 732 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3119 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement_list: /* consider_statement_list  */
#line 732 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3125 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_body_statement: /* consider_body_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3131 "src/parser.tab.c"
        break;

    case YYSYMBOL_function_statement: /* function_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3137 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_statement: /* modifier_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3143 "src/parser.tab.c"
        break;

    case YYSYMBOL_program_statement: /* program_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3149 "src/parser.tab.c"
        break;

    case YYSYMBOL_library_statement: /* library_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3155 "src/parser.tab.c"
        break;

    case YYSYMBOL_use_statement: /* use_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3161 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_signature: /* modifier_signature  */
#line 778 "src/parser.y"
            { ast_free_modifier_signature(((*yyvaluep).modifier_signature)); }
#line 3167 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_context: /* modifier_context  */
#line 769 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3173 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_statement: /* watch_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3179 "src/parser.tab.c"
        break;

    case YYSYMBOL_unwatch_statement: /* unwatch_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3185 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_list: /* watch_target_list  */
#line 776 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3191 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_statement: /* server_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3197 "src/parser.tab.c"
        break;

    case YYSYMBOL_window_block: /* window_block  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3203 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item_list: /* server_item_list  */
#line 782 "src/parser.y"
            { ast_free_server_item_list(((*yyvaluep).server_item_list)); }
#line 3209 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item: /* server_item  */
#line 781 "src/parser.y"
            { AstServerItemList one = ast_server_item_list_append(ast_server_item_list_empty(), ((*yyvaluep).server_item)); ast_free_server_item_list(one); }
#line 3215 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_string_list: /* server_string_list  */
#line 776 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3221 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_path: /* watch_target_path  */
#line 769 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3227 "src/parser.tab.c"
        break;

    case YYSYMBOL_without_watchers_statement: /* without_watchers_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3233 "src/parser.tab.c"
        break;

    case YYSYMBOL_on_error_statement: /* on_error_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3239 "src/parser.tab.c"
        break;

    case YYSYMBOL_error_statement: /* error_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3245 "src/parser.tab.c"
        break;

    case YYSYMBOL_return_statement: /* return_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3251 "src/parser.tab.c"
        break;

    case YYSYMBOL_label_statement: /* label_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3257 "src/parser.tab.c"
        break;

    case YYSYMBOL_goto_statement: /* goto_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3263 "src/parser.tab.c"
        break;

    case YYSYMBOL_gosub_statement: /* gosub_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3269 "src/parser.tab.c"
        break;

    case YYSYMBOL_break_statement: /* break_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3275 "src/parser.tab.c"
        break;

    case YYSYMBOL_continue_statement: /* continue_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3281 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_statement: /* if_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3287 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_block_tail: /* if_block_tail  */
#line 732 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3293 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_inline_tail: /* if_inline_tail  */
#line 732 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3299 "src/parser.tab.c"
        break;

    case YYSYMBOL_inline_statement: /* inline_statement  */
#line 771 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3305 "src/parser.tab.c"
        break;

    case YYSYMBOL_expression: /* expression  */
#line 770 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3311 "src/parser.tab.c"
        break;

    case YYSYMBOL_or_expression: /* or_expression  */
#line 770 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3317 "src/parser.tab.c"
        break;

    case YYSYMBOL_and_expression: /* and_expression  */
#line 770 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3323 "src/parser.tab.c"
        break;

    case YYSYMBOL_not_expression: /* not_expression  */
#line 770 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3329 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_expression: /* comparison_expression  */
#line 770 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3335 "src/parser.tab.c"
        break;

    case YYSYMBOL_set_expression: /* set_expression  */
#line 770 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3341 "src/parser.tab.c"
        break;

    case YYSYMBOL_additive_expression: /* additive_expression  */
#line 770 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3347 "src/parser.tab.c"
        break;

    case YYSYMBOL_multiplicative_expression: /* multiplicative_expression  */
#line 770 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3353 "src/parser.tab.c"
        break;

    case YYSYMBOL_unary_expression: /* unary_expression  */
#line 770 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3359 "src/parser.tab.c"
        break;

    case YYSYMBOL_postfix_expression: /* postfix_expression  */
#line 770 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3365 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_operator: /* comparison_operator  */
#line 769 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3371 "src/parser.tab.c"
        break;

    case YYSYMBOL_primary: /* primary  */
#line 770 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3377 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_literal: /* record_literal  */
#line 770 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3383 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_suffix: /* ident_suffix  */
#line 779 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3389 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_dot_suffix: /* ident_dot_suffix  */
#line 779 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3395 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list_opt: /* argument_list_opt  */
#line 773 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3401 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list: /* argument_list  */
#line 773 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3407 "src/parser.tab.c"
        break;

    case YYSYMBOL_array_argument_list: /* array_argument_list  */
#line 773 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3413 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list_opt: /* parameter_list_opt  */
#line 776 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3419 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_default: /* parameter_default  */
#line 770 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3425 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list: /* parameter_list  */
#line 776 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3431 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_name: /* field_name  */
#line 769 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3437 "src/parser.tab.c"
        break;

    case YYSYMBOL_dot_field_name: /* dot_field_name  */
#line 769 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3443 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_field_list: /* record_field_list  */
#line 774 "src/parser.y"
            { ast_free_record_field_list(((*yyvaluep).record_field_list)); }
#line 3449 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_policy: /* field_policy  */
#line 780 "src/parser.y"
            { ast_free_expr(((*yyvaluep).field_policy).reset_expr); }
#line 3455 "src/parser.tab.c"
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
#line 797 "src/parser.y"
                     { ctx->parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3761 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 801 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3767 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 802 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3773 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 803 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3779 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 807 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3785 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 808 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3791 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 809 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3797 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 810 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3803 "src/parser.tab.c"
    break;

  case 10: /* statement: window_block  */
#line 811 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3809 "src/parser.tab.c"
    break;

  case 11: /* statement: for_each_statement  */
#line 812 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3815 "src/parser.tab.c"
    break;

  case 12: /* statement: while_statement  */
#line 813 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3821 "src/parser.tab.c"
    break;

  case 13: /* statement: do_loop_statement  */
#line 814 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3827 "src/parser.tab.c"
    break;

  case 14: /* statement: consider_statement  */
#line 815 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3833 "src/parser.tab.c"
    break;

  case 15: /* statement: function_statement  */
#line 816 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3839 "src/parser.tab.c"
    break;

  case 16: /* statement: modifier_statement  */
#line 817 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3845 "src/parser.tab.c"
    break;

  case 17: /* statement: program_statement  */
#line 818 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3851 "src/parser.tab.c"
    break;

  case 18: /* statement: library_statement  */
#line 819 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3857 "src/parser.tab.c"
    break;

  case 19: /* statement: use_statement NEWLINE  */
#line 820 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3863 "src/parser.tab.c"
    break;

  case 20: /* statement: watch_statement  */
#line 821 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3869 "src/parser.tab.c"
    break;

  case 21: /* statement: server_statement  */
#line 822 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3875 "src/parser.tab.c"
    break;

  case 22: /* statement: unwatch_statement NEWLINE  */
#line 823 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3881 "src/parser.tab.c"
    break;

  case 23: /* statement: without_watchers_statement  */
#line 824 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3887 "src/parser.tab.c"
    break;

  case 24: /* statement: on_error_statement NEWLINE  */
#line 825 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3893 "src/parser.tab.c"
    break;

  case 25: /* statement: error_statement NEWLINE  */
#line 826 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3899 "src/parser.tab.c"
    break;

  case 26: /* statement: return_statement NEWLINE  */
#line 827 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3905 "src/parser.tab.c"
    break;

  case 27: /* statement: label_statement NEWLINE  */
#line 828 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3911 "src/parser.tab.c"
    break;

  case 28: /* statement: goto_statement NEWLINE  */
#line 829 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3917 "src/parser.tab.c"
    break;

  case 29: /* statement: gosub_statement NEWLINE  */
#line 830 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3923 "src/parser.tab.c"
    break;

  case 30: /* statement: break_statement NEWLINE  */
#line 831 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3929 "src/parser.tab.c"
    break;

  case 31: /* statement: continue_statement NEWLINE  */
#line 832 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3935 "src/parser.tab.c"
    break;

  case 32: /* statement: if_statement  */
#line 833 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3941 "src/parser.tab.c"
    break;

  case 33: /* statement: DIM  */
#line 839 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 3954 "src/parser.tab.c"
    break;

  case 34: /* assignment: lvalue OP_EQ expression  */
#line 850 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 3960 "src/parser.tab.c"
    break;

  case 35: /* assignment: lvalue compound_op expression  */
#line 855 "src/parser.y"
                                    { (yyval.stmt) = ast_assign_op((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr), (yyvsp[-1].op_char)); }
#line 3966 "src/parser.tab.c"
    break;

  case 36: /* assignment: lvalue comparison_lens compound_op expression  */
#line 856 "src/parser.y"
                                                    {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign_op((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr), (yyvsp[-1].op_char));
      }
#line 3980 "src/parser.tab.c"
    break;

  case 37: /* assignment: lvalue comparison_lens OP_EQ expression  */
#line 868 "src/parser.y"
                                              {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 3994 "src/parser.tab.c"
    break;

  case 38: /* compound_op: PLUS_EQ  */
#line 880 "src/parser.y"
               { (yyval.op_char) = '+'; }
#line 4000 "src/parser.tab.c"
    break;

  case 39: /* compound_op: MINUS_EQ  */
#line 881 "src/parser.y"
               { (yyval.op_char) = '-'; }
#line 4006 "src/parser.tab.c"
    break;

  case 40: /* compound_op: STAR_EQ  */
#line 882 "src/parser.y"
               { (yyval.op_char) = '*'; }
#line 4012 "src/parser.tab.c"
    break;

  case 41: /* compound_op: SLASH_EQ  */
#line 883 "src/parser.y"
               { (yyval.op_char) = '/'; }
#line 4018 "src/parser.tab.c"
    break;

  case 42: /* lvalue: variable_name  */
#line 887 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4024 "src/parser.tab.c"
    break;

  case 43: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 888 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4030 "src/parser.tab.c"
    break;

  case 44: /* lvalue: lvalue DOT dot_field_name  */
#line 889 "src/parser.y"
                                             { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4036 "src/parser.tab.c"
    break;

  case 45: /* variable_name: IDENT  */
#line 893 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 4042 "src/parser.tab.c"
    break;

  case 46: /* variable_name: END  */
#line 894 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 4048 "src/parser.tab.c"
    break;

  case 47: /* variable_name: NEXT  */
#line 895 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 4054 "src/parser.tab.c"
    break;

  case 48: /* $@1: %empty  */
#line 906 "src/parser.y"
             { lexer_begin_lens_content(ctx->active_lexer); }
#line 4060 "src/parser.tab.c"
    break;

  case 49: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 906 "src/parser.y"
                                                                                  {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 4068 "src/parser.tab.c"
    break;

  case 50: /* comparison_lens: MODIFIER_PREFIX  */
#line 914 "src/parser.y"
                      { (yyval.modifier) = parse_modifier_use((yyvsp[0].text)); }
#line 4074 "src/parser.tab.c"
    break;

  case 51: /* modifier_name: modifier_word  */
#line 918 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 4080 "src/parser.tab.c"
    break;

  case 52: /* modifier_name: modifier_name modifier_word  */
#line 919 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 4086 "src/parser.tab.c"
    break;

  case 53: /* modifier_word: IDENT  */
#line 923 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4092 "src/parser.tab.c"
    break;

  case 54: /* modifier_word: TO  */
#line 924 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 4098 "src/parser.tab.c"
    break;

  case 55: /* modifier_word: END  */
#line 925 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 4104 "src/parser.tab.c"
    break;

  case 56: /* modifier_word: NEXT  */
#line 926 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 4110 "src/parser.tab.c"
    break;

  case 57: /* print_statement: PRINT expression  */
#line 930 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 4116 "src/parser.tab.c"
    break;

  case 58: /* print_statement: PRINT TO ERROR_VALUE expression  */
#line 936 "src/parser.y"
                                      { (yyval.stmt) = ast_print_error((yyvsp[0].expr)); }
#line 4122 "src/parser.tab.c"
    break;

  case 59: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 940 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 4128 "src/parser.tab.c"
    break;

  case 60: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 941 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 4139 "src/parser.tab.c"
    break;

  case 61: /* call_statement: lvalue DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 947 "src/parser.y"
                                                       {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 4149 "src/parser.tab.c"
    break;

  case 62: /* call_statement: lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 952 "src/parser.y"
                                                                 {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 4163 "src/parser.tab.c"
    break;

  case 63: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 961 "src/parser.y"
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
#line 4178 "src/parser.tab.c"
    break;

  case 64: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 974 "src/parser.y"
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
#line 4201 "src/parser.tab.c"
    break;

  case 65: /* for_end: END FOR NEWLINE  */
#line 1003 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 4207 "src/parser.tab.c"
    break;

  case 66: /* for_end: NEXT NEWLINE  */
#line 1004 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 4213 "src/parser.tab.c"
    break;

  case 67: /* for_end: NEXT variable_name NEWLINE  */
#line 1005 "src/parser.y"
                                 { (yyval.text) = (yyvsp[-1].text); }
#line 4219 "src/parser.tab.c"
    break;

  case 68: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list for_end  */
#line 1009 "src/parser.y"
                                                             {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4228 "src/parser.tab.c"
    break;

  case 69: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list for_end  */
#line 1013 "src/parser.y"
                                                                  {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4237 "src/parser.tab.c"
    break;

  case 70: /* for_each_statement: FOR IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1029 "src/parser.y"
                                                                         {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { YYERROR; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4247 "src/parser.tab.c"
    break;

  case 71: /* for_each_statement: FOR EACH IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1034 "src/parser.y"
                                                                              {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { YYERROR; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4257 "src/parser.tab.c"
    break;

  case 72: /* for_each_statement: FOR IDENT OP_EQ expression TO expression NEWLINE statement_list for_end  */
#line 1042 "src/parser.y"
                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].expr), NULL, (yyvsp[-1].stmt_list));
      }
#line 4266 "src/parser.tab.c"
    break;

  case 73: /* for_each_statement: FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list for_end  */
#line 1046 "src/parser.y"
                                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-9].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-9].text), (yyvsp[-7].expr), (yyvsp[-5].expr), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4275 "src/parser.tab.c"
    break;

  case 74: /* do_loop_statement: DO NEWLINE statement_list UNTIL expression NEWLINE  */
#line 1067 "src/parser.y"
                                                         {
        (yyval.stmt) = ast_do_loop((yyvsp[-3].stmt_list), (yyvsp[-1].expr));
      }
#line 4283 "src/parser.tab.c"
    break;

  case 75: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 1073 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 4291 "src/parser.tab.c"
    break;

  case 76: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 1079 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 4299 "src/parser.tab.c"
    break;

  case 77: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 1085 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 4307 "src/parser.tab.c"
    break;

  case 78: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 1088 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 4315 "src/parser.tab.c"
    break;

  case 79: /* consider_else_opt: %empty  */
#line 1094 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4321 "src/parser.tab.c"
    break;

  case 80: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 1095 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 4327 "src/parser.tab.c"
    break;

  case 81: /* consider_statement_list: %empty  */
#line 1099 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4333 "src/parser.tab.c"
    break;

  case 82: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 1100 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 4339 "src/parser.tab.c"
    break;

  case 83: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 1101 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 4345 "src/parser.tab.c"
    break;

  case 84: /* consider_body_statement: assignment NEWLINE  */
#line 1105 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4351 "src/parser.tab.c"
    break;

  case 85: /* consider_body_statement: print_statement NEWLINE  */
#line 1106 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4357 "src/parser.tab.c"
    break;

  case 86: /* consider_body_statement: call_statement NEWLINE  */
#line 1107 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4363 "src/parser.tab.c"
    break;

  case 87: /* consider_body_statement: with_lock_statement  */
#line 1108 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4369 "src/parser.tab.c"
    break;

  case 88: /* consider_body_statement: window_block  */
#line 1109 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4375 "src/parser.tab.c"
    break;

  case 89: /* consider_body_statement: for_each_statement  */
#line 1110 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4381 "src/parser.tab.c"
    break;

  case 90: /* consider_body_statement: while_statement  */
#line 1111 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4387 "src/parser.tab.c"
    break;

  case 91: /* consider_body_statement: do_loop_statement  */
#line 1112 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4393 "src/parser.tab.c"
    break;

  case 92: /* consider_body_statement: consider_statement  */
#line 1113 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4399 "src/parser.tab.c"
    break;

  case 93: /* consider_body_statement: function_statement  */
#line 1114 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4405 "src/parser.tab.c"
    break;

  case 94: /* consider_body_statement: modifier_statement  */
#line 1115 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4411 "src/parser.tab.c"
    break;

  case 95: /* consider_body_statement: program_statement  */
#line 1116 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4417 "src/parser.tab.c"
    break;

  case 96: /* consider_body_statement: library_statement  */
#line 1117 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4423 "src/parser.tab.c"
    break;

  case 97: /* consider_body_statement: use_statement NEWLINE  */
#line 1118 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4429 "src/parser.tab.c"
    break;

  case 98: /* consider_body_statement: watch_statement  */
#line 1119 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4435 "src/parser.tab.c"
    break;

  case 99: /* consider_body_statement: unwatch_statement NEWLINE  */
#line 1120 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4441 "src/parser.tab.c"
    break;

  case 100: /* consider_body_statement: without_watchers_statement  */
#line 1121 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4447 "src/parser.tab.c"
    break;

  case 101: /* consider_body_statement: on_error_statement NEWLINE  */
#line 1122 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4453 "src/parser.tab.c"
    break;

  case 102: /* consider_body_statement: error_statement NEWLINE  */
#line 1123 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4459 "src/parser.tab.c"
    break;

  case 103: /* consider_body_statement: return_statement NEWLINE  */
#line 1124 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4465 "src/parser.tab.c"
    break;

  case 104: /* consider_body_statement: label_statement NEWLINE  */
#line 1125 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4471 "src/parser.tab.c"
    break;

  case 105: /* consider_body_statement: goto_statement NEWLINE  */
#line 1126 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4477 "src/parser.tab.c"
    break;

  case 106: /* consider_body_statement: gosub_statement NEWLINE  */
#line 1127 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4483 "src/parser.tab.c"
    break;

  case 107: /* consider_body_statement: break_statement NEWLINE  */
#line 1128 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4489 "src/parser.tab.c"
    break;

  case 108: /* consider_body_statement: continue_statement NEWLINE  */
#line 1129 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4495 "src/parser.tab.c"
    break;

  case 109: /* consider_body_statement: if_statement  */
#line 1130 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4501 "src/parser.tab.c"
    break;

  case 110: /* consider_body_statement: DIM  */
#line 1136 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 4514 "src/parser.tab.c"
    break;

  case 111: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1147 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4522 "src/parser.tab.c"
    break;

  case 112: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1150 "src/parser.y"
                                                                                                            {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4531 "src/parser.tab.c"
    break;

  case 113: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 1157 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 4539 "src/parser.tab.c"
    break;

  case 114: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 1160 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 4547 "src/parser.tab.c"
    break;

  case 115: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 1166 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4555 "src/parser.tab.c"
    break;

  case 116: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 1172 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 4563 "src/parser.tab.c"
    break;

  case 117: /* use_statement: USE IDENT  */
#line 1178 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4569 "src/parser.tab.c"
    break;

  case 118: /* use_statement: LOAD IDENT  */
#line 1179 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4575 "src/parser.tab.c"
    break;

  case 119: /* use_statement: USE STRING  */
#line 1180 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4581 "src/parser.tab.c"
    break;

  case 120: /* use_statement: LOAD STRING  */
#line 1181 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4587 "src/parser.tab.c"
    break;

  case 121: /* use_statement: LOAD IDENT AS IDENT  */
#line 1182 "src/parser.y"
                          { (yyval.stmt) = ast_use((yyvsp[-2].text), NULL, (yyvsp[0].text)); }
#line 4593 "src/parser.tab.c"
    break;

  case 122: /* use_statement: USE IDENT IDENT STRING  */
#line 1183 "src/parser.y"
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
#line 4614 "src/parser.tab.c"
    break;

  case 123: /* use_statement: LOAD IDENT IDENT STRING  */
#line 1199 "src/parser.y"
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
#line 4635 "src/parser.tab.c"
    break;

  case 124: /* use_statement: LOAD IDENT IDENT STRING AS IDENT  */
#line 1215 "src/parser.y"
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
#line 4658 "src/parser.tab.c"
    break;

  case 125: /* modifier_signature: modifier_name  */
#line 1236 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 4664 "src/parser.tab.c"
    break;

  case 126: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 1237 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 4670 "src/parser.tab.c"
    break;

  case 127: /* modifier_context: IDENT  */
#line 1241 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4676 "src/parser.tab.c"
    break;

  case 128: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1245 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4684 "src/parser.tab.c"
    break;

  case 129: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 1248 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 4692 "src/parser.tab.c"
    break;

  case 130: /* watch_statement: WATCH IDENT LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1256 "src/parser.y"
                                                                                           {
        (yyval.stmt) = ast_watch((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4700 "src/parser.tab.c"
    break;

  case 131: /* unwatch_statement: UNWATCH expression  */
#line 1262 "src/parser.y"
                         { (yyval.stmt) = ast_unwatch((yyvsp[0].expr)); }
#line 4706 "src/parser.tab.c"
    break;

  case 132: /* watch_target_list: watch_target_path  */
#line 1266 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4712 "src/parser.tab.c"
    break;

  case 133: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 1267 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4718 "src/parser.tab.c"
    break;

  case 134: /* @2: %empty  */
#line 1287 "src/parser.y"
      { (yyval.stmt) = NULL;   /* the mid-rule carries no value; typed so bison stays quiet */
        server_head_note(ctx, (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column); }
#line 4725 "src/parser.tab.c"
    break;

  case 135: /* server_statement: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE @2 server_item_list END IDENT NEWLINE  */
#line 1289 "src/parser.y"
                                         {
        ctx->bad_block_word[0] = '\0';
        (yyval.stmt) = ast_server((yyvsp[-10].text), (yyvsp[-9].text), (yyvsp[-7].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4734 "src/parser.tab.c"
    break;

  case 136: /* @3: %empty  */
#line 1294 "src/parser.y"
      { (yyval.stmt) = NULL;   /* the mid-rule carries no value; typed so bison stays quiet */
        server_head_note(ctx, (yyvsp[-4].text), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4741 "src/parser.tab.c"
    break;

  case 137: /* server_statement: IDENT IDENT LPAREN RPAREN NEWLINE @3 server_item_list END IDENT NEWLINE  */
#line 1296 "src/parser.y"
                                         {
        ctx->bad_block_word[0] = '\0';
        (yyval.stmt) = ast_server((yyvsp[-9].text), (yyvsp[-8].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4750 "src/parser.tab.c"
    break;

  case 138: /* window_block: WINDOW_OPEN IDENT LPAREN record_field_list RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1312 "src/parser.y"
                                                                                                   {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), NULL, NULL, NULL, (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4759 "src/parser.tab.c"
    break;

  case 139: /* window_block: WINDOW_OPEN IDENT LPAREN RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1316 "src/parser.y"
                                                                                 {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text), NULL, NULL, NULL,
                        (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 4769 "src/parser.tab.c"
    break;

  case 140: /* window_block: WINDOW_OPEN IDENT LPAREN record_field_list RPAREN TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1326 "src/parser.y"
                                                                                                                  {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-11].text), (yyvsp[-9].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text),
                        (yylsp[-12]).first_line, (yylsp[-12]).first_column);
      }
#line 4779 "src/parser.tab.c"
    break;

  case 141: /* window_item_list: %empty  */
#line 1334 "src/parser.y"
             { (yyval.window_item_list) = ast_window_item_list_empty(); }
#line 4785 "src/parser.tab.c"
    break;

  case 142: /* window_item_list: window_item_list NEWLINE  */
#line 1335 "src/parser.y"
                               { (yyval.window_item_list) = (yyvsp[-1].window_item_list); }
#line 4791 "src/parser.tab.c"
    break;

  case 143: /* window_item_list: window_item_list window_item  */
#line 1336 "src/parser.y"
                                   { (yyval.window_item_list) = ast_window_item_list_append((yyvsp[-1].window_item_list), (yyvsp[0].window_item)); }
#line 4797 "src/parser.tab.c"
    break;

  case 144: /* window_item: IDENT LPAREN RPAREN NEWLINE  */
#line 1341 "src/parser.y"
                                  {
        (yyval.window_item) = ast_window_leaf((yyvsp[-3].text), NULL, ast_record_field_list_empty(),
                             (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 4806 "src/parser.tab.c"
    break;

  case 145: /* window_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE  */
#line 1346 "src/parser.y"
                                                          {
        (yyval.window_item) = ast_window_leaf((yyvsp[-5].text), (yyvsp[-4].text), (yyvsp[-2].record_field_list), (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 4814 "src/parser.tab.c"
    break;

  case 146: /* window_item: IDENT IDENT LPAREN RPAREN NEWLINE  */
#line 1350 "src/parser.y"
                                        {
        (yyval.window_item) = ast_window_leaf((yyvsp[-4].text), (yyvsp[-3].text), ast_record_field_list_empty(),
                             (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 4823 "src/parser.tab.c"
    break;

  case 147: /* window_item: IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1355 "src/parser.y"
                                                       {
        (yyval.window_item) = ast_window_container((yyvsp[-5].text), NULL, ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                  (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 4832 "src/parser.tab.c"
    break;

  case 148: /* window_item: IDENT LPAREN record_field_list RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1360 "src/parser.y"
                                                                                       {
        (yyval.window_item) = ast_window_container((yyvsp[-8].text), NULL, (yyvsp[-6].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 4840 "src/parser.tab.c"
    break;

  case 149: /* window_item: IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1374 "src/parser.y"
                                                             {
        (yyval.window_item) = ast_window_named_form((yyvsp[-6].text), (yyvsp[-5].text), NULL, NULL, NULL, (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                   (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 4849 "src/parser.tab.c"
    break;

  case 150: /* window_item: IDENT STRING NEWLINE window_item_list END IDENT NEWLINE  */
#line 1379 "src/parser.y"
                                                              {
        (yyval.window_item) = ast_window_container((yyvsp[-6].text), (yyvsp[-5].text), ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                  (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 4858 "src/parser.tab.c"
    break;

  case 151: /* window_item: IDENT TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1390 "src/parser.y"
                                                                      {
        (yyval.window_item) = ast_window_form((yyvsp[-8].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                             (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 4867 "src/parser.tab.c"
    break;

  case 152: /* window_item: IDENT IDENT TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1405 "src/parser.y"
                                                                            {
        (yyval.window_item) = ast_window_named_form((yyvsp[-9].text), (yyvsp[-8].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                   (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4876 "src/parser.tab.c"
    break;

  case 153: /* window_item: ON IDENT DOT IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1410 "src/parser.y"
                                                               {
        (yyval.window_item) = ast_window_handler((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 4884 "src/parser.tab.c"
    break;

  case 154: /* server_item_list: %empty  */
#line 1416 "src/parser.y"
             { (yyval.server_item_list) = ast_server_item_list_empty(); }
#line 4890 "src/parser.tab.c"
    break;

  case 155: /* server_item_list: server_item_list NEWLINE  */
#line 1417 "src/parser.y"
                               { (yyval.server_item_list) = (yyvsp[-1].server_item_list); }
#line 4896 "src/parser.tab.c"
    break;

  case 156: /* server_item_list: server_item_list server_item  */
#line 1418 "src/parser.y"
                                   { (yyval.server_item_list) = ast_server_item_list_append((yyvsp[-1].server_item_list), (yyvsp[0].server_item)); }
#line 4902 "src/parser.tab.c"
    break;

  case 157: /* server_item: IDENT server_string_list NEWLINE  */
#line 1422 "src/parser.y"
                                       {
        (yyval.server_item) = ast_server_directive((yyvsp[-2].text), (yyvsp[-1].name_list), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 4910 "src/parser.tab.c"
    break;

  case 158: /* server_item: IDENT STRING LPAREN parameter_list_opt RPAREN NEWLINE statement_list END IDENT NEWLINE  */
#line 1425 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_handler((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4918 "src/parser.tab.c"
    break;

  case 159: /* server_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1428 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_site((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4926 "src/parser.tab.c"
    break;

  case 160: /* server_item: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1431 "src/parser.y"
                                                                           {
        (yyval.server_item) = ast_server_site((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 4934 "src/parser.tab.c"
    break;

  case 161: /* server_item: ON IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1434 "src/parser.y"
                                                     {
        (yyval.server_item) = ast_server_hook((yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 4942 "src/parser.tab.c"
    break;

  case 162: /* server_string_list: STRING  */
#line 1440 "src/parser.y"
             { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4948 "src/parser.tab.c"
    break;

  case 163: /* server_string_list: server_string_list COMMA STRING  */
#line 1441 "src/parser.y"
                                      { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4954 "src/parser.tab.c"
    break;

  case 164: /* watch_target_path: variable_name  */
#line 1445 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 4960 "src/parser.tab.c"
    break;

  case 165: /* watch_target_path: watch_target_path DOT IDENT  */
#line 1446 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 4966 "src/parser.tab.c"
    break;

  case 166: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 1450 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 4974 "src/parser.tab.c"
    break;

  case 167: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 1456 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 4980 "src/parser.tab.c"
    break;

  case 168: /* on_error_statement: ON ERROR_VALUE GOTO NEXT  */
#line 1457 "src/parser.y"
                               { (yyval.stmt) = ast_on_error_goto_next(); }
#line 4986 "src/parser.tab.c"
    break;

  case 169: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 1458 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 4992 "src/parser.tab.c"
    break;

  case 170: /* on_error_statement: ON IDENT GOTO NEXT  */
#line 1459 "src/parser.y"
                         {
        if (!warn_channel_ok(ctx, (yyvsp[-2].text), (yylsp[-2]).first_line, (yylsp[-2]).first_column)) { YYERROR; }
        free((yyvsp[-2].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_NEXT);
      }
#line 5002 "src/parser.tab.c"
    break;

  case 171: /* on_error_statement: ON IDENT GOTO IDENT  */
#line 1464 "src/parser.y"
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
#line 5020 "src/parser.tab.c"
    break;

  case 172: /* on_error_statement: ON IDENT STOP  */
#line 1477 "src/parser.y"
                    {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_STOP);
      }
#line 5030 "src/parser.tab.c"
    break;

  case 173: /* on_error_statement: ON IDENT PRINT  */
#line 1482 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_PRINT);
      }
#line 5040 "src/parser.tab.c"
    break;

  case 174: /* on_error_statement: ON IDENT IDENT  */
#line 1487 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        int mode = warn_mode_word(ctx, (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column);
        if (mode < 0) { free((yyvsp[-1].text)); free((yyvsp[0].text)); YYERROR; }
        free((yyvsp[-1].text)); free((yyvsp[0].text));
        (yyval.stmt) = ast_on_warning(mode);
      }
#line 5052 "src/parser.tab.c"
    break;

  case 175: /* error_statement: ERROR_VALUE expression  */
#line 1497 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 5058 "src/parser.tab.c"
    break;

  case 176: /* return_statement: RETURN  */
#line 1501 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 5064 "src/parser.tab.c"
    break;

  case 177: /* return_statement: RETURN expression  */
#line 1502 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 5070 "src/parser.tab.c"
    break;

  case 178: /* label_statement: variable_name COLON  */
#line 1506 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 5076 "src/parser.tab.c"
    break;

  case 179: /* goto_statement: GOTO variable_name  */
#line 1513 "src/parser.y"
                         { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 5082 "src/parser.tab.c"
    break;

  case 180: /* gosub_statement: GOSUB variable_name  */
#line 1517 "src/parser.y"
                          { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 5088 "src/parser.tab.c"
    break;

  case 181: /* break_statement: BREAK  */
#line 1526 "src/parser.y"
            { (yyval.stmt) = ast_break(NULL); }
#line 5094 "src/parser.tab.c"
    break;

  case 182: /* break_statement: BREAK IDENT  */
#line 1527 "src/parser.y"
                  { (yyval.stmt) = ast_break((yyvsp[0].text)); }
#line 5100 "src/parser.tab.c"
    break;

  case 183: /* continue_statement: CONTINUE  */
#line 1531 "src/parser.y"
               { (yyval.stmt) = ast_continue(NULL); }
#line 5106 "src/parser.tab.c"
    break;

  case 184: /* continue_statement: CONTINUE IDENT  */
#line 1532 "src/parser.y"
                     { (yyval.stmt) = ast_continue((yyvsp[0].text)); }
#line 5112 "src/parser.tab.c"
    break;

  case 185: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1536 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 5121 "src/parser.tab.c"
    break;

  case 186: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1540 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 5130 "src/parser.tab.c"
    break;

  case 187: /* if_block_tail: END IF NEWLINE  */
#line 1547 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 5138 "src/parser.tab.c"
    break;

  case 188: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 1550 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 5146 "src/parser.tab.c"
    break;

  case 189: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1553 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 5154 "src/parser.tab.c"
    break;

  case 190: /* if_block_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1562 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5166 "src/parser.tab.c"
    break;

  case 191: /* if_inline_tail: %empty  */
#line 1572 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 5174 "src/parser.tab.c"
    break;

  case 192: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 1575 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 5182 "src/parser.tab.c"
    break;

  case 193: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1578 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 5190 "src/parser.tab.c"
    break;

  case 194: /* if_inline_tail: ELSE IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1584 "src/parser.y"
                                                                      {
        AstStmt *inner = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5202 "src/parser.tab.c"
    break;

  case 195: /* if_inline_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1591 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5214 "src/parser.tab.c"
    break;

  case 196: /* inline_statement: assignment  */
#line 1601 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5220 "src/parser.tab.c"
    break;

  case 197: /* inline_statement: print_statement  */
#line 1602 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5226 "src/parser.tab.c"
    break;

  case 198: /* inline_statement: call_statement  */
#line 1603 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5232 "src/parser.tab.c"
    break;

  case 199: /* inline_statement: use_statement  */
#line 1604 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5238 "src/parser.tab.c"
    break;

  case 200: /* inline_statement: on_error_statement  */
#line 1605 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5244 "src/parser.tab.c"
    break;

  case 201: /* inline_statement: error_statement  */
#line 1606 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5250 "src/parser.tab.c"
    break;

  case 202: /* inline_statement: return_statement  */
#line 1607 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5256 "src/parser.tab.c"
    break;

  case 203: /* inline_statement: goto_statement  */
#line 1608 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5262 "src/parser.tab.c"
    break;

  case 204: /* inline_statement: gosub_statement  */
#line 1609 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5268 "src/parser.tab.c"
    break;

  case 205: /* inline_statement: break_statement  */
#line 1610 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5274 "src/parser.tab.c"
    break;

  case 206: /* inline_statement: continue_statement  */
#line 1611 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5280 "src/parser.tab.c"
    break;

  case 207: /* expression: or_expression  */
#line 1615 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 5286 "src/parser.tab.c"
    break;

  case 208: /* or_expression: and_expression  */
#line 1619 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5292 "src/parser.tab.c"
    break;

  case 209: /* or_expression: or_expression OR and_expression  */
#line 1620 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5298 "src/parser.tab.c"
    break;

  case 210: /* and_expression: not_expression  */
#line 1624 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5304 "src/parser.tab.c"
    break;

  case 211: /* and_expression: and_expression AND not_expression  */
#line 1625 "src/parser.y"
                                        { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5310 "src/parser.tab.c"
    break;

  case 212: /* not_expression: comparison_expression  */
#line 1647 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 5316 "src/parser.tab.c"
    break;

  case 213: /* not_expression: NOT not_expression  */
#line 1648 "src/parser.y"
                         { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5322 "src/parser.tab.c"
    break;

  case 214: /* comparison_expression: set_expression  */
#line 1652 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5328 "src/parser.tab.c"
    break;

  case 215: /* comparison_expression: set_expression comparison_operator set_expression  */
#line 1653 "src/parser.y"
                                                        { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5334 "src/parser.tab.c"
    break;

  case 216: /* comparison_expression: set_expression comparison_lens comparison_operator set_expression  */
#line 1654 "src/parser.y"
                                                                        {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 5342 "src/parser.tab.c"
    break;

  case 217: /* set_expression: additive_expression  */
#line 1660 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 5348 "src/parser.tab.c"
    break;

  case 218: /* set_expression: set_expression EXCLUDING additive_expression  */
#line 1661 "src/parser.y"
                                                   { (yyval.expr) = expr_at(ast_binary(copy_const("excluding"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5354 "src/parser.tab.c"
    break;

  case 219: /* set_expression: set_expression INTERSECTING additive_expression  */
#line 1662 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("intersecting"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5360 "src/parser.tab.c"
    break;

  case 220: /* additive_expression: multiplicative_expression  */
#line 1666 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 5366 "src/parser.tab.c"
    break;

  case 221: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 1667 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5372 "src/parser.tab.c"
    break;

  case 222: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 1668 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5378 "src/parser.tab.c"
    break;

  case 223: /* multiplicative_expression: unary_expression  */
#line 1672 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 5384 "src/parser.tab.c"
    break;

  case 224: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 1673 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5390 "src/parser.tab.c"
    break;

  case 225: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 1674 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5396 "src/parser.tab.c"
    break;

  case 226: /* unary_expression: postfix_expression  */
#line 1678 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 5402 "src/parser.tab.c"
    break;

  case 227: /* unary_expression: MINUS unary_expression  */
#line 1679 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5408 "src/parser.tab.c"
    break;

  case 228: /* unary_expression: MODIFIER_PREFIX unary_expression  */
#line 1686 "src/parser.y"
                                       {
        (yyval.expr) = expr_at(ast_modifier_apply(parse_modifier_use((yyvsp[-1].text)), (yyvsp[0].expr)),
                     (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 5417 "src/parser.tab.c"
    break;

  case 229: /* unary_expression: NEW postfix_expression  */
#line 1690 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5423 "src/parser.tab.c"
    break;

  case 230: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 1691 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 5429 "src/parser.tab.c"
    break;

  case 231: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 1692 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5435 "src/parser.tab.c"
    break;

  case 232: /* postfix_expression: primary  */
#line 1696 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 5441 "src/parser.tab.c"
    break;

  case 233: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 1697 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5447 "src/parser.tab.c"
    break;

  case 234: /* postfix_expression: postfix_expression DOT dot_field_name  */
#line 1698 "src/parser.y"
                                            { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5453 "src/parser.tab.c"
    break;

  case 235: /* postfix_expression: postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1699 "src/parser.y"
                                                                   {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        (yyval.expr) = expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5463 "src/parser.tab.c"
    break;

  case 236: /* postfix_expression: postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1704 "src/parser.y"
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
#line 5479 "src/parser.tab.c"
    break;

  case 237: /* comparison_operator: OP_EQ  */
#line 1718 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 5485 "src/parser.tab.c"
    break;

  case 238: /* comparison_operator: OP_NE  */
#line 1719 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 5491 "src/parser.tab.c"
    break;

  case 239: /* comparison_operator: OP_GT  */
#line 1720 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 5497 "src/parser.tab.c"
    break;

  case 240: /* comparison_operator: OP_LT  */
#line 1721 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 5503 "src/parser.tab.c"
    break;

  case 241: /* comparison_operator: OP_GE  */
#line 1722 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 5509 "src/parser.tab.c"
    break;

  case 242: /* comparison_operator: OP_LE  */
#line 1723 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 5515 "src/parser.tab.c"
    break;

  case 243: /* comparison_operator: OP_NGT  */
#line 1724 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 5521 "src/parser.tab.c"
    break;

  case 244: /* comparison_operator: OP_NLT  */
#line 1725 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 5527 "src/parser.tab.c"
    break;

  case 245: /* comparison_operator: OP_NGE  */
#line 1726 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 5533 "src/parser.tab.c"
    break;

  case 246: /* comparison_operator: OP_NLE  */
#line 1727 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 5539 "src/parser.tab.c"
    break;

  case 247: /* primary: MODIFIER_VALUE  */
#line 1731 "src/parser.y"
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
#line 5562 "src/parser.tab.c"
    break;

  case 248: /* primary: NUMBER  */
#line 1749 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5568 "src/parser.tab.c"
    break;

  case 249: /* primary: WATCHERS LPAREN RPAREN  */
#line 1750 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_call(copy_const("watchers"), ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5574 "src/parser.tab.c"
    break;

  case 250: /* primary: duration_terms  */
#line 1751 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5580 "src/parser.tab.c"
    break;

  case 251: /* primary: STRING  */
#line 1752 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5586 "src/parser.tab.c"
    break;

  case 252: /* primary: variable_name ident_suffix  */
#line 1753 "src/parser.y"
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
#line 5608 "src/parser.tab.c"
    break;

  case 253: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1770 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 5619 "src/parser.tab.c"
    break;

  case 254: /* primary: ERROR_VALUE  */
#line 1776 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5625 "src/parser.tab.c"
    break;

  case 255: /* primary: TRUE  */
#line 1777 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5631 "src/parser.tab.c"
    break;

  case 256: /* primary: FALSE  */
#line 1778 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5637 "src/parser.tab.c"
    break;

  case 257: /* primary: NOTHING  */
#line 1779 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5643 "src/parser.tab.c"
    break;

  case 258: /* primary: UNKNOWN_VALUE  */
#line 1780 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5649 "src/parser.tab.c"
    break;

  case 259: /* primary: LPAREN expression RPAREN  */
#line 1781 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 5655 "src/parser.tab.c"
    break;

  case 260: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 1782 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5661 "src/parser.tab.c"
    break;

  case 261: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 1783 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5667 "src/parser.tab.c"
    break;

  case 262: /* primary: record_literal  */
#line 1784 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5673 "src/parser.tab.c"
    break;

  case 263: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 1788 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5679 "src/parser.tab.c"
    break;

  case 264: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 1789 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5685 "src/parser.tab.c"
    break;

  case 265: /* ident_suffix: %empty  */
#line 1793 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 5695 "src/parser.tab.c"
    break;

  case 266: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 1798 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5705 "src/parser.tab.c"
    break;

  case 267: /* ident_suffix: DOT dot_field_name ident_dot_suffix  */
#line 1803 "src/parser.y"
                                          {
        /* dot_field_name, not IDENT: a keyword is a legal FIELD name after a
         * dot, because nothing but a name can appear there. */
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 5716 "src/parser.tab.c"
    break;

  case 268: /* ident_suffix: DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1809 "src/parser.y"
                                                          {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        (yyval.ident_suffix).kind = IDENT_SUFFIX_METHOD;
        (yyval.ident_suffix).name = (yyvsp[-3].text);
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5730 "src/parser.tab.c"
    break;

  case 269: /* ident_dot_suffix: %empty  */
#line 1821 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 5740 "src/parser.tab.c"
    break;

  case 270: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 1826 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5750 "src/parser.tab.c"
    break;

  case 271: /* duration_terms: NUMBER IDENT  */
#line 1834 "src/parser.y"
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
#line 5765 "src/parser.tab.c"
    break;

  case 272: /* duration_terms: duration_terms NUMBER IDENT  */
#line 1844 "src/parser.y"
                                  {
        char *bad = NULL;
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].numlit).value, (yyvsp[0].text), &bad);
        if (bad) {
            duration_unit_error(ctx, bad, (yylsp[0]).first_line, (yylsp[0]).first_column,
                                (yylsp[0]).last_line, (yylsp[0]).last_column);
            YYERROR;
        }
      }
#line 5779 "src/parser.tab.c"
    break;

  case 273: /* argument_list_opt: %empty  */
#line 1856 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 5785 "src/parser.tab.c"
    break;

  case 274: /* argument_list_opt: argument_list  */
#line 1857 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 5791 "src/parser.tab.c"
    break;

  case 275: /* argument_list: expression  */
#line 1861 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 5797 "src/parser.tab.c"
    break;

  case 276: /* argument_list: argument_list COMMA expression  */
#line 1862 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 5803 "src/parser.tab.c"
    break;

  case 277: /* array_argument_list: expression  */
#line 1866 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 5809 "src/parser.tab.c"
    break;

  case 278: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 1867 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 5815 "src/parser.tab.c"
    break;

  case 279: /* parameter_list_opt: %empty  */
#line 1871 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 5821 "src/parser.tab.c"
    break;

  case 280: /* parameter_list_opt: parameter_list  */
#line 1872 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 5827 "src/parser.tab.c"
    break;

  case 281: /* parameter_default: NUMBER  */
#line 1886 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5833 "src/parser.tab.c"
    break;

  case 282: /* parameter_default: MINUS NUMBER  */
#line 1890 "src/parser.y"
                   { (yyval.expr) = expr_at(ast_number(-(yyvsp[0].numlit).value,
                                             (yyvsp[0].numlit).exact == LLONG_MIN ? 0 : -(yyvsp[0].numlit).exact,
                                             (yyvsp[0].numlit).exact == LLONG_MIN ? 0 : (yyvsp[0].numlit).is_exact),
                                  (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5842 "src/parser.tab.c"
    break;

  case 283: /* parameter_default: PLUS NUMBER  */
#line 1894 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5848 "src/parser.tab.c"
    break;

  case 284: /* parameter_default: STRING  */
#line 1895 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5854 "src/parser.tab.c"
    break;

  case 285: /* parameter_default: TRUE  */
#line 1896 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5860 "src/parser.tab.c"
    break;

  case 286: /* parameter_default: FALSE  */
#line 1897 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5866 "src/parser.tab.c"
    break;

  case 287: /* parameter_default: NOTHING  */
#line 1898 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5872 "src/parser.tab.c"
    break;

  case 288: /* parameter_default: UNKNOWN_VALUE  */
#line 1899 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5878 "src/parser.tab.c"
    break;

  case 289: /* parameter_list: IDENT  */
#line 1903 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5884 "src/parser.tab.c"
    break;

  case 290: /* parameter_list: IDENT OP_EQ parameter_default  */
#line 1904 "src/parser.y"
                                    {
        (yyval.name_list) = ast_name_list_append_default(ast_name_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 5892 "src/parser.tab.c"
    break;

  case 291: /* parameter_list: parameter_list COMMA IDENT  */
#line 1907 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5898 "src/parser.tab.c"
    break;

  case 292: /* parameter_list: parameter_list COMMA IDENT OP_EQ parameter_default  */
#line 1908 "src/parser.y"
                                                         {
        (yyval.name_list) = ast_name_list_append_default((yyvsp[-4].name_list), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 5906 "src/parser.tab.c"
    break;

  case 293: /* field_name: dot_field_name  */
#line 1923 "src/parser.y"
                     { (yyval.text) = (yyvsp[0].text); }
#line 5912 "src/parser.tab.c"
    break;

  case 294: /* field_name: STRING  */
#line 1930 "src/parser.y"
             { (yyval.text) = (yyvsp[0].text); }
#line 5918 "src/parser.tab.c"
    break;

  case 295: /* dot_field_name: IDENT  */
#line 1939 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 5924 "src/parser.tab.c"
    break;

  case 296: /* dot_field_name: AS  */
#line 1940 "src/parser.y"
                     { (yyval.text) = kw_name("as"); }
#line 5930 "src/parser.tab.c"
    break;

  case 297: /* dot_field_name: NEXT  */
#line 1941 "src/parser.y"
                     { (yyval.text) = kw_name("next"); }
#line 5936 "src/parser.tab.c"
    break;

  case 298: /* dot_field_name: STOP  */
#line 1942 "src/parser.y"
                     { (yyval.text) = kw_name("stop"); }
#line 5942 "src/parser.tab.c"
    break;

  case 299: /* dot_field_name: ERROR_VALUE  */
#line 1943 "src/parser.y"
                     { (yyval.text) = kw_name("error"); }
#line 5948 "src/parser.tab.c"
    break;

  case 300: /* dot_field_name: END  */
#line 1944 "src/parser.y"
                     { (yyval.text) = kw_name("end"); }
#line 5954 "src/parser.tab.c"
    break;

  case 301: /* dot_field_name: TO  */
#line 1945 "src/parser.y"
                     { (yyval.text) = kw_name("to"); }
#line 5960 "src/parser.tab.c"
    break;

  case 302: /* dot_field_name: IN  */
#line 1946 "src/parser.y"
                     { (yyval.text) = kw_name("in"); }
#line 5966 "src/parser.tab.c"
    break;

  case 303: /* dot_field_name: ON  */
#line 1947 "src/parser.y"
                     { (yyval.text) = kw_name("on"); }
#line 5972 "src/parser.tab.c"
    break;

  case 304: /* dot_field_name: NEW  */
#line 1948 "src/parser.y"
                     { (yyval.text) = kw_name("new"); }
#line 5978 "src/parser.tab.c"
    break;

  case 305: /* dot_field_name: EACH  */
#line 1949 "src/parser.y"
                     { (yyval.text) = kw_name("each"); }
#line 5984 "src/parser.tab.c"
    break;

  case 306: /* dot_field_name: WITH  */
#line 1950 "src/parser.y"
                     { (yyval.text) = kw_name("with"); }
#line 5990 "src/parser.tab.c"
    break;

  case 307: /* dot_field_name: WITHOUT  */
#line 1951 "src/parser.y"
                     { (yyval.text) = kw_name("without"); }
#line 5996 "src/parser.tab.c"
    break;

  case 308: /* dot_field_name: THEN  */
#line 1952 "src/parser.y"
                     { (yyval.text) = kw_name("then"); }
#line 6002 "src/parser.tab.c"
    break;

  case 309: /* dot_field_name: ELSE  */
#line 1953 "src/parser.y"
                     { (yyval.text) = kw_name("else"); }
#line 6008 "src/parser.tab.c"
    break;

  case 310: /* dot_field_name: FOR  */
#line 1954 "src/parser.y"
                     { (yyval.text) = kw_name("for"); }
#line 6014 "src/parser.tab.c"
    break;

  case 311: /* dot_field_name: IF  */
#line 1955 "src/parser.y"
                     { (yyval.text) = kw_name("if"); }
#line 6020 "src/parser.tab.c"
    break;

  case 312: /* dot_field_name: WHILE  */
#line 1956 "src/parser.y"
                     { (yyval.text) = kw_name("while"); }
#line 6026 "src/parser.tab.c"
    break;

  case 313: /* dot_field_name: DO  */
#line 1957 "src/parser.y"
                     { (yyval.text) = kw_name("do"); }
#line 6032 "src/parser.tab.c"
    break;

  case 314: /* dot_field_name: UNTIL  */
#line 1958 "src/parser.y"
                     { (yyval.text) = kw_name("until"); }
#line 6038 "src/parser.tab.c"
    break;

  case 315: /* dot_field_name: PRINT  */
#line 1959 "src/parser.y"
                     { (yyval.text) = kw_name("print"); }
#line 6044 "src/parser.tab.c"
    break;

  case 316: /* dot_field_name: RETURN  */
#line 1960 "src/parser.y"
                     { (yyval.text) = kw_name("return"); }
#line 6050 "src/parser.tab.c"
    break;

  case 317: /* dot_field_name: LOAD  */
#line 1961 "src/parser.y"
                     { (yyval.text) = kw_name("load"); }
#line 6056 "src/parser.tab.c"
    break;

  case 318: /* dot_field_name: USE  */
#line 1962 "src/parser.y"
                     { (yyval.text) = kw_name("use"); }
#line 6062 "src/parser.tab.c"
    break;

  case 319: /* dot_field_name: NOT  */
#line 1963 "src/parser.y"
                     { (yyval.text) = kw_name("not"); }
#line 6068 "src/parser.tab.c"
    break;

  case 320: /* dot_field_name: AND  */
#line 1964 "src/parser.y"
                     { (yyval.text) = kw_name("and"); }
#line 6074 "src/parser.tab.c"
    break;

  case 321: /* dot_field_name: OR  */
#line 1965 "src/parser.y"
                     { (yyval.text) = kw_name("or"); }
#line 6080 "src/parser.tab.c"
    break;

  case 322: /* dot_field_name: TRUE  */
#line 1966 "src/parser.y"
                     { (yyval.text) = kw_name("true"); }
#line 6086 "src/parser.tab.c"
    break;

  case 323: /* dot_field_name: FALSE  */
#line 1967 "src/parser.y"
                     { (yyval.text) = kw_name("false"); }
#line 6092 "src/parser.tab.c"
    break;

  case 324: /* dot_field_name: NOTHING  */
#line 1968 "src/parser.y"
                     { (yyval.text) = kw_name("nothing"); }
#line 6098 "src/parser.tab.c"
    break;

  case 325: /* dot_field_name: BREAK  */
#line 1969 "src/parser.y"
                     { (yyval.text) = kw_name("break"); }
#line 6104 "src/parser.tab.c"
    break;

  case 326: /* dot_field_name: CONTINUE  */
#line 1970 "src/parser.y"
                     { (yyval.text) = kw_name("continue"); }
#line 6110 "src/parser.tab.c"
    break;

  case 327: /* dot_field_name: GOTO  */
#line 1971 "src/parser.y"
                     { (yyval.text) = kw_name("goto"); }
#line 6116 "src/parser.tab.c"
    break;

  case 328: /* dot_field_name: GOSUB  */
#line 1972 "src/parser.y"
                     { (yyval.text) = kw_name("gosub"); }
#line 6122 "src/parser.tab.c"
    break;

  case 329: /* dot_field_name: SPAWN  */
#line 1973 "src/parser.y"
                     { (yyval.text) = kw_name("spawn"); }
#line 6128 "src/parser.tab.c"
    break;

  case 330: /* dot_field_name: EXPORT  */
#line 1974 "src/parser.y"
                     { (yyval.text) = kw_name("export"); }
#line 6134 "src/parser.tab.c"
    break;

  case 331: /* dot_field_name: LIBRARY  */
#line 1975 "src/parser.y"
                     { (yyval.text) = kw_name("library"); }
#line 6140 "src/parser.tab.c"
    break;

  case 332: /* dot_field_name: FUNCTION  */
#line 1976 "src/parser.y"
                     { (yyval.text) = kw_name("function"); }
#line 6146 "src/parser.tab.c"
    break;

  case 333: /* dot_field_name: MODIFIER  */
#line 1977 "src/parser.y"
                     { (yyval.text) = kw_name("modifier"); }
#line 6152 "src/parser.tab.c"
    break;

  case 334: /* dot_field_name: PROGRAM  */
#line 1978 "src/parser.y"
                     { (yyval.text) = kw_name("program"); }
#line 6158 "src/parser.tab.c"
    break;

  case 335: /* dot_field_name: WATCH  */
#line 1979 "src/parser.y"
                     { (yyval.text) = kw_name("watch"); }
#line 6164 "src/parser.tab.c"
    break;

  case 336: /* dot_field_name: WATCHERS  */
#line 1980 "src/parser.y"
                     { (yyval.text) = kw_name("watchers"); }
#line 6170 "src/parser.tab.c"
    break;

  case 337: /* dot_field_name: CONSIDER  */
#line 1981 "src/parser.y"
                     { (yyval.text) = kw_name("consider"); }
#line 6176 "src/parser.tab.c"
    break;

  case 338: /* dot_field_name: STEP  */
#line 1982 "src/parser.y"
                     { (yyval.text) = kw_name("step"); }
#line 6182 "src/parser.tab.c"
    break;

  case 339: /* dot_field_name: UNWATCH  */
#line 1983 "src/parser.y"
                     { (yyval.text) = kw_name("unwatch"); }
#line 6188 "src/parser.tab.c"
    break;

  case 340: /* dot_field_name: UNKNOWN_VALUE  */
#line 1984 "src/parser.y"
                     { (yyval.text) = kw_name("unknown"); }
#line 6194 "src/parser.tab.c"
    break;

  case 341: /* dot_field_name: DIM  */
#line 1985 "src/parser.y"
                     { (yyval.text) = kw_name("dim"); }
#line 6200 "src/parser.tab.c"
    break;

  case 342: /* record_field_list: field_name OP_EQ expression  */
#line 1989 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6206 "src/parser.tab.c"
    break;

  case 343: /* record_field_list: field_name COLON expression  */
#line 1990 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6212 "src/parser.tab.c"
    break;

  case 344: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1991 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 6218 "src/parser.tab.c"
    break;

  case 345: /* record_field_list: record_field_list COMMA optional_newlines field_name OP_EQ expression  */
#line 1992 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6224 "src/parser.tab.c"
    break;

  case 346: /* record_field_list: record_field_list COMMA optional_newlines field_name COLON expression  */
#line 1993 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6230 "src/parser.tab.c"
    break;

  case 347: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1994 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 6236 "src/parser.tab.c"
    break;

  case 348: /* field_policy: IDENT  */
#line 2002 "src/parser.y"
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
#line 6268 "src/parser.tab.c"
    break;

  case 349: /* field_policy: IDENT expression  */
#line 2029 "src/parser.y"
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
#line 6289 "src/parser.tab.c"
    break;


#line 6293 "src/parser.tab.c"

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

#line 2052 "src/parser.y"


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
    if (n != 3) {
        return NULL;
    }
    char word[4];
    for (size_t i = 0; i < 3; i++) {
        char c = w[i];
        word[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    word[3] = '\0';
    while (*p == ' ' || *p == '\t') {
        p++;
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
