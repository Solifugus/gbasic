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



#line 696 "src/parser.tab.c"

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
  YYSYMBOL_AS = 9,                         /* AS  */
  YYSYMBOL_DIM = 10,                       /* DIM  */
  YYSYMBOL_PLUS_EQ = 11,                   /* PLUS_EQ  */
  YYSYMBOL_MINUS_EQ = 12,                  /* MINUS_EQ  */
  YYSYMBOL_STAR_EQ = 13,                   /* STAR_EQ  */
  YYSYMBOL_SLASH_EQ = 14,                  /* SLASH_EQ  */
  YYSYMBOL_EXCLUDING = 15,                 /* EXCLUDING  */
  YYSYMBOL_INTERSECTING = 16,              /* INTERSECTING  */
  YYSYMBOL_IF = 17,                        /* IF  */
  YYSYMBOL_CONSIDER_IF = 18,               /* CONSIDER_IF  */
  YYSYMBOL_THEN = 19,                      /* THEN  */
  YYSYMBOL_ELSE = 20,                      /* ELSE  */
  YYSYMBOL_CONSIDER_ELSE = 21,             /* CONSIDER_ELSE  */
  YYSYMBOL_END = 22,                       /* END  */
  YYSYMBOL_END_CONSIDER = 23,              /* END_CONSIDER  */
  YYSYMBOL_PRINT = 24,                     /* PRINT  */
  YYSYMBOL_TRUE = 25,                      /* TRUE  */
  YYSYMBOL_FALSE = 26,                     /* FALSE  */
  YYSYMBOL_NOTHING = 27,                   /* NOTHING  */
  YYSYMBOL_UNKNOWN_VALUE = 28,             /* UNKNOWN_VALUE  */
  YYSYMBOL_AND = 29,                       /* AND  */
  YYSYMBOL_OR = 30,                        /* OR  */
  YYSYMBOL_NOT = 31,                       /* NOT  */
  YYSYMBOL_WITH = 32,                      /* WITH  */
  YYSYMBOL_NEW = 33,                       /* NEW  */
  YYSYMBOL_SPAWN = 34,                     /* SPAWN  */
  YYSYMBOL_FOR = 35,                       /* FOR  */
  YYSYMBOL_TO = 36,                        /* TO  */
  YYSYMBOL_STEP = 37,                      /* STEP  */
  YYSYMBOL_DO = 38,                        /* DO  */
  YYSYMBOL_UNTIL = 39,                     /* UNTIL  */
  YYSYMBOL_IN = 40,                        /* IN  */
  YYSYMBOL_EACH = 41,                      /* EACH  */
  YYSYMBOL_WHILE = 42,                     /* WHILE  */
  YYSYMBOL_CONSIDER = 43,                  /* CONSIDER  */
  YYSYMBOL_BREAK = 44,                     /* BREAK  */
  YYSYMBOL_CONTINUE = 45,                  /* CONTINUE  */
  YYSYMBOL_FUNCTION = 46,                  /* FUNCTION  */
  YYSYMBOL_RETURN = 47,                    /* RETURN  */
  YYSYMBOL_GOTO = 48,                      /* GOTO  */
  YYSYMBOL_GOSUB = 49,                     /* GOSUB  */
  YYSYMBOL_WATCH = 50,                     /* WATCH  */
  YYSYMBOL_UNWATCH = 51,                   /* UNWATCH  */
  YYSYMBOL_WITHOUT = 52,                   /* WITHOUT  */
  YYSYMBOL_WATCHERS = 53,                  /* WATCHERS  */
  YYSYMBOL_ON = 54,                        /* ON  */
  YYSYMBOL_NEXT = 55,                      /* NEXT  */
  YYSYMBOL_STOP = 56,                      /* STOP  */
  YYSYMBOL_ERROR_VALUE = 57,               /* ERROR_VALUE  */
  YYSYMBOL_MODIFIER = 58,                  /* MODIFIER  */
  YYSYMBOL_PROGRAM = 59,                   /* PROGRAM  */
  YYSYMBOL_LIBRARY = 60,                   /* LIBRARY  */
  YYSYMBOL_LOAD = 61,                      /* LOAD  */
  YYSYMBOL_USE = 62,                       /* USE  */
  YYSYMBOL_EXPORT = 63,                    /* EXPORT  */
  YYSYMBOL_OP_EQ = 64,                     /* OP_EQ  */
  YYSYMBOL_OP_NE = 65,                     /* OP_NE  */
  YYSYMBOL_OP_GT = 66,                     /* OP_GT  */
  YYSYMBOL_OP_LT = 67,                     /* OP_LT  */
  YYSYMBOL_OP_GE = 68,                     /* OP_GE  */
  YYSYMBOL_OP_LE = 69,                     /* OP_LE  */
  YYSYMBOL_OP_NGT = 70,                    /* OP_NGT  */
  YYSYMBOL_OP_NLT = 71,                    /* OP_NLT  */
  YYSYMBOL_OP_NGE = 72,                    /* OP_NGE  */
  YYSYMBOL_OP_NLE = 73,                    /* OP_NLE  */
  YYSYMBOL_PLUS = 74,                      /* PLUS  */
  YYSYMBOL_MINUS = 75,                     /* MINUS  */
  YYSYMBOL_STAR = 76,                      /* STAR  */
  YYSYMBOL_SLASH = 77,                     /* SLASH  */
  YYSYMBOL_LPAREN = 78,                    /* LPAREN  */
  YYSYMBOL_RPAREN = 79,                    /* RPAREN  */
  YYSYMBOL_LBRACKET = 80,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 81,                  /* RBRACKET  */
  YYSYMBOL_LBRACE = 82,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 83,                    /* RBRACE  */
  YYSYMBOL_COMMA = 84,                     /* COMMA  */
  YYSYMBOL_COLON = 85,                     /* COLON  */
  YYSYMBOL_NEWLINE = 86,                   /* NEWLINE  */
  YYSYMBOL_IF_WITHOUT_ELSE = 87,           /* IF_WITHOUT_ELSE  */
  YYSYMBOL_NO_DOT = 88,                    /* NO_DOT  */
  YYSYMBOL_DOT = 89,                       /* DOT  */
  YYSYMBOL_YYACCEPT = 90,                  /* $accept  */
  YYSYMBOL_program = 91,                   /* program  */
  YYSYMBOL_statement_list = 92,            /* statement_list  */
  YYSYMBOL_statement = 93,                 /* statement  */
  YYSYMBOL_assignment = 94,                /* assignment  */
  YYSYMBOL_compound_op = 95,               /* compound_op  */
  YYSYMBOL_lvalue = 96,                    /* lvalue  */
  YYSYMBOL_variable_name = 97,             /* variable_name  */
  YYSYMBOL_comparison_lens = 98,           /* comparison_lens  */
  YYSYMBOL_99_1 = 99,                      /* $@1  */
  YYSYMBOL_modifier_name = 100,            /* modifier_name  */
  YYSYMBOL_modifier_word = 101,            /* modifier_word  */
  YYSYMBOL_print_statement = 102,          /* print_statement  */
  YYSYMBOL_call_statement = 103,           /* call_statement  */
  YYSYMBOL_with_lock_statement = 104,      /* with_lock_statement  */
  YYSYMBOL_for_end = 105,                  /* for_end  */
  YYSYMBOL_for_each_statement = 106,       /* for_each_statement  */
  YYSYMBOL_do_loop_statement = 107,        /* do_loop_statement  */
  YYSYMBOL_while_statement = 108,          /* while_statement  */
  YYSYMBOL_consider_statement = 109,       /* consider_statement  */
  YYSYMBOL_consider_branch_list = 110,     /* consider_branch_list  */
  YYSYMBOL_consider_else_opt = 111,        /* consider_else_opt  */
  YYSYMBOL_consider_statement_list = 112,  /* consider_statement_list  */
  YYSYMBOL_consider_body_statement = 113,  /* consider_body_statement  */
  YYSYMBOL_function_statement = 114,       /* function_statement  */
  YYSYMBOL_modifier_statement = 115,       /* modifier_statement  */
  YYSYMBOL_program_statement = 116,        /* program_statement  */
  YYSYMBOL_library_statement = 117,        /* library_statement  */
  YYSYMBOL_use_statement = 118,            /* use_statement  */
  YYSYMBOL_modifier_signature = 119,       /* modifier_signature  */
  YYSYMBOL_modifier_context = 120,         /* modifier_context  */
  YYSYMBOL_watch_statement = 121,          /* watch_statement  */
  YYSYMBOL_unwatch_statement = 122,        /* unwatch_statement  */
  YYSYMBOL_watch_target_list = 123,        /* watch_target_list  */
  YYSYMBOL_server_statement = 124,         /* server_statement  */
  YYSYMBOL_125_2 = 125,                    /* @2  */
  YYSYMBOL_126_3 = 126,                    /* @3  */
  YYSYMBOL_server_item_list = 127,         /* server_item_list  */
  YYSYMBOL_server_item = 128,              /* server_item  */
  YYSYMBOL_server_string_list = 129,       /* server_string_list  */
  YYSYMBOL_watch_target_path = 130,        /* watch_target_path  */
  YYSYMBOL_without_watchers_statement = 131, /* without_watchers_statement  */
  YYSYMBOL_on_error_statement = 132,       /* on_error_statement  */
  YYSYMBOL_error_statement = 133,          /* error_statement  */
  YYSYMBOL_return_statement = 134,         /* return_statement  */
  YYSYMBOL_label_statement = 135,          /* label_statement  */
  YYSYMBOL_goto_statement = 136,           /* goto_statement  */
  YYSYMBOL_gosub_statement = 137,          /* gosub_statement  */
  YYSYMBOL_break_statement = 138,          /* break_statement  */
  YYSYMBOL_continue_statement = 139,       /* continue_statement  */
  YYSYMBOL_if_statement = 140,             /* if_statement  */
  YYSYMBOL_if_block_tail = 141,            /* if_block_tail  */
  YYSYMBOL_if_inline_tail = 142,           /* if_inline_tail  */
  YYSYMBOL_inline_statement = 143,         /* inline_statement  */
  YYSYMBOL_expression = 144,               /* expression  */
  YYSYMBOL_or_expression = 145,            /* or_expression  */
  YYSYMBOL_and_expression = 146,           /* and_expression  */
  YYSYMBOL_not_expression = 147,           /* not_expression  */
  YYSYMBOL_comparison_expression = 148,    /* comparison_expression  */
  YYSYMBOL_set_expression = 149,           /* set_expression  */
  YYSYMBOL_additive_expression = 150,      /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 151, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 152,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 153,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 154,      /* comparison_operator  */
  YYSYMBOL_primary = 155,                  /* primary  */
  YYSYMBOL_record_literal = 156,           /* record_literal  */
  YYSYMBOL_ident_suffix = 157,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 158,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 159,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 160,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 161,            /* argument_list  */
  YYSYMBOL_array_argument_list = 162,      /* array_argument_list  */
  YYSYMBOL_parameter_list_opt = 163,       /* parameter_list_opt  */
  YYSYMBOL_parameter_default = 164,        /* parameter_default  */
  YYSYMBOL_parameter_list = 165,           /* parameter_list  */
  YYSYMBOL_field_name = 166,               /* field_name  */
  YYSYMBOL_dot_field_name = 167,           /* dot_field_name  */
  YYSYMBOL_record_field_list = 168,        /* record_field_list  */
  YYSYMBOL_field_policy = 169,             /* field_policy  */
  YYSYMBOL_optional_newlines = 170         /* optional_newlines  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;



/* Unqualified %code blocks.  */
#line 696 "src/parser.y"

static int yylex(YYSTYPE *lvalp, YYLTYPE *llocp, gb_parse_ctx *ctx);
static void yyerror(YYLTYPE *llocp, gb_parse_ctx *ctx, const char *message);
static void report_syntax_error(gb_parse_ctx *ctx, int line, int column,
                                int end_line, int end_column, const char *message);

#line 908 "src/parser.tab.c"

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
#define YYLAST   2687

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  90
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  81
/* YYNRULES -- Number of rules.  */
#define YYNRULES  332
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  707

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   344


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
      85,    86,    87,    88,    89
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   780,   780,   784,   785,   786,   790,   791,   792,   793,
     794,   795,   796,   797,   798,   799,   800,   801,   802,   803,
     804,   805,   806,   807,   808,   809,   810,   811,   812,   813,
     814,   815,   821,   832,   837,   838,   850,   862,   863,   864,
     865,   869,   870,   871,   875,   876,   877,   888,   888,   896,
     900,   901,   905,   906,   907,   908,   912,   918,   922,   923,
     929,   934,   943,   956,   985,   986,   987,   991,   995,  1011,
    1016,  1024,  1028,  1049,  1055,  1061,  1067,  1070,  1076,  1077,
    1081,  1082,  1083,  1087,  1088,  1089,  1090,  1091,  1092,  1093,
    1094,  1095,  1096,  1097,  1098,  1099,  1100,  1101,  1102,  1103,
    1104,  1105,  1106,  1107,  1108,  1109,  1110,  1111,  1117,  1128,
    1131,  1138,  1141,  1147,  1153,  1159,  1160,  1161,  1162,  1163,
    1164,  1180,  1196,  1217,  1218,  1222,  1226,  1229,  1237,  1243,
    1247,  1248,  1268,  1267,  1275,  1274,  1284,  1285,  1286,  1290,
    1293,  1296,  1299,  1302,  1308,  1309,  1313,  1314,  1318,  1324,
    1325,  1326,  1327,  1332,  1345,  1350,  1355,  1365,  1369,  1370,
    1374,  1381,  1385,  1394,  1395,  1399,  1400,  1404,  1408,  1415,
    1418,  1421,  1430,  1440,  1443,  1446,  1452,  1459,  1469,  1470,
    1471,  1472,  1473,  1474,  1475,  1476,  1477,  1478,  1479,  1483,
    1487,  1488,  1492,  1493,  1515,  1516,  1520,  1521,  1522,  1528,
    1529,  1530,  1534,  1535,  1536,  1540,  1541,  1542,  1546,  1547,
    1554,  1558,  1559,  1560,  1564,  1565,  1566,  1567,  1572,  1586,
    1587,  1588,  1589,  1590,  1591,  1592,  1593,  1594,  1595,  1599,
    1600,  1601,  1602,  1603,  1620,  1626,  1627,  1628,  1629,  1630,
    1631,  1632,  1633,  1634,  1638,  1639,  1643,  1648,  1653,  1659,
    1671,  1676,  1684,  1694,  1706,  1707,  1711,  1712,  1716,  1717,
    1721,  1722,  1736,  1737,  1738,  1739,  1740,  1741,  1742,  1743,
    1747,  1748,  1751,  1752,  1767,  1774,  1783,  1784,  1785,  1786,
    1787,  1788,  1789,  1790,  1791,  1792,  1793,  1794,  1795,  1796,
    1797,  1798,  1799,  1800,  1801,  1802,  1803,  1804,  1805,  1806,
    1807,  1808,  1809,  1810,  1811,  1812,  1813,  1814,  1815,  1816,
    1817,  1818,  1819,  1820,  1821,  1822,  1823,  1824,  1825,  1826,
    1827,  1828,  1829,  1833,  1834,  1835,  1836,  1837,  1838,  1846,
    1873,  1892,  1893
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
  "STRING", "LENS_CONTENT", "QUALIFIED_IDENT", "MODIFIER_PREFIX", "AS",
  "DIM", "PLUS_EQ", "MINUS_EQ", "STAR_EQ", "SLASH_EQ", "EXCLUDING",
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

