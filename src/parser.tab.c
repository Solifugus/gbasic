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
  YYSYMBOL_WITH = 37,                      /* WITH  */
  YYSYMBOL_NEW = 38,                       /* NEW  */
  YYSYMBOL_SPAWN = 39,                     /* SPAWN  */
  YYSYMBOL_FOR = 40,                       /* FOR  */
  YYSYMBOL_TO = 41,                        /* TO  */
  YYSYMBOL_STEP = 42,                      /* STEP  */
  YYSYMBOL_DO = 43,                        /* DO  */
  YYSYMBOL_UNTIL = 44,                     /* UNTIL  */
  YYSYMBOL_IN = 45,                        /* IN  */
  YYSYMBOL_EACH = 46,                      /* EACH  */
  YYSYMBOL_WHILE = 47,                     /* WHILE  */
  YYSYMBOL_CONSIDER = 48,                  /* CONSIDER  */
  YYSYMBOL_BREAK = 49,                     /* BREAK  */
  YYSYMBOL_CONTINUE = 50,                  /* CONTINUE  */
  YYSYMBOL_FUNCTION = 51,                  /* FUNCTION  */
  YYSYMBOL_RETURN = 52,                    /* RETURN  */
  YYSYMBOL_GOTO = 53,                      /* GOTO  */
  YYSYMBOL_GOSUB = 54,                     /* GOSUB  */
  YYSYMBOL_WATCH = 55,                     /* WATCH  */
  YYSYMBOL_UNWATCH = 56,                   /* UNWATCH  */
  YYSYMBOL_WITHOUT = 57,                   /* WITHOUT  */
  YYSYMBOL_WATCHERS = 58,                  /* WATCHERS  */
  YYSYMBOL_ON = 59,                        /* ON  */
  YYSYMBOL_NEXT = 60,                      /* NEXT  */
  YYSYMBOL_STOP = 61,                      /* STOP  */
  YYSYMBOL_ERROR_VALUE = 62,               /* ERROR_VALUE  */
  YYSYMBOL_MODIFIER = 63,                  /* MODIFIER  */
  YYSYMBOL_PROGRAM = 64,                   /* PROGRAM  */
  YYSYMBOL_LIBRARY = 65,                   /* LIBRARY  */
  YYSYMBOL_LOAD = 66,                      /* LOAD  */
  YYSYMBOL_USE = 67,                       /* USE  */
  YYSYMBOL_EXPORT = 68,                    /* EXPORT  */
  YYSYMBOL_OP_EQ = 69,                     /* OP_EQ  */
  YYSYMBOL_OP_NE = 70,                     /* OP_NE  */
  YYSYMBOL_OP_GT = 71,                     /* OP_GT  */
  YYSYMBOL_OP_LT = 72,                     /* OP_LT  */
  YYSYMBOL_OP_GE = 73,                     /* OP_GE  */
  YYSYMBOL_OP_LE = 74,                     /* OP_LE  */
  YYSYMBOL_OP_NGT = 75,                    /* OP_NGT  */
  YYSYMBOL_OP_NLT = 76,                    /* OP_NLT  */
  YYSYMBOL_OP_NGE = 77,                    /* OP_NGE  */
  YYSYMBOL_OP_NLE = 78,                    /* OP_NLE  */
  YYSYMBOL_PLUS = 79,                      /* PLUS  */
  YYSYMBOL_MINUS = 80,                     /* MINUS  */
  YYSYMBOL_STAR = 81,                      /* STAR  */
  YYSYMBOL_SLASH = 82,                     /* SLASH  */
  YYSYMBOL_LPAREN = 83,                    /* LPAREN  */
  YYSYMBOL_RPAREN = 84,                    /* RPAREN  */
  YYSYMBOL_LBRACKET = 85,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 86,                  /* RBRACKET  */
  YYSYMBOL_LBRACE = 87,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 88,                    /* RBRACE  */
  YYSYMBOL_COMMA = 89,                     /* COMMA  */
  YYSYMBOL_COLON = 90,                     /* COLON  */
  YYSYMBOL_NEWLINE = 91,                   /* NEWLINE  */
  YYSYMBOL_IF_WITHOUT_ELSE = 92,           /* IF_WITHOUT_ELSE  */
  YYSYMBOL_NO_DOT = 93,                    /* NO_DOT  */
  YYSYMBOL_DOT = 94,                       /* DOT  */
  YYSYMBOL_YYACCEPT = 95,                  /* $accept  */
  YYSYMBOL_program = 96,                   /* program  */
  YYSYMBOL_statement_list = 97,            /* statement_list  */
  YYSYMBOL_statement = 98,                 /* statement  */
  YYSYMBOL_assignment = 99,                /* assignment  */
  YYSYMBOL_compound_op = 100,              /* compound_op  */
  YYSYMBOL_lvalue = 101,                   /* lvalue  */
  YYSYMBOL_variable_name = 102,            /* variable_name  */
  YYSYMBOL_comparison_lens = 103,          /* comparison_lens  */
  YYSYMBOL_104_1 = 104,                    /* $@1  */
  YYSYMBOL_modifier_name = 105,            /* modifier_name  */
  YYSYMBOL_modifier_word = 106,            /* modifier_word  */
  YYSYMBOL_print_statement = 107,          /* print_statement  */
  YYSYMBOL_call_statement = 108,           /* call_statement  */
  YYSYMBOL_with_lock_statement = 109,      /* with_lock_statement  */
  YYSYMBOL_for_end = 110,                  /* for_end  */
  YYSYMBOL_for_each_statement = 111,       /* for_each_statement  */
  YYSYMBOL_do_loop_statement = 112,        /* do_loop_statement  */
  YYSYMBOL_while_statement = 113,          /* while_statement  */
  YYSYMBOL_consider_statement = 114,       /* consider_statement  */
  YYSYMBOL_consider_branch_list = 115,     /* consider_branch_list  */
  YYSYMBOL_consider_else_opt = 116,        /* consider_else_opt  */
  YYSYMBOL_consider_statement_list = 117,  /* consider_statement_list  */
  YYSYMBOL_consider_body_statement = 118,  /* consider_body_statement  */
  YYSYMBOL_function_statement = 119,       /* function_statement  */
  YYSYMBOL_modifier_statement = 120,       /* modifier_statement  */
  YYSYMBOL_program_statement = 121,        /* program_statement  */
  YYSYMBOL_library_statement = 122,        /* library_statement  */
  YYSYMBOL_use_statement = 123,            /* use_statement  */
  YYSYMBOL_modifier_signature = 124,       /* modifier_signature  */
  YYSYMBOL_modifier_context = 125,         /* modifier_context  */
  YYSYMBOL_watch_statement = 126,          /* watch_statement  */
  YYSYMBOL_unwatch_statement = 127,        /* unwatch_statement  */
  YYSYMBOL_watch_target_list = 128,        /* watch_target_list  */
  YYSYMBOL_server_statement = 129,         /* server_statement  */
  YYSYMBOL_130_2 = 130,                    /* @2  */
  YYSYMBOL_131_3 = 131,                    /* @3  */
  YYSYMBOL_gsql_block = 132,               /* gsql_block  */
  YYSYMBOL_window_block = 133,             /* window_block  */
  YYSYMBOL_window_item_list = 134,         /* window_item_list  */
  YYSYMBOL_window_item = 135,              /* window_item  */
  YYSYMBOL_server_item_list = 136,         /* server_item_list  */
  YYSYMBOL_server_item = 137,              /* server_item  */
  YYSYMBOL_server_string_list = 138,       /* server_string_list  */
  YYSYMBOL_watch_target_path = 139,        /* watch_target_path  */
  YYSYMBOL_without_watchers_statement = 140, /* without_watchers_statement  */
  YYSYMBOL_on_error_statement = 141,       /* on_error_statement  */
  YYSYMBOL_error_statement = 142,          /* error_statement  */
  YYSYMBOL_return_statement = 143,         /* return_statement  */
  YYSYMBOL_label_statement = 144,          /* label_statement  */
  YYSYMBOL_goto_statement = 145,           /* goto_statement  */
  YYSYMBOL_gosub_statement = 146,          /* gosub_statement  */
  YYSYMBOL_break_statement = 147,          /* break_statement  */
  YYSYMBOL_continue_statement = 148,       /* continue_statement  */
  YYSYMBOL_if_statement = 149,             /* if_statement  */
  YYSYMBOL_if_block_tail = 150,            /* if_block_tail  */
  YYSYMBOL_if_inline_tail = 151,           /* if_inline_tail  */
  YYSYMBOL_inline_statement = 152,         /* inline_statement  */
  YYSYMBOL_expression = 153,               /* expression  */
  YYSYMBOL_or_expression = 154,            /* or_expression  */
  YYSYMBOL_and_expression = 155,           /* and_expression  */
  YYSYMBOL_not_expression = 156,           /* not_expression  */
  YYSYMBOL_comparison_expression = 157,    /* comparison_expression  */
  YYSYMBOL_set_expression = 158,           /* set_expression  */
  YYSYMBOL_additive_expression = 159,      /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 160, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 161,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 162,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 163,      /* comparison_operator  */
  YYSYMBOL_primary = 164,                  /* primary  */
  YYSYMBOL_record_literal = 165,           /* record_literal  */
  YYSYMBOL_ident_suffix = 166,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 167,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 168,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 169,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 170,            /* argument_list  */
  YYSYMBOL_array_argument_list = 171,      /* array_argument_list  */
  YYSYMBOL_parameter_list_opt = 172,       /* parameter_list_opt  */
  YYSYMBOL_parameter_default = 173,        /* parameter_default  */
  YYSYMBOL_parameter_list = 174,           /* parameter_list  */
  YYSYMBOL_field_name = 175,               /* field_name  */
  YYSYMBOL_dot_field_name = 176,           /* dot_field_name  */
  YYSYMBOL_record_field_list = 177,        /* record_field_list  */
  YYSYMBOL_field_policy = 178,             /* field_policy  */
  YYSYMBOL_optional_newlines = 179         /* optional_newlines  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;



/* Unqualified %code blocks.  */
#line 712 "src/parser.y"

static int yylex(YYSTYPE *lvalp, YYLTYPE *llocp, gb_parse_ctx *ctx);
static void yyerror(YYLTYPE *llocp, gb_parse_ctx *ctx, const char *message);
static void report_syntax_error(gb_parse_ctx *ctx, int line, int column,
                                int end_line, int end_column, const char *message);

#line 919 "src/parser.tab.c"

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
#define YYLAST   3288

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  95
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  85
/* YYNRULES -- Number of rules.  */
#define YYNRULES  356
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  819

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   349


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
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   800,   800,   804,   805,   806,   810,   811,   812,   813,
     814,   815,   816,   817,   818,   819,   820,   821,   822,   823,
     824,   825,   826,   827,   828,   829,   830,   831,   832,   833,
     834,   835,   836,   837,   843,   854,   859,   860,   872,   884,
     885,   886,   887,   891,   892,   893,   897,   898,   899,   910,
     910,   918,   922,   923,   927,   928,   929,   930,   934,   940,
     944,   945,   951,   956,   965,   978,  1007,  1008,  1009,  1013,
    1017,  1033,  1038,  1046,  1050,  1071,  1077,  1083,  1089,  1092,
    1098,  1099,  1103,  1104,  1105,  1109,  1110,  1111,  1112,  1113,
    1114,  1115,  1116,  1117,  1118,  1119,  1120,  1121,  1122,  1123,
    1124,  1125,  1126,  1127,  1128,  1129,  1130,  1131,  1132,  1133,
    1134,  1140,  1151,  1154,  1161,  1164,  1170,  1176,  1182,  1183,
    1184,  1185,  1186,  1187,  1203,  1219,  1240,  1241,  1245,  1249,
    1252,  1260,  1266,  1270,  1271,  1291,  1290,  1298,  1297,  1320,
    1323,  1326,  1333,  1337,  1347,  1355,  1356,  1357,  1362,  1367,
    1371,  1376,  1381,  1395,  1410,  1415,  1426,  1441,  1446,  1452,
    1453,  1454,  1458,  1461,  1464,  1467,  1470,  1476,  1477,  1481,
    1482,  1486,  1492,  1493,  1494,  1495,  1500,  1513,  1518,  1523,
    1533,  1537,  1538,  1542,  1549,  1553,  1562,  1563,  1567,  1568,
    1572,  1576,  1583,  1586,  1589,  1598,  1608,  1611,  1614,  1620,
    1627,  1637,  1638,  1639,  1640,  1641,  1642,  1643,  1644,  1645,
    1646,  1647,  1651,  1655,  1656,  1660,  1661,  1683,  1684,  1688,
    1689,  1690,  1696,  1697,  1698,  1702,  1703,  1704,  1708,  1709,
    1710,  1714,  1715,  1722,  1726,  1727,  1728,  1732,  1733,  1734,
    1735,  1740,  1754,  1755,  1756,  1757,  1758,  1759,  1760,  1761,
    1762,  1763,  1767,  1785,  1786,  1787,  1788,  1789,  1806,  1812,
    1813,  1814,  1815,  1816,  1817,  1818,  1819,  1820,  1824,  1825,
    1829,  1834,  1839,  1845,  1857,  1862,  1870,  1880,  1892,  1893,
    1897,  1898,  1902,  1903,  1907,  1908,  1922,  1926,  1930,  1931,
    1932,  1933,  1934,  1935,  1939,  1940,  1943,  1944,  1959,  1966,
    1975,  1976,  1977,  1978,  1979,  1980,  1981,  1982,  1983,  1984,
    1985,  1986,  1987,  1988,  1989,  1990,  1991,  1992,  1993,  1994,
    1995,  1996,  1997,  1998,  1999,  2000,  2001,  2002,  2003,  2004,
    2005,  2006,  2007,  2008,  2009,  2010,  2011,  2012,  2013,  2014,
    2015,  2016,  2017,  2018,  2019,  2020,  2021,  2025,  2026,  2027,
    2028,  2029,  2030,  2038,  2065,  2084,  2085
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
  "UNKNOWN_VALUE", "AND", "OR", "NOT", "WITH", "NEW", "SPAWN", "FOR", "TO",
  "STEP", "DO", "UNTIL", "IN", "EACH", "WHILE", "CONSIDER", "BREAK",
  "CONTINUE", "FUNCTION", "RETURN", "GOTO", "GOSUB", "WATCH", "UNWATCH",
  "WITHOUT", "WATCHERS", "ON", "NEXT", "STOP", "ERROR_VALUE", "MODIFIER",
  "PROGRAM", "LIBRARY", "LOAD", "USE", "EXPORT", "OP_EQ", "OP_NE", "OP_GT",
  "OP_LT", "OP_GE", "OP_LE", "OP_NGT", "OP_NLT", "OP_NGE", "OP_NLE",
  "PLUS", "MINUS", "STAR", "SLASH", "LPAREN", "RPAREN", "LBRACKET",
  "RBRACKET", "LBRACE", "RBRACE", "COMMA", "COLON", "NEWLINE",
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

