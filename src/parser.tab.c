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



#line 704 "src/parser.tab.c"

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
  YYSYMBOL_type_lock = 100,                /* type_lock  */
  YYSYMBOL_compound_op = 101,              /* compound_op  */
  YYSYMBOL_lvalue = 102,                   /* lvalue  */
  YYSYMBOL_variable_name = 103,            /* variable_name  */
  YYSYMBOL_comparison_lens = 104,          /* comparison_lens  */
  YYSYMBOL_105_1 = 105,                    /* $@1  */
  YYSYMBOL_modifier_name = 106,            /* modifier_name  */
  YYSYMBOL_modifier_word = 107,            /* modifier_word  */
  YYSYMBOL_print_statement = 108,          /* print_statement  */
  YYSYMBOL_call_statement = 109,           /* call_statement  */
  YYSYMBOL_with_lock_statement = 110,      /* with_lock_statement  */
  YYSYMBOL_for_end = 111,                  /* for_end  */
  YYSYMBOL_for_each_statement = 112,       /* for_each_statement  */
  YYSYMBOL_do_loop_statement = 113,        /* do_loop_statement  */
  YYSYMBOL_while_statement = 114,          /* while_statement  */
  YYSYMBOL_consider_statement = 115,       /* consider_statement  */
  YYSYMBOL_consider_branch_list = 116,     /* consider_branch_list  */
  YYSYMBOL_consider_else_opt = 117,        /* consider_else_opt  */
  YYSYMBOL_consider_statement_list = 118,  /* consider_statement_list  */
  YYSYMBOL_consider_body_statement = 119,  /* consider_body_statement  */
  YYSYMBOL_function_statement = 120,       /* function_statement  */
  YYSYMBOL_modifier_statement = 121,       /* modifier_statement  */
  YYSYMBOL_program_statement = 122,        /* program_statement  */
  YYSYMBOL_library_statement = 123,        /* library_statement  */
  YYSYMBOL_use_statement = 124,            /* use_statement  */
  YYSYMBOL_modifier_signature = 125,       /* modifier_signature  */
  YYSYMBOL_modifier_context = 126,         /* modifier_context  */
  YYSYMBOL_watch_statement = 127,          /* watch_statement  */
  YYSYMBOL_unwatch_statement = 128,        /* unwatch_statement  */
  YYSYMBOL_watch_target_list = 129,        /* watch_target_list  */
  YYSYMBOL_server_statement = 130,         /* server_statement  */
  YYSYMBOL_131_2 = 131,                    /* @2  */
  YYSYMBOL_132_3 = 132,                    /* @3  */
  YYSYMBOL_gsql_block = 133,               /* gsql_block  */
  YYSYMBOL_window_block = 134,             /* window_block  */
  YYSYMBOL_window_item_list = 135,         /* window_item_list  */
  YYSYMBOL_window_item = 136,              /* window_item  */
  YYSYMBOL_server_item_list = 137,         /* server_item_list  */
  YYSYMBOL_server_item = 138,              /* server_item  */
  YYSYMBOL_server_string_list = 139,       /* server_string_list  */
  YYSYMBOL_watch_target_path = 140,        /* watch_target_path  */
  YYSYMBOL_without_watchers_statement = 141, /* without_watchers_statement  */
  YYSYMBOL_on_error_statement = 142,       /* on_error_statement  */
  YYSYMBOL_error_statement = 143,          /* error_statement  */
  YYSYMBOL_return_statement = 144,         /* return_statement  */
  YYSYMBOL_label_statement = 145,          /* label_statement  */
  YYSYMBOL_goto_statement = 146,           /* goto_statement  */
  YYSYMBOL_gosub_statement = 147,          /* gosub_statement  */
  YYSYMBOL_break_statement = 148,          /* break_statement  */
  YYSYMBOL_continue_statement = 149,       /* continue_statement  */
  YYSYMBOL_if_statement = 150,             /* if_statement  */
  YYSYMBOL_if_block_tail = 151,            /* if_block_tail  */
  YYSYMBOL_if_inline_tail = 152,           /* if_inline_tail  */
  YYSYMBOL_inline_statement = 153,         /* inline_statement  */
  YYSYMBOL_expression = 154,               /* expression  */
  YYSYMBOL_or_expression = 155,            /* or_expression  */
  YYSYMBOL_and_expression = 156,           /* and_expression  */
  YYSYMBOL_not_expression = 157,           /* not_expression  */
  YYSYMBOL_comparison_expression = 158,    /* comparison_expression  */
  YYSYMBOL_set_expression = 159,           /* set_expression  */
  YYSYMBOL_additive_expression = 160,      /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 161, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 162,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 163,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 164,      /* comparison_operator  */
  YYSYMBOL_primary = 165,                  /* primary  */
  YYSYMBOL_record_literal = 166,           /* record_literal  */
  YYSYMBOL_ident_suffix = 167,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 168,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 169,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 170,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 171,            /* argument_list  */
  YYSYMBOL_array_argument_list = 172,      /* array_argument_list  */
  YYSYMBOL_parameter_list_opt = 173,       /* parameter_list_opt  */
  YYSYMBOL_parameter_default = 174,        /* parameter_default  */
  YYSYMBOL_parameter_list = 175,           /* parameter_list  */
  YYSYMBOL_field_name = 176,               /* field_name  */
  YYSYMBOL_dot_field_name = 177,           /* dot_field_name  */
  YYSYMBOL_record_field_list = 178,        /* record_field_list  */
  YYSYMBOL_field_policy = 179,             /* field_policy  */
  YYSYMBOL_optional_newlines = 180         /* optional_newlines  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;



/* Unqualified %code blocks.  */
#line 719 "src/parser.y"

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

#line 1008 "src/parser.tab.c"

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
#define YYLAST   3355

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  95
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  86
/* YYNRULES -- Number of rules.  */
#define YYNRULES  365
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  849

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
       0,   891,   891,   895,   896,   897,   901,   902,   903,   904,
     905,   906,   907,   908,   909,   910,   911,   912,   913,   914,
     915,   916,   917,   918,   919,   920,   921,   922,   923,   924,
     925,   926,   927,   928,   934,   945,   950,   951,   967,   974,
     988,   989,   993,   994,   995,   996,  1000,  1001,  1002,  1006,
    1007,  1008,  1019,  1019,  1027,  1031,  1032,  1036,  1037,  1038,
    1039,  1043,  1049,  1053,  1054,  1060,  1065,  1074,  1087,  1116,
    1117,  1118,  1122,  1126,  1142,  1147,  1155,  1159,  1180,  1186,
    1192,  1198,  1201,  1207,  1208,  1212,  1213,  1214,  1218,  1219,
    1220,  1221,  1222,  1223,  1224,  1225,  1226,  1227,  1228,  1229,
    1230,  1231,  1232,  1233,  1234,  1235,  1236,  1237,  1238,  1239,
    1240,  1241,  1242,  1243,  1249,  1260,  1263,  1270,  1279,  1291,
    1294,  1300,  1306,  1312,  1313,  1314,  1315,  1316,  1317,  1333,
    1349,  1370,  1371,  1375,  1379,  1382,  1390,  1396,  1400,  1401,
    1421,  1420,  1428,  1427,  1450,  1453,  1456,  1463,  1467,  1477,
    1485,  1486,  1487,  1492,  1497,  1501,  1506,  1511,  1525,  1540,
    1545,  1556,  1571,  1576,  1582,  1583,  1584,  1588,  1591,  1594,
    1597,  1600,  1606,  1607,  1611,  1612,  1616,  1622,  1623,  1624,
    1625,  1630,  1643,  1648,  1653,  1663,  1667,  1668,  1672,  1679,
    1683,  1692,  1693,  1697,  1698,  1702,  1706,  1713,  1716,  1719,
    1728,  1738,  1741,  1744,  1750,  1757,  1767,  1768,  1769,  1770,
    1771,  1772,  1773,  1774,  1775,  1776,  1777,  1781,  1785,  1786,
    1790,  1791,  1813,  1814,  1818,  1819,  1820,  1826,  1827,  1828,
    1832,  1833,  1834,  1838,  1839,  1840,  1844,  1845,  1852,  1856,
    1857,  1858,  1862,  1863,  1864,  1865,  1870,  1884,  1885,  1886,
    1887,  1888,  1889,  1890,  1891,  1892,  1893,  1897,  1915,  1916,
    1917,  1918,  1919,  1936,  1942,  1943,  1944,  1945,  1946,  1947,
    1948,  1949,  1950,  1954,  1955,  1959,  1964,  1969,  1975,  1987,
    1992,  2000,  2010,  2022,  2023,  2027,  2028,  2032,  2033,  2037,
    2038,  2052,  2056,  2060,  2061,  2062,  2063,  2064,  2065,  2069,
    2070,  2073,  2077,  2084,  2091,  2098,  2105,  2120,  2127,  2136,
    2137,  2138,  2139,  2140,  2141,  2142,  2143,  2144,  2145,  2146,
    2147,  2148,  2149,  2150,  2151,  2152,  2153,  2154,  2155,  2156,
    2157,  2158,  2159,  2160,  2161,  2162,  2163,  2164,  2165,  2166,
    2167,  2168,  2169,  2170,  2171,  2172,  2173,  2174,  2175,  2176,
    2177,  2178,  2179,  2180,  2181,  2182,  2186,  2187,  2188,  2189,
    2190,  2191,  2199,  2226,  2245,  2246
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