#define YYPACT_NINF (-519)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -519,    21,   922,  -519,    41,   -27,  -519,  2234,  -519,  2200,
      59,   100,    36,  2234,  2234,   166,   170,   215,  2234,    75,
      75,   109,  2234,   131,    45,  -519,   174,   121,   202,   228,
     264,   266,   181,  -519,  -519,   175,   177,   179,   188,   191,
    -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,   208,
    -519,   211,  -519,  -519,   212,   214,   217,   218,   220,   221,
     222,   225,  -519,   223,  2234,  2234,   308,  -519,  -519,   242,
    2293,  -519,  -519,  -519,  -519,  2234,  2303,   319,   246,  -519,
    2293,  2234,  -519,  -519,    78,   306,   301,   305,  -519,  -519,
    2269,   198,   204,  -519,    43,  -519,  -519,   332,   279,  -519,
     259,   129,   334,  -519,   253,   255,  -519,  -519,   269,   272,
    -519,  -519,  -519,   273,    75,  -519,    -6,   267,  -519,   258,
      62,   155,   353,  -519,  -519,  -519,  -519,  -519,    48,  -519,
     324,   282,   275,    72,  -519,   358,  -519,   121,  -519,  -519,
    -519,  -519,  -519,  -519,  2234,  2234,  -519,  2504,  2234,   231,
    -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,
    -519,  -519,  -519,  2382,  -519,   286,   283,   289,  -519,  2234,
    -519,  -519,   103,   293,   294,  -519,   298,   574,   677,  2234,
    2564,  -519,  2116,  2234,  2234,  2293,  2293,  -519,  -519,  -519,
    -519,  -519,  -519,  -519,  -519,  -519,  -519,   477,  2293,  2293,
    2293,  2293,  2293,  2234,  2624,   374,  2234,  2234,  2234,  2234,
     375,    22,   802,  -519,   362,   377,   377,    75,    89,    75,
    -519,   378,  -519,  -519,  -519,    33,  -519,    38,  -519,   307,
     377,  -519,   379,   377,  -519,   383,   380,   388,   359,  -519,
     316,   392,   321,   322,  -519,  -519,  -519,  -519,  -519,  -519,
    -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,
    -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,
    -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,
    -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,
    -519,  -519,  2234,  2234,   323,  -519,   320,     7,  -519,   125,
    -519,  2234,  -519,   326,   325,  2234,  -519,  -519,  -519,  -519,
    -519,   327,  -519,   333,   339,  -519,   341,   342,   344,  -519,
    -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,
    -519,  -519,   337,   305,  -519,   198,   198,  2293,   271,   204,
     204,  -519,  -519,   345,   347,   351,  -519,  -519,  -519,   355,
     349,   394,   396,  2234,   441,  2234,   982,  2234,   232,   393,
     368,   381,   373,   137,   372,   267,  1042,  -519,  1102,  -519,
    -519,  -519,  -519,  2234,   384,  -519,   376,   385,  1162,   459,
    -519,  -519,   379,  -519,   387,  2234,  2234,  -519,  -519,   467,
    -519,  2234,  2234,   390,  -519,  -519,  -519,  -519,   395,  -519,
     147,   172,  -519,  2234,  2234,  -519,   862,   453,   271,  -519,
    2234,  2234,   402,  -519,  2234,  2234,   404,   439,   405,   438,
     462,  2234,   409,   473,   209,   412,   478,   413,   414,  -519,
     451,   454,   426,  -519,  -519,   421,   448,   505,   424,  -519,
     432,   433,  2234,   436,  -519,  -519,  -519,  -519,   742,  -519,
     587,  -519,  -519,   440,   443,  2051,   500,  -519,  2070,  -519,
     444,   446,  -519,  1222,     1,   434,  -519,  2234,  -519,   445,
     449,   499,  -519,   450,  -519,  -519,  -519,  -519,  -519,  -519,
     524,   526,  -519,  -519,   470,  -519,  -519,  1282,   465,   468,
    -519,  1342,  -519,   469,  -519,  -519,  -519,  -519,  -519,   472,
      60,  -519,   461,   133,  -519,  -519,  -519,  2234,  -519,   475,
     476,  2234,  -519,   479,  -519,  -519,  1402,   523,    61,  -519,
    2234,  -519,  -519,  1222,   480,  -519,  -519,   484,  1462,  -519,
    -519,  -519,  1522,   209,  1582,  1642,   509,  -519,  -519,   513,
    1702,  -519,  1762,  2234,   284,   568,   569,  -519,  -519,    85,
     467,  2234,  2234,   555,  1822,  -519,  -519,   556,  1882,  -519,
     544,   494,  -519,   498,   501,  1222,  1222,  -519,  -519,  1462,
    -519,  -519,  -519,   503,   507,   511,  -519,  -519,  -519,  -519,
    -519,  -519,  -519,  -519,  -519,  -519,   512,  -519,   517,  -519,
     518,   520,   525,   530,   533,   537,   538,   542,  -519,   539,
    -519,   564,   580,   546,   547,   575,   577,  -519,   558,   559,
     176,   552,   553,   637,   566,  -519,  -519,   557,   629,  2135,
     630,   562,  -519,  -519,  -519,  -519,  -519,  1222,  1462,  -519,
    -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,  -519,
    -519,  -519,   565,   567,   571,  -519,  -519,   572,   573,  2443,
     377,   645,  -519,  -519,  -519,   578,   581,  -519,   582,  -519,
     584,   585,  -519,  1222,  -519,  -519,  -519,  -519,  -519,  -519,
     588,   167,   597,  -519,  1942,  -519,  2234,   862,  -519,   862,
     453,  -519,  -519,  -519,   592,   593,   609,  -519,  -519,  -519,
    -519,    86,  -519,  -519,   594,   679,   144,  2002,  -519,   598,
     681,   684,  -519,   603,   604,  -519,  -519
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       3,     0,     2,     1,    44,     0,    32,     0,    45,     0,
       0,     0,     0,     0,     0,   163,   165,     0,   158,     0,
       0,     0,     0,     0,     0,    46,     0,     0,     0,     0,
       0,     0,     0,     4,     5,     0,     0,    41,     0,     0,
       9,    10,    12,    11,    13,    14,    15,    16,    17,     0,
      19,     0,    20,    22,     0,     0,     0,     0,     0,     0,
       0,     0,    31,     0,   254,   254,   229,    44,   232,     0,
       0,   236,   237,   238,   239,     0,     0,     0,     0,   235,
       0,     0,   331,   331,   246,     0,   189,   190,   192,   194,
     196,   199,   202,   205,   208,   214,   243,   231,     0,    56,
       0,     0,     0,     3,     0,     0,   164,   166,     0,     0,
     159,   161,   162,    44,     0,   146,     0,   130,   129,     0,
       0,     0,     0,   157,    52,    54,    53,    55,   123,    50,
       0,     0,     0,   116,   118,   115,   117,     0,     6,    49,
      37,    38,    39,    40,     0,     0,    47,     0,     0,     0,
     160,     7,     8,    18,    21,    23,    24,    25,    26,    27,
      28,    29,    30,     0,   256,     0,   255,     0,   252,   254,
     210,   195,   211,     0,     0,   209,     0,     0,     0,   254,
       0,   233,     0,     0,     0,     0,     0,   219,   220,   221,
     222,   223,   224,   225,   226,   227,   228,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     3,     0,   260,   260,     0,     0,     0,
       3,     0,     3,   156,   155,     0,   154,     0,   151,     0,
     260,    51,     0,   260,     3,     0,     0,     0,     0,    33,
       0,     0,   276,     0,   277,   322,   292,   289,   290,   281,
     296,   303,   304,   305,   321,   301,   302,   300,   287,   285,
     310,   291,   282,   319,   294,   295,   283,   286,   293,   318,
     306,   307,   313,   297,   308,   309,   316,   320,   288,   317,
     284,   278,   279,   280,   314,   315,   312,   298,   299,   311,
      43,    34,     0,     0,   276,   275,     0,     0,   274,     0,
      58,     0,    59,     0,     0,   254,   230,   240,   241,   332,
     258,   331,   244,   331,     0,   276,     0,   250,    44,     3,
     178,    41,   179,   180,   181,   182,   183,   184,   185,   186,
     187,   188,     0,   191,   193,   200,   201,     0,   197,   203,
     204,   206,   207,     0,   276,     0,   216,   253,    57,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    78,   270,
       0,   261,     0,     0,     0,   131,     0,   147,     0,   153,
     152,   149,   150,   254,     0,   125,     0,     0,     0,   121,
     119,   120,     0,    42,     0,   254,   254,    36,    35,     0,
     134,     0,     0,     0,   331,   257,   234,   212,     0,   331,
       0,     0,   247,   254,   254,   248,     0,   173,   198,   215,
     254,   254,     0,     3,     0,     0,     0,     0,     0,    45,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     3,
      45,    45,     0,   124,     3,     0,    45,     0,     0,    48,
       0,     0,   329,     0,   136,   323,   324,   132,     0,   213,
       0,   242,   245,     0,     0,     0,    45,   167,     0,   168,
       0,     0,     3,     0,     0,     0,     3,     0,    73,     0,
       0,     0,    80,     0,   262,   265,   266,   267,   268,   269,
       0,     0,   271,     3,   272,     3,     3,     0,     0,     0,
      62,     0,     3,     0,   122,     3,    60,    61,   330,     0,
       0,   136,   276,     0,   259,   249,   251,     0,     3,     0,
       0,     0,     3,     0,   217,   218,     0,    45,    46,    67,
       0,     3,     3,     0,     0,    74,    80,     0,    79,    75,
     264,   263,     0,     0,     0,     0,    45,   127,   148,    45,
       0,   114,     0,     0,     0,     0,     0,   137,   138,     0,
       0,     0,     0,     0,     0,   170,   169,     0,     0,   174,
      45,     0,    65,     0,     0,     0,     0,    68,     3,    76,
      80,   108,    81,     0,     0,     0,    86,    87,    89,    88,
      90,    82,    91,    92,    93,    94,     0,    96,     0,    98,
       0,     0,     0,     0,     0,     0,     0,     0,   107,    45,
     273,    45,    45,     0,     0,    45,    45,   325,     0,   144,
       0,     0,     0,     0,     0,   326,   327,     0,    45,     0,
      45,     0,    64,    66,     3,    71,    69,     0,    77,    83,
      84,    85,    95,    97,    99,   100,   101,   102,   103,   104,
     105,   106,     0,     0,     0,   126,   111,     0,     0,     0,
     260,     0,   139,   135,     3,     0,     0,     3,     0,     3,
       0,     0,    63,     0,    70,   109,   110,   128,   113,   112,
       0,     0,     0,   145,     0,   133,     0,     0,   171,     0,
     173,   175,    72,   136,     0,     0,    45,   328,   172,   177,
     176,     0,   136,     3,     0,     0,     0,     0,   143,     0,
       0,    45,   142,     0,     0,   141,   140
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -519,  -519,   -71,  -519,  -180,   543,  -519,    -2,   601,  -519,
    -519,   570,  -179,  -173,  -508,  -518,  -501,  -500,  -495,  -494,
    -519,  -519,  -453,  -519,  -492,  -484,  -478,  -475,  -159,   563,
     360,  -474,  -473,  -102,  -519,  -519,  -519,  -497,  -519,  -519,
     522,  -470,  -153,  -142,  -141,  -469,  -136,  -126,  -125,  -115,
    -468,  -412,    63,  -444,    17,  -519,   561,   -60,  -519,  -185,
     105,    93,   -64,   669,   551,  -519,   452,  -519,  -519,  -519,
     -58,  -519,  -519,  -208,   216,  -519,   302,   -75,  -177,   203,
     -73
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    34,    35,   148,    36,    84,   149,   241,
     128,   129,    38,    39,    40,   519,    41,    42,    43,    44,
     358,   423,   528,   581,    45,    46,    47,    48,    49,   130,
     376,    50,    51,   116,    52,   501,   444,   500,   548,   610,
     117,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,   457,   459,   332,   164,    86,    87,    88,    89,    90,
      91,    92,    93,    94,   198,    95,    96,   181,   405,    97,
     165,   166,   311,   360,   482,   361,   297,   298,   299,   443,
     177
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      37,   313,   320,   322,   549,   567,   170,   167,   362,   323,
     178,   509,   218,   338,   513,   171,   175,   111,   112,   115,
     576,     3,   374,   324,    85,   377,    99,   577,   578,   325,
     104,   105,   212,   579,   580,   110,   582,   369,   520,   118,
     326,   327,   371,   123,   583,    63,   328,   625,   626,   120,
     584,    65,   124,   585,   587,   588,   329,   330,   589,   593,
     598,   576,   353,   100,   544,    67,   223,   331,   577,   578,
     125,   391,   290,   569,   579,   580,   235,   582,   219,    67,
     220,   236,   545,     8,   126,   583,   224,   521,   370,   544,
     544,   584,   392,   372,   585,   587,   588,     8,   176,   589,
     593,   598,   121,   127,   101,   317,   354,   613,   695,   664,
     225,   303,   115,   113,   546,   363,    25,   628,   226,    64,
     576,   314,   103,   203,   334,   124,   230,   577,   578,   346,
      25,     8,   204,   579,   580,   304,   582,   341,   342,   546,
     546,   102,   356,   125,   583,   682,   547,   562,   544,   366,
     584,   368,   408,   585,   587,   588,   179,   126,   589,   593,
     598,   239,   240,   378,    25,   291,   700,   180,   364,   208,
     106,   547,   547,   219,   107,   660,   127,    66,    67,    68,
     321,    69,    70,   203,   119,   139,   691,   114,   140,   141,
     142,   143,   204,   209,   310,   696,     8,   551,   546,    71,
      72,    73,    74,   227,   393,    75,   131,    76,    77,   394,
      37,   228,   474,   210,   475,   115,   428,   115,   552,   108,
     343,   219,   109,   348,   349,   350,   351,    78,   451,    25,
     547,    79,   132,   309,   476,   477,   478,   479,   400,   137,
     401,   144,   140,   141,   142,   143,   684,   398,   406,    80,
     421,   394,    81,   422,    82,   452,    83,   145,   309,   146,
     651,   138,   652,   122,   150,   688,   147,   689,   133,   134,
     135,   136,   199,   200,   151,   320,   322,   152,   320,   322,
     201,   202,   323,   480,   481,   323,   185,   186,   608,   609,
     335,   336,   339,   340,   153,   292,   324,   154,   155,   324,
     156,   163,   325,   157,   158,   325,   159,   160,   161,   387,
     388,   162,   168,   326,   327,   432,   326,   327,   395,   328,
     169,   448,   328,   173,   174,   182,   450,   440,   441,   329,
     330,   183,   329,   330,   184,   205,   206,   207,   211,   213,
     331,   214,   463,   331,   222,   453,   454,   215,   573,   574,
     216,   217,   460,   461,    37,   575,   221,   229,   487,   232,
     233,   234,   237,   491,    37,   300,    37,   301,   302,   586,
     416,   305,   418,   306,   420,   590,    37,   307,   347,   352,
     357,   359,   367,   375,   380,   373,   591,   592,   379,   573,
     574,   516,   594,   381,   382,   523,   575,   383,   384,   385,
     386,   389,   595,   596,    37,   396,   390,    83,   445,   446,
     586,   399,   532,   597,   534,   535,   590,   394,   402,   403,
     404,   540,    64,   407,   542,   410,   409,   591,   592,   411,
     414,   464,   465,   594,   412,   413,   415,   554,   471,   320,
     322,   558,   672,   595,   596,   417,   323,   425,   573,   574,
     565,   566,   427,   321,   597,   575,   321,   424,   429,   498,
     324,    37,   434,   433,   435,   426,   325,   504,   437,   586,
     439,   442,   671,   458,   449,   590,   447,   326,   327,   467,
     469,   470,   484,   328,   524,    37,   591,   592,   462,    37,
     466,   468,   594,   329,   330,   472,   473,   627,   483,   485,
     486,   488,   595,   596,   331,   490,   489,   492,   493,   494,
     495,   496,   497,   597,    37,   499,   563,   510,   527,   505,
     522,    37,   506,   514,   553,   515,    37,   530,   557,   531,
      37,   525,    37,    37,   533,   526,   529,   564,    37,   550,
      37,   187,   188,   189,   190,   191,   192,   193,   194,   195,
     196,   537,    37,   663,   538,   541,    37,   543,   561,   603,
     607,   555,   556,    37,    37,   559,   568,    37,   615,   616,
     570,   604,   611,   612,   617,   619,   621,    66,    67,    68,
     622,    69,    70,   674,   623,   642,   677,   624,   679,   629,
      66,    67,    68,   630,    69,    70,     8,   631,   632,    71,
      72,    73,    74,   633,   634,    75,   635,    76,    77,     8,
     643,   636,    71,    72,    73,    74,   637,   321,    75,   638,
      76,    77,   697,   639,   640,    37,    37,    78,   641,    25,
     644,    79,   645,   646,   647,   648,   649,   650,   653,   654,
      78,   655,    25,   657,    79,   656,   658,   661,   662,    80,
     673,   665,    81,   666,    82,   308,    83,   667,   668,   669,
     309,    37,    80,   694,   675,    81,   676,    82,   678,    83,
     680,   681,    37,   309,   683,    37,   685,    37,   692,   693,
     698,   294,   295,   699,   702,   703,   244,   245,   704,   705,
     706,   197,   293,   687,   246,    37,   247,   248,   231,   249,
     238,   250,   251,   252,   253,   254,   255,   256,   257,   258,
     259,   260,   261,   262,   263,   264,   265,   266,   267,   268,
     269,   270,   271,   272,   273,   274,   275,   276,   277,   278,
     279,   280,   281,   282,   283,   284,   285,   286,   287,   288,
     289,   365,   438,   690,   333,   172,   502,   295,   337,   600,
     503,   244,   245,   614,     0,     0,   397,     0,     0,   246,
     312,   247,   248,   309,   249,     0,   250,   251,   252,   253,
     254,   255,   256,   257,   258,   259,   260,   261,   262,   263,
     264,   265,   266,   267,   268,   269,   270,   271,   272,   273,
     274,   275,   276,   277,   278,   279,   280,   281,   282,   283,
     284,   285,   286,   287,   288,   289,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     0,     0,     7,
       0,     0,     0,     0,     8,     0,     9,     0,   309,     0,
       0,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,   355,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     0,     0,     7,
       0,     0,   455,     0,   456,     0,     9,     0,    33,     0,
       0,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     0,     0,     7,
       0,     0,     0,     0,     8,     0,     9,     0,    33,     0,
       0,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   419,     0,     9,     0,    33,     0,
       0,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   430,     0,     9,     0,    33,     0,
       0,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   431,     0,     9,     0,    33,     0,
       0,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   436,     0,     9,     0,    33,     0,
       0,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   517,     0,     9,     0,    33,     0,
       0,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,   518,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   536,     0,     9,     0,    33,     0,
       0,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   539,     0,     9,     0,    33,     0,
       0,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   560,     0,     9,     0,    33,     0,
       0,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,   318,     0,     0,     5,
       0,     0,   571,     0,     0,     0,     0,     0,     0,     7,
       0,     0,     0,     0,     8,     0,     9,     0,    33,     0,
       0,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   599,     0,     9,     0,   572,     0,
       0,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   601,     0,     9,     0,    33,     0,
       0,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   602,     0,     9,     0,    33,     0,
       0,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   605,     0,     9,     0,    33,     0,
       0,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   606,     0,     9,     0,    33,     0,
       0,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   618,     0,     9,     0,    33,     0,
       0,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   620,     0,     9,     0,    33,     0,
       0,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   686,     0,     9,     0,    33,     0,
       0,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24,    25,     0,    26,
      27,    28,    29,    30,    31,    32,     4,     0,     0,     5,
       0,     0,     6,     0,     0,     0,     0,     0,     0,     7,
       0,     0,     0,     0,   701,     0,     9,     0,    33,     0,
       0,     0,     0,     0,    10,     0,     0,    11,     0,     0,
      12,     0,     0,     0,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,   318,    24,    25,     5,    26,
      27,    28,    29,    30,    31,    32,     0,     0,   507,     0,
       0,     0,     0,     8,   318,     9,     0,     5,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   511,    33,     0,
       0,     0,     8,     0,     9,    15,    16,     0,    18,    19,
      20,     0,     0,     0,     0,    24,    25,     0,    26,     0,
       0,     0,    30,    31,    15,    16,     0,    18,    19,    20,
     318,     0,     0,     5,    24,    25,     0,    26,     0,     0,
       0,    30,    31,     0,     0,     0,     0,   508,     8,   318,
       9,     0,     5,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   512,     8,     0,     9,
      15,    16,     0,    18,    19,    20,     0,     0,     0,     0,
      24,    25,     0,    26,     0,     0,     0,    30,    31,    15,
      16,     0,    18,    19,    20,     0,     0,     0,     0,    24,
      25,     0,    26,     0,     0,     0,    30,    31,     0,     0,
       0,     0,   319,    66,    67,    68,     0,    69,    70,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   659,     8,     0,     0,    71,    72,    73,    74,     0,
       0,    75,     0,    76,    77,     0,    98,    66,    67,    68,
       0,    69,    70,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    78,     0,    25,     8,    79,     0,    71,
      72,    73,    74,     0,     0,    75,     0,    76,    77,     0,
       0,     0,     0,     0,     0,    80,     0,   139,    81,     0,
      82,     0,    83,     0,   185,   186,     0,    78,     0,    25,
       0,    79,     0,     0,     0,     0,    66,    67,    68,     0,
      69,    70,     0,     0,     0,     0,    66,    67,    68,    80,
      69,     0,    81,     0,    82,     8,    83,     0,    71,    72,
      73,    74,     0,     0,     0,     8,    76,    77,    71,    72,
      73,    74,     0,   187,   188,   189,   190,   191,   192,   193,
     194,   195,   196,     0,     0,     0,    78,     0,    25,     0,
      79,   146,     0,     0,     0,     0,    78,     0,    25,     0,
      79,     0,     0,     0,     0,     0,     0,     0,    80,     0,
       0,    81,     0,    82,     0,    83,     0,     0,     0,     0,
       0,    81,     0,    82,     0,    83,   294,   295,     0,     0,
       0,   244,   245,     0,     0,     0,     0,     0,     0,   246,
       0,   247,   248,     0,   249,     0,   250,   251,   252,   253,
     254,   255,   256,   257,   258,   259,   260,   261,   262,   263,
     264,   265,   266,   267,   268,   269,   270,   271,   272,   273,
     274,   275,   276,   277,   278,   279,   280,   281,   282,   283,
     284,   285,   286,   287,   288,   289,     0,   294,   295,     0,
       0,     0,   244,   245,     0,     0,     0,     0,     0,     0,
     246,   296,   247,   248,     0,   249,     0,   250,   251,   252,
     253,   254,   255,   256,   257,   258,   259,   260,   261,   262,
     263,   264,   265,   266,   267,   268,   269,   270,   271,   272,
     273,   274,   275,   276,   277,   278,   279,   280,   281,   282,
     283,   284,   285,   286,   287,   288,   289,     0,   242,     0,
       0,   243,     0,   244,   245,     0,     0,     0,     0,     0,
       0,   246,   670,   247,   248,     0,   249,     0,   250,   251,
     252,   253,   254,   255,   256,   257,   258,   259,   260,   261,
     262,   263,   264,   265,   266,   267,   268,   269,   270,   271,
     272,   273,   274,   275,   276,   277,   278,   279,   280,   281,
     282,   283,   284,   285,   286,   287,   288,   289,   315,     0,
       0,   316,     0,   244,   245,     0,     0,     0,     0,     0,
       0,   246,     0,   247,   248,     0,   249,     0,   250,   251,
     252,   253,   254,   255,   256,   257,   258,   259,   260,   261,
     262,   263,   264,   265,   266,   267,   268,   269,   270,   271,
     272,   273,   274,   275,   276,   277,   278,   279,   280,   281,
     282,   283,   284,   285,   286,   287,   288,   289,   344,     0,
       0,   345,     0,   244,   245,     0,     0,     0,     0,     0,
       0,   246,     0,   247,   248,     0,   249,     0,   250,   251,
     252,   253,   254,   255,   256,   257,   258,   259,   260,   261,
     262,   263,   264,   265,   266,   267,   268,   269,   270,   271,
     272,   273,   274,   275,   276,   277,   278,   279,   280,   281,
     282,   283,   284,   285,   286,   287,   288,   289
};