#define YYPACT_NINF (-548)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -548,    71,  1058,  -548,    23,     4,    96,   100,   128,  -548,
    2494,  -548,  2455,   146,    95,    74,  2494,  2494,   174,   210,
     287,  2494,   248,   248,   103,  2494,   172,    58,  -548,   614,
     334,   230,   237,   219,   351,   215,  -548,  -548,   177,   571,
     220,   226,   238,  -548,  -548,  -548,  -548,  -548,  -548,  -548,
    -548,  -548,   249,  -548,   266,  -548,  -548,  -548,  -548,   274,
     277,   283,   285,   289,   291,   299,   306,  -548,   179,  2494,
    2494,   316,   318,   223,   317,  -548,  -548,   323,  2555,  -548,
    -548,  -548,  -548,  -548,  2494,  2540,   339,   324,  -548,  2555,
    2494,  -548,  -548,   108,   386,   376,   379,  -548,  -548,  3015,
     280,   288,  -548,   -20,  -548,  -548,   417,   371,  -548,   352,
      16,   418,  -548,   343,   349,  -548,  -548,   358,   359,  -548,
    -548,  -548,   361,   248,  -548,   121,   353,  -548,   355,   252,
     205,   446,  -548,  -548,  -548,  -548,  -548,   232,  -548,   408,
     369,   362,   192,  -548,   453,  -548,   334,  -548,  -548,  -548,
    -548,  -548,  -548,  2494,  2494,  -548,  3090,  2494,   444,  -548,
    -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,
    -548,  -548,  2629,  -548,   382,   378,   385,  2694,  2759,   466,
    -548,  2494,  -548,  -548,   157,   389,   391,  -548,   393,   708,
     798,  2494,  3155,  -548,  2366,  2494,  2494,  2555,  2555,  -548,
    -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,  2378,
    2555,  2555,  2555,  2555,  2555,  2494,  3220,   469,  2494,  2494,
    2494,  2494,   475,    44,   928,  -548,   458,   478,   478,   248,
      85,   248,  -548,   480,  -548,  -548,  -548,    48,  -548,    69,
    -548,   402,   478,  -548,   482,   478,  -548,   484,   483,   486,
     456,  -548,   411,   493,   423,   424,  -548,  -548,  -548,  -548,
    -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,
    -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,
    -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,
    -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,
    -548,  -548,  -548,  -548,  2494,  2494,   426,  -548,   409,   114,
    -548,   124,  -548,  2494,  -548,   410,   156,   419,   171,   420,
     430,   431,  2494,  -548,  -548,  -548,  -548,  -548,   434,  -548,
     435,   441,  -548,   443,   448,   449,  -548,  -548,  -548,  -548,
    -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,   436,
     379,  -548,   280,   280,  2555,   342,   288,   288,  -548,  -548,
     450,   452,   454,  -548,  -548,  -548,   460,   447,   499,   500,
    2494,   542,  2494,  1123,  2494,   281,   485,   463,   464,   471,
     181,   467,   353,  1188,  -548,  1253,  -548,  -548,  -548,  -548,
    2494,   473,  -548,   468,   476,  1318,   548,  -548,  -548,   482,
    -548,   481,  2494,  2494,  -548,  -548,   559,  -548,  2494,  2494,
     487,  -548,  -548,  -548,    25,  -548,   489,   551,  -548,  -548,
     488,  -548,   183,   221,  -548,  2494,  2494,  -548,   993,   541,
     342,  -548,  2494,  2494,   490,  -548,  2494,  2494,   491,   526,
     494,   529,   553,  2494,   501,   556,    81,   502,   587,   503,
     504,  -548,   543,   539,   515,  -548,  -548,   509,   537,   599,
     513,  -548,   521,   523,  2494,   527,  -548,  -548,  -548,  -548,
     863,    51,   606,  -548,  -548,   586,  -548,   724,  -548,  -548,
     536,   540,   166,   603,  -548,  2347,  -548,   544,   545,  -548,
    1383,    40,   535,  -548,  2494,  -548,   546,   547,   607,  -548,
     552,  -548,  -548,  -548,  -548,  -548,  -548,   624,   627,  -548,
    -548,   564,  -548,  -548,  1448,   557,   558,  -548,  1513,  -548,
     560,  -548,  -548,  -548,  -548,  -548,   549,    64,  -548,   572,
     115,    55,   631,   632,  -548,  -548,   638,    99,   650,  -548,
    -548,  -548,  2494,  -548,   566,   568,  2494,  -548,   569,  -548,
    -548,  1578,   622,    68,  -548,  2494,  -548,  -548,  1383,   573,
    -548,  -548,   575,  2293,  -548,  -548,  -548,  1643,    81,  1708,
    1773,   608,  -548,  -548,   604,  1838,  -548,  1903,  2494,   368,
     664,   665,  -548,  -548,   118,   559,  2494,  2494,    26,   579,
     667,  2824,  -548,   582,   581,   588,   674,   589,   657,  1968,
    -548,  -548,   658,  2033,  -548,   646,   593,  -548,   596,   597,
    1383,  1383,  -548,  -548,  2293,  -548,  -548,  -548,   600,   601,
     602,  -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,
    -548,   605,  -548,   609,  -548,  -548,   611,   612,   613,   615,
     616,   618,   619,   623,  -548,   639,  -548,   644,   643,   628,
     629,   641,   655,  -548,   640,   642,   204,   630,   633,   718,
     652,  -548,  -548,   635,   726,  2889,  -548,  -548,   733,   651,
     187,   120,  -548,   739,  -548,   659,  -548,   661,   727,  2417,
     731,   673,  -548,  -548,  -548,  -548,  -548,  1383,  2293,  -548,
    -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,  -548,
    -548,  -548,   676,   678,   680,  -548,  -548,   681,   682,  2954,
     478,   743,  -548,  -548,  -548,   683,   668,  -548,   761,   684,
     188,   176,   194,   685,  -548,   688,   776,   690,   227,  -548,
    -548,   692,  -548,   694,   696,  -548,  1383,  -548,  -548,  -548,
    -548,  -548,  -548,   698,   199,   706,  -548,  2098,  -548,  2494,
     257,   701,  -548,   705,   794,   796,  -548,  -548,   710,  -548,
     801,   993,  -548,   993,   541,  -548,  -548,  -548,   715,   717,
     751,  -548,   810,  -548,  -548,   725,   728,   260,   275,  -548,
    2163,   730,  -548,  -548,  -548,   276,  -548,  -548,   735,   778,
     327,  -548,  -548,   813,   814,   765,  -548,   866,   360,  2228,
    -548,  -548,   867,   781,   782,   783,   784,   872,   875,   789,
    -548,  -548,  -548,  -548,   790,   791,  -548,  -548,  -548
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       3,     0,     2,     1,    46,     0,     0,     0,     0,    34,
       0,    47,     0,     0,     0,     0,     0,     0,   186,   188,
       0,   181,     0,     0,     0,     0,     0,     0,    48,     0,
       0,     0,     0,     0,     0,     0,     4,     5,     0,     0,
      43,     0,     0,     9,    12,    14,    13,    15,    16,    17,
      18,    19,     0,    21,     0,    22,    11,    10,    24,     0,
       0,     0,     0,     0,     0,     0,     0,    33,     0,   278,
     278,     0,     0,     0,   253,    46,   256,     0,     0,   252,
     260,   261,   262,   263,     0,     0,     0,     0,   259,     0,
       0,   355,   355,   270,     0,   212,   213,   215,   217,   219,
     222,   225,   228,   231,   237,   267,   255,     0,    58,     0,
       0,     0,     3,     0,     0,   187,   189,     0,     0,   182,
     184,   185,    46,     0,   169,     0,   133,   132,     0,     0,
       0,     0,   180,    54,    56,    55,    57,   126,    52,     0,
       0,     0,   119,   121,   118,   120,     0,     6,    51,    39,
      40,    41,    42,     0,     0,    49,     0,     0,     0,   183,
       7,     8,    20,    23,    25,    26,    27,    28,    29,    30,
      31,    32,     0,   280,     0,   279,     0,     0,     0,     0,
     276,   278,   233,   218,   234,     0,     0,   232,     0,     0,
       0,   278,     0,   257,     0,     0,     0,     0,     0,   242,
     243,   244,   245,   246,   247,   248,   249,   250,   251,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     3,     0,   284,   284,     0,
       0,     0,     3,     0,     3,   179,   178,     0,   177,     0,
     174,     0,   284,    53,     0,   284,     3,     0,     0,     0,
       0,    35,     0,     0,   300,     0,   301,   346,   316,   313,
     314,   305,   320,   327,   328,   329,   345,   325,   326,   324,
     311,   309,   334,   315,   306,   343,   318,   319,   307,   310,
     317,   342,   330,   331,   337,   321,   332,   333,   340,   344,
     312,   341,   308,   302,   303,   304,   338,   339,   336,   322,
     323,   335,    45,    36,     0,     0,   300,   299,     0,     0,
     298,     0,    60,     0,    61,     0,     0,     0,     0,     0,
       0,     0,   278,   254,   264,   265,   356,   282,   355,   268,
     355,     0,   300,     0,   274,    46,     3,   201,    43,   202,
     203,   204,   205,   206,   207,   208,   209,   210,   211,     0,
     214,   216,   223,   224,     0,   220,   226,   227,   229,   230,
       0,   300,     0,   239,   277,    59,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    80,   294,     0,   285,     0,
       0,     0,   134,     0,   170,     0,   176,   175,   172,   173,
     278,     0,   128,     0,     0,     0,   124,   122,   123,     0,
      44,     0,   278,   278,    38,    37,     0,   137,     0,     0,
       0,   355,   281,   145,     0,   140,     0,     0,   258,   235,
       0,   355,     0,     0,   271,   278,   278,   272,     0,   196,
     221,   238,   278,   278,     0,     3,     0,     0,     0,     0,
       0,    47,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     3,    47,    47,     0,   127,     3,     0,    47,     0,
       0,    50,     0,     0,   353,     0,   159,   347,   348,   135,
       0,     0,     0,   145,   139,     0,   236,     0,   266,   269,
       0,     0,     0,    47,   190,     0,   191,     0,     0,     3,
       0,     0,     0,     3,     0,    75,     0,     0,     0,    82,
       0,   286,   289,   290,   291,   292,   293,     0,     0,   295,
       3,   296,     3,     3,     0,     0,     0,    64,     0,     3,
       0,   125,     3,    62,    63,   354,     0,     0,   159,   300,
       0,     0,     0,     0,   146,   147,     0,     0,     0,   283,
     273,   275,     0,     3,     0,     0,     0,     3,     0,   240,
     241,     0,    47,    48,    69,     0,     3,     3,     0,     0,
      76,    82,     0,    81,    77,   288,   287,     0,     0,     0,
       0,    47,   130,   171,    47,     0,   117,     0,     0,     0,
       0,     0,   160,   161,     0,     0,     0,     0,     0,     0,
       0,     0,   145,     0,     0,     0,     0,     0,     0,     0,
     193,   192,     0,     0,   197,    47,     0,    67,     0,     0,
       0,     0,    70,     3,    78,    82,   111,    83,     0,     0,
       0,    88,    90,    92,    91,    93,    84,    94,    95,    96,
      97,     0,    99,     0,    89,   101,     0,     0,     0,     0,
       0,     0,     0,     0,   110,    47,   297,    47,    47,     0,
       0,    47,    47,   349,     0,   167,     0,     0,     0,     0,
       0,   350,   351,     0,     0,     0,   145,   145,     0,     0,
       0,     0,   143,     0,   145,     0,   141,     0,    47,     0,
      47,     0,    66,    68,     3,    73,    71,     0,    79,    85,
      86,    87,    98,   100,   102,   103,   104,   105,   106,   107,
     108,   109,     0,     0,     0,   129,   114,     0,     0,     0,
     284,     0,   162,   138,     3,     0,     0,   145,     0,     0,
       0,     0,     0,     0,   148,     0,     0,     0,     0,   142,
       3,     0,     3,     0,     0,    65,     0,    72,   112,   113,
     131,   116,   115,     0,     0,     0,   168,     0,   136,     0,
       0,     0,   150,     0,     0,     0,   145,   145,     0,     3,
       0,     0,   194,     0,   196,   198,    74,   159,     0,     0,
      47,   352,     0,   145,   149,     0,     0,     0,     0,   151,
       0,     0,   195,   200,   199,     0,   159,     3,     0,     0,
       0,   153,   155,     0,     0,    47,   144,     0,     0,     0,
     166,   154,     0,     0,     0,     0,     0,     0,    47,     0,
     156,   152,   158,   165,     0,     0,   157,   164,   163
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -548,  -548,   -98,  -548,  -186,   775,  -548,    -2,   785,  -548,
    -548,   746,  -185,  -184,  -545,  -547,  -544,  -539,  -537,  -534,
    -548,  -548,  -480,  -548,  -531,  -526,  -524,  -522,  -171,   745,
     538,  -521,  -520,  -110,  -548,  -548,  -548,  -548,  -517,  -467,
    -548,  -523,  -548,  -548,   703,  -516,  -160,  -159,  -158,  -513,
    -146,  -140,  -138,  -136,  -512,  -411,   178,  -478,    28,  -548,
     741,   -59,  -548,  -198,   191,   173,   -61,   856,   736,  -548,
     625,  -548,  -548,  -548,     6,  -548,  -548,  -212,   380,  -548,
     474,   -77,  -175,   364,   -91
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    37,    38,   157,    39,    93,   158,   253,
     137,   138,    41,    42,    43,   554,    44,    45,    46,    47,
     375,   445,   563,   626,    48,    49,    50,    51,    52,   139,
     393,    53,    54,   125,    55,   528,   466,    56,    57,   471,
     535,   527,   583,   656,   126,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,   484,   486,   349,   173,    95,
      96,    97,    98,    99,   100,   101,   102,   103,   210,   104,
     105,   193,   427,   106,   174,   175,   328,   377,   509,   378,
     309,   310,   311,   465,   189
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      40,   190,   316,   318,   544,   584,   537,   548,   337,   339,
     340,   612,   355,   230,   224,   330,   379,   182,   621,   622,
     120,   121,   124,   341,   623,   183,   624,    68,   187,   625,
     391,   663,   627,   394,   342,   343,   344,   628,    94,   629,
     108,   630,   632,   633,   113,   114,   634,   635,   345,   119,
     639,   644,   386,   127,   346,   531,   347,   132,   348,   588,
     589,   220,   129,   685,   686,   215,   472,   664,   579,   621,
     622,     3,    75,   388,   216,   623,   176,   624,   532,   302,
     625,   614,   555,   627,   501,   221,   502,    70,   628,   370,
     629,   580,   630,   632,   633,    11,   590,   634,   635,   110,
      71,   639,   644,   531,    72,   222,    69,   122,   387,   665,
     533,   503,   504,   505,   506,   334,   473,   666,   188,   380,
     130,   124,   579,   581,   531,   671,   596,   373,    28,   389,
      11,   556,    73,   371,   383,   688,   385,   351,   591,   363,
     737,   111,   534,   621,   622,   659,   592,   726,   395,   623,
     109,   624,   358,   359,   625,   582,   430,   627,   533,   607,
     507,   508,   628,    28,   629,   112,   630,   632,   633,   381,
     335,   634,   635,     5,   231,   639,   644,   581,   115,   533,
     531,   251,   252,   408,   586,   303,   123,   320,   542,   766,
     534,   191,   338,    11,   321,    12,   247,   331,   531,   721,
     722,   733,   192,   754,   409,   587,   248,   728,   410,   582,
     231,   534,   232,   411,   116,    18,    19,   327,    21,    22,
      23,   755,    40,   142,   143,    27,    28,   124,    29,   124,
     128,   531,    33,    34,   140,   533,   133,   422,   428,   423,
     414,   141,   215,   360,   785,   411,   365,   366,   367,   368,
     750,   216,    75,   533,   760,   416,   235,   543,   239,   134,
     411,   531,   172,   798,   531,   450,   240,   534,   147,   478,
     231,   725,   753,   135,   326,    11,   411,   411,   146,   531,
     579,   236,   179,   768,   772,   534,   533,   793,   411,   777,
     778,   117,   136,   711,   118,   712,   337,   339,   340,   337,
     339,   340,   794,   797,   443,   237,   790,   444,    28,   479,
     159,   341,   326,   238,   341,   242,   533,   160,   534,   533,
     470,   180,   342,   343,   344,   342,   343,   344,   420,   161,
     477,   531,   404,   405,   533,   581,   345,   490,   133,   345,
     162,   412,   346,   185,   347,   346,   348,   347,   534,   348,
     782,   534,   783,   514,   802,   144,   145,   163,   518,   211,
     212,   134,   197,   198,   579,   164,   534,   582,   165,   213,
     214,    40,   654,   655,   166,   135,   167,   618,   619,   620,
     168,    40,   169,    40,   356,   357,   533,   807,   352,   353,
     170,   551,   631,    40,   136,   558,   454,   171,   438,   177,
     440,   178,   442,   636,   637,   638,   181,   186,   462,   463,
     194,   195,   567,   196,   569,   570,   670,   640,   534,   581,
     217,   575,   223,   641,   577,   642,    40,   643,   618,   619,
     620,   480,   481,   218,   225,   219,   467,   468,   487,   488,
     226,   227,   228,   631,   229,   599,   234,   233,   244,   603,
     241,   582,   245,   246,   636,   637,   638,   249,   610,   611,
     149,   150,   151,   152,   491,   492,   312,   313,   640,   314,
     319,   498,   322,   364,   641,   323,   642,   324,   643,   369,
     338,   374,   376,   338,   384,   390,   392,   397,    40,   396,
     720,   398,   525,   337,   339,   340,   399,   400,   745,   401,
     407,   413,   618,   619,   620,   539,   402,   403,   341,   406,
     415,   417,    40,   304,   418,   687,    40,   631,    92,   342,
     343,   344,   559,   421,   411,   424,   425,   429,   636,   637,
     638,   426,    69,   345,   744,   432,   431,   433,   435,   346,
     436,   347,   640,   348,   434,   437,   439,   447,   641,    40,
     642,   608,   643,   448,   446,   449,    40,   455,   451,   456,
     457,    40,   459,   464,   475,    40,   485,    40,    40,   461,
     598,   494,   476,    40,   602,    40,   496,   497,   469,   148,
     474,   489,   493,   609,   500,   495,   736,   149,   150,   151,
     152,   511,   499,   510,   512,   513,   516,    40,   515,   517,
     519,    40,   520,   521,   522,   523,   653,   524,    40,    40,
     536,   526,    40,   538,   661,   662,   747,    74,    75,    76,
     540,    77,    78,    79,   541,   545,   557,   565,   549,   550,
     566,   562,   761,   568,   763,   593,   594,   560,   561,   578,
     153,    11,   595,   564,    80,    81,    82,    83,   572,   573,
      84,   576,    85,    86,   597,   585,   154,   600,   155,   601,
     604,   780,   606,   649,   613,   156,   615,   650,   657,   658,
     667,   668,    87,   672,    28,   673,    88,   338,   675,   674,
     676,   677,   679,   681,   682,    40,    40,   683,   684,   799,
     702,   689,   690,   691,    89,   703,   692,    90,   704,    91,
     693,    92,   694,   695,   696,   707,   697,   698,   131,   699,
     700,    74,    75,    76,   701,    77,    78,    79,   708,   705,
     706,   713,   715,   709,   714,   710,   717,    74,    75,    76,
     718,    77,    78,    79,    40,    11,   716,   723,    80,    81,
      82,    83,   724,   727,    84,    40,    85,    86,   746,   731,
     729,    11,   730,   734,    80,    81,    82,    83,   749,    40,
      84,    40,    85,    86,   735,   751,    87,   738,    28,   739,
      88,   740,   741,   742,   748,   752,   756,   771,    40,   757,
     758,   759,    87,   762,    28,   764,    88,   765,    89,   767,
     769,    90,   773,    91,   325,    92,   774,    40,   775,   326,
     776,   779,   306,   307,    89,   781,   786,    90,   787,    91,
     788,    92,   256,   257,   789,   326,   791,   803,   804,   792,
     258,   796,   259,   260,   805,   261,   800,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,   300,   301,   529,   307,   801,
     806,   809,   810,   811,   812,   813,   814,   256,   257,   815,
     816,   817,   818,   243,   209,   258,   329,   259,   260,   326,
     261,   250,   262,   263,   264,   265,   266,   267,   268,   269,
     270,   271,   272,   273,   274,   275,   276,   277,   278,   279,
     280,   281,   282,   283,   284,   285,   286,   287,   288,   289,
     290,   291,   292,   293,   294,   295,   296,   297,   298,   299,
     300,   301,     4,   305,   382,     5,   350,   460,     6,     7,
       8,   184,   784,     9,   530,   354,   419,     0,   646,   660,
      10,     0,     0,     0,   326,    11,     0,    12,     0,     0,
       0,     0,     0,     0,     0,    13,     0,     0,    14,     0,
       0,    15,   372,     0,     0,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,     0,    27,    28,     0,
      29,    30,    31,    32,    33,    34,    35,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,   482,    36,
     483,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,     0,     0,    14,     0,     0,    15,     0,     0,     0,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,     0,    27,    28,     0,    29,    30,    31,    32,    33,
      34,    35,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     0,     0,     9,     0,     0,     0,     0,     0,     0,
      10,     0,     0,     0,    36,    11,     0,    12,     0,     0,
       0,     0,     0,     0,     0,    13,     0,     0,    14,     0,
       0,    15,     0,     0,     0,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,     0,    27,    28,     0,
      29,    30,    31,    32,    33,    34,    35,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    36,
     441,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,     0,     0,    14,     0,     0,    15,     0,     0,     0,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,     0,    27,    28,     0,    29,    30,    31,    32,    33,
      34,    35,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     0,     0,     9,     0,     0,     0,     0,     0,     0,
      10,     0,     0,     0,    36,   452,     0,    12,     0,     0,
       0,     0,     0,     0,     0,    13,     0,     0,    14,     0,
       0,    15,     0,     0,     0,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,     0,    27,    28,     0,
      29,    30,    31,    32,    33,    34,    35,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    36,
     453,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,     0,     0,    14,     0,     0,    15,     0,     0,     0,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,     0,    27,    28,     0,    29,    30,    31,    32,    33,
      34,    35,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     0,     0,     9,     0,     0,     0,     0,     0,     0,
      10,     0,     0,     0,    36,   458,     0,    12,     0,     0,
       0,     0,     0,     0,     0,    13,     0,     0,    14,     0,
       0,    15,     0,     0,     0,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,     0,    27,    28,     0,
      29,    30,    31,    32,    33,    34,    35,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    36,
     552,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,     0,     0,    14,     0,     0,    15,     0,     0,     0,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,     0,    27,   553,     0,    29,    30,    31,    32,    33,
      34,    35,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     0,     0,     9,     0,     0,     0,     0,     0,     0,
      10,     0,     0,     0,    36,   571,     0,    12,     0,     0,
       0,     0,     0,     0,     0,    13,     0,     0,    14,     0,
       0,    15,     0,     0,     0,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,     0,    27,    28,     0,
      29,    30,    31,    32,    33,    34,    35,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    36,
     574,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,     0,     0,    14,     0,     0,    15,     0,     0,     0,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,     0,    27,    28,     0,    29,    30,    31,    32,    33,
      34,    35,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     0,     0,     9,     0,     0,     0,     0,     0,     0,
      10,     0,     0,     0,    36,   605,     0,    12,     0,     0,
       0,     0,     0,     0,     0,    13,     0,     0,    14,     0,
       0,    15,     0,     0,     0,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,     0,    27,    28,     0,
      29,    30,    31,    32,    33,    34,    35,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    36,
     645,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,     0,     0,    14,     0,     0,    15,     0,     0,     0,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,     0,    27,    28,     0,    29,    30,    31,    32,    33,
      34,    35,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     0,     0,     9,     0,     0,     0,     0,     0,     0,
      10,     0,     0,     0,    36,   647,     0,    12,     0,     0,
       0,     0,     0,     0,     0,    13,     0,     0,    14,     0,
       0,    15,     0,     0,     0,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,     0,    27,    28,     0,
      29,    30,    31,    32,    33,    34,    35,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    36,
     648,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,     0,     0,    14,     0,     0,    15,     0,     0,     0,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,     0,    27,    28,     0,    29,    30,    31,    32,    33,
      34,    35,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     0,     0,     9,     0,     0,     0,     0,     0,     0,
      10,     0,     0,     0,    36,   651,     0,    12,     0,     0,
       0,     0,     0,     0,     0,    13,     0,     0,    14,     0,
       0,    15,     0,     0,     0,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,     0,    27,    28,     0,
      29,    30,    31,    32,    33,    34,    35,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    36,
     652,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,     0,     0,    14,     0,     0,    15,     0,     0,     0,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,     0,    27,    28,     0,    29,    30,    31,    32,    33,
      34,    35,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     0,     0,     9,     0,     0,     0,     0,     0,     0,
      10,     0,     0,     0,    36,   678,     0,    12,     0,     0,
       0,     0,     0,     0,     0,    13,     0,     0,    14,     0,
       0,    15,     0,     0,     0,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,     0,    27,    28,     0,
      29,    30,    31,    32,    33,    34,    35,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    36,
     680,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,     0,     0,    14,     0,     0,    15,     0,     0,     0,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,     0,    27,    28,     0,    29,    30,    31,    32,    33,
      34,    35,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     0,     0,     9,     0,     0,     0,     0,     0,     0,
      10,     0,     0,     0,    36,   770,     0,    12,     0,     0,
       0,     0,     0,     0,     0,    13,     0,     0,    14,     0,
       0,    15,     0,     0,     0,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,     0,    27,    28,     0,
      29,    30,    31,    32,    33,    34,    35,     4,     0,     0,
       5,     0,     0,     6,     7,     8,     0,     0,     9,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    36,
     795,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,     0,     0,    14,     0,     0,    15,     0,     0,     0,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,     0,    27,    28,     0,    29,    30,    31,    32,    33,
      34,    35,     4,     0,     0,     5,     0,     0,     6,     7,
       8,     0,     0,     9,     0,     0,     0,     0,     0,     0,
      10,     0,     0,     0,    36,   808,     0,    12,     0,     0,
       0,     0,     0,     0,     0,    13,     0,     0,    14,     0,
       0,    15,     0,     0,     0,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,     0,    27,    28,     0,
      29,    30,    31,    32,    33,    34,    35,   335,     0,     0,
       5,     0,     0,     6,     0,     0,     0,     0,   616,     0,
       0,     0,     0,     0,     0,    10,     0,     0,     0,    36,
      11,     0,    12,     0,     0,     0,     0,     0,     0,     0,
      13,     0,     0,    14,     0,     0,    15,     0,     0,     0,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,   335,    27,    28,     5,    29,    30,    31,    32,    33,
      34,    35,     0,     0,     0,     0,     0,     0,     0,   546,
     335,     0,     0,     5,    11,     0,    12,     0,     0,     0,
       0,     0,     0,     0,   617,     0,     0,     0,     0,     0,
       0,     0,     0,    11,     0,    12,    18,    19,     0,    21,
      22,    23,     0,     0,     0,     0,    27,    28,     0,    29,
       0,     0,     0,    33,    34,    18,    19,     0,    21,    22,
      23,   335,     0,     0,     5,    27,    28,     0,    29,     0,
       0,     0,    33,    34,     0,     0,     0,     0,   547,     0,
       0,     0,     0,     0,    11,     0,    12,   199,   200,   201,
     202,   203,   204,   205,   206,   207,   208,   336,    74,    75,
      76,     0,    77,    78,    79,     0,    18,    19,     0,    21,
      22,    23,     0,     0,     0,     0,    27,    28,     0,    29,
       0,     0,    11,    33,    34,    80,    81,    82,    83,     0,
       0,    84,     0,    85,    86,     0,   107,    74,    75,    76,
       0,    77,    78,    79,     0,     0,     0,     0,   732,     0,
       0,     0,     0,    87,     0,    28,     0,    88,     0,     0,
       0,    11,     0,     0,    80,    81,    82,    83,     0,     0,
      84,     0,    85,    86,     0,    89,     0,     0,    90,     0,
      91,     0,    92,    74,    75,    76,     0,    77,     0,    79,
       0,     0,    87,     0,    28,     0,    88,     0,    74,    75,
      76,     0,    77,    78,    79,     0,     0,    11,     0,     0,
      80,    81,    82,    83,    89,     0,     0,    90,     0,    91,
       0,    92,    11,     0,     0,    80,    81,    82,    83,     0,
       0,     0,     0,    85,    86,     0,     0,     0,    87,     0,
      28,     0,    88,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    87,     0,    28,     0,    88,     0,     0,
       0,     0,     0,    90,     0,    91,     0,    92,     0,     0,
       0,     0,     0,   306,   307,    89,     0,     0,    90,     0,
      91,     0,    92,   256,   257,     0,     0,     0,     0,     0,
       0,   258,     0,   259,   260,     0,   261,     0,   262,   263,
     264,   265,   266,   267,   268,   269,   270,   271,   272,   273,
     274,   275,   276,   277,   278,   279,   280,   281,   282,   283,
     284,   285,   286,   287,   288,   289,   290,   291,   292,   293,
     294,   295,   296,   297,   298,   299,   300,   301,   306,   307,
       0,     0,     0,     0,     0,     0,     0,     0,   256,   257,
       0,     0,     0,   308,     0,     0,   258,     0,   259,   260,
       0,   261,     0,   262,   263,   264,   265,   266,   267,   268,
     269,   270,   271,   272,   273,   274,   275,   276,   277,   278,
     279,   280,   281,   282,   283,   284,   285,   286,   287,   288,
     289,   290,   291,   292,   293,   294,   295,   296,   297,   298,
     299,   300,   301,   306,   307,     0,     0,     0,     0,     0,
       0,     0,     0,   256,   257,     0,     0,     0,   315,     0,
       0,   258,     0,   259,   260,     0,   261,     0,   262,   263,
     264,   265,   266,   267,   268,   269,   270,   271,   272,   273,
     274,   275,   276,   277,   278,   279,   280,   281,   282,   283,
     284,   285,   286,   287,   288,   289,   290,   291,   292,   293,
     294,   295,   296,   297,   298,   299,   300,   301,   306,   307,
       0,     0,     0,     0,     0,     0,     0,     0,   256,   257,
       0,     0,     0,   317,     0,     0,   258,     0,   259,   260,
       0,   261,     0,   262,   263,   264,   265,   266,   267,   268,
     269,   270,   271,   272,   273,   274,   275,   276,   277,   278,
     279,   280,   281,   282,   283,   284,   285,   286,   287,   288,
     289,   290,   291,   292,   293,   294,   295,   296,   297,   298,
     299,   300,   301,   306,   307,     0,     0,     0,     0,     0,
       0,     0,     0,   256,   257,     0,     0,     0,   669,     0,
       0,   258,     0,   259,   260,     0,   261,     0,   262,   263,
     264,   265,   266,   267,   268,   269,   270,   271,   272,   273,
     274,   275,   276,   277,   278,   279,   280,   281,   282,   283,
     284,   285,   286,   287,   288,   289,   290,   291,   292,   293,
     294,   295,   296,   297,   298,   299,   300,   301,   306,   307,
       0,     0,     0,     0,     0,     0,     0,     0,   256,   257,
       0,     0,     0,   719,     0,     0,   258,     0,   259,   260,
       0,   261,     0,   262,   263,   264,   265,   266,   267,   268,
     269,   270,   271,   272,   273,   274,   275,   276,   277,   278,
     279,   280,   281,   282,   283,   284,   285,   286,   287,   288,
     289,   290,   291,   292,   293,   294,   295,   296,   297,   298,
     299,   300,   301,   148,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   197,   198,     0,   743,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   199,   200,   201,   202,   203,   204,
     205,   206,   207,   208,   254,     0,     0,   255,     0,     0,
       0,     0,   155,     0,   256,   257,     0,     0,     0,     0,
       0,     0,   258,     0,   259,   260,     0,   261,     0,   262,
     263,   264,   265,   266,   267,   268,   269,   270,   271,   272,
     273,   274,   275,   276,   277,   278,   279,   280,   281,   282,
     283,   284,   285,   286,   287,   288,   289,   290,   291,   292,
     293,   294,   295,   296,   297,   298,   299,   300,   301,   332,
       0,     0,   333,     0,     0,     0,     0,     0,     0,   256,
     257,     0,     0,     0,     0,     0,     0,   258,     0,   259,
     260,     0,   261,     0,   262,   263,   264,   265,   266,   267,
     268,   269,   270,   271,   272,   273,   274,   275,   276,   277,
     278,   279,   280,   281,   282,   283,   284,   285,   286,   287,
     288,   289,   290,   291,   292,   293,   294,   295,   296,   297,
     298,   299,   300,   301,   361,     0,     0,   362,     0,     0,
       0,     0,     0,     0,   256,   257,     0,     0,     0,     0,
       0,     0,   258,     0,   259,   260,     0,   261,     0,   262,
     263,   264,   265,   266,   267,   268,   269,   270,   271,   272,
     273,   274,   275,   276,   277,   278,   279,   280,   281,   282,
     283,   284,   285,   286,   287,   288,   289,   290,   291,   292,
     293,   294,   295,   296,   297,   298,   299,   300,   301
};