#define YYPACT_NINF (-565)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -565,    79,  1066,  -565,    51,     2,   100,   140,   152,  -565,
    2632,  -565,  2593,   195,    54,    37,  2632,  2632,   214,   228,
     226,  2632,    87,    87,   305,  2632,    82,    67,  -565,   599,
      72,   232,   260,    61,   299,   219,  -565,  -565,    97,   447,
     202,   222,   230,  -565,  -565,  -565,  -565,  -565,  -565,  -565,
    -565,  -565,   281,  -565,   286,  -565,  -565,  -565,  -565,   295,
     309,   324,   326,   327,   328,   331,   333,  -565,   342,  2632,
    2632,   343,   345,   376,   432,  -565,  -565,   357,  2693,  -565,
    -565,  -565,  -565,  -565,  2632,  2678,   437,   361,  -565,  2693,
    2632,  -565,  -565,    96,   422,   412,   414,  -565,  -565,   577,
     249,   294,  -565,     7,  -565,  -565,   448,   388,  -565,   370,
     155,   450,  -565,   365,   366,  -565,  -565,   384,   386,  -565,
    -565,  -565,   387,    87,  -565,   154,   378,  -565,   382,   287,
     217,   470,  -565,  -565,  -565,  -565,  -565,   310,  -565,   435,
     394,   389,   192,  -565,   474,  -565,    72,  -565,  -565,  -565,
    -565,  -565,  -565,  2632,  2632,  -565,  3157,  2632,   242,   475,
    -565,  -565,  -565,  -565,  -565,  -565,  -565,  -565,  -565,  -565,
    -565,  -565,  2767,  -565,   397,   395,   399,  2832,  2897,   481,
    -565,  2632,  -565,  -565,    41,   404,   405,  -565,   406,   706,
     806,  2632,  3222,  -565,  2504,  2632,  2632,  2693,  2693,  -565,
    -565,  -565,  -565,  -565,  -565,  -565,  -565,  -565,  -565,  2516,
    2693,  2693,  2693,  2693,  2693,  2632,  3287,   490,  2632,  2632,
    2632,  2632,   491,    28,   936,  -565,   476,   496,   496,    87,
     124,    87,  -565,   497,  -565,  -565,  -565,    71,  -565,    93,
    -565,   419,   496,  -565,   499,   496,  -565,   502,   500,   504,
     471,  -565,   424,   507,   441,   442,  -565,  -565,  -565,  -565,
    -565,  -565,  -565,  -565,  -565,  -565,  -565,  -565,  -565,  -565,
    -565,  -565,  -565,  -565,  -565,  -565,  -565,  -565,  -565,  -565,
    -565,  -565,  -565,  -565,  -565,  -565,  -565,  -565,  -565,  -565,
    -565,  -565,  -565,  -565,  -565,  -565,  -565,  -565,  -565,  -565,
    -565,  -565,  -565,  -565,  2632,  2632,   443,   449,   445,  -565,
     429,   114,  -565,   158,  -565,  2632,  -565,   440,   179,   444,
     182,   446,   452,   455,  2632,  -565,  -565,  -565,  -565,  -565,
     454,  -565,   457,   460,  -565,   456,   464,   465,  -565,  -565,
     463,  -565,  -565,  -565,  -565,  -565,  -565,  -565,  -565,  -565,
    -565,   459,   414,  -565,   249,   249,  2693,   360,   294,   294,
    -565,  -565,   472,   482,   486,  -565,  -565,  -565,   473,   479,
     492,   527,  2632,   573,  2632,  1131,  2632,   318,   125,   498,
     501,   508,   221,   489,   378,  1196,  -565,  1261,  -565,  -565,
    -565,  -565,  2632,   509,  -565,   495,   511,  1326,   567,  -565,
    -565,   499,  -565,   512,  2632,  2632,  -565,  -565,   584,  2632,
     592,  -565,  2632,  2632,   510,  -565,  -565,  -565,    36,  -565,
     514,   586,  -565,  -565,   525,  -565,   264,   283,  -565,  2632,
    2632,  -565,  1001,   475,   585,   360,  -565,  2632,  2632,   520,
    -565,  2632,  2632,   521,   570,   528,   569,   594,  2632,   529,
     593,   637,   475,   308,   629,   311,   543,  -565,   581,   582,
     557,  -565,  -565,   553,   591,   654,   571,  -565,   576,   579,
     587,  -565,  2632,   588,  -565,  -565,  -565,  -565,   871,    60,
     661,  -565,  -565,   639,  -565,   722,  -565,  -565,   589,   590,
     227,   655,  -565,  2485,  -565,   596,   597,  -565,  1391,    21,
     598,  -565,  2632,  -565,   600,   601,   652,  -565,   603,  -565,
    -565,  -565,  -565,  -565,  -565,   675,   680,  -565,   616,   475,
    -565,   126,   475,  -565,  -565,  1456,   604,   605,  -565,  1521,
    -565,   607,  -565,  -565,  -565,  -565,  -565,  -565,   609,    66,
    -565,   617,   136,    55,   683,   684,  -565,  -565,   686,    78,
     697,  -565,  -565,  -565,  2632,  -565,   611,   612,  2632,  -565,
     613,  -565,  -565,  1586,   668,    63,  -565,  2632,  -565,  -565,
    1391,   621,  -565,  -565,   627,  2431,  -565,  -565,  -565,   637,
     628,  1651,   637,   475,   630,  1716,  1781,   667,  -565,  -565,
     665,  1846,  -565,  1911,  2632,   385,   720,   728,  -565,  -565,
      91,   592,  2632,  2632,   205,   643,   731,  2962,  -565,   649,
     647,   656,   739,   657,   726,  1976,  -565,  -565,   727,  2041,
    -565,   709,   671,  -565,   672,   674,  1391,  1391,  -565,  -565,
    2431,  -565,  -565,  -565,   676,   678,   679,  -565,  -565,  -565,
    -565,  -565,  -565,  -565,  -565,  -565,  -565,   681,  -565,   682,
    -565,  -565,   685,   687,   688,   690,   692,   699,   703,   705,
    -565,  -565,  -565,   708,  -565,   719,  -565,   723,   701,   707,
     710,   711,   714,  -565,   716,   717,   184,   712,   713,   804,
     730,  -565,  -565,   721,   811,  3027,  -565,  -565,   812,   732,
     278,   112,  -565,   813,  -565,   733,  -565,   734,   796,  2555,
     797,   735,  -565,  -565,  -565,  -565,  -565,  1391,  2431,  -565,
    -565,  -565,  -565,  -565,  -565,  -565,  -565,  -565,  -565,  -565,
    -565,  -565,  2106,   738,   637,  2171,   741,   743,  -565,  -565,
     786,   787,  3092,   496,   817,  -565,  -565,  -565,   788,   790,
    -565,   877,   791,   279,   116,   164,   792,  -565,   793,   883,
     798,   166,  -565,  -565,   799,  -565,   800,   801,  -565,  1391,
    -565,   837,  -565,  -565,   848,  -565,  -565,  -565,  -565,   850,
     280,   858,  -565,  2236,  -565,  2632,   193,   853,  -565,   854,
     945,   946,  -565,  -565,   861,  -565,   949,  1001,  -565,  1001,
     585,  -565,  -565,   863,   864,  -565,   865,   866,   900,  -565,
     956,  -565,  -565,   870,   873,   194,   210,  -565,  2301,   875,
    -565,  -565,  -565,  -565,  -565,   247,  -565,  -565,   876,   878,
     263,  -565,  -565,   964,   966,   912,  -565,   968,   268,  2366,
    -565,  -565,   970,   884,   886,   887,   890,   978,   990,   906,
    -565,  -565,  -565,  -565,   915,   916,  -565,  -565,  -565
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       3,     0,     2,     1,    49,     0,     0,     0,     0,    34,
       0,    50,     0,     0,     0,     0,     0,     0,   191,   193,
       0,   186,     0,     0,     0,     0,     0,     0,    51,     0,
       0,     0,     0,     0,     0,     0,     4,     5,     0,     0,
      46,     0,     0,     9,    12,    14,    13,    15,    16,    17,
      18,    19,     0,    21,     0,    22,    11,    10,    24,     0,
       0,     0,     0,     0,     0,     0,     0,    33,     0,   283,
     283,     0,     0,     0,   258,    49,   261,     0,     0,   257,
     265,   266,   267,   268,     0,     0,     0,     0,   264,     0,
       0,   364,   364,   275,     0,   217,   218,   220,   222,   224,
     227,   230,   233,   236,   242,   272,   260,     0,    61,     0,
       0,     0,     3,     0,     0,   192,   194,     0,     0,   187,
     189,   190,    49,     0,   174,     0,   138,   137,     0,     0,
       0,     0,   185,    57,    59,    58,    60,   131,    55,     0,
       0,     0,   124,   126,   123,   125,     0,     6,    54,    42,
      43,    44,    45,     0,     0,    52,     0,     0,     0,   188,
       7,     8,    20,    23,    25,    26,    27,    28,    29,    30,
      31,    32,     0,   285,     0,   284,     0,     0,     0,     0,
     281,   283,   238,   223,   239,     0,     0,   237,     0,     0,
       0,   283,     0,   262,     0,     0,     0,     0,     0,   247,
     248,   249,   250,   251,   252,   253,   254,   255,   256,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     3,     0,   289,   289,     0,
       0,     0,     3,     0,     3,   184,   183,     0,   182,     0,
     179,     0,   289,    56,     0,   289,     3,     0,     0,     0,
       0,    35,     0,     0,   309,     0,   310,   355,   325,   322,
     323,   314,   329,   336,   337,   338,   354,   334,   335,   333,
     320,   318,   343,   324,   315,   352,   327,   328,   316,   319,
     326,   351,   339,   340,   346,   330,   341,   342,   349,   353,
     321,   350,   317,   311,   312,   313,   347,   348,   345,   331,
     332,   344,    48,    36,     0,     0,    40,     0,   309,   308,
       0,     0,   307,     0,    63,     0,    64,     0,     0,     0,
       0,     0,     0,     0,   283,   259,   269,   270,   365,   287,
     364,   273,   364,     0,   309,     0,   279,    49,     3,   206,
      46,   207,   208,   209,   210,   211,   212,   213,   214,   215,
     216,     0,   219,   221,   228,   229,     0,   225,   231,   232,
     234,   235,     0,   309,     0,   244,   282,    62,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    83,   299,     0,
     290,     0,     0,     0,   139,     0,   175,     0,   181,   180,
     177,   178,   283,     0,   133,     0,     0,     0,   129,   127,
     128,     0,    47,     0,   283,   283,    39,    37,     0,     0,
       0,   142,     0,     0,     0,   364,   286,   150,     0,   145,
       0,     0,   263,   240,     0,   364,     0,     0,   276,   283,
     283,   277,     0,     0,   201,   226,   243,   283,   283,     0,
       3,     0,     0,     0,     0,     0,    50,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     3,    50,    50,
       0,   132,     3,     0,    50,     0,     0,    53,     0,     0,
       0,    38,   362,     0,   164,   356,   357,   140,     0,     0,
       0,   150,   144,     0,   241,     0,   271,   274,     0,     0,
       0,    50,   195,     0,   196,     0,     0,     3,     0,     0,
       0,     3,     0,    78,     0,     0,     0,    85,     0,   291,
     294,   295,   296,   297,   298,     0,     0,   300,   302,     0,
       3,   301,     0,     3,     3,     0,     0,     0,    67,     0,
       3,     0,   130,     3,    65,    66,    41,   363,     0,     0,
     164,   309,     0,     0,     0,     0,   151,   152,     0,     0,
       0,   288,   278,   280,     0,     3,     0,     0,     0,     3,
       0,   245,   246,     0,    50,    51,    72,     0,     3,     3,
       0,     0,    79,    85,     0,    84,    80,   293,   292,     0,
       0,     0,     0,     0,     0,     0,     0,    50,   135,   176,
      50,     0,   122,     0,     0,     0,     0,     0,   165,   166,
       0,     0,     0,     0,     0,     0,     0,     0,   150,     0,
       0,     0,     0,     0,     0,     0,   198,   197,     0,     0,
     202,    50,     0,    70,     0,     0,     0,     0,    73,     3,
      81,    85,   114,    86,     0,     0,     0,    91,    93,    95,
      94,    96,    87,    97,    98,    99,   100,     0,   102,     0,
      92,   104,     0,     0,     0,     0,     0,     0,     0,     0,
     113,   303,     3,    50,   306,   304,     3,    50,    50,     0,
       0,    50,    50,   358,     0,   172,     0,     0,     0,     0,
       0,   359,   360,     0,     0,     0,   150,   150,     0,     0,
       0,     0,   148,     0,   150,     0,   146,     0,    50,     0,
      50,     0,    69,    71,     3,    76,    74,     0,    82,    88,
      89,    90,   101,   103,   105,   106,   107,   108,   109,   110,
     111,   112,     0,     0,     0,     0,     0,     0,   134,   119,
       0,     0,     0,   289,     0,   167,   143,     3,     0,     0,
     150,     0,     0,     0,     0,     0,     0,   153,     0,     0,
       0,     0,   147,     3,     0,     3,     0,     0,    68,     0,
      75,    50,   115,   305,    50,   116,   136,   121,   120,     0,
       0,     0,   173,     0,   141,     0,     0,     0,   155,     0,
       0,     0,   150,   150,     0,     3,     0,     0,   199,     0,
     201,   203,    77,     0,     0,   164,     0,     0,    50,   361,
       0,   150,   154,     0,     0,     0,     0,   156,     0,     0,
     200,   205,   204,   117,   118,     0,   164,     3,     0,     0,
       0,   158,   160,     0,     0,    50,   149,     0,     0,     0,
     171,   159,     0,     0,     0,     0,     0,     0,    50,     0,
     161,   157,   163,   170,     0,     0,   162,   169,   168
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -565,  -565,  -110,  -565,  -193,  -438,   851,  -565,    -2,   911,
    -565,  -565,   880,  -191,  -178,  -556,  -558,  -550,  -547,  -544,
    -542,  -565,  -565,  -501,  -565,  -541,  -536,  -532,  -528,  -170,
     868,   614,  -527,  -524,   -81,  -565,  -565,  -565,  -565,  -523,
    -475,  -565,  -533,  -565,  -565,   789,  -522,  -159,  -154,  -148,
    -521,  -144,  -137,  -133,  -132,  -519,  -470,   229,  -482,    20,
    -565,   823,   -55,  -565,  -197,   209,   197,   -51,   937,   815,
    -565,   698,  -565,  -565,  -565,   -26,  -565,  -565,  -219,  -564,
    -565,   547,   -14,  -173,   428,   -82
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    37,    38,   307,   157,    39,    93,   158,
     253,   137,   138,    41,    42,    43,   566,    44,    45,    46,
      47,   377,   450,   575,   642,    48,    49,    50,    51,    52,
     139,   395,    53,    54,   125,    55,   540,   474,    56,    57,
     479,   547,   539,   599,   676,   126,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,   492,   494,   351,   173,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   210,
     104,   105,   193,   431,   106,   174,   175,   330,   379,   517,
     380,   311,   312,   313,   473,   189
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      40,   339,   224,   341,   318,   320,   549,   600,   556,   381,
     190,   560,   628,   357,   518,   661,   342,   332,   664,   637,
     120,   121,   124,   393,   343,   638,   396,   182,   639,   183,
      94,   640,   108,   641,   643,   344,   113,   114,   187,   644,
     345,   119,   230,   645,   176,   127,   346,   646,   648,   132,
     347,   649,   650,   651,   655,    68,   660,   348,   110,   604,
     605,   349,   350,   567,   543,   142,   143,    75,   705,   706,
     595,   129,   630,   372,   637,   388,   133,   480,   323,     3,
     638,   580,   543,   639,   584,    70,   640,   544,   641,   643,
      11,    75,   215,   596,   644,   595,   606,   390,   645,   134,
     111,   216,   646,   648,    71,   612,   649,   650,   651,   655,
     188,   660,   568,   135,    11,   375,   543,   373,   679,   545,
     543,   124,   385,    28,   387,   597,   215,   481,   112,   130,
     708,   389,   136,   691,    69,   216,   397,   545,   607,   749,
     128,   353,   302,   780,    72,   665,   608,    28,   382,   760,
     597,   546,   637,   391,   623,   322,    73,   598,   638,   435,
     763,   639,   360,   361,   640,   333,   641,   643,   543,   546,
     543,   545,   644,   251,   252,   545,   645,   303,   336,   191,
     646,   648,   598,   412,   649,   650,   651,   655,   147,   660,
     192,   781,   340,   786,   451,   582,   247,   543,   543,   109,
     220,   792,   365,   546,   413,   602,   248,   546,   383,   329,
     683,   744,   745,   231,   543,   452,   583,   756,   115,   751,
     800,   823,    40,   545,   221,   545,   603,   124,   432,   124,
     117,   337,   116,   118,     5,   362,   140,   824,   367,   368,
     369,   370,   414,   231,   222,   232,   684,   415,   426,   554,
     427,   595,   545,   545,    11,   546,    12,   546,   149,   150,
     151,   152,   815,   418,   141,   776,   420,   543,   415,   545,
     239,   415,   595,   734,   827,   735,    18,    19,   240,    21,
      22,    23,   146,   828,   546,   546,    27,    28,   685,    29,
     832,   235,   159,    33,    34,   837,   686,   339,   424,   341,
     339,   546,   341,   144,   145,   456,   597,   805,   806,   122,
     231,   304,   342,   160,   133,   342,   236,   810,   555,   811,
     343,   161,   545,   343,   406,   407,   820,   597,   211,   212,
     498,   344,    11,   478,   344,   416,   345,   134,   598,   345,
     237,   448,   346,   485,   449,   346,   347,   525,   238,   347,
     486,   135,   529,   348,   546,   328,   348,   349,   350,   598,
     349,   350,   748,   779,   796,    28,   460,   415,   415,   415,
     136,   487,   162,    40,   328,   213,   214,   163,   468,   469,
     197,   198,   634,    40,   635,    40,   164,   563,   123,   674,
     675,   570,   443,   242,   445,    40,   447,   636,   519,   520,
     165,   522,   523,   488,   489,   647,   354,   355,   358,   359,
     581,   495,   496,   585,   586,   166,   652,   167,   168,   169,
     591,   653,   170,   593,   171,   172,   177,   654,   178,   471,
      40,   656,   475,   476,   690,   179,   180,   634,   657,   635,
     181,   185,   658,   659,   186,   615,   194,   195,   196,   619,
     218,   217,   636,   219,   223,   148,   225,   226,   626,   627,
     647,   499,   500,   149,   150,   151,   152,   227,   506,   228,
     229,   652,   233,   234,   241,   244,   653,   245,   249,   306,
     246,   314,   654,   316,   315,   321,   656,   324,   340,   325,
     326,   340,   537,   657,   366,   371,    40,   658,   659,   376,
     378,   386,   392,   394,   399,   551,   339,   398,   341,   400,
     402,   401,   743,   403,   771,   634,   153,   635,   409,   707,
     411,   342,   571,    40,   404,   405,   408,    40,   410,   343,
     636,   417,   154,   441,   155,   419,   422,   421,   647,   429,
     344,   156,    92,   425,   428,   345,   415,   430,    69,   652,
     434,   346,   722,   433,   653,   347,   725,   439,   436,   770,
     654,    40,   348,   624,   656,   437,   349,   350,    40,   438,
     440,   657,   442,    40,   614,   658,   659,   444,   618,    40,
     457,   465,   453,    40,    40,   148,   462,   625,   470,    40,
     454,    40,   455,   461,   759,   463,   472,   197,   198,   483,
     467,   477,    74,    75,    76,   482,    77,    78,    79,   484,
     493,   497,   501,    40,   673,   502,   504,    40,   505,   503,
     507,   508,   681,   682,    40,    40,    11,   773,    40,    80,
      81,    82,    83,   521,   524,    84,   526,    85,    86,   527,
     509,   528,   510,   787,   530,   789,   199,   200,   201,   202,
     203,   204,   205,   206,   207,   208,   531,    87,   532,    28,
     534,    88,   533,   535,   155,   548,   550,   511,   512,   513,
     514,   536,   538,   552,   553,   808,   574,   557,   577,    89,
     561,   562,    90,   578,    91,   579,    92,   609,   610,   569,
     611,   572,   573,   131,   576,   588,   589,   340,   592,   594,
     601,   613,   616,   617,   620,    40,    40,   829,   622,    74,
      75,    76,   629,    77,    78,    79,   515,   516,   631,   662,
      40,   666,   669,    40,   677,    74,    75,    76,   670,    77,
      78,    79,   678,    11,   687,   688,    80,    81,    82,    83,
     692,   693,    84,   695,    85,    86,   701,   694,   696,    11,
     697,   699,    80,    81,    82,    83,   727,    40,    84,   723,
      85,    86,   702,   703,    87,   704,    28,   709,    88,   710,
     711,    40,   712,   713,   726,   730,   714,   731,   715,   716,
      87,   717,    28,   718,    88,    40,    89,    40,   724,    90,
     719,    91,   327,    92,   720,   799,   721,   328,   728,   732,
     733,   729,    89,   736,   737,    90,    40,    91,   738,    92,
     308,   309,   740,   328,   739,   741,   746,   750,   754,   757,
     256,   257,   772,   747,   752,   753,   758,    40,   258,   762,
     259,   260,   765,   261,   766,   262,   263,   264,   265,   266,
     267,   268,   269,   270,   271,   272,   273,   274,   275,   276,
     277,   278,   279,   280,   281,   282,   283,   284,   285,   286,
     287,   288,   289,   290,   291,   292,   293,   294,   295,   296,
     297,   298,   299,   300,   301,   541,   309,   767,   768,   774,
     775,   777,   778,   782,   783,   256,   257,   784,   793,   785,
     788,   790,   791,   258,   331,   259,   260,   328,   261,   794,
     262,   263,   264,   265,   266,   267,   268,   269,   270,   271,
     272,   273,   274,   275,   276,   277,   278,   279,   280,   281,
     282,   283,   284,   285,   286,   287,   288,   289,   290,   291,
     292,   293,   294,   295,   296,   297,   298,   299,   300,   301,
       4,   795,   797,     5,   801,   802,     6,     7,     8,   803,
     804,     9,   807,   809,   813,   814,   816,   817,    10,   818,
     819,   821,   328,    11,   822,    12,   826,   830,   833,   831,
     834,   835,   836,    13,   839,   840,    14,   841,   842,    15,
     374,   843,   844,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,   845,    27,    28,   846,    29,    30,
      31,    32,    33,    34,    35,     4,   847,   848,     5,   305,
     209,     6,     7,     8,   250,   466,     9,   243,   352,   812,
     384,   423,   184,    10,   356,   542,   490,    36,   491,   680,
      12,     0,     0,     0,     0,     0,     0,     0,    13,     0,
       0,    14,     0,     0,    15,     0,     0,     0,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,     0,
      27,    28,     0,    29,    30,    31,    32,    33,    34,    35,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     0,
       0,     9,     0,     0,     0,     0,     0,     0,    10,     0,
       0,     0,    36,    11,     0,    12,     0,     0,     0,     0,
       0,     0,     0,    13,     0,     0,    14,     0,     0,    15,
       0,     0,     0,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,     0,    27,    28,     0,    29,    30,
      31,    32,    33,    34,    35,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    36,   446,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,     0,
       0,    14,     0,     0,    15,     0,     0,     0,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,     0,
      27,    28,     0,    29,    30,    31,    32,    33,    34,    35,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     0,
       0,     9,     0,     0,     0,     0,     0,     0,    10,     0,
       0,     0,    36,   458,     0,    12,     0,     0,     0,     0,
       0,     0,     0,    13,     0,     0,    14,     0,     0,    15,
       0,     0,     0,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,     0,    27,    28,     0,    29,    30,
      31,    32,    33,    34,    35,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    36,   459,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,     0,
       0,    14,     0,     0,    15,     0,     0,     0,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,     0,
      27,    28,     0,    29,    30,    31,    32,    33,    34,    35,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     0,
       0,     9,     0,     0,     0,     0,     0,     0,    10,     0,
       0,     0,    36,   464,     0,    12,     0,     0,     0,     0,
       0,     0,     0,    13,     0,     0,    14,     0,     0,    15,
       0,     0,     0,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,     0,    27,    28,     0,    29,    30,
      31,    32,    33,    34,    35,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    36,   564,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,     0,
       0,    14,     0,     0,    15,     0,     0,     0,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,     0,
      27,   565,     0,    29,    30,    31,    32,    33,    34,    35,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     0,
       0,     9,     0,     0,     0,     0,     0,     0,    10,     0,
       0,     0,    36,   587,     0,    12,     0,     0,     0,     0,
       0,     0,     0,    13,     0,     0,    14,     0,     0,    15,
       0,     0,     0,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,     0,    27,    28,     0,    29,    30,
      31,    32,    33,    34,    35,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    36,   590,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,     0,
       0,    14,     0,     0,    15,     0,     0,     0,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,     0,
      27,    28,     0,    29,    30,    31,    32,    33,    34,    35,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     0,
       0,     9,     0,     0,     0,     0,     0,     0,    10,     0,
       0,     0,    36,   621,     0,    12,     0,     0,     0,     0,
       0,     0,     0,    13,     0,     0,    14,     0,     0,    15,
       0,     0,     0,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,     0,    27,    28,     0,    29,    30,
      31,    32,    33,    34,    35,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    36,   663,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,     0,
       0,    14,     0,     0,    15,     0,     0,     0,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,     0,
      27,    28,     0,    29,    30,    31,    32,    33,    34,    35,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     0,
       0,     9,     0,     0,     0,     0,     0,     0,    10,     0,
       0,     0,    36,   667,     0,    12,     0,     0,     0,     0,
       0,     0,     0,    13,     0,     0,    14,     0,     0,    15,
       0,     0,     0,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,     0,    27,    28,     0,    29,    30,
      31,    32,    33,    34,    35,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    36,   668,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,     0,
       0,    14,     0,     0,    15,     0,     0,     0,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,     0,
      27,    28,     0,    29,    30,    31,    32,    33,    34,    35,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     0,
       0,     9,     0,     0,     0,     0,     0,     0,    10,     0,
       0,     0,    36,   671,     0,    12,     0,     0,     0,     0,
       0,     0,     0,    13,     0,     0,    14,     0,     0,    15,
       0,     0,     0,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,     0,    27,    28,     0,    29,    30,
      31,    32,    33,    34,    35,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    36,   672,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,     0,
       0,    14,     0,     0,    15,     0,     0,     0,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,     0,
      27,    28,     0,    29,    30,    31,    32,    33,    34,    35,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     0,
       0,     9,     0,     0,     0,     0,     0,     0,    10,     0,
       0,     0,    36,   698,     0,    12,     0,     0,     0,     0,
       0,     0,     0,    13,     0,     0,    14,     0,     0,    15,
       0,     0,     0,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,     0,    27,    28,     0,    29,    30,
      31,    32,    33,    34,    35,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    36,   700,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,     0,
       0,    14,     0,     0,    15,     0,     0,     0,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,     0,
      27,    28,     0,    29,    30,    31,    32,    33,    34,    35,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     0,
       0,     9,     0,     0,     0,     0,     0,     0,    10,     0,
       0,     0,    36,   761,     0,    12,     0,     0,     0,     0,
       0,     0,     0,    13,     0,     0,    14,     0,     0,    15,
       0,     0,     0,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,     0,    27,    28,     0,    29,    30,
      31,    32,    33,    34,    35,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    36,   764,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,     0,
       0,    14,     0,     0,    15,     0,     0,     0,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,     0,
      27,    28,     0,    29,    30,    31,    32,    33,    34,    35,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     0,
       0,     9,     0,     0,     0,     0,     0,     0,    10,     0,
       0,     0,    36,   798,     0,    12,     0,     0,     0,     0,
       0,     0,     0,    13,     0,     0,    14,     0,     0,    15,
       0,     0,     0,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,     0,    27,    28,     0,    29,    30,
      31,    32,    33,    34,    35,     4,     0,     0,     5,     0,
       0,     6,     7,     8,     0,     0,     9,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    36,   825,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,     0,
       0,    14,     0,     0,    15,     0,     0,     0,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,     0,
      27,    28,     0,    29,    30,    31,    32,    33,    34,    35,
       4,     0,     0,     5,     0,     0,     6,     7,     8,     0,
       0,     9,     0,     0,     0,     0,     0,     0,    10,     0,
       0,     0,    36,   838,     0,    12,     0,     0,     0,     0,
       0,     0,     0,    13,     0,     0,    14,     0,     0,    15,
       0,     0,     0,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,     0,    27,    28,     0,    29,    30,
      31,    32,    33,    34,    35,   337,     0,     0,     5,     0,
       0,     6,     0,     0,     0,     0,   632,     0,     0,     0,
       0,     0,     0,    10,     0,     0,     0,    36,    11,     0,
      12,     0,     0,     0,     0,     0,     0,     0,    13,     0,
       0,    14,     0,     0,    15,     0,     0,     0,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,   337,
      27,    28,     5,    29,    30,    31,    32,    33,    34,    35,
       0,     0,     0,     0,     0,     0,     0,   558,   337,     0,
       0,     5,    11,     0,    12,     0,     0,     0,     0,     0,
       0,     0,   633,     0,     0,     0,     0,     0,     0,     0,
       0,    11,     0,    12,    18,    19,     0,    21,    22,    23,
       0,     0,     0,     0,    27,    28,     0,    29,     0,     0,
       0,    33,    34,    18,    19,     0,    21,    22,    23,   337,
       0,     0,     5,    27,    28,     0,    29,     0,     0,     0,
      33,    34,     0,     0,     0,     0,   559,     0,     0,     0,
       0,     0,    11,     0,    12,   199,   200,   201,   202,   203,
     204,   205,   206,   207,   208,   338,    74,    75,    76,     0,
      77,    78,    79,     0,    18,    19,     0,    21,    22,    23,
       0,     0,     0,     0,    27,    28,     0,    29,     0,     0,
      11,    33,    34,    80,    81,    82,    83,     0,     0,    84,
       0,    85,    86,     0,   107,    74,    75,    76,     0,    77,
      78,    79,     0,     0,     0,     0,   755,     0,     0,     0,
       0,    87,     0,    28,     0,    88,     0,     0,     0,    11,
       0,     0,    80,    81,    82,    83,     0,     0,    84,     0,
      85,    86,     0,    89,     0,     0,    90,     0,    91,     0,
      92,    74,    75,    76,     0,    77,     0,    79,     0,     0,
      87,     0,    28,     0,    88,     0,    74,    75,    76,     0,
      77,    78,    79,     0,     0,    11,     0,     0,    80,    81,
      82,    83,    89,     0,     0,    90,     0,    91,     0,    92,
      11,     0,     0,    80,    81,    82,    83,     0,     0,     0,
       0,    85,    86,     0,     0,     0,    87,     0,    28,     0,
      88,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    87,     0,    28,     0,    88,     0,     0,     0,     0,
       0,    90,     0,    91,     0,    92,     0,     0,     0,     0,
       0,   308,   309,    89,     0,     0,    90,     0,    91,     0,
      92,   256,   257,     0,     0,     0,     0,     0,     0,   258,
       0,   259,   260,     0,   261,     0,   262,   263,   264,   265,
     266,   267,   268,   269,   270,   271,   272,   273,   274,   275,
     276,   277,   278,   279,   280,   281,   282,   283,   284,   285,
     286,   287,   288,   289,   290,   291,   292,   293,   294,   295,
     296,   297,   298,   299,   300,   301,   308,   309,     0,     0,
       0,     0,     0,     0,     0,     0,   256,   257,     0,     0,
       0,   310,     0,     0,   258,     0,   259,   260,     0,   261,
       0,   262,   263,   264,   265,   266,   267,   268,   269,   270,
     271,   272,   273,   274,   275,   276,   277,   278,   279,   280,
     281,   282,   283,   284,   285,   286,   287,   288,   289,   290,
     291,   292,   293,   294,   295,   296,   297,   298,   299,   300,
     301,   308,   309,     0,     0,     0,     0,     0,     0,     0,
       0,   256,   257,     0,     0,     0,   317,     0,     0,   258,
       0,   259,   260,     0,   261,     0,   262,   263,   264,   265,
     266,   267,   268,   269,   270,   271,   272,   273,   274,   275,
     276,   277,   278,   279,   280,   281,   282,   283,   284,   285,
     286,   287,   288,   289,   290,   291,   292,   293,   294,   295,
     296,   297,   298,   299,   300,   301,   308,   309,     0,     0,
       0,     0,     0,     0,     0,     0,   256,   257,     0,     0,
       0,   319,     0,     0,   258,     0,   259,   260,     0,   261,
       0,   262,   263,   264,   265,   266,   267,   268,   269,   270,
     271,   272,   273,   274,   275,   276,   277,   278,   279,   280,
     281,   282,   283,   284,   285,   286,   287,   288,   289,   290,
     291,   292,   293,   294,   295,   296,   297,   298,   299,   300,
     301,   308,   309,     0,     0,     0,     0,     0,     0,     0,
       0,   256,   257,     0,     0,     0,   689,     0,     0,   258,
       0,   259,   260,     0,   261,     0,   262,   263,   264,   265,
     266,   267,   268,   269,   270,   271,   272,   273,   274,   275,
     276,   277,   278,   279,   280,   281,   282,   283,   284,   285,
     286,   287,   288,   289,   290,   291,   292,   293,   294,   295,
     296,   297,   298,   299,   300,   301,   308,   309,     0,     0,
       0,     0,     0,     0,     0,     0,   256,   257,     0,     0,
       0,   742,     0,     0,   258,     0,   259,   260,     0,   261,
       0,   262,   263,   264,   265,   266,   267,   268,   269,   270,
     271,   272,   273,   274,   275,   276,   277,   278,   279,   280,
     281,   282,   283,   284,   285,   286,   287,   288,   289,   290,
     291,   292,   293,   294,   295,   296,   297,   298,   299,   300,
     301,   254,     0,     0,   255,     0,     0,     0,     0,     0,
       0,   256,   257,     0,     0,     0,   769,     0,     0,   258,
       0,   259,   260,     0,   261,     0,   262,   263,   264,   265,
     266,   267,   268,   269,   270,   271,   272,   273,   274,   275,
     276,   277,   278,   279,   280,   281,   282,   283,   284,   285,
     286,   287,   288,   289,   290,   291,   292,   293,   294,   295,
     296,   297,   298,   299,   300,   301,   334,     0,     0,   335,
       0,     0,     0,     0,     0,     0,   256,   257,     0,     0,
       0,     0,     0,     0,   258,     0,   259,   260,     0,   261,
       0,   262,   263,   264,   265,   266,   267,   268,   269,   270,
     271,   272,   273,   274,   275,   276,   277,   278,   279,   280,
     281,   282,   283,   284,   285,   286,   287,   288,   289,   290,
     291,   292,   293,   294,   295,   296,   297,   298,   299,   300,
     301,   363,     0,     0,   364,     0,     0,     0,     0,     0,
       0,   256,   257,     0,     0,     0,     0,     0,     0,   258,
       0,   259,   260,     0,   261,     0,   262,   263,   264,   265,
     266,   267,   268,   269,   270,   271,   272,   273,   274,   275,
     276,   277,   278,   279,   280,   281,   282,   283,   284,   285,
     286,   287,   288,   289,   290,   291,   292,   293,   294,   295,
     296,   297,   298,   299,   300,   301
};