static const yytype_int16 yycheck[] =
{
       2,   178,   182,   182,   501,   523,    70,    65,   216,   182,
      83,   455,   114,   198,   458,    75,    80,    19,    20,    21,
     528,     0,   230,   182,     7,   233,     9,   528,   528,   182,
      13,    14,   103,   528,   528,    18,   528,     4,    37,    22,
     182,   182,     4,    26,   528,     4,   182,   565,   566,     4,
     528,    78,     4,   528,   528,   528,   182,   182,   528,   528,
     528,   569,    40,     4,     4,     4,     4,   182,   569,   569,
      22,    64,   147,   526,   569,   569,     4,   569,    84,     4,
      86,     9,    22,    22,    36,   569,    24,    86,    55,     4,
       4,   569,    85,    55,   569,   569,   569,    22,    81,   569,
     569,   569,    57,    55,     4,   180,    84,    22,    22,   627,
      48,   169,   114,     4,    54,   217,    55,   570,    56,    78,
     628,   179,    86,    80,   184,     4,    78,   628,   628,   204,
      55,    22,    89,   628,   628,    32,   628,   201,   202,    54,
      54,    41,   213,    22,   628,   663,    86,    86,     4,   220,
     628,   222,   337,   628,   628,   628,    78,    36,   628,   628,
     628,   144,   145,   234,    55,   148,    22,    89,    79,    40,
       4,    86,    86,    84,     4,   619,    55,     3,     4,     5,
     182,     7,     8,    80,    53,     8,   683,    78,    11,    12,
      13,    14,    89,    64,   177,   692,    22,    64,    54,    25,
      26,    27,    28,    48,    79,    31,     4,    33,    34,    84,
     212,    56,     3,    84,     5,   217,    79,   219,    85,     4,
     203,    84,     7,   206,   207,   208,   209,    53,    81,    55,
      86,    57,     4,    86,    25,    26,    27,    28,   311,    58,
     313,    64,    11,    12,    13,    14,    79,   305,   319,    75,
      18,    84,    78,    21,    80,    83,    82,    80,    86,    82,
      84,    86,    86,    89,    85,   677,    89,   679,     4,     5,
       4,     5,    74,    75,    86,   455,   455,    86,   458,   458,
      76,    77,   455,    74,    75,   458,    15,    16,     4,     5,
     185,   186,   199,   200,    86,    64,   455,    86,    86,   458,
      86,    78,   455,    86,    86,   458,    86,    86,    86,   292,
     293,    86,     4,   455,   455,   373,   458,   458,   301,   455,
      78,   394,   458,     4,    78,    19,   399,   385,   386,   455,
     455,    30,   458,   458,    29,     3,    57,    78,     4,    86,
     455,    86,   413,   458,    86,   403,   404,    78,   528,   528,
      78,    78,   410,   411,   356,   528,    89,     4,   429,    35,
      78,    86,     4,   434,   366,    79,   368,    84,    79,   528,
     353,    78,   355,    79,   357,   528,   378,    79,     4,     4,
      18,     4,     4,     4,     4,    78,   528,   528,     5,   569,
     569,   462,   528,     5,    35,   466,   569,    81,     6,    78,
      78,    78,   528,   528,   406,    79,    86,    82,   391,   392,
     569,    84,   483,   528,   485,   486,   569,    84,    79,    78,
      78,   492,    78,    86,   495,    78,    81,   569,   569,    78,
      36,   414,   415,   569,    79,    86,    40,   508,   421,   619,
     619,   512,   650,   569,   569,     4,   619,    79,   628,   628,
     521,   522,    79,   455,   569,   628,   458,    64,    86,   442,
     619,   463,    86,    79,    79,    84,   619,   450,     9,   628,
      83,     4,   649,    20,    79,   628,    86,   619,   619,    40,
      42,    19,     4,   619,   467,   487,   628,   628,    86,   491,
      86,    86,   628,   619,   619,    86,    23,   568,    86,    86,
      86,    50,   628,   628,   619,    79,    52,    86,    60,     4,
      86,    79,    79,   628,   516,    79,   518,    17,    19,    79,
      86,   523,    79,    79,   507,    79,   528,     3,   511,     3,
     532,    86,   534,   535,    64,    86,    86,   520,   540,    78,
     542,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    86,   554,   624,    86,    86,   558,    85,    35,    50,
     543,    86,    86,   565,   566,    86,    86,   569,   551,   552,
      86,    58,     4,     4,    19,    19,    32,     3,     4,     5,
      86,     7,     8,   654,    86,    46,   657,    86,   659,    86,
       3,     4,     5,    86,     7,     8,    22,    86,    86,    25,
      26,    27,    28,    86,    86,    31,    86,    33,    34,    22,
      46,    86,    25,    26,    27,    28,    86,   619,    31,    86,
      33,    34,   693,    86,    86,   627,   628,    53,    86,    55,
      50,    57,    86,    86,    59,    58,    78,    78,    86,    86,
      53,     4,    55,    86,    57,    79,    17,    17,    86,    75,
       5,    86,    78,    86,    80,    81,    82,    86,    86,    86,
      86,   663,    75,    54,    86,    78,    85,    80,    86,    82,
      86,    86,   674,    86,    86,   677,    79,   679,    86,    86,
      86,     4,     5,     4,    86,     4,     9,    10,     4,    86,
      86,    90,   149,   676,    17,   697,    19,    20,   128,    22,
     137,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,   219,   382,   680,   183,    76,     4,     5,   197,   533,
     448,     9,    10,   550,    -1,    -1,   304,    -1,    -1,    17,
      83,    19,    20,    86,    22,    -1,    24,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    -1,    -1,    17,
      -1,    -1,    -1,    -1,    22,    -1,    24,    -1,    86,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      38,    39,    -1,    -1,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    -1,    54,    55,    -1,    57,
      58,    59,    60,    61,    62,    63,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    -1,    -1,    17,
      -1,    -1,    20,    -1,    22,    -1,    24,    -1,    86,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      38,    -1,    -1,    -1,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    -1,    54,    55,    -1,    57,
      58,    59,    60,    61,    62,    63,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    -1,    -1,    17,
      -1,    -1,    -1,    -1,    22,    -1,    24,    -1,    86,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      38,    -1,    -1,    -1,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    -1,    54,    55,    -1,    57,
      58,    59,    60,    61,    62,    63,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    -1,    -1,    17,
      -1,    -1,    -1,    -1,    22,    -1,    24,    -1,    86,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      38,    -1,    -1,    -1,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    -1,    54,    55,    -1,    57,
      58,    59,    60,    61,    62,    63,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    -1,    -1,    17,
      -1,    -1,    -1,    -1,    22,    -1,    24,    -1,    86,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      38,    -1,    -1,    -1,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    -1,    54,    55,    -1,    57,
      58,    59,    60,    61,    62,    63,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    -1,    -1,    17,
      -1,    -1,    -1,    -1,    22,    -1,    24,    -1,    86,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      38,    -1,    -1,    -1,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    -1,    54,    55,    -1,    57,
      58,    59,    60,    61,    62,    63,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    -1,    -1,    17,
      -1,    -1,    -1,    -1,    22,    -1,    24,    -1,    86,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      38,    -1,    -1,    -1,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    -1,    54,    55,    -1,    57,
      58,    59,    60,    61,    62,    63,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    -1,    -1,    17,
      -1,    -1,    -1,    -1,    22,    -1,    24,    -1,    86,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      38,    -1,    -1,    -1,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    -1,    54,    55,    -1,    57,
      58,    59,    60,    61,    62,    63,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    -1,    -1,    17,
      -1,    -1,    -1,    -1,    22,    -1,    24,    -1,    86,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      38,    -1,    -1,    -1,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    -1,    54,    55,    -1,    57,
      58,    59,    60,    61,    62,    63,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    -1,    -1,    17,
      -1,    -1,    -1,    -1,    22,    -1,    24,    -1,    86,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      38,    -1,    -1,    -1,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    -1,    54,    55,    -1,    57,
      58,    59,    60,    61,    62,    63,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    -1,    -1,    17,
      -1,    -1,    -1,    -1,    22,    -1,    24,    -1,    86,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      38,    -1,    -1,    -1,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    -1,    54,    55,    -1,    57,
      58,    59,    60,    61,    62,    63,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    -1,    -1,    17,
      -1,    -1,    -1,    -1,    22,    -1,    24,    -1,    86,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      38,    -1,    -1,    -1,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    -1,    54,    55,    -1,    57,
      58,    59,    60,    61,    62,    63,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    -1,    -1,    17,
      -1,    -1,    -1,    -1,    22,    -1,    24,    -1,    86,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      38,    -1,    -1,    -1,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    -1,    54,    55,    -1,    57,
      58,    59,    60,    61,    62,    63,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    -1,    -1,    17,
      -1,    -1,    -1,    -1,    22,    -1,    24,    -1,    86,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      38,    -1,    -1,    -1,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    -1,    54,    55,    -1,    57,
      58,    59,    60,    61,    62,    63,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    -1,    -1,    17,
      -1,    -1,    -1,    -1,    22,    -1,    24,    -1,    86,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      38,    -1,    -1,    -1,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    -1,    54,    55,    -1,    57,
      58,    59,    60,    61,    62,    63,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    -1,    -1,    17,
      -1,    -1,    -1,    -1,    22,    -1,    24,    -1,    86,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      38,    -1,    -1,    -1,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    -1,    54,    55,    -1,    57,
      58,    59,    60,    61,    62,    63,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    -1,    -1,    17,
      -1,    -1,    -1,    -1,    22,    -1,    24,    -1,    86,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      38,    -1,    -1,    -1,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    -1,    54,    55,    -1,    57,
      58,    59,    60,    61,    62,    63,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    -1,    -1,    17,
      -1,    -1,    -1,    -1,    22,    -1,    24,    -1,    86,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      38,    -1,    -1,    -1,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    -1,    54,    55,    -1,    57,
      58,    59,    60,    61,    62,    63,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    -1,    -1,    17,
      -1,    -1,    -1,    -1,    22,    -1,    24,    -1,    86,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      38,    -1,    -1,    -1,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    -1,    54,    55,    -1,    57,
      58,    59,    60,    61,    62,    63,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    -1,    -1,    17,
      -1,    -1,    -1,    -1,    22,    -1,    24,    -1,    86,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      38,    -1,    -1,    -1,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    -1,    54,    55,    -1,    57,
      58,    59,    60,    61,    62,    63,     4,    -1,    -1,     7,
      -1,    -1,    10,    -1,    -1,    -1,    -1,    -1,    -1,    17,
      -1,    -1,    -1,    -1,    22,    -1,    24,    -1,    86,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    35,    -1,    -1,
      38,    -1,    -1,    -1,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,     4,    54,    55,     7,    57,
      58,    59,    60,    61,    62,    63,    -1,    -1,    17,    -1,
      -1,    -1,    -1,    22,     4,    24,    -1,     7,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    17,    86,    -1,
      -1,    -1,    22,    -1,    24,    44,    45,    -1,    47,    48,
      49,    -1,    -1,    -1,    -1,    54,    55,    -1,    57,    -1,
      -1,    -1,    61,    62,    44,    45,    -1,    47,    48,    49,
       4,    -1,    -1,     7,    54,    55,    -1,    57,    -1,    -1,
      -1,    61,    62,    -1,    -1,    -1,    -1,    86,    22,     4,
      24,    -1,     7,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    86,    22,    -1,    24,
      44,    45,    -1,    47,    48,    49,    -1,    -1,    -1,    -1,
      54,    55,    -1,    57,    -1,    -1,    -1,    61,    62,    44,
      45,    -1,    47,    48,    49,    -1,    -1,    -1,    -1,    54,
      55,    -1,    57,    -1,    -1,    -1,    61,    62,    -1,    -1,
      -1,    -1,    86,     3,     4,     5,    -1,     7,     8,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    86,    22,    -1,    -1,    25,    26,    27,    28,    -1,
      -1,    31,    -1,    33,    34,    -1,    36,     3,     4,     5,
      -1,     7,     8,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    53,    -1,    55,    22,    57,    -1,    25,
      26,    27,    28,    -1,    -1,    31,    -1,    33,    34,    -1,
      -1,    -1,    -1,    -1,    -1,    75,    -1,     8,    78,    -1,
      80,    -1,    82,    -1,    15,    16,    -1,    53,    -1,    55,
      -1,    57,    -1,    -1,    -1,    -1,     3,     4,     5,    -1,
       7,     8,    -1,    -1,    -1,    -1,     3,     4,     5,    75,
       7,    -1,    78,    -1,    80,    22,    82,    -1,    25,    26,
      27,    28,    -1,    -1,    -1,    22,    33,    34,    25,    26,
      27,    28,    -1,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    -1,    -1,    -1,    53,    -1,    55,    -1,
      57,    82,    -1,    -1,    -1,    -1,    53,    -1,    55,    -1,
      57,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    75,    -1,
      -1,    78,    -1,    80,    -1,    82,    -1,    -1,    -1,    -1,
      -1,    78,    -1,    80,    -1,    82,     4,     5,    -1,    -1,
      -1,     9,    10,    -1,    -1,    -1,    -1,    -1,    -1,    17,
      -1,    19,    20,    -1,    22,    -1,    24,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    -1,     4,     5,    -1,
      -1,    -1,     9,    10,    -1,    -1,    -1,    -1,    -1,    -1,
      17,    79,    19,    20,    -1,    22,    -1,    24,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    -1,     4,    -1,
      -1,     7,    -1,     9,    10,    -1,    -1,    -1,    -1,    -1,
      -1,    17,    79,    19,    20,    -1,    22,    -1,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,     4,    -1,
      -1,     7,    -1,     9,    10,    -1,    -1,    -1,    -1,    -1,
      -1,    17,    -1,    19,    20,    -1,    22,    -1,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,     4,    -1,
      -1,     7,    -1,     9,    10,    -1,    -1,    -1,    -1,    -1,
      -1,    17,    -1,    19,    20,    -1,    22,    -1,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    91,    92,     0,     4,     7,    10,    17,    22,    24,
      32,    35,    38,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    54,    55,    57,    58,    59,    60,
      61,    62,    63,    86,    93,    94,    96,    97,   102,   103,
     104,   106,   107,   108,   109,   114,   115,   116,   117,   118,
     121,   122,   124,   131,   132,   133,   134,   135,   136,   137,
     138,   139,   140,     4,    78,    78,     3,     4,     5,     7,
       8,    25,    26,    27,    28,    31,    33,    34,    53,    57,
      75,    78,    80,    82,    97,   144,   145,   146,   147,   148,
     149,   150,   151,   152,   153,   155,   156,   159,    36,   144,
       4,     4,    41,    86,   144,   144,     4,     4,     4,     7,
     144,    97,    97,     4,    78,    97,   123,   130,   144,    53,
       4,    57,    89,   144,     4,    22,    36,    55,   100,   101,
     119,     4,     4,     4,     5,     4,     5,    58,    86,     8,
      11,    12,    13,    14,    64,    80,    82,    89,    95,    98,
      85,    86,    86,    86,    86,    86,    86,    86,    86,    86,
      86,    86,    86,    78,   144,   160,   161,   160,     4,    78,
     152,   147,   153,     4,    78,   152,   144,   170,   170,    78,
      89,   157,    19,    30,    29,    15,    16,    64,    65,    66,
      67,    68,    69,    70,    71,    72,    73,    98,   154,    74,
      75,    76,    77,    80,    89,     3,    57,    78,    40,    64,
      84,     4,    92,    86,    86,    78,    78,    78,   123,    84,
      86,    89,    86,     4,    24,    48,    56,    48,    56,     4,
      78,   101,    35,    78,    86,     4,     9,     4,   119,   144,
     144,    99,     4,     7,     9,    10,    17,    19,    20,    22,
      24,    25,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
     167,   144,    64,    95,     4,     5,    79,   166,   167,   168,
      79,    84,    79,   160,    32,    78,    79,    79,    81,    86,
     144,   162,    83,   168,   160,     4,     7,   167,     4,    86,
      94,    97,   102,   103,   118,   132,   133,   134,   136,   137,
     138,   139,   143,   146,   147,   150,   150,   154,   149,   151,
     151,   152,   152,   144,     4,     7,   167,     4,   144,   144,
     144,   144,     4,    40,    84,    39,    92,    18,   110,     4,
     163,   165,   163,   123,    79,   130,    92,     4,    92,     4,
      55,     4,    55,    78,   163,     4,   120,   163,    92,     5,
       4,     5,    35,    81,     6,    78,    78,   144,   144,    78,
      86,    64,    85,    79,    84,   144,    79,   156,   160,    84,
     170,   170,    79,    78,    78,   158,    92,    86,   149,    81,
      78,    78,    79,    86,    36,    40,   144,     4,   144,    22,
     144,    18,    21,   111,    64,    79,    84,    79,    79,    86,
      22,    22,   160,    79,    86,    79,    22,     9,   120,    83,
     160,   160,     4,   169,   126,   144,   144,    86,   170,    79,
     170,    81,    83,   160,   160,    20,    22,   141,    20,   142,
     160,   160,    86,    92,   144,   144,    86,    40,    86,    42,
      19,   144,    86,    23,     3,     5,    25,    26,    27,    28,
      74,    75,   164,    86,     4,    86,    86,    92,    50,    52,
      79,    92,    86,    60,     4,    86,    79,    79,   144,    79,
     127,   125,     4,   166,   144,    79,    79,    17,    86,   143,
      17,    17,    86,   143,    79,    79,    92,    22,    55,   105,
      37,    86,    86,    92,   144,    86,    86,    19,   112,    86,
       3,     3,    92,    64,    92,    92,    22,    86,    86,    22,
      92,    86,    92,    85,     4,    22,    54,    86,   128,   127,
      78,    64,    85,   144,    92,    86,    86,   144,    92,    86,
      22,    35,    86,    97,   144,    92,    92,   105,    86,   112,
      86,    10,    86,    94,   102,   103,   104,   106,   107,   108,
     109,   113,   114,   115,   116,   117,   118,   121,   122,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,    22,
     164,    22,    22,    50,    58,    22,    22,   144,     4,     5,
     129,     4,     4,    22,   169,   144,   144,    19,    22,    19,
      22,    32,    86,    86,    86,   105,   105,    92,   112,    86,
      86,    86,    86,    86,    86,    86,    86,    86,    86,    86,
      86,    86,    46,    46,    50,    86,    86,    59,    58,    78,
      78,    84,    86,    86,    86,     4,    79,    86,    17,    86,
     143,    17,    86,    92,   105,    86,    86,    86,    86,    86,
      79,   168,   163,     5,    92,    86,    85,    92,    86,    92,
      86,    86,   105,    86,    79,    79,    22,   144,   141,   141,
     142,   127,    86,    86,    54,    22,   127,    92,    86,     4,
      22,    22,    86,     4,     4,    86,    86
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    90,    91,    92,    92,    92,    93,    93,    93,    93,
      93,    93,    93,    93,    93,    93,    93,    93,    93,    93,
      93,    93,    93,    93,    93,    93,    93,    93,    93,    93,
      93,    93,    93,    94,    94,    94,    94,    95,    95,    95,
      95,    96,    96,    96,    97,    97,    97,    99,    98,    98,
     100,   100,   101,   101,   101,   101,   102,   102,   103,   103,
     103,   103,   103,   104,   105,   105,   105,   106,   106,   106,
     106,   106,   106,   107,   108,   109,   110,   110,   111,   111,
     112,   112,   112,   113,   113,   113,   113,   113,   113,   113,
     113,   113,   113,   113,   113,   113,   113,   113,   113,   113,
     113,   113,   113,   113,   113,   113,   113,   113,   113,   114,
     114,   115,   115,   116,   117,   118,   118,   118,   118,   118,
     118,   118,   118,   119,   119,   120,   121,   121,   121,   122,
     123,   123,   125,   124,   126,   124,   127,   127,   127,   128,
     128,   128,   128,   128,   129,   129,   130,   130,   131,   132,
     132,   132,   132,   132,   132,   132,   132,   133,   134,   134,
     135,   136,   137,   138,   138,   139,   139,   140,   140,   141,
     141,   141,   141,   142,   142,   142,   142,   142,   143,   143,
     143,   143,   143,   143,   143,   143,   143,   143,   143,   144,
     145,   145,   146,   146,   147,   147,   148,   148,   148,   149,
     149,   149,   150,   150,   150,   151,   151,   151,   152,   152,
     152,   152,   152,   152,   153,   153,   153,   153,   153,   154,
     154,   154,   154,   154,   154,   154,   154,   154,   154,   155,
     155,   155,   155,   155,   155,   155,   155,   155,   155,   155,
     155,   155,   155,   155,   156,   156,   157,   157,   157,   157,
     158,   158,   159,   159,   160,   160,   161,   161,   162,   162,
     163,   163,   164,   164,   164,   164,   164,   164,   164,   164,
     165,   165,   165,   165,   166,   166,   167,   167,   167,   167,
     167,   167,   167,   167,   167,   167,   167,   167,   167,   167,
     167,   167,   167,   167,   167,   167,   167,   167,   167,   167,
     167,   167,   167,   167,   167,   167,   167,   167,   167,   167,
     167,   167,   167,   167,   167,   167,   167,   167,   167,   167,
     167,   167,   167,   168,   168,   168,   168,   168,   168,   169,
     169,   170,   170
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     2,     1,
       1,     2,     1,     2,     2,     2,     2,     2,     2,     2,
       2,     1,     1,     3,     3,     4,     4,     1,     1,     1,
       1,     1,     4,     3,     1,     1,     1,     0,     4,     1,
       1,     2,     1,     1,     1,     1,     2,     4,     4,     4,
       6,     6,     6,    10,     3,     2,     3,     7,     8,     9,
      10,     9,    11,     6,     7,     7,     5,     6,     0,     3,
       0,     2,     2,     2,     2,     2,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     1,     2,     1,     2,
       2,     2,     2,     2,     2,     2,     2,     1,     1,    10,
      10,     9,    10,    10,     7,     2,     2,     2,     2,     4,
       4,     4,     6,     1,     4,     1,     9,     7,    10,     2,
       1,     3,     0,    11,     0,    10,     0,     2,     2,     3,
      10,    10,     9,     7,     1,     3,     1,     3,     7,     4,
       4,     3,     4,     4,     3,     3,     3,     2,     1,     2,
       2,     2,     2,     1,     2,     1,     2,     6,     6,     3,
       3,     6,     7,     0,     3,     6,     7,     7,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     3,     1,     3,     1,     2,     1,     3,     4,     1,
       3,     3,     1,     3,     3,     1,     3,     3,     1,     2,
       2,     2,     4,     5,     1,     4,     3,     6,     6,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       3,     1,     1,     2,     4,     1,     1,     1,     1,     1,
       3,     3,     5,     1,     3,     5,     0,     3,     3,     5,
       0,     3,     2,     3,     0,     1,     1,     3,     1,     4,
       0,     1,     1,     2,     2,     1,     1,     1,     1,     1,
       1,     3,     3,     5,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     3,     3,     6,     6,     6,     9,     1,
       2,     0,     2
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
#line 752 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2855 "src/parser.tab.c"
        break;

    case YYSYMBOL_STRING: /* STRING  */
#line 752 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2861 "src/parser.tab.c"
        break;

    case YYSYMBOL_LENS_CONTENT: /* LENS_CONTENT  */
#line 752 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2867 "src/parser.tab.c"
        break;

    case YYSYMBOL_QUALIFIED_IDENT: /* QUALIFIED_IDENT  */
#line 752 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2873 "src/parser.tab.c"
        break;

    case YYSYMBOL_MODIFIER_PREFIX: /* MODIFIER_PREFIX  */
#line 752 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2879 "src/parser.tab.c"
        break;

    case YYSYMBOL_program: /* program  */
#line 775 "src/parser.y"
            { (void) ((*yyvaluep).stmt_list); }
#line 2885 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement_list: /* statement_list  */
#line 718 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2891 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement: /* statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2897 "src/parser.tab.c"
        break;

    case YYSYMBOL_assignment: /* assignment  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2903 "src/parser.tab.c"
        break;

    case YYSYMBOL_lvalue: /* lvalue  */
#line 753 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 2909 "src/parser.tab.c"
        break;

    case YYSYMBOL_variable_name: /* variable_name  */
#line 752 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2915 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_lens: /* comparison_lens  */
#line 760 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 2921 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_name: /* modifier_name  */
#line 752 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2927 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_word: /* modifier_word  */
#line 752 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2933 "src/parser.tab.c"
        break;

    case YYSYMBOL_print_statement: /* print_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2939 "src/parser.tab.c"
        break;

    case YYSYMBOL_call_statement: /* call_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2945 "src/parser.tab.c"
        break;

    case YYSYMBOL_with_lock_statement: /* with_lock_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2951 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_end: /* for_end  */
#line 752 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 2957 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_each_statement: /* for_each_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2963 "src/parser.tab.c"
        break;

    case YYSYMBOL_do_loop_statement: /* do_loop_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2969 "src/parser.tab.c"
        break;

    case YYSYMBOL_while_statement: /* while_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2975 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement: /* consider_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 2981 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_branch_list: /* consider_branch_list  */
#line 758 "src/parser.y"
            { ast_free_consider_branch_list(((*yyvaluep).consider_branch_list)); }
#line 2987 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_else_opt: /* consider_else_opt  */
#line 718 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2993 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement_list: /* consider_statement_list  */
#line 718 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 2999 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_body_statement: /* consider_body_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3005 "src/parser.tab.c"
        break;

    case YYSYMBOL_function_statement: /* function_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3011 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_statement: /* modifier_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3017 "src/parser.tab.c"
        break;

    case YYSYMBOL_program_statement: /* program_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3023 "src/parser.tab.c"
        break;

    case YYSYMBOL_library_statement: /* library_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3029 "src/parser.tab.c"
        break;

    case YYSYMBOL_use_statement: /* use_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3035 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_signature: /* modifier_signature  */
#line 761 "src/parser.y"
            { ast_free_modifier_signature(((*yyvaluep).modifier_signature)); }
#line 3041 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_context: /* modifier_context  */
#line 752 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3047 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_statement: /* watch_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3053 "src/parser.tab.c"
        break;

    case YYSYMBOL_unwatch_statement: /* unwatch_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3059 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_list: /* watch_target_list  */
#line 759 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3065 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_statement: /* server_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3071 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item_list: /* server_item_list  */
#line 765 "src/parser.y"
            { ast_free_server_item_list(((*yyvaluep).server_item_list)); }
#line 3077 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item: /* server_item  */
#line 764 "src/parser.y"
            { AstServerItemList one = ast_server_item_list_append(ast_server_item_list_empty(), ((*yyvaluep).server_item)); ast_free_server_item_list(one); }
#line 3083 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_string_list: /* server_string_list  */
#line 759 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3089 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_path: /* watch_target_path  */
#line 752 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3095 "src/parser.tab.c"
        break;

    case YYSYMBOL_without_watchers_statement: /* without_watchers_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3101 "src/parser.tab.c"
        break;

    case YYSYMBOL_on_error_statement: /* on_error_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3107 "src/parser.tab.c"
        break;

    case YYSYMBOL_error_statement: /* error_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3113 "src/parser.tab.c"
        break;

    case YYSYMBOL_return_statement: /* return_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3119 "src/parser.tab.c"
        break;

    case YYSYMBOL_label_statement: /* label_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3125 "src/parser.tab.c"
        break;

    case YYSYMBOL_goto_statement: /* goto_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3131 "src/parser.tab.c"
        break;

    case YYSYMBOL_gosub_statement: /* gosub_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3137 "src/parser.tab.c"
        break;

    case YYSYMBOL_break_statement: /* break_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3143 "src/parser.tab.c"
        break;

    case YYSYMBOL_continue_statement: /* continue_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3149 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_statement: /* if_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3155 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_block_tail: /* if_block_tail  */
#line 718 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3161 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_inline_tail: /* if_inline_tail  */
#line 718 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3167 "src/parser.tab.c"
        break;

    case YYSYMBOL_inline_statement: /* inline_statement  */
#line 754 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3173 "src/parser.tab.c"
        break;

    case YYSYMBOL_expression: /* expression  */
#line 753 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3179 "src/parser.tab.c"
        break;

    case YYSYMBOL_or_expression: /* or_expression  */
#line 753 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3185 "src/parser.tab.c"
        break;

    case YYSYMBOL_and_expression: /* and_expression  */
#line 753 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3191 "src/parser.tab.c"
        break;

    case YYSYMBOL_not_expression: /* not_expression  */
#line 753 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3197 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_expression: /* comparison_expression  */
#line 753 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3203 "src/parser.tab.c"
        break;

    case YYSYMBOL_set_expression: /* set_expression  */
#line 753 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3209 "src/parser.tab.c"
        break;

    case YYSYMBOL_additive_expression: /* additive_expression  */
#line 753 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3215 "src/parser.tab.c"
        break;

    case YYSYMBOL_multiplicative_expression: /* multiplicative_expression  */
#line 753 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3221 "src/parser.tab.c"
        break;

    case YYSYMBOL_unary_expression: /* unary_expression  */
#line 753 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3227 "src/parser.tab.c"
        break;

    case YYSYMBOL_postfix_expression: /* postfix_expression  */
#line 753 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3233 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_operator: /* comparison_operator  */
#line 752 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3239 "src/parser.tab.c"
        break;

    case YYSYMBOL_primary: /* primary  */
#line 753 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3245 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_literal: /* record_literal  */
#line 753 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3251 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_suffix: /* ident_suffix  */
#line 762 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3257 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_dot_suffix: /* ident_dot_suffix  */
#line 762 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3263 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list_opt: /* argument_list_opt  */
#line 756 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3269 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list: /* argument_list  */
#line 756 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3275 "src/parser.tab.c"
        break;

    case YYSYMBOL_array_argument_list: /* array_argument_list  */
#line 756 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3281 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list_opt: /* parameter_list_opt  */
#line 759 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3287 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_default: /* parameter_default  */
#line 753 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3293 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list: /* parameter_list  */
#line 759 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3299 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_name: /* field_name  */
#line 752 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3305 "src/parser.tab.c"
        break;

    case YYSYMBOL_dot_field_name: /* dot_field_name  */
#line 752 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3311 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_field_list: /* record_field_list  */
#line 757 "src/parser.y"
            { ast_free_record_field_list(((*yyvaluep).record_field_list)); }
#line 3317 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_policy: /* field_policy  */
#line 763 "src/parser.y"
            { ast_free_expr(((*yyvaluep).field_policy).reset_expr); }
#line 3323 "src/parser.tab.c"
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
#line 780 "src/parser.y"
                     { ctx->parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3629 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 784 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3635 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 785 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3641 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 786 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3647 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 790 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3653 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 791 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3659 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 792 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3665 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 793 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3671 "src/parser.tab.c"
    break;

  case 10: /* statement: for_each_statement  */
#line 794 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3677 "src/parser.tab.c"
    break;

  case 11: /* statement: while_statement  */
#line 795 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3683 "src/parser.tab.c"
    break;

  case 12: /* statement: do_loop_statement  */
#line 796 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3689 "src/parser.tab.c"
    break;

  case 13: /* statement: consider_statement  */
#line 797 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3695 "src/parser.tab.c"
    break;

  case 14: /* statement: function_statement  */
#line 798 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3701 "src/parser.tab.c"
    break;

  case 15: /* statement: modifier_statement  */
#line 799 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3707 "src/parser.tab.c"
    break;

  case 16: /* statement: program_statement  */
#line 800 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3713 "src/parser.tab.c"
    break;

  case 17: /* statement: library_statement  */
#line 801 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3719 "src/parser.tab.c"
    break;

  case 18: /* statement: use_statement NEWLINE  */
#line 802 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3725 "src/parser.tab.c"
    break;

  case 19: /* statement: watch_statement  */
#line 803 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3731 "src/parser.tab.c"
    break;

  case 20: /* statement: server_statement  */
#line 804 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3737 "src/parser.tab.c"
    break;

  case 21: /* statement: unwatch_statement NEWLINE  */
#line 805 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3743 "src/parser.tab.c"
    break;

  case 22: /* statement: without_watchers_statement  */
#line 806 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3749 "src/parser.tab.c"
    break;

  case 23: /* statement: on_error_statement NEWLINE  */
#line 807 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3755 "src/parser.tab.c"
    break;

  case 24: /* statement: error_statement NEWLINE  */
#line 808 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3761 "src/parser.tab.c"
    break;

  case 25: /* statement: return_statement NEWLINE  */
#line 809 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3767 "src/parser.tab.c"
    break;

  case 26: /* statement: label_statement NEWLINE  */
#line 810 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3773 "src/parser.tab.c"
    break;

  case 27: /* statement: goto_statement NEWLINE  */
#line 811 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3779 "src/parser.tab.c"
    break;

  case 28: /* statement: gosub_statement NEWLINE  */
#line 812 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3785 "src/parser.tab.c"
    break;

  case 29: /* statement: break_statement NEWLINE  */
#line 813 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3791 "src/parser.tab.c"
    break;

  case 30: /* statement: continue_statement NEWLINE  */
#line 814 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3797 "src/parser.tab.c"
    break;

  case 31: /* statement: if_statement  */
#line 815 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3803 "src/parser.tab.c"
    break;

  case 32: /* statement: DIM  */
#line 821 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 3816 "src/parser.tab.c"
    break;

  case 33: /* assignment: lvalue OP_EQ expression  */
#line 832 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 3822 "src/parser.tab.c"
    break;

  case 34: /* assignment: lvalue compound_op expression  */
#line 837 "src/parser.y"
                                    { (yyval.stmt) = ast_assign_op((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr), (yyvsp[-1].op_char)); }
#line 3828 "src/parser.tab.c"
    break;

  case 35: /* assignment: lvalue comparison_lens compound_op expression  */
#line 838 "src/parser.y"
                                                    {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign_op((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr), (yyvsp[-1].op_char));
      }
#line 3842 "src/parser.tab.c"
    break;

  case 36: /* assignment: lvalue comparison_lens OP_EQ expression  */
#line 850 "src/parser.y"
                                              {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 3856 "src/parser.tab.c"
    break;

  case 37: /* compound_op: PLUS_EQ  */
#line 862 "src/parser.y"
               { (yyval.op_char) = '+'; }
#line 3862 "src/parser.tab.c"
    break;

  case 38: /* compound_op: MINUS_EQ  */
#line 863 "src/parser.y"
               { (yyval.op_char) = '-'; }
#line 3868 "src/parser.tab.c"
    break;

  case 39: /* compound_op: STAR_EQ  */
#line 864 "src/parser.y"
               { (yyval.op_char) = '*'; }
#line 3874 "src/parser.tab.c"
    break;

  case 40: /* compound_op: SLASH_EQ  */
#line 865 "src/parser.y"
               { (yyval.op_char) = '/'; }
#line 3880 "src/parser.tab.c"
    break;

  case 41: /* lvalue: variable_name  */
#line 869 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3886 "src/parser.tab.c"
    break;

  case 42: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 870 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3892 "src/parser.tab.c"
    break;

  case 43: /* lvalue: lvalue DOT dot_field_name  */
#line 871 "src/parser.y"
                                             { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3898 "src/parser.tab.c"
    break;

  case 44: /* variable_name: IDENT  */
#line 875 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 3904 "src/parser.tab.c"
    break;

  case 45: /* variable_name: END  */
#line 876 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 3910 "src/parser.tab.c"
    break;

  case 46: /* variable_name: NEXT  */
#line 877 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 3916 "src/parser.tab.c"
    break;

  case 47: /* $@1: %empty  */
#line 888 "src/parser.y"
             { lexer_begin_lens_content(ctx->active_lexer); }
#line 3922 "src/parser.tab.c"
    break;

  case 48: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 888 "src/parser.y"
                                                                                  {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 3930 "src/parser.tab.c"
    break;

  case 49: /* comparison_lens: MODIFIER_PREFIX  */
#line 896 "src/parser.y"
                      { (yyval.modifier) = parse_modifier_use((yyvsp[0].text)); }
#line 3936 "src/parser.tab.c"
    break;

  case 50: /* modifier_name: modifier_word  */
#line 900 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 3942 "src/parser.tab.c"
    break;

  case 51: /* modifier_name: modifier_name modifier_word  */
#line 901 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 3948 "src/parser.tab.c"
    break;

  case 52: /* modifier_word: IDENT  */
#line 905 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 3954 "src/parser.tab.c"
    break;

  case 53: /* modifier_word: TO  */
#line 906 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 3960 "src/parser.tab.c"
    break;

  case 54: /* modifier_word: END  */
#line 907 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 3966 "src/parser.tab.c"
    break;

  case 55: /* modifier_word: NEXT  */
#line 908 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 3972 "src/parser.tab.c"
    break;

  case 56: /* print_statement: PRINT expression  */
#line 912 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 3978 "src/parser.tab.c"
    break;

  case 57: /* print_statement: PRINT TO ERROR_VALUE expression  */
#line 918 "src/parser.y"
                                      { (yyval.stmt) = ast_print_error((yyvsp[0].expr)); }
#line 3984 "src/parser.tab.c"
    break;

  case 58: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 922 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 3990 "src/parser.tab.c"
    break;

  case 59: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 923 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 4001 "src/parser.tab.c"
    break;

  case 60: /* call_statement: lvalue DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 929 "src/parser.y"
                                                       {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 4011 "src/parser.tab.c"
    break;

  case 61: /* call_statement: lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 934 "src/parser.y"
                                                                 {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 4025 "src/parser.tab.c"
    break;

  case 62: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 943 "src/parser.y"
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
#line 4040 "src/parser.tab.c"
    break;

  case 63: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 956 "src/parser.y"
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
#line 4063 "src/parser.tab.c"
    break;

  case 64: /* for_end: END FOR NEWLINE  */
#line 985 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 4069 "src/parser.tab.c"
    break;

  case 65: /* for_end: NEXT NEWLINE  */
#line 986 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 4075 "src/parser.tab.c"
    break;

  case 66: /* for_end: NEXT variable_name NEWLINE  */
#line 987 "src/parser.y"
                                 { (yyval.text) = (yyvsp[-1].text); }
#line 4081 "src/parser.tab.c"
    break;

  case 67: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list for_end  */
#line 991 "src/parser.y"
                                                             {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4090 "src/parser.tab.c"
    break;

  case 68: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list for_end  */
#line 995 "src/parser.y"
                                                                  {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4099 "src/parser.tab.c"
    break;

  case 69: /* for_each_statement: FOR IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1011 "src/parser.y"
                                                                         {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { YYERROR; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4109 "src/parser.tab.c"
    break;

  case 70: /* for_each_statement: FOR EACH IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1016 "src/parser.y"
                                                                              {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { YYERROR; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4119 "src/parser.tab.c"
    break;

  case 71: /* for_each_statement: FOR IDENT OP_EQ expression TO expression NEWLINE statement_list for_end  */
#line 1024 "src/parser.y"
                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].expr), NULL, (yyvsp[-1].stmt_list));
      }
#line 4128 "src/parser.tab.c"
    break;

  case 72: /* for_each_statement: FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list for_end  */
#line 1028 "src/parser.y"
                                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-9].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-9].text), (yyvsp[-7].expr), (yyvsp[-5].expr), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4137 "src/parser.tab.c"
    break;

  case 73: /* do_loop_statement: DO NEWLINE statement_list UNTIL expression NEWLINE  */