static const yytype_int16 yycheck[] =
{
       2,    92,   177,   178,   482,   528,   473,   485,   194,   194,
     194,   558,   210,   123,   112,   190,   228,    78,   563,   563,
      22,    23,    24,   194,   563,    84,   563,     4,    89,   563,
     242,     5,   563,   245,   194,   194,   194,   563,    10,   563,
      12,   563,   563,   563,    16,    17,   563,   563,   194,    21,
     563,   563,     4,    25,   194,     4,   194,    29,   194,     4,
       5,    45,     4,   610,   611,    85,    41,    41,     4,   614,
     614,     0,     4,     4,    94,   614,    70,   614,    27,   156,
     614,   561,    42,   614,     3,    69,     5,    83,   614,    45,
     614,    27,   614,   614,   614,    27,    41,   614,   614,     4,
       4,   614,   614,     4,     4,    89,    83,     4,    60,    83,
      59,    30,    31,    32,    33,   192,    91,    91,    90,   229,
      62,   123,     4,    59,     4,   592,    27,   225,    60,    60,
      27,    91,     4,    89,   232,   615,   234,   196,    83,   216,
     687,    46,    91,   688,   688,    27,    91,    27,   246,   688,
       4,   688,   213,   214,   688,    91,   354,   688,    59,    91,
      79,    80,   688,    60,   688,    91,   688,   688,   688,    84,
       4,   688,   688,     7,    89,   688,   688,    59,     4,    59,
       4,   153,   154,    69,    69,   157,    83,   181,    22,   736,
      91,    83,   194,    27,    37,    29,     4,   191,     4,   666,
     667,   679,    94,    27,    90,    90,    14,   674,    84,    91,
      89,    91,    91,    89,     4,    49,    50,   189,    52,    53,
      54,    27,   224,     4,     5,    59,    60,   229,    62,   231,
      58,     4,    66,    67,     4,    59,     4,   328,   336,   330,
      84,     4,    85,   215,   767,    89,   218,   219,   220,   221,
     717,    94,     4,    59,    27,    84,     4,    91,    53,    27,
      89,     4,    83,   786,     4,    84,    61,    91,    91,    86,
      89,    84,    84,    41,    91,    27,    89,    89,    63,     4,
       4,    29,    59,    84,    27,    91,    59,    27,    89,   756,
     757,     4,    60,    89,     7,    91,   482,   482,   482,   485,
     485,   485,    27,    27,    23,    53,   773,    26,    60,    88,
      90,   482,    91,    61,   485,    83,    59,    91,    91,    59,
     411,     4,   482,   482,   482,   485,   485,   485,   322,    91,
     421,     4,   304,   305,    59,    59,   482,   435,     4,   485,
      91,   313,   482,     4,   482,   485,   482,   485,    91,   485,
     761,    91,   763,   451,    27,     4,     5,    91,   456,    79,
      80,    27,    20,    21,     4,    91,    91,    91,    91,    81,
      82,   373,     4,     5,    91,    41,    91,   563,   563,   563,
      91,   383,    91,   385,   211,   212,    59,    27,   197,   198,
      91,   489,   563,   395,    60,   493,   390,    91,   370,    83,
     372,    83,   374,   563,   563,   563,    83,    83,   402,   403,
      24,    35,   510,    34,   512,   513,   591,   563,    91,    59,
       3,   519,     4,   563,   522,   563,   428,   563,   614,   614,
     614,   425,   426,    62,    91,    83,   408,   409,   432,   433,
      91,    83,    83,   614,    83,   543,    91,    94,    40,   547,
       4,    91,    83,    91,   614,   614,   614,     4,   556,   557,
      16,    17,    18,    19,   436,   437,    84,    89,   614,    84,
       4,   443,    83,     4,   614,    84,   614,    84,   614,     4,
     482,    23,     4,   485,     4,    83,     4,     4,   490,     5,
     665,     5,   464,   679,   679,   679,    40,    86,   710,     6,
      91,    91,   688,   688,   688,   477,    83,    83,   679,    83,
      91,    91,   514,    69,    84,   613,   518,   688,    87,   679,
     679,   679,   494,    89,    89,    84,    83,    91,   688,   688,
     688,    83,    83,   679,   709,    83,    86,    83,    91,   679,
      41,   679,   688,   679,    84,    45,     4,    84,   688,   551,
     688,   553,   688,    89,    69,    84,   558,    84,    91,    91,
      84,   563,    14,     4,    13,   567,    25,   569,   570,    88,
     542,    45,    84,   575,   546,   577,    47,    24,    91,     8,
      91,    91,    91,   555,    28,    91,   684,    16,    17,    18,
      19,     4,    91,    91,    91,    91,    57,   599,    55,    84,
      91,   603,    65,     4,    91,    84,   578,    84,   610,   611,
       4,    84,   614,    27,   586,   587,   714,     3,     4,     5,
      84,     7,     8,     9,    84,    22,    91,     3,    84,    84,
       3,    24,   730,    69,   732,     4,     4,    91,    91,    90,
      69,    27,     4,    91,    30,    31,    32,    33,    91,    91,
      36,    91,    38,    39,     4,    83,    85,    91,    87,    91,
      91,   759,    40,    55,    91,    94,    91,    63,     4,     4,
      91,     4,    58,    91,    60,    94,    62,   679,     4,    91,
      91,    24,    24,    37,    91,   687,   688,    91,    91,   787,
      51,    91,    91,    91,    80,    51,    91,    83,    55,    85,
      91,    87,    91,    91,    91,    64,    91,    91,    94,    91,
      91,     3,     4,     5,    91,     7,     8,     9,    63,    91,
      91,    91,     4,    83,    91,    83,    91,     3,     4,     5,
       4,     7,     8,     9,   736,    27,    84,     4,    30,    31,
      32,    33,    91,     4,    36,   747,    38,    39,     5,    22,
      91,    27,    91,    22,    30,    31,    32,    33,    90,   761,
      36,   763,    38,    39,    91,     4,    58,    91,    60,    91,
      62,    91,    91,    91,    91,    91,    91,   749,   780,    91,
       4,    91,    58,    91,    60,    91,    62,    91,    80,    91,
      84,    83,    91,    85,    86,    87,    91,   799,     4,    91,
       4,    91,     4,     5,    80,     4,    91,    83,    91,    85,
      59,    87,    14,    15,     4,    91,    91,     4,     4,    91,
      22,    91,    24,    25,    59,    27,    91,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,     4,     5,    91,
       4,     4,    91,    91,    91,    91,     4,    14,    15,     4,
      91,    91,    91,   137,    99,    22,    88,    24,    25,    91,
      27,   146,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,     4,   158,   231,     7,   195,   399,    10,    11,
      12,    85,   764,    15,   470,   209,   321,    -1,   568,   585,
      22,    -1,    -1,    -1,    91,    27,    -1,    29,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    37,    -1,    -1,    40,    -1,
      -1,    43,    44,    -1,    -1,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    -1,    59,    60,    -1,
      62,    63,    64,    65,    66,    67,    68,     4,    -1,    -1,
       7,    -1,    -1,    10,    11,    12,    -1,    -1,    15,    -1,
      -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,    25,    91,
      27,    -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      37,    -1,    -1,    40,    -1,    -1,    43,    -1,    -1,    -1,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    -1,    59,    60,    -1,    62,    63,    64,    65,    66,
      67,    68,     4,    -1,    -1,     7,    -1,    -1,    10,    11,
      12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,
      22,    -1,    -1,    -1,    91,    27,    -1,    29,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    37,    -1,    -1,    40,    -1,
      -1,    43,    -1,    -1,    -1,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    -1,    59,    60,    -1,
      62,    63,    64,    65,    66,    67,    68,     4,    -1,    -1,
       7,    -1,    -1,    10,    11,    12,    -1,    -1,    15,    -1,
      -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,    91,
      27,    -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      37,    -1,    -1,    40,    -1,    -1,    43,    -1,    -1,    -1,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    -1,    59,    60,    -1,    62,    63,    64,    65,    66,
      67,    68,     4,    -1,    -1,     7,    -1,    -1,    10,    11,
      12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,
      22,    -1,    -1,    -1,    91,    27,    -1,    29,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    37,    -1,    -1,    40,    -1,
      -1,    43,    -1,    -1,    -1,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    -1,    59,    60,    -1,
      62,    63,    64,    65,    66,    67,    68,     4,    -1,    -1,
       7,    -1,    -1,    10,    11,    12,    -1,    -1,    15,    -1,
      -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,    91,
      27,    -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      37,    -1,    -1,    40,    -1,    -1,    43,    -1,    -1,    -1,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    -1,    59,    60,    -1,    62,    63,    64,    65,    66,
      67,    68,     4,    -1,    -1,     7,    -1,    -1,    10,    11,
      12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,
      22,    -1,    -1,    -1,    91,    27,    -1,    29,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    37,    -1,    -1,    40,    -1,
      -1,    43,    -1,    -1,    -1,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    -1,    59,    60,    -1,
      62,    63,    64,    65,    66,    67,    68,     4,    -1,    -1,
       7,    -1,    -1,    10,    11,    12,    -1,    -1,    15,    -1,
      -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,    91,
      27,    -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      37,    -1,    -1,    40,    -1,    -1,    43,    -1,    -1,    -1,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    -1,    59,    60,    -1,    62,    63,    64,    65,    66,
      67,    68,     4,    -1,    -1,     7,    -1,    -1,    10,    11,
      12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,
      22,    -1,    -1,    -1,    91,    27,    -1,    29,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    37,    -1,    -1,    40,    -1,
      -1,    43,    -1,    -1,    -1,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    -1,    59,    60,    -1,
      62,    63,    64,    65,    66,    67,    68,     4,    -1,    -1,
       7,    -1,    -1,    10,    11,    12,    -1,    -1,    15,    -1,
      -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,    91,
      27,    -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      37,    -1,    -1,    40,    -1,    -1,    43,    -1,    -1,    -1,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    -1,    59,    60,    -1,    62,    63,    64,    65,    66,
      67,    68,     4,    -1,    -1,     7,    -1,    -1,    10,    11,
      12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,
      22,    -1,    -1,    -1,    91,    27,    -1,    29,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    37,    -1,    -1,    40,    -1,
      -1,    43,    -1,    -1,    -1,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    -1,    59,    60,    -1,
      62,    63,    64,    65,    66,    67,    68,     4,    -1,    -1,
       7,    -1,    -1,    10,    11,    12,    -1,    -1,    15,    -1,
      -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,    91,
      27,    -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      37,    -1,    -1,    40,    -1,    -1,    43,    -1,    -1,    -1,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    -1,    59,    60,    -1,    62,    63,    64,    65,    66,
      67,    68,     4,    -1,    -1,     7,    -1,    -1,    10,    11,
      12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,
      22,    -1,    -1,    -1,    91,    27,    -1,    29,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    37,    -1,    -1,    40,    -1,
      -1,    43,    -1,    -1,    -1,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    -1,    59,    60,    -1,
      62,    63,    64,    65,    66,    67,    68,     4,    -1,    -1,
       7,    -1,    -1,    10,    11,    12,    -1,    -1,    15,    -1,
      -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,    91,
      27,    -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      37,    -1,    -1,    40,    -1,    -1,    43,    -1,    -1,    -1,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    -1,    59,    60,    -1,    62,    63,    64,    65,    66,
      67,    68,     4,    -1,    -1,     7,    -1,    -1,    10,    11,
      12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,
      22,    -1,    -1,    -1,    91,    27,    -1,    29,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    37,    -1,    -1,    40,    -1,
      -1,    43,    -1,    -1,    -1,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    -1,    59,    60,    -1,
      62,    63,    64,    65,    66,    67,    68,     4,    -1,    -1,
       7,    -1,    -1,    10,    11,    12,    -1,    -1,    15,    -1,
      -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,    91,
      27,    -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      37,    -1,    -1,    40,    -1,    -1,    43,    -1,    -1,    -1,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    -1,    59,    60,    -1,    62,    63,    64,    65,    66,
      67,    68,     4,    -1,    -1,     7,    -1,    -1,    10,    11,
      12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,
      22,    -1,    -1,    -1,    91,    27,    -1,    29,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    37,    -1,    -1,    40,    -1,
      -1,    43,    -1,    -1,    -1,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    -1,    59,    60,    -1,
      62,    63,    64,    65,    66,    67,    68,     4,    -1,    -1,
       7,    -1,    -1,    10,    11,    12,    -1,    -1,    15,    -1,
      -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,    91,
      27,    -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      37,    -1,    -1,    40,    -1,    -1,    43,    -1,    -1,    -1,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    -1,    59,    60,    -1,    62,    63,    64,    65,    66,
      67,    68,     4,    -1,    -1,     7,    -1,    -1,    10,    11,
      12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,
      22,    -1,    -1,    -1,    91,    27,    -1,    29,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    37,    -1,    -1,    40,    -1,
      -1,    43,    -1,    -1,    -1,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    -1,    59,    60,    -1,
      62,    63,    64,    65,    66,    67,    68,     4,    -1,    -1,
       7,    -1,    -1,    10,    11,    12,    -1,    -1,    15,    -1,
      -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,    91,
      27,    -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      37,    -1,    -1,    40,    -1,    -1,    43,    -1,    -1,    -1,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    -1,    59,    60,    -1,    62,    63,    64,    65,    66,
      67,    68,     4,    -1,    -1,     7,    -1,    -1,    10,    11,
      12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,
      22,    -1,    -1,    -1,    91,    27,    -1,    29,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    37,    -1,    -1,    40,    -1,
      -1,    43,    -1,    -1,    -1,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    -1,    59,    60,    -1,
      62,    63,    64,    65,    66,    67,    68,     4,    -1,    -1,
       7,    -1,    -1,    10,    -1,    -1,    -1,    -1,    15,    -1,
      -1,    -1,    -1,    -1,    -1,    22,    -1,    -1,    -1,    91,
      27,    -1,    29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      37,    -1,    -1,    40,    -1,    -1,    43,    -1,    -1,    -1,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,     4,    59,    60,     7,    62,    63,    64,    65,    66,
      67,    68,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    22,
       4,    -1,    -1,     7,    27,    -1,    29,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    91,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    27,    -1,    29,    49,    50,    -1,    52,
      53,    54,    -1,    -1,    -1,    -1,    59,    60,    -1,    62,
      -1,    -1,    -1,    66,    67,    49,    50,    -1,    52,    53,
      54,     4,    -1,    -1,     7,    59,    60,    -1,    62,    -1,
      -1,    -1,    66,    67,    -1,    -1,    -1,    -1,    91,    -1,
      -1,    -1,    -1,    -1,    27,    -1,    29,    69,    70,    71,
      72,    73,    74,    75,    76,    77,    78,    91,     3,     4,
       5,    -1,     7,     8,     9,    -1,    49,    50,    -1,    52,
      53,    54,    -1,    -1,    -1,    -1,    59,    60,    -1,    62,
      -1,    -1,    27,    66,    67,    30,    31,    32,    33,    -1,
      -1,    36,    -1,    38,    39,    -1,    41,     3,     4,     5,
      -1,     7,     8,     9,    -1,    -1,    -1,    -1,    91,    -1,
      -1,    -1,    -1,    58,    -1,    60,    -1,    62,    -1,    -1,
      -1,    27,    -1,    -1,    30,    31,    32,    33,    -1,    -1,
      36,    -1,    38,    39,    -1,    80,    -1,    -1,    83,    -1,
      85,    -1,    87,     3,     4,     5,    -1,     7,    -1,     9,
      -1,    -1,    58,    -1,    60,    -1,    62,    -1,     3,     4,
       5,    -1,     7,     8,     9,    -1,    -1,    27,    -1,    -1,
      30,    31,    32,    33,    80,    -1,    -1,    83,    -1,    85,
      -1,    87,    27,    -1,    -1,    30,    31,    32,    33,    -1,
      -1,    -1,    -1,    38,    39,    -1,    -1,    -1,    58,    -1,
      60,    -1,    62,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    58,    -1,    60,    -1,    62,    -1,    -1,
      -1,    -1,    -1,    83,    -1,    85,    -1,    87,    -1,    -1,
      -1,    -1,    -1,     4,     5,    80,    -1,    -1,    83,    -1,
      85,    -1,    87,    14,    15,    -1,    -1,    -1,    -1,    -1,
      -1,    22,    -1,    24,    25,    -1,    27,    -1,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,     4,     5,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    14,    15,
      -1,    -1,    -1,    84,    -1,    -1,    22,    -1,    24,    25,
      -1,    27,    -1,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,     4,     5,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    14,    15,    -1,    -1,    -1,    84,    -1,
      -1,    22,    -1,    24,    25,    -1,    27,    -1,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,     4,     5,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    14,    15,
      -1,    -1,    -1,    84,    -1,    -1,    22,    -1,    24,    25,
      -1,    27,    -1,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,     4,     5,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    14,    15,    -1,    -1,    -1,    84,    -1,
      -1,    22,    -1,    24,    25,    -1,    27,    -1,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,     4,     5,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    14,    15,
      -1,    -1,    -1,    84,    -1,    -1,    22,    -1,    24,    25,
      -1,    27,    -1,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,     8,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    20,    21,    -1,    84,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,     4,    -1,    -1,     7,    -1,    -1,
      -1,    -1,    87,    -1,    14,    15,    -1,    -1,    -1,    -1,
      -1,    -1,    22,    -1,    24,    25,    -1,    27,    -1,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,     4,
      -1,    -1,     7,    -1,    -1,    -1,    -1,    -1,    -1,    14,
      15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,    24,
      25,    -1,    27,    -1,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,     4,    -1,    -1,     7,    -1,    -1,
      -1,    -1,    -1,    -1,    14,    15,    -1,    -1,    -1,    -1,
      -1,    -1,    22,    -1,    24,    25,    -1,    27,    -1,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    96,    97,     0,     4,     7,    10,    11,    12,    15,
      22,    27,    29,    37,    40,    43,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    59,    60,    62,
      63,    64,    65,    66,    67,    68,    91,    98,    99,   101,
     102,   107,   108,   109,   111,   112,   113,   114,   119,   120,
     121,   122,   123,   126,   127,   129,   132,   133,   140,   141,
     142,   143,   144,   145,   146,   147,   148,   149,     4,    83,
      83,     4,     4,     4,     3,     4,     5,     7,     8,     9,
      30,    31,    32,    33,    36,    38,    39,    58,    62,    80,
      83,    85,    87,   102,   153,   154,   155,   156,   157,   158,
     159,   160,   161,   162,   164,   165,   168,    41,   153,     4,
       4,    46,    91,   153,   153,     4,     4,     4,     7,   153,
     102,   102,     4,    83,   102,   128,   139,   153,    58,     4,
      62,    94,   153,     4,    27,    41,    60,   105,   106,   124,
       4,     4,     4,     5,     4,     5,    63,    91,     8,    16,
      17,    18,    19,    69,    85,    87,    94,   100,   103,    90,
      91,    91,    91,    91,    91,    91,    91,    91,    91,    91,
      91,    91,    83,   153,   169,   170,   169,    83,    83,    59,
       4,    83,   161,   156,   162,     4,    83,   161,   153,   179,
     179,    83,    94,   166,    24,    35,    34,    20,    21,    69,
      70,    71,    72,    73,    74,    75,    76,    77,    78,   103,
     163,    79,    80,    81,    82,    85,    94,     3,    62,    83,
      45,    69,    89,     4,    97,    91,    91,    83,    83,    83,
     128,    89,    91,    94,    91,     4,    29,    53,    61,    53,
      61,     4,    83,   106,    40,    83,    91,     4,    14,     4,
     124,   153,   153,   104,     4,     7,    14,    15,    22,    24,
      25,    27,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,   176,   153,    69,   100,     4,     5,    84,   175,
     176,   177,    84,    89,    84,    84,   177,    84,   177,     4,
     169,    37,    83,    84,    84,    86,    91,   153,   171,    88,
     177,   169,     4,     7,   176,     4,    91,    99,   102,   107,
     108,   123,   141,   142,   143,   145,   146,   147,   148,   152,
     155,   156,   159,   159,   163,   158,   160,   160,   161,   161,
     153,     4,     7,   176,     4,   153,   153,   153,   153,     4,
      45,    89,    44,    97,    23,   115,     4,   172,   174,   172,
     128,    84,   139,    97,     4,    97,     4,    60,     4,    60,
      83,   172,     4,   125,   172,    97,     5,     4,     5,    40,
      86,     6,    83,    83,   153,   153,    83,    91,    69,    90,
      84,    89,   153,    91,    84,    91,    84,    91,    84,   165,
     169,    89,   179,   179,    84,    83,    83,   167,    97,    91,
     158,    86,    83,    83,    84,    91,    41,    45,   153,     4,
     153,    27,   153,    23,    26,   116,    69,    84,    89,    84,
      84,    91,    27,    27,   169,    84,    91,    84,    27,    14,
     125,    88,   169,   169,     4,   178,   131,   153,   153,    91,
     179,   134,    41,    91,    91,    13,    84,   179,    86,    88,
     169,   169,    25,    27,   150,    25,   151,   169,   169,    91,
      97,   153,   153,    91,    45,    91,    47,    24,   153,    91,
      28,     3,     5,    30,    31,    32,    33,    79,    80,   173,
      91,     4,    91,    91,    97,    55,    57,    84,    97,    91,
      65,     4,    91,    84,    84,   153,    84,   136,   130,     4,
     175,     4,    27,    59,    91,   135,     4,   134,    27,   153,
      84,    84,    22,    91,   152,    22,    22,    91,   152,    84,
      84,    97,    27,    60,   110,    42,    91,    91,    97,   153,
      91,    91,    24,   117,    91,     3,     3,    97,    69,    97,
      97,    27,    91,    91,    27,    97,    91,    97,    90,     4,
      27,    59,    91,   137,   136,    83,    69,    90,     4,     5,
      41,    83,    91,     4,     4,     4,    27,     4,   153,    97,
      91,    91,   153,    97,    91,    27,    40,    91,   102,   153,
      97,    97,   110,    91,   117,    91,    15,    91,    99,   107,
     108,   109,   111,   112,   113,   114,   118,   119,   120,   121,
     122,   123,   126,   127,   133,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,    27,   173,    27,    27,    55,
      63,    27,    27,   153,     4,     5,   138,     4,     4,    27,
     178,   153,   153,     5,    41,    83,    91,    91,     4,    84,
     177,   134,    91,    94,    91,     4,    91,    24,    27,    24,
      27,    37,    91,    91,    91,   110,   110,    97,   117,    91,
      91,    91,    91,    91,    91,    91,    91,    91,    91,    91,
      91,    91,    51,    51,    55,    91,    91,    64,    63,    83,
      83,    89,    91,    91,    91,     4,    84,    91,     4,    84,
     177,   134,   134,     4,    91,    84,    27,     4,   134,    91,
      91,    22,    91,   152,    22,    91,    97,   110,    91,    91,
      91,    91,    91,    84,   177,   172,     5,    97,    91,    90,
     134,     4,    91,    84,    27,    27,    91,    91,     4,    91,
      27,    97,    91,    97,    91,    91,   110,    91,    84,    84,
      27,   153,    27,    91,    91,     4,     4,   134,   134,    91,
      97,     4,   150,   150,   151,   136,    91,    91,    59,     4,
     134,    91,    91,    27,    27,    27,    91,    27,   136,    97,
      91,    91,    27,     4,     4,    59,     4,    27,    27,     4,
      91,    91,    91,    91,     4,     4,    91,    91,    91
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    95,    96,    97,    97,    97,    98,    98,    98,    98,
      98,    98,    98,    98,    98,    98,    98,    98,    98,    98,
      98,    98,    98,    98,    98,    98,    98,    98,    98,    98,
      98,    98,    98,    98,    98,    99,    99,    99,    99,   100,
     100,   100,   100,   101,   101,   101,   102,   102,   102,   104,
     103,   103,   105,   105,   106,   106,   106,   106,   107,   107,
     108,   108,   108,   108,   108,   109,   110,   110,   110,   111,
     111,   111,   111,   111,   111,   112,   113,   114,   115,   115,
     116,   116,   117,   117,   117,   118,   118,   118,   118,   118,
     118,   118,   118,   118,   118,   118,   118,   118,   118,   118,
     118,   118,   118,   118,   118,   118,   118,   118,   118,   118,
     118,   118,   119,   119,   120,   120,   121,   122,   123,   123,
     123,   123,   123,   123,   123,   123,   124,   124,   125,   126,
     126,   126,   127,   128,   128,   130,   129,   131,   129,   132,
     132,   132,   133,   133,   133,   134,   134,   134,   135,   135,
     135,   135,   135,   135,   135,   135,   135,   135,   135,   136,
     136,   136,   137,   137,   137,   137,   137,   138,   138,   139,
     139,   140,   141,   141,   141,   141,   141,   141,   141,   141,
     142,   143,   143,   144,   145,   146,   147,   147,   148,   148,
     149,   149,   150,   150,   150,   150,   151,   151,   151,   151,
     151,   152,   152,   152,   152,   152,   152,   152,   152,   152,
     152,   152,   153,   154,   154,   155,   155,   156,   156,   157,
     157,   157,   158,   158,   158,   159,   159,   159,   160,   160,
     160,   161,   161,   161,   161,   161,   161,   162,   162,   162,
     162,   162,   163,   163,   163,   163,   163,   163,   163,   163,
     163,   163,   164,   164,   164,   164,   164,   164,   164,   164,
     164,   164,   164,   164,   164,   164,   164,   164,   165,   165,
     166,   166,   166,   166,   167,   167,   168,   168,   169,   169,
     170,   170,   171,   171,   172,   172,   173,   173,   173,   173,
     173,   173,   173,   173,   174,   174,   174,   174,   175,   175,
     176,   176,   176,   176,   176,   176,   176,   176,   176,   176,
     176,   176,   176,   176,   176,   176,   176,   176,   176,   176,
     176,   176,   176,   176,   176,   176,   176,   176,   176,   176,
     176,   176,   176,   176,   176,   176,   176,   176,   176,   176,
     176,   176,   176,   176,   176,   176,   176,   177,   177,   177,
     177,   177,   177,   178,   178,   179,   179
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       2,     1,     1,     2,     1,     2,     2,     2,     2,     2,
       2,     2,     2,     1,     1,     3,     3,     4,     4,     1,
       1,     1,     1,     1,     4,     3,     1,     1,     1,     0,
       4,     1,     1,     2,     1,     1,     1,     1,     2,     4,
       4,     4,     6,     6,     6,    10,     3,     2,     3,     7,
       8,     9,    10,     9,    11,     6,     7,     7,     5,     6,
       0,     3,     0,     2,     2,     2,     2,     2,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     2,     1,
       2,     1,     2,     2,     2,     2,     2,     2,     2,     2,
       1,     1,    10,    10,     9,    10,    10,     7,     2,     2,
       2,     2,     4,     4,     4,     6,     1,     4,     1,     9,
       7,    10,     2,     1,     3,     0,    11,     0,    10,     6,
       5,     9,    10,     9,    13,     0,     2,     2,     4,     6,
       5,     6,     9,     7,     8,     7,     9,    10,     9,     0,
       2,     2,     3,    10,    10,     9,     7,     1,     3,     1,
       3,     7,     4,     4,     3,     4,     4,     3,     3,     3,
       2,     1,     2,     2,     2,     2,     1,     2,     1,     2,
       6,     6,     3,     3,     6,     7,     0,     3,     6,     7,
       7,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     3,     1,     3,     1,     2,     1,
       3,     4,     1,     3,     3,     1,     3,     3,     1,     3,
       3,     1,     2,     2,     2,     4,     5,     1,     4,     3,
       6,     6,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     3,     1,     1,     2,     4,     1,
       1,     1,     1,     1,     3,     3,     5,     1,     3,     5,
       0,     3,     3,     5,     0,     3,     2,     3,     0,     1,
       1,     3,     1,     4,     0,     1,     1,     2,     2,     1,
       1,     1,     1,     1,     1,     3,     3,     5,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     3,     3,     6,
       6,     6,     9,     1,     2,     0,     2
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
#line 772 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3027 "src/parser.tab.c"
        break;

    case YYSYMBOL_STRING: /* STRING  */
#line 772 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3033 "src/parser.tab.c"
        break;

    case YYSYMBOL_LENS_CONTENT: /* LENS_CONTENT  */
#line 772 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3039 "src/parser.tab.c"
        break;

    case YYSYMBOL_QUALIFIED_IDENT: /* QUALIFIED_IDENT  */
#line 772 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3045 "src/parser.tab.c"
        break;

    case YYSYMBOL_MODIFIER_PREFIX: /* MODIFIER_PREFIX  */
#line 772 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3051 "src/parser.tab.c"
        break;

    case YYSYMBOL_MODIFIER_VALUE: /* MODIFIER_VALUE  */
#line 772 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3057 "src/parser.tab.c"
        break;

    case YYSYMBOL_SQL_TEXT: /* SQL_TEXT  */
#line 772 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3063 "src/parser.tab.c"
        break;

    case YYSYMBOL_program: /* program  */
#line 795 "src/parser.y"
            { (void) ((*yyvaluep).stmt_list); }
#line 3069 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement_list: /* statement_list  */
#line 734 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3075 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement: /* statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3081 "src/parser.tab.c"
        break;

    case YYSYMBOL_assignment: /* assignment  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3087 "src/parser.tab.c"
        break;

    case YYSYMBOL_lvalue: /* lvalue  */
#line 773 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3093 "src/parser.tab.c"
        break;

    case YYSYMBOL_variable_name: /* variable_name  */
#line 772 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3099 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_lens: /* comparison_lens  */
#line 780 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 3105 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_name: /* modifier_name  */
#line 772 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3111 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_word: /* modifier_word  */
#line 772 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3117 "src/parser.tab.c"
        break;

    case YYSYMBOL_print_statement: /* print_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3123 "src/parser.tab.c"
        break;

    case YYSYMBOL_call_statement: /* call_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3129 "src/parser.tab.c"
        break;

    case YYSYMBOL_with_lock_statement: /* with_lock_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3135 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_end: /* for_end  */
#line 772 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3141 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_each_statement: /* for_each_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3147 "src/parser.tab.c"
        break;

    case YYSYMBOL_do_loop_statement: /* do_loop_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3153 "src/parser.tab.c"
        break;

    case YYSYMBOL_while_statement: /* while_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3159 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement: /* consider_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3165 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_branch_list: /* consider_branch_list  */
#line 778 "src/parser.y"
            { ast_free_consider_branch_list(((*yyvaluep).consider_branch_list)); }
#line 3171 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_else_opt: /* consider_else_opt  */
#line 734 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3177 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement_list: /* consider_statement_list  */
#line 734 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3183 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_body_statement: /* consider_body_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3189 "src/parser.tab.c"
        break;

    case YYSYMBOL_function_statement: /* function_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3195 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_statement: /* modifier_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3201 "src/parser.tab.c"
        break;

    case YYSYMBOL_program_statement: /* program_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3207 "src/parser.tab.c"
        break;

    case YYSYMBOL_library_statement: /* library_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3213 "src/parser.tab.c"
        break;

    case YYSYMBOL_use_statement: /* use_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3219 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_signature: /* modifier_signature  */
#line 781 "src/parser.y"
            { ast_free_modifier_signature(((*yyvaluep).modifier_signature)); }
#line 3225 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_context: /* modifier_context  */
#line 772 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3231 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_statement: /* watch_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3237 "src/parser.tab.c"
        break;

    case YYSYMBOL_unwatch_statement: /* unwatch_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3243 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_list: /* watch_target_list  */
#line 779 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3249 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_statement: /* server_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3255 "src/parser.tab.c"
        break;

    case YYSYMBOL_gsql_block: /* gsql_block  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3261 "src/parser.tab.c"
        break;

    case YYSYMBOL_window_block: /* window_block  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3267 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item_list: /* server_item_list  */
#line 785 "src/parser.y"
            { ast_free_server_item_list(((*yyvaluep).server_item_list)); }
#line 3273 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item: /* server_item  */
#line 784 "src/parser.y"
            { AstServerItemList one = ast_server_item_list_append(ast_server_item_list_empty(), ((*yyvaluep).server_item)); ast_free_server_item_list(one); }
#line 3279 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_string_list: /* server_string_list  */
#line 779 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3285 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_path: /* watch_target_path  */
#line 772 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3291 "src/parser.tab.c"
        break;

    case YYSYMBOL_without_watchers_statement: /* without_watchers_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3297 "src/parser.tab.c"
        break;

    case YYSYMBOL_on_error_statement: /* on_error_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3303 "src/parser.tab.c"
        break;

    case YYSYMBOL_error_statement: /* error_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3309 "src/parser.tab.c"
        break;

    case YYSYMBOL_return_statement: /* return_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3315 "src/parser.tab.c"
        break;

    case YYSYMBOL_label_statement: /* label_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3321 "src/parser.tab.c"
        break;

    case YYSYMBOL_goto_statement: /* goto_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3327 "src/parser.tab.c"
        break;

    case YYSYMBOL_gosub_statement: /* gosub_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3333 "src/parser.tab.c"
        break;

    case YYSYMBOL_break_statement: /* break_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3339 "src/parser.tab.c"
        break;

    case YYSYMBOL_continue_statement: /* continue_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3345 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_statement: /* if_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3351 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_block_tail: /* if_block_tail  */
#line 734 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3357 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_inline_tail: /* if_inline_tail  */
#line 734 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3363 "src/parser.tab.c"
        break;

    case YYSYMBOL_inline_statement: /* inline_statement  */
#line 774 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3369 "src/parser.tab.c"
        break;

    case YYSYMBOL_expression: /* expression  */
#line 773 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3375 "src/parser.tab.c"
        break;

    case YYSYMBOL_or_expression: /* or_expression  */
#line 773 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3381 "src/parser.tab.c"
        break;

    case YYSYMBOL_and_expression: /* and_expression  */
#line 773 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3387 "src/parser.tab.c"
        break;

    case YYSYMBOL_not_expression: /* not_expression  */
#line 773 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3393 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_expression: /* comparison_expression  */
#line 773 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3399 "src/parser.tab.c"
        break;

    case YYSYMBOL_set_expression: /* set_expression  */
#line 773 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3405 "src/parser.tab.c"
        break;

    case YYSYMBOL_additive_expression: /* additive_expression  */
#line 773 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3411 "src/parser.tab.c"
        break;

    case YYSYMBOL_multiplicative_expression: /* multiplicative_expression  */
#line 773 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3417 "src/parser.tab.c"
        break;

    case YYSYMBOL_unary_expression: /* unary_expression  */
#line 773 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3423 "src/parser.tab.c"
        break;

    case YYSYMBOL_postfix_expression: /* postfix_expression  */
#line 773 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3429 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_operator: /* comparison_operator  */
#line 772 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3435 "src/parser.tab.c"
        break;

    case YYSYMBOL_primary: /* primary  */
#line 773 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3441 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_literal: /* record_literal  */
#line 773 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3447 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_suffix: /* ident_suffix  */
#line 782 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3453 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_dot_suffix: /* ident_dot_suffix  */
#line 782 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3459 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list_opt: /* argument_list_opt  */
#line 776 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3465 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list: /* argument_list  */
#line 776 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3471 "src/parser.tab.c"
        break;

    case YYSYMBOL_array_argument_list: /* array_argument_list  */
#line 776 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3477 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list_opt: /* parameter_list_opt  */
#line 779 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3483 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_default: /* parameter_default  */
#line 773 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3489 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list: /* parameter_list  */
#line 779 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3495 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_name: /* field_name  */
#line 772 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3501 "src/parser.tab.c"
        break;

    case YYSYMBOL_dot_field_name: /* dot_field_name  */
#line 772 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3507 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_field_list: /* record_field_list  */
#line 777 "src/parser.y"
            { ast_free_record_field_list(((*yyvaluep).record_field_list)); }
#line 3513 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_policy: /* field_policy  */
#line 783 "src/parser.y"
            { ast_free_expr(((*yyvaluep).field_policy).reset_expr); }
#line 3519 "src/parser.tab.c"
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
#line 800 "src/parser.y"
                     { ctx->parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3825 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 804 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3831 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 805 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3837 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 806 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3843 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 810 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3849 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 811 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3855 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 812 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3861 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 813 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3867 "src/parser.tab.c"
    break;

  case 10: /* statement: window_block  */
#line 814 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3873 "src/parser.tab.c"
    break;

  case 11: /* statement: gsql_block  */
#line 815 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3879 "src/parser.tab.c"
    break;

  case 12: /* statement: for_each_statement  */
#line 816 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3885 "src/parser.tab.c"
    break;

  case 13: /* statement: while_statement  */
#line 817 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3891 "src/parser.tab.c"
    break;

  case 14: /* statement: do_loop_statement  */
#line 818 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3897 "src/parser.tab.c"
    break;

  case 15: /* statement: consider_statement  */
#line 819 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3903 "src/parser.tab.c"
    break;

  case 16: /* statement: function_statement  */
#line 820 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3909 "src/parser.tab.c"
    break;

  case 17: /* statement: modifier_statement  */
#line 821 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3915 "src/parser.tab.c"
    break;

  case 18: /* statement: program_statement  */
#line 822 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3921 "src/parser.tab.c"
    break;

  case 19: /* statement: library_statement  */
#line 823 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3927 "src/parser.tab.c"
    break;

  case 20: /* statement: use_statement NEWLINE  */
#line 824 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3933 "src/parser.tab.c"
    break;

  case 21: /* statement: watch_statement  */
#line 825 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3939 "src/parser.tab.c"
    break;

  case 22: /* statement: server_statement  */
#line 826 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3945 "src/parser.tab.c"
    break;

  case 23: /* statement: unwatch_statement NEWLINE  */
#line 827 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3951 "src/parser.tab.c"
    break;

  case 24: /* statement: without_watchers_statement  */
#line 828 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3957 "src/parser.tab.c"
    break;

  case 25: /* statement: on_error_statement NEWLINE  */
#line 829 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3963 "src/parser.tab.c"
    break;

  case 26: /* statement: error_statement NEWLINE  */
#line 830 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3969 "src/parser.tab.c"
    break;

  case 27: /* statement: return_statement NEWLINE  */
#line 831 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3975 "src/parser.tab.c"
    break;

  case 28: /* statement: label_statement NEWLINE  */
#line 832 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3981 "src/parser.tab.c"
    break;

  case 29: /* statement: goto_statement NEWLINE  */
#line 833 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3987 "src/parser.tab.c"
    break;

  case 30: /* statement: gosub_statement NEWLINE  */
#line 834 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3993 "src/parser.tab.c"
    break;

  case 31: /* statement: break_statement NEWLINE  */
#line 835 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3999 "src/parser.tab.c"
    break;

  case 32: /* statement: continue_statement NEWLINE  */
#line 836 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4005 "src/parser.tab.c"
    break;

  case 33: /* statement: if_statement  */
#line 837 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4011 "src/parser.tab.c"
    break;

  case 34: /* statement: DIM  */
#line 843 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 4024 "src/parser.tab.c"
    break;

  case 35: /* assignment: lvalue OP_EQ expression  */
#line 854 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 4030 "src/parser.tab.c"
    break;

  case 36: /* assignment: lvalue compound_op expression  */
#line 859 "src/parser.y"
                                    { (yyval.stmt) = ast_assign_op((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr), (yyvsp[-1].op_char)); }
#line 4036 "src/parser.tab.c"
    break;

  case 37: /* assignment: lvalue comparison_lens compound_op expression  */
#line 860 "src/parser.y"
                                                    {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign_op((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr), (yyvsp[-1].op_char));
      }
#line 4050 "src/parser.tab.c"
    break;

  case 38: /* assignment: lvalue comparison_lens OP_EQ expression  */
#line 872 "src/parser.y"
                                              {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 4064 "src/parser.tab.c"
    break;

  case 39: /* compound_op: PLUS_EQ  */
#line 884 "src/parser.y"
               { (yyval.op_char) = '+'; }
#line 4070 "src/parser.tab.c"
    break;

  case 40: /* compound_op: MINUS_EQ  */
#line 885 "src/parser.y"
               { (yyval.op_char) = '-'; }
#line 4076 "src/parser.tab.c"
    break;

  case 41: /* compound_op: STAR_EQ  */
#line 886 "src/parser.y"
               { (yyval.op_char) = '*'; }
#line 4082 "src/parser.tab.c"
    break;

  case 42: /* compound_op: SLASH_EQ  */
#line 887 "src/parser.y"
               { (yyval.op_char) = '/'; }
#line 4088 "src/parser.tab.c"
    break;

  case 43: /* lvalue: variable_name  */
#line 891 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4094 "src/parser.tab.c"
    break;

  case 44: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 892 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4100 "src/parser.tab.c"
    break;

  case 45: /* lvalue: lvalue DOT dot_field_name  */
#line 893 "src/parser.y"
                                             { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4106 "src/parser.tab.c"
    break;

  case 46: /* variable_name: IDENT  */
#line 897 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 4112 "src/parser.tab.c"
    break;

  case 47: /* variable_name: END  */
#line 898 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 4118 "src/parser.tab.c"
    break;

  case 48: /* variable_name: NEXT  */
#line 899 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 4124 "src/parser.tab.c"
    break;

  case 49: /* $@1: %empty  */
#line 910 "src/parser.y"
             { lexer_begin_lens_content(ctx->active_lexer); }
#line 4130 "src/parser.tab.c"
    break;

  case 50: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 910 "src/parser.y"
                                                                                  {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 4138 "src/parser.tab.c"
    break;

  case 51: /* comparison_lens: MODIFIER_PREFIX  */
#line 918 "src/parser.y"
                      { (yyval.modifier) = parse_modifier_use((yyvsp[0].text)); }
#line 4144 "src/parser.tab.c"
    break;

  case 52: /* modifier_name: modifier_word  */
#line 922 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 4150 "src/parser.tab.c"
    break;

  case 53: /* modifier_name: modifier_name modifier_word  */
#line 923 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 4156 "src/parser.tab.c"
    break;

  case 54: /* modifier_word: IDENT  */
#line 927 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4162 "src/parser.tab.c"
    break;

  case 55: /* modifier_word: TO  */
#line 928 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 4168 "src/parser.tab.c"
    break;

  case 56: /* modifier_word: END  */
#line 929 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 4174 "src/parser.tab.c"
    break;

  case 57: /* modifier_word: NEXT  */
#line 930 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 4180 "src/parser.tab.c"
    break;

  case 58: /* print_statement: PRINT expression  */
#line 934 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 4186 "src/parser.tab.c"
    break;

  case 59: /* print_statement: PRINT TO ERROR_VALUE expression  */
#line 940 "src/parser.y"
                                      { (yyval.stmt) = ast_print_error((yyvsp[0].expr)); }
#line 4192 "src/parser.tab.c"
    break;

  case 60: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 944 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 4198 "src/parser.tab.c"
    break;

  case 61: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 945 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 4209 "src/parser.tab.c"
    break;

  case 62: /* call_statement: lvalue DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 951 "src/parser.y"
                                                       {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 4219 "src/parser.tab.c"
    break;

  case 63: /* call_statement: lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 956 "src/parser.y"
                                                                 {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 4233 "src/parser.tab.c"
    break;

  case 64: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 965 "src/parser.y"
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
#line 4248 "src/parser.tab.c"
    break;

  case 65: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 978 "src/parser.y"
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
#line 4271 "src/parser.tab.c"
    break;

  case 66: /* for_end: END FOR NEWLINE  */
#line 1007 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 4277 "src/parser.tab.c"
    break;

  case 67: /* for_end: NEXT NEWLINE  */
#line 1008 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 4283 "src/parser.tab.c"
    break;

  case 68: /* for_end: NEXT variable_name NEWLINE  */
#line 1009 "src/parser.y"
                                 { (yyval.text) = (yyvsp[-1].text); }
#line 4289 "src/parser.tab.c"
    break;

  case 69: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list for_end  */
#line 1013 "src/parser.y"
                                                             {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4298 "src/parser.tab.c"
    break;

  case 70: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list for_end  */
#line 1017 "src/parser.y"
                                                                  {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4307 "src/parser.tab.c"
    break;

  case 71: /* for_each_statement: FOR IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1033 "src/parser.y"
                                                                         {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { YYERROR; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4317 "src/parser.tab.c"
    break;

  case 72: /* for_each_statement: FOR EACH IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1038 "src/parser.y"
                                                                              {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { YYERROR; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4327 "src/parser.tab.c"
    break;

  case 73: /* for_each_statement: FOR IDENT OP_EQ expression TO expression NEWLINE statement_list for_end  */
#line 1046 "src/parser.y"
                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].expr), NULL, (yyvsp[-1].stmt_list));
      }
#line 4336 "src/parser.tab.c"
    break;

  case 74: /* for_each_statement: FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list for_end  */
#line 1050 "src/parser.y"
                                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-9].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-9].text), (yyvsp[-7].expr), (yyvsp[-5].expr), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4345 "src/parser.tab.c"
    break;

  case 75: /* do_loop_statement: DO NEWLINE statement_list UNTIL expression NEWLINE  */
