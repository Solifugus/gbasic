#ifndef GBASIC_DIAGNOSTICS_H
#define GBASIC_DIAGNOSTICS_H

#include <stddef.h>
#include <stdio.h>

/* Structured diagnostics for the gBASIC front end (PLAN.md Phase 1).
 *
 * The sink is an append-only, growable list of diagnostics. Producers (lexer,
 * parser, and — for reporting only — the evaluator) push structured records
 * instead of writing straight to stderr; consumers (today: the CLI) pull the
 * records and decide how to present them. This keeps the *display* format a
 * consumer concern, so the CLI can reproduce today's byte-exact stderr while an
 * embedder gets structure instead. */

typedef enum {
    GB_SEVERITY_ERROR   = 0,
    GB_SEVERITY_WARNING = 1,
    GB_SEVERITY_NOTE    = 2
} gb_severity;

/* Stable, machine-readable diagnostic codes. Numeric values are FROZEN: append
 * only, never renumber or reuse (an embedder may switch on them). The mnemonic
 * string is gb_diag_code_str().
 *
 * The human "kind" word the CLI prints ("lexer error" / "parse error" /
 * "runtime error") is deliberately NOT stored here — it is a presentation
 * mapping owned by the consumer. Note one preserved legacy quirk: a
 * message-bearing lexer error currently prints as "runtime error", not "lexer
 * error" (verified against the current binary). GB_DIAG_LEX_DETAIL exists so the
 * CLI can reproduce that exactly; a future cleanup could re-map it without
 * changing this code's identity. */
typedef enum {
    GB_DIAG_NONE           = 0,
    GB_DIAG_LEX_ERROR      = 1000,  /* message-less lexer error; CLI kind "lexer error"   */
    GB_DIAG_LEX_DETAIL     = 1001,  /* message-bearing lexer error (unterminated string,
                                       bad escape); CLI kind "runtime error" (legacy quirk) */
    GB_DIAG_PARSE_ERROR    = 2000,  /* grammar/yyerror; CLI kind "parse error"            */
    GB_DIAG_STRING_LITERAL = 2001,  /* parser string-literal / \u{} decode; CLI "runtime error" */
    GB_DIAG_RUNTIME_ERROR  = 3000   /* evaluator; CLI kind "runtime error"                */
} gb_diag_code;

/* 1-based, inclusive-start / exclusive-end source span. A point diagnostic at
 * column C on line L over a token of length N has start (L,C) and end (L,C+N),
 * matching the parser's existing yylloc last_column convention. Single-token
 * errors stay on one line; multi-line tokens carry the real end line/column.
 *
 * COLUMN/LINE UNITS — verified against the current lexer (src/lexer.c advance()):
 * both line and column are counted in *bytes*, 1-based, and the lexer has NO
 * Unicode awareness. advance() adds 1 to `column` for every byte that is not
 * '\n'; '\n' resets column to 1 and increments line. Consequences:
 *   - A literal multi-byte UTF-8 character in the source advances the column by
 *     its BYTE count, not by 1 — so columns are byte offsets, NOT Unicode
 *     codepoints and NOT UTF-16 code units.
 *   - A tab advances the column by exactly 1 (no tab-stop expansion).
 *   - Only '\n' starts a new line. A lone '\r' (and the CR of a CRLF pair) is
 *     ordinary whitespace that advances the column; it does not bump the line.
 *   - end_column = start_column + token byte-length (parser yylloc convention).
 * This is a single, consistent mechanism — there is no ambiguity to convert
 * around. An LSP server (LSP positions default to UTF-16 code units) must
 * therefore transcode using the actual source bytes of the line: read the line's
 * bytes up to the given byte column and re-measure in the client's unit. These
 * are the current semantics stated precisely; changing them (e.g. to codepoint
 * columns) would be a separate, deliberate change. */
typedef struct {
    int start_line;
    int start_column;
    int end_line;
    int end_column;
} gb_span;

typedef struct {
    gb_severity  severity;
    gb_diag_code code;
    int          subcode;   /* language-level code (evaluator RuntimeError.code); 0 if N/A */
    char        *path;      /* owned copy; NULL when the source path is unknown */
    gb_span      span;
    char        *message;   /* owned copy */
} gb_diag;

/* Zero-initialization ({0}) is a valid empty sink; gb_diagnostics_init() is the
 * explicit form. */
typedef struct {
    gb_diag *items;
    size_t   count;
    size_t   capacity;
} gb_diagnostics;

void           gb_diagnostics_init(gb_diagnostics *diags);
void           gb_diagnostics_free(gb_diagnostics *diags);

/* Append one diagnostic. `message` and `path` are copied (path may be NULL), so
 * the caller keeps ownership of the arguments. */
void           gb_diagnostics_add(gb_diagnostics *diags,
                                  gb_severity severity,
                                  gb_diag_code code,
                                  int subcode,
                                  const char *path,
                                  gb_span span,
                                  const char *message);

size_t         gb_diagnostics_count(const gb_diagnostics *diags);
const gb_diag *gb_diagnostics_at(const gb_diagnostics *diags, size_t index);

const char    *gb_diag_code_str(gb_diag_code code);
const char    *gb_severity_str(gb_severity severity);

/* ---- Reporting (Phase 1 wiring) ---------------------------------------------
 *
 * Producers (lexer/parser via parser.y, evaluator via eval.c) call gb_report.
 * If an "active sink" is set, the diagnostic is appended to it (deferred, to be
 * drained by the consumer); if not, it is emitted immediately to stderr in the
 * legacy one-line format. The active sink is a single process-global for now —
 * Phase 2/3 move it into per-parse / per-interpreter context. Both the deferred
 * drain and the immediate fallback go through gb_diag_format, so their bytes are
 * identical. gb_diag_kind_str maps a machine code to the human "kind" word the
 * CLI prints ("lexer error" / "parse error" / "runtime error"). */

void            gb_set_active_sink(gb_diagnostics *sink);
gb_diagnostics *gb_get_active_sink(void);

void            gb_report(gb_diag_code code, int subcode, const char *path,
                          gb_span span, const char *message);

/* Same as gb_report, but to an explicit sink instead of the process-global one
 * (NULL sink => immediate stderr, byte-identical to gb_report's fallback). The
 * reentrant parser passes its per-parse ctx->diags here so concurrent parses do
 * not share the global sink. gb_report(...) == gb_report_to(<global sink>, ...). */
void            gb_report_to(gb_diagnostics *sink, gb_diag_code code, int subcode,
                             const char *path, gb_span span, const char *message);

/* Emit one diagnostic to `out` as "<kind> at <path>:<line>:<col>: <msg>\n"
 * (path segment omitted when path is NULL/empty), using span.start_* — the exact
 * legacy CLI format. */
void            gb_diag_format(FILE *out, const gb_diag *diag);
const char     *gb_diag_kind_str(gb_diag_code code);

/* Emit one diagnostic as a single compact JSON object followed by '\n' — the
 * `gbasic --json-diagnostics` format. Positions are gBASIC-native: 1-based BYTE
 * line/column (see gb_span). Hand-rolled (no cJSON), so the main `gbasic` binary
 * gains no JSON dependency. Schema (stable field order):
 *   {"severity","code","subcode","path","start":{"line","column"},
 *    "end":{"line","column"},"message"}
 * `path` is JSON null when unknown; message/path strings are JSON-escaped with
 * multi-byte UTF-8 passed through as raw bytes. */
void            gb_diag_write_json(FILE *out, const gb_diag *diag);

#endif