#line 1049 "src/parser.y"
                                                         {
        (yyval.stmt) = ast_do_loop((yyvsp[-3].stmt_list), (yyvsp[-1].expr));
      }
#line 4145 "src/parser.tab.c"
    break;

  case 74: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 1055 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 4153 "src/parser.tab.c"
    break;

  case 75: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 1061 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 4161 "src/parser.tab.c"
    break;

  case 76: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 1067 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 4169 "src/parser.tab.c"
    break;

  case 77: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 1070 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 4177 "src/parser.tab.c"
    break;

  case 78: /* consider_else_opt: %empty  */
#line 1076 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4183 "src/parser.tab.c"
    break;

  case 79: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 1077 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 4189 "src/parser.tab.c"
    break;

  case 80: /* consider_statement_list: %empty  */
#line 1081 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4195 "src/parser.tab.c"
    break;

  case 81: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 1082 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 4201 "src/parser.tab.c"
    break;

  case 82: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 1083 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 4207 "src/parser.tab.c"
    break;

  case 83: /* consider_body_statement: assignment NEWLINE  */
#line 1087 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4213 "src/parser.tab.c"
    break;

  case 84: /* consider_body_statement: print_statement NEWLINE  */
#line 1088 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4219 "src/parser.tab.c"
    break;

  case 85: /* consider_body_statement: call_statement NEWLINE  */