#line 1071 "src/parser.y"
                                                         {
        (yyval.stmt) = ast_do_loop((yyvsp[-3].stmt_list), (yyvsp[-1].expr));
      }
#line 4353 "src/parser.tab.c"
    break;

  case 76: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 1077 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 4361 "src/parser.tab.c"
    break;

  case 77: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 1083 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 4369 "src/parser.tab.c"
    break;

  case 78: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 1089 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 4377 "src/parser.tab.c"
    break;

  case 79: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 1092 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 4385 "src/parser.tab.c"
    break;

  case 80: /* consider_else_opt: %empty  */
#line 1098 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4391 "src/parser.tab.c"
    break;

  case 81: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 1099 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 4397 "src/parser.tab.c"
    break;

  case 82: /* consider_statement_list: %empty  */
#line 1103 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4403 "src/parser.tab.c"
    break;

  case 83: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 1104 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 4409 "src/parser.tab.c"
    break;

  case 84: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 1105 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 4415 "src/parser.tab.c"
    break;

  case 85: /* consider_body_statement: assignment NEWLINE  */
#line 1109 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4421 "src/parser.tab.c"
    break;

  case 86: /* consider_body_statement: print_statement NEWLINE  */
#line 1110 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4427 "src/parser.tab.c"
    break;

  case 87: /* consider_body_statement: call_statement NEWLINE  */