static const yytype_int16 yycheck[] =
{
       2,   194,   112,   194,   177,   178,   481,   540,   490,   228,
      92,   493,   570,   210,   452,   579,   194,   190,   582,   575,
      22,    23,    24,   242,   194,   575,   245,    78,   575,    84,
      10,   575,    12,   575,   575,   194,    16,    17,    89,   575,
     194,    21,   123,   575,    70,    25,   194,   575,   575,    29,
     194,   575,   575,   575,   575,     4,   575,   194,     4,     4,
       5,   194,   194,    42,     4,     4,     5,     4,   626,   627,
       4,     4,   573,    45,   630,     4,     4,    41,    37,     0,
     630,   519,     4,   630,   522,    83,   630,    27,   630,   630,
      27,     4,    85,    27,   630,     4,    41,     4,   630,    27,
      46,    94,   630,   630,     4,    27,   630,   630,   630,   630,
      90,   630,    91,    41,    27,   225,     4,    89,    27,    59,
       4,   123,   232,    60,   234,    59,    85,    91,    91,    62,
     631,    60,    60,   608,    83,    94,   246,    59,    83,    27,
      58,   196,   156,    27,     4,   583,    91,    60,   229,   707,
      59,    91,   708,    60,    91,   181,     4,    91,   708,   356,
     724,   708,   213,   214,   708,   191,   708,   708,     4,    91,
       4,    59,   708,   153,   154,    59,   708,   157,   192,    83,
     708,   708,    91,    69,   708,   708,   708,   708,    91,   708,
      94,    27,   194,    27,    69,    69,     4,     4,     4,     4,
      45,   759,   216,    91,    90,    69,    14,    91,    84,   189,
       5,   686,   687,    89,     4,    90,    90,   699,     4,   694,
      27,    27,   224,    59,    69,    59,    90,   229,   338,   231,
       4,     4,     4,     7,     7,   215,     4,    27,   218,   219,
     220,   221,    84,    89,    89,    91,    41,    89,   330,    22,
     332,     4,    59,    59,    27,    91,    29,    91,    16,    17,
      18,    19,   795,    84,     4,   740,    84,     4,    89,    59,
      53,    89,     4,    89,    27,    91,    49,    50,    61,    52,
      53,    54,    63,   816,    91,    91,    59,    60,    83,    62,
      27,     4,    90,    66,    67,    27,    91,   490,   324,   490,
     493,    91,   493,     4,     5,    84,    59,   782,   783,     4,
      89,    69,   490,    91,     4,   493,    29,   787,    91,   789,
     490,    91,    59,   493,   304,   305,   801,    59,    79,    80,
     440,   490,    27,   415,   493,   315,   490,    27,    91,   493,
      53,    23,   490,   425,    26,   493,   490,   457,    61,   493,
      86,    41,   462,   490,    91,    91,   493,   490,   490,    91,
     493,   493,    84,    84,    84,    60,   392,    89,    89,    89,
      60,    88,    91,   375,    91,    81,    82,    91,   404,   405,
      20,    21,   575,   385,   575,   387,    91,   497,    83,     4,
       5,   501,   372,    83,   374,   397,   376,   575,    90,    91,
      91,    90,    91,   429,   430,   575,   197,   198,   211,   212,
     520,   437,   438,   523,   524,    91,   575,    91,    91,    91,
     530,   575,    91,   533,    91,    83,    83,   575,    83,   409,
     432,   575,   412,   413,   607,    59,     4,   630,   575,   630,
      83,     4,   575,   575,    83,   555,    24,    35,    34,   559,
      62,     3,   630,    83,     4,     8,    91,    91,   568,   569,
     630,   441,   442,    16,    17,    18,    19,    83,   448,    83,
      83,   630,    94,    91,     4,    40,   630,    83,     4,     4,
      91,    84,   630,    84,    89,     4,   630,    83,   490,    84,
      84,   493,   472,   630,     4,     4,   498,   630,   630,    23,
       4,     4,    83,     4,     4,   485,   699,     5,   699,     5,
      86,    40,   685,     6,   733,   708,    69,   708,    69,   629,
      91,   699,   502,   525,    83,    83,    83,   529,    83,   699,
     708,    91,    85,    41,    87,    91,    84,    91,   708,    83,
     699,    94,    87,    89,    84,   699,    89,    83,    83,   708,
      91,   699,   662,    90,   708,   699,   666,    84,    86,   732,
     708,   563,   699,   565,   708,    83,   699,   699,   570,    83,
      91,   708,    45,   575,   554,   708,   708,     4,   558,   581,
      91,    14,    84,   585,   586,     8,    91,   567,     4,   591,
      89,   593,    84,    84,   704,    84,     4,    20,    21,    13,
      88,    91,     3,     4,     5,    91,     7,     8,     9,    84,
      25,    91,    91,   615,   594,    45,    47,   619,    24,    91,
      91,    28,   602,   603,   626,   627,    27,   737,   630,    30,
      31,    32,    33,     4,    91,    36,    55,    38,    39,    57,
       3,    84,     5,   753,    91,   755,    69,    70,    71,    72,
      73,    74,    75,    76,    77,    78,    65,    58,     4,    60,
      84,    62,    91,    84,    87,     4,    27,    30,    31,    32,
      33,    84,    84,    84,    84,   785,    24,    22,     3,    80,
      84,    84,    83,     3,    85,    69,    87,     4,     4,    91,
       4,    91,    91,    94,    91,    91,    91,   699,    91,    90,
      83,     4,    91,    91,    91,   707,   708,   817,    40,     3,
       4,     5,    91,     7,     8,     9,    79,    80,    91,    91,
     722,    91,    55,   725,     4,     3,     4,     5,    63,     7,
       8,     9,     4,    27,    91,     4,    30,    31,    32,    33,
      91,    94,    36,     4,    38,    39,    37,    91,    91,    27,
      24,    24,    30,    31,    32,    33,    55,   759,    36,    51,
      38,    39,    91,    91,    58,    91,    60,    91,    62,    91,
      91,   773,    91,    91,    51,    64,    91,    63,    91,    91,
      58,    91,    60,    91,    62,   787,    80,   789,    69,    83,
      91,    85,    86,    87,    91,   775,    91,    91,    91,    83,
      83,    91,    80,    91,    91,    83,   808,    85,     4,    87,
       4,     5,    91,    91,    84,     4,     4,     4,    22,    22,
      14,    15,     5,    91,    91,    91,    91,   829,    22,    91,
      24,    25,    91,    27,    91,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,     4,     5,    91,    91,    91,
      90,     4,    91,    91,    91,    14,    15,     4,    51,    91,
      91,    91,    91,    22,    88,    24,    25,    91,    27,    51,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
       4,    91,    84,     7,    91,    91,    10,    11,    12,     4,
       4,    15,    91,     4,    91,    91,    91,    91,    22,    59,
       4,    91,    91,    27,    91,    29,    91,    91,     4,    91,
       4,    59,     4,    37,     4,    91,    40,    91,    91,    43,
      44,    91,     4,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,     4,    59,    60,    91,    62,    63,
      64,    65,    66,    67,    68,     4,    91,    91,     7,   158,
      99,    10,    11,    12,   146,   401,    15,   137,   195,   790,
     231,   323,    85,    22,   209,   478,    25,    91,    27,   601,
      29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    -1,
      -1,    40,    -1,    -1,    43,    -1,    -1,    -1,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    -1,
      59,    60,    -1,    62,    63,    64,    65,    66,    67,    68,
       4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,    -1,
      -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,
      -1,    -1,    91,    27,    -1,    29,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    37,    -1,    -1,    40,    -1,    -1,    43,
      -1,    -1,    -1,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    -1,    59,    60,    -1,    62,    63,
      64,    65,    66,    67,    68,     4,    -1,    -1,     7,    -1,
      -1,    10,    11,    12,    -1,    -1,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    -1,    -1,    91,    27,    -1,
      29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    -1,
      -1,    40,    -1,    -1,    43,    -1,    -1,    -1,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    -1,
      59,    60,    -1,    62,    63,    64,    65,    66,    67,    68,
       4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,    -1,
      -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,
      -1,    -1,    91,    27,    -1,    29,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    37,    -1,    -1,    40,    -1,    -1,    43,
      -1,    -1,    -1,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    -1,    59,    60,    -1,    62,    63,
      64,    65,    66,    67,    68,     4,    -1,    -1,     7,    -1,
      -1,    10,    11,    12,    -1,    -1,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    -1,    -1,    91,    27,    -1,
      29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    -1,
      -1,    40,    -1,    -1,    43,    -1,    -1,    -1,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    -1,
      59,    60,    -1,    62,    63,    64,    65,    66,    67,    68,
       4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,    -1,
      -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,
      -1,    -1,    91,    27,    -1,    29,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    37,    -1,    -1,    40,    -1,    -1,    43,
      -1,    -1,    -1,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    -1,    59,    60,    -1,    62,    63,
      64,    65,    66,    67,    68,     4,    -1,    -1,     7,    -1,
      -1,    10,    11,    12,    -1,    -1,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    -1,    -1,    91,    27,    -1,
      29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    -1,
      -1,    40,    -1,    -1,    43,    -1,    -1,    -1,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    -1,
      59,    60,    -1,    62,    63,    64,    65,    66,    67,    68,
       4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,    -1,
      -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,
      -1,    -1,    91,    27,    -1,    29,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    37,    -1,    -1,    40,    -1,    -1,    43,
      -1,    -1,    -1,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    -1,    59,    60,    -1,    62,    63,
      64,    65,    66,    67,    68,     4,    -1,    -1,     7,    -1,
      -1,    10,    11,    12,    -1,    -1,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    -1,    -1,    91,    27,    -1,
      29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    -1,
      -1,    40,    -1,    -1,    43,    -1,    -1,    -1,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    -1,
      59,    60,    -1,    62,    63,    64,    65,    66,    67,    68,
       4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,    -1,
      -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,
      -1,    -1,    91,    27,    -1,    29,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    37,    -1,    -1,    40,    -1,    -1,    43,
      -1,    -1,    -1,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    -1,    59,    60,    -1,    62,    63,
      64,    65,    66,    67,    68,     4,    -1,    -1,     7,    -1,
      -1,    10,    11,    12,    -1,    -1,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    -1,    -1,    91,    27,    -1,
      29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    -1,
      -1,    40,    -1,    -1,    43,    -1,    -1,    -1,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    -1,
      59,    60,    -1,    62,    63,    64,    65,    66,    67,    68,
       4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,    -1,
      -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,
      -1,    -1,    91,    27,    -1,    29,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    37,    -1,    -1,    40,    -1,    -1,    43,
      -1,    -1,    -1,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    -1,    59,    60,    -1,    62,    63,
      64,    65,    66,    67,    68,     4,    -1,    -1,     7,    -1,
      -1,    10,    11,    12,    -1,    -1,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    -1,    -1,    91,    27,    -1,
      29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    -1,
      -1,    40,    -1,    -1,    43,    -1,    -1,    -1,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    -1,
      59,    60,    -1,    62,    63,    64,    65,    66,    67,    68,
       4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,    -1,
      -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,
      -1,    -1,    91,    27,    -1,    29,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    37,    -1,    -1,    40,    -1,    -1,    43,
      -1,    -1,    -1,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    -1,    59,    60,    -1,    62,    63,
      64,    65,    66,    67,    68,     4,    -1,    -1,     7,    -1,
      -1,    10,    11,    12,    -1,    -1,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    -1,    -1,    91,    27,    -1,
      29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    -1,
      -1,    40,    -1,    -1,    43,    -1,    -1,    -1,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    -1,
      59,    60,    -1,    62,    63,    64,    65,    66,    67,    68,
       4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,    -1,
      -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,
      -1,    -1,    91,    27,    -1,    29,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    37,    -1,    -1,    40,    -1,    -1,    43,
      -1,    -1,    -1,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    -1,    59,    60,    -1,    62,    63,
      64,    65,    66,    67,    68,     4,    -1,    -1,     7,    -1,
      -1,    10,    11,    12,    -1,    -1,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    -1,    -1,    91,    27,    -1,
      29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    -1,
      -1,    40,    -1,    -1,    43,    -1,    -1,    -1,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    -1,
      59,    60,    -1,    62,    63,    64,    65,    66,    67,    68,
       4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,    -1,
      -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,
      -1,    -1,    91,    27,    -1,    29,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    37,    -1,    -1,    40,    -1,    -1,    43,
      -1,    -1,    -1,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    -1,    59,    60,    -1,    62,    63,
      64,    65,    66,    67,    68,     4,    -1,    -1,     7,    -1,
      -1,    10,    11,    12,    -1,    -1,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    -1,    -1,    91,    27,    -1,
      29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    -1,
      -1,    40,    -1,    -1,    43,    -1,    -1,    -1,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    -1,
      59,    60,    -1,    62,    63,    64,    65,    66,    67,    68,
       4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,    -1,
      -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,
      -1,    -1,    91,    27,    -1,    29,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    37,    -1,    -1,    40,    -1,    -1,    43,
      -1,    -1,    -1,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    -1,    59,    60,    -1,    62,    63,
      64,    65,    66,    67,    68,     4,    -1,    -1,     7,    -1,
      -1,    10,    11,    12,    -1,    -1,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    -1,    -1,    91,    27,    -1,
      29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    -1,
      -1,    40,    -1,    -1,    43,    -1,    -1,    -1,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    -1,
      59,    60,    -1,    62,    63,    64,    65,    66,    67,    68,
       4,    -1,    -1,     7,    -1,    -1,    10,    11,    12,    -1,
      -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,    -1,
      -1,    -1,    91,    27,    -1,    29,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    37,    -1,    -1,    40,    -1,    -1,    43,
      -1,    -1,    -1,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    -1,    59,    60,    -1,    62,    63,
      64,    65,    66,    67,    68,     4,    -1,    -1,     7,    -1,
      -1,    10,    -1,    -1,    -1,    -1,    15,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    -1,    -1,    -1,    91,    27,    -1,
      29,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    -1,
      -1,    40,    -1,    -1,    43,    -1,    -1,    -1,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,     4,
      59,    60,     7,    62,    63,    64,    65,    66,    67,    68,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    22,     4,    -1,
      -1,     7,    27,    -1,    29,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    91,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    27,    -1,    29,    49,    50,    -1,    52,    53,    54,
      -1,    -1,    -1,    -1,    59,    60,    -1,    62,    -1,    -1,
      -1,    66,    67,    49,    50,    -1,    52,    53,    54,     4,
      -1,    -1,     7,    59,    60,    -1,    62,    -1,    -1,    -1,
      66,    67,    -1,    -1,    -1,    -1,    91,    -1,    -1,    -1,
      -1,    -1,    27,    -1,    29,    69,    70,    71,    72,    73,
      74,    75,    76,    77,    78,    91,     3,     4,     5,    -1,
       7,     8,     9,    -1,    49,    50,    -1,    52,    53,    54,
      -1,    -1,    -1,    -1,    59,    60,    -1,    62,    -1,    -1,
      27,    66,    67,    30,    31,    32,    33,    -1,    -1,    36,
      -1,    38,    39,    -1,    41,     3,     4,     5,    -1,     7,
       8,     9,    -1,    -1,    -1,    -1,    91,    -1,    -1,    -1,
      -1,    58,    -1,    60,    -1,    62,    -1,    -1,    -1,    27,
      -1,    -1,    30,    31,    32,    33,    -1,    -1,    36,    -1,
      38,    39,    -1,    80,    -1,    -1,    83,    -1,    85,    -1,
      87,     3,     4,     5,    -1,     7,    -1,     9,    -1,    -1,
      58,    -1,    60,    -1,    62,    -1,     3,     4,     5,    -1,
       7,     8,     9,    -1,    -1,    27,    -1,    -1,    30,    31,
      32,    33,    80,    -1,    -1,    83,    -1,    85,    -1,    87,
      27,    -1,    -1,    30,    31,    32,    33,    -1,    -1,    -1,
      -1,    38,    39,    -1,    -1,    -1,    58,    -1,    60,    -1,
      62,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    58,    -1,    60,    -1,    62,    -1,    -1,    -1,    -1,
      -1,    83,    -1,    85,    -1,    87,    -1,    -1,    -1,    -1,
      -1,     4,     5,    80,    -1,    -1,    83,    -1,    85,    -1,
      87,    14,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,
      -1,    24,    25,    -1,    27,    -1,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,     4,     5,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    14,    15,    -1,    -1,
      -1,    84,    -1,    -1,    22,    -1,    24,    25,    -1,    27,
      -1,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,     4,     5,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    14,    15,    -1,    -1,    -1,    84,    -1,    -1,    22,
      -1,    24,    25,    -1,    27,    -1,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,     4,     5,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    14,    15,    -1,    -1,
      -1,    84,    -1,    -1,    22,    -1,    24,    25,    -1,    27,
      -1,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,     4,     5,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    14,    15,    -1,    -1,    -1,    84,    -1,    -1,    22,
      -1,    24,    25,    -1,    27,    -1,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,     4,     5,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    14,    15,    -1,    -1,
      -1,    84,    -1,    -1,    22,    -1,    24,    25,    -1,    27,
      -1,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,     4,    -1,    -1,     7,    -1,    -1,    -1,    -1,    -1,
      -1,    14,    15,    -1,    -1,    -1,    84,    -1,    -1,    22,
      -1,    24,    25,    -1,    27,    -1,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,     4,    -1,    -1,     7,
      -1,    -1,    -1,    -1,    -1,    -1,    14,    15,    -1,    -1,
      -1,    -1,    -1,    -1,    22,    -1,    24,    25,    -1,    27,
      -1,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,     4,    -1,    -1,     7,    -1,    -1,    -1,    -1,    -1,
      -1,    14,    15,    -1,    -1,    -1,    -1,    -1,    -1,    22,
      -1,    24,    25,    -1,    27,    -1,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    96,    97,     0,     4,     7,    10,    11,    12,    15,
      22,    27,    29,    37,    40,    43,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    59,    60,    62,
      63,    64,    65,    66,    67,    68,    91,    98,    99,   102,
     103,   108,   109,   110,   112,   113,   114,   115,   120,   121,
     122,   123,   124,   127,   128,   130,   133,   134,   141,   142,
     143,   144,   145,   146,   147,   148,   149,   150,     4,    83,
      83,     4,     4,     4,     3,     4,     5,     7,     8,     9,
      30,    31,    32,    33,    36,    38,    39,    58,    62,    80,
      83,    85,    87,   103,   154,   155,   156,   157,   158,   159,
     160,   161,   162,   163,   165,   166,   169,    41,   154,     4,
       4,    46,    91,   154,   154,     4,     4,     4,     7,   154,
     103,   103,     4,    83,   103,   129,   140,   154,    58,     4,
      62,    94,   154,     4,    27,    41,    60,   106,   107,   125,
       4,     4,     4,     5,     4,     5,    63,    91,     8,    16,
      17,    18,    19,    69,    85,    87,    94,   101,   104,    90,
      91,    91,    91,    91,    91,    91,    91,    91,    91,    91,
      91,    91,    83,   154,   170,   171,   170,    83,    83,    59,
       4,    83,   162,   157,   163,     4,    83,   162,   154,   180,
     180,    83,    94,   167,    24,    35,    34,    20,    21,    69,
      70,    71,    72,    73,    74,    75,    76,    77,    78,   104,
     164,    79,    80,    81,    82,    85,    94,     3,    62,    83,
      45,    69,    89,     4,    97,    91,    91,    83,    83,    83,
     129,    89,    91,    94,    91,     4,    29,    53,    61,    53,
      61,     4,    83,   107,    40,    83,    91,     4,    14,     4,
     125,   154,   154,   105,     4,     7,    14,    15,    22,    24,
      25,    27,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,   177,   154,    69,   101,     4,   100,     4,     5,
      84,   176,   177,   178,    84,    89,    84,    84,   178,    84,
     178,     4,   170,    37,    83,    84,    84,    86,    91,   154,
     172,    88,   178,   170,     4,     7,   177,     4,    91,    99,
     103,   108,   109,   124,   142,   143,   144,   146,   147,   148,
     149,   153,   156,   157,   160,   160,   164,   159,   161,   161,
     162,   162,   154,     4,     7,   177,     4,   154,   154,   154,
     154,     4,    45,    89,    44,    97,    23,   116,     4,   173,
     175,   173,   129,    84,   140,    97,     4,    97,     4,    60,
       4,    60,    83,   173,     4,   126,   173,    97,     5,     4,
       5,    40,    86,     6,    83,    83,   154,   154,    83,    69,
      83,    91,    69,    90,    84,    89,   154,    91,    84,    91,
      84,    91,    84,   166,   170,    89,   180,   180,    84,    83,
      83,   168,    97,    90,    91,   159,    86,    83,    83,    84,
      91,    41,    45,   154,     4,   154,    27,   154,    23,    26,
     117,    69,    90,    84,    89,    84,    84,    91,    27,    27,
     170,    84,    91,    84,    27,    14,   126,    88,   170,   170,
       4,   154,     4,   179,   132,   154,   154,    91,   180,   135,
      41,    91,    91,    13,    84,   180,    86,    88,   170,   170,
      25,    27,   151,    25,   152,   170,   170,    91,    97,   154,
     154,    91,    45,    91,    47,    24,   154,    91,    28,     3,
       5,    30,    31,    32,    33,    79,    80,   174,   100,    90,
      91,     4,    90,    91,    91,    97,    55,    57,    84,    97,
      91,    65,     4,    91,    84,    84,    84,   154,    84,   137,
     131,     4,   176,     4,    27,    59,    91,   136,     4,   135,
      27,   154,    84,    84,    22,    91,   153,    22,    22,    91,
     153,    84,    84,    97,    27,    60,   111,    42,    91,    91,
      97,   154,    91,    91,    24,   118,    91,     3,     3,    69,
     100,    97,    69,    90,   100,    97,    97,    27,    91,    91,
      27,    97,    91,    97,    90,     4,    27,    59,    91,   138,
     137,    83,    69,    90,     4,     5,    41,    83,    91,     4,
       4,     4,    27,     4,   154,    97,    91,    91,   154,    97,
      91,    27,    40,    91,   103,   154,    97,    97,   111,    91,
     118,    91,    15,    91,    99,   108,   109,   110,   112,   113,
     114,   115,   119,   120,   121,   122,   123,   124,   127,   128,
     134,   141,   142,   143,   144,   145,   146,   147,   148,   149,
     150,   174,    91,    27,   174,   100,    91,    27,    27,    55,
      63,    27,    27,   154,     4,     5,   139,     4,     4,    27,
     179,   154,   154,     5,    41,    83,    91,    91,     4,    84,
     178,   135,    91,    94,    91,     4,    91,    24,    27,    24,
      27,    37,    91,    91,    91,   111,   111,    97,   118,    91,
      91,    91,    91,    91,    91,    91,    91,    91,    91,    91,
      91,    91,    97,    51,    69,    97,    51,    55,    91,    91,
      64,    63,    83,    83,    89,    91,    91,    91,     4,    84,
      91,     4,    84,   178,   135,   135,     4,    91,    84,    27,
       4,   135,    91,    91,    22,    91,   153,    22,    91,    97,
     111,    27,    91,   174,    27,    91,    91,    91,    91,    84,
     178,   173,     5,    97,    91,    90,   135,     4,    91,    84,
      27,    27,    91,    91,     4,    91,    27,    97,    91,    97,
      91,    91,   111,    51,    51,    91,    84,    84,    27,   154,
      27,    91,    91,     4,     4,   135,   135,    91,    97,     4,
     151,   151,   152,    91,    91,   137,    91,    91,    59,     4,
     135,    91,    91,    27,    27,    27,    91,    27,   137,    97,
      91,    91,    27,     4,     4,    59,     4,    27,    27,     4,
      91,    91,    91,    91,     4,     4,    91,    91,    91
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    95,    96,    97,    97,    97,    98,    98,    98,    98,
      98,    98,    98,    98,    98,    98,    98,    98,    98,    98,
      98,    98,    98,    98,    98,    98,    98,    98,    98,    98,
      98,    98,    98,    98,    98,    99,    99,    99,    99,    99,
     100,   100,   101,   101,   101,   101,   102,   102,   102,   103,
     103,   103,   105,   104,   104,   106,   106,   107,   107,   107,
     107,   108,   108,   109,   109,   109,   109,   109,   110,   111,
     111,   111,   112,   112,   112,   112,   112,   112,   113,   114,
     115,   116,   116,   117,   117,   118,   118,   118,   119,   119,
     119,   119,   119,   119,   119,   119,   119,   119,   119,   119,
     119,   119,   119,   119,   119,   119,   119,   119,   119,   119,
     119,   119,   119,   119,   119,   120,   120,   120,   120,   121,
     121,   122,   123,   124,   124,   124,   124,   124,   124,   124,
     124,   125,   125,   126,   127,   127,   127,   128,   129,   129,
     131,   130,   132,   130,   133,   133,   133,   134,   134,   134,
     135,   135,   135,   136,   136,   136,   136,   136,   136,   136,
     136,   136,   136,   136,   137,   137,   137,   138,   138,   138,
     138,   138,   139,   139,   140,   140,   141,   142,   142,   142,
     142,   142,   142,   142,   142,   143,   144,   144,   145,   146,
     147,   148,   148,   149,   149,   150,   150,   151,   151,   151,
     151,   152,   152,   152,   152,   152,   153,   153,   153,   153,
     153,   153,   153,   153,   153,   153,   153,   154,   155,   155,
     156,   156,   157,   157,   158,   158,   158,   159,   159,   159,
     160,   160,   160,   161,   161,   161,   162,   162,   162,   162,
     162,   162,   163,   163,   163,   163,   163,   164,   164,   164,
     164,   164,   164,   164,   164,   164,   164,   165,   165,   165,
     165,   165,   165,   165,   165,   165,   165,   165,   165,   165,
     165,   165,   165,   166,   166,   167,   167,   167,   167,   168,
     168,   169,   169,   170,   170,   171,   171,   172,   172,   173,
     173,   174,   174,   174,   174,   174,   174,   174,   174,   175,
     175,   175,   175,   175,   175,   175,   175,   176,   176,   177,
     177,   177,   177,   177,   177,   177,   177,   177,   177,   177,
     177,   177,   177,   177,   177,   177,   177,   177,   177,   177,
     177,   177,   177,   177,   177,   177,   177,   177,   177,   177,
     177,   177,   177,   177,   177,   177,   177,   177,   177,   177,
     177,   177,   177,   177,   177,   177,   178,   178,   178,   178,
     178,   178,   179,   179,   180,   180
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
       1,     2,     4,     4,     4,     6,     6,     6,    10,     3,
       2,     3,     7,     8,     9,    10,     9,    11,     6,     7,
       7,     5,     6,     0,     3,     0,     2,     2,     2,     2,
       2,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     2,     1,     2,     1,     2,     2,     2,     2,     2,
       2,     2,     2,     1,     1,    10,    10,    12,    12,     9,
      10,    10,     7,     2,     2,     2,     2,     4,     4,     4,
       6,     1,     4,     1,     9,     7,    10,     2,     1,     3,
       0,    11,     0,    10,     6,     5,     9,    10,     9,    13,
       0,     2,     2,     4,     6,     5,     6,     9,     7,     8,
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
       3,     3,     3,     5,     5,     7,     5,     1,     1,     1,
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
#line 862 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3142 "src/parser.tab.c"
        break;

    case YYSYMBOL_STRING: /* STRING  */
#line 862 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3148 "src/parser.tab.c"
        break;

    case YYSYMBOL_LENS_CONTENT: /* LENS_CONTENT  */
#line 862 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3154 "src/parser.tab.c"
        break;

    case YYSYMBOL_QUALIFIED_IDENT: /* QUALIFIED_IDENT  */
#line 862 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3160 "src/parser.tab.c"
        break;

    case YYSYMBOL_MODIFIER_PREFIX: /* MODIFIER_PREFIX  */
#line 862 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3166 "src/parser.tab.c"
        break;

    case YYSYMBOL_MODIFIER_VALUE: /* MODIFIER_VALUE  */
#line 862 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3172 "src/parser.tab.c"
        break;

    case YYSYMBOL_SQL_TEXT: /* SQL_TEXT  */
#line 862 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3178 "src/parser.tab.c"
        break;

    case YYSYMBOL_program: /* program  */
#line 886 "src/parser.y"
            { (void) ((*yyvaluep).stmt_list); }
#line 3184 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement_list: /* statement_list  */
#line 823 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3190 "src/parser.tab.c"
        break;

    case YYSYMBOL_statement: /* statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3196 "src/parser.tab.c"
        break;

    case YYSYMBOL_assignment: /* assignment  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3202 "src/parser.tab.c"
        break;

    case YYSYMBOL_type_lock: /* type_lock  */
#line 876 "src/parser.y"
            { ast_type_lock_free(((*yyvaluep).type_lock)); }
#line 3208 "src/parser.tab.c"
        break;

    case YYSYMBOL_lvalue: /* lvalue  */
#line 863 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3214 "src/parser.tab.c"
        break;

    case YYSYMBOL_variable_name: /* variable_name  */
#line 862 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3220 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_lens: /* comparison_lens  */
#line 870 "src/parser.y"
            { ast_free_modifier_use(((*yyvaluep).modifier)); }
#line 3226 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_name: /* modifier_name  */
#line 862 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3232 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_word: /* modifier_word  */
#line 862 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3238 "src/parser.tab.c"
        break;

    case YYSYMBOL_print_statement: /* print_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3244 "src/parser.tab.c"
        break;

    case YYSYMBOL_call_statement: /* call_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3250 "src/parser.tab.c"
        break;

    case YYSYMBOL_with_lock_statement: /* with_lock_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3256 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_end: /* for_end  */
#line 862 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3262 "src/parser.tab.c"
        break;

    case YYSYMBOL_for_each_statement: /* for_each_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3268 "src/parser.tab.c"
        break;

    case YYSYMBOL_do_loop_statement: /* do_loop_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3274 "src/parser.tab.c"
        break;

    case YYSYMBOL_while_statement: /* while_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3280 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement: /* consider_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3286 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_branch_list: /* consider_branch_list  */
#line 868 "src/parser.y"
            { ast_free_consider_branch_list(((*yyvaluep).consider_branch_list)); }
#line 3292 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_else_opt: /* consider_else_opt  */
#line 823 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3298 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_statement_list: /* consider_statement_list  */
#line 823 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3304 "src/parser.tab.c"
        break;

    case YYSYMBOL_consider_body_statement: /* consider_body_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3310 "src/parser.tab.c"
        break;

    case YYSYMBOL_function_statement: /* function_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3316 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_statement: /* modifier_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3322 "src/parser.tab.c"
        break;

    case YYSYMBOL_program_statement: /* program_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3328 "src/parser.tab.c"
        break;

    case YYSYMBOL_library_statement: /* library_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3334 "src/parser.tab.c"
        break;

    case YYSYMBOL_use_statement: /* use_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3340 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_signature: /* modifier_signature  */
#line 871 "src/parser.y"
            { ast_free_modifier_signature(((*yyvaluep).modifier_signature)); }
#line 3346 "src/parser.tab.c"
        break;

    case YYSYMBOL_modifier_context: /* modifier_context  */
#line 862 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3352 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_statement: /* watch_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3358 "src/parser.tab.c"
        break;

    case YYSYMBOL_unwatch_statement: /* unwatch_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3364 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_list: /* watch_target_list  */
#line 869 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3370 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_statement: /* server_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3376 "src/parser.tab.c"
        break;

    case YYSYMBOL_gsql_block: /* gsql_block  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3382 "src/parser.tab.c"
        break;

    case YYSYMBOL_window_block: /* window_block  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3388 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item_list: /* server_item_list  */
#line 875 "src/parser.y"
            { ast_free_server_item_list(((*yyvaluep).server_item_list)); }
#line 3394 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_item: /* server_item  */
#line 874 "src/parser.y"
            { AstServerItemList one = ast_server_item_list_append(ast_server_item_list_empty(), ((*yyvaluep).server_item)); ast_free_server_item_list(one); }
#line 3400 "src/parser.tab.c"
        break;

    case YYSYMBOL_server_string_list: /* server_string_list  */
#line 869 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3406 "src/parser.tab.c"
        break;

    case YYSYMBOL_watch_target_path: /* watch_target_path  */
#line 862 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3412 "src/parser.tab.c"
        break;

    case YYSYMBOL_without_watchers_statement: /* without_watchers_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3418 "src/parser.tab.c"
        break;

    case YYSYMBOL_on_error_statement: /* on_error_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3424 "src/parser.tab.c"
        break;

    case YYSYMBOL_error_statement: /* error_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3430 "src/parser.tab.c"
        break;

    case YYSYMBOL_return_statement: /* return_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3436 "src/parser.tab.c"
        break;

    case YYSYMBOL_label_statement: /* label_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3442 "src/parser.tab.c"
        break;

    case YYSYMBOL_goto_statement: /* goto_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3448 "src/parser.tab.c"
        break;

    case YYSYMBOL_gosub_statement: /* gosub_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3454 "src/parser.tab.c"
        break;

    case YYSYMBOL_break_statement: /* break_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3460 "src/parser.tab.c"
        break;

    case YYSYMBOL_continue_statement: /* continue_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3466 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_statement: /* if_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3472 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_block_tail: /* if_block_tail  */
#line 823 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3478 "src/parser.tab.c"
        break;

    case YYSYMBOL_if_inline_tail: /* if_inline_tail  */
#line 823 "src/parser.y"
            { ast_free_program(((*yyvaluep).stmt_list)); }
#line 3484 "src/parser.tab.c"
        break;

    case YYSYMBOL_inline_statement: /* inline_statement  */
#line 864 "src/parser.y"
            { ast_free_stmt(((*yyvaluep).stmt)); }
#line 3490 "src/parser.tab.c"
        break;

    case YYSYMBOL_expression: /* expression  */
#line 863 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3496 "src/parser.tab.c"
        break;

    case YYSYMBOL_or_expression: /* or_expression  */
#line 863 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3502 "src/parser.tab.c"
        break;

    case YYSYMBOL_and_expression: /* and_expression  */
#line 863 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3508 "src/parser.tab.c"
        break;

    case YYSYMBOL_not_expression: /* not_expression  */
#line 863 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3514 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_expression: /* comparison_expression  */
#line 863 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3520 "src/parser.tab.c"
        break;

    case YYSYMBOL_set_expression: /* set_expression  */
#line 863 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3526 "src/parser.tab.c"
        break;

    case YYSYMBOL_additive_expression: /* additive_expression  */
#line 863 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3532 "src/parser.tab.c"
        break;

    case YYSYMBOL_multiplicative_expression: /* multiplicative_expression  */
#line 863 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3538 "src/parser.tab.c"
        break;

    case YYSYMBOL_unary_expression: /* unary_expression  */
#line 863 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3544 "src/parser.tab.c"
        break;

    case YYSYMBOL_postfix_expression: /* postfix_expression  */
#line 863 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3550 "src/parser.tab.c"
        break;

    case YYSYMBOL_comparison_operator: /* comparison_operator  */
#line 862 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3556 "src/parser.tab.c"
        break;

    case YYSYMBOL_primary: /* primary  */
#line 863 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3562 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_literal: /* record_literal  */
#line 863 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3568 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_suffix: /* ident_suffix  */
#line 872 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3574 "src/parser.tab.c"
        break;

    case YYSYMBOL_ident_dot_suffix: /* ident_dot_suffix  */
#line 872 "src/parser.y"
            { free(((*yyvaluep).ident_suffix).name); ast_free_expr_list(((*yyvaluep).ident_suffix).args); }
#line 3580 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list_opt: /* argument_list_opt  */
#line 866 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3586 "src/parser.tab.c"
        break;

    case YYSYMBOL_argument_list: /* argument_list  */
#line 866 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3592 "src/parser.tab.c"
        break;

    case YYSYMBOL_array_argument_list: /* array_argument_list  */
#line 866 "src/parser.y"
            { ast_free_expr_list(((*yyvaluep).expr_list)); }
#line 3598 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list_opt: /* parameter_list_opt  */
#line 869 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3604 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_default: /* parameter_default  */
#line 863 "src/parser.y"
            { ast_free_expr(((*yyvaluep).expr)); }
#line 3610 "src/parser.tab.c"
        break;

    case YYSYMBOL_parameter_list: /* parameter_list  */
#line 869 "src/parser.y"
            { ast_free_name_list(((*yyvaluep).name_list)); }
#line 3616 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_name: /* field_name  */
#line 862 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3622 "src/parser.tab.c"
        break;

    case YYSYMBOL_dot_field_name: /* dot_field_name  */
#line 862 "src/parser.y"
            { free(((*yyvaluep).text)); }
#line 3628 "src/parser.tab.c"
        break;

    case YYSYMBOL_record_field_list: /* record_field_list  */
#line 867 "src/parser.y"
            { ast_free_record_field_list(((*yyvaluep).record_field_list)); }
#line 3634 "src/parser.tab.c"
        break;

    case YYSYMBOL_field_policy: /* field_policy  */
#line 873 "src/parser.y"
            { ast_free_expr(((*yyvaluep).field_policy).reset_expr); }
#line 3640 "src/parser.tab.c"
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
#line 891 "src/parser.y"
                     { ctx->parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 3946 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 895 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 3952 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 896 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 3958 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 897 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 3964 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 901 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3970 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 902 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3976 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 903 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 3982 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 904 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3988 "src/parser.tab.c"
    break;

  case 10: /* statement: window_block  */
#line 905 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 3994 "src/parser.tab.c"
    break;

  case 11: /* statement: gsql_block  */
#line 906 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4000 "src/parser.tab.c"
    break;

  case 12: /* statement: for_each_statement  */
#line 907 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4006 "src/parser.tab.c"
    break;

  case 13: /* statement: while_statement  */
#line 908 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4012 "src/parser.tab.c"
    break;

  case 14: /* statement: do_loop_statement  */
#line 909 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4018 "src/parser.tab.c"
    break;

  case 15: /* statement: consider_statement  */
#line 910 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4024 "src/parser.tab.c"
    break;

  case 16: /* statement: function_statement  */
#line 911 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4030 "src/parser.tab.c"
    break;

  case 17: /* statement: modifier_statement  */
#line 912 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4036 "src/parser.tab.c"
    break;

  case 18: /* statement: program_statement  */
#line 913 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4042 "src/parser.tab.c"
    break;

  case 19: /* statement: library_statement  */
#line 914 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4048 "src/parser.tab.c"
    break;

  case 20: /* statement: use_statement NEWLINE  */
#line 915 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4054 "src/parser.tab.c"
    break;

  case 21: /* statement: watch_statement  */
#line 916 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4060 "src/parser.tab.c"
    break;

  case 22: /* statement: server_statement  */
#line 917 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4066 "src/parser.tab.c"
    break;

  case 23: /* statement: unwatch_statement NEWLINE  */
#line 918 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4072 "src/parser.tab.c"
    break;

  case 24: /* statement: without_watchers_statement  */
#line 919 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4078 "src/parser.tab.c"
    break;

  case 25: /* statement: on_error_statement NEWLINE  */
#line 920 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4084 "src/parser.tab.c"
    break;

  case 26: /* statement: error_statement NEWLINE  */
#line 921 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4090 "src/parser.tab.c"
    break;

  case 27: /* statement: return_statement NEWLINE  */
#line 922 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4096 "src/parser.tab.c"
    break;

  case 28: /* statement: label_statement NEWLINE  */
#line 923 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4102 "src/parser.tab.c"
    break;

  case 29: /* statement: goto_statement NEWLINE  */
#line 924 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4108 "src/parser.tab.c"
    break;

  case 30: /* statement: gosub_statement NEWLINE  */
#line 925 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4114 "src/parser.tab.c"
    break;

  case 31: /* statement: break_statement NEWLINE  */
#line 926 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4120 "src/parser.tab.c"
    break;

  case 32: /* statement: continue_statement NEWLINE  */
#line 927 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4126 "src/parser.tab.c"
    break;

  case 33: /* statement: if_statement  */
#line 928 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4132 "src/parser.tab.c"
    break;

  case 34: /* statement: DIM  */
#line 934 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 4145 "src/parser.tab.c"
    break;

  case 35: /* assignment: lvalue OP_EQ expression  */
#line 945 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 4151 "src/parser.tab.c"
    break;

  case 36: /* assignment: lvalue compound_op expression  */
#line 950 "src/parser.y"
                                    { (yyval.stmt) = ast_assign_op((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr), (yyvsp[-1].op_char)); }
#line 4157 "src/parser.tab.c"
    break;

  case 37: /* assignment: lvalue comparison_lens compound_op expression  */
#line 951 "src/parser.y"
                                                    {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign_op((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr), (yyvsp[-1].op_char));
      }
#line 4171 "src/parser.tab.c"
    break;

  case 38: /* assignment: variable_name COLON type_lock OP_EQ expression  */
#line 967 "src/parser.y"
                                                     {
        if (!type_lock_check(ctx, (yyvsp[-4].text), (yyvsp[-2].type_lock).name, (yyvsp[-2].type_lock).arg, (yylsp[-2]).first_line, (yylsp[-2]).first_column,
                             (yylsp[-2]).last_line, (yylsp[-2]).last_column)) {
            YYERROR;
        }
        (yyval.stmt) = ast_assign_locked(expr_at(ast_ident((yyvsp[-4].text)), (yylsp[-4]).first_line, (yylsp[-4]).first_column), (yyvsp[-2].type_lock), (yyvsp[0].expr));
      }
#line 4183 "src/parser.tab.c"
    break;

  case 39: /* assignment: lvalue comparison_lens OP_EQ expression  */
#line 974 "src/parser.y"
                                              {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 4197 "src/parser.tab.c"
    break;

  case 40: /* type_lock: IDENT  */
#line 988 "src/parser.y"
            { (yyval.type_lock) = ast_type_lock((yyvsp[0].text), NULL, (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4203 "src/parser.tab.c"
    break;

  case 41: /* type_lock: IDENT LPAREN IDENT RPAREN  */
#line 989 "src/parser.y"
                                { (yyval.type_lock) = ast_type_lock((yyvsp[-3].text), (yyvsp[-1].text), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 4209 "src/parser.tab.c"
    break;

  case 42: /* compound_op: PLUS_EQ  */
#line 993 "src/parser.y"
               { (yyval.op_char) = '+'; }
#line 4215 "src/parser.tab.c"
    break;

  case 43: /* compound_op: MINUS_EQ  */
#line 994 "src/parser.y"
               { (yyval.op_char) = '-'; }
#line 4221 "src/parser.tab.c"
    break;

  case 44: /* compound_op: STAR_EQ  */
#line 995 "src/parser.y"
               { (yyval.op_char) = '*'; }
#line 4227 "src/parser.tab.c"
    break;

  case 45: /* compound_op: SLASH_EQ  */
#line 996 "src/parser.y"
               { (yyval.op_char) = '/'; }
#line 4233 "src/parser.tab.c"
    break;

  case 46: /* lvalue: variable_name  */
#line 1000 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 4239 "src/parser.tab.c"
    break;

  case 47: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 1001 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 4245 "src/parser.tab.c"
    break;

  case 48: /* lvalue: lvalue DOT dot_field_name  */
#line 1002 "src/parser.y"
                                             { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 4251 "src/parser.tab.c"
    break;

  case 49: /* variable_name: IDENT  */
#line 1006 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 4257 "src/parser.tab.c"
    break;

  case 50: /* variable_name: END  */
#line 1007 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 4263 "src/parser.tab.c"
    break;

  case 51: /* variable_name: NEXT  */
#line 1008 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 4269 "src/parser.tab.c"
    break;

  case 52: /* $@1: %empty  */
#line 1019 "src/parser.y"
             { lexer_begin_lens_content(ctx->active_lexer); }
#line 4275 "src/parser.tab.c"
    break;

  case 53: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 1019 "src/parser.y"
                                                                                  {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 4283 "src/parser.tab.c"
    break;

  case 54: /* comparison_lens: MODIFIER_PREFIX  */
#line 1027 "src/parser.y"
                      { (yyval.modifier) = parse_modifier_use((yyvsp[0].text)); }
#line 4289 "src/parser.tab.c"
    break;

  case 55: /* modifier_name: modifier_word  */
#line 1031 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 4295 "src/parser.tab.c"
    break;

  case 56: /* modifier_name: modifier_name modifier_word  */
#line 1032 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 4301 "src/parser.tab.c"
    break;

  case 57: /* modifier_word: IDENT  */
#line 1036 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4307 "src/parser.tab.c"
    break;

  case 58: /* modifier_word: TO  */
#line 1037 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 4313 "src/parser.tab.c"
    break;

  case 59: /* modifier_word: END  */
#line 1038 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 4319 "src/parser.tab.c"
    break;

  case 60: /* modifier_word: NEXT  */
#line 1039 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 4325 "src/parser.tab.c"
    break;

  case 61: /* print_statement: PRINT expression  */
#line 1043 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 4331 "src/parser.tab.c"
    break;

  case 62: /* print_statement: PRINT TO ERROR_VALUE expression  */
#line 1049 "src/parser.y"
                                      { (yyval.stmt) = ast_print_error((yyvsp[0].expr)); }
#line 4337 "src/parser.tab.c"
    break;

  case 63: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 1053 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 4343 "src/parser.tab.c"
    break;

  case 64: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1054 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 4354 "src/parser.tab.c"
    break;

  case 65: /* call_statement: lvalue DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1060 "src/parser.y"
                                                       {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 4364 "src/parser.tab.c"
    break;

  case 66: /* call_statement: lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1065 "src/parser.y"
                                                                 {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident((yyvsp[-3].text), &field, &method);
        AstExpr *recv = expr_at(ast_field((yyvsp[-5].expr), field), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
        (yyval.stmt) = ast_expr_stmt(expr_at(ast_method_call(recv, method, (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column));
      }
#line 4378 "src/parser.tab.c"
    break;

  case 67: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1074 "src/parser.y"
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
#line 4393 "src/parser.tab.c"
    break;

  case 68: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 1087 "src/parser.y"
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
#line 4416 "src/parser.tab.c"
    break;

  case 69: /* for_end: END FOR NEWLINE  */
#line 1116 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 4422 "src/parser.tab.c"
    break;

  case 70: /* for_end: NEXT NEWLINE  */
#line 1117 "src/parser.y"
                                 { (yyval.text) = NULL; }
#line 4428 "src/parser.tab.c"
    break;

  case 71: /* for_end: NEXT variable_name NEWLINE  */
#line 1118 "src/parser.y"
                                 { (yyval.text) = (yyvsp[-1].text); }
#line 4434 "src/parser.tab.c"
    break;

  case 72: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list for_end  */
#line 1122 "src/parser.y"
                                                             {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4443 "src/parser.tab.c"
    break;

  case 73: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list for_end  */
#line 1126 "src/parser.y"
                                                                  {
        if (!for_end_matches(ctx, (yyvsp[-5].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-5].text), NULL, (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4452 "src/parser.tab.c"
    break;

  case 74: /* for_each_statement: FOR IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1142 "src/parser.y"
                                                                         {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { YYERROR; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4462 "src/parser.tab.c"
    break;

  case 75: /* for_each_statement: FOR EACH IDENT COMMA IDENT IN expression NEWLINE statement_list for_end  */
#line 1147 "src/parser.y"
                                                                              {
        if (!for_each_index_distinct(ctx, (yyvsp[-7].text), (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column)) { YYERROR; }
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4472 "src/parser.tab.c"
    break;

  case 76: /* for_each_statement: FOR IDENT OP_EQ expression TO expression NEWLINE statement_list for_end  */
#line 1155 "src/parser.y"
                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-7].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].expr), NULL, (yyvsp[-1].stmt_list));
      }
#line 4481 "src/parser.tab.c"
    break;

  case 77: /* for_each_statement: FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list for_end  */
#line 1159 "src/parser.y"
                                                                                              {
        if (!for_end_matches(ctx, (yyvsp[-9].text), (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column)) { YYERROR; }
        (yyval.stmt) = ast_for_range((yyvsp[-9].text), (yyvsp[-7].expr), (yyvsp[-5].expr), (yyvsp[-3].expr), (yyvsp[-1].stmt_list));
      }
#line 4490 "src/parser.tab.c"
    break;

  case 78: /* do_loop_statement: DO NEWLINE statement_list UNTIL expression NEWLINE  */
#line 1180 "src/parser.y"
                                                         {
        (yyval.stmt) = ast_do_loop((yyvsp[-3].stmt_list), (yyvsp[-1].expr));
      }
#line 4498 "src/parser.tab.c"
    break;

  case 79: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 1186 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 4506 "src/parser.tab.c"
    break;

  case 80: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 1192 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 4514 "src/parser.tab.c"
    break;

  case 81: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 1198 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 4522 "src/parser.tab.c"
    break;

  case 82: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 1201 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 4530 "src/parser.tab.c"
    break;

  case 83: /* consider_else_opt: %empty  */
#line 1207 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4536 "src/parser.tab.c"
    break;

  case 84: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 1208 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 4542 "src/parser.tab.c"
    break;

  case 85: /* consider_statement_list: %empty  */
#line 1212 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 4548 "src/parser.tab.c"
    break;

  case 86: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 1213 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 4554 "src/parser.tab.c"
    break;

  case 87: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 1214 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 4560 "src/parser.tab.c"
    break;

  case 88: /* consider_body_statement: assignment NEWLINE  */
#line 1218 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4566 "src/parser.tab.c"
    break;

  case 89: /* consider_body_statement: print_statement NEWLINE  */
#line 1219 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4572 "src/parser.tab.c"
    break;

  case 90: /* consider_body_statement: call_statement NEWLINE  */
#line 1220 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4578 "src/parser.tab.c"
    break;

  case 91: /* consider_body_statement: with_lock_statement  */
#line 1221 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4584 "src/parser.tab.c"
    break;

  case 92: /* consider_body_statement: window_block  */
#line 1222 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4590 "src/parser.tab.c"
    break;

  case 93: /* consider_body_statement: for_each_statement  */
#line 1223 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4596 "src/parser.tab.c"
    break;

  case 94: /* consider_body_statement: while_statement  */
#line 1224 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4602 "src/parser.tab.c"
    break;

  case 95: /* consider_body_statement: do_loop_statement  */
#line 1225 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4608 "src/parser.tab.c"
    break;

  case 96: /* consider_body_statement: consider_statement  */
#line 1226 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4614 "src/parser.tab.c"
    break;

  case 97: /* consider_body_statement: function_statement  */
#line 1227 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4620 "src/parser.tab.c"
    break;

  case 98: /* consider_body_statement: modifier_statement  */
#line 1228 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4626 "src/parser.tab.c"
    break;

  case 99: /* consider_body_statement: program_statement  */
#line 1229 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4632 "src/parser.tab.c"
    break;

  case 100: /* consider_body_statement: library_statement  */
#line 1230 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4638 "src/parser.tab.c"
    break;

  case 101: /* consider_body_statement: use_statement NEWLINE  */
#line 1231 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4644 "src/parser.tab.c"
    break;

  case 102: /* consider_body_statement: watch_statement  */
#line 1232 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4650 "src/parser.tab.c"
    break;

  case 103: /* consider_body_statement: unwatch_statement NEWLINE  */
#line 1233 "src/parser.y"
                                { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4656 "src/parser.tab.c"
    break;

  case 104: /* consider_body_statement: without_watchers_statement  */
#line 1234 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4662 "src/parser.tab.c"
    break;

  case 105: /* consider_body_statement: on_error_statement NEWLINE  */
#line 1235 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4668 "src/parser.tab.c"
    break;

  case 106: /* consider_body_statement: error_statement NEWLINE  */
#line 1236 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4674 "src/parser.tab.c"
    break;

  case 107: /* consider_body_statement: return_statement NEWLINE  */
#line 1237 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4680 "src/parser.tab.c"
    break;

  case 108: /* consider_body_statement: label_statement NEWLINE  */
#line 1238 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4686 "src/parser.tab.c"
    break;

  case 109: /* consider_body_statement: goto_statement NEWLINE  */
#line 1239 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4692 "src/parser.tab.c"
    break;

  case 110: /* consider_body_statement: gosub_statement NEWLINE  */
#line 1240 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4698 "src/parser.tab.c"
    break;

  case 111: /* consider_body_statement: break_statement NEWLINE  */
#line 1241 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4704 "src/parser.tab.c"
    break;

  case 112: /* consider_body_statement: continue_statement NEWLINE  */
#line 1242 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_span((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column, (yylsp[-1]).last_line, (yylsp[-1]).last_column); }
#line 4710 "src/parser.tab.c"
    break;

  case 113: /* consider_body_statement: if_statement  */
#line 1243 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 4716 "src/parser.tab.c"
    break;

  case 114: /* consider_body_statement: DIM  */
#line 1249 "src/parser.y"
          {
        (yyval.stmt) = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, (yylsp[0]).first_line, (yylsp[0]).first_column,
                            (yylsp[0]).last_line, (yylsp[0]).last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
#line 4729 "src/parser.tab.c"
    break;

  case 115: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1260 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4737 "src/parser.tab.c"
    break;

  case 116: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1263 "src/parser.y"
                                                                                                            {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4746 "src/parser.tab.c"
    break;

  case 117: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN COLON type_lock NEWLINE statement_list END FUNCTION NEWLINE  */
#line 1270 "src/parser.y"
                                                                                                                  {
        char who[160];
        snprintf(who, sizeof who, "%s(...)", (yyvsp[-10].text));
        if (!type_lock_check(ctx, who, (yyvsp[-5].type_lock).name, (yyvsp[-5].type_lock).arg, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                             (yylsp[-5]).last_line, (yylsp[-5]).last_column)) {
            YYERROR;
        }
        (yyval.stmt) = ast_function_returning((yyvsp[-10].text), (yyvsp[-8].name_list), (yyvsp[-5].type_lock), (yyvsp[-3].stmt_list));
      }
#line 4760 "src/parser.tab.c"
    break;

  case 118: /* function_statement: FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN COLON type_lock NEWLINE statement_list END FUNCTION NEWLINE  */
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
#line 4774 "src/parser.tab.c"
    break;

  case 119: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 1291 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 4782 "src/parser.tab.c"
    break;

  case 120: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 1294 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 4790 "src/parser.tab.c"
    break;

  case 121: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 1300 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4798 "src/parser.tab.c"
    break;

  case 122: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 1306 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 4806 "src/parser.tab.c"
    break;

  case 123: /* use_statement: USE IDENT  */
#line 1312 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4812 "src/parser.tab.c"
    break;

  case 124: /* use_statement: LOAD IDENT  */
#line 1313 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4818 "src/parser.tab.c"
    break;

  case 125: /* use_statement: USE STRING  */
#line 1314 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4824 "src/parser.tab.c"
    break;

  case 126: /* use_statement: LOAD STRING  */
#line 1315 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL, NULL); }
#line 4830 "src/parser.tab.c"
    break;

  case 127: /* use_statement: LOAD IDENT AS IDENT  */
#line 1316 "src/parser.y"
                          { (yyval.stmt) = ast_use((yyvsp[-2].text), NULL, (yyvsp[0].text)); }
#line 4836 "src/parser.tab.c"
    break;

  case 128: /* use_statement: USE IDENT IDENT STRING  */
#line 1317 "src/parser.y"
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
#line 4857 "src/parser.tab.c"
    break;

  case 129: /* use_statement: LOAD IDENT IDENT STRING  */
#line 1333 "src/parser.y"
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
#line 4878 "src/parser.tab.c"
    break;

  case 130: /* use_statement: LOAD IDENT IDENT STRING AS IDENT  */
#line 1349 "src/parser.y"
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
#line 4901 "src/parser.tab.c"
    break;

  case 131: /* modifier_signature: modifier_name  */
#line 1370 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 4907 "src/parser.tab.c"
    break;

  case 132: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 1371 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 4913 "src/parser.tab.c"
    break;

  case 133: /* modifier_context: IDENT  */
#line 1375 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 4919 "src/parser.tab.c"
    break;

  case 134: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1379 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4927 "src/parser.tab.c"
    break;

  case 135: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 1382 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch(NULL, (yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 4935 "src/parser.tab.c"
    break;

  case 136: /* watch_statement: WATCH IDENT LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 1390 "src/parser.y"
                                                                                           {
        (yyval.stmt) = ast_watch((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 4943 "src/parser.tab.c"
    break;

  case 137: /* unwatch_statement: UNWATCH expression  */
#line 1396 "src/parser.y"
                         { (yyval.stmt) = ast_unwatch((yyvsp[0].expr)); }
#line 4949 "src/parser.tab.c"
    break;

  case 138: /* watch_target_list: watch_target_path  */
#line 1400 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 4955 "src/parser.tab.c"
    break;

  case 139: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 1401 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 4961 "src/parser.tab.c"
    break;

  case 140: /* @2: %empty  */
#line 1421 "src/parser.y"
      { (yyval.stmt) = NULL;   /* the mid-rule carries no value; typed so bison stays quiet */
        server_head_note(ctx, (yyvsp[-5].text), (yylsp[-5]).first_line, (yylsp[-5]).first_column); }
#line 4968 "src/parser.tab.c"
    break;

  case 141: /* server_statement: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE @2 server_item_list END IDENT NEWLINE  */
#line 1423 "src/parser.y"
                                         {
        ctx->bad_block_word[0] = '\0';
        (yyval.stmt) = ast_server((yyvsp[-10].text), (yyvsp[-9].text), (yyvsp[-7].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4977 "src/parser.tab.c"
    break;

  case 142: /* @3: %empty  */
#line 1428 "src/parser.y"
      { (yyval.stmt) = NULL;   /* the mid-rule carries no value; typed so bison stays quiet */
        server_head_note(ctx, (yyvsp[-4].text), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 4984 "src/parser.tab.c"
    break;

  case 143: /* server_statement: IDENT IDENT LPAREN RPAREN NEWLINE @3 server_item_list END IDENT NEWLINE  */
#line 1430 "src/parser.y"
                                         {
        ctx->bad_block_word[0] = '\0';
        (yyval.stmt) = ast_server((yyvsp[-9].text), (yyvsp[-8].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text));
      }
#line 4993 "src/parser.tab.c"
    break;

  case 144: /* gsql_block: DATABASE_OPEN IDENT LPAREN record_field_list RPAREN NEWLINE  */
#line 1450 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_database((yyvsp[-4].text), (yyvsp[-2].record_field_list), (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 5001 "src/parser.tab.c"
    break;

  case 145: /* gsql_block: DATABASE_OPEN IDENT LPAREN RPAREN NEWLINE  */
#line 1453 "src/parser.y"
                                                {
        (yyval.stmt) = ast_database((yyvsp[-3].text), ast_record_field_list_empty(), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5009 "src/parser.tab.c"
    break;

  case 146: /* gsql_block: SQL_OPEN IDENT ON IDENT NEWLINE SQL_TEXT END IDENT NEWLINE  */
#line 1456 "src/parser.y"
                                                                 {
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_sql((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].text), (yylsp[-3]).first_line, (yylsp[-3]).first_column, (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5018 "src/parser.tab.c"
    break;

  case 147: /* window_block: WINDOW_OPEN IDENT LPAREN record_field_list RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1463 "src/parser.y"
                                                                                                   {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), NULL, NULL, NULL, (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5027 "src/parser.tab.c"
    break;

  case 148: /* window_block: WINDOW_OPEN IDENT LPAREN RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1467 "src/parser.y"
                                                                                 {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text), NULL, NULL, NULL,
                        (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5037 "src/parser.tab.c"
    break;

  case 149: /* window_block: WINDOW_OPEN IDENT LPAREN record_field_list RPAREN TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1477 "src/parser.y"
                                                                                                                  {
        ctx->window_depth--;
        (yyval.stmt) = ast_window((yyvsp[-11].text), (yyvsp[-9].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text),
                        (yylsp[-12]).first_line, (yylsp[-12]).first_column);
      }
#line 5047 "src/parser.tab.c"
    break;

  case 150: /* window_item_list: %empty  */
#line 1485 "src/parser.y"
             { (yyval.window_item_list) = ast_window_item_list_empty(); }
#line 5053 "src/parser.tab.c"
    break;

  case 151: /* window_item_list: window_item_list NEWLINE  */
#line 1486 "src/parser.y"
                               { (yyval.window_item_list) = (yyvsp[-1].window_item_list); }
#line 5059 "src/parser.tab.c"
    break;

  case 152: /* window_item_list: window_item_list window_item  */
#line 1487 "src/parser.y"
                                   { (yyval.window_item_list) = ast_window_item_list_append((yyvsp[-1].window_item_list), (yyvsp[0].window_item)); }
#line 5065 "src/parser.tab.c"
    break;

  case 153: /* window_item: IDENT LPAREN RPAREN NEWLINE  */
#line 1492 "src/parser.y"
                                  {
        (yyval.window_item) = ast_window_leaf((yyvsp[-3].text), NULL, ast_record_field_list_empty(),
                             (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 5074 "src/parser.tab.c"
    break;

  case 154: /* window_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE  */
#line 1497 "src/parser.y"
                                                          {
        (yyval.window_item) = ast_window_leaf((yyvsp[-5].text), (yyvsp[-4].text), (yyvsp[-2].record_field_list), (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 5082 "src/parser.tab.c"
    break;

  case 155: /* window_item: IDENT IDENT LPAREN RPAREN NEWLINE  */
#line 1501 "src/parser.y"
                                        {
        (yyval.window_item) = ast_window_leaf((yyvsp[-4].text), (yyvsp[-3].text), ast_record_field_list_empty(),
                             (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5091 "src/parser.tab.c"
    break;

  case 156: /* window_item: IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1506 "src/parser.y"
                                                       {
        (yyval.window_item) = ast_window_container((yyvsp[-5].text), NULL, ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                  (yylsp[-5]).first_line, (yylsp[-5]).first_column);
      }
#line 5100 "src/parser.tab.c"
    break;

  case 157: /* window_item: IDENT LPAREN record_field_list RPAREN NEWLINE window_item_list END IDENT NEWLINE  */
#line 1511 "src/parser.y"
                                                                                       {
        (yyval.window_item) = ast_window_container((yyvsp[-8].text), NULL, (yyvsp[-6].record_field_list), (yyvsp[-3].window_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5108 "src/parser.tab.c"
    break;

  case 158: /* window_item: IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1525 "src/parser.y"
                                                             {
        (yyval.window_item) = ast_window_named_form((yyvsp[-6].text), (yyvsp[-5].text), NULL, NULL, NULL, (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                   (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5117 "src/parser.tab.c"
    break;

  case 159: /* window_item: IDENT IDENT STRING NEWLINE window_item_list END IDENT NEWLINE  */
#line 1540 "src/parser.y"
                                                                    {
        (yyval.window_item) = ast_window_named_container((yyvsp[-7].text), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                        (yylsp[-7]).first_line, (yylsp[-7]).first_column);
      }
#line 5126 "src/parser.tab.c"
    break;

  case 160: /* window_item: IDENT STRING NEWLINE window_item_list END IDENT NEWLINE  */
#line 1545 "src/parser.y"
                                                              {
        (yyval.window_item) = ast_window_container((yyvsp[-6].text), (yyvsp[-5].text), ast_record_field_list_empty(), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                  (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5135 "src/parser.tab.c"
    break;

  case 161: /* window_item: IDENT TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1556 "src/parser.y"
                                                                      {
        (yyval.window_item) = ast_window_form((yyvsp[-8].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                             (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5144 "src/parser.tab.c"
    break;

  case 162: /* window_item: IDENT IDENT TO IDENT IDENT NEWLINE window_item_list END IDENT NEWLINE  */
#line 1571 "src/parser.y"
                                                                            {
        (yyval.window_item) = ast_window_named_form((yyvsp[-9].text), (yyvsp[-8].text), copy_const("to"), (yyvsp[-6].text), (yyvsp[-5].text), (yyvsp[-3].window_item_list), (yyvsp[-1].text),
                                   (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5153 "src/parser.tab.c"
    break;

  case 163: /* window_item: ON IDENT DOT IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1576 "src/parser.y"
                                                               {
        (yyval.window_item) = ast_window_handler((yyvsp[-7].text), (yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5161 "src/parser.tab.c"
    break;

  case 164: /* server_item_list: %empty  */
#line 1582 "src/parser.y"
             { (yyval.server_item_list) = ast_server_item_list_empty(); }
#line 5167 "src/parser.tab.c"
    break;

  case 165: /* server_item_list: server_item_list NEWLINE  */
#line 1583 "src/parser.y"
                               { (yyval.server_item_list) = (yyvsp[-1].server_item_list); }
#line 5173 "src/parser.tab.c"
    break;

  case 166: /* server_item_list: server_item_list server_item  */
#line 1584 "src/parser.y"
                                   { (yyval.server_item_list) = ast_server_item_list_append((yyvsp[-1].server_item_list), (yyvsp[0].server_item)); }
#line 5179 "src/parser.tab.c"
    break;

  case 167: /* server_item: IDENT server_string_list NEWLINE  */
#line 1588 "src/parser.y"
                                       {
        (yyval.server_item) = ast_server_directive((yyvsp[-2].text), (yyvsp[-1].name_list), (yylsp[-2]).first_line, (yylsp[-2]).first_column);
      }
#line 5187 "src/parser.tab.c"
    break;

  case 168: /* server_item: IDENT STRING LPAREN parameter_list_opt RPAREN NEWLINE statement_list END IDENT NEWLINE  */
#line 1591 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_handler((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5195 "src/parser.tab.c"
    break;

  case 169: /* server_item: IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1594 "src/parser.y"
                                                                                             {
        (yyval.server_item) = ast_server_site((yyvsp[-9].text), (yyvsp[-8].text), (yyvsp[-6].record_field_list), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-9]).first_line, (yylsp[-9]).first_column);
      }
#line 5203 "src/parser.tab.c"
    break;

  case 170: /* server_item: IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE  */
#line 1597 "src/parser.y"
                                                                           {
        (yyval.server_item) = ast_server_site((yyvsp[-8].text), (yyvsp[-7].text), ast_record_field_list_empty(), (yyvsp[-3].server_item_list), (yyvsp[-1].text), (yylsp[-8]).first_line, (yylsp[-8]).first_column);
      }
#line 5211 "src/parser.tab.c"
    break;

  case 171: /* server_item: ON IDENT NEWLINE statement_list END ON NEWLINE  */
#line 1600 "src/parser.y"
                                                     {
        (yyval.server_item) = ast_server_hook((yyvsp[-5].text), (yyvsp[-3].stmt_list), (yylsp[-6]).first_line, (yylsp[-6]).first_column);
      }
#line 5219 "src/parser.tab.c"
    break;

  case 172: /* server_string_list: STRING  */
#line 1606 "src/parser.y"
             { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 5225 "src/parser.tab.c"
    break;

  case 173: /* server_string_list: server_string_list COMMA STRING  */
#line 1607 "src/parser.y"
                                      { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 5231 "src/parser.tab.c"
    break;

  case 174: /* watch_target_path: variable_name  */
#line 1611 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 5237 "src/parser.tab.c"
    break;

  case 175: /* watch_target_path: watch_target_path DOT IDENT  */
#line 1612 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 5243 "src/parser.tab.c"
    break;

  case 176: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 1616 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 5251 "src/parser.tab.c"
    break;

  case 177: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 1622 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 5257 "src/parser.tab.c"
    break;

  case 178: /* on_error_statement: ON ERROR_VALUE GOTO NEXT  */
#line 1623 "src/parser.y"
                               { (yyval.stmt) = ast_on_error_goto_next(); }
#line 5263 "src/parser.tab.c"
    break;

  case 179: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 1624 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 5269 "src/parser.tab.c"
    break;

  case 180: /* on_error_statement: ON IDENT GOTO NEXT  */
#line 1625 "src/parser.y"
                         {
        if (!warn_channel_ok(ctx, (yyvsp[-2].text), (yylsp[-2]).first_line, (yylsp[-2]).first_column)) { YYERROR; }
        free((yyvsp[-2].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_NEXT);
      }
#line 5279 "src/parser.tab.c"
    break;

  case 181: /* on_error_statement: ON IDENT GOTO IDENT  */
#line 1630 "src/parser.y"
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
#line 5297 "src/parser.tab.c"
    break;

  case 182: /* on_error_statement: ON IDENT STOP  */
#line 1643 "src/parser.y"
                    {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_STOP);
      }
#line 5307 "src/parser.tab.c"
    break;

  case 183: /* on_error_statement: ON IDENT PRINT  */
#line 1648 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_on_warning(WARN_MODE_PRINT);
      }
#line 5317 "src/parser.tab.c"
    break;

  case 184: /* on_error_statement: ON IDENT IDENT  */
#line 1653 "src/parser.y"
                     {
        if (!warn_channel_ok(ctx, (yyvsp[-1].text), (yylsp[-1]).first_line, (yylsp[-1]).first_column)) { YYERROR; }
        int mode = warn_mode_word(ctx, (yyvsp[0].text), (yylsp[0]).first_line, (yylsp[0]).first_column);
        if (mode < 0) { free((yyvsp[-1].text)); free((yyvsp[0].text)); YYERROR; }
        free((yyvsp[-1].text)); free((yyvsp[0].text));
        (yyval.stmt) = ast_on_warning(mode);
      }
#line 5329 "src/parser.tab.c"
    break;

  case 185: /* error_statement: ERROR_VALUE expression  */
#line 1663 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 5335 "src/parser.tab.c"
    break;

  case 186: /* return_statement: RETURN  */
#line 1667 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 5341 "src/parser.tab.c"
    break;

  case 187: /* return_statement: RETURN expression  */
#line 1668 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 5347 "src/parser.tab.c"
    break;

  case 188: /* label_statement: variable_name COLON  */
#line 1672 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 5353 "src/parser.tab.c"
    break;

  case 189: /* goto_statement: GOTO variable_name  */
#line 1679 "src/parser.y"
                         { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 5359 "src/parser.tab.c"
    break;

  case 190: /* gosub_statement: GOSUB variable_name  */
#line 1683 "src/parser.y"
                          { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 5365 "src/parser.tab.c"
    break;

  case 191: /* break_statement: BREAK  */
#line 1692 "src/parser.y"
            { (yyval.stmt) = ast_break(NULL); }
#line 5371 "src/parser.tab.c"
    break;

  case 192: /* break_statement: BREAK IDENT  */
#line 1693 "src/parser.y"
                  { (yyval.stmt) = ast_break((yyvsp[0].text)); }
#line 5377 "src/parser.tab.c"
    break;

  case 193: /* continue_statement: CONTINUE  */
#line 1697 "src/parser.y"
               { (yyval.stmt) = ast_continue(NULL); }
#line 5383 "src/parser.tab.c"
    break;

  case 194: /* continue_statement: CONTINUE IDENT  */
#line 1698 "src/parser.y"
                     { (yyval.stmt) = ast_continue((yyvsp[0].text)); }
#line 5389 "src/parser.tab.c"
    break;

  case 195: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1702 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 5398 "src/parser.tab.c"
    break;

  case 196: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1706 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 5407 "src/parser.tab.c"
    break;

  case 197: /* if_block_tail: END IF NEWLINE  */
#line 1713 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 5415 "src/parser.tab.c"
    break;

  case 198: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 1716 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 5423 "src/parser.tab.c"
    break;

  case 199: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1719 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 5431 "src/parser.tab.c"
    break;

  case 200: /* if_block_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1728 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5443 "src/parser.tab.c"
    break;

  case 201: /* if_inline_tail: %empty  */
#line 1738 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 5451 "src/parser.tab.c"
    break;

  case 202: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 1741 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 5459 "src/parser.tab.c"
    break;

  case 203: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 1744 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 5467 "src/parser.tab.c"
    break;

  case 204: /* if_inline_tail: ELSE IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 1750 "src/parser.y"
                                                                      {
        AstStmt *inner = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5479 "src/parser.tab.c"
    break;

  case 205: /* if_inline_tail: ELSE IF expression THEN NEWLINE statement_list if_block_tail  */
#line 1757 "src/parser.y"
                                                                   {
        AstStmt *inner = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        inner->as.if_stmt.else_body = (yyvsp[0].stmt_list);
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, (yylsp[-5]).first_line, (yylsp[-5]).first_column,
                                      (yylsp[-5]).last_line, (yylsp[-5]).last_column));
      }
#line 5491 "src/parser.tab.c"
    break;

  case 206: /* inline_statement: assignment  */
#line 1767 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5497 "src/parser.tab.c"
    break;

  case 207: /* inline_statement: print_statement  */
#line 1768 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5503 "src/parser.tab.c"
    break;

  case 208: /* inline_statement: call_statement  */
#line 1769 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5509 "src/parser.tab.c"
    break;

  case 209: /* inline_statement: use_statement  */
#line 1770 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5515 "src/parser.tab.c"
    break;

  case 210: /* inline_statement: on_error_statement  */
#line 1771 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5521 "src/parser.tab.c"
    break;

  case 211: /* inline_statement: error_statement  */
#line 1772 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5527 "src/parser.tab.c"
    break;

  case 212: /* inline_statement: return_statement  */
#line 1773 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5533 "src/parser.tab.c"
    break;

  case 213: /* inline_statement: goto_statement  */
#line 1774 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5539 "src/parser.tab.c"
    break;

  case 214: /* inline_statement: gosub_statement  */
#line 1775 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5545 "src/parser.tab.c"
    break;

  case 215: /* inline_statement: break_statement  */
#line 1776 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5551 "src/parser.tab.c"
    break;

  case 216: /* inline_statement: continue_statement  */
#line 1777 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_span((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column, (yylsp[0]).last_line, (yylsp[0]).last_column); }
#line 5557 "src/parser.tab.c"
    break;

  case 217: /* expression: or_expression  */
#line 1781 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 5563 "src/parser.tab.c"
    break;

  case 218: /* or_expression: and_expression  */
#line 1785 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5569 "src/parser.tab.c"
    break;

  case 219: /* or_expression: or_expression OR and_expression  */
#line 1786 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5575 "src/parser.tab.c"
    break;

  case 220: /* and_expression: not_expression  */
#line 1790 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5581 "src/parser.tab.c"
    break;

  case 221: /* and_expression: and_expression AND not_expression  */
#line 1791 "src/parser.y"
                                        { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5587 "src/parser.tab.c"
    break;

  case 222: /* not_expression: comparison_expression  */
#line 1813 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 5593 "src/parser.tab.c"
    break;

  case 223: /* not_expression: NOT not_expression  */
#line 1814 "src/parser.y"
                         { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5599 "src/parser.tab.c"
    break;

  case 224: /* comparison_expression: set_expression  */
#line 1818 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5605 "src/parser.tab.c"
    break;

  case 225: /* comparison_expression: set_expression comparison_operator set_expression  */
#line 1819 "src/parser.y"
                                                        { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5611 "src/parser.tab.c"
    break;

  case 226: /* comparison_expression: set_expression comparison_lens comparison_operator set_expression  */
#line 1820 "src/parser.y"
                                                                        {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 5619 "src/parser.tab.c"
    break;

  case 227: /* set_expression: additive_expression  */
#line 1826 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 5625 "src/parser.tab.c"
    break;

  case 228: /* set_expression: set_expression EXCLUDING additive_expression  */
#line 1827 "src/parser.y"
                                                   { (yyval.expr) = expr_at(ast_binary(copy_const("excluding"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5631 "src/parser.tab.c"
    break;

  case 229: /* set_expression: set_expression INTERSECTING additive_expression  */
#line 1828 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("intersecting"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5637 "src/parser.tab.c"
    break;

  case 230: /* additive_expression: multiplicative_expression  */
#line 1832 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 5643 "src/parser.tab.c"
    break;

  case 231: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 1833 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5649 "src/parser.tab.c"
    break;

  case 232: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 1834 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5655 "src/parser.tab.c"
    break;

  case 233: /* multiplicative_expression: unary_expression  */
#line 1838 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 5661 "src/parser.tab.c"
    break;

  case 234: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 1839 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5667 "src/parser.tab.c"
    break;

  case 235: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 1840 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5673 "src/parser.tab.c"
    break;

  case 236: /* unary_expression: postfix_expression  */
#line 1844 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 5679 "src/parser.tab.c"
    break;

  case 237: /* unary_expression: MINUS unary_expression  */
#line 1845 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5685 "src/parser.tab.c"
    break;

  case 238: /* unary_expression: MODIFIER_PREFIX unary_expression  */
#line 1852 "src/parser.y"
                                       {
        (yyval.expr) = expr_at(ast_modifier_apply(parse_modifier_use((yyvsp[-1].text)), (yyvsp[0].expr)),
                     (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 5694 "src/parser.tab.c"
    break;

  case 239: /* unary_expression: NEW postfix_expression  */
#line 1856 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5700 "src/parser.tab.c"
    break;

  case 240: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 1857 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 5706 "src/parser.tab.c"
    break;

  case 241: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 1858 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5712 "src/parser.tab.c"
    break;

  case 242: /* postfix_expression: primary  */
#line 1862 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 5718 "src/parser.tab.c"
    break;

  case 243: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 1863 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5724 "src/parser.tab.c"
    break;

  case 244: /* postfix_expression: postfix_expression DOT dot_field_name  */
#line 1864 "src/parser.y"
                                            { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 5730 "src/parser.tab.c"
    break;

  case 245: /* postfix_expression: postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 1865 "src/parser.y"
                                                                   {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        (yyval.expr) = expr_at(ast_method_call((yyvsp[-5].expr), (yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column);
      }
#line 5740 "src/parser.tab.c"
    break;

  case 246: /* postfix_expression: postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1870 "src/parser.y"
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
#line 5756 "src/parser.tab.c"
    break;

  case 247: /* comparison_operator: OP_EQ  */
#line 1884 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 5762 "src/parser.tab.c"
    break;

  case 248: /* comparison_operator: OP_NE  */
#line 1885 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 5768 "src/parser.tab.c"
    break;

  case 249: /* comparison_operator: OP_GT  */
#line 1886 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 5774 "src/parser.tab.c"
    break;

  case 250: /* comparison_operator: OP_LT  */
#line 1887 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 5780 "src/parser.tab.c"
    break;

  case 251: /* comparison_operator: OP_GE  */
#line 1888 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 5786 "src/parser.tab.c"
    break;

  case 252: /* comparison_operator: OP_LE  */
#line 1889 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 5792 "src/parser.tab.c"
    break;

  case 253: /* comparison_operator: OP_NGT  */
#line 1890 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 5798 "src/parser.tab.c"
    break;

  case 254: /* comparison_operator: OP_NLT  */
#line 1891 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 5804 "src/parser.tab.c"
    break;

  case 255: /* comparison_operator: OP_NGE  */
#line 1892 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 5810 "src/parser.tab.c"
    break;

  case 256: /* comparison_operator: OP_NLE  */
#line 1893 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 5816 "src/parser.tab.c"
    break;

  case 257: /* primary: MODIFIER_VALUE  */
#line 1897 "src/parser.y"
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
#line 5839 "src/parser.tab.c"
    break;

  case 258: /* primary: NUMBER  */
#line 1915 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5845 "src/parser.tab.c"
    break;

  case 259: /* primary: WATCHERS LPAREN RPAREN  */
#line 1916 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_call(copy_const("watchers"), ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5851 "src/parser.tab.c"
    break;

  case 260: /* primary: duration_terms  */
#line 1917 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5857 "src/parser.tab.c"
    break;

  case 261: /* primary: STRING  */
#line 1918 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5863 "src/parser.tab.c"
    break;

  case 262: /* primary: variable_name ident_suffix  */
#line 1919 "src/parser.y"
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
#line 5885 "src/parser.tab.c"
    break;

  case 263: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1936 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 5896 "src/parser.tab.c"
    break;

  case 264: /* primary: ERROR_VALUE  */
#line 1942 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5902 "src/parser.tab.c"
    break;

  case 265: /* primary: TRUE  */
#line 1943 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5908 "src/parser.tab.c"
    break;

  case 266: /* primary: FALSE  */
#line 1944 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5914 "src/parser.tab.c"
    break;

  case 267: /* primary: NOTHING  */
#line 1945 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5920 "src/parser.tab.c"
    break;

  case 268: /* primary: UNKNOWN_VALUE  */
#line 1946 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 5926 "src/parser.tab.c"
    break;

  case 269: /* primary: LPAREN expression RPAREN  */
#line 1947 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 5932 "src/parser.tab.c"
    break;

  case 270: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 1948 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5938 "src/parser.tab.c"
    break;

  case 271: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 1949 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5944 "src/parser.tab.c"
    break;

  case 272: /* primary: record_literal  */
#line 1950 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 5950 "src/parser.tab.c"
    break;

  case 273: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 1954 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 5956 "src/parser.tab.c"
    break;

  case 274: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 1955 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 5962 "src/parser.tab.c"
    break;

  case 275: /* ident_suffix: %empty  */
#line 1959 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 5972 "src/parser.tab.c"
    break;

  case 276: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 1964 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 5982 "src/parser.tab.c"
    break;

  case 277: /* ident_suffix: DOT dot_field_name ident_dot_suffix  */
#line 1969 "src/parser.y"
                                          {
        /* dot_field_name, not IDENT: a keyword is a legal FIELD name after a
         * dot, because nothing but a name can appear there. */
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 5993 "src/parser.tab.c"
    break;

  case 278: /* ident_suffix: DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 1975 "src/parser.y"
                                                          {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        (yyval.ident_suffix).kind = IDENT_SUFFIX_METHOD;
        (yyval.ident_suffix).name = (yyvsp[-3].text);
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 6007 "src/parser.tab.c"
    break;

  case 279: /* ident_dot_suffix: %empty  */
#line 1987 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 6017 "src/parser.tab.c"
    break;

  case 280: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 1992 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 6027 "src/parser.tab.c"
    break;

  case 281: /* duration_terms: NUMBER IDENT  */
#line 2000 "src/parser.y"
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
#line 6042 "src/parser.tab.c"
    break;

  case 282: /* duration_terms: duration_terms NUMBER IDENT  */
#line 2010 "src/parser.y"
                                  {
        char *bad = NULL;
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].numlit).value, (yyvsp[0].text), &bad);
        if (bad) {
            duration_unit_error(ctx, bad, (yylsp[0]).first_line, (yylsp[0]).first_column,
                                (yylsp[0]).last_line, (yylsp[0]).last_column);
            YYERROR;
        }
      }
#line 6056 "src/parser.tab.c"
    break;

  case 283: /* argument_list_opt: %empty  */
#line 2022 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 6062 "src/parser.tab.c"
    break;

  case 284: /* argument_list_opt: argument_list  */
#line 2023 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 6068 "src/parser.tab.c"
    break;

  case 285: /* argument_list: expression  */
#line 2027 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 6074 "src/parser.tab.c"
    break;

  case 286: /* argument_list: argument_list COMMA expression  */
#line 2028 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 6080 "src/parser.tab.c"
    break;

  case 287: /* array_argument_list: expression  */
#line 2032 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 6086 "src/parser.tab.c"
    break;

  case 288: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 2033 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 6092 "src/parser.tab.c"
    break;

  case 289: /* parameter_list_opt: %empty  */
#line 2037 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 6098 "src/parser.tab.c"
    break;

  case 290: /* parameter_list_opt: parameter_list  */
#line 2038 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 6104 "src/parser.tab.c"
    break;

  case 291: /* parameter_default: NUMBER  */
#line 2052 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6110 "src/parser.tab.c"
    break;

  case 292: /* parameter_default: MINUS NUMBER  */
#line 2056 "src/parser.y"
                   { (yyval.expr) = expr_at(ast_number(-(yyvsp[0].numlit).value,
                                             (yyvsp[0].numlit).exact == LLONG_MIN ? 0 : -(yyvsp[0].numlit).exact,
                                             (yyvsp[0].numlit).exact == LLONG_MIN ? 0 : (yyvsp[0].numlit).is_exact),
                                  (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6119 "src/parser.tab.c"
    break;

  case 293: /* parameter_default: PLUS NUMBER  */
#line 2060 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_number((yyvsp[0].numlit).value, (yyvsp[0].numlit).exact, (yyvsp[0].numlit).is_exact), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 6125 "src/parser.tab.c"
    break;

  case 294: /* parameter_default: STRING  */
#line 2061 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6131 "src/parser.tab.c"
    break;

  case 295: /* parameter_default: TRUE  */
#line 2062 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6137 "src/parser.tab.c"
    break;

  case 296: /* parameter_default: FALSE  */
#line 2063 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6143 "src/parser.tab.c"
    break;

  case 297: /* parameter_default: NOTHING  */
#line 2064 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6149 "src/parser.tab.c"
    break;

  case 298: /* parameter_default: UNKNOWN_VALUE  */
#line 2065 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 6155 "src/parser.tab.c"
    break;

  case 299: /* parameter_list: IDENT  */
#line 2069 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 6161 "src/parser.tab.c"
    break;

  case 300: /* parameter_list: IDENT OP_EQ parameter_default  */
#line 2070 "src/parser.y"
                                    {
        (yyval.name_list) = ast_name_list_append_default(ast_name_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 6169 "src/parser.tab.c"
    break;

  case 301: /* parameter_list: parameter_list COMMA IDENT  */
#line 2073 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 6175 "src/parser.tab.c"
    break;

  case 302: /* parameter_list: IDENT COLON type_lock  */
#line 2077 "src/parser.y"
                            {
        if (!type_lock_check(ctx, (yyvsp[-2].text), (yyvsp[0].type_lock).name, (yyvsp[0].type_lock).arg, (yylsp[0]).first_line, (yylsp[0]).first_column,
                             (yylsp[0]).last_line, (yylsp[0]).last_column)) {
            YYERROR;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append(ast_name_list_empty(), (yyvsp[-2].text)), (yyvsp[0].type_lock));
      }
#line 6187 "src/parser.tab.c"
    break;

  case 303: /* parameter_list: IDENT COLON type_lock OP_EQ parameter_default  */
#line 2084 "src/parser.y"
                                                    {
        if (!type_lock_check(ctx, (yyvsp[-4].text), (yyvsp[-2].type_lock).name, (yyvsp[-2].type_lock).arg, (yylsp[-2]).first_line, (yylsp[-2]).first_column,
                             (yylsp[-2]).last_line, (yylsp[-2]).last_column)) {
            YYERROR;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append_default(ast_name_list_empty(), (yyvsp[-4].text), (yyvsp[0].expr)), (yyvsp[-2].type_lock));
      }
#line 6199 "src/parser.tab.c"
    break;

  case 304: /* parameter_list: parameter_list COMMA IDENT COLON type_lock  */
#line 2091 "src/parser.y"
                                                 {
        if (!type_lock_check(ctx, (yyvsp[-2].text), (yyvsp[0].type_lock).name, (yyvsp[0].type_lock).arg, (yylsp[0]).first_line, (yylsp[0]).first_column,
                             (yylsp[0]).last_line, (yylsp[0]).last_column)) {
            YYERROR;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append((yyvsp[-4].name_list), (yyvsp[-2].text)), (yyvsp[0].type_lock));
      }
#line 6211 "src/parser.tab.c"
    break;

  case 305: /* parameter_list: parameter_list COMMA IDENT COLON type_lock OP_EQ parameter_default  */
#line 2098 "src/parser.y"
                                                                         {
        if (!type_lock_check(ctx, (yyvsp[-4].text), (yyvsp[-2].type_lock).name, (yyvsp[-2].type_lock).arg, (yylsp[-2]).first_line, (yylsp[-2]).first_column,
                             (yylsp[-2]).last_line, (yylsp[-2]).last_column)) {
            YYERROR;
        }
        (yyval.name_list) = ast_name_list_lock_last(ast_name_list_append_default((yyvsp[-6].name_list), (yyvsp[-4].text), (yyvsp[0].expr)), (yyvsp[-2].type_lock));
      }
#line 6223 "src/parser.tab.c"
    break;

  case 306: /* parameter_list: parameter_list COMMA IDENT OP_EQ parameter_default  */
#line 2105 "src/parser.y"
                                                         {
        (yyval.name_list) = ast_name_list_append_default((yyvsp[-4].name_list), (yyvsp[-2].text), (yyvsp[0].expr));
      }
#line 6231 "src/parser.tab.c"
    break;

  case 307: /* field_name: dot_field_name  */
#line 2120 "src/parser.y"
                     { (yyval.text) = (yyvsp[0].text); }
#line 6237 "src/parser.tab.c"
    break;

  case 308: /* field_name: STRING  */
#line 2127 "src/parser.y"
             { (yyval.text) = (yyvsp[0].text); }
#line 6243 "src/parser.tab.c"
    break;

  case 309: /* dot_field_name: IDENT  */
#line 2136 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 6249 "src/parser.tab.c"
    break;

  case 310: /* dot_field_name: AS  */
#line 2137 "src/parser.y"
                     { (yyval.text) = kw_name("as"); }
#line 6255 "src/parser.tab.c"
    break;

  case 311: /* dot_field_name: NEXT  */
#line 2138 "src/parser.y"
                     { (yyval.text) = kw_name("next"); }
#line 6261 "src/parser.tab.c"
    break;

  case 312: /* dot_field_name: STOP  */
#line 2139 "src/parser.y"
                     { (yyval.text) = kw_name("stop"); }
#line 6267 "src/parser.tab.c"
    break;

  case 313: /* dot_field_name: ERROR_VALUE  */
#line 2140 "src/parser.y"
                     { (yyval.text) = kw_name("error"); }
#line 6273 "src/parser.tab.c"
    break;

  case 314: /* dot_field_name: END  */
#line 2141 "src/parser.y"
                     { (yyval.text) = kw_name("end"); }
#line 6279 "src/parser.tab.c"
    break;

  case 315: /* dot_field_name: TO  */
#line 2142 "src/parser.y"
                     { (yyval.text) = kw_name("to"); }
#line 6285 "src/parser.tab.c"
    break;

  case 316: /* dot_field_name: IN  */
#line 2143 "src/parser.y"
                     { (yyval.text) = kw_name("in"); }
#line 6291 "src/parser.tab.c"
    break;

  case 317: /* dot_field_name: ON  */
#line 2144 "src/parser.y"
                     { (yyval.text) = kw_name("on"); }
#line 6297 "src/parser.tab.c"
    break;

  case 318: /* dot_field_name: NEW  */
#line 2145 "src/parser.y"
                     { (yyval.text) = kw_name("new"); }
#line 6303 "src/parser.tab.c"
    break;

  case 319: /* dot_field_name: EACH  */
#line 2146 "src/parser.y"
                     { (yyval.text) = kw_name("each"); }
#line 6309 "src/parser.tab.c"
    break;

  case 320: /* dot_field_name: WITH  */
#line 2147 "src/parser.y"
                     { (yyval.text) = kw_name("with"); }
#line 6315 "src/parser.tab.c"
    break;

  case 321: /* dot_field_name: WITHOUT  */
#line 2148 "src/parser.y"
                     { (yyval.text) = kw_name("without"); }
#line 6321 "src/parser.tab.c"
    break;

  case 322: /* dot_field_name: THEN  */
#line 2149 "src/parser.y"
                     { (yyval.text) = kw_name("then"); }
#line 6327 "src/parser.tab.c"
    break;

  case 323: /* dot_field_name: ELSE  */
#line 2150 "src/parser.y"
                     { (yyval.text) = kw_name("else"); }
#line 6333 "src/parser.tab.c"
    break;

  case 324: /* dot_field_name: FOR  */
#line 2151 "src/parser.y"
                     { (yyval.text) = kw_name("for"); }
#line 6339 "src/parser.tab.c"
    break;

  case 325: /* dot_field_name: IF  */
#line 2152 "src/parser.y"
                     { (yyval.text) = kw_name("if"); }
#line 6345 "src/parser.tab.c"
    break;

  case 326: /* dot_field_name: WHILE  */
#line 2153 "src/parser.y"
                     { (yyval.text) = kw_name("while"); }
#line 6351 "src/parser.tab.c"
    break;

  case 327: /* dot_field_name: DO  */
#line 2154 "src/parser.y"
                     { (yyval.text) = kw_name("do"); }
#line 6357 "src/parser.tab.c"
    break;

  case 328: /* dot_field_name: UNTIL  */
#line 2155 "src/parser.y"
                     { (yyval.text) = kw_name("until"); }
#line 6363 "src/parser.tab.c"
    break;

  case 329: /* dot_field_name: PRINT  */
#line 2156 "src/parser.y"
                     { (yyval.text) = kw_name("print"); }
#line 6369 "src/parser.tab.c"
    break;

  case 330: /* dot_field_name: RETURN  */
#line 2157 "src/parser.y"
                     { (yyval.text) = kw_name("return"); }
#line 6375 "src/parser.tab.c"
    break;

  case 331: /* dot_field_name: LOAD  */
#line 2158 "src/parser.y"
                     { (yyval.text) = kw_name("load"); }
#line 6381 "src/parser.tab.c"
    break;

  case 332: /* dot_field_name: USE  */
#line 2159 "src/parser.y"
                     { (yyval.text) = kw_name("use"); }
#line 6387 "src/parser.tab.c"
    break;

  case 333: /* dot_field_name: NOT  */
#line 2160 "src/parser.y"
                     { (yyval.text) = kw_name("not"); }
#line 6393 "src/parser.tab.c"
    break;

  case 334: /* dot_field_name: AND  */
#line 2161 "src/parser.y"
                     { (yyval.text) = kw_name("and"); }
#line 6399 "src/parser.tab.c"
    break;

  case 335: /* dot_field_name: OR  */
#line 2162 "src/parser.y"
                     { (yyval.text) = kw_name("or"); }
#line 6405 "src/parser.tab.c"
    break;

  case 336: /* dot_field_name: TRUE  */
#line 2163 "src/parser.y"
                     { (yyval.text) = kw_name("true"); }
#line 6411 "src/parser.tab.c"
    break;

  case 337: /* dot_field_name: FALSE  */
#line 2164 "src/parser.y"
                     { (yyval.text) = kw_name("false"); }
#line 6417 "src/parser.tab.c"
    break;

  case 338: /* dot_field_name: NOTHING  */
#line 2165 "src/parser.y"
                     { (yyval.text) = kw_name("nothing"); }
#line 6423 "src/parser.tab.c"
    break;

  case 339: /* dot_field_name: BREAK  */
#line 2166 "src/parser.y"
                     { (yyval.text) = kw_name("break"); }
#line 6429 "src/parser.tab.c"
    break;

  case 340: /* dot_field_name: CONTINUE  */
#line 2167 "src/parser.y"
                     { (yyval.text) = kw_name("continue"); }
#line 6435 "src/parser.tab.c"
    break;

  case 341: /* dot_field_name: GOTO  */
#line 2168 "src/parser.y"
                     { (yyval.text) = kw_name("goto"); }
#line 6441 "src/parser.tab.c"
    break;

  case 342: /* dot_field_name: GOSUB  */
#line 2169 "src/parser.y"
                     { (yyval.text) = kw_name("gosub"); }
#line 6447 "src/parser.tab.c"
    break;

  case 343: /* dot_field_name: SPAWN  */
#line 2170 "src/parser.y"
                     { (yyval.text) = kw_name("spawn"); }
#line 6453 "src/parser.tab.c"
    break;

  case 344: /* dot_field_name: EXPORT  */
#line 2171 "src/parser.y"
                     { (yyval.text) = kw_name("export"); }
#line 6459 "src/parser.tab.c"
    break;

  case 345: /* dot_field_name: LIBRARY  */
#line 2172 "src/parser.y"
                     { (yyval.text) = kw_name("library"); }
#line 6465 "src/parser.tab.c"
    break;

  case 346: /* dot_field_name: FUNCTION  */
#line 2173 "src/parser.y"
                     { (yyval.text) = kw_name("function"); }
#line 6471 "src/parser.tab.c"
    break;

  case 347: /* dot_field_name: MODIFIER  */
#line 2174 "src/parser.y"
                     { (yyval.text) = kw_name("modifier"); }
#line 6477 "src/parser.tab.c"
    break;

  case 348: /* dot_field_name: PROGRAM  */
#line 2175 "src/parser.y"
                     { (yyval.text) = kw_name("program"); }
#line 6483 "src/parser.tab.c"
    break;

  case 349: /* dot_field_name: WATCH  */
#line 2176 "src/parser.y"
                     { (yyval.text) = kw_name("watch"); }
#line 6489 "src/parser.tab.c"
    break;

  case 350: /* dot_field_name: WATCHERS  */
#line 2177 "src/parser.y"
                     { (yyval.text) = kw_name("watchers"); }
#line 6495 "src/parser.tab.c"
    break;

  case 351: /* dot_field_name: CONSIDER  */
#line 2178 "src/parser.y"
                     { (yyval.text) = kw_name("consider"); }
#line 6501 "src/parser.tab.c"
    break;

  case 352: /* dot_field_name: STEP  */
#line 2179 "src/parser.y"
                     { (yyval.text) = kw_name("step"); }
#line 6507 "src/parser.tab.c"
    break;

  case 353: /* dot_field_name: UNWATCH  */
#line 2180 "src/parser.y"
                     { (yyval.text) = kw_name("unwatch"); }
#line 6513 "src/parser.tab.c"
    break;

  case 354: /* dot_field_name: UNKNOWN_VALUE  */
#line 2181 "src/parser.y"
                     { (yyval.text) = kw_name("unknown"); }
#line 6519 "src/parser.tab.c"
    break;

  case 355: /* dot_field_name: DIM  */
#line 2182 "src/parser.y"
                     { (yyval.text) = kw_name("dim"); }
#line 6525 "src/parser.tab.c"
    break;

  case 356: /* record_field_list: field_name OP_EQ expression  */
#line 2186 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6531 "src/parser.tab.c"
    break;

  case 357: /* record_field_list: field_name COLON expression  */
#line 2187 "src/parser.y"
                                  { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6537 "src/parser.tab.c"
    break;

  case 358: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 2188 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 6543 "src/parser.tab.c"
    break;

  case 359: /* record_field_list: record_field_list COMMA optional_newlines field_name OP_EQ expression  */
#line 2189 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6549 "src/parser.tab.c"
    break;

  case 360: /* record_field_list: record_field_list COMMA optional_newlines field_name COLON expression  */
#line 2190 "src/parser.y"
                                                                            { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 6555 "src/parser.tab.c"
    break;

  case 361: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 2191 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 6561 "src/parser.tab.c"
    break;

  case 362: /* field_policy: IDENT  */
#line 2199 "src/parser.y"
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
#line 6593 "src/parser.tab.c"
    break;

  case 363: /* field_policy: IDENT expression  */
#line 2226 "src/parser.y"
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
#line 6614 "src/parser.tab.c"
    break;


#line 6618 "src/parser.tab.c"

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

#line 2249 "src/parser.y"


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