#line 1089 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4225 "src/parser.tab.c"
    break;

  case 86: /* consider_body_statement: with_lock_statement  */
#line 1090 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4231 "src/parser.tab.c"
    break;

  case 87: /* consider_body_statement: for_each_statement  */
#line 1091 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4237 "src/parser.tab.c"
    break;

  case 88: /* consider_body_statement: while_statement  */
#line 1092 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4243 "src/parser.tab.c"
    break;

  case 89: /* consider_body_statement: do_loop_statement  */
#line 1093 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4249 "src/parser.tab.c"
    break;

  case 90: /* consider_body_statement: consider_statement  */
#line 1094 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4255 "src/parser.tab.c"
    break;

  case 91: /* consider_body_statement: function_statement  */
#line 1095 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4261 "src/parser.tab.c"
    break;

  case 92: /* consider_body_statement: modifier_statement  */
#line 1096 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4267 "src/parser.tab.c"
    break;

  case 93: /* consider_body_statement: program_statement  */
#line 1097 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4273 "src/parser.tab.c"
    break;

  case 94: /* consider_body_statement: library_statement  */
#line 1098 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4279 "src/parser.tab.c"
    break;

  case 95: /* consider_body_statement: use_statement NEWLINE  */
#line 1099 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4285 "src/parser.tab.c"
    break;

  case 96: /* consider_body_statement: watch_statement  */