#line 1111 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4433 "src/parser.tab.c"
    break;

  case 88: /* consider_body_statement: with_lock_statement  */
#line 1112 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4439 "src/parser.tab.c"
    break;

  case 89: /* consider_body_statement: window_block  */
#line 1113 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4445 "src/parser.tab.c"
    break;

  case 90: /* consider_body_statement: for_each_statement  */
#line 1114 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4451 "src/parser.tab.c"
    break;

  case 91: /* consider_body_statement: while_statement  */
#line 1115 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4457 "src/parser.tab.c"
    break;

  case 92: /* consider_body_statement: do_loop_statement  */
#line 1116 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4463 "src/parser.tab.c"
    break;

  case 93: /* consider_body_statement: consider_statement  */
#line 1117 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4469 "src/parser.tab.c"
    break;

  case 94: /* consider_body_statement: function_statement  */
#line 1118 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4475 "src/parser.tab.c"
    break;

  case 95: /* consider_body_statement: modifier_statement  */
#line 1119 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4481 "src/parser.tab.c"
    break;

  case 96: /* consider_body_statement: program_statement  */
#line 1120 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4487 "src/parser.tab.c"
    break;

  case 97: /* consider_body_statement: library_statement  */
#line 1121 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4493 "src/parser.tab.c"
    break;

  case 98: /* consider_body_statement: use_statement NEWLINE  */
#line 1122 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4499 "src/parser.tab.c"
    break;

  case 99: /* consider_body_statement: watch_statement  */
#line 1123 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4505 "src/parser.tab.c"
    break;

  case 100: /* consider_body_statement: unwatch_statement NEWLINE  */
#line 1124 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4511 "src/parser.tab.c"
    break;

  case 101: /* consider_body_statement: without_watchers_statement  */
#line 1125 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4517 "src/parser.tab.c"
    break;

  case 102: /* consider_body_statement: on_error_statement NEWLINE  */
#line 1126 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4523 "src/parser.tab.c"
    break;

  case 103: /* consider_body_statement: error_statement NEWLINE  */
#line 1127 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4529 "src/parser.tab.c"
    break;

  case 104: /* consider_body_statement: return_statement NEWLINE  */
#line 1128 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4535 "src/parser.tab.c"
    break;

  case 105: /* consider_body_statement: label_statement NEWLINE  */
#line 1129 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4541 "src/parser.tab.c"
    break;

  case 106: /* consider_body_statement: goto_statement NEWLINE  */
#line 1130 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4547 "src/parser.tab.c"
    break;

  case 107: /* consider_body_statement: gosub_statement NEWLINE  */
#line 1131 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4553 "src/parser.tab.c"
    break;

  case 108: /* consider_body_statement: break_statement NEWLINE  */
#line 1132 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4559 "src/parser.tab.c"
    break;

  case 109: /* consider_body_statement: continue_statement NEWLINE  */
#line 1133 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4565 "src/parser.tab.c"
    break;

  case 110: /* consider_body_statement: if_statement  */
#line 1134 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4571 "src/parser.tab.c"
    break;

  case 111: /* consider_body_statement: DIM  */