#line 1100 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4291 "src/parser.tab.c"
    break;

  case 97: /* consider_body_statement: unwatch_statement NEWLINE  */
#line 1101 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4297 "src/parser.tab.c"
    break;

  case 98: /* consider_body_statement: without_watchers_statement  */
#line 1102 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4303 "src/parser.tab.c"
    break;

  case 99: /* consider_body_statement: on_error_statement NEWLINE  */
#line 1103 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4309 "src/parser.tab.c"
    break;

  case 100: /* consider_body_statement: error_statement NEWLINE  */
#line 1104 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4315 "src/parser.tab.c"
    break;

  case 101: /* consider_body_statement: return_statement NEWLINE  */
#line 1105 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4321 "src/parser.tab.c"
    break;

  case 102: /* consider_body_statement: label_statement NEWLINE  */
#line 1106 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4327 "src/parser.tab.c"
    break;

  case 103: /* consider_body_statement: goto_statement NEWLINE  */
#line 1107 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4333 "src/parser.tab.c"
    break;

  case 104: /* consider_body_statement: gosub_statement NEWLINE  */
#line 1108 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4339 "src/parser.tab.c"
    break;

  case 105: /* consider_body_statement: break_statement NEWLINE  */
#line 1109 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4345 "src/parser.tab.c"
    break;

  case 106: /* consider_body_statement: continue_statement NEWLINE  */
#line 1110 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4351 "src/parser.tab.c"
    break;

  case 107: /* consider_body_statement: if_statement  */
#line 1111 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4357 "src/parser.tab.c"
    break;

  case 108: /* consider_body_statement: DIM  */