#line 1140 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 4584 "src/parser.tab.c"
    break;

  case 112: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1151 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4592 "src/parser.tab.c"
    break;

  case 113: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1154 "src/parser.y"
                                                                                                            {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4601 "src/parser.tab.c"
    break;

  case 114: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 1161 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 4609 "src/parser.tab.c"
    break;

  case 115: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 1164 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 4617 "src/parser.tab.c"
    break;

  case 116: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 1170 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4625 "src/parser.tab.c"
    break;

  case 117: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 1176 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 4633 "src/parser.tab.c"
    break;

  case 118: /* use_statement: USE IDENT  */
#line 1182 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4639 "src/parser.tab.c"
    break;

  case 119: /* use_statement: LOAD IDENT  */
#line 1183 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4645 "src/parser.tab.c"
    break;

  case 120: /* use_statement: USE STRING  */
#line 1184 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4651 "src/parser.tab.c"
    break;

  case 121: /* use_statement: LOAD STRING  */
#line 1185 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4657 "src/parser.tab.c"
    break;

  case 122: /* use_statement: LOAD IDENT AS IDENT  */
#line 1186 "src/parser.y"
                          { (yyval.stmt) = ast_use((yyvsp[-2].text), NULL, (yyvsp[0].text)); }
#line 4663 "src/parser.tab.c"
    break;

  case 123: /* use_statement: USE IDENT IDENT STRING  */
#line 1187 "src/parser.y"
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
#line 4684 "src/parser.tab.c"
    break;

  case 124: /* use_statement: LOAD IDENT IDENT STRING  */
#line 1203 "src/parser.y"
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
#line 4705 "src/parser.tab.c"
    break;

  case 125: /* use_statement: LOAD IDENT IDENT STRING AS IDENT  */
#line 1219 "src/parser.y"
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
#line 4728 "src/parser.tab.c"
    break;

  case 126: /* modifier_signature: modifier_name  */
#line 1240 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 4734 "src/parser.tab.c"
    break;

  case 127: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 1241 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 4740 "src/parser.tab.c"
    break;

  case 128: /* modifier_context: IDENT  */
#line 1245 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4746 "src/parser.tab.c"
    break;

  case 129: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1249 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4754 "src/parser.tab.c"
    break;

  case 130: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 1252 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 4762 "src/parser.tab.c"
    break;

  case 131: /* watch_statement: WATCH IDENT LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1260 "src/parser.y"
                                                                                           {
        (yyval.stmt) = ast_watch((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4770 "src/parser.tab.c"
    break;

  case 132: /* unwatch_statement: UNWATCH expression  */
#line 1266 "src/parser.y"
                         { (yyval.stmt) = ast_unwatch((yyvsp[0].expr)); }
#line 4776 "src/parser.tab.c"
    break;

  case 133: /* watch_target_list: watch_target_path  */
#line 1270 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4782 "src/parser.tab.c"
    break;

  case 134: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 1271 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4788 "src/parser.tab.c"
    break;

  case 135: /* @2: %empty  */
#line 1291 "src/parser.y"
      { (yyval.stmt) = NULL;   /* the mid-rule carries no value; typed so bison stays quiet */
        server_head_note(ctx, (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column); }
#line 4795 "src/parser.tab.c"
    break;

  case 136: /* server_statement: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE @2 server_item_list END IDENT NEWLINE  */
#line 1293 "src/parser.y"
                                         {
        ctx->bad_block_word[0] = '\0';
        (yyval.stmt) = ast_server((yyvsp[-10].text), (yyvsp[-9].text), (yyvsp[-7].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4804 "src/parser.tab.c"
    break;

  case 137: /* @3: %empty  */
#line 1298 "src/parser.y"
      { (yyval.stmt) = NULL;   /* the mid-rule carries no value; typed so bison stays quiet */
        server_head_note(ctx, (yyvsp[-4].text), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4811 "src/parser.tab.c"
    break;

  case 138: /* server_statement: IDENT IDENT LPAREN RPAREN NEWLINE @3 server_item_list END IDENT NEWLINE  */
#line 1300 "src/parser.y"
                                         {
        ctx->bad_block_word[0] = '\0';
        (yyval.stmt) = ast_server((yyvsp[-9].text), (yyvsp[-8].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4820 "src/parser.tab.c"
    break;

  case 139: /* gsql_block: DATABASE_OPEN IDENT LPAREN record_field_list RPAREN NEWLINE  */
#line 1320 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_database((yyvsp[-4].text), (yyvsp[-2].record_field_list), (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 4828 "src/parser.tab.c"
    break;

  case 140: /* gsql_block: DATABASE_OPEN IDENT LPAREN RPAREN NEWLINE  */
#line 1323 "src/parser.y"
                                                {
        (yyval.stmt) = ast_database((yyvsp[-3].text), ast_record_field_list_empty(), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 4836 "src/parser.tab.c"
    break;

  case 141: /* gsql_block: SQL_OPEN IDENT ON IDENT NEWLINE SQL_TEXT END IDENT NEWLINE  */
#line 1326 "src/parser.y"
                                                                 {
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_sql((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].text), (yylsp[-3]).first_line, (yylsp[-3]).first_column, (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 4845 "src/parser.tab.c"
    break;

  case 142: /* window_block: WINDOW_OPEN IDENT LPAREN record_field_list RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1333 "src/parser.y"
                                                                                                   {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), NULL, NULL, NULL, (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4854 "src/parser.tab.c"
    break;

  case 143: /* window_block: WINDOW_OPEN IDENT LPAREN RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1337 "src/parser.y"
                                                                                 {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text), NULL, NULL, NULL,
                        (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 4864 "src/parser.tab.c"
    break;

  case 144: /* window_block: WINDOW_OPEN IDENT LPAREN record_field_list RPAREN TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1347 "src/parser.y"
                                                                                                                  {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-11].text), (yyvsp[-9].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text),
                        (yylsp[-12]).first_line, (yylsp[-12]).first_column);
      }
#line 4874 "src/parser.tab.c"
    break;

  case 145: /* window_item_list: %empty  */
#line 1355 "src/parser.y"
             { (yyval.window_item_list) = ast_window_item_list_empty(); }
#line 4880 "src/parser.tab.c"
    break;

  case 146: /* window_item_list: window_item_list NEWLINE  */
#line 1356 "src/parser.y"
                               { (yyval.window_item_list) = (yyvsp[-1].window_item_list); }
#line 4886 "src/parser.tab.c"
    break;

  case 147: /* window_item_list: window_item_list window_item  */
#line 1357 "src/parser.y"
                                   { (yyval.window_item_list) = ast_window_item_list_append((yyvsp[-1].window_item_list), (yyvsp[0].window_item)); }
#line 4892 "src/parser.tab.c"
    break;

  case 148: /* window_item: IDENT LPAREN RPAREN NEWLINE  */
#line 1362 "src/parser.y"
                                  {
        (yyval.window_item) = ast_window_leaf((yyvsp[-3].text), NULL, ast_record_field_list_empty(),
                             (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 4901 "src/parser.tab.c"
    break;

  case 149: /* window_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE  */
#line 1367 "src/parser.y"
                                                          {
        (yyval.window_item) = ast_window_leaf((yyvsp[-5].text), (yyvsp[-4].text), (yyvsp[-2].record_field_list), (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 4909 "src/parser.tab.c"
    break;

  case 150: /* window_item: IDENT IDENT LPAREN RPAREN NEWLINE  */
#line 1371 "src/parser.y"
                                        {
        (yyval.window_item) = ast_window_leaf((yyvsp[-4].text), (yyvsp[-3].text), ast_record_field_list_empty(),
                             (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 4918 "src/parser.tab.c"
    break;

  case 151: /* window_item: IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1376 "src/parser.y"
                                                       {
        (yyval.window_item) = ast_window_container((yyvsp[-5].text), NULL, ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                  (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 4927 "src/parser.tab.c"
    break;

  case 152: /* window_item: IDENT LPAREN record_field_list RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1381 "src/parser.y"
                                                                                       {
        (yyval.window_item) = ast_window_container((yyvsp[-8].text), NULL, (yyvsp[-6].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 4935 "src/parser.tab.c"
    break;

  case 153: /* window_item: IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1395 "src/parser.y"
                                                             {
        (yyval.window_item) = ast_window_named_form((yyvsp[-6].text), (yyvsp[-5].text), NULL, NULL, NULL, (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                   (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 4944 "src/parser.tab.c"
    break;

  case 154: /* window_item: IDENT IDENT STRING NEWLINE window_item_list END IDENT NEWLINE  */
#line 1410 "src/parser.y"
                                                                    {
        (yyval.window_item) = ast_window_named_container((yyvsp[-7].text), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                        (yylsp[-7]).first_line, (yylsp[-7]).first_column);
      }
#line 4953 "src/parser.tab.c"
    break;

  case 155: /* window_item: IDENT STRING NEWLINE window_item_list END IDENT NEWLINE  */
#line 1415 "src/parser.y"
                                                              {
        (yyval.window_item) = ast_window_container((yyvsp[-6].text), (yyvsp[-5].text), ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                  (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 4962 "src/parser.tab.c"
    break;

  case 156: /* window_item: IDENT TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1426 "src/parser.y"
                                                                      {
        (yyval.window_item) = ast_window_form((yyvsp[-8].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                             (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 4971 "src/parser.tab.c"
    break;

  case 157: /* window_item: IDENT IDENT TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1441 "src/parser.y"
                                                                            {
        (yyval.window_item) = ast_window_named_form((yyvsp[-9].text), (yyvsp[-8].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                   (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4980 "src/parser.tab.c"
    break;

  case 158: /* window_item: ON IDENT DOT IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1446 "src/parser.y"
                                                               {
        (yyval.window_item) = ast_window_handler((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 4988 "src/parser.tab.c"
    break;

  case 159: /* server_item_list: %empty  */
#line 1452 "src/parser.y"
             { (yyval.server_item_list) = ast_server_item_list_empty(); }
#line 4994 "src/parser.tab.c"
    break;

  case 160: /* server_item_list: server_item_list NEWLINE  */
#line 1453 "src/parser.y"
                               { (yyval.server_item_list) = (yyvsp[-1].server_item_list); }
#line 5000 "src/parser.tab.c"
    break;

  case 161: /* server_item_list: server_item_list server_item  */
#line 1454 "src/parser.y"
                                   { (yyval.server_item_list) = ast_server_item_list_append((yyvsp[-1].server_item_list), (yyvsp[0].server_item)); }
#line 5006 "src/parser.tab.c"
    break;

  case 162: /* server_item: IDENT server_string_list NEWLINE  */
#line 1458 "src/parser.y"
                                       {
        (yyval.server_item) = ast_server_directive((yyvsp[-2].text), (yyvsp[-1].name_list), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 5014 "src/parser.tab.c"
    break;

  case 163: /* server_item: IDENT STRING LPAREN parameter_list_opt RPAREN NEWLINE statement_list END IDENT NEWLINE  */
#line 1461 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_handler((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5022 "src/parser.tab.c"
    break;

  case 164: /* server_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1464 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_site((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5030 "src/parser.tab.c"
    break;

  case 165: /* server_item: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1467 "src/parser.y"
                                                                           {
        (yyval.server_item) = ast_server_site((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5038 "src/parser.tab.c"
    break;

  case 166: /* server_item: ON IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1470 "src/parser.y"
                                                     {
        (yyval.server_item) = ast_server_hook((yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5046 "src/parser.tab.c"
    break;

  case 167: /* server_string_list: STRING  */
#line 1476 "src/parser.y"
             { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5052 "src/parser.tab.c"
    break;

  case 168: /* server_string_list: server_string_list COMMA STRING  */
#line 1477 "src/parser.y"
                                      { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5058 "src/parser.tab.c"
    break;

  case 169: /* watch_target_path: variable_name  */
#line 1481 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 5064 "src/parser.tab.c"
    break;

  case 170: /* watch_target_path: watch_target_path DOT IDENT  */
#line 1482 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 5070 "src/parser.tab.c"
    break;

  case 171: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 1486 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 5078 "src/parser.tab.c"
    break;

  case 172: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 1492 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 5084 "src/parser.tab.c"
    break;

  case 173: /* on_error_statement: ON ERROR_VALUE GOTO NEXT  */
#line 1493 "src/parser.y"
                               { (yyval.stmt) = ast_on_error_goto_next(); }
#line 5090 "src/parser.tab.c"
    break;

  case 174: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 1494 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 5096 "src/parser.tab.c"
    break;

  case 175: /* on_error_statement: ON IDENT GOTO NEXT  */
#line 1495 "src/parser.y"
                         {
        if (!warn_channel_ok(ctx, (yyvsp[-2].text), (yylsp[-2]).first_line, (yylsp[-2]).first_column)) { YYERROR; }
        free((yyvsp[-2].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_NEXT);
      }
#line 5106 "src/parser.tab.c"
    break;

  case 176: /* on_error_statement: ON IDENT GOTO IDENT  */
#line 1500 "src/parser.y"
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
#line 5124 "src/parser.tab.c"
    break;

  case 177: /* on_error_statement: ON IDENT STOP  */
#line 1513 "src/parser.y"
                    {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_STOP);
      }
#line 5134 "src/parser.tab.c"
    break;

  case 178: /* on_error_statement: ON IDENT PRINT  */
#line 1518 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_PRINT);
      }
#line 5144 "src/parser.tab.c"
    break;

  case 179: /* on_error_statement: ON IDENT IDENT  */
#line 1523 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        int mode = warn_mode_word(ctx, (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column);
        if (mode < 0) { free((yyvsp[-1].text)); free((yyvsp[0].text)); YYERROR; }
        free((yyvsp[-1].text)); free((yyvsp[0].text));
        (yyval.stmt) = ast_on_warning(mode);
      }
#line 5156 "src/parser.tab.c"
    break;

  case 180: /* error_statement: ERROR_VALUE expression  */
#line 1533 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 5162 "src/parser.tab.c"
    break;

  case 181: /* return_statement: RETURN  */
#line 1537 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 5168 "src/parser.tab.c"
    break;

  case 182: /* return_statement: RETURN expression  */
#line 1538 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 5174 "src/parser.tab.c"
    break;

  case 183: /* label_statement: variable_name COLON  */
#line 1542 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 5180 "src/parser.tab.c"
    break;

  case 184: /* goto_statement: GOTO variable_name  */
#line 1549 "src/parser.y"
                         { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 5186 "src/parser.tab.c"
    break;

  case 185: /* gosub_statement: GOSUB variable_name  */
#line 1553 "src/parser.y"
                          { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 5192 "src/parser.tab.c"
    break;

  case 186: /* break_statement: BREAK  */
#line 1562 "src/parser.y"
            { (yyval.stmt) = ast_break(NULL); }
#line 5198 "src/parser.tab.c"
    break;

  case 187: /* break_statement: BREAK IDENT  */
#line 1563 "src/parser.y"
                  { (yyval.stmt) = ast_break((yyvsp[0].text)); }
#line 5204 "src/parser.tab.c"
    break;

  case 188: /* continue_statement: CONTINUE  */
#line 1567 "src/parser.y"
               { (yyval.stmt) = ast_continue(NULL); }
#line 5210 "src/parser.tab.c"
    break;

  case 189: /* continue_statement: CONTINUE IDENT  */
#line 1568 "src/parser.y"
                     { (yyval.stmt) = ast_continue((yyvsp[0].text)); }
#line 5216 "src/parser.tab.c"
    break;

  case 190: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1572 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 5225 "src/parser.tab.c"
    break;

  case 191: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1576 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 5234 "src/parser.tab.c"
    break;

  case 192: /* if_block_tail: END IF NEWLINE  */
#line 1583 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 5242 "src/parser.tab.c"
    break;

  case 193: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 1586 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 5250 "src/parser.tab.c"
    break;

  case 194: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1589 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 5258 "src/parser.tab.c"
    break;

  case 195: /* if_block_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1598 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5270 "src/parser.tab.c"
    break;

  case 196: /* if_inline_tail: %empty  */
#line 1608 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 5278 "src/parser.tab.c"
    break;

  case 197: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 1611 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 5286 "src/parser.tab.c"
    break;

  case 198: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1614 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 5294 "src/parser.tab.c"
    break;

  case 199: /* if_inline_tail: ELSE IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1620 "src/parser.y"
                                                                      {
        AstStmt *inner = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5306 "src/parser.tab.c"
    break;

  case 200: /* if_inline_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1627 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5318 "src/parser.tab.c"
    break;

  case 201: /* inline_statement: assignment  */
#line 1637 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5324 "src/parser.tab.c"
    break;

  case 202: /* inline_statement: print_statement  */
#line 1638 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5330 "src/parser.tab.c"
    break;

  case 203: /* inline_statement: call_statement  */
#line 1639 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5336 "src/parser.tab.c"
    break;

  case 204: /* inline_statement: use_statement  */
#line 1640 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5342 "src/parser.tab.c"
    break;

  case 205: /* inline_statement: on_error_statement  */
#line 1641 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5348 "src/parser.tab.c"
    break;

  case 206: /* inline_statement: error_statement  */
#line 1642 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5354 "src/parser.tab.c"
    break;

  case 207: /* inline_statement: return_statement  */
#line 1643 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5360 "src/parser.tab.c"
    break;

  case 208: /* inline_statement: goto_statement  */
#line 1644 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5366 "src/parser.tab.c"
    break;

  case 209: /* inline_statement: gosub_statement  */
#line 1645 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5372 "src/parser.tab.c"
    break;

  case 210: /* inline_statement: break_statement  */
#line 1646 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5378 "src/parser.tab.c"
    break;

  case 211: /* inline_statement: continue_statement  */
#line 1647 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5384 "src/parser.tab.c"
    break;

  case 212: /* expression: or_expression  */
#line 1651 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 5390 "src/parser.tab.c"
    break;

  case 213: /* or_expression: and_expression  */
#line 1655 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5396 "src/parser.tab.c"
    break;

  case 214: /* or_expression: or_expression OR and_expression  */
#line 1656 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5402 "src/parser.tab.c"
    break;

  case 215: /* and_expression: not_expression  */
#line 1660 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5408 "src/parser.tab.c"
    break;

  case 216: /* and_expression: and_expression AND not_expression  */
#line 1661 "src/parser.y"
                                        { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5414 "src/parser.tab.c"
    break;

  case 217: /* not_expression: comparison_expression  */
#line 1683 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 5420 "src/parser.tab.c"
    break;

  case 218: /* not_expression: NOT not_expression  */
#line 1684 "src/parser.y"
                         { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5426 "src/parser.tab.c"
    break;

  case 219: /* comparison_expression: set_expression  */
#line 1688 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5432 "src/parser.tab.c"
    break;

  case 220: /* comparison_expression: set_expression comparison_operator set_expression  */
#line 1689 "src/parser.y"
                                                        { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5438 "src/parser.tab.c"
    break;

  case 221: /* comparison_expression: set_expression comparison_lens comparison_operator set_expression  */
#line 1690 "src/parser.y"
                                                                        {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 5446 "src/parser.tab.c"
    break;

  case 222: /* set_expression: additive_expression  */
#line 1696 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 5452 "src/parser.tab.c"
    break;

  case 223: /* set_expression: set_expression EXCLUDING additive_expression  */
#line 1697 "src/parser.y"
                                                   { (yyval.expr) = expr_at(ast_binary(copy_const("excluding"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5458 "src/parser.tab.c"
    break;

  case 224: /* set_expression: set_expression INTERSECTING additive_expression  */
#line 1698 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("intersecting"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5464 "src/parser.tab.c"
    break;

  case 225: /* additive_expression: multiplicative_expression  */
#line 1702 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 5470 "src/parser.tab.c"
    break;

  case 226: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 1703 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5476 "src/parser.tab.c"
    break;

  case 227: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 1704 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5482 "src/parser.tab.c"
    break;

  case 228: /* multiplicative_expression: unary_expression  */
#line 1708 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 5488 "src/parser.tab.c"
    break;

  case 229: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 1709 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5494 "src/parser.tab.c"
    break;

  case 230: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 1710 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5500 "src/parser.tab.c"
    break;

  case 231: /* unary_expression: postfix_expression  */
#line 1714 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 5506 "src/parser.tab.c"
    break;

  case 232: /* unary_expression: MINUS unary_expression  */
#line 1715 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5512 "src/parser.tab.c"
    break;

  case 233: /* unary_expression: MODIFIER_PREFIX unary_expression  */
#line 1722 "src/parser.y"
                                       {
        (yyval.expr) = expr_at(ast_modifier_apply(parse_modifier_use((yyvsp[-1].text)), (yyvsp[0].expr)),
                     (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 5521 "src/parser.tab.c"
    break;

  case 234: /* unary_expression: NEW postfix_expression  */
#line 1726 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5527 "src/parser.tab.c"
    break;

  case 235: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 1727 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 5533 "src/parser.tab.c"
    break;

  case 236: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 1728 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5539 "src/parser.tab.c"
    break;

  case 237: /* postfix_expression: primary  */
#line 1732 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 5545 "src/parser.tab.c"
    break;

  case 238: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 1733 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5551 "src/parser.tab.c"
    break;

  case 239: /* postfix_expression: postfix_expression DOT dot_field_name  */
#line 1734 "src/parser.y"
                                            { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5557 "src/parser.tab.c"
    break;

  case 240: /* postfix_expression: postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1735 "src/parser.y"
                                                                   {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        (yyval.expr) = expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5567 "src/parser.tab.c"
    break;

  case 241: /* postfix_expression: postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1740 "src/parser.y"
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
#line 5583 "src/parser.tab.c"
    break;

  case 242: /* comparison_operator: OP_EQ  */
#line 1754 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 5589 "src/parser.tab.c"
    break;

  case 243: /* comparison_operator: OP_NE  */
#line 1755 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 5595 "src/parser.tab.c"
    break;

  case 244: /* comparison_operator: OP_GT  */
#line 1756 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 5601 "src/parser.tab.c"
    break;

  case 245: /* comparison_operator: OP_LT  */
#line 1757 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 5607 "src/parser.tab.c"
    break;

  case 246: /* comparison_operator: OP_GE  */
#line 1758 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 5613 "src/parser.tab.c"
    break;

  case 247: /* comparison_operator: OP_LE  */
#line 1759 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 5619 "src/parser.tab.c"
    break;

  case 248: /* comparison_operator: OP_NGT  */
#line 1760 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 5625 "src/parser.tab.c"
    break;

  case 249: /* comparison_operator: OP_NLT  */
#line 1761 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 5631 "src/parser.tab.c"
    break;

  case 250: /* comparison_operator: OP_NGE  */
#line 1762 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 5637 "src/parser.tab.c"
    break;

  case 251: /* comparison_operator: OP_NLE  */
#line 1763 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 5643 "src/parser.tab.c"
    break;

  case 252: /* primary: MODIFIER_VALUE  */
#line 1767 "src/parser.y"
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
#line 5666 "src/parser.tab.c"
    break;

  case 253: /* primary: NUMBER  */
#line 1785 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5672 "src/parser.tab.c"
    break;

  case 254: /* primary: WATCHERS LPAREN RPAREN  */
#line 1786 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_call(copy_const("watchers"), ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5678 "src/parser.tab.c"
    break;

  case 255: /* primary: duration_terms  */
#line 1787 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5684 "src/parser.tab.c"
    break;

  case 256: /* primary: STRING  */
#line 1788 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5690 "src/parser.tab.c"
    break;

  case 257: /* primary: variable_name ident_suffix  */
#line 1789 "src/parser.y"
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
#line 5712 "src/parser.tab.c"
    break;

  case 258: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1806 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 5723 "src/parser.tab.c"
    break;

  case 259: /* primary: ERROR_VALUE  */
#line 1812 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5729 "src/parser.tab.c"
    break;

  case 260: /* primary: TRUE  */
#line 1813 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5735 "src/parser.tab.c"
    break;

  case 261: /* primary: FALSE  */
#line 1814 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5741 "src/parser.tab.c"
    break;

  case 262: /* primary: NOTHING  */
#line 1815 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5747 "src/parser.tab.c"
    break;

  case 263: /* primary: UNKNOWN_VALUE  */
#line 1816 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5753 "src/parser.tab.c"
    break;

  case 264: /* primary: LPAREN expression RPAREN  */
#line 1817 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 5759 "src/parser.tab.c"
    break;

  case 265: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 1818 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5765 "src/parser.tab.c"
    break;

  case 266: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 1819 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5771 "src/parser.tab.c"
    break;

  case 267: /* primary: record_literal  */
#line 1820 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5777 "src/parser.tab.c"
    break;

  case 268: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 1824 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5783 "src/parser.tab.c"
    break;

  case 269: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 1825 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5789 "src/parser.tab.c"
    break;

  case 270: /* ident_suffix: %empty  */
#line 1829 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 5799 "src/parser.tab.c"
    break;

  case 271: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 1834 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5809 "src/parser.tab.c"
    break;

  case 272: /* ident_suffix: DOT dot_field_name ident_dot_suffix  */
#line 1839 "src/parser.y"
                                          {
        /* dot_field_name, not IDENT: a keyword is a legal FIELD name after a
         * dot, because nothing but a name can appear there. */
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 5820 "src/parser.tab.c"
    break;

  case 273: /* ident_suffix: DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1845 "src/parser.y"
                                                          {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        (yyval.ident_suffix).kind = IDENT_SUFFIX_METHOD;
        (yyval.ident_suffix).name = (yyvsp[-3].text);
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5834 "src/parser.tab.c"
    break;

  case 274: /* ident_dot_suffix: %empty  */
#line 1857 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 5844 "src/parser.tab.c"
    break;

  case 275: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 1862 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5854 "src/parser.tab.c"
    break;

  case 276: /* duration_terms: NUMBER IDENT  */
#line 1870 "src/parser.y"
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
#line 5869 "src/parser.tab.c"
    break;

  case 277: /* duration_terms: duration_terms NUMBER IDENT  */
#line 1880 "src/parser.y"
                                  {
        char *bad = NULL;
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].numlit).value, (yyvsp[0].text), &bad);
        if (bad) {
            duration_unit_error(ctx, bad, (yylsp[0]).first_line, (yylsp[0]).first_column,
                                (yylsp[0]).last_line, (yylsp[0]).last_column);
            YYERROR;
        }
      }
#line 5883 "src/parser.tab.c"
    break;

  case 278: /* argument_list_opt: %empty  */
#line 1892 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 5889 "src/parser.tab.c"
    break;

  case 279: /* argument_list_opt: argument_list  */
#line 1893 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 5895 "src/parser.tab.c"
    break;

  case 280: /* argument_list: expression  */
#line 1897 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 5901 "src/parser.tab.c"
    break;

  case 281: /* argument_list: argument_list COMMA expression  */
#line 1898 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 5907 "src/parser.tab.c"
    break;

  case 282: /* array_argument_list: expression  */
#line 1902 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 5913 "src/parser.tab.c"
    break;

  case 283: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 1903 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 5919 "src/parser.tab.c"
    break;

  case 284: /* parameter_list_opt: %empty  */
#line 1907 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 5925 "src/parser.tab.c"
    break;

  case 285: /* parameter_list_opt: parameter_list  */
#line 1908 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 5931 "src/parser.tab.c"
    break;

  case 286: /* parameter_default: NUMBER  */
#line 1922 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5937 "src/parser.tab.c"
    break;

  case 287: /* parameter_default: MINUS NUMBER  */
#line 1926 "src/parser.y"
                   { (yyval.expr) = expr_at(ast_number(-(yyvsp[0].numlit).value,
                                             (yyvsp[0].numlit).exact == LLONG_MIN ? 0 : -(yyvsp[0].numlit).exact,
                                             (yyvsp[0].numlit).exact == LLONG_MIN ? 0 : (yyvsp[0].numlit).is_exact),
                                  (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5946 "src/parser.tab.c"
    break;

  case 288: /* parameter_default: PLUS NUMBER  */
#line 1930 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5952 "src/parser.tab.c"
    break;

  case 289: /* parameter_default: STRING  */
#line 1931 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5958 "src/parser.tab.c"
    break;

  case 290: /* parameter_default: TRUE  */
#line 1932 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5964 "src/parser.tab.c"
    break;

  case 291: /* parameter_default: FALSE  */
#line 1933 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5970 "src/parser.tab.c"
    break;

  case 292: /* parameter_default: NOTHING  */
#line 1934 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5976 "src/parser.tab.c"
    break;

  case 293: /* parameter_default: UNKNOWN_VALUE  */
#line 1935 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5982 "src/parser.tab.c"
    break;

  case 294: /* parameter_list: IDENT  */
#line 1939 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5988 "src/parser.tab.c"
    break;

  case 295: /* parameter_list: IDENT OP_EQ parameter_default  */
#line 1940 "src/parser.y"
                                    {
        (yyval.name_list) = ast_name_list_append_default(ast_name_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 5996 "src/parser.tab.c"
    break;

  case 296: /* parameter_list: parameter_list COMMA IDENT  */
#line 1943 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 6002 "src/parser.tab.c"
    break;

  case 297: /* parameter_list: parameter_list COMMA IDENT OP_EQ parameter_default  */
#line 1944 "src/parser.y"
                                                         {
        (yyval.name_list) = ast_name_list_append_default((yyvsp[-4].name_list), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 6010 "src/parser.tab.c"
    break;

  case 298: /* field_name: dot_field_name  */
#line 1959 "src/parser.y"
                     { (yyval.text) = (yyvsp[0].text); }
#line 6016 "src/parser.tab.c"
    break;

  case 299: /* field_name: STRING  */
#line 1966 "src/parser.y"
             { (yyval.text) = (yyvsp[0].text); }
#line 6022 "src/parser.tab.c"
    break;

  case 300: /* dot_field_name: IDENT  */
#line 1975 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 6028 "src/parser.tab.c"
    break;

  case 301: /* dot_field_name: AS  */
#line 1976 "src/parser.y"
                     { (yyval.text) = kw_name("as"); }
#line 6034 "src/parser.tab.c"
    break;

  case 302: /* dot_field_name: NEXT  */
#line 1977 "src/parser.y"
                     { (yyval.text) = kw_name("next"); }
#line 6040 "src/parser.tab.c"
    break;

  case 303: /* dot_field_name: STOP  */
#line 1978 "src/parser.y"
                     { (yyval.text) = kw_name("stop"); }
#line 6046 "src/parser.tab.c"
    break;

  case 304: /* dot_field_name: ERROR_VALUE  */
#line 1979 "src/parser.y"
                     { (yyval.text) = kw_name("error"); }
#line 6052 "src/parser.tab.c"
    break;

  case 305: /* dot_field_name: END  */
#line 1980 "src/parser.y"
                     { (yyval.text) = kw_name("end"); }
#line 6058 "src/parser.tab.c"
    break;

  case 306: /* dot_field_name: TO  */
#line 1981 "src/parser.y"
                     { (yyval.text) = kw_name("to"); }
#line 6064 "src/parser.tab.c"
    break;

  case 307: /* dot_field_name: IN  */
#line 1982 "src/parser.y"
                     { (yyval.text) = kw_name("in"); }
#line 6070 "src/parser.tab.c"
    break;

  case 308: /* dot_field_name: ON  */
#line 1983 "src/parser.y"
                     { (yyval.text) = kw_name("on"); }
#line 6076 "src/parser.tab.c"
    break;

  case 309: /* dot_field_name: NEW  */
#line 1984 "src/parser.y"
                     { (yyval.text) = kw_name("new"); }
#line 6082 "src/parser.tab.c"
    break;

  case 310: /* dot_field_name: EACH  */
#line 1985 "src/parser.y"
                     { (yyval.text) = kw_name("each"); }
#line 6088 "src/parser.tab.c"
    break;

  case 311: /* dot_field_name: WITH  */
#line 1986 "src/parser.y"
                     { (yyval.text) = kw_name("with"); }
#line 6094 "src/parser.tab.c"
    break;

  case 312: /* dot_field_name: WITHOUT  */
#line 1987 "src/parser.y"
                     { (yyval.text) = kw_name("without"); }
#line 6100 "src/parser.tab.c"
    break;

  case 313: /* dot_field_name: THEN  */
#line 1988 "src/parser.y"
                     { (yyval.text) = kw_name("then"); }
#line 6106 "src/parser.tab.c"
    break;

  case 314: /* dot_field_name: ELSE  */
#line 1989 "src/parser.y"
                     { (yyval.text) = kw_name("else"); }
#line 6112 "src/parser.tab.c"
    break;

  case 315: /* dot_field_name: FOR  */
#line 1990 "src/parser.y"
                     { (yyval.text) = kw_name("for"); }
#line 6118 "src/parser.tab.c"
    break;

  case 316: /* dot_field_name: IF  */
#line 1991 "src/parser.y"
                     { (yyval.text) = kw_name("if"); }
#line 6124 "src/parser.tab.c"
    break;

  case 317: /* dot_field_name: WHILE  */
#line 1992 "src/parser.y"
                     { (yyval.text) = kw_name("while"); }
#line 6130 "src/parser.tab.c"
    break;

  case 318: /* dot_field_name: DO  */
#line 1993 "src/parser.y"
                     { (yyval.text) = kw_name("do"); }
#line 6136 "src/parser.tab.c"
    break;

  case 319: /* dot_field_name: UNTIL  */
#line 1994 "src/parser.y"
                     { (yyval.text) = kw_name("until"); }
#line 6142 "src/parser.tab.c"
    break;

  case 320: /* dot_field_name: PRINT  */
#line 1995 "src/parser.y"
                     { (yyval.text) = kw_name("print"); }
#line 6148 "src/parser.tab.c"
    break;

  case 321: /* dot_field_name: RETURN  */
#line 1996 "src/parser.y"
                     { (yyval.text) = kw_name("return"); }
#line 6154 "src/parser.tab.c"
    break;

  case 322: /* dot_field_name: LOAD  */
#line 1997 "src/parser.y"
                     { (yyval.text) = kw_name("load"); }
#line 6160 "src/parser.tab.c"
    break;

  case 323: /* dot_field_name: USE  */
#line 1998 "src/parser.y"
                     { (yyval.text) = kw_name("use"); }
#line 6166 "src/parser.tab.c"
    break;

  case 324: /* dot_field_name: NOT  */
#line 1999 "src/parser.y"
                     { (yyval.text) = kw_name("not"); }
#line 6172 "src/parser.tab.c"
    break;

  case 325: /* dot_field_name: AND  */
#line 2000 "src/parser.y"
                     { (yyval.text) = kw_name("and"); }
#line 6178 "src/parser.tab.c"
    break;

  case 326: /* dot_field_name: OR  */
#line 2001 "src/parser.y"
                     { (yyval.text) = kw_name("or"); }
#line 6184 "src/parser.tab.c"
    break;

  case 327: /* dot_field_name: TRUE  */
#line 2002 "src/parser.y"
                     { (yyval.text) = kw_name("true"); }
#line 6190 "src/parser.tab.c"
    break;

  case 328: /* dot_field_name: FALSE  */
#line 2003 "src/parser.y"
                     { (yyval.text) = kw_name("false"); }
#line 6196 "src/parser.tab.c"
    break;

  case 329: /* dot_field_name: NOTHING  */
#line 2004 "src/parser.y"
                     { (yyval.text) = kw_name("nothing"); }
#line 6202 "src/parser.tab.c"
    break;

  case 330: /* dot_field_name: BREAK  */
#line 2005 "src/parser.y"
                     { (yyval.text) = kw_name("break"); }
#line 6208 "src/parser.tab.c"
    break;

  case 331: /* dot_field_name: CONTINUE  */
#line 2006 "src/parser.y"
                     { (yyval.text) = kw_name("continue"); }
#line 6214 "src/parser.tab.c"
    break;

  case 332: /* dot_field_name: GOTO  */
#line 2007 "src/parser.y"
                     { (yyval.text) = kw_name("goto"); }
#line 6220 "src/parser.tab.c"
    break;

  case 333: /* dot_field_name: GOSUB  */
#line 2008 "src/parser.y"
                     { (yyval.text) = kw_name("gosub"); }
#line 6226 "src/parser.tab.c"
    break;

  case 334: /* dot_field_name: SPAWN  */
#line 2009 "src/parser.y"
                     { (yyval.text) = kw_name("spawn"); }
#line 6232 "src/parser.tab.c"
    break;

  case 335: /* dot_field_name: EXPORT  */
#line 2010 "src/parser.y"
                     { (yyval.text) = kw_name("export"); }
#line 6238 "src/parser.tab.c"
    break;

  case 336: /* dot_field_name: LIBRARY  */
#line 2011 "src/parser.y"
                     { (yyval.text) = kw_name("library"); }
#line 6244 "src/parser.tab.c"
    break;

  case 337: /* dot_field_name: FUNCTION  */
#line 2012 "src/parser.y"
                     { (yyval.text) = kw_name("function"); }
#line 6250 "src/parser.tab.c"
    break;

  case 338: /* dot_field_name: MODIFIER  */
#line 2013 "src/parser.y"
                     { (yyval.text) = kw_name("modifier"); }
#line 6256 "src/parser.tab.c"
    break;

  case 339: /* dot_field_name: PROGRAM  */
#line 2014 "src/parser.y"
                     { (yyval.text) = kw_name("program"); }
#line 6262 "src/parser.tab.c"
    break;

  case 340: /* dot_field_name: WATCH  */
#line 2015 "src/parser.y"
                     { (yyval.text) = kw_name("watch"); }
#line 6268 "src/parser.tab.c"
    break;

  case 341: /* dot_field_name: WATCHERS  */
#line 2016 "src/parser.y"
                     { (yyval.text) = kw_name("watchers"); }
#line 6274 "src/parser.tab.c"
    break;

  case 342: /* dot_field_name: CONSIDER  */
#line 2017 "src/parser.y"
                     { (yyval.text) = kw_name("consider"); }
#line 6280 "src/parser.tab.c"
    break;

  case 343: /* dot_field_name: STEP  */
#line 2018 "src/parser.y"
                     { (yyval.text) = kw_name("step"); }
#line 6286 "src/parser.tab.c"
    break;

  case 344: /* dot_field_name: UNWATCH  */
#line 2019 "src/parser.y"
                     { (yyval.text) = kw_name("unwatch"); }
#line 6292 "src/parser.tab.c"
    break;

  case 345: /* dot_field_name: UNKNOWN_VALUE  */
#line 2020 "src/parser.y"
                     { (yyval.text) = kw_name("unknown"); }
#line 6298 "src/parser.tab.c"
    break;

  case 346: /* dot_field_name: DIM  */
#line 2021 "src/parser.y"
                     { (yyval.text) = kw_name("dim"); }
#line 6304 "src/parser.tab.c"
    break;

  case 347: /* record_field_list: field_name OP_EQ expression  */
#line 2025 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6310 "src/parser.tab.c"
    break;

  case 348: /* record_field_list: field_name COLON expression  */
#line 2026 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6316 "src/parser.tab.c"
    break;

  case 349: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 2027 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 6322 "src/parser.tab.c"
    break;

  case 350: /* record_field_list: record_field_list COMMA optional_newlines field_name OP_EQ expression  */
#line 2028 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6328 "src/parser.tab.c"
    break;

  case 351: /* record_field_list: record_field_list COMMA optional_newlines field_name COLON expression  */
#line 2029 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6334 "src/parser.tab.c"
    break;

  case 352: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 2030 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 6340 "src/parser.tab.c"
    break;

  case 353: /* field_policy: IDENT  */
#line 2038 "src/parser.y"
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
#line 6372 "src/parser.tab.c"
    break;

  case 354: /* field_policy: IDENT expression  */
#line 2065 "src/parser.y"
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
#line 6393 "src/parser.tab.c"
    break;


#line 6397 "src/parser.tab.c"

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

#line 2088 "src/parser.y"


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