#line 1117 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 4370 "src/parser.tab.c"
    break;

  case 109: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1128 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4378 "src/parser.tab.c"
    break;

  case 110: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1131 "src/parser.y"
                                                                                                            {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4387 "src/parser.tab.c"
    break;

  case 111: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 1138 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 4395 "src/parser.tab.c"
    break;

  case 112: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 1141 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 4403 "src/parser.tab.c"
    break;

  case 113: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 1147 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4411 "src/parser.tab.c"
    break;

  case 114: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 1153 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 4419 "src/parser.tab.c"
    break;

  case 115: /* use_statement: USE IDENT  */
#line 1159 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4425 "src/parser.tab.c"
    break;

  case 116: /* use_statement: LOAD IDENT  */
#line 1160 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4431 "src/parser.tab.c"
    break;

  case 117: /* use_statement: USE STRING  */
#line 1161 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4437 "src/parser.tab.c"
    break;

  case 118: /* use_statement: LOAD STRING  */
#line 1162 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4443 "src/parser.tab.c"
    break;

  case 119: /* use_statement: LOAD IDENT AS IDENT  */
#line 1163 "src/parser.y"
                          { (yyval.stmt) = ast_use((yyvsp[-2].text), NULL, (yyvsp[0].text)); }
#line 4449 "src/parser.tab.c"
    break;

  case 120: /* use_statement: USE IDENT IDENT STRING  */
#line 1164 "src/parser.y"
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
#line 4470 "src/parser.tab.c"
    break;

  case 121: /* use_statement: LOAD IDENT IDENT STRING  */
#line 1180 "src/parser.y"
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
#line 4491 "src/parser.tab.c"
    break;

  case 122: /* use_statement: LOAD IDENT IDENT STRING AS IDENT  */
#line 1196 "src/parser.y"
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
#line 4514 "src/parser.tab.c"
    break;

  case 123: /* modifier_signature: modifier_name  */
#line 1217 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 4520 "src/parser.tab.c"
    break;

  case 124: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 1218 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 4526 "src/parser.tab.c"
    break;

  case 125: /* modifier_context: IDENT  */
#line 1222 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4532 "src/parser.tab.c"
    break;

  case 126: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1226 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4540 "src/parser.tab.c"
    break;

  case 127: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 1229 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 4548 "src/parser.tab.c"
    break;

  case 128: /* watch_statement: WATCH IDENT LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1237 "src/parser.y"
                                                                                           {
        (yyval.stmt) = ast_watch((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4556 "src/parser.tab.c"
    break;

  case 129: /* unwatch_statement: UNWATCH expression  */
#line 1243 "src/parser.y"
                         { (yyval.stmt) = ast_unwatch((yyvsp[0].expr)); }
#line 4562 "src/parser.tab.c"
    break;

  case 130: /* watch_target_list: watch_target_path  */
#line 1247 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4568 "src/parser.tab.c"
    break;

  case 131: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 1248 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4574 "src/parser.tab.c"
    break;

  case 132: /* @2: %empty  */
#line 1268 "src/parser.y"
      { (yyval.stmt) = NULL;   /* the mid-rule carries no value; typed so bison stays quiet */
        server_head_note(ctx, (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column); }
#line 4581 "src/parser.tab.c"
    break;

  case 133: /* server_statement: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE @2 server_item_list END IDENT NEWLINE  */
#line 1270 "src/parser.y"
                                         {
        ctx->bad_block_word[0] = '\0';
        (yyval.stmt) = ast_server((yyvsp[-10].text), (yyvsp[-9].text), (yyvsp[-7].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4590 "src/parser.tab.c"
    break;

  case 134: /* @3: %empty  */
#line 1275 "src/parser.y"
      { (yyval.stmt) = NULL;   /* the mid-rule carries no value; typed so bison stays quiet */
        server_head_note(ctx, (yyvsp[-4].text), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4597 "src/parser.tab.c"
    break;

  case 135: /* server_statement: IDENT IDENT LPAREN RPAREN NEWLINE @3 server_item_list END IDENT NEWLINE  */
#line 1277 "src/parser.y"
                                         {
        ctx->bad_block_word[0] = '\0';
        (yyval.stmt) = ast_server((yyvsp[-9].text), (yyvsp[-8].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4606 "src/parser.tab.c"
    break;

  case 136: /* server_item_list: %empty  */
#line 1284 "src/parser.y"
             { (yyval.server_item_list) = ast_server_item_list_empty(); }
#line 4612 "src/parser.tab.c"
    break;

  case 137: /* server_item_list: server_item_list NEWLINE  */
#line 1285 "src/parser.y"
                               { (yyval.server_item_list) = (yyvsp[-1].server_item_list); }
#line 4618 "src/parser.tab.c"
    break;

  case 138: /* server_item_list: server_item_list server_item  */
#line 1286 "src/parser.y"
                                   { (yyval.server_item_list) = ast_server_item_list_append((yyvsp[-1].server_item_list), (yyvsp[0].server_item)); }
#line 4624 "src/parser.tab.c"
    break;

  case 139: /* server_item: IDENT server_string_list NEWLINE  */
#line 1290 "src/parser.y"
                                       {
        (yyval.server_item) = ast_server_directive((yyvsp[-2].text), (yyvsp[-1].name_list), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 4632 "src/parser.tab.c"
    break;

  case 140: /* server_item: IDENT STRING LPAREN parameter_list_opt RPAREN NEWLINE statement_list END IDENT NEWLINE  */
#line 1293 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_handler((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4640 "src/parser.tab.c"
    break;

  case 141: /* server_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1296 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_site((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 4648 "src/parser.tab.c"
    break;

  case 142: /* server_item: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1299 "src/parser.y"
                                                                           {
        (yyval.server_item) = ast_server_site((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 4656 "src/parser.tab.c"
    break;

  case 143: /* server_item: ON IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1302 "src/parser.y"
                                                     {
        (yyval.server_item) = ast_server_hook((yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 4664 "src/parser.tab.c"
    break;

  case 144: /* server_string_list: STRING  */
#line 1308 "src/parser.y"
             { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4670 "src/parser.tab.c"
    break;

  case 145: /* server_string_list: server_string_list COMMA STRING  */
#line 1309 "src/parser.y"
                                      { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4676 "src/parser.tab.c"
    break;

  case 146: /* watch_target_path: variable_name  */
#line 1313 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 4682 "src/parser.tab.c"
    break;

  case 147: /* watch_target_path: watch_target_path DOT IDENT  */
#line 1314 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 4688 "src/parser.tab.c"
    break;

  case 148: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 1318 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 4696 "src/parser.tab.c"
    break;

  case 149: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 1324 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 4702 "src/parser.tab.c"
    break;

  case 150: /* on_error_statement: ON ERROR_VALUE GOTO NEXT  */
#line 1325 "src/parser.y"
                               { (yyval.stmt) = ast_on_error_goto_next(); }
#line 4708 "src/parser.tab.c"
    break;

  case 151: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 1326 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 4714 "src/parser.tab.c"
    break;

  case 152: /* on_error_statement: ON IDENT GOTO NEXT  */
#line 1327 "src/parser.y"
                         {
        if (!warn_channel_ok(ctx, (yyvsp[-2].text), (yylsp[-2]).first_line, (yylsp[-2]).first_column)) { YYERROR; }
        free((yyvsp[-2].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_NEXT);
      }
#line 4724 "src/parser.tab.c"
    break;

  case 153: /* on_error_statement: ON IDENT GOTO IDENT  */
#line 1332 "src/parser.y"
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
#line 4742 "src/parser.tab.c"
    break;

  case 154: /* on_error_statement: ON IDENT STOP  */
#line 1345 "src/parser.y"
                    {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_STOP);
      }
#line 4752 "src/parser.tab.c"
    break;

  case 155: /* on_error_statement: ON IDENT PRINT  */
#line 1350 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_PRINT);
      }
#line 4762 "src/parser.tab.c"
    break;

  case 156: /* on_error_statement: ON IDENT IDENT  */
#line 1355 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        int mode = warn_mode_word(ctx, (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column);
        if (mode < 0) { free((yyvsp[-1].text)); free((yyvsp[0].text)); YYERROR; }
        free((yyvsp[-1].text)); free((yyvsp[0].text));
        (yyval.stmt) = ast_on_warning(mode);
      }
#line 4774 "src/parser.tab.c"
    break;

  case 157: /* error_statement: ERROR_VALUE expression  */
#line 1365 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 4780 "src/parser.tab.c"
    break;

  case 158: /* return_statement: RETURN  */
#line 1369 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 4786 "src/parser.tab.c"
    break;

  case 159: /* return_statement: RETURN expression  */
#line 1370 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 4792 "src/parser.tab.c"
    break;

  case 160: /* label_statement: variable_name COLON  */
#line 1374 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 4798 "src/parser.tab.c"
    break;

  case 161: /* goto_statement: GOTO variable_name  */
#line 1381 "src/parser.y"
                         { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 4804 "src/parser.tab.c"
    break;

  case 162: /* gosub_statement: GOSUB variable_name  */
#line 1385 "src/parser.y"
                          { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 4810 "src/parser.tab.c"
    break;

  case 163: /* break_statement: BREAK  */
#line 1394 "src/parser.y"
            { (yyval.stmt) = ast_break(NULL); }
#line 4816 "src/parser.tab.c"
    break;

  case 164: /* break_statement: BREAK IDENT  */
#line 1395 "src/parser.y"
                  { (yyval.stmt) = ast_break((yyvsp[0].text)); }
#line 4822 "src/parser.tab.c"
    break;

  case 165: /* continue_statement: CONTINUE  */
#line 1399 "src/parser.y"
               { (yyval.stmt) = ast_continue(NULL); }
#line 4828 "src/parser.tab.c"
    break;

  case 166: /* continue_statement: CONTINUE IDENT  */
#line 1400 "src/parser.y"
                     { (yyval.stmt) = ast_continue((yyvsp[0].text)); }
#line 4834 "src/parser.tab.c"
    break;

  case 167: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1404 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4843 "src/parser.tab.c"
    break;

  case 168: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1408 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 4852 "src/parser.tab.c"
    break;

  case 169: /* if_block_tail: END IF NEWLINE  */
#line 1415 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4860 "src/parser.tab.c"
    break;

  case 170: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 1418 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4868 "src/parser.tab.c"
    break;

  case 171: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1421 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4876 "src/parser.tab.c"
    break;

  case 172: /* if_block_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1430 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 4888 "src/parser.tab.c"
    break;

  case 173: /* if_inline_tail: %empty  */
#line 1440 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 4896 "src/parser.tab.c"
    break;

  case 174: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 1443 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 4904 "src/parser.tab.c"
    break;

  case 175: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1446 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 4912 "src/parser.tab.c"
    break;

  case 176: /* if_inline_tail: ELSE IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1452 "src/parser.y"
                                                                      {
        AstStmt *inner = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 4924 "src/parser.tab.c"
    break;

  case 177: /* if_inline_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1459 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 4936 "src/parser.tab.c"
    break;

  case 178: /* inline_statement: assignment  */
#line 1469 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4942 "src/parser.tab.c"
    break;

  case 179: /* inline_statement: print_statement  */
#line 1470 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4948 "src/parser.tab.c"
    break;

  case 180: /* inline_statement: call_statement  */
#line 1471 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4954 "src/parser.tab.c"
    break;

  case 181: /* inline_statement: use_statement  */
#line 1472 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4960 "src/parser.tab.c"
    break;

  case 182: /* inline_statement: on_error_statement  */
#line 1473 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4966 "src/parser.tab.c"
    break;

  case 183: /* inline_statement: error_statement  */
#line 1474 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4972 "src/parser.tab.c"
    break;

  case 184: /* inline_statement: return_statement  */
#line 1475 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4978 "src/parser.tab.c"
    break;

  case 185: /* inline_statement: goto_statement  */
#line 1476 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4984 "src/parser.tab.c"
    break;

  case 186: /* inline_statement: gosub_statement  */
#line 1477 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4990 "src/parser.tab.c"
    break;

  case 187: /* inline_statement: break_statement  */
#line 1478 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4996 "src/parser.tab.c"
    break;

  case 188: /* inline_statement: continue_statement  */
#line 1479 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5002 "src/parser.tab.c"
    break;

  case 189: /* expression: or_expression  */
#line 1483 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 5008 "src/parser.tab.c"
    break;

  case 190: /* or_expression: and_expression  */
#line 1487 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5014 "src/parser.tab.c"
    break;

  case 191: /* or_expression: or_expression OR and_expression  */
#line 1488 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5020 "src/parser.tab.c"
    break;

  case 192: /* and_expression: not_expression  */
#line 1492 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5026 "src/parser.tab.c"
    break;

  case 193: /* and_expression: and_expression AND not_expression  */
#line 1493 "src/parser.y"
                                        { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5032 "src/parser.tab.c"
    break;

  case 194: /* not_expression: comparison_expression  */
#line 1515 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 5038 "src/parser.tab.c"
    break;

  case 195: /* not_expression: NOT not_expression  */
#line 1516 "src/parser.y"
                         { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5044 "src/parser.tab.c"
    break;

  case 196: /* comparison_expression: set_expression  */
#line 1520 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5050 "src/parser.tab.c"
    break;

  case 197: /* comparison_expression: set_expression comparison_operator set_expression  */
#line 1521 "src/parser.y"
                                                        { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5056 "src/parser.tab.c"
    break;

  case 198: /* comparison_expression: set_expression comparison_lens comparison_operator set_expression  */
#line 1522 "src/parser.y"
                                                                        {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 5064 "src/parser.tab.c"
    break;

  case 199: /* set_expression: additive_expression  */
#line 1528 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 5070 "src/parser.tab.c"
    break;

  case 200: /* set_expression: set_expression EXCLUDING additive_expression  */
#line 1529 "src/parser.y"
                                                   { (yyval.expr) = expr_at(ast_binary(copy_const("excluding"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5076 "src/parser.tab.c"
    break;

  case 201: /* set_expression: set_expression INTERSECTING additive_expression  */
#line 1530 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("intersecting"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5082 "src/parser.tab.c"
    break;

  case 202: /* additive_expression: multiplicative_expression  */
#line 1534 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 5088 "src/parser.tab.c"
    break;

  case 203: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 1535 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5094 "src/parser.tab.c"
    break;

  case 204: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 1536 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5100 "src/parser.tab.c"
    break;

  case 205: /* multiplicative_expression: unary_expression  */
#line 1540 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 5106 "src/parser.tab.c"
    break;

  case 206: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 1541 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5112 "src/parser.tab.c"
    break;

  case 207: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 1542 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5118 "src/parser.tab.c"
    break;

  case 208: /* unary_expression: postfix_expression  */
#line 1546 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 5124 "src/parser.tab.c"
    break;

  case 209: /* unary_expression: MINUS unary_expression  */
#line 1547 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5130 "src/parser.tab.c"
    break;

  case 210: /* unary_expression: MODIFIER_PREFIX unary_expression  */
#line 1554 "src/parser.y"
                                       {
        (yyval.expr) = expr_at(ast_modifier_apply(parse_modifier_use((yyvsp[-1].text)), (yyvsp[0].expr)),
                     (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 5139 "src/parser.tab.c"
    break;

  case 211: /* unary_expression: NEW postfix_expression  */
#line 1558 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5145 "src/parser.tab.c"
    break;

  case 212: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 1559 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 5151 "src/parser.tab.c"
    break;

  case 213: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 1560 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5157 "src/parser.tab.c"
    break;

  case 214: /* postfix_expression: primary  */
#line 1564 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 5163 "src/parser.tab.c"
    break;

  case 215: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 1565 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5169 "src/parser.tab.c"
    break;

  case 216: /* postfix_expression: postfix_expression DOT dot_field_name  */
#line 1566 "src/parser.y"
                                            { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5175 "src/parser.tab.c"
    break;

  case 217: /* postfix_expression: postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1567 "src/parser.y"
                                                                   {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        (yyval.expr) = expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5185 "src/parser.tab.c"
    break;

  case 218: /* postfix_expression: postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1572 "src/parser.y"
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
#line 5201 "src/parser.tab.c"
    break;

  case 219: /* comparison_operator: OP_EQ  */
#line 1586 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 5207 "src/parser.tab.c"
    break;

  case 220: /* comparison_operator: OP_NE  */
#line 1587 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 5213 "src/parser.tab.c"
    break;

  case 221: /* comparison_operator: OP_GT  */
#line 1588 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 5219 "src/parser.tab.c"
    break;

  case 222: /* comparison_operator: OP_LT  */
#line 1589 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 5225 "src/parser.tab.c"
    break;

  case 223: /* comparison_operator: OP_GE  */
#line 1590 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 5231 "src/parser.tab.c"
    break;

  case 224: /* comparison_operator: OP_LE  */
#line 1591 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 5237 "src/parser.tab.c"
    break;

  case 225: /* comparison_operator: OP_NGT  */
#line 1592 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 5243 "src/parser.tab.c"
    break;

  case 226: /* comparison_operator: OP_NLT  */
#line 1593 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 5249 "src/parser.tab.c"
    break;

  case 227: /* comparison_operator: OP_NGE  */
#line 1594 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 5255 "src/parser.tab.c"
    break;

  case 228: /* comparison_operator: OP_NLE  */
#line 1595 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 5261 "src/parser.tab.c"
    break;

  case 229: /* primary: NUMBER  */
#line 1599 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5267 "src/parser.tab.c"
    break;

  case 230: /* primary: WATCHERS LPAREN RPAREN  */
#line 1600 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_call(copy_const("watchers"), ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5273 "src/parser.tab.c"
    break;

  case 231: /* primary: duration_terms  */
#line 1601 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5279 "src/parser.tab.c"
    break;

  case 232: /* primary: STRING  */
#line 1602 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5285 "src/parser.tab.c"
    break;

  case 233: /* primary: variable_name ident_suffix  */
#line 1603 "src/parser.y"
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
#line 5307 "src/parser.tab.c"
    break;

  case 234: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1620 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 5318 "src/parser.tab.c"
    break;

  case 235: /* primary: ERROR_VALUE  */
#line 1626 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5324 "src/parser.tab.c"
    break;

  case 236: /* primary: TRUE  */
#line 1627 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5330 "src/parser.tab.c"
    break;

  case 237: /* primary: FALSE  */
#line 1628 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5336 "src/parser.tab.c"
    break;

  case 238: /* primary: NOTHING  */
#line 1629 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5342 "src/parser.tab.c"
    break;

  case 239: /* primary: UNKNOWN_VALUE  */
#line 1630 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5348 "src/parser.tab.c"
    break;

  case 240: /* primary: LPAREN expression RPAREN  */
#line 1631 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 5354 "src/parser.tab.c"
    break;

  case 241: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 1632 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5360 "src/parser.tab.c"
    break;

  case 242: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 1633 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5366 "src/parser.tab.c"
    break;

  case 243: /* primary: record_literal  */
#line 1634 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5372 "src/parser.tab.c"
    break;

  case 244: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 1638 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5378 "src/parser.tab.c"
    break;

  case 245: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 1639 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5384 "src/parser.tab.c"
    break;

  case 246: /* ident_suffix: %empty  */
#line 1643 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 5394 "src/parser.tab.c"
    break;

  case 247: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 1648 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5404 "src/parser.tab.c"
    break;

  case 248: /* ident_suffix: DOT dot_field_name ident_dot_suffix  */
#line 1653 "src/parser.y"
                                          {
        /* dot_field_name, not IDENT: a keyword is a legal FIELD name after a
         * dot, because nothing but a name can appear there. */
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 5415 "src/parser.tab.c"
    break;

  case 249: /* ident_suffix: DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1659 "src/parser.y"
                                                          {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        (yyval.ident_suffix).kind = IDENT_SUFFIX_METHOD;
        (yyval.ident_suffix).name = (yyvsp[-3].text);
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5429 "src/parser.tab.c"
    break;

  case 250: /* ident_dot_suffix: %empty  */
#line 1671 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 5439 "src/parser.tab.c"
    break;

  case 251: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 1676 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5449 "src/parser.tab.c"
    break;

  case 252: /* duration_terms: NUMBER IDENT  */
#line 1684 "src/parser.y"
                   {
        AstDuration duration = {0};
        char *bad = NULL;
        (yyval.duration) = duration_add_unit(duration, (yyvsp[-1].number), (yyvsp[0].text), &bad);
        if (bad) {
            duration_unit_error(ctx, bad, (yylsp[0]).first_line, (yylsp[0]).first_column,
                                (yylsp[0]).last_line, (yylsp[0]).last_column);
            YYERROR;
        }
      }
#line 5464 "src/parser.tab.c"
    break;

  case 253: /* duration_terms: duration_terms NUMBER IDENT  */
#line 1694 "src/parser.y"
                                  {
        char *bad = NULL;
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].number), (yyvsp[0].text), &bad);
        if (bad) {
            duration_unit_error(ctx, bad, (yylsp[0]).first_line, (yylsp[0]).first_column,
                                (yylsp[0]).last_line, (yylsp[0]).last_column);
            YYERROR;
        }
      }
#line 5478 "src/parser.tab.c"
    break;

  case 254: /* argument_list_opt: %empty  */
#line 1706 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 5484 "src/parser.tab.c"
    break;

  case 255: /* argument_list_opt: argument_list  */
#line 1707 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 5490 "src/parser.tab.c"
    break;

  case 256: /* argument_list: expression  */
#line 1711 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 5496 "src/parser.tab.c"
    break;

  case 257: /* argument_list: argument_list COMMA expression  */
#line 1712 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 5502 "src/parser.tab.c"
    break;

  case 258: /* array_argument_list: expression  */
#line 1716 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 5508 "src/parser.tab.c"
    break;

  case 259: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 1717 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 5514 "src/parser.tab.c"
    break;

  case 260: /* parameter_list_opt: %empty  */
#line 1721 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 5520 "src/parser.tab.c"
    break;

  case 261: /* parameter_list_opt: parameter_list  */
#line 1722 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 5526 "src/parser.tab.c"
    break;

  case 262: /* parameter_default: NUMBER  */
#line 1736 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5532 "src/parser.tab.c"
    break;

  case 263: /* parameter_default: MINUS NUMBER  */
#line 1737 "src/parser.y"
                   { (yyval.expr) = expr_at(ast_number(-(yyvsp[0].number)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5538 "src/parser.tab.c"
    break;

  case 264: /* parameter_default: PLUS NUMBER  */
#line 1738 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5544 "src/parser.tab.c"
    break;

  case 265: /* parameter_default: STRING  */
#line 1739 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5550 "src/parser.tab.c"
    break;

  case 266: /* parameter_default: TRUE  */
#line 1740 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5556 "src/parser.tab.c"
    break;

  case 267: /* parameter_default: FALSE  */
#line 1741 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5562 "src/parser.tab.c"
    break;

  case 268: /* parameter_default: NOTHING  */
#line 1742 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5568 "src/parser.tab.c"
    break;

  case 269: /* parameter_default: UNKNOWN_VALUE  */
#line 1743 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5574 "src/parser.tab.c"
    break;

  case 270: /* parameter_list: IDENT  */
#line 1747 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5580 "src/parser.tab.c"
    break;

  case 271: /* parameter_list: IDENT OP_EQ parameter_default  */
#line 1748 "src/parser.y"
                                    {
        (yyval.name_list) = ast_name_list_append_default(ast_name_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 5588 "src/parser.tab.c"
    break;

  case 272: /* parameter_list: parameter_list COMMA IDENT  */
#line 1751 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5594 "src/parser.tab.c"
    break;

  case 273: /* parameter_list: parameter_list COMMA IDENT OP_EQ parameter_default  */
#line 1752 "src/parser.y"
                                                         {
        (yyval.name_list) = ast_name_list_append_default((yyvsp[-4].name_list), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 5602 "src/parser.tab.c"
    break;

  case 274: /* field_name: dot_field_name  */
#line 1767 "src/parser.y"
                     { (yyval.text) = (yyvsp[0].text); }
#line 5608 "src/parser.tab.c"
    break;

  case 275: /* field_name: STRING  */
#line 1774 "src/parser.y"
             { (yyval.text) = (yyvsp[0].text); }
#line 5614 "src/parser.tab.c"
    break;

  case 276: /* dot_field_name: IDENT  */
#line 1783 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 5620 "src/parser.tab.c"
    break;

  case 277: /* dot_field_name: AS  */
#line 1784 "src/parser.y"
                     { (yyval.text) = kw_name("as"); }
#line 5626 "src/parser.tab.c"
    break;

  case 278: /* dot_field_name: NEXT  */
#line 1785 "src/parser.y"
                     { (yyval.text) = kw_name("next"); }
#line 5632 "src/parser.tab.c"
    break;

  case 279: /* dot_field_name: STOP  */
#line 1786 "src/parser.y"
                     { (yyval.text) = kw_name("stop"); }
#line 5638 "src/parser.tab.c"
    break;

  case 280: /* dot_field_name: ERROR_VALUE  */
#line 1787 "src/parser.y"
                     { (yyval.text) = kw_name("error"); }
#line 5644 "src/parser.tab.c"
    break;

  case 281: /* dot_field_name: END  */
#line 1788 "src/parser.y"
                     { (yyval.text) = kw_name("end"); }
#line 5650 "src/parser.tab.c"
    break;

  case 282: /* dot_field_name: TO  */
#line 1789 "src/parser.y"
                     { (yyval.text) = kw_name("to"); }
#line 5656 "src/parser.tab.c"
    break;

  case 283: /* dot_field_name: IN  */
#line 1790 "src/parser.y"
                     { (yyval.text) = kw_name("in"); }
#line 5662 "src/parser.tab.c"
    break;

  case 284: /* dot_field_name: ON  */
#line 1791 "src/parser.y"
                     { (yyval.text) = kw_name("on"); }
#line 5668 "src/parser.tab.c"
    break;

  case 285: /* dot_field_name: NEW  */
#line 1792 "src/parser.y"
                     { (yyval.text) = kw_name("new"); }
#line 5674 "src/parser.tab.c"
    break;

  case 286: /* dot_field_name: EACH  */
#line 1793 "src/parser.y"
                     { (yyval.text) = kw_name("each"); }
#line 5680 "src/parser.tab.c"
    break;

  case 287: /* dot_field_name: WITH  */
#line 1794 "src/parser.y"
                     { (yyval.text) = kw_name("with"); }
#line 5686 "src/parser.tab.c"
    break;

  case 288: /* dot_field_name: WITHOUT  */
#line 1795 "src/parser.y"
                     { (yyval.text) = kw_name("without"); }
#line 5692 "src/parser.tab.c"
    break;

  case 289: /* dot_field_name: THEN  */
#line 1796 "src/parser.y"
                     { (yyval.text) = kw_name("then"); }
#line 5698 "src/parser.tab.c"
    break;

  case 290: /* dot_field_name: ELSE  */
#line 1797 "src/parser.y"
                     { (yyval.text) = kw_name("else"); }
#line 5704 "src/parser.tab.c"
    break;

  case 291: /* dot_field_name: FOR  */
#line 1798 "src/parser.y"
                     { (yyval.text) = kw_name("for"); }
#line 5710 "src/parser.tab.c"
    break;

  case 292: /* dot_field_name: IF  */
#line 1799 "src/parser.y"
                     { (yyval.text) = kw_name("if"); }
#line 5716 "src/parser.tab.c"
    break;

  case 293: /* dot_field_name: WHILE  */
#line 1800 "src/parser.y"
                     { (yyval.text) = kw_name("while"); }
#line 5722 "src/parser.tab.c"
    break;

  case 294: /* dot_field_name: DO  */
#line 1801 "src/parser.y"
                     { (yyval.text) = kw_name("do"); }
#line 5728 "src/parser.tab.c"
    break;

  case 295: /* dot_field_name: UNTIL  */
#line 1802 "src/parser.y"
                     { (yyval.text) = kw_name("until"); }
#line 5734 "src/parser.tab.c"
    break;

  case 296: /* dot_field_name: PRINT  */
#line 1803 "src/parser.y"
                     { (yyval.text) = kw_name("print"); }
#line 5740 "src/parser.tab.c"
    break;

  case 297: /* dot_field_name: RETURN  */
#line 1804 "src/parser.y"
                     { (yyval.text) = kw_name("return"); }
#line 5746 "src/parser.tab.c"
    break;

  case 298: /* dot_field_name: LOAD  */
#line 1805 "src/parser.y"
                     { (yyval.text) = kw_name("load"); }
#line 5752 "src/parser.tab.c"
    break;

  case 299: /* dot_field_name: USE  */
#line 1806 "src/parser.y"
                     { (yyval.text) = kw_name("use"); }
#line 5758 "src/parser.tab.c"
    break;

  case 300: /* dot_field_name: NOT  */
#line 1807 "src/parser.y"
                     { (yyval.text) = kw_name("not"); }
#line 5764 "src/parser.tab.c"
    break;

  case 301: /* dot_field_name: AND  */
#line 1808 "src/parser.y"
                     { (yyval.text) = kw_name("and"); }
#line 5770 "src/parser.tab.c"
    break;

  case 302: /* dot_field_name: OR  */
#line 1809 "src/parser.y"
                     { (yyval.text) = kw_name("or"); }
#line 5776 "src/parser.tab.c"
    break;

  case 303: /* dot_field_name: TRUE  */
#line 1810 "src/parser.y"
                     { (yyval.text) = kw_name("true"); }
#line 5782 "src/parser.tab.c"
    break;

  case 304: /* dot_field_name: FALSE  */
#line 1811 "src/parser.y"
                     { (yyval.text) = kw_name("false"); }
#line 5788 "src/parser.tab.c"
    break;

  case 305: /* dot_field_name: NOTHING  */
#line 1812 "src/parser.y"
                     { (yyval.text) = kw_name("nothing"); }
#line 5794 "src/parser.tab.c"
    break;

  case 306: /* dot_field_name: BREAK  */
#line 1813 "src/parser.y"
                     { (yyval.text) = kw_name("break"); }
#line 5800 "src/parser.tab.c"
    break;

  case 307: /* dot_field_name: CONTINUE  */
#line 1814 "src/parser.y"
                     { (yyval.text) = kw_name("continue"); }
#line 5806 "src/parser.tab.c"
    break;

  case 308: /* dot_field_name: GOTO  */
#line 1815 "src/parser.y"
                     { (yyval.text) = kw_name("goto"); }
#line 5812 "src/parser.tab.c"
    break;

  case 309: /* dot_field_name: GOSUB  */
#line 1816 "src/parser.y"
                     { (yyval.text) = kw_name("gosub"); }
#line 5818 "src/parser.tab.c"
    break;

  case 310: /* dot_field_name: SPAWN  */
#line 1817 "src/parser.y"
                     { (yyval.text) = kw_name("spawn"); }
#line 5824 "src/parser.tab.c"
    break;

  case 311: /* dot_field_name: EXPORT  */
#line 1818 "src/parser.y"
                     { (yyval.text) = kw_name("export"); }
#line 5830 "src/parser.tab.c"
    break;

  case 312: /* dot_field_name: LIBRARY  */
#line 1819 "src/parser.y"
                     { (yyval.text) = kw_name("library"); }
#line 5836 "src/parser.tab.c"
    break;

  case 313: /* dot_field_name: FUNCTION  */
#line 1820 "src/parser.y"
                     { (yyval.text) = kw_name("function"); }
#line 5842 "src/parser.tab.c"
    break;

  case 314: /* dot_field_name: MODIFIER  */
#line 1821 "src/parser.y"
                     { (yyval.text) = kw_name("modifier"); }
#line 5848 "src/parser.tab.c"
    break;

  case 315: /* dot_field_name: PROGRAM  */
#line 1822 "src/parser.y"
                     { (yyval.text) = kw_name("program"); }
#line 5854 "src/parser.tab.c"
    break;

  case 316: /* dot_field_name: WATCH  */
#line 1823 "src/parser.y"
                     { (yyval.text) = kw_name("watch"); }
#line 5860 "src/parser.tab.c"
    break;

  case 317: /* dot_field_name: WATCHERS  */
#line 1824 "src/parser.y"
                     { (yyval.text) = kw_name("watchers"); }
#line 5866 "src/parser.tab.c"
    break;

  case 318: /* dot_field_name: CONSIDER  */
#line 1825 "src/parser.y"
                     { (yyval.text) = kw_name("consider"); }
#line 5872 "src/parser.tab.c"
    break;

  case 319: /* dot_field_name: STEP  */
#line 1826 "src/parser.y"
                     { (yyval.text) = kw_name("step"); }
#line 5878 "src/parser.tab.c"
    break;

  case 320: /* dot_field_name: UNWATCH  */
#line 1827 "src/parser.y"
                     { (yyval.text) = kw_name("unwatch"); }
#line 5884 "src/parser.tab.c"
    break;

  case 321: /* dot_field_name: UNKNOWN_VALUE  */
#line 1828 "src/parser.y"
                     { (yyval.text) = kw_name("unknown"); }
#line 5890 "src/parser.tab.c"
    break;

  case 322: /* dot_field_name: DIM  */
#line 1829 "src/parser.y"
                     { (yyval.text) = kw_name("dim"); }
#line 5896 "src/parser.tab.c"
    break;

  case 323: /* record_field_list: field_name OP_EQ expression  */
#line 1833 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5902 "src/parser.tab.c"
    break;

  case 324: /* record_field_list: field_name COLON expression  */
#line 1834 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5908 "src/parser.tab.c"
    break;

  case 325: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1835 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5914 "src/parser.tab.c"
    break;

  case 326: /* record_field_list: record_field_list COMMA optional_newlines field_name OP_EQ expression  */
#line 1836 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5920 "src/parser.tab.c"
    break;

  case 327: /* record_field_list: record_field_list COMMA optional_newlines field_name COLON expression  */
#line 1837 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 5926 "src/parser.tab.c"
    break;

  case 328: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1838 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 5932 "src/parser.tab.c"
    break;

  case 329: /* field_policy: IDENT  */
#line 1846 "src/parser.y"
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
#line 5964 "src/parser.tab.c"
    break;

  case 330: /* field_policy: IDENT expression  */
#line 1873 "src/parser.y"
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
#line 5985 "src/parser.tab.c"
    break;


#line 5989 "src/parser.tab.c"

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

#line 1896 "src/parser.y"


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
